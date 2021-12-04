// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2021 Intel Corporation
 * Copyright (C) 2013-2014 Intel Mobile Communications GmbH
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 */
#include <net/mac80211.h>

#include "iwl-debug.h"
#include "iwl-io.h"
#include "iwl-prph.h"
#include "iwl-csr.h"
#include "mvm.h"
#include "fw/api/rs.h"
#include "fw/img.h"

/*
 * Will return 0 even if the cmd failed when RFKILL is asserted unless
 * CMD_WANT_SKB is set in cmd->flags.
 */
int iwl_mvm_send_cmd(struct iwl_mvm *mvm, struct iwl_host_cmd *cmd)
{
	int ret;

#if defined(CONFIG_IWLWIFI_DEBUGFS) && defined(CONFIG_PM_SLEEP)
	if (WARN_ON(mvm->d3_test_active))
		return -EIO;
#endif

	/*
	 * Synchronous commands from this op-mode must hold
	 * the mutex, this ensures we don't try to send two
	 * (or more) synchronous commands at a time.
	 */
	if (!(cmd->flags & CMD_ASYNC))
		lockdep_assert_held(&mvm->mutex);

	ret = iwl_trans_send_cmd(mvm->trans, cmd);

	/*
	 * If the caller wants the SKB, then don't hide any problems, the
	 * caller might access the response buffer which will be NULL if
	 * the command failed.
	 */
	if (cmd->flags & CMD_WANT_SKB)
		return ret;

	/*
	 * Silently ignore failures if RFKILL is asserted or
	 * we are in suspend\resume process
	 */
	if (!ret || ret == -ERFKILL || ret == -EHOSTDOWN)
		return 0;
	return ret;
}

int iwl_mvm_send_cmd_pdu(struct iwl_mvm *mvm, u32 id,
			 u32 flags, u16 len, const void *data)
{
	struct iwl_host_cmd cmd = {
		.id = id,
		.len = { len, },
		.data = { data, },
		.flags = flags,
	};

	return iwl_mvm_send_cmd(mvm, &cmd);
}

/*
 * We assume that the caller set the status to the success value
 */
int iwl_mvm_send_cmd_status(struct iwl_mvm *mvm, struct iwl_host_cmd *cmd,
			    u32 *status)
{
	struct iwl_rx_packet *pkt;
	struct iwl_cmd_response *resp;
	int ret, resp_len;

	lockdep_assert_held(&mvm->mutex);

#if defined(CONFIG_IWLWIFI_DEBUGFS) && defined(CONFIG_PM_SLEEP)
	if (WARN_ON(mvm->d3_test_active))
		return -EIO;
#endif

	/*
	 * Only synchronous commands can wait for status,
	 * we use WANT_SKB so the caller can't.
	 */
	if (WARN_ONCE(cmd->flags & (CMD_ASYNC | CMD_WANT_SKB),
		      "cmd flags %x", cmd->flags))
		return -EINVAL;

	cmd->flags |= CMD_WANT_SKB;

	ret = iwl_trans_send_cmd(mvm->trans, cmd);
	if (ret == -ERFKILL) {
		/*
		 * The command failed because of RFKILL, don't update
		 * the status, leave it as success and return 0.
		 */
		return 0;
	} else if (ret) {
		return ret;
	}

	pkt = cmd->resp_pkt;

	resp_len = iwl_rx_packet_payload_len(pkt);
	if (WARN_ON_ONCE(resp_len != sizeof(*resp))) {
		ret = -EIO;
		goto out_free_resp;
	}

	resp = (void *)pkt->data;
	*status = le32_to_cpu(resp->status);
 out_free_resp:
	iwl_free_resp(cmd);
	return ret;
}

/*
 * We assume that the caller set the status to the sucess value
 */
int iwl_mvm_send_cmd_pdu_status(struct iwl_mvm *mvm, u32 id, u16 len,
				const void *data, u32 *status)
{
	struct iwl_host_cmd cmd = {
		.id = id,
		.len = { len, },
		.data = { data, },
	};

	return iwl_mvm_send_cmd_status(mvm, &cmd, status);
}

int iwl_mvm_legacy_hw_idx_to_mac80211_idx(u32 rate_n_flags,
					  enum nl80211_band band)
{
	int format = rate_n_flags & RATE_MCS_MOD_TYPE_MSK;
	int rate = rate_n_flags & RATE_LEGACY_RATE_MSK;
	bool is_LB = band == NL80211_BAND_2GHZ;

	if (format == RATE_MCS_LEGACY_OFDM_MSK)
		return is_LB ? rate + IWL_FIRST_OFDM_RATE :
			rate;

	/* CCK is not allowed in HB */
	return is_LB ? rate : -1;
}

int iwl_mvm_legacy_rate_to_mac80211_idx(u32 rate_n_flags,
					enum nl80211_band band)
{
	int rate = rate_n_flags & RATE_LEGACY_RATE_MSK_V1;
	int idx;
	int band_offset = 0;

