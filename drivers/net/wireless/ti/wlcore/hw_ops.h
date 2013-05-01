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

static inline int
wlcore_hw_prepare_read(struct wl1271 *wl, u32 rx_desc, u32 len)
{
	if (wl->ops->prepare_read)
		return wl->ops->prepare_read(wl, rx_desc, len);

	return 0;
}

static inline u32
wlcore_hw_get_rx_packet_len(struct wl1271 *wl, void *rx_data, u32 data_len)
{
	if (!wl->ops->get_rx_packet_len)
		BUG_ON(1);

	return wl->ops->get_rx_packet_len(wl, rx_data, data_len);
}

static inline int wlcore_hw_tx_delayed_compl(struct wl1271 *wl)
{
	if (wl->ops->tx_delayed_compl)
		return wl->ops->tx_delayed_compl(wl);

	return 0;
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

static inline void
wlcore_hw_set_tx_desc_csum(struct wl1271 *wl,
			   struct wl1271_tx_hw_descr *desc,
			   struct sk_buff *skb)
{
	if (!wl->ops->set_tx_desc_csum)
		BUG_ON(1);

	wl->ops->set_tx_desc_csum(wl, desc, skb);
}

static inline void
wlcore_hw_set_rx_csum(struct wl1271 *wl,
		      struct wl1271_rx_descriptor *desc,
		      struct sk_buff *skb)
{
	if (wl->ops->set_rx_csum)
		wl->ops->set_rx_csum(wl, desc, skb);
}

static inline u32
wlcore_hw_ap_get_mimo_wide_rate_mask(struct wl1271 *wl,
				     struct wl12xx_vif *wlvif)
{
	if (wl->ops->ap_get_mimo_wide_rate_mask)
		return wl->ops->ap_get_mimo_wide_rate_mask(wl, wlvif);

	return 0;
}

static inline int
wlcore_debugfs_init(struct wl1271 *wl, struct dentry *rootdir)
{
	if (wl->ops->debugfs_init)
		return wl->ops->debugfs_init(wl, rootdir);

	return 0;
}

static inline int
wlcore_handle_static_data(struct wl1271 *wl, void *static_data)
{
	if (wl->ops->handle_static_data)
		return wl->ops->handle_static_data(wl, static_data);

	return 0;
}

static inline int
wlcore_hw_get_spare_blocks(struct wl1271 *wl, bool is_gem)
{
	if (!wl->ops->get_spare_blocks)
		BUG_ON(1);

	return wl->ops->get_spare_blocks(wl, is_gem);
}

static inline int
wlcore_hw_set_key(struct wl1271 *wl, enum set_key_cmd cmd,
		  struct ieee80211_vif *vif,
		  struct ieee80211_sta *sta,
		  struct ieee80211_key_conf *key_conf)
{
	if (!wl->ops->set_key)
		BUG_ON(1);

	return wl->ops->set_key(wl, cmd, vif, sta, key_conf);
}

static inline u32
wlcore_hw_pre_pkt_send(struct wl1271 *wl, u32 buf_offset, u32 last_len)
{
	if (wl->ops->pre_pkt_send)
		return wl->ops->pre_pkt_send(wl, buf_offset, last_len);

	return buf_offset;
}

static inline void
wlcore_hw_sta_rc_update(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			struct ieee80211_sta *sta, u32 changed)
{
	if (wl->ops->sta_rc_update)
		wl->ops->sta_rc_update(wl, wlvif, sta, changed);
}

static inline int
wlcore_hw_set_peer_cap(struct wl1271 *wl,
		       struct ieee80211_sta_ht_cap *ht_cap,
		       bool allow_ht_operation,
		       u32 rate_set, u8 hlid)
{
	if (wl->ops->set_peer_cap)
		return wl->ops->set_peer_cap(wl, ht_cap, allow_ht_operation,
					     rate_set, hlid);

	return 0;
}

static inline bool
wlcore_hw_lnk_high_prio(struct wl1271 *wl, u8 hlid,
			struct wl1271_link *lnk)
{
	if (!wl->ops->lnk_high_prio)
		BUG_ON(1);

	return wl->ops->lnk_high_prio(wl, hlid, lnk);
}

static inline bool
wlcore_hw_lnk_low_prio(struct wl1271 *wl, u8 hlid,
		       struct wl1271_link *lnk)
{
	if (!wl->ops->lnk_low_prio)
		BUG_ON(1);

	return wl->ops->lnk_low_prio(wl, hlid, lnk);
}

#endif
