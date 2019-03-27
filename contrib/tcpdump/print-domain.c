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

/* \summary: Domain Name System (DNS) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "nameser.h"

#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "addrtostr.h"
#include "extract.h"

static const char *ns_ops[] = {
	"", " inv_q", " stat", " op3", " notify", " update", " op6", " op7",
	" op8", " updateA", " updateD", " updateDA",
	" updateM", " updateMA", " zoneInit", " zoneRef",
};

static const char *ns_resp[] = {
	"", " FormErr", " ServFail", " NXDomain",
	" NotImp", " Refused", " YXDomain", " YXRRSet",
	" NXRRSet", " NotAuth", " NotZone", " Resp11",
	" Resp12", " Resp13", " Resp14", " NoChange",
};

/* skip over a domain name */
static const u_char *
ns_nskip(netdissect_options *ndo,
         register const u_char *cp)
{
	register u_char i;

	if (!ND_TTEST2(*cp, 1))
		return (NULL);
	i = *cp++;
	while (i) {
		if ((i & INDIR_MASK) == INDIR_MASK)
			return (cp + 1);
		if ((i & INDIR_MASK) == EDNS0_MASK) {
			int bitlen, bytelen;

			if ((i & ~INDIR_MASK) != EDNS0_ELT_BITLABEL)
				return(NULL); /* unknown ELT */
			if (!ND_TTEST2(*cp, 1))
				return (NULL);
			if ((bitlen = *cp++) == 0)
				bitlen = 256;
			bytelen = (bitlen + 7) / 8;
			cp += bytelen;
		} else
			cp += i;
		if (!ND_TTEST2(*cp, 1))
			return (NULL);
		i = *cp++;
	}
	return (cp);
}

/* print a <domain-name> */
static const u_char *
blabel_print(netdissect_options *ndo,
             const u_char *cp)
{
	int bitlen, slen, b;
	const u_char *bitp, *lim;
	char tc;

	if (!ND_TTEST2(*cp, 1))
		return(NULL);
	if ((bitlen = *cp) == 0)
		bitlen = 256;
	slen = (bitlen + 3) / 4;
	lim = cp + 1 + slen;

	/* print the bit string as a hex string */
	ND_PRINT((ndo, "\\[x"));
	for (bitp = cp + 1, b = bitlen; bitp < lim && b > 7; b -= 8, bitp++) {
		ND_TCHECK(*bitp);
		ND_PRINT((ndo, "%02x", *bitp));
	}
	if (b > 4) {
		ND_TCHECK(*bitp);
		tc = *bitp++;
		ND_PRINT((ndo, "%02x", tc & (0xff << (8 - b))));
	} else if (b > 0) {
		ND_TCHECK(*bitp);
		tc = *bitp++;
		ND_PRINT((ndo, "%1x", ((tc >> 4) & 0x0f) & (0x0f << (4 - b))));
	}
	ND_PRINT((ndo, "/%d]", bitlen));
	return lim;
trunc:
	ND_PRINT((ndo, ".../%d]", bitlen));
	return NULL;
}

static int
labellen(netdissect_options *ndo,
         const u_char *cp)
{
	register u_int i;

	if (!ND_TTEST2(*cp, 1))
		return(-1);
	i = *cp;
	if ((i & INDIR_MASK) == EDNS0_MASK) {
		int bitlen, elt;
		if ((elt = (i & ~INDIR_MASK)) != EDNS0_ELT_BITLABEL) {
			ND_PRINT((ndo, "<ELT %d>", elt));
			return(-1);
		}
		if (!ND_TTEST2(*(cp + 1), 1))
			return(-1);
		if ((bitlen = *(cp + 1)) == 0)
			bitlen = 256;
		return(((bitlen + 7) / 8) + 1);
	} else
		return(i);
}

