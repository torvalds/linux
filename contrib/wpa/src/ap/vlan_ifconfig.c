/*
 * hostapd / VLAN ifconfig helpers
 * Copyright 2003, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#include <net/if.h>
#include <sys/ioctl.h>

#include "utils/common.h"
#include "vlan_util.h"


int ifconfig_helper(const char *if_name, int up)
{
	int fd;
	struct ifreq ifr;

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: socket(AF_INET,SOCK_STREAM) "
			   "failed: %s", __func__, strerror(errno));
		return -1;
	}

	os_memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, if_name, IFNAMSIZ);

	if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: ioctl(SIOCGIFFLAGS) failed "
			   "for interface %s: %s",
			   __func__, if_name, strerror(errno));
		close(fd);
		return -1;
	}

	if (up)
		ifr.ifr_flags |= IFF_UP;
	else
		ifr.ifr_flags &= ~IFF_UP;

	if (ioctl(fd, SIOCSIFFLAGS, &ifr) != 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: ioctl(SIOCSIFFLAGS) failed "
			   "for interface %s (up=%d): %s",
			   __func__, if_name, up, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}


int ifconfig_up(const char *if_name)
{
	wpa_printf(MSG_DEBUG, "VLAN: Set interface %s up", if_name);
	return ifconfig_helper(if_name, 1);
}


int iface_exists(const char *ifname)
{
	return if_nametoindex(ifname);
}
