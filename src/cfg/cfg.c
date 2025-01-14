/*
 * ============================================================================
 *  Title:    Configuration Manager
 *  Author:   J. Zbiciak
 * ============================================================================
 *  This module manages the machine configuration.  It does commandline
 *  parsing and processes the configuration elements that were read in 
 *  via the config-file parser.
 *
 *  CFG owns the entire machine -- it is encapsulated in a cfg_t.
 * ============================================================================
 *  CFG_INIT     -- Parse command line and get started
 *  CFG_FILE     -- Parse a config file and extend the state of the machine.
 * ============================================================================
 */


#include "config.h"
#include "lzoe/lzoe.h"
#include "file/file.h"
#include "periph/periph.h"
#include "cp1600/cp1600.h"
#include "cp1600/emu_link.h"
#include "mem/mem.h"
#include "icart/icart.h"
#include "bincfg/bincfg.h"
#include "bincfg/legacy.h"
#include "pads/pads.h"
#include "pads/pads_cgc.h"
#include "pads/pads_intv2pc.h"
#include "gfx/gfx.h"
#include "snd/snd.h"
#include "ay8910/ay8910.h"
#include "demo/demo.h"
#include "stic/stic.h"
#include "ivoice/ivoice.h"
#include "speed/speed.h"
#include "debug/debug_.h"
#include "event/event.h"
#include "joy/joy.h"
#include "serializer/serializer.h"
#include "plat/plat_lib.h"
#include "misc/file_crc32.h"
#include "name/name.h"
#include "debug/source.h"
#include "file/elfi.h"
#ifndef GCWZERO
#include "locutus/locutus_adapt.h"
#include "jlp/jlp.h"
#else
#include "jzintv.h"
#endif
#include "mapping.h"
#include "cfg.h"

#include <errno.h>

LOCAL path_t *rom_path;

void cfg_default(event_t *event);


/* ======================================================================== */
/*  CFG_GET_EVTACT   -- Convert an event action name into an event action   */
/*                      index.  This is a horrible linear search.  :-P      */
/*                      Hey, it works for now.                              */
/* ======================================================================== */
int cfg_get_evtact(const char *event_action_name)
{
    int i;

    for (i = 0; i < cfg_event_action_cnt; i++)
    {
        if (!strcmp(cfg_event_action[i].name, event_action_name))
            return i;
    }

    return -1;
}

/* ======================================================================== */
/*  CFG_SETBIND  -- Set all of the key-bindings for the Intellivision.      */
/* ======================================================================== */
int cfg_setbind(cfg_t *cfg, const char *kbdhackfile)
{
    int i, j, map;
    LZFILE *f = NULL;
    int action;
    char buf[256];

    /* -------------------------------------------------------------------- */
    /*  Iterate over the bindings table.                                    */
    /* -------------------------------------------------------------------- */
    for (i = 0; cfg->binding[i].key != NULL; i++)
    {
        /* ---------------------------------------------------------------- */
        /*  Iterate over the four possible "event spaces" that the user     */
        /*  may have configured.  For instance, the user may have set up    */
        /*  "Normal", "Swapped", "Alpha-numeric", and one other.            */
        /* ---------------------------------------------------------------- */
        for (j = 0; j < 4; j++)
        {
            /* ------------------------------------------------------------ */
            /*  Skip empty event bindings.  These keys aren't bound.        */
            /* ------------------------------------------------------------ */
            if (!cfg->binding[i].event_action[j] || 
                !cfg->binding[i].event_action[j][0]) 
                continue;

            /* ------------------------------------------------------------ */
            /*  Look up the event name, and skip if the name is invalid.    */
            /* ------------------------------------------------------------ */
            if ((action = cfg_get_evtact(cfg->binding[i].event_action[j])) < 0)
            {
                fprintf(stderr, "cfg:  Invalid event action '%s'\n", 
                        cfg->binding[i].event_action[j]);
                continue;
            }

            /* ------------------------------------------------------------ */
            /*  Map the key to the event.                                   */
            /* ------------------------------------------------------------ */
            event_map(&cfg->event, cfg->binding[i].key, j,
                      cfg_event_action[action].word, 
                      cfg_event_action[action].and_mask, 
                      cfg_event_action[action].or_mask);
        }
    }

    /* -------------------------------------------------------------------- */
    /*  HACK: If the user specified a keyboard mapping file, read that in.  */
    /* -------------------------------------------------------------------- */
    if (!kbdhackfile)
        return 0;

    if (!(f = lzoe_fopen(kbdhackfile, "r")))
    {
        fprintf(stderr, "Couldn't open keyboard map file '%s'\n", kbdhackfile);
        exit(1);
    }

    map = 0;
    while (lzoe_fgets(buf, 256, f) != NULL)
    {
        char *s1, *s2;
        char cmd[256], arg[256], arg2[256], arg3[256];
        int bad, too_many;

        bad = too_many = 0;

        if ((s1 = strrchr(buf, ';')))  *s1 = 0;
        if ((s1 = strrchr(buf, '\r'))) *s1 = 0;
        if ((s1 = strrchr(buf, '\n'))) *s1 = 0;

        cmd[0] = 0;
        arg[0] = 0;


        s1 = buf;
        while (*s1 && isspace(*s1)) s1++;

        s2 = cmd;
        while (*s1 && !isspace(*s1) && s2 < &cmd[255]) *s2++ = *s1++;
        if (s2 >= &cmd[255]) bad = 1;
        *s2 = 0;

        while (*s1 && isspace(*s1)) s1++;

        s2 = arg;
        while (*s1 && !isspace(*s1) && s2 < &arg[255]) *s2++ = *s1++;
        if (s2 >= &arg[255]) bad = 2;
        *s2 = 0;

        while (*s1 && isspace(*s1)) s1++;

        s2 = arg2;
        while (*s1 && !isspace(*s1) && s2 < &arg2[255]) *s2++ = *s1++;
        if (s2 >= &arg2[255]) bad = 3;
        *s2 = 0;

        while (*s1 && isspace(*s1)) s1++;

        s2 = arg3;
        while (*s1 && !isspace(*s1) && s2 < &arg3[255]) *s2++ = *s1++;
        if (s2 >= &arg3[255]) bad = 4;
        *s2 = 0;

        while (*s1 && isspace(*s1)) s1++;

        if (*s1) too_many = 1;

        if (bad)
        {
            fprintf(stderr, "cfg:  Fatal error (argument too long?) parsing "
                            "kbdhackfile (%d)\n", bad);
            goto fail;
        }

        if (!too_many && !stricmp(cmd, "add_combo")) 
        {
            j = atoi(arg);

            jzp_printf("cfg:  Combining %s and %s as COMBO%d\n", arg2, arg3, j);

            if (j > 31 || j < 0)
            {
                fprintf(stderr, "cfg:  COMBO number out of range\n");
                goto fail;
            }

            if (event_combine(&cfg->event, arg2, arg3, j))
            {
                fprintf(stderr, "cfg:  Error registering combo\n");
                goto fail;
            }

            continue;
        } 
        
        if (arg2[0] != 0 || arg3[0] != 0 || too_many)
        {
            fprintf(stderr, "cfg:  Too many arguments in kbdhackfile:\n> %s\n",
                    buf);
            fprintf(stderr, "cmd='%s' arg='%s' arg2='%s' arg3='%s' s1='%s'\n",
                    cmd,arg,arg2,arg3,s1);
            goto fail;
        }

        if (!stricmp(cmd, "combo_delay"))
        {
            double ms = atof(arg);

            if (ms < 0. || ms > 100.)
            {
                fprintf(stderr, "cfg:  COMBO_DELAY out of range.\n");
                goto fail;
            }

            jzp_printf("cfg:  Setting combo event delay to %5.2fms\n", ms);

            cfg->event.coalesce_time = ms / 1000.;
            continue;
        }

        if (!stricmp(cmd, "map")) 
        { 
            map = atoi(arg); 

            if (map < 0 || map > 3)
            {
                fprintf(stderr, "cfg:  Map number out of range\n");
                goto fail;
            }
            continue; 
        }

        if (cmd[0] == 0 || arg[0] == 0)
        {
            if (cmd[0])
            {
                fprintf(stderr, "cfg:  Unknown command '%s' in %s\n",
                        cmd, kbdhackfile);
                goto fail;
            }
            continue;
        }

        jzp_printf("cfg:  Binding %s to %s in map %d\n", cmd, arg, map);

        if ((action = cfg_get_evtact(arg)) < 0)
        {
            fprintf(stderr, "cfg: Invalid event action '%s'\n", arg);
            goto fail;
        }

        event_map(&cfg->event, cmd, map, 
                  cfg_event_action[action].word, 
                  cfg_event_action[action].and_mask, 
                  cfg_event_action[action].or_mask);
    }

    lzoe_fclose(f);
    return 0;

fail:
    if (f) lzoe_fclose(f);
    return -1;
}
/* ======================================================================== */
/*  Built-in display resolutions.                                           */
/* ======================================================================== */
LOCAL const int res_x[] = { 320, 640, 320, 1024, 1680, 800, 1600, 3280 };
LOCAL const int res_y[] = { 200, 480, 240,  768, 1050, 400, 1200, 1200 };
LOCAL const int res_d[] = { 8,   8  , 16,     8,    8, 16 ,   32,   32 };
#define NUM_RES ((int)(sizeof(res_x) / sizeof(res_x[0])))