	/* Legacy rate format, search for match in table */
	if (band != NL80211_BAND_2GHZ)
		band_offset = IWL_FIRST_OFDM_RATE;
	for (idx = band_offset; idx < IWL_RATE_COUNT_LEGACY; idx++)
		if (iwl_fw_rate_idx_to_plcp(idx) == rate)
			return idx - band_offset;

	return -1;
}

u8 iwl_mvm_mac80211_idx_to_hwrate(const struct iwl_fw *fw, int rate_idx)
{
	if (iwl_fw_lookup_cmd_ver(fw, LONG_GROUP,
				  TX_CMD, 0) > 8)
		/* In the new rate legacy rates are indexed:
		 * 0 - 3 for CCK and 0 - 7 for OFDM.
		 */
		return (rate_idx >= IWL_FIRST_OFDM_RATE ?
			rate_idx - IWL_FIRST_OFDM_RATE :
			rate_idx);

	return iwl_fw_rate_idx_to_plcp(rate_idx);
}

u8 iwl_mvm_mac80211_ac_to_ucode_ac(enum ieee80211_ac_numbers ac)
{
	static const u8 mac80211_ac_to_ucode_ac[] = {
		AC_VO,
		AC_VI,
		AC_BE,
		AC_BK
	};

	return mac80211_ac_to_ucode_ac[ac];
}

void iwl_mvm_rx_fw_error(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_error_resp *err_resp = (void *)pkt->data;

	IWL_ERR(mvm, "FW Error notification: type 0x%08X cmd_id 0x%02X\n",
		le32_to_cpu(err_resp->error_type), err_resp->cmd_id);
	IWL_ERR(mvm, "FW Error notification: seq 0x%04X service 0x%08X\n",
		le16_to_cpu(err_resp->bad_cmd_seq_num),
		le32_to_cpu(err_resp->error_service));
	IWL_ERR(mvm, "FW Error notification: timestamp 0x%016llX\n",
		le64_to_cpu(err_resp->timestamp));
}

/*
 * Returns the first antenna as ANT_[ABC], as defined in iwl-config.h.
 * The parameter should also be a combination of ANT_[ABC].
 */
u8 first_antenna(u8 mask)
{
	BUILD_BUG_ON(ANT_A != BIT(0)); /* using ffs is wrong if not */
	if (WARN_ON_ONCE(!mask)) /* ffs will return 0 if mask is zeroed */
		return BIT(0);
	return BIT(ffs(mask) - 1);
}

#define MAX_ANT_NUM 2
/*
 * Toggles between TX antennas to send the probe request on.
 * Receives the bitmask of valid TX antennas and the *index* used
 * for the last TX, and returns the next valid *index* to use.
 * In order to set it in the tx_cmd, must do BIT(idx).
 */
u8 iwl_mvm_next_antenna(struct iwl_mvm *mvm, u8 valid, u8 last_idx)
{
	u8 ind = last_idx;
	int i;

	for (i = 0; i < MAX_ANT_NUM; i++) {
		ind = (ind + 1) % MAX_ANT_NUM;
		if (valid & BIT(ind))
			return ind;
	}

	WARN_ONCE(1, "Failed to toggle between antennas 0x%x", valid);
	return last_idx;
}

int iwl_mvm_reconfig_scd(struct iwl_mvm *mvm, int queue, int fifo, int sta_id,
			 int tid, int frame_limit, u16 ssn)
{
	struct iwl_scd_txq_cfg_cmd cmd = {
		.scd_queue = queue,
		.action = SCD_CFG_ENABLE_QUEUE,
		.window = frame_limit,
		.sta_id = sta_id,
		.ssn = cpu_to_le16(ssn),
		.tx_fifo = fifo,
		.aggregate = (queue >= IWL_MVM_DQA_MIN_DATA_QUEUE ||
			      queue == IWL_MVM_DQA_BSS_CLIENT_QUEUE),
		.tid = tid,
	};
	int ret;

	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return -EINVAL;

	if (WARN(mvm->queue_info[queue].tid_bitmap == 0,
		 "Trying to reconfig unallocated queue %d\n", queue))
		return -ENXIO;

	IWL_DEBUG_TX_QUEUES(mvm, "Reconfig SCD for TXQ #%d\n", queue);

	ret = iwl_mvm_send_cmd_pdu(mvm, SCD_QUEUE_CFG, 0, sizeof(cmd), &cmd);
	WARN_ONCE(ret, "Failed to re-configure queue %d on FIFO %d, ret=%d\n",
		  queue, fifo, ret);

	return ret;
}

/**
 * iwl_mvm_send_lq_cmd() - Send link quality command
 * @mvm: Driver data.
 * @lq: Link quality command to send.
 *
 * The link quality command is sent as the last step of station creation.
 * This is the special case in which init is set and we call a callback in
 * this case to clear the state indicating that station creation is in
 * progress.
 */
