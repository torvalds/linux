/* vi: set sw=4 ts=4: */
/*
 * DHCP server client/server socket creation
 *
 * udhcp client/server
 * Copyright (C) 1999 Matthew Ramsay <matthewr@moreton.com.au>
 *			Chris Trew <ctrew@moreton.com.au>
 *
 * Rewrite by Russ Dill <Russ.Dill@asu.edu> July 2001
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include "common.h"
#include <net/if.h>

int FAST_FUNC udhcp_read_interface(const char *interface, int *ifindex, uint32_t *nip, uint8_t *mac)
{
	/* char buffer instead of bona-fide struct avoids aliasing warning */
	char ifr_buf[sizeof(struct ifreq)];
	struct ifreq *const ifr = (void *)ifr_buf;

	int fd;
	struct sockaddr_in *our_ip;

	memset(ifr, 0, sizeof(*ifr));
	fd = xsocket(AF_INET, SOCK_RAW, IPPROTO_RAW);

	ifr->ifr_addr.sa_family = AF_INET;
	strncpy_IFNAMSIZ(ifr->ifr_name, interface);
	if (nip) {
		if (ioctl_or_perror(fd, SIOCGIFADDR, ifr,
			"is interface %s up and configured?", interface)
		) {
			close(fd);
			return -1;
		}
		our_ip = (struct sockaddr_in *) &ifr->ifr_addr;
		*nip = our_ip->sin_addr.s_addr;
		log1("IP %s", inet_ntoa(our_ip->sin_addr));
	}

	if (ifindex) {
		if (ioctl_or_warn(fd, SIOCGIFINDEX, ifr) != 0) {
			close(fd);
			return -1;
		}
		log2("ifindex %d", ifr->ifr_ifindex);
		*ifindex = ifr->ifr_ifindex;
	}

	if (mac) {
		if (ioctl_or_warn(fd, SIOCGIFHWADDR, ifr) != 0) {
			close(fd);
			return -1;
		}
		memcpy(mac, ifr->ifr_hwaddr.sa_data, 6);
		log2("MAC %02x:%02x:%02x:%02x:%02x:%02x",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	}

	close(fd);
	return 0;
}

/* 1. None of the callers expects it to ever fail */
/* 2. ip was always INADDR_ANY */
int FAST_FUNC udhcp_listen_socket(/*uint32_t ip,*/ int port, const char *inf)
{
	int fd;
	struct sockaddr_in addr;
	char *colon;

	log1("opening listen socket on *:%d %s", port, inf);
	fd = xsocket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	setsockopt_reuseaddr(fd);
	if (setsockopt_broadcast(fd) == -1)
		bb_perror_msg_and_die("SO_BROADCAST");

	/* SO_BINDTODEVICE doesn't work on ethernet aliases (ethN:M) */
	colon = strrchr(inf, ':');
	if (colon)
		*colon = '\0';

	if (setsockopt_bindtodevice(fd, inf))
		xfunc_die(); /* warning is already printed */

	if (colon)
		*colon = ':';

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	/* addr.sin_addr.s_addr = ip; - all-zeros is INADDR_ANY */
	xbind(fd, (struct sockaddr *)&addr, sizeof(addr));

	return fd;
}
