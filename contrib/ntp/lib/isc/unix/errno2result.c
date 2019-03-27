/*
 * Copyright (C) 2004, 2005, 2007, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000-2002  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

/*! \file */

#include <config.h>

#include <isc/result.h>
#include <isc/strerror.h>
#include <isc/util.h>

#include "errno2result.h"

/*%
 * Convert a POSIX errno value into an isc_result_t.  The
 * list of supported errno values is not complete; new users
 * of this function should add any expected errors that are
 * not already there.
 */
isc_result_t
isc___errno2result(int posixerrno, const char *file, unsigned int line) {
	char strbuf[ISC_STRERRORSIZE];

	switch (posixerrno) {
	case ENOTDIR:
	case ELOOP:
	case EINVAL:		/* XXX sometimes this is not for files */
	case ENAMETOOLONG:
	case EBADF:
		return (ISC_R_INVALIDFILE);
	case ENOENT:
		return (ISC_R_FILENOTFOUND);
	case EACCES:
	case EPERM:
		return (ISC_R_NOPERM);
	case EEXIST:
		return (ISC_R_FILEEXISTS);
	case EIO:
		return (ISC_R_IOERROR);
	case ENOMEM:
		return (ISC_R_NOMEMORY);
	case ENFILE:
	case EMFILE:
		return (ISC_R_TOOMANYOPENFILES);
	case EPIPE:
#ifdef ECONNRESET
	case ECONNRESET:
#endif
#ifdef ECONNABORTED
	case ECONNABORTED:
#endif
		return (ISC_R_CONNECTIONRESET);
#ifdef ENOTCONN
	case ENOTCONN:
		return (ISC_R_NOTCONNECTED);
#endif
#ifdef ETIMEDOUT
	case ETIMEDOUT:
		return (ISC_R_TIMEDOUT);
#endif
#ifdef ENOBUFS
	case ENOBUFS:
		return (ISC_R_NORESOURCES);
#endif
#ifdef EAFNOSUPPORT
	case EAFNOSUPPORT:
		return (ISC_R_FAMILYNOSUPPORT);
#endif
#ifdef ENETDOWN
	case ENETDOWN:
		return (ISC_R_NETDOWN);
#endif
#ifdef EHOSTDOWN
	case EHOSTDOWN:
		return (ISC_R_HOSTDOWN);
#endif
#ifdef ENETUNREACH
	case ENETUNREACH:
		return (ISC_R_NETUNREACH);
#endif
#ifdef EHOSTUNREACH
	case EHOSTUNREACH:
		return (ISC_R_HOSTUNREACH);
#endif
#ifdef EADDRINUSE
	case EADDRINUSE:
		return (ISC_R_ADDRINUSE);
#endif
	case EADDRNOTAVAIL:
		return (ISC_R_ADDRNOTAVAIL);
	case ECONNREFUSED:
		return (ISC_R_CONNREFUSED);
	default:
		isc__strerror(posixerrno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(file, line, "unable to convert errno "
				 "to isc_result: %d: %s",
				 posixerrno, strbuf);
		/*
		 * XXXDCL would be nice if perhaps this function could
		 * return the system's error string, so the caller
		 * might have something more descriptive than "unexpected
		 * error" to log with.
		 */
		return (ISC_R_UNEXPECTED);
	}
}
