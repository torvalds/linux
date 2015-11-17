/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2013 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2015 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
 *****************************************************************************/

#include "mvm.h"

#define IWL_MVM_TEMP_NOTIF_WAIT_TIMEOUT	HZ

static void iwl_mvm_enter_ctkill(struct iwl_mvm *mvm)
{
	struct iwl_mvm_tt_mgmt *tt = &mvm->thermal_throttle;
	u32 duration = tt->params.ct_kill_duration;

	if (test_bit(IWL_MVM_STATUS_HW_CTKILL, &mvm->status))
		return;

	IWL_ERR(mvm, "Enter CT Kill\n");
	iwl_mvm_set_hw_ctkill_state(mvm, true);

	tt->throttle = false;
	tt->dynamic_smps = false;

	/* Don't schedule an exit work if we're in test mode, since
	 * the temperature will not change unless we manually set it
	 * again (or disable testing).
	 */
	if (!mvm->temperature_test)
		schedule_delayed_work(&tt->ct_kill_exit,
				      round_jiffies_relative(duration * HZ));
}

static void iwl_mvm_exit_ctkill(struct iwl_mvm *mvm)
{
	if (!test_bit(IWL_MVM_STATUS_HW_CTKILL, &mvm->status))
		return;

	IWL_ERR(mvm, "Exit CT Kill\n");
	iwl_mvm_set_hw_ctkill_state(mvm, false);
}

void iwl_mvm_tt_temp_changed(struct iwl_mvm *mvm, u32 temp)
{
	/* ignore the notification if we are in test mode */
	if (mvm->temperature_test)
		return;

	if (mvm->temperature == temp)
		return;

	mvm->temperature = temp;
	iwl_mvm_tt_handler(mvm);
}

static int iwl_mvm_temp_notif_parse(struct iwl_mvm *mvm,
				    struct iwl_rx_packet *pkt)
{
	struct iwl_dts_measurement_notif *notif;
	int len = iwl_rx_packet_payload_len(pkt);
	int temp;

	if (WARN_ON_ONCE(len != sizeof(*notif))) {
		IWL_ERR(mvm, "Invalid DTS_MEASUREMENT_NOTIFICATION\n");
		return -EINVAL;
	}

	notif = (void *)pkt->data;

	temp = le32_to_cpu(notif->temp);

	/* shouldn't be negative, but since it's s32, make sure it isn't */
	if (WARN_ON_ONCE(temp < 0))
		temp = 0;

	IWL_DEBUG_TEMP(mvm, "DTS_MEASUREMENT_NOTIFICATION - %d\n", temp);

	return temp;
}

static bool iwl_mvm_temp_notif_wait(struct iwl_notif_wait_data *notif_wait,
				    struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mvm *mvm =
		container_of(notif_wait, struct iwl_mvm, notif_wait);
	int *temp = data;
	int ret;

	ret = iwl_mvm_temp_notif_parse(mvm, pkt);
	if (ret < 0)
		return true;

	*temp = ret;

	return true;
}

void iwl_mvm_temp_notif(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	int temp;

	/* the notification is handled synchronously in ctkill, so skip here */
	if (test_bit(IWL_MVM_STATUS_HW_CTKILL, &mvm->status))
		return;

	temp = iwl_mvm_temp_notif_parse(mvm, pkt);
	if (temp < 0)
		return;

	iwl_mvm_tt_temp_changed(mvm, temp);
}

static int iwl_mvm_get_temp_cmd(struct iwl_mvm *mvm)
{
	struct iwl_dts_measurement_cmd cmd = {
		.flags = cpu_to_le32(DTS_TRIGGER_CMD_FLAGS_TEMP),
	};
	struct iwl_ext_dts_measurement_cmd extcmd = {
		.control_mode = cpu_to_le32(DTS_AUTOMATIC),
	};
	u32 cmdid;

	if (fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_WIDE_CMD_HDR))
		cmdid = iwl_cmd_id(CMD_DTS_MEASUREMENT_TRIGGER_WIDE,
				   PHY_OPS_GROUP, 0);
	else
		cmdid = CMD_DTS_MEASUREMENT_TRIGGER;

	if (!fw_has_capa(&mvm->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_EXTENDED_DTS_MEASURE))
		return iwl_mvm_send_cmd_pdu(mvm, cmdid, 0, sizeof(cmd), &cmd);

	return iwl_mvm_send_cmd_pdu(mvm, cmdid, 0, sizeof(extcmd), &extcmd);
}

