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

#include "wl1271_init.h"
#include "wl12xx_80211.h"
#include "wl1271_acx.h"
#include "wl1271_cmd.h"
#include "wl1271_reg.h"

static int wl1271_init_hwenc_config(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_acx_feature_cfg(wl);
	if (ret < 0) {
		wl1271_warning("couldn't set feature config");
		return ret;
	}

	ret = wl1271_cmd_set_default_wep_key(wl, wl->default_key);
	if (ret < 0) {
		wl1271_warning("couldn't set default key");
		return ret;
	}

	return 0;
}

static int wl1271_init_templates_config(struct wl1271 *wl)
{
	int ret;

	/* send empty templates for fw memory reservation */
	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_CFG_PROBE_REQ_2_4, NULL,
				      sizeof(struct wl12xx_probe_req_template));
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_NULL_DATA, NULL,
				      sizeof(struct wl12xx_null_data_template));
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_PS_POLL, NULL,
				      sizeof(struct wl12xx_ps_poll_template));
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_QOS_NULL_DATA, NULL,
				      sizeof
				      (struct wl12xx_qos_null_data_template));
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_PROBE_RESPONSE, NULL,
				      sizeof
				      (struct wl12xx_probe_resp_template));
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_BEACON, NULL,
				      sizeof
				      (struct wl12xx_beacon_template));
	if (ret < 0)
		return ret;

	return 0;
}

static int wl1271_init_rx_config(struct wl1271 *wl, u32 config, u32 filter)
{
	int ret;

	ret = wl1271_acx_rx_msdu_life_time(wl, RX_MSDU_LIFETIME_DEF);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_rx_config(wl, config, filter);
	if (ret < 0)
		return ret;

	return 0;
}

