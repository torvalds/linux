/* vi: set sw=4 ts=4: */
/*
 * ether-wake.c - Send a magic packet to wake up sleeping machines.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Author:      Donald Becker, http://www.scyld.com/"; http://www.scyld.com/wakeonlan.html
 * Busybox port: Christian Volkmann <haveaniceday@online.de>
 *               Used version of ether-wake.c: v1.09 11/12/2003 Donald Becker, http://www.scyld.com/";
 */

/* full usage according Donald Becker
 * usage: ether-wake [-i <ifname>] [-p aa:bb:cc:dd[:ee:ff]] 00:11:22:33:44:55\n"
 *
 *	This program generates and transmits a Wake-On-LAN (WOL)\n"
 *	\"Magic Packet\", used for restarting machines that have been\n"
 *	soft-powered-down (ACPI D3-warm state).\n"
 *	It currently generates the standard AMD Magic Packet format, with\n"
 *	an optional password appended.\n"
 *
 *	The single required parameter is the Ethernet MAC (station) address\n"
 *	of the machine to wake or a host ID with known NSS 'ethers' entry.\n"
 *	The MAC address may be found with the 'arp' program while the target\n"
 *	machine is awake.\n"
 *
 *	Options:\n"
 *		-b	Send wake-up packet to the broadcast address.\n"
 *		-D	Increase the debug level.\n"
 *		-i ifname	Use interface IFNAME instead of the default 'eth0'.\n"
 *		-p <pw>		Append the four or six byte password PW to the packet.\n"
 *					A password is only required for a few adapter types.\n"
 *					The password may be specified in ethernet hex format\n"
 *					or dotted decimal (Internet address)\n"
 *		-p 00:22:44:66:88:aa\n"
 *		-p 192.168.1.1\n";
 *
 *
 *	This program generates and transmits a Wake-On-LAN (WOL) "Magic Packet",
 *	used for restarting machines that have been soft-powered-down
 *	(ACPI D3-warm state).  It currently generates the standard AMD Magic Packet
 *	format, with an optional password appended.
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU Public License, incorporated herein by reference.
 *	Contact the author for use under other terms.
 *
 *	This source file was originally part of the network tricks package, and
 *	is now distributed to support the Scyld Beowulf system.
 *	Copyright 1999-2003 Donald Becker and Scyld Computing Corporation.
 *
 *	The author may be reached as becker@scyld, or C/O
 *	Scyld Computing Corporation
 *	914 Bay Ridge Road, Suite 220
 *	Annapolis MD 21403
 *
 *   Notes:
 *   On some systems dropping root capability allows the process to be
 *   dumped, traced or debugged.
 *   If someone traces this program, they get control of a raw socket.
 *   Linux handles this safely, but beware when porting this program.
 *
 *   An alternative to needing 'root' is using a UDP broadcast socket, however
 *   doing so only works with adapters configured for unicast+broadcast Rx
 *   filter.  That configuration consumes more power.
 */
//config:config ETHER_WAKE
//config:	bool "ether-wake (6.6 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Send a magic packet to wake up sleeping machines.

//applet:IF_ETHER_WAKE(APPLET_ODDNAME(ether-wake, ether_wake, BB_DIR_USR_SBIN, BB_SUID_DROP, ether_wake))

//kbuild:lib-$(CONFIG_ETHER_WAKE) += ether-wake.o

//usage:#define ether_wake_trivial_usage
//usage:       "[-b] [-i IFACE] [-p aa:bb:cc:dd[:ee:ff]/a.b.c.d] MAC"
//usage:#define ether_wake_full_usage "\n\n"
//usage:       "Send a magic packet to wake up sleeping machines.\n"
//usage:       "MAC must be a station address (00:11:22:33:44:55) or\n"
//usage:       "a hostname with a known 'ethers' entry.\n"
//usage:     "\n	-b		Broadcast the packet"
//usage:     "\n	-i IFACE	Interface to use (default eth0)"
//usage:     "\n	-p PASSWORD	Append four or six byte PASSWORD to the packet"

