/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2010 Intel Corporation. All rights reserved.
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

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-agn-hw.h"
#include "iwl-agn.h"
#include "iwl-agn-calib.h"

#define IWL_AC_UNSET -1

struct queue_to_fifo_ac {
	s8 fifo, ac;
};

static const struct queue_to_fifo_ac iwlagn_default_queue_to_tx_fifo[] = {
	{ IWL_TX_FIFO_VO, 0, },
	{ IWL_TX_FIFO_VI, 1, },
	{ IWL_TX_FIFO_BE, 2, },
	{ IWL_TX_FIFO_BK, 3, },
	{ IWLAGN_CMD_FIFO_NUM, IWL_AC_UNSET, },
	{ IWL_TX_FIFO_UNUSED, IWL_AC_UNSET, },
	{ IWL_TX_FIFO_UNUSED, IWL_AC_UNSET, },
	{ IWL_TX_FIFO_UNUSED, IWL_AC_UNSET, },
	{ IWL_TX_FIFO_UNUSED, IWL_AC_UNSET, },
	{ IWL_TX_FIFO_UNUSED, IWL_AC_UNSET, },
};

static const struct queue_to_fifo_ac iwlagn_ipan_queue_to_tx_fifo[] = {
	{ IWL_TX_FIFO_VO, 0, },
	{ IWL_TX_FIFO_VI, 1, },
	{ IWL_TX_FIFO_BE, 2, },
	{ IWL_TX_FIFO_BK, 3, },
	{ IWL_TX_FIFO_BK_IPAN, 3, },
	{ IWL_TX_FIFO_BE_IPAN, 2, },
	{ IWL_TX_FIFO_VI_IPAN, 1, },
	{ IWL_TX_FIFO_VO_IPAN, 0, },
	{ IWL_TX_FIFO_BE_IPAN, 2, },
	{ IWLAGN_CMD_FIFO_NUM, IWL_AC_UNSET, },
};

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

/*
 * ucode
 */
static int iwlagn_load_section(struct iwl_priv *priv, const char *name,
				struct fw_desc *image, u32 dst_addr)
{
	dma_addr_t phy_addr = image->p_addr;
	u32 byte_cnt = image->len;
	int ret;

	priv->ucode_write_complete = 0;

	iwl_write_direct32(priv,
		FH_TCSR_CHNL_TX_CONFIG_REG(FH_SRVC_CHNL),
		FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE);

	iwl_write_direct32(priv,
		FH_SRVC_CHNL_SRAM_ADDR_REG(FH_SRVC_CHNL), dst_addr);

	iwl_write_direct32(priv,
		FH_TFDIB_CTRL0_REG(FH_SRVC_CHNL),
		phy_addr & FH_MEM_TFDIB_DRAM_ADDR_LSB_MSK);

	iwl_write_direct32(priv,
		FH_TFDIB_CTRL1_REG(FH_SRVC_CHNL),
		(iwl_get_dma_hi_addr(phy_addr)
			<< FH_MEM_TFDIB_REG1_ADDR_BITSHIFT) | byte_cnt);

	iwl_write_direct32(priv,
		FH_TCSR_CHNL_TX_BUF_STS_REG(FH_SRVC_CHNL),
		1 << FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_NUM |
		1 << FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_IDX |
		FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID);

	iwl_write_direct32(priv,
		FH_TCSR_CHNL_TX_CONFIG_REG(FH_SRVC_CHNL),
		FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE	|
		FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE	|
		FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD);

	IWL_DEBUG_INFO(priv, "%s uCode section being loaded...\n", name);
	ret = wait_event_interruptible_timeout(priv->wait_command_queue,
					priv->ucode_write_complete, 5 * HZ);
	if (ret == -ERESTARTSYS) {
		IWL_ERR(priv, "Could not load the %s uCode section due "
			"to interrupt\n", name);
		return ret;
	}
	if (!ret) {
		IWL_ERR(priv, "Could not load the %s uCode section\n",
			name);
		return -ETIMEDOUT;
	}

	return 0;
}

static int iwlagn_load_given_ucode(struct iwl_priv *priv,
		struct fw_desc *inst_image,
		struct fw_desc *data_image)
{
	int ret = 0;

	ret = iwlagn_load_section(priv, "INST", inst_image,
				   IWLAGN_RTC_INST_LOWER_BOUND);
	if (ret)
		return ret;

	return iwlagn_load_section(priv, "DATA", data_image,
				    IWLAGN_RTC_DATA_LOWER_BOUND);
}

