/*
 * Copyright (C) 2006-2008  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: httpd.h,v 1.9 2008/08/08 05:06:49 marka Exp $ */

#ifndef ISC_HTTPD_H
#define ISC_HTTPD_H 1

/*! \file */

#include <isc/event.h>
#include <isc/eventclass.h>
#include <isc/types.h>
#include <isc/mutex.h>
#include <isc/task.h>

#define HTTPD_EVENTCLASS		ISC_EVENTCLASS(4300)
#define HTTPD_SHUTDOWN			(HTTPD_EVENTCLASS + 0x0001)

#define ISC_HTTPDMGR_FLAGSHUTTINGDOWN	0x00000001

/*
 * Create a new http daemon which will send, once every time period,
 * a http-like header followed by HTTP data.
 */
isc_result_t
isc_httpdmgr_create(isc_mem_t *mctx, isc_socket_t *sock, isc_task_t *task,
		    isc_httpdclientok_t *client_ok,
		    isc_httpdondestroy_t *ondestory, void *cb_arg,
		    isc_timermgr_t *tmgr, isc_httpdmgr_t **httpdp);

void
isc_httpdmgr_shutdown(isc_httpdmgr_t **httpdp);

isc_result_t
isc_httpdmgr_addurl(isc_httpdmgr_t *httpdmgr, const char *url,
		    isc_httpdaction_t *func, void *arg);

isc_result_t
isc_httpd_response(isc_httpd_t *httpd);

isc_result_t
isc_httpd_addheader(isc_httpd_t *httpd, const char *name,
		    const char *val);

isc_result_t
isc_httpd_addheaderuint(isc_httpd_t *httpd, const char *name, int val);

isc_result_t isc_httpd_endheaders(isc_httpd_t *httpd);

#endif /* ISC_HTTPD_H */
