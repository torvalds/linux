/*
 * This file is part of wlcore
 *
 * Copyright (C) 2011 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WLCORE_HW_OPS_H__
#define __WLCORE_HW_OPS_H__

#include "wlcore.h"
#include "rx.h"

static inline u32
wlcore_hw_calc_tx_blocks(struct wl1271 *wl, u32 len, u32 spare_blks)
{
	if (!wl->ops->calc_tx_blocks)
		BUG_ON(1);

	return wl->ops->calc_tx_blocks(wl, len, spare_blks);
}

static inline void
wlcore_hw_set_tx_desc_blocks(struct wl1271 *wl, struct wl1271_tx_hw_descr *desc,
			     u32 blks, u32 spare_blks)
{
	if (!wl->ops->set_tx_desc_blocks)
		BUG_ON(1);

	return wl->ops->set_tx_desc_blocks(wl, desc, blks, spare_blks);
}

static inline void
wlcore_hw_set_tx_desc_data_len(struct wl1271 *wl,
			       struct wl1271_tx_hw_descr *desc,
			       struct sk_buff *skb)
{
	if (!wl->ops->set_tx_desc_data_len)
		BUG_ON(1);

	wl->ops->set_tx_desc_data_len(wl, desc, skb);
}

static inline enum wl_rx_buf_align
wlcore_hw_get_rx_buf_align(struct wl1271 *wl, u32 rx_desc)
{

	if (!wl->ops->get_rx_buf_align)
		BUG_ON(1);

	return wl->ops->get_rx_buf_align(wl, rx_desc);
}

static inline void
wlcore_hw_prepare_read(struct wl1271 *wl, u32 rx_desc, u32 len)
{
	if (wl->ops->prepare_read)
		wl->ops->prepare_read(wl, rx_desc, len);
}

static inline u32
wlcore_hw_get_rx_packet_len(struct wl1271 *wl, void *rx_data, u32 data_len)
{
	if (!wl->ops->get_rx_packet_len)
		BUG_ON(1);

	return wl->ops->get_rx_packet_len(wl, rx_data, data_len);
}

static inline void wlcore_hw_tx_delayed_compl(struct wl1271 *wl)
{
	if (wl->ops->tx_delayed_compl)
		wl->ops->tx_delayed_compl(wl);
}

static inline void wlcore_hw_tx_immediate_compl(struct wl1271 *wl)
{
	if (wl->ops->tx_immediate_compl)
		wl->ops->tx_immediate_compl(wl);
}

static inline int
wlcore_hw_init_vif(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	if (wl->ops->init_vif)
		return wl->ops->init_vif(wl, wlvif);

	return 0;
}

static inline u32
wlcore_hw_sta_get_ap_rate_mask(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	if (!wl->ops->sta_get_ap_rate_mask)
		BUG_ON(1);

	return wl->ops->sta_get_ap_rate_mask(wl, wlvif);
}

static inline int wlcore_identify_fw(struct wl1271 *wl)
{
	if (wl->ops->identify_fw)
		return wl->ops->identify_fw(wl);

	return 0;
}

#endif
