/*
 * Copyright 2014 Google Inc.
 * Author: willemb@google.com (Willem de Bruijn)
 *
 * Test software tx timestamping, including
 *
 * - SCHED, SND and ACK timestamps
 * - RAW, UDP and TCP
 * - IPv4 and IPv6
 * - various packet sizes (to test GSO and TSO)
 *
 * Consult the command line arguments for help on running
 * the various testcases.
 *
 * This test requires a dummy TCP server.
 * A simple `nc6 [-u] -l -p $DESTPORT` will do
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. * See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <arpa/inet.h>
#include <asm/types.h>
#include <error.h>
#include <errno.h>
#include <linux/errqueue.h>
#include <linux/if_ether.h>
#include <linux/net_tstamp.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <netpacket/packet.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ugly hack to work around netinet/in.h and linux/ipv6.h conflicts */
#ifndef in6_pktinfo
struct in6_pktinfo {
	struct in6_addr	ipi6_addr;
	int		ipi6_ifindex;
};
#endif

/* command line parameters */
static int cfg_proto = SOCK_STREAM;
static int cfg_ipproto = IPPROTO_TCP;
static int cfg_num_pkts = 4;
static int do_ipv4 = 1;
static int do_ipv6 = 1;
static int cfg_payload_len = 10;
static bool cfg_show_payload;
static bool cfg_do_pktinfo;
static uint16_t dest_port = 9000;

static struct sockaddr_in daddr;
static struct sockaddr_in6 daddr6;
static struct timespec ts_prev;

static void __print_timestamp(const char *name, struct timespec *cur,
			      uint32_t key, int payload_len)
{
	if (!(cur->tv_sec | cur->tv_nsec))
		return;

	fprintf(stderr, "  %s: %lu s %lu us (seq=%u, len=%u)",
			name, cur->tv_sec, cur->tv_nsec / 1000,
			key, payload_len);

	if ((ts_prev.tv_sec | ts_prev.tv_nsec)) {
		int64_t cur_ms, prev_ms;

		cur_ms = (long) cur->tv_sec * 1000 * 1000;
		cur_ms += cur->tv_nsec / 1000;

		prev_ms = (long) ts_prev.tv_sec * 1000 * 1000;
		prev_ms += ts_prev.tv_nsec / 1000;

		fprintf(stderr, "  (%+ld us)", cur_ms - prev_ms);
	}

	ts_prev = *cur;
	fprintf(stderr, "\n");
}

static void print_timestamp_usr(void)
{
	struct timespec ts;
	struct timeval tv;	/* avoid dependency on -lrt */

	gettimeofday(&tv, NULL);
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = tv.tv_usec * 1000;

	__print_timestamp("  USR", &ts, 0, 0);
}

static void print_timestamp(struct scm_timestamping *tss, int tstype,
			    int tskey, int payload_len)
{
	const char *tsname;

	switch (tstype) {
	case SCM_TSTAMP_SCHED:
		tsname = "  ENQ";
		break;
	case SCM_TSTAMP_SND:
		tsname = "  SND";
		break;
	case SCM_TSTAMP_ACK:
		tsname = "  ACK";
		break;
	default:
		error(1, 0, "unknown timestamp type: %u",
		tstype);
	}
	__print_timestamp(tsname, &tss->ts[0], tskey, payload_len);
}

/* TODO: convert to check_and_print payload once API is stable */
static void print_payload(char *data, int len)
{
	int i;

	if (len > 70)
		len = 70;

	fprintf(stderr, "payload: ");
	for (i = 0; i < len; i++)
		fprintf(stderr, "%02hhx ", data[i]);
	fprintf(stderr, "\n");
}

static void print_pktinfo(int family, int ifindex, void *saddr, void *daddr)
{
	char sa[INET6_ADDRSTRLEN], da[INET6_ADDRSTRLEN];

	fprintf(stderr, "         pktinfo: ifindex=%u src=%s dst=%s\n",
		ifindex,
		saddr ? inet_ntop(family, saddr, sa, sizeof(sa)) : "unknown",
		daddr ? inet_ntop(family, daddr, da, sizeof(da)) : "unknown");
}

