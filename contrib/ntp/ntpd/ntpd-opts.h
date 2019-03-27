/*
 *  EDIT THIS FILE WITH CAUTION  (ntpd-opts.h)
 *
 *  It has been AutoGen-ed  February 20, 2019 at 09:56:15 AM by AutoGen 5.18.5
 *  From the definitions    ntpd-opts.def
 *  and the template file   options
 *
 * Generated from AutoOpts 41:1:16 templates.
 *
 *  AutoOpts is a copyrighted work.  This header file is not encumbered
 *  by AutoOpts licensing, but is provided under the licensing terms chosen
 *  by the ntpd author or copyright holder.  AutoOpts is
 *  licensed under the terms of the LGPL.  The redistributable library
 *  (``libopts'') is licensed under the terms of either the LGPL or, at the
 *  users discretion, the BSD license.  See the AutoOpts and/or libopts sources
 *  for details.
 *
 * The ntpd program is copyrighted and licensed
 * under the following terms:
 *
 *  Copyright (C) 1992-2017 The University of Delaware and Network Time Foundation, all rights reserved.
 *  This is free software. It is licensed for use, modification and
 *  redistribution under the terms of the NTP License, copies of which
 *  can be seen at:
 *    <http://ntp.org/license>
 *    <http://opensource.org/licenses/ntp-license.php>
 *
 *  Permission to use, copy, modify, and distribute this software and its
 *  documentation for any purpose with or without fee is hereby granted,
 *  provided that the above copyright notice appears in all copies and that
 *  both the copyright notice and this permission notice appear in
 *  supporting documentation, and that the name The University of Delaware not be used in
 *  advertising or publicity pertaining to distribution of the software
 *  without specific, written prior permission. The University of Delaware and Network Time Foundation makes no
 *  representations about the suitability this software for any purpose. It
 *  is provided "as is" without express or implied warranty.
 */
/**
 *  This file contains the programmatic interface to the Automated
 *  Options generated for the ntpd program.
 *  These macros are documented in the AutoGen info file in the
 *  "AutoOpts" chapter.  Please refer to that doc for usage help.
 */
#ifndef AUTOOPTS_NTPD_OPTS_H_GUARD
#define AUTOOPTS_NTPD_OPTS_H_GUARD 1
#include "config.h"
#include <autoopts/options.h>

/**
 *  Ensure that the library used for compiling this generated header is at
 *  least as new as the version current when the header template was released
 *  (not counting patch version increments).  Also ensure that the oldest
 *  tolerable version is at least as old as what was current when the header
 *  template was released.
 */
#define AO_TEMPLATE_VERSION 167937
#if (AO_TEMPLATE_VERSION < OPTIONS_MINIMUM_VERSION) \
 || (AO_TEMPLATE_VERSION > OPTIONS_STRUCT_VERSION)
# error option template version mismatches autoopts/options.h header
  Choke Me.
#endif

/**
 *  Enumeration of each option type for ntpd
 */
