
/*
 *  \file autoopts.h
 *
 *  This file defines all the global structures and special values
 *  used in the automated option processing library.
 *
 * @group autoopts
 * @{
 */
/*
 *  This file is part of AutoOpts, a companion to AutoGen.
 *  AutoOpts is free software.
 *  AutoOpts is Copyright (C) 1992-2015 by Bruce Korb - all rights reserved
 *
 *  AutoOpts is available under any one of two licenses.  The license
 *  in use must be one of these two and the choice is under the control
 *  of the user of the license.
 *
 *   The GNU Lesser General Public License, version 3 or later
 *      See the files "COPYING.lgplv3" and "COPYING.gplv3"
 *
 *   The Modified Berkeley Software Distribution License
 *      See the file "COPYING.mbsd"
 *
 *  These files have the following sha256 sums:
 *
 *  8584710e9b04216a394078dc156b781d0b47e1729104d666658aecef8ee32e95  COPYING.gplv3
 *  4379e7444a0e2ce2b12dd6f5a52a27a4d02d39d247901d3285c88cf0d37f477b  COPYING.lgplv3
 *  13aa749a5b0a454917a944ed8fffc530b784f5ead522b1aacaf4ec8aa55a6239  COPYING.mbsd
 */

#ifndef AUTOGEN_AUTOOPTS_H
#define AUTOGEN_AUTOOPTS_H
#include <stdnoreturn.h>

#define AO_NAME_LIMIT           127
#define AO_NAME_SIZE            ((size_t)(AO_NAME_LIMIT + 1))

#ifndef AG_PATH_MAX
#  ifdef PATH_MAX
#    define AG_PATH_MAX         ((size_t)PATH_MAX)
#  else
#    define AG_PATH_MAX         ((size_t)4096)
#  endif
#else
#  if defined(PATH_MAX) && (PATH_MAX > MAXPATHLEN)
#     undef  AG_PATH_MAX
#     define AG_PATH_MAX        ((size_t)PATH_MAX)
#  endif
#endif

#undef  EXPORT
#define EXPORT

#ifndef NUL
#define NUL                     '\0'
#endif
#define BEL                     '\a'
#define BS                      '\b'
#define HT                      '\t'
#define LF                      '\n'
#define VT                      '\v'
#define FF                      '\f'
#define CR                      '\r'

#if defined(_WIN32) && !defined(__CYGWIN__)
# define DIRCH                  '\\'
#else
# define DIRCH                  '/'
#endif

#ifndef EX_USAGE
   /**
    *  Command line usage problem
    */
#  define EX_USAGE              64
#endif
#ifndef EX_DATAERR
   /**
    *  The input data was incorrect in some way.
    */
#  define EX_DATAERR            64
#endif
#ifndef EX_NOINPUT
   /**
    *  option state was requested from a file that cannot be loaded.
    */
#  define EX_NOINPUT            66
#endif
#ifndef EX_SOFTWARE
   /**
    *  AutoOpts Software failure.
    */
#  define EX_SOFTWARE           70
#endif
#ifndef EX_OSERR
   /**
    *  Command line usage problem
    */
#  define EX_OSERR              71
#endif

#define NL '\n'
#ifndef C
/**
 *  Coercive cast.  Compel an address to be interpreted as the type
 *  of the first argument.  No complaints, just do it.
 */
#define C(_t,_p)  ((_t)VOIDP(_p))
#endif

/* The __attribute__((__warn_unused_result__)) feature
   is available in gcc versions 3.4 and newer,
   while the typeof feature has been available since 2.7 at least.  */
# if __GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 4)
#  define ignore_val(x) ((void) (x))
# else
#  define ignore_val(x) (({ __typeof__ (x) __x = (x); (void) __x; }))
# endif

/*
 *  Convert the number to a list usable in a printf call
 */
#define NUM_TO_VER(n)           ((n) >> 12), ((n) >> 7) & 0x001F, (n) & 0x007F

