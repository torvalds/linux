/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998
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
 * Name to id translation routines used by the scanner.
 * These functions are not time critical.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef DECNETLIB
#include <sys/types.h>
#include <netdnet/dnetdb.h>
#endif

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>

  #ifdef INET6
    /*
     * To quote the MSDN page for getaddrinfo() at
     *
     *    https://msdn.microsoft.com/en-us/library/windows/desktop/ms738520(v=vs.85).aspx
     *
     * "Support for getaddrinfo on Windows 2000 and older versions
     * The getaddrinfo function was added to the Ws2_32.dll on Windows XP and
     * later. To execute an application that uses this function on earlier
     * versions of Windows, then you need to include the Ws2tcpip.h and
     * Wspiapi.h files. When the Wspiapi.h include file is added, the
     * getaddrinfo function is defined to the WspiapiGetAddrInfo inline
     * function in the Wspiapi.h file. At runtime, the WspiapiGetAddrInfo
     * function is implemented in such a way that if the Ws2_32.dll or the
     * Wship6.dll (the file containing getaddrinfo in the IPv6 Technology
     * Preview for Windows 2000) does not include getaddrinfo, then a
     * version of getaddrinfo is implemented inline based on code in the
     * Wspiapi.h header file. This inline code will be used on older Windows
     * platforms that do not natively support the getaddrinfo function."
     *
     * We use getaddrinfo(), so we include Wspiapi.h here.
     */
    #include <wspiapi.h>
  #endif /* INET6 */
#else /* _WIN32 */
  #include <sys/param.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <sys/time.h>

  #include <netinet/in.h>

  #ifdef HAVE_ETHER_HOSTTON
    #if defined(NET_ETHERNET_H_DECLARES_ETHER_HOSTTON)
      /*
       * OK, just include <net/ethernet.h>.
       */
      #include <net/ethernet.h>
    #elif defined(NETINET_ETHER_H_DECLARES_ETHER_HOSTTON)
      /*
       * OK, just include <netinet/ether.h>
       */
      #include <netinet/ether.h>
    #elif defined(SYS_ETHERNET_H_DECLARES_ETHER_HOSTTON)
      /*
       * OK, just include <sys/ethernet.h>
       */
      #include <sys/ethernet.h>
    #elif defined(ARPA_INET_H_DECLARES_ETHER_HOSTTON)
      /*
       * OK, just include <arpa/inet.h>
       */
      #include <arpa/inet.h>
    #elif defined(NETINET_IF_ETHER_H_DECLARES_ETHER_HOSTTON)
      /*
       * OK, include <netinet/if_ether.h>, after all the other stuff we
       * need to include or define for its benefit.
       */
      #define NEED_NETINET_IF_ETHER_H
    #else
      /*
       * We'll have to declare it ourselves.
       * If <netinet/if_ether.h> defines struct ether_addr, include
       * it.  Otherwise, define it ourselves.
       */
      #ifdef HAVE_STRUCT_ETHER_ADDR
        #define NEED_NETINET_IF_ETHER_H
      #else /* HAVE_STRUCT_ETHER_ADDR */
	struct ether_addr {
		unsigned char ether_addr_octet[6];
	};
      #endif /* HAVE_STRUCT_ETHER_ADDR */
    #endif /* what declares ether_hostton() */

    #ifdef NEED_NETINET_IF_ETHER_H
      #include <net/if.h>	/* Needed on some platforms */
      #include <netinet/in.h>	/* Needed on some platforms */
      #include <netinet/if_ether.h>
    #endif /* NEED_NETINET_IF_ETHER_H */

    #ifndef HAVE_DECL_ETHER_HOSTTON
      /*
       * No header declares it, so declare it ourselves.
       */
      extern int ether_hostton(const char *, struct ether_addr *);
    #endif /* !defined(HAVE_DECL_ETHER_HOSTTON) */
  #endif /* HAVE_ETHER_HOSTTON */

  #include <arpa/inet.h>
  #include <netdb.h>
#endif /* _WIN32 */

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "pcap-int.h"

#include "gencode.h"
#include <pcap/namedb.h>
#include "nametoaddr.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#ifndef NTOHL
#define NTOHL(x) (x) = ntohl(x)
#define NTOHS(x) (x) = ntohs(x)
#endif

