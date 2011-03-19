/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 */

#include <linux/kernel.h>
#include <linux/wireless.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "iwm.h"
#include "bus.h"
#include "hal.h"
#include "umac.h"
#include "commands.h"
#include "debug.h"

static int iwm_send_lmac_ptrough_cmd(struct iwm_priv *iwm,
				     u8 lmac_cmd_id,
				     const void *lmac_payload,
				     u16 lmac_payload_size,
				     u8 resp)
{
	struct iwm_udma_wifi_cmd udma_cmd = UDMA_LMAC_INIT;
	struct iwm_umac_cmd umac_cmd;
	struct iwm_lmac_cmd lmac_cmd;

	lmac_cmd.id = lmac_cmd_id;

	umac_cmd.id = UMAC_CMD_OPCODE_WIFI_PASS_THROUGH;
	umac_cmd.resp = resp;

	return iwm_hal_send_host_cmd(iwm, &udma_cmd, &umac_cmd, &lmac_cmd,
				     lmac_payload, lmac_payload_size);
}

int iwm_send_wifi_if_cmd(struct iwm_priv *iwm, void *payload, u16 payload_size,
			 bool resp)
{
	struct iwm_umac_wifi_if *hdr = (struct iwm_umac_wifi_if *)payload;
	struct iwm_udma_wifi_cmd udma_cmd = UDMA_UMAC_INIT;
	struct iwm_umac_cmd umac_cmd;
	int ret;
	u8 oid = hdr->oid;

	if (!test_bit(IWM_STATUS_READY, &iwm->status)) {
		IWM_ERR(iwm, "Interface is not ready yet");
		return -EAGAIN;
	}

	umac_cmd.id = UMAC_CMD_OPCODE_WIFI_IF_WRAPPER;
	umac_cmd.resp = resp;

	ret = iwm_hal_send_umac_cmd(iwm, &udma_cmd, &umac_cmd,
				    payload, payload_size);

	if (resp) {
		ret = wait_event_interruptible_timeout(iwm->wifi_ntfy_queue,
				   test_and_clear_bit(oid, &iwm->wifi_ntfy[0]),
				   3 * HZ);

		return ret ? 0 : -EBUSY;
	}

	return ret;
}

static int modparam_wiwi = COEX_MODE_CM;
module_param_named(wiwi, modparam_wiwi, int, 0644);
MODULE_PARM_DESC(wiwi, "Wifi-WiMAX coexistence: 1=SA, 2=XOR, 3=CM (default)");

static struct coex_event iwm_sta_xor_prio_tbl[COEX_EVENTS_NUM] =
{
	{4, 3, 0, COEX_UNASSOC_IDLE_FLAGS},
	{4, 3, 0, COEX_UNASSOC_MANUAL_SCAN_FLAGS},
	{4, 3, 0, COEX_UNASSOC_AUTO_SCAN_FLAGS},
	{4, 3, 0, COEX_CALIBRATION_FLAGS},
	{4, 3, 0, COEX_PERIODIC_CALIBRATION_FLAGS},
	{4, 3, 0, COEX_CONNECTION_ESTAB_FLAGS},
	{4, 3, 0, COEX_ASSOCIATED_IDLE_FLAGS},
	{4, 3, 0, COEX_ASSOC_MANUAL_SCAN_FLAGS},
	{4, 3, 0, COEX_ASSOC_AUTO_SCAN_FLAGS},
	{4, 3, 0, COEX_ASSOC_ACTIVE_LEVEL_FLAGS},
	{6, 3, 0, COEX_XOR_RF_ON_FLAGS},
	{4, 3, 0, COEX_RF_OFF_FLAGS},
	{6, 6, 0, COEX_STAND_ALONE_DEBUG_FLAGS},
	{4, 3, 0, COEX_IPAN_ASSOC_LEVEL_FLAGS},
	{4, 3, 0, COEX_RSRVD1_FLAGS},
	{4, 3, 0, COEX_RSRVD2_FLAGS}
};

static struct coex_event iwm_sta_cm_prio_tbl[COEX_EVENTS_NUM] =
{
	{1, 1, 0, COEX_UNASSOC_IDLE_FLAGS},
	{4, 4, 0, COEX_UNASSOC_MANUAL_SCAN_FLAGS},
	{3, 3, 0, COEX_UNASSOC_AUTO_SCAN_FLAGS},
	{6, 6, 0, COEX_CALIBRATION_FLAGS},
	{3, 3, 0, COEX_PERIODIC_CALIBRATION_FLAGS},
	{6, 5, 0, COEX_CONNECTION_ESTAB_FLAGS},
	{4, 4, 0, COEX_ASSOCIATED_IDLE_FLAGS},
	{4, 4, 0, COEX_ASSOC_MANUAL_SCAN_FLAGS},
	{4, 4, 0, COEX_ASSOC_AUTO_SCAN_FLAGS},
	{4, 4, 0, COEX_ASSOC_ACTIVE_LEVEL_FLAGS},
	{1, 1, 0, COEX_RF_ON_FLAGS},
	{1, 1, 0, COEX_RF_OFF_FLAGS},
	{7, 7, 0, COEX_STAND_ALONE_DEBUG_FLAGS},
	{5, 4, 0, COEX_IPAN_ASSOC_LEVEL_FLAGS},
	{1, 1, 0, COEX_RSRVD1_FLAGS},
	{1, 1, 0, COEX_RSRVD2_FLAGS}
};

