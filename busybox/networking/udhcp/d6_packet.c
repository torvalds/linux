/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2011 Denys Vlasenko.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "common.h"
#include "d6_common.h"
#include "dhcpd.h"
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netpacket/packet.h>

#if defined CONFIG_UDHCP_DEBUG && CONFIG_UDHCP_DEBUG >= 2
void FAST_FUNC d6_dump_packet(struct d6_packet *packet)
{
	if (dhcp_verbose < 2)
		return;

	bb_error_msg(
		" xid %x"
		, packet->d6_xid32
	);
	//*bin2hex(buf, (void *) packet->chaddr, sizeof(packet->chaddr)) = '\0';
	//bb_error_msg(" chaddr %s", buf);
}
#endif

int FAST_FUNC d6_recv_kernel_packet(struct in6_addr *peer_ipv6
	UNUSED_PARAM
	, struct d6_packet *packet, int fd)
{
	int bytes;

	memset(packet, 0, sizeof(*packet));
	bytes = safe_read(fd, packet, sizeof(*packet));
	if (bytes < 0) {
		log1("packet read error, ignoring");
		return bytes; /* returns -1 */
	}

	if (bytes < offsetof(struct d6_packet, d6_options)) {
		bb_error_msg("packet with bad magic, ignoring");
		return -2;
	}
	log1("received %s", "a packet");
	d6_dump_packet(packet);

	return bytes;
}

/* Construct a ipv6+udp header for a packet, send packet */
int FAST_FUNC d6_send_raw_packet(
		struct d6_packet *d6_pkt, unsigned d6_pkt_size,
		struct in6_addr *src_ipv6, int source_port,
		struct in6_addr *dst_ipv6, int dest_port, const uint8_t *dest_arp,
		int ifindex)
{
	struct sockaddr_ll dest_sll;
	struct ip6_udp_d6_packet packet;
	int fd;
	int result = -1;
	const char *msg;

	fd = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_IPV6));
	if (fd < 0) {
		msg = "socket(%s)";
		goto ret_msg;
	}

	memset(&dest_sll, 0, sizeof(dest_sll));
	memset(&packet, 0, offsetof(struct ip6_udp_d6_packet, data));
	packet.data = *d6_pkt; /* struct copy */

	dest_sll.sll_family = AF_PACKET;
	dest_sll.sll_protocol = htons(ETH_P_IPV6);
	dest_sll.sll_ifindex = ifindex;
	/*dest_sll.sll_hatype = ARPHRD_???;*/
	/*dest_sll.sll_pkttype = PACKET_???;*/
	dest_sll.sll_halen = 6;
	memcpy(dest_sll.sll_addr, dest_arp, 6);

	if (bind(fd, (struct sockaddr *)&dest_sll, sizeof(dest_sll)) < 0) {
		msg = "bind(%s)";
		goto ret_close;
	}

	packet.ip6.ip6_vfc = (6 << 4); /* 4 bits version, top 4 bits of tclass */
	if (src_ipv6)
		packet.ip6.ip6_src = *src_ipv6; /* struct copy */
	packet.ip6.ip6_dst = *dst_ipv6; /* struct copy */
	packet.udp.source = htons(source_port);
	packet.udp.dest = htons(dest_port);
	/* size, excluding IP header: */
	packet.udp.len = htons(sizeof(struct udphdr) + d6_pkt_size);
	packet.ip6.ip6_plen = packet.udp.len;
	/*
	 * Someone was smoking weed (at least) while inventing UDP checksumming:
	 * UDP checksum skips first four bytes of IPv6 header.
	 * 'next header' field should be summed as if it is one more byte
	 * to the right, therefore we write its value (IPPROTO_UDP)
	 * into ip6_hlim, and its 'real' location remains zero-filled for now.
	 */
	packet.ip6.ip6_hlim = IPPROTO_UDP;
	packet.udp.check = inet_cksum(
				(uint16_t *)&packet + 2,
				offsetof(struct ip6_udp_d6_packet, data) - 4 + d6_pkt_size
	);
	/* fix 'hop limit' and 'next header' after UDP checksumming */
	packet.ip6.ip6_hlim = 1; /* observed Windows machines to use hlim=1 */
	packet.ip6.ip6_nxt = IPPROTO_UDP;

	d6_dump_packet(d6_pkt);
	result = sendto(fd, &packet, offsetof(struct ip6_udp_d6_packet, data) + d6_pkt_size,
			/*flags:*/ 0,
			(struct sockaddr *) &dest_sll, sizeof(dest_sll)
	);
	msg = "sendto";
 ret_close:
	close(fd);
	if (result < 0) {
 ret_msg:
		bb_perror_msg(msg, "PACKET");
	}
	return result;
}

/* Let the kernel do all the work for packet generation */
int FAST_FUNC d6_send_kernel_packet(
		struct d6_packet *d6_pkt, unsigned d6_pkt_size,
		struct in6_addr *src_ipv6, int source_port,
		struct in6_addr *dst_ipv6, int dest_port,
		int ifindex)
{
	struct sockaddr_in6 sa;
	int fd;
	int result = -1;
	const char *msg;

	fd = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		msg = "socket(%s)";
		goto ret_msg;
	}
	setsockopt_reuseaddr(fd);

	memset(&sa, 0, sizeof(sa));
	sa.sin6_family = AF_INET6;
	sa.sin6_port = htons(source_port);
	sa.sin6_addr = *src_ipv6; /* struct copy */
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		msg = "bind(%s)";
		goto ret_close;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sin6_family = AF_INET6;
	sa.sin6_port = htons(dest_port);
	sa.sin6_addr = *dst_ipv6; /* struct copy */
	sa.sin6_scope_id = ifindex;
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		msg = "connect";
		goto ret_close;
	}

	d6_dump_packet(d6_pkt);
	result = safe_write(fd, d6_pkt, d6_pkt_size);
	msg = "write";
 ret_close:
	close(fd);
	if (result < 0) {
 ret_msg:
		bb_perror_msg(msg, "UDP");
	}
	return result;
}