int iwlagn_load_ucode(struct iwl_priv *priv)
{
	int ret = 0;

	/* check whether init ucode should be loaded, or rather runtime ucode */
	if (priv->ucode_init.len && (priv->ucode_type == UCODE_NONE)) {
		IWL_DEBUG_INFO(priv, "Init ucode found. Loading init ucode...\n");
		ret = iwlagn_load_given_ucode(priv,
			&priv->ucode_init, &priv->ucode_init_data);
		if (!ret) {
			IWL_DEBUG_INFO(priv, "Init ucode load complete.\n");
			priv->ucode_type = UCODE_INIT;
		}
	} else {
		IWL_DEBUG_INFO(priv, "Init ucode not found, or already loaded. "
			"Loading runtime ucode...\n");
		ret = iwlagn_load_given_ucode(priv,
			&priv->ucode_code, &priv->ucode_data);
		if (!ret) {
			IWL_DEBUG_INFO(priv, "Runtime ucode load complete.\n");
			priv->ucode_type = UCODE_RT;
		}
	}

	return ret;
}

/*
 *  Calibration
 */
static int iwlagn_set_Xtal_calib(struct iwl_priv *priv)
{
	struct iwl_calib_xtal_freq_cmd cmd;
	__le16 *xtal_calib =
		(__le16 *)iwl_eeprom_query_addr(priv, EEPROM_XTAL);

	cmd.hdr.op_code = IWL_PHY_CALIBRATE_CRYSTAL_FRQ_CMD;
	cmd.hdr.first_group = 0;
	cmd.hdr.groups_num = 1;
	cmd.hdr.data_valid = 1;
	cmd.cap_pin1 = le16_to_cpu(xtal_calib[0]);
	cmd.cap_pin2 = le16_to_cpu(xtal_calib[1]);
	return iwl_calib_set(&priv->calib_results[IWL_CALIB_XTAL],
			     (u8 *)&cmd, sizeof(cmd));
}

static int iwlagn_set_temperature_offset_calib(struct iwl_priv *priv)
{
	struct iwl_calib_temperature_offset_cmd cmd;
	__le16 *offset_calib =
		(__le16 *)iwl_eeprom_query_addr(priv, EEPROM_5000_TEMPERATURE);
	cmd.hdr.op_code = IWL_PHY_CALIBRATE_TEMP_OFFSET_CMD;
	cmd.hdr.first_group = 0;
	cmd.hdr.groups_num = 1;
	cmd.hdr.data_valid = 1;
	cmd.radio_sensor_offset = le16_to_cpu(offset_calib[1]);
	if (!(cmd.radio_sensor_offset))
		cmd.radio_sensor_offset = DEFAULT_RADIO_SENSOR_OFFSET;
	cmd.reserved = 0;
	IWL_DEBUG_CALIB(priv, "Radio sensor offset: %d\n",
			cmd.radio_sensor_offset);
	return iwl_calib_set(&priv->calib_results[IWL_CALIB_TEMP_OFFSET],
			     (u8 *)&cmd, sizeof(cmd));
}

static int iwlagn_send_calib_cfg(struct iwl_priv *priv)
{
	struct iwl_calib_cfg_cmd calib_cfg_cmd;
	struct iwl_host_cmd cmd = {
		.id = CALIBRATION_CFG_CMD,
		.len = sizeof(struct iwl_calib_cfg_cmd),
		.data = &calib_cfg_cmd,
	};

	memset(&calib_cfg_cmd, 0, sizeof(calib_cfg_cmd));
	calib_cfg_cmd.ucd_calib_cfg.once.is_enable = IWL_CALIB_INIT_CFG_ALL;
	calib_cfg_cmd.ucd_calib_cfg.once.start = IWL_CALIB_INIT_CFG_ALL;
	calib_cfg_cmd.ucd_calib_cfg.once.send_res = IWL_CALIB_INIT_CFG_ALL;
	calib_cfg_cmd.ucd_calib_cfg.flags = IWL_CALIB_INIT_CFG_ALL;

	return iwl_send_cmd(priv, &cmd);
}

