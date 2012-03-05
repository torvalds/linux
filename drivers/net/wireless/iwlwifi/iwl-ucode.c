/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2012 Intel Corporation. All rights reserved.
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
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>

#include "iwl-ucode.h"
#include "iwl-wifi.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-agn-hw.h"
#include "iwl-agn.h"
#include "iwl-agn-calib.h"
#include "iwl-trans.h"
#include "iwl-fh.h"
#include "iwl-op-mode.h"

static struct iwl_wimax_coex_event_entry cu_priorities[COEX_NUM_OF_EVENTS] = {
	{COEX_CU_UNASSOC_IDLE_RP, COEX_CU_UNASSOC_IDLE_WP,
	 0, COEX_UNASSOC_IDLE_FLAGS},
	{COEX_CU_UNASSOC_MANUAL_SCAN_RP, COEX_CU_UNASSOC_MANUAL_SCAN_WP,
	 0, COEX_UNASSOC_MANUAL_SCAN_FLAGS},
	{COEX_CU_UNASSOC_AUTO_SCAN_RP, COEX_CU_UNASSOC_AUTO_SCAN_WP,
	 0, COEX_UNASSOC_AUTO_SCAN_FLAGS},
	{COEX_CU_CALIBRATION_RP, COEX_CU_CALIBRATION_WP,
	 0, COEX_CALIBRATION_FLAGS},
	{COEX_CU_PERIODIC_CALIBRATION_RP, COEX_CU_PERIODIC_CALIBRATION_WP,
	 0, COEX_PERIODIC_CALIBRATION_FLAGS},
	{COEX_CU_CONNECTION_ESTAB_RP, COEX_CU_CONNECTION_ESTAB_WP,
	 0, COEX_CONNECTION_ESTAB_FLAGS},
	{COEX_CU_ASSOCIATED_IDLE_RP, COEX_CU_ASSOCIATED_IDLE_WP,
	 0, COEX_ASSOCIATED_IDLE_FLAGS},
	{COEX_CU_ASSOC_MANUAL_SCAN_RP, COEX_CU_ASSOC_MANUAL_SCAN_WP,
	 0, COEX_ASSOC_MANUAL_SCAN_FLAGS},
	{COEX_CU_ASSOC_AUTO_SCAN_RP, COEX_CU_ASSOC_AUTO_SCAN_WP,
	 0, COEX_ASSOC_AUTO_SCAN_FLAGS},
	{COEX_CU_ASSOC_ACTIVE_LEVEL_RP, COEX_CU_ASSOC_ACTIVE_LEVEL_WP,
	 0, COEX_ASSOC_ACTIVE_LEVEL_FLAGS},
	{COEX_CU_RF_ON_RP, COEX_CU_RF_ON_WP, 0, COEX_CU_RF_ON_FLAGS},
	{COEX_CU_RF_OFF_RP, COEX_CU_RF_OFF_WP, 0, COEX_RF_OFF_FLAGS},
	{COEX_CU_STAND_ALONE_DEBUG_RP, COEX_CU_STAND_ALONE_DEBUG_WP,
	 0, COEX_STAND_ALONE_DEBUG_FLAGS},
	{COEX_CU_IPAN_ASSOC_LEVEL_RP, COEX_CU_IPAN_ASSOC_LEVEL_WP,
	 0, COEX_IPAN_ASSOC_LEVEL_FLAGS},
	{COEX_CU_RSRVD1_RP, COEX_CU_RSRVD1_WP, 0, COEX_RSRVD1_FLAGS},
	{COEX_CU_RSRVD2_RP, COEX_CU_RSRVD2_WP, 0, COEX_RSRVD2_FLAGS}
};

/******************************************************************************
 *
 * uCode download functions
 *
 ******************************************************************************/

static void iwl_free_fw_desc(struct iwl_nic *nic, struct fw_desc *desc)
{
	if (desc->v_addr)
		dma_free_coherent(trans(nic)->dev, desc->len,
				  desc->v_addr, desc->p_addr);
	desc->v_addr = NULL;
	desc->len = 0;
}

static void iwl_free_fw_img(struct iwl_nic *nic, struct fw_img *img)
{
	iwl_free_fw_desc(nic, &img->code);
	iwl_free_fw_desc(nic, &img->data);
}

void iwl_dealloc_ucode(struct iwl_nic *nic)
{
	iwl_free_fw_img(nic, &nic->fw.ucode_rt);
	iwl_free_fw_img(nic, &nic->fw.ucode_init);
	iwl_free_fw_img(nic, &nic->fw.ucode_wowlan);
}

static int iwl_alloc_fw_desc(struct iwl_nic *nic, struct fw_desc *desc,
		      const void *data, size_t len)
{
	if (!len) {
		desc->v_addr = NULL;
		return -EINVAL;
	}

	desc->v_addr = dma_alloc_coherent(trans(nic)->dev, len,
					  &desc->p_addr, GFP_KERNEL);
	if (!desc->v_addr)
		return -ENOMEM;

	desc->len = len;
	memcpy(desc->v_addr, data, len);
	return 0;
}

static inline struct fw_img *iwl_get_ucode_image(struct iwl_nic *nic,
					enum iwl_ucode_type ucode_type)
{
	switch (ucode_type) {
	case IWL_UCODE_INIT:
		return &nic->fw.ucode_init;
	case IWL_UCODE_WOWLAN:
		return &nic->fw.ucode_wowlan;
	case IWL_UCODE_REGULAR:
		return &nic->fw.ucode_rt;
	case IWL_UCODE_NONE:
		break;
	}
	return NULL;
}

/*
 *  Calibration
 */