/*
 *  Convert host name to internet address.
 *  Return 0 upon failure.
 *  XXX - not thread-safe; don't use it inside libpcap.
 */
bpf_u_int32 **
pcap_nametoaddr(const char *name)
{
#ifndef h_addr
	static bpf_u_int32 *hlist[2];
#endif
	bpf_u_int32 **p;
	struct hostent *hp;

	if ((hp = gethostbyname(name)) != NULL) {
#ifndef h_addr
		hlist[0] = (bpf_u_int32 *)hp->h_addr;
		NTOHL(hp->h_addr);
		return hlist;
#else
		for (p = (bpf_u_int32 **)hp->h_addr_list; *p; ++p)
			NTOHL(**p);
		return (bpf_u_int32 **)hp->h_addr_list;
#endif
	}
	else
		return 0;
}

struct addrinfo *
pcap_nametoaddrinfo(const char *name)
{
	struct addrinfo hints, *res;
	int error;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;	/*not really*/
	hints.ai_protocol = IPPROTO_TCP;	/*not really*/
	error = getaddrinfo(name, NULL, &hints, &res);
	if (error)
		return NULL;
	else
		return res;
}

/*
 *  Convert net name to internet address.
 *  Return 0 upon failure.
 *  XXX - not guaranteed to be thread-safe!  See below for platforms
 *  on which it is thread-safe and on which it isn't.
 */
bpf_u_int32
pcap_nametonetaddr(const char *name)
{
#ifdef _WIN32
	/*
	 * There's no "getnetbyname()" on Windows.
	 *
	 * XXX - I guess we could use the BSD code to read
	 * C:\Windows\System32\drivers\etc/networks, assuming
	 * that's its home on all the versions of Windows
	 * we use, but that file probably just has the loopback
	 * network on 127/24 on 99 44/100% of Windows machines.
	 *
	 * (Heck, these days it probably just has that on 99 44/100%
	 * of *UN*X* machines.)
	 */
	return 0;
#else
	/*
	 * UN*X.
	 */
	struct netent *np;
  #if defined(HAVE_LINUX_GETNETBYNAME_R)
	/*
	 * We have Linux's reentrant getnetbyname_r().
	 */
	struct netent result_buf;
	char buf[1024];	/* arbitrary size */
	int h_errnoval;
	int err;

	err = getnetbyname_r(name, &result_buf, buf, sizeof buf, &np,
	    &h_errnoval);
	if (err != 0) {
		/*
		 * XXX - dynamically allocate the buffer, and make it
		 * bigger if we get ERANGE back?
		 */
		return 0;
	}
  #elif defined(HAVE_SOLARIS_IRIX_GETNETBYNAME_R)
	/*
	 * We have Solaris's and IRIX's reentrant getnetbyname_r().
	 */
	struct netent result_buf;
	char buf[1024];	/* arbitrary size */

	np = getnetbyname_r(name, &result_buf, buf, (int)sizeof buf);
  #elif defined(HAVE_AIX_GETNETBYNAME_R)
	/*
	 * We have AIX's reentrant getnetbyname_r().
	 */
	struct netent result_buf;
	struct netent_data net_data;

	if (getnetbyname_r(name, &result_buf, &net_data) == -1)
		np = NULL;
	else
		np = &result_buf;
  #else
 	/*
 	 * We don't have any getnetbyname_r(); either we have a
 	 * getnetbyname() that uses thread-specific data, in which
 	 * case we're thread-safe (sufficiently recent FreeBSD,
 	 * sufficiently recent Darwin-based OS, sufficiently recent
 	 * HP-UX, sufficiently recent Tru64 UNIX), or we have the
 	 * traditional getnetbyname() (everything else, including
 	 * current NetBSD and OpenBSD), in which case we're not
 	 * thread-safe.
 	 */
	np = getnetbyname(name);
  #endif
	if (np != NULL)
		return np->n_net;
	else
		return 0;
#endif /* _WIN32 */
}

/*
 * Convert a port name to its port and protocol numbers.
 * We assume only TCP or UDP.
 * Return 0 upon failure.
 */
