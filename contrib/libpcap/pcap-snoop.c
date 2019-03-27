/*
 * Copyright (c) 1993, 1994, 1995, 1996, 1997
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

#include <sys/param.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <net/raw.h>
#include <net/if.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>
#include <netinet/tcpip.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pcap-int.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

/*
 * Private data for capturing on snoop devices.
 */
struct pcap_snoop {
	struct pcap_stat stat;
};

static int
pcap_read_snoop(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{
	struct pcap_snoop *psn = p->priv;
	int cc;
	register struct snoopheader *sh;
	register u_int datalen;
	register u_int caplen;
	register u_char *cp;

again:
	/*
	 * Has "pcap_breakloop()" been called?
	 */
	if (p->break_loop) {
		/*
		 * Yes - clear the flag that indicates that it
		 * has, and return -2 to indicate that we were
		 * told to break out of the loop.
		 */
		p->break_loop = 0;
		return (-2);
	}
	cc = read(p->fd, (char *)p->buffer, p->bufsize);
	if (cc < 0) {
		/* Don't choke when we get ptraced */
		switch (errno) {

		case EINTR:
			goto again;

		case EWOULDBLOCK:
			return (0);			/* XXX */
		}
		pcap_fmt_errmsg_for_errno(p->errbuf, sizeof(p->errbuf),
		    errno, "read");
		return (-1);
	}
	sh = (struct snoopheader *)p->buffer;
	datalen = sh->snoop_packetlen;

	/*
	 * XXX - Sigh, snoop_packetlen is a 16 bit quantity.  If we
	 * got a short length, but read a full sized snoop pakcet,
	 * assume we overflowed and add back the 64K...
	 */
	if (cc == (p->snapshot + sizeof(struct snoopheader)) &&
	    (datalen < p->snapshot))
		datalen += (64 * 1024);

	caplen = (datalen < p->snapshot) ? datalen : p->snapshot;
	cp = (u_char *)(sh + 1) + p->offset;		/* XXX */

	/*
	 * XXX unfortunately snoop loopback isn't exactly like
	 * BSD's.  The address family is encoded in the first 2
	 * bytes rather than the first 4 bytes!  Luckily the last
	 * two snoop loopback bytes are zeroed.
	 */
	if (p->linktype == DLT_NULL && *((short *)(cp + 2)) == 0) {
		u_int *uip = (u_int *)cp;
		*uip >>= 16;
	}

	if (p->fcode.bf_insns == NULL ||
	    bpf_filter(p->fcode.bf_insns, cp, datalen, caplen)) {
		struct pcap_pkthdr h;
		++psn->stat.ps_recv;
		h.ts.tv_sec = sh->snoop_timestamp.tv_sec;
		h.ts.tv_usec = sh->snoop_timestamp.tv_usec;
		h.len = datalen;
		h.caplen = caplen;
		(*callback)(user, &h, cp);
		return (1);
	}
	return (0);
}

static int
pcap_inject_snoop(pcap_t *p, const void *buf, size_t size)
{
	int ret;

	/*
	 * XXX - libnet overwrites the source address with what I
	 * presume is the interface's address; is that required?
	 */
	ret = write(p->fd, buf, size);
	if (ret == -1) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "send");
		return (-1);
	}
	return (ret);
}

static int
pcap_stats_snoop(pcap_t *p, struct pcap_stat *ps)
{
	struct pcap_snoop *psn = p->priv;
	register struct rawstats *rs;
	struct rawstats rawstats;

	rs = &rawstats;
	memset(rs, 0, sizeof(*rs));
	if (ioctl(p->fd, SIOCRAWSTATS, (char *)rs) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, sizeof(p->errbuf),
		    errno, "SIOCRAWSTATS");
		return (-1);
	}

	/*
	 * "ifdrops" are those dropped by the network interface
	 * due to resource shortages or hardware errors.
	 *
	 * "sbdrops" are those dropped due to socket buffer limits.
	 *
	 * As filter is done in userland, "sbdrops" counts packets
	 * regardless of whether they would've passed the filter.
	 *
	 * XXX - does this count *all* Snoop or Drain sockets,
	 * rather than just this socket?  If not, why does it have
	 * both Snoop and Drain statistics?
	 */
	psn->stat.ps_drop =
	    rs->rs_snoop.ss_ifdrops + rs->rs_snoop.ss_sbdrops +
	    rs->rs_drain.ds_ifdrops + rs->rs_drain.ds_sbdrops;

	/*
	 * "ps_recv" counts only packets that passed the filter.
	 * As filtering is done in userland, this does not include
	 * packets dropped because we ran out of buffer space.
	 */
	*ps = psn->stat;
	return (0);
}

