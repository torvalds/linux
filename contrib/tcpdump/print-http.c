/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code
 * distributions retain the above copyright notice and this paragraph
 * in its entirety, and (2) distributions including binary code include
 * the above copyright notice and this paragraph in its entirety in
 * the documentation or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 */

/* \summary: Hypertext Transfer Protocol (HTTP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>
#include <stdlib.h>

#include "netdissect.h"
#include "extract.h"

/*
 * Includes WebDAV requests.
 */
static const char *httpcmds[] = {
	"GET",
	"PUT",
	"COPY",
	"HEAD",
	"LOCK",
	"MOVE",
	"POLL",
	"POST",
	"BCOPY",
	"BMOVE",
	"MKCOL",
	"TRACE",
	"LABEL",
	"MERGE",
	"DELETE",
	"SEARCH",
	"UNLOCK",
	"REPORT",
	"UPDATE",
	"NOTIFY",
	"BDELETE",
	"CONNECT",
	"OPTIONS",
	"CHECKIN",
	"PROPFIND",
	"CHECKOUT",
	"CCM_POST",
	"SUBSCRIBE",
	"PROPPATCH",
	"BPROPFIND",
	"BPROPPATCH",
	"UNCHECKOUT",
	"MKACTIVITY",
	"MKWORKSPACE",
	"UNSUBSCRIBE",
	"RPC_CONNECT",
	"VERSION-CONTROL",
	"BASELINE-CONTROL",
	NULL
};

void
http_print(netdissect_options *ndo, const u_char *pptr, u_int len)
{
	txtproto_print(ndo, pptr, len, "http", httpcmds, RESP_CODE_SECOND_TOKEN);
}
