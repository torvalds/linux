/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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
#include <net/mac80211.h>

#include "iwl-trans.h"
#include "iwl-op-mode.h"
#include "iwl-fw.h"
#include "iwl-debug.h"
#include "iwl-csr.h" /* for iwl_mvm_rx_card_state_notif */
#include "iwl-io.h" /* for iwl_mvm_rx_card_state_notif */
#include "iwl-eeprom-parse.h"

#include "mvm.h"
#include "iwl-phy-db.h"

#define MVM_UCODE_ALIVE_TIMEOUT	HZ
#define MVM_UCODE_CALIB_TIMEOUT	(2*HZ)

#define UCODE_VALID_OK	cpu_to_le32(0x1)

/* Default calibration values for WkP - set to INIT image w/o running */
static const u8 wkp_calib_values_bb_filter[] = { 0xbf, 0x00, 0x5f, 0x00, 0x2f,
						 0x00, 0x18, 0x00 };
static const u8 wkp_calib_values_rx_dc[] = { 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
					     0x7f, 0x7f, 0x7f };
static const u8 wkp_calib_values_tx_lo[] = { 0x00, 0x00, 0x00, 0x00 };
static const u8 wkp_calib_values_tx_iq[] = { 0xff, 0x00, 0xff, 0x00, 0x00,
					     0x00 };
static const u8 wkp_calib_values_rx_iq[] = { 0xff, 0x00, 0x00, 0x00 };
static const u8 wkp_calib_values_rx_iq_skew[] = { 0x00, 0x00, 0x01, 0x00 };
static const u8 wkp_calib_values_tx_iq_skew[] = { 0x01, 0x00, 0x00, 0x00 };
static const u8 wkp_calib_values_xtal[] = { 0xd2, 0xd2 };

struct iwl_calib_default_data {
	u16 size;
	void *data;
};

#define CALIB_SIZE_N_DATA(_buf) {.size = sizeof(_buf), .data = &_buf}

static const struct iwl_calib_default_data wkp_calib_default_data[12] = {
	[5] = CALIB_SIZE_N_DATA(wkp_calib_values_rx_dc),
	[6] = CALIB_SIZE_N_DATA(wkp_calib_values_bb_filter),
	[7] = CALIB_SIZE_N_DATA(wkp_calib_values_tx_lo),
	[8] = CALIB_SIZE_N_DATA(wkp_calib_values_tx_iq),
	[9] = CALIB_SIZE_N_DATA(wkp_calib_values_tx_iq_skew),
	[10] = CALIB_SIZE_N_DATA(wkp_calib_values_rx_iq),
	[11] = CALIB_SIZE_N_DATA(wkp_calib_values_rx_iq_skew),
};

struct iwl_mvm_alive_data {
	bool valid;
	u32 scd_base_addr;
};

static inline const struct fw_img *
iwl_get_ucode_image(struct iwl_mvm *mvm, enum iwl_ucode_type ucode_type)
{
	if (ucode_type >= IWL_UCODE_TYPE_MAX)
		return NULL;

	return &mvm->fw->img[ucode_type];
}

static int iwl_send_tx_ant_cfg(struct iwl_mvm *mvm, u8 valid_tx_ant)
{
	struct iwl_tx_ant_cfg_cmd tx_ant_cmd = {
		.valid = cpu_to_le32(valid_tx_ant),
	};

	IWL_DEBUG_HC(mvm, "select valid tx ant: %u\n", valid_tx_ant);
	return iwl_mvm_send_cmd_pdu(mvm, TX_ANT_CONFIGURATION_CMD, CMD_SYNC,
				    sizeof(tx_ant_cmd), &tx_ant_cmd);
}

