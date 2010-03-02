/*
 * This file is part of wl1271
 *
 * Copyright (C) 2008-2009 Nokia Corporation
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

#include "wl1271_acx.h"

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/crc7.h>
#include <linux/spi/spi.h>

#include "wl1271.h"
#include "wl12xx_80211.h"
#include "wl1271_reg.h"
#include "wl1271_spi.h"
#include "wl1271_ps.h"

int wl1271_acx_wake_up_conditions(struct wl1271 *wl)
{
	struct acx_wake_up_condition *wake_up;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx wake up conditions");

	wake_up = kzalloc(sizeof(*wake_up), GFP_KERNEL);
	if (!wake_up) {
		ret = -ENOMEM;
		goto out;
	}

	wake_up->wake_up_event = wl->conf.conn.wake_up_event;
	wake_up->listen_interval = wl->conf.conn.listen_interval;

	ret = wl1271_cmd_configure(wl, ACX_WAKE_UP_CONDITIONS,
				   wake_up, sizeof(*wake_up));
	if (ret < 0) {
		wl1271_warning("could not set wake up conditions: %d", ret);
		goto out;
	}

out:
	kfree(wake_up);
	return ret;
}

int wl1271_acx_sleep_auth(struct wl1271 *wl, u8 sleep_auth)
{
	struct acx_sleep_auth *auth;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx sleep auth");

	auth = kzalloc(sizeof(*auth), GFP_KERNEL);
	if (!auth) {
		ret = -ENOMEM;
		goto out;
	}

	auth->sleep_auth = sleep_auth;

	ret = wl1271_cmd_configure(wl, ACX_SLEEP_AUTH, auth, sizeof(*auth));
	if (ret < 0)
		return ret;

out:
	kfree(auth);
	return ret;
}

int wl1271_acx_fw_version(struct wl1271 *wl, char *buf, size_t len)
{
	struct acx_revision *rev;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx fw rev");

	rev = kzalloc(sizeof(*rev), GFP_KERNEL);
	if (!rev) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wl1271_cmd_interrogate(wl, ACX_FW_REV, rev, sizeof(*rev));
	if (ret < 0) {
		wl1271_warning("ACX_FW_REV interrogate failed");
		goto out;
	}

	/* be careful with the buffer sizes */
	strncpy(buf, rev->fw_version, min(len, sizeof(rev->fw_version)));

	/*
	 * if the firmware version string is exactly
	 * sizeof(rev->fw_version) long or fw_len is less than
	 * sizeof(rev->fw_version) it won't be null terminated
	 */
	buf[min(len, sizeof(rev->fw_version)) - 1] = '\0';

out:
	kfree(rev);
	return ret;
}