void iwlagn_rx_calib_result(struct iwl_priv *priv,
			     struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_calib_hdr *hdr = (struct iwl_calib_hdr *)pkt->u.raw;
	int len = le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK;
	int index;

	/* reduce the size of the length field itself */
	len -= 4;

	/* Define the order in which the results will be sent to the runtime
	 * uCode. iwl_send_calib_results sends them in a row according to
	 * their index. We sort them here
	 */
	switch (hdr->op_code) {
	case IWL_PHY_CALIBRATE_DC_CMD:
		index = IWL_CALIB_DC;
		break;
	case IWL_PHY_CALIBRATE_LO_CMD:
		index = IWL_CALIB_LO;
		break;
	case IWL_PHY_CALIBRATE_TX_IQ_CMD:
		index = IWL_CALIB_TX_IQ;
		break;
	case IWL_PHY_CALIBRATE_TX_IQ_PERD_CMD:
		index = IWL_CALIB_TX_IQ_PERD;
		break;
	case IWL_PHY_CALIBRATE_BASE_BAND_CMD:
		index = IWL_CALIB_BASE_BAND;
		break;
	default:
		IWL_ERR(priv, "Unknown calibration notification %d\n",
			  hdr->op_code);
		return;
	}
	iwl_calib_set(&priv->calib_results[index], pkt->u.raw, len);
}

void iwlagn_rx_calib_complete(struct iwl_priv *priv,
			       struct iwl_rx_mem_buffer *rxb)
{
	IWL_DEBUG_INFO(priv, "Init. calibration is completed, restarting fw.\n");
	queue_work(priv->workqueue, &priv->restart);
}

void iwlagn_init_alive_start(struct iwl_priv *priv)
{
	int ret = 0;

	/* Check alive response for "valid" sign from uCode */
	if (priv->card_alive_init.is_valid != UCODE_VALID_OK) {
		/* We had an error bringing up the hardware, so take it
		 * all the way back down so we can try again */
		IWL_DEBUG_INFO(priv, "Initialize Alive failed.\n");
		goto restart;
	}

	/* initialize uCode was loaded... verify inst image.
	 * This is a paranoid check, because we would not have gotten the
	 * "initialize" alive if code weren't properly loaded.  */
	if (iwl_verify_ucode(priv)) {
		/* Runtime instruction load was bad;
		 * take it all the way back down so we can try again */
		IWL_DEBUG_INFO(priv, "Bad \"initialize\" uCode load.\n");
		goto restart;
	}

	ret = priv->cfg->ops->lib->alive_notify(priv);
	if (ret) {
		IWL_WARN(priv,
			"Could not complete ALIVE transition: %d\n", ret);
		goto restart;
	}

	if (priv->cfg->bt_params &&
	    priv->cfg->bt_params->advanced_bt_coexist) {
		/*
		 * Tell uCode we are ready to perform calibration
		 * need to perform this before any calibration
		 * no need to close the envlope since we are going
		 * to load the runtime uCode later.
		 */
		iwlagn_send_bt_env(priv, IWL_BT_COEX_ENV_OPEN,
			BT_COEX_PRIO_TBL_EVT_INIT_CALIB2);

	}
	iwlagn_send_calib_cfg(priv);

	/**
	 * temperature offset calibration is only needed for runtime ucode,
	 * so prepare the value now.
	 */
	if (priv->cfg->need_temp_offset_calib)
		iwlagn_set_temperature_offset_calib(priv);

	return;

restart:
	/* real restart (first load init_ucode) */
	queue_work(priv->workqueue, &priv->restart);
}