int iwm_send_prio_table(struct iwm_priv *iwm)
{
	struct iwm_coex_prio_table_cmd coex_table_cmd;
	u32 coex_enabled, mode_enabled;

	memset(&coex_table_cmd, 0, sizeof(struct iwm_coex_prio_table_cmd));

	coex_table_cmd.flags = COEX_FLAGS_STA_TABLE_VALID_MSK;

	switch (modparam_wiwi) {
	case COEX_MODE_XOR:
	case COEX_MODE_CM:
		coex_enabled = 1;
		break;
	default:
		coex_enabled = 0;
		break;
	}

	switch (iwm->conf.mode) {
	case UMAC_MODE_BSS:
	case UMAC_MODE_IBSS:
		mode_enabled = 1;
		break;
	default:
		mode_enabled = 0;
		break;
	}

	if (coex_enabled && mode_enabled) {
		coex_table_cmd.flags |= COEX_FLAGS_COEX_ENABLE_MSK |
					COEX_FLAGS_ASSOC_WAKEUP_UMASK_MSK |
					COEX_FLAGS_UNASSOC_WAKEUP_UMASK_MSK;

		switch (modparam_wiwi) {
		case COEX_MODE_XOR:
			memcpy(coex_table_cmd.sta_prio, iwm_sta_xor_prio_tbl,
			       sizeof(iwm_sta_xor_prio_tbl));
			break;
		case COEX_MODE_CM:
			memcpy(coex_table_cmd.sta_prio, iwm_sta_cm_prio_tbl,
			       sizeof(iwm_sta_cm_prio_tbl));
			break;
		default:
			IWM_ERR(iwm, "Invalid coex_mode 0x%x\n",
				modparam_wiwi);
			break;
		}
	} else
		IWM_WARN(iwm, "coexistense disabled\n");

	return iwm_send_lmac_ptrough_cmd(iwm, COEX_PRIORITY_TABLE_CMD,
				&coex_table_cmd,
				sizeof(struct iwm_coex_prio_table_cmd), 0);
}

int iwm_send_init_calib_cfg(struct iwm_priv *iwm, u8 calib_requested)
{
	struct iwm_lmac_cal_cfg_cmd cal_cfg_cmd;

	memset(&cal_cfg_cmd, 0, sizeof(struct iwm_lmac_cal_cfg_cmd));

	cal_cfg_cmd.ucode_cfg.init.enable = cpu_to_le32(calib_requested);
	cal_cfg_cmd.ucode_cfg.init.start = cpu_to_le32(calib_requested);
	cal_cfg_cmd.ucode_cfg.init.send_res = cpu_to_le32(calib_requested);
	cal_cfg_cmd.ucode_cfg.flags =
		cpu_to_le32(CALIB_CFG_FLAG_SEND_COMPLETE_NTFY_AFTER_MSK);

	return iwm_send_lmac_ptrough_cmd(iwm, CALIBRATION_CFG_CMD, &cal_cfg_cmd,
				sizeof(struct iwm_lmac_cal_cfg_cmd), 1);
}

int iwm_send_periodic_calib_cfg(struct iwm_priv *iwm, u8 calib_requested)
{
	struct iwm_lmac_cal_cfg_cmd cal_cfg_cmd;

	memset(&cal_cfg_cmd, 0, sizeof(struct iwm_lmac_cal_cfg_cmd));

	cal_cfg_cmd.ucode_cfg.periodic.enable = cpu_to_le32(calib_requested);
	cal_cfg_cmd.ucode_cfg.periodic.start = cpu_to_le32(calib_requested);

	return iwm_send_lmac_ptrough_cmd(iwm, CALIBRATION_CFG_CMD, &cal_cfg_cmd,
				sizeof(struct iwm_lmac_cal_cfg_cmd), 0);
}

int iwm_store_rxiq_calib_result(struct iwm_priv *iwm)
{
	struct iwm_calib_rxiq *rxiq;
	u8 *eeprom_rxiq = iwm_eeprom_access(iwm, IWM_EEPROM_CALIB_RXIQ);
	int grplen = sizeof(struct iwm_calib_rxiq_group);

	rxiq = kzalloc(sizeof(struct iwm_calib_rxiq), GFP_KERNEL);
	if (!rxiq) {
		IWM_ERR(iwm, "Couldn't alloc memory for RX IQ\n");
		return -ENOMEM;
	}

	eeprom_rxiq = iwm_eeprom_access(iwm, IWM_EEPROM_CALIB_RXIQ);
	if (IS_ERR(eeprom_rxiq)) {
		IWM_ERR(iwm, "Couldn't access EEPROM RX IQ entry\n");
		kfree(rxiq);
		return PTR_ERR(eeprom_rxiq);
	}

	iwm->calib_res[SHILOH_PHY_CALIBRATE_RX_IQ_CMD].buf = (u8 *)rxiq;
	iwm->calib_res[SHILOH_PHY_CALIBRATE_RX_IQ_CMD].size = sizeof(*rxiq);

	rxiq->hdr.opcode = SHILOH_PHY_CALIBRATE_RX_IQ_CMD;
	rxiq->hdr.first_grp = 0;
	rxiq->hdr.grp_num = 1;
	rxiq->hdr.all_data_valid = 1;

	memcpy(&rxiq->group[0], eeprom_rxiq, 4 * grplen);
	memcpy(&rxiq->group[4], eeprom_rxiq + 6 * grplen, grplen);

	return 0;
}

