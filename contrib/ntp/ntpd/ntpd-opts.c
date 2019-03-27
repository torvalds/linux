/*
 *  EDIT THIS FILE WITH CAUTION  (ntpd-opts.c)
 *
 *  It has been AutoGen-ed  February 20, 2019 at 09:56:15 AM by AutoGen 5.18.5
 *  From the definitions    ntpd-opts.def
 *  and the template file   options
 *
 * Generated from AutoOpts 41:1:16 templates.
 *
 *  AutoOpts is a copyrighted work.  This source file is not encumbered
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

#ifndef __doxygen__
#define OPTION_CODE_COMPILE 1
#include "ntpd-opts.h"
#include <sys/types.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef  __cplusplus
extern "C" {
#endif
extern FILE * option_usage_fp;
#define zCopyright      (ntpd_opt_strs+0)
#define zLicenseDescrip (ntpd_opt_strs+341)

/*
 *  global included definitions
 */
#ifdef __windows
  extern int atoi(const char *);
#else
# include <stdlib.h>
#endif

#ifdef __windows
  extern int atoi(const char*);
#else
# include <stdlib.h>
#endif

#ifndef NULL
#  define NULL 0
#endif

/**
 *  static const strings for ntpd options
 */
static char const ntpd_opt_strs[3132] =
/*     0 */ "ntpd 4.2.8p13\n"
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
/*  1091 */ "Require crypto authentication\0"
/*  1121 */ "AUTHREQ\0"
/*  1129 */ "authreq\0"
/*  1137 */ "Do not require crypto authentication\0"
/*  1174 */ "AUTHNOREQ\0"
/*  1184 */ "authnoreq\0"
/*  1194 */ "Allow us to sync to broadcast servers\0"
/*  1232 */ "BCASTSYNC\0"
/*  1242 */ "bcastsync\0"
/*  1252 */ "configuration file name\0"
/*  1276 */ "CONFIGFILE\0"
/*  1287 */ "configfile\0"
/*  1298 */ "Increase debug verbosity level\0"
/*  1329 */ "DEBUG_LEVEL\0"
/*  1341 */ "debug-level\0"
/*  1353 */ "Set the debug verbosity level\0"
/*  1383 */ "SET_DEBUG_LEVEL\0"
/*  1399 */ "set-debug-level\0"
/*  1415 */ "frequency drift file name\0"
/*  1441 */ "DRIFTFILE\0"
/*  1451 */ "driftfile\0"
/*  1461 */ "Allow the first adjustment to be Big\0"
/*  1498 */ "PANICGATE\0"
/*  1508 */ "panicgate\0"
/*  1518 */ "Step any initial offset correction.\0"
/*  1554 */ "FORCE_STEP_ONCE\0"
/*  1570 */ "force-step-once\0"
/*  1586 */ "Jail directory\0"
/*  1601 */ "JAILDIR\0"
/*  1609 */ "jaildir\0"
/*  1617 */ "built without --enable-clockctl or --enable-linuxcaps or --enable-solarisprivs\0"
/*  1696 */ "Listen on an interface name or address\0"
/*  1735 */ "INTERFACE\0"
/*  1745 */ "interface\0"
/*  1755 */ "path to symmetric keys\0"
/*  1778 */ "KEYFILE\0"
/*  1786 */ "keyfile\0"
/*  1794 */ "path to the log file\0"
/*  1815 */ "LOGFILE\0"
/*  1823 */ "logfile\0"
/*  1831 */ "Do not listen to virtual interfaces\0"
/*  1867 */ "NOVIRTUALIPS\0"
/*  1880 */ "novirtualips\0"
/*  1893 */ "Modify Multimedia Timer (Windows only)\0"
/*  1932 */ "MODIFYMMTIMER\0"
/*  1946 */ "modifymmtimer\0"
/*  1960 */ "Do not fork\0"
/*  1972 */ "NOFORK\0"
/*  1979 */ "nofork\0"
/*  1986 */ "Run at high priority\0"
/*  2007 */ "NICE\0"
/*  2012 */ "nice\0"
/*  2017 */ "path to the PID file\0"
/*  2038 */ "PIDFILE\0"
/*  2046 */ "pidfile\0"
/*  2054 */ "Process priority\0"
/*  2071 */ "PRIORITY\0"
/*  2080 */ "priority\0"
/*  2089 */ "Set the time and quit\0"
/*  2111 */ "QUIT\0"
/*  2116 */ "quit\0"
/*  2121 */ "Broadcast/propagation delay\0"
/*  2149 */ "PROPAGATIONDELAY\0"
/*  2166 */ "propagationdelay\0"
/*  2183 */ "Save parsed configuration and quit\0"
/*  2218 */ "SAVECONFIGQUIT\0"
/*  2233 */ "saveconfigquit\0"
/*  2248 */ "Statistics file location\0"
/*  2273 */ "STATSDIR\0"
/*  2282 */ "statsdir\0"
/*  2291 */ "Trusted key number\0"
/*  2310 */ "TRUSTEDKEY\0"
/*  2321 */ "trustedkey\0"
/*  2332 */ "Run as userid (or userid:groupid)\0"
/*  2366 */ "USER\0"
/*  2371 */ "user\0"
/*  2376 */ "interval in seconds between scans for new or dropped interfaces\0"
/*  2440 */ "UPDATEINTERVAL\0"
/*  2455 */ "updateinterval\0"
/*  2470 */ "make ARG an ntp variable (RW)\0"
/*  2500 */ "VAR\0"
/*  2504 */ "var\0"
/*  2508 */ "make ARG an ntp variable (RW|DEF)\0"
/*  2542 */ "DVAR\0"
/*  2547 */ "dvar\0"
/*  2552 */ "Seconds to wait for first clock sync\0"
/*  2589 */ "WAIT_SYNC\0"
/*  2599 */ "wait-sync\0"
/*  2609 */ "Slew up to 600 seconds\0"
/*  2632 */ "SLEW\0"
/*  2637 */ "slew\0"
/*  2642 */ "Use CPU cycle counter (Windows only)\0"
/*  2679 */ "USEPCC\0"
/*  2686 */ "usepcc\0"
/*  2693 */ "Force CPU cycle counter use (Windows only)\0"
/*  2736 */ "PCCFREQ\0"
/*  2744 */ "pccfreq\0"
/*  2752 */ "Register with mDNS as a NTP server\0"
/*  2787 */ "MDNS\0"
/*  2792 */ "mdns\0"
/*  2797 */ "display extended usage information and exit\0"
/*  2841 */ "help\0"
/*  2846 */ "extended usage information passed thru pager\0"
/*  2891 */ "more-help\0"
/*  2901 */ "output version information and exit\0"
/*  2937 */ "version\0"
/*  2945 */ "NTPD\0"
/*  2950 */ "ntpd - NTP daemon program - Ver. 4.2.8p13\n"
            "Usage:  %s [ -<flag> [<val>] | --<name>[{=| }<val>] ]... \\\n"
            "\t\t[ <server1> ... <serverN> ]\n\0"
