/*
 * ntp_net.h - definitions for NTP network stuff
 */

#ifndef NTP_NET_H
#define NTP_NET_H

#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NET_IF_VAR_H
#include <net/if_var.h>
#endif
#ifdef HAVE_NETINET_IN_VAR_H
#include <netinet/in_var.h>
#endif

#include "ntp_rfc2553.h"
#include "ntp_malloc.h"

typedef union {
	struct sockaddr		sa;
	struct sockaddr_in	sa4;
	struct sockaddr_in6	sa6;
} sockaddr_u;

/*
 * Utilities for manipulating sockaddr_u v4/v6 unions
 */
#define SOCK_ADDR4(psau)	((psau)->sa4.sin_addr)
#define SOCK_ADDR6(psau)	((psau)->sa6.sin6_addr)

#define PSOCK_ADDR4(psau)	(&SOCK_ADDR4(psau))
#define PSOCK_ADDR6(psau)	(&SOCK_ADDR6(psau))

#define AF(psau)		((psau)->sa.sa_family)

#define IS_IPV4(psau)		(AF_INET == AF(psau))
#define IS_IPV6(psau)		(AF_INET6 == AF(psau))

/* sockaddr_u v4 address in network byte order */
#define	NSRCADR(psau)		(SOCK_ADDR4(psau).s_addr)

/* sockaddr_u v4 address in host byte order */
#define	SRCADR(psau)		(ntohl(NSRCADR(psau)))

/* sockaddr_u v6 address in network byte order */
#define NSRCADR6(psau)		(SOCK_ADDR6(psau).s6_addr)

/* assign sockaddr_u v4 address from host byte order */
#define	SET_ADDR4(psau, addr4)	(NSRCADR(psau) = htonl(addr4))

/* assign sockaddr_u v4 address from network byte order */
#define SET_ADDR4N(psau, addr4n) (NSRCADR(psau) = (addr4n));

/* assign sockaddr_u v6 address from network byte order */
#define SET_ADDR6N(psau, s6_addr)				\
	(SOCK_ADDR6(psau) = (s6_addr))

/* sockaddr_u v4/v6 port in network byte order */
#define	NSRCPORT(psau)		((psau)->sa4.sin_port)

/* sockaddr_u v4/v6 port in host byte order */
#define	SRCPORT(psau)		(ntohs(NSRCPORT(psau)))

/* assign sockaddr_u v4/v6 port from host byte order */
#define SET_PORT(psau, port)	(NSRCPORT(psau) = htons(port))

/* sockaddr_u v6 scope */
#define SCOPE_VAR(psau)		((psau)->sa6.sin6_scope_id)

#ifdef ISC_PLATFORM_HAVESCOPEID
/* v4/v6 scope (always zero for v4) */
# define SCOPE(psau)		(IS_IPV4(psau)			\
				    ? 0				\
				    : SCOPE_VAR(psau))

/* are two v6 sockaddr_u scopes equal? */
# define SCOPE_EQ(psau1, psau2)					\
	(SCOPE_VAR(psau1) == SCOPE_VAR(psau2))

/* assign scope if supported */
# define SET_SCOPE(psau, s)					\
	do							\
		if (IS_IPV6(psau))				\
			SCOPE_VAR(psau) = (s);			\
	while (0)
#else	/* ISC_PLATFORM_HAVESCOPEID not defined */
# define SCOPE(psau)		(0)
# define SCOPE_EQ(psau1, psau2)	(1)
# define SET_SCOPE(psau, s)	do { } while (0)
#endif	/* ISC_PLATFORM_HAVESCOPEID */

/* v4/v6 is multicast address */
#define IS_MCAST(psau)						\
	(IS_IPV4(psau)						\
	    ? IN_CLASSD(SRCADR(psau))				\
	    : IN6_IS_ADDR_MULTICAST(PSOCK_ADDR6(psau)))

/* v6 is interface ID scope universal, as with MAC-derived addresses */
#define IS_IID_UNIV(psau)					\
	(!!(0x02 & NSRCADR6(psau)[8]))

#define SIZEOF_INADDR(fam)					\
	((AF_INET == (fam))					\
	    ? sizeof(struct in_addr)				\
	    : sizeof(struct in6_addr))

#define SIZEOF_SOCKADDR(fam)					\
	((AF_INET == (fam))					\
	    ? sizeof(struct sockaddr_in)			\
	    : sizeof(struct sockaddr_in6))

#define SOCKLEN(psau)						\
	(IS_IPV4(psau)						\
	    ? sizeof((psau)->sa4)				\
	    : sizeof((psau)->sa6))

#define ZERO_SOCK(psau)						\
	ZERO(*(psau))

