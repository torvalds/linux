/*
 * Copyright (c) 1991, 1992, 1993, 1994, 1995, 1996, 1997
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

/* \summary: Fiber Distributed Data Interface (FDDI) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "ether.h"

/*
 * Based on Ultrix if_fddi.h
 */

struct fddi_header {
	u_char  fddi_fc;		/* frame control */
	u_char  fddi_dhost[6];
	u_char  fddi_shost[6];
};

/*
 * Length of an FDDI header; note that some compilers may pad
 * "struct fddi_header" to a multiple of 4 bytes, for example, so
 * "sizeof (struct fddi_header)" may not give the right
 * answer.
 */
#define FDDI_HDRLEN 13

/* Useful values for fddi_fc (frame control) field */

/*
 * FDDI Frame Control bits
 */
#define	FDDIFC_C		0x80		/* Class bit */
#define	FDDIFC_L		0x40		/* Address length bit */
#define	FDDIFC_F		0x30		/* Frame format bits */
#define	FDDIFC_Z		0x0f		/* Control bits */

/*
 * FDDI Frame Control values. (48-bit addressing only).
 */
#define	FDDIFC_VOID		0x40		/* Void frame */
#define	FDDIFC_NRT		0x80		/* Nonrestricted token */
#define	FDDIFC_RT		0xc0		/* Restricted token */
#define	FDDIFC_SMT_INFO		0x41		/* SMT Info */
#define	FDDIFC_SMT_NSA		0x4F		/* SMT Next station adrs */
#define	FDDIFC_MAC_BEACON	0xc2		/* MAC Beacon frame */
#define	FDDIFC_MAC_CLAIM	0xc3		/* MAC Claim frame */
#define	FDDIFC_LLC_ASYNC	0x50		/* Async. LLC frame */
#define	FDDIFC_LLC_SYNC		0xd0		/* Sync. LLC frame */
#define	FDDIFC_IMP_ASYNC	0x60		/* Implementor Async. */
#define	FDDIFC_IMP_SYNC		0xe0		/* Implementor Synch. */
#define FDDIFC_SMT		0x40		/* SMT frame */
#define FDDIFC_MAC		0xc0		/* MAC frame */

#define	FDDIFC_CLFF		0xF0		/* Class/Length/Format bits */
#define	FDDIFC_ZZZZ		0x0F		/* Control bits */

/*
 * Some FDDI interfaces use bit-swapped addresses.
 */
#if defined(ultrix) || defined(__alpha) || defined(__bsdi) || defined(__NetBSD__) || defined(__linux__)
static int fddi_bitswap = 0;
#else
static int fddi_bitswap = 1;
#endif

/*
 * FDDI support for tcpdump, by Jeffrey Mogul [DECWRL], June 1992
 *
 * Based in part on code by Van Jacobson, which bears this note:
 *
 * NOTE:  This is a very preliminary hack for FDDI support.
 * There are all sorts of wired in constants & nothing (yet)
 * to print SMT packets as anything other than hex dumps.
 * Most of the necessary changes are waiting on my redoing
 * the "header" that a kernel fddi driver supplies to bpf:  I
 * want it to look like one byte of 'direction' (0 or 1
 * depending on whether the packet was inbound or outbound),
 * two bytes of system/driver dependent data (anything an
 * implementor thinks would be useful to filter on and/or
 * save per-packet, then the real 21-byte FDDI header.
 * Steve McCanne & I have also talked about adding the
 * 'direction' byte to all bpf headers (e.g., in the two
 * bytes of padding on an ethernet header).  It's not clear
 * we could do this in a backwards compatible way & we hate
 * the idea of an incompatible bpf change.  Discussions are
 * proceeding.
 *
 * Also, to really support FDDI (and better support 802.2
 * over ethernet) we really need to re-think the rather simple
 * minded assumptions about fixed length & fixed format link
 * level headers made in gencode.c.  One day...
 *
 *  - vj
 */

static const u_char fddi_bit_swap[] = {
	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
	0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
	0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
	0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
	0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
	0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
	0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
	0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
	0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
	0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
	0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
	0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
	0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
	0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
	0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
	0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
	0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
	0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
	0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
	0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
	0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
	0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
	0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
	0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
	0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
	0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
	0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
	0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
	0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
	0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
	0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
	0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff,
};

/*
 * Print FDDI frame-control bits
 */
static inline void
print_fddi_fc(netdissect_options *ndo, u_char fc)
{
	switch (fc) {

	case FDDIFC_VOID:                         /* Void frame */
		ND_PRINT((ndo, "void "));
		break;

	case FDDIFC_NRT:                          /* Nonrestricted token */
		ND_PRINT((ndo, "nrt "));
		break;

	case FDDIFC_RT:                           /* Restricted token */
		ND_PRINT((ndo, "rt "));
		break;

	case FDDIFC_SMT_INFO:                     /* SMT Info */
		ND_PRINT((ndo, "info "));
		break;

	case FDDIFC_SMT_NSA:                      /* SMT Next station adrs */
		ND_PRINT((ndo, "nsa "));
		break;

	case FDDIFC_MAC_BEACON:                   /* MAC Beacon frame */
		ND_PRINT((ndo, "beacon "));
		break;

	case FDDIFC_MAC_CLAIM:                    /* MAC Claim frame */
		ND_PRINT((ndo, "claim "));
		break;

	default:
		switch (fc & FDDIFC_CLFF) {

		case FDDIFC_MAC:
			ND_PRINT((ndo, "mac%1x ", fc & FDDIFC_ZZZZ));
			break;

		case FDDIFC_SMT:
			ND_PRINT((ndo, "smt%1x ", fc & FDDIFC_ZZZZ));
			break;

		case FDDIFC_LLC_ASYNC:
			ND_PRINT((ndo, "async%1x ", fc & FDDIFC_ZZZZ));
			break;

		case FDDIFC_LLC_SYNC:
			ND_PRINT((ndo, "sync%1x ", fc & FDDIFC_ZZZZ));
			break;

		case FDDIFC_IMP_ASYNC:
			ND_PRINT((ndo, "imp_async%1x ", fc & FDDIFC_ZZZZ));
			break;

		case FDDIFC_IMP_SYNC:
			ND_PRINT((ndo, "imp_sync%1x ", fc & FDDIFC_ZZZZ));
			break;

		default:
			ND_PRINT((ndo, "%02x ", fc));
			break;
		}
	}
}