/*  3082 */ "http://bugs.ntp.org, bugs@ntp.org\0"
/*  3116 */ "\n\0"
/*  3118 */ "ntpd 4.2.8p13";

/**
 *  ipv4 option description with
 *  "Must also have options" and "Incompatible options":
 */
/** Descriptive text for the ipv4 option */
#define IPV4_DESC      (ntpd_opt_strs+1009)
/** Upper-cased name for the ipv4 option */
#define IPV4_NAME      (ntpd_opt_strs+1040)
/** Name string for the ipv4 option */
#define IPV4_name      (ntpd_opt_strs+1045)
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
#define IPV6_DESC      (ntpd_opt_strs+1050)
/** Upper-cased name for the ipv6 option */
#define IPV6_NAME      (ntpd_opt_strs+1081)
/** Name string for the ipv6 option */
#define IPV6_name      (ntpd_opt_strs+1086)
/** Other options that appear in conjunction with the ipv6 option */
static int const aIpv6CantList[] = {
    INDEX_OPT_IPV4, NO_EQUIVALENT };
/** Compiled in flag settings for the ipv6 option */
#define IPV6_FLAGS     (OPTST_DISABLED)

/**
 *  authreq option description with
 *  "Must also have options" and "Incompatible options":
 */
/** Descriptive text for the authreq option */
#define AUTHREQ_DESC      (ntpd_opt_strs+1091)
/** Upper-cased name for the authreq option */
#define AUTHREQ_NAME      (ntpd_opt_strs+1121)
/** Name string for the authreq option */
#define AUTHREQ_name      (ntpd_opt_strs+1129)
/** Other options that appear in conjunction with the authreq option */
static int const aAuthreqCantList[] = {
    INDEX_OPT_AUTHNOREQ, NO_EQUIVALENT };
/** Compiled in flag settings for the authreq option */
#define AUTHREQ_FLAGS     (OPTST_DISABLED)

/**
 *  authnoreq option description with
 *  "Must also have options" and "Incompatible options":
 */
/** Descriptive text for the authnoreq option */
#define AUTHNOREQ_DESC      (ntpd_opt_strs+1137)
/** Upper-cased name for the authnoreq option */
#define AUTHNOREQ_NAME      (ntpd_opt_strs+1174)
/** Name string for the authnoreq option */
#define AUTHNOREQ_name      (ntpd_opt_strs+1184)
/** Other options that appear in conjunction with the authnoreq option */
static int const aAuthnoreqCantList[] = {
    INDEX_OPT_AUTHREQ, NO_EQUIVALENT };
/** Compiled in flag settings for the authnoreq option */
#define AUTHNOREQ_FLAGS     (OPTST_DISABLED)

/**
 *  bcastsync option description:
 */
/** Descriptive text for the bcastsync option */
#define BCASTSYNC_DESC      (ntpd_opt_strs+1194)
/** Upper-cased name for the bcastsync option */
#define BCASTSYNC_NAME      (ntpd_opt_strs+1232)
/** Name string for the bcastsync option */
#define BCASTSYNC_name      (ntpd_opt_strs+1242)
/** Compiled in flag settings for the bcastsync option */
#define BCASTSYNC_FLAGS     (OPTST_DISABLED)

/**
 *  configfile option description:
 */
/** Descriptive text for the configfile option */
#define CONFIGFILE_DESC      (ntpd_opt_strs+1252)
/** Upper-cased name for the configfile option */
#define CONFIGFILE_NAME      (ntpd_opt_strs+1276)
/** Name string for the configfile option */
#define CONFIGFILE_name      (ntpd_opt_strs+1287)
/** Compiled in flag settings for the configfile option */
#define CONFIGFILE_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

/**
 *  debug-level option description:
 */
/** Descriptive text for the debug-level option */
#define DEBUG_LEVEL_DESC      (ntpd_opt_strs+1298)
/** Upper-cased name for the debug-level option */
#define DEBUG_LEVEL_NAME      (ntpd_opt_strs+1329)
/** Name string for the debug-level option */
#define DEBUG_LEVEL_name      (ntpd_opt_strs+1341)
/** Compiled in flag settings for the debug-level option */
#define DEBUG_LEVEL_FLAGS     (OPTST_DISABLED)

/**
 *  set-debug-level option description:
 */
/** Descriptive text for the set-debug-level option */
#define SET_DEBUG_LEVEL_DESC      (ntpd_opt_strs+1353)
/** Upper-cased name for the set-debug-level option */
#define SET_DEBUG_LEVEL_NAME      (ntpd_opt_strs+1383)
/** Name string for the set-debug-level option */
#define SET_DEBUG_LEVEL_name      (ntpd_opt_strs+1399)
/** Compiled in flag settings for the set-debug-level option */
#define SET_DEBUG_LEVEL_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

