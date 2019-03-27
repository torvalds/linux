/*
 * Copyright (C) 2001 WIDE Project.
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

/* \summary: BIND9 Lightweight Resolver protocol printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "nameser.h"

#include <stdio.h>
#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

/* BIND9 lib/lwres/include/lwres */
typedef uint32_t lwres_uint32_t;
typedef uint16_t lwres_uint16_t;
typedef uint8_t lwres_uint8_t;

struct lwres_lwpacket {
	lwres_uint32_t		length;
	lwres_uint16_t		version;
	lwres_uint16_t		pktflags;
	lwres_uint32_t		serial;
	lwres_uint32_t		opcode;
	lwres_uint32_t		result;
	lwres_uint32_t		recvlength;
	lwres_uint16_t		authtype;
	lwres_uint16_t		authlength;
};

#define LWRES_LWPACKETFLAG_RESPONSE	0x0001U	/* if set, pkt is a response */

#define LWRES_LWPACKETVERSION_0		0

#define LWRES_FLAG_TRUSTNOTREQUIRED	0x00000001U
#define LWRES_FLAG_SECUREDATA		0x00000002U

/*
 * no-op
 */
#define LWRES_OPCODE_NOOP		0x00000000U

typedef struct {
	/* public */
	lwres_uint16_t			datalength;
	/* data follows */
} lwres_nooprequest_t;

typedef struct {
	/* public */
	lwres_uint16_t			datalength;
	/* data follows */
} lwres_noopresponse_t;

/*
 * get addresses by name
 */
#define LWRES_OPCODE_GETADDRSBYNAME	0x00010001U

typedef struct lwres_addr lwres_addr_t;

struct lwres_addr {
	lwres_uint32_t			family;
	lwres_uint16_t			length;
	/* address folows */
};

typedef struct {
	/* public */
	lwres_uint32_t			flags;
	lwres_uint32_t			addrtypes;
	lwres_uint16_t			namelen;
	/* name follows */
} lwres_gabnrequest_t;

typedef struct {
	/* public */
	lwres_uint32_t			flags;
	lwres_uint16_t			naliases;
	lwres_uint16_t			naddrs;
	lwres_uint16_t			realnamelen;
	/* aliases follows */
	/* addrs follows */
	/* realname follows */
} lwres_gabnresponse_t;

/*
 * get name by address
 */
#define LWRES_OPCODE_GETNAMEBYADDR	0x00010002U
typedef struct {
	/* public */
	lwres_uint32_t			flags;
	lwres_addr_t			addr;
	/* addr body follows */
} lwres_gnbarequest_t;

typedef struct {
	/* public */
	lwres_uint32_t			flags;
	lwres_uint16_t			naliases;
	lwres_uint16_t			realnamelen;
	/* aliases follows */
	/* realname follows */
} lwres_gnbaresponse_t;

/*
 * get rdata by name
 */
#define LWRES_OPCODE_GETRDATABYNAME	0x00010003U

typedef struct {
	/* public */
	lwres_uint32_t			flags;
	lwres_uint16_t			rdclass;
	lwres_uint16_t			rdtype;
	lwres_uint16_t			namelen;
	/* name follows */
} lwres_grbnrequest_t;

typedef struct {
	/* public */
	lwres_uint32_t			flags;
	lwres_uint16_t			rdclass;
	lwres_uint16_t			rdtype;
	lwres_uint32_t			ttl;
	lwres_uint16_t			nrdatas;
	lwres_uint16_t			nsigs;
	/* realname here (len + name) */
	/* rdata here (len + name) */
	/* signatures here (len + name) */
} lwres_grbnresponse_t;

#define LWRDATA_VALIDATED	0x00000001

#define LWRES_ADDRTYPE_V4		0x00000001U	/* ipv4 */
#define LWRES_ADDRTYPE_V6		0x00000002U	/* ipv6 */

#define LWRES_MAX_ALIASES		16		/* max # of aliases */
#define LWRES_MAX_ADDRS			64		/* max # of addrs */

static const struct tok opcode[] = {
	{ LWRES_OPCODE_NOOP,		"noop", },
	{ LWRES_OPCODE_GETADDRSBYNAME,	"getaddrsbyname", },
	{ LWRES_OPCODE_GETNAMEBYADDR,	"getnamebyaddr", },
	{ LWRES_OPCODE_GETRDATABYNAME,	"getrdatabyname", },
	{ 0, 				NULL, },
};

/* print-domain.c */
extern const struct tok ns_type2str[];
extern const struct tok ns_class2str[];

