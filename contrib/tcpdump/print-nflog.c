/*
 * Copyright (c) 2013, Petar Alilovic,
 * Faculty of Electrical Engineering and Computing, University of Zagreb
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *	 this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/* \summary: DLT_NFLOG printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"

#if defined(DLT_NFLOG) && defined(HAVE_PCAP_NFLOG_H)
#include <pcap/nflog.h>

static const struct tok nflog_values[] = {
	{ AF_INET,		"IPv4" },
#ifdef AF_INET6
	{ AF_INET6,		"IPv6" },
#endif /*AF_INET6*/
	{ 0,			NULL }
};

static inline void
nflog_hdr_print(netdissect_options *ndo, const nflog_hdr_t *hdr, u_int length)
{
	ND_PRINT((ndo, "version %d, resource ID %d", hdr->nflog_version, ntohs(hdr->nflog_rid)));

	if (!ndo->ndo_qflag) {
		ND_PRINT((ndo,", family %s (%d)",
						  tok2str(nflog_values, "Unknown",
								  hdr->nflog_family),
						  hdr->nflog_family));
		} else {
		ND_PRINT((ndo,", %s",
						  tok2str(nflog_values,
								  "Unknown NFLOG (0x%02x)",
								  hdr->nflog_family)));
		}

	ND_PRINT((ndo, ", length %u: ", length));
}

u_int
nflog_if_print(netdissect_options *ndo,
			   const struct pcap_pkthdr *h, const u_char *p)
{
	const nflog_hdr_t *hdr = (const nflog_hdr_t *)p;
	const nflog_tlv_t *tlv;
	uint16_t size;
	uint16_t h_size = sizeof(nflog_hdr_t);
	u_int caplen = h->caplen;
	u_int length = h->len;

	if (caplen < (int) sizeof(nflog_hdr_t) || length < (int) sizeof(nflog_hdr_t)) {
		ND_PRINT((ndo, "[|nflog]"));
		return h_size;
	}

	if (hdr->nflog_version != 0) {
		ND_PRINT((ndo, "version %u (unknown)", hdr->nflog_version));
		return h_size;
	}

	if (ndo->ndo_eflag)
		nflog_hdr_print(ndo, hdr, length);

	p += sizeof(nflog_hdr_t);
	length -= sizeof(nflog_hdr_t);
	caplen -= sizeof(nflog_hdr_t);

	while (length > 0) {
		/* We have some data.  Do we have enough for the TLV header? */
		if (caplen < sizeof(nflog_tlv_t) || length < sizeof(nflog_tlv_t)) {
			/* No. */
			ND_PRINT((ndo, "[|nflog]"));
			return h_size;
		}

		tlv = (const nflog_tlv_t *) p;
		size = tlv->tlv_length;
		if (size % 4 != 0)
			size += 4 - size % 4;

		/* Is the TLV's length less than the minimum? */
		if (size < sizeof(nflog_tlv_t)) {
			/* Yes. Give up now. */
			ND_PRINT((ndo, "[|nflog]"));
			return h_size;
		}

		/* Do we have enough data for the full TLV? */
		if (caplen < size || length < size) {
			/* No. */
			ND_PRINT((ndo, "[|nflog]"));
			return h_size;
		}

		if (tlv->tlv_type == NFULA_PAYLOAD) {
			/*
			 * This TLV's data is the packet payload.
			 * Skip past the TLV header, and break out
			 * of the loop so we print the packet data.
			 */
			p += sizeof(nflog_tlv_t);
			h_size += sizeof(nflog_tlv_t);
			length -= sizeof(nflog_tlv_t);
			caplen -= sizeof(nflog_tlv_t);
			break;
		}

		p += size;
		h_size += size;
		length -= size;
		caplen -= size;
	}

	switch (hdr->nflog_family) {

	case AF_INET:
		ip_print(ndo, p, length);
		break;

#ifdef AF_INET6
	case AF_INET6:
		ip6_print(ndo, p, length);
		break;
#endif /* AF_INET6 */

	default:
		if (!ndo->ndo_eflag)
			nflog_hdr_print(ndo, hdr,
				length + sizeof(nflog_hdr_t));

		if (!ndo->ndo_suppress_default_print)
			ND_DEFAULTPRINT(p, caplen);
		break;
	}

	return h_size;
}

#endif /* defined(DLT_NFLOG) && defined(HAVE_PCAP_NFLOG_H) */