/**
 *  driftfile option description:
 */
/** Descriptive text for the driftfile option */
#define DRIFTFILE_DESC      (ntpd_opt_strs+1415)
/** Upper-cased name for the driftfile option */
#define DRIFTFILE_NAME      (ntpd_opt_strs+1441)
/** Name string for the driftfile option */
#define DRIFTFILE_name      (ntpd_opt_strs+1451)
/** Compiled in flag settings for the driftfile option */
#define DRIFTFILE_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

/**
 *  panicgate option description:
 */
/** Descriptive text for the panicgate option */
#define PANICGATE_DESC      (ntpd_opt_strs+1461)
/** Upper-cased name for the panicgate option */
#define PANICGATE_NAME      (ntpd_opt_strs+1498)
/** Name string for the panicgate option */
#define PANICGATE_name      (ntpd_opt_strs+1508)
/** Compiled in flag settings for the panicgate option */
#define PANICGATE_FLAGS     (OPTST_DISABLED)

/**
 *  force_step_once option description:
 */
/** Descriptive text for the force_step_once option */
#define FORCE_STEP_ONCE_DESC      (ntpd_opt_strs+1518)
/** Upper-cased name for the force_step_once option */
#define FORCE_STEP_ONCE_NAME      (ntpd_opt_strs+1554)
/** Name string for the force_step_once option */
#define FORCE_STEP_ONCE_name      (ntpd_opt_strs+1570)
/** Compiled in flag settings for the force_step_once option */
#define FORCE_STEP_ONCE_FLAGS     (OPTST_DISABLED)

/**
 *  jaildir option description:
 */
#ifdef HAVE_DROPROOT
/** Descriptive text for the jaildir option */
#define JAILDIR_DESC      (ntpd_opt_strs+1586)
/** Upper-cased name for the jaildir option */
#define JAILDIR_NAME      (ntpd_opt_strs+1601)
/** Name string for the jaildir option */
#define JAILDIR_name      (ntpd_opt_strs+1609)
/** Compiled in flag settings for the jaildir option */
#define JAILDIR_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

#else   /* disable jaildir */
#define JAILDIR_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define JAILDIR_NAME      NULL
/** Descriptive text for the jaildir option */
#define JAILDIR_DESC      (ntpd_opt_strs+1617)
#define JAILDIR_name      (ntpd_opt_strs+1609)
#endif  /* HAVE_DROPROOT */

/**
 *  interface option description:
 */
/** Descriptive text for the interface option */
#define INTERFACE_DESC      (ntpd_opt_strs+1696)
/** Upper-cased name for the interface option */
#define INTERFACE_NAME      (ntpd_opt_strs+1735)
/** Name string for the interface option */
#define INTERFACE_name      (ntpd_opt_strs+1745)
/** Compiled in flag settings for the interface option */
#define INTERFACE_FLAGS     (OPTST_DISABLED | OPTST_STACKED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

/**
 *  keyfile option description:
 */
/** Descriptive text for the keyfile option */
#define KEYFILE_DESC      (ntpd_opt_strs+1755)
/** Upper-cased name for the keyfile option */
#define KEYFILE_NAME      (ntpd_opt_strs+1778)
/** Name string for the keyfile option */
#define KEYFILE_name      (ntpd_opt_strs+1786)
/** Compiled in flag settings for the keyfile option */
#define KEYFILE_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

/**
 *  logfile option description:
 */
/** Descriptive text for the logfile option */
#define LOGFILE_DESC      (ntpd_opt_strs+1794)
/** Upper-cased name for the logfile option */
#define LOGFILE_NAME      (ntpd_opt_strs+1815)
/** Name string for the logfile option */
#define LOGFILE_name      (ntpd_opt_strs+1823)
/** Compiled in flag settings for the logfile option */
#define LOGFILE_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

/**
 *  novirtualips option description:
 */
/** Descriptive text for the novirtualips option */
#define NOVIRTUALIPS_DESC      (ntpd_opt_strs+1831)
/** Upper-cased name for the novirtualips option */
#define NOVIRTUALIPS_NAME      (ntpd_opt_strs+1867)
/** Name string for the novirtualips option */
#define NOVIRTUALIPS_name      (ntpd_opt_strs+1880)
/** Compiled in flag settings for the novirtualips option */
#define NOVIRTUALIPS_FLAGS     (OPTST_DISABLED)

/**
 *  modifymmtimer option description:
 */
#ifdef SYS_WINNT
/** Descriptive text for the modifymmtimer option */
#define MODIFYMMTIMER_DESC      (ntpd_opt_strs+1893)
/** Upper-cased name for the modifymmtimer option */
#define MODIFYMMTIMER_NAME      (ntpd_opt_strs+1932)
/** Name string for the modifymmtimer option */
#define MODIFYMMTIMER_name      (ntpd_opt_strs+1946)
/** Compiled in flag settings for the modifymmtimer option */
#define MODIFYMMTIMER_FLAGS     (OPTST_DISABLED)

#else   /* disable modifymmtimer */
#define MODIFYMMTIMER_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define MODIFYMMTIMER_NAME      NULL
#define MODIFYMMTIMER_DESC      NULL
#define MODIFYMMTIMER_name      NULL
#endif  /* SYS_WINNT */

/**
 *  nofork option description with
 *  "Must also have options" and "Incompatible options":
 */
/** Descriptive text for the nofork option */
#define NOFORK_DESC      (ntpd_opt_strs+1960)
/** Upper-cased name for the nofork option */
#define NOFORK_NAME      (ntpd_opt_strs+1972)
/** Name string for the nofork option */
#define NOFORK_name      (ntpd_opt_strs+1979)
/** Other options that appear in conjunction with the nofork option */
static int const aNoforkCantList[] = {
    INDEX_OPT_WAIT_SYNC, NO_EQUIVALENT };
