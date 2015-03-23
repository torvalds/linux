/*
 * hostapd / VLAN initialization
 * Copyright 2003, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "hostapd.h"
#include "ap_config.h"
#include "ap_drv_ops.h"
#include "vlan_init.h"


#ifdef CONFIG_FULL_DYNAMIC_VLAN

#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <linux/if_vlan.h>
#include <linux/if_bridge.h>

#include "drivers/priv_netlink.h"
#include "utils/eloop.h"


struct full_dynamic_vlan {
	int s; /* socket on which to listen for new/removed interfaces. */
};


static int ifconfig_helper(const char *if_name, int up)
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


static int ifconfig_up(const char *if_name)
{
	wpa_printf(MSG_DEBUG, "VLAN: Set interface %s up", if_name);
	return ifconfig_helper(if_name, 1);
}


static int ifconfig_down(const char *if_name)
{
	wpa_printf(MSG_DEBUG, "VLAN: Set interface %s down", if_name);
	return ifconfig_helper(if_name, 0);
}


/*
 * These are only available in recent linux headers (without the leading
 * underscore).
 */
#define _GET_VLAN_REALDEV_NAME_CMD	8
#define _GET_VLAN_VID_CMD		9

/* This value should be 256 ONLY. If it is something else, then hostapd
 * might crash!, as this value has been hard-coded in 2.4.x kernel
 * bridging code.
 */
#define MAX_BR_PORTS      		256

static int br_delif(const char *br_name, const char *if_name)
{
	int fd;
	struct ifreq ifr;
	unsigned long args[2];
	int if_index;

	wpa_printf(MSG_DEBUG, "VLAN: br_delif(%s, %s)", br_name, if_name);
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: socket(AF_INET,SOCK_STREAM) "
			   "failed: %s", __func__, strerror(errno));
		return -1;
	}

	if_index = if_nametoindex(if_name);

	if (if_index == 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: Failure determining "
			   "interface index for '%s'",
			   __func__, if_name);
		close(fd);
		return -1;
	}

	args[0] = BRCTL_DEL_IF;
	args[1] = if_index;

	os_strlcpy(ifr.ifr_name, br_name, sizeof(ifr.ifr_name));
	ifr.ifr_data = (__caddr_t) args;

	if (ioctl(fd, SIOCDEVPRIVATE, &ifr) < 0 && errno != EINVAL) {
		/* No error if interface already removed. */
		wpa_printf(MSG_ERROR, "VLAN: %s: ioctl[SIOCDEVPRIVATE,"
			   "BRCTL_DEL_IF] failed for br_name=%s if_name=%s: "
			   "%s", __func__, br_name, if_name, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}


/*
	Add interface 'if_name' to the bridge 'br_name'

	returns -1 on error
	returns 1 if the interface is already part of the bridge
	returns 0 otherwise
*/
static int br_addif(const char *br_name, const char *if_name)
{
	int fd;
	struct ifreq ifr;
	unsigned long args[2];
	int if_index;

	wpa_printf(MSG_DEBUG, "VLAN: br_addif(%s, %s)", br_name, if_name);
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: socket(AF_INET,SOCK_STREAM) "
			   "failed: %s", __func__, strerror(errno));
		return -1;
	}

	if_index = if_nametoindex(if_name);

	if (if_index == 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: Failure determining "
			   "interface index for '%s'",
			   __func__, if_name);
		close(fd);
		return -1;
	}

	args[0] = BRCTL_ADD_IF;
	args[1] = if_index;

	os_strlcpy(ifr.ifr_name, br_name, sizeof(ifr.ifr_name));
	ifr.ifr_data = (__caddr_t) args;

	if (ioctl(fd, SIOCDEVPRIVATE, &ifr) < 0) {
		if (errno == EBUSY) {
			/* The interface is already added. */
			close(fd);
			return 1;
		}

		wpa_printf(MSG_ERROR, "VLAN: %s: ioctl[SIOCDEVPRIVATE,"
			   "BRCTL_ADD_IF] failed for br_name=%s if_name=%s: "
			   "%s", __func__, br_name, if_name, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}


static int br_delbr(const char *br_name)
{
	int fd;
	unsigned long arg[2];

	wpa_printf(MSG_DEBUG, "VLAN: br_delbr(%s)", br_name);
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: socket(AF_INET,SOCK_STREAM) "
			   "failed: %s", __func__, strerror(errno));
		return -1;
	}

	arg[0] = BRCTL_DEL_BRIDGE;
	arg[1] = (unsigned long) br_name;

	if (ioctl(fd, SIOCGIFBR, arg) < 0 && errno != ENXIO) {
		/* No error if bridge already removed. */
		wpa_printf(MSG_ERROR, "VLAN: %s: BRCTL_DEL_BRIDGE failed for "
			   "%s: %s", __func__, br_name, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}


/*
	Add a bridge with the name 'br_name'.

	returns -1 on error
	returns 1 if the bridge already exists
	returns 0 otherwise
*/
static int br_addbr(const char *br_name)
{
	int fd;
	unsigned long arg[4];
	struct ifreq ifr;

	wpa_printf(MSG_DEBUG, "VLAN: br_addbr(%s)", br_name);
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: socket(AF_INET,SOCK_STREAM) "
			   "failed: %s", __func__, strerror(errno));
		return -1;
	}

	arg[0] = BRCTL_ADD_BRIDGE;
	arg[1] = (unsigned long) br_name;

	if (ioctl(fd, SIOCGIFBR, arg) < 0) {
 		if (errno == EEXIST) {
			/* The bridge is already added. */
			close(fd);
			return 1;
		} else {
			wpa_printf(MSG_ERROR, "VLAN: %s: BRCTL_ADD_BRIDGE "
				   "failed for %s: %s",
				   __func__, br_name, strerror(errno));
			close(fd);
			return -1;
		}
	}

	/* Decrease forwarding delay to avoid EAPOL timeouts. */
	os_memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, br_name, IFNAMSIZ);
	arg[0] = BRCTL_SET_BRIDGE_FORWARD_DELAY;
	arg[1] = 1;
	arg[2] = 0;
	arg[3] = 0;
	ifr.ifr_data = (char *) &arg;
	if (ioctl(fd, SIOCDEVPRIVATE, &ifr) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: "
			   "BRCTL_SET_BRIDGE_FORWARD_DELAY (1 sec) failed for "
			   "%s: %s", __func__, br_name, strerror(errno));
		/* Continue anyway */
	}

	close(fd);
	return 0;
}


