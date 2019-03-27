/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
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
 *
 *  Internet, ethernet, port, and protocol string to address
 *  and address to string conversion routines
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_CASPER
#include <libcasper.h>
#include <casper/cap_dns.h>
#endif /* HAVE_CASPER */

#include <netdissect-stdinc.h>

#ifdef USE_ETHER_NTOHOST
#ifdef HAVE_NETINET_IF_ETHER_H
struct mbuf;		/* Squelch compiler warnings on some platforms for */
struct rtentry;		/* declarations in <net/if.h> */
#include <net/if.h>	/* for "struct ifnet" in "struct arpcom" on Solaris */
#include <netinet/if_ether.h>
#endif /* HAVE_NETINET_IF_ETHER_H */
#ifdef NETINET_ETHER_H_DECLARES_ETHER_NTOHOST
#include <netinet/ether.h>
#endif /* NETINET_ETHER_H_DECLARES_ETHER_NTOHOST */

#if !defined(HAVE_DECL_ETHER_NTOHOST) || !HAVE_DECL_ETHER_NTOHOST
#ifndef HAVE_STRUCT_ETHER_ADDR
struct ether_addr {
	unsigned char ether_addr_octet[6];
};
#endif
extern int ether_ntohost(char *, const struct ether_addr *);
#endif

#endif /* USE_ETHER_NTOHOST */

#include <pcap.h>
#include <pcap-namedb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "addrtostr.h"
#include "ethertype.h"
#include "llc.h"
#include "setsignal.h"
#include "extract.h"
#include "oui.h"

#ifndef ETHER_ADDR_LEN
#define ETHER_ADDR_LEN	6
#endif

/*
 * hash tables for whatever-to-name translations
 *
 * ndo_error() called on strdup(3) failure
 */

#define HASHNAMESIZE 4096

struct hnamemem {
	uint32_t addr;
	const char *name;
	struct hnamemem *nxt;
};

static struct hnamemem hnametable[HASHNAMESIZE];
static struct hnamemem tporttable[HASHNAMESIZE];
static struct hnamemem uporttable[HASHNAMESIZE];
static struct hnamemem eprototable[HASHNAMESIZE];
static struct hnamemem dnaddrtable[HASHNAMESIZE];
static struct hnamemem ipxsaptable[HASHNAMESIZE];

#ifdef _WIN32
/*
 * fake gethostbyaddr for Win2k/XP
 * gethostbyaddr() returns incorrect value when AF_INET6 is passed
 * to 3rd argument.
 *
 * h_name in struct hostent is only valid.
 */
static struct hostent *
win32_gethostbyaddr(const char *addr, int len, int type)
{
	static struct hostent host;
	static char hostbuf[NI_MAXHOST];
	char hname[NI_MAXHOST];
	struct sockaddr_in6 addr6;

	host.h_name = hostbuf;
	switch (type) {
	case AF_INET:
		return gethostbyaddr(addr, len, type);
		break;
	case AF_INET6:
		memset(&addr6, 0, sizeof(addr6));
		addr6.sin6_family = AF_INET6;
		memcpy(&addr6.sin6_addr, addr, len);
		if (getnameinfo((struct sockaddr *)&addr6, sizeof(addr6),
		    hname, sizeof(hname), NULL, 0, 0)) {
			return NULL;
		} else {
			strcpy(host.h_name, hname);
			return &host;
		}
		break;
	default:
		return NULL;
	}
}
#define gethostbyaddr win32_gethostbyaddr
#endif /* _WIN32 */

struct h6namemem {
	struct in6_addr addr;
	char *name;
	struct h6namemem *nxt;
};

static struct h6namemem h6nametable[HASHNAMESIZE];

struct enamemem {
	u_short e_addr0;
	u_short e_addr1;
	u_short e_addr2;
	const char *e_name;
	u_char *e_nsap;			/* used only for nsaptable[] */
	struct enamemem *e_nxt;
};

static struct enamemem enametable[HASHNAMESIZE];
static struct enamemem nsaptable[HASHNAMESIZE];

struct bsnamemem {
	u_short bs_addr0;
	u_short bs_addr1;
	u_short bs_addr2;
	const char *bs_name;
	u_char *bs_bytes;
	unsigned int bs_nbytes;
	struct bsnamemem *bs_nxt;
};

static struct bsnamemem bytestringtable[HASHNAMESIZE];

struct protoidmem {
	uint32_t p_oui;
	u_short p_proto;
	const char *p_name;
	struct protoidmem *p_nxt;
};

static struct protoidmem protoidtable[HASHNAMESIZE];

/*
 * A faster replacement for inet_ntoa().
 */
const char *
intoa(uint32_t addr)
{
	register char *cp;
	register u_int byte;
	register int n;
	static char buf[sizeof(".xxx.xxx.xxx.xxx")];

	NTOHL(addr);
	cp = buf + sizeof(buf);
	*--cp = '\0';

	n = 4;
	do {
		byte = addr & 0xff;
		*--cp = byte % 10 + '0';
		byte /= 10;
		if (byte > 0) {
			*--cp = byte % 10 + '0';
			byte /= 10;
			if (byte > 0)
				*--cp = byte + '0';
		}
		*--cp = '.';
		addr >>= 8;
	} while (--n > 0);

	return cp + 1;
}

static uint32_t f_netmask;
static uint32_t f_localnet;
#ifdef HAVE_CASPER
extern cap_channel_t *capdns;
#endif

/*
 * Return a name for the IP address pointed to by ap.  This address
 * is assumed to be in network byte order.
 *
 * NOTE: ap is *NOT* necessarily part of the packet data (not even if
 * this is being called with the "ipaddr_string()" macro), so you
 * *CANNOT* use the ND_TCHECK{2}/ND_TTEST{2} macros on it.  Furthermore,
 * even in cases where it *is* part of the packet data, the caller
 * would still have to check for a null return value, even if it's
 * just printing the return value with "%s" - not all versions of
 * printf print "(null)" with "%s" and a null pointer, some of them
 * don't check for a null pointer and crash in that case.
 *
 * The callers of this routine should, before handing this routine
 * a pointer to packet data, be sure that the data is present in
 * the packet buffer.  They should probably do those checks anyway,
 * as other data at that layer might not be IP addresses, and it
 * also needs to check whether they're present in the packet buffer.
 */
