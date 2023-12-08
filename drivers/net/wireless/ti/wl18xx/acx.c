// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2011 Texas Instruments Inc.
 */

#include "../wlcore/cmd.h"
#include "../wlcore/debug.h"
#include "../wlcore/acx.h"

#include "acx.h"
#include "wl18xx.h"

int wl18xx_acx_host_if_cfg_bitmap(struct wl1271 *wl, u32 host_cfg_bitmap,
				  u32 sdio_blk_size, u32 extra_mem_blks,
				  u32 len_field_size)
{
	struct wl18xx_acx_host_config_bitmap *bitmap_conf;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx cfg bitmap %d blk %d spare %d field %d",
		     host_cfg_bitmap, sdio_blk_size, extra_mem_blks,
		     len_field_size);

	bitmap_conf = kzalloc(sizeof(*bitmap_conf), GFP_KERNEL);
	if (!bitmap_conf) {
		ret = -ENOMEM;
		goto out;
	}

	bitmap_conf->host_cfg_bitmap = cpu_to_le32(host_cfg_bitmap);
	bitmap_conf->host_sdio_block_size = cpu_to_le32(sdio_blk_size);
	bitmap_conf->extra_mem_blocks = cpu_to_le32(extra_mem_blks);
	bitmap_conf->length_field_size = cpu_to_le32(len_field_size);

	ret = wl1271_cmd_configure(wl, ACX_HOST_IF_CFG_BITMAP,
				   bitmap_conf, sizeof(*bitmap_conf));
	if (ret < 0) {
		wl1271_warning("wl1271 bitmap config opt failed: %d", ret);
		goto out;
	}

out:
	kfree(bitmap_conf);

	return ret;
}

int wl18xx_acx_set_checksum_state(struct wl1271 *wl)
{
	struct wl18xx_acx_checksum_state *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx checksum state");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->checksum_state = CHECKSUM_OFFLOAD_ENABLED;

	ret = wl1271_cmd_configure(wl, ACX_CSUM_CONFIG, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("failed to set Tx checksum state: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl18xx_acx_clear_statistics(struct wl1271 *wl)
{
	struct wl18xx_acx_clear_statistics *acx;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx clear statistics");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wl1271_cmd_configure(wl, ACX_CLEAR_STATISTICS, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("failed to clear firmware statistics: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl18xx_acx_peer_ht_operation_mode(struct wl1271 *wl, u8 hlid, bool wide)
{
	struct wlcore_peer_ht_operation_mode *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx peer ht operation mode hlid %d bw %d",
		     hlid, wide);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->hlid = hlid;
	acx->bandwidth = wide ? WLCORE_BANDWIDTH_40MHZ : WLCORE_BANDWIDTH_20MHZ;

	ret = wl1271_cmd_configure(wl, ACX_PEER_HT_OPERATION_MODE_CFG, acx,
				   sizeof(*acx));

	if (ret < 0) {
		wl1271_warning("acx peer ht operation mode failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;

}

/*
 * this command is basically the same as wl1271_acx_ht_capabilities,
 * with the addition of supported rates. they should be unified in
 * the next fw api change
 */
int wl18xx_acx_set_peer_cap(struct wl1271 *wl,
			    struct ieee80211_sta_ht_cap *ht_cap,
			    bool allow_ht_operation,
			    u32 rate_set, u8 hlid)
{
	struct wlcore_acx_peer_cap *acx;
	int ret = 0;
	u32 ht_capabilites = 0;

	wl1271_debug(DEBUG_ACX,
		     "acx set cap ht_supp: %d ht_cap: %d rates: 0x%x",
		     ht_cap->ht_supported, ht_cap->cap, rate_set);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	if (allow_ht_operation && ht_cap->ht_supported) {
		/* no need to translate capabilities - use the spec values */
		ht_capabilites = ht_cap->cap;

		/*
		 * this bit is not employed by the spec but only by FW to
		 * indicate peer HT support
		 */
		ht_capabilites |= WL12XX_HT_CAP_HT_OPERATION;

		/* get data from A-MPDU parameters field */
		acx->ampdu_max_length = ht_cap->ampdu_factor;
		acx->ampdu_min_spacing = ht_cap->ampdu_density;
	}

	acx->hlid = hlid;
	acx->ht_capabilites = cpu_to_le32(ht_capabilites);
	acx->supported_rates = cpu_to_le32(rate_set);

	ret = wl1271_cmd_configure(wl, ACX_PEER_CAP, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx ht capabilities setting failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

/*
 * When the host is suspended, we don't want to get any fast-link/PSM
 * notifications
 */
int wl18xx_acx_interrupt_notify_config(struct wl1271 *wl,
				       bool action)
{
	struct wl18xx_acx_interrupt_notify *acx;
	int ret = 0;

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->enable = action;
	ret = wl1271_cmd_configure(wl, ACX_INTERRUPT_NOTIFY, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx interrupt notify setting failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

/*
 * When the host is suspended, we can configure the FW to disable RX BA
 * notifications.
 */
int wl18xx_acx_rx_ba_filter(struct wl1271 *wl, bool action)
{
	struct wl18xx_acx_rx_ba_filter *acx;
	int ret = 0;

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->enable = (u32)action;
	ret = wl1271_cmd_configure(wl, ACX_RX_BA_FILTER, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx rx ba activity filter setting failed: %d",
			       ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl18xx_acx_ap_sleep(struct wl1271 *wl)
{
	struct wl18xx_priv *priv = wl->priv;
	struct acx_ap_sleep_cfg *acx;
	struct conf_ap_sleep_settings *conf = &priv->conf.ap_sleep;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx config ap sleep");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->idle_duty_cycle = conf->idle_duty_cycle;
	acx->connected_duty_cycle = conf->connected_duty_cycle;
	acx->max_stations_thresh = conf->max_stations_thresh;
	acx->idle_conn_thresh = conf->idle_conn_thresh;

	ret = wl1271_cmd_configure(wl, ACX_AP_SLEEP_CFG, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx config ap-sleep failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl18xx_acx_dynamic_fw_traces(struct wl1271 *wl)
{
	struct acx_dynamic_fw_traces_cfg *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx dynamic fw traces config %d",
		     wl->dynamic_fw_traces);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->dynamic_fw_traces = cpu_to_le32(wl->dynamic_fw_traces);

	ret = wl1271_cmd_configure(wl, ACX_DYNAMIC_TRACES_CFG,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx config dynamic fw traces failed: %d", ret);
		goto out;
	}
out:
	kfree(acx);
	return ret;
}

int wl18xx_acx_time_sync_cfg(struct wl1271 *wl)
{
	struct acx_time_sync_cfg *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx time sync cfg: mode %d, addr: %pM",
		     wl->conf.sg.params[WL18XX_CONF_SG_TIME_SYNC],
		     wl->zone_master_mac_addr);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->sync_mode = wl->conf.sg.params[WL18XX_CONF_SG_TIME_SYNC];
	memcpy(acx->zone_mac_addr, wl->zone_master_mac_addr, ETH_ALEN);

	ret = wl1271_cmd_configure(wl, ACX_TIME_SYNC_CFG,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx time sync cfg failed: %d", ret);
		goto out;
	}
out:
	kfree(acx);
	return ret;
}