static int iwl_set_Xtal_calib(struct iwl_trans *trans)
{
	struct iwl_calib_xtal_freq_cmd cmd;
	__le16 *xtal_calib =
		(__le16 *)iwl_eeprom_query_addr(trans->shrd, EEPROM_XTAL);

	iwl_set_calib_hdr(&cmd.hdr, IWL_PHY_CALIBRATE_CRYSTAL_FRQ_CMD);
	cmd.cap_pin1 = le16_to_cpu(xtal_calib[0]);
	cmd.cap_pin2 = le16_to_cpu(xtal_calib[1]);
	return iwl_calib_set(trans, (void *)&cmd, sizeof(cmd));
}

static int iwl_set_temperature_offset_calib(struct iwl_trans *trans)
{
	struct iwl_calib_temperature_offset_cmd cmd;
	__le16 *offset_calib =
		(__le16 *)iwl_eeprom_query_addr(trans->shrd,
						EEPROM_RAW_TEMPERATURE);

	memset(&cmd, 0, sizeof(cmd));
	iwl_set_calib_hdr(&cmd.hdr, IWL_PHY_CALIBRATE_TEMP_OFFSET_CMD);
	memcpy(&cmd.radio_sensor_offset, offset_calib, sizeof(*offset_calib));
	if (!(cmd.radio_sensor_offset))
		cmd.radio_sensor_offset = DEFAULT_RADIO_SENSOR_OFFSET;

	IWL_DEBUG_CALIB(trans, "Radio sensor offset: %d\n",
			le16_to_cpu(cmd.radio_sensor_offset));
	return iwl_calib_set(trans, (void *)&cmd, sizeof(cmd));
}

static int iwl_set_temperature_offset_calib_v2(struct iwl_trans *trans)
{
	struct iwl_calib_temperature_offset_v2_cmd cmd;
	__le16 *offset_calib_high = (__le16 *)iwl_eeprom_query_addr(trans->shrd,
				     EEPROM_KELVIN_TEMPERATURE);
	__le16 *offset_calib_low =
		(__le16 *)iwl_eeprom_query_addr(trans->shrd,
						EEPROM_RAW_TEMPERATURE);
	struct iwl_eeprom_calib_hdr *hdr;

	memset(&cmd, 0, sizeof(cmd));
	iwl_set_calib_hdr(&cmd.hdr, IWL_PHY_CALIBRATE_TEMP_OFFSET_CMD);
	hdr = (struct iwl_eeprom_calib_hdr *)iwl_eeprom_query_addr(trans->shrd,
							EEPROM_CALIB_ALL);
	memcpy(&cmd.radio_sensor_offset_high, offset_calib_high,
		sizeof(*offset_calib_high));
	memcpy(&cmd.radio_sensor_offset_low, offset_calib_low,
		sizeof(*offset_calib_low));
	if (!(cmd.radio_sensor_offset_low)) {
		IWL_DEBUG_CALIB(trans, "no info in EEPROM, use default\n");
		cmd.radio_sensor_offset_low = DEFAULT_RADIO_SENSOR_OFFSET;
		cmd.radio_sensor_offset_high = DEFAULT_RADIO_SENSOR_OFFSET;
	}
	memcpy(&cmd.burntVoltageRef, &hdr->voltage,
		sizeof(hdr->voltage));

	IWL_DEBUG_CALIB(trans, "Radio sensor offset high: %d\n",
			le16_to_cpu(cmd.radio_sensor_offset_high));
	IWL_DEBUG_CALIB(trans, "Radio sensor offset low: %d\n",
			le16_to_cpu(cmd.radio_sensor_offset_low));
	IWL_DEBUG_CALIB(trans, "Voltage Ref: %d\n",
			le16_to_cpu(cmd.burntVoltageRef));

	return iwl_calib_set(trans, (void *)&cmd, sizeof(cmd));
}

static int iwl_send_calib_cfg(struct iwl_trans *trans)
{
	struct iwl_calib_cfg_cmd calib_cfg_cmd;
	struct iwl_host_cmd cmd = {
		.id = CALIBRATION_CFG_CMD,
		.len = { sizeof(struct iwl_calib_cfg_cmd), },
		.data = { &calib_cfg_cmd, },
	};

	memset(&calib_cfg_cmd, 0, sizeof(calib_cfg_cmd));
	calib_cfg_cmd.ucd_calib_cfg.once.is_enable = IWL_CALIB_INIT_CFG_ALL;
	calib_cfg_cmd.ucd_calib_cfg.once.start = IWL_CALIB_INIT_CFG_ALL;
	calib_cfg_cmd.ucd_calib_cfg.once.send_res = IWL_CALIB_INIT_CFG_ALL;
	calib_cfg_cmd.ucd_calib_cfg.flags =
		IWL_CALIB_CFG_FLAG_SEND_COMPLETE_NTFY_MSK;

	return iwl_trans_send_cmd(trans, &cmd);
}

int iwlagn_rx_calib_result(struct iwl_priv *priv,
			    struct iwl_rx_mem_buffer *rxb,
			    struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_calib_hdr *hdr = (struct iwl_calib_hdr *)pkt->u.raw;
	int len = le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK;

	/* reduce the size of the length field itself */
	len -= 4;

	if (iwl_calib_set(trans(priv), hdr, len))
		IWL_ERR(priv, "Failed to record calibration data %d\n",
			hdr->op_code);

	return 0;
}

int iwl_init_alive_start(struct iwl_trans *trans)
{
	int ret;

	if (cfg(trans)->bt_params &&
	    cfg(trans)->bt_params->advanced_bt_coexist) {
		/*
		 * Tell uCode we are ready to perform calibration
		 * need to perform this before any calibration
		 * no need to close the envlope since we are going
		 * to load the runtime uCode later.
		 */
		ret = iwl_send_bt_env(trans, IWL_BT_COEX_ENV_OPEN,
			BT_COEX_PRIO_TBL_EVT_INIT_CALIB2);
		if (ret)
			return ret;

	}

	ret = iwl_send_calib_cfg(trans);
	if (ret)
		return ret;

	/**
	 * temperature offset calibration is only needed for runtime ucode,
	 * so prepare the value now.
	 */
	if (cfg(trans)->need_temp_offset_calib) {
		if (cfg(trans)->temp_offset_v2)
			return iwl_set_temperature_offset_calib_v2(trans);
		else
			return iwl_set_temperature_offset_calib(trans);
	}

	return 0;
}

