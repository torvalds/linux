/*
 *  EDIT THIS FILE WITH CAUTION  (ntp-keygen-opts.h)
 *
 *  It has been AutoGen-ed  February 20, 2019 at 09:57:09 AM by AutoGen 5.18.5
 *  From the definitions    ntp-keygen-opts.def
 *  and the template file   options
 *
 * Generated from AutoOpts 41:1:16 templates.
 *
 *  AutoOpts is a copyrighted work.  This header file is not encumbered
 *  by AutoOpts licensing, but is provided under the licensing terms chosen
 *  by the ntp-keygen author or copyright holder.  AutoOpts is
 *  licensed under the terms of the LGPL.  The redistributable library
 *  (``libopts'') is licensed under the terms of either the LGPL or, at the
 *  users discretion, the BSD license.  See the AutoOpts and/or libopts sources
 *  for details.
 *
 * The ntp-keygen program is copyrighted and licensed
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
 *  Options generated for the ntp-keygen program.
 *  These macros are documented in the AutoGen info file in the
 *  "AutoOpts" chapter.  Please refer to that doc for usage help.
 */
#ifndef AUTOOPTS_NTP_KEYGEN_OPTS_H_GUARD
#define AUTOOPTS_NTP_KEYGEN_OPTS_H_GUARD 1
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
 *  Enumeration of each option type for ntp-keygen
 */
typedef enum {
    INDEX_OPT_IMBITS           =  0,
    INDEX_OPT_CERTIFICATE      =  1,
    INDEX_OPT_CIPHER           =  2,
    INDEX_OPT_DEBUG_LEVEL      =  3,
    INDEX_OPT_SET_DEBUG_LEVEL  =  4,
    INDEX_OPT_ID_KEY           =  5,
    INDEX_OPT_GQ_PARAMS        =  6,
    INDEX_OPT_HOST_KEY         =  7,
    INDEX_OPT_IFFKEY           =  8,
    INDEX_OPT_IDENT            =  9,
    INDEX_OPT_LIFETIME         = 10,
    INDEX_OPT_MODULUS          = 11,
    INDEX_OPT_MD5KEY           = 12,
    INDEX_OPT_PVT_CERT         = 13,
    INDEX_OPT_PASSWORD         = 14,
    INDEX_OPT_EXPORT_PASSWD    = 15,
    INDEX_OPT_SUBJECT_NAME     = 16,
    INDEX_OPT_SIGN_KEY         = 17,
    INDEX_OPT_TRUSTED_CERT     = 18,
    INDEX_OPT_MV_PARAMS        = 19,
    INDEX_OPT_MV_KEYS          = 20,
    INDEX_OPT_VERSION          = 21,
    INDEX_OPT_HELP             = 22,
    INDEX_OPT_MORE_HELP        = 23,
    INDEX_OPT_SAVE_OPTS        = 24,
    INDEX_OPT_LOAD_OPTS        = 25
} teOptIndex;
/** count of all options for ntp-keygen */
#define OPTION_CT    26
/** ntp-keygen version */
#define NTP_KEYGEN_VERSION       "4.2.8p13"
/** Full ntp-keygen version text */
#define NTP_KEYGEN_FULL_VERSION  "ntp-keygen (ntp) 4.2.8p13"

/**
 *  Interface defines for all options.  Replace "n" with the UPPER_CASED
 *  option name (as in the teOptIndex enumeration above).
 *  e.g. HAVE_OPT(IMBITS)
 */
#define         DESC(n) (ntp_keygenOptions.pOptDesc[INDEX_OPT_## n])
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
 *  Enumeration of ntp-keygen exit codes
 */
typedef enum {
    NTP_KEYGEN_EXIT_SUCCESS         = 0,
    NTP_KEYGEN_EXIT_FAILURE         = 1,
    NTP_KEYGEN_EXIT_USAGE_ERROR     = 64,
    NTP_KEYGEN_EXIT_NO_CONFIG_INPUT = 66,
    NTP_KEYGEN_EXIT_LIBOPTS_FAILURE = 70
}   ntp_keygen_exit_code_t;
/** @} */
/**
 *  Make sure there are no #define name conflicts with the option names
 */