/** Compiled in flag settings for the nofork option */
#define NOFORK_FLAGS     (OPTST_DISABLED)

/**
 *  nice option description:
 */
/** Descriptive text for the nice option */
#define NICE_DESC      (ntpd_opt_strs+1986)
/** Upper-cased name for the nice option */
#define NICE_NAME      (ntpd_opt_strs+2007)
/** Name string for the nice option */
#define NICE_name      (ntpd_opt_strs+2012)
/** Compiled in flag settings for the nice option */
#define NICE_FLAGS     (OPTST_DISABLED)

/**
 *  pidfile option description:
 */
/** Descriptive text for the pidfile option */
#define PIDFILE_DESC      (ntpd_opt_strs+2017)
/** Upper-cased name for the pidfile option */
#define PIDFILE_NAME      (ntpd_opt_strs+2038)
/** Name string for the pidfile option */
#define PIDFILE_name      (ntpd_opt_strs+2046)
/** Compiled in flag settings for the pidfile option */
#define PIDFILE_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

/**
 *  priority option description:
 */
/** Descriptive text for the priority option */
#define PRIORITY_DESC      (ntpd_opt_strs+2054)
/** Upper-cased name for the priority option */
#define PRIORITY_NAME      (ntpd_opt_strs+2071)
/** Name string for the priority option */
#define PRIORITY_name      (ntpd_opt_strs+2080)
/** Compiled in flag settings for the priority option */
#define PRIORITY_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

/**
 *  quit option description with
 *  "Must also have options" and "Incompatible options":
 */
/** Descriptive text for the quit option */
#define QUIT_DESC      (ntpd_opt_strs+2089)
/** Upper-cased name for the quit option */
#define QUIT_NAME      (ntpd_opt_strs+2111)
/** Name string for the quit option */
#define QUIT_name      (ntpd_opt_strs+2116)
/** Other options that appear in conjunction with the quit option */
static int const aQuitCantList[] = {
    INDEX_OPT_SAVECONFIGQUIT,
    INDEX_OPT_WAIT_SYNC, NO_EQUIVALENT };
/** Compiled in flag settings for the quit option */
#define QUIT_FLAGS     (OPTST_DISABLED)

/**
 *  propagationdelay option description:
 */
/** Descriptive text for the propagationdelay option */
#define PROPAGATIONDELAY_DESC      (ntpd_opt_strs+2121)
/** Upper-cased name for the propagationdelay option */
#define PROPAGATIONDELAY_NAME      (ntpd_opt_strs+2149)
/** Name string for the propagationdelay option */
#define PROPAGATIONDELAY_name      (ntpd_opt_strs+2166)
/** Compiled in flag settings for the propagationdelay option */
#define PROPAGATIONDELAY_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

/**
 *  saveconfigquit option description with
 *  "Must also have options" and "Incompatible options":
 */
#ifdef SAVECONFIG
/** Descriptive text for the saveconfigquit option */
#define SAVECONFIGQUIT_DESC      (ntpd_opt_strs+2183)
/** Upper-cased name for the saveconfigquit option */
#define SAVECONFIGQUIT_NAME      (ntpd_opt_strs+2218)
/** Name string for the saveconfigquit option */
#define SAVECONFIGQUIT_name      (ntpd_opt_strs+2233)
/** Other options that appear in conjunction with the saveconfigquit option */
static int const aSaveconfigquitCantList[] = {
    INDEX_OPT_QUIT,
    INDEX_OPT_WAIT_SYNC, NO_EQUIVALENT };
/** Compiled in flag settings for the saveconfigquit option */
#define SAVECONFIGQUIT_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

#else   /* disable saveconfigquit */
#define SAVECONFIGQUIT_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define aSaveconfigquitCantList   NULL
#define SAVECONFIGQUIT_NAME      NULL
#define SAVECONFIGQUIT_DESC      NULL
#define SAVECONFIGQUIT_name      NULL
#endif  /* SAVECONFIG */

/**
 *  statsdir option description:
 */
/** Descriptive text for the statsdir option */
#define STATSDIR_DESC      (ntpd_opt_strs+2248)
/** Upper-cased name for the statsdir option */
#define STATSDIR_NAME      (ntpd_opt_strs+2273)
/** Name string for the statsdir option */
#define STATSDIR_name      (ntpd_opt_strs+2282)
/** Compiled in flag settings for the statsdir option */
#define STATSDIR_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

/**
 *  trustedkey option description:
 */
/** Descriptive text for the trustedkey option */
#define TRUSTEDKEY_DESC      (ntpd_opt_strs+2291)
/** Upper-cased name for the trustedkey option */
#define TRUSTEDKEY_NAME      (ntpd_opt_strs+2310)
/** Name string for the trustedkey option */
#define TRUSTEDKEY_name      (ntpd_opt_strs+2321)
/** Compiled in flag settings for the trustedkey option */
#define TRUSTEDKEY_FLAGS     (OPTST_DISABLED | OPTST_STACKED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

/**
 *  user option description:
 */
#ifdef HAVE_DROPROOT
/** Descriptive text for the user option */
#define USER_DESC      (ntpd_opt_strs+2332)
/** Upper-cased name for the user option */
#define USER_NAME      (ntpd_opt_strs+2366)
/** Name string for the user option */
#define USER_name      (ntpd_opt_strs+2371)
/** Compiled in flag settings for the user option */
#define USER_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

#else   /* disable user */
#define USER_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define USER_NAME      NULL
/** Descriptive text for the user option */
#define USER_DESC      (ntpd_opt_strs+1617)
#define USER_name      (ntpd_opt_strs+2371)
#endif  /* HAVE_DROPROOT */

/**
 *  updateinterval option description:
 */