const u_char *
ns_nprint(netdissect_options *ndo,
          register const u_char *cp, register const u_char *bp)
{
	register u_int i, l;
	register const u_char *rp = NULL;
	register int compress = 0;
	int elt;
	u_int offset, max_offset;

	if ((l = labellen(ndo, cp)) == (u_int)-1)
		return(NULL);
	if (!ND_TTEST2(*cp, 1))
		return(NULL);
	max_offset = (u_int)(cp - bp);
	if (((i = *cp++) & INDIR_MASK) != INDIR_MASK) {
		compress = 0;
		rp = cp + l;
	}

	if (i != 0)
		while (i && cp < ndo->ndo_snapend) {
			if ((i & INDIR_MASK) == INDIR_MASK) {
				if (!compress) {
					rp = cp + 1;
					compress = 1;
				}
				if (!ND_TTEST2(*cp, 1))
					return(NULL);
				offset = (((i << 8) | *cp) & 0x3fff);
				/*
				 * This must move backwards in the packet.
				 * No RFC explicitly says that, but BIND's
				 * name decompression code requires it,
				 * as a way of preventing infinite loops
				 * and other bad behavior, and it's probably
				 * what was intended (compress by pointing
				 * to domain name suffixes already seen in
				 * the packet).
				 */
				if (offset >= max_offset) {
					ND_PRINT((ndo, "<BAD PTR>"));
					return(NULL);
				}
				max_offset = offset;
				cp = bp + offset;
				if ((l = labellen(ndo, cp)) == (u_int)-1)
					return(NULL);
				if (!ND_TTEST2(*cp, 1))
					return(NULL);
				i = *cp++;
				continue;
			}
			if ((i & INDIR_MASK) == EDNS0_MASK) {
				elt = (i & ~INDIR_MASK);
				switch(elt) {
				case EDNS0_ELT_BITLABEL:
					if (blabel_print(ndo, cp) == NULL)
						return (NULL);
					break;
				default:
					/* unknown ELT */
					ND_PRINT((ndo, "<ELT %d>", elt));
					return(NULL);
				}
			} else {
				if (fn_printn(ndo, cp, l, ndo->ndo_snapend))
					return(NULL);
			}

			cp += l;
			ND_PRINT((ndo, "."));
			if ((l = labellen(ndo, cp)) == (u_int)-1)
				return(NULL);
			if (!ND_TTEST2(*cp, 1))
				return(NULL);
			i = *cp++;
			if (!compress)
				rp += l + 1;
		}
	else
		ND_PRINT((ndo, "."));
	return (rp);
}

/* print a <character-string> */
static const u_char *
ns_cprint(netdissect_options *ndo,
          register const u_char *cp)
{
	register u_int i;

	if (!ND_TTEST2(*cp, 1))
		return (NULL);
	i = *cp++;
	if (fn_printn(ndo, cp, i, ndo->ndo_snapend))
		return (NULL);
	return (cp + i);
}

/* http://www.iana.org/assignments/dns-parameters */
const struct tok ns_type2str[] = {
	{ T_A,		"A" },			/* RFC 1035 */
	{ T_NS,		"NS" },			/* RFC 1035 */
	{ T_MD,		"MD" },			/* RFC 1035 */
	{ T_MF,		"MF" },			/* RFC 1035 */
	{ T_CNAME,	"CNAME" },		/* RFC 1035 */
	{ T_SOA,	"SOA" },		/* RFC 1035 */
	{ T_MB,		"MB" },			/* RFC 1035 */
	{ T_MG,		"MG" },			/* RFC 1035 */
	{ T_MR,		"MR" },			/* RFC 1035 */
	{ T_NULL,	"NULL" },		/* RFC 1035 */
	{ T_WKS,	"WKS" },		/* RFC 1035 */
	{ T_PTR,	"PTR" },		/* RFC 1035 */
	{ T_HINFO,	"HINFO" },		/* RFC 1035 */
	{ T_MINFO,	"MINFO" },		/* RFC 1035 */
	{ T_MX,		"MX" },			/* RFC 1035 */
	{ T_TXT,	"TXT" },		/* RFC 1035 */
	{ T_RP,		"RP" },			/* RFC 1183 */
	{ T_AFSDB,	"AFSDB" },		/* RFC 1183 */
	{ T_X25,	"X25" },		/* RFC 1183 */
	{ T_ISDN,	"ISDN" },		/* RFC 1183 */
	{ T_RT,		"RT" },			/* RFC 1183 */
	{ T_NSAP,	"NSAP" },		/* RFC 1706 */
	{ T_NSAP_PTR,	"NSAP_PTR" },
	{ T_SIG,	"SIG" },		/* RFC 2535 */
	{ T_KEY,	"KEY" },		/* RFC 2535 */
	{ T_PX,		"PX" },			/* RFC 2163 */
	{ T_GPOS,	"GPOS" },		/* RFC 1712 */
	{ T_AAAA,	"AAAA" },		/* RFC 1886 */
	{ T_LOC,	"LOC" },		/* RFC 1876 */
	{ T_NXT,	"NXT" },		/* RFC 2535 */
	{ T_EID,	"EID" },		/* Nimrod */
	{ T_NIMLOC,	"NIMLOC" },		/* Nimrod */
	{ T_SRV,	"SRV" },		/* RFC 2782 */
	{ T_ATMA,	"ATMA" },		/* ATM Forum */
	{ T_NAPTR,	"NAPTR" },		/* RFC 2168, RFC 2915 */
	{ T_KX,		"KX" },			/* RFC 2230 */
	{ T_CERT,	"CERT" },		/* RFC 2538 */
	{ T_A6,		"A6" },			/* RFC 2874 */
	{ T_DNAME,	"DNAME" },		/* RFC 2672 */
	{ T_SINK, 	"SINK" },
	{ T_OPT,	"OPT" },		/* RFC 2671 */
	{ T_APL, 	"APL" },		/* RFC 3123 */
	{ T_DS,		"DS" },			/* RFC 4034 */
	{ T_SSHFP,	"SSHFP" },		/* RFC 4255 */
	{ T_IPSECKEY,	"IPSECKEY" },		/* RFC 4025 */
	{ T_RRSIG, 	"RRSIG" },		/* RFC 4034 */
	{ T_NSEC,	"NSEC" },		/* RFC 4034 */
	{ T_DNSKEY,	"DNSKEY" },		/* RFC 4034 */
	{ T_SPF,	"SPF" },		/* RFC-schlitt-spf-classic-02.txt */
	{ T_UINFO,	"UINFO" },
	{ T_UID,	"UID" },
	{ T_GID,	"GID" },
	{ T_UNSPEC,	"UNSPEC" },
	{ T_UNSPECA,	"UNSPECA" },
	{ T_TKEY,	"TKEY" },		/* RFC 2930 */
	{ T_TSIG,	"TSIG" },		/* RFC 2845 */
	{ T_IXFR,	"IXFR" },		/* RFC 1995 */
	{ T_AXFR,	"AXFR" },		/* RFC 1035 */
	{ T_MAILB,	"MAILB" },		/* RFC 1035 */
	{ T_MAILA,	"MAILA" },		/* RFC 1035 */
	{ T_ANY,	"ANY" },
	{ 0,		NULL }
};

