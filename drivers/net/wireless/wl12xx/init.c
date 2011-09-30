/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "init.h"
#include "wl12xx_80211.h"
#include "acx.h"
#include "cmd.h"
#include "reg.h"
#include "tx.h"
#include "io.h"

int wl1271_sta_init_templates_config(struct wl1271 *wl)
{
	int ret, i;

	/* send empty templates for fw memory reservation */
	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_CFG_PROBE_REQ_2_4, NULL,
				      WL1271_CMD_TEMPL_DFLT_SIZE,
				      0, WL1271_RATE_AUTOMATIC);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_CFG_PROBE_REQ_5,
				      NULL, WL1271_CMD_TEMPL_DFLT_SIZE, 0,
				      WL1271_RATE_AUTOMATIC);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_NULL_DATA, NULL,
				      sizeof(struct wl12xx_null_data_template),
				      0, WL1271_RATE_AUTOMATIC);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_PS_POLL, NULL,
				      sizeof(struct wl12xx_ps_poll_template),
				      0, WL1271_RATE_AUTOMATIC);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_QOS_NULL_DATA, NULL,
				      sizeof
				      (struct wl12xx_qos_null_data_template),
				      0, WL1271_RATE_AUTOMATIC);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_PROBE_RESPONSE, NULL,
				      WL1271_CMD_TEMPL_DFLT_SIZE,
				      0, WL1271_RATE_AUTOMATIC);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_BEACON, NULL,
				      WL1271_CMD_TEMPL_DFLT_SIZE,
				      0, WL1271_RATE_AUTOMATIC);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_ARP_RSP, NULL,
				      sizeof
				      (struct wl12xx_arp_rsp_template),
				      0, WL1271_RATE_AUTOMATIC);
	if (ret < 0)
		return ret;

	for (i = 0; i < CMD_TEMPL_KLV_IDX_MAX; i++) {
		ret = wl1271_cmd_template_set(wl, CMD_TEMPL_KLV, NULL,
					      WL1271_CMD_TEMPL_DFLT_SIZE, i,
					      WL1271_RATE_AUTOMATIC);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int wl1271_ap_init_deauth_template(struct wl1271 *wl)
{
	struct wl12xx_disconn_template *tmpl;
	int ret;
	u32 rate;

	tmpl = kzalloc(sizeof(*tmpl), GFP_KERNEL);
	if (!tmpl) {
		ret = -ENOMEM;
		goto out;
	}

	tmpl->header.frame_ctl = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					     IEEE80211_STYPE_DEAUTH);

	rate = wl1271_tx_min_rate_get(wl, wl->basic_rate_set);
	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_DEAUTH_AP,
				      tmpl, sizeof(*tmpl), 0, rate);

out:
	kfree(tmpl);
	return ret;
}

static int wl1271_ap_init_null_template(struct wl1271 *wl)
{
	struct ieee80211_hdr_3addr *nullfunc;
	int ret;
	u32 rate;

	nullfunc = kzalloc(sizeof(*nullfunc), GFP_KERNEL);
	if (!nullfunc) {
		ret = -ENOMEM;
		goto out;
	}

	nullfunc->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					      IEEE80211_STYPE_NULLFUNC |
					      IEEE80211_FCTL_FROMDS);

	/* nullfunc->addr1 is filled by FW */

	memcpy(nullfunc->addr2, wl->mac_addr, ETH_ALEN);
	memcpy(nullfunc->addr3, wl->mac_addr, ETH_ALEN);

	rate = wl1271_tx_min_rate_get(wl, wl->basic_rate_set);
	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_NULL_DATA, nullfunc,
				      sizeof(*nullfunc), 0, rate);

out:
	kfree(nullfunc);
	return ret;
}

static int wl1271_ap_init_qos_null_template(struct wl1271 *wl)
{
	struct ieee80211_qos_hdr *qosnull;
	int ret;
	u32 rate;

	qosnull = kzalloc(sizeof(*qosnull), GFP_KERNEL);
	if (!qosnull) {
		ret = -ENOMEM;
		goto out;
	}

	qosnull->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					     IEEE80211_STYPE_QOS_NULLFUNC |
					     IEEE80211_FCTL_FROMDS);

	/* qosnull->addr1 is filled by FW */

	memcpy(qosnull->addr2, wl->mac_addr, ETH_ALEN);
	memcpy(qosnull->addr3, wl->mac_addr, ETH_ALEN);

	rate = wl1271_tx_min_rate_get(wl, wl->basic_rate_set);
	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_QOS_NULL_DATA, qosnull,
				      sizeof(*qosnull), 0, rate);