static void __poll(int fd)
{
	struct pollfd pollfd;
	int ret;

	memset(&pollfd, 0, sizeof(pollfd));
	pollfd.fd = fd;
	ret = poll(&pollfd, 1, 100);
	if (ret != 1)
		error(1, errno, "poll");
}

static void __recv_errmsg_cmsg(struct msghdr *msg, int payload_len)
{
	struct sock_extended_err *serr = NULL;
	struct scm_timestamping *tss = NULL;
	struct cmsghdr *cm;

	for (cm = CMSG_FIRSTHDR(msg);
	     cm && cm->cmsg_len;
	     cm = CMSG_NXTHDR(msg, cm)) {
		if (cm->cmsg_level == SOL_SOCKET &&
		    cm->cmsg_type == SCM_TIMESTAMPING) {
			tss = (void *) CMSG_DATA(cm);
		} else if ((cm->cmsg_level == SOL_IP &&
			    cm->cmsg_type == IP_RECVERR) ||
			   (cm->cmsg_level == SOL_IPV6 &&
			    cm->cmsg_type == IPV6_RECVERR)) {
			serr = (void *) CMSG_DATA(cm);
			if (serr->ee_errno != ENOMSG ||
			    serr->ee_origin != SO_EE_ORIGIN_TIMESTAMPING) {
				fprintf(stderr, "unknown ip error %d %d\n",
						serr->ee_errno,
						serr->ee_origin);
				serr = NULL;
			}
		} else if (cm->cmsg_level == SOL_IP &&
			   cm->cmsg_type == IP_PKTINFO) {
			struct in_pktinfo *info = (void *) CMSG_DATA(cm);
			print_pktinfo(AF_INET, info->ipi_ifindex,
				      &info->ipi_spec_dst, &info->ipi_addr);
		} else if (cm->cmsg_level == SOL_IPV6 &&
			   cm->cmsg_type == IPV6_PKTINFO) {
			struct in6_pktinfo *info6 = (void *) CMSG_DATA(cm);
			print_pktinfo(AF_INET6, info6->ipi6_ifindex,
				      NULL, &info6->ipi6_addr);
		} else
			fprintf(stderr, "unknown cmsg %d,%d\n",
					cm->cmsg_level, cm->cmsg_type);
	}

	if (serr && tss)
		print_timestamp(tss, serr->ee_info, serr->ee_data, payload_len);
}

static int recv_errmsg(int fd)
{
	static char ctrl[1024 /* overprovision*/];
	static struct msghdr msg;
	struct iovec entry;
	static char *data;
	int ret = 0;

	data = malloc(cfg_payload_len);
	if (!data)
		error(1, 0, "malloc");

	memset(&msg, 0, sizeof(msg));
	memset(&entry, 0, sizeof(entry));
	memset(ctrl, 0, sizeof(ctrl));

	entry.iov_base = data;
	entry.iov_len = cfg_payload_len;
	msg.msg_iov = &entry;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);

	ret = recvmsg(fd, &msg, MSG_ERRQUEUE);
	if (ret == -1 && errno != EAGAIN)
		error(1, errno, "recvmsg");

	if (ret > 0) {
		__recv_errmsg_cmsg(&msg, ret);
		if (cfg_show_payload)
			print_payload(data, cfg_payload_len);
	}

	free(data);
	return ret == -1;
}

