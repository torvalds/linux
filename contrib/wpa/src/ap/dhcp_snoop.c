/*
 * DHCP snooping for Proxy ARP
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "common/dhcp.h"
#include "l2_packet/l2_packet.h"
#include "hostapd.h"
#include "sta_info.h"
#include "ap_drv_ops.h"
#include "x_snoop.h"
#include "dhcp_snoop.h"


static const char * ipaddr_str(u32 addr)
{
	static char buf[17];

	os_snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
		    (addr >> 24) & 0xff, (addr >> 16) & 0xff,
		    (addr >> 8) & 0xff, addr & 0xff);
	return buf;
}


static void handle_dhcp(void *ctx, const u8 *src_addr, const u8 *buf,
			size_t len)
{
	struct hostapd_data *hapd = ctx;
	const struct bootp_pkt *b;
	struct sta_info *sta;
	int exten_len;
	const u8 *end, *pos;
	int res, msgtype = 0, prefixlen = 32;
	u32 subnet_mask = 0;
	u16 tot_len;

	exten_len = len - ETH_HLEN - (sizeof(*b) - sizeof(b->exten));
	if (exten_len < 4)
		return;

	b = (const struct bootp_pkt *) &buf[ETH_HLEN];
	tot_len = ntohs(b->iph.tot_len);
	if (tot_len > (unsigned int) (len - ETH_HLEN))
		return;

	if (WPA_GET_BE32(b->exten) != DHCP_MAGIC)
		return;

	/* Parse DHCP options */
	end = (const u8 *) b + tot_len;
	pos = &b->exten[4];
	while (pos < end && *pos != DHCP_OPT_END) {
		const u8 *opt = pos++;

		if (*opt == DHCP_OPT_PAD)
			continue;

		if (pos >= end || 1 + *pos > end - pos)
			break;
		pos += *pos + 1;
		if (pos >= end)
			break;

		switch (*opt) {
		case DHCP_OPT_SUBNET_MASK:
			if (opt[1] == 4)
				subnet_mask = WPA_GET_BE32(&opt[2]);
			if (subnet_mask == 0)
				return;
			while (!(subnet_mask & 0x1)) {
				subnet_mask >>= 1;
				prefixlen--;
			}
			break;
		case DHCP_OPT_MSG_TYPE:
			if (opt[1])
				msgtype = opt[2];
			break;
		default:
			break;
		}
	}

	if (msgtype == DHCPACK) {
		if (b->your_ip == 0)
			return;

		/* DHCPACK for DHCPREQUEST */
		sta = ap_get_sta(hapd, b->hw_addr);
		if (!sta)
			return;

		wpa_printf(MSG_DEBUG, "dhcp_snoop: Found DHCPACK for " MACSTR
			   " @ IPv4 address %s/%d",
			   MAC2STR(sta->addr),
			   ipaddr_str(be_to_host32(b->your_ip)),
			   prefixlen);

		if (sta->ipaddr == b->your_ip)
			return;

		if (sta->ipaddr != 0) {
			wpa_printf(MSG_DEBUG,
				   "dhcp_snoop: Removing IPv4 address %s from the ip neigh table",
				   ipaddr_str(be_to_host32(sta->ipaddr)));
			hostapd_drv_br_delete_ip_neigh(hapd, 4,
						       (u8 *) &sta->ipaddr);
		}

		res = hostapd_drv_br_add_ip_neigh(hapd, 4, (u8 *) &b->your_ip,
						  prefixlen, sta->addr);
		if (res) {
			wpa_printf(MSG_DEBUG,
				   "dhcp_snoop: Adding ip neigh table failed: %d",
				   res);
			return;
		}
		sta->ipaddr = b->your_ip;
	}

	if (hapd->conf->disable_dgaf && is_broadcast_ether_addr(buf)) {
		for (sta = hapd->sta_list; sta; sta = sta->next) {
			if (!(sta->flags & WLAN_STA_AUTHORIZED))
				continue;
			x_snoop_mcast_to_ucast_convert_send(hapd, sta,
							    (u8 *) buf, len);
		}
	}
}


int dhcp_snoop_init(struct hostapd_data *hapd)
{
	hapd->sock_dhcp = x_snoop_get_l2_packet(hapd, handle_dhcp,
						L2_PACKET_FILTER_DHCP);
	if (hapd->sock_dhcp == NULL) {
		wpa_printf(MSG_DEBUG,
			   "dhcp_snoop: Failed to initialize L2 packet processing for DHCP packet: %s",
			   strerror(errno));
		return -1;
	}

	return 0;
}


void dhcp_snoop_deinit(struct hostapd_data *hapd)
{
	l2_packet_deinit(hapd->sock_dhcp);
	hapd->sock_dhcp = NULL;
}
