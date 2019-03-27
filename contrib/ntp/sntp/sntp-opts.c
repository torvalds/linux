/*
 *  EDIT THIS FILE WITH CAUTION  (sntp-opts.c)
 *
 *  It has been AutoGen-ed  February 20, 2019 at 09:55:45 AM by AutoGen 5.18.5
 *  From the definitions    sntp-opts.def
 *  and the template file   options
 *
 * Generated from AutoOpts 41:1:16 templates.
 *
 *  AutoOpts is a copyrighted work.  This source file is not encumbered
 *  by AutoOpts licensing, but is provided under the licensing terms chosen
 *  by the sntp author or copyright holder.  AutoOpts is
 *  licensed under the terms of the LGPL.  The redistributable library
 *  (``libopts'') is licensed under the terms of either the LGPL or, at the
 *  users discretion, the BSD license.  See the AutoOpts and/or libopts sources
 *  for details.
 *
 * The sntp program is copyrighted and licensed
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
#include "sntp-opts.h"
#include <sys/types.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef  __cplusplus
extern "C" {
#endif
extern FILE * option_usage_fp;
#define zCopyright      (sntp_opt_strs+0)
#define zLicenseDescrip (sntp_opt_strs+341)

/*
 *  global included definitions
 */
#ifdef __windows
  extern int atoi(const char*);
#else
# include <stdlib.h>
#endif

#ifndef NULL
#  define NULL 0
#endif

/**
 *  static const strings for sntp options
 */
static char const sntp_opt_strs[2566] =
/*     0 */ "sntp 4.2.8p13\n"
            "Copyright (C) 1992-2017 The University of Delaware and Network Time Foundation, all rights reserved.\n"
            "This is free software. It is licensed for use, modification and\n"
            "redistribution under the terms of the NTP License, copies of which\n"
            "can be seen at:\n"
            "  <http://ntp.org/license>\n"
            "  <http://opensource.org/licenses/ntp-license.php>\n\0"
/*   341 */ "Permission to use, copy, modify, and distribute this software and its\n"
            "documentation for any purpose with or without fee is hereby granted,\n"
            "provided that the above copyright notice appears in all copies and that\n"
            "both the copyright notice and this permission notice appear in supporting\n"
            "documentation, and that the name The University of Delaware not be used in\n"
            "advertising or publicity pertaining to distribution of the software without\n"
            "specific, written prior permission.  The University of Delaware and Network\n"
            "Time Foundation makes no representations about the suitability this\n"
            "software for any purpose.  It is provided \"as is\" without express or\n"
            "implied warranty.\n\0"