const struct tok ns_class2str[] = {
	{ C_IN,		"IN" },		/* Not used */
	{ C_CHAOS,	"CHAOS" },
	{ C_HS,		"HS" },
	{ C_ANY,	"ANY" },
	{ 0,		NULL }
};

/* print a query */
static const u_char *
ns_qprint(netdissect_options *ndo,
          register const u_char *cp, register const u_char *bp, int is_mdns)
{
	register const u_char *np = cp;
	register u_int i, class;

	cp = ns_nskip(ndo, cp);

	if (cp == NULL || !ND_TTEST2(*cp, 4))
		return(NULL);

	/* print the qtype */
	i = EXTRACT_16BITS(cp);
	cp += 2;
	ND_PRINT((ndo, " %s", tok2str(ns_type2str, "Type%d", i)));
	/* print the qclass (if it's not IN) */
	i = EXTRACT_16BITS(cp);
	cp += 2;
	if (is_mdns)
		class = (i & ~C_QU);
	else
		class = i;
	if (class != C_IN)
		ND_PRINT((ndo, " %s", tok2str(ns_class2str, "(Class %d)", class)));
	if (is_mdns) {
		ND_PRINT((ndo, i & C_QU ? " (QU)" : " (QM)"));
	}

	ND_PRINT((ndo, "? "));
	cp = ns_nprint(ndo, np, bp);
	return(cp ? cp + 4 : NULL);
}