static bool iwl_alive_fn(struct iwl_notif_wait_data *notif_wait,
			 struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mvm *mvm =
		container_of(notif_wait, struct iwl_mvm, notif_wait);
	struct iwl_mvm_alive_data *alive_data = data;
	struct mvm_alive_resp *palive;

	palive = (void *)pkt->data;

	mvm->error_event_table = le32_to_cpu(palive->error_event_table_ptr);
	mvm->log_event_table = le32_to_cpu(palive->log_event_table_ptr);
	alive_data->scd_base_addr = le32_to_cpu(palive->scd_base_ptr);

	alive_data->valid = le16_to_cpu(palive->status) == IWL_ALIVE_STATUS_OK;
	IWL_DEBUG_FW(mvm, "Alive ucode status 0x%04x revision 0x%01X 0x%01X\n",
		     le16_to_cpu(palive->status), palive->ver_type,
		     palive->ver_subtype);

	return true;
}

static bool iwl_wait_phy_db_entry(struct iwl_notif_wait_data *notif_wait,
				  struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_phy_db *phy_db = data;

	if (pkt->hdr.cmd != CALIB_RES_NOTIF_PHY_DB) {
		WARN_ON(pkt->hdr.cmd != INIT_COMPLETE_NOTIF);
		return true;
	}

	WARN_ON(iwl_phy_db_set_section(phy_db, pkt, GFP_ATOMIC));

	return false;
}

static int iwl_mvm_load_ucode_wait_alive(struct iwl_mvm *mvm,
					 enum iwl_ucode_type ucode_type)
{
	struct iwl_notification_wait alive_wait;
	struct iwl_mvm_alive_data alive_data;
	const struct fw_img *fw;
	int ret, i;
	enum iwl_ucode_type old_type = mvm->cur_ucode;
	static const u8 alive_cmd[] = { MVM_ALIVE };

	mvm->cur_ucode = ucode_type;
	fw = iwl_get_ucode_image(mvm, ucode_type);

	mvm->ucode_loaded = false;

	if (!fw)
		return -EINVAL;

	iwl_init_notification_wait(&mvm->notif_wait, &alive_wait,
				   alive_cmd, ARRAY_SIZE(alive_cmd),
				   iwl_alive_fn, &alive_data);

	ret = iwl_trans_start_fw(mvm->trans, fw, ucode_type == IWL_UCODE_INIT);
	if (ret) {
		mvm->cur_ucode = old_type;
		iwl_remove_notification(&mvm->notif_wait, &alive_wait);
		return ret;
	}

	/*
	 * Some things may run in the background now, but we
	 * just wait for the ALIVE notification here.
	 */
	ret = iwl_wait_notification(&mvm->notif_wait, &alive_wait,
				    MVM_UCODE_ALIVE_TIMEOUT);
	if (ret) {
		mvm->cur_ucode = old_type;
		return ret;
	}

	if (!alive_data.valid) {
		IWL_ERR(mvm, "Loaded ucode is not valid!\n");
		mvm->cur_ucode = old_type;
		return -EIO;
	}

	iwl_trans_fw_alive(mvm->trans, alive_data.scd_base_addr);

	/*
	 * Note: all the queues are enabled as part of the interface
	 * initialization, but in firmware restart scenarios they
	 * could be stopped, so wake them up. In firmware restart,
	 * mac80211 will have the queues stopped as well until the
	 * reconfiguration completes. During normal startup, they
	 * will be empty.
	 */

	for (i = 0; i < IWL_MAX_HW_QUEUES; i++) {
		if (i < IWL_MVM_FIRST_AGG_QUEUE && i != IWL_MVM_CMD_QUEUE)
			mvm->queue_to_mac80211[i] = i;
		else
			mvm->queue_to_mac80211[i] = IWL_INVALID_MAC80211_QUEUE;
		atomic_set(&mvm->queue_stop_count[i], 0);
	}

	mvm->transport_queue_stop = 0;

	mvm->ucode_loaded = true;

	return 0;
}
#define IWL_HW_REV_ID_RAINBOW	0x2
#define IWL_PROJ_TYPE_LHP	0x5