/** Descriptive text for the updateinterval option */
#define UPDATEINTERVAL_DESC      (ntpd_opt_strs+2376)
/** Upper-cased name for the updateinterval option */
#define UPDATEINTERVAL_NAME      (ntpd_opt_strs+2440)
/** Name string for the updateinterval option */
#define UPDATEINTERVAL_name      (ntpd_opt_strs+2455)
/** Compiled in flag settings for the updateinterval option */
#define UPDATEINTERVAL_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

/**
 *  var option description:
 */
/** Descriptive text for the var option */
#define VAR_DESC      (ntpd_opt_strs+2470)
/** Upper-cased name for the var option */
#define VAR_NAME      (ntpd_opt_strs+2500)
/** Name string for the var option */
#define VAR_name      (ntpd_opt_strs+2504)
/** Compiled in flag settings for the var option */
#define VAR_FLAGS     (OPTST_DISABLED | OPTST_STACKED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

/**
 *  dvar option description:
 */
/** Descriptive text for the dvar option */
#define DVAR_DESC      (ntpd_opt_strs+2508)
/** Upper-cased name for the dvar option */
#define DVAR_NAME      (ntpd_opt_strs+2542)
/** Name string for the dvar option */
#define DVAR_name      (ntpd_opt_strs+2547)
/** Compiled in flag settings for the dvar option */
#define DVAR_FLAGS     (OPTST_DISABLED | OPTST_STACKED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

/**
 *  wait-sync option description with
 *  "Must also have options" and "Incompatible options":
 */
#ifdef HAVE_WORKING_FORK
/** Descriptive text for the wait-sync option */
#define WAIT_SYNC_DESC      (ntpd_opt_strs+2552)
/** Upper-cased name for the wait-sync option */
#define WAIT_SYNC_NAME      (ntpd_opt_strs+2589)
/** Name string for the wait-sync option */
#define WAIT_SYNC_name      (ntpd_opt_strs+2599)
/** Other options that appear in conjunction with the wait-sync option */
static int const aWait_SyncCantList[] = {
    INDEX_OPT_NOFORK,
    INDEX_OPT_QUIT,
    INDEX_OPT_SAVECONFIGQUIT, NO_EQUIVALENT };
/** Compiled in flag settings for the wait-sync option */
#define WAIT_SYNC_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_NUMERIC))

#else   /* disable wait-sync */
#define WAIT_SYNC_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define aWait_SyncCantList   NULL
#define WAIT_SYNC_NAME      NULL
#define WAIT_SYNC_DESC      NULL
#define WAIT_SYNC_name      NULL
#endif  /* HAVE_WORKING_FORK */

/**
 *  slew option description:
 */
/** Descriptive text for the slew option */
#define SLEW_DESC      (ntpd_opt_strs+2609)
/** Upper-cased name for the slew option */
#define SLEW_NAME      (ntpd_opt_strs+2632)
/** Name string for the slew option */
#define SLEW_name      (ntpd_opt_strs+2637)
/** Compiled in flag settings for the slew option */
#define SLEW_FLAGS     (OPTST_DISABLED)

/**
 *  usepcc option description:
 */
#ifdef SYS_WINNT
/** Descriptive text for the usepcc option */
#define USEPCC_DESC      (ntpd_opt_strs+2642)
/** Upper-cased name for the usepcc option */
#define USEPCC_NAME      (ntpd_opt_strs+2679)
/** Name string for the usepcc option */
#define USEPCC_name      (ntpd_opt_strs+2686)
/** Compiled in flag settings for the usepcc option */
#define USEPCC_FLAGS     (OPTST_DISABLED)

#else   /* disable usepcc */
#define USEPCC_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define USEPCC_NAME      NULL
#define USEPCC_DESC      NULL
#define USEPCC_name      NULL
#endif  /* SYS_WINNT */

/**
 *  pccfreq option description:
 */
#ifdef SYS_WINNT
/** Descriptive text for the pccfreq option */
#define PCCFREQ_DESC      (ntpd_opt_strs+2693)
/** Upper-cased name for the pccfreq option */
#define PCCFREQ_NAME      (ntpd_opt_strs+2736)
/** Name string for the pccfreq option */
#define PCCFREQ_name      (ntpd_opt_strs+2744)
/** Compiled in flag settings for the pccfreq option */
#define PCCFREQ_FLAGS     (OPTST_DISABLED \
        | OPTST_SET_ARGTYPE(OPARG_TYPE_STRING))

#else   /* disable pccfreq */
#define PCCFREQ_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define PCCFREQ_NAME      NULL
#define PCCFREQ_DESC      NULL
#define PCCFREQ_name      NULL
#endif  /* SYS_WINNT */

/**
 *  mdns option description:
 */
#ifdef HAVE_DNSREGISTRATION
/** Descriptive text for the mdns option */
#define MDNS_DESC      (ntpd_opt_strs+2752)
/** Upper-cased name for the mdns option */
#define MDNS_NAME      (ntpd_opt_strs+2787)
/** Name string for the mdns option */
#define MDNS_name      (ntpd_opt_strs+2792)
/** Compiled in flag settings for the mdns option */
#define MDNS_FLAGS     (OPTST_DISABLED)

#else   /* disable mdns */
#define MDNS_FLAGS     (OPTST_OMITTED | OPTST_NO_INIT)
#define MDNS_NAME      NULL
#define MDNS_DESC      NULL
#define MDNS_name      NULL
#endif  /* HAVE_DNSREGISTRATION */

/*
 *  Help/More_Help/Version option descriptions:
 */
