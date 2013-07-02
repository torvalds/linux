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

#include "acx.h"

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>

#include "wlcore.h"
#include "debug.h"
#include "wl12xx_80211.h"
#include "ps.h"
#include "hw_ops.h"

int wl1271_acx_wake_up_conditions(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				  u8 wake_up_event, u8 listen_interval)
{
	struct acx_wake_up_condition *wake_up;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx wake up conditions (wake_up_event %d listen_interval %d)",
		     wake_up_event, listen_interval);

	wake_up = kzalloc(sizeof(*wake_up), GFP_KERNEL);
	if (!wake_up) {
		ret = -ENOMEM;
		goto out;
	}

	wake_up->role_id = wlvif->role_id;
	wake_up->wake_up_event = wake_up_event;
	wake_up->listen_interval = listen_interval;

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

	wl1271_debug(DEBUG_ACX, "acx sleep auth %d", sleep_auth);

	auth = kzalloc(sizeof(*auth), GFP_KERNEL);
	if (!auth) {
		ret = -ENOMEM;
		goto out;
	}

	auth->sleep_auth = sleep_auth;

	ret = wl1271_cmd_configure(wl, ACX_SLEEP_AUTH, auth, sizeof(*auth));
	if (ret < 0) {
		wl1271_error("could not configure sleep_auth to %d: %d",
			     sleep_auth, ret);
		goto out;
	}

	wl->sleep_auth = sleep_auth;
out:
	kfree(auth);
	return ret;
}
EXPORT_SYMBOL_GPL(wl1271_acx_sleep_auth);

