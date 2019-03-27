/*
 *  EDIT THIS FILE WITH CAUTION  (ntp-keygen-opts.c)
 *
 *  It has been AutoGen-ed  February 20, 2019 at 09:57:09 AM by AutoGen 5.18.5
 *  From the definitions    ntp-keygen-opts.def
 *  and the template file   options
 *
 * Generated from AutoOpts 41:1:16 templates.
 *
 *  AutoOpts is a copyrighted work.  This source file is not encumbered
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

#ifndef __doxygen__
#define OPTION_CODE_COMPILE 1
#include "ntp-keygen-opts.h"
#include <sys/types.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef  __cplusplus
extern "C" {
#endif
extern FILE * option_usage_fp;
#define zCopyright      (ntp_keygen_opt_strs+0)
#define zLicenseDescrip (ntp_keygen_opt_strs+353)

/*
 *  global included definitions
 */
#include <stdlib.h>

#ifdef __windows
  extern int atoi(const char*);
#else
# include <stdlib.h>
#endif

#ifndef NULL
#  define NULL 0
#endif

/**
 *  static const strings for ntp-keygen options
 */
static char const ntp_keygen_opt_strs[2442] =
/*     0 */ "ntp-keygen (ntp) 4.2.8p13\n"
            "Copyright (C) 1992-2017 The University of Delaware and Network Time Foundation, all rights reserved.\n"
            "This is free software. It is licensed for use, modification and\n"
            "redistribution under the terms of the NTP License, copies of which\n"
            "can be seen at:\n"
            "  <http://ntp.org/license>\n"
            "  <http://opensource.org/licenses/ntp-license.php>\n\0"
/*   353 */ "Permission to use, copy, modify, and distribute this software and its\n"
            "documentation for any purpose with or without fee is hereby granted,\n"
            "provided that the above copyright notice appears in all copies and that\n"
            "both the copyright notice and this permission notice appear in supporting\n"
            "documentation, and that the name The University of Delaware not be used in\n"
            "advertising or publicity pertaining to distribution of the software without\n"
            "specific, written prior permission.  The University of Delaware and Network\n"
            "Time Foundation makes no representations about the suitability this\n"
            "software for any purpose.  It is provided \"as is\" without express or\n"
            "implied warranty.\n\0"
/*  1021 */ "identity modulus bits\0"
/*  1043 */ "IMBITS\0"
/*  1050 */ "imbits\0"
/*  1057 */ "certificate scheme\0"
/*  1076 */ "CERTIFICATE\0"
/*  1088 */ "certificate\0"
/*  1100 */ "privatekey cipher\0"
/*  1118 */ "CIPHER\0"
/*  1125 */ "cipher\0"
/*  1132 */ "Increase debug verbosity level\0"
/*  1163 */ "DEBUG_LEVEL\0"
/*  1175 */ "debug-level\0"
/*  1187 */ "Set the debug verbosity level\0"
/*  1217 */ "SET_DEBUG_LEVEL\0"
/*  1233 */ "set-debug-level\0"
/*  1249 */ "Write IFF or GQ identity keys\0"
/*  1279 */ "ID_KEY\0"
/*  1286 */ "id-key\0"
/*  1293 */ "Generate GQ parameters and keys\0"
/*  1325 */ "GQ_PARAMS\0"
/*  1335 */ "gq-params\0"
/*  1345 */ "generate RSA host key\0"
/*  1367 */ "HOST_KEY\0"
/*  1376 */ "host-key\0"
/*  1385 */ "generate IFF parameters\0"
/*  1409 */ "IFFKEY\0"
/*  1416 */ "iffkey\0"
/*  1423 */ "set Autokey group name\0"
/*  1446 */ "IDENT\0"
/*  1452 */ "ident\0"
/*  1458 */ "set certificate lifetime\0"
/*  1483 */ "LIFETIME\0"
/*  1492 */ "lifetime\0"
/*  1501 */ "prime modulus\0"
/*  1515 */ "MODULUS\0"
/*  1523 */ "modulus\0"
/*  1531 */ "generate symmetric keys\0"
/*  1555 */ "MD5KEY\0"
/*  1562 */ "md5key\0"
/*  1569 */ "generate PC private certificate\0"
/*  1601 */ "PVT_CERT\0"
/*  1610 */ "pvt-cert\0"
/*  1619 */ "local private password\0"
/*  1642 */ "PASSWORD\0"
/*  1651 */ "password\0"
/*  1660 */ "export IFF or GQ group keys with password\0"
/*  1702 */ "EXPORT_PASSWD\0"
/*  1716 */ "export-passwd\0"
/*  1730 */ "set host and optionally group name\0"
/*  1765 */ "SUBJECT_NAME\0"
/*  1778 */ "subject-name\0"
/*  1791 */ "generate sign key (RSA or DSA)\0"
/*  1822 */ "SIGN_KEY\0"
/*  1831 */ "sign-key\0"
/*  1840 */ "trusted certificate (TC scheme)\0"
/*  1872 */ "TRUSTED_CERT\0"
/*  1885 */ "trusted-cert\0"
/*  1898 */ "generate <num> MV parameters\0"
/*  1927 */ "MV_PARAMS\0"
/*  1937 */ "mv-params\0"
/*  1947 */ "update <num> MV keys\0"
/*  1968 */ "MV_KEYS\0"
/*  1976 */ "mv-keys\0"
/*  1984 */ "display extended usage information and exit\0"
/*  2028 */ "help\0"
/*  2033 */ "extended usage information passed thru pager\0"
/*  2078 */ "more-help\0"
/*  2088 */ "output version information and exit\0"
/*  2124 */ "version\0"
/*  2132 */ "save the option state to a config file\0"
/*  2171 */ "save-opts\0"
/*  2181 */ "load options from a config file\0"
/*  2213 */ "LOAD_OPTS\0"
/*  2223 */ "no-load-opts\0"
/*  2236 */ "no\0"
/*  2239 */ "NTP_KEYGEN\0"
/*  2250 */ "ntp-keygen (ntp) - Create a NTP host key - Ver. 4.2.8p13\n"
            "Usage:  %s [ -<flag> [<val>] | --<name>[{=| }<val>] ]...\n\0"
/*  2365 */ "$HOME\0"
/*  2371 */ ".\0"
/*  2373 */ ".ntprc\0"
/*  2380 */ "http://bugs.ntp.org, bugs@ntp.org\0"
/*  2414 */ "\n\0"
/*  2416 */ "ntp-keygen (ntp) 4.2.8p13";

/**
 *  imbits option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the imbits option */
#define IMBITS_DESC      (ntp_keygen_opt_strs+1021)
/** Upper-cased name for the imbits option */
#define IMBITS_NAME      (ntp_keygen_opt_strs+1043)
/** Name string for the imbits option */
#define IMBITS_name      (ntp_keygen_opt_strs+1050)
/** Compiled in flag settings for the imbits option */
#define IMBITS_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

#else   /* disable imbits */
#define IMBITS_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define IMBITS_NAME      NULL
#define IMBITS_DESC      NULL
#define IMBITS_name      NULL
#endif  /* AUTOKEY */

/**
 *  certificate option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the certificate option */
#define CERTIFICATE_DESC      (ntp_keygen_opt_strs+1057)
/** Upper-cased name for the certificate option */
#define CERTIFICATE_NAME      (ntp_keygen_opt_strs+1076)
/** Name string for the certificate option */
#define CERTIFICATE_name      (ntp_keygen_opt_strs+1088)
/** Compiled in flag settings for the certificate option */
#define CERTIFICATE_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