const char *
getname(netdissect_options *ndo, const u_char *ap)
{
	register struct hostent *hp;
	uint32_t addr;
	struct hnamemem *p;

	memcpy(&addr, ap, sizeof(addr));
	p = &hnametable[addr & (HASHNAMESIZE-1)];
	for (; p->nxt; p = p->nxt) {
		if (p->addr == addr)
			return (p->name);
	}
	p->addr = addr;
	p->nxt = newhnamemem(ndo);

	/*
	 * Print names unless:
	 *	(1) -n was given.
	 *      (2) Address is foreign and -f was given. (If -f was not
	 *	    given, f_netmask and f_localnet are 0 and the test
	 *	    evaluates to true)
	 */
	if (!ndo->ndo_nflag &&
	    (addr & f_netmask) == f_localnet) {
#ifdef HAVE_CASPER
		if (capdns != NULL) {
			hp = cap_gethostbyaddr(capdns, (char *)&addr, 4,
			    AF_INET);
		} else
#endif
			hp = gethostbyaddr((char *)&addr, 4, AF_INET);
		if (hp) {
			char *dotp;

			p->name = strdup(hp->h_name);
			if (p->name == NULL)
				(*ndo->ndo_error)(ndo,
						  "getname: strdup(hp->h_name)");
			if (ndo->ndo_Nflag) {
				/* Remove domain qualifications */
				dotp = strchr(p->name, '.');
				if (dotp)
					*dotp = '\0';
			}
			return (p->name);
		}
	}
	p->name = strdup(intoa(addr));
	if (p->name == NULL)
		(*ndo->ndo_error)(ndo, "getname: strdup(intoa(addr))");
	return (p->name);
}

/*
 * Return a name for the IP6 address pointed to by ap.  This address
 * is assumed to be in network byte order.
 */
const char *
getname6(netdissect_options *ndo, const u_char *ap)
{
	register struct hostent *hp;
	union {
		struct in6_addr addr;
		struct for_hash_addr {
			char fill[14];
			uint16_t d;
		} addra;
	} addr;
	struct h6namemem *p;
	register const char *cp;
	char ntop_buf[INET6_ADDRSTRLEN];

	memcpy(&addr, ap, sizeof(addr));
	p = &h6nametable[addr.addra.d & (HASHNAMESIZE-1)];
	for (; p->nxt; p = p->nxt) {
		if (memcmp(&p->addr, &addr, sizeof(addr)) == 0)
			return (p->name);
	}
	p->addr = addr.addr;
	p->nxt = newh6namemem(ndo);

	/*
	 * Do not print names if -n was given.
	 */
	if (!ndo->ndo_nflag) {
#ifdef HAVE_CASPER
		if (capdns != NULL) {
			hp = cap_gethostbyaddr(capdns, (char *)&addr,
			    sizeof(addr), AF_INET6);
		} else
#endif
			hp = gethostbyaddr((char *)&addr, sizeof(addr),
			    AF_INET6);
		if (hp) {
			char *dotp;

			p->name = strdup(hp->h_name);
			if (p->name == NULL)
				(*ndo->ndo_error)(ndo,
						  "getname6: strdup(hp->h_name)");
			if (ndo->ndo_Nflag) {
				/* Remove domain qualifications */
				dotp = strchr(p->name, '.');
				if (dotp)
					*dotp = '\0';
			}
			return (p->name);
		}
	}
	cp = addrtostr6(ap, ntop_buf, sizeof(ntop_buf));
	p->name = strdup(cp);
	if (p->name == NULL)
		(*ndo->ndo_error)(ndo, "getname6: strdup(cp)");
	return (p->name);
}

static const char hex[16] = "0123456789abcdef";


/* Find the hash node that corresponds the ether address 'ep' */

static inline struct enamemem *
lookup_emem(netdissect_options *ndo, const u_char *ep)
{
	register u_int i, j, k;
	struct enamemem *tp;

	k = (ep[0] << 8) | ep[1];
	j = (ep[2] << 8) | ep[3];
	i = (ep[4] << 8) | ep[5];

	tp = &enametable[(i ^ j) & (HASHNAMESIZE-1)];
	while (tp->e_nxt)
		if (tp->e_addr0 == i &&
		    tp->e_addr1 == j &&
		    tp->e_addr2 == k)
			return tp;
		else
			tp = tp->e_nxt;
	tp->e_addr0 = i;
	tp->e_addr1 = j;
	tp->e_addr2 = k;
	tp->e_nxt = (struct enamemem *)calloc(1, sizeof(*tp));
	if (tp->e_nxt == NULL)
		(*ndo->ndo_error)(ndo, "lookup_emem: calloc");

	return tp;
}

/*
 * Find the hash node that corresponds to the bytestring 'bs'
 * with length 'nlen'
 */

