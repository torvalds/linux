/* vi: set sw=4 ts=4: */
/*
 * Packet ops
 *
 * Rewrite by Russ Dill <Russ.Dill@asu.edu> July 2001
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "common.h"
#include "dhcpd.h"
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netpacket/packet.h>

#if ENABLE_UDHCPC || ENABLE_UDHCPD
void FAST_FUNC udhcp_init_header(struct dhcp_packet *packet, char type)
{
	memset(packet, 0, sizeof(*packet));
	packet->op = BOOTREQUEST; /* if client to a server */
	switch (type) {
	case DHCPOFFER:
	case DHCPACK:
	case DHCPNAK:
		packet->op = BOOTREPLY; /* if server to client */
	}
	packet->htype = 1; /* ethernet */
	packet->hlen = 6;
	packet->cookie = htonl(DHCP_MAGIC);
	if (DHCP_END != 0)
		packet->options[0] = DHCP_END;
	udhcp_add_simple_option(packet, DHCP_MESSAGE_TYPE, type);
}
#endif

#if defined CONFIG_UDHCP_DEBUG && CONFIG_UDHCP_DEBUG >= 2
void FAST_FUNC udhcp_dump_packet(struct dhcp_packet *packet)
{
	char buf[sizeof(packet->chaddr)*2 + 1];

	if (dhcp_verbose < 2)
		return;

	bb_error_msg(
		//" op %x"
		//" htype %x"
		" hlen %x"
		//" hops %x"
		" xid %x"
		//" secs %x"
		//" flags %x"
		" ciaddr %x"
		" yiaddr %x"
		" siaddr %x"
		" giaddr %x"
		//" sname %s"
		//" file %s"
		//" cookie %x"
		//" options %s"
		//, packet->op
		//, packet->htype
		, packet->hlen
		//, packet->hops
		, packet->xid
		//, packet->secs
		//, packet->flags
		, packet->ciaddr
		, packet->yiaddr
		, packet->siaddr_nip
		, packet->gateway_nip
		//, packet->sname[64]
		//, packet->file[128]
		//, packet->cookie
		//, packet->options[]
	);
	*bin2hex(buf, (void *) packet->chaddr, sizeof(packet->chaddr)) = '\0';
	bb_error_msg(" chaddr %s", buf);
}
#endif

/* Read a packet from socket fd, return -1 on read error, -2 on packet error */
int FAST_FUNC udhcp_recv_kernel_packet(struct dhcp_packet *packet, int fd)
{
	int bytes;

	memset(packet, 0, sizeof(*packet));
	bytes = safe_read(fd, packet, sizeof(*packet));
	if (bytes < 0) {
		log1("packet read error, ignoring");
		return bytes; /* returns -1 */
	}

	if (bytes < offsetof(struct dhcp_packet, options)
	 || packet->cookie != htonl(DHCP_MAGIC)
	) {
		bb_error_msg("packet with bad magic, ignoring");
		return -2;
	}
	log1("received %s", "a packet");
	udhcp_dump_packet(packet);

	return bytes;
}