static int
lwres_printname(netdissect_options *ndo,
                size_t l, const char *p0)
{
	const char *p;
	size_t i;

	p = p0;
	/* + 1 for terminating \0 */
	if (p + l + 1 > (const char *)ndo->ndo_snapend)
		goto trunc;

	ND_PRINT((ndo, " "));
	for (i = 0; i < l; i++)
		safeputchar(ndo, *p++);
	p++;	/* skip terminating \0 */

	return p - p0;

  trunc:
	return -1;
}

static int
lwres_printnamelen(netdissect_options *ndo,
                   const char *p)
{
	uint16_t l;
	int advance;

	if (p + 2 > (const char *)ndo->ndo_snapend)
		goto trunc;
	l = EXTRACT_16BITS(p);
	advance = lwres_printname(ndo, l, p + 2);
	if (advance < 0)
		goto trunc;
	return 2 + advance;

  trunc:
	return -1;
}

static int
lwres_printbinlen(netdissect_options *ndo,
                  const char *p0)
{
	const char *p;
	uint16_t l;
	int i;

	p = p0;
	if (p + 2 > (const char *)ndo->ndo_snapend)
		goto trunc;
	l = EXTRACT_16BITS(p);
	if (p + 2 + l > (const char *)ndo->ndo_snapend)
		goto trunc;
	p += 2;
	for (i = 0; i < l; i++)
		ND_PRINT((ndo, "%02x", *p++));
	return p - p0;

  trunc:
	return -1;
}

static int
lwres_printaddr(netdissect_options *ndo,
                const lwres_addr_t *ap)
{
	uint16_t l;
	const char *p;
	int i;

	ND_TCHECK(ap->length);
	l = EXTRACT_16BITS(&ap->length);
	/* XXX ap points to packed struct */
	p = (const char *)&ap->length + sizeof(ap->length);
	ND_TCHECK2(*p, l);

	switch (EXTRACT_32BITS(&ap->family)) {
	case 1:	/* IPv4 */
		if (l < 4)
			return -1;
		ND_PRINT((ndo, " %s", ipaddr_string(ndo, p)));
		p += sizeof(struct in_addr);
		break;
	case 2:	/* IPv6 */
		if (l < 16)
			return -1;
		ND_PRINT((ndo, " %s", ip6addr_string(ndo, p)));
		p += sizeof(struct in6_addr);
		break;
	default:
		ND_PRINT((ndo, " %u/", EXTRACT_32BITS(&ap->family)));
		for (i = 0; i < l; i++)
			ND_PRINT((ndo, "%02x", *p++));
	}

	return p - (const char *)ap;

  trunc:
	return -1;
}