typedef enum {
    INDEX_OPT_IPV4              =  0,
    INDEX_OPT_IPV6              =  1,
    INDEX_OPT_AUTHREQ           =  2,
    INDEX_OPT_AUTHNOREQ         =  3,
    INDEX_OPT_BCASTSYNC         =  4,
    INDEX_OPT_CONFIGFILE        =  5,
    INDEX_OPT_DEBUG_LEVEL       =  6,
    INDEX_OPT_SET_DEBUG_LEVEL   =  7,
    INDEX_OPT_DRIFTFILE         =  8,
    INDEX_OPT_PANICGATE         =  9,
    INDEX_OPT_FORCE_STEP_ONCE   = 10,
    INDEX_OPT_JAILDIR           = 11,
    INDEX_OPT_INTERFACE         = 12,
    INDEX_OPT_KEYFILE           = 13,
    INDEX_OPT_LOGFILE           = 14,
    INDEX_OPT_NOVIRTUALIPS      = 15,
    INDEX_OPT_MODIFYMMTIMER     = 16,
    INDEX_OPT_NOFORK            = 17,
    INDEX_OPT_NICE              = 18,
    INDEX_OPT_PIDFILE           = 19,
    INDEX_OPT_PRIORITY          = 20,
    INDEX_OPT_QUIT              = 21,
    INDEX_OPT_PROPAGATIONDELAY  = 22,
    INDEX_OPT_SAVECONFIGQUIT    = 23,
    INDEX_OPT_STATSDIR          = 24,
    INDEX_OPT_TRUSTEDKEY        = 25,
    INDEX_OPT_USER              = 26,
    INDEX_OPT_UPDATEINTERVAL    = 27,
    INDEX_OPT_VAR               = 28,
    INDEX_OPT_DVAR              = 29,
    INDEX_OPT_WAIT_SYNC         = 30,
    INDEX_OPT_SLEW              = 31,
    INDEX_OPT_USEPCC            = 32,
    INDEX_OPT_PCCFREQ           = 33,
    INDEX_OPT_MDNS              = 34,
    INDEX_OPT_VERSION           = 35,
    INDEX_OPT_HELP              = 36,
    INDEX_OPT_MORE_HELP         = 37
} teOptIndex;
/** count of all options for ntpd */
#define OPTION_CT    38
/** ntpd version */
#define NTPD_VERSION       "4.2.8p13"
/** Full ntpd version text */
#define NTPD_FULL_VERSION  "ntpd 4.2.8p13"

/**
 *  Interface defines for all options.  Replace "n" with the UPPER_CASED
 *  option name (as in the teOptIndex enumeration above).
 *  e.g. HAVE_OPT(IPV4)
 */
#define         DESC(n) (ntpdOptions.pOptDesc[INDEX_OPT_## n])
/** 'true' if an option has been specified in any way */
#define     HAVE_OPT(n) (! UNUSED_OPT(& DESC(n)))
/** The string argument to an option. The argument type must be \"string\". */
#define      OPT_ARG(n) (DESC(n).optArg.argString)
/** Mask the option state revealing how an option was specified.
 *  It will be one and only one of \a OPTST_SET, \a OPTST_PRESET,
 * \a OPTST_DEFINED, \a OPTST_RESET or zero.
 */
#define    STATE_OPT(n) (DESC(n).fOptState & OPTST_SET_MASK)
/** Count of option's occurrances *on the command line*. */
#define    COUNT_OPT(n) (DESC(n).optOccCt)
/** mask of \a OPTST_SET and \a OPTST_DEFINED. */
#define    ISSEL_OPT(n) (SELECTED_OPT(&DESC(n)))
/** 'true' if \a HAVE_OPT would yield 'false'. */
#define ISUNUSED_OPT(n) (UNUSED_OPT(& DESC(n)))
/** 'true' if OPTST_DISABLED bit not set. */
#define  ENABLED_OPT(n) (! DISABLED_OPT(& DESC(n)))
/** number of stacked option arguments.
 *  Valid only for stacked option arguments. */
#define  STACKCT_OPT(n) (((tArgList*)(DESC(n).optCookie))->useCt)
/** stacked argument vector.
 *  Valid only for stacked option arguments. */
#define STACKLST_OPT(n) (((tArgList*)(DESC(n).optCookie))->apzArgs)
/** Reset an option. */
#define    CLEAR_OPT(n) STMTS( \
                DESC(n).fOptState &= OPTST_PERSISTENT_MASK;   \
                if ((DESC(n).fOptState & OPTST_INITENABLED) == 0) \
                    DESC(n).fOptState |= OPTST_DISABLED; \
                DESC(n).optCookie = NULL )
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 *  Enumeration of ntpd exit codes
 */
typedef enum {
    NTPD_EXIT_SUCCESS         = 0,
    NTPD_EXIT_FAILURE         = 1,
    NTPD_EXIT_USAGE_ERROR     = 64,
    NTPD_EXIT_LIBOPTS_FAILURE = 70
}   ntpd_exit_code_t;
/** @} */
/**
 *  Make sure there are no #define name conflicts with the option names
 */