#define NAMED_OPTS(po) \
        (((po)->fOptSet & (OPTPROC_SHORTOPT | OPTPROC_LONGOPT)) == 0)

#define SKIP_OPT(p)  (((p)->fOptState & OPTST_IMMUTABLE_MASK) != 0)

typedef int tDirection;
/**
 * handling option presets.  Start with command line and work through
 * config settings in reverse order.
 */
#define DIRECTION_PRESET        -1
/**
 * handling normal options.  Start with first config file, then environment
 * variables and finally the command line.
 */
#define DIRECTION_PROCESS       1
/**
 * An initialzation phase or an option being loaded from program sources.
 */
#define DIRECTION_CALLED        0

#define PROCESSING(d)           ((d)>0)
#define PRESETTING(d)           ((d)<0)
#define CALLED(d)               ((d)==0)

/**
 *  When loading a line (or block) of text as an option, the value can
 *  be processed in any of several modes.
 */
typedef enum {
    /**
     *  If the value looks like a quoted string, then process it.  Double
     *  quoted strings are processed the way strings are in "C" programs,
     *  except they are treated as regular characters if the following
     *  character is not a well-established escape sequence.  Single quoted
     *  strings (quoted with apostrophies) are handled the way strings are
     *  handled in shell scripts, *except* that backslash escapes are
     *  honored before backslash escapes and apostrophies.
     */
    OPTION_LOAD_COOKED,

    /**
     * Even if the value begins with quote characters, do not do quote
     * processing.  Strip leading and trailing white space.
     */
    OPTION_LOAD_UNCOOKED,

    /**
     * Keep every part of the value between the delimiters.
     */
    OPTION_LOAD_KEEP
} tOptionLoadMode;

static tOptionLoadMode option_load_mode;

/**
 *  The pager state is used by optionPagedUsage() procedure.
 *  When it runs, it sets itself up to be called again on exit.
 *  If, however, a routine needs a child process to do some work
 *  before it is done, then 'pagerState' must be set to
 *  'PAGER_STATE_CHILD' so that optionPagedUsage() will not try
 *  to run the pager program before its time.
 */
typedef enum {
    PAGER_STATE_INITIAL, //@< initial option paging state

    /**
     * temp file created and optionPagedUsage is scheduled to run at exit
     */
    PAGER_STATE_READY,

    /**
     *  This is a child process used in creating shell script usage.
     */
    PAGER_STATE_CHILD
} tePagerState;

typedef enum {
    ENV_ALL,
    ENV_IMM,
    ENV_NON_IMM
} teEnvPresetType;

typedef enum {
    TOPT_UNDEFINED = 0,
    TOPT_SHORT,
    TOPT_LONG,
    TOPT_DEFAULT
} teOptType;

