/*	$OpenBSD: print-enc.c,v 1.7 2002/02/19 19:39:40 millert Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1993, 1994, 1995, 1996
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

/* \summary: OpenBSD IPsec encapsulation BPF layer printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

/* From $OpenBSD: if_enc.h,v 1.8 2001/06/25 05:14:00 angelos Exp $ */
/*
 * The authors of this code are John Ioannidis (ji@tla.org),
 * Angelos D. Keromytis (kermit@csd.uch.gr) and
 * Niels Provos (provos@physnet.uni-hamburg.de).
 *
 * This code was written by John Ioannidis for BSD/OS in Athens, Greece,
 * in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis.
 *
 * Additional transforms and features in 1997 and 1998 by Angelos D. Keromytis
 * and Niels Provos.
 *
 * Copyright (C) 1995, 1996, 1997, 1998 by John Ioannidis, Angelos D. Keromytis
 * and Niels Provos.
 * Copyright (c) 2001, Angelos D. Keromytis.
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software.
 * You may use this code under the GNU public license if you so wish. Please
 * contribute changes back to the authors under this freer than GPL license
 * so that we may further the use of strong encryption without limitations to
 * all.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#define ENC_HDRLEN	12

/* From $OpenBSD: mbuf.h,v 1.56 2002/01/25 15:50:23 art Exp $	*/
#define M_CONF		0x0400  /* packet was encrypted (ESP-transport) */
#define M_AUTH		0x0800  /* packet was authenticated (AH) */

struct enchdr {
	uint32_t af;
	uint32_t spi;
	uint32_t flags;
};

#define ENC_PRINT_TYPE(wh, xf, nam) \
	if ((wh) & (xf)) { \
		ND_PRINT((ndo, "%s%s", nam, (wh) == (xf) ? "): " : ",")); \
		(wh) &= ~(xf); \
	}

u_int
enc_if_print(netdissect_options *ndo,
             const struct pcap_pkthdr *h, register const u_char *p)
{
	register u_int length = h->len;
	register u_int caplen = h->caplen;
	int flags;
	const struct enchdr *hdr;

	if (caplen < ENC_HDRLEN) {
		ND_PRINT((ndo, "[|enc]"));
		goto out;
	}

	hdr = (const struct enchdr *)p;
	flags = hdr->flags;
	if (flags == 0)
		ND_PRINT((ndo, "(unprotected): "));
	else
		ND_PRINT((ndo, "("));
	ENC_PRINT_TYPE(flags, M_AUTH, "authentic");
	ENC_PRINT_TYPE(flags, M_CONF, "confidential");
	/* ENC_PRINT_TYPE(flags, M_TUNNEL, "tunnel"); */
	ND_PRINT((ndo, "SPI 0x%08x: ", EXTRACT_32BITS(&hdr->spi)));

	length -= ENC_HDRLEN;
	caplen -= ENC_HDRLEN;
	p += ENC_HDRLEN;

	switch (hdr->af) {
	case AF_INET:
		ip_print(ndo, p, length);
		break;
#ifdef AF_INET6
	case AF_INET6:
		ip6_print(ndo, p, length);
		break;
#endif
	}

out:
	return (ENC_HDRLEN);
}


/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