static u32 iwl_mvm_build_phy_cfg(struct iwl_mvm *mvm)
{
	struct iwl_nvm_data *data = mvm->nvm_data;
	/* Temp calls to static definitions, will be changed to CSR calls */
	u8 hw_rev_id = IWL_HW_REV_ID_RAINBOW;
	u8 project_type = IWL_PROJ_TYPE_LHP;

	return data->radio_cfg_dash | (data->radio_cfg_step << 2) |
		(hw_rev_id << 4) | ((project_type & 0x7f) << 6) |
		(data->valid_tx_ant << 16) | (data->valid_rx_ant << 20);
}

static int iwl_send_phy_cfg_cmd(struct iwl_mvm *mvm)
{
	struct iwl_phy_cfg_cmd phy_cfg_cmd;
	enum iwl_ucode_type ucode_type = mvm->cur_ucode;

	/* Set parameters */
	phy_cfg_cmd.phy_cfg = cpu_to_le32(iwl_mvm_build_phy_cfg(mvm));
	phy_cfg_cmd.calib_control.event_trigger =
		mvm->fw->default_calib[ucode_type].event_trigger;
	phy_cfg_cmd.calib_control.flow_trigger =
		mvm->fw->default_calib[ucode_type].flow_trigger;

	IWL_DEBUG_INFO(mvm, "Sending Phy CFG command: 0x%x\n",
		       phy_cfg_cmd.phy_cfg);

	return iwl_mvm_send_cmd_pdu(mvm, PHY_CONFIGURATION_CMD, CMD_SYNC,
				    sizeof(phy_cfg_cmd), &phy_cfg_cmd);
}

/* Starting with the new PHY DB implementation - New calibs are enabled */
/* Value - 0x405e7 */
#define IWL_CALIB_DEFAULT_FLOW_INIT	(IWL_CALIB_CFG_XTAL_IDX		|\
					 IWL_CALIB_CFG_TEMPERATURE_IDX	|\
					 IWL_CALIB_CFG_VOLTAGE_READ_IDX	|\
					 IWL_CALIB_CFG_DC_IDX		|\
					 IWL_CALIB_CFG_BB_FILTER_IDX	|\
					 IWL_CALIB_CFG_LO_LEAKAGE_IDX	|\
					 IWL_CALIB_CFG_TX_IQ_IDX	|\
					 IWL_CALIB_CFG_RX_IQ_IDX	|\
					 IWL_CALIB_CFG_AGC_IDX)

#define IWL_CALIB_DEFAULT_EVENT_INIT	0x0

/* Value 0x41567 */
#define IWL_CALIB_DEFAULT_FLOW_RUN	(IWL_CALIB_CFG_XTAL_IDX		|\
					 IWL_CALIB_CFG_TEMPERATURE_IDX	|\
					 IWL_CALIB_CFG_VOLTAGE_READ_IDX	|\
					 IWL_CALIB_CFG_BB_FILTER_IDX	|\
					 IWL_CALIB_CFG_DC_IDX		|\
					 IWL_CALIB_CFG_TX_IQ_IDX	|\
					 IWL_CALIB_CFG_RX_IQ_IDX	|\
					 IWL_CALIB_CFG_SENSITIVITY_IDX	|\
					 IWL_CALIB_CFG_AGC_IDX)

#define IWL_CALIB_DEFAULT_EVENT_RUN	(IWL_CALIB_CFG_XTAL_IDX		|\
					 IWL_CALIB_CFG_TEMPERATURE_IDX	|\
					 IWL_CALIB_CFG_VOLTAGE_READ_IDX	|\
					 IWL_CALIB_CFG_TX_PWR_IDX	|\
					 IWL_CALIB_CFG_DC_IDX		|\
					 IWL_CALIB_CFG_TX_IQ_IDX	|\
					 IWL_CALIB_CFG_SENSITIVITY_IDX)

/*
 * Sets the calibrations trigger values that will be sent to the FW for runtime
 * and init calibrations.
 * The ones given in the FW TLV are not correct.
 */