static int br_getnumports(const char *br_name)
{
	int fd;
	int i;
	int port_cnt = 0;
	unsigned long arg[4];
	int ifindices[MAX_BR_PORTS];
	struct ifreq ifr;

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: socket(AF_INET,SOCK_STREAM) "
			   "failed: %s", __func__, strerror(errno));
		return -1;
	}

	arg[0] = BRCTL_GET_PORT_LIST;
	arg[1] = (unsigned long) ifindices;
	arg[2] = MAX_BR_PORTS;
	arg[3] = 0;

	os_memset(ifindices, 0, sizeof(ifindices));
	os_strlcpy(ifr.ifr_name, br_name, sizeof(ifr.ifr_name));
	ifr.ifr_data = (__caddr_t) arg;

	if (ioctl(fd, SIOCDEVPRIVATE, &ifr) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: BRCTL_GET_PORT_LIST "
			   "failed for %s: %s",
			   __func__, br_name, strerror(errno));
		close(fd);
		return -1;
	}

	for (i = 1; i < MAX_BR_PORTS; i++) {
		if (ifindices[i] > 0) {
			port_cnt++;
		}
	}

	close(fd);
	return port_cnt;
}


static int vlan_rem(const char *if_name)
{
	int fd;
	struct vlan_ioctl_args if_request;

	wpa_printf(MSG_DEBUG, "VLAN: vlan_rem(%s)", if_name);
	if ((os_strlen(if_name) + 1) > sizeof(if_request.device1)) {
		wpa_printf(MSG_ERROR, "VLAN: Interface name too long: '%s'",
			   if_name);
		return -1;
	}

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: socket(AF_INET,SOCK_STREAM) "
			   "failed: %s", __func__, strerror(errno));
		return -1;
	}

	os_memset(&if_request, 0, sizeof(if_request));

	os_strlcpy(if_request.device1, if_name, sizeof(if_request.device1));
	if_request.cmd = DEL_VLAN_CMD;

	if (ioctl(fd, SIOCSIFVLAN, &if_request) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: DEL_VLAN_CMD failed for %s: "
			   "%s", __func__, if_name, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}


