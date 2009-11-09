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

	if (wl1271_11a_enabled()) {
		ret = wl1271_cmd_template_set(wl, CMD_TEMPL_CFG_PROBE_REQ_5,
				NULL,
				sizeof(struct wl12xx_probe_req_template));
		if (ret < 0)
			return ret;
	}

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

	ret = wl1271_acx_rx_msdu_life_time(wl);
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

	ret = wl1271_acx_group_address_tbl(wl, true, NULL, 0);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_service_period_timeout(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_acx_rts_threshold(wl, wl->conf.rx.rts_threshold);
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
	struct conf_general_parms *g = &wl->conf.init.genparam;
	int ret;

	gen_parms = kzalloc(sizeof(*gen_parms), GFP_KERNEL);
	if (!gen_parms)
		return -ENOMEM;

	gen_parms->id = TEST_CMD_INI_FILE_GENERAL_PARAM;

	gen_parms->ref_clk = g->ref_clk;
	gen_parms->settling_time = g->settling_time;
	gen_parms->clk_valid_on_wakeup = g->clk_valid_on_wakeup;
	gen_parms->dc2dcmode = g->dc2dcmode;
	gen_parms->single_dual_band = g->single_dual_band;
	gen_parms->tx_bip_fem_autodetect = g->tx_bip_fem_autodetect;
	gen_parms->tx_bip_fem_manufacturer = g->tx_bip_fem_manufacturer;
	gen_parms->settings = g->settings;

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
	struct wl1271_radio_parms *radio_parms;
	struct conf_radio_parms *r = &wl->conf.init.radioparam;
	int i, ret;

	radio_parms = kzalloc(sizeof(*radio_parms), GFP_KERNEL);
	if (!radio_parms)
		return -ENOMEM;

	radio_parms->id = TEST_CMD_INI_FILE_RADIO_PARAM;

	/* Static radio parameters */
	radio_parms->rx_trace_loss = r->rx_trace_loss;
	radio_parms->tx_trace_loss = r->tx_trace_loss;
	memcpy(radio_parms->rx_rssi_and_proc_compens,
	       r->rx_rssi_and_proc_compens,
	       CONF_RSSI_AND_PROCESS_COMPENSATION_SIZE);

	memcpy(radio_parms->rx_trace_loss_5, r->rx_trace_loss_5,
	       CONF_NUMBER_OF_SUB_BANDS_5);
	memcpy(radio_parms->tx_trace_loss_5, r->tx_trace_loss_5,
	       CONF_NUMBER_OF_SUB_BANDS_5);
	memcpy(radio_parms->rx_rssi_and_proc_compens_5,
	       r->rx_rssi_and_proc_compens_5,
	       CONF_RSSI_AND_PROCESS_COMPENSATION_SIZE);

	/* Dynamic radio parameters */
	radio_parms->tx_ref_pd_voltage = cpu_to_le16(r->tx_ref_pd_voltage);
	radio_parms->tx_ref_power = r->tx_ref_power;
	radio_parms->tx_offset_db = r->tx_offset_db;

	memcpy(radio_parms->tx_rate_limits_normal, r->tx_rate_limits_normal,
	       CONF_NUMBER_OF_RATE_GROUPS);
	memcpy(radio_parms->tx_rate_limits_degraded, r->tx_rate_limits_degraded,
	       CONF_NUMBER_OF_RATE_GROUPS);

	memcpy(radio_parms->tx_channel_limits_11b, r->tx_channel_limits_11b,
	       CONF_NUMBER_OF_CHANNELS_2_4);
	memcpy(radio_parms->tx_channel_limits_ofdm, r->tx_channel_limits_ofdm,
	       CONF_NUMBER_OF_CHANNELS_2_4);
	memcpy(radio_parms->tx_pdv_rate_offsets, r->tx_pdv_rate_offsets,
	       CONF_NUMBER_OF_RATE_GROUPS);
	memcpy(radio_parms->tx_ibias, r->tx_ibias, CONF_NUMBER_OF_RATE_GROUPS);

	radio_parms->rx_fem_insertion_loss = r->rx_fem_insertion_loss;

	for (i = 0; i < CONF_NUMBER_OF_SUB_BANDS_5; i++)
		radio_parms->tx_ref_pd_voltage_5[i] =
			cpu_to_le16(r->tx_ref_pd_voltage_5[i]);
	memcpy(radio_parms->tx_ref_power_5, r->tx_ref_power_5,
	       CONF_NUMBER_OF_SUB_BANDS_5);
	memcpy(radio_parms->tx_offset_db_5, r->tx_offset_db_5,
	       CONF_NUMBER_OF_SUB_BANDS_5);
	memcpy(radio_parms->tx_rate_limits_normal_5,
	       r->tx_rate_limits_normal_5, CONF_NUMBER_OF_RATE_GROUPS);
	memcpy(radio_parms->tx_rate_limits_degraded_5,
	       r->tx_rate_limits_degraded_5, CONF_NUMBER_OF_RATE_GROUPS);
	memcpy(radio_parms->tx_channel_limits_ofdm_5,
	       r->tx_channel_limits_ofdm_5, CONF_NUMBER_OF_CHANNELS_5);
	memcpy(radio_parms->tx_pdv_rate_offsets_5, r->tx_pdv_rate_offsets_5,
	       CONF_NUMBER_OF_RATE_GROUPS);
	memcpy(radio_parms->tx_ibias_5, r->tx_ibias_5,
	       CONF_NUMBER_OF_RATE_GROUPS);
	memcpy(radio_parms->rx_fem_insertion_loss_5,
	       r->rx_fem_insertion_loss_5, CONF_NUMBER_OF_SUB_BANDS_5);

	ret = wl1271_cmd_test(wl, radio_parms, sizeof(*radio_parms), 0);
	if (ret < 0)
		wl1271_warning("CMD_INI_FILE_RADIO_PARAM failed");

	kfree(radio_parms);
	return ret;
}

int wl1271_hw_init(struct wl1271 *wl)
{
	int ret;

	/* FIXME: the following parameter setting functions return error
	 * codes - the reason is so far unknown. The -EIO is therefore
	 * ignored for the time being. */
	ret = wl1271_init_general_parms(wl);
	if (ret < 0 && ret != -EIO)
		return ret;

	ret = wl1271_init_radio_parms(wl);
	if (ret < 0 && ret != -EIO)
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

	/* Initialize connection monitoring thresholds */
	ret = wl1271_acx_conn_monit_params(wl);
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
	ret = wl1271_acx_rate_policies(wl, CONF_TX_RATE_MASK_ALL);
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

	/* Configure smart reflex */
	ret = wl1271_acx_smart_reflex(wl);
	if (ret < 0)
		goto out_free_memmap;

	return 0;

 out_free_memmap:
	kfree(wl->target_mem_map);
	wl->target_mem_map = NULL;

	return ret;
}