/*  1009 */ "Force IPv4 DNS name resolution\0"
/*  1040 */ "IPV4\0"
/*  1045 */ "ipv4\0"
/*  1050 */ "Force IPv6 DNS name resolution\0"
/*  1081 */ "IPV6\0"
/*  1086 */ "ipv6\0"
/*  1091 */ "Enable authentication with the key auth-keynumber\0"
/*  1141 */ "AUTHENTICATION\0"
/*  1156 */ "authentication\0"
/*  1171 */ "Listen to the address specified for broadcast time sync\0"
/*  1227 */ "BROADCAST\0"
/*  1237 */ "broadcast\0"
/*  1247 */ "Concurrently query all IPs returned for host-name\0"
/*  1297 */ "CONCURRENT\0"
/*  1308 */ "concurrent\0"
/*  1319 */ "Increase debug verbosity level\0"
/*  1350 */ "DEBUG_LEVEL\0"
/*  1362 */ "debug-level\0"
/*  1374 */ "Set the debug verbosity level\0"
/*  1404 */ "SET_DEBUG_LEVEL\0"
/*  1420 */ "set-debug-level\0"
/*  1436 */ "The gap (in milliseconds) between time requests\0"
/*  1484 */ "GAP\0"
/*  1488 */ "gap\0"
/*  1492 */ "KoD history filename\0"
/*  1513 */ "KOD\0"
/*  1517 */ "kod\0"
/*  1521 */ "/var/db/ntp-kod\0"
/*  1537 */ "Look in this file for the key specified with -a\0"
/*  1585 */ "KEYFILE\0"
/*  1593 */ "keyfile\0"
/*  1601 */ "/etc/ntp.keys\0"
/*  1615 */ "Log to specified logfile\0"
/*  1640 */ "LOGFILE\0"
/*  1648 */ "logfile\0"
/*  1656 */ "Adjustments less than steplimit msec will be slewed\0"
/*  1708 */ "STEPLIMIT\0"
/*  1718 */ "steplimit\0"
/*  1728 */ "Send int as our NTP protocol version\0"
/*  1765 */ "NTPVERSION\0"
/*  1776 */ "ntpversion\0"
/*  1787 */ "Use the NTP Reserved Port (port 123)\0"
/*  1824 */ "USERESERVEDPORT\0"
/*  1840 */ "usereservedport\0"
/*  1856 */ "OK to 'step' the time with settimeofday(2)\0"
/*  1899 */ "STEP\0"
/*  1904 */ "step\0"
/*  1909 */ "OK to 'slew' the time with adjtime(2)\0"
/*  1947 */ "SLEW\0"
/*  1952 */ "slew\0"
/*  1957 */ "The number of seconds to wait for responses\0"
/*  2001 */ "TIMEOUT\0"
/*  2009 */ "timeout\0"
/*  2017 */ "Wait for pending replies (if not setting the time)\0"
/*  2068 */ "WAIT\0"
/*  2073 */ "no-wait\0"
/*  2081 */ "no\0"
/*  2084 */ "display extended usage information and exit\0"
/*  2128 */ "help\0"
/*  2133 */ "extended usage information passed thru pager\0"
/*  2178 */ "more-help\0"
/*  2188 */ "output version information and exit\0"
/*  2224 */ "version\0"
/*  2232 */ "save the option state to a config file\0"
/*  2271 */ "save-opts\0"
/*  2281 */ "load options from a config file\0"
/*  2313 */ "LOAD_OPTS\0"
/*  2323 */ "no-load-opts\0"
/*  2336 */ "SNTP\0"
/*  2341 */ "sntp - standard Simple Network Time Protocol client program - Ver. 4.2.8p13\n"
            "Usage:  %s [ -<flag> [<val>] | --<name>[{=| }<val>] ]... \\\n"
            "\t\t[ hostname-or-IP ...]\n\0"
/*  2501 */ "$HOME\0"
/*  2507 */ ".\0"
/*  2509 */ ".ntprc\0"
/*  2516 */ "http://bugs.ntp.org, bugs@ntp.org\0"
/*  2550 */ "\n\0"
/*  2552 */ "sntp 4.2.8p13";

/**
 *  ipv4 option description with
 *  "Must also have options" and "Incompatible options":
 */
/** Descriptive text for the ipv4 option */
#define IPV4_DESC      (sntp_opt_strs+1009)
/** Upper-cased name for the ipv4 option */
#define IPV4_NAME      (sntp_opt_strs+1040)
/** Name string for the ipv4 option */
#define IPV4_name      (sntp_opt_strs+1045)
/** Other options that appear in conjunction with the ipv4 option */
static int const aIpv4CantList[] = {
    INDEX_OPT_IPV6, NO_EQUIVALENT };
/** Compiled in flag settings for the ipv4 option */
#define IPV4_FLAGS     (OPTST_DISABLED)

/**
 *  ipv6 option description with
 *  "Must also have options" and "Incompatible options":
 */
/** Descriptive text for the ipv6 option */
#define IPV6_DESC      (sntp_opt_strs+1050)
/** Upper-cased name for the ipv6 option */
#define IPV6_NAME      (sntp_opt_strs+1081)
/** Name string for the ipv6 option */
#define IPV6_name      (sntp_opt_strs+1086)
/** Other options that appear in conjunction with the ipv6 option */
static int const aIpv6CantList[] = {
    INDEX_OPT_IPV4, NO_EQUIVALENT };
/** Compiled in flag settings for the ipv6 option */
#define IPV6_FLAGS     (OPTST_DISABLED)

/**
 *  authentication option description:
 */
/** Descriptive text for the authentication option */
#define AUTHENTICATION_DESC      (sntp_opt_strs+1091)
/** Upper-cased name for the authentication option */
#define AUTHENTICATION_NAME      (sntp_opt_strs+1141)
/** Name string for the authentication option */
#define AUTHENTICATION_name      (sntp_opt_strs+1156)
/** Compiled in flag settings for the authentication option */
#define AUTHENTICATION_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