static inline struct bsnamemem *
lookup_bytestring(netdissect_options *ndo, register const u_char *bs,
		  const unsigned int nlen)
{
	struct bsnamemem *tp;
	register u_int i, j, k;

	if (nlen >= 6) {
		k = (bs[0] << 8) | bs[1];
		j = (bs[2] << 8) | bs[3];
		i = (bs[4] << 8) | bs[5];
	} else if (nlen >= 4) {
		k = (bs[0] << 8) | bs[1];
		j = (bs[2] << 8) | bs[3];
		i = 0;
	} else
		i = j = k = 0;

	tp = &bytestringtable[(i ^ j) & (HASHNAMESIZE-1)];
	while (tp->bs_nxt)
		if (nlen == tp->bs_nbytes &&
		    tp->bs_addr0 == i &&
		    tp->bs_addr1 == j &&
		    tp->bs_addr2 == k &&
		    memcmp((const char *)bs, (const char *)(tp->bs_bytes), nlen) == 0)
			return tp;
		else
			tp = tp->bs_nxt;

	tp->bs_addr0 = i;
	tp->bs_addr1 = j;
	tp->bs_addr2 = k;

	tp->bs_bytes = (u_char *) calloc(1, nlen);
	if (tp->bs_bytes == NULL)
		(*ndo->ndo_error)(ndo, "lookup_bytestring: calloc");

	memcpy(tp->bs_bytes, bs, nlen);
	tp->bs_nbytes = nlen;
	tp->bs_nxt = (struct bsnamemem *)calloc(1, sizeof(*tp));
	if (tp->bs_nxt == NULL)
		(*ndo->ndo_error)(ndo, "lookup_bytestring: calloc");

	return tp;
}

/* Find the hash node that corresponds the NSAP 'nsap' */

static inline struct enamemem *
lookup_nsap(netdissect_options *ndo, register const u_char *nsap,
	    register u_int nsap_length)
{
	register u_int i, j, k;
	struct enamemem *tp;
	const u_char *ensap;

	if (nsap_length > 6) {
		ensap = nsap + nsap_length - 6;
		k = (ensap[0] << 8) | ensap[1];
		j = (ensap[2] << 8) | ensap[3];
		i = (ensap[4] << 8) | ensap[5];
	}
	else
		i = j = k = 0;

	tp = &nsaptable[(i ^ j) & (HASHNAMESIZE-1)];
	while (tp->e_nxt)
		if (nsap_length == tp->e_nsap[0] &&
		    tp->e_addr0 == i &&
		    tp->e_addr1 == j &&
		    tp->e_addr2 == k &&
		    memcmp((const char *)nsap,
			(char *)&(tp->e_nsap[1]), nsap_length) == 0)
			return tp;
		else
			tp = tp->e_nxt;
	tp->e_addr0 = i;
	tp->e_addr1 = j;
	tp->e_addr2 = k;
	tp->e_nsap = (u_char *)malloc(nsap_length + 1);
	if (tp->e_nsap == NULL)
		(*ndo->ndo_error)(ndo, "lookup_nsap: malloc");
	tp->e_nsap[0] = (u_char)nsap_length;	/* guaranteed < ISONSAP_MAX_LENGTH */
	memcpy((char *)&tp->e_nsap[1], (const char *)nsap, nsap_length);
	tp->e_nxt = (struct enamemem *)calloc(1, sizeof(*tp));
	if (tp->e_nxt == NULL)
		(*ndo->ndo_error)(ndo, "lookup_nsap: calloc");

	return tp;
}

/* Find the hash node that corresponds the protoid 'pi'. */

static inline struct protoidmem *
lookup_protoid(netdissect_options *ndo, const u_char *pi)
{
	register u_int i, j;
	struct protoidmem *tp;

	/* 5 octets won't be aligned */
	i = (((pi[0] << 8) + pi[1]) << 8) + pi[2];
	j =   (pi[3] << 8) + pi[4];
	/* XXX should be endian-insensitive, but do big-endian testing  XXX */

	tp = &protoidtable[(i ^ j) & (HASHNAMESIZE-1)];
	while (tp->p_nxt)
		if (tp->p_oui == i && tp->p_proto == j)
			return tp;
		else
			tp = tp->p_nxt;
	tp->p_oui = i;
	tp->p_proto = j;
	tp->p_nxt = (struct protoidmem *)calloc(1, sizeof(*tp));
	if (tp->p_nxt == NULL)
		(*ndo->ndo_error)(ndo, "lookup_protoid: calloc");

	return tp;
}

const char *
etheraddr_string(netdissect_options *ndo, register const u_char *ep)
{
	register int i;
	register char *cp;
	register struct enamemem *tp;
	int oui;
	char buf[BUFSIZE];

	tp = lookup_emem(ndo, ep);
	if (tp->e_name)
		return (tp->e_name);
#ifdef USE_ETHER_NTOHOST
	if (!ndo->ndo_nflag) {
		char buf2[BUFSIZE];

		if (ether_ntohost(buf2, (const struct ether_addr *)ep) == 0) {
			tp->e_name = strdup(buf2);
			if (tp->e_name == NULL)
				(*ndo->ndo_error)(ndo,
						  "etheraddr_string: strdup(buf2)");
			return (tp->e_name);
		}
	}
#endif
	cp = buf;
	oui = EXTRACT_24BITS(ep);
	*cp++ = hex[*ep >> 4 ];
	*cp++ = hex[*ep++ & 0xf];
	for (i = 5; --i >= 0;) {
		*cp++ = ':';
		*cp++ = hex[*ep >> 4 ];
		*cp++ = hex[*ep++ & 0xf];
	}

	if (!ndo->ndo_nflag) {
		snprintf(cp, BUFSIZE - (2 + 5*3), " (oui %s)",
		    tok2str(oui_values, "Unknown", oui));
	} else
		*cp = '\0';
	tp->e_name = strdup(buf);
	if (tp->e_name == NULL)
		(*ndo->ndo_error)(ndo, "etheraddr_string: strdup(buf)");
	return (tp->e_name);
}

const char *
le64addr_string(netdissect_options *ndo, const u_char *ep)
{
	const unsigned int len = 8;
	register u_int i;
	register char *cp;
	register struct bsnamemem *tp;
	char buf[BUFSIZE];

	tp = lookup_bytestring(ndo, ep, len);
	if (tp->bs_name)
		return (tp->bs_name);

	cp = buf;
	for (i = len; i > 0 ; --i) {
		*cp++ = hex[*(ep + i - 1) >> 4];
		*cp++ = hex[*(ep + i - 1) & 0xf];
		*cp++ = ':';
	}
	cp --;

	*cp = '\0';

	tp->bs_name = strdup(buf);
	if (tp->bs_name == NULL)
		(*ndo->ndo_error)(ndo, "le64addr_string: strdup(buf)");

	return (tp->bs_name);
}

