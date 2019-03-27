/* \summary: Solaris DLT_IPNET printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"

typedef struct ipnet_hdr {
	uint8_t		iph_version;
	uint8_t		iph_family;
	uint16_t	iph_htype;
	uint32_t	iph_pktlen;
	uint32_t	iph_ifindex;
	uint32_t	iph_grifindex;
	uint32_t	iph_zsrc;
	uint32_t	iph_zdst;
} ipnet_hdr_t;

#define	IPH_AF_INET	2		/* Matches Solaris's AF_INET */
#define	IPH_AF_INET6	26		/* Matches Solaris's AF_INET6 */

#ifdef DLT_IPNET

static const struct tok ipnet_values[] = {
	{ IPH_AF_INET,		"IPv4" },
	{ IPH_AF_INET6,		"IPv6" },
	{ 0,			NULL }
};

static inline void
ipnet_hdr_print(netdissect_options *ndo, const u_char *bp, u_int length)
{
	const ipnet_hdr_t *hdr;
	hdr = (const ipnet_hdr_t *)bp;

	ND_PRINT((ndo, "%d > %d", hdr->iph_zsrc, hdr->iph_zdst));

	if (!ndo->ndo_qflag) {
		ND_PRINT((ndo,", family %s (%d)",
                          tok2str(ipnet_values, "Unknown",
                                  hdr->iph_family),
                          hdr->iph_family));
        } else {
		ND_PRINT((ndo,", %s",
                          tok2str(ipnet_values,
                                  "Unknown Ethertype (0x%04x)",
                                  hdr->iph_family)));
        }

	ND_PRINT((ndo, ", length %u: ", length));
}

static void
ipnet_print(netdissect_options *ndo, const u_char *p, u_int length, u_int caplen)
{
	const ipnet_hdr_t *hdr;

	if (caplen < sizeof(ipnet_hdr_t)) {
		ND_PRINT((ndo, "[|ipnet]"));
		return;
	}

	if (ndo->ndo_eflag)
		ipnet_hdr_print(ndo, p, length);

	length -= sizeof(ipnet_hdr_t);
	caplen -= sizeof(ipnet_hdr_t);
	hdr = (const ipnet_hdr_t *)p;
	p += sizeof(ipnet_hdr_t);

	switch (hdr->iph_family) {

	case IPH_AF_INET:
	        ip_print(ndo, p, length);
		break;

	case IPH_AF_INET6:
		ip6_print(ndo, p, length);
		break;

	default:
		if (!ndo->ndo_eflag)
			ipnet_hdr_print(ndo, (const u_char *)hdr,
					length + sizeof(ipnet_hdr_t));

		if (!ndo->ndo_suppress_default_print)
			ND_DEFAULTPRINT(p, caplen);
		break;
	}
}

/*
 * This is the top level routine of the printer.  'p' points
 * to the ether header of the packet, 'h->ts' is the timestamp,
 * 'h->len' is the length of the packet off the wire, and 'h->caplen'
 * is the number of bytes actually captured.
 */
u_int
ipnet_if_print(netdissect_options *ndo,
               const struct pcap_pkthdr *h, const u_char *p)
{
	ipnet_print(ndo, p, h->len, h->caplen);

	return (sizeof(ipnet_hdr_t));
}

/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */

#endif /* DLT_IPNET */