int iwl_mvm_send_lq_cmd(struct iwl_mvm *mvm, struct iwl_lq_cmd *lq)
{
	struct iwl_host_cmd cmd = {
		.id = LQ_CMD,
		.len = { sizeof(struct iwl_lq_cmd), },
		.flags = CMD_ASYNC,
		.data = { lq, },
	};

	if (WARN_ON(lq->sta_id == IWL_MVM_INVALID_STA ||
		    iwl_mvm_has_tlc_offload(mvm)))
		return -EINVAL;

	return iwl_mvm_send_cmd(mvm, &cmd);
}

/**
 * iwl_mvm_update_smps - Get a request to change the SMPS mode
 * @mvm: Driver data.
 * @vif: Pointer to the ieee80211_vif structure
 * @req_type: The part of the driver who call for a change.
 * @smps_request: The request to change the SMPS mode.
 *
 * Get a requst to change the SMPS mode,
 * and change it according to all other requests in the driver.
 */
void iwl_mvm_update_smps(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			 enum iwl_mvm_smps_type_request req_type,
			 enum ieee80211_smps_mode smps_request)
{
	struct iwl_mvm_vif *mvmvif;
	enum ieee80211_smps_mode smps_mode = IEEE80211_SMPS_AUTOMATIC;
	int i;

	lockdep_assert_held(&mvm->mutex);

	/* SMPS is irrelevant for NICs that don't have at least 2 RX antenna */
	if (num_of_ant(iwl_mvm_get_valid_rx_ant(mvm)) == 1)
		return;

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	mvmvif = iwl_mvm_vif_from_mac80211(vif);
	mvmvif->smps_requests[req_type] = smps_request;
	for (i = 0; i < NUM_IWL_MVM_SMPS_REQ; i++) {
		if (mvmvif->smps_requests[i] == IEEE80211_SMPS_STATIC) {
			smps_mode = IEEE80211_SMPS_STATIC;
			break;
		}
		if (mvmvif->smps_requests[i] == IEEE80211_SMPS_DYNAMIC)
			smps_mode = IEEE80211_SMPS_DYNAMIC;
	}

	ieee80211_request_smps(vif, smps_mode);
}

static bool iwl_wait_stats_complete(struct iwl_notif_wait_data *notif_wait,
				    struct iwl_rx_packet *pkt, void *data)
{
	WARN_ON(pkt->hdr.cmd != STATISTICS_NOTIFICATION);

	return true;
}

int iwl_mvm_request_statistics(struct iwl_mvm *mvm, bool clear)
{
	struct iwl_statistics_cmd scmd = {
		.flags = clear ? cpu_to_le32(IWL_STATISTICS_FLG_CLEAR) : 0,
	};

	struct iwl_host_cmd cmd = {
		.id = STATISTICS_CMD,
		.len[0] = sizeof(scmd),
		.data[0] = &scmd,
	};
	int ret;

	/* From version 15 - STATISTICS_NOTIFICATION, the reply for
	 * STATISTICS_CMD is empty, and the response is with
	 * STATISTICS_NOTIFICATION notification
	 */
	if (iwl_fw_lookup_notif_ver(mvm->fw, LEGACY_GROUP,
				    STATISTICS_NOTIFICATION, 0) < 15) {
		cmd.flags = CMD_WANT_SKB;

		ret = iwl_mvm_send_cmd(mvm, &cmd);
		if (ret)
			return ret;

		iwl_mvm_handle_rx_statistics(mvm, cmd.resp_pkt);
		iwl_free_resp(&cmd);
	} else {
		struct iwl_notification_wait stats_wait;
		static const u16 stats_complete[] = {
			STATISTICS_NOTIFICATION,
		};

		iwl_init_notification_wait(&mvm->notif_wait, &stats_wait,
					   stats_complete, ARRAY_SIZE(stats_complete),
					   iwl_wait_stats_complete, NULL);

		ret = iwl_mvm_send_cmd(mvm, &cmd);
		if (ret) {
			iwl_remove_notification(&mvm->notif_wait, &stats_wait);
			return ret;
		}

		/* 200ms should be enough for FW to collect data from all
		 * LMACs and send STATISTICS_NOTIFICATION to host
		 */
		ret = iwl_wait_notification(&mvm->notif_wait, &stats_wait, HZ / 5);
		if (ret)
			return ret;
	}

	if (clear)
		iwl_mvm_accu_radio_stats(mvm);

	return 0;
}

void iwl_mvm_accu_radio_stats(struct iwl_mvm *mvm)
{
	mvm->accu_radio_stats.rx_time += mvm->radio_stats.rx_time;
	mvm->accu_radio_stats.tx_time += mvm->radio_stats.tx_time;
	mvm->accu_radio_stats.on_time_rf += mvm->radio_stats.on_time_rf;
	mvm->accu_radio_stats.on_time_scan += mvm->radio_stats.on_time_scan;
}

struct iwl_mvm_diversity_iter_data {
	struct iwl_mvm_phy_ctxt *ctxt;
	bool result;
};