static int iwl_send_wimax_coex(struct iwl_trans *trans)
{
	struct iwl_wimax_coex_cmd coex_cmd;

	if (cfg(trans)->base_params->support_wimax_coexist) {
		/* UnMask wake up src at associated sleep */
		coex_cmd.flags = COEX_FLAGS_ASSOC_WA_UNMASK_MSK;

		/* UnMask wake up src at unassociated sleep */
		coex_cmd.flags |= COEX_FLAGS_UNASSOC_WA_UNMASK_MSK;
		memcpy(coex_cmd.sta_prio, cu_priorities,
			sizeof(struct iwl_wimax_coex_event_entry) *
			 COEX_NUM_OF_EVENTS);

		/* enabling the coexistence feature */
		coex_cmd.flags |= COEX_FLAGS_COEX_ENABLE_MSK;

		/* enabling the priorities tables */
		coex_cmd.flags |= COEX_FLAGS_STA_TABLE_VALID_MSK;
	} else {
		/* coexistence is disabled */
		memset(&coex_cmd, 0, sizeof(coex_cmd));
	}
	return iwl_trans_send_cmd_pdu(trans,
				COEX_PRIORITY_TABLE_CMD, CMD_SYNC,
				sizeof(coex_cmd), &coex_cmd);
}

static const u8 iwl_bt_prio_tbl[BT_COEX_PRIO_TBL_EVT_MAX] = {
	((BT_COEX_PRIO_TBL_PRIO_BYPASS << IWL_BT_COEX_PRIO_TBL_PRIO_POS) |
		(0 << IWL_BT_COEX_PRIO_TBL_SHARED_ANTENNA_POS)),
	((BT_COEX_PRIO_TBL_PRIO_BYPASS << IWL_BT_COEX_PRIO_TBL_PRIO_POS) |
		(1 << IWL_BT_COEX_PRIO_TBL_SHARED_ANTENNA_POS)),
	((BT_COEX_PRIO_TBL_PRIO_LOW << IWL_BT_COEX_PRIO_TBL_PRIO_POS) |
		(0 << IWL_BT_COEX_PRIO_TBL_SHARED_ANTENNA_POS)),
	((BT_COEX_PRIO_TBL_PRIO_LOW << IWL_BT_COEX_PRIO_TBL_PRIO_POS) |
		(1 << IWL_BT_COEX_PRIO_TBL_SHARED_ANTENNA_POS)),
	((BT_COEX_PRIO_TBL_PRIO_HIGH << IWL_BT_COEX_PRIO_TBL_PRIO_POS) |
		(0 << IWL_BT_COEX_PRIO_TBL_SHARED_ANTENNA_POS)),
	((BT_COEX_PRIO_TBL_PRIO_HIGH << IWL_BT_COEX_PRIO_TBL_PRIO_POS) |
		(1 << IWL_BT_COEX_PRIO_TBL_SHARED_ANTENNA_POS)),
	((BT_COEX_PRIO_TBL_PRIO_BYPASS << IWL_BT_COEX_PRIO_TBL_PRIO_POS) |
		(0 << IWL_BT_COEX_PRIO_TBL_SHARED_ANTENNA_POS)),
	((BT_COEX_PRIO_TBL_PRIO_COEX_OFF << IWL_BT_COEX_PRIO_TBL_PRIO_POS) |
		(0 << IWL_BT_COEX_PRIO_TBL_SHARED_ANTENNA_POS)),
	((BT_COEX_PRIO_TBL_PRIO_COEX_ON << IWL_BT_COEX_PRIO_TBL_PRIO_POS) |
		(0 << IWL_BT_COEX_PRIO_TBL_SHARED_ANTENNA_POS)),
	0, 0, 0, 0, 0, 0, 0
};

void iwl_send_prio_tbl(struct iwl_trans *trans)
{
	struct iwl_bt_coex_prio_table_cmd prio_tbl_cmd;

	memcpy(prio_tbl_cmd.prio_tbl, iwl_bt_prio_tbl,
		sizeof(iwl_bt_prio_tbl));
	if (iwl_trans_send_cmd_pdu(trans,
				REPLY_BT_COEX_PRIO_TABLE, CMD_SYNC,
				sizeof(prio_tbl_cmd), &prio_tbl_cmd))
		IWL_ERR(trans, "failed to send BT prio tbl command\n");
}

int iwl_send_bt_env(struct iwl_trans *trans, u8 action, u8 type)
{
	struct iwl_bt_coex_prot_env_cmd env_cmd;
	int ret;

	env_cmd.action = action;
	env_cmd.type = type;
	ret = iwl_trans_send_cmd_pdu(trans,
			       REPLY_BT_COEX_PROT_ENV, CMD_SYNC,
			       sizeof(env_cmd), &env_cmd);
	if (ret)
		IWL_ERR(trans, "failed to send BT env command\n");
	return ret;
}


static int iwl_alive_notify(struct iwl_trans *trans)
{
	struct iwl_priv *priv = priv(trans);
	struct iwl_rxon_context *ctx;
	int ret;

	if (!priv->tx_cmd_pool)
		priv->tx_cmd_pool =
			kmem_cache_create("iwl_dev_cmd",
					  sizeof(struct iwl_device_cmd),
					  sizeof(void *), 0, NULL);

	if (!priv->tx_cmd_pool)
		return -ENOMEM;

	iwl_trans_fw_alive(trans);
	for_each_context(priv, ctx)
		ctx->last_tx_rejected = false;

	ret = iwl_send_wimax_coex(trans);
	if (ret)
		return ret;

	if (!cfg(priv)->no_xtal_calib) {
		ret = iwl_set_Xtal_calib(trans);
		if (ret)
			return ret;
	}

	return iwl_send_calib_results(trans);
}