/*
	Add a vlan interface with VLAN ID 'vid' and tagged interface
	'if_name'.

	returns -1 on error
	returns 1 if the interface already exists
	returns 0 otherwise
*/
static int vlan_add(const char *if_name, int vid)
{
	int fd;
	struct vlan_ioctl_args if_request;

	wpa_printf(MSG_DEBUG, "VLAN: vlan_add(if_name=%s, vid=%d)",
		   if_name, vid);
	ifconfig_up(if_name);

	if ((os_strlen(if_name) + 1) > sizeof(if_request.device1)) {
		wpa_printf(MSG_ERROR, "VLAN: Interface name too long: '%s'",
			   if_name);
		return -1;
	}

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: socket(AF_INET,SOCK_STREAM) "
			   "failed: %s", __func__, strerror(errno));
		return -1;
	}

	os_memset(&if_request, 0, sizeof(if_request));

	/* Determine if a suitable vlan device already exists. */

	os_snprintf(if_request.device1, sizeof(if_request.device1), "vlan%d",
		    vid);

	if_request.cmd = _GET_VLAN_VID_CMD;

	if (ioctl(fd, SIOCSIFVLAN, &if_request) == 0) {

		if (if_request.u.VID == vid) {
			if_request.cmd = _GET_VLAN_REALDEV_NAME_CMD;

			if (ioctl(fd, SIOCSIFVLAN, &if_request) == 0 &&
			    os_strncmp(if_request.u.device2, if_name,
				       sizeof(if_request.u.device2)) == 0) {
				close(fd);
				wpa_printf(MSG_DEBUG, "VLAN: vlan_add: "
					   "if_name %s exists already",
					   if_request.device1);
				return 1;
			}
		}
	}

	/* A suitable vlan device does not already exist, add one. */

	os_memset(&if_request, 0, sizeof(if_request));
	os_strlcpy(if_request.device1, if_name, sizeof(if_request.device1));
	if_request.u.VID = vid;
	if_request.cmd = ADD_VLAN_CMD;

	if (ioctl(fd, SIOCSIFVLAN, &if_request) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: ADD_VLAN_CMD failed for %s: "
			   "%s",
			   __func__, if_request.device1, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}


static int vlan_set_name_type(unsigned int name_type)
{
	int fd;
	struct vlan_ioctl_args if_request;

	wpa_printf(MSG_DEBUG, "VLAN: vlan_set_name_type(name_type=%u)",
		   name_type);
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: socket(AF_INET,SOCK_STREAM) "
			   "failed: %s", __func__, strerror(errno));
		return -1;
	}

	os_memset(&if_request, 0, sizeof(if_request));

	if_request.u.name_type = name_type;
	if_request.cmd = SET_VLAN_NAME_TYPE_CMD;
	if (ioctl(fd, SIOCSIFVLAN, &if_request) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: SET_VLAN_NAME_TYPE_CMD "
			   "name_type=%u failed: %s",
			   __func__, name_type, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}


static void vlan_newlink(char *ifname, struct hostapd_data *hapd)
{
	char vlan_ifname[IFNAMSIZ];
	char br_name[IFNAMSIZ];
	struct hostapd_vlan *vlan = hapd->conf->vlan;
	char *tagged_interface = hapd->conf->ssid.vlan_tagged_interface;

	wpa_printf(MSG_DEBUG, "VLAN: vlan_newlink(%s)", ifname);

	while (vlan) {
		if (os_strcmp(ifname, vlan->ifname) == 0) {

			os_snprintf(br_name, sizeof(br_name), "brvlan%d",
				    vlan->vlan_id);

			if (!br_addbr(br_name))
				vlan->clean |= DVLAN_CLEAN_BR;

			ifconfig_up(br_name);

			if (tagged_interface) {

				if (!vlan_add(tagged_interface, vlan->vlan_id))
					vlan->clean |= DVLAN_CLEAN_VLAN;

				os_snprintf(vlan_ifname, sizeof(vlan_ifname),
					    "vlan%d", vlan->vlan_id);

				if (!br_addif(br_name, vlan_ifname))
					vlan->clean |= DVLAN_CLEAN_VLAN_PORT;

				ifconfig_up(vlan_ifname);
			}

			if (!br_addif(br_name, ifname))
				vlan->clean |= DVLAN_CLEAN_WLAN_PORT;

			ifconfig_up(ifname);

			break;
		}
		vlan = vlan->next;
	}
}