#define HELP_DESC       (ntpd_opt_strs+2797)
#define HELP_name       (ntpd_opt_strs+2841)
#ifdef HAVE_WORKING_FORK
#define MORE_HELP_DESC  (ntpd_opt_strs+2846)
#define MORE_HELP_name  (ntpd_opt_strs+2891)
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
#define VER_DESC        (ntpd_opt_strs+2901)
#define VER_name        (ntpd_opt_strs+2937)
/**
 *  Declare option callback procedures
 */
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
 *  Define the ntpd Option Descriptions.
 * This is an array of OPTION_CT entries, one for each
 * option that the ntpd program responds to.
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

  {  /* entry idx, value */ 2, VALUE_OPT_AUTHREQ,
     /* equiv idx, value */ 2, VALUE_OPT_AUTHREQ,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ AUTHREQ_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --authreq */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, aAuthreqCantList,
     /* option proc      */ NULL,
     /* desc, NAME, name */ AUTHREQ_DESC, AUTHREQ_NAME, AUTHREQ_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 3, VALUE_OPT_AUTHNOREQ,
     /* equiv idx, value */ 3, VALUE_OPT_AUTHNOREQ,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ AUTHNOREQ_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --authnoreq */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, aAuthnoreqCantList,
     /* option proc      */ NULL,
     /* desc, NAME, name */ AUTHNOREQ_DESC, AUTHNOREQ_NAME, AUTHNOREQ_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 4, VALUE_OPT_BCASTSYNC,
     /* equiv idx, value */ 4, VALUE_OPT_BCASTSYNC,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ BCASTSYNC_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --bcastsync */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ BCASTSYNC_DESC, BCASTSYNC_NAME, BCASTSYNC_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 5, VALUE_OPT_CONFIGFILE,
     /* equiv idx, value */ 5, VALUE_OPT_CONFIGFILE,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ CONFIGFILE_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --configfile */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ CONFIGFILE_DESC, CONFIGFILE_NAME, CONFIGFILE_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 6, VALUE_OPT_DEBUG_LEVEL,
     /* equiv idx, value */ 6, VALUE_OPT_DEBUG_LEVEL,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ DEBUG_LEVEL_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --debug-level */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ doOptDebug_Level,
     /* desc, NAME, name */ DEBUG_LEVEL_DESC, DEBUG_LEVEL_NAME, DEBUG_LEVEL_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 7, VALUE_OPT_SET_DEBUG_LEVEL,
     /* equiv idx, value */ 7, VALUE_OPT_SET_DEBUG_LEVEL,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ SET_DEBUG_LEVEL_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --set-debug-level */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionNumericVal,
     /* desc, NAME, name */ SET_DEBUG_LEVEL_DESC, SET_DEBUG_LEVEL_NAME, SET_DEBUG_LEVEL_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 8, VALUE_OPT_DRIFTFILE,
     /* equiv idx, value */ 8, VALUE_OPT_DRIFTFILE,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ DRIFTFILE_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --driftfile */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ DRIFTFILE_DESC, DRIFTFILE_NAME, DRIFTFILE_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 9, VALUE_OPT_PANICGATE,
     /* equiv idx, value */ 9, VALUE_OPT_PANICGATE,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ PANICGATE_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --panicgate */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ PANICGATE_DESC, PANICGATE_NAME, PANICGATE_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 10, VALUE_OPT_FORCE_STEP_ONCE,
     /* equiv idx, value */ 10, VALUE_OPT_FORCE_STEP_ONCE,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ FORCE_STEP_ONCE_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --force_step_once */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ FORCE_STEP_ONCE_DESC, FORCE_STEP_ONCE_NAME, FORCE_STEP_ONCE_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 11, VALUE_OPT_JAILDIR,
     /* equiv idx, value */ 11, VALUE_OPT_JAILDIR,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ JAILDIR_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --jaildir */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ JAILDIR_DESC, JAILDIR_NAME, JAILDIR_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 12, VALUE_OPT_INTERFACE,
     /* equiv idx, value */ 12, VALUE_OPT_INTERFACE,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ INTERFACE_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --interface */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionStackArg,
     /* desc, NAME, name */ INTERFACE_DESC, INTERFACE_NAME, INTERFACE_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 13, VALUE_OPT_KEYFILE,
     /* equiv idx, value */ 13, VALUE_OPT_KEYFILE,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ KEYFILE_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --keyfile */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ KEYFILE_DESC, KEYFILE_NAME, KEYFILE_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 14, VALUE_OPT_LOGFILE,
     /* equiv idx, value */ 14, VALUE_OPT_LOGFILE,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ LOGFILE_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --logfile */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ LOGFILE_DESC, LOGFILE_NAME, LOGFILE_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 15, VALUE_OPT_NOVIRTUALIPS,
     /* equiv idx, value */ 15, VALUE_OPT_NOVIRTUALIPS,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ NOVIRTUALIPS_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --novirtualips */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ NOVIRTUALIPS_DESC, NOVIRTUALIPS_NAME, NOVIRTUALIPS_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 16, VALUE_OPT_MODIFYMMTIMER,
     /* equiv idx, value */ 16, VALUE_OPT_MODIFYMMTIMER,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ MODIFYMMTIMER_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --modifymmtimer */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ MODIFYMMTIMER_DESC, MODIFYMMTIMER_NAME, MODIFYMMTIMER_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 17, VALUE_OPT_NOFORK,
     /* equiv idx, value */ 17, VALUE_OPT_NOFORK,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ NOFORK_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --nofork */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, aNoforkCantList,
     /* option proc      */ NULL,
     /* desc, NAME, name */ NOFORK_DESC, NOFORK_NAME, NOFORK_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 18, VALUE_OPT_NICE,
     /* equiv idx, value */ 18, VALUE_OPT_NICE,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ NICE_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --nice */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ NICE_DESC, NICE_NAME, NICE_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 19, VALUE_OPT_PIDFILE,
     /* equiv idx, value */ 19, VALUE_OPT_PIDFILE,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ PIDFILE_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --pidfile */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ PIDFILE_DESC, PIDFILE_NAME, PIDFILE_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 20, VALUE_OPT_PRIORITY,
     /* equiv idx, value */ 20, VALUE_OPT_PRIORITY,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ PRIORITY_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --priority */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionNumericVal,
     /* desc, NAME, name */ PRIORITY_DESC, PRIORITY_NAME, PRIORITY_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 21, VALUE_OPT_QUIT,
     /* equiv idx, value */ 21, VALUE_OPT_QUIT,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ QUIT_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --quit */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, aQuitCantList,
     /* option proc      */ NULL,
     /* desc, NAME, name */ QUIT_DESC, QUIT_NAME, QUIT_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 22, VALUE_OPT_PROPAGATIONDELAY,
     /* equiv idx, value */ 22, VALUE_OPT_PROPAGATIONDELAY,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ PROPAGATIONDELAY_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --propagationdelay */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ PROPAGATIONDELAY_DESC, PROPAGATIONDELAY_NAME, PROPAGATIONDELAY_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 23, VALUE_OPT_SAVECONFIGQUIT,
     /* equiv idx, value */ 23, VALUE_OPT_SAVECONFIGQUIT,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ SAVECONFIGQUIT_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --saveconfigquit */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, aSaveconfigquitCantList,
     /* option proc      */ NULL,
     /* desc, NAME, name */ SAVECONFIGQUIT_DESC, SAVECONFIGQUIT_NAME, SAVECONFIGQUIT_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 24, VALUE_OPT_STATSDIR,
     /* equiv idx, value */ 24, VALUE_OPT_STATSDIR,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ STATSDIR_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --statsdir */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ STATSDIR_DESC, STATSDIR_NAME, STATSDIR_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 25, VALUE_OPT_TRUSTEDKEY,
     /* equiv idx, value */ 25, VALUE_OPT_TRUSTEDKEY,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ TRUSTEDKEY_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --trustedkey */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionStackArg,
     /* desc, NAME, name */ TRUSTEDKEY_DESC, TRUSTEDKEY_NAME, TRUSTEDKEY_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 26, VALUE_OPT_USER,
     /* equiv idx, value */ 26, VALUE_OPT_USER,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ USER_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --user */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ USER_DESC, USER_NAME, USER_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 27, VALUE_OPT_UPDATEINTERVAL,
     /* equiv idx, value */ 27, VALUE_OPT_UPDATEINTERVAL,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ UPDATEINTERVAL_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --updateinterval */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionNumericVal,
     /* desc, NAME, name */ UPDATEINTERVAL_DESC, UPDATEINTERVAL_NAME, UPDATEINTERVAL_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 28, VALUE_OPT_VAR,
     /* equiv idx, value */ 28, VALUE_OPT_VAR,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ VAR_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --var */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionStackArg,
     /* desc, NAME, name */ VAR_DESC, VAR_NAME, VAR_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 29, VALUE_OPT_DVAR,
     /* equiv idx, value */ 29, VALUE_OPT_DVAR,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, NOLIMIT, 0,
     /* opt state flags  */ DVAR_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --dvar */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ optionStackArg,
     /* desc, NAME, name */ DVAR_DESC, DVAR_NAME, DVAR_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 30, VALUE_OPT_WAIT_SYNC,
     /* equiv idx, value */ 30, VALUE_OPT_WAIT_SYNC,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ WAIT_SYNC_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --wait-sync */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, aWait_SyncCantList,
     /* option proc      */ optionNumericVal,
     /* desc, NAME, name */ WAIT_SYNC_DESC, WAIT_SYNC_NAME, WAIT_SYNC_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 31, VALUE_OPT_SLEW,
     /* equiv idx, value */ 31, VALUE_OPT_SLEW,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ SLEW_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --slew */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ SLEW_DESC, SLEW_NAME, SLEW_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 32, VALUE_OPT_USEPCC,
     /* equiv idx, value */ 32, VALUE_OPT_USEPCC,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ USEPCC_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --usepcc */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ USEPCC_DESC, USEPCC_NAME, USEPCC_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 33, VALUE_OPT_PCCFREQ,
     /* equiv idx, value */ 33, VALUE_OPT_PCCFREQ,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ PCCFREQ_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --pccfreq */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ PCCFREQ_DESC, PCCFREQ_NAME, PCCFREQ_name,
     /* disablement strs */ NULL, NULL },

  {  /* entry idx, value */ 34, VALUE_OPT_MDNS,
     /* equiv idx, value */ 34, VALUE_OPT_MDNS,
     /* equivalenced to  */ NO_EQUIVALENT,
     /* min, max, act ct */ 0, 1, 0,
     /* opt state flags  */ MDNS_FLAGS, 0,
     /* last opt argumnt */ { NULL }, /* --mdns */
     /* arg list/cookie  */ NULL,
     /* must/cannot opts */ NULL, NULL,
     /* option proc      */ NULL,
     /* desc, NAME, name */ MDNS_DESC, MDNS_NAME, MDNS_name,
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
     /* disablement strs */ NULL, NULL }
};


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/** Reference to the upper cased version of ntpd. */
#define zPROGNAME       (ntpd_opt_strs+2945)
/** Reference to the title line for ntpd usage. */
#define zUsageTitle     (ntpd_opt_strs+2950)
/** There is no ntpd configuration file. */
#define zRcName         NULL
/** There are no directories to search for ntpd config files. */
#define apzHomeList     NULL
/** The ntpd program bug email address. */
#define zBugsAddr       (ntpd_opt_strs+3082)
/** Clarification/explanation of what ntpd does. */
#define zExplain        (ntpd_opt_strs+3116)
/** Extra detail explaining what ntpd does. */
#define zDetail         (NULL)
/** The full version string for ntpd. */
#define zFullVersion    (ntpd_opt_strs+3118)
/* extracted from optcode.tlib near line 364 */