int wl1271_acx_tx_power(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			int power)
{
	struct acx_current_tx_power *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx dot11_cur_tx_pwr %d", power);

	if (power < 0 || power > 25)
		return -EINVAL;

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->role_id = wlvif->role_id;
	acx->current_tx_power = power * 10;

	ret = wl1271_cmd_configure(wl, DOT11_CUR_TX_PWR, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("configure of tx power failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_feature_cfg(struct wl1271 *wl, struct wl12xx_vif *wlvif)
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
	feature->role_id = wlvif->role_id;
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

int wl1271_acx_slot(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		    enum acx_slot_type slot_time)
{
	struct acx_slot *slot;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx slot");

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot) {
		ret = -ENOMEM;
		goto out;
	}

	slot->role_id = wlvif->role_id;
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

int wl1271_acx_group_address_tbl(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				 bool enable, void *mc_list, u32 mc_list_len)
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
	acx->role_id = wlvif->role_id;
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

int wl1271_acx_service_period_timeout(struct wl1271 *wl,
				      struct wl12xx_vif *wlvif)
{
	struct acx_rx_timeout *rx_timeout;
	int ret;

	rx_timeout = kzalloc(sizeof(*rx_timeout), GFP_KERNEL);
	if (!rx_timeout) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_ACX, "acx service period timeout");

	rx_timeout->role_id = wlvif->role_id;
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

int wl1271_acx_rts_threshold(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			     u32 rts_threshold)
{
	struct acx_rts_threshold *rts;
	int ret;

	/*
	 * If the RTS threshold is not configured or out of range, use the
	 * default value.
	 */
	if (rts_threshold > IEEE80211_MAX_RTS_THRESHOLD)
		rts_threshold = wl->conf.rx.rts_threshold;

	wl1271_debug(DEBUG_ACX, "acx rts threshold: %d", rts_threshold);

	rts = kzalloc(sizeof(*rts), GFP_KERNEL);
	if (!rts) {
		ret = -ENOMEM;
		goto out;
	}

	rts->role_id = wlvif->role_id;
	rts->threshold = cpu_to_le16((u16)rts_threshold);

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

int wl1271_acx_beacon_filter_opt(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				 bool enable_filter)
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

	beacon_filter->role_id = wlvif->role_id;
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

int wl1271_acx_beacon_filter_table(struct wl1271 *wl,
				   struct wl12xx_vif *wlvif)
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
	ie_table->role_id = wlvif->role_id;
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

#define ACX_CONN_MONIT_DISABLE_VALUE  0xffffffff

int wl1271_acx_conn_monit_params(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				 bool enable)
{
	struct acx_conn_monit_params *acx;
	u32 threshold = ACX_CONN_MONIT_DISABLE_VALUE;
	u32 timeout = ACX_CONN_MONIT_DISABLE_VALUE;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx connection monitor parameters: %s",
		     enable ? "enabled" : "disabled");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	if (enable) {
		threshold = wl->conf.conn.synch_fail_thold;
		timeout = wl->conf.conn.bss_lose_timeout;
	}

	acx->role_id = wlvif->role_id;
	acx->synch_fail_thold = cpu_to_le32(threshold);
	acx->bss_lose_timeout = cpu_to_le32(timeout);

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


int wl1271_acx_sg_enable(struct wl1271 *wl, bool enable)
{
	struct acx_bt_wlan_coex *pta;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx sg enable");

	pta = kzalloc(sizeof(*pta), GFP_KERNEL);
	if (!pta) {
		ret = -ENOMEM;
		goto out;
	}

	if (enable)
		pta->enable = wl->conf.sg.state;
	else
		pta->enable = CONF_SG_DISABLE;

	ret = wl1271_cmd_configure(wl, ACX_SG_ENABLE, pta, sizeof(*pta));
	if (ret < 0) {
		wl1271_warning("failed to set softgemini enable: %d", ret);
		goto out;
	}

out:
	kfree(pta);
	return ret;
}

int wl12xx_acx_sg_cfg(struct wl1271 *wl)
{
	struct acx_bt_wlan_coex_param *param;
	struct conf_sg_settings *c = &wl->conf.sg;
	int i, ret;

	wl1271_debug(DEBUG_ACX, "acx sg cfg");

	param = kzalloc(sizeof(*param), GFP_KERNEL);
	if (!param) {
		ret = -ENOMEM;
		goto out;
	}

	/* BT-WLAN coext parameters */
	for (i = 0; i < CONF_SG_PARAMS_MAX; i++)
		param->params[i] = cpu_to_le32(c->params[i]);
	param->param_idx = CONF_SG_PARAMS_ALL;

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
	if (ret < 0)
		wl1271_warning("failed to set cca threshold: %d", ret);

out:
	kfree(detection);
	return ret;
}

int wl1271_acx_bcn_dtim_options(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct acx_beacon_broadcast *bb;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx bcn dtim options");

	bb = kzalloc(sizeof(*bb), GFP_KERNEL);
	if (!bb) {
		ret = -ENOMEM;
		goto out;
	}

	bb->role_id = wlvif->role_id;
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

int wl1271_acx_aid(struct wl1271 *wl, struct wl12xx_vif *wlvif, u16 aid)
{
	struct acx_aid *acx_aid;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx aid");

	acx_aid = kzalloc(sizeof(*acx_aid), GFP_KERNEL);
	if (!acx_aid) {
		ret = -ENOMEM;
		goto out;
	}

	acx_aid->role_id = wlvif->role_id;
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

int wl1271_acx_set_preamble(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			    enum acx_preamble_type preamble)
{
	struct acx_preamble *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx_set_preamble");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->role_id = wlvif->role_id;
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

int wl1271_acx_cts_protect(struct wl1271 *wl, struct wl12xx_vif *wlvif,
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

	acx->role_id = wlvif->role_id;
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

int wl1271_acx_statistics(struct wl1271 *wl, void *stats)
{
	int ret;

	wl1271_debug(DEBUG_ACX, "acx statistics");

	ret = wl1271_cmd_interrogate(wl, ACX_STATISTICS, stats,
				     wl->stats.fw_stats_len);
	if (ret < 0) {
		wl1271_warning("acx statistics failed: %d", ret);
		return -ENOMEM;
	}

	return 0;
}

int wl1271_acx_sta_rate_policies(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct acx_rate_policy *acx;
	struct conf_tx_rate_class *c = &wl->conf.tx.sta_rc_conf;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx rate policies");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);

	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_ACX, "basic_rate: 0x%x, full_rate: 0x%x",
		wlvif->basic_rate, wlvif->rate_set);

	/* configure one basic rate class */
	acx->rate_policy_idx = cpu_to_le32(wlvif->sta.basic_rate_idx);
	acx->rate_policy.enabled_rates = cpu_to_le32(wlvif->basic_rate);
	acx->rate_policy.short_retry_limit = c->short_retry_limit;
	acx->rate_policy.long_retry_limit = c->long_retry_limit;
	acx->rate_policy.aflags = c->aflags;

	ret = wl1271_cmd_configure(wl, ACX_RATE_POLICY, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("Setting of rate policies failed: %d", ret);
		goto out;
	}

	/* configure one AP supported rate class */
	acx->rate_policy_idx = cpu_to_le32(wlvif->sta.ap_rate_idx);

	/* the AP policy is HW specific */
	acx->rate_policy.enabled_rates =
		cpu_to_le32(wlcore_hw_sta_get_ap_rate_mask(wl, wlvif));
	acx->rate_policy.short_retry_limit = c->short_retry_limit;
	acx->rate_policy.long_retry_limit = c->long_retry_limit;
	acx->rate_policy.aflags = c->aflags;

	ret = wl1271_cmd_configure(wl, ACX_RATE_POLICY, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("Setting of rate policies failed: %d", ret);
		goto out;
	}

	/*
	 * configure one rate class for basic p2p operations.
	 * (p2p packets should always go out with OFDM rates, even
	 * if we are currently connected to 11b AP)
	 */
	acx->rate_policy_idx = cpu_to_le32(wlvif->sta.p2p_rate_idx);
	acx->rate_policy.enabled_rates =
				cpu_to_le32(CONF_TX_RATE_MASK_BASIC_P2P);
	acx->rate_policy.short_retry_limit = c->short_retry_limit;
	acx->rate_policy.long_retry_limit = c->long_retry_limit;
	acx->rate_policy.aflags = c->aflags;

	ret = wl1271_cmd_configure(wl, ACX_RATE_POLICY, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("Setting of rate policies failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_ap_rate_policy(struct wl1271 *wl, struct conf_tx_rate_class *c,
		      u8 idx)
{
	struct acx_rate_policy *acx;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx ap rate policy %d rates 0x%x",
		     idx, c->enabled_rates);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->rate_policy.enabled_rates = cpu_to_le32(c->enabled_rates);
	acx->rate_policy.short_retry_limit = c->short_retry_limit;
	acx->rate_policy.long_retry_limit = c->long_retry_limit;
	acx->rate_policy.aflags = c->aflags;

	acx->rate_policy_idx = cpu_to_le32(idx);

	ret = wl1271_cmd_configure(wl, ACX_RATE_POLICY, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("Setting of ap rate policy failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_ac_cfg(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		      u8 ac, u8 cw_min, u16 cw_max, u8 aifsn, u16 txop)
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

	acx->role_id = wlvif->role_id;
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

int wl1271_acx_tid_cfg(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		       u8 queue_id, u8 channel_type,
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

	acx->role_id = wlvif->role_id;
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

int wl1271_acx_frag_threshold(struct wl1271 *wl, u32 frag_threshold)
{
	struct acx_frag_threshold *acx;
	int ret = 0;

	/*
	 * If the fragmentation is not configured or out of range, use the
	 * default value.
	 */
	if (frag_threshold > IEEE80211_MAX_FRAG_THRESHOLD)
		frag_threshold = wl->conf.tx.frag_threshold;

	wl1271_debug(DEBUG_ACX, "acx frag threshold: %d", frag_threshold);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);

	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->frag_threshold = cpu_to_le16((u16)frag_threshold);
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

int wl12xx_acx_mem_cfg(struct wl1271 *wl)
{
	struct wl12xx_acx_config_memory *mem_conf;
	struct conf_memory_settings *mem;
	int ret;

	wl1271_debug(DEBUG_ACX, "wl1271 mem cfg");

	mem_conf = kzalloc(sizeof(*mem_conf), GFP_KERNEL);
	if (!mem_conf) {
		ret = -ENOMEM;
		goto out;
	}

	mem = &wl->conf.mem;

	/* memory config */
	mem_conf->num_stations = mem->num_stations;
	mem_conf->rx_mem_block_num = mem->rx_block_num;
	mem_conf->tx_min_mem_block_num = mem->tx_min_block_num;
	mem_conf->num_ssid_profiles = mem->ssid_profiles;
	mem_conf->total_tx_descriptors = cpu_to_le32(wl->num_tx_desc);
	mem_conf->dyn_mem_enable = mem->dynamic_memory;
	mem_conf->tx_free_req = mem->min_req_tx_blocks;
	mem_conf->rx_free_req = mem->min_req_rx_blocks;
	mem_conf->tx_min = mem->tx_min;
	mem_conf->fwlog_blocks = wl->conf.fwlog.mem_blocks;

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
EXPORT_SYMBOL_GPL(wl12xx_acx_mem_cfg);

int wl1271_acx_init_mem_config(struct wl1271 *wl)
{
	int ret;

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
EXPORT_SYMBOL_GPL(wl1271_acx_init_mem_config);

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

int wl1271_acx_bet_enable(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			  bool enable)
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

	acx->role_id = wlvif->role_id;
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

int wl1271_acx_arp_ip_filter(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			     u8 enable, __be32 address)
{
	struct wl1271_acx_arp_filter *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx arp ip filter, enable: %d", enable);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->role_id = wlvif->role_id;
	acx->version = ACX_IPV4_VERSION;
	acx->enable = enable;

	if (enable)
		memcpy(acx->address, &address, ACX_IPV4_ADDR_SIZE);

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
EXPORT_SYMBOL_GPL(wl1271_acx_pm_config);

int wl1271_acx_keep_alive_mode(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			       bool enable)
{
	struct wl1271_acx_keep_alive_mode *acx = NULL;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx keep alive mode: %d", enable);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->role_id = wlvif->role_id;
	acx->enabled = enable;

	ret = wl1271_cmd_configure(wl, ACX_KEEP_ALIVE_MODE, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx keep alive mode failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_keep_alive_config(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				 u8 index, u8 tpl_valid)
{
	struct wl1271_acx_keep_alive_config *acx = NULL;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx keep alive config");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->role_id = wlvif->role_id;
	acx->period = cpu_to_le32(wl->conf.conn.keep_alive_interval);
	acx->index = index;
	acx->tpl_validation = tpl_valid;
	acx->trigger = ACX_KEEP_ALIVE_NO_TX;

	ret = wl1271_cmd_configure(wl, ACX_SET_KEEP_ALIVE_CONFIG,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx keep alive config failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_rssi_snr_trigger(struct wl1271 *wl, struct wl12xx_vif *wlvif,
				bool enable, s16 thold, u8 hyst)
{
	struct wl1271_acx_rssi_snr_trigger *acx = NULL;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx rssi snr trigger");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	wlvif->last_rssi_event = -1;

	acx->role_id = wlvif->role_id;
	acx->pacing = cpu_to_le16(wl->conf.roam_trigger.trigger_pacing);
	acx->metric = WL1271_ACX_TRIG_METRIC_RSSI_BEACON;
	acx->type = WL1271_ACX_TRIG_TYPE_EDGE;
	if (enable)
		acx->enable = WL1271_ACX_TRIG_ENABLE;
	else
		acx->enable = WL1271_ACX_TRIG_DISABLE;

	acx->index = WL1271_ACX_TRIG_IDX_RSSI;
	acx->dir = WL1271_ACX_TRIG_DIR_BIDIR;
	acx->threshold = cpu_to_le16(thold);
	acx->hysteresis = hyst;

	ret = wl1271_cmd_configure(wl, ACX_RSSI_SNR_TRIGGER, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx rssi snr trigger setting failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_rssi_snr_avg_weights(struct wl1271 *wl,
				    struct wl12xx_vif *wlvif)
{
	struct wl1271_acx_rssi_snr_avg_weights *acx = NULL;
	struct conf_roam_trigger_settings *c = &wl->conf.roam_trigger;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx rssi snr avg weights");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->role_id = wlvif->role_id;
	acx->rssi_beacon = c->avg_weight_rssi_beacon;
	acx->rssi_data = c->avg_weight_rssi_data;
	acx->snr_beacon = c->avg_weight_snr_beacon;
	acx->snr_data = c->avg_weight_snr_data;

	ret = wl1271_cmd_configure(wl, ACX_RSSI_SNR_WEIGHTS, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx rssi snr trigger weights failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_set_ht_capabilities(struct wl1271 *wl,
				    struct ieee80211_sta_ht_cap *ht_cap,
				    bool allow_ht_operation, u8 hlid)
{
	struct wl1271_acx_ht_capabilities *acx;
	int ret = 0;
	u32 ht_capabilites = 0;

	wl1271_debug(DEBUG_ACX, "acx ht capabilities setting "
		     "sta supp: %d sta cap: %d", ht_cap->ht_supported,
		     ht_cap->cap);

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

	ret = wl1271_cmd_configure(wl, ACX_PEER_HT_CAP, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx ht capabilities setting failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}
EXPORT_SYMBOL_GPL(wl1271_acx_set_ht_capabilities);


int wl1271_acx_set_ht_information(struct wl1271 *wl,
				   struct wl12xx_vif *wlvif,
				   u16 ht_operation_mode)
{
	struct wl1271_acx_ht_information *acx;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx ht information setting");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->role_id = wlvif->role_id;
	acx->ht_protection =
		(u8)(ht_operation_mode & IEEE80211_HT_OP_MODE_PROTECTION);
	acx->rifs_mode = 0;
	acx->gf_protection =
		!!(ht_operation_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
	acx->ht_tx_burst_limit = 0;
	acx->dual_cts_protection = 0;

	ret = wl1271_cmd_configure(wl, ACX_HT_BSS_OPERATION, acx, sizeof(*acx));

	if (ret < 0) {
		wl1271_warning("acx ht information setting failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

/* Configure BA session initiator/receiver parameters setting in the FW. */
int wl12xx_acx_set_ba_initiator_policy(struct wl1271 *wl,
				       struct wl12xx_vif *wlvif)
{
	struct wl1271_acx_ba_initiator_policy *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx ba initiator policy");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	/* set for the current role */
	acx->role_id = wlvif->role_id;
	acx->tid_bitmap = wl->conf.ht.tx_ba_tid_bitmap;
	acx->win_size = wl->conf.ht.tx_ba_win_size;
	acx->inactivity_timeout = wl->conf.ht.inactivity_timeout;

	ret = wl1271_cmd_configure(wl,
				   ACX_BA_SESSION_INIT_POLICY,
				   acx,
				   sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx ba initiator policy failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

/* setup BA session receiver setting in the FW. */
int wl12xx_acx_set_ba_receiver_session(struct wl1271 *wl, u8 tid_index,
				       u16 ssn, bool enable, u8 peer_hlid)
{
	struct wl1271_acx_ba_receiver_setup *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx ba receiver session setting");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->hlid = peer_hlid;
	acx->tid = tid_index;
	acx->enable = enable;
	acx->win_size = wl->conf.ht.rx_ba_win_size;
	acx->ssn = ssn;

	ret = wlcore_cmd_configure_failsafe(wl, ACX_BA_SESSION_RX_SETUP, acx,
					    sizeof(*acx),
					    BIT(CMD_STATUS_NO_RX_BA_SESSION));
	if (ret < 0) {
		wl1271_warning("acx ba receiver session failed: %d", ret);
		goto out;
	}

	/* sometimes we can't start the session */
	if (ret == CMD_STATUS_NO_RX_BA_SESSION) {
		wl1271_warning("no fw rx ba on tid %d", tid_index);
		ret = -EBUSY;
		goto out;
	}

	ret = 0;
out:
	kfree(acx);
	return ret;
}

int wl12xx_acx_tsf_info(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			u64 *mactime)
{
	struct wl12xx_acx_fw_tsf_information *tsf_info;
	int ret;

	tsf_info = kzalloc(sizeof(*tsf_info), GFP_KERNEL);
	if (!tsf_info) {
		ret = -ENOMEM;
		goto out;
	}

	tsf_info->role_id = wlvif->role_id;

	ret = wl1271_cmd_interrogate(wl, ACX_TSF_INFO,
				     tsf_info, sizeof(*tsf_info));
	if (ret < 0) {
		wl1271_warning("acx tsf info interrogate failed");
		goto out;
	}

	*mactime = le32_to_cpu(tsf_info->current_tsf_low) |
		((u64) le32_to_cpu(tsf_info->current_tsf_high) << 32);

out:
	kfree(tsf_info);
	return ret;
}

int wl1271_acx_ps_rx_streaming(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			       bool enable)
{
	struct wl1271_acx_ps_rx_streaming *rx_streaming;
	u32 conf_queues, enable_queues;
	int i, ret = 0;

	wl1271_debug(DEBUG_ACX, "acx ps rx streaming");

	rx_streaming = kzalloc(sizeof(*rx_streaming), GFP_KERNEL);
	if (!rx_streaming) {
		ret = -ENOMEM;
		goto out;
	}

	conf_queues = wl->conf.rx_streaming.queues;
	if (enable)
		enable_queues = conf_queues;
	else
		enable_queues = 0;

	for (i = 0; i < 8; i++) {
		/*
		 * Skip non-changed queues, to avoid redundant acxs.
		 * this check assumes conf.rx_streaming.queues can't
		 * be changed while rx_streaming is enabled.
		 */
		if (!(conf_queues & BIT(i)))
			continue;

		rx_streaming->role_id = wlvif->role_id;
		rx_streaming->tid = i;
		rx_streaming->enable = enable_queues & BIT(i);
		rx_streaming->period = wl->conf.rx_streaming.interval;
		rx_streaming->timeout = wl->conf.rx_streaming.interval;

		ret = wl1271_cmd_configure(wl, ACX_PS_RX_STREAMING,
					   rx_streaming,
					   sizeof(*rx_streaming));
		if (ret < 0) {
			wl1271_warning("acx ps rx streaming failed: %d", ret);
			goto out;
		}
	}
out:
	kfree(rx_streaming);
	return ret;
}

int wl1271_acx_ap_max_tx_retry(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct wl1271_acx_ap_max_tx_retry *acx = NULL;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx ap max tx retry");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	acx->role_id = wlvif->role_id;
	acx->max_tx_retry = cpu_to_le16(wl->conf.tx.max_tx_retries);

	ret = wl1271_cmd_configure(wl, ACX_MAX_TX_FAILURE, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx ap max tx retry failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl12xx_acx_config_ps(struct wl1271 *wl, struct wl12xx_vif *wlvif)
{
	struct wl1271_acx_config_ps *config_ps;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx config ps");

	config_ps = kzalloc(sizeof(*config_ps), GFP_KERNEL);
	if (!config_ps) {
		ret = -ENOMEM;
		goto out;
	}

	config_ps->exit_retries = wl->conf.conn.psm_exit_retries;
	config_ps->enter_retries = wl->conf.conn.psm_entry_retries;
	config_ps->null_data_rate = cpu_to_le32(wlvif->basic_rate);

	ret = wl1271_cmd_configure(wl, ACX_CONFIG_PS, config_ps,
				   sizeof(*config_ps));

	if (ret < 0) {
		wl1271_warning("acx config ps failed: %d", ret);
		goto out;
	}

out:
	kfree(config_ps);
	return ret;
}

int wl1271_acx_set_inconnection_sta(struct wl1271 *wl, u8 *addr)
{
	struct wl1271_acx_inconnection_sta *acx = NULL;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx set inconnaction sta %pM", addr);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	memcpy(acx->addr, addr, ETH_ALEN);

	ret = wl1271_cmd_configure(wl, ACX_UPDATE_INCONNECTION_STA_LIST,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx set inconnaction sta failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1271_acx_fm_coex(struct wl1271 *wl)
{
	struct wl1271_acx_fm_coex *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx fm coex setting");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->enable = wl->conf.fm_coex.enable;
	acx->swallow_period = wl->conf.fm_coex.swallow_period;
	acx->n_divider_fref_set_1 = wl->conf.fm_coex.n_divider_fref_set_1;
	acx->n_divider_fref_set_2 = wl->conf.fm_coex.n_divider_fref_set_2;
	acx->m_divider_fref_set_1 =
		cpu_to_le16(wl->conf.fm_coex.m_divider_fref_set_1);
	acx->m_divider_fref_set_2 =
		cpu_to_le16(wl->conf.fm_coex.m_divider_fref_set_2);
	acx->coex_pll_stabilization_time =
		cpu_to_le32(wl->conf.fm_coex.coex_pll_stabilization_time);
	acx->ldo_stabilization_time =
		cpu_to_le16(wl->conf.fm_coex.ldo_stabilization_time);
	acx->fm_disturbed_band_margin =
		wl->conf.fm_coex.fm_disturbed_band_margin;
	acx->swallow_clk_diff = wl->conf.fm_coex.swallow_clk_diff;

	ret = wl1271_cmd_configure(wl, ACX_FM_COEX_CFG, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx fm coex setting failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl12xx_acx_set_rate_mgmt_params(struct wl1271 *wl)
{
	struct wl12xx_acx_set_rate_mgmt_params *acx = NULL;
	struct conf_rate_policy_settings *conf = &wl->conf.rate;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx set rate mgmt params");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	acx->index = ACX_RATE_MGMT_ALL_PARAMS;
	acx->rate_retry_score = cpu_to_le16(conf->rate_retry_score);
	acx->per_add = cpu_to_le16(conf->per_add);
	acx->per_th1 = cpu_to_le16(conf->per_th1);
	acx->per_th2 = cpu_to_le16(conf->per_th2);
	acx->max_per = cpu_to_le16(conf->max_per);
	acx->inverse_curiosity_factor = conf->inverse_curiosity_factor;
	acx->tx_fail_low_th = conf->tx_fail_low_th;
	acx->tx_fail_high_th = conf->tx_fail_high_th;
	acx->per_alpha_shift = conf->per_alpha_shift;
	acx->per_add_shift = conf->per_add_shift;
	acx->per_beta1_shift = conf->per_beta1_shift;
	acx->per_beta2_shift = conf->per_beta2_shift;
	acx->rate_check_up = conf->rate_check_up;
	acx->rate_check_down = conf->rate_check_down;
	memcpy(acx->rate_retry_policy, conf->rate_retry_policy,
	       sizeof(acx->rate_retry_policy));

	ret = wl1271_cmd_configure(wl, ACX_SET_RATE_MGMT_PARAMS,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx set rate mgmt params failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl12xx_acx_config_hangover(struct wl1271 *wl)
{
	struct wl12xx_acx_config_hangover *acx;
	struct conf_hangover_settings *conf = &wl->conf.hangover;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx config hangover");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->recover_time = cpu_to_le32(conf->recover_time);
	acx->hangover_period = conf->hangover_period;
	acx->dynamic_mode = conf->dynamic_mode;
	acx->early_termination_mode = conf->early_termination_mode;
	acx->max_period = conf->max_period;
	acx->min_period = conf->min_period;
	acx->increase_delta = conf->increase_delta;
	acx->decrease_delta = conf->decrease_delta;
	acx->quiet_time = conf->quiet_time;
	acx->increase_time = conf->increase_time;
	acx->window_size = acx->window_size;

	ret = wl1271_cmd_configure(wl, ACX_CONFIG_HANGOVER, acx,
				   sizeof(*acx));

	if (ret < 0) {
		wl1271_warning("acx config hangover failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;

}

int wlcore_acx_average_rssi(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			    s8 *avg_rssi)
{
	struct acx_roaming_stats *acx;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx roaming statistics");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->role_id = wlvif->role_id;
	ret = wl1271_cmd_interrogate(wl, ACX_ROAMING_STATISTICS_TBL,
				     acx, sizeof(*acx));
	if (ret	< 0) {
		wl1271_warning("acx roaming statistics failed: %d", ret);
		ret = -ENOMEM;
		goto out;
	}

	*avg_rssi = acx->rssi_beacon;
out:
	kfree(acx);
	return ret;
}

#ifdef CONFIG_PM
/* Set the global behaviour of RX filters - On/Off + default action */
int wl1271_acx_default_rx_filter_enable(struct wl1271 *wl, bool enable,
					enum rx_filter_action action)
{
	struct acx_default_rx_filter *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx default rx filter en: %d act: %d",
		     enable, action);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	acx->enable = enable;
	acx->default_action = action;

	ret = wl1271_cmd_configure(wl, ACX_ENABLE_RX_DATA_FILTER, acx,
				   sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("acx default rx filter enable failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

/* Configure or disable a specific RX filter pattern */
int wl1271_acx_set_rx_filter(struct wl1271 *wl, u8 index, bool enable,
			     struct wl12xx_rx_filter *filter)
{
	struct acx_rx_filter_cfg *acx;
	int fields_size = 0;
	int acx_size;
	int ret;

	WARN_ON(enable && !filter);
	WARN_ON(index >= WL1271_MAX_RX_FILTERS);

	wl1271_debug(DEBUG_ACX,
		     "acx set rx filter idx: %d enable: %d filter: %p",
		     index, enable, filter);

	if (enable) {
		fields_size = wl1271_rx_filter_get_fields_size(filter);

		wl1271_debug(DEBUG_ACX, "act: %d num_fields: %d field_size: %d",
		      filter->action, filter->num_fields, fields_size);
	}

	acx_size = ALIGN(sizeof(*acx) + fields_size, 4);
	acx = kzalloc(acx_size, GFP_KERNEL);

	if (!acx)
		return -ENOMEM;

	acx->enable = enable;
	acx->index = index;

	if (enable) {
		acx->num_fields = filter->num_fields;
		acx->action = filter->action;
		wl1271_rx_filter_flatten_fields(filter, acx->fields);
	}

	wl1271_dump(DEBUG_ACX, "RX_FILTER: ", acx, acx_size);

	ret = wl1271_cmd_configure(wl, ACX_SET_RX_DATA_FILTER, acx, acx_size);
	if (ret < 0) {
		wl1271_warning("setting rx filter failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}
#endif /* CONFIG_PM */