/**
 *  broadcast option description:
 */
/** Descriptive text for the broadcast option */
#define BROADCAST_DESC      (sntp_opt_strs+1171)
/** Upper-cased name for the broadcast option */
#define BROADCAST_NAME      (sntp_opt_strs+1227)
/** Name string for the broadcast option */
#define BROADCAST_name      (sntp_opt_strs+1237)
/** Compiled in flag settings for the broadcast option */
#define BROADCAST_FLAGS     (OPTST_DISABLED | OPTST_STACKED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

/**
 *  concurrent option description:
 */
/** Descriptive text for the concurrent option */
#define CONCURRENT_DESC      (sntp_opt_strs+1247)
/** Upper-cased name for the concurrent option */
#define CONCURRENT_NAME      (sntp_opt_strs+1297)
/** Name string for the concurrent option */
#define CONCURRENT_name      (sntp_opt_strs+1308)
/** Compiled in flag settings for the concurrent option */
#define CONCURRENT_FLAGS     (OPTST_DISABLED | OPTST_STACKED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

/**
 *  debug-level option description:
 */
/** Descriptive text for the debug-level option */
#define DEBUG_LEVEL_DESC      (sntp_opt_strs+1319)
/** Upper-cased name for the debug-level option */
#define DEBUG_LEVEL_NAME      (sntp_opt_strs+1350)
/** Name string for the debug-level option */
#define DEBUG_LEVEL_name      (sntp_opt_strs+1362)
/** Compiled in flag settings for the debug-level option */
#define DEBUG_LEVEL_FLAGS     (OPTST_DISABLED)

/**
 *  set-debug-level option description:
 */
/** Descriptive text for the set-debug-level option */
#define SET_DEBUG_LEVEL_DESC      (sntp_opt_strs+1374)
/** Upper-cased name for the set-debug-level option */
#define SET_DEBUG_LEVEL_NAME      (sntp_opt_strs+1404)
/** Name string for the set-debug-level option */
#define SET_DEBUG_LEVEL_name      (sntp_opt_strs+1420)
/** Compiled in flag settings for the set-debug-level option */
#define SET_DEBUG_LEVEL_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

/**
 *  gap option description:
 */
/** Descriptive text for the gap option */
#define GAP_DESC      (sntp_opt_strs+1436)
/** Upper-cased name for the gap option */
#define GAP_NAME      (sntp_opt_strs+1484)
/** Name string for the gap option */
#define GAP_name      (sntp_opt_strs+1488)
/** The compiled in default value for the gap option argument */
#define GAP_DFT_ARG   ((char const*)50)
/** Compiled in flag settings for the gap option */
#define GAP_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

/**
 *  kod option description:
 */
/** Descriptive text for the kod option */
#define KOD_DESC      (sntp_opt_strs+1492)
/** Upper-cased name for the kod option */
#define KOD_NAME      (sntp_opt_strs+1513)
/** Name string for the kod option */
#define KOD_name      (sntp_opt_strs+1517)
/** The compiled in default value for the kod option argument */
#define KOD_DFT_ARG   (sntp_opt_strs+1521)
/** Compiled in flag settings for the kod option */
#define KOD_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_FILE))

/**
 *  keyfile option description:
 */
/** Descriptive text for the keyfile option */
#define KEYFILE_DESC      (sntp_opt_strs+1537)
/** Upper-cased name for the keyfile option */
#define KEYFILE_NAME      (sntp_opt_strs+1585)
/** Name string for the keyfile option */
#define KEYFILE_name      (sntp_opt_strs+1593)
/** The compiled in default value for the keyfile option argument */
#define KEYFILE_DFT_ARG   (sntp_opt_strs+1601)
/** Compiled in flag settings for the keyfile option */
#define KEYFILE_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_FILE))

/**
 *  logfile option description:
 */
/** Descriptive text for the logfile option */
#define LOGFILE_DESC      (sntp_opt_strs+1615)
/** Upper-cased name for the logfile option */
#define LOGFILE_NAME      (sntp_opt_strs+1640)
/** Name string for the logfile option */
#define LOGFILE_name      (sntp_opt_strs+1648)
/** Compiled in flag settings for the logfile option */
#define LOGFILE_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_FILE))

