/*
 * This code is derived from code formerly in pcap-dlpi.c, originally
 * contributed by Atanu Ghosh (atanu@cs.ucl.ac.uk), University College
 * London, and subsequently modified by Guy Harris (guy@alum.mit.edu),
 * Mark Pizzolato <List-tcpdump-workers@subscriptions.pizzolato.net>,
 * Mark C. Brown (mbrown@hp.com), and Sagun Shakya <Sagun.Shakya@Sun.COM>.
 */

/*
 * This file contains dlpi/libdlpi related common functions used
 * by pcap-[dlpi,libdlpi].c.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef DL_IPATM
#define DL_IPATM	0x12	/* ATM Classical IP interface */
#endif

#ifdef HAVE_SYS_BUFMOD_H
	/*
	 * Size of a bufmod chunk to pass upstream; that appears to be the
	 * biggest value to which you can set it, and setting it to that value
	 * (which is bigger than what appears to be the Solaris default of 8192)
	 * reduces the number of packet drops.
	 */
#define	CHUNKSIZE	65536

	/*
	 * Size of the buffer to allocate for packet data we read; it must be
	 * large enough to hold a chunk.
	 */
#define	PKTBUFSIZE	CHUNKSIZE

#else /* HAVE_SYS_BUFMOD_H */

	/*
	 * Size of the buffer to allocate for packet data we read; this is
	 * what the value used to be - there's no particular reason why it
	 * should be tied to MAXDLBUF, but we'll leave it as this for now.
	 */
#define	MAXDLBUF	8192
#define	PKTBUFSIZE	(MAXDLBUF * sizeof(bpf_u_int32))

#endif

#include <sys/types.h>
#include <sys/time.h>
#ifdef HAVE_SYS_BUFMOD_H
#include <sys/bufmod.h>
#endif
#include <sys/dlpi.h>
#include <sys/stream.h>

#include <errno.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stropts.h>
#include <unistd.h>

#ifdef HAVE_LIBDLPI
#include <libdlpi.h>
#endif

#include "pcap-int.h"
#include "dlpisubs.h"

#ifdef HAVE_SYS_BUFMOD_H
static void pcap_stream_err(const char *, int, char *);
#endif

/*
 * Get the packet statistics.
 */
int
pcap_stats_dlpi(pcap_t *p, struct pcap_stat *ps)
{
	struct pcap_dlpi *pd = p->priv;

	/*
	 * "ps_recv" counts packets handed to the filter, not packets
	 * that passed the filter.  As filtering is done in userland,
	 * this would not include packets dropped because we ran out
	 * of buffer space; in order to make this more like other
	 * platforms (Linux 2.4 and later, BSDs with BPF), where the
	 * "packets received" count includes packets received but dropped
	 * due to running out of buffer space, and to keep from confusing
	 * applications that, for example, compute packet drop percentages,
	 * we also make it count packets dropped by "bufmod" (otherwise we
	 * might run the risk of the packet drop count being bigger than
	 * the received-packet count).
	 *
	 * "ps_drop" counts packets dropped by "bufmod" because of
	 * flow control requirements or resource exhaustion; it doesn't
	 * count packets dropped by the interface driver, or packets
	 * dropped upstream.  As filtering is done in userland, it counts
	 * packets regardless of whether they would've passed the filter.
	 *
	 * These statistics don't include packets not yet read from
	 * the kernel by libpcap, but they may include packets not
	 * yet read from libpcap by the application.
	 */
	*ps = pd->stat;

	/*
	 * Add in the drop count, as per the above comment.
	 */
	ps->ps_recv += ps->ps_drop;
	return (0);
}

/*
 * Loop through the packets and call the callback for each packet.
 * Return the number of packets read.
 */
int
pcap_process_pkts(pcap_t *p, pcap_handler callback, u_char *user,
	int count, u_char *bufp, int len)
{
	struct pcap_dlpi *pd = p->priv;
	int n, caplen, origlen;
	u_char *ep, *pk;
	struct pcap_pkthdr pkthdr;
#ifdef HAVE_SYS_BUFMOD_H
	struct sb_hdr *sbp;
#ifdef LBL_ALIGN
	struct sb_hdr sbhdr;
#endif
#endif

	/* Loop through packets */
	ep = bufp + len;
	n = 0;

#ifdef HAVE_SYS_BUFMOD_H
	while (bufp < ep) {
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
				p->bp = bufp;
				p->cc = ep - bufp;
				return (n);
			}
		}
#ifdef LBL_ALIGN
		if ((long)bufp & 3) {
			sbp = &sbhdr;
			memcpy(sbp, bufp, sizeof(*sbp));
		} else
#endif
			sbp = (struct sb_hdr *)bufp;
		pd->stat.ps_drop = sbp->sbh_drops;
		pk = bufp + sizeof(*sbp);
		bufp += sbp->sbh_totlen;
		origlen = sbp->sbh_origlen;
		caplen = sbp->sbh_msglen;
#else
		origlen = len;
		caplen = min(p->snapshot, len);
		pk = bufp;
		bufp += caplen;
#endif
		++pd->stat.ps_recv;
		if (bpf_filter(p->fcode.bf_insns, pk, origlen, caplen)) {
#ifdef HAVE_SYS_BUFMOD_H
			pkthdr.ts.tv_sec = sbp->sbh_timestamp.tv_sec;
			pkthdr.ts.tv_usec = sbp->sbh_timestamp.tv_usec;
#else
			(void) gettimeofday(&pkthdr.ts, NULL);
#endif
			pkthdr.len = origlen;
			pkthdr.caplen = caplen;
			/* Insure caplen does not exceed snapshot */
			if (pkthdr.caplen > (bpf_u_int32)p->snapshot)
				pkthdr.caplen = (bpf_u_int32)p->snapshot;
			(*callback)(user, &pkthdr, pk);
			if (++n >= count && !PACKET_COUNT_IS_UNLIMITED(count)) {
				p->cc = ep - bufp;
				p->bp = bufp;
				return (n);
			}
		}