int iwm_send_calib_results(struct iwm_priv *iwm)
{
	int i, ret = 0;

	for (i = PHY_CALIBRATE_OPCODES_NUM; i < CALIBRATION_CMD_NUM; i++) {
		if (test_bit(i - PHY_CALIBRATE_OPCODES_NUM,
			     &iwm->calib_done_map)) {
			IWM_DBG_CMD(iwm, DBG,
				    "Send calibration %d result\n", i);
			ret |= iwm_send_lmac_ptrough_cmd(iwm,
					REPLY_PHY_CALIBRATION_CMD,
					iwm->calib_res[i].buf,
					iwm->calib_res[i].size, 0);

			kfree(iwm->calib_res[i].buf);
			iwm->calib_res[i].buf = NULL;
			iwm->calib_res[i].size = 0;
		}
	}

	return ret;
}

int iwm_send_ct_kill_cfg(struct iwm_priv *iwm, u8 entry, u8 exit)
{
	struct iwm_ct_kill_cfg_cmd cmd;

	cmd.entry_threshold = entry;
	cmd.exit_threshold = exit;

	return iwm_send_lmac_ptrough_cmd(iwm, REPLY_CT_KILL_CONFIG_CMD, &cmd,
					 sizeof(struct iwm_ct_kill_cfg_cmd), 0);
}

int iwm_send_umac_reset(struct iwm_priv *iwm, __le32 reset_flags, bool resp)
{
	struct iwm_udma_wifi_cmd udma_cmd = UDMA_UMAC_INIT;
	struct iwm_umac_cmd umac_cmd;
	struct iwm_umac_cmd_reset reset;

	reset.flags = reset_flags;

	umac_cmd.id = UMAC_CMD_OPCODE_RESET;
	umac_cmd.resp = resp;

	return iwm_hal_send_umac_cmd(iwm, &udma_cmd, &umac_cmd, &reset,
				     sizeof(struct iwm_umac_cmd_reset));
}

int iwm_umac_set_config_fix(struct iwm_priv *iwm, u16 tbl, u16 key, u32 value)
{
	struct iwm_udma_wifi_cmd udma_cmd = UDMA_UMAC_INIT;
	struct iwm_umac_cmd umac_cmd;
	struct iwm_umac_cmd_set_param_fix param;

	if ((tbl != UMAC_PARAM_TBL_CFG_FIX) &&
	    (tbl != UMAC_PARAM_TBL_FA_CFG_FIX))
		return -EINVAL;

	umac_cmd.id = UMAC_CMD_OPCODE_SET_PARAM_FIX;
	umac_cmd.resp = 0;

	param.tbl = cpu_to_le16(tbl);
	param.key = cpu_to_le16(key);
	param.value = cpu_to_le32(value);

	return iwm_hal_send_umac_cmd(iwm, &udma_cmd, &umac_cmd, &param,
				     sizeof(struct iwm_umac_cmd_set_param_fix));
}

int iwm_umac_set_config_var(struct iwm_priv *iwm, u16 key,
			    void *payload, u16 payload_size)
{
	struct iwm_udma_wifi_cmd udma_cmd = UDMA_UMAC_INIT;
	struct iwm_umac_cmd umac_cmd;
	struct iwm_umac_cmd_set_param_var *param_hdr;
	u8 *param;
	int ret;

	param = kzalloc(payload_size +
			sizeof(struct iwm_umac_cmd_set_param_var), GFP_KERNEL);
	if (!param) {
		IWM_ERR(iwm, "Couldn't allocate param\n");
		return -ENOMEM;
	}

	param_hdr = (struct iwm_umac_cmd_set_param_var *)param;

	umac_cmd.id = UMAC_CMD_OPCODE_SET_PARAM_VAR;
	umac_cmd.resp = 0;

	param_hdr->tbl = cpu_to_le16(UMAC_PARAM_TBL_CFG_VAR);
	param_hdr->key = cpu_to_le16(key);
	param_hdr->len = cpu_to_le16(payload_size);
	memcpy(param + sizeof(struct iwm_umac_cmd_set_param_var),
	       payload, payload_size);

	ret = iwm_hal_send_umac_cmd(iwm, &udma_cmd, &umac_cmd, param,
				    sizeof(struct iwm_umac_cmd_set_param_var) +
				    payload_size);
	kfree(param);

	return ret;
}

