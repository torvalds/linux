#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([OpenBSM], [1.2-alpha5], [trustedbsd-audit@TrustedBSD.org],[openbsm])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([bin/auditreduce/auditreduce.c])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER([config/config.h])
AM_MAINTAINER_MODE

# --with-native-includes forces the use of the system bsm headers.
AC_ARG_WITH([native-includes],
[AS_HELP_STRING([--with-native-includes],
[Use the system native include files instead of those included with openbsm.])],
[
AC_DEFINE(USE_NATIVE_INCLUDES,, Define to use native include files)
use_native_includes=true
],
[use_native_includes=false])
AM_CONDITIONAL(USE_NATIVE_INCLUDES, $use_native_includes)

AC_PATH_PROGS(MIG, mig)

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LEX
AC_PROG_LIBTOOL
AC_PROG_LN_S
AC_PROG_YACC

AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

AC_SEARCH_LIBS(dlsym, dl)
AC_SEARCH_LIBS(pthread_create, pthread)
AC_SEARCH_LIBS(clock_gettime, rt)
AC_SEARCH_LIBS(SSL_connect, ssl)
AC_SEARCH_LIBS(humanize_number, util)
AC_SEARCH_LIBS(pidfile_open, util)

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([mach/mach.h stdint.h pthread_np.h printf.h])

AC_DEFINE([_GNU_SOURCE],,[Use extended API on platforms that require it])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_UID_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_rdev])

AC_CHECK_MEMBER([struct ipc_perm.__key],
[AC_DEFINE(HAVE_IPC_PERM___KEY,, Define if ipc_perm.__key instead of key)],
[],[
#include <sys/types.h>
#include <sys/ipc.h>
])

AC_CHECK_MEMBER([struct ipc_perm._key],
[AC_DEFINE(HAVE_IPC_PERM__KEY,, Define if ipc_perm._key instead of key)],
[],[
#include <sys/types.h>
#include <sys/ipc.h>
])

AC_CHECK_MEMBER([struct ipc_perm.__seq],
[AC_DEFINE(HAVE_IPC_PERM___SEQ,, Define if ipc_perm.__seq instead of seq)],
[],[
#include <sys/types.h>
#include <sys/ipc.h>
])

AC_CHECK_MEMBER([struct ipc_perm._seq],
[AC_DEFINE(HAVE_IPC_PERM__SEQ,, Define if ipc_perm._seq instead of seq)],
[],[
#include <sys/types.h>
#include <sys/ipc.h>
])

AC_CHECK_MEMBER([struct sockaddr_storage.ss_len],
[AC_DEFINE(HAVE_SOCKADDR_STORAGE_SS_LEN,, Define if sockaddr_storage.ss_len field exists)],
[],[
#include <sys/types.h>
#include <sys/socket.h>
])

AC_HEADER_TIME
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_FUNC_STRFTIME
AC_CHECK_FUNCS([arc4random arc4random_buf bzero cap_enter clock_gettime closefrom faccessat fdopendir fstatat ftruncate getresgid getresuid gettimeofday inet_ntoa jail kqueue memset openat pthread_cond_timedwait_relative_np pthread_condattr_setclock pthread_mutex_lock renameat setproctitle sigtimedwait strchr strerror strlcat strlcpy strndup strrchr strstr strtol strtoul unlinkat vis])

# sys/queue.h exists on most systems, but its capabilities vary a great deal.
# test for LIST_FIRST and TAILQ_FOREACH_SAFE, which appears to not exist in
# all of them, and are necessary for OpenBSM.
AC_TRY_LINK([
	#include <sys/queue.h>
], [

	#ifndef LIST_FIRST
	#error LIST_FIRST missing
	#endif
	#ifndef TAILQ_FOREACH_SAFE
	#error TAILQ_FOREACH_SAFE
	#endif
], [
AC_DEFINE(HAVE_FULL_QUEUE_H,, Define if queue.h includes LIST_FIRST)
])

# Systems may not define key audit system calls, in which case libbsm cannot
# depend on them or it will generate link-time or run-time errors.  Test for
# just one.
AC_TRY_LINK([
	#include <stddef.h>

	extern int auditon(int, void *, int);
], [
	int err;

	err = auditon(0, NULL, 0);
], [
AC_DEFINE(HAVE_AUDIT_SYSCALLS,, Define if audit system calls present)
have_audit_syscalls=true
], [
have_audit_syscalls=false
])
AM_CONDITIONAL(HAVE_AUDIT_SYSCALLS, $have_audit_syscalls)