static void iwl_mvm_diversity_iter(void *_data, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_diversity_iter_data *data = _data;
	int i;

	if (mvmvif->phy_ctxt != data->ctxt)
		return;

	for (i = 0; i < NUM_IWL_MVM_SMPS_REQ; i++) {
		if (mvmvif->smps_requests[i] == IEEE80211_SMPS_STATIC ||
		    mvmvif->smps_requests[i] == IEEE80211_SMPS_DYNAMIC) {
			data->result = false;
			break;
		}
	}
}

bool iwl_mvm_rx_diversity_allowed(struct iwl_mvm *mvm,
				  struct iwl_mvm_phy_ctxt *ctxt)
{
	struct iwl_mvm_diversity_iter_data data = {
		.ctxt = ctxt,
		.result = true,
	};

	lockdep_assert_held(&mvm->mutex);

	if (iwlmvm_mod_params.power_scheme != IWL_POWER_SCHEME_CAM)
		return false;

	if (num_of_ant(iwl_mvm_get_valid_rx_ant(mvm)) == 1)
		return false;

	if (mvm->cfg->rx_with_siso_diversity)
		return false;

	ieee80211_iterate_active_interfaces_atomic(
			mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
			iwl_mvm_diversity_iter, &data);

	return data.result;
}

void iwl_mvm_send_low_latency_cmd(struct iwl_mvm *mvm,
				  bool low_latency, u16 mac_id)
{
	struct iwl_mac_low_latency_cmd cmd = {
		.mac_id = cpu_to_le32(mac_id)
	};

	if (!fw_has_capa(&mvm->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_DYNAMIC_QUOTA))
		return;

	if (low_latency) {
		/* currently we don't care about the direction */
		cmd.low_latency_rx = 1;
		cmd.low_latency_tx = 1;
	}

	if (iwl_mvm_send_cmd_pdu(mvm, iwl_cmd_id(LOW_LATENCY_CMD,
						 MAC_CONF_GROUP, 0),
				 0, sizeof(cmd), &cmd))
		IWL_ERR(mvm, "Failed to send low latency command\n");
}

int iwl_mvm_update_low_latency(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			       bool low_latency,
			       enum iwl_mvm_low_latency_cause cause)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int res;
	bool prev;

	lockdep_assert_held(&mvm->mutex);

	prev = iwl_mvm_vif_low_latency(mvmvif);
	iwl_mvm_vif_set_low_latency(mvmvif, low_latency, cause);

	low_latency = iwl_mvm_vif_low_latency(mvmvif);

	if (low_latency == prev)
		return 0;

	iwl_mvm_send_low_latency_cmd(mvm, low_latency, mvmvif->id);

	res = iwl_mvm_update_quotas(mvm, false, NULL);
	if (res)
		return res;

	iwl_mvm_bt_coex_vif_change(mvm);

	return iwl_mvm_power_update_mac(mvm);
}

struct iwl_mvm_low_latency_iter {
	bool result;
	bool result_per_band[NUM_NL80211_BANDS];
};

static void iwl_mvm_ll_iter(void *_data, u8 *mac, struct ieee80211_vif *vif)
{
	struct iwl_mvm_low_latency_iter *result = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	enum nl80211_band band;

	if (iwl_mvm_vif_low_latency(mvmvif)) {
		result->result = true;

		if (!mvmvif->phy_ctxt)
			return;

		band = mvmvif->phy_ctxt->channel->band;
		result->result_per_band[band] = true;
	}
}

bool iwl_mvm_low_latency(struct iwl_mvm *mvm)
{
	struct iwl_mvm_low_latency_iter data = {};

	ieee80211_iterate_active_interfaces_atomic(
			mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
			iwl_mvm_ll_iter, &data);

	return data.result;
}

bool iwl_mvm_low_latency_band(struct iwl_mvm *mvm, enum nl80211_band band)
{
	struct iwl_mvm_low_latency_iter data = {};

	ieee80211_iterate_active_interfaces_atomic(
			mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
			iwl_mvm_ll_iter, &data);

	return data.result_per_band[band];
}

struct iwl_bss_iter_data {
	struct ieee80211_vif *vif;
	bool error;
};

static void iwl_mvm_bss_iface_iterator(void *_data, u8 *mac,
				       struct ieee80211_vif *vif)
{
	struct iwl_bss_iter_data *data = _data;

	if (vif->type != NL80211_IFTYPE_STATION || vif->p2p)
		return;

	if (data->vif) {
		data->error = true;
		return;
	}

	data->vif = vif;
}

struct ieee80211_vif *iwl_mvm_get_bss_vif(struct iwl_mvm *mvm)
{
	struct iwl_bss_iter_data bss_iter_data = {};

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_bss_iface_iterator, &bss_iter_data);

	if (bss_iter_data.error) {
		IWL_ERR(mvm, "More than one managed interface active!\n");
		return ERR_PTR(-EINVAL);
	}

	return bss_iter_data.vif;
}

struct iwl_bss_find_iter_data {
	struct ieee80211_vif *vif;
	u32 macid;
};

static void iwl_mvm_bss_find_iface_iterator(void *_data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	struct iwl_bss_find_iter_data *data = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (mvmvif->id == data->macid)
		data->vif = vif;
}