#if defined(ENABLE_NLS)
# define OPTPROC_BASE OPTPROC_TRANSLATE
  static tOptionXlateProc translate_option_strings;
#else
# define OPTPROC_BASE OPTPROC_NONE
# define translate_option_strings NULL
#endif /* ENABLE_NLS */

#define ntpd_full_usage (NULL)
#define ntpd_short_usage (NULL)

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
    ex_code = NTPD_EXIT_SUCCESS;
    optionUsage(&ntpdOptions, ex_code);
    /* NOTREACHED */
    exit(1);
    (void)opts;
    (void)od;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 * Code to handle the debug-level option.
 *
 * @param[in] pOptions the ntpd options data structure
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
/* extracted from optmain.tlib near line 1250 */

/**
 * The directory containing the data associated with ntpd.
 */
#ifndef  PKGDATADIR
# define PKGDATADIR ""
#endif

/**
 * Information about the person or institution that packaged ntpd
 * for the current distribution.
 */
#ifndef  WITH_PACKAGER
# define ntpd_packager_info NULL
#else
/** Packager information for ntpd. */
static char const ntpd_packager_info[] =
    "Packaged by " WITH_PACKAGER

# ifdef WITH_PACKAGER_VERSION
        " ("WITH_PACKAGER_VERSION")"