#ifndef     NO_OPTION_NAME_WARNINGS
# ifdef    IMBITS
#  warning undefining IMBITS due to option name conflict
#  undef   IMBITS
# endif
# ifdef    CERTIFICATE
#  warning undefining CERTIFICATE due to option name conflict
#  undef   CERTIFICATE
# endif
# ifdef    CIPHER
#  warning undefining CIPHER due to option name conflict
#  undef   CIPHER
# endif
# ifdef    DEBUG_LEVEL
#  warning undefining DEBUG_LEVEL due to option name conflict
#  undef   DEBUG_LEVEL
# endif
# ifdef    SET_DEBUG_LEVEL
#  warning undefining SET_DEBUG_LEVEL due to option name conflict
#  undef   SET_DEBUG_LEVEL
# endif
# ifdef    ID_KEY
#  warning undefining ID_KEY due to option name conflict
#  undef   ID_KEY
# endif
# ifdef    GQ_PARAMS
#  warning undefining GQ_PARAMS due to option name conflict
#  undef   GQ_PARAMS
# endif
# ifdef    HOST_KEY
#  warning undefining HOST_KEY due to option name conflict
#  undef   HOST_KEY
# endif
# ifdef    IFFKEY
#  warning undefining IFFKEY due to option name conflict
#  undef   IFFKEY
# endif
# ifdef    IDENT
#  warning undefining IDENT due to option name conflict
#  undef   IDENT
# endif
# ifdef    LIFETIME
#  warning undefining LIFETIME due to option name conflict
#  undef   LIFETIME
# endif
# ifdef    MODULUS
#  warning undefining MODULUS due to option name conflict
#  undef   MODULUS
# endif
# ifdef    MD5KEY
#  warning undefining MD5KEY due to option name conflict
#  undef   MD5KEY
# endif
# ifdef    PVT_CERT
#  warning undefining PVT_CERT due to option name conflict
#  undef   PVT_CERT
# endif
# ifdef    PASSWORD
#  warning undefining PASSWORD due to option name conflict
#  undef   PASSWORD
# endif
# ifdef    EXPORT_PASSWD
#  warning undefining EXPORT_PASSWD due to option name conflict
#  undef   EXPORT_PASSWD
# endif
# ifdef    SUBJECT_NAME
#  warning undefining SUBJECT_NAME due to option name conflict
#  undef   SUBJECT_NAME
# endif
# ifdef    SIGN_KEY
#  warning undefining SIGN_KEY due to option name conflict
#  undef   SIGN_KEY
# endif
# ifdef    TRUSTED_CERT
#  warning undefining TRUSTED_CERT due to option name conflict
#  undef   TRUSTED_CERT
# endif
# ifdef    MV_PARAMS
#  warning undefining MV_PARAMS due to option name conflict
#  undef   MV_PARAMS
# endif
# ifdef    MV_KEYS
#  warning undefining MV_KEYS due to option name conflict
#  undef   MV_KEYS
# endif
#else  /* NO_OPTION_NAME_WARNINGS */
# undef IMBITS
# undef CERTIFICATE
# undef CIPHER
# undef DEBUG_LEVEL
# undef SET_DEBUG_LEVEL
# undef ID_KEY
# undef GQ_PARAMS
# undef HOST_KEY
# undef IFFKEY
# undef IDENT
# undef LIFETIME
# undef MODULUS
# undef MD5KEY
# undef PVT_CERT
# undef PASSWORD
# undef EXPORT_PASSWD
# undef SUBJECT_NAME
# undef SIGN_KEY
# undef TRUSTED_CERT
# undef MV_PARAMS
# undef MV_KEYS
#endif  /*  NO_OPTION_NAME_WARNINGS */

/**
 *  Interface defines for specific options.
 * @{
 */
#define VALUE_OPT_IMBITS         'b'
#ifdef AUTOKEY
#define OPT_VALUE_IMBITS         (DESC(IMBITS).optArg.argInt)
#endif /* AUTOKEY */
#define VALUE_OPT_CERTIFICATE    'c'
#define VALUE_OPT_CIPHER         'C'
#define VALUE_OPT_DEBUG_LEVEL    'd'
#define VALUE_OPT_SET_DEBUG_LEVEL 'D'

