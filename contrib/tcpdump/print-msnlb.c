/*
 * Copyright (c) 2013 Romain Francoise <romain@orebokech.com>
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

/* \summary: MS Network Load Balancing's (NLB) heartbeat printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

struct msnlb_heartbeat_pkt {
	uint32_t unknown1;
	uint32_t unknown2;
	uint32_t host_prio;	/* little-endian */
	uint32_t virtual_ip;
	uint32_t host_ip;
	/* the protocol is undocumented so we ignore the rest */
};

void
msnlb_print(netdissect_options *ndo, const u_char *bp)
{
	const struct msnlb_heartbeat_pkt *hb;

	hb = (const struct msnlb_heartbeat_pkt *)bp;
	ND_TCHECK(*hb);

	ND_PRINT((ndo, "MS NLB heartbeat, host priority: %u,",
		EXTRACT_LE_32BITS(&(hb->host_prio))));
	ND_PRINT((ndo, " cluster IP: %s,", ipaddr_string(ndo, &(hb->virtual_ip))));
	ND_PRINT((ndo, " host IP: %s", ipaddr_string(ndo, &(hb->host_ip))));
	return;
trunc:
	ND_PRINT((ndo, "[|MS NLB]"));
}