/* Extract src, dst addresses */
static inline void
extract_fddi_addrs(const struct fddi_header *fddip, char *fsrc, char *fdst)
{
	register int i;

	if (fddi_bitswap) {
		/*
		 * bit-swap the fddi addresses (isn't the IEEE standards
		 * process wonderful!) then convert them to names.
		 */
		for (i = 0; i < 6; ++i)
			fdst[i] = fddi_bit_swap[fddip->fddi_dhost[i]];
		for (i = 0; i < 6; ++i)
			fsrc[i] = fddi_bit_swap[fddip->fddi_shost[i]];
	}
	else {
		memcpy(fdst, (const char *)fddip->fddi_dhost, 6);
		memcpy(fsrc, (const char *)fddip->fddi_shost, 6);
	}
}

/*
 * Print the FDDI MAC header
 */
static inline void
fddi_hdr_print(netdissect_options *ndo,
               register const struct fddi_header *fddip, register u_int length,
               register const u_char *fsrc, register const u_char *fdst)
{
	const char *srcname, *dstname;

	srcname = etheraddr_string(ndo, fsrc);
	dstname = etheraddr_string(ndo, fdst);

	if (!ndo->ndo_qflag)
		print_fddi_fc(ndo, fddip->fddi_fc);
	ND_PRINT((ndo, "%s > %s, length %u: ",
	       srcname, dstname,
	       length));
}

static inline void
fddi_smt_print(netdissect_options *ndo, const u_char *p _U_, u_int length _U_)
{
	ND_PRINT((ndo, "<SMT printer not yet implemented>"));
}

u_int
fddi_print(netdissect_options *ndo, const u_char *p, u_int length, u_int caplen)
{
	const struct fddi_header *fddip = (const struct fddi_header *)p;
	struct ether_header ehdr;
	struct lladdr_info src, dst;
	int llc_hdrlen;

	if (caplen < FDDI_HDRLEN) {
		ND_PRINT((ndo, "[|fddi]"));
		return (caplen);
	}

	/*
	 * Get the FDDI addresses into a canonical form
	 */
	extract_fddi_addrs(fddip, (char *)ESRC(&ehdr), (char *)EDST(&ehdr));

	if (ndo->ndo_eflag)
		fddi_hdr_print(ndo, fddip, length, ESRC(&ehdr), EDST(&ehdr));

	src.addr = ESRC(&ehdr);
	src.addr_string = etheraddr_string;
	dst.addr = EDST(&ehdr);
	dst.addr_string = etheraddr_string;

	/* Skip over FDDI MAC header */
	length -= FDDI_HDRLEN;
	p += FDDI_HDRLEN;
	caplen -= FDDI_HDRLEN;

	/* Frame Control field determines interpretation of packet */
	if ((fddip->fddi_fc & FDDIFC_CLFF) == FDDIFC_LLC_ASYNC) {
		/* Try to print the LLC-layer header & higher layers */
		llc_hdrlen = llc_print(ndo, p, length, caplen, &src, &dst);
		if (llc_hdrlen < 0) {
			/*
			 * Some kinds of LLC packet we cannot
			 * handle intelligently
			 */
			if (!ndo->ndo_suppress_default_print)
				ND_DEFAULTPRINT(p, caplen);
			llc_hdrlen = -llc_hdrlen;
		}
	} else if ((fddip->fddi_fc & FDDIFC_CLFF) == FDDIFC_SMT) {
		fddi_smt_print(ndo, p, caplen);
		llc_hdrlen = 0;
	} else {
		/* Some kinds of FDDI packet we cannot handle intelligently */
		if (!ndo->ndo_eflag)
			fddi_hdr_print(ndo, fddip, length + FDDI_HDRLEN, ESRC(&ehdr),
			    EDST(&ehdr));
		if (!ndo->ndo_suppress_default_print)
			ND_DEFAULTPRINT(p, caplen);
		llc_hdrlen = 0;
	}
	return (FDDI_HDRLEN + llc_hdrlen);
}

/*
 * This is the top level routine of the printer.  'p' points
 * to the FDDI header of the packet, 'h->ts' is the timestamp,
 * 'h->len' is the length of the packet off the wire, and 'h->caplen'
 * is the number of bytes actually captured.
 */
u_int
fddi_if_print(netdissect_options *ndo, const struct pcap_pkthdr *h, register const u_char *p)
{
	return (fddi_print(ndo, p, h->len, h->caplen));
}