/* blast a byte value across sockaddr_u v6 address */
#define	MEMSET_ADDR6(psau, v)					\
	memset((psau)->sa6.sin6_addr.s6_addr, (v),		\
		sizeof((psau)->sa6.sin6_addr.s6_addr))

#define SET_ONESMASK(psau)					\
	do {							\
		if (IS_IPV6(psau))				\
			MEMSET_ADDR6((psau), 0xff);		\
		else						\
			NSRCADR(psau) = 0xffffffff;		\
	} while(0)

/* zero sockaddr_u, fill in family and all-ones (host) mask */
#define SET_HOSTMASK(psau, family)				\
	do {							\
		ZERO_SOCK(psau);				\
		AF(psau) = (family);				\
		SET_ONESMASK(psau);				\
	} while (0)

/* 
 * compare two in6_addr returning negative, 0, or positive.
 * ADDR6_CMP is negative if *pin6A is lower than *pin6B, zero if they
 * are equal, positive if *pin6A is higher than *pin6B.  IN6ADDR_ANY
 * is the lowest address (128 zero bits).
 */
#define	ADDR6_CMP(pin6A, pin6B)					\
	memcmp((pin6A)->s6_addr, (pin6B)->s6_addr,		\
	       sizeof(pin6A)->s6_addr)

/* compare two in6_addr for equality only */
#if !defined(SYS_WINNT) || !defined(in_addr6)
#define ADDR6_EQ(pin6A, pin6B)					\
	(!ADDR6_CMP(pin6A, pin6B))
#else
#define ADDR6_EQ(pin6A, pin6B)					\
	IN6_ADDR_EQUAL(pin6A, pin6B)
#endif

/* compare a in6_addr with socket address */
#define	S_ADDR6_EQ(psau, pin6)					\
	ADDR6_EQ(&(psau)->sa6.sin6_addr, pin6)

/* are two sockaddr_u's addresses equal? (port excluded) */
#define SOCK_EQ(psau1, psau2)					\
	((AF(psau1) != AF(psau2))				\
	     ? 0						\
	     : IS_IPV4(psau1)					\
		   ? (NSRCADR(psau1) == NSRCADR(psau2))		\
		   : (S_ADDR6_EQ((psau1), PSOCK_ADDR6(psau2))	\
		      && SCOPE_EQ((psau1), (psau2))))

/* are two sockaddr_u's addresses and ports equal? */
#define ADDR_PORT_EQ(psau1, psau2)				\
	((NSRCPORT(psau1) != NSRCPORT(psau2)			\
	     ? 0						\
	     : SOCK_EQ((psau1), (psau2))))

/* is sockaddr_u address unspecified? */
#define SOCK_UNSPEC(psau)					\
	(IS_IPV4(psau)						\
	    ? !NSRCADR(psau)					\
	    : IN6_IS_ADDR_UNSPECIFIED(PSOCK_ADDR6(psau)))

/* just how unspecified do you mean? (scope 0/unspec too) */
#define SOCK_UNSPEC_S(psau)					\
	(SOCK_UNSPEC(psau) && !SCOPE(psau))

/* choose a default net interface (struct interface) for v4 or v6 */
#define ANY_INTERFACE_BYFAM(family)				\
	((AF_INET == family)					\
	     ? any_interface					\
	     : any6_interface)

/* choose a default interface for addresses' protocol (addr family) */
#define ANY_INTERFACE_CHOOSE(psau)				\
	ANY_INTERFACE_BYFAM(AF(psau))


/*
 * We tell reference clocks from real peers by giving the reference
 * clocks an address of the form 127.127.t.u, where t is the type and
 * u is the unit number.  We define some of this here since we will need
 * some sanity checks to make sure this address isn't interpretted as
 * that of a normal peer.
 */
#define	REFCLOCK_ADDR	0x7f7f0000	/* 127.127.0.0 */
#define	REFCLOCK_MASK	0xffff0000	/* 255.255.0.0 */

#define	ISREFCLOCKADR(srcadr)					\
	(IS_IPV4(srcadr) &&					\
	 (SRCADR(srcadr) & REFCLOCK_MASK) == REFCLOCK_ADDR)

/*
 * Macro for checking for invalid addresses.  This is really, really
 * gross, but is needed so no one configures a host on net 127 now that
 * we're encouraging it the the configuration file.
 */
#define	LOOPBACKADR	0x7f000001
#define	LOOPNETMASK	0xff000000

#define	ISBADADR(srcadr)					\
	(IS_IPV4(srcadr)					\
	 && ((SRCADR(srcadr) & LOOPNETMASK)			\
	     == (LOOPBACKADR & LOOPNETMASK))			\
	 && SRCADR(srcadr) != LOOPBACKADR)


#endif /* NTP_NET_H */