#include "libbb.h"
#include <netpacket/packet.h>
#include <netinet/ether.h>
#include <linux/if.h>

/* Note: PF_INET, SOCK_DGRAM, IPPROTO_UDP would allow SIOCGIFHWADDR to
 * work as non-root, but we need SOCK_PACKET to specify the Ethernet
 * destination address.
 */
#ifdef PF_PACKET
# define whereto_t sockaddr_ll
# define make_socket() xsocket(PF_PACKET, SOCK_RAW, 0)
#else
# define whereto_t sockaddr
# define make_socket() xsocket(AF_INET, SOCK_PACKET, SOCK_PACKET)
#endif

#ifdef DEBUG
# define bb_debug_msg(fmt, args...) fprintf(stderr, fmt, ## args)
void bb_debug_dump_packet(unsigned char *outpack, int pktsize)
{
	int i;
	printf("packet dump:\n");
	for (i = 0; i < pktsize; ++i) {
		printf("%2.2x ", outpack[i]);
		if (i % 20 == 19) bb_putchar('\n');
	}
	printf("\n\n");
}
#else
# define bb_debug_msg(fmt, args...)             ((void)0)
# define bb_debug_dump_packet(outpack, pktsize) ((void)0)
#endif

/* Convert the host ID string to a MAC address.
 * The string may be a:
 *    Host name
 *    IP address string
 *    MAC address string
 */
static void get_dest_addr(const char *hostid, struct ether_addr *eaddr)
{
	struct ether_addr *eap;

	eap = ether_aton_r(hostid, eaddr);
	if (eap) {
		bb_debug_msg("The target station address is %s\n\n", ether_ntoa(eap));
#if !defined(__UCLIBC__) || UCLIBC_VERSION >= KERNEL_VERSION(0, 9, 30)
	} else if (ether_hostton(hostid, eaddr) == 0) {
		bb_debug_msg("Station address for hostname %s is %s\n\n", hostid, ether_ntoa(eaddr));
#endif
	} else {
		bb_show_usage();
	}
}

#define PKT_HEADER_SIZE (20 + 16*6)
static int fill_pkt_header(unsigned char *pkt, struct ether_addr *eaddr, int broadcast)
{
	int i;
	unsigned char *station_addr = eaddr->ether_addr_octet;

	memset(pkt, 0xff, 6);
	if (!broadcast)
		memcpy(pkt, station_addr, 6);
	pkt += 6;

	memcpy(pkt, station_addr, 6); /* 6 */
	pkt += 6;

	*pkt++ = 0x08; /* 12 */ /* Or 0x0806 for ARP, 0x8035 for RARP */
	*pkt++ = 0x42; /* 13 */

	memset(pkt, 0xff, 6); /* 14 */

	for (i = 0; i < 16; ++i) {
		pkt += 6;
		memcpy(pkt, station_addr, 6); /* 20,26,32,... */
	}

	return PKT_HEADER_SIZE; /* length of packet */
}

static int get_wol_pw(const char *ethoptarg, unsigned char *wol_passwd)
{
	unsigned passwd[6];
	int byte_cnt, i;

	/* handle MAC format */
	byte_cnt = sscanf(ethoptarg, "%2x:%2x:%2x:%2x:%2x:%2x",
	                  &passwd[0], &passwd[1], &passwd[2],
	                  &passwd[3], &passwd[4], &passwd[5]);
	/* handle IP format */
// FIXME: why < 4?? should it be < 6?
	if (byte_cnt < 4)
		byte_cnt = sscanf(ethoptarg, "%u.%u.%u.%u",
		                  &passwd[0], &passwd[1], &passwd[2], &passwd[3]);
	if (byte_cnt < 4) {
		bb_error_msg("can't read Wake-On-LAN pass");
		return 0;
	}
// TODO: check invalid numbers >255??
	for (i = 0; i < byte_cnt; ++i)
		wol_passwd[i] = passwd[i];

	bb_debug_msg("password: %2.2x %2.2x %2.2x %2.2x (%d)\n\n",
	             wol_passwd[0], wol_passwd[1], wol_passwd[2], wol_passwd[3],
	             byte_cnt);

	return byte_cnt;
}