int iwm_send_umac_config(struct iwm_priv *iwm, __le32 reset_flags)
{
	int ret;

	/* Use UMAC default values */
	ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				      CFG_POWER_INDEX, iwm->conf.power_index);
	if (ret < 0)
		return ret;

	ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_FA_CFG_FIX,
				      CFG_FRAG_THRESHOLD,
				      iwm->conf.frag_threshold);
	if (ret < 0)
		return ret;

	ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				      CFG_RTS_THRESHOLD,
				      iwm->conf.rts_threshold);
	if (ret < 0)
		return ret;

	ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				      CFG_CTS_TO_SELF, iwm->conf.cts_to_self);
	if (ret < 0)
		return ret;

	ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				      CFG_WIRELESS_MODE,
				      iwm->conf.wireless_mode);
	if (ret < 0)
		return ret;

	ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				      CFG_COEX_MODE, modparam_wiwi);
	if (ret < 0)
		return ret;

	/*
	ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				      CFG_ASSOCIATION_TIMEOUT,
				      iwm->conf.assoc_timeout);
	if (ret < 0)
		return ret;

	ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				      CFG_ROAM_TIMEOUT,
				      iwm->conf.roam_timeout);
	if (ret < 0)
		return ret;

	ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				      CFG_WIRELESS_MODE,
				      WIRELESS_MODE_11A | WIRELESS_MODE_11G);
	if (ret < 0)
		return ret;
	*/

	ret = iwm_umac_set_config_var(iwm, CFG_NET_ADDR,
				      iwm_to_ndev(iwm)->dev_addr, ETH_ALEN);
	if (ret < 0)
		return ret;

	/* UMAC PM static configurations */
	ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				      CFG_PM_LEGACY_RX_TIMEOUT, 0x12C);
	if (ret < 0)
		return ret;

	ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				      CFG_PM_LEGACY_TX_TIMEOUT, 0x15E);
	if (ret < 0)
		return ret;

	ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				      CFG_PM_CTRL_FLAGS, 0x1);
	if (ret < 0)
		return ret;

	ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				      CFG_PM_KEEP_ALIVE_IN_BEACONS, 0x80);
	if (ret < 0)
		return ret;

	/* reset UMAC */
	ret = iwm_send_umac_reset(iwm, reset_flags, 1);
	if (ret < 0)
		return ret;

	ret = iwm_notif_handle(iwm, UMAC_CMD_OPCODE_RESET, IWM_SRC_UMAC,
			       WAIT_NOTIF_TIMEOUT);
	if (ret) {
		IWM_ERR(iwm, "Wait for UMAC RESET timeout\n");
		return ret;
	}

	return ret;
}

int iwm_send_packet(struct iwm_priv *iwm, struct sk_buff *skb, int pool_id)
{
	struct iwm_udma_wifi_cmd udma_cmd;
	struct iwm_umac_cmd umac_cmd;
	struct iwm_tx_info *tx_info = skb_to_tx_info(skb);

	udma_cmd.eop = 1; /* always set eop for non-concatenated Tx */
	udma_cmd.credit_group = pool_id;
	udma_cmd.ra_tid = tx_info->sta << 4 | tx_info->tid;
	udma_cmd.lmac_offset = 0;

	umac_cmd.id = REPLY_TX;
	umac_cmd.color = tx_info->color;
	umac_cmd.resp = 0;

	return iwm_hal_send_umac_cmd(iwm, &udma_cmd, &umac_cmd,
				     skb->data, skb->len);
}

static int iwm_target_read(struct iwm_priv *iwm, __le32 address,
			   u8 *response, u32 resp_size)
{
	struct iwm_udma_nonwifi_cmd target_cmd;
	struct iwm_nonwifi_cmd *cmd;
	u16 seq_num;
	int ret = 0;

	target_cmd.opcode = UMAC_HDI_OUT_OPCODE_READ;
	target_cmd.addr = address;
	target_cmd.op1_sz = cpu_to_le32(resp_size);
	target_cmd.op2 = 0;
	target_cmd.handle_by_hw = 0;
	target_cmd.resp = 1;
	target_cmd.eop = 1;

	ret = iwm_hal_send_target_cmd(iwm, &target_cmd, NULL);
	if (ret < 0) {
		IWM_ERR(iwm, "Couldn't send READ command\n");
		return ret;
	}

	/* When succeeding, the send_target routine returns the seq number */
	seq_num = ret;

	ret = wait_event_interruptible_timeout(iwm->nonwifi_queue,
		(cmd = iwm_get_pending_nonwifi_cmd(iwm, seq_num,
					  UMAC_HDI_OUT_OPCODE_READ)) != NULL,
					       2 * HZ);

	if (!ret) {
		IWM_ERR(iwm, "Didn't receive a target READ answer\n");
		return ret;
	}

	memcpy(response, cmd->buf.hdr + sizeof(struct iwm_udma_in_hdr),
	       resp_size);

	kfree(cmd);

	return 0;
}

int iwm_read_mac(struct iwm_priv *iwm, u8 *mac)
{
	int ret;
	u8 mac_align[ALIGN(ETH_ALEN, 8)];

	ret = iwm_target_read(iwm, cpu_to_le32(WICO_MAC_ADDRESS_ADDR),
			      mac_align, sizeof(mac_align));
	if (ret)
		return ret;

	if (is_valid_ether_addr(mac_align))
		memcpy(mac, mac_align, ETH_ALEN);
	else {
		IWM_ERR(iwm, "Invalid EEPROM MAC\n");
		memcpy(mac, iwm->conf.mac_addr, ETH_ALEN);
		get_random_bytes(&mac[3], 3);
	}

	return 0;
}