/**
 * iwl_verify_inst_sparse - verify runtime uCode image in card vs. host,
 *   using sample data 100 bytes apart.  If these sample points are good,
 *   it's a pretty good bet that everything between them is good, too.
 */
static int iwl_verify_inst_sparse(struct iwl_nic *nic,
				      struct fw_desc *fw_desc)
{
	struct iwl_trans *trans = trans(nic);
	__le32 *image = (__le32 *)fw_desc->v_addr;
	u32 len = fw_desc->len;
	u32 val;
	u32 i;

	IWL_DEBUG_FW(nic, "ucode inst image size is %u\n", len);

	for (i = 0; i < len; i += 100, image += 100/sizeof(u32)) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		iwl_write_direct32(trans, HBUS_TARG_MEM_RADDR,
			i + IWLAGN_RTC_INST_LOWER_BOUND);
		val = iwl_read32(trans, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image))
			return -EIO;
	}

	return 0;
}

static void iwl_print_mismatch_inst(struct iwl_nic *nic,
				    struct fw_desc *fw_desc)
{
	struct iwl_trans *trans = trans(nic);
	__le32 *image = (__le32 *)fw_desc->v_addr;
	u32 len = fw_desc->len;
	u32 val;
	u32 offs;
	int errors = 0;

	IWL_DEBUG_FW(nic, "ucode inst image size is %u\n", len);

	iwl_write_direct32(trans, HBUS_TARG_MEM_RADDR,
			   IWLAGN_RTC_INST_LOWER_BOUND);

	for (offs = 0;
	     offs < len && errors < 20;
	     offs += sizeof(u32), image++) {
		/* read data comes through single port, auto-incr addr */
		val = iwl_read32(trans, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
			IWL_ERR(nic, "uCode INST section at "
				"offset 0x%x, is 0x%x, s/b 0x%x\n",
				offs, val, le32_to_cpu(*image));
			errors++;
		}
	}
}

/**
 * iwl_verify_ucode - determine which instruction image is in SRAM,
 *    and verify its contents
 */
static int iwl_verify_ucode(struct iwl_nic *nic,
			    enum iwl_ucode_type ucode_type)
{
	struct fw_img *img = iwl_get_ucode_image(nic, ucode_type);

	if (!img) {
		IWL_ERR(nic, "Invalid ucode requested (%d)\n", ucode_type);
		return -EINVAL;
	}

	if (!iwl_verify_inst_sparse(nic, &img->code)) {
		IWL_DEBUG_FW(nic, "uCode is good in inst SRAM\n");
		return 0;
	}

	IWL_ERR(nic, "UCODE IMAGE IN INSTRUCTION SRAM NOT VALID!!\n");

	iwl_print_mismatch_inst(nic, &img->code);
	return -EIO;
}

struct iwl_alive_data {
	bool valid;
	u8 subtype;
};

static void iwl_alive_fn(struct iwl_trans *trans,
			    struct iwl_rx_packet *pkt,
			    void *data)
{
	struct iwl_alive_data *alive_data = data;
	struct iwl_alive_resp *palive;

	palive = &pkt->u.alive_frame;

	IWL_DEBUG_FW(trans, "Alive ucode status 0x%08X revision "
		       "0x%01X 0x%01X\n",
		       palive->is_valid, palive->ver_type,
		       palive->ver_subtype);

	trans->shrd->device_pointers.error_event_table =
		le32_to_cpu(palive->error_event_table_ptr);
	trans->shrd->device_pointers.log_event_table =
		le32_to_cpu(palive->log_event_table_ptr);

	alive_data->subtype = palive->ver_subtype;
	alive_data->valid = palive->is_valid == UCODE_VALID_OK;
}

/* notification wait support */
void iwl_init_notification_wait(struct iwl_shared *shrd,
				   struct iwl_notification_wait *wait_entry,
				   u8 cmd,
				   void (*fn)(struct iwl_trans *trans,
					      struct iwl_rx_packet *pkt,
					      void *data),
				   void *fn_data)
{
	wait_entry->fn = fn;
	wait_entry->fn_data = fn_data;
	wait_entry->cmd = cmd;
	wait_entry->triggered = false;
	wait_entry->aborted = false;

	spin_lock_bh(&shrd->notif_wait_lock);
	list_add(&wait_entry->list, &shrd->notif_waits);
	spin_unlock_bh(&shrd->notif_wait_lock);
}

int iwl_wait_notification(struct iwl_shared *shrd,
			     struct iwl_notification_wait *wait_entry,
			     unsigned long timeout)
{
	int ret;

	ret = wait_event_timeout(shrd->notif_waitq,
				 wait_entry->triggered || wait_entry->aborted,
				 timeout);

	spin_lock_bh(&shrd->notif_wait_lock);
	list_del(&wait_entry->list);
	spin_unlock_bh(&shrd->notif_wait_lock);

	if (wait_entry->aborted)
		return -EIO;

	/* return value is always >= 0 */
	if (ret <= 0)
		return -ETIMEDOUT;
	return 0;
}

void iwl_remove_notification(struct iwl_shared *shrd,
				struct iwl_notification_wait *wait_entry)
{
	spin_lock_bh(&shrd->notif_wait_lock);
	list_del(&wait_entry->list);
	spin_unlock_bh(&shrd->notif_wait_lock);
}

void iwl_abort_notification_waits(struct iwl_shared *shrd)
{
	unsigned long flags;
	struct iwl_notification_wait *wait_entry;

	spin_lock_irqsave(&shrd->notif_wait_lock, flags);
	list_for_each_entry(wait_entry, &shrd->notif_waits, list)
		wait_entry->aborted = true;
	spin_unlock_irqrestore(&shrd->notif_wait_lock, flags);

	wake_up_all(&shrd->notif_waitq);
}

