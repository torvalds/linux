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
#include <linux/init.h>

#include "iwl-io.h"
#include "iwl-agn-hw.h"
#include "iwl-trans.h"
#include "iwl-fh.h"
#include "iwl-op-mode.h"

#include "dev.h"
#include "agn.h"
#include "calib.h"

/******************************************************************************
 *
 * uCode download functions
 *
 ******************************************************************************/

static inline const struct fw_img *
iwl_get_ucode_image(struct iwl_priv *priv, enum iwl_ucode_type ucode_type)
{
	if (ucode_type >= IWL_UCODE_TYPE_MAX)
		return NULL;

	return &priv->fw->img[ucode_type];
}

/*
 *  Calibration
 */
static int iwl_set_Xtal_calib(struct iwl_priv *priv)
{
	struct iwl_calib_xtal_freq_cmd cmd;
	__le16 *xtal_calib = priv->eeprom_data->xtal_calib;

	iwl_set_calib_hdr(&cmd.hdr, IWL_PHY_CALIBRATE_CRYSTAL_FRQ_CMD);
	cmd.cap_pin1 = le16_to_cpu(xtal_calib[0]);
	cmd.cap_pin2 = le16_to_cpu(xtal_calib[1]);
	return iwl_calib_set(priv, (void *)&cmd, sizeof(cmd));
}

static int iwl_set_temperature_offset_calib(struct iwl_priv *priv)
{
	struct iwl_calib_temperature_offset_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	iwl_set_calib_hdr(&cmd.hdr, IWL_PHY_CALIBRATE_TEMP_OFFSET_CMD);
	cmd.radio_sensor_offset = priv->eeprom_data->raw_temperature;
	if (!(cmd.radio_sensor_offset))
		cmd.radio_sensor_offset = DEFAULT_RADIO_SENSOR_OFFSET;

	IWL_DEBUG_CALIB(priv, "Radio sensor offset: %d\n",
			le16_to_cpu(cmd.radio_sensor_offset));
	return iwl_calib_set(priv, (void *)&cmd, sizeof(cmd));
}

static int iwl_set_temperature_offset_calib_v2(struct iwl_priv *priv)
{
	struct iwl_calib_temperature_offset_v2_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	iwl_set_calib_hdr(&cmd.hdr, IWL_PHY_CALIBRATE_TEMP_OFFSET_CMD);
	cmd.radio_sensor_offset_high = priv->eeprom_data->kelvin_temperature;
	cmd.radio_sensor_offset_low = priv->eeprom_data->raw_temperature;
	if (!cmd.radio_sensor_offset_low) {
		IWL_DEBUG_CALIB(priv, "no info in EEPROM, use default\n");
		cmd.radio_sensor_offset_low = DEFAULT_RADIO_SENSOR_OFFSET;
		cmd.radio_sensor_offset_high = DEFAULT_RADIO_SENSOR_OFFSET;
	}
	cmd.burntVoltageRef = priv->eeprom_data->calib_voltage;

	IWL_DEBUG_CALIB(priv, "Radio sensor offset high: %d\n",
			le16_to_cpu(cmd.radio_sensor_offset_high));
	IWL_DEBUG_CALIB(priv, "Radio sensor offset low: %d\n",
			le16_to_cpu(cmd.radio_sensor_offset_low));
	IWL_DEBUG_CALIB(priv, "Voltage Ref: %d\n",
			le16_to_cpu(cmd.burntVoltageRef));

	return iwl_calib_set(priv, (void *)&cmd, sizeof(cmd));
}

static int iwl_send_calib_cfg(struct iwl_priv *priv)
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

	return iwl_dvm_send_cmd(priv, &cmd);
}

