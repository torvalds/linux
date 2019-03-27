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
 *
 * packet filter subroutines for tcpdump
 *	Extraction/creation by Jeffrey Mogul, DECWRL
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <net/pfilt.h>

struct mbuf;
struct rtentry;
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

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Make "pcap.h" not include "pcap/bpf.h"; we are going to include the
 * native OS version, as we need various BPF ioctls from it.
 */
#define PCAP_DONT_INCLUDE_PCAP_BPF_H
#include <net/bpf.h>

#include "pcap-int.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

/*
 * FDDI packets are padded to make everything line up on a nice boundary.
 */
#define       PCAP_FDDIPAD 3

/*
 * Private data for capturing on Ultrix and DEC OSF/1^WDigital UNIX^W^W
 * Tru64 UNIX packetfilter devices.
 */
struct pcap_pf {
	int	filtering_in_kernel; /* using kernel filter */
	u_long	TotPkts;	/* can't oflow for 79 hrs on ether */
	u_long	TotAccepted;	/* count accepted by filter */
	u_long	TotDrops;	/* count of dropped packets */
	long	TotMissed;	/* missed by i/f during this run */
	long	OrigMissed;	/* missed by i/f before this run */
};

static int pcap_setfilter_pf(pcap_t *, struct bpf_program *);

/*
 * BUFSPACE is the size in bytes of the packet read buffer.  Most tcpdump
 * applications aren't going to need more than 200 bytes of packet header
 * and the read shouldn't return more packets than packetfilter's internal
 * queue limit (bounded at 256).
 */
#define BUFSPACE (200 * 256)

static int
pcap_read_pf(pcap_t *pc, int cnt, pcap_handler callback, u_char *user)
{
	struct pcap_pf *pf = pc->priv;
	register u_char *p, *bp;
	register int cc, n, buflen, inc;
	register struct enstamp *sp;
#ifdef LBL_ALIGN
	struct enstamp stamp;
#endif
	register u_int pad;

 again:
	cc = pc->cc;
	if (cc == 0) {
		cc = read(pc->fd, (char *)pc->buffer + pc->offset, pc->bufsize);
		if (cc < 0) {
			if (errno == EWOULDBLOCK)
				return (0);
			if (errno == EINVAL &&
			    lseek(pc->fd, 0L, SEEK_CUR) + pc->bufsize < 0) {
				/*
				 * Due to a kernel bug, after 2^31 bytes,
				 * the kernel file offset overflows and
				 * read fails with EINVAL. The lseek()
				 * to 0 will fix things.
				 */
				(void)lseek(pc->fd, 0L, SEEK_SET);
				goto again;
			}
			pcap_fmt_errmsg_for_errno(pc->errbuf,
			    sizeof(pc->errbuf), errno, "pf read");
			return (-1);
		}
		bp = (u_char *)pc->buffer + pc->offset;
	} else
		bp = pc->bp;
	/*
	 * Loop through each packet.
	 */
	n = 0;
	pad = pc->fddipad;
	while (cc > 0) {
		/*
		 * Has "pcap_breakloop()" been called?
		 * If so, return immediately - if we haven't read any
		 * packets, clear the flag and return -2 to indicate
		 * that we were told to break out of the loop, otherwise
		 * leave the flag set, so that the *next* call will break
		 * out of the loop without having read any packets, and
		 * return the number of packets we've processed so far.
		 */
		if (pc->break_loop) {
			if (n == 0) {
				pc->break_loop = 0;
				return (-2);
			} else {
				pc->cc = cc;
				pc->bp = bp;
				return (n);
			}
		}
		if (cc < sizeof(*sp)) {
			pcap_snprintf(pc->errbuf, sizeof(pc->errbuf),
			    "pf short read (%d)", cc);
			return (-1);
		}
#ifdef LBL_ALIGN
		if ((long)bp & 3) {
			sp = &stamp;
			memcpy((char *)sp, (char *)bp, sizeof(*sp));
		} else
#endif
			sp = (struct enstamp *)bp;
		if (sp->ens_stamplen != sizeof(*sp)) {
			pcap_snprintf(pc->errbuf, sizeof(pc->errbuf),
			    "pf short stamplen (%d)",
			    sp->ens_stamplen);
			return (-1);
		}

		p = bp + sp->ens_stamplen;
		buflen = sp->ens_count;
		if (buflen > pc->snapshot)
			buflen = pc->snapshot;

		/* Calculate inc before possible pad update */
		inc = ENALIGN(buflen + sp->ens_stamplen);
		cc -= inc;
		bp += inc;
		pf->TotPkts++;
		pf->TotDrops += sp->ens_dropped;
		pf->TotMissed = sp->ens_ifoverflows;
		if (pf->OrigMissed < 0)
			pf->OrigMissed = pf->TotMissed;

		/*
		 * Short-circuit evaluation: if using BPF filter
		 * in kernel, no need to do it now - we already know
		 * the packet passed the filter.
		 *
		 * Note: the filter code was generated assuming
		 * that pc->fddipad was the amount of padding
		 * before the header, as that's what's required
		 * in the kernel, so we run the filter before
		 * skipping that padding.
		 */
		if (pf->filtering_in_kernel ||
		    bpf_filter(pc->fcode.bf_insns, p, sp->ens_count, buflen)) {
			struct pcap_pkthdr h;
			pf->TotAccepted++;
			h.ts = sp->ens_tstamp;
			h.len = sp->ens_count - pad;
			p += pad;
			buflen -= pad;
			h.caplen = buflen;
			(*callback)(user, &h, p);
			if (++n >= cnt && !PACKET_COUNT_IS_UNLIMITED(cnt)) {
				pc->cc = cc;
				pc->bp = bp;
				return (n);
			}
		}
	}
	pc->cc = 0;
	return (n);
}

