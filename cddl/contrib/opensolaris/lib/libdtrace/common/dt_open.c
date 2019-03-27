/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 */

#include <sys/types.h>
#ifdef illumos
#include <sys/modctl.h>
#include <sys/systeminfo.h>
#else
#include <sys/param.h>
#include <sys/module.h>
#include <sys/linker.h>
#endif
#include <sys/resource.h>

#include <libelf.h>
#include <strings.h>
#ifdef illumos
#include <alloca.h>
#endif
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#define	_POSIX_PTHREAD_SEMANTICS
#include <dirent.h>
#undef	_POSIX_PTHREAD_SEMANTICS

#include <dt_impl.h>
#include <dt_program.h>
#include <dt_module.h>
#include <dt_printf.h>
#include <dt_string.h>
#include <dt_provider.h>
#ifndef illumos
#include <sys/sysctl.h>
#include <string.h>
#endif
#if defined(__i386__)
#include <ieeefp.h>
#endif

/*
 * Stability and versioning definitions.  These #defines are used in the tables
 * of identifiers below to fill in the attribute and version fields associated
 * with each identifier.  The DT_ATTR_* macros are a convenience to permit more
 * concise declarations of common attributes such as Stable/Stable/Common.  The
 * DT_VERS_* macros declare the encoded integer values of all versions used so
 * far.  DT_VERS_LATEST must correspond to the latest version value among all
 * versions exported by the D compiler.  DT_VERS_STRING must be an ASCII string
 * that contains DT_VERS_LATEST within it along with any suffixes (e.g. Beta).
 * You must update DT_VERS_LATEST and DT_VERS_STRING when adding a new version,
 * and then add the new version to the _dtrace_versions[] array declared below.
 * Refer to the Solaris Dynamic Tracing Guide Stability and Versioning chapters
 * respectively for an explanation of these DTrace features and their values.
 *
 * NOTE: Although the DTrace versioning scheme supports the labeling and
 *       introduction of incompatible changes (e.g. dropping an interface in a
 *       major release), the libdtrace code does not currently support this.
 *       All versions are assumed to strictly inherit from one another.  If
 *       we ever need to provide divergent interfaces, this will need work.
 */
#define	DT_ATTR_STABCMN	{ DTRACE_STABILITY_STABLE, \
	DTRACE_STABILITY_STABLE, DTRACE_CLASS_COMMON }

#define	DT_ATTR_EVOLCMN { DTRACE_STABILITY_EVOLVING, \
	DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_COMMON \
}

/*
 * The version number should be increased for every customer visible release
 * of DTrace. The major number should be incremented when a fundamental
 * change has been made that would affect all consumers, and would reflect
 * sweeping changes to DTrace or the D language. The minor number should be
 * incremented when a change is introduced that could break scripts that had
 * previously worked; for example, adding a new built-in variable could break
 * a script which was already using that identifier. The micro number should
 * be changed when introducing functionality changes or major bug fixes that
 * do not affect backward compatibility -- this is merely to make capabilities
 * easily determined from the version number. Minor bugs do not require any
 * modification to the version number.
 */
#define	DT_VERS_1_0	DT_VERSION_NUMBER(1, 0, 0)
#define	DT_VERS_1_1	DT_VERSION_NUMBER(1, 1, 0)
#define	DT_VERS_1_2	DT_VERSION_NUMBER(1, 2, 0)
#define	DT_VERS_1_2_1	DT_VERSION_NUMBER(1, 2, 1)
#define	DT_VERS_1_2_2	DT_VERSION_NUMBER(1, 2, 2)
#define	DT_VERS_1_3	DT_VERSION_NUMBER(1, 3, 0)
#define	DT_VERS_1_4	DT_VERSION_NUMBER(1, 4, 0)
#define	DT_VERS_1_4_1	DT_VERSION_NUMBER(1, 4, 1)
#define	DT_VERS_1_5	DT_VERSION_NUMBER(1, 5, 0)
#define	DT_VERS_1_6	DT_VERSION_NUMBER(1, 6, 0)
#define	DT_VERS_1_6_1	DT_VERSION_NUMBER(1, 6, 1)
#define	DT_VERS_1_6_2	DT_VERSION_NUMBER(1, 6, 2)
#define	DT_VERS_1_6_3	DT_VERSION_NUMBER(1, 6, 3)
#define	DT_VERS_1_7	DT_VERSION_NUMBER(1, 7, 0)
#define	DT_VERS_1_7_1	DT_VERSION_NUMBER(1, 7, 1)
#define	DT_VERS_1_8	DT_VERSION_NUMBER(1, 8, 0)
#define	DT_VERS_1_8_1	DT_VERSION_NUMBER(1, 8, 1)
#define	DT_VERS_1_9	DT_VERSION_NUMBER(1, 9, 0)
#define	DT_VERS_1_9_1	DT_VERSION_NUMBER(1, 9, 1)
#define	DT_VERS_1_10	DT_VERSION_NUMBER(1, 10, 0)
#define	DT_VERS_1_11	DT_VERSION_NUMBER(1, 11, 0)
#define	DT_VERS_1_12	DT_VERSION_NUMBER(1, 12, 0)
#define	DT_VERS_1_12_1	DT_VERSION_NUMBER(1, 12, 1)
#define	DT_VERS_1_13	DT_VERSION_NUMBER(1, 13, 0)
#define	DT_VERS_LATEST	DT_VERS_1_13
#define	DT_VERS_STRING	"Sun D 1.13"

const dt_version_t _dtrace_versions[] = {
	DT_VERS_1_0,	/* D API 1.0.0 (PSARC 2001/466) Solaris 10 FCS */
	DT_VERS_1_1,	/* D API 1.1.0 Solaris Express 6/05 */
	DT_VERS_1_2,	/* D API 1.2.0 Solaris 10 Update 1 */
	DT_VERS_1_2_1,	/* D API 1.2.1 Solaris Express 4/06 */
	DT_VERS_1_2_2,	/* D API 1.2.2 Solaris Express 6/06 */
	DT_VERS_1_3,	/* D API 1.3 Solaris Express 10/06 */
	DT_VERS_1_4,	/* D API 1.4 Solaris Express 2/07 */
	DT_VERS_1_4_1,	/* D API 1.4.1 Solaris Express 4/07 */
	DT_VERS_1_5,	/* D API 1.5 Solaris Express 7/07 */
	DT_VERS_1_6,	/* D API 1.6 */
	DT_VERS_1_6_1,	/* D API 1.6.1 */
	DT_VERS_1_6_2,	/* D API 1.6.2 */
	DT_VERS_1_6_3,	/* D API 1.6.3 */
	DT_VERS_1_7,	/* D API 1.7 */
	DT_VERS_1_7_1,	/* D API 1.7.1 */
	DT_VERS_1_8,	/* D API 1.8 */
	DT_VERS_1_8_1,	/* D API 1.8.1 */
	DT_VERS_1_9,	/* D API 1.9 */
	DT_VERS_1_9_1,	/* D API 1.9.1 */
	DT_VERS_1_10,	/* D API 1.10 */
	DT_VERS_1_11,	/* D API 1.11 */
	DT_VERS_1_12,	/* D API 1.12 */
	DT_VERS_1_12_1,	/* D API 1.12.1 */
	DT_VERS_1_13,	/* D API 1.13 */
	0
};

/*
 * Global variables that are formatted on FreeBSD based on the kernel file name.
 */
#ifndef illumos
static char	curthread_str[MAXPATHLEN];
static char	intmtx_str[MAXPATHLEN];
static char	threadmtx_str[MAXPATHLEN];
static char	rwlock_str[MAXPATHLEN];
static char	sxlock_str[MAXPATHLEN];
#endif

/*
 * Table of global identifiers.  This is used to populate the global identifier
 * hash when a new dtrace client open occurs.  For more info see dt_ident.h.
 * The global identifiers that represent functions use the dt_idops_func ops
 * and specify the private data pointer as a prototype string which is parsed
 * when the identifier is first encountered.  These prototypes look like ANSI
 * C function prototypes except that the special symbol "@" can be used as a
 * wildcard to represent a single parameter of any type (i.e. any dt_node_t).
 * The standard "..." notation can also be used to represent varargs.  An empty
 * parameter list is taken to mean void (that is, no arguments are permitted).
 * A parameter enclosed in square brackets (e.g. "[int]") denotes an optional
 * argument.
 */