/* XXX can't disable promiscuous */
static int
pcap_activate_snoop(pcap_t *p)
{
	int fd;
	struct sockaddr_raw sr;
	struct snoopfilter sf;
	u_int v;
	int ll_hdrlen;
	int snooplen;
	struct ifreq ifr;

	fd = socket(PF_RAW, SOCK_RAW, RAWPROTO_SNOOP);
	if (fd < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "snoop socket");
		goto bad;
	}
	p->fd = fd;
	memset(&sr, 0, sizeof(sr));
	sr.sr_family = AF_RAW;
	(void)strncpy(sr.sr_ifname, p->opt.device, sizeof(sr.sr_ifname));
	if (bind(fd, (struct sockaddr *)&sr, sizeof(sr))) {
		/*
		 * XXX - there's probably a particular bind error that
		 * means "there's no such device" and a particular bind
		 * error that means "that device doesn't support snoop";
		 * they might be the same error, if they both end up
		 * meaning "snoop doesn't know about that device".
		 */
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "snoop bind");
		goto bad;
	}
	memset(&sf, 0, sizeof(sf));
	if (ioctl(fd, SIOCADDSNOOP, &sf) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "SIOCADDSNOOP");
		goto bad;
	}
	if (p->opt.buffer_size != 0)
		v = p->opt.buffer_size;
	else
		v = 64 * 1024;	/* default to 64K buffer size */
	(void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *)&v, sizeof(v));
	/*
	 * XXX hack - map device name to link layer type
	 */
	if (strncmp("et", p->opt.device, 2) == 0 ||	/* Challenge 10 Mbit */
	    strncmp("ec", p->opt.device, 2) == 0 ||	/* Indigo/Indy 10 Mbit,
							   O2 10/100 */
	    strncmp("ef", p->opt.device, 2) == 0 ||	/* O200/2000 10/100 Mbit */
	    strncmp("eg", p->opt.device, 2) == 0 ||	/* Octane/O2xxx/O3xxx Gigabit */
	    strncmp("gfe", p->opt.device, 3) == 0 ||	/* GIO 100 Mbit */
	    strncmp("fxp", p->opt.device, 3) == 0 ||	/* Challenge VME Enet */
	    strncmp("ep", p->opt.device, 2) == 0 ||	/* Challenge 8x10 Mbit EPLEX */
	    strncmp("vfe", p->opt.device, 3) == 0 ||	/* Challenge VME 100Mbit */
	    strncmp("fa", p->opt.device, 2) == 0 ||
	    strncmp("qaa", p->opt.device, 3) == 0 ||
	    strncmp("cip", p->opt.device, 3) == 0 ||
	    strncmp("el", p->opt.device, 2) == 0) {
		p->linktype = DLT_EN10MB;
		p->offset = RAW_HDRPAD(sizeof(struct ether_header));
		ll_hdrlen = sizeof(struct ether_header);
		/*
		 * This is (presumably) a real Ethernet capture; give it a
		 * link-layer-type list with DLT_EN10MB and DLT_DOCSIS, so
		 * that an application can let you choose it, in case you're
		 * capturing DOCSIS traffic that a Cisco Cable Modem
		 * Termination System is putting out onto an Ethernet (it
		 * doesn't put an Ethernet header onto the wire, it puts raw
		 * DOCSIS frames out on the wire inside the low-level
		 * Ethernet framing).
		 *
		 * XXX - are there any sorts of "fake Ethernet" that have
		 * Ethernet link-layer headers but that *shouldn't offer
		 * DLT_DOCSIS as a Cisco CMTS won't put traffic onto it
		 * or get traffic bridged onto it?  "el" is for ATM LANE
		 * Ethernet devices, so that might be the case for them;
		 * the same applies for "qaa" classical IP devices.  If
		 * "fa" devices are for FORE SPANS, that'd apply to them
		 * as well; what are "cip" devices - some other ATM
		 * Classical IP devices?
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
	} else if (strncmp("ipg", p->opt.device, 3) == 0 ||
		   strncmp("rns", p->opt.device, 3) == 0 ||	/* O2/200/2000 FDDI */
		   strncmp("xpi", p->opt.device, 3) == 0) {
		p->linktype = DLT_FDDI;
		p->offset = 3;				/* XXX yeah? */
		ll_hdrlen = 13;
	} else if (strncmp("ppp", p->opt.device, 3) == 0) {
		p->linktype = DLT_RAW;
		ll_hdrlen = 0;	/* DLT_RAW meaning "no PPP header, just the IP packet"? */
	} else if (strncmp("qfa", p->opt.device, 3) == 0) {
		p->linktype = DLT_IP_OVER_FC;
		ll_hdrlen = 24;
	} else if (strncmp("pl", p->opt.device, 2) == 0) {
		p->linktype = DLT_RAW;
		ll_hdrlen = 0;	/* Cray UNICOS/mp pseudo link */
	} else if (strncmp("lo", p->opt.device, 2) == 0) {
		p->linktype = DLT_NULL;
		ll_hdrlen = 4;
	} else {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		    "snoop: unknown physical layer type");
		goto bad;
	}

	if (p->opt.rfmon) {
		/*
		 * No monitor mode on Irix (no Wi-Fi devices on
		 * hardware supported by Irix).
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

#ifdef SIOCGIFMTU
	/*
	 * XXX - IRIX appears to give you an error if you try to set the
	 * capture length to be greater than the MTU, so let's try to get
	 * the MTU first and, if that succeeds, trim the snap length
	 * to be no greater than the MTU.
	 */
	(void)strncpy(ifr.ifr_name, p->opt.device, sizeof(ifr.ifr_name));
	if (ioctl(fd, SIOCGIFMTU, (char *)&ifr) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "SIOCGIFMTU");
		goto bad;
	}
	/*
	 * OK, we got it.
	 *
	 * XXX - some versions of IRIX 6.5 define "ifr_mtu" and have an
	 * "ifru_metric" member of the "ifr_ifru" union in an "ifreq"
	 * structure, others don't.
	 *
	 * I've no idea what's going on, so, if "ifr_mtu" isn't defined,
	 * we define it as "ifr_metric", as using that field appears to
	 * work on the versions that lack "ifr_mtu" (and, on those that
	 * don't lack it, "ifru_metric" and "ifru_mtu" are both "int"
	 * members of the "ifr_ifru" union, which suggests that they
	 * may be interchangeable in this case).
	 */