static int iwm_check_profile(struct iwm_priv *iwm)
{
	if (!iwm->umac_profile_active)
		return -EAGAIN;

	if (iwm->umac_profile->sec.ucast_cipher != UMAC_CIPHER_TYPE_WEP_40 &&
	    iwm->umac_profile->sec.ucast_cipher != UMAC_CIPHER_TYPE_WEP_104 &&
	    iwm->umac_profile->sec.ucast_cipher != UMAC_CIPHER_TYPE_TKIP &&
	    iwm->umac_profile->sec.ucast_cipher != UMAC_CIPHER_TYPE_CCMP) {
		IWM_ERR(iwm, "Wrong unicast cipher: 0x%x\n",
			iwm->umac_profile->sec.ucast_cipher);
		return -EAGAIN;
	}

	if (iwm->umac_profile->sec.mcast_cipher != UMAC_CIPHER_TYPE_WEP_40 &&
	    iwm->umac_profile->sec.mcast_cipher != UMAC_CIPHER_TYPE_WEP_104 &&
	    iwm->umac_profile->sec.mcast_cipher != UMAC_CIPHER_TYPE_TKIP &&
	    iwm->umac_profile->sec.mcast_cipher != UMAC_CIPHER_TYPE_CCMP) {
		IWM_ERR(iwm, "Wrong multicast cipher: 0x%x\n",
			iwm->umac_profile->sec.mcast_cipher);
		return -EAGAIN;
	}

	if ((iwm->umac_profile->sec.ucast_cipher == UMAC_CIPHER_TYPE_WEP_40 ||
	     iwm->umac_profile->sec.ucast_cipher == UMAC_CIPHER_TYPE_WEP_104) &&
	    (iwm->umac_profile->sec.ucast_cipher !=
	     iwm->umac_profile->sec.mcast_cipher)) {
		IWM_ERR(iwm, "Unicast and multicast ciphers differ for WEP\n");
	}

	return 0;
}

int iwm_set_tx_key(struct iwm_priv *iwm, u8 key_idx)
{
	struct iwm_umac_tx_key_id tx_key_id;
	int ret;

	ret = iwm_check_profile(iwm);
	if (ret < 0)
		return ret;

	/* UMAC only allows to set default key for WEP and auth type is
	 * NOT 802.1X or RSNA. */
	if ((iwm->umac_profile->sec.ucast_cipher != UMAC_CIPHER_TYPE_WEP_40 &&
	     iwm->umac_profile->sec.ucast_cipher != UMAC_CIPHER_TYPE_WEP_104) ||
	    iwm->umac_profile->sec.auth_type == UMAC_AUTH_TYPE_8021X ||
	    iwm->umac_profile->sec.auth_type == UMAC_AUTH_TYPE_RSNA_PSK)
		return 0;

	tx_key_id.hdr.oid = UMAC_WIFI_IF_CMD_GLOBAL_TX_KEY_ID;
	tx_key_id.hdr.buf_size = cpu_to_le16(sizeof(struct iwm_umac_tx_key_id) -
					     sizeof(struct iwm_umac_wifi_if));

	tx_key_id.key_idx = key_idx;

	return iwm_send_wifi_if_cmd(iwm, &tx_key_id, sizeof(tx_key_id), 1);
}

