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
#include <sys/file.h>
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

#include "pcap-int.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

/*
 * Get a list of all interfaces that are up and that we can open.
 * Returns -1 on error, 0 otherwise.
 * The list, as returned through "alldevsp", may be null if no interfaces
 * were up and could be opened.
 *
 * This is the implementation used on platforms that have SIOCGLIFCONF
 * but don't have "getifaddrs()".  (Solaris 8 and later; we use
 * SIOCGLIFCONF rather than SIOCGIFCONF in order to get IPv6 addresses.)
 */
int
pcap_findalldevs_interfaces(pcap_if_list_t *devlistp, char *errbuf,
    int (*check_usable)(const char *), get_if_flags_func get_flags_func)
{
	register int fd4, fd6, fd;
	register struct lifreq *ifrp, *ifend;
	struct lifnum ifn;
	struct lifconf ifc;
	char *buf = NULL;
	unsigned buf_size;
#ifdef HAVE_SOLARIS
	char *p, *q;
#endif
	struct lifreq ifrflags, ifrnetmask, ifrbroadaddr, ifrdstaddr;
	struct sockaddr *netmask, *broadaddr, *dstaddr;
	int ret = 0;

	/*
	 * Create a socket from which to fetch the list of interfaces,
	 * and from which to fetch IPv4 information.
	 */
	fd4 = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd4 < 0) {
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "socket: AF_INET");
		return (-1);
	}

	/*
	 * Create a socket from which to fetch IPv6 information.
	 */
	fd6 = socket(AF_INET6, SOCK_DGRAM, 0);
	if (fd6 < 0) {
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "socket: AF_INET6");
		(void)close(fd4);
		return (-1);
	}

	/*
	 * How many entries will SIOCGLIFCONF return?
	 */
	ifn.lifn_family = AF_UNSPEC;
	ifn.lifn_flags = 0;
	ifn.lifn_count = 0;
	if (ioctl(fd4, SIOCGLIFNUM, (char *)&ifn) < 0) {
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "SIOCGLIFNUM");
		(void)close(fd6);
		(void)close(fd4);
		return (-1);
	}

	/*
	 * Allocate a buffer for those entries.
	 */
	buf_size = ifn.lifn_count * sizeof (struct lifreq);
	buf = malloc(buf_size);
	if (buf == NULL) {
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "malloc");
		(void)close(fd6);
		(void)close(fd4);
		return (-1);
	}

	/*
	 * Get the entries.
	 */
	ifc.lifc_len = buf_size;
	ifc.lifc_buf = buf;
	ifc.lifc_family = AF_UNSPEC;
	ifc.lifc_flags = 0;
	memset(buf, 0, buf_size);
	if (ioctl(fd4, SIOCGLIFCONF, (char *)&ifc) < 0) {
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "SIOCGLIFCONF");
		(void)close(fd6);
		(void)close(fd4);
		free(buf);
		return (-1);
	}

	/*
	 * Loop over the entries.
	 */
	ifrp = (struct lifreq *)buf;
	ifend = (struct lifreq *)(buf + ifc.lifc_len);

	for (; ifrp < ifend; ifrp++) {
		/*
		 * Skip entries that begin with "dummy".
		 * XXX - what are these?  Is this Linux-specific?
		 * Are there platforms on which we shouldn't do this?
		 */
		if (strncmp(ifrp->lifr_name, "dummy", 5) == 0)
			continue;

		/*
		 * Can we capture on this device?
		 */
		if (!(*check_usable)(ifrp->lifr_name)) {
			/*
			 * No.
			 */
			continue;
		}

		/*
		 * IPv6 or not?
		 */
		if (((struct sockaddr *)&ifrp->lifr_addr)->sa_family == AF_INET6)
			fd = fd6;
		else
			fd = fd4;

		/*
		 * Get the flags for this interface.
		 */
		strncpy(ifrflags.lifr_name, ifrp->lifr_name,
		    sizeof(ifrflags.lifr_name));
		if (ioctl(fd, SIOCGLIFFLAGS, (char *)&ifrflags) < 0) {
			if (errno == ENXIO)
				continue;
			pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
			    errno, "SIOCGLIFFLAGS: %.*s",
			    (int)sizeof(ifrflags.lifr_name),
			    ifrflags.lifr_name);
			ret = -1;
			break;
		}

		/*
		 * Get the netmask for this address on this interface.
		 */
		strncpy(ifrnetmask.lifr_name, ifrp->lifr_name,
		    sizeof(ifrnetmask.lifr_name));
		memcpy(&ifrnetmask.lifr_addr, &ifrp->lifr_addr,
		    sizeof(ifrnetmask.lifr_addr));
		if (ioctl(fd, SIOCGLIFNETMASK, (char *)&ifrnetmask) < 0) {
			if (errno == EADDRNOTAVAIL) {
				/*
				 * Not available.
				 */
				netmask = NULL;
			} else {
				pcap_fmt_errmsg_for_errno(errbuf,
				    PCAP_ERRBUF_SIZE, errno,
				    "SIOCGLIFNETMASK: %.*s",
				    (int)sizeof(ifrnetmask.lifr_name),
				    ifrnetmask.lifr_name);
				ret = -1;
				break;
			}
		} else
			netmask = (struct sockaddr *)&ifrnetmask.lifr_addr;

		/*
		 * Get the broadcast address for this address on this
		 * interface (if any).
		 */
		if (ifrflags.lifr_flags & IFF_BROADCAST) {
			strncpy(ifrbroadaddr.lifr_name, ifrp->lifr_name,
			    sizeof(ifrbroadaddr.lifr_name));
			memcpy(&ifrbroadaddr.lifr_addr, &ifrp->lifr_addr,
			    sizeof(ifrbroadaddr.lifr_addr));
			if (ioctl(fd, SIOCGLIFBRDADDR,
			    (char *)&ifrbroadaddr) < 0) {
				if (errno == EADDRNOTAVAIL) {
					/*
					 * Not available.
					 */
					broadaddr = NULL;
				} else {
					pcap_fmt_errmsg_for_errno(errbuf,
					    PCAP_ERRBUF_SIZE, errno,
					    "SIOCGLIFBRDADDR: %.*s",
					    (int)sizeof(ifrbroadaddr.lifr_name),
					    ifrbroadaddr.lifr_name);
					ret = -1;
					break;
				}
			} else
				broadaddr = (struct sockaddr *)&ifrbroadaddr.lifr_broadaddr;
		} else {
			/*
			 * Not a broadcast interface, so no broadcast
			 * address.
			 */
			broadaddr = NULL;
		}

		/*
		 * Get the destination address for this address on this
		 * interface (if any).
		 */
		if (ifrflags.lifr_flags & IFF_POINTOPOINT) {
			strncpy(ifrdstaddr.lifr_name, ifrp->lifr_name,
			    sizeof(ifrdstaddr.lifr_name));
			memcpy(&ifrdstaddr.lifr_addr, &ifrp->lifr_addr,
			    sizeof(ifrdstaddr.lifr_addr));
			if (ioctl(fd, SIOCGLIFDSTADDR,
			    (char *)&ifrdstaddr) < 0) {
				if (errno == EADDRNOTAVAIL) {
					/*
					 * Not available.
					 */
					dstaddr = NULL;
				} else {
					pcap_fmt_errmsg_for_errno(errbuf,
					    PCAP_ERRBUF_SIZE, errno,
					    "SIOCGLIFDSTADDR: %.*s",
					    (int)sizeof(ifrdstaddr.lifr_name),
					    ifrdstaddr.lifr_name);
					ret = -1;
					break;
				}
			} else
				dstaddr = (struct sockaddr *)&ifrdstaddr.lifr_dstaddr;
		} else
			dstaddr = NULL;

#ifdef HAVE_SOLARIS
		/*
		 * If this entry has a colon followed by a number at
		 * the end, it's a logical interface.  Those are just
		 * the way you assign multiple IP addresses to a real
		 * interface, so an entry for a logical interface should
		 * be treated like the entry for the real interface;
		 * we do that by stripping off the ":" and the number.
		 */
		p = strchr(ifrp->lifr_name, ':');
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
		if (add_addr_to_if(devlistp, ifrp->lifr_name,
		    ifrflags.lifr_flags, get_flags_func,
		    (struct sockaddr *)&ifrp->lifr_addr,
		    sizeof (struct sockaddr_storage),
		    netmask, sizeof (struct sockaddr_storage),
		    broadaddr, sizeof (struct sockaddr_storage),
		    dstaddr, sizeof (struct sockaddr_storage), errbuf) < 0) {
			ret = -1;
			break;
		}
	}
	free(buf);
	(void)close(fd6);
	(void)close(fd4);

	return (ret);
}