static int iwlagn_send_wimax_coex(struct iwl_priv *priv)
{
	struct iwl_wimax_coex_cmd coex_cmd;

	if (priv->cfg->base_params->support_wimax_coexist) {
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
	return iwl_send_cmd_pdu(priv, COEX_PRIORITY_TABLE_CMD,
				sizeof(coex_cmd), &coex_cmd);
}

static const u8 iwlagn_bt_prio_tbl[BT_COEX_PRIO_TBL_EVT_MAX] = {
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

void iwlagn_send_prio_tbl(struct iwl_priv *priv)
{
	struct iwl_bt_coex_prio_table_cmd prio_tbl_cmd;

	memcpy(prio_tbl_cmd.prio_tbl, iwlagn_bt_prio_tbl,
		sizeof(iwlagn_bt_prio_tbl));
	if (iwl_send_cmd_pdu(priv, REPLY_BT_COEX_PRIO_TABLE,
				sizeof(prio_tbl_cmd), &prio_tbl_cmd))
		IWL_ERR(priv, "failed to send BT prio tbl command\n");
}

void iwlagn_send_bt_env(struct iwl_priv *priv, u8 action, u8 type)
{
	struct iwl_bt_coex_prot_env_cmd env_cmd;

	env_cmd.action = action;
	env_cmd.type = type;
	if (iwl_send_cmd_pdu(priv, REPLY_BT_COEX_PROT_ENV,
			     sizeof(env_cmd), &env_cmd))
		IWL_ERR(priv, "failed to send BT env command\n");
}


int iwlagn_alive_notify(struct iwl_priv *priv)
{
	const struct queue_to_fifo_ac *queue_to_fifo;
	u32 a;
	unsigned long flags;
	int i, chan;
	u32 reg_val;

	spin_lock_irqsave(&priv->lock, flags);

	priv->scd_base_addr = iwl_read_prph(priv, IWLAGN_SCD_SRAM_BASE_ADDR);
	a = priv->scd_base_addr + IWLAGN_SCD_CONTEXT_DATA_OFFSET;
	for (; a < priv->scd_base_addr + IWLAGN_SCD_TX_STTS_BITMAP_OFFSET;
		a += 4)
		iwl_write_targ_mem(priv, a, 0);
	for (; a < priv->scd_base_addr + IWLAGN_SCD_TRANSLATE_TBL_OFFSET;
		a += 4)
		iwl_write_targ_mem(priv, a, 0);
	for (; a < priv->scd_base_addr +
	       IWLAGN_SCD_TRANSLATE_TBL_OFFSET_QUEUE(priv->hw_params.max_txq_num); a += 4)
		iwl_write_targ_mem(priv, a, 0);

	iwl_write_prph(priv, IWLAGN_SCD_DRAM_BASE_ADDR,
		       priv->scd_bc_tbls.dma >> 10);

	/* Enable DMA channel */
	for (chan = 0; chan < FH50_TCSR_CHNL_NUM ; chan++)
		iwl_write_direct32(priv, FH_TCSR_CHNL_TX_CONFIG_REG(chan),
				FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
				FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE);

	/* Update FH chicken bits */
	reg_val = iwl_read_direct32(priv, FH_TX_CHICKEN_BITS_REG);
	iwl_write_direct32(priv, FH_TX_CHICKEN_BITS_REG,
			   reg_val | FH_TX_CHICKEN_BITS_SCD_AUTO_RETRY_EN);

	iwl_write_prph(priv, IWLAGN_SCD_QUEUECHAIN_SEL,
		IWLAGN_SCD_QUEUECHAIN_SEL_ALL(priv));
	iwl_write_prph(priv, IWLAGN_SCD_AGGR_SEL, 0);

	/* initiate the queues */
	for (i = 0; i < priv->hw_params.max_txq_num; i++) {
		iwl_write_prph(priv, IWLAGN_SCD_QUEUE_RDPTR(i), 0);
		iwl_write_direct32(priv, HBUS_TARG_WRPTR, 0 | (i << 8));
		iwl_write_targ_mem(priv, priv->scd_base_addr +
				IWLAGN_SCD_CONTEXT_QUEUE_OFFSET(i), 0);
		iwl_write_targ_mem(priv, priv->scd_base_addr +
				IWLAGN_SCD_CONTEXT_QUEUE_OFFSET(i) +
				sizeof(u32),
				((SCD_WIN_SIZE <<
				IWLAGN_SCD_QUEUE_CTX_REG2_WIN_SIZE_POS) &
				IWLAGN_SCD_QUEUE_CTX_REG2_WIN_SIZE_MSK) |
				((SCD_FRAME_LIMIT <<
				IWLAGN_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS) &
				IWLAGN_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK));
	}

	iwl_write_prph(priv, IWLAGN_SCD_INTERRUPT_MASK,
			IWL_MASK(0, priv->hw_params.max_txq_num));

	/* Activate all Tx DMA/FIFO channels */
	priv->cfg->ops->lib->txq_set_sched(priv, IWL_MASK(0, 7));

	/* map queues to FIFOs */
	if (priv->valid_contexts != BIT(IWL_RXON_CTX_BSS))
		queue_to_fifo = iwlagn_ipan_queue_to_tx_fifo;
	else
		queue_to_fifo = iwlagn_default_queue_to_tx_fifo;

	iwlagn_set_wr_ptrs(priv, priv->cmd_queue, 0);

	/* make sure all queue are not stopped */
	memset(&priv->queue_stopped[0], 0, sizeof(priv->queue_stopped));
	for (i = 0; i < 4; i++)
		atomic_set(&priv->queue_stop_count[i], 0);

	/* reset to 0 to enable all the queue first */
	priv->txq_ctx_active_msk = 0;

	BUILD_BUG_ON(ARRAY_SIZE(iwlagn_default_queue_to_tx_fifo) != 10);
	BUILD_BUG_ON(ARRAY_SIZE(iwlagn_ipan_queue_to_tx_fifo) != 10);

	for (i = 0; i < 10; i++) {
		int fifo = queue_to_fifo[i].fifo;
		int ac = queue_to_fifo[i].ac;

		iwl_txq_ctx_activate(priv, i);

		if (fifo == IWL_TX_FIFO_UNUSED)
			continue;

		if (ac != IWL_AC_UNSET)
			iwl_set_swq_id(&priv->txq[i], ac, i);
		iwlagn_tx_queue_set_status(priv, &priv->txq[i], fifo, 0);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	iwlagn_send_wimax_coex(priv);

	iwlagn_set_Xtal_calib(priv);
	iwl_send_calib_results(priv);

	return 0;
}


/**
 * iwl_verify_inst_sparse - verify runtime uCode image in card vs. host,
 *   using sample data 100 bytes apart.  If these sample points are good,
 *   it's a pretty good bet that everything between them is good, too.
 */
static int iwlcore_verify_inst_sparse(struct iwl_priv *priv, __le32 *image, u32 len)
{
	u32 val;
	int ret = 0;
	u32 errcnt = 0;
	u32 i;

	IWL_DEBUG_INFO(priv, "ucode inst image size is %u\n", len);

	for (i = 0; i < len; i += 100, image += 100/sizeof(u32)) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		iwl_write_direct32(priv, HBUS_TARG_MEM_RADDR,
			i + IWLAGN_RTC_INST_LOWER_BOUND);
		val = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
			ret = -EIO;
			errcnt++;
			if (errcnt >= 3)
				break;
		}
	}

	return ret;
}

/**
 * iwlcore_verify_inst_full - verify runtime uCode image in card vs. host,
 *     looking at all data.
 */
static int iwl_verify_inst_full(struct iwl_priv *priv, __le32 *image,
				 u32 len)
{
	u32 val;
	u32 save_len = len;
	int ret = 0;
	u32 errcnt;

	IWL_DEBUG_INFO(priv, "ucode inst image size is %u\n", len);

	iwl_write_direct32(priv, HBUS_TARG_MEM_RADDR,
			   IWLAGN_RTC_INST_LOWER_BOUND);

	errcnt = 0;
	for (; len > 0; len -= sizeof(u32), image++) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IWL_DL_IO is set */
		val = _iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
			IWL_ERR(priv, "uCode INST section is invalid at "
				  "offset 0x%x, is 0x%x, s/b 0x%x\n",
				  save_len - len, val, le32_to_cpu(*image));
			ret = -EIO;
			errcnt++;
			if (errcnt >= 20)
				break;
		}
	}

	if (!errcnt)
		IWL_DEBUG_INFO(priv,
		    "ucode image in INSTRUCTION memory is good\n");

	return ret;
}