static void iwl_set_default_calib_trigger(struct iwl_mvm *mvm)
{
	struct iwl_tlv_calib_ctrl default_calib;

	/*
	 * WkP FW TLV calib bits are wrong, overwrite them.
	 * This defines the dynamic calibrations which are implemented in the
	 * uCode both for init(flow) calculation and event driven calibs.
	 */

	/* Init Image */
	default_calib.event_trigger = cpu_to_le32(IWL_CALIB_DEFAULT_EVENT_INIT);
	default_calib.flow_trigger = cpu_to_le32(IWL_CALIB_DEFAULT_FLOW_INIT);

	if (default_calib.event_trigger !=
	    mvm->fw->default_calib[IWL_UCODE_INIT].event_trigger)
		IWL_ERR(mvm,
			"Updating the event calib for INIT image: 0x%x -> 0x%x\n",
			mvm->fw->default_calib[IWL_UCODE_INIT].event_trigger,
			default_calib.event_trigger);
	if (default_calib.flow_trigger !=
	    mvm->fw->default_calib[IWL_UCODE_INIT].flow_trigger)
		IWL_ERR(mvm,
			"Updating the flow calib for INIT image: 0x%x -> 0x%x\n",
			mvm->fw->default_calib[IWL_UCODE_INIT].flow_trigger,
			default_calib.flow_trigger);

	memcpy((void *)&mvm->fw->default_calib[IWL_UCODE_INIT],
	       &default_calib, sizeof(struct iwl_tlv_calib_ctrl));
	IWL_ERR(mvm,
		"Setting uCode init calibrations event 0x%x, trigger 0x%x\n",
		default_calib.event_trigger,
		default_calib.flow_trigger);

	/* Run time image */
	default_calib.event_trigger = cpu_to_le32(IWL_CALIB_DEFAULT_EVENT_RUN);
	default_calib.flow_trigger = cpu_to_le32(IWL_CALIB_DEFAULT_FLOW_RUN);

	if (default_calib.event_trigger !=
	    mvm->fw->default_calib[IWL_UCODE_REGULAR].event_trigger)
		IWL_ERR(mvm,
			"Updating the event calib for RT image: 0x%x -> 0x%x\n",
			mvm->fw->default_calib[IWL_UCODE_REGULAR].event_trigger,
			default_calib.event_trigger);
	if (default_calib.flow_trigger !=
	    mvm->fw->default_calib[IWL_UCODE_REGULAR].flow_trigger)
		IWL_ERR(mvm,
			"Updating the flow calib for RT image: 0x%x -> 0x%x\n",
			mvm->fw->default_calib[IWL_UCODE_REGULAR].flow_trigger,
			default_calib.flow_trigger);

	memcpy((void *)&mvm->fw->default_calib[IWL_UCODE_REGULAR],
	       &default_calib, sizeof(struct iwl_tlv_calib_ctrl));
	IWL_ERR(mvm,
		"Setting uCode runtime calibs event 0x%x, trigger 0x%x\n",
		default_calib.event_trigger,
		default_calib.flow_trigger);
}

static int iwl_set_default_calibrations(struct iwl_mvm *mvm)
{
	u8 cmd_raw[16]; /* holds the variable size commands */
	struct iwl_set_calib_default_cmd *cmd =
		(struct iwl_set_calib_default_cmd *)cmd_raw;
	int ret, i;

	/* Setting default values for calibrations we don't run */
	for (i = 0; i < ARRAY_SIZE(wkp_calib_default_data); i++) {
		u16 cmd_len;

		if (wkp_calib_default_data[i].size == 0)
			continue;

		memset(cmd_raw, 0, sizeof(cmd_raw));
		cmd_len = wkp_calib_default_data[i].size + sizeof(cmd);
		cmd->calib_index = cpu_to_le16(i);
		cmd->length = cpu_to_le16(wkp_calib_default_data[i].size);
		if (WARN_ONCE(cmd_len > sizeof(cmd_raw),
			      "Need to enlarge cmd_raw to %d\n", cmd_len))
			break;
		memcpy(cmd->data, wkp_calib_default_data[i].data,
		       wkp_calib_default_data[i].size);
		ret = iwl_mvm_send_cmd_pdu(mvm, SET_CALIB_DEFAULT_CMD, 0,
					   sizeof(*cmd) +
					   wkp_calib_default_data[i].size,
					   cmd);
		if (ret)
			return ret;
	}

	return 0;
}