struct ieee80211_vif *iwl_mvm_get_vif_by_macid(struct iwl_mvm *mvm, u32 macid)
{
	struct iwl_bss_find_iter_data data = {
		.macid = macid,
	};

	lockdep_assert_held(&mvm->mutex);

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_bss_find_iface_iterator, &data);

	return data.vif;
}

struct iwl_sta_iter_data {
	bool assoc;
};

static void iwl_mvm_sta_iface_iterator(void *_data, u8 *mac,
				       struct ieee80211_vif *vif)
{
	struct iwl_sta_iter_data *data = _data;

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	if (vif->bss_conf.assoc)
		data->assoc = true;
}

bool iwl_mvm_is_vif_assoc(struct iwl_mvm *mvm)
{
	struct iwl_sta_iter_data data = {
		.assoc = false,
	};

	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   iwl_mvm_sta_iface_iterator,
						   &data);
	return data.assoc;
}

unsigned int iwl_mvm_get_wd_timeout(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    bool tdls, bool cmd_q)
{
	struct iwl_fw_dbg_trigger_tlv *trigger;
	struct iwl_fw_dbg_trigger_txq_timer *txq_timer;
	unsigned int default_timeout = cmd_q ?
		IWL_DEF_WD_TIMEOUT :
		mvm->trans->trans_cfg->base_params->wd_timeout;

	if (!iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_TXQ_TIMERS)) {
		/*
		 * We can't know when the station is asleep or awake, so we
		 * must disable the queue hang detection.
		 */
		if (fw_has_capa(&mvm->fw->ucode_capa,
				IWL_UCODE_TLV_CAPA_STA_PM_NOTIF) &&
		    vif && vif->type == NL80211_IFTYPE_AP)
			return IWL_WATCHDOG_DISABLED;
		return default_timeout;
	}

	trigger = iwl_fw_dbg_get_trigger(mvm->fw, FW_DBG_TRIGGER_TXQ_TIMERS);
	txq_timer = (void *)trigger->data;

	if (tdls)
		return le32_to_cpu(txq_timer->tdls);

	if (cmd_q)
		return le32_to_cpu(txq_timer->command_queue);

	if (WARN_ON(!vif))
		return default_timeout;

	switch (ieee80211_vif_type_p2p(vif)) {
	case NL80211_IFTYPE_ADHOC:
		return le32_to_cpu(txq_timer->ibss);
	case NL80211_IFTYPE_STATION:
		return le32_to_cpu(txq_timer->bss);
	case NL80211_IFTYPE_AP:
		return le32_to_cpu(txq_timer->softap);
	case NL80211_IFTYPE_P2P_CLIENT:
		return le32_to_cpu(txq_timer->p2p_client);
	case NL80211_IFTYPE_P2P_GO:
		return le32_to_cpu(txq_timer->p2p_go);
	case NL80211_IFTYPE_P2P_DEVICE:
		return le32_to_cpu(txq_timer->p2p_device);
	case NL80211_IFTYPE_MONITOR:
		return default_timeout;
	default:
		WARN_ON(1);
		return mvm->trans->trans_cfg->base_params->wd_timeout;
	}
}

void iwl_mvm_connection_loss(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     const char *errmsg)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_mlme *trig_mlme;

	trig = iwl_fw_dbg_trigger_on(&mvm->fwrt, ieee80211_vif_to_wdev(vif),
				     FW_DBG_TRIGGER_MLME);
	if (!trig)
		goto out;

	trig_mlme = (void *)trig->data;

	if (trig_mlme->stop_connection_loss &&
	    --trig_mlme->stop_connection_loss)
		goto out;

	iwl_fw_dbg_collect_trig(&mvm->fwrt, trig, "%s", errmsg);

out:
	ieee80211_connection_loss(vif);
}

void iwl_mvm_event_frame_timeout_callback(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  const struct ieee80211_sta *sta,
					  u16 tid)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_ba *ba_trig;

	trig = iwl_fw_dbg_trigger_on(&mvm->fwrt, ieee80211_vif_to_wdev(vif),
				     FW_DBG_TRIGGER_BA);
	if (!trig)
		return;

	ba_trig = (void *)trig->data;

	if (!(le16_to_cpu(ba_trig->frame_timeout) & BIT(tid)))
		return;

	iwl_fw_dbg_collect_trig(&mvm->fwrt, trig,
				"Frame from %pM timed out, tid %d",
				sta->addr, tid);
}

u8 iwl_mvm_tcm_load_percentage(u32 airtime, u32 elapsed)
{
	if (!elapsed)
		return 0;

	return (100 * airtime / elapsed) / USEC_PER_MSEC;
}

static enum iwl_mvm_traffic_load
iwl_mvm_tcm_load(struct iwl_mvm *mvm, u32 airtime, unsigned long elapsed)
{
	u8 load = iwl_mvm_tcm_load_percentage(airtime, elapsed);

	if (load > IWL_MVM_TCM_LOAD_HIGH_THRESH)
		return IWL_MVM_TRAFFIC_HIGH;
	if (load > IWL_MVM_TCM_LOAD_MEDIUM_THRESH)
		return IWL_MVM_TRAFFIC_MEDIUM;

	return IWL_MVM_TRAFFIC_LOW;
}

