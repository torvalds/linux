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

static const s8 iwlagn_default_queue_to_tx_fifo[] = {
	IWL_TX_FIFO_VO,
	IWL_TX_FIFO_VI,
	IWL_TX_FIFO_BE,
	IWL_TX_FIFO_BK,
	IWLAGN_CMD_FIFO_NUM,
	IWL_TX_FIFO_UNUSED,
	IWL_TX_FIFO_UNUSED,
	IWL_TX_FIFO_UNUSED,
	IWL_TX_FIFO_UNUSED,
	IWL_TX_FIFO_UNUSED,
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

#define IWL_UCODE_GET(item)						\
static u32 iwlagn_ucode_get_##item(const struct iwl_ucode_header *ucode,\
				    u32 api_ver)			\
{									\
	if (api_ver <= 2)						\
		return le32_to_cpu(ucode->u.v1.item);			\
	return le32_to_cpu(ucode->u.v2.item);				\
}

static u32 iwlagn_ucode_get_header_size(u32 api_ver)
{
	if (api_ver <= 2)
		return UCODE_HEADER_SIZE(1);
	return UCODE_HEADER_SIZE(2);
}

static u32 iwlagn_ucode_get_build(const struct iwl_ucode_header *ucode,
				   u32 api_ver)
{
	if (api_ver <= 2)
		return 0;
	return le32_to_cpu(ucode->u.v2.build);
}

static u8 *iwlagn_ucode_get_data(const struct iwl_ucode_header *ucode,
				  u32 api_ver)
{
	if (api_ver <= 2)
		return (u8 *) ucode->u.v1.data;
	return (u8 *) ucode->u.v2.data;
}

IWL_UCODE_GET(inst_size);
IWL_UCODE_GET(data_size);
IWL_UCODE_GET(init_size);
IWL_UCODE_GET(init_data_size);
IWL_UCODE_GET(boot_size);

struct iwl_ucode_ops iwlagn_ucode = {
	.get_header_size = iwlagn_ucode_get_header_size,
	.get_build = iwlagn_ucode_get_build,
	.get_inst_size = iwlagn_ucode_get_inst_size,
	.get_data_size = iwlagn_ucode_get_data_size,
	.get_init_size = iwlagn_ucode_get_init_size,
	.get_init_data_size = iwlagn_ucode_get_init_data_size,
	.get_boot_size = iwlagn_ucode_get_boot_size,
	.get_data = iwlagn_ucode_get_data,
};

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

	iwlagn_send_calib_cfg(priv);
	return;

restart:
	/* real restart (first load init_ucode) */
	queue_work(priv->workqueue, &priv->restart);
}

int iwlagn_alive_notify(struct iwl_priv *priv)
{
	u32 a;
	unsigned long flags;
	int i, chan;
	u32 reg_val;

	spin_lock_irqsave(&priv->lock, flags);

	priv->scd_base_addr = iwl_read_prph(priv, IWL50_SCD_SRAM_BASE_ADDR);
	a = priv->scd_base_addr + IWL50_SCD_CONTEXT_DATA_OFFSET;
	for (; a < priv->scd_base_addr + IWL50_SCD_TX_STTS_BITMAP_OFFSET;
		a += 4)
		iwl_write_targ_mem(priv, a, 0);
	for (; a < priv->scd_base_addr + IWL50_SCD_TRANSLATE_TBL_OFFSET;
		a += 4)
		iwl_write_targ_mem(priv, a, 0);
	for (; a < priv->scd_base_addr +
	       IWL50_SCD_TRANSLATE_TBL_OFFSET_QUEUE(priv->hw_params.max_txq_num); a += 4)
		iwl_write_targ_mem(priv, a, 0);

	iwl_write_prph(priv, IWL50_SCD_DRAM_BASE_ADDR,
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

	iwl_write_prph(priv, IWL50_SCD_QUEUECHAIN_SEL,
		IWL50_SCD_QUEUECHAIN_SEL_ALL(priv->hw_params.max_txq_num));
	iwl_write_prph(priv, IWL50_SCD_AGGR_SEL, 0);

	/* initiate the queues */
	for (i = 0; i < priv->hw_params.max_txq_num; i++) {
		iwl_write_prph(priv, IWL50_SCD_QUEUE_RDPTR(i), 0);
		iwl_write_direct32(priv, HBUS_TARG_WRPTR, 0 | (i << 8));
		iwl_write_targ_mem(priv, priv->scd_base_addr +
				IWL50_SCD_CONTEXT_QUEUE_OFFSET(i), 0);
		iwl_write_targ_mem(priv, priv->scd_base_addr +
				IWL50_SCD_CONTEXT_QUEUE_OFFSET(i) +
				sizeof(u32),
				((SCD_WIN_SIZE <<
				IWL50_SCD_QUEUE_CTX_REG2_WIN_SIZE_POS) &
				IWL50_SCD_QUEUE_CTX_REG2_WIN_SIZE_MSK) |
				((SCD_FRAME_LIMIT <<
				IWL50_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS) &
				IWL50_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK));
	}

	iwl_write_prph(priv, IWL50_SCD_INTERRUPT_MASK,
			IWL_MASK(0, priv->hw_params.max_txq_num));

	/* Activate all Tx DMA/FIFO channels */
	priv->cfg->ops->lib->txq_set_sched(priv, IWL_MASK(0, 7));

	iwlagn_set_wr_ptrs(priv, IWL_CMD_QUEUE_NUM, 0);

	/* make sure all queue are not stopped */
	memset(&priv->queue_stopped[0], 0, sizeof(priv->queue_stopped));
	for (i = 0; i < 4; i++)
		atomic_set(&priv->queue_stop_count[i], 0);

	/* reset to 0 to enable all the queue first */
	priv->txq_ctx_active_msk = 0;
	/* map qos queues to fifos one-to-one */
	BUILD_BUG_ON(ARRAY_SIZE(iwlagn_default_queue_to_tx_fifo) != 10);

	for (i = 0; i < ARRAY_SIZE(iwlagn_default_queue_to_tx_fifo); i++) {
		int ac = iwlagn_default_queue_to_tx_fifo[i];

		iwl_txq_ctx_activate(priv, i);

		if (ac == IWL_TX_FIFO_UNUSED)
			continue;

		iwlagn_tx_queue_set_status(priv, &priv->txq[i], ac, 0);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_send_wimax_coex(priv);

	iwlagn_set_Xtal_calib(priv);
	iwl_send_calib_results(priv);

	return 0;
}
