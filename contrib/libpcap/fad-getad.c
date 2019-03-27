/* -*- Mode: c; tab-width: 8; indent-tabs-mode: 1; c-basic-offset: 8; -*- */
/*
 * Copyright (c) 1994, 1995, 1996, 1997, 1998
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <net/if.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ifaddrs.h>

#include "pcap-int.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

/*
 * We don't do this on Solaris 11 and later, as it appears there aren't
 * any AF_PACKET addresses on interfaces, so we don't need this, and
 * we end up including both the OS's <net/bpf.h> and our <pcap/bpf.h>,
 * and their definitions of some data structures collide.
 */
#if (defined(linux) || defined(__Lynx__)) && defined(AF_PACKET)
# ifdef HAVE_NETPACKET_PACKET_H
/* Linux distributions with newer glibc */
#  include <netpacket/packet.h>
# else /* HAVE_NETPACKET_PACKET_H */
/* LynxOS, Linux distributions with older glibc */
# ifdef __Lynx__
/* LynxOS */
#  include <netpacket/if_packet.h>
# else /* __Lynx__ */
/* Linux */
#  include <linux/types.h>
#  include <linux/if_packet.h>
# endif /* __Lynx__ */
# endif /* HAVE_NETPACKET_PACKET_H */
#endif /* (defined(linux) || defined(__Lynx__)) && defined(AF_PACKET) */

/*
 * This is fun.
 *
 * In older BSD systems, socket addresses were fixed-length, and
 * "sizeof (struct sockaddr)" gave the size of the structure.
 * All addresses fit within a "struct sockaddr".
 *
 * In newer BSD systems, the socket address is variable-length, and
 * there's an "sa_len" field giving the length of the structure;
 * this allows socket addresses to be longer than 2 bytes of family
 * and 14 bytes of data.
 *
 * Some commercial UNIXes use the old BSD scheme, some use the RFC 2553
 * variant of the old BSD scheme (with "struct sockaddr_storage" rather
 * than "struct sockaddr"), and some use the new BSD scheme.
 *
 * Some versions of GNU libc use neither scheme, but has an "SA_LEN()"
 * macro that determines the size based on the address family.  Other
 * versions don't have "SA_LEN()" (as it was in drafts of RFC 2553
 * but not in the final version).  On the latter systems, we explicitly
 * check the AF_ type to determine the length; we assume that on
 * all those systems we have "struct sockaddr_storage".
 */
#ifndef SA_LEN
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
#define SA_LEN(addr)	((addr)->sa_len)
#else /* HAVE_STRUCT_SOCKADDR_SA_LEN */
#ifdef HAVE_STRUCT_SOCKADDR_STORAGE
static size_t
get_sa_len(struct sockaddr *addr)
{
	switch (addr->sa_family) {

#ifdef AF_INET
	case AF_INET:
		return (sizeof (struct sockaddr_in));
#endif

#ifdef AF_INET6
	case AF_INET6:
		return (sizeof (struct sockaddr_in6));
#endif

#if (defined(linux) || defined(__Lynx__)) && defined(AF_PACKET)
	case AF_PACKET:
		return (sizeof (struct sockaddr_ll));
#endif

	default:
		return (sizeof (struct sockaddr));
	}
}
#define SA_LEN(addr)	(get_sa_len(addr))
#else /* HAVE_STRUCT_SOCKADDR_STORAGE */
#define SA_LEN(addr)	(sizeof (struct sockaddr))
#endif /* HAVE_STRUCT_SOCKADDR_STORAGE */
#endif /* HAVE_STRUCT_SOCKADDR_SA_LEN */
#endif /* SA_LEN */

/*
 * Get a list of all interfaces that are up and that we can open.
 * Returns -1 on error, 0 otherwise.
 * The list, as returned through "alldevsp", may be null if no interfaces
 * could be opened.
 */