static void vlan_dellink(char *ifname, struct hostapd_data *hapd)
{
	char vlan_ifname[IFNAMSIZ];
	char br_name[IFNAMSIZ];
	struct hostapd_vlan *first, *prev, *vlan = hapd->conf->vlan;
	char *tagged_interface = hapd->conf->ssid.vlan_tagged_interface;

	wpa_printf(MSG_DEBUG, "VLAN: vlan_dellink(%s)", ifname);

	first = prev = vlan;

	while (vlan) {
		if (os_strcmp(ifname, vlan->ifname) == 0) {
			os_snprintf(br_name, sizeof(br_name), "brvlan%d",
				    vlan->vlan_id);

			if (vlan->clean & DVLAN_CLEAN_WLAN_PORT)
				br_delif(br_name, vlan->ifname);

			if (tagged_interface) {
				os_snprintf(vlan_ifname, sizeof(vlan_ifname),
					    "vlan%d", vlan->vlan_id);
				if (vlan->clean & DVLAN_CLEAN_VLAN_PORT)
					br_delif(br_name, vlan_ifname);
				ifconfig_down(vlan_ifname);

				if (vlan->clean & DVLAN_CLEAN_VLAN)
					vlan_rem(vlan_ifname);
			}

			if ((vlan->clean & DVLAN_CLEAN_BR) &&
			    br_getnumports(br_name) == 0) {
				ifconfig_down(br_name);
				br_delbr(br_name);
			}

			if (vlan == first) {
				hapd->conf->vlan = vlan->next;
			} else {
				prev->next = vlan->next;
			}
			os_free(vlan);

			break;
		}
		prev = vlan;
		vlan = vlan->next;
	}
}


static void
vlan_read_ifnames(struct nlmsghdr *h, size_t len, int del,
		  struct hostapd_data *hapd)
{
	struct ifinfomsg *ifi;
	int attrlen, nlmsg_len, rta_len;
	struct rtattr *attr;

	if (len < sizeof(*ifi))
		return;

	ifi = NLMSG_DATA(h);

	nlmsg_len = NLMSG_ALIGN(sizeof(struct ifinfomsg));

	attrlen = h->nlmsg_len - nlmsg_len;
	if (attrlen < 0)
		return;

	attr = (struct rtattr *) (((char *) ifi) + nlmsg_len);

	rta_len = RTA_ALIGN(sizeof(struct rtattr));
	while (RTA_OK(attr, attrlen)) {
		char ifname[IFNAMSIZ + 1];

		if (attr->rta_type == IFLA_IFNAME) {
			int n = attr->rta_len - rta_len;
			if (n < 0)
				break;

			os_memset(ifname, 0, sizeof(ifname));

			if ((size_t) n > sizeof(ifname))
				n = sizeof(ifname);
			os_memcpy(ifname, ((char *) attr) + rta_len, n);

			if (del)
				vlan_dellink(ifname, hapd);
			else
				vlan_newlink(ifname, hapd);
		}

		attr = RTA_NEXT(attr, attrlen);
	}
}


