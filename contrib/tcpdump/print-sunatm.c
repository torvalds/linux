/*
 * Copyright (c) 1997 Yen Yen Lim and North Dakota State University
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Yen Yen Lim and
	North Dakota State University
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* \summary: SunATM DLPI capture printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

struct mbuf;
struct rtentry;

#include "netdissect.h"
#include "extract.h"

#include "atm.h"

/* SunATM header for ATM packet */
#define DIR_POS		0	/* Direction (0x80 = transmit, 0x00 = receive) */
#define VPI_POS		1	/* VPI */
#define VCI_POS		2	/* VCI */
#define PKT_BEGIN_POS   4	/* Start of the ATM packet */

/* Protocol type values in the bottom for bits of the byte at SUNATM_DIR_POS. */
#define PT_LANE		0x01	/* LANE */
#define PT_LLC		0x02	/* LLC encapsulation */

/*
 * This is the top level routine of the printer.  'p' points
 * to the SunATM pseudo-header for the packet, 'h->ts' is the timestamp,
 * 'h->len' is the length of the packet off the wire, and 'h->caplen'
 * is the number of bytes actually captured.
 */
u_int
sunatm_if_print(netdissect_options *ndo,
                const struct pcap_pkthdr *h, const u_char *p)
{
	u_int caplen = h->caplen;
	u_int length = h->len;
	u_short vci;
	u_char vpi;
	u_int traftype;

	if (caplen < PKT_BEGIN_POS) {
		ND_PRINT((ndo, "[|atm]"));
		return (caplen);
	}

	if (ndo->ndo_eflag) {
		ND_PRINT((ndo, p[DIR_POS] & 0x80 ? "Tx: " : "Rx: "));
	}

	switch (p[DIR_POS] & 0x0f) {

	case PT_LANE:
		traftype = ATM_LANE;
		break;

	case PT_LLC:
		traftype = ATM_LLC;
		break;

	default:
		traftype = ATM_UNKNOWN;
		break;
	}

	vci = EXTRACT_16BITS(&p[VCI_POS]);
	vpi = p[VPI_POS];

	p += PKT_BEGIN_POS;
	caplen -= PKT_BEGIN_POS;
	length -= PKT_BEGIN_POS;
	atm_print(ndo, vpi, vci, traftype, p, length, caplen);

	return (PKT_BEGIN_POS);
}
