/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/nit.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>
#include <netinet/tcpip.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>

#include "pcap-int.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

/*
 * The chunk size for NIT.  This is the amount of buffering
 * done for read calls.
 */
#define CHUNKSIZE (2*1024)

/*
 * The total buffer space used by NIT.
 */
#define BUFSPACE (4*CHUNKSIZE)

/* Forwards */
static int nit_setflags(int, int, int, char *);

/*
 * Private data for capturing on NIT devices.
 */
struct pcap_nit {
	struct pcap_stat stat;
};

static int
pcap_stats_nit(pcap_t *p, struct pcap_stat *ps)
{
	struct pcap_nit *pn = p->priv;

	/*
	 * "ps_recv" counts packets handed to the filter, not packets
	 * that passed the filter.  As filtering is done in userland,
	 * this does not include packets dropped because we ran out
	 * of buffer space.
	 *
	 * "ps_drop" presumably counts packets dropped by the socket
	 * because of flow control requirements or resource exhaustion;
	 * it doesn't count packets dropped by the interface driver.
	 * As filtering is done in userland, it counts packets regardless
	 * of whether they would've passed the filter.
	 *
	 * These statistics don't include packets not yet read from the
	 * kernel by libpcap or packets not yet read from libpcap by the
	 * application.
	 */
	*ps = pn->stat;
	return (0);
}

static int
pcap_read_nit(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{
	struct pcap_nit *pn = p->priv;
	register int cc, n;
	register u_char *bp, *cp, *ep;
	register struct nit_hdr *nh;
	register int caplen;

	cc = p->cc;
	if (cc == 0) {
		cc = read(p->fd, (char *)p->buffer, p->bufsize);
		if (cc < 0) {
			if (errno == EWOULDBLOCK)
				return (0);
			pcap_fmt_errmsg_for_errno(p->errbuf, sizeof(p->errbuf),
			    errno, "pcap_read");
			return (-1);
		}
		bp = (u_char *)p->buffer;
	} else
		bp = p->bp;

	/*
	 * Loop through each packet.  The increment expression
	 * rounds up to the next int boundary past the end of
	 * the previous packet.
	 */
	n = 0;
	ep = bp + cc;
	while (bp < ep) {
		/*
		 * Has "pcap_breakloop()" been called?
		 * If so, return immediately - if we haven't read any
		 * packets, clear the flag and return -2 to indicate
		 * that we were told to break out of the loop, otherwise
		 * leave the flag set, so that the *next* call will break
		 * out of the loop without having read any packets, and
		 * return the number of packets we've processed so far.
		 */
		if (p->break_loop) {
			if (n == 0) {
				p->break_loop = 0;
				return (-2);
			} else {
				p->cc = ep - bp;
				p->bp = bp;
				return (n);
			}
		}

		nh = (struct nit_hdr *)bp;
		cp = bp + sizeof(*nh);

		switch (nh->nh_state) {

		case NIT_CATCH:
			break;

		case NIT_NOMBUF:
		case NIT_NOCLUSTER:
		case NIT_NOSPACE:
			pn->stat.ps_drop = nh->nh_dropped;
			continue;

		case NIT_SEQNO:
			continue;

		default:
			pcap_snprintf(p->errbuf, sizeof(p->errbuf),
			    "bad nit state %d", nh->nh_state);
			return (-1);
		}
		++pn->stat.ps_recv;
		bp += ((sizeof(struct nit_hdr) + nh->nh_datalen +
		    sizeof(int) - 1) & ~(sizeof(int) - 1));

		caplen = nh->nh_wirelen;
		if (caplen > p->snapshot)
			caplen = p->snapshot;
		if (bpf_filter(p->fcode.bf_insns, cp, nh->nh_wirelen, caplen)) {
			struct pcap_pkthdr h;
			h.ts = nh->nh_timestamp;
			h.len = nh->nh_wirelen;
			h.caplen = caplen;
			(*callback)(user, &h, cp);
			if (++n >= cnt && !PACKET_COUNT_IS_UNLIMITED(cnt)) {
				p->cc = ep - bp;
				p->bp = bp;
				return (n);
			}
		}
	}
	p->cc = 0;
	return (n);
}

static int
pcap_inject_nit(pcap_t *p, const void *buf, size_t size)
{
	struct sockaddr sa;
	int ret;

	memset(&sa, 0, sizeof(sa));
	strncpy(sa.sa_data, device, sizeof(sa.sa_data));
	ret = sendto(p->fd, buf, size, 0, &sa, sizeof(sa));
	if (ret == -1) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "send");
		return (-1);
	}
	return (ret);
}

static int
nit_setflags(pcap_t *p)
{
	struct nit_ioc nioc;

	memset(&nioc, 0, sizeof(nioc));
	nioc.nioc_typetomatch = NT_ALLTYPES;
	nioc.nioc_snaplen = p->snapshot;
	nioc.nioc_bufalign = sizeof(int);
	nioc.nioc_bufoffset = 0;

	if (p->opt.buffer_size != 0)
		nioc.nioc_bufspace = p->opt.buffer_size;
	else {
		/* Default buffer size */
		nioc.nioc_bufspace = BUFSPACE;
	}

	if (p->opt.immediate) {
		/*
		 * XXX - will this cause packets to be delivered immediately?
		 * XXX - given that this is for SunOS prior to 4.0, do
		 * we care?
		 */
		nioc.nioc_chunksize = 0;
	} else
		nioc.nioc_chunksize = CHUNKSIZE;
	if (p->opt.timeout != 0) {
		nioc.nioc_flags |= NF_TIMEOUT;
		nioc.nioc_timeout.tv_sec = p->opt.timeout / 1000;
		nioc.nioc_timeout.tv_usec = (p->opt.timeout * 1000) % 1000000;
	}
	if (p->opt.promisc)
		nioc.nioc_flags |= NF_PROMISC;

	if (ioctl(p->fd, SIOCSNIT, &nioc) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "SIOCSNIT");
		return (-1);
	}
	return (0);
}

