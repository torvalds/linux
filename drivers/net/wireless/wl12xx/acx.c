#include "acx.h"

#include <linux/module.h>
#include <linux/crc7.h>
#include <linux/spi/spi.h>

#include "wl12xx.h"
#include "wl12xx_80211.h"
#include "reg.h"
#include "spi.h"
#include "ps.h"

int wl12xx_acx_frame_rates(struct wl12xx *wl, u8 ctrl_rate, u8 ctrl_mod,
			   u8 mgt_rate, u8 mgt_mod)
{
	int ret;
	struct acx_fw_gen_frame_rates rates;

	wl12xx_debug(DEBUG_ACX, "acx frame rates");

	rates.header.id = ACX_FW_GEN_FRAME_RATES;
	rates.header.len = sizeof(struct acx_fw_gen_frame_rates) -
		sizeof(struct acx_header);

	rates.tx_ctrl_frame_rate = ctrl_rate;
	rates.tx_ctrl_frame_mod = ctrl_mod;
	rates.tx_mgt_frame_rate = mgt_rate;
	rates.tx_mgt_frame_mod = mgt_mod;

	ret = wl12xx_cmd_configure(wl, &rates, sizeof(rates));
	if (ret < 0) {
		wl12xx_error("Failed to set FW rates and modulation");
		return ret;
	}

	return 0;
}


int wl12xx_acx_station_id(struct wl12xx *wl)
{
	int ret, i;
	struct dot11_station_id mac;

	wl12xx_debug(DEBUG_ACX, "acx dot11_station_id");

	mac.header.id = DOT11_STATION_ID;
	mac.header.len = sizeof(mac) - sizeof(struct acx_header);

	for (i = 0; i < ETH_ALEN; i++)
		mac.mac[i] = wl->mac_addr[ETH_ALEN - 1 - i];

	ret = wl12xx_cmd_configure(wl, &mac, sizeof(mac));
	if (ret < 0)
		return ret;

	return 0;
}

int wl12xx_acx_default_key(struct wl12xx *wl, u8 key_id)
{
	struct acx_dot11_default_key default_key;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx dot11_default_key (%d)", key_id);

	default_key.header.id = DOT11_DEFAULT_KEY;
	default_key.header.len = sizeof(default_key) -
		sizeof(struct acx_header);

	default_key.id = key_id;

	ret = wl12xx_cmd_configure(wl, &default_key, sizeof(default_key));
	if (ret < 0) {
		wl12xx_error("Couldnt set default key");
		return ret;
	}

	wl->default_key = key_id;

	return 0;
}

int wl12xx_acx_wake_up_conditions(struct wl12xx *wl, u8 listen_interval)
{
	struct acx_wake_up_condition wake_up;

	wl12xx_debug(DEBUG_ACX, "acx wake up conditions");

	wake_up.header.id = ACX_WAKE_UP_CONDITIONS;
	wake_up.header.len = sizeof(wake_up) - sizeof(struct acx_header);

	wake_up.wake_up_event = WAKE_UP_EVENT_DTIM_BITMAP;
	wake_up.listen_interval = listen_interval;

	return wl12xx_cmd_configure(wl, &wake_up, sizeof(wake_up));
}

int wl12xx_acx_sleep_auth(struct wl12xx *wl, u8 sleep_auth)
{
	int ret;
	struct acx_sleep_auth auth;

	wl12xx_debug(DEBUG_ACX, "acx sleep auth");

	auth.header.id = ACX_SLEEP_AUTH;
	auth.header.len = sizeof(auth) - sizeof(struct acx_header);

	auth.sleep_auth = sleep_auth;

	ret = wl12xx_cmd_configure(wl, &auth, sizeof(auth));
	if (ret < 0)
		return ret;

	return 0;
}

int wl12xx_acx_fw_version(struct wl12xx *wl, char *buf, size_t len)
{
	struct wl12xx_command cmd;
	struct acx_revision *rev;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx fw rev");

	memset(&cmd, 0, sizeof(cmd));

	ret = wl12xx_cmd_interrogate(wl, ACX_FW_REV, sizeof(*rev), &cmd);
	if (ret < 0) {
		wl12xx_warning("ACX_FW_REV interrogate failed");
		return ret;
	}

	rev = (struct acx_revision *) &cmd.parameters;

	/* be careful with the buffer sizes */
	strncpy(buf, rev->fw_version, min(len, sizeof(rev->fw_version)));

	/*
	 * if the firmware version string is exactly
	 * sizeof(rev->fw_version) long or fw_len is less than
	 * sizeof(rev->fw_version) it won't be null terminated
	 */
	buf[min(len, sizeof(rev->fw_version)) - 1] = '\0';

	return 0;
}