int iwl_mvm_get_temp(struct iwl_mvm *mvm)
{
	struct iwl_notification_wait wait_temp_notif;
	static u16 temp_notif[] = { WIDE_ID(PHY_OPS_GROUP,
					    DTS_MEASUREMENT_NOTIF_WIDE) };
	int ret, temp;

	if (!fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_WIDE_CMD_HDR))
		temp_notif[0] = DTS_MEASUREMENT_NOTIFICATION;

	lockdep_assert_held(&mvm->mutex);

	iwl_init_notification_wait(&mvm->notif_wait, &wait_temp_notif,
				   temp_notif, ARRAY_SIZE(temp_notif),
				   iwl_mvm_temp_notif_wait, &temp);

	ret = iwl_mvm_get_temp_cmd(mvm);
	if (ret) {
		IWL_ERR(mvm, "Failed to get the temperature (err=%d)\n", ret);
		iwl_remove_notification(&mvm->notif_wait, &wait_temp_notif);
		return ret;
	}

	ret = iwl_wait_notification(&mvm->notif_wait, &wait_temp_notif,
				    IWL_MVM_TEMP_NOTIF_WAIT_TIMEOUT);
	if (ret) {
		IWL_ERR(mvm, "Getting the temperature timed out\n");
		return ret;
	}

	return temp;
}

static void check_exit_ctkill(struct work_struct *work)
{
	struct iwl_mvm_tt_mgmt *tt;
	struct iwl_mvm *mvm;
	u32 duration;
	s32 temp;

	tt = container_of(work, struct iwl_mvm_tt_mgmt, ct_kill_exit.work);
	mvm = container_of(tt, struct iwl_mvm, thermal_throttle);

	duration = tt->params.ct_kill_duration;

	mutex_lock(&mvm->mutex);

	if (__iwl_mvm_mac_start(mvm))
		goto reschedule;

	/* make sure the device is available for direct read/writes */
	if (iwl_mvm_ref_sync(mvm, IWL_MVM_REF_CHECK_CTKILL)) {
		__iwl_mvm_mac_stop(mvm);
		goto reschedule;
	}

	temp = iwl_mvm_get_temp(mvm);

	iwl_mvm_unref(mvm, IWL_MVM_REF_CHECK_CTKILL);

	__iwl_mvm_mac_stop(mvm);

	if (temp < 0)
		goto reschedule;

	IWL_DEBUG_TEMP(mvm, "NIC temperature: %d\n", temp);

	if (temp <= tt->params.ct_kill_exit) {
		mutex_unlock(&mvm->mutex);
		iwl_mvm_exit_ctkill(mvm);
		return;
	}

reschedule:
	mutex_unlock(&mvm->mutex);
	schedule_delayed_work(&mvm->thermal_throttle.ct_kill_exit,
			      round_jiffies(duration * HZ));
}

static void iwl_mvm_tt_smps_iterator(void *_data, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = _data;
	enum ieee80211_smps_mode smps_mode;

	lockdep_assert_held(&mvm->mutex);

	if (mvm->thermal_throttle.dynamic_smps)
		smps_mode = IEEE80211_SMPS_DYNAMIC;
	else
		smps_mode = IEEE80211_SMPS_AUTOMATIC;

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_TT, smps_mode);
}

static void iwl_mvm_tt_tx_protection(struct iwl_mvm *mvm, bool enable)
{
	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvmsta;
	int i, err;

	for (i = 0; i < IWL_MVM_STATION_COUNT; i++) {
		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[i],
						lockdep_is_held(&mvm->mutex));
		if (IS_ERR_OR_NULL(sta))
			continue;
		mvmsta = iwl_mvm_sta_from_mac80211(sta);
		if (enable == mvmsta->tt_tx_protection)
			continue;
		err = iwl_mvm_tx_protection(mvm, mvmsta, enable);
		if (err) {
			IWL_ERR(mvm, "Failed to %s Tx protection\n",
				enable ? "enable" : "disable");
		} else {
			IWL_DEBUG_TEMP(mvm, "%s Tx protection\n",
				       enable ? "Enable" : "Disable");
			mvmsta->tt_tx_protection = enable;
		}
	}
}

void iwl_mvm_tt_tx_backoff(struct iwl_mvm *mvm, u32 backoff)
{
	struct iwl_host_cmd cmd = {
		.id = REPLY_THERMAL_MNG_BACKOFF,
		.len = { sizeof(u32), },
		.data = { &backoff, },
	};

	backoff = max(backoff, mvm->thermal_throttle.min_backoff);

	if (iwl_mvm_send_cmd(mvm, &cmd) == 0) {
		IWL_DEBUG_TEMP(mvm, "Set Thermal Tx backoff to: %u\n",
			       backoff);
		mvm->thermal_throttle.tx_backoff = backoff;
	} else {
		IWL_ERR(mvm, "Failed to change Thermal Tx backoff\n");
	}
}