const char *
linkaddr_string(netdissect_options *ndo, const u_char *ep,
		const unsigned int type, const unsigned int len)
{
	register u_int i;
	register char *cp;
	register struct bsnamemem *tp;

	if (len == 0)
		return ("<empty>");

	if (type == LINKADDR_ETHER && len == ETHER_ADDR_LEN)
		return (etheraddr_string(ndo, ep));

	if (type == LINKADDR_FRELAY)
		return (q922_string(ndo, ep, len));

	tp = lookup_bytestring(ndo, ep, len);
	if (tp->bs_name)
		return (tp->bs_name);

	tp->bs_name = cp = (char *)malloc(len*3);
	if (tp->bs_name == NULL)
		(*ndo->ndo_error)(ndo, "linkaddr_string: malloc");
	*cp++ = hex[*ep >> 4];
	*cp++ = hex[*ep++ & 0xf];
	for (i = len-1; i > 0 ; --i) {
		*cp++ = ':';
		*cp++ = hex[*ep >> 4];
		*cp++ = hex[*ep++ & 0xf];
	}
	*cp = '\0';
	return (tp->bs_name);
}

const char *
etherproto_string(netdissect_options *ndo, u_short port)
{
	register char *cp;
	register struct hnamemem *tp;
	register uint32_t i = port;
	char buf[sizeof("0000")];

	for (tp = &eprototable[i & (HASHNAMESIZE-1)]; tp->nxt; tp = tp->nxt)
		if (tp->addr == i)
			return (tp->name);

	tp->addr = i;
	tp->nxt = newhnamemem(ndo);

	cp = buf;
	NTOHS(port);
	*cp++ = hex[port >> 12 & 0xf];
	*cp++ = hex[port >> 8 & 0xf];
	*cp++ = hex[port >> 4 & 0xf];
	*cp++ = hex[port & 0xf];
	*cp++ = '\0';
	tp->name = strdup(buf);
	if (tp->name == NULL)
		(*ndo->ndo_error)(ndo, "etherproto_string: strdup(buf)");
	return (tp->name);
}

const char *
protoid_string(netdissect_options *ndo, register const u_char *pi)
{
	register u_int i, j;
	register char *cp;
	register struct protoidmem *tp;
	char buf[sizeof("00:00:00:00:00")];

	tp = lookup_protoid(ndo, pi);
	if (tp->p_name)
		return tp->p_name;

	cp = buf;
	if ((j = *pi >> 4) != 0)
		*cp++ = hex[j];
	*cp++ = hex[*pi++ & 0xf];
	for (i = 4; (int)--i >= 0;) {
		*cp++ = ':';
		if ((j = *pi >> 4) != 0)
			*cp++ = hex[j];
		*cp++ = hex[*pi++ & 0xf];
	}
	*cp = '\0';
	tp->p_name = strdup(buf);
	if (tp->p_name == NULL)
		(*ndo->ndo_error)(ndo, "protoid_string: strdup(buf)");
	return (tp->p_name);
}

#define ISONSAP_MAX_LENGTH 20
const char *
isonsap_string(netdissect_options *ndo, const u_char *nsap,
	       register u_int nsap_length)
{
	register u_int nsap_idx;
	register char *cp;
	register struct enamemem *tp;

	if (nsap_length < 1 || nsap_length > ISONSAP_MAX_LENGTH)
		return ("isonsap_string: illegal length");

	tp = lookup_nsap(ndo, nsap, nsap_length);
	if (tp->e_name)
		return tp->e_name;

	tp->e_name = cp = (char *)malloc(sizeof("xx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xx"));
	if (cp == NULL)
		(*ndo->ndo_error)(ndo, "isonsap_string: malloc");

	for (nsap_idx = 0; nsap_idx < nsap_length; nsap_idx++) {
		*cp++ = hex[*nsap >> 4];
		*cp++ = hex[*nsap++ & 0xf];
		if (((nsap_idx & 1) == 0) &&
		     (nsap_idx + 1 < nsap_length)) {
		     	*cp++ = '.';
		}
	}
	*cp = '\0';
	return (tp->e_name);
}

const char *
tcpport_string(netdissect_options *ndo, u_short port)
{
	register struct hnamemem *tp;
	register uint32_t i = port;
	char buf[sizeof("00000")];

	for (tp = &tporttable[i & (HASHNAMESIZE-1)]; tp->nxt; tp = tp->nxt)
		if (tp->addr == i)
			return (tp->name);

	tp->addr = i;
	tp->nxt = newhnamemem(ndo);

	(void)snprintf(buf, sizeof(buf), "%u", i);
	tp->name = strdup(buf);
	if (tp->name == NULL)
		(*ndo->ndo_error)(ndo, "tcpport_string: strdup(buf)");
	return (tp->name);
}

const char *
udpport_string(netdissect_options *ndo, register u_short port)
{
	register struct hnamemem *tp;
	register uint32_t i = port;
	char buf[sizeof("00000")];

	for (tp = &uporttable[i & (HASHNAMESIZE-1)]; tp->nxt; tp = tp->nxt)
		if (tp->addr == i)
			return (tp->name);

	tp->addr = i;
	tp->nxt = newhnamemem(ndo);

	(void)snprintf(buf, sizeof(buf), "%u", i);
	tp->name = strdup(buf);
	if (tp->name == NULL)
		(*ndo->ndo_error)(ndo, "udpport_string: strdup(buf)");
	return (tp->name);
}