int iwl_run_init_mvm_ucode(struct iwl_mvm *mvm, bool read_nvm)
{
	struct iwl_notification_wait calib_wait;
	static const u8 init_complete[] = {
		INIT_COMPLETE_NOTIF,
		CALIB_RES_NOTIF_PHY_DB
	};
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (mvm->init_ucode_run)
		return 0;

	iwl_init_notification_wait(&mvm->notif_wait,
				   &calib_wait,
				   init_complete,
				   ARRAY_SIZE(init_complete),
				   iwl_wait_phy_db_entry,
				   mvm->phy_db);

	/* Will also start the device */
	ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_INIT);
	if (ret) {
		IWL_ERR(mvm, "Failed to start INIT ucode: %d\n", ret);
		goto error;
	}

	if (read_nvm) {
		/* Read nvm */
		ret = iwl_nvm_init(mvm);
		if (ret) {
			IWL_ERR(mvm, "Failed to read NVM: %d\n", ret);
			goto error;
		}
	}

	ret = iwl_nvm_check_version(mvm->nvm_data, mvm->trans);
	WARN_ON(ret);

	/* Send TX valid antennas before triggering calibrations */
	ret = iwl_send_tx_ant_cfg(mvm, mvm->nvm_data->valid_tx_ant);
	if (ret)
		goto error;

	/* Override the calibrations from TLV and the const of fw */
	iwl_set_default_calib_trigger(mvm);

	/* WkP doesn't have all calibrations, need to set default values */
	if (mvm->cfg->device_family == IWL_DEVICE_FAMILY_7000) {
		ret = iwl_set_default_calibrations(mvm);
		if (ret)
			goto error;
	}

	/*
	 * Send phy configurations command to init uCode
	 * to start the 16.0 uCode init image internal calibrations.
	 */
	ret = iwl_send_phy_cfg_cmd(mvm);
	if (ret) {
		IWL_ERR(mvm, "Failed to run INIT calibrations: %d\n",
			ret);
		goto error;
	}

	/*
	 * Some things may run in the background now, but we
	 * just wait for the calibration complete notification.
	 */
	ret = iwl_wait_notification(&mvm->notif_wait, &calib_wait,
			MVM_UCODE_CALIB_TIMEOUT);
	if (!ret)
		mvm->init_ucode_run = true;
	goto out;

error:
	iwl_remove_notification(&mvm->notif_wait, &calib_wait);
out:
	if (!iwlmvm_mod_params.init_dbg) {
		iwl_trans_stop_device(mvm->trans);
	} else if (!mvm->nvm_data) {
		/* we want to debug INIT and we have no NVM - fake */
		mvm->nvm_data = kzalloc(sizeof(struct iwl_nvm_data) +
					sizeof(struct ieee80211_channel) +
					sizeof(struct ieee80211_rate),
					GFP_KERNEL);
		if (!mvm->nvm_data)
			return -ENOMEM;
		mvm->nvm_data->valid_rx_ant = 1;
		mvm->nvm_data->valid_tx_ant = 1;
		mvm->nvm_data->bands[0].channels = mvm->nvm_data->channels;
		mvm->nvm_data->bands[0].n_channels = 1;
		mvm->nvm_data->bands[0].n_bitrates = 1;
		mvm->nvm_data->bands[0].bitrates =
			(void *)mvm->nvm_data->channels + 1;
		mvm->nvm_data->bands[0].bitrates->hw_value = 10;
	}

	return ret;
}

#define UCODE_CALIB_TIMEOUT	(2*HZ)