#ifndef     NO_OPTION_NAME_WARNINGS
# ifdef    IPV4
#  warning undefining IPV4 due to option name conflict
#  undef   IPV4
# endif
# ifdef    IPV6
#  warning undefining IPV6 due to option name conflict
#  undef   IPV6
# endif
# ifdef    AUTHREQ
#  warning undefining AUTHREQ due to option name conflict
#  undef   AUTHREQ
# endif
# ifdef    AUTHNOREQ
#  warning undefining AUTHNOREQ due to option name conflict
#  undef   AUTHNOREQ
# endif
# ifdef    BCASTSYNC
#  warning undefining BCASTSYNC due to option name conflict
#  undef   BCASTSYNC
# endif
# ifdef    CONFIGFILE
#  warning undefining CONFIGFILE due to option name conflict
#  undef   CONFIGFILE
# endif
# ifdef    DEBUG_LEVEL
#  warning undefining DEBUG_LEVEL due to option name conflict
#  undef   DEBUG_LEVEL
# endif
# ifdef    SET_DEBUG_LEVEL
#  warning undefining SET_DEBUG_LEVEL due to option name conflict
#  undef   SET_DEBUG_LEVEL
# endif
# ifdef    DRIFTFILE
#  warning undefining DRIFTFILE due to option name conflict
#  undef   DRIFTFILE
# endif
# ifdef    PANICGATE
#  warning undefining PANICGATE due to option name conflict
#  undef   PANICGATE
# endif
# ifdef    FORCE_STEP_ONCE
#  warning undefining FORCE_STEP_ONCE due to option name conflict
#  undef   FORCE_STEP_ONCE
# endif
# ifdef    JAILDIR
#  warning undefining JAILDIR due to option name conflict
#  undef   JAILDIR
# endif
# ifdef    INTERFACE
#  warning undefining INTERFACE due to option name conflict
#  undef   INTERFACE
# endif
# ifdef    KEYFILE
#  warning undefining KEYFILE due to option name conflict
#  undef   KEYFILE
# endif
# ifdef    LOGFILE
#  warning undefining LOGFILE due to option name conflict
#  undef   LOGFILE
# endif
# ifdef    NOVIRTUALIPS
#  warning undefining NOVIRTUALIPS due to option name conflict
#  undef   NOVIRTUALIPS
# endif
# ifdef    MODIFYMMTIMER
#  warning undefining MODIFYMMTIMER due to option name conflict
#  undef   MODIFYMMTIMER
# endif
# ifdef    NOFORK
#  warning undefining NOFORK due to option name conflict
#  undef   NOFORK
# endif
# ifdef    NICE
#  warning undefining NICE due to option name conflict
#  undef   NICE
# endif
# ifdef    PIDFILE
#  warning undefining PIDFILE due to option name conflict
#  undef   PIDFILE
# endif
# ifdef    PRIORITY
#  warning undefining PRIORITY due to option name conflict
#  undef   PRIORITY
# endif
# ifdef    QUIT
#  warning undefining QUIT due to option name conflict
#  undef   QUIT
# endif
# ifdef    PROPAGATIONDELAY
#  warning undefining PROPAGATIONDELAY due to option name conflict
#  undef   PROPAGATIONDELAY
# endif
# ifdef    SAVECONFIGQUIT
#  warning undefining SAVECONFIGQUIT due to option name conflict
#  undef   SAVECONFIGQUIT
# endif
# ifdef    STATSDIR
#  warning undefining STATSDIR due to option name conflict
#  undef   STATSDIR
# endif
# ifdef    TRUSTEDKEY
#  warning undefining TRUSTEDKEY due to option name conflict
#  undef   TRUSTEDKEY
# endif
# ifdef    USER
#  warning undefining USER due to option name conflict
#  undef   USER
# endif
# ifdef    UPDATEINTERVAL
#  warning undefining UPDATEINTERVAL due to option name conflict
#  undef   UPDATEINTERVAL
# endif
# ifdef    VAR
#  warning undefining VAR due to option name conflict
#  undef   VAR
# endif
# ifdef    DVAR
#  warning undefining DVAR due to option name conflict
#  undef   DVAR
# endif
# ifdef    WAIT_SYNC
#  warning undefining WAIT_SYNC due to option name conflict
#  undef   WAIT_SYNC
# endif
# ifdef    SLEW
#  warning undefining SLEW due to option name conflict
#  undef   SLEW
# endif
# ifdef    USEPCC
#  warning undefining USEPCC due to option name conflict
#  undef   USEPCC
# endif
# ifdef    PCCFREQ
#  warning undefining PCCFREQ due to option name conflict
#  undef   PCCFREQ
# endif
# ifdef    MDNS
#  warning undefining MDNS due to option name conflict
#  undef   MDNS
# endif
#else  /* NO_OPTION_NAME_WARNINGS */
# undef IPV4
# undef IPV6
# undef AUTHREQ
# undef AUTHNOREQ
# undef BCASTSYNC
# undef CONFIGFILE
# undef DEBUG_LEVEL
# undef SET_DEBUG_LEVEL
# undef DRIFTFILE
# undef PANICGATE
# undef FORCE_STEP_ONCE
# undef JAILDIR
# undef INTERFACE
# undef KEYFILE
# undef LOGFILE
# undef NOVIRTUALIPS
# undef MODIFYMMTIMER
# undef NOFORK
# undef NICE
# undef PIDFILE
# undef PRIORITY
# undef QUIT
# undef PROPAGATIONDELAY
# undef SAVECONFIGQUIT
# undef STATSDIR
# undef TRUSTEDKEY
# undef USER
# undef UPDATEINTERVAL
# undef VAR
# undef DVAR
# undef WAIT_SYNC
# undef SLEW
# undef USEPCC
# undef PCCFREQ
# undef MDNS
#endif  /*  NO_OPTION_NAME_WARNINGS */

