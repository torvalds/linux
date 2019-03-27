/*
 * Copyright (C) 2004, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2001-2003  Internet Software Consortium.
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

/* $Id: syslog.c,v 1.10 2007/06/19 23:47:19 tbox Exp $ */

#include <config.h>

#include <stdio.h>
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>

#include <isc/bindevt.h>
#include <isc/result.h>
#include <isc/syslog.h>
#include <isc/util.h>

static HANDLE hAppLog = NULL;
static FILE *log_stream;
static int debug_level = 0;

static struct dsn_c_pvt_sfnt {
	int val;
	const char *strval;
} facilities[] = {
	{ LOG_KERN,		"kern" },
	{ LOG_USER,		"user" },
	{ LOG_MAIL,		"mail" },
	{ LOG_DAEMON,		"daemon" },
	{ LOG_AUTH,		"auth" },
	{ LOG_SYSLOG,		"syslog" },
	{ LOG_LPR,		"lpr" },
#ifdef LOG_NEWS
	{ LOG_NEWS,		"news" },
#endif
#ifdef LOG_UUCP
	{ LOG_UUCP,		"uucp" },
#endif
#ifdef LOG_CRON
	{ LOG_CRON,		"cron" },
#endif
#ifdef LOG_AUTHPRIV
	{ LOG_AUTHPRIV,		"authpriv" },
#endif
#ifdef LOG_FTP
	{ LOG_FTP,		"ftp" },
#endif
	{ LOG_LOCAL0,		"local0"},
	{ LOG_LOCAL1,		"local1"},
	{ LOG_LOCAL2,		"local2"},
	{ LOG_LOCAL3,		"local3"},
	{ LOG_LOCAL4,		"local4"},
	{ LOG_LOCAL5,		"local5"},
	{ LOG_LOCAL6,		"local6"},
	{ LOG_LOCAL7,		"local7"},
	{ 0,			NULL }
};

isc_result_t
isc_syslog_facilityfromstring(const char *str, int *facilityp) {
	int i;

	REQUIRE(str != NULL);
	REQUIRE(facilityp != NULL);

	for (i = 0; facilities[i].strval != NULL; i++) {
		if (strcasecmp(facilities[i].strval, str) == 0) {
			*facilityp = facilities[i].val;
			return (ISC_R_SUCCESS);
		}
	}
	return (ISC_R_NOTFOUND);
}

/*
 * Log to the NT Event Log
 */
void
syslog(int level, const char *fmt, ...) {
	va_list ap;
	char buf[1024];
	char *str[1];

	str[0] = buf;

	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);

	/* Make sure that the channel is open to write the event */
	if (hAppLog != NULL) {
		switch (level) {
		case LOG_INFO:
		case LOG_NOTICE:
		case LOG_DEBUG:
			ReportEvent(hAppLog, EVENTLOG_INFORMATION_TYPE, 0,
				    BIND_INFO_MSG, NULL, 1, 0, str, NULL);
			break;
		case LOG_WARNING:
			ReportEvent(hAppLog, EVENTLOG_WARNING_TYPE, 0,
				    BIND_WARN_MSG, NULL, 1, 0, str, NULL);
			break;
		default:
			ReportEvent(hAppLog, EVENTLOG_ERROR_TYPE, 0,
				    BIND_ERR_MSG, NULL, 1, 0, str, NULL);
			break;
		}
	}
}

/*
 * Initialize event logging
 */
void
openlog(const char *name, int flags, ...) {
	/* Get a handle to the Application event log */
	hAppLog = RegisterEventSource(NULL, name);
}

/*
 * Close the Handle to the application Event Log
 * We don't care whether or not we succeeded so ignore return values
 * In fact if we failed then we would have nowhere to put the message
 */
void
closelog() {
	DeregisterEventSource(hAppLog);
}

/*
 * Keep event logging synced with the current debug level
 */
void
ModifyLogLevel(int level) {
	debug_level = level;	
}

/*
 * Initialize logging for the port section of libbind.
 * Piggyback onto stream given.
 */
void
InitNTLogging(FILE *stream, int debug) {
	log_stream = stream;
	ModifyLogLevel(debug);
}
/*
 * This function is for reporting errors to the application
 * event log in case the regular syslog is not available
 * mainly during startup. It should not be used under normal
 * circumstances.
 */
void
NTReportError(const char *name, const char *str) {
	HANDLE hNTAppLog = NULL;
	const char *buf[1];

	buf[0] = str;

	hNTAppLog = RegisterEventSource(NULL, name);

	ReportEvent(hNTAppLog, EVENTLOG_ERROR_TYPE, 0,
		    BIND_ERR_MSG, NULL, 1, 0, buf, NULL);

	DeregisterEventSource(hNTAppLog);
}