typedef struct {
    tOptDesc *          pOD;
    char const *        pzOptArg;
    opt_state_mask_t    flags;
    teOptType           optType;
} tOptState;
#define OPTSTATE_INITIALIZER(st) \
    { NULL, NULL, OPTST_ ## st, TOPT_UNDEFINED }

#define TEXTTO_TABLE \
        _TT_(LONGUSAGE) \
        _TT_(USAGE) \
        _TT_(VERSION)
#define _TT_(n) \
        TT_ ## n ,

typedef enum { TEXTTO_TABLE COUNT_TT } teTextTo;

#undef _TT_

/**
 * option argument types.  Used to create usage information for
 * particular options.
 */
typedef struct {
    char const * pzStr;
    char const * pzReq;
    char const * pzNum;
    char const * pzFile;
    char const * pzKey;
    char const * pzKeyL;
    char const * pzBool;
    char const * pzNest;
    char const * pzOpt;
    char const * pzNo;
    char const * pzBrk;
    char const * pzNoF;
    char const * pzSpc;
    char const * pzOptFmt;
    char const * pzTime;
} arg_types_t;

#define AGALOC(_c, _w)        ao_malloc((size_t)_c)
#define AGREALOC(_p, _c, _w)  ao_realloc(VOIDP(_p), (size_t)_c)
#define AGFREE(_p)            free(VOIDP(_p))
#define AGDUPSTR(_p, _s, _w)  (_p = ao_strdup(_s))

static void *
ao_malloc(size_t sz);

static void *
ao_realloc(void *p, size_t sz);

#define ao_free(_p) free(VOIDP(_p))

static char *
ao_strdup(char const * str);

/**
 *  DO option handling?
 *
 *  Options are examined at two times:  at immediate handling time and at
 *  normal handling time.  If an option is disabled, the timing may be
 *  different from the handling of the undisabled option.  The OPTST_DIABLED
 *  bit indicates the state of the currently discovered option.
 *  So, here's how it works:
 *
 *  A) handling at "immediate" time, either 1 or 2:
 *
 *  1.  OPTST_DISABLED is not set:
 *      IMM           must be set
 *      DISABLE_IMM   don't care
 *      TWICE         don't care
 *      DISABLE_TWICE don't care
 *      0 -and-  1 x x x
 *
 *  2.  OPTST_DISABLED is set:
 *      IMM           don't care
 *      DISABLE_IMM   must be set
 *      TWICE         don't care
 *      DISABLE_TWICE don't care
 *      1 -and-  x 1 x x
 */
#define DO_IMMEDIATELY(_flg) \
    (  (((_flg) & (OPTST_DISABLED|OPTST_IMM)) == OPTST_IMM) \
    || (   ((_flg) & (OPTST_DISABLED|OPTST_DISABLE_IMM))    \
        == (OPTST_DISABLED|OPTST_DISABLE_IMM)  ))

/**
 *  B) handling at "regular" time because it was not immediate
 *
 *  1.  OPTST_DISABLED is not set:
 *      IMM           must *NOT* be set
 *      DISABLE_IMM   don't care
 *      TWICE         don't care
 *      DISABLE_TWICE don't care
 *      0 -and-  0 x x x
 *
 *  2.  OPTST_DISABLED is set:
 *      IMM           don't care
 *      DISABLE_IMM   don't care
 *      TWICE         must be set
 *      DISABLE_TWICE don't care
 *      1 -and-  x x 1 x
 */
#define DO_NORMALLY(_flg) ( \
       (((_flg) & (OPTST_DISABLED|OPTST_IMM))            == 0)  \
    || (((_flg) & (OPTST_DISABLED|OPTST_DISABLE_IMM))    ==     \
                  OPTST_DISABLED)  )

/**
 *  C)  handling at "regular" time because it is to be handled twice.
 *      The immediate bit was already tested and found to be set:
 *
 *  3.  OPTST_DISABLED is not set:
 *      IMM           is set (but don't care)
 *      DISABLE_IMM   don't care
 *      TWICE         must be set
 *      DISABLE_TWICE don't care
 *      0 -and-  ? x 1 x
 *
 *  4.  OPTST_DISABLED is set:
 *      IMM           don't care
 *      DISABLE_IMM   is set (but don't care)
 *      TWICE         don't care
 *      DISABLE_TWICE must be set
 *      1 -and-  x ? x 1
 */
#define DO_SECOND_TIME(_flg) ( \
       (((_flg) & (OPTST_DISABLED|OPTST_TWICE))          ==     \
                  OPTST_TWICE)                                  \
    || (((_flg) & (OPTST_DISABLED|OPTST_DISABLE_TWICE))  ==     \
                  (OPTST_DISABLED|OPTST_DISABLE_TWICE)  ))

/*
 *  text_mmap structure.  Only active on platforms with mmap(2).
 */
#ifdef HAVE_SYS_MMAN_H
#  include <sys/mman.h>
#else
#  ifndef  PROT_READ
#   define PROT_READ            0x01
#  endif
#  ifndef  PROT_WRITE
#   define PROT_WRITE           0x02
#  endif
#  ifndef  MAP_SHARED
#   define MAP_SHARED           0x01
#  endif
#  ifndef  MAP_PRIVATE
#   define MAP_PRIVATE          0x02
#  endif
#endif

