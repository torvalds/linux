/*
 * Marko Kiiskila carnil@cs.tut.fi
 *
 * Tampere University of Technology - Telecommunications Laboratory
 *
 * Permission to use, copy, modify and distribute this
 * software and its documentation is hereby granted,
 * provided that both the copyright notice and this
 * permission notice appear in all copies of the software,
 * derivative works or modified versions, and any portions
 * thereof, that both notices appear in supporting
 * documentation, and that the use of this software is
 * acknowledged in any publications resulting from using
 * the software.
 *
 * TUT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS
 * SOFTWARE.
 *
 */

/* \summary: Classical-IP over ATM printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "addrtoname.h"

static const unsigned char rfcllc[] = {
	0xaa,	/* DSAP: non-ISO */
	0xaa,	/* SSAP: non-ISO */
	0x03,	/* Ctrl: Unnumbered Information Command PDU */
	0x00,	/* OUI: EtherType */
	0x00,
	0x00 };

static inline void
cip_print(netdissect_options *ndo, u_int length)
{
	/*
	 * There is no MAC-layer header, so just print the length.
	 */
	ND_PRINT((ndo, "%u: ", length));
}

/*
 * This is the top level routine of the printer.  'p' points
 * to the LLC/SNAP or raw header of the packet, 'h->ts' is the timestamp,
 * 'h->len' is the length of the packet off the wire, and 'h->caplen'
 * is the number of bytes actually captured.
 */
u_int
cip_if_print(netdissect_options *ndo, const struct pcap_pkthdr *h, const u_char *p)
{
	u_int caplen = h->caplen;
	u_int length = h->len;
	size_t cmplen;
	int llc_hdrlen;

	cmplen = sizeof(rfcllc);
	if (cmplen > caplen)
		cmplen = caplen;
	if (cmplen > length)
		cmplen = length;

	if (ndo->ndo_eflag)
		cip_print(ndo, length);

	if (cmplen == 0) {
		ND_PRINT((ndo, "[|cip]"));
		return 0;
	}
	if (memcmp(rfcllc, p, cmplen) == 0) {
		/*
		 * LLC header is present.  Try to print it & higher layers.
		 */
		llc_hdrlen = llc_print(ndo, p, length, caplen, NULL, NULL);
		if (llc_hdrlen < 0) {
			/* packet type not known, print raw packet */
			if (!ndo->ndo_suppress_default_print)
				ND_DEFAULTPRINT(p, caplen);
			llc_hdrlen = -llc_hdrlen;
		}
	} else {
		/*
		 * LLC header is absent; treat it as just IP.
		 */
		llc_hdrlen = 0;
		ip_print(ndo, p, length);
	}

	return (llc_hdrlen);
}


/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
