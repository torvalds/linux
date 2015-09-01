#include "acx.h"

#include <linux/module.h>
#include <linux/slab.h>

#include "wl1251.h"
#include "reg.h"
#include "cmd.h"
#include "ps.h"

int wl1251_acx_frame_rates(struct wl1251 *wl, u8 ctrl_rate, u8 ctrl_mod,
			   u8 mgt_rate, u8 mgt_mod)
{
	struct acx_fw_gen_frame_rates *rates;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx frame rates");

	rates = kzalloc(sizeof(*rates), GFP_KERNEL);
	if (!rates)
		return -ENOMEM;

	rates->tx_ctrl_frame_rate = ctrl_rate;
	rates->tx_ctrl_frame_mod = ctrl_mod;
	rates->tx_mgt_frame_rate = mgt_rate;
	rates->tx_mgt_frame_mod = mgt_mod;

	ret = wl1251_cmd_configure(wl, ACX_FW_GEN_FRAME_RATES,
				   rates, sizeof(*rates));
	if (ret < 0) {
		wl1251_error("Failed to set FW rates and modulation");
		goto out;
	}

out:
	kfree(rates);
	return ret;
}


int wl1251_acx_station_id(struct wl1251 *wl)
{
	struct acx_dot11_station_id *mac;
	int ret, i;

	wl1251_debug(DEBUG_ACX, "acx dot11_station_id");

	mac = kzalloc(sizeof(*mac), GFP_KERNEL);
	if (!mac)
		return -ENOMEM;

	for (i = 0; i < ETH_ALEN; i++)
		mac->mac[i] = wl->mac_addr[ETH_ALEN - 1 - i];

	ret = wl1251_cmd_configure(wl, DOT11_STATION_ID, mac, sizeof(*mac));

	kfree(mac);
	return ret;
}

int wl1251_acx_default_key(struct wl1251 *wl, u8 key_id)
{
	struct acx_dot11_default_key *default_key;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx dot11_default_key (%d)", key_id);

	default_key = kzalloc(sizeof(*default_key), GFP_KERNEL);
	if (!default_key)
		return -ENOMEM;

	default_key->id = key_id;

	ret = wl1251_cmd_configure(wl, DOT11_DEFAULT_KEY,
				   default_key, sizeof(*default_key));
	if (ret < 0) {
		wl1251_error("Couldn't set default key");
		goto out;
	}

	wl->default_key = key_id;

out:
	kfree(default_key);
	return ret;
}

int wl1251_acx_wake_up_conditions(struct wl1251 *wl, u8 wake_up_event,
				  u8 listen_interval)
{
	struct acx_wake_up_condition *wake_up;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx wake up conditions");

	wake_up = kzalloc(sizeof(*wake_up), GFP_KERNEL);
	if (!wake_up)
		return -ENOMEM;

	wake_up->wake_up_event = wake_up_event;
	wake_up->listen_interval = listen_interval;

	ret = wl1251_cmd_configure(wl, ACX_WAKE_UP_CONDITIONS,
				   wake_up, sizeof(*wake_up));
	if (ret < 0) {
		wl1251_warning("could not set wake up conditions: %d", ret);
		goto out;
	}

out:
	kfree(wake_up);
	return ret;
}

int wl1251_acx_sleep_auth(struct wl1251 *wl, u8 sleep_auth)
{
	struct acx_sleep_auth *auth;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx sleep auth");

	auth = kzalloc(sizeof(*auth), GFP_KERNEL);
	if (!auth)
		return -ENOMEM;

	auth->sleep_auth = sleep_auth;

	ret = wl1251_cmd_configure(wl, ACX_SLEEP_AUTH, auth, sizeof(*auth));

	kfree(auth);
	return ret;
}

