/*
 * This file is part of wl1251
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Kalle Valo <kalle.valo@nokia.com>
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

#include "wl1251_init.h"
#include "wl12xx_80211.h"
#include "wl1251_acx.h"
#include "wl1251_cmd.h"
#include "wl1251_reg.h"

int wl1251_hw_init_hwenc_config(struct wl1251 *wl)
{
	int ret;

	ret = wl1251_acx_feature_cfg(wl);
	if (ret < 0) {
		wl1251_warning("couldn't set feature config");
		return ret;
	}

	ret = wl1251_acx_default_key(wl, wl->default_key);
	if (ret < 0) {
		wl1251_warning("couldn't set default key");
		return ret;
	}

	return 0;
}

int wl1251_hw_init_templates_config(struct wl1251 *wl)
{
	int ret;
	u8 partial_vbm[PARTIAL_VBM_MAX];

	/* send empty templates for fw memory reservation */
	ret = wl1251_cmd_template_set(wl, CMD_PROBE_REQ, NULL,
				      sizeof(struct wl12xx_probe_req_template));
	if (ret < 0)
		return ret;

	ret = wl1251_cmd_template_set(wl, CMD_NULL_DATA, NULL,
				      sizeof(struct wl12xx_null_data_template));
	if (ret < 0)
		return ret;

	ret = wl1251_cmd_template_set(wl, CMD_PS_POLL, NULL,
				      sizeof(struct wl12xx_ps_poll_template));
	if (ret < 0)
		return ret;

	ret = wl1251_cmd_template_set(wl, CMD_QOS_NULL_DATA, NULL,
				      sizeof
				      (struct wl12xx_qos_null_data_template));
	if (ret < 0)
		return ret;

	ret = wl1251_cmd_template_set(wl, CMD_PROBE_RESP, NULL,
				      sizeof
				      (struct wl12xx_probe_resp_template));
	if (ret < 0)
		return ret;

	ret = wl1251_cmd_template_set(wl, CMD_BEACON, NULL,
				      sizeof
				      (struct wl12xx_beacon_template));
	if (ret < 0)
		return ret;

	/* tim templates, first reserve space then allocate an empty one */
	memset(partial_vbm, 0, PARTIAL_VBM_MAX);
	ret = wl1251_cmd_vbm(wl, TIM_ELE_ID, partial_vbm, PARTIAL_VBM_MAX, 0);
	if (ret < 0)
		return ret;

	ret = wl1251_cmd_vbm(wl, TIM_ELE_ID, partial_vbm, 1, 0);
	if (ret < 0)
		return ret;

	return 0;
}

int wl1251_hw_init_rx_config(struct wl1251 *wl, u32 config, u32 filter)
{
	int ret;

	ret = wl1251_acx_rx_msdu_life_time(wl, RX_MSDU_LIFETIME_DEF);
	if (ret < 0)
		return ret;

	ret = wl1251_acx_rx_config(wl, config, filter);
	if (ret < 0)
		return ret;

	return 0;
}

int wl1251_hw_init_phy_config(struct wl1251 *wl)
{
	int ret;

	ret = wl1251_acx_pd_threshold(wl);
	if (ret < 0)
		return ret;

	ret = wl1251_acx_slot(wl, DEFAULT_SLOT_TIME);
	if (ret < 0)
		return ret;

	ret = wl1251_acx_group_address_tbl(wl);
	if (ret < 0)
		return ret;

	ret = wl1251_acx_service_period_timeout(wl);
	if (ret < 0)
		return ret;

	ret = wl1251_acx_rts_threshold(wl, RTS_THRESHOLD_DEF);
	if (ret < 0)
		return ret;

	return 0;
}

int wl1251_hw_init_beacon_filter(struct wl1251 *wl)
{
	int ret;

	/* disable beacon filtering at this stage */
	ret = wl1251_acx_beacon_filter_opt(wl, false);
	if (ret < 0)
		return ret;

	ret = wl1251_acx_beacon_filter_table(wl);
	if (ret < 0)
		return ret;

	return 0;
}

