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
 *
 * Original code by Hannes Gredler (hannes@gredler.at)
 * Turned into common "text protocol" code, which this uses, by
 * Guy Harris.
 */

/* \summary: Session Initiation Protocol (SIP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

static const char *sipcmds[] = {
	"ACK",
	"BYE",
	"CANCEL",
	"DO",
	"INFO",
	"INVITE",
	"MESSAGE",
	"NOTIFY",
	"OPTIONS",
	"PRACK",
	"QAUTH",
	"REFER",
	"REGISTER",
	"SPRACK",
	"SUBSCRIBE",
	"UPDATE",
	"PUBLISH",
	NULL
};

void
sip_print(netdissect_options *ndo, const u_char *pptr, u_int len)
{
	txtproto_print(ndo, pptr, len, "sip", sipcmds, RESP_CODE_SECOND_TOKEN);
}
