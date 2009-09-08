/*
 * This file is part of wl12xx
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

#include "init.h"
#include "wl12xx_80211.h"
#include "acx.h"
#include "cmd.h"

int wl12xx_hw_init_hwenc_config(struct wl12xx *wl)
{
	int ret;

	ret = wl12xx_acx_feature_cfg(wl);
	if (ret < 0) {
		wl12xx_warning("couldn't set feature config");
		return ret;
	}

	ret = wl12xx_acx_default_key(wl, wl->default_key);
	if (ret < 0) {
		wl12xx_warning("couldn't set default key");
		return ret;
	}

	return 0;
}

int wl12xx_hw_init_templates_config(struct wl12xx *wl)
{
	int ret;
	u8 partial_vbm[PARTIAL_VBM_MAX];

	/* send empty templates for fw memory reservation */
	ret = wl12xx_cmd_template_set(wl, CMD_PROBE_REQ, NULL,
				      sizeof(struct wl12xx_probe_req_template));
	if (ret < 0)
		return ret;

	ret = wl12xx_cmd_template_set(wl, CMD_NULL_DATA, NULL,
				      sizeof(struct wl12xx_null_data_template));
	if (ret < 0)
		return ret;

	ret = wl12xx_cmd_template_set(wl, CMD_PS_POLL, NULL,
				      sizeof(struct wl12xx_ps_poll_template));
	if (ret < 0)
		return ret;

	ret = wl12xx_cmd_template_set(wl, CMD_QOS_NULL_DATA, NULL,
				      sizeof
				      (struct wl12xx_qos_null_data_template));
	if (ret < 0)
		return ret;

	ret = wl12xx_cmd_template_set(wl, CMD_PROBE_RESP, NULL,
				      sizeof
				      (struct wl12xx_probe_resp_template));
	if (ret < 0)
		return ret;

	ret = wl12xx_cmd_template_set(wl, CMD_BEACON, NULL,
				      sizeof
				      (struct wl12xx_beacon_template));
	if (ret < 0)
		return ret;

	/* tim templates, first reserve space then allocate an empty one */
	memset(partial_vbm, 0, PARTIAL_VBM_MAX);
	ret = wl12xx_cmd_vbm(wl, TIM_ELE_ID, partial_vbm, PARTIAL_VBM_MAX, 0);
	if (ret < 0)
		return ret;

	ret = wl12xx_cmd_vbm(wl, TIM_ELE_ID, partial_vbm, 1, 0);
	if (ret < 0)
		return ret;

	return 0;
}

int wl12xx_hw_init_rx_config(struct wl12xx *wl, u32 config, u32 filter)
{
	int ret;

	ret = wl12xx_acx_rx_msdu_life_time(wl, RX_MSDU_LIFETIME_DEF);
	if (ret < 0)
		return ret;

	ret = wl12xx_acx_rx_config(wl, config, filter);
	if (ret < 0)
		return ret;

	return 0;
}

int wl12xx_hw_init_phy_config(struct wl12xx *wl)
{
	int ret;

	ret = wl12xx_acx_pd_threshold(wl);
	if (ret < 0)
		return ret;

	ret = wl12xx_acx_slot(wl, DEFAULT_SLOT_TIME);
	if (ret < 0)
		return ret;

	ret = wl12xx_acx_group_address_tbl(wl);
	if (ret < 0)
		return ret;

	ret = wl12xx_acx_service_period_timeout(wl);
	if (ret < 0)
		return ret;

	ret = wl12xx_acx_rts_threshold(wl, RTS_THRESHOLD_DEF);
	if (ret < 0)
		return ret;

	return 0;
}

int wl12xx_hw_init_beacon_filter(struct wl12xx *wl)
{
	int ret;

	ret = wl12xx_acx_beacon_filter_opt(wl);
	if (ret < 0)
		return ret;

	ret = wl12xx_acx_beacon_filter_table(wl);
	if (ret < 0)
		return ret;

	return 0;
}

int wl12xx_hw_init_pta(struct wl12xx *wl)
{
	int ret;

	ret = wl12xx_acx_sg_enable(wl);
	if (ret < 0)
		return ret;

	ret = wl12xx_acx_sg_cfg(wl);
	if (ret < 0)
		return ret;

	return 0;
}

int wl12xx_hw_init_energy_detection(struct wl12xx *wl)
{
	int ret;

	ret = wl12xx_acx_cca_threshold(wl);
	if (ret < 0)
		return ret;

	return 0;
}

int wl12xx_hw_init_beacon_broadcast(struct wl12xx *wl)
{
	int ret;

	ret = wl12xx_acx_bcn_dtim_options(wl);
	if (ret < 0)
		return ret;

	return 0;
}

int wl12xx_hw_init_power_auth(struct wl12xx *wl)
{
	return wl12xx_acx_sleep_auth(wl, WL12XX_PSM_CAM);
}