int
pcap_nametoport(const char *name, int *port, int *proto)
{
	struct addrinfo hints, *res, *ai;
	int error;
	struct sockaddr_in *in4;
#ifdef INET6
	struct sockaddr_in6 *in6;
#endif
	int tcp_port = -1;
	int udp_port = -1;

	/*
	 * We check for both TCP and UDP in case there are
	 * ambiguous entries.
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	error = getaddrinfo(NULL, name, &hints, &res);
	if (error != 0) {
		if (error != EAI_NONAME) {
			/*
			 * This is a real error, not just "there's
			 * no such service name".
			 * XXX - this doesn't return an error string.
			 */
			return 0;
		}
	} else {
		/*
		 * OK, we found it.  Did it find anything?
		 */
		for (ai = res; ai != NULL; ai = ai->ai_next) {
			/*
			 * Does it have an address?
			 */
			if (ai->ai_addr != NULL) {
				/*
				 * Yes.  Get a port number; we're done.
				 */
				if (ai->ai_addr->sa_family == AF_INET) {
					in4 = (struct sockaddr_in *)ai->ai_addr;
					tcp_port = ntohs(in4->sin_port);
					break;
				}
#ifdef INET6
				if (ai->ai_addr->sa_family == AF_INET6) {
					in6 = (struct sockaddr_in6 *)ai->ai_addr;
					tcp_port = ntohs(in6->sin6_port);
					break;
				}
#endif
			}
		}
		freeaddrinfo(res);
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	error = getaddrinfo(NULL, name, &hints, &res);
	if (error != 0) {
		if (error != EAI_NONAME) {
			/*
			 * This is a real error, not just "there's
			 * no such service name".
			 * XXX - this doesn't return an error string.
			 */
			return 0;
		}
	} else {
		/*
		 * OK, we found it.  Did it find anything?
		 */
		for (ai = res; ai != NULL; ai = ai->ai_next) {
			/*
			 * Does it have an address?
			 */
			if (ai->ai_addr != NULL) {
				/*
				 * Yes.  Get a port number; we're done.
				 */
				if (ai->ai_addr->sa_family == AF_INET) {
					in4 = (struct sockaddr_in *)ai->ai_addr;
					udp_port = ntohs(in4->sin_port);
					break;
				}
#ifdef INET6
				if (ai->ai_addr->sa_family == AF_INET6) {
					in6 = (struct sockaddr_in6 *)ai->ai_addr;
					udp_port = ntohs(in6->sin6_port);
					break;
				}
#endif
			}
		}
		freeaddrinfo(res);
	}

	/*
	 * We need to check /etc/services for ambiguous entries.
	 * If we find an ambiguous entry, and it has the
	 * same port number, change the proto to PROTO_UNDEF
	 * so both TCP and UDP will be checked.
	 */
	if (tcp_port >= 0) {
		*port = tcp_port;
		*proto = IPPROTO_TCP;
		if (udp_port >= 0) {
			if (udp_port == tcp_port)
				*proto = PROTO_UNDEF;
#ifdef notdef
			else
				/* Can't handle ambiguous names that refer
				   to different port numbers. */
				warning("ambiguous port %s in /etc/services",
					name);
#endif
		}
		return 1;
	}
	if (udp_port >= 0) {
		*port = udp_port;
		*proto = IPPROTO_UDP;
		return 1;
	}
#if defined(ultrix) || defined(__osf__)
	/* Special hack in case NFS isn't in /etc/services */
	if (strcmp(name, "nfs") == 0) {
		*port = 2049;
		*proto = PROTO_UNDEF;
		return 1;
	}
#endif
	return 0;
}

/*
 * Convert a string in the form PPP-PPP, where correspond to ports, to
 * a starting and ending port in a port range.
 * Return 0 on failure.
 */
int
pcap_nametoportrange(const char *name, int *port1, int *port2, int *proto)
{
	u_int p1, p2;
	char *off, *cpy;
	int save_proto;

	if (sscanf(name, "%d-%d", &p1, &p2) != 2) {
		if ((cpy = strdup(name)) == NULL)
			return 0;

		if ((off = strchr(cpy, '-')) == NULL) {
			free(cpy);
			return 0;
		}

		*off = '\0';

		if (pcap_nametoport(cpy, port1, proto) == 0) {
			free(cpy);
			return 0;
		}
		save_proto = *proto;

		if (pcap_nametoport(off + 1, port2, proto) == 0) {
			free(cpy);
			return 0;
		}
		free(cpy);

		if (*proto != save_proto)
			*proto = PROTO_UNDEF;
	} else {
		*port1 = p1;
		*port2 = p2;
		*proto = PROTO_UNDEF;
	}

	return 1;
}

