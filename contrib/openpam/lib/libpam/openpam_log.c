/*-
 * Copyright (c) 2002-2003 Networks Associates Technology, Inc.
 * Copyright (c) 2004-2011 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by ThinkSec AS and
 * Network Associates Laboratories, the Security Research Division of
 * Network Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $OpenPAM: openpam_log.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include <security/pam_appl.h>

#include "openpam_impl.h"
#include "openpam_asprintf.h"

int openpam_debug = 0;

#if !defined(openpam_log)

/*
 * OpenPAM extension
 *
 * Log a message through syslog
 */

void
openpam_log(int level, const char *fmt, ...)
{
	va_list ap;
	int priority;
	int serrno;

	switch (level) {
	case PAM_LOG_LIBDEBUG:
	case PAM_LOG_DEBUG:
		if (!openpam_debug)
			return;
		priority = LOG_DEBUG;
		break;
	case PAM_LOG_VERBOSE:
		priority = LOG_INFO;
		break;
	case PAM_LOG_NOTICE:
		priority = LOG_NOTICE;
		break;
	case PAM_LOG_ERROR:
	default:
		priority = LOG_ERR;
		break;
	}
	serrno = errno;
	va_start(ap, fmt);
	vsyslog(priority, fmt, ap);
	va_end(ap);
	errno = serrno;
}

#else

void
_openpam_log(int level, const char *func, const char *fmt, ...)
{
	va_list ap;
	char *format;
	int priority;
	int serrno;

	switch (level) {
	case PAM_LOG_LIBDEBUG:
	case PAM_LOG_DEBUG:
		if (!openpam_debug)
			return;
		priority = LOG_DEBUG;
		break;
	case PAM_LOG_VERBOSE:
		priority = LOG_INFO;
		break;
	case PAM_LOG_NOTICE:
		priority = LOG_NOTICE;
		break;
	case PAM_LOG_ERROR:
	default:
		priority = LOG_ERR;
		break;
	}
	serrno = errno;
	va_start(ap, fmt);
	if (asprintf(&format, "in %s(): %s", func, fmt) > 0) {
		errno = serrno;
		vsyslog(priority, format, ap);
		FREE(format);
	} else {
		errno = serrno;
		vsyslog(priority, fmt, ap);
	}
	va_end(ap);
	errno = serrno;
}

#endif

/**
 * The =openpam_log function logs messages using =syslog.
 * It is primarily intended for internal use by the library and modules.
 *
 * The =level argument indicates the importance of the message.
 * The following levels are defined:
 *
 *	=PAM_LOG_LIBDEBUG:
 *		Debugging messages.
 *		For internal use only.
 *	=PAM_LOG_DEBUG:
 *		Debugging messages.
 *		These messages are normally not logged unless the global
 *		integer variable :openpam_debug is set to a non-zero
 *		value, in which case they are logged with a =syslog
 *		priority of =LOG_DEBUG.
 *	=PAM_LOG_VERBOSE:
 *		Information about the progress of the authentication
 *		process, or other non-essential messages.
 *		These messages are logged with a =syslog priority of
 *		=LOG_INFO.
 *	=PAM_LOG_NOTICE:
 *		Messages relating to non-fatal errors.
 *		These messages are logged with a =syslog priority of
 *		=LOG_NOTICE.
 *	=PAM_LOG_ERROR:
 *		Messages relating to serious errors.
 *		These messages are logged with a =syslog priority of
 *		=LOG_ERR.
 *
 * The remaining arguments are a =printf format string and the
 * corresponding arguments.
 *
 * The =openpam_log function does not modify the value of :errno.
 */