/**
 *  steplimit option description:
 */
/** Descriptive text for the steplimit option */
#define STEPLIMIT_DESC      (sntp_opt_strs+1656)
/** Upper-cased name for the steplimit option */
#define STEPLIMIT_NAME      (sntp_opt_strs+1708)
/** Name string for the steplimit option */
#define STEPLIMIT_name      (sntp_opt_strs+1718)
/** Compiled in flag settings for the steplimit option */
#define STEPLIMIT_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

/**
 *  ntpversion option description:
 */
/** Descriptive text for the ntpversion option */
#define NTPVERSION_DESC      (sntp_opt_strs+1728)
/** Upper-cased name for the ntpversion option */
#define NTPVERSION_NAME      (sntp_opt_strs+1765)
/** Name string for the ntpversion option */
#define NTPVERSION_name      (sntp_opt_strs+1776)
/** The compiled in default value for the ntpversion option argument */
#define NTPVERSION_DFT_ARG   ((char const*)4)
/** Compiled in flag settings for the ntpversion option */
#define NTPVERSION_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

/**
 *  usereservedport option description:
 */
/** Descriptive text for the usereservedport option */
#define USERESERVEDPORT_DESC      (sntp_opt_strs+1787)
/** Upper-cased name for the usereservedport option */
#define USERESERVEDPORT_NAME      (sntp_opt_strs+1824)
/** Name string for the usereservedport option */
#define USERESERVEDPORT_name      (sntp_opt_strs+1840)
/** Compiled in flag settings for the usereservedport option */
#define USERESERVEDPORT_FLAGS     (OPTST_DISABLED)

/**
 *  step option description:
 */
/** Descriptive text for the step option */
#define STEP_DESC      (sntp_opt_strs+1856)
/** Upper-cased name for the step option */
#define STEP_NAME      (sntp_opt_strs+1899)
/** Name string for the step option */
#define STEP_name      (sntp_opt_strs+1904)
/** Compiled in flag settings for the step option */
#define STEP_FLAGS     (OPTST_DISABLED)

/**
 *  slew option description:
 */
/** Descriptive text for the slew option */
#define SLEW_DESC      (sntp_opt_strs+1909)
/** Upper-cased name for the slew option */
#define SLEW_NAME      (sntp_opt_strs+1947)
/** Name string for the slew option */
#define SLEW_name      (sntp_opt_strs+1952)
/** Compiled in flag settings for the slew option */
#define SLEW_FLAGS     (OPTST_DISABLED)

/**
 *  timeout option description:
 */
/** Descriptive text for the timeout option */
#define TIMEOUT_DESC      (sntp_opt_strs+1957)
/** Upper-cased name for the timeout option */
#define TIMEOUT_NAME      (sntp_opt_strs+2001)
/** Name string for the timeout option */
#define TIMEOUT_name      (sntp_opt_strs+2009)
/** The compiled in default value for the timeout option argument */
#define TIMEOUT_DFT_ARG   ((char const*)5)
/** Compiled in flag settings for the timeout option */
#define TIMEOUT_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

/**
 *  wait option description:
 */
/** Descriptive text for the wait option */
#define WAIT_DESC      (sntp_opt_strs+2017)
/** Upper-cased name for the wait option */
#define WAIT_NAME      (sntp_opt_strs+2068)
/** disablement name for the wait option */
#define NOT_WAIT_name  (sntp_opt_strs+2073)
/** disablement prefix for the wait option */
#define NOT_WAIT_PFX   (sntp_opt_strs+2081)
/** Name string for the wait option */
#define WAIT_name      (NOT_WAIT_name + 3)
/** Compiled in flag settings for the wait option */
#define WAIT_FLAGS     (OPTST_INITENABLED)

/*
 *  Help/More_Help/Version option descriptions:
 */