static void do_test(int family, unsigned int opt)
{
	char *buf;
	int fd, i, val = 1, total_len;

	if (family == AF_INET6 && cfg_proto != SOCK_STREAM) {
		/* due to lack of checksum generation code */
		fprintf(stderr, "test: skipping datagram over IPv6\n");
		return;
	}

	total_len = cfg_payload_len;
	if (cfg_proto == SOCK_RAW) {
		total_len += sizeof(struct udphdr);
		if (cfg_ipproto == IPPROTO_RAW)
			total_len += sizeof(struct iphdr);
	}

	buf = malloc(total_len);
	if (!buf)
		error(1, 0, "malloc");

	fd = socket(family, cfg_proto, cfg_ipproto);
	if (fd < 0)
		error(1, errno, "socket");

	if (cfg_proto == SOCK_STREAM) {
		if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
			       (char*) &val, sizeof(val)))
			error(1, 0, "setsockopt no nagle");

		if (family == PF_INET) {
			if (connect(fd, (void *) &daddr, sizeof(daddr)))
				error(1, errno, "connect ipv4");
		} else {
			if (connect(fd, (void *) &daddr6, sizeof(daddr6)))
				error(1, errno, "connect ipv6");
		}
	}

	if (cfg_do_pktinfo) {
		if (family == AF_INET6) {
			if (setsockopt(fd, SOL_IPV6, IPV6_RECVPKTINFO,
				       &val, sizeof(val)))
				error(1, errno, "setsockopt pktinfo ipv6");
		} else {
			if (setsockopt(fd, SOL_IP, IP_PKTINFO,
				       &val, sizeof(val)))
				error(1, errno, "setsockopt pktinfo ipv4");
		}
	}

	opt |= SOF_TIMESTAMPING_SOFTWARE |
	       SOF_TIMESTAMPING_OPT_CMSG |
	       SOF_TIMESTAMPING_OPT_ID;
	if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING,
		       (char *) &opt, sizeof(opt)))
		error(1, 0, "setsockopt timestamping");

	for (i = 0; i < cfg_num_pkts; i++) {
		memset(&ts_prev, 0, sizeof(ts_prev));
		memset(buf, 'a' + i, total_len);

		if (cfg_proto == SOCK_RAW) {
			struct udphdr *udph;
			int off = 0;

			if (cfg_ipproto == IPPROTO_RAW) {
				struct iphdr *iph = (void *) buf;

				memset(iph, 0, sizeof(*iph));
				iph->ihl      = 5;
				iph->version  = 4;
				iph->ttl      = 2;
				iph->daddr    = daddr.sin_addr.s_addr;
				iph->protocol = IPPROTO_UDP;
				/* kernel writes saddr, csum, len */

				off = sizeof(*iph);
			}

			udph = (void *) buf + off;
			udph->source = ntohs(9000); 	/* random spoof */
			udph->dest   = ntohs(dest_port);
			udph->len    = ntohs(sizeof(*udph) + cfg_payload_len);
			udph->check  = 0;	/* not allowed for IPv6 */
		}

		print_timestamp_usr();
		if (cfg_proto != SOCK_STREAM) {
			if (family == PF_INET)
				val = sendto(fd, buf, total_len, 0, (void *) &daddr, sizeof(daddr));
			else
				val = sendto(fd, buf, total_len, 0, (void *) &daddr6, sizeof(daddr6));
		} else {
			val = send(fd, buf, cfg_payload_len, 0);
		}
		if (val != total_len)
			error(1, errno, "send");

		/* wait for all errors to be queued, else ACKs arrive OOO */
		usleep(50 * 1000);

		__poll(fd);

		while (!recv_errmsg(fd)) {}
	}

	if (close(fd))
		error(1, errno, "close");

	free(buf);
	usleep(400 * 1000);
}

static void __attribute__((noreturn)) usage(const char *filepath)
{
	fprintf(stderr, "\nUsage: %s [options] hostname\n"
			"\nwhere options are:\n"
			"  -4:   only IPv4\n"
			"  -6:   only IPv6\n"
			"  -h:   show this message\n"
			"  -I:   request PKTINFO\n"
			"  -l N: send N bytes at a time\n"
			"  -r:   use raw\n"
			"  -R:   use raw (IP_HDRINCL)\n"
			"  -p N: connect to port N\n"
			"  -u:   use udp\n"
			"  -x:   show payload (up to 70 bytes)\n",
			filepath);
	exit(1);
}