/* print a reply */
static const u_char *
ns_rprint(netdissect_options *ndo,
          register const u_char *cp, register const u_char *bp, int is_mdns)
{
	register u_int i, class, opt_flags = 0;
	register u_short typ, len;
	register const u_char *rp;

	if (ndo->ndo_vflag) {
		ND_PRINT((ndo, " "));
		if ((cp = ns_nprint(ndo, cp, bp)) == NULL)
			return NULL;
	} else
		cp = ns_nskip(ndo, cp);

	if (cp == NULL || !ND_TTEST2(*cp, 10))
		return (ndo->ndo_snapend);

	/* print the type/qtype */
	typ = EXTRACT_16BITS(cp);
	cp += 2;
	/* print the class (if it's not IN and the type isn't OPT) */
	i = EXTRACT_16BITS(cp);
	cp += 2;
	if (is_mdns)
		class = (i & ~C_CACHE_FLUSH);
	else
		class = i;
	if (class != C_IN && typ != T_OPT)
		ND_PRINT((ndo, " %s", tok2str(ns_class2str, "(Class %d)", class)));
	if (is_mdns) {
		if (i & C_CACHE_FLUSH)
			ND_PRINT((ndo, " (Cache flush)"));
	}

	if (typ == T_OPT) {
		/* get opt flags */
		cp += 2;
		opt_flags = EXTRACT_16BITS(cp);
		/* ignore rest of ttl field */
		cp += 2;
	} else if (ndo->ndo_vflag > 2) {
		/* print ttl */
		ND_PRINT((ndo, " ["));
		unsigned_relts_print(ndo, EXTRACT_32BITS(cp));
		ND_PRINT((ndo, "]"));
		cp += 4;
	} else {
		/* ignore ttl */
		cp += 4;
	}

	len = EXTRACT_16BITS(cp);
	cp += 2;

	rp = cp + len;

	ND_PRINT((ndo, " %s", tok2str(ns_type2str, "Type%d", typ)));
	if (rp > ndo->ndo_snapend)
		return(NULL);

	switch (typ) {
	case T_A:
		if (!ND_TTEST2(*cp, sizeof(struct in_addr)))
			return(NULL);
		ND_PRINT((ndo, " %s", intoa(htonl(EXTRACT_32BITS(cp)))));
		break;

	case T_NS:
	case T_CNAME:
	case T_PTR:
#ifdef T_DNAME
	case T_DNAME:
#endif
		ND_PRINT((ndo, " "));
		if (ns_nprint(ndo, cp, bp) == NULL)
			return(NULL);
		break;

	case T_SOA:
		if (!ndo->ndo_vflag)
			break;
		ND_PRINT((ndo, " "));
		if ((cp = ns_nprint(ndo, cp, bp)) == NULL)
			return(NULL);
		ND_PRINT((ndo, " "));
		if ((cp = ns_nprint(ndo, cp, bp)) == NULL)
			return(NULL);
		if (!ND_TTEST2(*cp, 5 * 4))
			return(NULL);
		ND_PRINT((ndo, " %u", EXTRACT_32BITS(cp)));
		cp += 4;
		ND_PRINT((ndo, " %u", EXTRACT_32BITS(cp)));
		cp += 4;
		ND_PRINT((ndo, " %u", EXTRACT_32BITS(cp)));
		cp += 4;
		ND_PRINT((ndo, " %u", EXTRACT_32BITS(cp)));
		cp += 4;
		ND_PRINT((ndo, " %u", EXTRACT_32BITS(cp)));
		cp += 4;
		break;
	case T_MX:
		ND_PRINT((ndo, " "));
		if (!ND_TTEST2(*cp, 2))
			return(NULL);
		if (ns_nprint(ndo, cp + 2, bp) == NULL)
			return(NULL);
		ND_PRINT((ndo, " %d", EXTRACT_16BITS(cp)));
		break;

	case T_TXT:
		while (cp < rp) {
			ND_PRINT((ndo, " \""));
			cp = ns_cprint(ndo, cp);
			if (cp == NULL)
				return(NULL);
			ND_PRINT((ndo, "\""));
		}
		break;

	case T_SRV:
		ND_PRINT((ndo, " "));
		if (!ND_TTEST2(*cp, 6))
			return(NULL);
		if (ns_nprint(ndo, cp + 6, bp) == NULL)
			return(NULL);
		ND_PRINT((ndo, ":%d %d %d", EXTRACT_16BITS(cp + 4),
			EXTRACT_16BITS(cp), EXTRACT_16BITS(cp + 2)));
		break;

	case T_AAAA:
	    {
		char ntop_buf[INET6_ADDRSTRLEN];

		if (!ND_TTEST2(*cp, sizeof(struct in6_addr)))
			return(NULL);
		ND_PRINT((ndo, " %s",
		    addrtostr6(cp, ntop_buf, sizeof(ntop_buf))));

		break;
	    }

	case T_A6:
	    {
		struct in6_addr a;
		int pbit, pbyte;
		char ntop_buf[INET6_ADDRSTRLEN];

		if (!ND_TTEST2(*cp, 1))
			return(NULL);
		pbit = *cp;
		pbyte = (pbit & ~7) / 8;
		if (pbit > 128) {
			ND_PRINT((ndo, " %u(bad plen)", pbit));
			break;
		} else if (pbit < 128) {
			if (!ND_TTEST2(*(cp + 1), sizeof(a) - pbyte))
				return(NULL);
			memset(&a, 0, sizeof(a));
			memcpy(&a.s6_addr[pbyte], cp + 1, sizeof(a) - pbyte);
			ND_PRINT((ndo, " %u %s", pbit,
			    addrtostr6(&a, ntop_buf, sizeof(ntop_buf))));
		}
		if (pbit > 0) {
			ND_PRINT((ndo, " "));
			if (ns_nprint(ndo, cp + 1 + sizeof(a) - pbyte, bp) == NULL)
				return(NULL);
		}
		break;
	    }

	case T_OPT:
		ND_PRINT((ndo, " UDPsize=%u", class));
		if (opt_flags & 0x8000)
			ND_PRINT((ndo, " DO"));
		break;

	case T_UNSPECA:		/* One long string */
		if (!ND_TTEST2(*cp, len))
			return(NULL);
		if (fn_printn(ndo, cp, len, ndo->ndo_snapend))
			return(NULL);
		break;

	case T_TSIG:
	    {
		if (cp + len > ndo->ndo_snapend)
			return(NULL);
		if (!ndo->ndo_vflag)
			break;
		ND_PRINT((ndo, " "));
		if ((cp = ns_nprint(ndo, cp, bp)) == NULL)
			return(NULL);
		cp += 6;
		if (!ND_TTEST2(*cp, 2))
			return(NULL);
		ND_PRINT((ndo, " fudge=%u", EXTRACT_16BITS(cp)));
		cp += 2;
		if (!ND_TTEST2(*cp, 2))
			return(NULL);
		ND_PRINT((ndo, " maclen=%u", EXTRACT_16BITS(cp)));
		cp += 2 + EXTRACT_16BITS(cp);
		if (!ND_TTEST2(*cp, 2))
			return(NULL);
		ND_PRINT((ndo, " origid=%u", EXTRACT_16BITS(cp)));
		cp += 2;
		if (!ND_TTEST2(*cp, 2))
			return(NULL);
		ND_PRINT((ndo, " error=%u", EXTRACT_16BITS(cp)));
		cp += 2;
		if (!ND_TTEST2(*cp, 2))
			return(NULL);
		ND_PRINT((ndo, " otherlen=%u", EXTRACT_16BITS(cp)));
		cp += 2;
	    }
	}
	return (rp);		/* XXX This isn't always right */
}

