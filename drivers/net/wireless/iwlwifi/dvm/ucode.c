/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2014 Intel Corporation. All rights reserved.
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>

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
	__le16 *xtal_calib = priv->nvm_data->xtal_calib;

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
	cmd.radio_sensor_offset = priv->nvm_data->raw_temperature;
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
	cmd.radio_sensor_offset_high = priv->nvm_data->kelvin_temperature;
	cmd.radio_sensor_offset_low = priv->nvm_data->raw_temperature;
	if (!cmd.radio_sensor_offset_low) {
		IWL_DEBUG_CALIB(priv, "no info in EEPROM, use default\n");
		cmd.radio_sensor_offset_low = DEFAULT_RADIO_SENSOR_OFFSET;
		cmd.radio_sensor_offset_high = DEFAULT_RADIO_SENSOR_OFFSET;
	}
	cmd.burntVoltageRef = priv->nvm_data->calib_voltage;

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

	if (priv->lib->bt_params &&
	    priv->lib->bt_params->advanced_bt_coexist) {
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
	if (priv->lib->need_temp_offset_calib) {
		if (priv->lib->temp_offset_v2)
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
				COEX_PRIORITY_TABLE_CMD, 0,
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
				REPLY_BT_COEX_PRIO_TABLE, 0,
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
			       REPLY_BT_COEX_PROT_ENV, 0,
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
	    priv->nvm_data->sku_cap_ipan_enable) {
		n_queues = ARRAY_SIZE(iwlagn_ipan_queue_to_tx_fifo);
		queue_to_txf = iwlagn_ipan_queue_to_tx_fifo;
	} else {
		n_queues = ARRAY_SIZE(iwlagn_default_queue_to_tx_fifo);
		queue_to_txf = iwlagn_default_queue_to_tx_fifo;
	}

	for (i = 0; i < n_queues; i++)
		if (queue_to_txf[i] != IWL_TX_FIFO_UNUSED)
			iwl_trans_ac_txq_enable(priv->trans, i,
						queue_to_txf[i], 0);

	priv->passive_no_rx = false;
	priv->transport_queue_stop = 0;

	ret = iwl_send_wimax_coex(priv);
	if (ret)
		return ret;

	if (!priv->lib->no_xtal_calib) {
		ret = iwl_set_Xtal_calib(priv);
		if (ret)
			return ret;
	}

	return iwl_send_calib_results(priv);
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

	fw = iwl_get_ucode_image(priv, ucode_type);
	if (WARN_ON(!fw))
		return -EINVAL;

	old_type = priv->cur_ucode;
	priv->cur_ucode = ucode_type;
	priv->ucode_loaded = false;

	iwl_init_notification_wait(&priv->notif_wait, &alive_wait,
				   alive_cmd, ARRAY_SIZE(alive_cmd),
				   iwl_alive_fn, &alive_data);

	ret = iwl_trans_start_fw(priv->trans, fw, false);
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

	priv->ucode_loaded = true;

	if (ucode_type != IWL_UCODE_WOWLAN) {
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

	return 0;
}

static bool iwlagn_wait_calib(struct iwl_notif_wait_data *notif_wait,
			      struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_priv *priv = data;
	struct iwl_calib_hdr *hdr;

	if (pkt->hdr.cmd != CALIBRATION_RES_NOTIFICATION) {
		WARN_ON(pkt->hdr.cmd != CALIBRATION_COMPLETE_NOTIFICATION);
		return true;
	}

	hdr = (struct iwl_calib_hdr *)pkt->data;

	if (iwl_calib_set(priv, hdr, iwl_rx_packet_payload_len(pkt)))
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