#define OPT_VALUE_SET_DEBUG_LEVEL (DESC(SET_DEBUG_LEVEL).optArg.argInt)
#define VALUE_OPT_ID_KEY         'e'
#define VALUE_OPT_GQ_PARAMS      'G'
#define VALUE_OPT_HOST_KEY       'H'
#define VALUE_OPT_IFFKEY         'I'
#define VALUE_OPT_IDENT          'i'
#define VALUE_OPT_LIFETIME       'l'
#ifdef AUTOKEY
#define OPT_VALUE_LIFETIME       (DESC(LIFETIME).optArg.argInt)
#endif /* AUTOKEY */
#define VALUE_OPT_MODULUS        'm'
#ifdef AUTOKEY
#define OPT_VALUE_MODULUS        (DESC(MODULUS).optArg.argInt)
#endif /* AUTOKEY */
#define VALUE_OPT_MD5KEY         'M'
#define VALUE_OPT_PVT_CERT       'P'
#define VALUE_OPT_PASSWORD       'p'
#define VALUE_OPT_EXPORT_PASSWD  'q'
#define VALUE_OPT_SUBJECT_NAME   's'
#define VALUE_OPT_SIGN_KEY       'S'
#define VALUE_OPT_TRUSTED_CERT   'T'
#define VALUE_OPT_MV_PARAMS      'V'
#ifdef AUTOKEY
#define OPT_VALUE_MV_PARAMS      (DESC(MV_PARAMS).optArg.argInt)
#endif /* AUTOKEY */
#define VALUE_OPT_MV_KEYS        'v'
#ifdef AUTOKEY
#define OPT_VALUE_MV_KEYS        (DESC(MV_KEYS).optArg.argInt)
#endif /* AUTOKEY */
/** option flag (value) for help-value option */
#define VALUE_OPT_HELP          '?'
/** option flag (value) for more-help-value option */
#define VALUE_OPT_MORE_HELP     '!'
/** option flag (value) for version-value option */
#define VALUE_OPT_VERSION       0x1001
/** option flag (value) for save-opts-value option */
#define VALUE_OPT_SAVE_OPTS     '>'
/** option flag (value) for load-opts-value option */
#define VALUE_OPT_LOAD_OPTS     '<'
#define SET_OPT_SAVE_OPTS(a)   STMTS( \
        DESC(SAVE_OPTS).fOptState &= OPTST_PERSISTENT_MASK; \
        DESC(SAVE_OPTS).fOptState |= OPTST_SET; \
        DESC(SAVE_OPTS).optArg.argString = (char const*)(a))
/*
 *  Interface defines not associated with particular options
 */
#define ERRSKIP_OPTERR  STMTS(ntp_keygenOptions.fOptSet &= ~OPTPROC_ERRSTOP)
#define ERRSTOP_OPTERR  STMTS(ntp_keygenOptions.fOptSet |= OPTPROC_ERRSTOP)
#define RESTART_OPT(n)  STMTS( \
                ntp_keygenOptions.curOptIdx = (n); \
                ntp_keygenOptions.pzCurOpt  = NULL )
#define START_OPT       RESTART_OPT(1)
#define USAGE(c)        (*ntp_keygenOptions.pUsageProc)(&ntp_keygenOptions, c)

#ifdef  __cplusplus
extern "C" {
#endif


/* * * * * *
 *
 *  Declare the ntp-keygen option descriptor.
 */
extern tOptions ntp_keygenOptions;

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

# define OPT_NO_XLAT_CFG_NAMES  STMTS(ntp_keygenOptions.fOptSet |= \
                                    OPTPROC_NXLAT_OPT_CFG;)
# define OPT_NO_XLAT_OPT_NAMES  STMTS(ntp_keygenOptions.fOptSet |= \
                                    OPTPROC_NXLAT_OPT|OPTPROC_NXLAT_OPT_CFG;)

# define OPT_XLAT_CFG_NAMES     STMTS(ntp_keygenOptions.fOptSet &= \
                                  ~(OPTPROC_NXLAT_OPT|OPTPROC_NXLAT_OPT_CFG);)
# define OPT_XLAT_OPT_NAMES     STMTS(ntp_keygenOptions.fOptSet &= \
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
#endif /* AUTOOPTS_NTP_KEYGEN_OPTS_H_GUARD */

/* ntp-keygen-opts.h ends here */