int wl12xx_acx_tx_power(struct wl12xx *wl, int power)
{
	struct acx_current_tx_power ie;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx dot11_cur_tx_pwr");

	if (power < 0 || power > 25)
		return -EINVAL;

	memset(&ie, 0, sizeof(ie));

	ie.header.id = DOT11_CUR_TX_PWR;
	ie.header.len = sizeof(ie) - sizeof(struct acx_header);
	ie.current_tx_power = power * 10;

	ret = wl12xx_cmd_configure(wl, &ie, sizeof(ie));
	if (ret < 0) {
		wl12xx_warning("configure of tx power failed: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_feature_cfg(struct wl12xx *wl)
{
	struct acx_feature_config feature;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx feature cfg");

	memset(&feature, 0, sizeof(feature));

	feature.header.id = ACX_FEATURE_CFG;
	feature.header.len = sizeof(feature) - sizeof(struct acx_header);

	/* DF_ENCRYPTION_DISABLE and DF_SNIFF_MODE_ENABLE are disabled */
	feature.data_flow_options = 0;
	feature.options = 0;

	ret = wl12xx_cmd_configure(wl, &feature, sizeof(feature));
	if (ret < 0)
		wl12xx_error("Couldnt set HW encryption");

	return ret;
}

int wl12xx_acx_mem_map(struct wl12xx *wl, void *mem_map, size_t len)
{
	struct wl12xx_command cmd;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx mem map");

	ret = wl12xx_cmd_interrogate(wl, ACX_MEM_MAP, len, &cmd);
	if (ret < 0)
		return ret;
	else if (cmd.status != CMD_STATUS_SUCCESS)
		return -EIO;

	memcpy(mem_map, &cmd.parameters, len);

	return 0;
}

int wl12xx_acx_data_path_params(struct wl12xx *wl,
				struct acx_data_path_params_resp *data_path)
{
	struct acx_data_path_params params;
	struct wl12xx_command cmd;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx data path params");

	params.rx_packet_ring_chunk_size = DP_RX_PACKET_RING_CHUNK_SIZE;
	params.tx_packet_ring_chunk_size = DP_TX_PACKET_RING_CHUNK_SIZE;

	params.rx_packet_ring_chunk_num = DP_RX_PACKET_RING_CHUNK_NUM;
	params.tx_packet_ring_chunk_num = DP_TX_PACKET_RING_CHUNK_NUM;

	params.tx_complete_threshold = 1;

	params.tx_complete_ring_depth = FW_TX_CMPLT_BLOCK_SIZE;

	params.tx_complete_timeout = DP_TX_COMPLETE_TIME_OUT;

	params.header.id = ACX_DATA_PATH_PARAMS;
	params.header.len = sizeof(params) - sizeof(struct acx_header);

	ret = wl12xx_cmd_configure(wl, &params, sizeof(params));
	if (ret < 0)
		return ret;


	ret = wl12xx_cmd_interrogate(wl, ACX_DATA_PATH_PARAMS,
				     sizeof(struct acx_data_path_params_resp),
				     &cmd);

	if (ret < 0) {
		wl12xx_warning("failed to read data path parameters: %d", ret);
		return ret;
	} else if (cmd.status != CMD_STATUS_SUCCESS) {
		wl12xx_warning("data path parameter acx status failed");
		return -EIO;
	}

	memcpy(data_path, &cmd.parameters, sizeof(*data_path));

	return 0;
}

int wl12xx_acx_rx_msdu_life_time(struct wl12xx *wl, u32 life_time)
{
	struct rx_msdu_lifetime msdu_lifetime;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx rx msdu life time");

	msdu_lifetime.header.id = DOT11_RX_MSDU_LIFE_TIME;
	msdu_lifetime.header.len = sizeof(msdu_lifetime) -
		sizeof(struct acx_header);
	msdu_lifetime.lifetime = life_time;

	ret = wl12xx_cmd_configure(wl, &msdu_lifetime, sizeof(msdu_lifetime));
	if (ret < 0) {
		wl12xx_warning("failed to set rx msdu life time: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_rx_config(struct wl12xx *wl, u32 config, u32 filter)
{
	struct acx_rx_config rx_config;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx rx config");

	rx_config.header.id = ACX_RX_CFG;
	rx_config.header.len = sizeof(rx_config) - sizeof(struct acx_header);
	rx_config.config_options = config;
	rx_config.filter_options = filter;

	ret = wl12xx_cmd_configure(wl, &rx_config, sizeof(rx_config));
	if (ret < 0) {
		wl12xx_warning("failed to set rx config: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_pd_threshold(struct wl12xx *wl)
{
	struct acx_packet_detection packet_detection;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx data pd threshold");

	/* FIXME: threshold value not set */
	packet_detection.header.id = ACX_PD_THRESHOLD;
	packet_detection.header.len = sizeof(packet_detection) -
		sizeof(struct acx_header);

	ret = wl12xx_cmd_configure(wl, &packet_detection,
				   sizeof(packet_detection));
	if (ret < 0) {
		wl12xx_warning("failed to set pd threshold: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_slot(struct wl12xx *wl, enum acx_slot_type slot_time)
{
	struct acx_slot slot;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx slot");

	slot.header.id = ACX_SLOT;
	slot.header.len = sizeof(slot) - sizeof(struct acx_header);

	slot.wone_index = STATION_WONE_INDEX;
	slot.slot_time = slot_time;

	ret = wl12xx_cmd_configure(wl, &slot, sizeof(slot));
	if (ret < 0) {
		wl12xx_warning("failed to set slot time: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_group_address_tbl(struct wl12xx *wl)
{
	struct multicast_grp_addr_start multicast;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx group address tbl");

	/* MAC filtering */
	multicast.header.id = DOT11_GROUP_ADDRESS_TBL;
	multicast.header.len = sizeof(multicast) - sizeof(struct acx_header);

	multicast.enabled = 0;
	multicast.num_groups = 0;
	memset(multicast.mac_table, 0, ADDRESS_GROUP_MAX_LEN);

	ret = wl12xx_cmd_configure(wl, &multicast, sizeof(multicast));
	if (ret < 0) {
		wl12xx_warning("failed to set group addr table: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_service_period_timeout(struct wl12xx *wl)
{
	struct acx_rx_timeout rx_timeout;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx service period timeout");

	/* RX timeout */
	rx_timeout.header.id = ACX_SERVICE_PERIOD_TIMEOUT;
	rx_timeout.header.len = sizeof(rx_timeout) - sizeof(struct acx_header);

	rx_timeout.ps_poll_timeout = RX_TIMEOUT_PS_POLL_DEF;
	rx_timeout.upsd_timeout = RX_TIMEOUT_UPSD_DEF;

	ret = wl12xx_cmd_configure(wl, &rx_timeout, sizeof(rx_timeout));
	if (ret < 0) {
		wl12xx_warning("failed to set service period timeout: %d",
			       ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_rts_threshold(struct wl12xx *wl, u16 rts_threshold)
{
	struct acx_rts_threshold rts;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx rts threshold");

	rts.header.id = DOT11_RTS_THRESHOLD;
	rts.header.len = sizeof(rts) - sizeof(struct acx_header);

	rts.threshold = rts_threshold;

	ret = wl12xx_cmd_configure(wl, &rts, sizeof(rts));
	if (ret < 0) {
		wl12xx_warning("failed to set rts threshold: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_beacon_filter_opt(struct wl12xx *wl)
{
	struct acx_beacon_filter_option beacon_filter;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx beacon filter opt");

	beacon_filter.header.id = ACX_BEACON_FILTER_OPT;
	beacon_filter.header.len = sizeof(beacon_filter) -
		sizeof(struct acx_header);

	beacon_filter.enable = 0;
	beacon_filter.max_num_beacons = 0;

	ret = wl12xx_cmd_configure(wl, &beacon_filter, sizeof(beacon_filter));
	if (ret < 0) {
		wl12xx_warning("failed to set beacon filter opt: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_beacon_filter_table(struct wl12xx *wl)
{
	struct acx_beacon_filter_ie_table ie_table;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx beacon filter table");

	ie_table.header.id = ACX_BEACON_FILTER_TABLE;
	ie_table.header.len = sizeof(ie_table) - sizeof(struct acx_header);

	ie_table.num_ie = 0;
	memset(ie_table.table, 0, BEACON_FILTER_TABLE_MAX_SIZE);

	ret = wl12xx_cmd_configure(wl, &ie_table, sizeof(ie_table));
	if (ret < 0) {
		wl12xx_warning("failed to set beacon filter table: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_sg_enable(struct wl12xx *wl)
{
	struct acx_bt_wlan_coex pta;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx sg enable");

	pta.header.id = ACX_SG_ENABLE;
	pta.header.len = sizeof(pta) - sizeof(struct acx_header);

	pta.enable = SG_ENABLE;

	ret = wl12xx_cmd_configure(wl, &pta, sizeof(pta));
	if (ret < 0) {
		wl12xx_warning("failed to set softgemini enable: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_sg_cfg(struct wl12xx *wl)
{
	struct acx_bt_wlan_coex_param param;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx sg cfg");

	/* BT-WLAN coext parameters */
	param.header.id = ACX_SG_CFG;
	param.header.len = sizeof(param) - sizeof(struct acx_header);

	param.min_rate = RATE_INDEX_24MBPS;
	param.bt_hp_max_time = PTA_BT_HP_MAXTIME_DEF;
	param.wlan_hp_max_time = PTA_WLAN_HP_MAX_TIME_DEF;
	param.sense_disable_timer = PTA_SENSE_DISABLE_TIMER_DEF;
	param.rx_time_bt_hp = PTA_PROTECTIVE_RX_TIME_DEF;
	param.tx_time_bt_hp = PTA_PROTECTIVE_TX_TIME_DEF;
	param.rx_time_bt_hp_fast = PTA_PROTECTIVE_RX_TIME_FAST_DEF;
	param.tx_time_bt_hp_fast = PTA_PROTECTIVE_TX_TIME_FAST_DEF;
	param.wlan_cycle_fast = PTA_CYCLE_TIME_FAST_DEF;
	param.bt_anti_starvation_period = PTA_ANTI_STARVE_PERIOD_DEF;
	param.next_bt_lp_packet = PTA_TIMEOUT_NEXT_BT_LP_PACKET_DEF;
	param.wake_up_beacon = PTA_TIME_BEFORE_BEACON_DEF;
	param.hp_dm_max_guard_time = PTA_HPDM_MAX_TIME_DEF;
	param.next_wlan_packet = PTA_TIME_OUT_NEXT_WLAN_DEF;
	param.antenna_type = PTA_ANTENNA_TYPE_DEF;
	param.signal_type = PTA_SIGNALING_TYPE_DEF;
	param.afh_leverage_on = PTA_AFH_LEVERAGE_ON_DEF;
	param.quiet_cycle_num = PTA_NUMBER_QUIET_CYCLE_DEF;
	param.max_cts = PTA_MAX_NUM_CTS_DEF;
	param.wlan_packets_num = PTA_NUMBER_OF_WLAN_PACKETS_DEF;
	param.bt_packets_num = PTA_NUMBER_OF_BT_PACKETS_DEF;
	param.missed_rx_avalanche = PTA_RX_FOR_AVALANCHE_DEF;
	param.wlan_elp_hp = PTA_ELP_HP_DEF;
	param.bt_anti_starvation_cycles = PTA_ANTI_STARVE_NUM_CYCLE_DEF;
	param.ack_mode_dual_ant = PTA_ACK_MODE_DEF;
	param.pa_sd_enable = PTA_ALLOW_PA_SD_DEF;
	param.pta_auto_mode_enable = PTA_AUTO_MODE_NO_CTS_DEF;
	param.bt_hp_respected_num = PTA_BT_HP_RESPECTED_DEF;

	ret = wl12xx_cmd_configure(wl, &param, sizeof(param));
	if (ret < 0) {
		wl12xx_warning("failed to set sg config: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_cca_threshold(struct wl12xx *wl)
{
	struct acx_energy_detection detection;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx cca threshold");

	detection.header.id = ACX_CCA_THRESHOLD;
	detection.header.len = sizeof(detection) - sizeof(struct acx_header);

	detection.rx_cca_threshold = CCA_THRSH_DISABLE_ENERGY_D;
	detection.tx_energy_detection = 0;

	ret = wl12xx_cmd_configure(wl, &detection, sizeof(detection));
	if (ret < 0) {
		wl12xx_warning("failed to set cca threshold: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_bcn_dtim_options(struct wl12xx *wl)
{
	struct acx_beacon_broadcast bb;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx bcn dtim options");

	bb.header.id = ACX_BCN_DTIM_OPTIONS;
	bb.header.len = sizeof(bb) - sizeof(struct acx_header);

	bb.beacon_rx_timeout = BCN_RX_TIMEOUT_DEF_VALUE;
	bb.broadcast_timeout = BROADCAST_RX_TIMEOUT_DEF_VALUE;
	bb.rx_broadcast_in_ps = RX_BROADCAST_IN_PS_DEF_VALUE;
	bb.ps_poll_threshold = CONSECUTIVE_PS_POLL_FAILURE_DEF;

	ret = wl12xx_cmd_configure(wl, &bb, sizeof(bb));
	if (ret < 0) {
		wl12xx_warning("failed to set rx config: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_aid(struct wl12xx *wl, u16 aid)
{
	struct acx_aid acx_aid;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx aid");

	acx_aid.header.id = ACX_AID;
	acx_aid.header.len = sizeof(acx_aid) - sizeof(struct acx_header);

	acx_aid.aid = aid;

	ret = wl12xx_cmd_configure(wl, &acx_aid, sizeof(acx_aid));
	if (ret < 0) {
		wl12xx_warning("failed to set aid: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_event_mbox_mask(struct wl12xx *wl, u32 event_mask)
{
	struct acx_event_mask mask;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx event mbox mask");

	mask.header.id = ACX_EVENT_MBOX_MASK;
	mask.header.len = sizeof(mask) - sizeof(struct acx_header);

	/* high event mask is unused */
	mask.high_event_mask = 0xffffffff;

	mask.event_mask = event_mask;

	ret = wl12xx_cmd_configure(wl, &mask, sizeof(mask));
	if (ret < 0) {
		wl12xx_warning("failed to set aid: %d", ret);
		return ret;
	}

	return 0;
}

int wl12xx_acx_set_preamble(struct wl12xx *wl, enum acx_preamble_type preamble)
{
	struct acx_preamble ie;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx_set_preamble");

	memset(&ie, 0, sizeof(ie));

	ie.header.id = ACX_PREAMBLE_TYPE;
	ie.header.len = sizeof(ie) - sizeof(struct acx_header);
	ie.preamble = preamble;
	ret = wl12xx_cmd_configure(wl, &ie, sizeof(ie));
	if (ret < 0) {
		wl12xx_warning("Setting of preamble failed: %d", ret);
		return ret;
	}
	return 0;
}

int wl12xx_acx_cts_protect(struct wl12xx *wl,
			   enum acx_ctsprotect_type ctsprotect)
{
	struct acx_ctsprotect ie;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx_set_ctsprotect");

	memset(&ie, 0, sizeof(ie));

	ie.header.id = ACX_CTS_PROTECTION;
	ie.header.len = sizeof(ie) - sizeof(struct acx_header);
	ie.ctsprotect = ctsprotect;
	ret = wl12xx_cmd_configure(wl, &ie, sizeof(ie));
	if (ret < 0) {
		wl12xx_warning("Setting of ctsprotect failed: %d", ret);
		return ret;
	}
	return 0;
}

int wl12xx_acx_statistics(struct wl12xx *wl, struct acx_statistics *stats)
{
	struct wl12xx_command *answer;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx statistics");

	answer = kmalloc(sizeof(*answer), GFP_KERNEL);
	if (!answer) {
		wl12xx_warning("could not allocate memory for acx statistics");
		ret = -ENOMEM;
		goto out;
	}

	ret = wl12xx_cmd_interrogate(wl, ACX_STATISTICS, sizeof(*answer),
				     answer);
	if (ret < 0) {
		wl12xx_warning("acx statistics failed: %d", ret);
		goto out;
	}

	memcpy(stats, answer->parameters, sizeof(*stats));

out:
	kfree(answer);
	return ret;
}