static int
pcap_inject_pf(pcap_t *p, const void *buf, size_t size)
{
	int ret;

	ret = write(p->fd, buf, size);
	if (ret == -1) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "send");
		return (-1);
	}
	return (ret);
}

static int
pcap_stats_pf(pcap_t *p, struct pcap_stat *ps)
{
	struct pcap_pf *pf = p->priv;

	/*
	 * If packet filtering is being done in the kernel:
	 *
	 *	"ps_recv" counts only packets that passed the filter.
	 *	This does not include packets dropped because we
	 *	ran out of buffer space.  (XXX - perhaps it should,
	 *	by adding "ps_drop" to "ps_recv", for compatibility
	 *	with some other platforms.  On the other hand, on
	 *	some platforms "ps_recv" counts only packets that
	 *	passed the filter, and on others it counts packets
	 *	that didn't pass the filter....)
	 *
	 *	"ps_drop" counts packets that passed the kernel filter
	 *	(if any) but were dropped because the input queue was
	 *	full.
	 *
	 *	"ps_ifdrop" counts packets dropped by the network
	 *	inteface (regardless of whether they would have passed
	 *	the input filter, of course).
	 *
	 * If packet filtering is not being done in the kernel:
	 *
	 *	"ps_recv" counts only packets that passed the filter.
	 *
	 *	"ps_drop" counts packets that were dropped because the
	 *	input queue was full, regardless of whether they passed
	 *	the userland filter.
	 *
	 *	"ps_ifdrop" counts packets dropped by the network
	 *	inteface (regardless of whether they would have passed
	 *	the input filter, of course).
	 *
	 * These statistics don't include packets not yet read from
	 * the kernel by libpcap, but they may include packets not
	 * yet read from libpcap by the application.
	 */
	ps->ps_recv = pf->TotAccepted;
	ps->ps_drop = pf->TotDrops;
	ps->ps_ifdrop = pf->TotMissed - pf->OrigMissed;
	return (0);
}

/*
 * We include the OS's <net/bpf.h>, not our "pcap/bpf.h", so we probably
 * don't get DLT_DOCSIS defined.
 */
#ifndef DLT_DOCSIS
#define DLT_DOCSIS	143
#endif

static int
pcap_activate_pf(pcap_t *p)
{
	struct pcap_pf *pf = p->priv;
	short enmode;
	int backlog = -1;	/* request the most */
	struct enfilter Filter;
	struct endevp devparams;
	int err;

	/*
	 * Initially try a read/write open (to allow the inject
	 * method to work).  If that fails due to permission
	 * issues, fall back to read-only.  This allows a
	 * non-root user to be granted specific access to pcap
	 * capabilities via file permissions.
	 *
	 * XXX - we should have an API that has a flag that
	 * controls whether to open read-only or read-write,
	 * so that denial of permission to send (or inability
	 * to send, if sending packets isn't supported on
	 * the device in question) can be indicated at open
	 * time.
	 *
	 * XXX - we assume here that "pfopen()" does not, in fact, modify
	 * its argument, even though it takes a "char *" rather than a
	 * "const char *" as its first argument.  That appears to be
	 * the case, at least on Digital UNIX 4.0.
	 *
	 * XXX - is there an error that means "no such device"?  Is
	 * there one that means "that device doesn't support pf"?
	 */
	p->fd = pfopen(p->opt.device, O_RDWR);
	if (p->fd == -1 && errno == EACCES)
		p->fd = pfopen(p->opt.device, O_RDONLY);
	if (p->fd < 0) {
		if (errno == EACCES) {
			pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "pf open: %s: Permission denied\n"
"your system may not be properly configured; see the packetfilter(4) man page",
			    p->opt.device);
			err = PCAP_ERROR_PERM_DENIED;
		} else {
			pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
			    errno, "pf open: %s", p->opt.device);
			err = PCAP_ERROR;
		}
		goto bad;
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

	pf->OrigMissed = -1;
	enmode = ENTSTAMP|ENNONEXCL;
	if (!p->opt.immediate)
		enmode |= ENBATCH;
	if (p->opt.promisc)
		enmode |= ENPROMISC;
	if (ioctl(p->fd, EIOCMBIS, (caddr_t)&enmode) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "EIOCMBIS");
		err = PCAP_ERROR;
		goto bad;
	}