#define UCODE_ALIVE_TIMEOUT	HZ
#define UCODE_CALIB_TIMEOUT	(2*HZ)

int iwl_load_ucode_wait_alive(struct iwl_trans *trans,
				 enum iwl_ucode_type ucode_type)
{
	struct iwl_notification_wait alive_wait;
	struct iwl_alive_data alive_data;
	struct fw_img *fw;
	int ret;
	enum iwl_ucode_type old_type;

	iwl_init_notification_wait(trans->shrd, &alive_wait, REPLY_ALIVE,
				      iwl_alive_fn, &alive_data);

	old_type = trans->shrd->ucode_type;
	trans->shrd->ucode_type = ucode_type;
	fw = iwl_get_ucode_image(nic(trans), ucode_type);

	if (!fw)
		return -EINVAL;

	ret = iwl_trans_start_fw(trans, fw);
	if (ret) {
		trans->shrd->ucode_type = old_type;
		iwl_remove_notification(trans->shrd, &alive_wait);
		return ret;
	}

	/*
	 * Some things may run in the background now, but we
	 * just wait for the ALIVE notification here.
	 */
	ret = iwl_wait_notification(trans->shrd, &alive_wait,
					UCODE_ALIVE_TIMEOUT);
	if (ret) {
		trans->shrd->ucode_type = old_type;
		return ret;
	}

	if (!alive_data.valid) {
		IWL_ERR(trans, "Loaded ucode is not valid!\n");
		trans->shrd->ucode_type = old_type;
		return -EIO;
	}

	/*
	 * This step takes a long time (60-80ms!!) and
	 * WoWLAN image should be loaded quickly, so
	 * skip it for WoWLAN.
	 */
	if (ucode_type != IWL_UCODE_WOWLAN) {
		ret = iwl_verify_ucode(nic(trans), ucode_type);
		if (ret) {
			trans->shrd->ucode_type = old_type;
			return ret;
		}

		/* delay a bit to give rfkill time to run */
		msleep(5);
	}

	ret = iwl_alive_notify(trans);
	if (ret) {
		IWL_WARN(trans,
			"Could not complete ALIVE transition: %d\n", ret);
		trans->shrd->ucode_type = old_type;
		return ret;
	}

	return 0;
}

int iwl_run_init_ucode(struct iwl_trans *trans)
{
	struct iwl_notification_wait calib_wait;
	int ret;

	lockdep_assert_held(&trans->shrd->mutex);

	/* No init ucode required? Curious, but maybe ok */
	if (!nic(trans)->fw.ucode_init.code.len)
		return 0;

	if (trans->shrd->ucode_type != IWL_UCODE_NONE)
		return 0;

	iwl_init_notification_wait(trans->shrd, &calib_wait,
				      CALIBRATION_COMPLETE_NOTIFICATION,
				      NULL, NULL);

	/* Will also start the device */
	ret = iwl_load_ucode_wait_alive(trans, IWL_UCODE_INIT);
	if (ret)
		goto error;

	ret = iwl_init_alive_start(trans);
	if (ret)
		goto error;

	/*
	 * Some things may run in the background now, but we
	 * just wait for the calibration complete notification.
	 */
	ret = iwl_wait_notification(trans->shrd, &calib_wait,
					UCODE_CALIB_TIMEOUT);

	goto out;

 error:
	iwl_remove_notification(trans->shrd, &calib_wait);
 out:
	/* Whatever happened, stop the device */
	iwl_trans_stop_device(trans);
	return ret;
}

static void iwl_ucode_callback(const struct firmware *ucode_raw, void *context);

#define UCODE_EXPERIMENTAL_TAG		"exp"

int __must_check iwl_request_firmware(struct iwl_nic *nic, bool first)
{
	struct iwl_cfg *cfg = cfg(nic);
	const char *name_pre = cfg->fw_name_pre;
	char tag[8];

	if (first) {
#ifdef CONFIG_IWLWIFI_DEBUG_EXPERIMENTAL_UCODE
		nic->fw_index = UCODE_EXPERIMENTAL_INDEX;
		strcpy(tag, UCODE_EXPERIMENTAL_TAG);
	} else if (nic->fw_index == UCODE_EXPERIMENTAL_INDEX) {
#endif
		nic->fw_index = cfg->ucode_api_max;
		sprintf(tag, "%d", nic->fw_index);
	} else {
		nic->fw_index--;
		sprintf(tag, "%d", nic->fw_index);
	}

	if (nic->fw_index < cfg->ucode_api_min) {
		IWL_ERR(nic, "no suitable firmware found!\n");
		return -ENOENT;
	}

	sprintf(nic->firmware_name, "%s%s%s", name_pre, tag, ".ucode");

	IWL_DEBUG_INFO(nic, "attempting to load firmware %s'%s'\n",
		       (nic->fw_index == UCODE_EXPERIMENTAL_INDEX)
				? "EXPERIMENTAL " : "",
		       nic->firmware_name);

	return request_firmware_nowait(THIS_MODULE, 1, nic->firmware_name,
				       trans(nic)->dev,
				       GFP_KERNEL, nic, iwl_ucode_callback);
}

struct iwlagn_firmware_pieces {
	const void *inst, *data, *init, *init_data, *wowlan_inst, *wowlan_data;
	size_t inst_size, data_size, init_size, init_data_size,
	       wowlan_inst_size, wowlan_data_size;

	u32 init_evtlog_ptr, init_evtlog_size, init_errlog_ptr;
	u32 inst_evtlog_ptr, inst_evtlog_size, inst_errlog_ptr;
};

static int iwl_parse_v1_v2_firmware(struct iwl_nic *nic,
				       const struct firmware *ucode_raw,
				       struct iwlagn_firmware_pieces *pieces)
{
	struct iwl_ucode_header *ucode = (void *)ucode_raw->data;
	u32 api_ver, hdr_size, build;
	char buildstr[25];
	const u8 *src;

