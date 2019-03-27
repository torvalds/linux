/*
 * hostapd / VLAN initialization - full dynamic VLAN
 * Copyright 2003, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#include <net/if.h>
/* Avoid conflicts due to NetBSD net/if.h if_type define with driver.h */
#undef if_type
#include <sys/ioctl.h>

#include "utils/common.h"
#include "drivers/priv_netlink.h"
#include "common/linux_bridge.h"
#include "common/linux_vlan.h"
#include "utils/eloop.h"
#include "hostapd.h"
#include "ap_config.h"
#include "ap_drv_ops.h"
#include "wpa_auth.h"
#include "vlan_init.h"
#include "vlan_util.h"


struct full_dynamic_vlan {
	int s; /* socket on which to listen for new/removed interfaces. */
};

#define DVLAN_CLEAN_BR         0x1
#define DVLAN_CLEAN_VLAN       0x2
#define DVLAN_CLEAN_VLAN_PORT  0x4

struct dynamic_iface {
	char ifname[IFNAMSIZ + 1];
	int usage;
	int clean;
	struct dynamic_iface *next;
};


/* Increment ref counter for ifname and add clean flag.
 * If not in list, add it only if some flags are given.
 */
static void dyn_iface_get(struct hostapd_data *hapd, const char *ifname,
			  int clean)
{
	struct dynamic_iface *next, **dynamic_ifaces;
	struct hapd_interfaces *interfaces;

	interfaces = hapd->iface->interfaces;
	dynamic_ifaces = &interfaces->vlan_priv;

	for (next = *dynamic_ifaces; next; next = next->next) {
		if (os_strcmp(ifname, next->ifname) == 0)
			break;
	}

	if (next) {
		next->usage++;
		next->clean |= clean;
		return;
	}

	if (!clean)
		return;

	next = os_zalloc(sizeof(*next));
	if (!next)
		return;
	os_strlcpy(next->ifname, ifname, sizeof(next->ifname));
	next->usage = 1;
	next->clean = clean;
	next->next = *dynamic_ifaces;
	*dynamic_ifaces = next;
}


/* Decrement reference counter for given ifname.
 * Return clean flag iff reference counter was decreased to zero, else zero
 */
static int dyn_iface_put(struct hostapd_data *hapd, const char *ifname)
{
	struct dynamic_iface *next, *prev = NULL, **dynamic_ifaces;
	struct hapd_interfaces *interfaces;
	int clean;

	interfaces = hapd->iface->interfaces;
	dynamic_ifaces = &interfaces->vlan_priv;

	for (next = *dynamic_ifaces; next; next = next->next) {
		if (os_strcmp(ifname, next->ifname) == 0)
			break;
		prev = next;
	}

	if (!next)
		return 0;

	next->usage--;
	if (next->usage)
		return 0;

	if (prev)
		prev->next = next->next;
	else
		*dynamic_ifaces = next->next;
	clean = next->clean;
	os_free(next);

	return clean;
}


static int ifconfig_down(const char *if_name)
{
	wpa_printf(MSG_DEBUG, "VLAN: Set interface %s down", if_name);
	return ifconfig_helper(if_name, 0);
}


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
	ifr.ifr_data = (void *) args;

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
	ifr.ifr_data = (void *) args;

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
	ifr.ifr_data = (void *) arg;

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


static void vlan_newlink_tagged(int vlan_naming, const char *tagged_interface,
				const char *br_name, int vid,
				struct hostapd_data *hapd)
{
	char vlan_ifname[IFNAMSIZ];
	int clean;

	if (vlan_naming == DYNAMIC_VLAN_NAMING_WITH_DEVICE)
		os_snprintf(vlan_ifname, sizeof(vlan_ifname), "%s.%d",
			    tagged_interface, vid);
	else
		os_snprintf(vlan_ifname, sizeof(vlan_ifname), "vlan%d", vid);

	clean = 0;
	ifconfig_up(tagged_interface);
	if (!vlan_add(tagged_interface, vid, vlan_ifname))
		clean |= DVLAN_CLEAN_VLAN;

	if (!br_addif(br_name, vlan_ifname))
		clean |= DVLAN_CLEAN_VLAN_PORT;

	dyn_iface_get(hapd, vlan_ifname, clean);

	ifconfig_up(vlan_ifname);
}


static void vlan_bridge_name(char *br_name, struct hostapd_data *hapd, int vid)
{
	char *tagged_interface = hapd->conf->ssid.vlan_tagged_interface;

	if (hapd->conf->vlan_bridge[0]) {
		os_snprintf(br_name, IFNAMSIZ, "%s%d",
			    hapd->conf->vlan_bridge, vid);
	} else if (tagged_interface) {
		os_snprintf(br_name, IFNAMSIZ, "br%s.%d",
			    tagged_interface, vid);
	} else {
		os_snprintf(br_name, IFNAMSIZ, "brvlan%d", vid);
	}
}