static const dt_ident_t _dtrace_globals[] = {
{ "alloca", DT_IDENT_FUNC, 0, DIF_SUBR_ALLOCA, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void *(size_t)" },
{ "arg0", DT_IDENT_SCALAR, 0, DIF_VAR_ARG0, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "int64_t" },
{ "arg1", DT_IDENT_SCALAR, 0, DIF_VAR_ARG1, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "int64_t" },
{ "arg2", DT_IDENT_SCALAR, 0, DIF_VAR_ARG2, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "int64_t" },
{ "arg3", DT_IDENT_SCALAR, 0, DIF_VAR_ARG3, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "int64_t" },
{ "arg4", DT_IDENT_SCALAR, 0, DIF_VAR_ARG4, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "int64_t" },
{ "arg5", DT_IDENT_SCALAR, 0, DIF_VAR_ARG5, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "int64_t" },
{ "arg6", DT_IDENT_SCALAR, 0, DIF_VAR_ARG6, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "int64_t" },
{ "arg7", DT_IDENT_SCALAR, 0, DIF_VAR_ARG7, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "int64_t" },
{ "arg8", DT_IDENT_SCALAR, 0, DIF_VAR_ARG8, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "int64_t" },
{ "arg9", DT_IDENT_SCALAR, 0, DIF_VAR_ARG9, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "int64_t" },
{ "args", DT_IDENT_ARRAY, 0, DIF_VAR_ARGS, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_args, NULL },
{ "avg", DT_IDENT_AGGFUNC, 0, DTRACEAGG_AVG, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(@)" },
{ "basename", DT_IDENT_FUNC, 0, DIF_SUBR_BASENAME, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "string(const char *)" },
{ "bcopy", DT_IDENT_FUNC, 0, DIF_SUBR_BCOPY, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(void *, void *, size_t)" },
{ "breakpoint", DT_IDENT_ACTFUNC, 0, DT_ACT_BREAKPOINT,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void()" },
{ "caller", DT_IDENT_SCALAR, 0, DIF_VAR_CALLER, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "uintptr_t" },
{ "chill", DT_IDENT_ACTFUNC, 0, DT_ACT_CHILL, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(int)" },
{ "cleanpath", DT_IDENT_FUNC, 0, DIF_SUBR_CLEANPATH, DT_ATTR_STABCMN,
	DT_VERS_1_0, &dt_idops_func, "string(const char *)" },
{ "clear", DT_IDENT_ACTFUNC, 0, DT_ACT_CLEAR, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(...)" },
{ "commit", DT_IDENT_ACTFUNC, 0, DT_ACT_COMMIT, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(int)" },
{ "copyin", DT_IDENT_FUNC, 0, DIF_SUBR_COPYIN, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void *(uintptr_t, size_t)" },
{ "copyinstr", DT_IDENT_FUNC, 0, DIF_SUBR_COPYINSTR,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "string(uintptr_t, [size_t])" },
{ "copyinto", DT_IDENT_FUNC, 0, DIF_SUBR_COPYINTO, DT_ATTR_STABCMN,
	DT_VERS_1_0, &dt_idops_func, "void(uintptr_t, size_t, void *)" },
{ "copyout", DT_IDENT_FUNC, 0, DIF_SUBR_COPYOUT, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(void *, uintptr_t, size_t)" },
{ "copyoutstr", DT_IDENT_FUNC, 0, DIF_SUBR_COPYOUTSTR,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(char *, uintptr_t, size_t)" },
{ "count", DT_IDENT_AGGFUNC, 0, DTRACEAGG_COUNT, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void()" },
{ "curthread", DT_IDENT_SCALAR, 0, DIF_VAR_CURTHREAD,
	{ DTRACE_STABILITY_STABLE, DTRACE_STABILITY_PRIVATE,
	DTRACE_CLASS_COMMON }, DT_VERS_1_0,
#ifdef illumos
	&dt_idops_type, "genunix`kthread_t *" },
#else
	&dt_idops_type, curthread_str },
#endif
{ "ddi_pathname", DT_IDENT_FUNC, 0, DIF_SUBR_DDI_PATHNAME,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, "string(void *, int64_t)" },
{ "denormalize", DT_IDENT_ACTFUNC, 0, DT_ACT_DENORMALIZE, DT_ATTR_STABCMN,
	DT_VERS_1_0, &dt_idops_func, "void(...)" },
{ "dirname", DT_IDENT_FUNC, 0, DIF_SUBR_DIRNAME, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "string(const char *)" },
{ "discard", DT_IDENT_ACTFUNC, 0, DT_ACT_DISCARD, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(int)" },
{ "epid", DT_IDENT_SCALAR, 0, DIF_VAR_EPID, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "uint_t" },
{ "errno", DT_IDENT_SCALAR, 0, DIF_VAR_ERRNO, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "int" },
{ "execargs", DT_IDENT_SCALAR, 0, DIF_VAR_EXECARGS,
	DT_ATTR_STABCMN, DT_VERS_1_0, &dt_idops_type, "string" },
{ "execname", DT_IDENT_SCALAR, 0, DIF_VAR_EXECNAME,
	DT_ATTR_STABCMN, DT_VERS_1_0, &dt_idops_type, "string" },
{ "exit", DT_IDENT_ACTFUNC, 0, DT_ACT_EXIT, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(int)" },
{ "freopen", DT_IDENT_ACTFUNC, 0, DT_ACT_FREOPEN, DT_ATTR_STABCMN,
	DT_VERS_1_1, &dt_idops_func, "void(@, ...)" },
{ "ftruncate", DT_IDENT_ACTFUNC, 0, DT_ACT_FTRUNCATE, DT_ATTR_STABCMN,
	DT_VERS_1_0, &dt_idops_func, "void()" },
{ "func", DT_IDENT_ACTFUNC, 0, DT_ACT_SYM, DT_ATTR_STABCMN,
	DT_VERS_1_2, &dt_idops_func, "_symaddr(uintptr_t)" },
{ "getmajor", DT_IDENT_FUNC, 0, DIF_SUBR_GETMAJOR,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, "genunix`major_t(genunix`dev_t)" },
{ "getminor", DT_IDENT_FUNC, 0, DIF_SUBR_GETMINOR,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, "genunix`minor_t(genunix`dev_t)" },
{ "htonl", DT_IDENT_FUNC, 0, DIF_SUBR_HTONL, DT_ATTR_EVOLCMN, DT_VERS_1_3,
	&dt_idops_func, "uint32_t(uint32_t)" },
{ "htonll", DT_IDENT_FUNC, 0, DIF_SUBR_HTONLL, DT_ATTR_EVOLCMN, DT_VERS_1_3,
	&dt_idops_func, "uint64_t(uint64_t)" },
{ "htons", DT_IDENT_FUNC, 0, DIF_SUBR_HTONS, DT_ATTR_EVOLCMN, DT_VERS_1_3,
	&dt_idops_func, "uint16_t(uint16_t)" },
{ "getf", DT_IDENT_FUNC, 0, DIF_SUBR_GETF, DT_ATTR_STABCMN, DT_VERS_1_10,
	&dt_idops_func, "file_t *(int)" },
{ "gid", DT_IDENT_SCALAR, 0, DIF_VAR_GID, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "gid_t" },
{ "id", DT_IDENT_SCALAR, 0, DIF_VAR_ID, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "uint_t" },
{ "index", DT_IDENT_FUNC, 0, DIF_SUBR_INDEX, DT_ATTR_STABCMN, DT_VERS_1_1,
	&dt_idops_func, "int(const char *, const char *, [int])" },
{ "inet_ntoa", DT_IDENT_FUNC, 0, DIF_SUBR_INET_NTOA, DT_ATTR_STABCMN,
#ifdef illumos
	DT_VERS_1_5, &dt_idops_func, "string(ipaddr_t *)" },
#else
	DT_VERS_1_5, &dt_idops_func, "string(in_addr_t *)" },
#endif
{ "inet_ntoa6", DT_IDENT_FUNC, 0, DIF_SUBR_INET_NTOA6, DT_ATTR_STABCMN,
#ifdef illumos
	DT_VERS_1_5, &dt_idops_func, "string(in6_addr_t *)" },
#else
	DT_VERS_1_5, &dt_idops_func, "string(struct in6_addr *)" },
#endif
{ "inet_ntop", DT_IDENT_FUNC, 0, DIF_SUBR_INET_NTOP, DT_ATTR_STABCMN,
	DT_VERS_1_5, &dt_idops_func, "string(int, void *)" },
{ "ipl", DT_IDENT_SCALAR, 0, DIF_VAR_IPL, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "uint_t" },
#ifdef __FreeBSD__
{ "jailname", DT_IDENT_SCALAR, 0, DIF_VAR_JAILNAME,
	DT_ATTR_STABCMN, DT_VERS_1_13, &dt_idops_type, "string" },
{ "jid", DT_IDENT_SCALAR, 0, DIF_VAR_JID, DT_ATTR_STABCMN, DT_VERS_1_13,
	&dt_idops_type, "int" },
#endif
{ "json", DT_IDENT_FUNC, 0, DIF_SUBR_JSON, DT_ATTR_STABCMN, DT_VERS_1_11,
	&dt_idops_func, "string(const char *, const char *)" },
{ "jstack", DT_IDENT_ACTFUNC, 0, DT_ACT_JSTACK, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "stack(...)" },
{ "lltostr", DT_IDENT_FUNC, 0, DIF_SUBR_LLTOSTR, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "string(int64_t, [int])" },
{ "llquantize", DT_IDENT_AGGFUNC, 0, DTRACEAGG_LLQUANTIZE, DT_ATTR_STABCMN,
	DT_VERS_1_7, &dt_idops_func,
	"void(@, int32_t, int32_t, int32_t, int32_t, ...)" },
{ "lquantize", DT_IDENT_AGGFUNC, 0, DTRACEAGG_LQUANTIZE,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(@, int32_t, int32_t, ...)" },
{ "max", DT_IDENT_AGGFUNC, 0, DTRACEAGG_MAX, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(@)" },
{ "memref", DT_IDENT_FUNC, 0, DIF_SUBR_MEMREF, DT_ATTR_STABCMN, DT_VERS_1_1,
	&dt_idops_func, "uintptr_t *(void *, size_t)" },
#ifndef illumos
{ "memstr", DT_IDENT_FUNC, 0, DIF_SUBR_MEMSTR, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "string(void *, char, size_t)" },
#endif
{ "min", DT_IDENT_AGGFUNC, 0, DTRACEAGG_MIN, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(@)" },
{ "mod", DT_IDENT_ACTFUNC, 0, DT_ACT_MOD, DT_ATTR_STABCMN,
	DT_VERS_1_2, &dt_idops_func, "_symaddr(uintptr_t)" },
#ifdef illumos
{ "msgdsize", DT_IDENT_FUNC, 0, DIF_SUBR_MSGDSIZE,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "size_t(mblk_t *)" },
{ "msgsize", DT_IDENT_FUNC, 0, DIF_SUBR_MSGSIZE,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "size_t(mblk_t *)" },
{ "mutex_owned", DT_IDENT_FUNC, 0, DIF_SUBR_MUTEX_OWNED,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, "int(genunix`kmutex_t *)" },
{ "mutex_owner", DT_IDENT_FUNC, 0, DIF_SUBR_MUTEX_OWNER,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, "genunix`kthread_t *(genunix`kmutex_t *)" },
{ "mutex_type_adaptive", DT_IDENT_FUNC, 0, DIF_SUBR_MUTEX_TYPE_ADAPTIVE,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, "int(genunix`kmutex_t *)" },
{ "mutex_type_spin", DT_IDENT_FUNC, 0, DIF_SUBR_MUTEX_TYPE_SPIN,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, "int(genunix`kmutex_t *)" },
#else
{ "mutex_owned", DT_IDENT_FUNC, 0, DIF_SUBR_MUTEX_OWNED,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, intmtx_str },
{ "mutex_owner", DT_IDENT_FUNC, 0, DIF_SUBR_MUTEX_OWNER,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, threadmtx_str },
{ "mutex_type_adaptive", DT_IDENT_FUNC, 0, DIF_SUBR_MUTEX_TYPE_ADAPTIVE,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, intmtx_str },
{ "mutex_type_spin", DT_IDENT_FUNC, 0, DIF_SUBR_MUTEX_TYPE_SPIN,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, intmtx_str },
#endif
{ "ntohl", DT_IDENT_FUNC, 0, DIF_SUBR_NTOHL, DT_ATTR_EVOLCMN, DT_VERS_1_3,
	&dt_idops_func, "uint32_t(uint32_t)" },
{ "ntohll", DT_IDENT_FUNC, 0, DIF_SUBR_NTOHLL, DT_ATTR_EVOLCMN, DT_VERS_1_3,
	&dt_idops_func, "uint64_t(uint64_t)" },
{ "ntohs", DT_IDENT_FUNC, 0, DIF_SUBR_NTOHS, DT_ATTR_EVOLCMN, DT_VERS_1_3,
	&dt_idops_func, "uint16_t(uint16_t)" },
{ "normalize", DT_IDENT_ACTFUNC, 0, DT_ACT_NORMALIZE, DT_ATTR_STABCMN,
	DT_VERS_1_0, &dt_idops_func, "void(...)" },
{ "panic", DT_IDENT_ACTFUNC, 0, DT_ACT_PANIC, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void()" },
{ "pid", DT_IDENT_SCALAR, 0, DIF_VAR_PID, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "pid_t" },
{ "ppid", DT_IDENT_SCALAR, 0, DIF_VAR_PPID, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "pid_t" },
{ "print", DT_IDENT_ACTFUNC, 0, DT_ACT_PRINT, DT_ATTR_STABCMN, DT_VERS_1_9,
	&dt_idops_func, "void(@)" },
{ "printa", DT_IDENT_ACTFUNC, 0, DT_ACT_PRINTA, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(@, ...)" },
{ "printf", DT_IDENT_ACTFUNC, 0, DT_ACT_PRINTF, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(@, ...)" },
{ "printm", DT_IDENT_ACTFUNC, 0, DT_ACT_PRINTM, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(size_t, uintptr_t *)" },
{ "probefunc", DT_IDENT_SCALAR, 0, DIF_VAR_PROBEFUNC,
	DT_ATTR_STABCMN, DT_VERS_1_0, &dt_idops_type, "string" },
{ "probemod", DT_IDENT_SCALAR, 0, DIF_VAR_PROBEMOD,
	DT_ATTR_STABCMN, DT_VERS_1_0, &dt_idops_type, "string" },
{ "probename", DT_IDENT_SCALAR, 0, DIF_VAR_PROBENAME,
	DT_ATTR_STABCMN, DT_VERS_1_0, &dt_idops_type, "string" },
{ "probeprov", DT_IDENT_SCALAR, 0, DIF_VAR_PROBEPROV,
	DT_ATTR_STABCMN, DT_VERS_1_0, &dt_idops_type, "string" },
{ "progenyof", DT_IDENT_FUNC, 0, DIF_SUBR_PROGENYOF,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "int(pid_t)" },
{ "quantize", DT_IDENT_AGGFUNC, 0, DTRACEAGG_QUANTIZE,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(@, ...)" },
{ "raise", DT_IDENT_ACTFUNC, 0, DT_ACT_RAISE, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(int)" },
{ "rand", DT_IDENT_FUNC, 0, DIF_SUBR_RAND, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "int()" },
{ "rindex", DT_IDENT_FUNC, 0, DIF_SUBR_RINDEX, DT_ATTR_STABCMN, DT_VERS_1_1,
	&dt_idops_func, "int(const char *, const char *, [int])" },
#ifdef illumos
{ "rw_iswriter", DT_IDENT_FUNC, 0, DIF_SUBR_RW_ISWRITER,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, "int(genunix`krwlock_t *)" },
{ "rw_read_held", DT_IDENT_FUNC, 0, DIF_SUBR_RW_READ_HELD,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, "int(genunix`krwlock_t *)" },
{ "rw_write_held", DT_IDENT_FUNC, 0, DIF_SUBR_RW_WRITE_HELD,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, "int(genunix`krwlock_t *)" },
#else
{ "rw_iswriter", DT_IDENT_FUNC, 0, DIF_SUBR_RW_ISWRITER,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, rwlock_str },
{ "rw_read_held", DT_IDENT_FUNC, 0, DIF_SUBR_RW_READ_HELD,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, rwlock_str },
{ "rw_write_held", DT_IDENT_FUNC, 0, DIF_SUBR_RW_WRITE_HELD,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, rwlock_str },
#endif
{ "self", DT_IDENT_PTR, 0, 0, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "void" },
{ "setopt", DT_IDENT_ACTFUNC, 0, DT_ACT_SETOPT, DT_ATTR_STABCMN,
	DT_VERS_1_2, &dt_idops_func, "void(const char *, [const char *])" },
{ "speculate", DT_IDENT_ACTFUNC, 0, DT_ACT_SPECULATE,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(int)" },
{ "speculation", DT_IDENT_FUNC, 0, DIF_SUBR_SPECULATION,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "int()" },
{ "stack", DT_IDENT_ACTFUNC, 0, DT_ACT_STACK, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "stack(...)" },
{ "stackdepth", DT_IDENT_SCALAR, 0, DIF_VAR_STACKDEPTH,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "uint32_t" },
{ "stddev", DT_IDENT_AGGFUNC, 0, DTRACEAGG_STDDEV, DT_ATTR_STABCMN,
	DT_VERS_1_6, &dt_idops_func, "void(@)" },
{ "stop", DT_IDENT_ACTFUNC, 0, DT_ACT_STOP, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void()" },
{ "strchr", DT_IDENT_FUNC, 0, DIF_SUBR_STRCHR, DT_ATTR_STABCMN, DT_VERS_1_1,
	&dt_idops_func, "string(const char *, char)" },
{ "strlen", DT_IDENT_FUNC, 0, DIF_SUBR_STRLEN, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "size_t(const char *)" },
{ "strjoin", DT_IDENT_FUNC, 0, DIF_SUBR_STRJOIN, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "string(const char *, const char *)" },
{ "strrchr", DT_IDENT_FUNC, 0, DIF_SUBR_STRRCHR, DT_ATTR_STABCMN, DT_VERS_1_1,
	&dt_idops_func, "string(const char *, char)" },
{ "strstr", DT_IDENT_FUNC, 0, DIF_SUBR_STRSTR, DT_ATTR_STABCMN, DT_VERS_1_1,
	&dt_idops_func, "string(const char *, const char *)" },
{ "strtok", DT_IDENT_FUNC, 0, DIF_SUBR_STRTOK, DT_ATTR_STABCMN, DT_VERS_1_1,
	&dt_idops_func, "string(const char *, const char *)" },
{ "strtoll", DT_IDENT_FUNC, 0, DIF_SUBR_STRTOLL, DT_ATTR_STABCMN, DT_VERS_1_11,
	&dt_idops_func, "int64_t(const char *, [int])" },
{ "substr", DT_IDENT_FUNC, 0, DIF_SUBR_SUBSTR, DT_ATTR_STABCMN, DT_VERS_1_1,
	&dt_idops_func, "string(const char *, int, [int])" },
{ "sum", DT_IDENT_AGGFUNC, 0, DTRACEAGG_SUM, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(@)" },
#ifndef illumos
{ "sx_isexclusive", DT_IDENT_FUNC, 0, DIF_SUBR_SX_ISEXCLUSIVE,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, sxlock_str },
{ "sx_shared_held", DT_IDENT_FUNC, 0, DIF_SUBR_SX_SHARED_HELD,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, sxlock_str },
{ "sx_exclusive_held", DT_IDENT_FUNC, 0, DIF_SUBR_SX_EXCLUSIVE_HELD,
	DT_ATTR_EVOLCMN, DT_VERS_1_0,
	&dt_idops_func, sxlock_str },
#endif
{ "sym", DT_IDENT_ACTFUNC, 0, DT_ACT_SYM, DT_ATTR_STABCMN,
	DT_VERS_1_2, &dt_idops_func, "_symaddr(uintptr_t)" },
{ "system", DT_IDENT_ACTFUNC, 0, DT_ACT_SYSTEM, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(@, ...)" },
{ "this", DT_IDENT_PTR, 0, 0, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "void" },
{ "tid", DT_IDENT_SCALAR, 0, DIF_VAR_TID, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "id_t" },
{ "timestamp", DT_IDENT_SCALAR, 0, DIF_VAR_TIMESTAMP,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "uint64_t" },
{ "tolower", DT_IDENT_FUNC, 0, DIF_SUBR_TOLOWER, DT_ATTR_STABCMN, DT_VERS_1_8,
	&dt_idops_func, "string(const char *)" },
{ "toupper", DT_IDENT_FUNC, 0, DIF_SUBR_TOUPPER, DT_ATTR_STABCMN, DT_VERS_1_8,
	&dt_idops_func, "string(const char *)" },
{ "trace", DT_IDENT_ACTFUNC, 0, DT_ACT_TRACE, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(@)" },
{ "tracemem", DT_IDENT_ACTFUNC, 0, DT_ACT_TRACEMEM,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "void(@, size_t, ...)" },
{ "trunc", DT_IDENT_ACTFUNC, 0, DT_ACT_TRUNC, DT_ATTR_STABCMN,
	DT_VERS_1_0, &dt_idops_func, "void(...)" },
{ "uaddr", DT_IDENT_ACTFUNC, 0, DT_ACT_UADDR, DT_ATTR_STABCMN,
	DT_VERS_1_2, &dt_idops_func, "_usymaddr(uintptr_t)" },
{ "ucaller", DT_IDENT_SCALAR, 0, DIF_VAR_UCALLER, DT_ATTR_STABCMN,
	DT_VERS_1_2, &dt_idops_type, "uint64_t" },
{ "ufunc", DT_IDENT_ACTFUNC, 0, DT_ACT_USYM, DT_ATTR_STABCMN,
	DT_VERS_1_2, &dt_idops_func, "_usymaddr(uintptr_t)" },
{ "uid", DT_IDENT_SCALAR, 0, DIF_VAR_UID, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "uid_t" },
{ "umod", DT_IDENT_ACTFUNC, 0, DT_ACT_UMOD, DT_ATTR_STABCMN,
	DT_VERS_1_2, &dt_idops_func, "_usymaddr(uintptr_t)" },
{ "uregs", DT_IDENT_ARRAY, 0, DIF_VAR_UREGS, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_regs, NULL },
{ "ustack", DT_IDENT_ACTFUNC, 0, DT_ACT_USTACK, DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_func, "stack(...)" },
{ "ustackdepth", DT_IDENT_SCALAR, 0, DIF_VAR_USTACKDEPTH,
	DT_ATTR_STABCMN, DT_VERS_1_2,
	&dt_idops_type, "uint32_t" },
{ "usym", DT_IDENT_ACTFUNC, 0, DT_ACT_USYM, DT_ATTR_STABCMN,
	DT_VERS_1_2, &dt_idops_func, "_usymaddr(uintptr_t)" },
{ "vtimestamp", DT_IDENT_SCALAR, 0, DIF_VAR_VTIMESTAMP,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "uint64_t" },
{ "walltimestamp", DT_IDENT_SCALAR, 0, DIF_VAR_WALLTIMESTAMP,
	DT_ATTR_STABCMN, DT_VERS_1_0,
	&dt_idops_type, "int64_t" },
{ "zonename", DT_IDENT_SCALAR, 0, DIF_VAR_ZONENAME,
	DT_ATTR_STABCMN, DT_VERS_1_0, &dt_idops_type, "string" },

#ifndef illumos
{ "cpu", DT_IDENT_SCALAR, 0, DIF_VAR_CPU,
	DT_ATTR_STABCMN, DT_VERS_1_6_3, &dt_idops_type, "int" },
#endif

{ NULL, 0, 0, 0, { 0, 0, 0 }, 0, NULL, NULL }
};

/*
 * Tables of ILP32 intrinsic integer and floating-point type templates to use
 * to populate the dynamic "C" CTF type container.
 */
static const dt_intrinsic_t _dtrace_intrinsics_32[] = {
{ "void", { CTF_INT_SIGNED, 0, 0 }, CTF_K_INTEGER },
{ "signed", { CTF_INT_SIGNED, 0, 32 }, CTF_K_INTEGER },
{ "unsigned", { 0, 0, 32 }, CTF_K_INTEGER },
{ "char", { CTF_INT_SIGNED | CTF_INT_CHAR, 0, 8 }, CTF_K_INTEGER },
{ "short", { CTF_INT_SIGNED, 0, 16 }, CTF_K_INTEGER },
{ "int", { CTF_INT_SIGNED, 0, 32 }, CTF_K_INTEGER },
{ "long", { CTF_INT_SIGNED, 0, 32 }, CTF_K_INTEGER },
{ "long long", { CTF_INT_SIGNED, 0, 64 }, CTF_K_INTEGER },
{ "signed char", { CTF_INT_SIGNED | CTF_INT_CHAR, 0, 8 }, CTF_K_INTEGER },
{ "signed short", { CTF_INT_SIGNED, 0, 16 }, CTF_K_INTEGER },
{ "signed int", { CTF_INT_SIGNED, 0, 32 }, CTF_K_INTEGER },
{ "signed long", { CTF_INT_SIGNED, 0, 32 }, CTF_K_INTEGER },
{ "signed long long", { CTF_INT_SIGNED, 0, 64 }, CTF_K_INTEGER },
{ "unsigned char", { CTF_INT_CHAR, 0, 8 }, CTF_K_INTEGER },
{ "unsigned short", { 0, 0, 16 }, CTF_K_INTEGER },
{ "unsigned int", { 0, 0, 32 }, CTF_K_INTEGER },
{ "unsigned long", { 0, 0, 32 }, CTF_K_INTEGER },
{ "unsigned long long", { 0, 0, 64 }, CTF_K_INTEGER },
{ "_Bool", { CTF_INT_BOOL, 0, 8 }, CTF_K_INTEGER },
{ "float", { CTF_FP_SINGLE, 0, 32 }, CTF_K_FLOAT },
{ "double", { CTF_FP_DOUBLE, 0, 64 }, CTF_K_FLOAT },
{ "long double", { CTF_FP_LDOUBLE, 0, 128 }, CTF_K_FLOAT },
{ "float imaginary", { CTF_FP_IMAGRY, 0, 32 }, CTF_K_FLOAT },
{ "double imaginary", { CTF_FP_DIMAGRY, 0, 64 }, CTF_K_FLOAT },
{ "long double imaginary", { CTF_FP_LDIMAGRY, 0, 128 }, CTF_K_FLOAT },
{ "float complex", { CTF_FP_CPLX, 0, 64 }, CTF_K_FLOAT },
{ "double complex", { CTF_FP_DCPLX, 0, 128 }, CTF_K_FLOAT },
{ "long double complex", { CTF_FP_LDCPLX, 0, 256 }, CTF_K_FLOAT },
{ NULL, { 0, 0, 0 }, 0 }
};

/*
 * Tables of LP64 intrinsic integer and floating-point type templates to use
 * to populate the dynamic "C" CTF type container.
 */
static const dt_intrinsic_t _dtrace_intrinsics_64[] = {
{ "void", { CTF_INT_SIGNED, 0, 0 }, CTF_K_INTEGER },
{ "signed", { CTF_INT_SIGNED, 0, 32 }, CTF_K_INTEGER },
{ "unsigned", { 0, 0, 32 }, CTF_K_INTEGER },
{ "char", { CTF_INT_SIGNED | CTF_INT_CHAR, 0, 8 }, CTF_K_INTEGER },
{ "short", { CTF_INT_SIGNED, 0, 16 }, CTF_K_INTEGER },
{ "int", { CTF_INT_SIGNED, 0, 32 }, CTF_K_INTEGER },
{ "long", { CTF_INT_SIGNED, 0, 64 }, CTF_K_INTEGER },
{ "long long", { CTF_INT_SIGNED, 0, 64 }, CTF_K_INTEGER },
{ "signed char", { CTF_INT_SIGNED | CTF_INT_CHAR, 0, 8 }, CTF_K_INTEGER },
{ "signed short", { CTF_INT_SIGNED, 0, 16 }, CTF_K_INTEGER },
{ "signed int", { CTF_INT_SIGNED, 0, 32 }, CTF_K_INTEGER },
{ "signed long", { CTF_INT_SIGNED, 0, 64 }, CTF_K_INTEGER },
{ "signed long long", { CTF_INT_SIGNED, 0, 64 }, CTF_K_INTEGER },
{ "unsigned char", { CTF_INT_CHAR, 0, 8 }, CTF_K_INTEGER },
{ "unsigned short", { 0, 0, 16 }, CTF_K_INTEGER },
{ "unsigned int", { 0, 0, 32 }, CTF_K_INTEGER },
{ "unsigned long", { 0, 0, 64 }, CTF_K_INTEGER },
{ "unsigned long long", { 0, 0, 64 }, CTF_K_INTEGER },
{ "_Bool", { CTF_INT_BOOL, 0, 8 }, CTF_K_INTEGER },
{ "float", { CTF_FP_SINGLE, 0, 32 }, CTF_K_FLOAT },
{ "double", { CTF_FP_DOUBLE, 0, 64 }, CTF_K_FLOAT },
{ "long double", { CTF_FP_LDOUBLE, 0, 128 }, CTF_K_FLOAT },
{ "float imaginary", { CTF_FP_IMAGRY, 0, 32 }, CTF_K_FLOAT },
{ "double imaginary", { CTF_FP_DIMAGRY, 0, 64 }, CTF_K_FLOAT },
{ "long double imaginary", { CTF_FP_LDIMAGRY, 0, 128 }, CTF_K_FLOAT },
{ "float complex", { CTF_FP_CPLX, 0, 64 }, CTF_K_FLOAT },
{ "double complex", { CTF_FP_DCPLX, 0, 128 }, CTF_K_FLOAT },
{ "long double complex", { CTF_FP_LDCPLX, 0, 256 }, CTF_K_FLOAT },
{ NULL, { 0, 0, 0 }, 0 }
};

/*
 * Tables of ILP32 typedefs to use to populate the dynamic "D" CTF container.
 * These aliases ensure that D definitions can use typical <sys/types.h> names.
 */
static const dt_typedef_t _dtrace_typedefs_32[] = {
{ "char", "int8_t" },
{ "short", "int16_t" },
{ "int", "int32_t" },
{ "long long", "int64_t" },
{ "int", "intptr_t" },
{ "int", "ssize_t" },
{ "unsigned char", "uint8_t" },
{ "unsigned short", "uint16_t" },
{ "unsigned", "uint32_t" },
{ "unsigned long long", "uint64_t" },
{ "unsigned char", "uchar_t" },
{ "unsigned short", "ushort_t" },
{ "unsigned", "uint_t" },
{ "unsigned long", "ulong_t" },
{ "unsigned long long", "u_longlong_t" },
{ "int", "ptrdiff_t" },
{ "unsigned", "uintptr_t" },
{ "unsigned", "size_t" },
{ "long", "id_t" },
{ "long", "pid_t" },
{ NULL, NULL }
};

/*
 * Tables of LP64 typedefs to use to populate the dynamic "D" CTF container.
 * These aliases ensure that D definitions can use typical <sys/types.h> names.
 */
static const dt_typedef_t _dtrace_typedefs_64[] = {
{ "char", "int8_t" },
{ "short", "int16_t" },
{ "int", "int32_t" },
{ "long", "int64_t" },
{ "long", "intptr_t" },
{ "long", "ssize_t" },
{ "unsigned char", "uint8_t" },
{ "unsigned short", "uint16_t" },
{ "unsigned", "uint32_t" },
{ "unsigned long", "uint64_t" },
{ "unsigned char", "uchar_t" },
{ "unsigned short", "ushort_t" },
{ "unsigned", "uint_t" },
{ "unsigned long", "ulong_t" },
{ "unsigned long long", "u_longlong_t" },
{ "long", "ptrdiff_t" },
{ "unsigned long", "uintptr_t" },
{ "unsigned long", "size_t" },
{ "int", "id_t" },
{ "int", "pid_t" },
{ NULL, NULL }
};

/*
 * Tables of ILP32 integer type templates used to populate the dtp->dt_ints[]
 * cache when a new dtrace client open occurs.  Values are set by dtrace_open().
 */
static const dt_intdesc_t _dtrace_ints_32[] = {
{ "int", NULL, CTF_ERR, 0x7fffffffULL },
{ "unsigned int", NULL, CTF_ERR, 0xffffffffULL },
{ "long", NULL, CTF_ERR, 0x7fffffffULL },
{ "unsigned long", NULL, CTF_ERR, 0xffffffffULL },
{ "long long", NULL, CTF_ERR, 0x7fffffffffffffffULL },
{ "unsigned long long", NULL, CTF_ERR, 0xffffffffffffffffULL }
};

/*
 * Tables of LP64 integer type templates used to populate the dtp->dt_ints[]
 * cache when a new dtrace client open occurs.  Values are set by dtrace_open().
 */
static const dt_intdesc_t _dtrace_ints_64[] = {
{ "int", NULL, CTF_ERR, 0x7fffffffULL },
{ "unsigned int", NULL, CTF_ERR, 0xffffffffULL },
{ "long", NULL, CTF_ERR, 0x7fffffffffffffffULL },
{ "unsigned long", NULL, CTF_ERR, 0xffffffffffffffffULL },
{ "long long", NULL, CTF_ERR, 0x7fffffffffffffffULL },
{ "unsigned long long", NULL, CTF_ERR, 0xffffffffffffffffULL }
};

/*
 * Table of macro variable templates used to populate the macro identifier hash
 * when a new dtrace client open occurs.  Values are set by dtrace_update().
 */
static const dt_ident_t _dtrace_macros[] = {
{ "egid", DT_IDENT_SCALAR, 0, 0, DT_ATTR_STABCMN, DT_VERS_1_0 },
{ "euid", DT_IDENT_SCALAR, 0, 0, DT_ATTR_STABCMN, DT_VERS_1_0 },
{ "gid", DT_IDENT_SCALAR, 0, 0, DT_ATTR_STABCMN, DT_VERS_1_0 },
{ "pid", DT_IDENT_SCALAR, 0, 0, DT_ATTR_STABCMN, DT_VERS_1_0 },
{ "pgid", DT_IDENT_SCALAR, 0, 0, DT_ATTR_STABCMN, DT_VERS_1_0 },
{ "ppid", DT_IDENT_SCALAR, 0, 0, DT_ATTR_STABCMN, DT_VERS_1_0 },
{ "projid", DT_IDENT_SCALAR, 0, 0, DT_ATTR_STABCMN, DT_VERS_1_0 },
{ "sid", DT_IDENT_SCALAR, 0, 0, DT_ATTR_STABCMN, DT_VERS_1_0 },
{ "taskid", DT_IDENT_SCALAR, 0, 0, DT_ATTR_STABCMN, DT_VERS_1_0 },
{ "target", DT_IDENT_SCALAR, 0, 0, DT_ATTR_STABCMN, DT_VERS_1_0 },
{ "uid", DT_IDENT_SCALAR, 0, 0, DT_ATTR_STABCMN, DT_VERS_1_0 },
{ NULL, 0, 0, 0, { 0, 0, 0 }, 0 }
};

/*
 * Hard-wired definition string to be compiled and cached every time a new
 * DTrace library handle is initialized.  This string should only be used to
 * contain definitions that should be present regardless of DTRACE_O_NOLIBS.
 */
static const char _dtrace_hardwire[] = "\
inline long NULL = 0; \n\
#pragma D binding \"1.0\" NULL\n\
";

/*
 * Default DTrace configuration to use when opening libdtrace DTRACE_O_NODEV.
 * If DTRACE_O_NODEV is not set, we load the configuration from the kernel.
 * The use of CTF_MODEL_NATIVE is more subtle than it might appear: we are
 * relying on the fact that when running dtrace(1M), isaexec will invoke the
 * binary with the same bitness as the kernel, which is what we want by default
 * when generating our DIF.  The user can override the choice using oflags.
 */
static const dtrace_conf_t _dtrace_conf = {
	DIF_VERSION,		/* dtc_difversion */
	DIF_DIR_NREGS,		/* dtc_difintregs */
	DIF_DTR_NREGS,		/* dtc_diftupregs */
	CTF_MODEL_NATIVE	/* dtc_ctfmodel */
};

const dtrace_attribute_t _dtrace_maxattr = {
	DTRACE_STABILITY_MAX,
	DTRACE_STABILITY_MAX,
	DTRACE_CLASS_MAX
};

const dtrace_attribute_t _dtrace_defattr = {
	DTRACE_STABILITY_STABLE,
	DTRACE_STABILITY_STABLE,
	DTRACE_CLASS_COMMON
};

const dtrace_attribute_t _dtrace_symattr = {
	DTRACE_STABILITY_PRIVATE,
	DTRACE_STABILITY_PRIVATE,
	DTRACE_CLASS_UNKNOWN
};

const dtrace_attribute_t _dtrace_typattr = {
	DTRACE_STABILITY_PRIVATE,
	DTRACE_STABILITY_PRIVATE,
	DTRACE_CLASS_UNKNOWN
};

const dtrace_attribute_t _dtrace_prvattr = {
	DTRACE_STABILITY_PRIVATE,
	DTRACE_STABILITY_PRIVATE,
	DTRACE_CLASS_UNKNOWN
};

const dtrace_pattr_t _dtrace_prvdesc = {
{ DTRACE_STABILITY_UNSTABLE, DTRACE_STABILITY_UNSTABLE, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_UNSTABLE, DTRACE_STABILITY_UNSTABLE, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_UNSTABLE, DTRACE_STABILITY_UNSTABLE, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_UNSTABLE, DTRACE_STABILITY_UNSTABLE, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_UNSTABLE, DTRACE_STABILITY_UNSTABLE, DTRACE_CLASS_COMMON },
};

#ifdef illumos
const char *_dtrace_defcpp = "/usr/ccs/lib/cpp"; /* default cpp(1) to invoke */
const char *_dtrace_defld = "/usr/ccs/bin/ld";   /* default ld(1) to invoke */
#else
const char *_dtrace_defcpp = "cpp"; /* default cpp(1) to invoke */
const char *_dtrace_defld = "ld";   /* default ld(1) to invoke */
const char *_dtrace_defobjcopy = "objcopy"; /* default objcopy(1) to invoke */
#endif

const char *_dtrace_libdir = "/usr/lib/dtrace"; /* default library directory */
#ifdef illumos
const char *_dtrace_provdir = "/dev/dtrace/provider"; /* provider directory */
#else
const char *_dtrace_libdir32 = "/usr/lib32/dtrace";
const char *_dtrace_provdir = "/dev/dtrace"; /* provider directory */
#endif

int _dtrace_strbuckets = 211;	/* default number of hash buckets (prime) */
int _dtrace_intbuckets = 256;	/* default number of integer buckets (Pof2) */
uint_t _dtrace_strsize = 256;	/* default size of string intrinsic type */
uint_t _dtrace_stkindent = 14;	/* default whitespace indent for stack/ustack */
uint_t _dtrace_pidbuckets = 64; /* default number of pid hash buckets */
uint_t _dtrace_pidlrulim = 8;	/* default number of pid handles to cache */
size_t _dtrace_bufsize = 512;	/* default dt_buf_create() size */
int _dtrace_argmax = 32;	/* default maximum number of probe arguments */

int _dtrace_debug = 0;		/* debug messages enabled (off) */
const char *const _dtrace_version = DT_VERS_STRING; /* API version string */
int _dtrace_rdvers = RD_VERSION; /* rtld_db feature version */

typedef struct dt_fdlist {
	int *df_fds;		/* array of provider driver file descriptors */
	uint_t df_ents;		/* number of valid elements in df_fds[] */
	uint_t df_size;		/* size of df_fds[] */
} dt_fdlist_t;

#ifdef illumos
#pragma init(_dtrace_init)
#else
void _dtrace_init(void) __attribute__ ((constructor));
#endif
void
_dtrace_init(void)
{
	_dtrace_debug = getenv("DTRACE_DEBUG") != NULL;

	for (; _dtrace_rdvers > 0; _dtrace_rdvers--) {
		if (rd_init(_dtrace_rdvers) == RD_OK)
			break;
	}
#if defined(__i386__)
	/* make long doubles 64 bits -sson */
	(void) fpsetprec(FP_PE);
#endif
}

static dtrace_hdl_t *
set_open_errno(dtrace_hdl_t *dtp, int *errp, int err)
{
	if (dtp != NULL)
		dtrace_close(dtp);
	if (errp != NULL)
		*errp = err;
	return (NULL);
}

static void
dt_provmod_open(dt_provmod_t **provmod, dt_fdlist_t *dfp)
{
	dt_provmod_t *prov;
	char path[PATH_MAX];
	int fd;
#ifdef illumos
	struct dirent *dp, *ep;
	DIR *dirp;

	if ((dirp = opendir(_dtrace_provdir)) == NULL)
		return; /* failed to open directory; just skip it */

	ep = alloca(sizeof (struct dirent) + PATH_MAX + 1);
	bzero(ep, sizeof (struct dirent) + PATH_MAX + 1);

	while (readdir_r(dirp, ep, &dp) == 0 && dp != NULL) {
		if (dp->d_name[0] == '.')
			continue; /* skip "." and ".." */

		if (dfp->df_ents == dfp->df_size) {
			uint_t size = dfp->df_size ? dfp->df_size * 2 : 16;
			int *fds = realloc(dfp->df_fds, size * sizeof (int));

			if (fds == NULL)
				break; /* skip the rest of this directory */

			dfp->df_fds = fds;
			dfp->df_size = size;
		}

		(void) snprintf(path, sizeof (path), "%s/%s",
		    _dtrace_provdir, dp->d_name);

		if ((fd = open(path, O_RDONLY)) == -1)
			continue; /* failed to open driver; just skip it */

		if (((prov = malloc(sizeof (dt_provmod_t))) == NULL) ||
		    (prov->dp_name = malloc(strlen(dp->d_name) + 1)) == NULL) {
			free(prov);
			(void) close(fd);
			break;
		}

		(void) strcpy(prov->dp_name, dp->d_name);
		prov->dp_next = *provmod;
		*provmod = prov;

		dt_dprintf("opened provider %s\n", dp->d_name);
		dfp->df_fds[dfp->df_ents++] = fd;
	}

	(void) closedir(dirp);
#else	/* !illumos */
	char	*p;
	char	*p1;
	char	*p_providers = NULL;
	int	error;
	size_t	len = 0;

	/*
	 * Loop to allocate/reallocate memory for the string of provider
	 * names and retry:
	 */
	while(1) {
		/*
		 * The first time around, get the string length. The next time,
		 * hopefully we've allocated enough memory.
		 */
		error = sysctlbyname("debug.dtrace.providers",p_providers,&len,NULL,0);
		if (len == 0)
			/* No providers? That's strange. Where's dtrace? */
			break;
		else if (error == 0 && p_providers == NULL) {
			/*
			 * Allocate the initial memory which should be enough
			 * unless another provider loads before we have
			 * time to go back and get the string.
			 */
			if ((p_providers = malloc(len)) == NULL)
				/* How do we report errors here? */
				return;
		} else if (error == -1 && errno == ENOMEM) {
			/*
			 * The current buffer isn't large enough, so
			 * reallocate it. We normally won't need to do this
			 * because providers aren't being loaded all the time.
			 */
		        if ((p = realloc(p_providers,len)) == NULL) {
			        free(p_providers);
				/* How do we report errors here? */
				return;
			}
			p_providers = p;
		} else
			break;
	}

	/* Check if we got a string of provider names: */
	if (error == 0 && len > 0 && p_providers != NULL) {
		p = p_providers;

		/*
		 * Parse the string containing the space separated
		 * provider names.
		 */
		while ((p1 = strsep(&p," ")) != NULL) {
			if (dfp->df_ents == dfp->df_size) {
				uint_t size = dfp->df_size ? dfp->df_size * 2 : 16;
				int *fds = realloc(dfp->df_fds, size * sizeof (int));

				if (fds == NULL)
					break;

				dfp->df_fds = fds;
				dfp->df_size = size;
			}

			(void) snprintf(path, sizeof (path), "/dev/dtrace/%s", p1);

			if ((fd = open(path, O_RDONLY | O_CLOEXEC)) == -1)
				continue; /* failed to open driver; just skip it */

			if (((prov = malloc(sizeof (dt_provmod_t))) == NULL) ||
			    (prov->dp_name = malloc(strlen(p1) + 1)) == NULL) {
				free(prov);
				(void) close(fd);
				break;
			}

			(void) strcpy(prov->dp_name, p1);
			prov->dp_next = *provmod;
			*provmod = prov;

			dt_dprintf("opened provider %s\n", p1);
			dfp->df_fds[dfp->df_ents++] = fd;
		}
	}
	if (p_providers != NULL)
		free(p_providers);
#endif	/* illumos */
}

static void
dt_provmod_destroy(dt_provmod_t **provmod)
{
	dt_provmod_t *next, *current;

	for (current = *provmod; current != NULL; current = next) {
		next = current->dp_next;
		free(current->dp_name);
		free(current);
	}

	*provmod = NULL;
}

#ifdef illumos
static const char *
dt_get_sysinfo(int cmd, char *buf, size_t len)
{
	ssize_t rv = sysinfo(cmd, buf, len);
	char *p = buf;

	if (rv < 0 || rv > len)
		(void) snprintf(buf, len, "%s", "Unknown");

	while ((p = strchr(p, '.')) != NULL)
		*p++ = '_';

	return (buf);
}
#endif

static dtrace_hdl_t *
dt_vopen(int version, int flags, int *errp,
    const dtrace_vector_t *vector, void *arg)
{
	dtrace_hdl_t *dtp = NULL;
	int dtfd = -1, ftfd = -1, fterr = 0;
	dtrace_prog_t *pgp;
	dt_module_t *dmp;
	dt_provmod_t *provmod = NULL;
	int i, err;
	struct rlimit rl;

	const dt_intrinsic_t *dinp;
	const dt_typedef_t *dtyp;
	const dt_ident_t *idp;

	dtrace_typeinfo_t dtt;
	ctf_funcinfo_t ctc;
	ctf_arinfo_t ctr;

	dt_fdlist_t df = { NULL, 0, 0 };

	char isadef[32], utsdef[32];
	char s1[64], s2[64];

	if (version <= 0)
		return (set_open_errno(dtp, errp, EINVAL));

	if (version > DTRACE_VERSION)
		return (set_open_errno(dtp, errp, EDT_VERSION));

	if (version < DTRACE_VERSION) {
		/*
		 * Currently, increasing the library version number is used to
		 * denote a binary incompatible change.  That is, a consumer
		 * of the library cannot run on a version of the library with
		 * a higher DTRACE_VERSION number than the consumer compiled
		 * against.  Once the library API has been committed to,
		 * backwards binary compatibility will be required; at that
		 * time, this check should change to return EDT_OVERSION only
		 * if the specified version number is less than the version
		 * number at the time of interface commitment.
		 */
		return (set_open_errno(dtp, errp, EDT_OVERSION));
	}

	if (flags & ~DTRACE_O_MASK)
		return (set_open_errno(dtp, errp, EINVAL));

	if ((flags & DTRACE_O_LP64) && (flags & DTRACE_O_ILP32))
		return (set_open_errno(dtp, errp, EINVAL));

	if (vector == NULL && arg != NULL)
		return (set_open_errno(dtp, errp, EINVAL));

	if (elf_version(EV_CURRENT) == EV_NONE)
		return (set_open_errno(dtp, errp, EDT_ELFVERSION));

	if (vector != NULL || (flags & DTRACE_O_NODEV))
		goto alloc; /* do not attempt to open dtrace device */

	/*
	 * Before we get going, crank our limit on file descriptors up to the
	 * hard limit.  This is to allow for the fact that libproc keeps file
	 * descriptors to objects open for the lifetime of the proc handle;
	 * without raising our hard limit, we would have an acceptably small
	 * bound on the number of processes that we could concurrently
	 * instrument with the pid provider.
	 */
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		rl.rlim_cur = rl.rlim_max;
		(void) setrlimit(RLIMIT_NOFILE, &rl);
	}

	/*
	 * Get the device path of each of the providers.  We hold them open
	 * in the df.df_fds list until we open the DTrace driver itself,
	 * allowing us to see all of the probes provided on this system.  Once
	 * we have the DTrace driver open, we can safely close all the providers
	 * now that they have registered with the framework.
	 */
	dt_provmod_open(&provmod, &df);

	dtfd = open("/dev/dtrace/dtrace", O_RDWR | O_CLOEXEC);
	err = errno; /* save errno from opening dtfd */
#if defined(__FreeBSD__)
	/*
	 * Automatically load the 'dtraceall' module if we couldn't open the
	 * char device.
	 */
	if (err == ENOENT && modfind("dtraceall") < 0) {
		kldload("dtraceall"); /* ignore the error */
		dtfd = open("/dev/dtrace/dtrace", O_RDWR | O_CLOEXEC);
		err = errno;
	}
#endif
#ifdef illumos
	ftfd = open("/dev/dtrace/provider/fasttrap", O_RDWR);
#else
	ftfd = open("/dev/dtrace/fasttrap", O_RDWR | O_CLOEXEC);
#endif
	fterr = ftfd == -1 ? errno : 0; /* save errno from open ftfd */

	while (df.df_ents-- != 0)
		(void) close(df.df_fds[df.df_ents]);

	free(df.df_fds);

	/*
	 * If we failed to open the dtrace device, fail dtrace_open().
	 * We convert some kernel errnos to custom libdtrace errnos to
	 * improve the resulting message from the usual strerror().
	 */
	if (dtfd == -1) {
		dt_provmod_destroy(&provmod);
		switch (err) {
		case ENOENT:
			err = EDT_NOENT;
			break;
		case EBUSY:
			err = EDT_BUSY;
			break;
		case EACCES:
			err = EDT_ACCESS;
			break;
		}
		return (set_open_errno(dtp, errp, err));
	}

alloc:
	if ((dtp = malloc(sizeof (dtrace_hdl_t))) == NULL) {
	        dt_provmod_destroy(&provmod);
		return (set_open_errno(dtp, errp, EDT_NOMEM));
	}

	bzero(dtp, sizeof (dtrace_hdl_t));
	dtp->dt_oflags = flags;
#ifdef illumos
	dtp->dt_prcmode = DT_PROC_STOP_PREINIT;
#else
	dtp->dt_prcmode = DT_PROC_STOP_POSTINIT;
#endif
	dtp->dt_linkmode = DT_LINK_KERNEL;
	dtp->dt_linktype = DT_LTYP_ELF;
	dtp->dt_xlatemode = DT_XL_STATIC;
	dtp->dt_stdcmode = DT_STDC_XA;
	dtp->dt_encoding = DT_ENCODING_UNSET;
	dtp->dt_version = version;
	dtp->dt_fd = dtfd;
	dtp->dt_ftfd = ftfd;
	dtp->dt_fterr = fterr;
	dtp->dt_cdefs_fd = -1;
	dtp->dt_ddefs_fd = -1;
#ifdef illumos
	dtp->dt_stdout_fd = -1;
#else
	dtp->dt_freopen_fp = NULL;
#endif
	dtp->dt_modbuckets = _dtrace_strbuckets;
	dtp->dt_mods = calloc(dtp->dt_modbuckets, sizeof (dt_module_t *));
#ifdef __FreeBSD__
	dtp->dt_kmods = calloc(dtp->dt_modbuckets, sizeof (dt_module_t *));
#endif
	dtp->dt_provbuckets = _dtrace_strbuckets;
	dtp->dt_provs = calloc(dtp->dt_provbuckets, sizeof (dt_provider_t *));
	dt_proc_init(dtp);
	dtp->dt_vmax = DT_VERS_LATEST;
	dtp->dt_cpp_path = strdup(_dtrace_defcpp);
	dtp->dt_cpp_argv = malloc(sizeof (char *));
	dtp->dt_cpp_argc = 1;
	dtp->dt_cpp_args = 1;
	dtp->dt_ld_path = strdup(_dtrace_defld);
#ifdef __FreeBSD__
	dtp->dt_objcopy_path = strdup(_dtrace_defobjcopy);
#endif
	dtp->dt_provmod = provmod;
	dtp->dt_vector = vector;
	dtp->dt_varg = arg;
	dt_dof_init(dtp);
	(void) uname(&dtp->dt_uts);

	if (dtp->dt_mods == NULL || dtp->dt_provs == NULL ||
	    dtp->dt_procs == NULL || dtp->dt_proc_env == NULL ||
	    dtp->dt_ld_path == NULL || dtp->dt_cpp_path == NULL ||
#ifdef __FreeBSD__
	    dtp->dt_kmods == NULL ||
	    dtp->dt_objcopy_path == NULL ||
#endif
	    dtp->dt_cpp_argv == NULL)
		return (set_open_errno(dtp, errp, EDT_NOMEM));

	for (i = 0; i < DTRACEOPT_MAX; i++)
		dtp->dt_options[i] = DTRACEOPT_UNSET;

	dtp->dt_cpp_argv[0] = (char *)strbasename(dtp->dt_cpp_path);

#ifdef illumos
	(void) snprintf(isadef, sizeof (isadef), "-D__SUNW_D_%u",
	    (uint_t)(sizeof (void *) * NBBY));

	(void) snprintf(utsdef, sizeof (utsdef), "-D__%s_%s",
	    dt_get_sysinfo(SI_SYSNAME, s1, sizeof (s1)),
	    dt_get_sysinfo(SI_RELEASE, s2, sizeof (s2)));

	if (dt_cpp_add_arg(dtp, "-D__sun") == NULL ||
	    dt_cpp_add_arg(dtp, "-D__unix") == NULL ||
	    dt_cpp_add_arg(dtp, "-D__SVR4") == NULL ||
	    dt_cpp_add_arg(dtp, "-D__SUNW_D=1") == NULL ||
	    dt_cpp_add_arg(dtp, isadef) == NULL ||
	    dt_cpp_add_arg(dtp, utsdef) == NULL)
		return (set_open_errno(dtp, errp, EDT_NOMEM));
#endif

	if (flags & DTRACE_O_NODEV)
		bcopy(&_dtrace_conf, &dtp->dt_conf, sizeof (_dtrace_conf));
	else if (dt_ioctl(dtp, DTRACEIOC_CONF, &dtp->dt_conf) != 0)
		return (set_open_errno(dtp, errp, errno));

	if (flags & DTRACE_O_LP64)
		dtp->dt_conf.dtc_ctfmodel = CTF_MODEL_LP64;
	else if (flags & DTRACE_O_ILP32)
		dtp->dt_conf.dtc_ctfmodel = CTF_MODEL_ILP32;

#ifdef __sparc
	/*
	 * On SPARC systems, __sparc is always defined for <sys/isa_defs.h>
	 * and __sparcv9 is defined if we are doing a 64-bit compile.
	 */
	if (dt_cpp_add_arg(dtp, "-D__sparc") == NULL)
		return (set_open_errno(dtp, errp, EDT_NOMEM));

	if (dtp->dt_conf.dtc_ctfmodel == CTF_MODEL_LP64 &&
	    dt_cpp_add_arg(dtp, "-D__sparcv9") == NULL)
		return (set_open_errno(dtp, errp, EDT_NOMEM));
#endif

#ifdef illumos
#ifdef __x86
	/*
	 * On x86 systems, __i386 is defined for <sys/isa_defs.h> for 32-bit
	 * compiles and __amd64 is defined for 64-bit compiles.  Unlike SPARC,
	 * they are defined exclusive of one another (see PSARC 2004/619).
	 */
	if (dtp->dt_conf.dtc_ctfmodel == CTF_MODEL_LP64) {
		if (dt_cpp_add_arg(dtp, "-D__amd64") == NULL)
			return (set_open_errno(dtp, errp, EDT_NOMEM));
	} else {
		if (dt_cpp_add_arg(dtp, "-D__i386") == NULL)
			return (set_open_errno(dtp, errp, EDT_NOMEM));
	}
#endif
#else
#if defined(__amd64__) || defined(__i386__)
	if (dtp->dt_conf.dtc_ctfmodel == CTF_MODEL_LP64) {
		if (dt_cpp_add_arg(dtp, "-m64") == NULL)
			return (set_open_errno(dtp, errp, EDT_NOMEM));
	} else {
		if (dt_cpp_add_arg(dtp, "-m32") == NULL)
			return (set_open_errno(dtp, errp, EDT_NOMEM));
	}
#endif
#endif

	if (dtp->dt_conf.dtc_difversion < DIF_VERSION)
		return (set_open_errno(dtp, errp, EDT_DIFVERS));

	if (dtp->dt_conf.dtc_ctfmodel == CTF_MODEL_ILP32)
		bcopy(_dtrace_ints_32, dtp->dt_ints, sizeof (_dtrace_ints_32));
	else
		bcopy(_dtrace_ints_64, dtp->dt_ints, sizeof (_dtrace_ints_64));

	/*
	 * On FreeBSD the kernel module name can't be hard-coded. The
	 * 'kern.bootfile' sysctl value tells us exactly which file is being
	 * used as the kernel.
	 */
#ifndef illumos
	{
	char bootfile[MAXPATHLEN];
	char *p;
	int i;
	size_t len = sizeof(bootfile);

	/* This call shouldn't fail, but use a default just in case. */
	if (sysctlbyname("kern.bootfile", bootfile, &len, NULL, 0) != 0)
		strlcpy(bootfile, "kernel", sizeof(bootfile));

	if ((p = strrchr(bootfile, '/')) != NULL)
		p++;
	else
		p = bootfile;

	/*
	 * Format the global variables based on the kernel module name.
	 */
	snprintf(curthread_str, sizeof(curthread_str), "%s`struct thread *",p);
	snprintf(intmtx_str, sizeof(intmtx_str), "int(%s`struct mtx *)",p);
	snprintf(threadmtx_str, sizeof(threadmtx_str), "struct thread *(%s`struct mtx *)",p);
	snprintf(rwlock_str, sizeof(rwlock_str), "int(%s`struct rwlock *)",p);
	snprintf(sxlock_str, sizeof(sxlock_str), "int(%s`struct sx *)",p);
	}
#endif

	dtp->dt_macros = dt_idhash_create("macro", NULL, 0, UINT_MAX);
	dtp->dt_aggs = dt_idhash_create("aggregation", NULL,
	    DTRACE_AGGVARIDNONE + 1, UINT_MAX);

	dtp->dt_globals = dt_idhash_create("global", _dtrace_globals,
	    DIF_VAR_OTHER_UBASE, DIF_VAR_OTHER_MAX);

	dtp->dt_tls = dt_idhash_create("thread local", NULL,
	    DIF_VAR_OTHER_UBASE, DIF_VAR_OTHER_MAX);

	if (dtp->dt_macros == NULL || dtp->dt_aggs == NULL ||
	    dtp->dt_globals == NULL || dtp->dt_tls == NULL)
		return (set_open_errno(dtp, errp, EDT_NOMEM));

	/*
	 * Populate the dt_macros identifier hash table by hand: we can't use
	 * the dt_idhash_populate() mechanism because we're not yet compiling
	 * and dtrace_update() needs to immediately reference these idents.
	 */
	for (idp = _dtrace_macros; idp->di_name != NULL; idp++) {
		if (dt_idhash_insert(dtp->dt_macros, idp->di_name,
		    idp->di_kind, idp->di_flags, idp->di_id, idp->di_attr,
		    idp->di_vers, idp->di_ops ? idp->di_ops : &dt_idops_thaw,
		    idp->di_iarg, 0) == NULL)
			return (set_open_errno(dtp, errp, EDT_NOMEM));
	}

	/*
	 * Update the module list using /system/object and load the values for
	 * the macro variable definitions according to the current process.
	 */
	dtrace_update(dtp);

	/*
	 * Select the intrinsics and typedefs we want based on the data model.
	 * The intrinsics are under "C".  The typedefs are added under "D".
	 */
	if (dtp->dt_conf.dtc_ctfmodel == CTF_MODEL_ILP32) {
		dinp = _dtrace_intrinsics_32;
		dtyp = _dtrace_typedefs_32;
	} else {
		dinp = _dtrace_intrinsics_64;
		dtyp = _dtrace_typedefs_64;
	}

	/*
	 * Create a dynamic CTF container under the "C" scope for intrinsic
	 * types and types defined in ANSI-C header files that are included.
	 */
	if ((dmp = dtp->dt_cdefs = dt_module_create(dtp, "C")) == NULL)
		return (set_open_errno(dtp, errp, EDT_NOMEM));

	if ((dmp->dm_ctfp = ctf_create(&dtp->dt_ctferr)) == NULL)
		return (set_open_errno(dtp, errp, EDT_CTF));

	dt_dprintf("created CTF container for %s (%p)\n",
	    dmp->dm_name, (void *)dmp->dm_ctfp);

	(void) ctf_setmodel(dmp->dm_ctfp, dtp->dt_conf.dtc_ctfmodel);
	ctf_setspecific(dmp->dm_ctfp, dmp);

	dmp->dm_flags = DT_DM_LOADED; /* fake up loaded bit */
	dmp->dm_modid = -1; /* no module ID */

	/*
	 * Fill the dynamic "C" CTF container with all of the intrinsic
	 * integer and floating-point types appropriate for this data model.
	 */
	for (; dinp->din_name != NULL; dinp++) {
		if (dinp->din_kind == CTF_K_INTEGER) {
			err = ctf_add_integer(dmp->dm_ctfp, CTF_ADD_ROOT,
			    dinp->din_name, &dinp->din_data);
		} else {
			err = ctf_add_float(dmp->dm_ctfp, CTF_ADD_ROOT,
			    dinp->din_name, &dinp->din_data);
		}

		if (err == CTF_ERR) {
			dt_dprintf("failed to add %s to C container: %s\n",
			    dinp->din_name, ctf_errmsg(
			    ctf_errno(dmp->dm_ctfp)));
			return (set_open_errno(dtp, errp, EDT_CTF));
		}
	}

	if (ctf_update(dmp->dm_ctfp) != 0) {
		dt_dprintf("failed to update C container: %s\n",
		    ctf_errmsg(ctf_errno(dmp->dm_ctfp)));
		return (set_open_errno(dtp, errp, EDT_CTF));
	}

	/*
	 * Add intrinsic pointer types that are needed to initialize printf
	 * format dictionary types (see table in dt_printf.c).
	 */
	(void) ctf_add_pointer(dmp->dm_ctfp, CTF_ADD_ROOT,
	    ctf_lookup_by_name(dmp->dm_ctfp, "void"));

	(void) ctf_add_pointer(dmp->dm_ctfp, CTF_ADD_ROOT,
	    ctf_lookup_by_name(dmp->dm_ctfp, "char"));

	(void) ctf_add_pointer(dmp->dm_ctfp, CTF_ADD_ROOT,
	    ctf_lookup_by_name(dmp->dm_ctfp, "int"));

	if (ctf_update(dmp->dm_ctfp) != 0) {
		dt_dprintf("failed to update C container: %s\n",
		    ctf_errmsg(ctf_errno(dmp->dm_ctfp)));
		return (set_open_errno(dtp, errp, EDT_CTF));
	}

	/*
	 * Create a dynamic CTF container under the "D" scope for types that
	 * are defined by the D program itself or on-the-fly by the D compiler.
	 * The "D" CTF container is a child of the "C" CTF container.
	 */
	if ((dmp = dtp->dt_ddefs = dt_module_create(dtp, "D")) == NULL)
		return (set_open_errno(dtp, errp, EDT_NOMEM));

	if ((dmp->dm_ctfp = ctf_create(&dtp->dt_ctferr)) == NULL)
		return (set_open_errno(dtp, errp, EDT_CTF));

	dt_dprintf("created CTF container for %s (%p)\n",
	    dmp->dm_name, (void *)dmp->dm_ctfp);

	(void) ctf_setmodel(dmp->dm_ctfp, dtp->dt_conf.dtc_ctfmodel);
	ctf_setspecific(dmp->dm_ctfp, dmp);

	dmp->dm_flags = DT_DM_LOADED; /* fake up loaded bit */
	dmp->dm_modid = -1; /* no module ID */

	if (ctf_import(dmp->dm_ctfp, dtp->dt_cdefs->dm_ctfp) == CTF_ERR) {
		dt_dprintf("failed to import D parent container: %s\n",
		    ctf_errmsg(ctf_errno(dmp->dm_ctfp)));
		return (set_open_errno(dtp, errp, EDT_CTF));
	}

	/*
	 * Fill the dynamic "D" CTF container with all of the built-in typedefs
	 * that we need to use for our D variable and function definitions.
	 * This ensures that basic inttypes.h names are always available to us.
	 */
	for (; dtyp->dty_src != NULL; dtyp++) {
		if (ctf_add_typedef(dmp->dm_ctfp, CTF_ADD_ROOT,
		    dtyp->dty_dst, ctf_lookup_by_name(dmp->dm_ctfp,
		    dtyp->dty_src)) == CTF_ERR) {
			dt_dprintf("failed to add typedef %s %s to D "
			    "container: %s", dtyp->dty_src, dtyp->dty_dst,
			    ctf_errmsg(ctf_errno(dmp->dm_ctfp)));
			return (set_open_errno(dtp, errp, EDT_CTF));
		}
	}

	/*
	 * Insert a CTF ID corresponding to a pointer to a type of kind
	 * CTF_K_FUNCTION we can use in the compiler for function pointers.
	 * CTF treats all function pointers as "int (*)()" so we only need one.
	 */
	ctc.ctc_return = ctf_lookup_by_name(dmp->dm_ctfp, "int");
	ctc.ctc_argc = 0;
	ctc.ctc_flags = 0;

	dtp->dt_type_func = ctf_add_function(dmp->dm_ctfp,
	    CTF_ADD_ROOT, &ctc, NULL);

	dtp->dt_type_fptr = ctf_add_pointer(dmp->dm_ctfp,
	    CTF_ADD_ROOT, dtp->dt_type_func);

	/*
	 * We also insert CTF definitions for the special D intrinsic types
	 * string and <DYN> into the D container.  The string type is added
	 * as a typedef of char[n].  The <DYN> type is an alias for void.
	 * We compare types to these special CTF ids throughout the compiler.
	 */
	ctr.ctr_contents = ctf_lookup_by_name(dmp->dm_ctfp, "char");
	ctr.ctr_index = ctf_lookup_by_name(dmp->dm_ctfp, "long");
	ctr.ctr_nelems = _dtrace_strsize;

	dtp->dt_type_str = ctf_add_typedef(dmp->dm_ctfp, CTF_ADD_ROOT,
	    "string", ctf_add_array(dmp->dm_ctfp, CTF_ADD_ROOT, &ctr));

	dtp->dt_type_dyn = ctf_add_typedef(dmp->dm_ctfp, CTF_ADD_ROOT,
	    "<DYN>", ctf_lookup_by_name(dmp->dm_ctfp, "void"));

	dtp->dt_type_stack = ctf_add_typedef(dmp->dm_ctfp, CTF_ADD_ROOT,
	    "stack", ctf_lookup_by_name(dmp->dm_ctfp, "void"));

	dtp->dt_type_symaddr = ctf_add_typedef(dmp->dm_ctfp, CTF_ADD_ROOT,
	    "_symaddr", ctf_lookup_by_name(dmp->dm_ctfp, "void"));

	dtp->dt_type_usymaddr = ctf_add_typedef(dmp->dm_ctfp, CTF_ADD_ROOT,
	    "_usymaddr", ctf_lookup_by_name(dmp->dm_ctfp, "void"));

	if (dtp->dt_type_func == CTF_ERR || dtp->dt_type_fptr == CTF_ERR ||
	    dtp->dt_type_str == CTF_ERR || dtp->dt_type_dyn == CTF_ERR ||
	    dtp->dt_type_stack == CTF_ERR || dtp->dt_type_symaddr == CTF_ERR ||
	    dtp->dt_type_usymaddr == CTF_ERR) {
		dt_dprintf("failed to add intrinsic to D container: %s\n",
		    ctf_errmsg(ctf_errno(dmp->dm_ctfp)));
		return (set_open_errno(dtp, errp, EDT_CTF));
	}

	if (ctf_update(dmp->dm_ctfp) != 0) {
		dt_dprintf("failed update D container: %s\n",
		    ctf_errmsg(ctf_errno(dmp->dm_ctfp)));
		return (set_open_errno(dtp, errp, EDT_CTF));
	}

	/*
	 * Initialize the integer description table used to convert integer
	 * constants to the appropriate types.  Refer to the comments above
	 * dt_node_int() for a complete description of how this table is used.
	 */
	for (i = 0; i < sizeof (dtp->dt_ints) / sizeof (dtp->dt_ints[0]); i++) {
		if (dtrace_lookup_by_type(dtp, DTRACE_OBJ_EVERY,
		    dtp->dt_ints[i].did_name, &dtt) != 0) {
			dt_dprintf("failed to lookup integer type %s: %s\n",
			    dtp->dt_ints[i].did_name,
			    dtrace_errmsg(dtp, dtrace_errno(dtp)));
			return (set_open_errno(dtp, errp, dtp->dt_errno));
		}
		dtp->dt_ints[i].did_ctfp = dtt.dtt_ctfp;
		dtp->dt_ints[i].did_type = dtt.dtt_type;
	}

	/*
	 * Now that we've created the "C" and "D" containers, move them to the
	 * start of the module list so that these types and symbols are found
	 * first (for stability) when iterating through the module list.
	 */
	dt_list_delete(&dtp->dt_modlist, dtp->dt_ddefs);
	dt_list_prepend(&dtp->dt_modlist, dtp->dt_ddefs);

	dt_list_delete(&dtp->dt_modlist, dtp->dt_cdefs);
	dt_list_prepend(&dtp->dt_modlist, dtp->dt_cdefs);

	if (dt_pfdict_create(dtp) == -1)
		return (set_open_errno(dtp, errp, dtp->dt_errno));

	/*
	 * If we are opening libdtrace DTRACE_O_NODEV enable C_ZDEFS by default
	 * because without /dev/dtrace open, we will not be able to load the
	 * names and attributes of any providers or probes from the kernel.
	 */
	if (flags & DTRACE_O_NODEV)
		dtp->dt_cflags |= DTRACE_C_ZDEFS;

	/*
	 * Load hard-wired inlines into the definition cache by calling the
	 * compiler on the raw definition string defined above.
	 */
	if ((pgp = dtrace_program_strcompile(dtp, _dtrace_hardwire,
	    DTRACE_PROBESPEC_NONE, DTRACE_C_EMPTY, 0, NULL)) == NULL) {
		dt_dprintf("failed to load hard-wired definitions: %s\n",
		    dtrace_errmsg(dtp, dtrace_errno(dtp)));
		return (set_open_errno(dtp, errp, EDT_HARDWIRE));
	}

	dt_program_destroy(dtp, pgp);

	/*
	 * Set up the default DTrace library path.  Once set, the next call to
	 * dt_compile() will compile all the libraries.  We intentionally defer
	 * library processing to improve overhead for clients that don't ever
	 * compile, and to provide better error reporting (because the full
	 * reporting of compiler errors requires dtrace_open() to succeed).
	 */
#ifdef __FreeBSD__
#ifdef __LP64__
	if ((dtp->dt_oflags & DTRACE_O_ILP32) != 0) {
		if (dtrace_setopt(dtp, "libdir", _dtrace_libdir32) != 0)
			return (set_open_errno(dtp, errp, dtp->dt_errno));
	}
#endif
	if (dtrace_setopt(dtp, "libdir", _dtrace_libdir) != 0)
		return (set_open_errno(dtp, errp, dtp->dt_errno));
#else
	if (dtrace_setopt(dtp, "libdir", _dtrace_libdir) != 0)
		return (set_open_errno(dtp, errp, dtp->dt_errno));
#endif

	return (dtp);
}

dtrace_hdl_t *
dtrace_open(int version, int flags, int *errp)
{
	return (dt_vopen(version, flags, errp, NULL, NULL));
}

dtrace_hdl_t *
dtrace_vopen(int version, int flags, int *errp,
    const dtrace_vector_t *vector, void *arg)
{
	return (dt_vopen(version, flags, errp, vector, arg));
}

void
dtrace_close(dtrace_hdl_t *dtp)
{
	dt_ident_t *idp, *ndp;
	dt_module_t *dmp;
	dt_provider_t *pvp;
	dtrace_prog_t *pgp;
	dt_xlator_t *dxp;
	dt_dirpath_t *dirp;
#ifdef __FreeBSD__
	dt_kmodule_t *dkm;
	uint_t h;
#endif
	int i;

	if (dtp->dt_procs != NULL)
		dt_proc_fini(dtp);

	while ((pgp = dt_list_next(&dtp->dt_programs)) != NULL)
		dt_program_destroy(dtp, pgp);

	while ((dxp = dt_list_next(&dtp->dt_xlators)) != NULL)
		dt_xlator_destroy(dtp, dxp);

	dt_free(dtp, dtp->dt_xlatormap);

	for (idp = dtp->dt_externs; idp != NULL; idp = ndp) {
		ndp = idp->di_next;
		dt_ident_destroy(idp);
	}

	if (dtp->dt_macros != NULL)
		dt_idhash_destroy(dtp->dt_macros);
	if (dtp->dt_aggs != NULL)
		dt_idhash_destroy(dtp->dt_aggs);
	if (dtp->dt_globals != NULL)
		dt_idhash_destroy(dtp->dt_globals);
	if (dtp->dt_tls != NULL)
		dt_idhash_destroy(dtp->dt_tls);

#ifdef __FreeBSD__
	for (h = 0; h < dtp->dt_modbuckets; h++)
		while ((dkm = dtp->dt_kmods[h]) != NULL) {
			dtp->dt_kmods[h] = dkm->dkm_next;
			free(dkm->dkm_name);
			free(dkm);
		}
#endif

	while ((dmp = dt_list_next(&dtp->dt_modlist)) != NULL)
		dt_module_destroy(dtp, dmp);

	while ((pvp = dt_list_next(&dtp->dt_provlist)) != NULL)
		dt_provider_destroy(dtp, pvp);

	if (dtp->dt_fd != -1)
		(void) close(dtp->dt_fd);
	if (dtp->dt_ftfd != -1)
		(void) close(dtp->dt_ftfd);
	if (dtp->dt_cdefs_fd != -1)
		(void) close(dtp->dt_cdefs_fd);
	if (dtp->dt_ddefs_fd != -1)
		(void) close(dtp->dt_ddefs_fd);
#ifdef illumos
	if (dtp->dt_stdout_fd != -1)
		(void) close(dtp->dt_stdout_fd);
#else
	if (dtp->dt_freopen_fp != NULL)
		(void) fclose(dtp->dt_freopen_fp);
#endif

	dt_epid_destroy(dtp);
	dt_aggid_destroy(dtp);
	dt_format_destroy(dtp);
	dt_strdata_destroy(dtp);
	dt_buffered_destroy(dtp);
	dt_aggregate_destroy(dtp);
	dt_pfdict_destroy(dtp);
	dt_provmod_destroy(&dtp->dt_provmod);
	dt_dof_fini(dtp);

	for (i = 1; i < dtp->dt_cpp_argc; i++)
		free(dtp->dt_cpp_argv[i]);

	while ((dirp = dt_list_next(&dtp->dt_lib_path)) != NULL) {
		dt_list_delete(&dtp->dt_lib_path, dirp);
		free(dirp->dir_path);
		free(dirp);
	}

	free(dtp->dt_cpp_argv);
	free(dtp->dt_cpp_path);
	free(dtp->dt_ld_path);
#ifdef __FreeBSD__
	free(dtp->dt_objcopy_path);
#endif

	free(dtp->dt_mods);
#ifdef __FreeBSD__
	free(dtp->dt_kmods);
#endif
	free(dtp->dt_provs);
	free(dtp);
}

int
dtrace_provider_modules(dtrace_hdl_t *dtp, const char **mods, int nmods)
{
	dt_provmod_t *prov;
	int i = 0;

	for (prov = dtp->dt_provmod; prov != NULL; prov = prov->dp_next, i++) {
		if (i < nmods)
			mods[i] = prov->dp_name;
	}

	return (i);
}

int
dtrace_ctlfd(dtrace_hdl_t *dtp)
{
	return (dtp->dt_fd);
}