int iwl_mvm_up(struct iwl_mvm *mvm)
{
	int ret, i;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_trans_start_hw(mvm->trans);
	if (ret)
		return ret;

	/* If we were in RFKILL during module loading, load init ucode now */
	if (!mvm->init_ucode_run) {
		ret = iwl_run_init_mvm_ucode(mvm, false);
		if (ret && !iwlmvm_mod_params.init_dbg) {
			IWL_ERR(mvm, "Failed to run INIT ucode: %d\n", ret);
			goto error;
		}
	}

	if (iwlmvm_mod_params.init_dbg)
		return 0;

	ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_REGULAR);
	if (ret) {
		IWL_ERR(mvm, "Failed to start RT ucode: %d\n", ret);
		goto error;
	}

	ret = iwl_send_tx_ant_cfg(mvm, mvm->nvm_data->valid_tx_ant);
	if (ret)
		goto error;

	/* Send phy db control command and then phy db calibration*/
	ret = iwl_send_phy_db_data(mvm->phy_db);
	if (ret)
		goto error;

	ret = iwl_send_phy_cfg_cmd(mvm);
	if (ret)
		goto error;

	/* init the fw <-> mac80211 STA mapping */
	for (i = 0; i < IWL_MVM_STATION_COUNT; i++)
		RCU_INIT_POINTER(mvm->fw_id_to_mac_id[i], NULL);

	/* Add auxiliary station for scanning */
	ret = iwl_mvm_add_aux_sta(mvm);
	if (ret)
		goto error;

	IWL_DEBUG_INFO(mvm, "RT uCode started.\n");

	return 0;
 error:
	iwl_trans_stop_device(mvm->trans);
	return ret;
}

int iwl_mvm_load_d3_fw(struct iwl_mvm *mvm)
{
	int ret, i;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_trans_start_hw(mvm->trans);
	if (ret)
		return ret;

	ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_WOWLAN);
	if (ret) {
		IWL_ERR(mvm, "Failed to start WoWLAN firmware: %d\n", ret);
		goto error;
	}

	ret = iwl_send_tx_ant_cfg(mvm, mvm->nvm_data->valid_tx_ant);
	if (ret)
		goto error;

	/* Send phy db control command and then phy db calibration*/
	ret = iwl_send_phy_db_data(mvm->phy_db);
	if (ret)
		goto error;

	ret = iwl_send_phy_cfg_cmd(mvm);
	if (ret)
		goto error;

	/* init the fw <-> mac80211 STA mapping */
	for (i = 0; i < IWL_MVM_STATION_COUNT; i++)
		RCU_INIT_POINTER(mvm->fw_id_to_mac_id[i], NULL);

	/* Add auxiliary station for scanning */
	ret = iwl_mvm_add_aux_sta(mvm);
	if (ret)
		goto error;

	return 0;
 error:
	iwl_trans_stop_device(mvm->trans);
	return ret;
}

int iwl_mvm_rx_card_state_notif(struct iwl_mvm *mvm,
				    struct iwl_rx_cmd_buffer *rxb,
				    struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_card_state_notif *card_state_notif = (void *)pkt->data;
	u32 flags = le32_to_cpu(card_state_notif->flags);

	IWL_DEBUG_RF_KILL(mvm, "Card state received: HW:%s SW:%s CT:%s\n",
			  (flags & HW_CARD_DISABLED) ? "Kill" : "On",
			  (flags & SW_CARD_DISABLED) ? "Kill" : "On",
			  (flags & CT_KILL_CARD_DISABLED) ?
			  "Reached" : "Not reached");

	return 0;
}

int iwl_mvm_rx_radio_ver(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb,
			 struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_radio_version_notif *radio_version = (void *)pkt->data;

	/* TODO: what to do with that? */
	IWL_DEBUG_INFO(mvm,
		       "Radio version: flavor: 0x%08x, step 0x%08x, dash 0x%08x\n",
		       le32_to_cpu(radio_version->radio_flavor),
		       le32_to_cpu(radio_version->radio_step),
		       le32_to_cpu(radio_version->radio_dash));
	return 0;
}
