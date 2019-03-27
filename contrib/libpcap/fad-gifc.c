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

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#include <sys/time.h>				/* concession to AIX */

struct mbuf;		/* Squelch compiler warnings on some platforms for */
struct rtentry;		/* declarations in <net/if.h> */
#include <net/if.h>
#include <netinet/in.h>

#include <ctype.h>
#include <errno.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_LIMITS_H
#include <limits.h>
#else
#define INT_MAX		2147483647
#endif

#include "pcap-int.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

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
 * but not in the final version).
 *
 * We assume that a UNIX that doesn't have "getifaddrs()" and doesn't have
 * SIOCGLIFCONF, but has SIOCGIFCONF, uses "struct sockaddr" for the
 * address in an entry returned by SIOCGIFCONF.
 */
#ifndef SA_LEN
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
#define SA_LEN(addr)	((addr)->sa_len)
#else /* HAVE_STRUCT_SOCKADDR_SA_LEN */
#define SA_LEN(addr)	(sizeof (struct sockaddr))
#endif /* HAVE_STRUCT_SOCKADDR_SA_LEN */
#endif /* SA_LEN */

/*
 * This is also fun.
 *
 * There is no ioctl that returns the amount of space required for all
 * the data that SIOCGIFCONF could return, and if a buffer is supplied
 * that's not large enough for all the data SIOCGIFCONF could return,
 * on at least some platforms it just returns the data that'd fit with
 * no indication that there wasn't enough room for all the data, much
 * less an indication of how much more room is required.
 *
 * The only way to ensure that we got all the data is to pass a buffer
 * large enough that the amount of space in the buffer *not* filled in
 * is greater than the largest possible entry.
 *
 * We assume that's "sizeof(ifreq.ifr_name)" plus 255, under the assumption
 * that no address is more than 255 bytes (on systems where the "sa_len"
 * field in a "struct sockaddr" is 1 byte, e.g. newer BSDs, that's the
 * case, and addresses are unlikely to be bigger than that in any case).
 */
#define MAX_SA_LEN	255

/*
 * Get a list of all interfaces that are up and that we can open.
 * Returns -1 on error, 0 otherwise.
 * The list, as returned through "alldevsp", may be null if no interfaces
 * were up and could be opened.
 *
 * This is the implementation used on platforms that have SIOCGIFCONF but
 * don't have any other mechanism for getting a list of interfaces.
 *
 * XXX - or platforms that have other, better mechanisms but for which
 * we don't yet have code to use that mechanism; I think there's a better
 * way on Linux, for example, but if that better way is "getifaddrs()",
 * we already have that.
 */