	nic->fw.ucode_ver = le32_to_cpu(ucode->ver);
	api_ver = IWL_UCODE_API(nic->fw.ucode_ver);

	switch (api_ver) {
	default:
		hdr_size = 28;
		if (ucode_raw->size < hdr_size) {
			IWL_ERR(nic, "File size too small!\n");
			return -EINVAL;
		}
		build = le32_to_cpu(ucode->u.v2.build);
		pieces->inst_size = le32_to_cpu(ucode->u.v2.inst_size);
		pieces->data_size = le32_to_cpu(ucode->u.v2.data_size);
		pieces->init_size = le32_to_cpu(ucode->u.v2.init_size);
		pieces->init_data_size = le32_to_cpu(ucode->u.v2.init_data_size);
		src = ucode->u.v2.data;
		break;
	case 0:
	case 1:
	case 2:
		hdr_size = 24;
		if (ucode_raw->size < hdr_size) {
			IWL_ERR(nic, "File size too small!\n");
			return -EINVAL;
		}
		build = 0;
		pieces->inst_size = le32_to_cpu(ucode->u.v1.inst_size);
		pieces->data_size = le32_to_cpu(ucode->u.v1.data_size);
		pieces->init_size = le32_to_cpu(ucode->u.v1.init_size);
		pieces->init_data_size = le32_to_cpu(ucode->u.v1.init_data_size);
		src = ucode->u.v1.data;
		break;
	}

	if (build)
		sprintf(buildstr, " build %u%s", build,
		       (nic->fw_index == UCODE_EXPERIMENTAL_INDEX)
				? " (EXP)" : "");
	else
		buildstr[0] = '\0';

	snprintf(nic->fw.fw_version,
		 sizeof(nic->fw.fw_version),
		 "%u.%u.%u.%u%s",
		 IWL_UCODE_MAJOR(nic->fw.ucode_ver),
		 IWL_UCODE_MINOR(nic->fw.ucode_ver),
		 IWL_UCODE_API(nic->fw.ucode_ver),
		 IWL_UCODE_SERIAL(nic->fw.ucode_ver),
		 buildstr);

	/* Verify size of file vs. image size info in file's header */
	if (ucode_raw->size != hdr_size + pieces->inst_size +
				pieces->data_size + pieces->init_size +
				pieces->init_data_size) {

		IWL_ERR(nic,
			"uCode file size %d does not match expected size\n",
			(int)ucode_raw->size);
		return -EINVAL;
	}

	pieces->inst = src;
	src += pieces->inst_size;
	pieces->data = src;
	src += pieces->data_size;
	pieces->init = src;
	src += pieces->init_size;
	pieces->init_data = src;
	src += pieces->init_data_size;

	return 0;
}

static int iwl_parse_tlv_firmware(struct iwl_nic *nic,
				const struct firmware *ucode_raw,
				struct iwlagn_firmware_pieces *pieces,
				struct iwl_ucode_capabilities *capa)
{
	struct iwl_tlv_ucode_header *ucode = (void *)ucode_raw->data;
	struct iwl_ucode_tlv *tlv;
	size_t len = ucode_raw->size;
	const u8 *data;
	int wanted_alternative = iwlagn_mod_params.wanted_ucode_alternative;
	int tmp;
	u64 alternatives;
	u32 tlv_len;
	enum iwl_ucode_tlv_type tlv_type;
	const u8 *tlv_data;
	char buildstr[25];
	u32 build;

	if (len < sizeof(*ucode)) {
		IWL_ERR(nic, "uCode has invalid length: %zd\n", len);
		return -EINVAL;
	}

	if (ucode->magic != cpu_to_le32(IWL_TLV_UCODE_MAGIC)) {
		IWL_ERR(nic, "invalid uCode magic: 0X%x\n",
			le32_to_cpu(ucode->magic));
		return -EINVAL;
	}

	/*
	 * Check which alternatives are present, and "downgrade"
	 * when the chosen alternative is not present, warning
	 * the user when that happens. Some files may not have
	 * any alternatives, so don't warn in that case.
	 */
	alternatives = le64_to_cpu(ucode->alternatives);
	tmp = wanted_alternative;
	if (wanted_alternative > 63)
		wanted_alternative = 63;
	while (wanted_alternative && !(alternatives & BIT(wanted_alternative)))
		wanted_alternative--;
	if (wanted_alternative && wanted_alternative != tmp)
		IWL_WARN(nic,
			 "uCode alternative %d not available, choosing %d\n",
			 tmp, wanted_alternative);

	nic->fw.ucode_ver = le32_to_cpu(ucode->ver);
	build = le32_to_cpu(ucode->build);

	if (build)
		sprintf(buildstr, " build %u%s", build,
		       (nic->fw_index == UCODE_EXPERIMENTAL_INDEX)
				? " (EXP)" : "");
	else
		buildstr[0] = '\0';

	snprintf(nic->fw.fw_version,
		 sizeof(nic->fw.fw_version),
		 "%u.%u.%u.%u%s",
		 IWL_UCODE_MAJOR(nic->fw.ucode_ver),
		 IWL_UCODE_MINOR(nic->fw.ucode_ver),
		 IWL_UCODE_API(nic->fw.ucode_ver),
		 IWL_UCODE_SERIAL(nic->fw.ucode_ver),
		 buildstr);

	data = ucode->data;

	len -= sizeof(*ucode);

