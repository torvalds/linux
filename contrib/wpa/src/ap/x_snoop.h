/*
 * Generic Snooping for Proxy ARP
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef X_SNOOP_H
#define X_SNOOP_H

#include "l2_packet/l2_packet.h"

#ifdef CONFIG_PROXYARP

int x_snoop_init(struct hostapd_data *hapd);
struct l2_packet_data *
x_snoop_get_l2_packet(struct hostapd_data *hapd,
		      void (*handler)(void *ctx, const u8 *src_addr,
				      const u8 *buf, size_t len),
		      enum l2_packet_filter_type type);
void x_snoop_mcast_to_ucast_convert_send(struct hostapd_data *hapd,
					 struct sta_info *sta, u8 *buf,
					 size_t len);
void x_snoop_deinit(struct hostapd_data *hapd);

#else /* CONFIG_PROXYARP */

static inline int x_snoop_init(struct hostapd_data *hapd)
{
	return 0;
}

static inline struct l2_packet_data *
x_snoop_get_l2_packet(struct hostapd_data *hapd,
		      void (*handler)(void *ctx, const u8 *src_addr,
				      const u8 *buf, size_t len),
		      enum l2_packet_filter_type type)
{
	return NULL;
}

static inline void
x_snoop_mcast_to_ucast_convert_send(struct hostapd_data *hapd,
				    struct sta_info *sta, void *buf,
				    size_t len)
{
}

static inline void x_snoop_deinit(struct hostapd_data *hapd)
{
}

#endif /* CONFIG_PROXYARP */

#endif /* X_SNOOP_H */