#else   /* disable certificate */
#define CERTIFICATE_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define CERTIFICATE_NAME      NULL
#define CERTIFICATE_DESC      NULL
#define CERTIFICATE_name      NULL
#endif  /* AUTOKEY */

/**
 *  cipher option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the cipher option */
#define CIPHER_DESC      (ntp_keygen_opt_strs+1100)
/** Upper-cased name for the cipher option */
#define CIPHER_NAME      (ntp_keygen_opt_strs+1118)
/** Name string for the cipher option */
#define CIPHER_name      (ntp_keygen_opt_strs+1125)
/** Compiled in flag settings for the cipher option */
#define CIPHER_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

#else   /* disable cipher */
#define CIPHER_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define CIPHER_NAME      NULL
#define CIPHER_DESC      NULL
#define CIPHER_name      NULL
#endif  /* AUTOKEY */

/**
 *  debug-level option description:
 */
/** Descriptive text for the debug-level option */
#define DEBUG_LEVEL_DESC      (ntp_keygen_opt_strs+1132)
/** Upper-cased name for the debug-level option */
#define DEBUG_LEVEL_NAME      (ntp_keygen_opt_strs+1163)
/** Name string for the debug-level option */
#define DEBUG_LEVEL_name      (ntp_keygen_opt_strs+1175)
/** Compiled in flag settings for the debug-level option */
#define DEBUG_LEVEL_FLAGS     (OPTST_DISABLED)

/**
 *  set-debug-level option description:
 */
/** Descriptive text for the set-debug-level option */
#define SET_DEBUG_LEVEL_DESC      (ntp_keygen_opt_strs+1187)
/** Upper-cased name for the set-debug-level option */
#define SET_DEBUG_LEVEL_NAME      (ntp_keygen_opt_strs+1217)
/** Name string for the set-debug-level option */
#define SET_DEBUG_LEVEL_name      (ntp_keygen_opt_strs+1233)
/** Compiled in flag settings for the set-debug-level option */
#define SET_DEBUG_LEVEL_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

/**
 *  id-key option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the id-key option */
#define ID_KEY_DESC      (ntp_keygen_opt_strs+1249)
/** Upper-cased name for the id-key option */
#define ID_KEY_NAME      (ntp_keygen_opt_strs+1279)
/** Name string for the id-key option */
#define ID_KEY_name      (ntp_keygen_opt_strs+1286)
/** Compiled in flag settings for the id-key option */
#define ID_KEY_FLAGS     (OPTST_DISABLED)

#else   /* disable id-key */
#define ID_KEY_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define ID_KEY_NAME      NULL
#define ID_KEY_DESC      NULL
#define ID_KEY_name      NULL
#endif  /* AUTOKEY */

/**
 *  gq-params option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the gq-params option */
#define GQ_PARAMS_DESC      (ntp_keygen_opt_strs+1293)
/** Upper-cased name for the gq-params option */
#define GQ_PARAMS_NAME      (ntp_keygen_opt_strs+1325)
/** Name string for the gq-params option */
#define GQ_PARAMS_name      (ntp_keygen_opt_strs+1335)
/** Compiled in flag settings for the gq-params option */
#define GQ_PARAMS_FLAGS     (OPTST_DISABLED)

#else   /* disable gq-params */
#define GQ_PARAMS_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define GQ_PARAMS_NAME      NULL
#define GQ_PARAMS_DESC      NULL
#define GQ_PARAMS_name      NULL
#endif  /* AUTOKEY */

/**
 *  host-key option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the host-key option */
#define HOST_KEY_DESC      (ntp_keygen_opt_strs+1345)
/** Upper-cased name for the host-key option */
#define HOST_KEY_NAME      (ntp_keygen_opt_strs+1367)
/** Name string for the host-key option */
#define HOST_KEY_name      (ntp_keygen_opt_strs+1376)
/** Compiled in flag settings for the host-key option */
#define HOST_KEY_FLAGS     (OPTST_DISABLED)

#else   /* disable host-key */
#define HOST_KEY_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define HOST_KEY_NAME      NULL
#define HOST_KEY_DESC      NULL
#define HOST_KEY_name      NULL
#endif  /* AUTOKEY */

/**
 *  iffkey option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the iffkey option */
#define IFFKEY_DESC      (ntp_keygen_opt_strs+1385)
/** Upper-cased name for the iffkey option */
#define IFFKEY_NAME      (ntp_keygen_opt_strs+1409)
/** Name string for the iffkey option */
#define IFFKEY_name      (ntp_keygen_opt_strs+1416)
/** Compiled in flag settings for the iffkey option */
#define IFFKEY_FLAGS     (OPTST_DISABLED)

#else   /* disable iffkey */
#define IFFKEY_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define IFFKEY_NAME      NULL
#define IFFKEY_DESC      NULL
#define IFFKEY_name      NULL
#endif  /* AUTOKEY */

/**
 *  ident option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the ident option */
#define IDENT_DESC      (ntp_keygen_opt_strs+1423)
/** Upper-cased name for the ident option */
#define IDENT_NAME      (ntp_keygen_opt_strs+1446)
/** Name string for the ident option */
#define IDENT_name      (ntp_keygen_opt_strs+1452)
/** Compiled in flag settings for the ident option */
#define IDENT_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

#else   /* disable ident */
#define IDENT_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define IDENT_NAME      NULL
#define IDENT_DESC      NULL
#define IDENT_name      NULL
#endif  /* AUTOKEY */

/**
 *  lifetime option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the lifetime option */
#define LIFETIME_DESC      (ntp_keygen_opt_strs+1458)
/** Upper-cased name for the lifetime option */
#define LIFETIME_NAME      (ntp_keygen_opt_strs+1483)
/** Name string for the lifetime option */
#define LIFETIME_name      (ntp_keygen_opt_strs+1492)
/** Compiled in flag settings for the lifetime option */
#define LIFETIME_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

#else   /* disable lifetime */
#define LIFETIME_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define LIFETIME_NAME      NULL
#define LIFETIME_DESC      NULL
#define LIFETIME_name      NULL
#endif  /* AUTOKEY */

/**
 *  modulus option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the modulus option */
#define MODULUS_DESC      (ntp_keygen_opt_strs+1501)
/** Upper-cased name for the modulus option */
#define MODULUS_NAME      (ntp_keygen_opt_strs+1515)
/** Name string for the modulus option */
#define MODULUS_name      (ntp_keygen_opt_strs+1523)
/** Compiled in flag settings for the modulus option */
#define MODULUS_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

#else   /* disable modulus */
#define MODULUS_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define MODULUS_NAME      NULL
#define MODULUS_DESC      NULL
#define MODULUS_name      NULL
#endif  /* AUTOKEY */

/**
 *  md5key option description:
 */
/** Descriptive text for the md5key option */
#define MD5KEY_DESC      (ntp_keygen_opt_strs+1531)
/** Upper-cased name for the md5key option */
#define MD5KEY_NAME      (ntp_keygen_opt_strs+1555)
/** Name string for the md5key option */
#define MD5KEY_name      (ntp_keygen_opt_strs+1562)
/** Compiled in flag settings for the md5key option */
#define MD5KEY_FLAGS     (OPTST_DISABLED)

/**
 *  pvt-cert option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the pvt-cert option */
