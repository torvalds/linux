/*
 * Copyright (C) 2014 Luigi Rizzo. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <poll.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

#include "pcap-int.h"
#include "pcap-netmap.h"

#ifndef __FreeBSD__
  /*
   * On FreeBSD we use IFF_PPROMISC which is in ifr_flagshigh.
   * Remap to IFF_PROMISC on other platforms.
   *
   * XXX - DragonFly BSD?
   */
  #define IFF_PPROMISC	IFF_PROMISC
#endif /* __FreeBSD__ */

struct pcap_netmap {
	struct nm_desc *d;	/* pointer returned by nm_open() */
	pcap_handler cb;	/* callback and argument */
	u_char *cb_arg;
	int must_clear_promisc;	/* flag */
	uint64_t rx_pkts;	/* # of pkts received before the filter */
};


static int
pcap_netmap_stats(pcap_t *p, struct pcap_stat *ps)
{
	struct pcap_netmap *pn = p->priv;

	ps->ps_recv = pn->rx_pkts;
	ps->ps_drop = 0;
	ps->ps_ifdrop = 0;
	return 0;
}


static void
pcap_netmap_filter(u_char *arg, struct pcap_pkthdr *h, const u_char *buf)
{
	pcap_t *p = (pcap_t *)arg;
	struct pcap_netmap *pn = p->priv;
	const struct bpf_insn *pc = p->fcode.bf_insns;

	++pn->rx_pkts;
	if (pc == NULL || bpf_filter(pc, buf, h->len, h->caplen))
		pn->cb(pn->cb_arg, h, buf);
}


static int
pcap_netmap_dispatch(pcap_t *p, int cnt, pcap_handler cb, u_char *user)
{
	int ret;
	struct pcap_netmap *pn = p->priv;
	struct nm_desc *d = pn->d;
	struct pollfd pfd = { .fd = p->fd, .events = POLLIN, .revents = 0 };

	pn->cb = cb;
	pn->cb_arg = user;

	for (;;) {
		if (p->break_loop) {
			p->break_loop = 0;
			return PCAP_ERROR_BREAK;
		}
		/* nm_dispatch won't run forever */

		ret = nm_dispatch((void *)d, cnt, (void *)pcap_netmap_filter, (void *)p);
		if (ret != 0)
			break;
		errno = 0;
		ret = poll(&pfd, 1, p->opt.timeout);
	}
	return ret;
}


/* XXX need to check the NIOCTXSYNC/poll */
static int
pcap_netmap_inject(pcap_t *p, const void *buf, size_t size)
{
	struct pcap_netmap *pn = p->priv;
	struct nm_desc *d = pn->d;

	return nm_inject(d, buf, size);
}


static int
pcap_netmap_ioctl(pcap_t *p, u_long what, uint32_t *if_flags)
{
	struct pcap_netmap *pn = p->priv;
	struct nm_desc *d = pn->d;
	struct ifreq ifr;
	int error, fd = d->fd;

#ifdef linux
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		fprintf(stderr, "Error: cannot get device control socket.\n");
		return -1;
	}
#endif /* linux */
	bzero(&ifr, sizeof(ifr));
	strncpy(ifr.ifr_name, d->req.nr_name, sizeof(ifr.ifr_name));
	switch (what) {
	case SIOCSIFFLAGS:
		/*
		 * The flags we pass in are 32-bit and unsigned.
		 *
		 * On most if not all UN*Xes, ifr_flags is 16-bit and
		 * signed, and the result of assigning a longer
		 * unsigned value to a shorter signed value is
		 * implementation-defined (even if, in practice, it'll
		 * do what's intended on all platforms we support
		 * result of assigning a 32-bit unsigned value).
		 * So we mask out the upper 16 bits.
		 */
		ifr.ifr_flags = *if_flags & 0xffff;
#ifdef __FreeBSD__
		/*
		 * In FreeBSD, we need to set the high-order flags,
		 * as we're using IFF_PPROMISC, which is in those bits.
		 *
		 * XXX - DragonFly BSD?
		 */
		ifr.ifr_flagshigh = *if_flags >> 16;
#endif /* __FreeBSD__ */
		break;
	}
	error = ioctl(fd, what, &ifr);
	if (!error) {
		switch (what) {
		case SIOCGIFFLAGS:
			/*
			 * The flags we return are 32-bit.
			 *
			 * On most if not all UN*Xes, ifr_flags is
			 * 16-bit and signed, and will get sign-
			 * extended, so that the upper 16 bits of
			 * those flags will be forced on.  So we
			 * mask out the upper 16 bits of the
			 * sign-extended value.
			 */
			*if_flags = ifr.ifr_flags & 0xffff;
#ifdef __FreeBSD__
			/*
			 * In FreeBSD, we need to return the
			 * high-order flags, as we're using
			 * IFF_PPROMISC, which is in those bits.
			 *
			 * XXX - DragonFly BSD?
			 */
			*if_flags |= (ifr.ifr_flagshigh << 16);
#endif /* __FreeBSD__ */
		}
	}