static void vlan_get_bridge(const char *br_name, struct hostapd_data *hapd,
			    int vid)
{
	char *tagged_interface = hapd->conf->ssid.vlan_tagged_interface;
	int vlan_naming = hapd->conf->ssid.vlan_naming;

	dyn_iface_get(hapd, br_name, br_addbr(br_name) ? 0 : DVLAN_CLEAN_BR);

	ifconfig_up(br_name);

	if (tagged_interface)
		vlan_newlink_tagged(vlan_naming, tagged_interface, br_name,
				    vid, hapd);
}


void vlan_newlink(const char *ifname, struct hostapd_data *hapd)
{
	char br_name[IFNAMSIZ];
	struct hostapd_vlan *vlan;
	int untagged, *tagged, i, notempty;

	wpa_printf(MSG_DEBUG, "VLAN: vlan_newlink(%s)", ifname);

	for (vlan = hapd->conf->vlan; vlan; vlan = vlan->next) {
		if (vlan->configured ||
		    os_strcmp(ifname, vlan->ifname) != 0)
			continue;
		break;
	}
	if (!vlan)
		return;

	vlan->configured = 1;

	notempty = vlan->vlan_desc.notempty;
	untagged = vlan->vlan_desc.untagged;
	tagged = vlan->vlan_desc.tagged;

	if (!notempty) {
		/* Non-VLAN STA */
		if (hapd->conf->bridge[0] &&
		    !br_addif(hapd->conf->bridge, ifname))
			vlan->clean |= DVLAN_CLEAN_WLAN_PORT;
	} else if (untagged > 0 && untagged <= MAX_VLAN_ID) {
		vlan_bridge_name(br_name, hapd, untagged);

		vlan_get_bridge(br_name, hapd, untagged);

		if (!br_addif(br_name, ifname))
			vlan->clean |= DVLAN_CLEAN_WLAN_PORT;
	}

	for (i = 0; i < MAX_NUM_TAGGED_VLAN && tagged[i]; i++) {
		if (tagged[i] == untagged ||
		    tagged[i] <= 0 || tagged[i] > MAX_VLAN_ID ||
		    (i > 0 && tagged[i] == tagged[i - 1]))
			continue;
		vlan_bridge_name(br_name, hapd, tagged[i]);
		vlan_get_bridge(br_name, hapd, tagged[i]);
		vlan_newlink_tagged(DYNAMIC_VLAN_NAMING_WITH_DEVICE,
				    ifname, br_name, tagged[i], hapd);
	}

	ifconfig_up(ifname);
}


static void vlan_dellink_tagged(int vlan_naming, const char *tagged_interface,
				const char *br_name, int vid,
				struct hostapd_data *hapd)
{
	char vlan_ifname[IFNAMSIZ];
	int clean;

	if (vlan_naming == DYNAMIC_VLAN_NAMING_WITH_DEVICE)
		os_snprintf(vlan_ifname, sizeof(vlan_ifname), "%s.%d",
			    tagged_interface, vid);
	else
		os_snprintf(vlan_ifname, sizeof(vlan_ifname), "vlan%d", vid);

	clean = dyn_iface_put(hapd, vlan_ifname);

	if (clean & DVLAN_CLEAN_VLAN_PORT)
		br_delif(br_name, vlan_ifname);

	if (clean & DVLAN_CLEAN_VLAN) {
		ifconfig_down(vlan_ifname);
		vlan_rem(vlan_ifname);
	}
}


static void vlan_put_bridge(const char *br_name, struct hostapd_data *hapd,
			    int vid)
{
	int clean;
	char *tagged_interface = hapd->conf->ssid.vlan_tagged_interface;
	int vlan_naming = hapd->conf->ssid.vlan_naming;

	if (tagged_interface)
		vlan_dellink_tagged(vlan_naming, tagged_interface, br_name,
				    vid, hapd);

	clean = dyn_iface_put(hapd, br_name);
	if ((clean & DVLAN_CLEAN_BR) && br_getnumports(br_name) == 0) {
		ifconfig_down(br_name);
		br_delbr(br_name);
	}
}