int iwm_set_key(struct iwm_priv *iwm, bool remove, struct iwm_key *key)
{
	int ret = 0;
	u8 cmd[64], *sta_addr, *key_data, key_len;
	s8 key_idx;
	u16 cmd_size = 0;
	struct iwm_umac_key_hdr *key_hdr = &key->hdr;
	struct iwm_umac_key_wep40 *wep40 = (struct iwm_umac_key_wep40 *)cmd;
	struct iwm_umac_key_wep104 *wep104 = (struct iwm_umac_key_wep104 *)cmd;
	struct iwm_umac_key_tkip *tkip = (struct iwm_umac_key_tkip *)cmd;
	struct iwm_umac_key_ccmp *ccmp = (struct iwm_umac_key_ccmp *)cmd;

	if (!remove) {
		ret = iwm_check_profile(iwm);
		if (ret < 0)
			return ret;
	}

	sta_addr = key->hdr.mac;
	key_data = key->key;
	key_len = key->key_len;
	key_idx = key->hdr.key_idx;

	if (!remove) {
		u8 auth_type = iwm->umac_profile->sec.auth_type;

		IWM_DBG_WEXT(iwm, DBG, "key_idx:%d\n", key_idx);
		IWM_DBG_WEXT(iwm, DBG, "key_len:%d\n", key_len);
		IWM_DBG_WEXT(iwm, DBG, "MAC:%pM, idx:%d, multicast:%d\n",
		       key_hdr->mac, key_hdr->key_idx, key_hdr->multicast);

		IWM_DBG_WEXT(iwm, DBG, "profile: mcast:0x%x, ucast:0x%x\n",
			     iwm->umac_profile->sec.mcast_cipher,
			     iwm->umac_profile->sec.ucast_cipher);
		IWM_DBG_WEXT(iwm, DBG, "profile: auth_type:0x%x, flags:0x%x\n",
			     iwm->umac_profile->sec.auth_type,
			     iwm->umac_profile->sec.flags);

		switch (key->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
			wep40->hdr.oid = UMAC_WIFI_IF_CMD_ADD_WEP40_KEY;
			wep40->hdr.buf_size =
				cpu_to_le16(sizeof(struct iwm_umac_key_wep40) -
					    sizeof(struct iwm_umac_wifi_if));

			memcpy(&wep40->key_hdr, key_hdr,
			       sizeof(struct iwm_umac_key_hdr));
			memcpy(wep40->key, key_data, key_len);
			wep40->static_key =
				!!((auth_type != UMAC_AUTH_TYPE_8021X) &&
				   (auth_type != UMAC_AUTH_TYPE_RSNA_PSK));

			cmd_size = sizeof(struct iwm_umac_key_wep40);
			break;

		case WLAN_CIPHER_SUITE_WEP104:
			wep104->hdr.oid = UMAC_WIFI_IF_CMD_ADD_WEP104_KEY;
			wep104->hdr.buf_size =
				cpu_to_le16(sizeof(struct iwm_umac_key_wep104) -
					    sizeof(struct iwm_umac_wifi_if));

			memcpy(&wep104->key_hdr, key_hdr,
			       sizeof(struct iwm_umac_key_hdr));
			memcpy(wep104->key, key_data, key_len);
			wep104->static_key =
				!!((auth_type != UMAC_AUTH_TYPE_8021X) &&
				   (auth_type != UMAC_AUTH_TYPE_RSNA_PSK));

			cmd_size = sizeof(struct iwm_umac_key_wep104);
			break;

		case WLAN_CIPHER_SUITE_CCMP:
			key_hdr->key_idx++;
			ccmp->hdr.oid = UMAC_WIFI_IF_CMD_ADD_CCMP_KEY;
			ccmp->hdr.buf_size =
				cpu_to_le16(sizeof(struct iwm_umac_key_ccmp) -
					    sizeof(struct iwm_umac_wifi_if));

			memcpy(&ccmp->key_hdr, key_hdr,
			       sizeof(struct iwm_umac_key_hdr));

			memcpy(ccmp->key, key_data, key_len);

			if (key->seq_len)
				memcpy(ccmp->iv_count, key->seq, key->seq_len);

			cmd_size = sizeof(struct iwm_umac_key_ccmp);
			break;

		case WLAN_CIPHER_SUITE_TKIP:
			key_hdr->key_idx++;
			tkip->hdr.oid = UMAC_WIFI_IF_CMD_ADD_TKIP_KEY;
			tkip->hdr.buf_size =
				cpu_to_le16(sizeof(struct iwm_umac_key_tkip) -
					    sizeof(struct iwm_umac_wifi_if));

			memcpy(&tkip->key_hdr, key_hdr,
			       sizeof(struct iwm_umac_key_hdr));

			memcpy(tkip->tkip_key, key_data, IWM_TKIP_KEY_SIZE);
			memcpy(tkip->mic_tx_key, key_data + IWM_TKIP_KEY_SIZE,
			       IWM_TKIP_MIC_SIZE);
			memcpy(tkip->mic_rx_key,
			       key_data + IWM_TKIP_KEY_SIZE + IWM_TKIP_MIC_SIZE,
			       IWM_TKIP_MIC_SIZE);

			if (key->seq_len)
				memcpy(ccmp->iv_count, key->seq, key->seq_len);

			cmd_size = sizeof(struct iwm_umac_key_tkip);
			break;

		default:
			return -ENOTSUPP;
		}

		if ((key->cipher == WLAN_CIPHER_SUITE_TKIP) ||
		    (key->cipher == WLAN_CIPHER_SUITE_CCMP))
			/*
			 * UGLY_UGLY_UGLY
			 * Copied HACK from the MWG driver.
			 * Without it, the key is set before the second
			 * EAPOL frame is sent, and the latter is thus
			 * encrypted.
			 */
			schedule_timeout_interruptible(usecs_to_jiffies(300));

		ret =  iwm_send_wifi_if_cmd(iwm, cmd, cmd_size, 1);
	} else {
		struct iwm_umac_key_remove key_remove;

		IWM_DBG_WEXT(iwm, ERR, "Removing key_idx:%d\n", key_idx);

		key_remove.hdr.oid = UMAC_WIFI_IF_CMD_REMOVE_KEY;
		key_remove.hdr.buf_size =
			cpu_to_le16(sizeof(struct iwm_umac_key_remove) -
				    sizeof(struct iwm_umac_wifi_if));
		memcpy(&key_remove.key_hdr, key_hdr,
		       sizeof(struct iwm_umac_key_hdr));

		ret =  iwm_send_wifi_if_cmd(iwm, &key_remove,
					    sizeof(struct iwm_umac_key_remove),
					    1);
		if (ret)
			return ret;

		iwm->keys[key_idx].key_len = 0;
	}

	return ret;
}