	while (len >= sizeof(*tlv)) {
		u16 tlv_alt;

		len -= sizeof(*tlv);
		tlv = (void *)data;

		tlv_len = le32_to_cpu(tlv->length);
		tlv_type = le16_to_cpu(tlv->type);
		tlv_alt = le16_to_cpu(tlv->alternative);
		tlv_data = tlv->data;

		if (len < tlv_len) {
			IWL_ERR(nic, "invalid TLV len: %zd/%u\n",
				len, tlv_len);
			return -EINVAL;
		}
		len -= ALIGN(tlv_len, 4);
		data += sizeof(*tlv) + ALIGN(tlv_len, 4);

		/*
		 * Alternative 0 is always valid.
		 *
		 * Skip alternative TLVs that are not selected.
		 */
		if (tlv_alt != 0 && tlv_alt != wanted_alternative)
			continue;

		switch (tlv_type) {
		case IWL_UCODE_TLV_INST:
			pieces->inst = tlv_data;
			pieces->inst_size = tlv_len;
			break;
		case IWL_UCODE_TLV_DATA:
			pieces->data = tlv_data;
			pieces->data_size = tlv_len;
			break;
		case IWL_UCODE_TLV_INIT:
			pieces->init = tlv_data;
			pieces->init_size = tlv_len;
			break;
		case IWL_UCODE_TLV_INIT_DATA:
			pieces->init_data = tlv_data;
			pieces->init_data_size = tlv_len;
			break;
		case IWL_UCODE_TLV_BOOT:
			IWL_ERR(nic, "Found unexpected BOOT ucode\n");
			break;
		case IWL_UCODE_TLV_PROBE_MAX_LEN:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->max_probe_length =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_PAN:
			if (tlv_len)
				goto invalid_tlv_len;
			capa->flags |= IWL_UCODE_TLV_FLAGS_PAN;
			break;
		case IWL_UCODE_TLV_FLAGS:
			/* must be at least one u32 */
			if (tlv_len < sizeof(u32))
				goto invalid_tlv_len;
			/* and a proper number of u32s */
			if (tlv_len % sizeof(u32))
				goto invalid_tlv_len;
			/*
			 * This driver only reads the first u32 as
			 * right now no more features are defined,
			 * if that changes then either the driver
			 * will not work with the new firmware, or
			 * it'll not take advantage of new features.
			 */
			capa->flags = le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_EVTLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_evtlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_EVTLOG_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_evtlog_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_ERRLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_errlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_EVTLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_evtlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_EVTLOG_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_evtlog_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_ERRLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_errlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_ENHANCE_SENS_TBL:
			if (tlv_len)
				goto invalid_tlv_len;
			nic->fw.enhance_sensitivity_table = true;
			break;
		case IWL_UCODE_TLV_WOWLAN_INST:
			pieces->wowlan_inst = tlv_data;
			pieces->wowlan_inst_size = tlv_len;
			break;
		case IWL_UCODE_TLV_WOWLAN_DATA:
			pieces->wowlan_data = tlv_data;
			pieces->wowlan_data_size = tlv_len;
			break;
		case IWL_UCODE_TLV_PHY_CALIBRATION_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->standard_phy_calibration_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		default:
			IWL_DEBUG_INFO(nic, "unknown TLV: %d\n", tlv_type);
			break;
		}
	}

	if (len) {
		IWL_ERR(nic, "invalid TLV after parsing: %zd\n", len);
		iwl_print_hex_dump(nic, IWL_DL_FW, (u8 *)data, len);
		return -EINVAL;
	}

	return 0;

 invalid_tlv_len:
	IWL_ERR(nic, "TLV %d has invalid size: %u\n", tlv_type, tlv_len);
	iwl_print_hex_dump(nic, IWL_DL_FW, tlv_data, tlv_len);

	return -EINVAL;
}

/**
 * iwl_ucode_callback - callback when firmware was loaded
 *
 * If loaded successfully, copies the firmware into buffers
 * for the card to fetch (via DMA).
 */