const char *
ipxsap_string(netdissect_options *ndo, u_short port)
{
	register char *cp;
	register struct hnamemem *tp;
	register uint32_t i = port;
	char buf[sizeof("0000")];

	for (tp = &ipxsaptable[i & (HASHNAMESIZE-1)]; tp->nxt; tp = tp->nxt)
		if (tp->addr == i)
			return (tp->name);

	tp->addr = i;
	tp->nxt = newhnamemem(ndo);

	cp = buf;
	NTOHS(port);
	*cp++ = hex[port >> 12 & 0xf];
	*cp++ = hex[port >> 8 & 0xf];
	*cp++ = hex[port >> 4 & 0xf];
	*cp++ = hex[port & 0xf];
	*cp++ = '\0';
	tp->name = strdup(buf);
	if (tp->name == NULL)
		(*ndo->ndo_error)(ndo, "ipxsap_string: strdup(buf)");
	return (tp->name);
}

static void
init_servarray(netdissect_options *ndo)
{
	struct servent *sv;
	register struct hnamemem *table;
	register int i;
	char buf[sizeof("0000000000")];

	while ((sv = getservent()) != NULL) {
		int port = ntohs(sv->s_port);
		i = port & (HASHNAMESIZE-1);
		if (strcmp(sv->s_proto, "tcp") == 0)
			table = &tporttable[i];
		else if (strcmp(sv->s_proto, "udp") == 0)
			table = &uporttable[i];
		else
			continue;

		while (table->name)
			table = table->nxt;
		if (ndo->ndo_nflag) {
			(void)snprintf(buf, sizeof(buf), "%d", port);
			table->name = strdup(buf);
		} else
			table->name = strdup(sv->s_name);
		if (table->name == NULL)
			(*ndo->ndo_error)(ndo, "init_servarray: strdup");

		table->addr = port;
		table->nxt = newhnamemem(ndo);
	}
	endservent();
}

static const struct eproto {
	const char *s;
	u_short p;
} eproto_db[] = {
	{ "pup", ETHERTYPE_PUP },
	{ "xns", ETHERTYPE_NS },
	{ "ip", ETHERTYPE_IP },
	{ "ip6", ETHERTYPE_IPV6 },
	{ "arp", ETHERTYPE_ARP },
	{ "rarp", ETHERTYPE_REVARP },
	{ "sprite", ETHERTYPE_SPRITE },
	{ "mopdl", ETHERTYPE_MOPDL },
	{ "moprc", ETHERTYPE_MOPRC },
	{ "decnet", ETHERTYPE_DN },
	{ "lat", ETHERTYPE_LAT },
	{ "sca", ETHERTYPE_SCA },
	{ "lanbridge", ETHERTYPE_LANBRIDGE },
	{ "vexp", ETHERTYPE_VEXP },
	{ "vprod", ETHERTYPE_VPROD },
	{ "atalk", ETHERTYPE_ATALK },
	{ "atalkarp", ETHERTYPE_AARP },
	{ "loopback", ETHERTYPE_LOOPBACK },
	{ "decdts", ETHERTYPE_DECDTS },
	{ "decdns", ETHERTYPE_DECDNS },
	{ (char *)0, 0 }
};

static void
init_eprotoarray(netdissect_options *ndo)
{
	register int i;
	register struct hnamemem *table;

	for (i = 0; eproto_db[i].s; i++) {
		int j = htons(eproto_db[i].p) & (HASHNAMESIZE-1);
		table = &eprototable[j];
		while (table->name)
			table = table->nxt;
		table->name = eproto_db[i].s;
		table->addr = htons(eproto_db[i].p);
		table->nxt = newhnamemem(ndo);
	}
}

static const struct protoidlist {
	const u_char protoid[5];
	const char *name;
} protoidlist[] = {
	{{ 0x00, 0x00, 0x0c, 0x01, 0x07 }, "CiscoMLS" },
	{{ 0x00, 0x00, 0x0c, 0x20, 0x00 }, "CiscoCDP" },
	{{ 0x00, 0x00, 0x0c, 0x20, 0x01 }, "CiscoCGMP" },
	{{ 0x00, 0x00, 0x0c, 0x20, 0x03 }, "CiscoVTP" },
	{{ 0x00, 0xe0, 0x2b, 0x00, 0xbb }, "ExtremeEDP" },
	{{ 0x00, 0x00, 0x00, 0x00, 0x00 }, NULL }
};

/*
 * SNAP proto IDs with org code 0:0:0 are actually encapsulated Ethernet
 * types.
 */
static void
init_protoidarray(netdissect_options *ndo)
{
	register int i;
	register struct protoidmem *tp;
	const struct protoidlist *pl;
	u_char protoid[5];

	protoid[0] = 0;
	protoid[1] = 0;
	protoid[2] = 0;
	for (i = 0; eproto_db[i].s; i++) {
		u_short etype = htons(eproto_db[i].p);

		memcpy((char *)&protoid[3], (char *)&etype, 2);
		tp = lookup_protoid(ndo, protoid);
		tp->p_name = strdup(eproto_db[i].s);
		if (tp->p_name == NULL)
			(*ndo->ndo_error)(ndo,
					  "init_protoidarray: strdup(eproto_db[i].s)");
	}
	/* Hardwire some SNAP proto ID names */
	for (pl = protoidlist; pl->name != NULL; ++pl) {
		tp = lookup_protoid(ndo, pl->protoid);
		/* Don't override existing name */
		if (tp->p_name != NULL)
			continue;

		tp->p_name = pl->name;
	}
}