int iwm_send_mlme_profile(struct iwm_priv *iwm)
{
	int ret;
	struct iwm_umac_profile profile;

	memcpy(&profile, iwm->umac_profile, sizeof(profile));

	profile.hdr.oid = UMAC_WIFI_IF_CMD_SET_PROFILE;
	profile.hdr.buf_size = cpu_to_le16(sizeof(struct iwm_umac_profile) -
					   sizeof(struct iwm_umac_wifi_if));

	ret = iwm_send_wifi_if_cmd(iwm, &profile, sizeof(profile), 1);
	if (ret) {
		IWM_ERR(iwm, "Send profile command failed\n");
		return ret;
	}

	set_bit(IWM_STATUS_SME_CONNECTING, &iwm->status);
	return 0;
}

int __iwm_invalidate_mlme_profile(struct iwm_priv *iwm)
{
	struct iwm_umac_invalidate_profile invalid;

	invalid.hdr.oid = UMAC_WIFI_IF_CMD_INVALIDATE_PROFILE;
	invalid.hdr.buf_size =
		cpu_to_le16(sizeof(struct iwm_umac_invalidate_profile) -
			    sizeof(struct iwm_umac_wifi_if));

	invalid.reason = WLAN_REASON_UNSPECIFIED;

	return iwm_send_wifi_if_cmd(iwm, &invalid, sizeof(invalid), 1);
}

int iwm_invalidate_mlme_profile(struct iwm_priv *iwm)
{
	int ret;

	ret = __iwm_invalidate_mlme_profile(iwm);
	if (ret)
		return ret;

	ret = wait_event_interruptible_timeout(iwm->mlme_queue,
				(iwm->umac_profile_active == 0), 5 * HZ);

	return ret ? 0 : -EBUSY;
}

int iwm_tx_power_trigger(struct iwm_priv *iwm)
{
	struct iwm_umac_pwr_trigger pwr_trigger;

	pwr_trigger.hdr.oid = UMAC_WIFI_IF_CMD_TX_PWR_TRIGGER;
	pwr_trigger.hdr.buf_size =
		cpu_to_le16(sizeof(struct iwm_umac_pwr_trigger) -
			    sizeof(struct iwm_umac_wifi_if));


	return iwm_send_wifi_if_cmd(iwm, &pwr_trigger, sizeof(pwr_trigger), 1);
}

int iwm_send_umac_stats_req(struct iwm_priv *iwm, u32 flags)
{
	struct iwm_udma_wifi_cmd udma_cmd = UDMA_UMAC_INIT;
	struct iwm_umac_cmd umac_cmd;
	struct iwm_umac_cmd_stats_req stats_req;

	stats_req.flags = cpu_to_le32(flags);

	umac_cmd.id = UMAC_CMD_OPCODE_STATISTIC_REQUEST;
	umac_cmd.resp = 0;

	return iwm_hal_send_umac_cmd(iwm, &udma_cmd, &umac_cmd, &stats_req,
				     sizeof(struct iwm_umac_cmd_stats_req));
}

int iwm_send_umac_channel_list(struct iwm_priv *iwm)
{
	struct iwm_udma_wifi_cmd udma_cmd = UDMA_UMAC_INIT;
	struct iwm_umac_cmd umac_cmd;
	struct iwm_umac_cmd_get_channel_list *ch_list;
	int size = sizeof(struct iwm_umac_cmd_get_channel_list) +
		   sizeof(struct iwm_umac_channel_info) * 4;
	int ret;

	ch_list = kzalloc(size, GFP_KERNEL);
	if (!ch_list) {
		IWM_ERR(iwm, "Couldn't allocate channel list cmd\n");
		return -ENOMEM;
	}

	ch_list->ch[0].band = UMAC_BAND_2GHZ;
	ch_list->ch[0].type = UMAC_CHANNEL_WIDTH_20MHZ;
	ch_list->ch[0].flags = UMAC_CHANNEL_FLAG_VALID;

	ch_list->ch[1].band = UMAC_BAND_5GHZ;
	ch_list->ch[1].type = UMAC_CHANNEL_WIDTH_20MHZ;
	ch_list->ch[1].flags = UMAC_CHANNEL_FLAG_VALID;

	ch_list->ch[2].band = UMAC_BAND_2GHZ;
	ch_list->ch[2].type = UMAC_CHANNEL_WIDTH_20MHZ;
	ch_list->ch[2].flags = UMAC_CHANNEL_FLAG_VALID | UMAC_CHANNEL_FLAG_IBSS;

	ch_list->ch[3].band = UMAC_BAND_5GHZ;
	ch_list->ch[3].type = UMAC_CHANNEL_WIDTH_20MHZ;
	ch_list->ch[3].flags = UMAC_CHANNEL_FLAG_VALID | UMAC_CHANNEL_FLAG_IBSS;

	ch_list->count = cpu_to_le16(4);

	umac_cmd.id = UMAC_CMD_OPCODE_GET_CHAN_INFO_LIST;
	umac_cmd.resp = 1;

	ret = iwm_hal_send_umac_cmd(iwm, &udma_cmd, &umac_cmd, ch_list, size);

	kfree(ch_list);

	return ret;
}