/* ======================================================================== */
/*  CFG_PARSERES -- Parse a resolution string.  Return -1 if invalid.       */
/* ======================================================================== */
LOCAL const char *cfg_parseres(const char *const res, 
                               int *const rx, int *const ry, int *const rd)
{
    int s;

    if (!rx || !ry || !rd) 
        return "cfg_parseres: internal error, invalid arguments";

    if (!res)
    {
        *rx = res_x[0];
        *ry = res_y[0];
        *rd = res_d[0];
        return NULL;
    }

    s = sscanf(res, "%d%*1c%d%*1c%d", rx, ry, rd);

    if (s < 1 || s > 3)
        return "Bad resolution string.";


    if (s == 1)
    {
        if (*rx >= 0 && *rx < NUM_RES)
        {
            int i = *rx;
            *rx = res_x[i];
            *ry = res_y[i];
            *rd = res_d[i];
            return NULL;
        }

        if (*rx < 0)
            return ""; /* not really an error, but force resolution listing */

        return "Resolution number out of range.";
    }

    if (s == 2)
        *rd = 8;

    if (!(*rd == 8 || *rd == 16 || *rd == 24 || *rd == 32))
        return "Color depth must be 8, 16, 24 or 32";

    if (*rx < 320)
        return "X resolution is too small.";

    if (*ry < 200)
        return "Y resolution is too small.";

    return NULL;
}


/* ======================================================================== */
/*  CFG_LONGOPT  -- Long options for getopt_long                            */
/* ======================================================================== */
struct option cfg_longopt[] =
{
    {   "ecsimg",       1,      NULL,       'E'     },
    {   "execimg",      1,      NULL,       'e'     },
    {   "gromimg",      1,      NULL,       'g'     },
    {   "ecs",          2,      NULL,       's'     },
    {   "fullscreen",   2,      NULL,       'f'     },
    {   "audiofile",    1,      NULL,       'F'     },
    {   "debugger",     0,      NULL,       'd'     },
    {   "ratecontrol",  2,      NULL,       'r'     },
    {   "macho",        2,      NULL,       'r'     },
    {   "fullscreen",   2,      NULL,       'x'     },
    {   "displaysize",  1,      NULL,       'z'     },
    {   "resolution",   1,      NULL,       'z'     },
    {   "audio",        1,      NULL,       'a'     },
    {   "audiorate",    1,      NULL,       'a'     },
    {   "audiowindow",  1,      NULL,       'w'     },
    {   "audiobufsize", 1,      NULL,       'B'     },
    {   "audiobufcnt",  1,      NULL,       'C'     },
    {   "audiomintick", 1,      NULL,       'M'     },
    {   "voice",        2,      NULL,       'v'     },
    {   "voicewindow",  2,      NULL,       'W'     },
    {   "voicefiles",   2,      NULL,       'V'     },
    {   "i2pc0",        2,      NULL,       'i'     },
    {   "i2pc1",        2,      NULL,       'I'     },
    {   "intv2pc0",     2,      NULL,       'i'     },
    {   "intv2pc1",     2,      NULL,       'I'     },
#ifdef CGC_DLL
    {   "cgc0",         2,      NULL,       1       },
    {   "cgc1",         2,      NULL,       2       },
#endif
#ifdef CGC_THREAD
    {   "cgc0",         1,      NULL,       1       },
    {   "cgc1",         1,      NULL,       2       },
#endif
    {   "icartcache",   2,      NULL,       'c'     },
    {   "help",         0,      NULL,       'h'     },
    {   "license",      0,      NULL,       'l'     },
    {   "nobusywait",   0,      NULL,       '9'     },
    {   "kbdhackfile",  1,      NULL,       3       },
    {   "kbdmap",       1,      NULL,       'm'     },  
    {   "demofile",     1,      NULL,       'D'     },

    {   "js0",          2,      NULL,       4       },
    {   "js1",          2,      NULL,       5       },
    {   "js2",          2,      NULL,       6       },
    {   "js3",          2,      NULL,       7       },

#ifdef GP2X
#ifdef GCWZERO
#else
    {   "gp2xclock",    1,      NULL,       8       },
    {   "gp2x-pad-bias",1,      NULL,       4       },
#endif
#endif

    {   "gfx-swsurf",   2,      NULL,       9       },
    {   "gfx-dblbuf",   2,      NULL,       10      },
    {   "gfx-asyncb",   2,      NULL,       11      },
    {   "gfx-hwpal",    2,      NULL,       12      },

    {   "gfx-verbose",  1,      NULL,       15      },

    {   "gfx-dirtyrect",2,      NULL,       16      }, 
    {   "gfx-dr-clean-merge",2, NULL,       17      }, 