#ifndef MAP_FAILED
#  define  MAP_FAILED           VOIDP(-1)
#endif

#ifndef  _SC_PAGESIZE
# ifdef  _SC_PAGE_SIZE
#  define _SC_PAGESIZE          _SC_PAGE_SIZE
# endif
#endif

#ifndef HAVE_STRCHR
extern char * strchr(char const * s, int c);
extern char * strrchr(char const * s, int c);
#endif

/**
 * INQUERY_CALL() tests whether the option handling function has been
 * called by an inquery (help text needed, or option being reset),
 * or called by a set-the-option operation.
 */
#define INQUERY_CALL(_o, _d) (                  \
    ((_o) <= OPTPROC_EMIT_LIMIT)                \
    || ((_d) == NULL)                           \
    || (((_d)->fOptState & OPTST_RESET) != 0) )

/**
 *  Define and initialize all the user visible strings.
 *  We do not do translations.  If translations are to be done, then
 *  the client will provide a callback for that purpose.
 */
#undef DO_TRANSLATIONS
#include "autoopts/usage-txt.h"

/**
 *  File pointer for usage output
 */
FILE * option_usage_fp;
/**
 *  If provided in the option structure
 */
static char const * program_pkgdatadir;
/**
 * privately exported functions
 */
extern tOptProc optionPrintVersion, optionPagedUsage, optionLoadOpt;

#ifdef AUTOOPTS_INTERNAL

#ifndef PKGDATADIR
#  define PKGDATADIR ""
#endif
#define APOSTROPHE '\''

#define OPTPROC_L_N_S  (OPTPROC_LONGOPT | OPTPROC_SHORTOPT)
#if defined(ENABLE_NLS) && defined(HAVE_LIBINTL_H)
# include <libintl.h>
#endif

typedef struct {
    size_t          fnm_len;
    uint32_t        fnm_mask;
    char const *    fnm_name;
} ao_flag_names_t;

/**
 * Automated Options Usage Flags.
 * NB: no entry may be a prefix of another entry
 */
#define AOFLAG_TABLE                            \
    _aof_(gnu,             OPTPROC_GNUUSAGE )   \
    _aof_(autoopts,        ~OPTPROC_GNUUSAGE)   \
    _aof_(no_misuse_usage, OPTPROC_MISUSE   )   \
    _aof_(misuse_usage,    ~OPTPROC_MISUSE  )   \
    _aof_(compute,         OPTPROC_COMPUTE  )

#define _aof_(_n, _f)   AOUF_ ## _n ## _ID,
typedef enum { AOFLAG_TABLE AOUF_COUNT } ao_flag_id_t;
#undef  _aof_

#define _aof_(_n, _f)   AOUF_ ## _n = (1 << AOUF_ ## _n ## _ID),
typedef enum { AOFLAG_TABLE } ao_flags_t;
#undef  _aof_

static char const   zNil[] = "";
static arg_types_t  argTypes             = { NULL };
static char         line_fmt_buf[32];
static bool         displayEnum          = false;
static char const   pkgdatadir_default[] = PKGDATADIR;
static char const * program_pkgdatadir   = pkgdatadir_default;
static tOptionLoadMode option_load_mode  = OPTION_LOAD_UNCOOKED;
static tePagerState pagerState           = PAGER_STATE_INITIAL;

       FILE *       option_usage_fp      = NULL;

static char const * pz_enum_err_fmt;

tOptions * optionParseShellOptions = NULL;

static char const * shell_prog = NULL;
static char * script_leader    = NULL;
static char * script_trailer   = NULL;
static char * script_text      = NULL;
static bool   print_exit       = false;
#endif /* AUTOOPTS_INTERNAL */

#endif /* AUTOGEN_AUTOOPTS_H */
/**
 * @}
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/autoopts.h */