static void iwl_mvm_tcm_iter(void *_data, u8 *mac, struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	bool low_latency, prev = mvmvif->low_latency & LOW_LATENCY_TRAFFIC;

	if (mvmvif->id >= NUM_MAC_INDEX_DRIVER)
		return;

	low_latency = mvm->tcm.result.low_latency[mvmvif->id];

	if (!mvm->tcm.result.change[mvmvif->id] &&
	    prev == low_latency) {
		iwl_mvm_update_quotas(mvm, false, NULL);
		return;
	}

	if (prev != low_latency) {
		/* this sends traffic load and updates quota as well */
		iwl_mvm_update_low_latency(mvm, vif, low_latency,
					   LOW_LATENCY_TRAFFIC);
	} else {
		iwl_mvm_update_quotas(mvm, false, NULL);
	}
}

static void iwl_mvm_tcm_results(struct iwl_mvm *mvm)
{
	mutex_lock(&mvm->mutex);

	ieee80211_iterate_active_interfaces(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_tcm_iter, mvm);

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN))
		iwl_mvm_config_scan(mvm);

	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_tcm_uapsd_nonagg_detected_wk(struct work_struct *wk)
{
	struct iwl_mvm *mvm;
	struct iwl_mvm_vif *mvmvif;
	struct ieee80211_vif *vif;

	mvmvif = container_of(wk, struct iwl_mvm_vif,
			      uapsd_nonagg_detected_wk.work);
	vif = container_of((void *)mvmvif, struct ieee80211_vif, drv_priv);
	mvm = mvmvif->mvm;

	if (mvm->tcm.data[mvmvif->id].opened_rx_ba_sessions)
		return;

	/* remember that this AP is broken */
	memcpy(mvm->uapsd_noagg_bssids[mvm->uapsd_noagg_bssid_write_idx].addr,
	       vif->bss_conf.bssid, ETH_ALEN);
	mvm->uapsd_noagg_bssid_write_idx++;
	if (mvm->uapsd_noagg_bssid_write_idx >= IWL_MVM_UAPSD_NOAGG_LIST_LEN)
		mvm->uapsd_noagg_bssid_write_idx = 0;

	iwl_mvm_connection_loss(mvm, vif,
				"AP isn't using AMPDU with uAPSD enabled");
}

static void iwl_mvm_uapsd_agg_disconnect(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	if (!vif->bss_conf.assoc)
		return;

	if (!mvmvif->queue_params[IEEE80211_AC_VO].uapsd &&
	    !mvmvif->queue_params[IEEE80211_AC_VI].uapsd &&
	    !mvmvif->queue_params[IEEE80211_AC_BE].uapsd &&
	    !mvmvif->queue_params[IEEE80211_AC_BK].uapsd)
		return;

	if (mvm->tcm.data[mvmvif->id].uapsd_nonagg_detect.detected)
		return;

	mvm->tcm.data[mvmvif->id].uapsd_nonagg_detect.detected = true;
	IWL_INFO(mvm,
		 "detected AP should do aggregation but isn't, likely due to U-APSD\n");
	schedule_delayed_work(&mvmvif->uapsd_nonagg_detected_wk, 15 * HZ);
}

static void iwl_mvm_check_uapsd_agg_expected_tpt(struct iwl_mvm *mvm,
						 unsigned int elapsed,
						 int mac)
{
	u64 bytes = mvm->tcm.data[mac].uapsd_nonagg_detect.rx_bytes;
	u64 tpt;
	unsigned long rate;
	struct ieee80211_vif *vif;

	rate = ewma_rate_read(&mvm->tcm.data[mac].uapsd_nonagg_detect.rate);

	if (!rate || mvm->tcm.data[mac].opened_rx_ba_sessions ||
	    mvm->tcm.data[mac].uapsd_nonagg_detect.detected)
		return;

	if (iwl_mvm_has_new_rx_api(mvm)) {
		tpt = 8 * bytes; /* kbps */
		do_div(tpt, elapsed);
		rate *= 1000; /* kbps */
		if (tpt < 22 * rate / 100)
			return;
	} else {
		/*
		 * the rate here is actually the threshold, in 100Kbps units,
		 * so do the needed conversion from bytes to 100Kbps:
		 * 100kb = bits / (100 * 1000),
		 * 100kbps = 100kb / (msecs / 1000) ==
		 *           (bits / (100 * 1000)) / (msecs / 1000) ==
		 *           bits / (100 * msecs)
		 */
		tpt = (8 * bytes);
		do_div(tpt, elapsed * 100);
		if (tpt < rate)
			return;
	}

	rcu_read_lock();
	vif = rcu_dereference(mvm->vif_id_to_mac[mac]);
	if (vif)
		iwl_mvm_uapsd_agg_disconnect(mvm, vif);
	rcu_read_unlock();
}