    {   "gui-mode",     0,      NULL,       13      },

    {   "rom-path",     1,      NULL,       'p'     },
    {   "quiet",        0,      NULL,       'q'     },
    {   "start-delay",  2,      NULL,       14      },
//    {   "jlp",          0,      NULL,       'J'     },
//    {   "jlp-savegame", 1,      NULL,       'J'     },
    {   "sym-file",     1,      NULL,       'S'     },
    {   "script",       1,      NULL,       18      },
    {   "src-map",      1,      NULL,       19      },

    {   "rand-mem",     0,      NULL,       20      },
    {   "file-io",      1,      NULL,       21      },
    {   "pal",          0,      NULL,       'P'     },

    {   "enable-mouse", 0,      NULL,       22      },
    {   "prescale",     1,      NULL,       23      },

//gcw    {   "locutus",      0,      NULL,       127     },  // for testing

    {   NULL,           0,      NULL,       0       }
};

LOCAL const char *optchars= "E:e:g:s::f::F:?dhlqr:P::x::z:a:w:B:C:M:m:"
                            "v::W::V::i::I::c:D:p:J:";

/* ======================================================================== */
/*  Supported I/O addresses for INTV2PC.                                    */
/* ======================================================================== */
const uint_32 i2pc_ports[4] = { 0x0, 0x378, 0x278, 0x3BC };

LOCAL char *joy_cfg[MAX_JOY];