/*
 * XXX - not guaranteed to be thread-safe!  See below for platforms
 * on which it is thread-safe and on which it isn't.
 */
int
pcap_nametoproto(const char *str)
{
	struct protoent *p;
  #if defined(HAVE_LINUX_GETNETBYNAME_R)
	/*
	 * We have Linux's reentrant getprotobyname_r().
	 */
	struct protoent result_buf;
	char buf[1024];	/* arbitrary size */
	int err;

	err = getprotobyname_r(str, &result_buf, buf, sizeof buf, &p);
	if (err != 0) {
		/*
		 * XXX - dynamically allocate the buffer, and make it
		 * bigger if we get ERANGE back?
		 */
		return 0;
	}
  #elif defined(HAVE_SOLARIS_IRIX_GETNETBYNAME_R)
	/*
	 * We have Solaris's and IRIX's reentrant getprotobyname_r().
	 */
	struct protoent result_buf;
	char buf[1024];	/* arbitrary size */

	p = getprotobyname_r(str, &result_buf, buf, (int)sizeof buf);
  #elif defined(HAVE_AIX_GETNETBYNAME_R)
	/*
	 * We have AIX's reentrant getprotobyname_r().
	 */
	struct protoent result_buf;
	struct protoent_data proto_data;

	if (getprotobyname_r(str, &result_buf, &proto_data) == -1)
		p = NULL;
	else
		p = &result_buf;
  #else
 	/*
 	 * We don't have any getprotobyname_r(); either we have a
 	 * getprotobyname() that uses thread-specific data, in which
 	 * case we're thread-safe (sufficiently recent FreeBSD,
 	 * sufficiently recent Darwin-based OS, sufficiently recent
 	 * HP-UX, sufficiently recent Tru64 UNIX, Windows), or we have
	 * the traditional getprotobyname() (everything else, including
 	 * current NetBSD and OpenBSD), in which case we're not
 	 * thread-safe.
 	 */
	p = getprotobyname(str);
  #endif
	if (p != 0)
		return p->p_proto;
	else
		return PROTO_UNDEF;
}

#include "ethertype.h"

struct eproto {
	const char *s;
	u_short p;
};

/*
 * Static data base of ether protocol types.
 * tcpdump used to import this, and it's declared as an export on
 * Debian, at least, so make it a public symbol, even though we
 * don't officially export it by declaring it in a header file.
 * (Programs *should* do this themselves, as tcpdump now does.)
 *
 * We declare it here, right before defining it, to squelch any
 * warnings we might get from compilers about the lack of a
 * declaration.
 */