#define PVT_CERT_DESC      (ntp_keygen_opt_strs+1569)
/** Upper-cased name for the pvt-cert option */
#define PVT_CERT_NAME      (ntp_keygen_opt_strs+1601)
/** Name string for the pvt-cert option */
#define PVT_CERT_name      (ntp_keygen_opt_strs+1610)
/** Compiled in flag settings for the pvt-cert option */
#define PVT_CERT_FLAGS     (OPTST_DISABLED)

#else   /* disable pvt-cert */
#define PVT_CERT_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define PVT_CERT_NAME      NULL
#define PVT_CERT_DESC      NULL
#define PVT_CERT_name      NULL
#endif  /* AUTOKEY */

/**
 *  password option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the password option */
#define PASSWORD_DESC      (ntp_keygen_opt_strs+1619)
/** Upper-cased name for the password option */
#define PASSWORD_NAME      (ntp_keygen_opt_strs+1642)
/** Name string for the password option */
#define PASSWORD_name      (ntp_keygen_opt_strs+1651)
/** Compiled in flag settings for the password option */
#define PASSWORD_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

#else   /* disable password */
#define PASSWORD_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define PASSWORD_NAME      NULL
#define PASSWORD_DESC      NULL
#define PASSWORD_name      NULL
#endif  /* AUTOKEY */

/**
 *  export-passwd option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the export-passwd option */
#define EXPORT_PASSWD_DESC      (ntp_keygen_opt_strs+1660)
/** Upper-cased name for the export-passwd option */
#define EXPORT_PASSWD_NAME      (ntp_keygen_opt_strs+1702)
/** Name string for the export-passwd option */
#define EXPORT_PASSWD_name      (ntp_keygen_opt_strs+1716)
/** Compiled in flag settings for the export-passwd option */
#define EXPORT_PASSWD_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

#else   /* disable export-passwd */
#define EXPORT_PASSWD_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define EXPORT_PASSWD_NAME      NULL
#define EXPORT_PASSWD_DESC      NULL
#define EXPORT_PASSWD_name      NULL
#endif  /* AUTOKEY */

/**
 *  subject-name option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the subject-name option */
#define SUBJECT_NAME_DESC      (ntp_keygen_opt_strs+1730)
/** Upper-cased name for the subject-name option */
#define SUBJECT_NAME_NAME      (ntp_keygen_opt_strs+1765)
/** Name string for the subject-name option */
#define SUBJECT_NAME_name      (ntp_keygen_opt_strs+1778)
/** Compiled in flag settings for the subject-name option */
#define SUBJECT_NAME_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

#else   /* disable subject-name */
#define SUBJECT_NAME_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define SUBJECT_NAME_NAME      NULL
#define SUBJECT_NAME_DESC      NULL
#define SUBJECT_NAME_name      NULL
#endif  /* AUTOKEY */

/**
 *  sign-key option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the sign-key option */
#define SIGN_KEY_DESC      (ntp_keygen_opt_strs+1791)
/** Upper-cased name for the sign-key option */
#define SIGN_KEY_NAME      (ntp_keygen_opt_strs+1822)
/** Name string for the sign-key option */
#define SIGN_KEY_name      (ntp_keygen_opt_strs+1831)
/** Compiled in flag settings for the sign-key option */
#define SIGN_KEY_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

#else   /* disable sign-key */
#define SIGN_KEY_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define SIGN_KEY_NAME      NULL
#define SIGN_KEY_DESC      NULL
#define SIGN_KEY_name      NULL
#endif  /* AUTOKEY */

/**
 *  trusted-cert option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the trusted-cert option */
#define TRUSTED_CERT_DESC      (ntp_keygen_opt_strs+1840)
/** Upper-cased name for the trusted-cert option */
#define TRUSTED_CERT_NAME      (ntp_keygen_opt_strs+1872)
/** Name string for the trusted-cert option */
#define TRUSTED_CERT_name      (ntp_keygen_opt_strs+1885)
/** Compiled in flag settings for the trusted-cert option */
#define TRUSTED_CERT_FLAGS     (OPTST_DISABLED)

#else   /* disable trusted-cert */
#define TRUSTED_CERT_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define TRUSTED_CERT_NAME      NULL
#define TRUSTED_CERT_DESC      NULL
#define TRUSTED_CERT_name      NULL
#endif  /* AUTOKEY */

/**
 *  mv-params option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the mv-params option */
#define MV_PARAMS_DESC      (ntp_keygen_opt_strs+1898)
/** Upper-cased name for the mv-params option */
#define MV_PARAMS_NAME      (ntp_keygen_opt_strs+1927)
/** Name string for the mv-params option */
#define MV_PARAMS_name      (ntp_keygen_opt_strs+1937)
/** Compiled in flag settings for the mv-params option */
#define MV_PARAMS_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

#else   /* disable mv-params */
#define MV_PARAMS_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define MV_PARAMS_NAME      NULL
#define MV_PARAMS_DESC      NULL
#define MV_PARAMS_name      NULL
#endif  /* AUTOKEY */

/**
 *  mv-keys option description:
 */
#ifdef AUTOKEY
/** Descriptive text for the mv-keys option */
#define MV_KEYS_DESC      (ntp_keygen_opt_strs+1947)
/** Upper-cased name for the mv-keys option */
#define MV_KEYS_NAME      (ntp_keygen_opt_strs+1968)
/** Name string for the mv-keys option */
#define MV_KEYS_name      (ntp_keygen_opt_strs+1976)
/** Compiled in flag settings for the mv-keys option */
#define MV_KEYS_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

#else   /* disable mv-keys */
#define MV_KEYS_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define MV_KEYS_NAME      NULL
#define MV_KEYS_DESC      NULL
#define MV_KEYS_name      NULL
#endif  /* AUTOKEY */

/*
 *  Help/More_Help/Version option descriptions:
 */
#define HELP_DESC       (ntp_keygen_opt_strs+1984)
#define HELP_name       (ntp_keygen_opt_strs+2028)
#ifdef HAVE_WORKING_FORK
#define MORE_HELP_DESC  (ntp_keygen_opt_strs+2033)
#define MORE_HELP_name  (ntp_keygen_opt_strs+2078)
#define MORE_HELP_FLAGS (OPTST_IMM | OPTST_NO_INIT)
#else
#define MORE_HELP_DESC  HELP_DESC
#define MORE_HELP_name  HELP_name
#define MORE_HELP_FLAGS (OPTST_OMITTED | OPTST_NO_INIT)
#endif
#ifdef NO_OPTIONAL_OPT_ARGS
#  define VER_FLAGS     (OPTST_IMM | OPTST_NO_INIT)
#else
#  define VER_FLAGS     (OPTST_SET_ARGTYPE(OPARG_TYPE_STRING) | \
                         OPTST_ARG_OPTIONAL | OPTST_IMM | OPTST_NO_INIT)
#endif
#define VER_DESC        (ntp_keygen_opt_strs+2088)
#define VER_name        (ntp_keygen_opt_strs+2124)
#define SAVE_OPTS_DESC  (ntp_keygen_opt_strs+2132)
#define SAVE_OPTS_name  (ntp_keygen_opt_strs+2171)
#define LOAD_OPTS_DESC     (ntp_keygen_opt_strs+2181)
#define LOAD_OPTS_NAME     (ntp_keygen_opt_strs+2213)
#define NO_LOAD_OPTS_name  (ntp_keygen_opt_strs+2223)
#define LOAD_OPTS_pfx      (ntp_keygen_opt_strs+2236)
#define LOAD_OPTS_name     (NO_LOAD_OPTS_name + 3)
/**
 *  Declare option callback procedures
 */