out:
	kfree(qosnull);
	return ret;
}

static int wl1271_ap_init_templates_config(struct wl1271 *wl)
{
	int ret;

	/*
	 * Put very large empty placeholders for all templates. These
	 * reserve memory for later.
	 */
	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_AP_PROBE_RESPONSE, NULL,
				      WL1271_CMD_TEMPL_MAX_SIZE,
				      0, WL1271_RATE_AUTOMATIC);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_AP_BEACON, NULL,
				      WL1271_CMD_TEMPL_MAX_SIZE,
				      0, WL1271_RATE_AUTOMATIC);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_DEAUTH_AP, NULL,
				      sizeof
				      (struct wl12xx_disconn_template),
				      0, WL1271_RATE_AUTOMATIC);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_NULL_DATA, NULL,
				      sizeof(struct wl12xx_null_data_template),
				      0, WL1271_RATE_AUTOMATIC);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_QOS_NULL_DATA, NULL,
				      sizeof
				      (struct wl12xx_qos_null_data_template),
				      0, WL1271_RATE_AUTOMATIC);
	if (ret < 0)
		return ret;

	return 0;
}

static int wl12xx_init_rx_config(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_acx_rx_msdu_life_time(wl);
	if (ret < 0)
		return ret;

	return 0;
}

int wl1271_init_phy_config(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_acx_pd_threshold(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_slot(wl, DEFAULT_SLOT_TIME);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_service_period_timeout(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_rts_threshold(wl, wl->hw->wiphy->rts_threshold);
	if (ret < 0)
		return ret;

	return 0;
}

static int wl1271_init_beacon_filter(struct wl1271 *wl)
{
	int ret;

	/* disable beacon filtering at this stage */
	ret = wl1271_acx_beacon_filter_opt(wl, false);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_beacon_filter_table(wl);
	if (ret < 0)
		return ret;

	return 0;
}

int wl1271_init_pta(struct wl1271 *wl)
{
	int ret;

	ret = wl12xx_acx_sg_cfg(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_sg_enable(wl, wl->sg_enabled);
	if (ret < 0)
		return ret;

	return 0;
}

int wl1271_init_energy_detection(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_acx_cca_threshold(wl);
	if (ret < 0)
		return ret;

	return 0;
}

static int wl1271_init_beacon_broadcast(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_acx_bcn_dtim_options(wl);
	if (ret < 0)
		return ret;

	return 0;
}

static int wl12xx_init_fwlog(struct wl1271 *wl)
{
	int ret;

	if (wl->quirks & WL12XX_QUIRK_FWLOG_NOT_IMPLEMENTED)
		return 0;

	ret = wl12xx_cmd_config_fwlog(wl);
	if (ret < 0)
		return ret;

	return 0;
}

static int wl1271_sta_hw_init(struct wl1271 *wl)
{
	int ret;

	if (wl->chip.id != CHIP_ID_1283_PG20) {
		ret = wl1271_cmd_ext_radio_parms(wl);
		if (ret < 0)
			return ret;
	}

	/* PS config */
	ret = wl1271_acx_config_ps(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_sta_init_templates_config(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_group_address_tbl(wl, true, NULL, 0);
	if (ret < 0)
		return ret;

	/* Initialize connection monitoring thresholds */
	ret = wl1271_acx_conn_monit_params(wl, false);
	if (ret < 0)
		return ret;

	/* Beacon filtering */
	ret = wl1271_init_beacon_filter(wl);
	if (ret < 0)
		return ret;

	/* FM WLAN coexistence */
	ret = wl1271_acx_fm_coex(wl);
	if (ret < 0)
		return ret;

	/* Beacons and broadcast settings */
	ret = wl1271_init_beacon_broadcast(wl);
	if (ret < 0)
		return ret;

	/* Configure for ELP power saving */
	ret = wl1271_acx_sleep_auth(wl, WL1271_PSM_ELP);
	if (ret < 0)
		return ret;

	/* Configure rssi/snr averaging weights */
	ret = wl1271_acx_rssi_snr_avg_weights(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_sta_rate_policies(wl);
	if (ret < 0)
		return ret;

	ret = wl12xx_acx_mem_cfg(wl);
	if (ret < 0)
		return ret;

	/* Configure the FW logger */
	ret = wl12xx_init_fwlog(wl);
	if (ret < 0)
		return ret;

	return 0;
}

static int wl1271_sta_hw_init_post_mem(struct wl1271 *wl)
{
	int ret, i;

	/* disable all keep-alive templates */
	for (i = 0; i < CMD_TEMPL_KLV_IDX_MAX; i++) {
		ret = wl1271_acx_keep_alive_config(wl, i,
						   ACX_KEEP_ALIVE_TPL_INVALID);
		if (ret < 0)
			return ret;
	}

	/* disable the keep-alive feature */
	ret = wl1271_acx_keep_alive_mode(wl, false);
	if (ret < 0)
		return ret;

	return 0;
}

static int wl1271_ap_hw_init(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_ap_init_templates_config(wl);
	if (ret < 0)
		return ret;

	/* Configure for power always on */
	ret = wl1271_acx_sleep_auth(wl, WL1271_PSM_CAM);
	if (ret < 0)
		return ret;

	ret = wl1271_init_ap_rates(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_ap_max_tx_retry(wl);
	if (ret < 0)
		return ret;

	ret = wl12xx_acx_mem_cfg(wl);
	if (ret < 0)
		return ret;

	/* initialize Tx power */
	ret = wl1271_acx_tx_power(wl, wl->power_level);
	if (ret < 0)
		return ret;

	return 0;
}

int wl1271_ap_init_templates(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_ap_init_deauth_template(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_ap_init_null_template(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_ap_init_qos_null_template(wl);
	if (ret < 0)
		return ret;

	/*
	 * when operating as AP we want to receive external beacons for
	 * configuring ERP protection.
	 */
	ret = wl1271_acx_beacon_filter_opt(wl, false);
	if (ret < 0)
		return ret;

	return 0;
}

static int wl1271_ap_hw_init_post_mem(struct wl1271 *wl)
{
	return wl1271_ap_init_templates(wl);
}

int wl1271_init_ap_rates(struct wl1271 *wl)
{
	int i, ret;
	struct conf_tx_rate_class rc;
	u32 supported_rates;

	wl1271_debug(DEBUG_AP, "AP basic rate set: 0x%x", wl->basic_rate_set);

	if (wl->basic_rate_set == 0)
		return -EINVAL;

	rc.enabled_rates = wl->basic_rate_set;
	rc.long_retry_limit = 10;
	rc.short_retry_limit = 10;
	rc.aflags = 0;
	ret = wl1271_acx_ap_rate_policy(wl, &rc, ACX_TX_AP_MODE_MGMT_RATE);
	if (ret < 0)
		return ret;

	/* use the min basic rate for AP broadcast/multicast */
	rc.enabled_rates = wl1271_tx_min_rate_get(wl, wl->basic_rate_set);
	rc.short_retry_limit = 10;
	rc.long_retry_limit = 10;
	rc.aflags = 0;
	ret = wl1271_acx_ap_rate_policy(wl, &rc, ACX_TX_AP_MODE_BCST_RATE);
	if (ret < 0)
		return ret;

	/*
	 * If the basic rates contain OFDM rates, use OFDM only
	 * rates for unicast TX as well. Else use all supported rates.
	 */
	if ((wl->basic_rate_set & CONF_TX_OFDM_RATES))
		supported_rates = CONF_TX_OFDM_RATES;
	else
		supported_rates = CONF_TX_AP_ENABLED_RATES;

	/* unconditionally enable HT rates */
	supported_rates |= CONF_TX_MCS_RATES;

	/* configure unicast TX rate classes */
	for (i = 0; i < wl->conf.tx.ac_conf_count; i++) {
		rc.enabled_rates = supported_rates;
		rc.short_retry_limit = 10;
		rc.long_retry_limit = 10;
		rc.aflags = 0;
		ret = wl1271_acx_ap_rate_policy(wl, &rc, i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int wl1271_set_ba_policies(struct wl1271 *wl)
{
	/* Reset the BA RX indicators */
	wl->ba_rx_bitmap = 0;
	wl->ba_allowed = true;
	wl->ba_rx_session_count = 0;

	/* BA is supported in STA/AP modes */
	if (wl->bss_type != BSS_TYPE_AP_BSS &&
	    wl->bss_type != BSS_TYPE_STA_BSS) {
		wl->ba_support = false;
		return 0;
	}

	wl->ba_support = true;

	/* 802.11n initiator BA session setting */
	return wl12xx_acx_set_ba_initiator_policy(wl);
}

int wl1271_chip_specific_init(struct wl1271 *wl)
{
	int ret = 0;

	if (wl->chip.id == CHIP_ID_1283_PG20) {
		u32 host_cfg_bitmap = HOST_IF_CFG_RX_FIFO_ENABLE;

		if (wl->quirks & WL12XX_QUIRK_BLOCKSIZE_ALIGNMENT)
			/* Enable SDIO padding */
			host_cfg_bitmap |= HOST_IF_CFG_TX_PAD_TO_SDIO_BLK;

		/* Must be before wl1271_acx_init_mem_config() */
		ret = wl1271_acx_host_if_cfg_bitmap(wl, host_cfg_bitmap);
		if (ret < 0)
			goto out;
	}
out:
	return ret;
}


int wl1271_hw_init(struct wl1271 *wl)
{
	struct conf_tx_ac_category *conf_ac;
	struct conf_tx_tid *conf_tid;
	int ret, i;
	bool is_ap = (wl->bss_type == BSS_TYPE_AP_BSS);

	if (wl->chip.id == CHIP_ID_1283_PG20)
		ret = wl128x_cmd_general_parms(wl);
	else
		ret = wl1271_cmd_general_parms(wl);
	if (ret < 0)
		return ret;

	if (wl->chip.id == CHIP_ID_1283_PG20)
		ret = wl128x_cmd_radio_parms(wl);
	else
		ret = wl1271_cmd_radio_parms(wl);
	if (ret < 0)
		return ret;

	/* Chip-specific init */
	ret = wl1271_chip_specific_init(wl);
	if (ret < 0)
		return ret;

	/* Mode specific init */
	if (is_ap)
		ret = wl1271_ap_hw_init(wl);
	else
		ret = wl1271_sta_hw_init(wl);

	if (ret < 0)
		return ret;

	/* Bluetooth WLAN coexistence */
	ret = wl1271_init_pta(wl);
	if (ret < 0)
		return ret;

	/* Default memory configuration */
	ret = wl1271_acx_init_mem_config(wl);
	if (ret < 0)
		return ret;

	/* RX config */
	ret = wl12xx_init_rx_config(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* PHY layer config */
	ret = wl1271_init_phy_config(wl);
	if (ret < 0)
		goto out_free_memmap;

	ret = wl1271_acx_dco_itrim_params(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Configure TX patch complete interrupt behavior */
	ret = wl1271_acx_tx_config_options(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* RX complete interrupt pacing */
	ret = wl1271_acx_init_rx_interrupt(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Energy detection */
	ret = wl1271_init_energy_detection(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Default fragmentation threshold */
	ret = wl1271_acx_frag_threshold(wl, wl->hw->wiphy->frag_threshold);
	if (ret < 0)
		goto out_free_memmap;

	/* Default TID/AC configuration */
	BUG_ON(wl->conf.tx.tid_conf_count != wl->conf.tx.ac_conf_count);
	for (i = 0; i < wl->conf.tx.tid_conf_count; i++) {
		conf_ac = &wl->conf.tx.ac_conf[i];
		ret = wl1271_acx_ac_cfg(wl, conf_ac->ac, conf_ac->cw_min,
					conf_ac->cw_max, conf_ac->aifsn,
					conf_ac->tx_op_limit);
		if (ret < 0)
			goto out_free_memmap;

		conf_tid = &wl->conf.tx.tid_conf[i];
		ret = wl1271_acx_tid_cfg(wl, conf_tid->queue_id,
					 conf_tid->channel_type,
					 conf_tid->tsid,
					 conf_tid->ps_scheme,
					 conf_tid->ack_policy,
					 conf_tid->apsd_conf[0],
					 conf_tid->apsd_conf[1]);
		if (ret < 0)
			goto out_free_memmap;
	}

	/* Enable data path */
	ret = wl1271_cmd_data_path(wl, 1);
	if (ret < 0)
		goto out_free_memmap;

	/* Configure HW encryption */
	ret = wl1271_acx_feature_cfg(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* configure PM */
	ret = wl1271_acx_pm_config(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Mode specific init - post mem init */
	if (is_ap)
		ret = wl1271_ap_hw_init_post_mem(wl);
	else
		ret = wl1271_sta_hw_init_post_mem(wl);

	if (ret < 0)
		goto out_free_memmap;

	ret = wl12xx_acx_set_rate_mgmt_params(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Configure initiator BA sessions policies */
	ret = wl1271_set_ba_policies(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* configure hangover */
	ret = wl12xx_acx_config_hangover(wl);
	if (ret < 0)
		goto out_free_memmap;

	return 0;

 out_free_memmap:
	kfree(wl->target_mem_map);
	wl->target_mem_map = NULL;

	return ret;
}
