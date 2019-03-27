/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* \summary: Marvell Extended Distributed Switch Architecture (MEDSA) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "ether.h"
#include "ethertype.h"
#include "addrtoname.h"
#include "extract.h"

static const char tstr[] = "[|MEDSA]";

/*
 * Marvell Extended Distributed Switch Archiecture.
 *
 * A Marvell propriatary header used for passing packets to/from
 * specific ports of a switch. There is no open specification of this
 * header, but is documented in the Marvell Switch data sheets. For
 * background, see:
 *
 * https://lwn.net/Articles/302333/
 */
struct	medsa_pkthdr {
	u_char bytes[6];
	u_short ether_type;
};

/* Bytes 0 and 1 are reserved and should contain 0 */
#define TAG(medsa)	(medsa->bytes[2] >> 6)
#define TAG_TO_CPU	0
#define TAG_FROM_CPU	1
#define TAG_FORWARD	3
#define SRC_TAG(medsa)	((medsa->bytes[2] >> 5) & 0x01)
#define SRC_DEV(medsa)	(medsa->bytes[2] & 0x1f)
#define SRC_PORT(medsa)	((medsa->bytes[3] >> 3) & 0x01f)
#define TRUNK(medsa)	((medsa->bytes[3] >> 2) & 0x01)
#define CODE(medsa)	((medsa->bytes[3] & 0x06) |	\
			 ((medsa->bytes[4] >> 4) & 0x01))
#define CODE_BDPU	0
#define CODE_IGMP_MLD	2
#define CODE_ARP_MIRROR	4
#define CFI(medsa)	(medsa->bytes[3] & 0x01)
#define PRI(medsa)	(medsa->bytes[4] >> 5)
#define VID(medsa)	(((u_short)(medsa->bytes[4] & 0xf) << 8 |	\
			  medsa->bytes[5]))

static const struct tok tag_values[] = {
	{ TAG_TO_CPU, "To_CPU" },
	{ TAG_FROM_CPU, "From_CPU" },
	{ TAG_FORWARD, "Forward" },
	{ 0, NULL },
};

static const struct tok code_values[] = {
	{ CODE_BDPU, "BDPU" },
	{ CODE_IGMP_MLD, "IGMP/MLD" },
	{ CODE_ARP_MIRROR, "APR_Mirror" },
	{ 0, NULL },
};

static void
medsa_print_full(netdissect_options *ndo,
		 const struct medsa_pkthdr *medsa,
		 u_int caplen)
{
	u_char tag = TAG(medsa);

	ND_PRINT((ndo, "%s",
		  tok2str(tag_values, "Unknown (%u)", tag)));

	switch (tag) {
	case TAG_TO_CPU:
		ND_PRINT((ndo, ", %stagged", SRC_TAG(medsa) ? "" : "un"));
		ND_PRINT((ndo, ", dev.port:vlan %d.%d:%d",
			  SRC_DEV(medsa), SRC_PORT(medsa), VID(medsa)));

		ND_PRINT((ndo, ", %s",
			  tok2str(code_values, "Unknown (%u)", CODE(medsa))));
		if (CFI(medsa))
			ND_PRINT((ndo, ", CFI"));

		ND_PRINT((ndo, ", pri %d: ", PRI(medsa)));
		break;
	case TAG_FROM_CPU:
		ND_PRINT((ndo, ", %stagged", SRC_TAG(medsa) ? "" : "un"));
		ND_PRINT((ndo, ", dev.port:vlan %d.%d:%d",
			  SRC_DEV(medsa), SRC_PORT(medsa), VID(medsa)));

		if (CFI(medsa))
			ND_PRINT((ndo, ", CFI"));

		ND_PRINT((ndo, ", pri %d: ", PRI(medsa)));
		break;
	case TAG_FORWARD:
		ND_PRINT((ndo, ", %stagged", SRC_TAG(medsa) ? "" : "un"));
		if (TRUNK(medsa))
			ND_PRINT((ndo, ", dev.trunk:vlan %d.%d:%d",
				  SRC_DEV(medsa), SRC_PORT(medsa), VID(medsa)));
		else
			ND_PRINT((ndo, ", dev.port:vlan %d.%d:%d",
				  SRC_DEV(medsa), SRC_PORT(medsa), VID(medsa)));

		if (CFI(medsa))
			ND_PRINT((ndo, ", CFI"));

		ND_PRINT((ndo, ", pri %d: ", PRI(medsa)));
		break;
	default:
		ND_DEFAULTPRINT((const u_char *)medsa, caplen);
		return;
	}
}

void
medsa_print(netdissect_options *ndo,
	    const u_char *bp, u_int length, u_int caplen,
	    const struct lladdr_info *src, const struct lladdr_info *dst)
{
	const struct medsa_pkthdr *medsa;
	u_short ether_type;

	medsa = (const struct medsa_pkthdr *)bp;
	ND_TCHECK(*medsa);

	if (!ndo->ndo_eflag)
		ND_PRINT((ndo, "MEDSA %d.%d:%d: ",
			  SRC_DEV(medsa), SRC_PORT(medsa), VID(medsa)));
	else
		medsa_print_full(ndo, medsa, caplen);

	bp += 8;
	length -= 8;
	caplen -= 8;

	ether_type = EXTRACT_16BITS(&medsa->ether_type);
	if (ether_type <= ETHERMTU) {
		/* Try to print the LLC-layer header & higher layers */
		if (llc_print(ndo, bp, length, caplen, src, dst) < 0) {
			/* packet type not known, print raw packet */
			if (!ndo->ndo_suppress_default_print)
				ND_DEFAULTPRINT(bp, caplen);
		}
	} else {
		if (ndo->ndo_eflag)
			ND_PRINT((ndo, "ethertype %s (0x%04x) ",
				  tok2str(ethertype_values, "Unknown",
					  ether_type),
				  ether_type));
		if (ethertype_print(ndo, ether_type, bp, length, caplen, src, dst) == 0) {
			/* ether_type not known, print raw packet */
			if (!ndo->ndo_eflag)
				ND_PRINT((ndo, "ethertype %s (0x%04x) ",
					  tok2str(ethertype_values, "Unknown",
						  ether_type),
					  ether_type));

			if (!ndo->ndo_suppress_default_print)
				ND_DEFAULTPRINT(bp, caplen);
		}
	}
	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
}

/*
 * Local Variables:
 * c-style: bsd
 * End:
 */