#ifndef ifr_mtu
#define ifr_mtu	ifr_metric
#endif
	if (p->snapshot > ifr.ifr_mtu + ll_hdrlen)
		p->snapshot = ifr.ifr_mtu + ll_hdrlen;
#endif

	/*
	 * The argument to SIOCSNOOPLEN is the number of link-layer
	 * payload bytes to capture - it doesn't count link-layer
	 * header bytes.
	 */
	snooplen = p->snapshot - ll_hdrlen;
	if (snooplen < 0)
		snooplen = 0;
	if (ioctl(fd, SIOCSNOOPLEN, &snooplen) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "SIOCSNOOPLEN");
		goto bad;
	}
	v = 1;
	if (ioctl(fd, SIOCSNOOPING, &v) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "SIOCSNOOPING");
		goto bad;
	}

	p->bufsize = 4096;				/* XXX */
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

	p->read_op = pcap_read_snoop;
	p->inject_op = pcap_inject_snoop;
	p->setfilter_op = install_bpf_program;	/* no kernel filtering */
	p->setdirection_op = NULL;	/* Not implemented. */
	p->set_datalink_op = NULL;	/* can't change data link type */
	p->getnonblock_op = pcap_getnonblock_fd;
	p->setnonblock_op = pcap_setnonblock_fd;
	p->stats_op = pcap_stats_snoop;

	return (0);
 bad:
	pcap_cleanup_live_common(p);
	return (PCAP_ERROR);
}

pcap_t *
pcap_create_interface(const char *device _U_, char *ebuf)
{
	pcap_t *p;

	p = pcap_create_common(ebuf, sizeof (struct pcap_snoop));
	if (p == NULL)
		return (NULL);

	p->activate_op = pcap_activate_snoop;
	return (p);
}

/*
 * XXX - there's probably a particular bind error that means "that device
 * doesn't support snoop"; if so, we should try a bind and use that.
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