#ifdef linux
	close(fd);
#endif /* linux */
	return error ? -1 : 0;
}


static void
pcap_netmap_close(pcap_t *p)
{
	struct pcap_netmap *pn = p->priv;
	struct nm_desc *d = pn->d;
	uint32_t if_flags = 0;

	if (pn->must_clear_promisc) {
		pcap_netmap_ioctl(p, SIOCGIFFLAGS, &if_flags); /* fetch flags */
		if (if_flags & IFF_PPROMISC) {
			if_flags &= ~IFF_PPROMISC;
			pcap_netmap_ioctl(p, SIOCSIFFLAGS, &if_flags);
		}
	}
	nm_close(d);
	pcap_cleanup_live_common(p);
}


static int
pcap_netmap_activate(pcap_t *p)
{
	struct pcap_netmap *pn = p->priv;
	struct nm_desc *d;
	uint32_t if_flags = 0;

	d = nm_open(p->opt.device, NULL, 0, NULL);
	if (d == NULL) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "netmap open: cannot access %s",
		    p->opt.device);
		pcap_cleanup_live_common(p);
		return (PCAP_ERROR);
	}
#if 0
	fprintf(stderr, "%s device %s priv %p fd %d ports %d..%d\n",
	    __FUNCTION__, p->opt.device, d, d->fd,
	    d->first_rx_ring, d->last_rx_ring);
#endif
	pn->d = d;
	p->fd = d->fd;

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

	if (p->opt.promisc && !(d->req.nr_ringid & NETMAP_SW_RING)) {
		pcap_netmap_ioctl(p, SIOCGIFFLAGS, &if_flags); /* fetch flags */
		if (!(if_flags & IFF_PPROMISC)) {
			pn->must_clear_promisc = 1;
			if_flags |= IFF_PPROMISC;
			pcap_netmap_ioctl(p, SIOCSIFFLAGS, &if_flags);
		}
	}
	p->linktype = DLT_EN10MB;
	p->selectable_fd = p->fd;
	p->read_op = pcap_netmap_dispatch;
	p->inject_op = pcap_netmap_inject;
	p->setfilter_op = install_bpf_program;
	p->setdirection_op = NULL;
	p->set_datalink_op = NULL;
	p->getnonblock_op = pcap_getnonblock_fd;
	p->setnonblock_op = pcap_setnonblock_fd;
	p->stats_op = pcap_netmap_stats;
	p->cleanup_op = pcap_netmap_close;

	return (0);
}


pcap_t *
pcap_netmap_create(const char *device, char *ebuf, int *is_ours)
{
	pcap_t *p;

	*is_ours = (!strncmp(device, "netmap:", 7) || !strncmp(device, "vale", 4));
	if (! *is_ours)
		return NULL;
	p = pcap_create_common(ebuf, sizeof (struct pcap_netmap));
	if (p == NULL)
		return (NULL);
	p->activate_op = pcap_netmap_activate;
	return (p);
}

/*
 * The "device name" for netmap devices isn't a name for a device, it's
 * an expression that indicates how the device should be set up, so
 * there's no way to enumerate them.
 */
int
pcap_netmap_findalldevs(pcap_if_list_t *devlistp _U_, char *err_str _U_)
{
	return 0;
}