static void vlan_event_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	char buf[8192];
	int left;
	struct sockaddr_nl from;
	socklen_t fromlen;
	struct nlmsghdr *h;
	struct hostapd_data *hapd = eloop_ctx;

	fromlen = sizeof(from);
	left = recvfrom(sock, buf, sizeof(buf), MSG_DONTWAIT,
			(struct sockaddr *) &from, &fromlen);
	if (left < 0) {
		if (errno != EINTR && errno != EAGAIN)
			wpa_printf(MSG_ERROR, "VLAN: %s: recvfrom failed: %s",
				   __func__, strerror(errno));
		return;
	}

	h = (struct nlmsghdr *) buf;
	while (left >= (int) sizeof(*h)) {
		int len, plen;

		len = h->nlmsg_len;
		plen = len - sizeof(*h);
		if (len > left || plen < 0) {
			wpa_printf(MSG_DEBUG, "VLAN: Malformed netlink "
				   "message: len=%d left=%d plen=%d",
				   len, left, plen);
			break;
		}

		switch (h->nlmsg_type) {
		case RTM_NEWLINK:
			vlan_read_ifnames(h, plen, 0, hapd);
			break;
		case RTM_DELLINK:
			vlan_read_ifnames(h, plen, 1, hapd);
			break;
		}

		len = NLMSG_ALIGN(len);
		left -= len;
		h = (struct nlmsghdr *) ((char *) h + len);
	}

	if (left > 0) {
		wpa_printf(MSG_DEBUG, "VLAN: %s: %d extra bytes in the end of "
			   "netlink message", __func__, left);
	}
}


static struct full_dynamic_vlan *
full_dynamic_vlan_init(struct hostapd_data *hapd)
{
	struct sockaddr_nl local;
	struct full_dynamic_vlan *priv;

	priv = os_zalloc(sizeof(*priv));
	if (priv == NULL)
		return NULL;

	vlan_set_name_type(VLAN_NAME_TYPE_PLUS_VID_NO_PAD);

	priv->s = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (priv->s < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: socket(PF_NETLINK,SOCK_RAW,"
			   "NETLINK_ROUTE) failed: %s",
			   __func__, strerror(errno));
		os_free(priv);
		return NULL;
	}

	os_memset(&local, 0, sizeof(local));
	local.nl_family = AF_NETLINK;
	local.nl_groups = RTMGRP_LINK;
	if (bind(priv->s, (struct sockaddr *) &local, sizeof(local)) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: bind(netlink) failed: %s",
			   __func__, strerror(errno));
		close(priv->s);
		os_free(priv);
		return NULL;
	}

	if (eloop_register_read_sock(priv->s, vlan_event_receive, hapd, NULL))
	{
		close(priv->s);
		os_free(priv);
		return NULL;
	}

	return priv;
}


static void full_dynamic_vlan_deinit(struct full_dynamic_vlan *priv)
{
	if (priv == NULL)
		return;
	eloop_unregister_read_sock(priv->s);
	close(priv->s);
	os_free(priv);
}
#endif /* CONFIG_FULL_DYNAMIC_VLAN */


int vlan_setup_encryption_dyn(struct hostapd_data *hapd,
			      struct hostapd_ssid *mssid, const char *dyn_vlan)
{
        int i;

        if (dyn_vlan == NULL)
		return 0;

	/* Static WEP keys are set here; IEEE 802.1X and WPA uses their own
	 * functions for setting up dynamic broadcast keys. */
	for (i = 0; i < 4; i++) {
		if (mssid->wep.key[i] &&
		    hostapd_drv_set_key(dyn_vlan, hapd, WPA_ALG_WEP, NULL, i,
					i == mssid->wep.idx, NULL, 0,
					mssid->wep.key[i], mssid->wep.len[i]))
		{
			wpa_printf(MSG_ERROR, "VLAN: Could not set WEP "
				   "encryption for dynamic VLAN");
			return -1;
		}
	}

	return 0;
}


static int vlan_dynamic_add(struct hostapd_data *hapd,
			    struct hostapd_vlan *vlan)
{
	while (vlan) {
		if (vlan->vlan_id != VLAN_ID_WILDCARD) {
			if (hostapd_vlan_if_add(hapd, vlan->ifname)) {
				if (errno != EEXIST) {
					wpa_printf(MSG_ERROR, "VLAN: Could "
						   "not add VLAN %s: %s",
						   vlan->ifname,
						   strerror(errno));
					return -1;
				}
			}
#ifdef CONFIG_FULL_DYNAMIC_VLAN
			ifconfig_up(vlan->ifname);
#endif /* CONFIG_FULL_DYNAMIC_VLAN */
		}

		vlan = vlan->next;
	}