int iwm_scan_ssids(struct iwm_priv *iwm, struct cfg80211_ssid *ssids,
		   int ssid_num)
{
	struct iwm_umac_cmd_scan_request req;
	int i, ret;

	memset(&req, 0, sizeof(struct iwm_umac_cmd_scan_request));

	req.hdr.oid = UMAC_WIFI_IF_CMD_SCAN_REQUEST;
	req.hdr.buf_size = cpu_to_le16(sizeof(struct iwm_umac_cmd_scan_request)
				       - sizeof(struct iwm_umac_wifi_if));
	req.type = UMAC_WIFI_IF_SCAN_TYPE_USER;
	req.timeout = 2;
	req.seq_num = iwm->scan_id;
	req.ssid_num = min(ssid_num, UMAC_WIFI_IF_PROBE_OPTION_MAX);

	for (i = 0; i < req.ssid_num; i++) {
		memcpy(req.ssids[i].ssid, ssids[i].ssid, ssids[i].ssid_len);
		req.ssids[i].ssid_len = ssids[i].ssid_len;
	}

	ret = iwm_send_wifi_if_cmd(iwm, &req, sizeof(req), 0);
	if (ret) {
		IWM_ERR(iwm, "Couldn't send scan request\n");
		return ret;
	}

	iwm->scan_id = (iwm->scan_id + 1) % IWM_SCAN_ID_MAX;

	return 0;
}

int iwm_scan_one_ssid(struct iwm_priv *iwm, u8 *ssid, int ssid_len)
{
	struct cfg80211_ssid one_ssid;

	if (test_and_set_bit(IWM_STATUS_SCANNING, &iwm->status))
		return 0;

	one_ssid.ssid_len = min(ssid_len, IEEE80211_MAX_SSID_LEN);
	memcpy(&one_ssid.ssid, ssid, one_ssid.ssid_len);

	return iwm_scan_ssids(iwm, &one_ssid, 1);
}

int iwm_target_reset(struct iwm_priv *iwm)
{
	struct iwm_udma_nonwifi_cmd target_cmd;

	target_cmd.opcode = UMAC_HDI_OUT_OPCODE_REBOOT;
	target_cmd.addr = 0;
	target_cmd.op1_sz = 0;
	target_cmd.op2 = 0;
	target_cmd.handle_by_hw = 0;
	target_cmd.resp = 0;
	target_cmd.eop = 1;

	return iwm_hal_send_target_cmd(iwm, &target_cmd, NULL);
}

int iwm_send_umac_stop_resume_tx(struct iwm_priv *iwm,
				 struct iwm_umac_notif_stop_resume_tx *ntf)
{
	struct iwm_udma_wifi_cmd udma_cmd = UDMA_UMAC_INIT;
	struct iwm_umac_cmd umac_cmd;
	struct iwm_umac_cmd_stop_resume_tx stp_res_cmd;
	struct iwm_sta_info *sta_info;
	u8 sta_id = STA_ID_N_COLOR_ID(ntf->sta_id);
	int i;

	sta_info = &iwm->sta_table[sta_id];
	if (!sta_info->valid) {
		IWM_ERR(iwm, "Invalid STA: %d\n", sta_id);
		return -EINVAL;
	}

	umac_cmd.id = UMAC_CMD_OPCODE_STOP_RESUME_STA_TX;
	umac_cmd.resp = 0;

	stp_res_cmd.flags = ntf->flags;
	stp_res_cmd.sta_id = ntf->sta_id;
	stp_res_cmd.stop_resume_tid_msk = ntf->stop_resume_tid_msk;
	for (i = 0; i < IWM_UMAC_TID_NR; i++)
		stp_res_cmd.last_seq_num[i] =
			sta_info->tid_info[i].last_seq_num;

	return iwm_hal_send_umac_cmd(iwm, &udma_cmd, &umac_cmd, &stp_res_cmd,
				 sizeof(struct iwm_umac_cmd_stop_resume_tx));

}

int iwm_send_pmkid_update(struct iwm_priv *iwm,
			  struct cfg80211_pmksa *pmksa, u32 command)
{
	struct iwm_umac_pmkid_update update;
	int ret;

	memset(&update, 0, sizeof(struct iwm_umac_pmkid_update));

	update.hdr.oid = UMAC_WIFI_IF_CMD_PMKID_UPDATE;
	update.hdr.buf_size = cpu_to_le16(sizeof(struct iwm_umac_pmkid_update) -
					  sizeof(struct iwm_umac_wifi_if));

	update.command = cpu_to_le32(command);
	if (pmksa->bssid)
		memcpy(&update.bssid, pmksa->bssid, ETH_ALEN);
	if (pmksa->pmkid)
		memcpy(&update.pmkid, pmksa->pmkid, WLAN_PMKID_LEN);

	ret = iwm_send_wifi_if_cmd(iwm, &update,
				   sizeof(struct iwm_umac_pmkid_update), 0);
	if (ret) {
		IWM_ERR(iwm, "PMKID update command failed\n");
		return ret;
	}

	return 0;
}