static void iwl_mvm_tcm_iterator(void *_data, u8 *mac,
				 struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	u32 *band = _data;

	if (!mvmvif->phy_ctxt)
		return;

	band[mvmvif->id] = mvmvif->phy_ctxt->channel->band;
}

static unsigned long iwl_mvm_calc_tcm_stats(struct iwl_mvm *mvm,
					    unsigned long ts,
					    bool handle_uapsd)
{
	unsigned int elapsed = jiffies_to_msecs(ts - mvm->tcm.ts);
	unsigned int uapsd_elapsed =
		jiffies_to_msecs(ts - mvm->tcm.uapsd_nonagg_ts);
	u32 total_airtime = 0;
	u32 band_airtime[NUM_NL80211_BANDS] = {0};
	u32 band[NUM_MAC_INDEX_DRIVER] = {0};
	int ac, mac, i;
	bool low_latency = false;
	enum iwl_mvm_traffic_load load, band_load;
	bool handle_ll = time_after(ts, mvm->tcm.ll_ts + MVM_LL_PERIOD);

	if (handle_ll)
		mvm->tcm.ll_ts = ts;
	if (handle_uapsd)
		mvm->tcm.uapsd_nonagg_ts = ts;

	mvm->tcm.result.elapsed = elapsed;

	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   iwl_mvm_tcm_iterator,
						   &band);

	for (mac = 0; mac < NUM_MAC_INDEX_DRIVER; mac++) {
		struct iwl_mvm_tcm_mac *mdata = &mvm->tcm.data[mac];
		u32 vo_vi_pkts = 0;
		u32 airtime = mdata->rx.airtime + mdata->tx.airtime;

		total_airtime += airtime;
		band_airtime[band[mac]] += airtime;

		load = iwl_mvm_tcm_load(mvm, airtime, elapsed);
		mvm->tcm.result.change[mac] = load != mvm->tcm.result.load[mac];
		mvm->tcm.result.load[mac] = load;
		mvm->tcm.result.airtime[mac] = airtime;

		for (ac = IEEE80211_AC_VO; ac <= IEEE80211_AC_VI; ac++)
			vo_vi_pkts += mdata->rx.pkts[ac] +
				      mdata->tx.pkts[ac];

		/* enable immediately with enough packets but defer disabling */
		if (vo_vi_pkts > IWL_MVM_TCM_LOWLAT_ENABLE_THRESH)
			mvm->tcm.result.low_latency[mac] = true;
		else if (handle_ll)
			mvm->tcm.result.low_latency[mac] = false;

		if (handle_ll) {
			/* clear old data */
			memset(&mdata->rx.pkts, 0, sizeof(mdata->rx.pkts));
			memset(&mdata->tx.pkts, 0, sizeof(mdata->tx.pkts));
		}
		low_latency |= mvm->tcm.result.low_latency[mac];

		if (!mvm->tcm.result.low_latency[mac] && handle_uapsd)
			iwl_mvm_check_uapsd_agg_expected_tpt(mvm, uapsd_elapsed,
							     mac);
		/* clear old data */
		if (handle_uapsd)
			mdata->uapsd_nonagg_detect.rx_bytes = 0;
		memset(&mdata->rx.airtime, 0, sizeof(mdata->rx.airtime));
		memset(&mdata->tx.airtime, 0, sizeof(mdata->tx.airtime));
	}

	load = iwl_mvm_tcm_load(mvm, total_airtime, elapsed);
	mvm->tcm.result.global_load = load;

	for (i = 0; i < NUM_NL80211_BANDS; i++) {
		band_load = iwl_mvm_tcm_load(mvm, band_airtime[i], elapsed);
		mvm->tcm.result.band_load[i] = band_load;
	}

	/*
	 * If the current load isn't low we need to force re-evaluation
	 * in the TCM period, so that we can return to low load if there
	 * was no traffic at all (and thus iwl_mvm_recalc_tcm didn't get
	 * triggered by traffic).
	 */
	if (load != IWL_MVM_TRAFFIC_LOW)
		return MVM_TCM_PERIOD;
	/*
	 * If low-latency is active we need to force re-evaluation after
	 * (the longer) MVM_LL_PERIOD, so that we can disable low-latency
	 * when there's no traffic at all.
	 */
	if (low_latency)
		return MVM_LL_PERIOD;
	/*
	 * Otherwise, we don't need to run the work struct because we're
	 * in the default "idle" state - traffic indication is low (which
	 * also covers the "no traffic" case) and low-latency is disabled
	 * so there's no state that may need to be disabled when there's
	 * no traffic at all.
	 *
	 * Note that this has no impact on the regular scheduling of the
	 * updates triggered by traffic - those happen whenever one of the
	 * two timeouts expire (if there's traffic at all.)
	 */
	return 0;
}