# endif

# ifdef WITH_PACKAGER_BUG_REPORTS
    "\nReport ntpd bugs to " WITH_PACKAGER_BUG_REPORTS
# endif
    "\n";
#endif
#ifndef __doxygen__

#endif /* __doxygen__ */
/**
 * The option definitions for ntpd.  The one structure that
 * binds them all.
 */
tOptions ntpdOptions = {
    OPTIONS_STRUCT_VERSION,
    0, NULL,                    /* original argc + argv    */
    ( OPTPROC_BASE
    + OPTPROC_ERRSTOP
    + OPTPROC_SHORTOPT
    + OPTPROC_LONGOPT
    + OPTPROC_NO_REQ_OPT
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
      NO_EQUIVALENT, /* save option index */
      NO_EQUIVALENT, /* '-#' option index */
      NO_EQUIVALENT /* index of default opt */
    },
    38 /* full option count */, 35 /* user option count */,
    ntpd_full_usage, ntpd_short_usage,
    NULL, NULL,
    PKGDATADIR, ntpd_packager_info
};

#if ENABLE_NLS
/**
 * This code is designed to translate translatable option text for the
 * ntpd program.  These translations happen upon entry
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
        exit(NTPD_EXIT_FAILURE);
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
 * Translate all the translatable strings in the ntpdOptions
 * structure defined above.  This is done only once.
 */
static void
translate_option_strings(void)
{
    tOptions * const opts = &ntpdOptions;

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
     by a field name in the ntpdOptions structure noted in the
     comments below.  The literal text is defined in ntpd_opt_strs.
   
     NOTE: the strings below are segmented with respect to the source string
     ntpd_opt_strs.  The strings above are handed off for translation
     at run time a paragraph at a time.  Consequently, they are presented here
     for translation a paragraph at a time.
   
     ALSO: often the description for an option will reference another option
     by name.  These are set off with apostrophe quotes (I hope).  Do not
     translate option names.
   */
  /* referenced via ntpdOptions.pzCopyright */
  puts(_("ntpd 4.2.8p13\n\
Copyright (C) 1992-2017 The University of Delaware and Network Time Foundation, all rights reserved.\n\
This is free software. It is licensed for use, modification and\n\
redistribution under the terms of the NTP License, copies of which\n\
can be seen at:\n"));
  puts(_("  <http://ntp.org/license>\n\
  <http://opensource.org/licenses/ntp-license.php>\n"));

  /* referenced via ntpdOptions.pzCopyNotice */
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

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Force IPv4 DNS name resolution"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Force IPv6 DNS name resolution"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Require crypto authentication"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Do not require crypto authentication"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Allow us to sync to broadcast servers"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("configuration file name"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Increase debug verbosity level"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Set the debug verbosity level"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("frequency drift file name"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Allow the first adjustment to be Big"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Step any initial offset correction."));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Jail directory"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("built without --enable-clockctl or --enable-linuxcaps or --enable-solarisprivs"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Listen on an interface name or address"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("path to symmetric keys"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("path to the log file"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Do not listen to virtual interfaces"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Modify Multimedia Timer (Windows only)"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Do not fork"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Run at high priority"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("path to the PID file"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Process priority"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Set the time and quit"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Broadcast/propagation delay"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Save parsed configuration and quit"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Statistics file location"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Trusted key number"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Run as userid (or userid:groupid)"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("built without --enable-clockctl or --enable-linuxcaps or --enable-solarisprivs"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("interval in seconds between scans for new or dropped interfaces"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("make ARG an ntp variable (RW)"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("make ARG an ntp variable (RW|DEF)"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Seconds to wait for first clock sync"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Slew up to 600 seconds"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Use CPU cycle counter (Windows only)"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Force CPU cycle counter use (Windows only)"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("Register with mDNS as a NTP server"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("display extended usage information and exit"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("extended usage information passed thru pager"));

  /* referenced via ntpdOptions.pOptDesc->pzText */
  puts(_("output version information and exit"));

  /* referenced via ntpdOptions.pzUsageTitle */
  puts(_("ntpd - NTP daemon program - Ver. 4.2.8p13\n\
Usage:  %s [ -<flag> [<val>] | --<name>[{=| }<val>] ]... \\\n\
\t\t[ <server1> ... <serverN> ]\n"));

  /* referenced via ntpdOptions.pzExplain */
  puts(_("\n"));

  /* referenced via ntpdOptions.pzFullVersion */
  puts(_("ntpd 4.2.8p13"));

  /* referenced via ntpdOptions.pzFullUsage */
  puts(_("<<<NOT-FOUND>>>"));

  /* referenced via ntpdOptions.pzShortUsage */
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
/* ntpd-opts.c ends here */
