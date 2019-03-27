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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 */

#include <string.h>
#include <strings.h>
#include <dt_impl.h>

static const struct {
	int err;
	const char *msg;
} _dt_errlist[] = {
	{ EDT_VERSION,	"Client requested version newer than library" },
	{ EDT_VERSINVAL, "Version is not properly formatted or is too large" },
	{ EDT_VERSUNDEF, "Requested version is not supported by compiler" },
	{ EDT_VERSREDUCED, "Requested version conflicts with earlier setting" },
	{ EDT_CTF,	"Unexpected libctf error" },
	{ EDT_COMPILER, "Error in D program compilation" },
	{ EDT_NOTUPREG,	"Insufficient tuple registers to generate code" },
	{ EDT_NOMEM,	"Memory allocation failure" },
	{ EDT_INT2BIG,	"Integer constant table limit exceeded" },
	{ EDT_STR2BIG,	"String constant table limit exceeded" },
	{ EDT_NOMOD,	"Unknown module name" },
	{ EDT_NOPROV,	"Unknown provider name" },
	{ EDT_NOPROBE,	"No probe matches description" },
	{ EDT_NOSYM,	"Unknown symbol name" },
	{ EDT_NOSYMADDR, "No symbol corresponds to address" },
	{ EDT_NOTYPE,	"Unknown type name" },
	{ EDT_NOVAR,	"Unknown variable name" },
	{ EDT_NOAGG,	"Unknown aggregation name" },
	{ EDT_BADSCOPE,	"Improper use of scoping operator in type name" },
	{ EDT_BADSPEC,	"Overspecified probe description" },
	{ EDT_BADSPCV,	"Undefined macro variable in probe description" },
	{ EDT_BADID,	"Unknown probe identifier" },
	{ EDT_NOTLOADED, "Module is no longer loaded" },
	{ EDT_NOCTF,	"Module does not contain any CTF data" },
	{ EDT_DATAMODEL, "Module and program data models do not match" },
	{ EDT_DIFVERS,	"Library uses newer DIF version than kernel" },
	{ EDT_BADAGG,	"Unknown aggregating action" },
	{ EDT_FIO,	"Error occurred while reading from input stream" },
	{ EDT_DIFINVAL,	"DIF program content is invalid" },
	{ EDT_DIFSIZE,	"DIF program exceeds maximum program size" },
	{ EDT_DIFFAULT,	"DIF program contains invalid pointer" },
	{ EDT_BADPROBE,	"Invalid probe specification" },
	{ EDT_BADPGLOB, "Probe description has too many globbing characters" },
	{ EDT_NOSCOPE,	"Declaration scope stack underflow" },
	{ EDT_NODECL,	"Declaration stack underflow" },
	{ EDT_DMISMATCH, "Data record list does not match statement" },
	{ EDT_DOFFSET,	"Data record offset exceeds buffer boundary" },
	{ EDT_DALIGN,	"Data record has inappropriate alignment" },
	{ EDT_BADOPTNAME, "Invalid option name" },
	{ EDT_BADOPTVAL, "Invalid value for specified option" },
	{ EDT_BADOPTCTX, "Option cannot be used from within a D program" },
	{ EDT_CPPFORK,	"Failed to fork preprocessor" },
	{ EDT_CPPEXEC,	"Failed to exec preprocessor" },
	{ EDT_CPPENT,	"Preprocessor not found" },
	{ EDT_CPPERR,	"Preprocessor failed to process input program" },
	{ EDT_SYMOFLOW,	"Symbol table identifier space exhausted" },
	{ EDT_ACTIVE,	"Operation illegal when tracing is active" },
	{ EDT_DESTRUCTIVE, "Destructive actions not allowed" },
	{ EDT_NOANON,	"No anonymous tracing state" },
	{ EDT_ISANON,	"Can't claim anonymous state and enable probes" },
	{ EDT_ENDTOOBIG, "END enablings exceed size of principal buffer" },
	{ EDT_NOCONV,	"Failed to load type for printf conversion" },
	{ EDT_BADCONV,	"Incomplete printf conversion" },
	{ EDT_BADERROR,	"Invalid library ERROR action" },
	{ EDT_ERRABORT,	"Abort due to error" },
	{ EDT_DROPABORT, "Abort due to drop" },
	{ EDT_DIRABORT,	"Abort explicitly directed" },
	{ EDT_BADRVAL,	"Invalid return value from callback" },
	{ EDT_BADNORMAL, "Invalid normalization" },
	{ EDT_BUFTOOSMALL, "Enabling exceeds size of buffer" },
	{ EDT_BADTRUNC, "Invalid truncation" },
	{ EDT_BUSY, "DTrace cannot be used when kernel debugger is active" },
	{ EDT_ACCESS, "DTrace requires additional privileges" },
	{ EDT_NOENT, "DTrace device not available on system" },
	{ EDT_BRICKED, "Abort due to systemic unresponsiveness" },
	{ EDT_HARDWIRE, "Failed to load language definitions" },
	{ EDT_ELFVERSION, "libelf is out-of-date with respect to libdtrace" },
	{ EDT_NOBUFFERED, "Attempt to buffer output without handler" },
	{ EDT_UNSTABLE, "Description matched an unstable set of probes" },
	{ EDT_BADSETOPT, "Invalid setopt() library action" },
	{ EDT_BADSTACKPC, "Invalid stack program counter size" },
	{ EDT_BADAGGVAR, "Invalid aggregation variable identifier" },
	{ EDT_OVERSION,	"Client requested deprecated version of library" },
	{ EDT_ENABLING_ERR, "Failed to enable probe" },
	{ EDT_NOPROBES, "No probe sites found for declared provider" },
	{ EDT_CANTLOAD, "Failed to load module" },
};