void
ns_print(netdissect_options *ndo,
         register const u_char *bp, u_int length, int is_mdns)
{
	register const HEADER *np;
	register int qdcount, ancount, nscount, arcount;
	register const u_char *cp;
	uint16_t b2;

	np = (const HEADER *)bp;
	ND_TCHECK(*np);
	/* get the byte-order right */
	qdcount = EXTRACT_16BITS(&np->qdcount);
	ancount = EXTRACT_16BITS(&np->ancount);
	nscount = EXTRACT_16BITS(&np->nscount);
	arcount = EXTRACT_16BITS(&np->arcount);

	if (DNS_QR(np)) {
		/* this is a response */
		ND_PRINT((ndo, "%d%s%s%s%s%s%s",
			EXTRACT_16BITS(&np->id),
			ns_ops[DNS_OPCODE(np)],
			ns_resp[DNS_RCODE(np)],
			DNS_AA(np)? "*" : "",
			DNS_RA(np)? "" : "-",
			DNS_TC(np)? "|" : "",
			DNS_AD(np)? "$" : ""));

		if (qdcount != 1)
			ND_PRINT((ndo, " [%dq]", qdcount));
		/* Print QUESTION section on -vv */
		cp = (const u_char *)(np + 1);
		while (qdcount--) {
			if (qdcount < EXTRACT_16BITS(&np->qdcount) - 1)
				ND_PRINT((ndo, ","));
			if (ndo->ndo_vflag > 1) {
				ND_PRINT((ndo, " q:"));
				if ((cp = ns_qprint(ndo, cp, bp, is_mdns)) == NULL)
					goto trunc;
			} else {
				if ((cp = ns_nskip(ndo, cp)) == NULL)
					goto trunc;
				cp += 4;	/* skip QTYPE and QCLASS */
			}
		}
		ND_PRINT((ndo, " %d/%d/%d", ancount, nscount, arcount));
		if (ancount--) {
			if ((cp = ns_rprint(ndo, cp, bp, is_mdns)) == NULL)
				goto trunc;
			while (cp < ndo->ndo_snapend && ancount--) {
				ND_PRINT((ndo, ","));
				if ((cp = ns_rprint(ndo, cp, bp, is_mdns)) == NULL)
					goto trunc;
			}
		}
		if (ancount > 0)
			goto trunc;
		/* Print NS and AR sections on -vv */
		if (ndo->ndo_vflag > 1) {
			if (cp < ndo->ndo_snapend && nscount--) {
				ND_PRINT((ndo, " ns:"));
				if ((cp = ns_rprint(ndo, cp, bp, is_mdns)) == NULL)
					goto trunc;
				while (cp < ndo->ndo_snapend && nscount--) {
					ND_PRINT((ndo, ","));
					if ((cp = ns_rprint(ndo, cp, bp, is_mdns)) == NULL)
						goto trunc;
				}
			}
			if (nscount > 0)
				goto trunc;
			if (cp < ndo->ndo_snapend && arcount--) {
				ND_PRINT((ndo, " ar:"));
				if ((cp = ns_rprint(ndo, cp, bp, is_mdns)) == NULL)
					goto trunc;
				while (cp < ndo->ndo_snapend && arcount--) {
					ND_PRINT((ndo, ","));
					if ((cp = ns_rprint(ndo, cp, bp, is_mdns)) == NULL)
						goto trunc;
				}
			}
			if (arcount > 0)
				goto trunc;
		}
	}
	else {
		/* this is a request */
		ND_PRINT((ndo, "%d%s%s%s", EXTRACT_16BITS(&np->id), ns_ops[DNS_OPCODE(np)],
		    DNS_RD(np) ? "+" : "",
		    DNS_CD(np) ? "%" : ""));

		/* any weirdness? */
		b2 = EXTRACT_16BITS(((const u_short *)np)+1);
		if (b2 & 0x6cf)
			ND_PRINT((ndo, " [b2&3=0x%x]", b2));

		if (DNS_OPCODE(np) == IQUERY) {
			if (qdcount)
				ND_PRINT((ndo, " [%dq]", qdcount));
			if (ancount != 1)
				ND_PRINT((ndo, " [%da]", ancount));
		}
		else {
			if (ancount)
				ND_PRINT((ndo, " [%da]", ancount));
			if (qdcount != 1)
				ND_PRINT((ndo, " [%dq]", qdcount));
		}
		if (nscount)
			ND_PRINT((ndo, " [%dn]", nscount));
		if (arcount)
			ND_PRINT((ndo, " [%dau]", arcount));

		cp = (const u_char *)(np + 1);
		if (qdcount--) {
			cp = ns_qprint(ndo, cp, (const u_char *)np, is_mdns);
			if (!cp)
				goto trunc;
			while (cp < ndo->ndo_snapend && qdcount--) {
				cp = ns_qprint(ndo, (const u_char *)cp,
					       (const u_char *)np,
					       is_mdns);
				if (!cp)
					goto trunc;
			}
		}
		if (qdcount > 0)
			goto trunc;

		/* Print remaining sections on -vv */
		if (ndo->ndo_vflag > 1) {
			if (ancount--) {
				if ((cp = ns_rprint(ndo, cp, bp, is_mdns)) == NULL)
					goto trunc;
				while (cp < ndo->ndo_snapend && ancount--) {
					ND_PRINT((ndo, ","));
					if ((cp = ns_rprint(ndo, cp, bp, is_mdns)) == NULL)
						goto trunc;
				}
			}
			if (ancount > 0)
				goto trunc;
			if (cp < ndo->ndo_snapend && nscount--) {
				ND_PRINT((ndo, " ns:"));
				if ((cp = ns_rprint(ndo, cp, bp, is_mdns)) == NULL)
					goto trunc;
				while (nscount-- && cp < ndo->ndo_snapend) {
					ND_PRINT((ndo, ","));
					if ((cp = ns_rprint(ndo, cp, bp, is_mdns)) == NULL)
						goto trunc;
				}
			}
			if (nscount > 0)
				goto trunc;
			if (cp < ndo->ndo_snapend && arcount--) {
				ND_PRINT((ndo, " ar:"));
				if ((cp = ns_rprint(ndo, cp, bp, is_mdns)) == NULL)
					goto trunc;
				while (cp < ndo->ndo_snapend && arcount--) {
					ND_PRINT((ndo, ","));
					if ((cp = ns_rprint(ndo, cp, bp, is_mdns)) == NULL)
						goto trunc;
				}
			}
			if (arcount > 0)
				goto trunc;
		}
	}
	ND_PRINT((ndo, " (%d)", length));
	return;

  trunc:
	ND_PRINT((ndo, "[|domain]"));
}