int
pcap_findalldevs_interfaces(pcap_if_list_t *devlistp, char *errbuf,
    int (*check_usable)(const char *), get_if_flags_func get_flags_func)
{
	struct ifaddrs *ifap, *ifa;
	struct sockaddr *addr, *netmask, *broadaddr, *dstaddr;
	size_t addr_size, broadaddr_size, dstaddr_size;
	int ret = 0;
	char *p, *q;

	/*
	 * Get the list of interface addresses.
	 *
	 * Note: this won't return information about interfaces
	 * with no addresses, so, if a platform has interfaces
	 * with no interfaces on which traffic can be captured,
	 * we must check for those interfaces as well (see, for
	 * example, what's done on Linux).
	 *
	 * LAN interfaces will probably have link-layer
	 * addresses; I don't know whether all implementations
	 * of "getifaddrs()" now, or in the future, will return
	 * those.
	 */
	if (getifaddrs(&ifap) != 0) {
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "getifaddrs");
		return (-1);
	}
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		/*
		 * If this entry has a colon followed by a number at
		 * the end, we assume it's a logical interface.  Those
		 * are just the way you assign multiple IP addresses to
		 * a real interface on Linux, so an entry for a logical
		 * interface should be treated like the entry for the
		 * real interface; we do that by stripping off the ":"
		 * and the number.
		 *
		 * XXX - should we do this only on Linux?
		 */
		p = strchr(ifa->ifa_name, ':');
		if (p != NULL) {
			/*
			 * We have a ":"; is it followed by a number?
			 */
			q = p + 1;
			while (isdigit((unsigned char)*q))
				q++;
			if (*q == '\0') {
				/*
				 * All digits after the ":" until the end.
				 * Strip off the ":" and everything after
				 * it.
				 */
			       *p = '\0';
			}
		}

		/*
		 * Can we capture on this device?
		 */
		if (!(*check_usable)(ifa->ifa_name)) {
			/*
			 * No.
			 */
			continue;
		}

		/*
		 * "ifa_addr" was apparently null on at least one
		 * interface on some system.  Therefore, we supply
		 * the address and netmask only if "ifa_addr" is
		 * non-null (if there's no address, there's obviously
		 * no netmask).
		 */
		if (ifa->ifa_addr != NULL) {
			addr = ifa->ifa_addr;
			addr_size = SA_LEN(addr);
			netmask = ifa->ifa_netmask;
		} else {
			addr = NULL;
			addr_size = 0;
			netmask = NULL;
		}

		/*
		 * Note that, on some platforms, ifa_broadaddr and
		 * ifa_dstaddr could be the same field (true on at
		 * least some versions of *BSD and macOS), so we
		 * can't just check whether the broadcast address
		 * is null and add it if so and check whether the
		 * destination address is null and add it if so.
		 *
		 * Therefore, we must also check the IFF_BROADCAST
		 * flag, and only add a broadcast address if it's
		 * set, and check the IFF_POINTTOPOINT flag, and
		 * only add a destination address if it's set (as
		 * per man page recommendations on some of those
		 * platforms).
		 */
		if (ifa->ifa_flags & IFF_BROADCAST &&
		    ifa->ifa_broadaddr != NULL) {
			broadaddr = ifa->ifa_broadaddr;
			broadaddr_size = SA_LEN(broadaddr);
		} else {
			broadaddr = NULL;
			broadaddr_size = 0;
		}
		if (ifa->ifa_flags & IFF_POINTOPOINT &&
		    ifa->ifa_dstaddr != NULL) {
			dstaddr = ifa->ifa_dstaddr;
			dstaddr_size = SA_LEN(ifa->ifa_dstaddr);
		} else {
			dstaddr = NULL;
			dstaddr_size = 0;
		}

		/*
		 * Add information for this address to the list.
		 */
		if (add_addr_to_if(devlistp, ifa->ifa_name, ifa->ifa_flags,
		    get_flags_func,
		    addr, addr_size, netmask, addr_size,
		    broadaddr, broadaddr_size, dstaddr, dstaddr_size,
		    errbuf) < 0) {
			ret = -1;
			break;
		}
	}

	freeifaddrs(ifap);

	return (ret);
}
