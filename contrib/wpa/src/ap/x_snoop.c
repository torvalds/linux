/*
 * Generic Snooping for Proxy ARP
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "hostapd.h"
#include "sta_info.h"
#include "ap_drv_ops.h"
#include "x_snoop.h"


int x_snoop_init(struct hostapd_data *hapd)
{
	struct hostapd_bss_config *conf = hapd->conf;

	if (!conf->isolate) {
		wpa_printf(MSG_DEBUG,
			   "x_snoop: ap_isolate must be enabled for x_snoop");
		return -1;
	}

	if (conf->bridge[0] == '\0') {
		wpa_printf(MSG_DEBUG,
			   "x_snoop: Bridge must be configured for x_snoop");
		return -1;
	}

	if (hostapd_drv_br_port_set_attr(hapd, DRV_BR_PORT_ATTR_HAIRPIN_MODE,
					 1)) {
		wpa_printf(MSG_DEBUG,
			   "x_snoop: Failed to enable hairpin_mode on the bridge port");
		return -1;
	}

	if (hostapd_drv_br_port_set_attr(hapd, DRV_BR_PORT_ATTR_PROXYARP, 1)) {
		wpa_printf(MSG_DEBUG,
			   "x_snoop: Failed to enable proxyarp on the bridge port");
		return -1;
	}

	if (hostapd_drv_br_set_net_param(hapd, DRV_BR_NET_PARAM_GARP_ACCEPT,
					 1)) {
		wpa_printf(MSG_DEBUG,
			   "x_snoop: Failed to enable accepting gratuitous ARP on the bridge");
		return -1;
	}

#ifdef CONFIG_IPV6
	if (hostapd_drv_br_set_net_param(hapd, DRV_BR_MULTICAST_SNOOPING, 1)) {
		wpa_printf(MSG_DEBUG,
			   "x_snoop: Failed to enable multicast snooping on the bridge");
		return -1;
	}
#endif /* CONFIG_IPV6 */

	return 0;
}


struct l2_packet_data *
x_snoop_get_l2_packet(struct hostapd_data *hapd,
		      void (*handler)(void *ctx, const u8 *src_addr,
				      const u8 *buf, size_t len),
		      enum l2_packet_filter_type type)
{
	struct hostapd_bss_config *conf = hapd->conf;
	struct l2_packet_data *l2;

	l2 = l2_packet_init(conf->bridge, NULL, ETH_P_ALL, handler, hapd, 1);
	if (l2 == NULL) {
		wpa_printf(MSG_DEBUG,
			   "x_snoop: Failed to initialize L2 packet processing %s",
			   strerror(errno));
		return NULL;
	}

	if (l2_packet_set_packet_filter(l2, type)) {
		wpa_printf(MSG_DEBUG,
			   "x_snoop: Failed to set L2 packet filter for type: %d",
			   type);
		l2_packet_deinit(l2);
		return NULL;
	}

	return l2;
}


void x_snoop_mcast_to_ucast_convert_send(struct hostapd_data *hapd,
					 struct sta_info *sta, u8 *buf,
					 size_t len)
{
	int res;
	u8 addr[ETH_ALEN];
	u8 *dst_addr = buf;

	if (!(dst_addr[0] & 0x01))
		return;

	wpa_printf(MSG_EXCESSIVE, "x_snoop: Multicast-to-unicast conversion "
		   MACSTR " -> " MACSTR " (len %u)",
		   MAC2STR(dst_addr), MAC2STR(sta->addr), (unsigned int) len);

	/* save the multicast destination address for restoring it later */
	os_memcpy(addr, buf, ETH_ALEN);

	os_memcpy(buf, sta->addr, ETH_ALEN);
	res = l2_packet_send(hapd->sock_dhcp, NULL, 0, buf, len);
	if (res < 0) {
		wpa_printf(MSG_DEBUG,
			   "x_snoop: Failed to send mcast to ucast converted packet to "
			   MACSTR, MAC2STR(sta->addr));
	}

	/* restore the multicast destination address */
	os_memcpy(buf, addr, ETH_ALEN);
}


void x_snoop_deinit(struct hostapd_data *hapd)
{
	hostapd_drv_br_set_net_param(hapd, DRV_BR_NET_PARAM_GARP_ACCEPT, 0);
	hostapd_drv_br_port_set_attr(hapd, DRV_BR_PORT_ATTR_PROXYARP, 0);
	hostapd_drv_br_port_set_attr(hapd, DRV_BR_PORT_ATTR_HAIRPIN_MODE, 0);
}