int wl1251_hw_init_pta(struct wl1251 *wl)
{
	int ret;

	ret = wl1251_acx_sg_enable(wl);
	if (ret < 0)
		return ret;

	ret = wl1251_acx_sg_cfg(wl);
	if (ret < 0)
		return ret;

	return 0;
}

int wl1251_hw_init_energy_detection(struct wl1251 *wl)
{
	int ret;

	ret = wl1251_acx_cca_threshold(wl);
	if (ret < 0)
		return ret;

	return 0;
}

int wl1251_hw_init_beacon_broadcast(struct wl1251 *wl)
{
	int ret;

	ret = wl1251_acx_bcn_dtim_options(wl);
	if (ret < 0)
		return ret;

	return 0;
}

int wl1251_hw_init_power_auth(struct wl1251 *wl)
{
	return wl1251_acx_sleep_auth(wl, WL1251_PSM_CAM);
}

int wl1251_hw_init_mem_config(struct wl1251 *wl)
{
	int ret;

	ret = wl1251_acx_mem_cfg(wl);
	if (ret < 0)
		return ret;

	wl->target_mem_map = kzalloc(sizeof(struct wl1251_acx_mem_map),
					  GFP_KERNEL);
	if (!wl->target_mem_map) {
		wl1251_error("couldn't allocate target memory map");
		return -ENOMEM;
	}

	/* we now ask for the firmware built memory map */
	ret = wl1251_acx_mem_map(wl, wl->target_mem_map,
				 sizeof(struct wl1251_acx_mem_map));
	if (ret < 0) {
		wl1251_error("couldn't retrieve firmware memory map");
		kfree(wl->target_mem_map);
		wl->target_mem_map = NULL;
		return ret;
	}

	return 0;
}

static int wl1251_hw_init_txq_fill(u8 qid,
				   struct acx_tx_queue_qos_config *config,
				   u32 num_blocks)
{
	config->qid = qid;

	switch (qid) {
	case QOS_AC_BE:
		config->high_threshold =
			(QOS_TX_HIGH_BE_DEF * num_blocks) / 100;
		config->low_threshold =
			(QOS_TX_LOW_BE_DEF * num_blocks) / 100;
		break;
	case QOS_AC_BK:
		config->high_threshold =
			(QOS_TX_HIGH_BK_DEF * num_blocks) / 100;
		config->low_threshold =
			(QOS_TX_LOW_BK_DEF * num_blocks) / 100;
		break;
	case QOS_AC_VI:
		config->high_threshold =
			(QOS_TX_HIGH_VI_DEF * num_blocks) / 100;
		config->low_threshold =
			(QOS_TX_LOW_VI_DEF * num_blocks) / 100;
		break;
	case QOS_AC_VO:
		config->high_threshold =
			(QOS_TX_HIGH_VO_DEF * num_blocks) / 100;
		config->low_threshold =
			(QOS_TX_LOW_VO_DEF * num_blocks) / 100;
		break;
	default:
		wl1251_error("Invalid TX queue id: %d", qid);
		return -EINVAL;
	}

	return 0;
}

static int wl1251_hw_init_tx_queue_config(struct wl1251 *wl)
{
	struct acx_tx_queue_qos_config *config;
	struct wl1251_acx_mem_map *wl_mem_map = wl->target_mem_map;
	int ret, i;

	wl1251_debug(DEBUG_ACX, "acx tx queue config");

	config = kzalloc(sizeof(*config), GFP_KERNEL);
	if (!config) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < MAX_NUM_OF_AC; i++) {
		ret = wl1251_hw_init_txq_fill(i, config,
					      wl_mem_map->num_tx_mem_blocks);
		if (ret < 0)
			goto out;

		ret = wl1251_cmd_configure(wl, ACX_TX_QUEUE_CFG,
					   config, sizeof(*config));
		if (ret < 0)
			goto out;
	}

	wl1251_acx_ac_cfg(wl, AC_BE, CWMIN_BE, CWMAX_BE, AIFS_DIFS, TXOP_BE);
	wl1251_acx_ac_cfg(wl, AC_BK, CWMIN_BK, CWMAX_BK, AIFS_DIFS, TXOP_BK);
	wl1251_acx_ac_cfg(wl, AC_VI, CWMIN_VI, CWMAX_VI, AIFS_DIFS, TXOP_VI);
	wl1251_acx_ac_cfg(wl, AC_VO, CWMIN_VO, CWMAX_VO, AIFS_DIFS, TXOP_VO);