int iwl_init_alive_start(struct iwl_priv *priv)
{
	int ret;

	if (priv->cfg->bt_params &&
	    priv->cfg->bt_params->advanced_bt_coexist) {
		/*
		 * Tell uCode we are ready to perform calibration
		 * need to perform this before any calibration
		 * no need to close the envlope since we are going
		 * to load the runtime uCode later.
		 */
		ret = iwl_send_bt_env(priv, IWL_BT_COEX_ENV_OPEN,
			BT_COEX_PRIO_TBL_EVT_INIT_CALIB2);
		if (ret)
			return ret;

	}

	ret = iwl_send_calib_cfg(priv);
	if (ret)
		return ret;

	/**
	 * temperature offset calibration is only needed for runtime ucode,
	 * so prepare the value now.
	 */
	if (priv->cfg->need_temp_offset_calib) {
		if (priv->cfg->temp_offset_v2)
			return iwl_set_temperature_offset_calib_v2(priv);
		else
			return iwl_set_temperature_offset_calib(priv);
	}

	return 0;
}

static int iwl_send_wimax_coex(struct iwl_priv *priv)
{
	struct iwl_wimax_coex_cmd coex_cmd;

	/* coexistence is disabled */
	memset(&coex_cmd, 0, sizeof(coex_cmd));

	return iwl_dvm_send_cmd_pdu(priv,
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

void iwl_send_prio_tbl(struct iwl_priv *priv)
{
	struct iwl_bt_coex_prio_table_cmd prio_tbl_cmd;

	memcpy(prio_tbl_cmd.prio_tbl, iwl_bt_prio_tbl,
		sizeof(iwl_bt_prio_tbl));
	if (iwl_dvm_send_cmd_pdu(priv,
				REPLY_BT_COEX_PRIO_TABLE, CMD_SYNC,
				sizeof(prio_tbl_cmd), &prio_tbl_cmd))
		IWL_ERR(priv, "failed to send BT prio tbl command\n");
}

int iwl_send_bt_env(struct iwl_priv *priv, u8 action, u8 type)
{
	struct iwl_bt_coex_prot_env_cmd env_cmd;
	int ret;

	env_cmd.action = action;
	env_cmd.type = type;
	ret = iwl_dvm_send_cmd_pdu(priv,
			       REPLY_BT_COEX_PROT_ENV, CMD_SYNC,
			       sizeof(env_cmd), &env_cmd);
	if (ret)
		IWL_ERR(priv, "failed to send BT env command\n");
	return ret;
}

static const u8 iwlagn_default_queue_to_tx_fifo[] = {
	IWL_TX_FIFO_VO,
	IWL_TX_FIFO_VI,
	IWL_TX_FIFO_BE,
	IWL_TX_FIFO_BK,
};

static const u8 iwlagn_ipan_queue_to_tx_fifo[] = {
	IWL_TX_FIFO_VO,
	IWL_TX_FIFO_VI,
	IWL_TX_FIFO_BE,
	IWL_TX_FIFO_BK,
	IWL_TX_FIFO_BK_IPAN,
	IWL_TX_FIFO_BE_IPAN,
	IWL_TX_FIFO_VI_IPAN,
	IWL_TX_FIFO_VO_IPAN,
	IWL_TX_FIFO_BE_IPAN,
	IWL_TX_FIFO_UNUSED,
	IWL_TX_FIFO_AUX,
};

static int iwl_alive_notify(struct iwl_priv *priv)
{
	const u8 *queue_to_txf;
	u8 n_queues;
	int ret;
	int i;

	iwl_trans_fw_alive(priv->trans, 0);

	if (priv->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_PAN &&
	    priv->eeprom_data->sku & EEPROM_SKU_CAP_IPAN_ENABLE) {
		n_queues = ARRAY_SIZE(iwlagn_ipan_queue_to_tx_fifo);
		queue_to_txf = iwlagn_ipan_queue_to_tx_fifo;
	} else {
		n_queues = ARRAY_SIZE(iwlagn_default_queue_to_tx_fifo);
		queue_to_txf = iwlagn_default_queue_to_tx_fifo;
	}

	for (i = 0; i < n_queues; i++)
		if (queue_to_txf[i] != IWL_TX_FIFO_UNUSED)
			iwl_trans_ac_txq_enable(priv->trans, i,
						queue_to_txf[i]);

	priv->passive_no_rx = false;
	priv->transport_queue_stop = 0;

	ret = iwl_send_wimax_coex(priv);
	if (ret)
		return ret;

	if (!priv->cfg->no_xtal_calib) {
		ret = iwl_set_Xtal_calib(priv);
		if (ret)
			return ret;
	}

	return iwl_send_calib_results(priv);
}


/**
 * iwl_verify_inst_sparse - verify runtime uCode image in card vs. host,
 *   using sample data 100 bytes apart.  If these sample points are good,
 *   it's a pretty good bet that everything between them is good, too.
 */
static int iwl_verify_sec_sparse(struct iwl_priv *priv,
				  const struct fw_desc *fw_desc)
{
	__le32 *image = (__le32 *)fw_desc->data;
	u32 len = fw_desc->len;
	u32 val;
	u32 i;

	IWL_DEBUG_FW(priv, "ucode inst image size is %u\n", len);

	for (i = 0; i < len; i += 100, image += 100/sizeof(u32)) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		iwl_write_direct32(priv->trans, HBUS_TARG_MEM_RADDR,
			i + fw_desc->offset);
		val = iwl_read32(priv->trans, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image))
			return -EIO;
	}

	return 0;
}