void iwl_mvm_recalc_tcm(struct iwl_mvm *mvm)
{
	unsigned long ts = jiffies;
	bool handle_uapsd =
		time_after(ts, mvm->tcm.uapsd_nonagg_ts +
			       msecs_to_jiffies(IWL_MVM_UAPSD_NONAGG_PERIOD));

	spin_lock(&mvm->tcm.lock);
	if (mvm->tcm.paused || !time_after(ts, mvm->tcm.ts + MVM_TCM_PERIOD)) {
		spin_unlock(&mvm->tcm.lock);
		return;
	}
	spin_unlock(&mvm->tcm.lock);

	if (handle_uapsd && iwl_mvm_has_new_rx_api(mvm)) {
		mutex_lock(&mvm->mutex);
		if (iwl_mvm_request_statistics(mvm, true))
			handle_uapsd = false;
		mutex_unlock(&mvm->mutex);
	}

	spin_lock(&mvm->tcm.lock);
	/* re-check if somebody else won the recheck race */
	if (!mvm->tcm.paused && time_after(ts, mvm->tcm.ts + MVM_TCM_PERIOD)) {
		/* calculate statistics */
		unsigned long work_delay = iwl_mvm_calc_tcm_stats(mvm, ts,
								  handle_uapsd);

		/* the memset needs to be visible before the timestamp */
		smp_mb();
		mvm->tcm.ts = ts;
		if (work_delay)
			schedule_delayed_work(&mvm->tcm.work, work_delay);
	}
	spin_unlock(&mvm->tcm.lock);

	iwl_mvm_tcm_results(mvm);
}

void iwl_mvm_tcm_work(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct iwl_mvm *mvm = container_of(delayed_work, struct iwl_mvm,
					   tcm.work);

	iwl_mvm_recalc_tcm(mvm);
}

void iwl_mvm_pause_tcm(struct iwl_mvm *mvm, bool with_cancel)
{
	spin_lock_bh(&mvm->tcm.lock);
	mvm->tcm.paused = true;
	spin_unlock_bh(&mvm->tcm.lock);
	if (with_cancel)
		cancel_delayed_work_sync(&mvm->tcm.work);
}

void iwl_mvm_resume_tcm(struct iwl_mvm *mvm)
{
	int mac;
	bool low_latency = false;

	spin_lock_bh(&mvm->tcm.lock);
	mvm->tcm.ts = jiffies;
	mvm->tcm.ll_ts = jiffies;
	for (mac = 0; mac < NUM_MAC_INDEX_DRIVER; mac++) {
		struct iwl_mvm_tcm_mac *mdata = &mvm->tcm.data[mac];

		memset(&mdata->rx.pkts, 0, sizeof(mdata->rx.pkts));
		memset(&mdata->tx.pkts, 0, sizeof(mdata->tx.pkts));
		memset(&mdata->rx.airtime, 0, sizeof(mdata->rx.airtime));
		memset(&mdata->tx.airtime, 0, sizeof(mdata->tx.airtime));

		if (mvm->tcm.result.low_latency[mac])
			low_latency = true;
	}
	/* The TCM data needs to be reset before "paused" flag changes */
	smp_mb();
	mvm->tcm.paused = false;

	/*
	 * if the current load is not low or low latency is active, force
	 * re-evaluation to cover the case of no traffic.
	 */
	if (mvm->tcm.result.global_load > IWL_MVM_TRAFFIC_LOW)
		schedule_delayed_work(&mvm->tcm.work, MVM_TCM_PERIOD);
	else if (low_latency)
		schedule_delayed_work(&mvm->tcm.work, MVM_LL_PERIOD);

	spin_unlock_bh(&mvm->tcm.lock);
}

void iwl_mvm_tcm_add_vif(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	INIT_DELAYED_WORK(&mvmvif->uapsd_nonagg_detected_wk,
			  iwl_mvm_tcm_uapsd_nonagg_detected_wk);
}

void iwl_mvm_tcm_rm_vif(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	cancel_delayed_work_sync(&mvmvif->uapsd_nonagg_detected_wk);
}

u32 iwl_mvm_get_systime(struct iwl_mvm *mvm)
{
	u32 reg_addr = DEVICE_SYSTEM_TIME_REG;

	if (mvm->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_22000 &&
	    mvm->trans->cfg->gp2_reg_addr)
		reg_addr = mvm->trans->cfg->gp2_reg_addr;

	return iwl_read_prph(mvm->trans, reg_addr);
}

void iwl_mvm_get_sync_time(struct iwl_mvm *mvm, int clock_type,
			   u32 *gp2, u64 *boottime, ktime_t *realtime)
{
	bool ps_disabled;

	lockdep_assert_held(&mvm->mutex);

	/* Disable power save when reading GP2 */
	ps_disabled = mvm->ps_disabled;
	if (!ps_disabled) {
		mvm->ps_disabled = true;
		iwl_mvm_power_update_device(mvm);
	}

	*gp2 = iwl_mvm_get_systime(mvm);

	if (clock_type == CLOCK_BOOTTIME && boottime)
		*boottime = ktime_get_boottime_ns();
	else if (clock_type == CLOCK_REALTIME && realtime)
		*realtime = ktime_get_real();

	if (!ps_disabled) {
		mvm->ps_disabled = ps_disabled;
		iwl_mvm_power_update_device(mvm);
	}
}