#define HELP_DESC       (sntp_opt_strs+2084)
#define HELP_name       (sntp_opt_strs+2128)
#ifdef HAVE_WORKING_FORK
#define MORE_HELP_DESC  (sntp_opt_strs+2133)
#define MORE_HELP_name  (sntp_opt_strs+2178)
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
#define VER_DESC        (sntp_opt_strs+2188)
#define VER_name        (sntp_opt_strs+2224)
#define SAVE_OPTS_DESC  (sntp_opt_strs+2232)
#define SAVE_OPTS_name  (sntp_opt_strs+2271)
#define LOAD_OPTS_DESC     (sntp_opt_strs+2281)
#define LOAD_OPTS_NAME     (sntp_opt_strs+2313)
#define NO_LOAD_OPTS_name  (sntp_opt_strs+2323)
#define LOAD_OPTS_pfx      (sntp_opt_strs+2081)
#define LOAD_OPTS_name     (NO_LOAD_OPTS_name + 3)
/**
 *  Declare option callback procedures
 */
extern tOptProc
    ntpOptionPrintVersion, optionBooleanVal,      optionNestedVal,
    optionNumericVal,      optionPagedUsage,      optionResetOpt,
    optionStackArg,        optionTimeDate,        optionTimeVal,
    optionUnstackArg,      optionVendorOption;
static tOptProc
    doOptDebug_Level, doOptKeyfile,     doOptKod,         doOptLogfile,
    doOptNtpversion,  doOptSteplimit,   doUsageOpt;
#define VER_PROC        ntpOptionPrintVersion

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 *  Define the sntp Option Descriptions.
 * This is an array of OPTION_CT entries, one for each
 * option that the sntp program responds to.
 */