#ifdef	ENCOPYALL
	/* Try to set COPYALL mode so that we see packets to ourself */
	enmode = ENCOPYALL;
	(void)ioctl(p->fd, EIOCMBIS, (caddr_t)&enmode);/* OK if this fails */
#endif
	/* set the backlog */
	if (ioctl(p->fd, EIOCSETW, (caddr_t)&backlog) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "EIOCSETW");
		err = PCAP_ERROR;
		goto bad;
	}
	/* discover interface type */
	if (ioctl(p->fd, EIOCDEVP, (caddr_t)&devparams) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "EIOCDEVP");
		err = PCAP_ERROR;
		goto bad;
	}
	/* HACK: to compile prior to Ultrix 4.2 */
#ifndef	ENDT_FDDI
#define	ENDT_FDDI	4
#endif
	switch (devparams.end_dev_type) {

	case ENDT_10MB:
		p->linktype = DLT_EN10MB;
		p->offset = 2;
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
		break;

	case ENDT_FDDI:
		p->linktype = DLT_FDDI;
		break;

#ifdef ENDT_SLIP
	case ENDT_SLIP:
		p->linktype = DLT_SLIP;
		break;
#endif

#ifdef ENDT_PPP
	case ENDT_PPP:
		p->linktype = DLT_PPP;
		break;
#endif

#ifdef ENDT_LOOPBACK
	case ENDT_LOOPBACK:
		/*
		 * It appears to use Ethernet framing, at least on
		 * Digital UNIX 4.0.
		 */
		p->linktype = DLT_EN10MB;
		p->offset = 2;
		break;
#endif

#ifdef ENDT_TRN
	case ENDT_TRN:
		p->linktype = DLT_IEEE802;
		break;
#endif

	default:
		/*
		 * XXX - what about ENDT_IEEE802?  The pfilt.h header
		 * file calls this "IEEE 802 networks (non-Ethernet)",
		 * but that doesn't specify a specific link layer type;
		 * it could be 802.4, or 802.5 (except that 802.5 is
		 * ENDT_TRN), or 802.6, or 802.11, or....  That's why
		 * DLT_IEEE802 was hijacked to mean Token Ring in various
		 * BSDs, and why we went along with that hijacking.
		 *
		 * XXX - what about ENDT_HDLC and ENDT_NULL?
		 * Presumably, as ENDT_OTHER is just "Miscellaneous
		 * framing", there's not much we can do, as that
		 * doesn't specify a particular type of header.
		 */
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		    "unknown data-link type %u", devparams.end_dev_type);
		err = PCAP_ERROR;
		goto bad;
	}
	/* set truncation */
	if (p->linktype == DLT_FDDI) {
		p->fddipad = PCAP_FDDIPAD;

		/* packetfilter includes the padding in the snapshot */
		p->snapshot += PCAP_FDDIPAD;
	} else
		p->fddipad = 0;
	if (ioctl(p->fd, EIOCTRUNCATE, (caddr_t)&p->snapshot) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "EIOCTRUNCATE");
		err = PCAP_ERROR;
		goto bad;
	}
	/* accept all packets */
	memset(&Filter, 0, sizeof(Filter));
	Filter.enf_Priority = 37;	/* anything > 2 */
	Filter.enf_FilterLen = 0;	/* means "always true" */
	if (ioctl(p->fd, EIOCSETF, (caddr_t)&Filter) < 0) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "EIOCSETF");
		err = PCAP_ERROR;
		goto bad;
	}

	if (p->opt.timeout != 0) {
		struct timeval timeout;
		timeout.tv_sec = p->opt.timeout / 1000;
		timeout.tv_usec = (p->opt.timeout * 1000) % 1000000;
		if (ioctl(p->fd, EIOCSRTIMEOUT, (caddr_t)&timeout) < 0) {
			pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
			    errno, "EIOCSRTIMEOUT");
			err = PCAP_ERROR;
			goto bad;
		}
	}

	p->bufsize = BUFSPACE;
	p->buffer = malloc(p->bufsize + p->offset);
	if (p->buffer == NULL) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "malloc");
		err = PCAP_ERROR;
		goto bad;
	}

	/*
	 * "select()" and "poll()" work on packetfilter devices.
	 */
	p->selectable_fd = p->fd;

	p->read_op = pcap_read_pf;
	p->inject_op = pcap_inject_pf;
	p->setfilter_op = pcap_setfilter_pf;
	p->setdirection_op = NULL;	/* Not implemented. */
	p->set_datalink_op = NULL;	/* can't change data link type */
	p->getnonblock_op = pcap_getnonblock_fd;
	p->setnonblock_op = pcap_setnonblock_fd;
	p->stats_op = pcap_stats_pf;

	return (0);
 bad:
	pcap_cleanup_live_common(p);
	return (err);
}