/**
 *  Interface defines for specific options.
 * @{
 */
#define VALUE_OPT_IPV4           '4'
#define VALUE_OPT_IPV6           '6'
#define VALUE_OPT_AUTHREQ        'a'
#define VALUE_OPT_AUTHNOREQ      'A'
#define VALUE_OPT_BCASTSYNC      'b'
#define VALUE_OPT_CONFIGFILE     'c'
#define VALUE_OPT_DEBUG_LEVEL    'd'
#define VALUE_OPT_SET_DEBUG_LEVEL 'D'

#define OPT_VALUE_SET_DEBUG_LEVEL (DESC(SET_DEBUG_LEVEL).optArg.argInt)
#define VALUE_OPT_DRIFTFILE      'f'
#define VALUE_OPT_PANICGATE      'g'
#define VALUE_OPT_FORCE_STEP_ONCE 'G'
#define VALUE_OPT_JAILDIR        'i'
#define VALUE_OPT_INTERFACE      'I'
#define VALUE_OPT_KEYFILE        'k'
#define VALUE_OPT_LOGFILE        'l'
#define VALUE_OPT_NOVIRTUALIPS   'L'
#define VALUE_OPT_MODIFYMMTIMER  'M'
#define VALUE_OPT_NOFORK         'n'
#define VALUE_OPT_NICE           'N'
#define VALUE_OPT_PIDFILE        'p'
#define VALUE_OPT_PRIORITY       'P'

#define OPT_VALUE_PRIORITY       (DESC(PRIORITY).optArg.argInt)
#define VALUE_OPT_QUIT           'q'
#define VALUE_OPT_PROPAGATIONDELAY 'r'
#define VALUE_OPT_SAVECONFIGQUIT 0x1001
#define VALUE_OPT_STATSDIR       's'
#define VALUE_OPT_TRUSTEDKEY     't'
#define VALUE_OPT_USER           'u'
#define VALUE_OPT_UPDATEINTERVAL 'U'