void vlan_dellink(const char *ifname, struct hostapd_data *hapd)
{
	struct hostapd_vlan *first, *prev, *vlan = hapd->conf->vlan;

	wpa_printf(MSG_DEBUG, "VLAN: vlan_dellink(%s)", ifname);

	first = prev = vlan;

	while (vlan) {
		if (os_strcmp(ifname, vlan->ifname) != 0) {
			prev = vlan;
			vlan = vlan->next;
			continue;
		}
		break;
	}
	if (!vlan)
		return;

	if (vlan->configured) {
		int notempty = vlan->vlan_desc.notempty;
		int untagged = vlan->vlan_desc.untagged;
		int *tagged = vlan->vlan_desc.tagged;
		char br_name[IFNAMSIZ];
		int i;

		for (i = 0; i < MAX_NUM_TAGGED_VLAN && tagged[i]; i++) {
			if (tagged[i] == untagged ||
			    tagged[i] <= 0 || tagged[i] > MAX_VLAN_ID ||
			    (i > 0 && tagged[i] == tagged[i - 1]))
				continue;
			vlan_bridge_name(br_name, hapd, tagged[i]);
			vlan_dellink_tagged(DYNAMIC_VLAN_NAMING_WITH_DEVICE,
					    ifname, br_name, tagged[i], hapd);
			vlan_put_bridge(br_name, hapd, tagged[i]);
		}

		if (!notempty) {
			/* Non-VLAN STA */
			if (hapd->conf->bridge[0] &&
			    (vlan->clean & DVLAN_CLEAN_WLAN_PORT))
				br_delif(hapd->conf->bridge, ifname);
		} else if (untagged > 0 && untagged <= MAX_VLAN_ID) {
			vlan_bridge_name(br_name, hapd, untagged);

			if (vlan->clean & DVLAN_CLEAN_WLAN_PORT)
				br_delif(br_name, vlan->ifname);

			vlan_put_bridge(br_name, hapd, untagged);
		}
	}

	/*
	 * Ensure this VLAN interface is actually removed even if
	 * NEWLINK message is only received later.
	 */
	if (if_nametoindex(vlan->ifname) && vlan_if_remove(hapd, vlan))
		wpa_printf(MSG_ERROR,
			   "VLAN: Could not remove VLAN iface: %s: %s",
			   vlan->ifname, strerror(errno));

	if (vlan == first)
		hapd->conf->vlan = vlan->next;
	else
		prev->next = vlan->next;

	os_free(vlan);
}


static void
vlan_read_ifnames(struct nlmsghdr *h, size_t len, int del,
		  struct hostapd_data *hapd)
{
	struct ifinfomsg *ifi;
	int attrlen, nlmsg_len, rta_len;
	struct rtattr *attr;
	char ifname[IFNAMSIZ + 1];

	if (len < sizeof(*ifi))
		return;

	ifi = NLMSG_DATA(h);

	nlmsg_len = NLMSG_ALIGN(sizeof(struct ifinfomsg));

	attrlen = h->nlmsg_len - nlmsg_len;
	if (attrlen < 0)
		return;

	attr = (struct rtattr *) (((char *) ifi) + nlmsg_len);

	os_memset(ifname, 0, sizeof(ifname));
	rta_len = RTA_ALIGN(sizeof(struct rtattr));
	while (RTA_OK(attr, attrlen)) {
		if (attr->rta_type == IFLA_IFNAME) {
			int n = attr->rta_len - rta_len;
			if (n < 0)
				break;

			if ((size_t) n >= sizeof(ifname))
				n = sizeof(ifname) - 1;
			os_memcpy(ifname, ((char *) attr) + rta_len, n);

		}

		attr = RTA_NEXT(attr, attrlen);
	}

	if (!ifname[0])
		return;
	if (del && if_nametoindex(ifname)) {
		 /* interface still exists, race condition ->
		  * iface has just been recreated */
		return;
	}

	wpa_printf(MSG_DEBUG,
		   "VLAN: RTM_%sLINK: ifi_index=%d ifname=%s ifi_family=%d ifi_flags=0x%x (%s%s%s%s)",
		   del ? "DEL" : "NEW",
		   ifi->ifi_index, ifname, ifi->ifi_family, ifi->ifi_flags,
		   (ifi->ifi_flags & IFF_UP) ? "[UP]" : "",
		   (ifi->ifi_flags & IFF_RUNNING) ? "[RUNNING]" : "",
		   (ifi->ifi_flags & IFF_LOWER_UP) ? "[LOWER_UP]" : "",
		   (ifi->ifi_flags & IFF_DORMANT) ? "[DORMANT]" : "");

	if (del)
		vlan_dellink(ifname, hapd);
	else
		vlan_newlink(ifname, hapd);
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
	while (NLMSG_OK(h, left)) {
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

		h = NLMSG_NEXT(h, left);
	}

	if (left > 0) {
		wpa_printf(MSG_DEBUG, "VLAN: %s: %d extra bytes in the end of "
			   "netlink message", __func__, left);
	}
}


struct full_dynamic_vlan *
full_dynamic_vlan_init(struct hostapd_data *hapd)
{
	struct sockaddr_nl local;
	struct full_dynamic_vlan *priv;

	priv = os_zalloc(sizeof(*priv));
	if (priv == NULL)
		return NULL;

	vlan_set_name_type(hapd->conf->ssid.vlan_naming ==
			   DYNAMIC_VLAN_NAMING_WITH_DEVICE ?
			   VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD :
			   VLAN_NAME_TYPE_PLUS_VID_NO_PAD);

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


void full_dynamic_vlan_deinit(struct full_dynamic_vlan *priv)
{
	if (priv == NULL)
		return;
	eloop_unregister_read_sock(priv->s);
	close(priv->s);
	os_free(priv);
}