static tOptDesc optDesc[OPTION_CT] = {
  {  /* entry idx, value */ 0, VALUE_OPT_IPV4,
     /* equiv idx, value */ 0, VALUE_OPT_IPV4,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ IPV4_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --ipv4 */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, aIpv4CantList,
     /* option proc      */ NULL,
     /* desc, NAME, name */ IPV4_DESC, IPV4_NAME, IPV4_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 1, VALUE_OPT_IPV6,
     /* equiv idx, value */ 1, VALUE_OPT_IPV6,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ IPV6_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --ipv6 */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, aIpv6CantList,
     /* option proc      */ NULL,
     /* desc, NAME, name */ IPV6_DESC, IPV6_NAME, IPV6_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 2, VALUE_OPT_AUTHENTICATION,
     /* equiv idx, value */ 2, VALUE_OPT_AUTHENTICATION,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ AUTHENTICATION_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --authentication */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionNumericVal,
     /* desc, NAME, name */ AUTHENTICATION_DESC, AUTHENTICATION_NAME, AUTHENTICATION_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 3, VALUE_OPT_BROADCAST,
     /* equiv idx, value */ 3, VALUE_OPT_BROADCAST,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ BROADCAST_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --broadcast */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionStackArg,
     /* desc, NAME, name */ BROADCAST_DESC, BROADCAST_NAME, BROADCAST_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 4, VALUE_OPT_CONCURRENT,
     /* equiv idx, value */ 4, VALUE_OPT_CONCURRENT,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ CONCURRENT_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --concurrent */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionStackArg,
     /* desc, NAME, name */ CONCURRENT_DESC, CONCURRENT_NAME, CONCURRENT_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 5, VALUE_OPT_DEBUG_LEVEL,
     /* equiv idx, value */ 5, VALUE_OPT_DEBUG_LEVEL,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ DEBUG_LEVEL_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --debug-level */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ doOptDebug_Level,
     /* desc, NAME, name */ DEBUG_LEVEL_DESC, DEBUG_LEVEL_NAME, DEBUG_LEVEL_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 6, VALUE_OPT_SET_DEBUG_LEVEL,
     /* equiv idx, value */ 6, VALUE_OPT_SET_DEBUG_LEVEL,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ SET_DEBUG_LEVEL_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --set-debug-level */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionNumericVal,
     /* desc, NAME, name */ SET_DEBUG_LEVEL_DESC, SET_DEBUG_LEVEL_NAME, SET_DEBUG_LEVEL_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 7, VALUE_OPT_GAP,
     /* equiv idx, value */ 7, VALUE_OPT_GAP,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ GAP_FLAGS, 0,
     /* last opt argumnt */ { GAP_DFT_ARG },
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionNumericVal,
     /* desc, NAME, name */ GAP_DESC, GAP_NAME, GAP_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 8, VALUE_OPT_KOD,
     /* equiv idx, value */ 8, VALUE_OPT_KOD,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ KOD_FLAGS, 0,
     /* last opt argumnt */ { KOD_DFT_ARG },
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ doOptKod,
     /* desc, NAME, name */ KOD_DESC, KOD_NAME, KOD_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 9, VALUE_OPT_KEYFILE,
     /* equiv idx, value */ 9, VALUE_OPT_KEYFILE,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ KEYFILE_FLAGS, 0,
     /* last opt argumnt */ { KEYFILE_DFT_ARG },
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ doOptKeyfile,
     /* desc, NAME, name */ KEYFILE_DESC, KEYFILE_NAME, KEYFILE_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 10, VALUE_OPT_LOGFILE,
     /* equiv idx, value */ 10, VALUE_OPT_LOGFILE,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ LOGFILE_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --logfile */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ doOptLogfile,
     /* desc, NAME, name */ LOGFILE_DESC, LOGFILE_NAME, LOGFILE_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 11, VALUE_OPT_STEPLIMIT,
     /* equiv idx, value */ 11, VALUE_OPT_STEPLIMIT,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ STEPLIMIT_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --steplimit */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ doOptSteplimit,
     /* desc, NAME, name */ STEPLIMIT_DESC, STEPLIMIT_NAME, STEPLIMIT_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 12, VALUE_OPT_NTPVERSION,
     /* equiv idx, value */ 12, VALUE_OPT_NTPVERSION,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ NTPVERSION_FLAGS, 0,
     /* last opt argumnt */ { NTPVERSION_DFT_ARG },
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ doOptNtpversion,
     /* desc, NAME, name */ NTPVERSION_DESC, NTPVERSION_NAME, NTPVERSION_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 13, VALUE_OPT_USERESERVEDPORT,
     /* equiv idx, value */ 13, VALUE_OPT_USERESERVEDPORT,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ USERESERVEDPORT_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --usereservedport */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ USERESERVEDPORT_DESC, USERESERVEDPORT_NAME, USERESERVEDPORT_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 14, VALUE_OPT_STEP,
     /* equiv idx, value */ 14, VALUE_OPT_STEP,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ STEP_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --step */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ STEP_DESC, STEP_NAME, STEP_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 15, VALUE_OPT_SLEW,
     /* equiv idx, value */ 15, VALUE_OPT_SLEW,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ SLEW_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --slew */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ SLEW_DESC, SLEW_NAME, SLEW_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 16, VALUE_OPT_TIMEOUT,
     /* equiv idx, value */ 16, VALUE_OPT_TIMEOUT,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ TIMEOUT_FLAGS, 0,
     /* last opt argumnt */ { TIMEOUT_DFT_ARG },
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionNumericVal,
     /* desc, NAME, name */ TIMEOUT_DESC, TIMEOUT_NAME, TIMEOUT_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 17, VALUE_OPT_WAIT,
     /* equiv idx, value */ 17, VALUE_OPT_WAIT,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ WAIT_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --wait */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ WAIT_DESC, WAIT_NAME, WAIT_name,
     /* disablement strs */ NOT_WAIT_name, NOT_WAIT_PFX },

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
/** Reference to the upper cased version of sntp. */
#define zPROGNAME       (sntp_opt_strs+2336)
/** Reference to the title line for sntp usage. */
#define zUsageTitle     (sntp_opt_strs+2341)
/** sntp configuration file name. */
#define zRcName         (sntp_opt_strs+2509)
/** Directories to search for sntp config files. */
static char const * const apzHomeList[3] = {
    sntp_opt_strs+2501,
    sntp_opt_strs+2507,
    NULL };
/** The sntp program bug email address. */
#define zBugsAddr       (sntp_opt_strs+2516)
/** Clarification/explanation of what sntp does. */
#define zExplain        (sntp_opt_strs+2550)
/** Extra detail explaining what sntp does. */
#define zDetail         (NULL)
/** The full version string for sntp. */
#define zFullVersion    (sntp_opt_strs+2552)
/* extracted from optcode.tlib near line 364 */

#if defined(ENABLE_NLS)
# define OPTPROC_BASE OPTPROC_TRANSLATE
  static tOptionXlateProc translate_option_strings;
#else
# define OPTPROC_BASE OPTPROC_NONE
# define translate_option_strings NULL
#endif /* ENABLE_NLS */

#define sntp_full_usage (NULL)
#define sntp_short_usage (NULL)

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
    ex_code = SNTP_EXIT_SUCCESS;
    optionUsage(&sntpOptions, ex_code);
    /* NOTREACHED */
    exit(1);
    (void)opts;
    (void)od;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 * Code to handle the debug-level option.
 *
 * @param[in] pOptions the sntp options data structure
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
 * Code to handle the kod option.
 * Specifies the filename to be used for the persistent history of KoD
 * responses received from servers.  If the file does not exist, a
 * warning message will be displayed.  The file will not be created.
 * @param[in] pOptions the sntp options data structure
 * @param[in,out] pOptDesc the option descriptor for this option.
 */
static void
doOptKod(tOptions* pOptions, tOptDesc* pOptDesc)
{
    static teOptFileType const  type =
        FTYPE_MODE_MAY_EXIST + FTYPE_MODE_NO_OPEN;
    static tuFileMode           mode;
#ifndef O_CLOEXEC
#  define O_CLOEXEC 0
#endif
    mode.file_flags = O_CLOEXEC;

    /*
     * This function handles special invalid values for "pOptions"
     */
    optionFileCheck(pOptions, pOptDesc, type, mode);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 * Code to handle the keyfile option.
 * This option specifies the keyfile.
 * @code{sntp} will search for the key specified with @option{-a}
 * @file{keyno} in this file.  See @command{ntp.keys(5)} for more
 * information.
 * @param[in] pOptions the sntp options data structure
 * @param[in,out] pOptDesc the option descriptor for this option.
 */
static void
doOptKeyfile(tOptions* pOptions, tOptDesc* pOptDesc)
{
    static teOptFileType const  type =
        FTYPE_MODE_MAY_EXIST + FTYPE_MODE_NO_OPEN;
    static tuFileMode           mode;
#ifndef O_CLOEXEC
#  define O_CLOEXEC 0
#endif
    mode.file_flags = O_CLOEXEC;

    /*
     * This function handles special invalid values for "pOptions"
     */
    optionFileCheck(pOptions, pOptDesc, type, mode);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 * Code to handle the logfile option.
 * This option causes the client to write log messages to the specified
 * @file{logfile}.
 * @param[in] pOptions the sntp options data structure
 * @param[in,out] pOptDesc the option descriptor for this option.
 */
static void
doOptLogfile(tOptions* pOptions, tOptDesc* pOptDesc)
{
    static teOptFileType const  type =
        FTYPE_MODE_MAY_EXIST + FTYPE_MODE_NO_OPEN;
    static tuFileMode           mode;
#ifndef O_CLOEXEC
#  define O_CLOEXEC 0
#endif
    mode.file_flags = O_CLOEXEC;

    /*
     * This function handles special invalid values for "pOptions"
     */
    optionFileCheck(pOptions, pOptDesc, type, mode);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 * Code to handle the steplimit option.
 * If the time adjustment is less than @file{steplimit} milliseconds,
 * slew the amount using @command{adjtime(2)}.  Otherwise, step the
 * correction using @command{settimeofday(2)}.  The default value is 0,
 * which means all adjustments will be stepped.  This is a feature, as
 * different situations demand different values.
 * @param[in] pOptions the sntp options data structure
 * @param[in,out] pOptDesc the option descriptor for this option.
 */
static void
doOptSteplimit(tOptions* pOptions, tOptDesc* pOptDesc)
{
    static struct {long rmin, rmax;} const rng[1] = {
        { 0, LONG_MAX } };
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

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 * Code to handle the ntpversion option.
 * When sending requests to a remote server, tell them we are running
 * NTP protocol version @file{ntpversion} .
 * @param[in] pOptions the sntp options data structure
 * @param[in,out] pOptDesc the option descriptor for this option.
 */
static void
doOptNtpversion(tOptions* pOptions, tOptDesc* pOptDesc)
{
    static struct {long rmin, rmax;} const rng[1] = {
        { 0, 7 } };
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
/* extracted from optmain.tlib near line 1250 */

/**
 * The directory containing the data associated with sntp.
 */
#ifndef  PKGDATADIR
# define PKGDATADIR ""
#endif

/**
 * Information about the person or institution that packaged sntp
 * for the current distribution.
 */
#ifndef  WITH_PACKAGER
# define sntp_packager_info NULL
#else
/** Packager information for sntp. */
static char const sntp_packager_info[] =
    "Packaged by " WITH_PACKAGER

# ifdef WITH_PACKAGER_VERSION
        " ("WITH_PACKAGER_VERSION")"
# endif

# ifdef WITH_PACKAGER_BUG_REPORTS
    "\nReport sntp bugs to " WITH_PACKAGER_BUG_REPORTS
# endif
    "\n";
#endif
#ifndef __doxygen__

#endif /* __doxygen__ */
/**
 * The option definitions for sntp.  The one structure that
 * binds them all.
 */
tOptions sntpOptions = {
    OPTIONS_STRUCT_VERSION,
    0, NULL,                    /* original argc + argv    */
    ( OPTPROC_BASE
    + OPTPROC_ERRSTOP
    + OPTPROC_SHORTOPT
    + OPTPROC_LONGOPT
    + OPTPROC_NO_REQ_OPT
    + OPTPROC_NEGATIONS
    + OPTPROC_ENVIRON
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
    23 /* full option count */, 18 /* user option count */,
    sntp_full_usage, sntp_short_usage,
    NULL, NULL,
    PKGDATADIR, sntp_packager_info
};

#if ENABLE_NLS
/**
 * This code is designed to translate translatable option text for the
 * sntp program.  These translations happen upon entry
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
        exit(SNTP_EXIT_FAILURE);
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
 * Translate all the translatable strings in the sntpOptions
 * structure defined above.  This is done only once.
 */
static void
translate_option_strings(void)
{
    tOptions * const opts = &sntpOptions;

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
     by a field name in the sntpOptions structure noted in the
     comments below.  The literal text is defined in sntp_opt_strs.
   
     NOTE: the strings below are segmented with respect to the source string
     sntp_opt_strs.  The strings above are handed off for translation
     at run time a paragraph at a time.  Consequently, they are presented here
     for translation a paragraph at a time.
   
     ALSO: often the description for an option will reference another option
     by name.  These are set off with apostrophe quotes (I hope).  Do not
     translate option names.
   */
  /* referenced via sntpOptions.pzCopyright */
  puts(_("sntp 4.2.8p13\n\
Copyright (C) 1992-2017 The University of Delaware and Network Time Foundation, all rights reserved.\n\
This is free software. It is licensed for use, modification and\n\
redistribution under the terms of the NTP License, copies of which\n\
can be seen at:\n"));
  puts(_("  <http://ntp.org/license>\n\
  <http://opensource.org/licenses/ntp-license.php>\n"));

  /* referenced via sntpOptions.pzCopyNotice */
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

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("Force IPv4 DNS name resolution"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("Force IPv6 DNS name resolution"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("Enable authentication with the key auth-keynumber"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("Listen to the address specified for broadcast time sync"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("Concurrently query all IPs returned for host-name"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("Increase debug verbosity level"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("Set the debug verbosity level"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("The gap (in milliseconds) between time requests"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("KoD history filename"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("Look in this file for the key specified with -a"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("Log to specified logfile"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("Adjustments less than steplimit msec will be slewed"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("Send int as our NTP protocol version"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("Use the NTP Reserved Port (port 123)"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("OK to 'step' the time with settimeofday(2)"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("OK to 'slew' the time with adjtime(2)"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("The number of seconds to wait for responses"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("Wait for pending replies (if not setting the time)"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("display extended usage information and exit"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("extended usage information passed thru pager"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("output version information and exit"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("save the option state to a config file"));

  /* referenced via sntpOptions.pOptDesc->pzText */
  puts(_("load options from a config file"));

  /* referenced via sntpOptions.pzUsageTitle */
  puts(_("sntp - standard Simple Network Time Protocol client program - Ver. 4.2.8p13\n\
Usage:  %s [ -<flag> [<val>] | --<name>[{=| }<val>] ]... \\\n\
\t\t[ hostname-or-IP ...]\n"));

  /* referenced via sntpOptions.pzExplain */
  puts(_("\n"));

  /* referenced via sntpOptions.pzFullVersion */
  puts(_("sntp 4.2.8p13"));

  /* referenced via sntpOptions.pzFullUsage */
  puts(_("<<<NOT-FOUND>>>"));

  /* referenced via sntpOptions.pzShortUsage */
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
/* sntp-opts.c ends here */