pcap_t *
pcap_create_interface(const char *device _U_, char *ebuf)
{
	pcap_t *p;

	p = pcap_create_common(ebuf, sizeof (struct pcap_pf));
	if (p == NULL)
		return (NULL);

	p->activate_op = pcap_activate_pf;
	return (p);
}

/*
 * XXX - is there an error from pfopen() that means "no such device"?
 * Is there one that means "that device doesn't support pf"?
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
	 * Nothing we can do other than mark loopback devices as "the
	 * connected/disconnected status doesn't apply".
	 *
	 * XXX - is there a way to find out whether an adapter has
	 * something plugged into it?
	 */
	if (*flags & PCAP_IF_LOOPBACK) {
		/*
		 * Loopback devices aren't wireless, and "connected"/
		 * "disconnected" doesn't apply to them.
		 */
		*flags |= PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE;
		return (0);
	}
	return (0);
}

int
pcap_platform_finddevs(pcap_if_list_t *devlistp, char *errbuf)
{
	return (pcap_findalldevs_interfaces(devlistp, errbuf, can_be_bound,
	    get_if_flags));
}

static int
pcap_setfilter_pf(pcap_t *p, struct bpf_program *fp)
{
	struct pcap_pf *pf = p->priv;
	struct bpf_version bv;

	/*
	 * See if BIOCVERSION works.  If not, we assume the kernel doesn't
	 * support BPF-style filters (it's not documented in the bpf(7)
	 * or packetfiler(7) man pages, but the code used to fail if
	 * BIOCSETF worked but BIOCVERSION didn't, and I've seen it do
	 * kernel filtering in DU 4.0, so presumably BIOCVERSION works
	 * there, at least).
	 */
	if (ioctl(p->fd, BIOCVERSION, (caddr_t)&bv) >= 0) {
		/*
		 * OK, we have the version of the BPF interpreter;
		 * is it the same major version as us, and the same
		 * or better minor version?
		 */
		if (bv.bv_major == BPF_MAJOR_VERSION &&
		    bv.bv_minor >= BPF_MINOR_VERSION) {
			/*
			 * Yes.  Try to install the filter.
			 */
			if (ioctl(p->fd, BIOCSETF, (caddr_t)fp) < 0) {
				pcap_fmt_errmsg_for_errno(p->errbuf,
				    sizeof(p->errbuf), errno, "BIOCSETF");
				return (-1);
			}

			/*
			 * OK, that succeeded.  We're doing filtering in
			 * the kernel.  (We assume we don't have a
			 * userland filter installed - that'd require
			 * a previous version check to have failed but
			 * this one to succeed.)
			 *
			 * XXX - this message should be supplied to the
			 * application as a warning of some sort,
			 * except that if it's a GUI application, it's
			 * not clear that it should be displayed in
			 * a window to annoy the user.
			 */
			fprintf(stderr, "tcpdump: Using kernel BPF filter\n");
			pf->filtering_in_kernel = 1;

			/*
			 * Discard any previously-received packets,
			 * as they might have passed whatever filter
			 * was formerly in effect, but might not pass
			 * this filter (BIOCSETF discards packets buffered
			 * in the kernel, so you can lose packets in any
			 * case).
			 */
			p->cc = 0;
			return (0);
		}

		/*
		 * We can't use the kernel's BPF interpreter; don't give
		 * up, just log a message and be inefficient.
		 *
		 * XXX - this should really be supplied to the application
		 * as a warning of some sort.
		 */
		fprintf(stderr,
	    "tcpdump: Requires BPF language %d.%d or higher; kernel is %d.%d\n",
		    BPF_MAJOR_VERSION, BPF_MINOR_VERSION,
		    bv.bv_major, bv.bv_minor);
	}

	/*
	 * We couldn't do filtering in the kernel; do it in userland.
	 */
	if (install_bpf_program(p, fp) < 0)
		return (-1);

	/*
	 * XXX - this message should be supplied by the application as
	 * a warning of some sort.
	 */
	fprintf(stderr, "tcpdump: Filtering in user process\n");
	pf->filtering_in_kernel = 0;
	return (0);
}

/*
 * Libpcap version string.
 */
const char *
pcap_lib_version(void)
{
	return (PCAP_VERSION_STRING);
}