#ifdef AUTOKEY
  static tOptProc doOptImbits;
#else /* not AUTOKEY */
# define doOptImbits NULL
#endif /* def/not AUTOKEY */
#ifdef AUTOKEY
  static tOptProc doOptModulus;
#else /* not AUTOKEY */
# define doOptModulus NULL
#endif /* def/not AUTOKEY */
extern tOptProc
    ntpOptionPrintVersion, optionBooleanVal,      optionNestedVal,
    optionNumericVal,      optionPagedUsage,      optionResetOpt,
    optionStackArg,        optionTimeDate,        optionTimeVal,
    optionUnstackArg,      optionVendorOption;
static tOptProc
    doOptDebug_Level, doUsageOpt;
#define VER_PROC        ntpOptionPrintVersion

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 *  Define the ntp-keygen Option Descriptions.
 * This is an array of OPTION_CT entries, one for each
 * option that the ntp-keygen program responds to.
 */
static tOptDesc optDesc[OPTION_CT] = {
  {  /* entry idx, value */ 0, VALUE_OPT_IMBITS,
     /* equiv idx, value */ 0, VALUE_OPT_IMBITS,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ IMBITS_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --imbits */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ doOptImbits,
     /* desc, NAME, name */ IMBITS_DESC, IMBITS_NAME, IMBITS_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 1, VALUE_OPT_CERTIFICATE,
     /* equiv idx, value */ 1, VALUE_OPT_CERTIFICATE,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ CERTIFICATE_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --certificate */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ CERTIFICATE_DESC, CERTIFICATE_NAME, CERTIFICATE_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 2, VALUE_OPT_CIPHER,
     /* equiv idx, value */ 2, VALUE_OPT_CIPHER,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ CIPHER_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --cipher */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ CIPHER_DESC, CIPHER_NAME, CIPHER_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 3, VALUE_OPT_DEBUG_LEVEL,
     /* equiv idx, value */ 3, VALUE_OPT_DEBUG_LEVEL,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ DEBUG_LEVEL_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --debug-level */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ doOptDebug_Level,
     /* desc, NAME, name */ DEBUG_LEVEL_DESC, DEBUG_LEVEL_NAME, DEBUG_LEVEL_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 4, VALUE_OPT_SET_DEBUG_LEVEL,
     /* equiv idx, value */ 4, VALUE_OPT_SET_DEBUG_LEVEL,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ SET_DEBUG_LEVEL_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --set-debug-level */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionNumericVal,
     /* desc, NAME, name */ SET_DEBUG_LEVEL_DESC, SET_DEBUG_LEVEL_NAME, SET_DEBUG_LEVEL_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 5, VALUE_OPT_ID_KEY,
     /* equiv idx, value */ 5, VALUE_OPT_ID_KEY,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ ID_KEY_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --id-key */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ ID_KEY_DESC, ID_KEY_NAME, ID_KEY_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 6, VALUE_OPT_GQ_PARAMS,
     /* equiv idx, value */ 6, VALUE_OPT_GQ_PARAMS,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ GQ_PARAMS_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --gq-params */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ GQ_PARAMS_DESC, GQ_PARAMS_NAME, GQ_PARAMS_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 7, VALUE_OPT_HOST_KEY,
     /* equiv idx, value */ 7, VALUE_OPT_HOST_KEY,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ HOST_KEY_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --host-key */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ HOST_KEY_DESC, HOST_KEY_NAME, HOST_KEY_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 8, VALUE_OPT_IFFKEY,
     /* equiv idx, value */ 8, VALUE_OPT_IFFKEY,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ IFFKEY_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --iffkey */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ IFFKEY_DESC, IFFKEY_NAME, IFFKEY_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 9, VALUE_OPT_IDENT,
     /* equiv idx, value */ 9, VALUE_OPT_IDENT,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ IDENT_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --ident */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ IDENT_DESC, IDENT_NAME, IDENT_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 10, VALUE_OPT_LIFETIME,
     /* equiv idx, value */ 10, VALUE_OPT_LIFETIME,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ LIFETIME_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --lifetime */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionNumericVal,
     /* desc, NAME, name */ LIFETIME_DESC, LIFETIME_NAME, LIFETIME_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 11, VALUE_OPT_MODULUS,
     /* equiv idx, value */ 11, VALUE_OPT_MODULUS,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ MODULUS_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --modulus */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ doOptModulus,
     /* desc, NAME, name */ MODULUS_DESC, MODULUS_NAME, MODULUS_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 12, VALUE_OPT_MD5KEY,
     /* equiv idx, value */ 12, VALUE_OPT_MD5KEY,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ MD5KEY_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --md5key */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ MD5KEY_DESC, MD5KEY_NAME, MD5KEY_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 13, VALUE_OPT_PVT_CERT,
     /* equiv idx, value */ 13, VALUE_OPT_PVT_CERT,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ PVT_CERT_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --pvt-cert */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ PVT_CERT_DESC, PVT_CERT_NAME, PVT_CERT_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 14, VALUE_OPT_PASSWORD,
     /* equiv idx, value */ 14, VALUE_OPT_PASSWORD,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ PASSWORD_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --password */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ PASSWORD_DESC, PASSWORD_NAME, PASSWORD_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 15, VALUE_OPT_EXPORT_PASSWD,
     /* equiv idx, value */ 15, VALUE_OPT_EXPORT_PASSWD,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ EXPORT_PASSWD_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --export-passwd */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ EXPORT_PASSWD_DESC, EXPORT_PASSWD_NAME, EXPORT_PASSWD_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 16, VALUE_OPT_SUBJECT_NAME,
     /* equiv idx, value */ 16, VALUE_OPT_SUBJECT_NAME,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ SUBJECT_NAME_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --subject-name */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ SUBJECT_NAME_DESC, SUBJECT_NAME_NAME, SUBJECT_NAME_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 17, VALUE_OPT_SIGN_KEY,
     /* equiv idx, value */ 17, VALUE_OPT_SIGN_KEY,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ SIGN_KEY_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --sign-key */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ SIGN_KEY_DESC, SIGN_KEY_NAME, SIGN_KEY_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 18, VALUE_OPT_TRUSTED_CERT,
     /* equiv idx, value */ 18, VALUE_OPT_TRUSTED_CERT,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ TRUSTED_CERT_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --trusted-cert */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ TRUSTED_CERT_DESC, TRUSTED_CERT_NAME, TRUSTED_CERT_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 19, VALUE_OPT_MV_PARAMS,
     /* equiv idx, value */ 19, VALUE_OPT_MV_PARAMS,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ MV_PARAMS_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --mv-params */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionNumericVal,
     /* desc, NAME, name */ MV_PARAMS_DESC, MV_PARAMS_NAME, MV_PARAMS_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 20, VALUE_OPT_MV_KEYS,
     /* equiv idx, value */ 20, VALUE_OPT_MV_KEYS,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ MV_KEYS_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --mv-keys */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionNumericVal,
     /* desc, NAME, name */ MV_KEYS_DESC, MV_KEYS_NAME, MV_KEYS_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ INDEX_OPT_VERSION, VALUE_OPT_VERSION,
     /* equiv idx value  */ NO_EQUIVALENT, VALUE_OPT_VERSION,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ VER_FLAGS, AOUSE_VERSION,
     /* last opt argumnt */ { NULL },
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ VER_PROC,
     /* desc, NAME, name */ VER_DESC, NULL, VER_name,
     /* disablement strs */ NULL, NULL },



  {  /* entry idx, value */ INDEX_OPT_HELP, VALUE_OPT_HELP,
     /* equiv idx value  */ NO_EQUIVALENT, VALUE_OPT_HELP,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ OPTST_IMM | OPTST_NO_INIT, AOUSE_HELP,
     /* last opt argumnt */ { NULL },
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ doUsageOpt,
     /* desc, NAME, name */ HELP_DESC, NULL, HELP_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ INDEX_OPT_MORE_HELP, VALUE_OPT_MORE_HELP,
     /* equiv idx value  */ NO_EQUIVALENT, VALUE_OPT_MORE_HELP,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ MORE_HELP_FLAGS, AOUSE_MORE_HELP,
     /* last opt argumnt */ { NULL },
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL,  NULL,
     /* option proc      */ optionPagedUsage,
     /* desc, NAME, name */ MORE_HELP_DESC, NULL, MORE_HELP_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ INDEX_OPT_SAVE_OPTS, VALUE_OPT_SAVE_OPTS,
     /* equiv idx value  */ NO_EQUIVALENT, VALUE_OPT_SAVE_OPTS,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ OPTST_SET_ARGTYPE(OPARG_TYPE_STRING)
                       | OPTST_ARG_OPTIONAL | OPTST_NO_INIT, AOUSE_SAVE_OPTS,
     /* last opt argumnt */ { NULL },
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL,  NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ SAVE_OPTS_DESC, NULL, SAVE_OPTS_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ INDEX_OPT_LOAD_OPTS, VALUE_OPT_LOAD_OPTS,
     /* equiv idx value  */ NO_EQUIVALENT, VALUE_OPT_LOAD_OPTS,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ OPTST_SET_ARGTYPE(OPARG_TYPE_STRING)
			  | OPTST_DISABLE_IMM, AOUSE_LOAD_OPTS,
     /* last opt argumnt */ { NULL },
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionLoadOpt,
     /* desc, NAME, name */ LOAD_OPTS_DESC, LOAD_OPTS_NAME, LOAD_OPTS_name,
     /* disablement strs */ NO_LOAD_OPTS_name, LOAD_OPTS_pfx }
};


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/** Reference to the upper cased version of ntp-keygen. */
#define zPROGNAME       (ntp_keygen_opt_strs+2239)
/** Reference to the title line for ntp-keygen usage. */
#define zUsageTitle     (ntp_keygen_opt_strs+2250)
/** ntp-keygen configuration file name. */
#define zRcName         (ntp_keygen_opt_strs+2373)
/** Directories to search for ntp-keygen config files. */
static char const * const apzHomeList[3] = {
    ntp_keygen_opt_strs+2365,
    ntp_keygen_opt_strs+2371,
    NULL };