static const struct etherlist {
	const u_char addr[6];
	const char *name;
} etherlist[] = {
	{{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, "Broadcast" },
	{{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, NULL }
};

/*
 * Initialize the ethers hash table.  We take two different approaches
 * depending on whether or not the system provides the ethers name
 * service.  If it does, we just wire in a few names at startup,
 * and etheraddr_string() fills in the table on demand.  If it doesn't,
 * then we suck in the entire /etc/ethers file at startup.  The idea
 * is that parsing the local file will be fast, but spinning through
 * all the ethers entries via NIS & next_etherent might be very slow.
 *
 * XXX pcap_next_etherent doesn't belong in the pcap interface, but
 * since the pcap module already does name-to-address translation,
 * it's already does most of the work for the ethernet address-to-name
 * translation, so we just pcap_next_etherent as a convenience.
 */
static void
init_etherarray(netdissect_options *ndo)
{
	register const struct etherlist *el;
	register struct enamemem *tp;
#ifdef USE_ETHER_NTOHOST
	char name[256];
#else
	register struct pcap_etherent *ep;
	register FILE *fp;

	/* Suck in entire ethers file */
	fp = fopen(PCAP_ETHERS_FILE, "r");
	if (fp != NULL) {
		while ((ep = pcap_next_etherent(fp)) != NULL) {
			tp = lookup_emem(ndo, ep->addr);
			tp->e_name = strdup(ep->name);
			if (tp->e_name == NULL)
				(*ndo->ndo_error)(ndo,
						  "init_etherarray: strdup(ep->addr)");
		}
		(void)fclose(fp);
	}
#endif

	/* Hardwire some ethernet names */
	for (el = etherlist; el->name != NULL; ++el) {
		tp = lookup_emem(ndo, el->addr);
		/* Don't override existing name */
		if (tp->e_name != NULL)
			continue;

#ifdef USE_ETHER_NTOHOST
		/*
		 * Use YP/NIS version of name if available.
		 */
		if (ether_ntohost(name, (const struct ether_addr *)el->addr) == 0) {
			tp->e_name = strdup(name);
			if (tp->e_name == NULL)
				(*ndo->ndo_error)(ndo,
						  "init_etherarray: strdup(name)");
			continue;
		}
#endif
		tp->e_name = el->name;
	}
}

static const struct tok ipxsap_db[] = {
	{ 0x0000, "Unknown" },
	{ 0x0001, "User" },
	{ 0x0002, "User Group" },
	{ 0x0003, "PrintQueue" },
	{ 0x0004, "FileServer" },
	{ 0x0005, "JobServer" },
	{ 0x0006, "Gateway" },
	{ 0x0007, "PrintServer" },
	{ 0x0008, "ArchiveQueue" },
	{ 0x0009, "ArchiveServer" },
	{ 0x000a, "JobQueue" },
	{ 0x000b, "Administration" },
	{ 0x000F, "Novell TI-RPC" },
	{ 0x0017, "Diagnostics" },
	{ 0x0020, "NetBIOS" },
	{ 0x0021, "NAS SNA Gateway" },
	{ 0x0023, "NACS AsyncGateway" },
	{ 0x0024, "RemoteBridge/RoutingService" },
	{ 0x0026, "BridgeServer" },
	{ 0x0027, "TCP/IP Gateway" },
	{ 0x0028, "Point-to-point X.25 BridgeServer" },
	{ 0x0029, "3270 Gateway" },
	{ 0x002a, "CHI Corp" },
	{ 0x002c, "PC Chalkboard" },
	{ 0x002d, "TimeSynchServer" },
	{ 0x002e, "ARCserve5.0/PalindromeBackup" },
	{ 0x0045, "DI3270 Gateway" },
	{ 0x0047, "AdvertisingPrintServer" },
	{ 0x004a, "NetBlazerModems" },
	{ 0x004b, "BtrieveVAP" },
	{ 0x004c, "NetwareSQL" },
	{ 0x004d, "XtreeNetwork" },
	{ 0x0050, "BtrieveVAP4.11" },
	{ 0x0052, "QuickLink" },
	{ 0x0053, "PrintQueueUser" },
	{ 0x0058, "Multipoint X.25 Router" },
	{ 0x0060, "STLB/NLM" },
	{ 0x0064, "ARCserve" },
	{ 0x0066, "ARCserve3.0" },
	{ 0x0072, "WAN CopyUtility" },
	{ 0x007a, "TES-NetwareVMS" },
	{ 0x0092, "WATCOM Debugger/EmeraldTapeBackupServer" },
	{ 0x0095, "DDA OBGYN" },
	{ 0x0098, "NetwareAccessServer" },
	{ 0x009a, "Netware for VMS II/NamedPipeServer" },
	{ 0x009b, "NetwareAccessServer" },
	{ 0x009e, "PortableNetwareServer/SunLinkNVT" },
	{ 0x00a1, "PowerchuteAPC UPS" },
	{ 0x00aa, "LAWserve" },
	{ 0x00ac, "CompaqIDA StatusMonitor" },
	{ 0x0100, "PIPE STAIL" },
	{ 0x0102, "LAN ProtectBindery" },
	{ 0x0103, "OracleDataBaseServer" },
	{ 0x0107, "Netware386/RSPX RemoteConsole" },
	{ 0x010f, "NovellSNA Gateway" },
	{ 0x0111, "TestServer" },
	{ 0x0112, "HP PrintServer" },
	{ 0x0114, "CSA MUX" },
	{ 0x0115, "CSA LCA" },
	{ 0x0116, "CSA CM" },
	{ 0x0117, "CSA SMA" },
	{ 0x0118, "CSA DBA" },
	{ 0x0119, "CSA NMA" },
	{ 0x011a, "CSA SSA" },
	{ 0x011b, "CSA STATUS" },
	{ 0x011e, "CSA APPC" },
	{ 0x0126, "SNA TEST SSA Profile" },
	{ 0x012a, "CSA TRACE" },
	{ 0x012b, "NetwareSAA" },
	{ 0x012e, "IKARUS VirusScan" },
	{ 0x0130, "CommunicationsExecutive" },
	{ 0x0133, "NNS DomainServer/NetwareNamingServicesDomain" },
	{ 0x0135, "NetwareNamingServicesProfile" },
	{ 0x0137, "Netware386 PrintQueue/NNS PrintQueue" },
	{ 0x0141, "LAN SpoolServer" },
	{ 0x0152, "IRMALAN Gateway" },
	{ 0x0154, "NamedPipeServer" },
	{ 0x0166, "NetWareManagement" },
	{ 0x0168, "Intel PICKIT CommServer/Intel CAS TalkServer" },
	{ 0x0173, "Compaq" },
	{ 0x0174, "Compaq SNMP Agent" },
	{ 0x0175, "Compaq" },
	{ 0x0180, "XTreeServer/XTreeTools" },
	{ 0x018A, "NASI ServicesBroadcastServer" },
	{ 0x01b0, "GARP Gateway" },
	{ 0x01b1, "Binfview" },
	{ 0x01bf, "IntelLanDeskManager" },
	{ 0x01ca, "AXTEC" },
	{ 0x01cb, "ShivaNetModem/E" },
	{ 0x01cc, "ShivaLanRover/E" },
	{ 0x01cd, "ShivaLanRover/T" },
	{ 0x01ce, "ShivaUniversal" },
	{ 0x01d8, "CastelleFAXPressServer" },
	{ 0x01da, "CastelleLANPressPrintServer" },
	{ 0x01dc, "CastelleFAX/Xerox7033 FaxServer/ExcelLanFax" },
	{ 0x01f0, "LEGATO" },
	{ 0x01f5, "LEGATO" },
	{ 0x0233, "NMS Agent/NetwareManagementAgent" },
	{ 0x0237, "NMS IPX Discovery/LANternReadWriteChannel" },
	{ 0x0238, "NMS IP Discovery/LANternTrapAlarmChannel" },
	{ 0x023a, "LANtern" },
	{ 0x023c, "MAVERICK" },
	{ 0x023f, "NovellSMDR" },
	{ 0x024e, "NetwareConnect" },
	{ 0x024f, "NASI ServerBroadcast Cisco" },
	{ 0x026a, "NMS ServiceConsole" },
	{ 0x026b, "TimeSynchronizationServer Netware 4.x" },
	{ 0x0278, "DirectoryServer Netware 4.x" },
	{ 0x027b, "NetwareManagementAgent" },
	{ 0x0280, "Novell File and Printer Sharing Service for PC" },
	{ 0x0304, "NovellSAA Gateway" },
	{ 0x0308, "COM/VERMED" },
	{ 0x030a, "GalacticommWorldgroupServer" },
	{ 0x030c, "IntelNetport2/HP JetDirect/HP Quicksilver" },
	{ 0x0320, "AttachmateGateway" },
	{ 0x0327, "MicrosoftDiagnostiocs" },
	{ 0x0328, "WATCOM SQL Server" },
	{ 0x0335, "MultiTechSystems MultisynchCommServer" },
	{ 0x0343, "Xylogics RemoteAccessServer/LANModem" },
	{ 0x0355, "ArcadaBackupExec" },
	{ 0x0358, "MSLCD1" },
	{ 0x0361, "NETINELO" },
	{ 0x037e, "Powerchute UPS Monitoring" },
	{ 0x037f, "ViruSafeNotify" },
	{ 0x0386, "HP Bridge" },
	{ 0x0387, "HP Hub" },
	{ 0x0394, "NetWare SAA Gateway" },
	{ 0x039b, "LotusNotes" },
	{ 0x03b7, "CertusAntiVirus" },
	{ 0x03c4, "ARCserve4.0" },
	{ 0x03c7, "LANspool3.5" },
	{ 0x03d7, "LexmarkPrinterServer" },
	{ 0x03d8, "LexmarkXLE PrinterServer" },
	{ 0x03dd, "BanyanENS NetwareClient" },
	{ 0x03de, "GuptaSequelBaseServer/NetWareSQL" },
	{ 0x03e1, "UnivelUnixware" },
	{ 0x03e4, "UnivelUnixware" },
	{ 0x03fc, "IntelNetport" },
	{ 0x03fd, "PrintServerQueue" },
	{ 0x040A, "ipnServer" },
	{ 0x040D, "LVERRMAN" },
	{ 0x040E, "LVLIC" },
	{ 0x0414, "NET Silicon (DPI)/Kyocera" },
	{ 0x0429, "SiteLockVirus" },
	{ 0x0432, "UFHELPR???" },
	{ 0x0433, "Synoptics281xAdvancedSNMPAgent" },
	{ 0x0444, "MicrosoftNT SNA Server" },
	{ 0x0448, "Oracle" },
	{ 0x044c, "ARCserve5.01" },
	{ 0x0457, "CanonGP55" },
	{ 0x045a, "QMS Printers" },
	{ 0x045b, "DellSCSI Array" },
	{ 0x0491, "NetBlazerModems" },
	{ 0x04ac, "OnTimeScheduler" },
	{ 0x04b0, "CD-Net" },
	{ 0x0513, "EmulexNQA" },
	{ 0x0520, "SiteLockChecks" },
	{ 0x0529, "SiteLockChecks" },
	{ 0x052d, "CitrixOS2 AppServer" },
	{ 0x0535, "Tektronix" },
	{ 0x0536, "Milan" },
	{ 0x055d, "Attachmate SNA gateway" },
	{ 0x056b, "IBM8235 ModemServer" },
	{ 0x056c, "ShivaLanRover/E PLUS" },
	{ 0x056d, "ShivaLanRover/T PLUS" },
	{ 0x0580, "McAfeeNetShield" },
	{ 0x05B8, "NLM to workstation communication (Revelation Software)" },
	{ 0x05BA, "CompatibleSystemsRouters" },
	{ 0x05BE, "CheyenneHierarchicalStorageManager" },
	{ 0x0606, "JCWatermarkImaging" },
	{ 0x060c, "AXISNetworkPrinter" },
	{ 0x0610, "AdaptecSCSIManagement" },
	{ 0x0621, "IBM AntiVirus" },
	{ 0x0640, "Windows95 RemoteRegistryService" },
	{ 0x064e, "MicrosoftIIS" },
	{ 0x067b, "Microsoft Win95/98 File and Print Sharing for NetWare" },
	{ 0x067c, "Microsoft Win95/98 File and Print Sharing for NetWare" },
	{ 0x076C, "Xerox" },
	{ 0x079b, "ShivaLanRover/E 115" },
	{ 0x079c, "ShivaLanRover/T 115" },
	{ 0x07B4, "CubixWorldDesk" },
	{ 0x07c2, "Quarterdeck IWare Connect V2.x NLM" },
	{ 0x07c1, "Quarterdeck IWare Connect V3.x NLM" },
	{ 0x0810, "ELAN License Server Demo" },
	{ 0x0824, "ShivaLanRoverAccessSwitch/E" },
	{ 0x086a, "ISSC Collector" },
	{ 0x087f, "ISSC DAS AgentAIX" },
	{ 0x0880, "Intel Netport PRO" },
	{ 0x0881, "Intel Netport PRO" },
	{ 0x0b29, "SiteLock" },
	{ 0x0c29, "SiteLockApplications" },
	{ 0x0c2c, "LicensingServer" },
	{ 0x2101, "PerformanceTechnologyInstantInternet" },
	{ 0x2380, "LAI SiteLock" },
	{ 0x238c, "MeetingMaker" },
	{ 0x4808, "SiteLockServer/SiteLockMetering" },
	{ 0x5555, "SiteLockUser" },
	{ 0x6312, "Tapeware" },
	{ 0x6f00, "RabbitGateway" },
	{ 0x7703, "MODEM" },
	{ 0x8002, "NetPortPrinters" },
	{ 0x8008, "WordPerfectNetworkVersion" },
	{ 0x85BE, "Cisco EIGRP" },
	{ 0x8888, "WordPerfectNetworkVersion/QuickNetworkManagement" },
	{ 0x9000, "McAfeeNetShield" },
	{ 0x9604, "CSA-NT_MON" },
	{ 0xb6a8, "OceanIsleReachoutRemoteControl" },
	{ 0xf11f, "SiteLockMetering" },
	{ 0xf1ff, "SiteLock" },
	{ 0xf503, "Microsoft SQL Server" },
	{ 0xF905, "IBM TimeAndPlace" },
	{ 0xfbfb, "TopCallIII FaxServer" },
	{ 0xffff, "AnyService/Wildcard" },
	{ 0, (char *)0 }
};

static void
init_ipxsaparray(netdissect_options *ndo)
{
	register int i;
	register struct hnamemem *table;

	for (i = 0; ipxsap_db[i].s != NULL; i++) {
		int j = htons(ipxsap_db[i].v) & (HASHNAMESIZE-1);
		table = &ipxsaptable[j];
		while (table->name)
			table = table->nxt;
		table->name = ipxsap_db[i].s;
		table->addr = htons(ipxsap_db[i].v);
		table->nxt = newhnamemem(ndo);
	}
}

/*
 * Initialize the address to name translation machinery.  We map all
 * non-local IP addresses to numeric addresses if ndo->ndo_fflag is true
 * (i.e., to prevent blocking on the nameserver).  localnet is the IP address
 * of the local network.  mask is its subnet mask.
 */
void
init_addrtoname(netdissect_options *ndo, uint32_t localnet, uint32_t mask)
{
	if (ndo->ndo_fflag) {
		f_localnet = localnet;
		f_netmask = mask;
	}
	if (ndo->ndo_nflag)
		/*
		 * Simplest way to suppress names.
		 */
		return;

	init_etherarray(ndo);
	init_servarray(ndo);
	init_eprotoarray(ndo);
	init_protoidarray(ndo);
	init_ipxsaparray(ndo);
}

const char *
dnaddr_string(netdissect_options *ndo, u_short dnaddr)
{
	register struct hnamemem *tp;

	for (tp = &dnaddrtable[dnaddr & (HASHNAMESIZE-1)]; tp->nxt != NULL;
	     tp = tp->nxt)
		if (tp->addr == dnaddr)
			return (tp->name);

	tp->addr = dnaddr;
	tp->nxt = newhnamemem(ndo);
	if (ndo->ndo_nflag)
		tp->name = dnnum_string(ndo, dnaddr);
	else
		tp->name = dnname_string(ndo, dnaddr);

	return(tp->name);
}

/* Return a zero'ed hnamemem struct and cuts down on calloc() overhead */
struct hnamemem *
newhnamemem(netdissect_options *ndo)
{
	register struct hnamemem *p;
	static struct hnamemem *ptr = NULL;
	static u_int num = 0;

	if (num  <= 0) {
		num = 64;
		ptr = (struct hnamemem *)calloc(num, sizeof (*ptr));
		if (ptr == NULL)
			(*ndo->ndo_error)(ndo, "newhnamemem: calloc");
	}
	--num;
	p = ptr++;
	return (p);
}

/* Return a zero'ed h6namemem struct and cuts down on calloc() overhead */
struct h6namemem *
newh6namemem(netdissect_options *ndo)
{
	register struct h6namemem *p;
	static struct h6namemem *ptr = NULL;
	static u_int num = 0;

	if (num  <= 0) {
		num = 64;
		ptr = (struct h6namemem *)calloc(num, sizeof (*ptr));
		if (ptr == NULL)
			(*ndo->ndo_error)(ndo, "newh6namemem: calloc");
	}
	--num;
	p = ptr++;
	return (p);
}

/* Represent TCI part of the 802.1Q 4-octet tag as text. */
const char *
ieee8021q_tci_string(const uint16_t tci)
{
	static char buf[128];
	snprintf(buf, sizeof(buf), "vlan %u, p %u%s",
	         tci & 0xfff,
	         tci >> 13,
	         (tci & 0x1000) ? ", DEI" : "");
	return buf;
}