void
lwres_print(netdissect_options *ndo,
            register const u_char *bp, u_int length)
{
	const struct lwres_lwpacket *np;
	uint32_t v;
	const char *s;
	int response;
	int advance;
	int unsupported = 0;

	np = (const struct lwres_lwpacket *)bp;
	ND_TCHECK(np->authlength);

	ND_PRINT((ndo, " lwres"));
	v = EXTRACT_16BITS(&np->version);
	if (ndo->ndo_vflag || v != LWRES_LWPACKETVERSION_0)
		ND_PRINT((ndo, " v%u", v));
	if (v != LWRES_LWPACKETVERSION_0) {
		s = (const char *)np + EXTRACT_32BITS(&np->length);
		goto tail;
	}

	response = EXTRACT_16BITS(&np->pktflags) & LWRES_LWPACKETFLAG_RESPONSE;

	/* opcode and pktflags */
	v = EXTRACT_32BITS(&np->opcode);
	s = tok2str(opcode, "#0x%x", v);
	ND_PRINT((ndo, " %s%s", s, response ? "" : "?"));

	/* pktflags */
	v = EXTRACT_16BITS(&np->pktflags);
	if (v & ~LWRES_LWPACKETFLAG_RESPONSE)
		ND_PRINT((ndo, "[0x%x]", v));

	if (ndo->ndo_vflag > 1) {
		ND_PRINT((ndo, " ("));	/*)*/
		ND_PRINT((ndo, "serial:0x%x", EXTRACT_32BITS(&np->serial)));
		ND_PRINT((ndo, " result:0x%x", EXTRACT_32BITS(&np->result)));
		ND_PRINT((ndo, " recvlen:%u", EXTRACT_32BITS(&np->recvlength)));
		/* BIND910: not used */
		if (ndo->ndo_vflag > 2) {
			ND_PRINT((ndo, " authtype:0x%x", EXTRACT_16BITS(&np->authtype)));
			ND_PRINT((ndo, " authlen:%u", EXTRACT_16BITS(&np->authlength)));
		}
		/*(*/
		ND_PRINT((ndo, ")"));
	}

	/* per-opcode content */
	if (!response) {
		/*
		 * queries
		 */
		const lwres_gabnrequest_t *gabn;
		const lwres_gnbarequest_t *gnba;
		const lwres_grbnrequest_t *grbn;
		uint32_t l;

		gabn = NULL;
		gnba = NULL;
		grbn = NULL;

		switch (EXTRACT_32BITS(&np->opcode)) {
		case LWRES_OPCODE_NOOP:
			break;
		case LWRES_OPCODE_GETADDRSBYNAME:
			gabn = (const lwres_gabnrequest_t *)(np + 1);
			ND_TCHECK(gabn->namelen);
			/* XXX gabn points to packed struct */
			s = (const char *)&gabn->namelen +
			    sizeof(gabn->namelen);
			l = EXTRACT_16BITS(&gabn->namelen);

			/* BIND910: not used */
			if (ndo->ndo_vflag > 2) {
				ND_PRINT((ndo, " flags:0x%x",
				    EXTRACT_32BITS(&gabn->flags)));
			}

			v = EXTRACT_32BITS(&gabn->addrtypes);
			switch (v & (LWRES_ADDRTYPE_V4 | LWRES_ADDRTYPE_V6)) {
			case LWRES_ADDRTYPE_V4:
				ND_PRINT((ndo, " IPv4"));
				break;
			case LWRES_ADDRTYPE_V6:
				ND_PRINT((ndo, " IPv6"));
				break;
			case LWRES_ADDRTYPE_V4 | LWRES_ADDRTYPE_V6:
				ND_PRINT((ndo, " IPv4/6"));
				break;
			}
			if (v & ~(LWRES_ADDRTYPE_V4 | LWRES_ADDRTYPE_V6))
				ND_PRINT((ndo, "[0x%x]", v));

			advance = lwres_printname(ndo, l, s);
			if (advance < 0)
				goto trunc;
			s += advance;
			break;
		case LWRES_OPCODE_GETNAMEBYADDR:
			gnba = (const lwres_gnbarequest_t *)(np + 1);
			ND_TCHECK(gnba->addr);

			/* BIND910: not used */
			if (ndo->ndo_vflag > 2) {
				ND_PRINT((ndo, " flags:0x%x",
				    EXTRACT_32BITS(&gnba->flags)));
			}

			s = (const char *)&gnba->addr;

			advance = lwres_printaddr(ndo, &gnba->addr);
			if (advance < 0)
				goto trunc;
			s += advance;
			break;
		case LWRES_OPCODE_GETRDATABYNAME:
			/* XXX no trace, not tested */
			grbn = (const lwres_grbnrequest_t *)(np + 1);
			ND_TCHECK(grbn->namelen);

			/* BIND910: not used */
			if (ndo->ndo_vflag > 2) {
				ND_PRINT((ndo, " flags:0x%x",
				    EXTRACT_32BITS(&grbn->flags)));
			}

			ND_PRINT((ndo, " %s", tok2str(ns_type2str, "Type%d",
			    EXTRACT_16BITS(&grbn->rdtype))));
			if (EXTRACT_16BITS(&grbn->rdclass) != C_IN) {
				ND_PRINT((ndo, " %s", tok2str(ns_class2str, "Class%d",
				    EXTRACT_16BITS(&grbn->rdclass))));
			}

			/* XXX grbn points to packed struct */
			s = (const char *)&grbn->namelen +
			    sizeof(grbn->namelen);
			l = EXTRACT_16BITS(&grbn->namelen);

			advance = lwres_printname(ndo, l, s);
			if (advance < 0)
				goto trunc;
			s += advance;
			break;
		default:
			unsupported++;
			break;
		}
	} else {
		/*
		 * responses
		 */
		const lwres_gabnresponse_t *gabn;
		const lwres_gnbaresponse_t *gnba;
		const lwres_grbnresponse_t *grbn;
		uint32_t l, na;
		uint32_t i;

		gabn = NULL;
		gnba = NULL;
		grbn = NULL;

		switch (EXTRACT_32BITS(&np->opcode)) {
		case LWRES_OPCODE_NOOP:
			break;
		case LWRES_OPCODE_GETADDRSBYNAME:
			gabn = (const lwres_gabnresponse_t *)(np + 1);
			ND_TCHECK(gabn->realnamelen);
			/* XXX gabn points to packed struct */
			s = (const char *)&gabn->realnamelen +
			    sizeof(gabn->realnamelen);
			l = EXTRACT_16BITS(&gabn->realnamelen);

			/* BIND910: not used */
			if (ndo->ndo_vflag > 2) {
				ND_PRINT((ndo, " flags:0x%x",
				    EXTRACT_32BITS(&gabn->flags)));
			}

			ND_PRINT((ndo, " %u/%u", EXTRACT_16BITS(&gabn->naliases),
			    EXTRACT_16BITS(&gabn->naddrs)));

			advance = lwres_printname(ndo, l, s);
			if (advance < 0)
				goto trunc;
			s += advance;

			/* aliases */
			na = EXTRACT_16BITS(&gabn->naliases);
			for (i = 0; i < na; i++) {
				advance = lwres_printnamelen(ndo, s);
				if (advance < 0)
					goto trunc;
				s += advance;
			}

			/* addrs */
			na = EXTRACT_16BITS(&gabn->naddrs);
			for (i = 0; i < na; i++) {
				advance = lwres_printaddr(ndo, (const lwres_addr_t *)s);
				if (advance < 0)
					goto trunc;
				s += advance;
			}
			break;
		case LWRES_OPCODE_GETNAMEBYADDR:
			gnba = (const lwres_gnbaresponse_t *)(np + 1);
			ND_TCHECK(gnba->realnamelen);
			/* XXX gnba points to packed struct */
			s = (const char *)&gnba->realnamelen +
			    sizeof(gnba->realnamelen);
			l = EXTRACT_16BITS(&gnba->realnamelen);

			/* BIND910: not used */
			if (ndo->ndo_vflag > 2) {
				ND_PRINT((ndo, " flags:0x%x",
				    EXTRACT_32BITS(&gnba->flags)));
			}

			ND_PRINT((ndo, " %u", EXTRACT_16BITS(&gnba->naliases)));

			advance = lwres_printname(ndo, l, s);
			if (advance < 0)
				goto trunc;
			s += advance;

			/* aliases */
			na = EXTRACT_16BITS(&gnba->naliases);
			for (i = 0; i < na; i++) {
				advance = lwres_printnamelen(ndo, s);
				if (advance < 0)
					goto trunc;
				s += advance;
			}
			break;
		case LWRES_OPCODE_GETRDATABYNAME:
			/* XXX no trace, not tested */
			grbn = (const lwres_grbnresponse_t *)(np + 1);
			ND_TCHECK(grbn->nsigs);

			/* BIND910: not used */
			if (ndo->ndo_vflag > 2) {
				ND_PRINT((ndo, " flags:0x%x",
				    EXTRACT_32BITS(&grbn->flags)));
			}

			ND_PRINT((ndo, " %s", tok2str(ns_type2str, "Type%d",
			    EXTRACT_16BITS(&grbn->rdtype))));
			if (EXTRACT_16BITS(&grbn->rdclass) != C_IN) {
				ND_PRINT((ndo, " %s", tok2str(ns_class2str, "Class%d",
				    EXTRACT_16BITS(&grbn->rdclass))));
			}
			ND_PRINT((ndo, " TTL "));
			unsigned_relts_print(ndo, EXTRACT_32BITS(&grbn->ttl));
			ND_PRINT((ndo, " %u/%u", EXTRACT_16BITS(&grbn->nrdatas),
			    EXTRACT_16BITS(&grbn->nsigs)));

			/* XXX grbn points to packed struct */
			s = (const char *)&grbn->nsigs+ sizeof(grbn->nsigs);

			advance = lwres_printnamelen(ndo, s);
			if (advance < 0)
				goto trunc;
			s += advance;

			/* rdatas */
			na = EXTRACT_16BITS(&grbn->nrdatas);
			for (i = 0; i < na; i++) {
				/* XXX should decode resource data */
				advance = lwres_printbinlen(ndo, s);
				if (advance < 0)
					goto trunc;
				s += advance;
			}

			/* sigs */
			na = EXTRACT_16BITS(&grbn->nsigs);
			for (i = 0; i < na; i++) {
				/* XXX how should we print it? */
				advance = lwres_printbinlen(ndo, s);
				if (advance < 0)
					goto trunc;
				s += advance;
			}
			break;
		default:
			unsupported++;
			break;
		}
	}

  tail:
	/* length mismatch */
	if (EXTRACT_32BITS(&np->length) != length) {
		ND_PRINT((ndo, " [len: %u != %u]", EXTRACT_32BITS(&np->length),
		    length));
	}
	if (!unsupported && s < (const char *)np + EXTRACT_32BITS(&np->length))
		ND_PRINT((ndo, "[extra]"));
	return;

  trunc:
	ND_PRINT((ndo, "[|lwres]"));
}