/* Construct a ip/udp header for a packet, send packet */
int FAST_FUNC udhcp_send_raw_packet(struct dhcp_packet *dhcp_pkt,
		uint32_t source_nip, int source_port,
		uint32_t dest_nip, int dest_port, const uint8_t *dest_arp,
		int ifindex)
{
	struct sockaddr_ll dest_sll;
	struct ip_udp_dhcp_packet packet;
	unsigned padding;
	int fd;
	int result = -1;
	const char *msg;

	fd = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
	if (fd < 0) {
		msg = "socket(%s)";
		goto ret_msg;
	}

	memset(&dest_sll, 0, sizeof(dest_sll));
	memset(&packet, 0, offsetof(struct ip_udp_dhcp_packet, data));
	packet.data = *dhcp_pkt; /* struct copy */

	dest_sll.sll_family = AF_PACKET;
	dest_sll.sll_protocol = htons(ETH_P_IP);
	dest_sll.sll_ifindex = ifindex;
	/*dest_sll.sll_hatype = ARPHRD_???;*/
	/*dest_sll.sll_pkttype = PACKET_???;*/
	dest_sll.sll_halen = 6;
	memcpy(dest_sll.sll_addr, dest_arp, 6);

	if (bind(fd, (struct sockaddr *)&dest_sll, sizeof(dest_sll)) < 0) {
		msg = "bind(%s)";
		goto ret_close;
	}

	/* We were sending full-sized DHCP packets (zero padded),
	 * but some badly configured servers were seen dropping them.
	 * Apparently they drop all DHCP packets >576 *ethernet* octets big,
	 * whereas they may only drop packets >576 *IP* octets big
	 * (which for typical Ethernet II means 590 octets: 6+6+2 + 576).
	 *
	 * In order to work with those buggy servers,
	 * we truncate packets after end option byte.
	 *
	 * However, RFC 1542 says "The IP Total Length and UDP Length
	 * must be large enough to contain the minimal BOOTP header of 300 octets".
	 * Thus, we retain enough padding to not go below 300 BOOTP bytes.
	 * Some devices have filters which drop DHCP packets shorter than that.
	 */
	padding = DHCP_OPTIONS_BUFSIZE - 1 - udhcp_end_option(packet.data.options);
	if (padding > DHCP_SIZE - 300)
		padding = DHCP_SIZE - 300;

	packet.ip.protocol = IPPROTO_UDP;
	packet.ip.saddr = source_nip;
	packet.ip.daddr = dest_nip;
	packet.udp.source = htons(source_port);
	packet.udp.dest = htons(dest_port);
	/* size, excluding IP header: */
	packet.udp.len = htons(UDP_DHCP_SIZE - padding);
	/* for UDP checksumming, ip.len is set to UDP packet len */
	packet.ip.tot_len = packet.udp.len;
	packet.udp.check = inet_cksum((uint16_t *)&packet,
			IP_UDP_DHCP_SIZE - padding);
	/* but for sending, it is set to IP packet len */
	packet.ip.tot_len = htons(IP_UDP_DHCP_SIZE - padding);
	packet.ip.ihl = sizeof(packet.ip) >> 2;
	packet.ip.version = IPVERSION;
	packet.ip.ttl = IPDEFTTL;
	packet.ip.check = inet_cksum((uint16_t *)&packet.ip, sizeof(packet.ip));

	udhcp_dump_packet(dhcp_pkt);
	result = sendto(fd, &packet, IP_UDP_DHCP_SIZE - padding, /*flags:*/ 0,
			(struct sockaddr *) &dest_sll, sizeof(dest_sll));
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
int FAST_FUNC udhcp_send_kernel_packet(struct dhcp_packet *dhcp_pkt,
		uint32_t source_nip, int source_port,
		uint32_t dest_nip, int dest_port)
{
	struct sockaddr_in sa;
	unsigned padding;
	int fd;
	int result = -1;
	const char *msg;

	fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		msg = "socket(%s)";
		goto ret_msg;
	}
	setsockopt_reuseaddr(fd);

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(source_port);
	sa.sin_addr.s_addr = source_nip;
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		msg = "bind(%s)";
		goto ret_close;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(dest_port);
	sa.sin_addr.s_addr = dest_nip;
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		msg = "connect";
		goto ret_close;
	}

	udhcp_dump_packet(dhcp_pkt);
	padding = DHCP_OPTIONS_BUFSIZE - 1 - udhcp_end_option(dhcp_pkt->options);
	if (padding > DHCP_SIZE - 300)
		padding = DHCP_SIZE - 300;
	result = safe_write(fd, dhcp_pkt, DHCP_SIZE - padding);
	msg = "write";
 ret_close:
	close(fd);
	if (result < 0) {
 ret_msg:
		bb_perror_msg(msg, "UDP");
	}
	return result;
}