/** The ntp-keygen program bug email address. */
#define zBugsAddr       (ntp_keygen_opt_strs+2380)
/** Clarification/explanation of what ntp-keygen does. */
#define zExplain        (ntp_keygen_opt_strs+2414)
/** Extra detail explaining what ntp-keygen does. */
#define zDetail         (NULL)
/** The full version string for ntp-keygen. */
#define zFullVersion    (ntp_keygen_opt_strs+2416)
/* extracted from optcode.tlib near line 364 */

#if defined(ENABLE_NLS)
# define OPTPROC_BASE OPTPROC_TRANSLATE
  static tOptionXlateProc translate_option_strings;
#else
# define OPTPROC_BASE OPTPROC_NONE
# define translate_option_strings NULL
#endif /* ENABLE_NLS */

#define ntp_keygen_full_usage (NULL)
#define ntp_keygen_short_usage (NULL)

#endif /* not defined __doxygen__ */

/*
 *  Create the static procedure(s) declared above.
 */
/**
 * The callout function that invokes the optionUsage function.
 *
 * @param[in] opts the AutoOpts option description structure
 * @param[in] od   the descriptor for the "help" (usage) option.
 * @noreturn
 */
static void
doUsageOpt(tOptions * opts, tOptDesc * od)
{
    int ex_code;
    ex_code = NTP_KEYGEN_EXIT_SUCCESS;
    optionUsage(&ntp_keygenOptions, ex_code);
    /* NOTREACHED */
    exit(1);
    (void)opts;
    (void)od;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 * Code to handle the imbits option, when AUTOKEY is #define-d.
 * The number of bits in the identity modulus.  The default is 256.
 * @param[in] pOptions the ntp-keygen options data structure
 * @param[in,out] pOptDesc the option descriptor for this option.
 */
#ifdef AUTOKEY
static void
doOptImbits(tOptions* pOptions, tOptDesc* pOptDesc)
{
    static struct {long rmin, rmax;} const rng[1] = {
        { 256, 2048 } };
    int  ix;

    if (pOptions <= OPTPROC_EMIT_LIMIT)
        goto emit_ranges;
    optionNumericVal(pOptions, pOptDesc);

    for (ix = 0; ix < 1; ix++) {
        if (pOptDesc->optArg.argInt < rng[ix].rmin)
            continue;  /* ranges need not be ordered. */
        if (pOptDesc->optArg.argInt == rng[ix].rmin)
            return;
        if (rng[ix].rmax == LONG_MIN)
            continue;
        if (pOptDesc->optArg.argInt <= rng[ix].rmax)
            return;
    }

    option_usage_fp = stderr;

 emit_ranges:
optionShowRange(pOptions, pOptDesc, VOIDP(rng), 1);
}
#endif /* defined AUTOKEY */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 * Code to handle the debug-level option.
 *
 * @param[in] pOptions the ntp-keygen options data structure
 * @param[in,out] pOptDesc the option descriptor for this option.
 */
static void
doOptDebug_Level(tOptions* pOptions, tOptDesc* pOptDesc)
{
    /*
     * Be sure the flag-code[0] handles special values for the options pointer
     * viz. (poptions <= OPTPROC_EMIT_LIMIT) *and also* the special flag bit
     * ((poptdesc->fOptState & OPTST_RESET) != 0) telling the option to
     * reset its state.
     */
    /* extracted from debug-opt.def, line 15 */
OPT_VALUE_SET_DEBUG_LEVEL++;
    (void)pOptDesc;
    (void)pOptions;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 * Code to handle the modulus option, when AUTOKEY is #define-d.
 * The number of bits in the prime modulus.  The default is 512.
 * @param[in] pOptions the ntp-keygen options data structure
 * @param[in,out] pOptDesc the option descriptor for this option.
 */
#ifdef AUTOKEY
static void
doOptModulus(tOptions* pOptions, tOptDesc* pOptDesc)
{
    static struct {long rmin, rmax;} const rng[1] = {
        { 256, 2048 } };
    int  ix;

    if (pOptions <= OPTPROC_EMIT_LIMIT)
        goto emit_ranges;
    optionNumericVal(pOptions, pOptDesc);

    for (ix = 0; ix < 1; ix++) {
        if (pOptDesc->optArg.argInt < rng[ix].rmin)
            continue;  /* ranges need not be ordered. */
        if (pOptDesc->optArg.argInt == rng[ix].rmin)
            return;
        if (rng[ix].rmax == LONG_MIN)
            continue;
        if (pOptDesc->optArg.argInt <= rng[ix].rmax)
            return;
    }

    option_usage_fp = stderr;

 emit_ranges:
optionShowRange(pOptions, pOptDesc, VOIDP(rng), 1);
}
#endif /* defined AUTOKEY */
/* extracted from optmain.tlib near line 1250 */

/**
 * The directory containing the data associated with ntp-keygen.
 */
#ifndef  PKGDATADIR
# define PKGDATADIR ""
#endif

/**
 * Information about the person or institution that packaged ntp-keygen
 * for the current distribution.
 */
#ifndef  WITH_PACKAGER
# define ntp_keygen_packager_info NULL
#else
/** Packager information for ntp-keygen. */
static char const ntp_keygen_packager_info[] =
    "Packaged by " WITH_PACKAGER

# ifdef WITH_PACKAGER_VERSION
        " ("WITH_PACKAGER_VERSION")"
# endif

# ifdef WITH_PACKAGER_BUG_REPORTS
    "\nReport ntp_keygen bugs to " WITH_PACKAGER_BUG_REPORTS
# endif
    "\n";
#endif
#ifndef __doxygen__

#endif /* __doxygen__ */
/**
 * The option definitions for ntp-keygen.  The one structure that
 * binds them all.
 */
tOptions ntp_keygenOptions = {
    OPTIONS_STRUCT_VERSION,
    0, NULL,                    /* original argc + argv    */
    ( OPTPROC_BASE
    + OPTPROC_ERRSTOP
    + OPTPROC_SHORTOPT
    + OPTPROC_LONGOPT
    + OPTPROC_NO_REQ_OPT
    + OPTPROC_ENVIRON
    + OPTPROC_NO_ARGS
    + OPTPROC_MISUSE ),
    0, NULL,                    /* current option index, current option */
    NULL,         NULL,         zPROGNAME,
    zRcName,      zCopyright,   zLicenseDescrip,
    zFullVersion, apzHomeList,  zUsageTitle,
    zExplain,     zDetail,      optDesc,
    zBugsAddr,                  /* address to send bugs to */
    NULL, NULL,                 /* extensions/saved state  */
    optionUsage, /* usage procedure */
    translate_option_strings,   /* translation procedure */
    /*
     *  Indexes to special options
     */
    { INDEX_OPT_MORE_HELP, /* more-help option index */
      INDEX_OPT_SAVE_OPTS, /* save option index */
      NO_EQUIVALENT, /* '-#' option index */
      NO_EQUIVALENT /* index of default opt */
    },
    26 /* full option count */, 21 /* user option count */,
    ntp_keygen_full_usage, ntp_keygen_short_usage,
    NULL, NULL,
    PKGDATADIR, ntp_keygen_packager_info
};

#if ENABLE_NLS
/**
 * This code is designed to translate translatable option text for the
 * ntp-keygen program.  These translations happen upon entry
 * to optionProcess().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_DCGETTEXT
# include <gettext.h>
#endif
#include <autoopts/usage-txt.h>

static char * AO_gettext(char const * pz);
static void   coerce_it(void ** s);

/**
 * AutoGen specific wrapper function for gettext.  It relies on the macro _()
 * to convert from English to the target language, then strdup-duplicates the
 * result string.  It tries the "libopts" domain first, then whatever has been
 * set via the \a textdomain(3) call.
 *
 * @param[in] pz the input text used as a lookup key.
 * @returns the translated text (if there is one),
 *   or the original text (if not).
 */
static char *
AO_gettext(char const * pz)
{
    char * res;
    if (pz == NULL)
        return NULL;
#ifdef HAVE_DCGETTEXT
    /*
     * While processing the option_xlateable_txt data, try to use the
     * "libopts" domain.  Once we switch to the option descriptor data,
     * do *not* use that domain.
     */
    if (option_xlateable_txt.field_ct != 0) {
        res = dgettext("libopts", pz);
        if (res == pz)
            res = (char *)VOIDP(_(pz));
    } else
        res = (char *)VOIDP(_(pz));
#else
    res = (char *)VOIDP(_(pz));
#endif
    if (res == pz)
        return res;
    res = strdup(res);
    if (res == NULL) {
        fputs(_("No memory for duping translated strings\n"), stderr);
        exit(NTP_KEYGEN_EXIT_FAILURE);
    }
    return res;
}

/**
 * All the pointers we use are marked "* const", but they are stored in
 * writable memory.  Coerce the mutability and set the pointer.
 */
static void coerce_it(void ** s) { *s = AO_gettext(*s);
}

/**
 * Translate all the translatable strings in the ntp_keygenOptions
 * structure defined above.  This is done only once.
 */
static void
translate_option_strings(void)
{
    tOptions * const opts = &ntp_keygenOptions;

    /*
     *  Guard against re-translation.  It won't work.  The strings will have
     *  been changed by the first pass through this code.  One shot only.
     */
    if (option_xlateable_txt.field_ct != 0) {
        /*
         *  Do the translations.  The first pointer follows the field count
         *  field.  The field count field is the size of a pointer.
         */
        char ** ppz = (char**)VOIDP(&(option_xlateable_txt));
        int     ix  = option_xlateable_txt.field_ct;

        do {
            ppz++; /* skip over field_ct */
            *ppz = AO_gettext(*ppz);
        } while (--ix > 0);
        /* prevent re-translation and disable "libopts" domain lookup */
        option_xlateable_txt.field_ct = 0;

        coerce_it(VOIDP(&(opts->pzCopyright)));
        coerce_it(VOIDP(&(opts->pzCopyNotice)));
        coerce_it(VOIDP(&(opts->pzFullVersion)));
        coerce_it(VOIDP(&(opts->pzUsageTitle)));
        coerce_it(VOIDP(&(opts->pzExplain)));
        coerce_it(VOIDP(&(opts->pzDetail)));
        {
            tOptDesc * od = opts->pOptDesc;
            for (ix = opts->optCt; ix > 0; ix--, od++)
                coerce_it(VOIDP(&(od->pzText)));
        }
    }
}
#endif /* ENABLE_NLS */

#ifdef DO_NOT_COMPILE_THIS_CODE_IT_IS_FOR_GETTEXT
/** I18N function strictly for xgettext.  Do not compile. */
static void bogus_function(void) {
  /* TRANSLATORS:

     The following dummy function was crated solely so that xgettext can
     extract the correct strings.  These strings are actually referenced
     by a field name in the ntp_keygenOptions structure noted in the
     comments below.  The literal text is defined in ntp_keygen_opt_strs.
   
     NOTE: the strings below are segmented with respect to the source string
     ntp_keygen_opt_strs.  The strings above are handed off for translation
     at run time a paragraph at a time.  Consequently, they are presented here
     for translation a paragraph at a time.
   
     ALSO: often the description for an option will reference another option
     by name.  These are set off with apostrophe quotes (I hope).  Do not
     translate option names.
   */
  /* referenced via ntp_keygenOptions.pzCopyright */
  puts(_("ntp-keygen (ntp) 4.2.8p13\n\
Copyright (C) 1992-2017 The University of Delaware and Network Time Foundation, all rights reserved.\n\
This is free software. It is licensed for use, modification and\n\
redistribution under the terms of the NTP License, copies of which\n\
can be seen at:\n"));
  puts(_("  <http://ntp.org/license>\n\
  <http://opensource.org/licenses/ntp-license.php>\n"));

  /* referenced via ntp_keygenOptions.pzCopyNotice */
  puts(_("Permission to use, copy, modify, and distribute this software and its\n\
documentation for any purpose with or without fee is hereby granted,\n\
provided that the above copyright notice appears in all copies and that\n\
both the copyright notice and this permission notice appear in supporting\n\
documentation, and that the name The University of Delaware not be used in\n\
advertising or publicity pertaining to distribution of the software without\n\
specific, written prior permission.  The University of Delaware and Network\n\
Time Foundation makes no representations about the suitability this\n\
software for any purpose.  It is provided \"as is\" without express or\n\
implied warranty.\n"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("identity modulus bits"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("certificate scheme"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("privatekey cipher"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("Increase debug verbosity level"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("Set the debug verbosity level"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("Write IFF or GQ identity keys"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("Generate GQ parameters and keys"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("generate RSA host key"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("generate IFF parameters"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("set Autokey group name"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("set certificate lifetime"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("prime modulus"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("generate symmetric keys"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("generate PC private certificate"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("local private password"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("export IFF or GQ group keys with password"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("set host and optionally group name"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("generate sign key (RSA or DSA)"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("trusted certificate (TC scheme)"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("generate <num> MV parameters"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("update <num> MV keys"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("display extended usage information and exit"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("extended usage information passed thru pager"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("output version information and exit"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("save the option state to a config file"));

  /* referenced via ntp_keygenOptions.pOptDesc->pzText */
  puts(_("load options from a config file"));

  /* referenced via ntp_keygenOptions.pzUsageTitle */
  puts(_("ntp-keygen (ntp) - Create a NTP host key - Ver. 4.2.8p13\n\
Usage:  %s [ -<flag> [<val>] | --<name>[{=| }<val>] ]...\n"));

  /* referenced via ntp_keygenOptions.pzExplain */
  puts(_("\n"));

  /* referenced via ntp_keygenOptions.pzFullVersion */
  puts(_("ntp-keygen (ntp) 4.2.8p13"));

  /* referenced via ntp_keygenOptions.pzFullUsage */
  puts(_("<<<NOT-FOUND>>>"));

  /* referenced via ntp_keygenOptions.pzShortUsage */
  puts(_("<<<NOT-FOUND>>>"));
  /* LIBOPTS-MESSAGES: */
#line 67 "../autoopts.c"
  puts(_("allocation of %d bytes failed\n"));
#line 93 "../autoopts.c"
  puts(_("allocation of %d bytes failed\n"));
#line 53 "../init.c"
  puts(_("AutoOpts function called without option descriptor\n"));
#line 86 "../init.c"
  puts(_("\tThis exceeds the compiled library version:  "));
#line 84 "../init.c"
  puts(_("Automated Options Processing Error!\n"
       "\t%s called AutoOpts function with structure version %d:%d:%d.\n"));
#line 80 "../autoopts.c"
  puts(_("realloc of %d bytes at 0x%p failed\n"));
#line 88 "../init.c"
  puts(_("\tThis is less than the minimum library version:  "));
#line 121 "../version.c"
  puts(_("Automated Options version %s\n"
       "\tCopyright (C) 1999-2014 by Bruce Korb - all rights reserved\n"));
#line 87 "../makeshell.c"
  puts(_("(AutoOpts bug):  %s.\n"));
#line 90 "../reset.c"
  puts(_("optionResetOpt() called, but reset-option not configured"));
#line 292 "../usage.c"
  puts(_("could not locate the 'help' option"));
#line 336 "../autoopts.c"
  puts(_("optionProcess() was called with invalid data"));
#line 748 "../usage.c"
  puts(_("invalid argument type specified"));
#line 598 "../find.c"
  puts(_("defaulted to option with optional arg"));
#line 76 "../alias.c"
  puts(_("aliasing option is out of range."));
#line 234 "../enum.c"
  puts(_("%s error:  the keyword '%s' is ambiguous for %s\n"));
#line 108 "../find.c"
  puts(_("  The following options match:\n"));
#line 293 "../find.c"
  puts(_("%s: ambiguous option name: %s (matches %d options)\n"));
#line 161 "../check.c"
  puts(_("%s: Command line arguments required\n"));
#line 43 "../alias.c"
  puts(_("%d %s%s options allowed\n"));
#line 94 "../makeshell.c"
  puts(_("%s error %d (%s) calling %s for '%s'\n"));
#line 306 "../makeshell.c"
  puts(_("interprocess pipe"));
#line 168 "../version.c"
  puts(_("error: version option argument '%c' invalid.  Use:\n"
       "\t'v' - version only\n"
       "\t'c' - version and copyright\n"
       "\t'n' - version and full copyright notice\n"));
#line 58 "../check.c"
  puts(_("%s error:  the '%s' and '%s' options conflict\n"));
#line 217 "../find.c"
  puts(_("%s: The '%s' option has been disabled."));
#line 430 "../find.c"
  puts(_("%s: The '%s' option has been disabled."));
#line 38 "../alias.c"
  puts(_("-equivalence"));
#line 469 "../find.c"
  puts(_("%s: illegal option -- %c\n"));
#line 110 "../reset.c"
  puts(_("%s: illegal option -- %c\n"));
#line 271 "../find.c"
  puts(_("%s: illegal option -- %s\n"));
#line 755 "../find.c"
  puts(_("%s: illegal option -- %s\n"));
#line 118 "../reset.c"
  puts(_("%s: illegal option -- %s\n"));
#line 335 "../find.c"
  puts(_("%s: unknown vendor extension option -- %s\n"));
#line 159 "../enum.c"
  puts(_("  or an integer from %d through %d\n"));
#line 169 "../enum.c"
  puts(_("  or an integer from %d through %d\n"));
#line 747 "../usage.c"
  puts(_("%s error:  invalid option descriptor for %s\n"));
#line 1081 "../usage.c"
  puts(_("%s error:  invalid option descriptor for %s\n"));
#line 385 "../find.c"
  puts(_("%s: invalid option name: %s\n"));
#line 527 "../find.c"
  puts(_("%s: The '%s' option requires an argument.\n"));
#line 156 "../autoopts.c"
  puts(_("(AutoOpts bug):  Equivalenced option '%s' was equivalenced to both\n"
       "\t'%s' and '%s'."));
#line 94 "../check.c"
  puts(_("%s error:  The %s option is required\n"));
#line 632 "../find.c"
  puts(_("%s: The '%s' option cannot have an argument.\n"));
#line 151 "../check.c"
  puts(_("%s: Command line arguments are not allowed.\n"));
#line 535 "../save.c"
  puts(_("error %d (%s) creating %s\n"));
#line 234 "../enum.c"
  puts(_("%s error:  '%s' does not match any %s keywords.\n"));
#line 93 "../reset.c"
  puts(_("%s error: The '%s' option requires an argument.\n"));
#line 184 "../save.c"
  puts(_("error %d (%s) stat-ing %s\n"));
#line 238 "../save.c"
  puts(_("error %d (%s) stat-ing %s\n"));
#line 143 "../restore.c"
  puts(_("%s error: no saved option state\n"));
#line 231 "../autoopts.c"
  puts(_("'%s' is not a command line option.\n"));
#line 111 "../time.c"
  puts(_("%s error:  '%s' is not a recognizable date/time.\n"));
#line 132 "../save.c"
  puts(_("'%s' not defined\n"));
#line 50 "../time.c"
  puts(_("%s error:  '%s' is not a recognizable time duration.\n"));
#line 92 "../check.c"
  puts(_("%s error:  The %s option must appear %d times.\n"));
#line 164 "../numeric.c"
  puts(_("%s error:  '%s' is not a recognizable number.\n"));
#line 200 "../enum.c"
  puts(_("%s error:  %s exceeds %s keyword count\n"));
#line 330 "../usage.c"
  puts(_("Try '%s %s' for more information.\n"));
#line 45 "../alias.c"
  puts(_("one %s%s option allowed\n"));
#line 208 "../makeshell.c"
  puts(_("standard output"));
#line 943 "../makeshell.c"
  puts(_("standard output"));
#line 274 "../usage.c"
  puts(_("standard output"));
#line 415 "../usage.c"
  puts(_("standard output"));
#line 625 "../usage.c"
  puts(_("standard output"));
#line 175 "../version.c"
  puts(_("standard output"));
#line 274 "../usage.c"
  puts(_("standard error"));
#line 415 "../usage.c"
  puts(_("standard error"));
#line 625 "../usage.c"
  puts(_("standard error"));
#line 175 "../version.c"
  puts(_("standard error"));
#line 208 "../makeshell.c"
  puts(_("write"));
#line 943 "../makeshell.c"
  puts(_("write"));
#line 273 "../usage.c"
  puts(_("write"));
#line 414 "../usage.c"
  puts(_("write"));
#line 624 "../usage.c"
  puts(_("write"));
#line 174 "../version.c"
  puts(_("write"));
#line 60 "../numeric.c"
  puts(_("%s error:  %s option value %ld is out of range.\n"));
#line 44 "../check.c"
  puts(_("%s error:  %s option requires the %s option\n"));
#line 131 "../save.c"
  puts(_("%s warning:  cannot save options - %s not regular file\n"));
#line 183 "../save.c"
  puts(_("%s warning:  cannot save options - %s not regular file\n"));
#line 237 "../save.c"
  puts(_("%s warning:  cannot save options - %s not regular file\n"));
#line 256 "../save.c"
  puts(_("%s warning:  cannot save options - %s not regular file\n"));
#line 534 "../save.c"
  puts(_("%s warning:  cannot save options - %s not regular file\n"));
  /* END-LIBOPTS-MESSAGES */

  /* USAGE-TEXT: */
#line 873 "../usage.c"
  puts(_("\t\t\t\t- an alternate for '%s'\n"));
#line 1148 "../usage.c"
  puts(_("Version, usage and configuration options:"));
#line 924 "../usage.c"
  puts(_("\t\t\t\t- default option for unnamed options\n"));
#line 837 "../usage.c"
  puts(_("\t\t\t\t- disabled as '--%s'\n"));
#line 1117 "../usage.c"
  puts(_(" --- %-14s %s\n"));
#line 1115 "../usage.c"
  puts(_("This option has been disabled"));
#line 864 "../usage.c"
  puts(_("\t\t\t\t- enabled by default\n"));
#line 40 "../alias.c"
  puts(_("%s error:  only "));
#line 1194 "../usage.c"
  puts(_(" - examining environment variables named %s_*\n"));
#line 168 "../file.c"
  puts(_("\t\t\t\t- file must not pre-exist\n"));
#line 172 "../file.c"
  puts(_("\t\t\t\t- file must pre-exist\n"));
#line 380 "../usage.c"
  puts(_("Options are specified by doubled hyphens and their name or by a single\n"
       "hyphen and the flag character.\n"));
#line 921 "../makeshell.c"
  puts(_("\n"
       "= = = = = = = =\n\n"
       "This incarnation of genshell will produce\n"
       "a shell script to parse the options for %s:\n\n"));
#line 166 "../enum.c"
  puts(_("  or an integer mask with any of the lower %d bits set\n"));
#line 897 "../usage.c"
  puts(_("\t\t\t\t- is a set membership option\n"));
#line 918 "../usage.c"
  puts(_("\t\t\t\t- must appear between %d and %d times\n"));
#line 382 "../usage.c"
  puts(_("Options are specified by single or double hyphens and their name.\n"));
#line 904 "../usage.c"
  puts(_("\t\t\t\t- may appear multiple times\n"));
#line 891 "../usage.c"
  puts(_("\t\t\t\t- may not be preset\n"));
#line 1309 "../usage.c"
  puts(_("   Arg Option-Name    Description\n"));
#line 1245 "../usage.c"
  puts(_("  Flg Arg Option-Name    Description\n"));
#line 1303 "../usage.c"
  puts(_("  Flg Arg Option-Name    Description\n"));
#line 1304 "../usage.c"
  puts(_(" %3s %s"));
#line 1310 "../usage.c"
  puts(_(" %3s %s"));
#line 387 "../usage.c"
  puts(_("The '-#<number>' option may omit the hash char\n"));
#line 383 "../usage.c"
  puts(_("All arguments are named options.\n"));
#line 971 "../usage.c"
  puts(_(" - reading file %s"));
#line 409 "../usage.c"
  puts(_("\n"
       "Please send bug reports to:  <%s>\n"));
#line 100 "../version.c"
  puts(_("\n"
       "Please send bug reports to:  <%s>\n"));
#line 129 "../version.c"
  puts(_("\n"
       "Please send bug reports to:  <%s>\n"));
#line 903 "../usage.c"
  puts(_("\t\t\t\t- may NOT appear - preset only\n"));
#line 944 "../usage.c"
  puts(_("\n"
       "The following option preset mechanisms are supported:\n"));
#line 1192 "../usage.c"
  puts(_("\n"
       "The following option preset mechanisms are supported:\n"));
#line 682 "../usage.c"
  puts(_("prohibits these options:\n"));
#line 677 "../usage.c"
  puts(_("prohibits the option '%s'\n"));
#line 81 "../numeric.c"
  puts(_("%s%ld to %ld"));
#line 79 "../numeric.c"
  puts(_("%sgreater than or equal to %ld"));
#line 75 "../numeric.c"
  puts(_("%s%ld exactly"));
#line 68 "../numeric.c"
  puts(_("%sit must lie in one of the ranges:\n"));
#line 68 "../numeric.c"
  puts(_("%sit must be in the range:\n"));
#line 88 "../numeric.c"
  puts(_(", or\n"));
#line 66 "../numeric.c"
  puts(_("%sis scalable with a suffix: k/K/m/M/g/G/t/T\n"));
#line 77 "../numeric.c"
  puts(_("%sless than or equal to %ld"));
#line 390 "../usage.c"
  puts(_("Operands and options may be intermixed.  They will be reordered.\n"));
#line 652 "../usage.c"
  puts(_("requires the option '%s'\n"));
#line 655 "../usage.c"
  puts(_("requires these options:\n"));
#line 1321 "../usage.c"
  puts(_("   Arg Option-Name   Req?  Description\n"));
#line 1315 "../usage.c"
  puts(_("  Flg Arg Option-Name   Req?  Description\n"));
#line 167 "../enum.c"
  puts(_("or you may use a numeric representation.  Preceding these with a '!'\n"
       "will clear the bits, specifying 'none' will clear all bits, and 'all'\n"
       "will set them all.  Multiple entries may be passed as an option\n"
       "argument list.\n"));
#line 910 "../usage.c"
  puts(_("\t\t\t\t- may appear up to %d times\n"));
#line 77 "../enum.c"
  puts(_("The valid \"%s\" option keywords are:\n"));
#line 1152 "../usage.c"
  puts(_("The next option supports vendor supported extra options:"));
#line 773 "../usage.c"
  puts(_("These additional options are:"));
  /* END-USAGE-TEXT */
}
#endif /* uncompilable code */
#ifdef  __cplusplus
}
#endif
/* ntp-keygen-opts.c ends here */