int
pcap_findalldevs_interfaces(pcap_if_list_t *devlistp, char *errbuf,
    int (*check_usable)(const char *), get_if_flags_func get_flags_func)
{
	register int fd;
	register struct ifreq *ifrp, *ifend, *ifnext;
	size_t n;
	struct ifconf ifc;
	char *buf = NULL;
	unsigned buf_size;
#if defined (HAVE_SOLARIS) || defined (HAVE_HPUX10_20_OR_LATER)
	char *p, *q;
#endif
	struct ifreq ifrflags, ifrnetmask, ifrbroadaddr, ifrdstaddr;
	struct sockaddr *netmask, *broadaddr, *dstaddr;
	size_t netmask_size, broadaddr_size, dstaddr_size;
	int ret = 0;

	/*
	 * Create a socket from which to fetch the list of interfaces.
	 */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "socket");
		return (-1);
	}

	/*
	 * Start with an 8K buffer, and keep growing the buffer until
	 * we have more than "sizeof(ifrp->ifr_name) + MAX_SA_LEN"
	 * bytes left over in the buffer or we fail to get the
	 * interface list for some reason other than EINVAL (which is
	 * presumed here to mean "buffer is too small").
	 */
	buf_size = 8192;
	for (;;) {
		/*
		 * Don't let the buffer size get bigger than INT_MAX.
		 */
		if (buf_size > INT_MAX) {
			(void)pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "interface information requires more than %u bytes",
			    INT_MAX);
			(void)close(fd);
			return (-1);
		}
		buf = malloc(buf_size);
		if (buf == NULL) {
			pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
			    errno, "malloc");
			(void)close(fd);
			return (-1);
		}

		ifc.ifc_len = buf_size;
		ifc.ifc_buf = buf;
		memset(buf, 0, buf_size);
		if (ioctl(fd, SIOCGIFCONF, (char *)&ifc) < 0
		    && errno != EINVAL) {
			pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
			    errno, "SIOCGIFCONF");
			(void)close(fd);
			free(buf);
			return (-1);
		}
		if (ifc.ifc_len < (int)buf_size &&
		    (buf_size - ifc.ifc_len) > sizeof(ifrp->ifr_name) + MAX_SA_LEN)
			break;
		free(buf);
		buf_size *= 2;
	}

	ifrp = (struct ifreq *)buf;
	ifend = (struct ifreq *)(buf + ifc.ifc_len);

	for (; ifrp < ifend; ifrp = ifnext) {
		/*
		 * XXX - what if this isn't an IPv4 address?  Can
		 * we still get the netmask, etc. with ioctls on
		 * an IPv4 socket?
		 *
		 * The answer is probably platform-dependent, and
		 * if the answer is "no" on more than one platform,
		 * the way you work around it is probably platform-
		 * dependent as well.
		 */
		n = SA_LEN(&ifrp->ifr_addr) + sizeof(ifrp->ifr_name);
		if (n < sizeof(*ifrp))
			ifnext = ifrp + 1;
		else
			ifnext = (struct ifreq *)((char *)ifrp + n);

		/*
		 * XXX - The 32-bit compatibility layer for Linux on IA-64
		 * is slightly broken. It correctly converts the structures
		 * to and from kernel land from 64 bit to 32 bit but
		 * doesn't update ifc.ifc_len, leaving it larger than the
		 * amount really used. This means we read off the end
		 * of the buffer and encounter an interface with an
		 * "empty" name. Since this is highly unlikely to ever
		 * occur in a valid case we can just finish looking for
		 * interfaces if we see an empty name.
		 */
		if (!(*ifrp->ifr_name))
			break;

		/*
		 * Skip entries that begin with "dummy".
		 * XXX - what are these?  Is this Linux-specific?
		 * Are there platforms on which we shouldn't do this?
		 */
		if (strncmp(ifrp->ifr_name, "dummy", 5) == 0)
			continue;

		/*
		 * Can we capture on this device?
		 */
		if (!(*check_usable)(ifrp->ifr_name)) {
			/*
			 * No.
			 */
			continue;
		}

		/*
		 * Get the flags for this interface.
		 */
		strncpy(ifrflags.ifr_name, ifrp->ifr_name,
		    sizeof(ifrflags.ifr_name));
		if (ioctl(fd, SIOCGIFFLAGS, (char *)&ifrflags) < 0) {
			if (errno == ENXIO)
				continue;
			pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
			    errno, "SIOCGIFFLAGS: %.*s",
			    (int)sizeof(ifrflags.ifr_name),
			    ifrflags.ifr_name);
			ret = -1;
			break;
		}

		/*
		 * Get the netmask for this address on this interface.
		 */
		strncpy(ifrnetmask.ifr_name, ifrp->ifr_name,
		    sizeof(ifrnetmask.ifr_name));
		memcpy(&ifrnetmask.ifr_addr, &ifrp->ifr_addr,
		    sizeof(ifrnetmask.ifr_addr));
		if (ioctl(fd, SIOCGIFNETMASK, (char *)&ifrnetmask) < 0) {
			if (errno == EADDRNOTAVAIL) {
				/*
				 * Not available.
				 */
				netmask = NULL;
				netmask_size = 0;
			} else {
				pcap_fmt_errmsg_for_errno(errbuf,
				    PCAP_ERRBUF_SIZE, errno,
				    "SIOCGIFNETMASK: %.*s",
				    (int)sizeof(ifrnetmask.ifr_name),
				    ifrnetmask.ifr_name);
				ret = -1;
				break;
			}
		} else {
			netmask = &ifrnetmask.ifr_addr;
			netmask_size = SA_LEN(netmask);
		}

		/*
		 * Get the broadcast address for this address on this
		 * interface (if any).
		 */
		if (ifrflags.ifr_flags & IFF_BROADCAST) {
			strncpy(ifrbroadaddr.ifr_name, ifrp->ifr_name,
			    sizeof(ifrbroadaddr.ifr_name));
			memcpy(&ifrbroadaddr.ifr_addr, &ifrp->ifr_addr,
			    sizeof(ifrbroadaddr.ifr_addr));
			if (ioctl(fd, SIOCGIFBRDADDR,
			    (char *)&ifrbroadaddr) < 0) {
				if (errno == EADDRNOTAVAIL) {
					/*
					 * Not available.
					 */
					broadaddr = NULL;
					broadaddr_size = 0;
				} else {
					pcap_fmt_errmsg_for_errno(errbuf,
					    PCAP_ERRBUF_SIZE, errno,
					    "SIOCGIFBRDADDR: %.*s",
					    (int)sizeof(ifrbroadaddr.ifr_name),
					    ifrbroadaddr.ifr_name);
					ret = -1;
					break;
				}
			} else {
				broadaddr = &ifrbroadaddr.ifr_broadaddr;
				broadaddr_size = SA_LEN(broadaddr);
			}
		} else {
			/*
			 * Not a broadcast interface, so no broadcast
			 * address.
			 */
			broadaddr = NULL;
			broadaddr_size = 0;
		}

		/*
		 * Get the destination address for this address on this
		 * interface (if any).
		 */
		if (ifrflags.ifr_flags & IFF_POINTOPOINT) {
			strncpy(ifrdstaddr.ifr_name, ifrp->ifr_name,
			    sizeof(ifrdstaddr.ifr_name));
			memcpy(&ifrdstaddr.ifr_addr, &ifrp->ifr_addr,
			    sizeof(ifrdstaddr.ifr_addr));
			if (ioctl(fd, SIOCGIFDSTADDR,
			    (char *)&ifrdstaddr) < 0) {
				if (errno == EADDRNOTAVAIL) {
					/*
					 * Not available.
					 */
					dstaddr = NULL;
					dstaddr_size = 0;
				} else {
					pcap_fmt_errmsg_for_errno(errbuf,
					    PCAP_ERRBUF_SIZE, errno,
					    "SIOCGIFDSTADDR: %.*s",
					    (int)sizeof(ifrdstaddr.ifr_name),
					    ifrdstaddr.ifr_name);
					ret = -1;
					break;
				}
			} else {
				dstaddr = &ifrdstaddr.ifr_dstaddr;
				dstaddr_size = SA_LEN(dstaddr);
			}
		} else {
			/*
			 * Not a point-to-point interface, so no destination
			 * address.
			 */
			dstaddr = NULL;
			dstaddr_size = 0;
		}

#if defined (HAVE_SOLARIS) || defined (HAVE_HPUX10_20_OR_LATER)
		/*
		 * If this entry has a colon followed by a number at
		 * the end, it's a logical interface.  Those are just
		 * the way you assign multiple IP addresses to a real
		 * interface, so an entry for a logical interface should
		 * be treated like the entry for the real interface;
		 * we do that by stripping off the ":" and the number.
		 */
		p = strchr(ifrp->ifr_name, ':');
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
#endif

		/*
		 * Add information for this address to the list.
		 */
		if (add_addr_to_if(devlistp, ifrp->ifr_name,
		    ifrflags.ifr_flags, get_flags_func,
		    &ifrp->ifr_addr, SA_LEN(&ifrp->ifr_addr),
		    netmask, netmask_size, broadaddr, broadaddr_size,
		    dstaddr, dstaddr_size, errbuf) < 0) {
			ret = -1;
			break;
		}
	}
	free(buf);
	(void)close(fd);

	return (ret);
}
