/*
 * hostapd / IEEE 802 OUI Extended EtherType 88-B7
 * Copyright (c) 2016, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "l2_packet/l2_packet.h"
#include "hostapd.h"
#include "eth_p_oui.h"

/*
 * See IEEE Std 802-2014, Clause 9.2.4 for the definition of the OUI Extended
 * EtherType 88-B7. This file implements this with OUI 00:13:74 and
 * vendor-specific subtype 0x0001.
 */
static const u8 global_oui[] = { 0x00, 0x13, 0x74, 0x00, 0x01 };

struct eth_p_oui_iface {
	struct dl_list list;
	char ifname[IFNAMSIZ + 1];
	struct l2_packet_data *l2;
	struct dl_list receiver;
};

struct eth_p_oui_ctx {
	struct dl_list list;
	struct eth_p_oui_iface *iface;
	/* all data needed to deliver and unregister */
	u8 oui_suffix; /* last byte of OUI */
	void (*rx_callback)(void *ctx, const u8 *src_addr,
			    const u8 *dst_addr, u8 oui_suffix,
			    const u8 *buf, size_t len);
	void *rx_callback_ctx;
};


void eth_p_oui_deliver(struct eth_p_oui_ctx *ctx, const u8 *src_addr,
		       const u8 *dst_addr, const u8 *buf, size_t len)
{
	ctx->rx_callback(ctx->rx_callback_ctx, src_addr, dst_addr,
			 ctx->oui_suffix, buf, len);
}


static void eth_p_rx(void *ctx, const u8 *src_addr, const u8 *buf, size_t len)
{
	struct eth_p_oui_iface *iface = ctx;
	struct eth_p_oui_ctx *receiver;
	const struct l2_ethhdr *ethhdr;

	if (len < sizeof(*ethhdr) + sizeof(global_oui) + 1) {
		/* too short packet */
		return;
	}

	ethhdr = (struct l2_ethhdr *) buf;
	/* trim eth_hdr from buf and len */
	buf += sizeof(*ethhdr);
	len -= sizeof(*ethhdr);

	/* verify OUI and vendor-specific subtype match */
	if (os_memcmp(buf, global_oui, sizeof(global_oui)) != 0)
		return;
	buf += sizeof(global_oui);
	len -= sizeof(global_oui);

	dl_list_for_each(receiver, &iface->receiver,
			 struct eth_p_oui_ctx, list) {
		if (buf[0] != receiver->oui_suffix)
			continue;

		eth_p_oui_deliver(receiver, ethhdr->h_source, ethhdr->h_dest,
				  buf + 1, len - 1);
	}
}


struct eth_p_oui_ctx *
eth_p_oui_register(struct hostapd_data *hapd, const char *ifname, u8 oui_suffix,
		   void (*rx_callback)(void *ctx, const u8 *src_addr,
				       const u8 *dst_addr, u8 oui_suffix,
				       const u8 *buf, size_t len),
		   void *rx_callback_ctx)
{
	struct eth_p_oui_iface *iface;
	struct eth_p_oui_ctx *receiver;
	int found = 0;
	struct hapd_interfaces *interfaces;

	receiver = os_zalloc(sizeof(*receiver));
	if (!receiver)
		goto err;

	receiver->oui_suffix = oui_suffix;
	receiver->rx_callback = rx_callback;
	receiver->rx_callback_ctx = rx_callback_ctx;

	interfaces = hapd->iface->interfaces;

	dl_list_for_each(iface, &interfaces->eth_p_oui, struct eth_p_oui_iface,
			 list) {
		if (os_strcmp(iface->ifname, ifname) != 0)
			continue;
		found = 1;
		break;
	}

	if (!found) {
		iface = os_zalloc(sizeof(*iface));
		if (!iface)
			goto err;

		os_strlcpy(iface->ifname, ifname, sizeof(iface->ifname));
		iface->l2 = l2_packet_init(ifname, NULL, ETH_P_OUI, eth_p_rx,
					   iface, 1);
		if (!iface->l2) {
			os_free(iface);
			goto err;
		}
		dl_list_init(&iface->receiver);

		dl_list_add_tail(&interfaces->eth_p_oui, &iface->list);
	}

	dl_list_add_tail(&iface->receiver, &receiver->list);
	receiver->iface = iface;

	return receiver;
err:
	os_free(receiver);
	return NULL;
}


void eth_p_oui_unregister(struct eth_p_oui_ctx *ctx)
{
	struct eth_p_oui_iface *iface;

	if (!ctx)
		return;

	iface = ctx->iface;

	dl_list_del(&ctx->list);
	os_free(ctx);

	if (dl_list_empty(&iface->receiver)) {
		dl_list_del(&iface->list);
		l2_packet_deinit(iface->l2);
		os_free(iface);
	}
}


int eth_p_oui_send(struct eth_p_oui_ctx *ctx, const u8 *src_addr,
		   const u8 *dst_addr, const u8 *buf, size_t len)
{
	struct eth_p_oui_iface *iface = ctx->iface;
	u8 *packet, *p;
	size_t packet_len;
	int ret;
	struct l2_ethhdr *ethhdr;

	packet_len = sizeof(*ethhdr) + sizeof(global_oui) + 1 + len;
	packet = os_zalloc(packet_len);
	if (!packet)
		return -1;
	p = packet;

	ethhdr = (struct l2_ethhdr *) packet;
	os_memcpy(ethhdr->h_source, src_addr, ETH_ALEN);
	os_memcpy(ethhdr->h_dest, dst_addr, ETH_ALEN);
	ethhdr->h_proto = host_to_be16(ETH_P_OUI);
	p += sizeof(*ethhdr);

	os_memcpy(p, global_oui, sizeof(global_oui));
	p[sizeof(global_oui)] = ctx->oui_suffix;
	p += sizeof(global_oui) + 1;

	os_memcpy(p, buf, len);

	ret = l2_packet_send(iface->l2, NULL, 0, packet, packet_len);
	os_free(packet);
	return ret;
}