int wl1251_acx_fw_version(struct wl1251 *wl, char *buf, size_t len)
{
	struct acx_revision *rev;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx fw rev");

	rev = kzalloc(sizeof(*rev), GFP_KERNEL);
	if (!rev)
		return -ENOMEM;

	ret = wl1251_cmd_interrogate(wl, ACX_FW_REV, rev, sizeof(*rev));
	if (ret < 0) {
		wl1251_warning("ACX_FW_REV interrogate failed");
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

int wl1251_acx_tx_power(struct wl1251 *wl, int power)
{
	struct acx_current_tx_power *acx;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx dot11_cur_tx_pwr");

	if (power < 0 || power > 25)
		return -EINVAL;

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	acx->current_tx_power = power * 10;

	ret = wl1251_cmd_configure(wl, DOT11_CUR_TX_PWR, acx, sizeof(*acx));
	if (ret < 0) {
		wl1251_warning("configure of tx power failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1251_acx_feature_cfg(struct wl1251 *wl, u32 data_flow_options)
{
	struct acx_feature_config *feature;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx feature cfg");

	feature = kzalloc(sizeof(*feature), GFP_KERNEL);
	if (!feature)
		return -ENOMEM;

	/* DF_ENCRYPTION_DISABLE and DF_SNIFF_MODE_ENABLE can be set */
	feature->data_flow_options = data_flow_options;
	feature->options = 0;

	ret = wl1251_cmd_configure(wl, ACX_FEATURE_CFG,
				   feature, sizeof(*feature));
	if (ret < 0) {
		wl1251_error("Couldn't set HW encryption");
		goto out;
	}

out:
	kfree(feature);
	return ret;
}

int wl1251_acx_mem_map(struct wl1251 *wl, struct acx_header *mem_map,
		       size_t len)
{
	int ret;

	wl1251_debug(DEBUG_ACX, "acx mem map");

	ret = wl1251_cmd_interrogate(wl, ACX_MEM_MAP, mem_map, len);
	if (ret < 0)
		return ret;

	return 0;
}

int wl1251_acx_data_path_params(struct wl1251 *wl,
				struct acx_data_path_params_resp *resp)
{
	struct acx_data_path_params *params;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx data path params");

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	params->rx_packet_ring_chunk_size = DP_RX_PACKET_RING_CHUNK_SIZE;
	params->tx_packet_ring_chunk_size = DP_TX_PACKET_RING_CHUNK_SIZE;

	params->rx_packet_ring_chunk_num = DP_RX_PACKET_RING_CHUNK_NUM;
	params->tx_packet_ring_chunk_num = DP_TX_PACKET_RING_CHUNK_NUM;

	params->tx_complete_threshold = 1;

	params->tx_complete_ring_depth = FW_TX_CMPLT_BLOCK_SIZE;

	params->tx_complete_timeout = DP_TX_COMPLETE_TIME_OUT;

	ret = wl1251_cmd_configure(wl, ACX_DATA_PATH_PARAMS,
				   params, sizeof(*params));
	if (ret < 0)
		goto out;

	/* FIXME: shouldn't this be ACX_DATA_PATH_RESP_PARAMS? */
	ret = wl1251_cmd_interrogate(wl, ACX_DATA_PATH_PARAMS,
				     resp, sizeof(*resp));

	if (ret < 0) {
		wl1251_warning("failed to read data path parameters: %d", ret);
		goto out;
	} else if (resp->header.cmd.status != CMD_STATUS_SUCCESS) {
		wl1251_warning("data path parameter acx status failed");
		ret = -EIO;
		goto out;
	}

out:
	kfree(params);
	return ret;
}

int wl1251_acx_rx_msdu_life_time(struct wl1251 *wl, u32 life_time)
{
	struct acx_rx_msdu_lifetime *acx;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx rx msdu life time");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	acx->lifetime = life_time;
	ret = wl1251_cmd_configure(wl, DOT11_RX_MSDU_LIFE_TIME,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl1251_warning("failed to set rx msdu life time: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1251_acx_rx_config(struct wl1251 *wl, u32 config, u32 filter)
{
	struct acx_rx_config *rx_config;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx rx config");

	rx_config = kzalloc(sizeof(*rx_config), GFP_KERNEL);
	if (!rx_config)
		return -ENOMEM;

	rx_config->config_options = config;
	rx_config->filter_options = filter;

	ret = wl1251_cmd_configure(wl, ACX_RX_CFG,
				   rx_config, sizeof(*rx_config));
	if (ret < 0) {
		wl1251_warning("failed to set rx config: %d", ret);
		goto out;
	}

out:
	kfree(rx_config);
	return ret;
}

int wl1251_acx_pd_threshold(struct wl1251 *wl)
{
	struct acx_packet_detection *pd;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx data pd threshold");

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	/* FIXME: threshold value not set */

	ret = wl1251_cmd_configure(wl, ACX_PD_THRESHOLD, pd, sizeof(*pd));
	if (ret < 0) {
		wl1251_warning("failed to set pd threshold: %d", ret);
		goto out;
	}

out:
	kfree(pd);
	return ret;
}

int wl1251_acx_slot(struct wl1251 *wl, enum acx_slot_type slot_time)
{
	struct acx_slot *slot;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx slot");

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot)
		return -ENOMEM;

	slot->wone_index = STATION_WONE_INDEX;
	slot->slot_time = slot_time;

	ret = wl1251_cmd_configure(wl, ACX_SLOT, slot, sizeof(*slot));
	if (ret < 0) {
		wl1251_warning("failed to set slot time: %d", ret);
		goto out;
	}

out:
	kfree(slot);
	return ret;
}

int wl1251_acx_group_address_tbl(struct wl1251 *wl, bool enable,
				 void *mc_list, u32 mc_list_len)
{
	struct acx_dot11_grp_addr_tbl *acx;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx group address tbl");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	/* MAC filtering */
	acx->enabled = enable;
	acx->num_groups = mc_list_len;
	memcpy(acx->mac_table, mc_list, mc_list_len * ETH_ALEN);

	ret = wl1251_cmd_configure(wl, DOT11_GROUP_ADDRESS_TBL,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl1251_warning("failed to set group addr table: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1251_acx_service_period_timeout(struct wl1251 *wl)
{
	struct acx_rx_timeout *rx_timeout;
	int ret;

	rx_timeout = kzalloc(sizeof(*rx_timeout), GFP_KERNEL);
	if (!rx_timeout)
		return -ENOMEM;

	wl1251_debug(DEBUG_ACX, "acx service period timeout");

	rx_timeout->ps_poll_timeout = RX_TIMEOUT_PS_POLL_DEF;
	rx_timeout->upsd_timeout = RX_TIMEOUT_UPSD_DEF;

	ret = wl1251_cmd_configure(wl, ACX_SERVICE_PERIOD_TIMEOUT,
				   rx_timeout, sizeof(*rx_timeout));
	if (ret < 0) {
		wl1251_warning("failed to set service period timeout: %d",
			       ret);
		goto out;
	}

out:
	kfree(rx_timeout);
	return ret;
}

int wl1251_acx_rts_threshold(struct wl1251 *wl, u16 rts_threshold)
{
	struct acx_rts_threshold *rts;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx rts threshold");

	rts = kzalloc(sizeof(*rts), GFP_KERNEL);
	if (!rts)
		return -ENOMEM;

	rts->threshold = rts_threshold;

	ret = wl1251_cmd_configure(wl, DOT11_RTS_THRESHOLD, rts, sizeof(*rts));
	if (ret < 0) {
		wl1251_warning("failed to set rts threshold: %d", ret);
		goto out;
	}

out:
	kfree(rts);
	return ret;
}

int wl1251_acx_beacon_filter_opt(struct wl1251 *wl, bool enable_filter)
{
	struct acx_beacon_filter_option *beacon_filter;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx beacon filter opt");

	beacon_filter = kzalloc(sizeof(*beacon_filter), GFP_KERNEL);
	if (!beacon_filter)
		return -ENOMEM;

	beacon_filter->enable = enable_filter;
	beacon_filter->max_num_beacons = 0;

	ret = wl1251_cmd_configure(wl, ACX_BEACON_FILTER_OPT,
				   beacon_filter, sizeof(*beacon_filter));
	if (ret < 0) {
		wl1251_warning("failed to set beacon filter opt: %d", ret);
		goto out;
	}

out:
	kfree(beacon_filter);
	return ret;
}

int wl1251_acx_beacon_filter_table(struct wl1251 *wl)
{
	struct acx_beacon_filter_ie_table *ie_table;
	int idx = 0;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx beacon filter table");

	ie_table = kzalloc(sizeof(*ie_table), GFP_KERNEL);
	if (!ie_table)
		return -ENOMEM;

	/* configure default beacon pass-through rules */
	ie_table->num_ie = 1;
	ie_table->table[idx++] = BEACON_FILTER_IE_ID_CHANNEL_SWITCH_ANN;
	ie_table->table[idx++] = BEACON_RULE_PASS_ON_APPEARANCE;

	ret = wl1251_cmd_configure(wl, ACX_BEACON_FILTER_TABLE,
				   ie_table, sizeof(*ie_table));
	if (ret < 0) {
		wl1251_warning("failed to set beacon filter table: %d", ret);
		goto out;
	}

out:
	kfree(ie_table);
	return ret;
}

int wl1251_acx_conn_monit_params(struct wl1251 *wl)
{
	struct acx_conn_monit_params *acx;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx connection monitor parameters");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	acx->synch_fail_thold = SYNCH_FAIL_DEFAULT_THRESHOLD;
	acx->bss_lose_timeout = NO_BEACON_DEFAULT_TIMEOUT;

	ret = wl1251_cmd_configure(wl, ACX_CONN_MONIT_PARAMS,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl1251_warning("failed to set connection monitor "
			       "parameters: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1251_acx_sg_enable(struct wl1251 *wl)
{
	struct acx_bt_wlan_coex *pta;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx sg enable");

	pta = kzalloc(sizeof(*pta), GFP_KERNEL);
	if (!pta)
		return -ENOMEM;

	pta->enable = SG_ENABLE;

	ret = wl1251_cmd_configure(wl, ACX_SG_ENABLE, pta, sizeof(*pta));
	if (ret < 0) {
		wl1251_warning("failed to set softgemini enable: %d", ret);
		goto out;
	}

out:
	kfree(pta);
	return ret;
}

int wl1251_acx_sg_cfg(struct wl1251 *wl)
{
	struct acx_bt_wlan_coex_param *param;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx sg cfg");

	param = kzalloc(sizeof(*param), GFP_KERNEL);
	if (!param)
		return -ENOMEM;

	/* BT-WLAN coext parameters */
	param->min_rate = RATE_INDEX_24MBPS;
	param->bt_hp_max_time = PTA_BT_HP_MAXTIME_DEF;
	param->wlan_hp_max_time = PTA_WLAN_HP_MAX_TIME_DEF;
	param->sense_disable_timer = PTA_SENSE_DISABLE_TIMER_DEF;
	param->rx_time_bt_hp = PTA_PROTECTIVE_RX_TIME_DEF;
	param->tx_time_bt_hp = PTA_PROTECTIVE_TX_TIME_DEF;
	param->rx_time_bt_hp_fast = PTA_PROTECTIVE_RX_TIME_FAST_DEF;
	param->tx_time_bt_hp_fast = PTA_PROTECTIVE_TX_TIME_FAST_DEF;
	param->wlan_cycle_fast = PTA_CYCLE_TIME_FAST_DEF;
	param->bt_anti_starvation_period = PTA_ANTI_STARVE_PERIOD_DEF;
	param->next_bt_lp_packet = PTA_TIMEOUT_NEXT_BT_LP_PACKET_DEF;
	param->wake_up_beacon = PTA_TIME_BEFORE_BEACON_DEF;
	param->hp_dm_max_guard_time = PTA_HPDM_MAX_TIME_DEF;
	param->next_wlan_packet = PTA_TIME_OUT_NEXT_WLAN_DEF;
	param->antenna_type = PTA_ANTENNA_TYPE_DEF;
	param->signal_type = PTA_SIGNALING_TYPE_DEF;
	param->afh_leverage_on = PTA_AFH_LEVERAGE_ON_DEF;
	param->quiet_cycle_num = PTA_NUMBER_QUIET_CYCLE_DEF;
	param->max_cts = PTA_MAX_NUM_CTS_DEF;
	param->wlan_packets_num = PTA_NUMBER_OF_WLAN_PACKETS_DEF;
	param->bt_packets_num = PTA_NUMBER_OF_BT_PACKETS_DEF;
	param->missed_rx_avalanche = PTA_RX_FOR_AVALANCHE_DEF;
	param->wlan_elp_hp = PTA_ELP_HP_DEF;
	param->bt_anti_starvation_cycles = PTA_ANTI_STARVE_NUM_CYCLE_DEF;
	param->ack_mode_dual_ant = PTA_ACK_MODE_DEF;
	param->pa_sd_enable = PTA_ALLOW_PA_SD_DEF;
	param->pta_auto_mode_enable = PTA_AUTO_MODE_NO_CTS_DEF;
	param->bt_hp_respected_num = PTA_BT_HP_RESPECTED_DEF;

	ret = wl1251_cmd_configure(wl, ACX_SG_CFG, param, sizeof(*param));
	if (ret < 0) {
		wl1251_warning("failed to set sg config: %d", ret);
		goto out;
	}

out:
	kfree(param);
	return ret;
}

int wl1251_acx_cca_threshold(struct wl1251 *wl)
{
	struct acx_energy_detection *detection;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx cca threshold");

	detection = kzalloc(sizeof(*detection), GFP_KERNEL);
	if (!detection)
		return -ENOMEM;

	detection->rx_cca_threshold = CCA_THRSH_DISABLE_ENERGY_D;
	detection->tx_energy_detection = 0;

	ret = wl1251_cmd_configure(wl, ACX_CCA_THRESHOLD,
				   detection, sizeof(*detection));
	if (ret < 0)
		wl1251_warning("failed to set cca threshold: %d", ret);

	kfree(detection);
	return ret;
}

int wl1251_acx_bcn_dtim_options(struct wl1251 *wl)
{
	struct acx_beacon_broadcast *bb;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx bcn dtim options");

	bb = kzalloc(sizeof(*bb), GFP_KERNEL);
	if (!bb)
		return -ENOMEM;

	bb->beacon_rx_timeout = BCN_RX_TIMEOUT_DEF_VALUE;
	bb->broadcast_timeout = BROADCAST_RX_TIMEOUT_DEF_VALUE;
	bb->rx_broadcast_in_ps = RX_BROADCAST_IN_PS_DEF_VALUE;
	bb->ps_poll_threshold = CONSECUTIVE_PS_POLL_FAILURE_DEF;

	ret = wl1251_cmd_configure(wl, ACX_BCN_DTIM_OPTIONS, bb, sizeof(*bb));
	if (ret < 0) {
		wl1251_warning("failed to set rx config: %d", ret);
		goto out;
	}

out:
	kfree(bb);
	return ret;
}

int wl1251_acx_aid(struct wl1251 *wl, u16 aid)
{
	struct acx_aid *acx_aid;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx aid");

	acx_aid = kzalloc(sizeof(*acx_aid), GFP_KERNEL);
	if (!acx_aid)
		return -ENOMEM;

	acx_aid->aid = aid;

	ret = wl1251_cmd_configure(wl, ACX_AID, acx_aid, sizeof(*acx_aid));
	if (ret < 0) {
		wl1251_warning("failed to set aid: %d", ret);
		goto out;
	}

out:
	kfree(acx_aid);
	return ret;
}

int wl1251_acx_event_mbox_mask(struct wl1251 *wl, u32 event_mask)
{
	struct acx_event_mask *mask;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx event mbox mask");

	mask = kzalloc(sizeof(*mask), GFP_KERNEL);
	if (!mask)
		return -ENOMEM;

	/* high event mask is unused */
	mask->high_event_mask = 0xffffffff;

	mask->event_mask = event_mask;

	ret = wl1251_cmd_configure(wl, ACX_EVENT_MBOX_MASK,
				   mask, sizeof(*mask));
	if (ret < 0) {
		wl1251_warning("failed to set acx_event_mbox_mask: %d", ret);
		goto out;
	}

out:
	kfree(mask);
	return ret;
}

int wl1251_acx_low_rssi(struct wl1251 *wl, s8 threshold, u8 weight,
			u8 depth, enum wl1251_acx_low_rssi_type type)
{
	struct acx_low_rssi *rssi;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx low rssi");

	rssi = kzalloc(sizeof(*rssi), GFP_KERNEL);
	if (!rssi)
		return -ENOMEM;

	rssi->threshold = threshold;
	rssi->weight = weight;
	rssi->depth = depth;
	rssi->type = type;

	ret = wl1251_cmd_configure(wl, ACX_LOW_RSSI, rssi, sizeof(*rssi));
	if (ret < 0)
		wl1251_warning("failed to set low rssi threshold: %d", ret);

	kfree(rssi);
	return ret;
}

int wl1251_acx_set_preamble(struct wl1251 *wl, enum acx_preamble_type preamble)
{
	struct acx_preamble *acx;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx_set_preamble");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	acx->preamble = preamble;

	ret = wl1251_cmd_configure(wl, ACX_PREAMBLE_TYPE, acx, sizeof(*acx));
	if (ret < 0) {
		wl1251_warning("Setting of preamble failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1251_acx_cts_protect(struct wl1251 *wl,
			   enum acx_ctsprotect_type ctsprotect)
{
	struct acx_ctsprotect *acx;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx_set_ctsprotect");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	acx->ctsprotect = ctsprotect;

	ret = wl1251_cmd_configure(wl, ACX_CTS_PROTECTION, acx, sizeof(*acx));
	if (ret < 0) {
		wl1251_warning("Setting of ctsprotect failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1251_acx_tsf_info(struct wl1251 *wl, u64 *mactime)
{
	struct acx_tsf_info *tsf_info;
	int ret;

	tsf_info = kzalloc(sizeof(*tsf_info), GFP_KERNEL);
	if (!tsf_info)
		return -ENOMEM;

	ret = wl1251_cmd_interrogate(wl, ACX_TSF_INFO,
				     tsf_info, sizeof(*tsf_info));
	if (ret < 0) {
		wl1251_warning("ACX_FW_REV interrogate failed");
		goto out;
	}

	*mactime = tsf_info->current_tsf_lsb |
		((u64)tsf_info->current_tsf_msb << 32);

out:
	kfree(tsf_info);
	return ret;
}

int wl1251_acx_statistics(struct wl1251 *wl, struct acx_statistics *stats)
{
	int ret;

	wl1251_debug(DEBUG_ACX, "acx statistics");

	ret = wl1251_cmd_interrogate(wl, ACX_STATISTICS, stats,
				     sizeof(*stats));
	if (ret < 0) {
		wl1251_warning("acx statistics failed: %d", ret);
		return -ENOMEM;
	}

	return 0;
}

int wl1251_acx_rate_policies(struct wl1251 *wl)
{
	struct acx_rate_policy *acx;
	int ret = 0;

	wl1251_debug(DEBUG_ACX, "acx rate policies");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	/* configure one default (one-size-fits-all) rate class */
	acx->rate_class_cnt = 2;
	acx->rate_class[0].enabled_rates = ACX_RATE_MASK_UNSPECIFIED;
	acx->rate_class[0].short_retry_limit = ACX_RATE_RETRY_LIMIT;
	acx->rate_class[0].long_retry_limit = ACX_RATE_RETRY_LIMIT;
	acx->rate_class[0].aflags = 0;

	/* no-retry rate class */
	acx->rate_class[1].enabled_rates = ACX_RATE_MASK_UNSPECIFIED;
	acx->rate_class[1].short_retry_limit = 0;
	acx->rate_class[1].long_retry_limit = 0;
	acx->rate_class[1].aflags = 0;

	ret = wl1251_cmd_configure(wl, ACX_RATE_POLICY, acx, sizeof(*acx));
	if (ret < 0) {
		wl1251_warning("Setting of rate policies failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1251_acx_mem_cfg(struct wl1251 *wl)
{
	struct wl1251_acx_config_memory *mem_conf;
	int ret, i;

	wl1251_debug(DEBUG_ACX, "acx mem cfg");

	mem_conf = kzalloc(sizeof(*mem_conf), GFP_KERNEL);
	if (!mem_conf)
		return -ENOMEM;

	/* memory config */
	mem_conf->mem_config.num_stations = cpu_to_le16(DEFAULT_NUM_STATIONS);
	mem_conf->mem_config.rx_mem_block_num = 35;
	mem_conf->mem_config.tx_min_mem_block_num = 64;
	mem_conf->mem_config.num_tx_queues = MAX_TX_QUEUES;
	mem_conf->mem_config.host_if_options = HOSTIF_PKT_RING;
	mem_conf->mem_config.num_ssid_profiles = 1;
	mem_conf->mem_config.debug_buffer_size =
		cpu_to_le16(TRACE_BUFFER_MAX_SIZE);

	/* RX queue config */
	mem_conf->rx_queue_config.dma_address = 0;
	mem_conf->rx_queue_config.num_descs = ACX_RX_DESC_DEF;
	mem_conf->rx_queue_config.priority = DEFAULT_RXQ_PRIORITY;
	mem_conf->rx_queue_config.type = DEFAULT_RXQ_TYPE;

	/* TX queue config */
	for (i = 0; i < MAX_TX_QUEUES; i++) {
		mem_conf->tx_queue_config[i].num_descs = ACX_TX_DESC_DEF;
		mem_conf->tx_queue_config[i].attributes = i;
	}

	ret = wl1251_cmd_configure(wl, ACX_MEM_CFG, mem_conf,
				   sizeof(*mem_conf));
	if (ret < 0) {
		wl1251_warning("wl1251 mem config failed: %d", ret);
		goto out;
	}

out:
	kfree(mem_conf);
	return ret;
}

int wl1251_acx_wr_tbtt_and_dtim(struct wl1251 *wl, u16 tbtt, u8 dtim)
{
	struct wl1251_acx_wr_tbtt_and_dtim *acx;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx tbtt and dtim");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	acx->tbtt = tbtt;
	acx->dtim = dtim;

	ret = wl1251_cmd_configure(wl, ACX_WR_TBTT_AND_DTIM,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl1251_warning("failed to set tbtt and dtim: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1251_acx_bet_enable(struct wl1251 *wl, enum wl1251_acx_bet_mode mode,
			  u8 max_consecutive)
{
	struct wl1251_acx_bet_enable *acx;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx bet enable");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	acx->enable = mode;
	acx->max_consecutive = max_consecutive;

	ret = wl1251_cmd_configure(wl, ACX_BET_ENABLE, acx, sizeof(*acx));
	if (ret < 0) {
		wl1251_warning("wl1251 acx bet enable failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1251_acx_arp_ip_filter(struct wl1251 *wl, bool enable, __be32 address)
{
	struct wl1251_acx_arp_filter *acx;
	int ret;

	wl1251_debug(DEBUG_ACX, "acx arp ip filter, enable: %d", enable);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	acx->version = ACX_IPV4_VERSION;
	acx->enable = enable;

	if (enable)
		memcpy(acx->address, &address, ACX_IPV4_ADDR_SIZE);

	ret = wl1251_cmd_configure(wl, ACX_ARP_IP_FILTER,
				   acx, sizeof(*acx));
	if (ret < 0)
		wl1251_warning("failed to set arp ip filter: %d", ret);

	kfree(acx);
	return ret;
}

int wl1251_acx_ac_cfg(struct wl1251 *wl, u8 ac, u8 cw_min, u16 cw_max,
		      u8 aifs, u16 txop)
{
	struct wl1251_acx_ac_cfg *acx;
	int ret = 0;

	wl1251_debug(DEBUG_ACX, "acx ac cfg %d cw_ming %d cw_max %d "
		     "aifs %d txop %d", ac, cw_min, cw_max, aifs, txop);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	acx->ac = ac;
	acx->cw_min = cw_min;
	acx->cw_max = cw_max;
	acx->aifsn = aifs;
	acx->txop_limit = txop;

	ret = wl1251_cmd_configure(wl, ACX_AC_CFG, acx, sizeof(*acx));
	if (ret < 0) {
		wl1251_warning("acx ac cfg failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl1251_acx_tid_cfg(struct wl1251 *wl, u8 queue,
		       enum wl1251_acx_channel_type type,
		       u8 tsid, enum wl1251_acx_ps_scheme ps_scheme,
		       enum wl1251_acx_ack_policy ack_policy)
{
	struct wl1251_acx_tid_cfg *acx;
	int ret = 0;

	wl1251_debug(DEBUG_ACX, "acx tid cfg %d type %d tsid %d "
		     "ps_scheme %d ack_policy %d", queue, type, tsid,
		     ps_scheme, ack_policy);

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx)
		return -ENOMEM;

	acx->queue = queue;
	acx->type = type;
	acx->tsid = tsid;
	acx->ps_scheme = ps_scheme;
	acx->ack_policy = ack_policy;

	ret = wl1251_cmd_configure(wl, ACX_TID_CFG, acx, sizeof(*acx));
	if (ret < 0) {
		wl1251_warning("acx tid cfg failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}
