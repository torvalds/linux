/*
 * Neighbor Discovery snooping for Proxy ARP
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#include <netinet/ip6.h>
#include <netinet/icmp6.h>

#include "utils/common.h"
#include "l2_packet/l2_packet.h"
#include "hostapd.h"
#include "sta_info.h"
#include "ap_drv_ops.h"
#include "list.h"
#include "x_snoop.h"
#include "ndisc_snoop.h"

struct ip6addr {
	struct in6_addr addr;
	struct dl_list list;
};

struct icmpv6_ndmsg {
	struct ip6_hdr ipv6h;
	struct icmp6_hdr icmp6h;
	struct in6_addr target_addr;
	u8 opt_type;
	u8 len;
	u8 opt_lladdr[0];
} STRUCT_PACKED;

#define ROUTER_ADVERTISEMENT	134
#define NEIGHBOR_SOLICITATION	135
#define NEIGHBOR_ADVERTISEMENT	136
#define SOURCE_LL_ADDR		1

static int sta_ip6addr_add(struct sta_info *sta, struct in6_addr *addr)
{
	struct ip6addr *ip6addr;

	ip6addr = os_zalloc(sizeof(*ip6addr));
	if (!ip6addr)
		return -1;

	os_memcpy(&ip6addr->addr, addr, sizeof(*addr));

	dl_list_add_tail(&sta->ip6addr, &ip6addr->list);

	return 0;
}


void sta_ip6addr_del(struct hostapd_data *hapd, struct sta_info *sta)
{
	struct ip6addr *ip6addr, *prev;

	dl_list_for_each_safe(ip6addr, prev, &sta->ip6addr, struct ip6addr,
			      list) {
		hostapd_drv_br_delete_ip_neigh(hapd, 6, (u8 *) &ip6addr->addr);
		os_free(ip6addr);
	}
}


static int sta_has_ip6addr(struct sta_info *sta, struct in6_addr *addr)
{
	struct ip6addr *ip6addr;

	dl_list_for_each(ip6addr, &sta->ip6addr, struct ip6addr, list) {
		if (ip6addr->addr.s6_addr32[0] == addr->s6_addr32[0] &&
		    ip6addr->addr.s6_addr32[1] == addr->s6_addr32[1] &&
		    ip6addr->addr.s6_addr32[2] == addr->s6_addr32[2] &&
		    ip6addr->addr.s6_addr32[3] == addr->s6_addr32[3])
			return 1;
	}

	return 0;
}


static void ucast_to_stas(struct hostapd_data *hapd, const u8 *buf, size_t len)
{
	struct sta_info *sta;

	for (sta = hapd->sta_list; sta; sta = sta->next) {
		if (!(sta->flags & WLAN_STA_AUTHORIZED))
			continue;
		x_snoop_mcast_to_ucast_convert_send(hapd, sta, (u8 *) buf, len);
	}
}


static void handle_ndisc(void *ctx, const u8 *src_addr, const u8 *buf,
			 size_t len)
{
	struct hostapd_data *hapd = ctx;
	struct icmpv6_ndmsg *msg;
	struct in6_addr saddr;
	struct sta_info *sta;
	int res;
	char addrtxt[INET6_ADDRSTRLEN + 1];

	if (len < ETH_HLEN + sizeof(struct ip6_hdr) + sizeof(struct icmp6_hdr))
		return;
	msg = (struct icmpv6_ndmsg *) &buf[ETH_HLEN];
	switch (msg->icmp6h.icmp6_type) {
	case NEIGHBOR_SOLICITATION:
		if (len < ETH_HLEN + sizeof(*msg))
			return;
		if (msg->opt_type != SOURCE_LL_ADDR)
			return;

		/*
		 * IPv6 header may not be 32-bit aligned in the buffer, so use
		 * a local copy to avoid unaligned reads.
		 */
		os_memcpy(&saddr, &msg->ipv6h.ip6_src, sizeof(saddr));
		if (!(saddr.s6_addr32[0] == 0 && saddr.s6_addr32[1] == 0 &&
		      saddr.s6_addr32[2] == 0 && saddr.s6_addr32[3] == 0)) {
			if (len < ETH_HLEN + sizeof(*msg) + ETH_ALEN)
				return;
			sta = ap_get_sta(hapd, msg->opt_lladdr);
			if (!sta)
				return;

			if (sta_has_ip6addr(sta, &saddr))
				return;

			if (inet_ntop(AF_INET6, &saddr, addrtxt,
				      sizeof(addrtxt)) == NULL)
				addrtxt[0] = '\0';
			wpa_printf(MSG_DEBUG, "ndisc_snoop: Learned new IPv6 address %s for "
				   MACSTR, addrtxt, MAC2STR(sta->addr));
			hostapd_drv_br_delete_ip_neigh(hapd, 6, (u8 *) &saddr);
			res = hostapd_drv_br_add_ip_neigh(hapd, 6,
							  (u8 *) &saddr,
							  128, sta->addr);
			if (res) {
				wpa_printf(MSG_ERROR,
					   "ndisc_snoop: Adding ip neigh failed: %d",
					   res);
				return;
			}

			if (sta_ip6addr_add(sta, &saddr))
				return;
		}
		break;
	case ROUTER_ADVERTISEMENT:
		if (hapd->conf->disable_dgaf)
			ucast_to_stas(hapd, buf, len);
		break;
	case NEIGHBOR_ADVERTISEMENT:
		if (hapd->conf->na_mcast_to_ucast)
			ucast_to_stas(hapd, buf, len);
		break;
	default:
		break;
	}
}


int ndisc_snoop_init(struct hostapd_data *hapd)
{
	hapd->sock_ndisc = x_snoop_get_l2_packet(hapd, handle_ndisc,
						 L2_PACKET_FILTER_NDISC);
	if (hapd->sock_ndisc == NULL) {
		wpa_printf(MSG_DEBUG,
			   "ndisc_snoop: Failed to initialize L2 packet processing for NDISC packets: %s",
			   strerror(errno));
		return -1;
	}

	return 0;
}


void ndisc_snoop_deinit(struct hostapd_data *hapd)
{
	l2_packet_deinit(hapd->sock_ndisc);
	hapd->sock_ndisc = NULL;
}