static void parse_opt(int argc, char **argv)
{
	int proto_count = 0;
	char c;

	while ((c = getopt(argc, argv, "46hIl:p:rRux")) != -1) {
		switch (c) {
		case '4':
			do_ipv6 = 0;
			break;
		case '6':
			do_ipv4 = 0;
			break;
		case 'I':
			cfg_do_pktinfo = true;
			break;
		case 'r':
			proto_count++;
			cfg_proto = SOCK_RAW;
			cfg_ipproto = IPPROTO_UDP;
			break;
		case 'R':
			proto_count++;
			cfg_proto = SOCK_RAW;
			cfg_ipproto = IPPROTO_RAW;
			break;
		case 'u':
			proto_count++;
			cfg_proto = SOCK_DGRAM;
			cfg_ipproto = IPPROTO_UDP;
			break;
		case 'l':
			cfg_payload_len = strtoul(optarg, NULL, 10);
			break;
		case 'p':
			dest_port = strtoul(optarg, NULL, 10);
			break;
		case 'x':
			cfg_show_payload = true;
			break;
		case 'h':
		default:
			usage(argv[0]);
		}
	}

	if (!cfg_payload_len)
		error(1, 0, "payload may not be nonzero");
	if (cfg_proto != SOCK_STREAM && cfg_payload_len > 1472)
		error(1, 0, "udp packet might exceed expected MTU");
	if (!do_ipv4 && !do_ipv6)
		error(1, 0, "pass -4 or -6, not both");
	if (proto_count > 1)
		error(1, 0, "pass -r, -R or -u, not multiple");

	if (optind != argc - 1)
		error(1, 0, "missing required hostname argument");
}

static void resolve_hostname(const char *hostname)
{
	struct addrinfo *addrs, *cur;
	int have_ipv4 = 0, have_ipv6 = 0;

	if (getaddrinfo(hostname, NULL, NULL, &addrs))
		error(1, errno, "getaddrinfo");

	cur = addrs;
	while (cur && !have_ipv4 && !have_ipv6) {
		if (!have_ipv4 && cur->ai_family == AF_INET) {
			memcpy(&daddr, cur->ai_addr, sizeof(daddr));
			daddr.sin_port = htons(dest_port);
			have_ipv4 = 1;
		}
		else if (!have_ipv6 && cur->ai_family == AF_INET6) {
			memcpy(&daddr6, cur->ai_addr, sizeof(daddr6));
			daddr6.sin6_port = htons(dest_port);
			have_ipv6 = 1;
		}
		cur = cur->ai_next;
	}
	if (addrs)
		freeaddrinfo(addrs);

	do_ipv4 &= have_ipv4;
	do_ipv6 &= have_ipv6;
}

static void do_main(int family)
{
	fprintf(stderr, "family:       %s\n",
			family == PF_INET ? "INET" : "INET6");

	fprintf(stderr, "test SND\n");
	do_test(family, SOF_TIMESTAMPING_TX_SOFTWARE);

	fprintf(stderr, "test ENQ\n");
	do_test(family, SOF_TIMESTAMPING_TX_SCHED);

	fprintf(stderr, "test ENQ + SND\n");
	do_test(family, SOF_TIMESTAMPING_TX_SCHED |
			SOF_TIMESTAMPING_TX_SOFTWARE);

	if (cfg_proto == SOCK_STREAM) {
		fprintf(stderr, "\ntest ACK\n");
		do_test(family, SOF_TIMESTAMPING_TX_ACK);

		fprintf(stderr, "\ntest SND + ACK\n");
		do_test(family, SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_TX_ACK);

		fprintf(stderr, "\ntest ENQ + SND + ACK\n");
		do_test(family, SOF_TIMESTAMPING_TX_SCHED |
				SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_TX_ACK);
	}
}

const char *sock_names[] = { NULL, "TCP", "UDP", "RAW" };

int main(int argc, char **argv)
{
	if (argc == 1)
		usage(argv[0]);

	parse_opt(argc, argv);
	resolve_hostname(argv[argc - 1]);

	fprintf(stderr, "protocol:     %s\n", sock_names[cfg_proto]);
	fprintf(stderr, "payload:      %u\n", cfg_payload_len);
	fprintf(stderr, "server port:  %u\n", dest_port);
	fprintf(stderr, "\n");

	if (do_ipv4)
		do_main(PF_INET);
	if (do_ipv6)
		do_main(PF_INET6);

	return 0;
}