	return 0;
}


static void vlan_dynamic_remove(struct hostapd_data *hapd,
				struct hostapd_vlan *vlan)
{
	struct hostapd_vlan *next;

	while (vlan) {
		next = vlan->next;

		if (vlan->vlan_id != VLAN_ID_WILDCARD &&
		    hostapd_vlan_if_remove(hapd, vlan->ifname)) {
			wpa_printf(MSG_ERROR, "VLAN: Could not remove VLAN "
				   "iface: %s: %s",
				   vlan->ifname, strerror(errno));
		}
#ifdef CONFIG_FULL_DYNAMIC_VLAN
		if (vlan->clean)
			vlan_dellink(vlan->ifname, hapd);
#endif /* CONFIG_FULL_DYNAMIC_VLAN */

		vlan = next;
	}
}


int vlan_init(struct hostapd_data *hapd)
{
#ifdef CONFIG_FULL_DYNAMIC_VLAN
	hapd->full_dynamic_vlan = full_dynamic_vlan_init(hapd);
#endif /* CONFIG_FULL_DYNAMIC_VLAN */

	if (vlan_dynamic_add(hapd, hapd->conf->vlan))
		return -1;

        return 0;
}


void vlan_deinit(struct hostapd_data *hapd)
{
	vlan_dynamic_remove(hapd, hapd->conf->vlan);

#ifdef CONFIG_FULL_DYNAMIC_VLAN
	full_dynamic_vlan_deinit(hapd->full_dynamic_vlan);
#endif /* CONFIG_FULL_DYNAMIC_VLAN */
}


struct hostapd_vlan * vlan_add_dynamic(struct hostapd_data *hapd,
				       struct hostapd_vlan *vlan,
				       int vlan_id)
{
	struct hostapd_vlan *n;
	char *ifname, *pos;

	if (vlan == NULL || vlan_id <= 0 || vlan_id > MAX_VLAN_ID ||
	    vlan->vlan_id != VLAN_ID_WILDCARD)
		return NULL;

	wpa_printf(MSG_DEBUG, "VLAN: %s(vlan_id=%d ifname=%s)",
		   __func__, vlan_id, vlan->ifname);
	ifname = os_strdup(vlan->ifname);
	if (ifname == NULL)
		return NULL;
	pos = os_strchr(ifname, '#');
	if (pos == NULL) {
		os_free(ifname);
		return NULL;
	}
	*pos++ = '\0';

	n = os_zalloc(sizeof(*n));
	if (n == NULL) {
		os_free(ifname);
		return NULL;
	}

	n->vlan_id = vlan_id;
	n->dynamic_vlan = 1;

	os_snprintf(n->ifname, sizeof(n->ifname), "%s%d%s", ifname, vlan_id,
		    pos);
	os_free(ifname);

	if (hostapd_vlan_if_add(hapd, n->ifname)) {
		os_free(n);
		return NULL;
	}

	n->next = hapd->conf->vlan;
	hapd->conf->vlan = n;

#ifdef CONFIG_FULL_DYNAMIC_VLAN
	ifconfig_up(n->ifname);
#endif /* CONFIG_FULL_DYNAMIC_VLAN */

	return n;
}


int vlan_remove_dynamic(struct hostapd_data *hapd, int vlan_id)
{
	struct hostapd_vlan *vlan;

	if (vlan_id <= 0 || vlan_id > MAX_VLAN_ID)
		return 1;

	wpa_printf(MSG_DEBUG, "VLAN: %s(vlan_id=%d)", __func__, vlan_id);

	vlan = hapd->conf->vlan;
	while (vlan) {
		if (vlan->vlan_id == vlan_id && vlan->dynamic_vlan > 0) {
			vlan->dynamic_vlan--;
			break;
		}
		vlan = vlan->next;
	}

	if (vlan == NULL)
		return 1;

	if (vlan->dynamic_vlan == 0)
		hostapd_vlan_if_remove(hapd, vlan->ifname);

	return 0;
}