int ether_wake_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ether_wake_main(int argc UNUSED_PARAM, char **argv)
{
	const char *ifname = "eth0";
	char *pass;
	unsigned flags;
	unsigned char wol_passwd[6];
	int wol_passwd_sz = 0;
	int s;  /* Raw socket */
	int pktsize;
	unsigned char outpack[PKT_HEADER_SIZE + 6 /* max passwd size */ + 16 /* paranoia */];

	struct ether_addr eaddr;
	struct whereto_t whereto;  /* who to wake up */

	/* handle misc user options */
	flags = getopt32(argv, "^" "bi:p:" "\0" "=1", &ifname, &pass);
	if (flags & 4) /* -p */
		wol_passwd_sz = get_wol_pw(pass, wol_passwd);
	flags &= 1; /* we further interested only in -b [bcast] flag */

	/* create the raw socket */
	s = make_socket();

	/* now that we have a raw socket we can drop root */
	/* xsetuid(getuid()); - but save on code size... */

	/* look up the dest mac address */
	get_dest_addr(argv[optind], &eaddr);

	/* fill out the header of the packet */
	pktsize = fill_pkt_header(outpack, &eaddr, flags /* & 1 OPT_BROADCAST */);

	bb_debug_dump_packet(outpack, pktsize);

	/* Fill in the source address, if possible. */
#ifdef __linux__
	{
		struct ifreq if_hwaddr;

		strncpy_IFNAMSIZ(if_hwaddr.ifr_name, ifname);
		ioctl_or_perror_and_die(s, SIOCGIFHWADDR, &if_hwaddr, "SIOCGIFHWADDR on %s failed", ifname);

		memcpy(outpack+6, if_hwaddr.ifr_hwaddr.sa_data, 6);

# ifdef DEBUG
		{
			unsigned char *hwaddr = if_hwaddr.ifr_hwaddr.sa_data;
			printf("The hardware address (SIOCGIFHWADDR) of %s is type %d  "
				"%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n\n", ifname,
				if_hwaddr.ifr_hwaddr.sa_family, hwaddr[0], hwaddr[1],
				hwaddr[2], hwaddr[3], hwaddr[4], hwaddr[5]);
		}
# endif
	}
#endif /* __linux__ */

	bb_debug_dump_packet(outpack, pktsize);

	/* append the password if specified */
	if (wol_passwd_sz > 0) {
		memcpy(outpack+pktsize, wol_passwd, wol_passwd_sz);
		pktsize += wol_passwd_sz;
	}

	bb_debug_dump_packet(outpack, pktsize);

	/* This is necessary for broadcasts to work */
	if (flags /* & 1 OPT_BROADCAST */) {
		if (setsockopt_broadcast(s) != 0)
			bb_perror_msg("SO_BROADCAST");
	}

#if defined(PF_PACKET)
	{
		struct ifreq ifr;
		strncpy_IFNAMSIZ(ifr.ifr_name, ifname);
		xioctl(s, SIOCGIFINDEX, &ifr);
		memset(&whereto, 0, sizeof(whereto));
		whereto.sll_family = AF_PACKET;
		whereto.sll_ifindex = ifr.ifr_ifindex;
		/* The manual page incorrectly claims the address must be filled.
		   We do so because the code may change to match the docs. */
		whereto.sll_halen = ETH_ALEN;
		memcpy(whereto.sll_addr, outpack, ETH_ALEN);
	}
#else
	whereto.sa_family = 0;
	strcpy(whereto.sa_data, ifname);
#endif
	xsendto(s, outpack, pktsize, (struct sockaddr *)&whereto, sizeof(whereto));
	if (ENABLE_FEATURE_CLEAN_UP)
		close(s);
	return EXIT_SUCCESS;
}