#
# We rely on the BSD be32toh() and be32enc()-style endian macros to perform
# byte order conversions.  Availability of these varies considerably -- in
# general, a system might have neither, be32toh(), or be32toh() and be32enc().
# There is also variation in which headers are even present, and whether they
# are macros or functions.  Try to organise the world into some simpler cases.
# The following macros may be set at the end:
#
# USE_ENDIAN_H
# USE_SYS_ENDIAN_H
# USE_MACHINE_ENDIAN_H
# USE_COMPAT_ENDIAN_H
# USE_COMPAT_ENDIAN_ENC_H
#
# First, decide which system endian.h to use.
#
AC_CHECK_HEADERS([endian.h], [
	have_endian_h=yes
], [
	have_endian_h=no
])

AC_CHECK_HEADERS([sys/endian.h], [
	have_sys_endian_h=yes
], [
	have_sys_endian_h=no
])

AC_CHECK_HEADERS([machine/endian.h], [
	have_machine_endian_h=yes
], [
	have_machine_endian_h=no
])

if test $have_endian_h = yes; then
	AC_DEFINE(USE_ENDIAN_H,, Define if endian.h should be included)
elif test $have_sys_endian_h = yes; then
	AC_DEFINE(USE_SYS_ENDIAN_H,, Define if sys/endian.h should be included)
elif test $have_machine_endian_h = yes; then
	AC_DEFINE(USE_MACHINE_ENDIAN_H,, Define if machine/endian.h should be included)
else
	AC_MSG_ERROR([no endian.h])
fi

#
# Next, decide if we need to supplement with compat headers.
#
AC_TRY_LINK([
	#ifdef USE_ENDIAN_H
	#include <endian.h>
	#endif
	#ifdef USE_SYS_ENDIAN_H
	#include <sys/endian.h>
	#endif
	#ifdef USE_MACHINE_ENDIAN_H
	#include <machine/endian.h>
	#endif
], [
	(void)be32toh(0);
], [], [
	AC_DEFINE(USE_COMPAT_ENDIAN_H,, Define if compat/endian.h is required)
	AC_MSG_RESULT([using compat/endian.h])
])

AC_TRY_LINK([
	#ifdef USE_ENDIAN_H
	#include <endian.h>
	#endif
	#ifdef USE_SYS_ENDIAN_H
	#include <sys/endian.h>
	#endif
	#ifdef USE_MACHINE_ENDIAN_H
	#include <machine/endian.h>
	#endif
	#ifdef USE_COMPAT_ENDIAN_H
	#include "compat/endian.h"
	#endif
	#include <stdlib.h>
], [
	int i;

	i = bswap16(0);
	i = bswap32(0);
	i = bswap64(0);
	be32enc(NULL, 0);
	i = htole64(0);
	i = le64toh(0);
], [], [
	AC_DEFINE(USE_COMPAT_ENDIAN_ENC_H,, Define if compat/endian_enc.h is required)
	AC_MSG_RESULT([using compat/endian_enc.h])
])

# Check to see if Mach IPC is used for trigger messages.  If so, use Mach IPC
# instead of the default for sending trigger messages to the audit components.
AC_CHECK_FILE([/usr/include/mach/audit_triggers.defs], [
AC_DEFINE(USE_MACH_IPC,, Define if uses Mach IPC for Triggers messages)
use_mach_ipc=true
], [
use_mach_ipc=false
])
AM_CONDITIONAL(USE_MACH_IPC, $use_mach_ipc)

AC_CONFIG_FILES([Makefile
                 bin/Makefile
                 bin/audit/Makefile
                 bin/auditd/Makefile
                 bin/auditdistd/Makefile
                 bin/auditfilterd/Makefile
                 bin/auditreduce/Makefile
                 bin/praudit/Makefile
                 bsm/Makefile
                 libauditd/Makefile
                 libbsm/Makefile
                 modules/Makefile
                 modules/auditfilter_noop/Makefile
                 man/Makefile
                 sys/Makefile
                 sys/bsm/Makefile
                 test/Makefile
                 test/bsm/Makefile
                 tools/Makefile])

AC_OUTPUT