static void iwl_ucode_callback(const struct firmware *ucode_raw, void *context)
{
	struct iwl_nic *nic = context;
	struct iwl_cfg *cfg = cfg(nic);
	struct iwl_fw *fw = &nic->fw;
	struct iwl_ucode_header *ucode;
	int err;
	struct iwlagn_firmware_pieces pieces;
	const unsigned int api_max = cfg->ucode_api_max;
	unsigned int api_ok = cfg->ucode_api_ok;
	const unsigned int api_min = cfg->ucode_api_min;
	u32 api_ver;

	fw->ucode_capa.max_probe_length = 200;
	fw->ucode_capa.standard_phy_calibration_size =
			IWL_DEFAULT_STANDARD_PHY_CALIBRATE_TBL_SIZE;

	if (!api_ok)
		api_ok = api_max;

	memset(&pieces, 0, sizeof(pieces));

	if (!ucode_raw) {
		if (nic->fw_index <= api_ok)
			IWL_ERR(nic,
				"request for firmware file '%s' failed.\n",
				nic->firmware_name);
		goto try_again;
	}

	IWL_DEBUG_INFO(nic, "Loaded firmware file '%s' (%zd bytes).\n",
		       nic->firmware_name, ucode_raw->size);

	/* Make sure that we got at least the API version number */
	if (ucode_raw->size < 4) {
		IWL_ERR(nic, "File size way too small!\n");
		goto try_again;
	}

	/* Data from ucode file:  header followed by uCode images */
	ucode = (struct iwl_ucode_header *)ucode_raw->data;

	if (ucode->ver)
		err = iwl_parse_v1_v2_firmware(nic, ucode_raw, &pieces);
	else
		err = iwl_parse_tlv_firmware(nic, ucode_raw, &pieces,
					   &fw->ucode_capa);

	if (err)
		goto try_again;

	api_ver = IWL_UCODE_API(nic->fw.ucode_ver);

	/*
	 * api_ver should match the api version forming part of the
	 * firmware filename ... but we don't check for that and only rely
	 * on the API version read from firmware header from here on forward
	 */
	/* no api version check required for experimental uCode */
	if (nic->fw_index != UCODE_EXPERIMENTAL_INDEX) {
		if (api_ver < api_min || api_ver > api_max) {
			IWL_ERR(nic,
				"Driver unable to support your firmware API. "
				"Driver supports v%u, firmware is v%u.\n",
				api_max, api_ver);
			goto try_again;
		}

		if (api_ver < api_ok) {
			if (api_ok != api_max)
				IWL_ERR(nic, "Firmware has old API version, "
					"expected v%u through v%u, got v%u.\n",
					api_ok, api_max, api_ver);
			else
				IWL_ERR(nic, "Firmware has old API version, "
					"expected v%u, got v%u.\n",
					api_max, api_ver);
			IWL_ERR(nic, "New firmware can be obtained from "
				      "http://www.intellinuxwireless.org/.\n");
		}
	}

	IWL_INFO(nic, "loaded firmware version %s", nic->fw.fw_version);

	/*
	 * For any of the failures below (before allocating pci memory)
	 * we will try to load a version with a smaller API -- maybe the
	 * user just got a corrupted version of the latest API.
	 */

	IWL_DEBUG_INFO(nic, "f/w package hdr ucode version raw = 0x%x\n",
		       nic->fw.ucode_ver);
	IWL_DEBUG_INFO(nic, "f/w package hdr runtime inst size = %Zd\n",
		       pieces.inst_size);
	IWL_DEBUG_INFO(nic, "f/w package hdr runtime data size = %Zd\n",
		       pieces.data_size);
	IWL_DEBUG_INFO(nic, "f/w package hdr init inst size = %Zd\n",
		       pieces.init_size);
	IWL_DEBUG_INFO(nic, "f/w package hdr init data size = %Zd\n",
		       pieces.init_data_size);

	/* Verify that uCode images will fit in card's SRAM */
	if (pieces.inst_size > cfg->max_inst_size) {
		IWL_ERR(nic, "uCode instr len %Zd too large to fit in\n",
			pieces.inst_size);
		goto try_again;
	}

	if (pieces.data_size > cfg->max_data_size) {
		IWL_ERR(nic, "uCode data len %Zd too large to fit in\n",
			pieces.data_size);
		goto try_again;
	}

	if (pieces.init_size > cfg->max_inst_size) {
		IWL_ERR(nic, "uCode init instr len %Zd too large to fit in\n",
			pieces.init_size);
		goto try_again;
	}

	if (pieces.init_data_size > cfg->max_data_size) {
		IWL_ERR(nic, "uCode init data len %Zd too large to fit in\n",
			pieces.init_data_size);
		goto try_again;
	}

	/* Allocate ucode buffers for card's bus-master loading ... */

	/* Runtime instructions and 2 copies of data:
	 * 1) unmodified from disk
	 * 2) backup cache for save/restore during power-downs */
	if (iwl_alloc_fw_desc(nic, &nic->fw.ucode_rt.code,
			      pieces.inst, pieces.inst_size))
		goto err_pci_alloc;
	if (iwl_alloc_fw_desc(nic, &nic->fw.ucode_rt.data,
			      pieces.data, pieces.data_size))
		goto err_pci_alloc;

	/* Initialization instructions and data */
	if (pieces.init_size && pieces.init_data_size) {
		if (iwl_alloc_fw_desc(nic,
				      &nic->fw.ucode_init.code,
				      pieces.init, pieces.init_size))
			goto err_pci_alloc;
		if (iwl_alloc_fw_desc(nic,
				      &nic->fw.ucode_init.data,
				      pieces.init_data, pieces.init_data_size))
			goto err_pci_alloc;
	}

	/* WoWLAN instructions and data */
	if (pieces.wowlan_inst_size && pieces.wowlan_data_size) {
		if (iwl_alloc_fw_desc(nic,
				      &nic->fw.ucode_wowlan.code,
				      pieces.wowlan_inst,
				      pieces.wowlan_inst_size))
			goto err_pci_alloc;
		if (iwl_alloc_fw_desc(nic,
				      &nic->fw.ucode_wowlan.data,
				      pieces.wowlan_data,
				      pieces.wowlan_data_size))
			goto err_pci_alloc;
	}

	/* Now that we can no longer fail, copy information */

	/*
	 * The (size - 16) / 12 formula is based on the information recorded
	 * for each event, which is of mode 1 (including timestamp) for all
	 * new microcodes that include this information.
	 */
	nic->init_evtlog_ptr = pieces.init_evtlog_ptr;
	if (pieces.init_evtlog_size)
		nic->init_evtlog_size = (pieces.init_evtlog_size - 16)/12;
	else
		nic->init_evtlog_size =
			cfg->base_params->max_event_log_size;
	nic->init_errlog_ptr = pieces.init_errlog_ptr;
	nic->inst_evtlog_ptr = pieces.inst_evtlog_ptr;
	if (pieces.inst_evtlog_size)
		nic->inst_evtlog_size = (pieces.inst_evtlog_size - 16)/12;
	else
		nic->inst_evtlog_size =
			cfg->base_params->max_event_log_size;
	nic->inst_errlog_ptr = pieces.inst_errlog_ptr;

	/*
	 * figure out the offset of chain noise reset and gain commands
	 * base on the size of standard phy calibration commands table size
	 */
	if (fw->ucode_capa.standard_phy_calibration_size >
	    IWL_MAX_PHY_CALIBRATE_TBL_SIZE)
		fw->ucode_capa.standard_phy_calibration_size =
			IWL_MAX_STANDARD_PHY_CALIBRATE_TBL_SIZE;

	/* We have our copies now, allow OS release its copies */
	release_firmware(ucode_raw);
	complete(&nic->request_firmware_complete);

	nic->op_mode = iwl_dvm_ops.start(nic->shrd->trans);

	if (!nic->op_mode)
		goto out_unbind;

	return;

 try_again:
	/* try next, if any */
	release_firmware(ucode_raw);
	if (iwl_request_firmware(nic, false))
		goto out_unbind;
	return;

 err_pci_alloc:
	IWL_ERR(nic, "failed to allocate pci memory\n");
	iwl_dealloc_ucode(nic);
	release_firmware(ucode_raw);
 out_unbind:
	complete(&nic->request_firmware_complete);
	device_release_driver(trans(nic)->dev);
}