PCAP_API struct eproto eproto_db[];
PCAP_API_DEF struct eproto eproto_db[] = {
	{ "pup", ETHERTYPE_PUP },
	{ "xns", ETHERTYPE_NS },
	{ "ip", ETHERTYPE_IP },
#ifdef INET6
	{ "ip6", ETHERTYPE_IPV6 },
#endif
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

int
pcap_nametoeproto(const char *s)
{
	struct eproto *p = eproto_db;

	while (p->s != 0) {
		if (strcmp(p->s, s) == 0)
			return p->p;
		p += 1;
	}
	return PROTO_UNDEF;
}

#include "llc.h"

/* Static data base of LLC values. */
static struct eproto llc_db[] = {
	{ "iso", LLCSAP_ISONS },
	{ "stp", LLCSAP_8021D },
	{ "ipx", LLCSAP_IPX },
	{ "netbeui", LLCSAP_NETBEUI },
	{ (char *)0, 0 }
};

int
pcap_nametollc(const char *s)
{
	struct eproto *p = llc_db;

	while (p->s != 0) {
		if (strcmp(p->s, s) == 0)
			return p->p;
		p += 1;
	}
	return PROTO_UNDEF;
}

/* Hex digit to 8-bit unsigned integer. */
static inline u_char
xdtoi(u_char c)
{
	if (isdigit(c))
		return (u_char)(c - '0');
	else if (islower(c))
		return (u_char)(c - 'a' + 10);
	else
		return (u_char)(c - 'A' + 10);
}

int
__pcap_atoin(const char *s, bpf_u_int32 *addr)
{
	u_int n;
	int len;

	*addr = 0;
	len = 0;
	for (;;) {
		n = 0;
		while (*s && *s != '.')
			n = n * 10 + *s++ - '0';
		*addr <<= 8;
		*addr |= n & 0xff;
		len += 8;
		if (*s == '\0')
			return len;
		++s;
	}
	/* NOTREACHED */
}

int
__pcap_atodn(const char *s, bpf_u_int32 *addr)
{
#define AREASHIFT 10
#define AREAMASK 0176000
#define NODEMASK 01777

	u_int node, area;

	if (sscanf(s, "%d.%d", &area, &node) != 2)
		return(0);

	*addr = (area << AREASHIFT) & AREAMASK;
	*addr |= (node & NODEMASK);

	return(32);
}

/*
 * Convert 's', which can have the one of the forms:
 *
 *	"xx:xx:xx:xx:xx:xx"
 *	"xx.xx.xx.xx.xx.xx"
 *	"xx-xx-xx-xx-xx-xx"
 *	"xxxx.xxxx.xxxx"
 *	"xxxxxxxxxxxx"
 *
 * (or various mixes of ':', '.', and '-') into a new
 * ethernet address.  Assumes 's' is well formed.
 */
u_char *
pcap_ether_aton(const char *s)
{
	register u_char *ep, *e;
	register u_char d;

	e = ep = (u_char *)malloc(6);
	if (e == NULL)
		return (NULL);

	while (*s) {
		if (*s == ':' || *s == '.' || *s == '-')
			s += 1;
		d = xdtoi(*s++);
		if (isxdigit((unsigned char)*s)) {
			d <<= 4;
			d |= xdtoi(*s++);
		}
		*ep++ = d;
	}

	return (e);
}

#ifndef HAVE_ETHER_HOSTTON
/*
 * Roll our own.
 * XXX - not thread-safe, because pcap_next_etherent() isn't thread-
 * safe!  Needs a mutex or a thread-safe pcap_next_etherent().
 */
u_char *
pcap_ether_hostton(const char *name)
{
	register struct pcap_etherent *ep;
	register u_char *ap;
	static FILE *fp = NULL;
	static int init = 0;

	if (!init) {
		fp = fopen(PCAP_ETHERS_FILE, "r");
		++init;
		if (fp == NULL)
			return (NULL);
	} else if (fp == NULL)
		return (NULL);
	else
		rewind(fp);

	while ((ep = pcap_next_etherent(fp)) != NULL) {
		if (strcmp(ep->name, name) == 0) {
			ap = (u_char *)malloc(6);
			if (ap != NULL) {
				memcpy(ap, ep->addr, 6);
				return (ap);
			}
			break;
		}
	}
	return (NULL);
}
#else
/*
 * Use the OS-supplied routine.
 * This *should* be thread-safe; the API doesn't have a static buffer.
 */
u_char *
pcap_ether_hostton(const char *name)
{
	register u_char *ap;
	u_char a[6];

	ap = NULL;
	if (ether_hostton(name, (struct ether_addr *)a) == 0) {
		ap = (u_char *)malloc(6);
		if (ap != NULL)
			memcpy((char *)ap, (char *)a, 6);
	}
	return (ap);
}
#endif

/*
 * XXX - not guaranteed to be thread-safe!
 */
int
#ifdef	DECNETLIB
__pcap_nametodnaddr(const char *name, u_short *res)
{
	struct nodeent *getnodebyname();
	struct nodeent *nep;

	nep = getnodebyname(name);
	if (nep == ((struct nodeent *)0))
		return(0);

	memcpy((char *)res, (char *)nep->n_addr, sizeof(unsigned short));
	return(1);
#else
__pcap_nametodnaddr(const char *name _U_, u_short *res _U_)
{
	return(0);
#endif
}