#define OPT_VALUE_UPDATEINTERVAL (DESC(UPDATEINTERVAL).optArg.argInt)
#define VALUE_OPT_VAR            0x1002
#define VALUE_OPT_DVAR           0x1003
#define VALUE_OPT_WAIT_SYNC      'w'
#ifdef HAVE_WORKING_FORK
#define OPT_VALUE_WAIT_SYNC      (DESC(WAIT_SYNC).optArg.argInt)
#endif /* HAVE_WORKING_FORK */
#define VALUE_OPT_SLEW           'x'
#define VALUE_OPT_USEPCC         0x1004
#define VALUE_OPT_PCCFREQ        0x1005
#define VALUE_OPT_MDNS           'm'
/** option flag (value) for help-value option */
#define VALUE_OPT_HELP          '?'
/** option flag (value) for more-help-value option */
#define VALUE_OPT_MORE_HELP     '!'
/** option flag (value) for version-value option */
#define VALUE_OPT_VERSION       0x1006
/*
 *  Interface defines not associated with particular options
 */
#define ERRSKIP_OPTERR  STMTS(ntpdOptions.fOptSet &= ~OPTPROC_ERRSTOP)
#define ERRSTOP_OPTERR  STMTS(ntpdOptions.fOptSet |= OPTPROC_ERRSTOP)
#define RESTART_OPT(n)  STMTS( \
                ntpdOptions.curOptIdx = (n); \
                ntpdOptions.pzCurOpt  = NULL )
#define START_OPT       RESTART_OPT(1)
#define USAGE(c)        (*ntpdOptions.pUsageProc)(&ntpdOptions, c)

#ifdef  __cplusplus
extern "C" {
#endif


/* * * * * *
 *
 *  Declare the ntpd option descriptor.
 */
extern tOptions ntpdOptions;

#if defined(ENABLE_NLS)
# ifndef _
#   include <stdio.h>
#   ifndef HAVE_GETTEXT
      extern char * gettext(char const *);
#   else
#     include <libintl.h>
#   endif

# ifndef ATTRIBUTE_FORMAT_ARG
#   define ATTRIBUTE_FORMAT_ARG(_a)
# endif

static inline char* aoGetsText(char const* pz) ATTRIBUTE_FORMAT_ARG(1);
static inline char* aoGetsText(char const* pz) {
    if (pz == NULL) return NULL;
    return (char*)gettext(pz);
}
#   define _(s)  aoGetsText(s)
# endif /* _() */

# define OPT_NO_XLAT_CFG_NAMES  STMTS(ntpdOptions.fOptSet |= \
                                    OPTPROC_NXLAT_OPT_CFG;)
# define OPT_NO_XLAT_OPT_NAMES  STMTS(ntpdOptions.fOptSet |= \
                                    OPTPROC_NXLAT_OPT|OPTPROC_NXLAT_OPT_CFG;)

# define OPT_XLAT_CFG_NAMES     STMTS(ntpdOptions.fOptSet &= \
                                  ~(OPTPROC_NXLAT_OPT|OPTPROC_NXLAT_OPT_CFG);)
# define OPT_XLAT_OPT_NAMES     STMTS(ntpdOptions.fOptSet &= \
                                  ~OPTPROC_NXLAT_OPT;)

#else   /* ENABLE_NLS */
# define OPT_NO_XLAT_CFG_NAMES
# define OPT_NO_XLAT_OPT_NAMES

# define OPT_XLAT_CFG_NAMES
# define OPT_XLAT_OPT_NAMES

# ifndef _
#   define _(_s)  _s
# endif
#endif  /* ENABLE_NLS */

#ifdef  __cplusplus
}
#endif
#endif /* AUTOOPTS_NTPD_OPTS_H_GUARD */

/* ntpd-opts.h ends here */