out:
	kfree(config);
	return ret;
}

static int wl1251_hw_init_data_path_config(struct wl1251 *wl)
{
	int ret;

	/* asking for the data path parameters */
	wl->data_path = kzalloc(sizeof(struct acx_data_path_params_resp),
				GFP_KERNEL);
	if (!wl->data_path) {
		wl1251_error("Couldnt allocate data path parameters");
		return -ENOMEM;
	}

	ret = wl1251_acx_data_path_params(wl, wl->data_path);
	if (ret < 0) {
		kfree(wl->data_path);
		wl->data_path = NULL;
		return ret;
	}

	return 0;
}


int wl1251_hw_init(struct wl1251 *wl)
{
	struct wl1251_acx_mem_map *wl_mem_map;
	int ret;

	ret = wl1251_hw_init_hwenc_config(wl);
	if (ret < 0)
		return ret;

	/* Template settings */
	ret = wl1251_hw_init_templates_config(wl);
	if (ret < 0)
		return ret;

	/* Default memory configuration */
	ret = wl1251_hw_init_mem_config(wl);
	if (ret < 0)
		return ret;

	/* Default data path configuration  */
	ret = wl1251_hw_init_data_path_config(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* RX config */
	ret = wl1251_hw_init_rx_config(wl,
				       RX_CFG_PROMISCUOUS | RX_CFG_TSF,
				       RX_FILTER_OPTION_DEF);
	/* RX_CONFIG_OPTION_ANY_DST_ANY_BSS,
	   RX_FILTER_OPTION_FILTER_ALL); */
	if (ret < 0)
		goto out_free_data_path;

	/* TX queues config */
	ret = wl1251_hw_init_tx_queue_config(wl);
	if (ret < 0)
		goto out_free_data_path;

	/* PHY layer config */
	ret = wl1251_hw_init_phy_config(wl);
	if (ret < 0)
		goto out_free_data_path;

	/* Initialize connection monitoring thresholds */
	ret = wl1251_acx_conn_monit_params(wl);
	if (ret < 0)
		goto out_free_data_path;

	/* Beacon filtering */
	ret = wl1251_hw_init_beacon_filter(wl);
	if (ret < 0)
		goto out_free_data_path;

	/* Bluetooth WLAN coexistence */
	ret = wl1251_hw_init_pta(wl);
	if (ret < 0)
		goto out_free_data_path;

	/* Energy detection */
	ret = wl1251_hw_init_energy_detection(wl);
	if (ret < 0)
		goto out_free_data_path;

	/* Beacons and boradcast settings */
	ret = wl1251_hw_init_beacon_broadcast(wl);
	if (ret < 0)
		goto out_free_data_path;

	/* Enable data path */
	ret = wl1251_cmd_data_path(wl, wl->channel, 1);
	if (ret < 0)
		goto out_free_data_path;

	/* Default power state */
	ret = wl1251_hw_init_power_auth(wl);
	if (ret < 0)
		goto out_free_data_path;

	wl_mem_map = wl->target_mem_map;
	wl1251_info("%d tx blocks at 0x%x, %d rx blocks at 0x%x",
		    wl_mem_map->num_tx_mem_blocks,
		    wl->data_path->tx_control_addr,
		    wl_mem_map->num_rx_mem_blocks,
		    wl->data_path->rx_control_addr);

	return 0;

 out_free_data_path:
	kfree(wl->data_path);

 out_free_memmap:
	kfree(wl->target_mem_map);

	return ret;
}