/* ======================================================================== */
/*  CFG_INIT     -- Parse command line and get started                      */
/* ======================================================================== */
#ifdef GCWZERO
int cfg_init(cfg_t *cfg, int argc, char * argv[])
#else
void cfg_init(cfg_t *cfg, int argc, char * argv[])
#endif
{
    int c, option_idx = 0, rx, ry, rd;
    int exec_type = 0, legacy_rom = 0; 
    int value = 1, busywaits = 1;
    uint_32 cache_flags = IC_CACHE_DFLT;
    char *audiofile = NULL, *tmp;
    char *kbdhackfile = NULL;
    char *demofile = NULL;
    char *jlpsg = NULL;
    char *elfi_prefix = NULL;
    int jlp = 0;
    LZFILE *f;
    int silent = 0;
    char *debug_symtbl = NULL;
    char *debug_script = NULL;
    char *debug_srcmap = NULL;
    int snd_buf_size   = 0;
    int snd_buf_cnt    = 0;
    int gfx_verbose    = 0;
    int rand_mem       = 0;
    int pal_mode       = 0;
    int enable_mouse   = 0;
    char       *disp_res = NULL;
    const char *err_msg  = NULL;
    int locutus        = 0;
#ifndef NO_SERIALIZER
    ser_hier_t *ser_cfg;
#endif

#ifdef GP2X
#ifdef GCWZERO
#else
    int gp2xclock = 200;
#endif
#endif

#ifdef WII
    silent = 1;
#endif

    /* -------------------------------------------------------------------- */
    /*  Set up the default state for everything.                            */
    /* -------------------------------------------------------------------- */
    memset((void *)cfg, 0, sizeof(cfg_t));

    cfg->audio_rate = DEFAULT_AUDIO_HZ;     /* see config.h                 */
    cfg->psg_window = -1;           /* Automatic window setting.            */
    cfg->ecs_enable = -1;           /* Automatic (dflt: ECS off)            */
    cfg->ivc_enable = -1;           /* Automatic (dflt: Intellivoice off.   */
    cfg->ivc_window = -1;           /* Automatic window setting.            */
    cfg->gfx_flags  = 0             /* Windowed, single buf, hardware surf  */
#if 0
                    | GFX_DRECTS    /* Dirty rectangle update               */
                    | GFX_DRCMRG;   /* Allow merging cln rect btwn 2 dirty  */
#else
                    /* dirty rectangles disabled for now. */
                    ;
#endif
    cfg->i2pc0_port = 0;            /* No INTV2PC #0                        */
    cfg->i2pc1_port = 0;            /* No INTV2PC #1                        */
    cfg->cgc0_num   = -1;           /* No CGC #0                            */
    cfg->cgc1_num   = -1;           /* No CGC #1                            */
    cfg->cgc0_dev   = NULL;         /* No CGC #0                            */
    cfg->cgc1_dev   = NULL;         /* No CGC #1                            */
    cfg->debugging  = 0;            /* No debugger.                         */
    cfg->rate_ctl   = 1.0;          /* Rate control enabled.                */
    cfg->accutick   = 1;            /* fully accurate audio.                */
    cfg->binding    = cfg_key_bind; /* default key bindings.                */
    cfg->start_dly  = -1;           /* No startup delay by default.         */

#ifdef GCWZERO
    cfg->fn_exec    = strdup("/media/data/local/home/.jzintellivision/bios/exec.bin");   /* Default name to look for     */
    cfg->fn_grom    = strdup("/media/data/local/home/.jzintellivision/bios/grom.bin");   /* ...                          */
    cfg->fn_game    = strdup("/media/data/local/home/.jzintellivision/bios/game.rom");   /* ...                          */
    cfg->fn_ecs     = strdup("/media/data/local/home/.jzintellivision/bios/ecs.bin");    /* ...                          */
#else
    cfg->fn_exec    = strdup("exec.bin");   /* Default name to look for     */
    cfg->fn_grom    = strdup("grom.bin");   /* ...                          */
    cfg->fn_game    = strdup("game.rom");   /* ...                          */
    cfg->fn_ecs     = strdup("ecs.bin");    /* ...                          */
#endif
#define STR_REPLACE(x,y) { CONDFREE(x); (x) = strdup(y); }

    /* -------------------------------------------------------------------- */
    /*  Figure out out our executable's path.  If none, assume ".".         */
    /* -------------------------------------------------------------------- */
    {
        char *s;

        exe_path = strdup(argv[0]);

        s = strrchr(exe_path, PATH_SEP);
#ifdef GCWZERO
	jzp_printf("rom path = \n%s\n");
#endif
        if (s)
            *s = 0;
        else
        {
            free(exe_path);
            exe_path = strdup(".");
        }
    }

#ifndef NO_SERIALIZER
    /* -------------------------------------------------------------------- */
    /*  Register our config variables for serialization.                    */
    /* -------------------------------------------------------------------- */
#define SER_REG(x,t,l,f)\
    ser_register(ser_cfg, #x, &cfg-> x, t, l, f)

    ser_cfg = ser_new_hierarchy(NULL, "cfg");
    SER_REG(ecs_enable, ser_s32,    1,  SER_INIT|SER_MAND);
    SER_REG(ivc_enable, ser_s32,    1,  SER_INIT|SER_MAND);
    SER_REG(ivc_tname,  ser_string, 1,  SER_INIT|SER_MAND);
#endif

    /* -------------------------------------------------------------------- */
    /*  Parse the commandline flags.                                        */
    /* -------------------------------------------------------------------- */
    while ((c = getopt_long(argc, argv, optchars, cfg_longopt,
                            &option_idx)) != EOF)
    {
        int noarg = 1;
        double dvalue;

        value = 1;
        dvalue = 1.0;
        if (optarg) 
        {
            noarg = 0;
            value = atoi(optarg);
            sscanf(optarg, "%lf", &dvalue);
        }

        switch (c)
        {
            case 127: locutus = 1;                                      break;

            case '?': case 'h': usage();                                break; 
            case 'l': license();                                        break; 
            case 'q': silent = 1;                                       break;
            case 'B': snd_buf_size    = value;                          break;
            case 'C': snd_buf_cnt     = value;                          break;
            case 'M': cfg->accutick   = value;                          break;
            case 'E': STR_REPLACE(cfg->fn_ecs     , optarg);            break;
            case 'e': STR_REPLACE(cfg->fn_exec    , optarg);            break;
            case 'g': STR_REPLACE(cfg->fn_grom    , optarg);            break;
            case 'F': STR_REPLACE(audiofile       , optarg);            break;
            case 's': cfg->ecs_enable = value;                          break;
            case 'z': STR_REPLACE(disp_res        , optarg);            break;
            case 'd': cfg->debugging  = 1;                              break;
            case 'r': cfg->rate_ctl   = dvalue;                         break;
            case 'a': cfg->audio_rate = value;                          break;
            case 'w': cfg->psg_window = value;                          break;
            case 'v': cfg->ivc_enable = value;                          break;
            case 'W': cfg->ivc_window = value;                          break;
            case 'V': STR_REPLACE(cfg->ivc_tname  , optarg);            break;
            case 'i': cfg->i2pc0_port = value;                          break;
            case 'I': cfg->i2pc1_port = value;                          break;
            case 'S': STR_REPLACE(debug_symtbl    , optarg);            break;
            case 18:  STR_REPLACE(debug_script    , optarg);            break;
            case 19:  STR_REPLACE(debug_srcmap    , optarg);            break;
            case 1:   cfg->cgc0_num   = noarg ? 0 : value;             
                      STR_REPLACE(cfg->cgc0_dev   , optarg);            break;
            case 2:   cfg->cgc1_num   = noarg ? 0 : value;             
                      STR_REPLACE(cfg->cgc1_dev   , optarg);            break;
            case 3:   STR_REPLACE(kbdhackfile     , optarg);            break;

            case 4: case 5: case 6: case 7:
                STR_REPLACE(joy_cfg[c - 4], (optarg ? optarg : ""));    break;

#ifdef GP2X
#ifdef GCWZERO
#else
            case 8:   gp2xclock = value;                        break;
#endif
#endif

#define CHG_BIT(var, bit, to) (var) = ((var) & ~(bit)) | ((to) ? (bit) : 0)

            case 'f': case 'x': 
                      CHG_BIT(cfg->gfx_flags, GFX_FULLSC, value);       break;
            case 9:   CHG_BIT(cfg->gfx_flags, GFX_SWSURF, value);       break;
            case 10:  CHG_BIT(cfg->gfx_flags, GFX_DBLBUF, value);      
                      if (value != 0)                                  
                        CHG_BIT(cfg->gfx_flags, GFX_DRECTS, 0);         break;
            case 11:  CHG_BIT(cfg->gfx_flags, GFX_ASYNCB, value);       break;
            case 12:  CHG_BIT(cfg->gfx_flags, GFX_HWPAL,  value);       break;
            case 16:  CHG_BIT(cfg->gfx_flags, GFX_DRECTS, value);      
                      if (value != 0)                                  
                        CHG_BIT(cfg->gfx_flags, GFX_DBLBUF, 0);         break;
            case 17:  CHG_BIT(cfg->gfx_flags, GFX_DRCMRG, value);       break;
            case 15:  gfx_verbose = 1;                                  break;
                                                                       
            case 13:  cfg->gui_mode = 1;                                break;
            case 20:  rand_mem = 1;                                     break;
            case 'P': pal_mode = 1;                                     break;
                                                                       
            case 21:  STR_REPLACE(elfi_prefix     , optarg);            break;
            case 'D': STR_REPLACE(demofile        , optarg);            break;
            case 'J': jlp = 1;                                         
                      if (!noarg && !jlpsg)                            
                          STR_REPLACE(jlpsg, optarg);                   break;
            case '9': busywaits = 0;                                    break;
                                                                       
            case 22:  enable_mouse = 1;                                 break;
            case 23:  cfg->prescale = value;                            break;

            case 'c': 
            {
                const char *name = "Default";
                switch (value)
                {
                    default:
                    case 0: cache_flags = IC_CACHE_CABS;
                            name = "Cache bankswitched";            break;
                    case 1: cache_flags = IC_CACHE_NOBS;
                            name = "Don't cache bankswitched";      break;
                    case 2: cache_flags = IC_CACHE_SAFE;
                            name = "Cache read-only, no banksw";    break;
                    case 3: cache_flags = IC_CACHE_NONE;
                            name = "Cache nothing";                 break;
                }

                UNUSED(name);
                break;
            }

            case 'p':
            {
                rom_path = parse_path_string(rom_path, optarg);
                break;
            }

            case 14:
            {
                float ftmp = -1.f;

                sscanf(optarg, "%f", &ftmp);

                cfg->start_dly = 1000.f * ftmp;
                break;
            }

            case 'm':
            {
                if (value >= 0 && value < 4)
                    cfg->event.cur_kbd = value;
                break;
            }


            default:
            {
                fprintf(stderr, "Unrecognized option: '%s'\n"
                        "Try jzintv --help for usage information.\n", 
                        argv[optind-1]);
                exit(1);
            }
        }
    }

    if (optind < argc)
        STR_REPLACE(cfg->fn_game, argv[optind]);

    rom_path = parse_path_string(rom_path, getenv("JZINTV_ROM_PATH"));

    if (DEFAULT_ROM_PATH)
        rom_path = parse_path_string(rom_path, DEFAULT_ROM_PATH);

    /* -------------------------------------------------------------------- */
    /*  Set up jzp_printf.                                                  */
    /* -------------------------------------------------------------------- */
    if (cfg->gui_mode)
    {
        cfg->debugging = 0;
        jzp_init(1, 0, NULL, NULL);
        setvbuf(stdin, NULL, _IONBF, 0);
#ifndef NO_FCNTL
        fcntl(STDIN_FILENO, F_SETFL, O_NDELAY);
#endif
    } else
        jzp_init(silent, stdout, NULL, NULL);

#ifdef WII
    /* -------------------------------------------------------------------- */
    /*  On WII, just make sure we're full-screen.                           */
    /* -------------------------------------------------------------------- */
    cfg->gfx_flags |= GFX_FULLSC;
#endif

#ifdef GP2X
    /* -------------------------------------------------------------------- */
    /*  On GP2X, simply force a few arguments to the only supported vals.   */
    /*  Also, adjust the clock if the user requests it.                     */
    /* -------------------------------------------------------------------- */
    cfg->gfx_flags |=  GFX_FULLSC;
    cfg->gfx_flags &= ~GFX_DBLBUF;
    STR_REPLACE(disp_res, "2");
#ifdef GCWZERO
    static char *alreadyreset;
#else
    if (gp2xclock > 0)
    {
        extern int gp2x_speed(int);

        if (gp2x_speed(gp2xclock))
        {
            jzp_printf("Clock rate %d unsupported.\n", gp2xclock);
            exit(1);
        }
    }
#endif
#endif

    /* -------------------------------------------------------------------- */
    /*  Sanity-check some of the flags.  Most get checked by peripherals.   */
    /* -------------------------------------------------------------------- */
    if ( (err_msg = cfg_parseres( disp_res, &rx, &ry, &rd )) != NULL )
    {
        int i;
        fprintf(stderr, 
"%s\n"
"Resolution string must be of the following forms:\n"
"\n"
"   N       where N is one of the built in resolutions\n"
"   WxH     where W is display width and H is display height\n"
"   WxH,D   where W is display width, H is display height, and D is color depth\n"
"\n"
"Width must be at least 320, and height must be at least 200.  Color depth\n"
"defaults to 8bpp.  jzIntv supports 8bpp, 16bpp, 24bpp and 32bpp\n"
"\n"
"Valid built-in resolutions:\n\n",
            err_msg
        );

        for (i = 0; i < NUM_RES; i++)
        {
            fprintf(stderr, "    -z%d:  %dx%dx%d\n", 
                    i, res_x[i], res_y[i], res_d[i]);
        }
        exit(1);
    }
    CONDFREE(disp_res);

    if ( gfx_check(rx, ry, rd, cfg->prescale) != 0 )
    {
        exit(1);
    }

    /* -------------------------------------------------------------------- */
    /*  Delay starting emulation if full-screen is specified and no other   */
    /*  start delay is specified.                                           */
    /* -------------------------------------------------------------------- */
#if FULLSC_START_DLY > 0
    if (cfg->start_dly < 0 && (cfg->gfx_flags & GFX_FULLSC) != 0)
        cfg->start_dly = FULLSC_START_DLY;
#endif


    /* -------------------------------------------------------------------- */
    /*  He's a macho, macho duck.  He's a macho, macho duck!                */
    /* -------------------------------------------------------------------- */
    if (cfg->rate_ctl < 1.0 && cfg->rate_ctl > 0.01)
        cfg->rate_ctl = 1.0;
    else if (cfg->rate_ctl <= 0.01)
        cfg->rate_ctl = 0;

#ifdef DIRECT_INTV2PC
    /* -------------------------------------------------------------------- */
    /*  Look up INTV2PC port numbers, if any.                               */
    /* -------------------------------------------------------------------- */
    if (cfg->i2pc0_port > 3 || cfg->i2pc1_port > 3)
    {
        fprintf(stderr, "ERROR:  "
            "INTV2PC port number out of range.  Valid values are 1..3 for\n"
            "typical ports for LPT1: through LPT3:, and 0 to disable.\n"
            "\n"
            "The following port numbers are selected by 1 through 3:\n"
            "   1 selects 0x%.3X\n"
            "   2 selects 0x%.3X\n"
            "   3 selects 0x%.3X\n"
            "\n", i2pc_ports[1], i2pc_ports[2], i2pc_ports[3]);
        exit(1);
    }
    if (cfg->i2pc0_port && cfg->i2pc0_port == cfg->i2pc1_port)
    {
        fprintf(stderr, "ERROR:  Cannot enable two INTV2PCs on same port #\n");
        exit(1);
    }
    cfg->i2pc0_port = i2pc_ports[cfg->i2pc0_port];
    cfg->i2pc1_port = i2pc_ports[cfg->i2pc1_port];
#endif

    /* -------------------------------------------------------------------- */
    /*  Create a new peripheral bus for the Intellivision main console.     */
    /* -------------------------------------------------------------------- */
    cfg->intv = periph_new(16, 16, 4);
    strncpy(cfg->intv->periph.name, "Master Component", 16);

    /* -------------------------------------------------------------------- */
    /*  Now, configure the Intellivision according to our flags.  Start     */
    /*  off by reading in the EXEC, GROM, and GAME images.                  */
    /* -------------------------------------------------------------------- */
    f = path_fopen(rom_path, cfg->fn_exec, "rb");  

    exec_type = 0;
    if (!f || file_read_rom16(f, 4096, cfg->exec_img) != 4096)
    {
#ifdef GCWZERO
        jzp_printf("\nNo exec image found\n");
        return 2;
#else
        if (errno) perror("file_read_rom16");
        fprintf(stderr, "ERROR:  Could not read EXEC image '%s'\n",
                cfg->fn_exec);
        dump_search_path(rom_path);
        exit(1);
#endif
    }
    lzoe_fseek(f, 0, SEEK_END);
    if (lzoe_ftell(f) == 2 * (4096 + 256))
    {
        exec_type = 1;
        lzoe_fseek(f, 8192, SEEK_SET);
        if (file_read_rom16(f, 256, cfg->exec_img + 4096) != 256)
        {
#ifdef GCWZERO
            jzp_printf("\nNo exec2 image found\n");
            return 3;
#else
            if (errno) perror("file_read_rom16");
            fprintf(stderr, "ERROR:  Could not read EXEC2 image '%s'\n",
                    cfg->fn_exec);
            exit(1);
#endif
        }
    }
    lzoe_fclose(f);

    f = path_fopen(rom_path, cfg->fn_grom, "rb");
    if (!f || file_read_rom8 (f, 2048, cfg->grom_img) != 2048)
    {
#ifdef GCWZERO
        jzp_printf("\nNo grom image found\n");
        return 4;
#else
        if (errno) perror("file_read_rom8");
        fprintf(stderr, "ERROR:  Could not read GROM image '%s'\n",
                cfg->fn_grom);
        dump_search_path(rom_path);
        exit(1);
#endif
    }
    lzoe_fclose(f);

    /* -------------------------------------------------------------------- */
    /*  XXX:  Hack:  If locutus == 1, then this is a LUIGI file.  Short     */
    /*  circuit everything and just read fn_game as a LUIGI file.           */
    /* -------------------------------------------------------------------- */
#ifdef GCWZERO
#else
    if (locutus)
    {
        if ( make_locutus( &(cfg->locutus), cfg->fn_game, &cfg->cp1600 ) )
        {
            fprintf(stderr, "ERROR:  make_locutus failed\n");
            exit(1);
        }

        goto locutus_loaded;
    }
#endif
    /* -------------------------------------------------------------------- */
    /*  First try to load it as a legacy ROM.  If the legacy code decides   */
    /*  it's not actually a BIN+CFG, it'll hand us back a .ROM filename.    */
    /* -------------------------------------------------------------------- */
#ifdef GCWZERO
cfgreset:
#endif
    tmp = legacy_bincfg(&(cfg->legacy), rom_path, cfg->fn_game, &legacy_rom,
                        &(cfg->cp1600));

    if (tmp == NULL)
    {
#ifdef GCWZERO
;
//        return;
#else
        fprintf(stderr, "ERROR:  Failed to initialize game\n");
        exit(1);
#endif
    }
    CONDFREE(cfg->fn_game);
    cfg->fn_game = tmp;
    /* -------------------------------------------------------------------- */
    /*  If it wasn't a legacy ROM, it must be an Intellicart ROM.           */
    /* -------------------------------------------------------------------- */
    if (!legacy_rom)
    {
        /* not path_fopen, because legacy_bincfg should do that for us. */
        if (!(f = lzoe_fopen(cfg->fn_game, "rb")))
        {
            perror("fopen()");
            fprintf(stderr, "ERROR:  Failed to open Intellicart ROM:\n  %s\n",
                    cfg->fn_game);
            exit(1);
        }

        /* ---------------------------------------------------------------- */
        /*  Process the Intellicart ROM itself.                             */
        /* ---------------------------------------------------------------- */
        if (icart_init(&cfg->icart, f, NULL))
        {
            fprintf(stderr, "ERROR:  Failed to register Intellicart\n");
            exit(1);
        }

        /* ---------------------------------------------------------------- */
        /*  TODO:  Process meta-data tags on Intellicart image.             */
        /* ---------------------------------------------------------------- */
        lzoe_fclose(f);
    }


    /* -------------------------------------------------------------------- */
    /*  Now that we've established the ROM path exactly, go see if we       */
    /*  know anything about it, such as its name, what year it was made,    */
    /*  and if it prefers to have voice / ECS turned on.  If the user       */
    /*  didn't specify voice on/off or ECS on/off, we'll use this info.     */
    /* -------------------------------------------------------------------- */
    {
        uint_32 crc32;
        int     default_ecs = -1, default_ivc = -1;
        crc32 = file_crc32(cfg->fn_game);
        if ((cfg->cart_name = find_cart_name(crc32, 
                                             &cfg->cart_year,
                                             &default_ecs,
                                             &default_ivc)) != NULL)
        {
            if (cfg->ecs_enable == -1)
                cfg->ecs_enable = default_ecs;
            if (cfg->ivc_enable == -1)
                cfg->ivc_enable = default_ivc;
        }
    }
#ifdef GCWZERO
#else
locutus_loaded:
#endif
#ifdef GCWZERO // If we've created a new .cfg file we must reload romfile or the emulator will crash
    jzp_printf("\nChecking if resetflag set: Currently %d\n\n",resetflag);
    if (resetflag) 
    {
        if(!alreadyreset)
        {
            alreadyreset=1; //avoid infinite loop!
            goto cfgreset;
        }
    }
#endif

#ifdef WII
    /* -------------------------------------------------------------------- */
    /*  On the Wii, default to the ECS keyboard bindings if ECS is enabled  */
    /*  since controller input will come from actual Wii controllers.       */
    /* -------------------------------------------------------------------- */
    if (cfg->ecs_enable > 0)
    {
        cfg->event.cur_kbd = 2;
        cfg->event.prv_kbd = 2;
    }
#endif


    /* -------------------------------------------------------------------- */
    /*  Initialize the peripherals.                                         */
    /* -------------------------------------------------------------------- */
    jzp_printf("jzintv:  Initializing Master Component and peripherals...\n");

#ifdef DIRECT_INTV2PC
    if (cfg->i2pc0_port > 0 &&
        pad_intv2pc_init(&cfg->i2pc0, 0x1F0, cfg->i2pc0_port))
    {
        fprintf(stderr, "ERROR:  Failed to initialize INTV2PC #0 at 0x%.3X\n",
                cfg->i2pc0_port);
        exit(1);
    }
    if (cfg->ecs_enable > 0 &&
        cfg->i2pc1_port &&
        pad_intv2pc_init(&cfg->i2pc1, 0x0F0, cfg->i2pc1_port))
    {
        fprintf(stderr, "ERROR:  Failed to initialize INTV2PC #1 at 0x%.3X\n",
                cfg->i2pc1_port);
        exit(1);
    }
#endif

    if (cfg->cgc0_num >= 0 &&
        pad_cgc_init(&cfg->cgc0, 0x1F0, cfg->cgc0_num, cfg->cgc0_dev))
    {
        fprintf(stderr, "ERROR:  Failed to initialize CGC #%d as pad pair 0\n",
                cfg->cgc0_num);
        exit(1);
    }

    if (cfg->ecs_enable > 0 &&
        cfg->cgc1_num >= 0 &&
        pad_cgc_init(&cfg->cgc1, 0x0F0, cfg->cgc1_num, cfg->cgc0_dev))
    {
        fprintf(stderr, "ERROR:  Failed to initialize CGC #%d as pad pair 1\n",
                cfg->cgc1_num);
        exit(1);
    }

    if (emu_link_init())
    {
        fprintf(stderr, "ERROR:  Failed to initialize EMU_LINK\n");
        exit(1);
    }

    /* -------------------------------------------------------------------- */
    /*  Enable the Emu-Link File I/O if requested.                          */
    /* -------------------------------------------------------------------- */
    if (elfi_prefix)
        if (elfi_init(elfi_prefix))
        {
            fprintf(stderr, "ERROR:  Failed to initialize Emu-Link File I/O\n");
            exit(1);
        }

    if (demofile &&
        demo_init(&cfg->demo, demofile, &cfg->psg0, 
                  cfg->ecs_enable > 0 ? &cfg->psg1 : 0))
    {
        fprintf(stderr, "ERROR:  Failed to initialize demo recorder\n");
        exit(1);
    }

//    if (jlp &&
//        jlp_init(&cfg->jlp, jlpsg, &(cfg->cp1600.r[7])))
//    {
//        fprintf(stderr, "ERROR:  Failed to initialize JLP save game '%s'\n",
//                jlpsg);
//        exit(1);
//    }

    if (gfx_init(&cfg->gfx, rx, ry, rd, cfg->gfx_flags, gfx_verbose, 
                  cfg->prescale))
    {
        fprintf(stderr, "ERROR:  Failed to initialize graphics\n");
        exit(1);
    }

    if (cfg->audio_rate && snd_init(&cfg->snd, cfg->audio_rate, audiofile,
                                    snd_buf_size, snd_buf_cnt))
    {
        fprintf(stderr, "WARNING:  Failed to initialize sound.  Disabled.\n");
        cfg->audio_rate = 0;
    }

    if (cp1600_init(&cfg->cp1600, 0x1000, 0x1004))
    {
        fprintf(stderr, "ERROR:  Failed to initialize CP-1610 CPU\n");
        exit(1);
    }

    if (mem_make_ram  (&cfg->scr_ram,  8, 0x0100, 8, rand_mem) ||
        mem_make_ram  (&cfg->sys_ram, 16, 0x0200, 9, rand_mem) /* ||
        mem_make_glitch_ram(&cfg->glt_ram, 0xD000, 12) ||
        mem_make_ram  (&cfg->gram,     8, 0x3800, 9)*/)
    {
        fprintf(stderr, "ERROR:  Failed to initialize RAMs\n");
        exit(1);
    }
    if (exec_type != 0 &&
        mem_make_9600a(&cfg->sys_ram2,    0x0300, 8))
    {
        fprintf(stderr, "ERROR:  Failed to initialize RAMs\n");
        exit(1);
    }

    if (stic_init(&cfg->stic, cfg->grom_img, &cfg->cp1600.req_bus, &cfg->gfx, 
                  demofile ? &cfg->demo : NULL, rand_mem, pal_mode))
    {
        fprintf(stderr, "ERROR:  Failed to initialize STIC\n");
        exit(1);
    }

    if (cfg->ecs_enable > 0)
    {

        f = path_fopen(rom_path, cfg->fn_ecs, "rb");
        if (!f || file_read_rom16(f, 12*1024, cfg->ecs_img) != 12*1024)
        {
            if (errno) perror("errno value e");
            fprintf(stderr, "ERROR:  Could not read ECS ROM image '%s'\n",
                    cfg->fn_ecs);
            dump_search_path(rom_path);
            exit(1);
        }
        lzoe_fclose(f);

        if (mem_make_prom(&cfg->ecs0, 16, 0x2000, 12, 1, cfg->ecs_img     , 
                                                            &cfg->cp1600) ||
            mem_make_prom(&cfg->ecs1, 16, 0x7000, 12, 0, cfg->ecs_img+4096, 
                                                            &cfg->cp1600) ||
            mem_make_prom(&cfg->ecs2, 16, 0xE000, 12, 1, cfg->ecs_img+8192, 
                                                            &cfg->cp1600))
        {
            fprintf(stderr, "ERROR:  Can't make Paged ROM from ECS image\n");
            exit(1);
        }
        if (mem_make_ram(&cfg->ecs_ram, 8, 0x4000, 11, rand_mem))
        {
            fprintf(stderr, "ERROR:  Can't allocate ECS RAM\n");
            exit(1);
        }
    }


    if (ay8910_init(&cfg->psg0, 0x1F0, &cfg->snd, 
                    cfg->audio_rate, cfg->psg_window, cfg->accutick,
                    cfg->rate_ctl > 0.0 ? cfg->rate_ctl : 1.0, pal_mode))
    {
        fprintf(stderr, "ERROR:  Failed to initialize PSG#1 (AY8914)\n");
        exit(1);
    }

    if (cfg->ecs_enable > 0 &&
        ay8910_init(&cfg->psg1, 0x0F0, &cfg->snd, 
                    cfg->audio_rate, cfg->psg_window, cfg->accutick,
                    cfg->rate_ctl > 0.0 ? cfg->rate_ctl : 1.0, pal_mode))
    {
        fprintf(stderr, "ERROR:  Failed to initialize PSG#2 (AY8914)\n");
        exit(1);
    }

    if (pad_init(&cfg->pad0, 0x1F0, PAD_HAND))
    {
        fprintf(stderr, "ERROR:  Failed to initialize game pads\n");
        exit(1);
    }

    if (cfg->ecs_enable > 0 &&
        pad_init(&cfg->pad1, 0x0F0, PAD_KEYBOARD))
    {
        fprintf(stderr, "ERROR:  Failed to ECS input device\n");
        exit(1);
    }

    if (cfg->rate_ctl > 0.0 && 
        speed_init(&cfg->speed, &cfg->gfx, &cfg->stic, 
                   busywaits, cfg->rate_ctl, pal_mode))
    {
        fprintf(stderr, "ERROR:  Failed to initialize rate control.\n");
        exit(1);
    }

    if (cfg->debugging && 
        debug_init(&cfg->debug, &cfg->cp1600,
                   cfg->rate_ctl > 0.0 ? &cfg->speed : NULL, &cfg->gfx,
                   debug_symtbl, &cfg->stic.vid_enable, 
                   &cfg->stic.debug_flags,
                   debug_script))
    {
        fprintf(stderr, "ERROR:  Failed to initialize debugger\n");
        exit(1);
    }

    if (joy_init(1, joy_cfg))
    {
        fprintf(stderr, "ERROR:  Failed to initialize joystick subsystem.\n");
        exit(1);
    }

    if (event_init(&cfg->event, enable_mouse))
    {
        fprintf(stderr, "ERROR:  Failed to initialize event subsystem.\n");
        exit(1);
    }
    
    if (cfg_setbind(cfg, kbdhackfile))
    {
        fprintf(stderr, "ERROR:  Failed to initialize key bindings\n");
        exit(1);
    }

    if (cfg->ivc_enable > 0 && cfg->audio_rate > 0 &&
        ivoice_init(&cfg->ivoice, 0x80, &cfg->snd, 
                    cfg->audio_rate, cfg->ivc_window, cfg->ivc_tname))
    {
        fprintf(stderr, "ERROR:  Failed to initialize Intellivoice\n");
        exit(1);
    }

    /* -------------------------------------------------------------------- */
    /*  Note:  We handle the EXEC ROM specially, since it's weird on        */
    /*  the Intellivision 2.                                                */
    /* -------------------------------------------------------------------- */
    if (exec_type == 0)
    {
        if (mem_make_rom(&cfg->exec,     10, 0x1000, 12, cfg->exec_img))
        {
            fprintf(stderr, "ERROR:  Failed to initialize EXEC ROM\n");
            exit(1);
        }
    } else 
    {
        if (mem_make_rom(&cfg->exec,     10, 0x1000, 12, cfg->exec_img+256) ||
            mem_make_rom(&cfg->exec2,    10, 0x0400,  8, cfg->exec_img))
        {
            fprintf(stderr, "ERROR:  Failed to initialize EXEC2 ROM\n");
            exit(1);
        }
    }

    /* -------------------------------------------------------------------- */
    /*  Now register all the devices on the Intellivision's bus.            */
    /* -------------------------------------------------------------------- */
    #define P(x) cfg->intv, ((periph_p)(&(cfg->x)))

    periph_register    (P(cp1600         ),  0x0000, 0x0000, "CP-1610"     );

    periph_register    (P(psg0           ),  0x01F0, 0x01FF, "PSG0 AY8914" );
    if (cfg->ecs_enable > 0)
        periph_register(P(psg1           ),  0x00F0, 0x00FF, "PSG1 AY8914" );

    if (cfg->ivc_enable > 0 && cfg->audio_rate)
        periph_register(P(ivoice         ),  0x0080, 0x0081, "Int. Voice"  );

    periph_register    (P(gfx            ),  0x0000, 0x0000, "[Graphics]"  );
    if (cfg->audio_rate)               
        periph_register(P(snd            ),  0x0000, 0x0000, "[Sound]"     );
                                       
    periph_register    (P(scr_ram        ),  0x0100, 0x01EF, "Scratch RAM" );
    periph_register    (P(sys_ram        ),  0x0200, 0x035F, "System RAM"  );
/*  periph_register    (P(glt_ram        ),  0xD000, 0xDFFF, "GLITCH RAM"  );*/

    if (exec_type != 0)
        periph_register(P(sys_ram2       ),  0x0360, 0x03FF, "System RAM B");

    if (exec_type == 0)                
    {                                  
        periph_register(P(exec           ),  0x1000, 0x1FFF, "EXEC ROM"    );
    } else                             
    {                                  
        periph_register(P(exec           ),  0x1000, 0x1FFF, "EXEC2 main"  );
        periph_register(P(exec2          ),  0x0400, 0x04FF, "EXEC2 aux."  );
    }                                  

    if (cfg->ecs_enable > 0)
    {
        periph_register(P(ecs_ram        ),  0x4000, 0x47FF, "ECS RAM" );
        periph_register(P(ecs0           ),  0x2000, 0x2FFF, "ECS ROM (2xxx)");
        periph_register(P(ecs1           ),  0x7000, 0x7FFF, "ECS ROM (7xxx)");
        periph_register(P(ecs2           ),  0xE000, 0xEFFF, "ECS ROM (Exxx)");
    }
                                       
/*  periph_register    (P(grom           ),  0x3000, 0x37FF, "GROM"        );*/
/*  periph_register    (P(gram           ),  0x3800, 0x3FFF, "GRAM"        );*/

    periph_register    (P(pad0           ),  0x01F0, 0x01FF, "Pad Pair 0"  );
    if (cfg->ecs_enable > 0)
        periph_register(P(pad1           ),  0x00F0, 0x00FF, "Pad Pair 1"  );
    if (cfg->i2pc0_port)
        periph_register(P(i2pc0          ),  0x01F0, 0x01FF, "INTV2PC #0"  );
    if (cfg->i2pc1_port && cfg->ecs_enable > 0)
        periph_register(P(i2pc1          ),  0x00F0, 0x00FF, "INTV2PC #1"  );
    if (cfg->cgc0_num >= 0)
        periph_register(P(cgc0           ),  0x01F0, 0x01FF, "CGC #0"      );
    if (cfg->cgc1_num >= 0 && cfg->ecs_enable > 0)
        periph_register(P(cgc1           ),  0x00F0, 0x00FF, "CGC #1"      );
    periph_register    (P(stic.stic_cr   ),  0x0000, 0x007F, "STIC"        );
    periph_register    (P(stic.stic_cr   ),  0x4000, 0x403F, "STIC (alias)");
    periph_register    (P(stic.stic_cr   ),  0x8000, 0x803F, "STIC (alias)");
    periph_register    (P(stic.stic_cr   ),  0xC000, 0xC03F, "STIC (alias)");
    periph_register    (P(stic.snoop_btab),  0x0200, 0x02EF, "STIC (BTAB)" );
    periph_register    (P(stic.snoop_gram),  0x3000, 0x3FFF, "STIC (GRAM)" );

    periph_register    (P(event          ),  0x0000, 0x0000, "[Event]"     );

    if (cfg->rate_ctl > 0.0)
        periph_register(P(speed          ),  0x0000, 0x0000, "[Rate Ctrl]" );

    /* -------------------------------------------------------------------- */
    /*  Register the game ROMs, or the Intellicart, as the case may be.     */
    /* -------------------------------------------------------------------- */
//gcw    if (locutus)
//    {
//        periph_register(P(locutus), 0x0000, 0xFFFF, "Locutus");
//    } else if (legacy_rom)
    if (legacy_rom)
    {
        legacy_register(&cfg->legacy, cfg->intv, &cfg->cp1600);
    } else
    {
        icart_register(&cfg->icart, cfg->intv, &cfg->cp1600, cache_flags);
    }

    /* -------------------------------------------------------------------- */
    /*  Mark the ROMs cacheable in the CPU.  Mark the 16-bit RAM as cache-  */
    /*  able, but in need of bus-snoop support.                             */
    /* -------------------------------------------------------------------- */
    cp1600_cacheable(&cfg->cp1600, 0x0200, 0x035F, 1);
    cp1600_cacheable(&cfg->cp1600, 0x1000, 0x1FFF, 0);
    cp1600_cacheable(&cfg->cp1600, 0x3000, 0x37FF, 0);

    /* -------------------------------------------------------------------- */
    /*  If JLP save-games are enabled, install the JLP neccessary portion   */
    /*  of the JLP RAM window at $9C80 to $9FFF.  JLP games that use the    */
    /*  SG support need to NOT declare RAM in this space!!!                 */
    /* -------------------------------------------------------------------- */
//    if (jlp)
//        periph_register(P(jlp            ),  0x8000, 0x9FFF, "JLP Support"   );

    /* -------------------------------------------------------------------- */
    /*  Register the debugger.  This _must_ be done last.                   */
    /* -------------------------------------------------------------------- */
    if (cfg->debugging)
        periph_register(P(debug          ),  0x0000, 0xFFFF, "[Debugger]"  );

#if 0
    {
        f = fopen("ser.txt", "w");
        if (f)
            ser_print_hierarchy(f, NULL, 0, 0);
    }
#endif

    /* -------------------------------------------------------------------- */
    /*  Load the source mapping if given one.                               */
    /* -------------------------------------------------------------------- */
    if (cfg->debugging && debug_srcmap)
        process_source_map(debug_srcmap);

    /* -------------------------------------------------------------------- */
    /*  Initialize random number generator.  Do this last in case the rest  */
    /*  of initialization takes a random amount of time.                    */
    /* -------------------------------------------------------------------- */
    srand_jz(time(0) + (uint_32)(~0U * get_time()));

    /* -------------------------------------------------------------------- */
    /*  Free up all of our temporary variables.                             */
    /* -------------------------------------------------------------------- */
    CONDFREE(audiofile);
    CONDFREE(kbdhackfile);
    CONDFREE(demofile);
    CONDFREE(jlpsg);
    CONDFREE(debug_symtbl);
    CONDFREE(debug_srcmap);
    CONDFREE(elfi_prefix); 
    return 1;
}

/* ======================================================================== */
/*  CFG_DTOR     -- Tear down a configured Intellivision.                   */
/* ======================================================================== */
void cfg_dtor(cfg_t *cfg)
{
    periph_delete(cfg->intv);
    CONDFREE(cfg->ivc_tname);
    CONDFREE(cfg->cgc0_dev);
    CONDFREE(cfg->cgc1_dev);
    CONDFREE(cfg->fn_exec);
    CONDFREE(cfg->fn_game);
    CONDFREE(cfg->fn_grom);
    CONDFREE(cfg->fn_ecs);

    elfi_dtor();
    memset(cfg, 0, sizeof(cfg_t));
}

/* ======================================================================== */
/*  This program is free software; you can redistribute it and/or modify    */
/*  it under the terms of the GNU General Public License as published by    */
/*  the Free Software Foundation; either version 2 of the License, or       */
/*  (at your option) any later version.                                     */
/*                                                                          */
/*  This program is distributed in the hope that it will be useful,         */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of          */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       */
/*  General Public License for more details.                                */
/*                                                                          */
/*  You should have received a copy of the GNU General Public License       */
/*  along with this program; if not, write to the Free Software             */
/*  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.               */
/* ======================================================================== */
/*                 Copyright (c) 1998-2000, Joseph Zbiciak                  */
/* ======================================================================== */