#ifdef HAVE_SYS_BUFMOD_H
	}
#endif
	p->cc = 0;
	return (n);
}

/*
 * Process the mac type. Returns -1 if no matching mac type found, otherwise 0.
 */
int
pcap_process_mactype(pcap_t *p, u_int mactype)
{
	int retv = 0;

	switch (mactype) {

	case DL_CSMACD:
	case DL_ETHER:
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
		p->dlt_list = (u_int *)malloc(sizeof(u_int) * 2);
		/*
		 * If that fails, just leave the list empty.
		 */
		if (p->dlt_list != NULL) {
			p->dlt_list[0] = DLT_EN10MB;
			p->dlt_list[1] = DLT_DOCSIS;
			p->dlt_count = 2;
		}
		break;

	case DL_FDDI:
		p->linktype = DLT_FDDI;
		p->offset = 3;
		break;

	case DL_TPR:
		/* XXX - what about DL_TPB?  Is that Token Bus?  */
		p->linktype = DLT_IEEE802;
		p->offset = 2;
		break;

#ifdef HAVE_SOLARIS
	case DL_IPATM:
		p->linktype = DLT_SUNATM;
		p->offset = 0;  /* works for LANE and LLC encapsulation */
		break;
#endif

#ifdef DL_IPV4
	case DL_IPV4:
		p->linktype = DLT_IPV4;
		p->offset = 0;
		break;
#endif

#ifdef DL_IPV6
	case DL_IPV6:
		p->linktype = DLT_IPV6;
		p->offset = 0;
		break;
#endif

#ifdef DL_IPNET
	case DL_IPNET:
		/*
		 * XXX - DL_IPNET devices default to "raw IP" rather than
		 * "IPNET header"; see
		 *
		 *    http://seclists.org/tcpdump/2009/q1/202
		 *
		 * We'd have to do DL_IOC_IPNET_INFO to enable getting
		 * the IPNET header.
		 */
		p->linktype = DLT_RAW;
		p->offset = 0;
		break;
#endif

	default:
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "unknown mactype 0x%x",
		    mactype);
		retv = -1;
	}

	return (retv);
}

#ifdef HAVE_SYS_BUFMOD_H
/*
 * Push and configure the buffer module. Returns -1 for error, otherwise 0.
 */
int
pcap_conf_bufmod(pcap_t *p, int snaplen)
{
	struct timeval to;
	bpf_u_int32 ss, chunksize;

	/* Non-standard call to get the data nicely buffered. */
	if (ioctl(p->fd, I_PUSH, "bufmod") != 0) {
		pcap_stream_err("I_PUSH bufmod", errno, p->errbuf);
		return (-1);
	}

	ss = snaplen;
	if (ss > 0 &&
	    strioctl(p->fd, SBIOCSSNAP, sizeof(ss), (char *)&ss) != 0) {
		pcap_stream_err("SBIOCSSNAP", errno, p->errbuf);
		return (-1);
	}

	if (p->opt.immediate) {
		/* Set the timeout to zero, for immediate delivery. */
		to.tv_sec = 0;
		to.tv_usec = 0;
		if (strioctl(p->fd, SBIOCSTIME, sizeof(to), (char *)&to) != 0) {
			pcap_stream_err("SBIOCSTIME", errno, p->errbuf);
			return (-1);
		}
	} else {
		/* Set up the bufmod timeout. */
		if (p->opt.timeout != 0) {
			to.tv_sec = p->opt.timeout / 1000;
			to.tv_usec = (p->opt.timeout * 1000) % 1000000;
			if (strioctl(p->fd, SBIOCSTIME, sizeof(to), (char *)&to) != 0) {
				pcap_stream_err("SBIOCSTIME", errno, p->errbuf);
				return (-1);
			}
		}

		/* Set the chunk length. */
		chunksize = CHUNKSIZE;
		if (strioctl(p->fd, SBIOCSCHUNK, sizeof(chunksize), (char *)&chunksize)
		    != 0) {
			pcap_stream_err("SBIOCSCHUNKP", errno, p->errbuf);
			return (-1);
		}
	}

	return (0);
}
#endif /* HAVE_SYS_BUFMOD_H */

/*
 * Allocate data buffer. Returns -1 if memory allocation fails, else 0.
 */
int
pcap_alloc_databuf(pcap_t *p)
{
	p->bufsize = PKTBUFSIZE;
	p->buffer = malloc(p->bufsize + p->offset);
	if (p->buffer == NULL) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "malloc");
		return (-1);
	}

	return (0);
}

/*
 * Issue a STREAMS I_STR ioctl. Returns -1 on error, otherwise
 * length of returned data on success.
 */
int
strioctl(int fd, int cmd, int len, char *dp)
{
	struct strioctl str;
	int retv;

	str.ic_cmd = cmd;
	str.ic_timout = -1;
	str.ic_len = len;
	str.ic_dp = dp;
	if ((retv = ioctl(fd, I_STR, &str)) < 0)
		return (retv);

	return (str.ic_len);
}

#ifdef HAVE_SYS_BUFMOD_H
/*
 * Write stream error message to errbuf.
 */
static void
pcap_stream_err(const char *func, int err, char *errbuf)
{
	pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE, err, "%s", func);
}
#endif