int wl1271_acx_tx_power(struct wl1271 *wl, int power)
{
	struct acx_current_tx_power *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx dot11_cur_tx_pwr");

	if (power < 0 || power > 25)
		return -EINVAL;

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * FIXME: This is a workaround needed while we don't the correct
	 * calibration, to avoid distortions
	 */
	/* acx->current_tx_power = power * 10; */
	acx->current_tx_power = 120;

	ret = wl1271_cmd_configure(wl, DOT11_CUR_TX_PWR, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("configure of tx power failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_feature_cfg(struct wl1271 *wl)
{
	struct acx_feature_config *feature;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx feature cfg");

	feature = kzalloc(sizeof(*feature), GFP_KERNEL);
	if (!feature) {
		ret = -ENOMEM;
		goto out;
	}

	/* DF_ENCRYPTION_DISABLE and DF_SNIFF_MODE_ENABLE are disabled */
	feature->data_flow_options = 0;
	feature->options = 0;

	ret = wl1271_cmd_configure(wl, ACX_FEATURE_CFG,
				   feature, sizeof(*feature));
	if (ret < 0) {
		wl1271_error("Couldnt set HW encryption");
		goto out;
	}

out:
	kfree(feature);
	return ret;
}

int wl1271_acx_mem_map(struct wl1271 *wl, struct acx_header *mem_map,
		       size_t len)
{
	int ret;

	wl1271_debug(DEBUG_ACX, "acx mem map");

	ret = wl1271_cmd_interrogate(wl, ACX_MEM_MAP, mem_map, len);
	if (ret < 0)
		return ret;

	return 0;
}

int wl1271_acx_rx_msdu_life_time(struct wl1271 *wl)
{
	struct acx_rx_msdu_lifetime *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx rx msdu life time");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->lifetime = cpu_to_le32(wl->conf.rx.rx_msdu_life_time);
	ret = wl1271_cmd_configure(wl, DOT11_RX_MSDU_LIFE_TIME,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("failed to set rx msdu life time: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_rx_config(struct wl1271 *wl, u32 config, u32 filter)
{
	struct acx_rx_config *rx_config;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx rx config");

	rx_config = kzalloc(sizeof(*rx_config), GFP_KERNEL);
	if (!rx_config) {
		ret = -ENOMEM;
		goto out;
	}

	rx_config->config_options = cpu_to_le32(config);
	rx_config->filter_options = cpu_to_le32(filter);

	ret = wl1271_cmd_configure(wl, ACX_RX_CFG,
				   rx_config, sizeof(*rx_config));
	if (ret < 0) {
		wl1271_warning("failed to set rx config: %d", ret);
		goto out;
	}

out:
	kfree(rx_config);
	return ret;
}

int wl1271_acx_pd_threshold(struct wl1271 *wl)
{
	struct acx_packet_detection *pd;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx data pd threshold");

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		ret = -ENOMEM;
		goto out;
	}

	pd->threshold = cpu_to_le32(wl->conf.rx.packet_detection_threshold);

	ret = wl1271_cmd_configure(wl, ACX_PD_THRESHOLD, pd, sizeof(*pd));
	if (ret < 0) {
		wl1271_warning("failed to set pd threshold: %d", ret);
		goto out;
	}

out:
	kfree(pd);
	return 0;
}

int wl1271_acx_slot(struct wl1271 *wl, enum acx_slot_type slot_time)
{
	struct acx_slot *slot;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx slot");

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot) {
		ret = -ENOMEM;
		goto out;
	}

	slot->wone_index = STATION_WONE_INDEX;
	slot->slot_time = slot_time;

	ret = wl1271_cmd_configure(wl, ACX_SLOT, slot, sizeof(*slot));
	if (ret < 0) {
		wl1271_warning("failed to set slot time: %d", ret);
		goto out;
	}

out:
	kfree(slot);
	return ret;
}

int wl1271_acx_group_address_tbl(struct wl1271 *wl, bool enable,
				 void *mc_list, u32 mc_list_len)
{
	struct acx_dot11_grp_addr_tbl *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx group address tbl");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	/* MAC filtering */
	acx->enabled = enable;
	acx->num_groups = mc_list_len;
	memcpy(acx->mac_table, mc_list, mc_list_len * ETH_ALEN);

	ret = wl1271_cmd_configure(wl, DOT11_GROUP_ADDRESS_TBL,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("failed to set group addr table: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_service_period_timeout(struct wl1271 *wl)
{
	struct acx_rx_timeout *rx_timeout;
	int ret;

	rx_timeout = kzalloc(sizeof(*rx_timeout), GFP_KERNEL);
	if (!rx_timeout) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_ACX, "acx service period timeout");

	rx_timeout->ps_poll_timeout = cpu_to_le16(wl->conf.rx.ps_poll_timeout);
	rx_timeout->upsd_timeout = cpu_to_le16(wl->conf.rx.upsd_timeout);

	ret = wl1271_cmd_configure(wl, ACX_SERVICE_PERIOD_TIMEOUT,
				   rx_timeout, sizeof(*rx_timeout));
	if (ret < 0) {
		wl1271_warning("failed to set service period timeout: %d",
			       ret);
		goto out;
	}

out:
	kfree(rx_timeout);
	return ret;
}

int wl1271_acx_rts_threshold(struct wl1271 *wl, u16 rts_threshold)
{
	struct acx_rts_threshold *rts;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx rts threshold");

	rts = kzalloc(sizeof(*rts), GFP_KERNEL);
	if (!rts) {
		ret = -ENOMEM;
		goto out;
	}

	rts->threshold = cpu_to_le16(rts_threshold);

	ret = wl1271_cmd_configure(wl, DOT11_RTS_THRESHOLD, rts, sizeof(*rts));
	if (ret < 0) {
		wl1271_warning("failed to set rts threshold: %d", ret);
		goto out;
	}

out:
	kfree(rts);
	return ret;
}

int wl1271_acx_dco_itrim_params(struct wl1271 *wl)
{
	struct acx_dco_itrim_params *dco;
	struct conf_itrim_settings *c = &wl->conf.itrim;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx dco itrim parameters");

	dco = kzalloc(sizeof(*dco), GFP_KERNEL);
	if (!dco) {
		ret = -ENOMEM;
		goto out;
	}

	dco->enable = c->enable;
	dco->timeout = cpu_to_le32(c->timeout);

	ret = wl1271_cmd_configure(wl, ACX_SET_DCO_ITRIM_PARAMS,
				   dco, sizeof(*dco));
	if (ret < 0) {
		wl1271_warning("failed to set dco itrim parameters: %d", ret);
		goto out;
	}

out:
	kfree(dco);
	return ret;
}

int wl1271_acx_beacon_filter_opt(struct wl1271 *wl, bool enable_filter)
{
	struct acx_beacon_filter_option *beacon_filter = NULL;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx beacon filter opt");

	if (enable_filter &&
	    wl->conf.conn.bcn_filt_mode == CONF_BCN_FILT_MODE_DISABLED)
		goto out;

	beacon_filter = kzalloc(sizeof(*beacon_filter), GFP_KERNEL);
	if (!beacon_filter) {
		ret = -ENOMEM;
		goto out;
	}

	beacon_filter->enable = enable_filter;

	/*
	 * When set to zero, and the filter is enabled, beacons
	 * without the unicast TIM bit set are dropped.
	 */
	beacon_filter->max_num_beacons = 0;

	ret = wl1271_cmd_configure(wl, ACX_BEACON_FILTER_OPT,
				   beacon_filter, sizeof(*beacon_filter));
	if (ret < 0) {
		wl1271_warning("failed to set beacon filter opt: %d", ret);
		goto out;
	}

out:
	kfree(beacon_filter);
	return ret;
}

int wl1271_acx_beacon_filter_table(struct wl1271 *wl)
{
	struct acx_beacon_filter_ie_table *ie_table;
	int i, idx = 0;
	int ret;
	bool vendor_spec = false;

	wl1271_debug(DEBUG_ACX, "acx beacon filter table");

	ie_table = kzalloc(sizeof(*ie_table), GFP_KERNEL);
	if (!ie_table) {
		ret = -ENOMEM;
		goto out;
	}

	/* configure default beacon pass-through rules */
	ie_table->num_ie = 0;
	for (i = 0; i < wl->conf.conn.bcn_filt_ie_count; i++) {
		struct conf_bcn_filt_rule *r = &(wl->conf.conn.bcn_filt_ie[i]);
		ie_table->table[idx++] = r->ie;
		ie_table->table[idx++] = r->rule;

		if (r->ie == WLAN_EID_VENDOR_SPECIFIC) {
			/* only one vendor specific ie allowed */
			if (vendor_spec)
				continue;

			/* for vendor specific rules configure the
			   additional fields */
			memcpy(&(ie_table->table[idx]), r->oui,
			       CONF_BCN_IE_OUI_LEN);
			idx += CONF_BCN_IE_OUI_LEN;
			ie_table->table[idx++] = r->type;
			memcpy(&(ie_table->table[idx]), r->version,
			       CONF_BCN_IE_VER_LEN);
			idx += CONF_BCN_IE_VER_LEN;
			vendor_spec = true;
		}

		ie_table->num_ie++;
	}

	ret = wl1271_cmd_configure(wl, ACX_BEACON_FILTER_TABLE,
				   ie_table, sizeof(*ie_table));
	if (ret < 0) {
		wl1271_warning("failed to set beacon filter table: %d", ret);
		goto out;
	}

out:
	kfree(ie_table);
	return ret;
}

int wl1271_acx_conn_monit_params(struct wl1271 *wl)
{
	struct acx_conn_monit_params *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx connection monitor parameters");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->synch_fail_thold = cpu_to_le32(wl->conf.conn.synch_fail_thold);
	acx->bss_lose_timeout = cpu_to_le32(wl->conf.conn.bss_lose_timeout);

	ret = wl1271_cmd_configure(wl, ACX_CONN_MONIT_PARAMS,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("failed to set connection monitor "
			       "parameters: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}


int wl1271_acx_sg_enable(struct wl1271 *wl)
{
	struct acx_bt_wlan_coex *pta;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx sg enable");

	pta = kzalloc(sizeof(*pta), GFP_KERNEL);
	if (!pta) {
		ret = -ENOMEM;
		goto out;
	}

	pta->enable = SG_ENABLE;

	ret = wl1271_cmd_configure(wl, ACX_SG_ENABLE, pta, sizeof(*pta));
	if (ret < 0) {
		wl1271_warning("failed to set softgemini enable: %d", ret);
		goto out;
	}

out:
	kfree(pta);
	return ret;
}

int wl1271_acx_sg_cfg(struct wl1271 *wl)
{
	struct acx_bt_wlan_coex_param *param;
	struct conf_sg_settings *c = &wl->conf.sg;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx sg cfg");

	param = kzalloc(sizeof(*param), GFP_KERNEL);
	if (!param) {
		ret = -ENOMEM;
		goto out;
	}

	/* BT-WLAN coext parameters */
	param->per_threshold = cpu_to_le32(c->per_threshold);
	param->max_scan_compensation_time =
		cpu_to_le32(c->max_scan_compensation_time);
	param->nfs_sample_interval = cpu_to_le16(c->nfs_sample_interval);
	param->load_ratio = c->load_ratio;
	param->auto_ps_mode = c->auto_ps_mode;
	param->probe_req_compensation = c->probe_req_compensation;
	param->scan_window_compensation = c->scan_window_compensation;
	param->antenna_config = c->antenna_config;
	param->beacon_miss_threshold = c->beacon_miss_threshold;
	param->rate_adaptation_threshold =
		cpu_to_le32(c->rate_adaptation_threshold);
	param->rate_adaptation_snr = c->rate_adaptation_snr;

	ret = wl1271_cmd_configure(wl, ACX_SG_CFG, param, sizeof(*param));
	if (ret < 0) {
		wl1271_warning("failed to set sg config: %d", ret);
		goto out;
	}

out:
	kfree(param);
	return ret;
}

int wl1271_acx_cca_threshold(struct wl1271 *wl)
{
	struct acx_energy_detection *detection;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx cca threshold");

	detection = kzalloc(sizeof(*detection), GFP_KERNEL);
	if (!detection) {
		ret = -ENOMEM;
		goto out;
	}

	detection->rx_cca_threshold = cpu_to_le16(wl->conf.rx.rx_cca_threshold);
	detection->tx_energy_detection = wl->conf.tx.tx_energy_detection;

	ret = wl1271_cmd_configure(wl, ACX_CCA_THRESHOLD,
				   detection, sizeof(*detection));
	if (ret < 0) {
		wl1271_warning("failed to set cca threshold: %d", ret);
		return ret;
	}

out:
	kfree(detection);
	return ret;
}

int wl1271_acx_bcn_dtim_options(struct wl1271 *wl)
{
	struct acx_beacon_broadcast *bb;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx bcn dtim options");

	bb = kzalloc(sizeof(*bb), GFP_KERNEL);
	if (!bb) {
		ret = -ENOMEM;
		goto out;
	}

	bb->beacon_rx_timeout = cpu_to_le16(wl->conf.conn.beacon_rx_timeout);
	bb->broadcast_timeout = cpu_to_le16(wl->conf.conn.broadcast_timeout);
	bb->rx_broadcast_in_ps = wl->conf.conn.rx_broadcast_in_ps;
	bb->ps_poll_threshold = wl->conf.conn.ps_poll_threshold;

	ret = wl1271_cmd_configure(wl, ACX_BCN_DTIM_OPTIONS, bb, sizeof(*bb));
	if (ret < 0) {
		wl1271_warning("failed to set rx config: %d", ret);
		goto out;
	}

out:
	kfree(bb);
	return ret;
}

int wl1271_acx_aid(struct wl1271 *wl, u16 aid)
{
	struct acx_aid *acx_aid;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx aid");

	acx_aid = kzalloc(sizeof(*acx_aid), GFP_KERNEL);
	if (!acx_aid) {
		ret = -ENOMEM;
		goto out;
	}

	acx_aid->aid = cpu_to_le16(aid);

	ret = wl1271_cmd_configure(wl, ACX_AID, acx_aid, sizeof(*acx_aid));
	if (ret < 0) {
		wl1271_warning("failed to set aid: %d", ret);
		goto out;
	}

out:
	kfree(acx_aid);
	return ret;
}

int wl1271_acx_event_mbox_mask(struct wl1271 *wl, u32 event_mask)
{
	struct acx_event_mask *mask;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx event mbox mask");

	mask = kzalloc(sizeof(*mask), GFP_KERNEL);
	if (!mask) {
		ret = -ENOMEM;
		goto out;
	}

	/* high event mask is unused */
	mask->high_event_mask = cpu_to_le32(0xffffffff);
	mask->event_mask = cpu_to_le32(event_mask);

	ret = wl1271_cmd_configure(wl, ACX_EVENT_MBOX_MASK,
				   mask, sizeof(*mask));
	if (ret < 0) {
		wl1271_warning("failed to set acx_event_mbox_mask: %d", ret);
		goto out;
	}

out:
	kfree(mask);
	return ret;
}

int wl1271_acx_set_preamble(struct wl1271 *wl, enum acx_preamble_type preamble)
{
	struct acx_preamble *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx_set_preamble");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->preamble = preamble;

	ret = wl1271_cmd_configure(wl, ACX_PREAMBLE_TYPE, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("Setting of preamble failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_cts_protect(struct wl1271 *wl,
			   enum acx_ctsprotect_type ctsprotect)
{
	struct acx_ctsprotect *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx_set_ctsprotect");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->ctsprotect = ctsprotect;

	ret = wl1271_cmd_configure(wl, ACX_CTS_PROTECTION, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("Setting of ctsprotect failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_statistics(struct wl1271 *wl, struct acx_statistics *stats)
{
	int ret;

	wl1271_debug(DEBUG_ACX, "acx statistics");

	ret = wl1271_cmd_interrogate(wl, ACX_STATISTICS, stats,
				     sizeof(*stats));
	if (ret < 0) {
		wl1271_warning("acx statistics failed: %d", ret);
		return -ENOMEM;
	}

	return 0;
}

int wl1271_acx_rate_policies(struct wl1271 *wl)
{
	struct acx_rate_policy *acx;
	struct conf_tx_rate_class *c = &wl->conf.tx.rc_conf;
	int idx = 0;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx rate policies");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);

	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	/* configure one basic rate class */
	idx = ACX_TX_BASIC_RATE;
	acx->rate_class[idx].enabled_rates = cpu_to_le32(wl->basic_rate_set);
	acx->rate_class[idx].short_retry_limit = c->short_retry_limit;
	acx->rate_class[idx].long_retry_limit = c->long_retry_limit;
	acx->rate_class[idx].aflags = c->aflags;

	/* configure one AP supported rate class */
	idx = ACX_TX_AP_FULL_RATE;
	acx->rate_class[idx].enabled_rates = cpu_to_le32(wl->rate_set);
	acx->rate_class[idx].short_retry_limit = c->short_retry_limit;
	acx->rate_class[idx].long_retry_limit = c->long_retry_limit;
	acx->rate_class[idx].aflags = c->aflags;

	acx->rate_class_cnt = cpu_to_le32(ACX_TX_RATE_POLICY_CNT);

	ret = wl1271_cmd_configure(wl, ACX_RATE_POLICY, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("Setting of rate policies failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_ac_cfg(struct wl1271 *wl, u8 ac, u8 cw_min, u16 cw_max,
		      u8 aifsn, u16 txop)
{
	struct acx_ac_cfg *acx;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx ac cfg %d cw_ming %d cw_max %d "
		     "aifs %d txop %d", ac, cw_min, cw_max, aifsn, txop);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);

	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->ac = ac;
	acx->cw_min = cw_min;
	acx->cw_max = cpu_to_le16(cw_max);
	acx->aifsn = aifsn;
	acx->tx_op_limit = cpu_to_le16(txop);

	ret = wl1271_cmd_configure(wl, ACX_AC_CFG, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx ac cfg failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_tid_cfg(struct wl1271 *wl, u8 queue_id, u8 channel_type,
		       u8 tsid, u8 ps_scheme, u8 ack_policy,
		       u32 apsd_conf0, u32 apsd_conf1)
{
	struct acx_tid_config *acx;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx tid config");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);

	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->queue_id = queue_id;
	acx->channel_type = channel_type;
	acx->tsid = tsid;
	acx->ps_scheme = ps_scheme;
	acx->ack_policy = ack_policy;
	acx->apsd_conf[0] = cpu_to_le32(apsd_conf0);
	acx->apsd_conf[1] = cpu_to_le32(apsd_conf1);

	ret = wl1271_cmd_configure(wl, ACX_TID_CFG, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("Setting of tid config failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_frag_threshold(struct wl1271 *wl)
{
	struct acx_frag_threshold *acx;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx frag threshold");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);

	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->frag_threshold = cpu_to_le16(wl->conf.tx.frag_threshold);
	ret = wl1271_cmd_configure(wl, ACX_FRAG_CFG, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("Setting of frag threshold failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_tx_config_options(struct wl1271 *wl)
{
	struct acx_tx_config_options *acx;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx tx config options");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);

	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->tx_compl_timeout = cpu_to_le16(wl->conf.tx.tx_compl_timeout);
	acx->tx_compl_threshold = cpu_to_le16(wl->conf.tx.tx_compl_threshold);
	ret = wl1271_cmd_configure(wl, ACX_TX_CONFIG_OPT, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("Setting of tx options failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_mem_cfg(struct wl1271 *wl)
{
	struct wl1271_acx_config_memory *mem_conf;
	int ret;

	wl1271_debug(DEBUG_ACX, "wl1271 mem cfg");

	mem_conf = kzalloc(sizeof(*mem_conf), GFP_KERNEL);
	if (!mem_conf) {
		ret = -ENOMEM;
		goto out;
	}

	/* memory config */
	mem_conf->num_stations = DEFAULT_NUM_STATIONS;
	mem_conf->rx_mem_block_num = ACX_RX_MEM_BLOCKS;
	mem_conf->tx_min_mem_block_num = ACX_TX_MIN_MEM_BLOCKS;
	mem_conf->num_ssid_profiles = ACX_NUM_SSID_PROFILES;
	mem_conf->total_tx_descriptors = cpu_to_le32(ACX_TX_DESCRIPTORS);

	ret = wl1271_cmd_configure(wl, ACX_MEM_CFG, mem_conf,
				   sizeof(*mem_conf));
	if (ret < 0) {
		wl1271_warning("wl1271 mem config failed: %d", ret);
		goto out;
	}

out:
	kfree(mem_conf);
	return ret;
}

int wl1271_acx_init_mem_config(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_acx_mem_cfg(wl);
	if (ret < 0)
		return ret;

	wl->target_mem_map = kzalloc(sizeof(struct wl1271_acx_mem_map),
				     GFP_KERNEL);
	if (!wl->target_mem_map) {
		wl1271_error("couldn't allocate target memory map");
		return -ENOMEM;
	}

	/* we now ask for the firmware built memory map */
	ret = wl1271_acx_mem_map(wl, (void *)wl->target_mem_map,
				 sizeof(struct wl1271_acx_mem_map));
	if (ret < 0) {
		wl1271_error("couldn't retrieve firmware memory map");
		kfree(wl->target_mem_map);
		wl->target_mem_map = NULL;
		return ret;
	}

	/* initialize TX block book keeping */
	wl->tx_blocks_available =
		le32_to_cpu(wl->target_mem_map->num_tx_mem_blocks);
	wl1271_debug(DEBUG_TX, "available tx blocks: %d",
		     wl->tx_blocks_available);

	return 0;
}

int wl1271_acx_init_rx_interrupt(struct wl1271 *wl)
{
	struct wl1271_acx_rx_config_opt *rx_conf;
	int ret;

	wl1271_debug(DEBUG_ACX, "wl1271 rx interrupt config");

	rx_conf = kzalloc(sizeof(*rx_conf), GFP_KERNEL);
	if (!rx_conf) {
		ret = -ENOMEM;
		goto out;
	}

	rx_conf->threshold = cpu_to_le16(wl->conf.rx.irq_pkt_threshold);
	rx_conf->timeout = cpu_to_le16(wl->conf.rx.irq_timeout);
	rx_conf->mblk_threshold = cpu_to_le16(wl->conf.rx.irq_blk_threshold);
	rx_conf->queue_type = wl->conf.rx.queue_type;

	ret = wl1271_cmd_configure(wl, ACX_RX_CONFIG_OPT, rx_conf,
				   sizeof(*rx_conf));
	if (ret < 0) {
		wl1271_warning("wl1271 rx config opt failed: %d", ret);
		goto out;
	}

out:
	kfree(rx_conf);
	return ret;
}

int wl1271_acx_bet_enable(struct wl1271 *wl, bool enable)
{
	struct wl1271_acx_bet_enable *acx = NULL;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx bet enable");

	if (enable && wl->conf.conn.bet_enable == CONF_BET_MODE_DISABLE)
		goto out;

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->enable = enable ? CONF_BET_MODE_ENABLE : CONF_BET_MODE_DISABLE;
	acx->max_consecutive = wl->conf.conn.bet_max_consecutive;

	ret = wl1271_cmd_configure(wl, ACX_BET_ENABLE, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx bet enable failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_arp_ip_filter(struct wl1271 *wl, bool enable, u8 *address,
			     u8 version)
{
	struct wl1271_acx_arp_filter *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx arp ip filter, enable: %d", enable);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->version = version;
	acx->enable = enable;

	if (enable == true) {
		if (version == ACX_IPV4_VERSION)
			memcpy(acx->address, address, ACX_IPV4_ADDR_SIZE);
		else if (version == ACX_IPV6_VERSION)
			memcpy(acx->address, address, sizeof(acx->address));
		else
			wl1271_error("Invalid IP version");
	}

	ret = wl1271_cmd_configure(wl, ACX_ARP_IP_FILTER,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("failed to set arp ip filter: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_pm_config(struct wl1271 *wl)
{
	struct wl1271_acx_pm_config *acx = NULL;
	struct  conf_pm_config_settings *c = &wl->conf.pm_config;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx pm config");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->host_clk_settling_time = cpu_to_le32(c->host_clk_settling_time);
	acx->host_fast_wakeup_support = c->host_fast_wakeup_support;

	ret = wl1271_cmd_configure(wl, ACX_PM_CONFIG, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx pm config failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}