static int
pcap_activate_nit(pcap_t *p)
{
	int fd;
	struct sockaddr_nit snit;

	if (p->opt.rfmon) {
		/*
		 * No monitor mode on SunOS 3.x or earlier (no
		 * Wi-Fi *devices* for the hardware that supported
		 * them!).
		 */
		return (PCAP_ERROR_RFMON_NOTSUP);
	}

	/*
	 * Turn a negative snapshot value (invalid), a snapshot value of
	 * 0 (unspecified), or a value bigger than the normal maximum
	 * value, into the maximum allowed value.
	 *
	 * If some application really *needs* a bigger snapshot
	 * length, we should just increase MAXIMUM_SNAPLEN.
	 */
	if (p->snapshot <= 0 || p->snapshot > MAXIMUM_SNAPLEN)
		p->snapshot = MAXIMUM_SNAPLEN;

	if (p->snapshot < 96)
		/*
		 * NIT requires a snapshot length of at least 96.
		 */
		p->snapshot = 96;

	memset(p, 0, sizeof(*p));
	p->fd = fd = socket(AF_NIT, SOCK_RAW, NITPROTO_RAW);
	if (fd < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "socket");
		goto bad;
	}
	snit.snit_family = AF_NIT;
	(void)strncpy(snit.snit_ifname, p->opt.device, NITIFSIZ);

	if (bind(fd, (struct sockaddr *)&snit, sizeof(snit))) {
		/*
		 * XXX - there's probably a particular bind error that
		 * means "there's no such device" and a particular bind
		 * error that means "that device doesn't support NIT";
		 * they might be the same error, if they both end up
		 * meaning "NIT doesn't know about that device".
		 */
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "bind: %s", snit.snit_ifname);
		goto bad;
	}
	if (nit_setflags(p) < 0)
		goto bad;

	/*
	 * NIT supports only ethernets.
	 */
	p->linktype = DLT_EN10MB;

	p->bufsize = BUFSPACE;
	p->buffer = malloc(p->bufsize);
	if (p->buffer == NULL) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "malloc");
		goto bad;
	}

	/*
	 * "p->fd" is a socket, so "select()" should work on it.
	 */
	p->selectable_fd = p->fd;

	/*
	 * This is (presumably) a real Ethernet capture; give it a
	 * link-layer-type list with DLT_EN10MB and DLT_DOCSIS, so
	 * that an application can let you choose it, in case you're
	 * capturing DOCSIS traffic that a Cisco Cable Modem
	 * Termination System is putting out onto an Ethernet (it
	 * doesn't put an Ethernet header onto the wire, it puts raw
	 * DOCSIS frames out on the wire inside the low-level
	 * Ethernet framing).
	 */
	p->dlt_list = (u_int *) malloc(sizeof(u_int) * 2);
	/*
	 * If that fails, just leave the list empty.
	 */
	if (p->dlt_list != NULL) {
		p->dlt_list[0] = DLT_EN10MB;
		p->dlt_list[1] = DLT_DOCSIS;
		p->dlt_count = 2;
	}

	p->read_op = pcap_read_nit;
	p->inject_op = pcap_inject_nit;
	p->setfilter_op = install_bpf_program;	/* no kernel filtering */
	p->setdirection_op = NULL;	/* Not implemented. */
	p->set_datalink_op = NULL;	/* can't change data link type */
	p->getnonblock_op = pcap_getnonblock_fd;
	p->setnonblock_op = pcap_setnonblock_fd;
	p->stats_op = pcap_stats_nit;

	return (0);
 bad:
	pcap_cleanup_live_common(p);
	return (PCAP_ERROR);
}

pcap_t *
pcap_create_interface(const char *device _U_, char *ebuf)
{
	pcap_t *p;

	p = pcap_create_common(ebuf, sizeof (struct pcap_nit));
	if (p == NULL)
		return (NULL);

	p->activate_op = pcap_activate_nit;
	return (p);
}

/*
 * XXX - there's probably a particular bind error that means "that device
 * doesn't support NIT"; if so, we should try a bind and use that.
 */
static int
can_be_bound(const char *name _U_)
{
	return (1);
}

static int
get_if_flags(const char *name _U_, bpf_u_int32 *flags _U_, char *errbuf _U_)
{
	/*
	 * Nothing we can do.
	 * XXX - is there a way to find out whether an adapter has
	 * something plugged into it?
	 */
	return (0);
}

int
pcap_platform_finddevs(pcap_if_list_t *devlistp, char *errbuf)
{
	return (pcap_findalldevs_interfaces(devlistp, errbuf, can_be_bound,
	    get_if_flags));
}

/*
 * Libpcap version string.
 */
const char *
pcap_lib_version(void)
{
	return (PCAP_VERSION_STRING);
}