static void iwl_print_mismatch_sec(struct iwl_priv *priv,
				    const struct fw_desc *fw_desc)
{
	__le32 *image = (__le32 *)fw_desc->data;
	u32 len = fw_desc->len;
	u32 val;
	u32 offs;
	int errors = 0;

	IWL_DEBUG_FW(priv, "ucode inst image size is %u\n", len);

	iwl_write_direct32(priv->trans, HBUS_TARG_MEM_RADDR,
				fw_desc->offset);

	for (offs = 0;
	     offs < len && errors < 20;
	     offs += sizeof(u32), image++) {
		/* read data comes through single port, auto-incr addr */
		val = iwl_read32(priv->trans, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
			IWL_ERR(priv, "uCode INST section at "
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
static int iwl_verify_ucode(struct iwl_priv *priv,
			    enum iwl_ucode_type ucode_type)
{
	const struct fw_img *img = iwl_get_ucode_image(priv, ucode_type);

	if (!img) {
		IWL_ERR(priv, "Invalid ucode requested (%d)\n", ucode_type);
		return -EINVAL;
	}

	if (!iwl_verify_sec_sparse(priv, &img->sec[IWL_UCODE_SECTION_INST])) {
		IWL_DEBUG_FW(priv, "uCode is good in inst SRAM\n");
		return 0;
	}

	IWL_ERR(priv, "UCODE IMAGE IN INSTRUCTION SRAM NOT VALID!!\n");

	iwl_print_mismatch_sec(priv, &img->sec[IWL_UCODE_SECTION_INST]);
	return -EIO;
}

struct iwl_alive_data {
	bool valid;
	u8 subtype;
};

static bool iwl_alive_fn(struct iwl_notif_wait_data *notif_wait,
			 struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_priv *priv =
		container_of(notif_wait, struct iwl_priv, notif_wait);
	struct iwl_alive_data *alive_data = data;
	struct iwl_alive_resp *palive;

	palive = (void *)pkt->data;

	IWL_DEBUG_FW(priv, "Alive ucode status 0x%08X revision "
		       "0x%01X 0x%01X\n",
		       palive->is_valid, palive->ver_type,
		       palive->ver_subtype);

	priv->device_pointers.error_event_table =
		le32_to_cpu(palive->error_event_table_ptr);
	priv->device_pointers.log_event_table =
		le32_to_cpu(palive->log_event_table_ptr);

	alive_data->subtype = palive->ver_subtype;
	alive_data->valid = palive->is_valid == UCODE_VALID_OK;

	return true;
}

#define UCODE_ALIVE_TIMEOUT	HZ
#define UCODE_CALIB_TIMEOUT	(2*HZ)

int iwl_load_ucode_wait_alive(struct iwl_priv *priv,
				 enum iwl_ucode_type ucode_type)
{
	struct iwl_notification_wait alive_wait;
	struct iwl_alive_data alive_data;
	const struct fw_img *fw;
	int ret;
	enum iwl_ucode_type old_type;
	static const u8 alive_cmd[] = { REPLY_ALIVE };

	old_type = priv->cur_ucode;
	priv->cur_ucode = ucode_type;
	fw = iwl_get_ucode_image(priv, ucode_type);

	priv->ucode_loaded = false;

	if (!fw)
		return -EINVAL;

	iwl_init_notification_wait(&priv->notif_wait, &alive_wait,
				   alive_cmd, ARRAY_SIZE(alive_cmd),
				   iwl_alive_fn, &alive_data);

	ret = iwl_trans_start_fw(priv->trans, fw);
	if (ret) {
		priv->cur_ucode = old_type;
		iwl_remove_notification(&priv->notif_wait, &alive_wait);
		return ret;
	}

	/*
	 * Some things may run in the background now, but we
	 * just wait for the ALIVE notification here.
	 */
	ret = iwl_wait_notification(&priv->notif_wait, &alive_wait,
					UCODE_ALIVE_TIMEOUT);
	if (ret) {
		priv->cur_ucode = old_type;
		return ret;
	}

	if (!alive_data.valid) {
		IWL_ERR(priv, "Loaded ucode is not valid!\n");
		priv->cur_ucode = old_type;
		return -EIO;
	}

	/*
	 * This step takes a long time (60-80ms!!) and
	 * WoWLAN image should be loaded quickly, so
	 * skip it for WoWLAN.
	 */
	if (ucode_type != IWL_UCODE_WOWLAN) {
		ret = iwl_verify_ucode(priv, ucode_type);
		if (ret) {
			priv->cur_ucode = old_type;
			return ret;
		}

		/* delay a bit to give rfkill time to run */
		msleep(5);
	}

	ret = iwl_alive_notify(priv);
	if (ret) {
		IWL_WARN(priv,
			"Could not complete ALIVE transition: %d\n", ret);
		priv->cur_ucode = old_type;
		return ret;
	}

	priv->ucode_loaded = true;

	return 0;
}

static bool iwlagn_wait_calib(struct iwl_notif_wait_data *notif_wait,
			      struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_priv *priv = data;
	struct iwl_calib_hdr *hdr;
	int len;

	if (pkt->hdr.cmd != CALIBRATION_RES_NOTIFICATION) {
		WARN_ON(pkt->hdr.cmd != CALIBRATION_COMPLETE_NOTIFICATION);
		return true;
	}

	hdr = (struct iwl_calib_hdr *)pkt->data;
	len = le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK;

	/* reduce the size by the length field itself */
	len -= sizeof(__le32);

	if (iwl_calib_set(priv, hdr, len))
		IWL_ERR(priv, "Failed to record calibration data %d\n",
			hdr->op_code);

	return false;
}

int iwl_run_init_ucode(struct iwl_priv *priv)
{
	struct iwl_notification_wait calib_wait;
	static const u8 calib_complete[] = {
		CALIBRATION_RES_NOTIFICATION,
		CALIBRATION_COMPLETE_NOTIFICATION
	};
	int ret;

	lockdep_assert_held(&priv->mutex);

	/* No init ucode required? Curious, but maybe ok */
	if (!priv->fw->img[IWL_UCODE_INIT].sec[0].len)
		return 0;

	if (priv->init_ucode_run)
		return 0;

	iwl_init_notification_wait(&priv->notif_wait, &calib_wait,
				   calib_complete, ARRAY_SIZE(calib_complete),
				   iwlagn_wait_calib, priv);

	/* Will also start the device */
	ret = iwl_load_ucode_wait_alive(priv, IWL_UCODE_INIT);
	if (ret)
		goto error;

	ret = iwl_init_alive_start(priv);
	if (ret)
		goto error;

	/*
	 * Some things may run in the background now, but we
	 * just wait for the calibration complete notification.
	 */
	ret = iwl_wait_notification(&priv->notif_wait, &calib_wait,
					UCODE_CALIB_TIMEOUT);
	if (!ret)
		priv->init_ucode_run = true;

	goto out;

 error:
	iwl_remove_notification(&priv->notif_wait, &calib_wait);
 out:
	/* Whatever happened, stop the device */
	iwl_trans_stop_device(priv->trans);
	priv->ucode_loaded = false;

	return ret;
}
