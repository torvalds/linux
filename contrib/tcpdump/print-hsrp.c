/*
 * Copyright (C) 2001 Julian Cowley
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* \summary: Cisco Hot Standby Router Protocol (HSRP) printer */

/* Cisco Hot Standby Router Protocol (HSRP). */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "addrtoname.h"

/* HSRP op code types. */
static const char *op_code_str[] = {
	"hello",
	"coup",
	"resign"
};

/* HSRP states and associated names. */
static const struct tok states[] = {
	{  0, "initial" },
	{  1, "learn" },
	{  2, "listen" },
	{  4, "speak" },
	{  8, "standby" },
	{ 16, "active" },
	{  0, NULL }
};

/*
 * RFC 2281:
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   Version     |   Op Code     |     State     |   Hellotime   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   Holdtime    |   Priority    |     Group     |   Reserved    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Authentication  Data                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Authentication  Data                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Virtual IP Address                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

#define HSRP_AUTH_SIZE	8

/* HSRP protocol header. */
struct hsrp {
	uint8_t		hsrp_version;
	uint8_t		hsrp_op_code;
	uint8_t		hsrp_state;
	uint8_t		hsrp_hellotime;
	uint8_t		hsrp_holdtime;
	uint8_t		hsrp_priority;
	uint8_t		hsrp_group;
	uint8_t		hsrp_reserved;
	uint8_t		hsrp_authdata[HSRP_AUTH_SIZE];
	struct in_addr	hsrp_virtaddr;
};

void
hsrp_print(netdissect_options *ndo, register const uint8_t *bp, register u_int len)
{
	const struct hsrp *hp = (const struct hsrp *) bp;

	ND_TCHECK(hp->hsrp_version);
	ND_PRINT((ndo, "HSRPv%d", hp->hsrp_version));
	if (hp->hsrp_version != 0)
		return;
	ND_TCHECK(hp->hsrp_op_code);
	ND_PRINT((ndo, "-"));
	ND_PRINT((ndo, "%s ", tok2strary(op_code_str, "unknown (%d)", hp->hsrp_op_code)));
	ND_PRINT((ndo, "%d: ", len));
	ND_TCHECK(hp->hsrp_state);
	ND_PRINT((ndo, "state=%s ", tok2str(states, "Unknown (%d)", hp->hsrp_state)));
	ND_TCHECK(hp->hsrp_group);
	ND_PRINT((ndo, "group=%d ", hp->hsrp_group));
	ND_TCHECK(hp->hsrp_reserved);
	if (hp->hsrp_reserved != 0) {
		ND_PRINT((ndo, "[reserved=%d!] ", hp->hsrp_reserved));
	}
	ND_TCHECK(hp->hsrp_virtaddr);
	ND_PRINT((ndo, "addr=%s", ipaddr_string(ndo, &hp->hsrp_virtaddr)));
	if (ndo->ndo_vflag) {
		ND_PRINT((ndo, " hellotime="));
		unsigned_relts_print(ndo, hp->hsrp_hellotime);
		ND_PRINT((ndo, " holdtime="));
		unsigned_relts_print(ndo, hp->hsrp_holdtime);
		ND_PRINT((ndo, " priority=%d", hp->hsrp_priority));
		ND_PRINT((ndo, " auth=\""));
		if (fn_printn(ndo, hp->hsrp_authdata, sizeof(hp->hsrp_authdata),
		    ndo->ndo_snapend)) {
			ND_PRINT((ndo, "\""));
			goto trunc;
		}
		ND_PRINT((ndo, "\""));
	}
	return;
trunc:
	ND_PRINT((ndo, "[|hsrp]"));
}
