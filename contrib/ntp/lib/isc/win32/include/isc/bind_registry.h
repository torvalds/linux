/*
 * Copyright (C) 2004, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2001  Internet Software Consortium.
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

/* $Id: bind_registry.h,v 1.8 2007/06/19 23:47:20 tbox Exp $ */

#ifndef ISC_BINDREGISTRY_H
#define ISC_BINDREGISTRY_H

/*
 * BIND makes use of the following Registry keys in various places, especially
 * during startup and installation
 */

#define BIND_SUBKEY		"Software\\ISC\\BIND"
#define BIND_SESSION		"CurrentSession"
#define BIND_SESSION_SUBKEY	"Software\\ISC\\BIND\\CurrentSession"
#define BIND_UNINSTALL_SUBKEY	\
	"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ISC BIND"

#define EVENTLOG_APP_SUBKEY	\
	"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application"
#define BIND_MESSAGE_SUBKEY	\
	"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\named"
#define BIND_MESSAGE_NAME	"named"

#define BIND_SERVICE_SUBKEY	\
	"SYSTEM\\CurrentControlSet\\Services\\named"


#define BIND_CONFIGFILE		0
#define BIND_DEBUGLEVEL		1
#define BIND_QUERYLOG		2
#define BIND_FOREGROUND		3
#define BIND_PORT		4

#endif /* ISC_BINDREGISTRY_H */