/**
 * iwl_verify_ucode - determine which instruction image is in SRAM,
 *    and verify its contents
 */
int iwl_verify_ucode(struct iwl_priv *priv)
{
	__le32 *image;
	u32 len;
	int ret;

	/* Try bootstrap */
	image = (__le32 *)priv->ucode_boot.v_addr;
	len = priv->ucode_boot.len;
	ret = iwlcore_verify_inst_sparse(priv, image, len);
	if (!ret) {
		IWL_DEBUG_INFO(priv, "Bootstrap uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try initialize */
	image = (__le32 *)priv->ucode_init.v_addr;
	len = priv->ucode_init.len;
	ret = iwlcore_verify_inst_sparse(priv, image, len);
	if (!ret) {
		IWL_DEBUG_INFO(priv, "Initialize uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try runtime/protocol */
	image = (__le32 *)priv->ucode_code.v_addr;
	len = priv->ucode_code.len;
	ret = iwlcore_verify_inst_sparse(priv, image, len);
	if (!ret) {
		IWL_DEBUG_INFO(priv, "Runtime uCode is good in inst SRAM\n");
		return 0;
	}

	IWL_ERR(priv, "NO VALID UCODE IMAGE IN INSTRUCTION SRAM!!\n");

	/* Since nothing seems to match, show first several data entries in
	 * instruction SRAM, so maybe visual inspection will give a clue.
	 * Selection of bootstrap image (vs. other images) is arbitrary. */
	image = (__le32 *)priv->ucode_boot.v_addr;
	len = priv->ucode_boot.len;
	ret = iwl_verify_inst_full(priv, image, len);

	return ret;
}