void iwl_mvm_tt_handler(struct iwl_mvm *mvm)
{
	struct iwl_tt_params *params = &mvm->thermal_throttle.params;
	struct iwl_mvm_tt_mgmt *tt = &mvm->thermal_throttle;
	s32 temperature = mvm->temperature;
	bool throttle_enable = false;
	int i;
	u32 tx_backoff;

	IWL_DEBUG_TEMP(mvm, "NIC temperature: %d\n", mvm->temperature);

	if (params->support_ct_kill && temperature >= params->ct_kill_entry) {
		iwl_mvm_enter_ctkill(mvm);
		return;
	}

	if (params->support_ct_kill &&
	    temperature <= params->ct_kill_exit) {
		iwl_mvm_exit_ctkill(mvm);
		return;
	}

	if (params->support_dynamic_smps) {
		if (!tt->dynamic_smps &&
		    temperature >= params->dynamic_smps_entry) {
			IWL_DEBUG_TEMP(mvm, "Enable dynamic SMPS\n");
			tt->dynamic_smps = true;
			ieee80211_iterate_active_interfaces_atomic(
					mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_tt_smps_iterator, mvm);
			throttle_enable = true;
		} else if (tt->dynamic_smps &&
			   temperature <= params->dynamic_smps_exit) {
			IWL_DEBUG_TEMP(mvm, "Disable dynamic SMPS\n");
			tt->dynamic_smps = false;
			ieee80211_iterate_active_interfaces_atomic(
					mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_tt_smps_iterator, mvm);
		}
	}

	if (params->support_tx_protection) {
		if (temperature >= params->tx_protection_entry) {
			iwl_mvm_tt_tx_protection(mvm, true);
			throttle_enable = true;
		} else if (temperature <= params->tx_protection_exit) {
			iwl_mvm_tt_tx_protection(mvm, false);
		}
	}

	if (params->support_tx_backoff) {
		tx_backoff = tt->min_backoff;
		for (i = 0; i < TT_TX_BACKOFF_SIZE; i++) {
			if (temperature < params->tx_backoff[i].temperature)
				break;
			tx_backoff = max(tt->min_backoff,
					 params->tx_backoff[i].backoff);
		}
		if (tx_backoff != tt->min_backoff)
			throttle_enable = true;
		if (tt->tx_backoff != tx_backoff)
			iwl_mvm_tt_tx_backoff(mvm, tx_backoff);
	}

	if (!tt->throttle && throttle_enable) {
		IWL_WARN(mvm,
			 "Due to high temperature thermal throttling initiated\n");
		tt->throttle = true;
	} else if (tt->throttle && !tt->dynamic_smps &&
		   tt->tx_backoff == tt->min_backoff &&
		   temperature <= params->tx_protection_exit) {
		IWL_WARN(mvm,
			 "Temperature is back to normal thermal throttling stopped\n");
		tt->throttle = false;
	}
}

static const struct iwl_tt_params iwl_mvm_default_tt_params = {
	.ct_kill_entry = 118,
	.ct_kill_exit = 96,
	.ct_kill_duration = 5,
	.dynamic_smps_entry = 114,
	.dynamic_smps_exit = 110,
	.tx_protection_entry = 114,
	.tx_protection_exit = 108,
	.tx_backoff = {
		{.temperature = 112, .backoff = 200},
		{.temperature = 113, .backoff = 600},
		{.temperature = 114, .backoff = 1200},
		{.temperature = 115, .backoff = 2000},
		{.temperature = 116, .backoff = 4000},
		{.temperature = 117, .backoff = 10000},
	},
	.support_ct_kill = true,
	.support_dynamic_smps = true,
	.support_tx_protection = true,
	.support_tx_backoff = true,
};

void iwl_mvm_tt_initialize(struct iwl_mvm *mvm, u32 min_backoff)
{
	struct iwl_mvm_tt_mgmt *tt = &mvm->thermal_throttle;

	IWL_DEBUG_TEMP(mvm, "Initialize Thermal Throttling\n");

	if (mvm->cfg->thermal_params)
		tt->params = *mvm->cfg->thermal_params;
	else
		tt->params = iwl_mvm_default_tt_params;

	tt->throttle = false;
	tt->dynamic_smps = false;
	tt->min_backoff = min_backoff;
	INIT_DELAYED_WORK(&tt->ct_kill_exit, check_exit_ctkill);
}

void iwl_mvm_tt_exit(struct iwl_mvm *mvm)
{
	cancel_delayed_work_sync(&mvm->thermal_throttle.ct_kill_exit);
	IWL_DEBUG_TEMP(mvm, "Exit Thermal Throttling\n");
}