static const int _dt_nerr = sizeof (_dt_errlist) / sizeof (_dt_errlist[0]);

const char *
dtrace_errmsg(dtrace_hdl_t *dtp, int error)
{
	const char *str;
	int i;

	if (error == EDT_COMPILER && dtp != NULL && dtp->dt_errmsg[0] != '\0')
		str = dtp->dt_errmsg;
	else if (error == EDT_CTF && dtp != NULL && dtp->dt_ctferr != 0)
		str = ctf_errmsg(dtp->dt_ctferr);
	else if (error >= EDT_BASE && (error - EDT_BASE) < _dt_nerr) {
		for (i = 0; i < _dt_nerr; i++) {
			if (_dt_errlist[i].err == error)
				return (_dt_errlist[i].msg);
		}
		str = NULL;
	} else
		str = strerror(error);

	return (str ? str : "Unknown error");
}

int
dtrace_errno(dtrace_hdl_t *dtp)
{
	return (dtp->dt_errno);
}

#ifdef illumos
int
dt_set_errno(dtrace_hdl_t *dtp, int err)
{
	dtp->dt_errno = err;
	return (-1);
}
#else
int
_dt_set_errno(dtrace_hdl_t *dtp, int err, const char *errfile, int errline)
{
	dtp->dt_errno = err;
	dtp->dt_errfile = errfile;
	dtp->dt_errline = errline;
	return (-1);
}

void dt_get_errloc(dtrace_hdl_t *dtp, const char **p_errfile, int *p_errline)
{
	*p_errfile = dtp->dt_errfile;
	*p_errline = dtp->dt_errline;
}
#endif

void
dt_set_errmsg(dtrace_hdl_t *dtp, const char *errtag, const char *region,
    const char *filename, int lineno, const char *format, va_list ap)
{
	size_t len, n;
	char *p, *s;

	s = dtp->dt_errmsg;
	n = sizeof (dtp->dt_errmsg);

	if (errtag != NULL && (yypcb->pcb_cflags & DTRACE_C_ETAGS))
		(void) snprintf(s, n, "[%s] ", errtag);
	else
		s[0] = '\0';

	len = strlen(dtp->dt_errmsg);
	s = dtp->dt_errmsg + len;
	n = sizeof (dtp->dt_errmsg) - len;

	if (filename == NULL)
		filename = dtp->dt_filetag;

	if (filename != NULL)
		(void) snprintf(s, n, "\"%s\", line %d: ", filename, lineno);
	else if (lineno != 0)
		(void) snprintf(s, n, "line %d: ", lineno);
	else if (region != NULL)
		(void) snprintf(s, n, "in %s: ", region);

	len = strlen(dtp->dt_errmsg);
	s = dtp->dt_errmsg + len;
	n = sizeof (dtp->dt_errmsg) - len;
	(void) vsnprintf(s, n, format, ap);

	if ((p = strrchr(dtp->dt_errmsg, '\n')) != NULL)
		*p = '\0'; /* remove trailing \n from message buffer */

	dtp->dt_errtag = errtag;
}

/*ARGSUSED*/
const char *
dtrace_faultstr(dtrace_hdl_t *dtp, int fault)
{
	int i;

	static const struct {
		int code;
		const char *str;
	} faults[] = {
		{ DTRACEFLT_BADADDR,	"invalid address" },
		{ DTRACEFLT_BADALIGN,	"invalid alignment" },
		{ DTRACEFLT_ILLOP,	"illegal operation" },
		{ DTRACEFLT_DIVZERO,	"divide-by-zero" },
		{ DTRACEFLT_NOSCRATCH,	"out of scratch space" },
		{ DTRACEFLT_KPRIV,	"invalid kernel access" },
		{ DTRACEFLT_UPRIV,	"invalid user access" },
		{ DTRACEFLT_TUPOFLOW,	"tuple stack overflow" },
		{ DTRACEFLT_BADSTACK,	"bad stack" },
		{ DTRACEFLT_LIBRARY,	"library-level fault" },
		{ 0,			NULL }
	};

	for (i = 0; faults[i].str != NULL; i++) {
		if (faults[i].code == fault)
			return (faults[i].str);
	}

	return ("unknown fault");
}