static int wl1271_init_phy_config(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_acx_pd_threshold(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_slot(wl, DEFAULT_SLOT_TIME);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_group_address_tbl(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_service_period_timeout(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_rts_threshold(wl, RTS_THRESHOLD_DEF);
	if (ret < 0)
		return ret;

	return 0;
}

static int wl1271_init_beacon_filter(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_acx_beacon_filter_opt(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_beacon_filter_table(wl);
	if (ret < 0)
		return ret;

	return 0;
}

static int wl1271_init_pta(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_acx_sg_enable(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_sg_cfg(wl);
	if (ret < 0)
		return ret;

	return 0;
}

static int wl1271_init_energy_detection(struct wl1271 *wl)
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

static int wl1271_init_general_parms(struct wl1271 *wl)
{
	struct wl1271_general_parms *gen_parms;
	int ret;

	gen_parms = kzalloc(sizeof(*gen_parms), GFP_KERNEL);
	if (!gen_parms)
		return -ENOMEM;

	gen_parms->id = TEST_CMD_INI_FILE_GENERAL_PARAM;

	gen_parms->ref_clk = REF_CLK_38_4_E;
	/* FIXME: magic numbers */
	gen_parms->settling_time = 5;
	gen_parms->clk_valid_on_wakeup = 0;
	gen_parms->dc2dcmode = 0;
	gen_parms->single_dual_band = 0;
	gen_parms->tx_bip_fem_autodetect = 1;
	gen_parms->tx_bip_fem_manufacturer = 1;
	gen_parms->settings = 1;

	ret = wl1271_cmd_test(wl, gen_parms, sizeof(*gen_parms), 0);
	if (ret < 0) {
		wl1271_warning("CMD_INI_FILE_GENERAL_PARAM failed");
		return ret;
	}

	kfree(gen_parms);
	return 0;
}

static int wl1271_init_radio_parms(struct wl1271 *wl)
{
	/*
	 * FIXME: All these magic numbers should be moved to some place where
	 * they can be configured (separate file?)
	 */

	struct wl1271_radio_parms *radio_parms;
	int ret;
	u8 compensation[] = { 0xec, 0xf6, 0x00, 0x0c, 0x18, 0xf8, 0xfc, 0x00,
			      0x08, 0x10, 0xf0, 0xf8, 0x00, 0x0a, 0x14 };

	u8 tx_rate_limits_normal[]   = { 0x1e, 0x1f, 0x22, 0x24, 0x28, 0x29 };
	u8 tx_rate_limits_degraded[] = { 0x1b, 0x1c, 0x1e, 0x20, 0x24, 0x25 };

	u8 tx_channel_limits_11b[] = { 0x22, 0x50, 0x50, 0x50,
				       0x50, 0x50, 0x50, 0x50,
				       0x50, 0x50, 0x22, 0x50,
				       0x22, 0x50 };

	u8 tx_channel_limits_ofdm[] = { 0x20, 0x50, 0x50, 0x50,
					0x50, 0x50, 0x50, 0x50,
					0x50, 0x50, 0x20, 0x50,
					0x20, 0x50 };

	u8 tx_pdv_rate_offsets[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	u8 tx_ibias[] = { 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x27 };

	radio_parms = kzalloc(sizeof(*radio_parms), GFP_KERNEL);
	if (!radio_parms)
		return -ENOMEM;

	radio_parms->id = TEST_CMD_INI_FILE_RADIO_PARAM;

	/* Static radio parameters */
	radio_parms->rx_trace_loss = 10;
	radio_parms->tx_trace_loss = 10;
	memcpy(radio_parms->rx_rssi_and_proc_compens, compensation,
	       sizeof(compensation));

	/* We don't set the 5GHz -- N/A */

	/* Dynamic radio parameters */
	radio_parms->tx_ref_pd_voltage = cpu_to_le16(0x24e);
	radio_parms->tx_ref_power = 0x78;
	radio_parms->tx_offset_db = 0x0;

	memcpy(radio_parms->tx_rate_limits_normal, tx_rate_limits_normal,
	       sizeof(tx_rate_limits_normal));
	memcpy(radio_parms->tx_rate_limits_degraded, tx_rate_limits_degraded,
	       sizeof(tx_rate_limits_degraded));

	memcpy(radio_parms->tx_channel_limits_11b, tx_channel_limits_11b,
	       sizeof(tx_channel_limits_11b));
	memcpy(radio_parms->tx_channel_limits_ofdm, tx_channel_limits_ofdm,
	       sizeof(tx_channel_limits_ofdm));
	memcpy(radio_parms->tx_pdv_rate_offsets, tx_pdv_rate_offsets,
	       sizeof(tx_pdv_rate_offsets));
	memcpy(radio_parms->tx_ibias, tx_ibias,
	       sizeof(tx_ibias));

	radio_parms->rx_fem_insertion_loss = 0x14;

	ret = wl1271_cmd_test(wl, radio_parms, sizeof(*radio_parms), 0);
	if (ret < 0)
		wl1271_warning("CMD_INI_FILE_RADIO_PARAM failed");

	kfree(radio_parms);
	return ret;
}

int wl1271_hw_init(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_init_general_parms(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_init_radio_parms(wl);
	if (ret < 0)
		return ret;

	/* Template settings */
	ret = wl1271_init_templates_config(wl);
	if (ret < 0)
		return ret;

	/* Default memory configuration */
	ret = wl1271_acx_init_mem_config(wl);
	if (ret < 0)
		return ret;

	/* RX config */
	ret = wl1271_init_rx_config(wl,
				       RX_CFG_PROMISCUOUS | RX_CFG_TSF,
				       RX_FILTER_OPTION_DEF);
	/* RX_CONFIG_OPTION_ANY_DST_ANY_BSS,
	   RX_FILTER_OPTION_FILTER_ALL); */
	if (ret < 0)
		goto out_free_memmap;

	/* PHY layer config */
	ret = wl1271_init_phy_config(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Beacon filtering */
	ret = wl1271_init_beacon_filter(wl);
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

	/* Bluetooth WLAN coexistence */
	ret = wl1271_init_pta(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Energy detection */
	ret = wl1271_init_energy_detection(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Beacons and boradcast settings */
	ret = wl1271_init_beacon_broadcast(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Default fragmentation threshold */
	ret = wl1271_acx_frag_threshold(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Default TID configuration */
	ret = wl1271_acx_tid_cfg(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Default AC configuration */
	ret = wl1271_acx_ac_cfg(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Configure TX rate classes */
	ret = wl1271_acx_rate_policies(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* Enable data path */
	ret = wl1271_cmd_data_path(wl, wl->channel, 1);
	if (ret < 0)
		goto out_free_memmap;

	/* Configure for ELP power saving */
	ret = wl1271_acx_sleep_auth(wl, WL1271_PSM_ELP);
	if (ret < 0)
		goto out_free_memmap;

	/* Configure HW encryption */
	ret = wl1271_init_hwenc_config(wl);
	if (ret < 0)
		goto out_free_memmap;

	return 0;

 out_free_memmap:
	kfree(wl->target_mem_map);

	return ret;
}
