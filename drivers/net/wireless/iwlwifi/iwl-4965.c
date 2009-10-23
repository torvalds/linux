/******************************************************************************
 *
 * Copyright(c) 2003 - 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <net/mac80211.h>
#include <linux/etherdevice.h>
#include <asm/unaligned.h>

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-calib.h"
#include "iwl-sta.h"
#include "iwl-agn-led.h"

static int iwl4965_send_tx_power(struct iwl_priv *priv);
static int iwl4965_hw_get_temperature(struct iwl_priv *priv);

/* Highest firmware API version supported */
#define IWL4965_UCODE_API_MAX 2

/* Lowest firmware API version supported */
#define IWL4965_UCODE_API_MIN 2

#define IWL4965_FW_PRE "iwlwifi-4965-"
#define _IWL4965_MODULE_FIRMWARE(api) IWL4965_FW_PRE #api ".ucode"
#define IWL4965_MODULE_FIRMWARE(api) _IWL4965_MODULE_FIRMWARE(api)


/* module parameters */
static struct iwl_mod_params iwl4965_mod_params = {
	.amsdu_size_8K = 1,
	.restart_fw = 1,
	/* the rest are 0 by default */
};

/* check contents of special bootstrap uCode SRAM */
static int iwl4965_verify_bsm(struct iwl_priv *priv)
{
	__le32 *image = priv->ucode_boot.v_addr;
	u32 len = priv->ucode_boot.len;
	u32 reg;
	u32 val;

	IWL_DEBUG_INFO(priv, "Begin verify bsm\n");

	/* verify BSM SRAM contents */
	val = iwl_read_prph(priv, BSM_WR_DWCOUNT_REG);
	for (reg = BSM_SRAM_LOWER_BOUND;
	     reg < BSM_SRAM_LOWER_BOUND + len;
	     reg += sizeof(u32), image++) {
		val = iwl_read_prph(priv, reg);
		if (val != le32_to_cpu(*image)) {
			IWL_ERR(priv, "BSM uCode verification failed at "
				  "addr 0x%08X+%u (of %u), is 0x%x, s/b 0x%x\n",
				  BSM_SRAM_LOWER_BOUND,
				  reg - BSM_SRAM_LOWER_BOUND, len,
				  val, le32_to_cpu(*image));
			return -EIO;
		}
	}

	IWL_DEBUG_INFO(priv, "BSM bootstrap uCode image OK\n");

	return 0;
}

/**
 * iwl4965_load_bsm - Load bootstrap instructions
 *
 * BSM operation:
 *
 * The Bootstrap State Machine (BSM) stores a short bootstrap uCode program
 * in special SRAM that does not power down during RFKILL.  When powering back
 * up after power-saving sleeps (or during initial uCode load), the BSM loads
 * the bootstrap program into the on-board processor, and starts it.
 *
 * The bootstrap program loads (via DMA) instructions and data for a new
 * program from host DRAM locations indicated by the host driver in the
 * BSM_DRAM_* registers.  Once the new program is loaded, it starts
 * automatically.
 *
 * When initializing the NIC, the host driver points the BSM to the
 * "initialize" uCode image.  This uCode sets up some internal data, then
 * notifies host via "initialize alive" that it is complete.
 *
 * The host then replaces the BSM_DRAM_* pointer values to point to the
 * normal runtime uCode instructions and a backup uCode data cache buffer
 * (filled initially with starting data values for the on-board processor),
 * then triggers the "initialize" uCode to load and launch the runtime uCode,
 * which begins normal operation.
 *
 * When doing a power-save shutdown, runtime uCode saves data SRAM into
 * the backup data cache in DRAM before SRAM is powered down.
 *
 * When powering back up, the BSM loads the bootstrap program.  This reloads
 * the runtime uCode instructions and the backup data cache into SRAM,
 * and re-launches the runtime uCode from where it left off.
 */
static int iwl4965_load_bsm(struct iwl_priv *priv)
{
	__le32 *image = priv->ucode_boot.v_addr;
	u32 len = priv->ucode_boot.len;
	dma_addr_t pinst;
	dma_addr_t pdata;
	u32 inst_len;
	u32 data_len;
	int i;
	u32 done;
	u32 reg_offset;
	int ret;

	IWL_DEBUG_INFO(priv, "Begin load bsm\n");

	priv->ucode_type = UCODE_RT;

	/* make sure bootstrap program is no larger than BSM's SRAM size */
	if (len > IWL49_MAX_BSM_SIZE)
		return -EINVAL;

	/* Tell bootstrap uCode where to find the "Initialize" uCode
	 *   in host DRAM ... host DRAM physical address bits 35:4 for 4965.
	 * NOTE:  iwl_init_alive_start() will replace these values,
	 *        after the "initialize" uCode has run, to point to
	 *        runtime/protocol instructions and backup data cache.
	 */
	pinst = priv->ucode_init.p_addr >> 4;
	pdata = priv->ucode_init_data.p_addr >> 4;
	inst_len = priv->ucode_init.len;
	data_len = priv->ucode_init_data.len;

	iwl_write_prph(priv, BSM_DRAM_INST_PTR_REG, pinst);
	iwl_write_prph(priv, BSM_DRAM_DATA_PTR_REG, pdata);
	iwl_write_prph(priv, BSM_DRAM_INST_BYTECOUNT_REG, inst_len);
	iwl_write_prph(priv, BSM_DRAM_DATA_BYTECOUNT_REG, data_len);

	/* Fill BSM memory with bootstrap instructions */
	for (reg_offset = BSM_SRAM_LOWER_BOUND;
	     reg_offset < BSM_SRAM_LOWER_BOUND + len;
	     reg_offset += sizeof(u32), image++)
		_iwl_write_prph(priv, reg_offset, le32_to_cpu(*image));

	ret = iwl4965_verify_bsm(priv);
	if (ret)
		return ret;

	/* Tell BSM to copy from BSM SRAM into instruction SRAM, when asked */
	iwl_write_prph(priv, BSM_WR_MEM_SRC_REG, 0x0);
	iwl_write_prph(priv, BSM_WR_MEM_DST_REG, IWL49_RTC_INST_LOWER_BOUND);
	iwl_write_prph(priv, BSM_WR_DWCOUNT_REG, len / sizeof(u32));

	/* Load bootstrap code into instruction SRAM now,
	 *   to prepare to load "initialize" uCode */
	iwl_write_prph(priv, BSM_WR_CTRL_REG, BSM_WR_CTRL_REG_BIT_START);

	/* Wait for load of bootstrap uCode to finish */
	for (i = 0; i < 100; i++) {
		done = iwl_read_prph(priv, BSM_WR_CTRL_REG);
		if (!(done & BSM_WR_CTRL_REG_BIT_START))
			break;
		udelay(10);
	}
	if (i < 100)
		IWL_DEBUG_INFO(priv, "BSM write complete, poll %d iterations\n", i);
	else {
		IWL_ERR(priv, "BSM write did not complete!\n");
		return -EIO;
	}

	/* Enable future boot loads whenever power management unit triggers it
	 *   (e.g. when powering back up after power-save shutdown) */
	iwl_write_prph(priv, BSM_WR_CTRL_REG, BSM_WR_CTRL_REG_BIT_START_EN);


	return 0;
}

/**
 * iwl4965_set_ucode_ptrs - Set uCode address location
 *
 * Tell initialization uCode where to find runtime uCode.
 *
 * BSM registers initially contain pointers to initialization uCode.
 * We need to replace them to load runtime uCode inst and data,
 * and to save runtime data when powering down.
 */
static int iwl4965_set_ucode_ptrs(struct iwl_priv *priv)
{
	dma_addr_t pinst;
	dma_addr_t pdata;
	int ret = 0;

	/* bits 35:4 for 4965 */
	pinst = priv->ucode_code.p_addr >> 4;
	pdata = priv->ucode_data_backup.p_addr >> 4;

	/* Tell bootstrap uCode where to find image to load */
	iwl_write_prph(priv, BSM_DRAM_INST_PTR_REG, pinst);
	iwl_write_prph(priv, BSM_DRAM_DATA_PTR_REG, pdata);
	iwl_write_prph(priv, BSM_DRAM_DATA_BYTECOUNT_REG,
				 priv->ucode_data.len);

	/* Inst byte count must be last to set up, bit 31 signals uCode
	 *   that all new ptr/size info is in place */
	iwl_write_prph(priv, BSM_DRAM_INST_BYTECOUNT_REG,
				 priv->ucode_code.len | BSM_DRAM_INST_LOAD);
	IWL_DEBUG_INFO(priv, "Runtime uCode pointers are set.\n");

	return ret;
}

/**
 * iwl4965_init_alive_start - Called after REPLY_ALIVE notification received
 *
 * Called after REPLY_ALIVE notification received from "initialize" uCode.
 *
 * The 4965 "initialize" ALIVE reply contains calibration data for:
 *   Voltage, temperature, and MIMO tx gain correction, now stored in priv
 *   (3945 does not contain this data).
 *
 * Tell "initialize" uCode to go ahead and load the runtime uCode.
*/
static void iwl4965_init_alive_start(struct iwl_priv *priv)
{
	/* Check alive response for "valid" sign from uCode */
	if (priv->card_alive_init.is_valid != UCODE_VALID_OK) {
		/* We had an error bringing up the hardware, so take it
		 * all the way back down so we can try again */
		IWL_DEBUG_INFO(priv, "Initialize Alive failed.\n");
		goto restart;
	}

	/* Bootstrap uCode has loaded initialize uCode ... verify inst image.
	 * This is a paranoid check, because we would not have gotten the
	 * "initialize" alive if code weren't properly loaded.  */
	if (iwl_verify_ucode(priv)) {
		/* Runtime instruction load was bad;
		 * take it all the way back down so we can try again */
		IWL_DEBUG_INFO(priv, "Bad \"initialize\" uCode load.\n");
		goto restart;
	}

	/* Calculate temperature */
	priv->temperature = iwl4965_hw_get_temperature(priv);

	/* Send pointers to protocol/runtime uCode image ... init code will
	 * load and launch runtime uCode, which will send us another "Alive"
	 * notification. */
	IWL_DEBUG_INFO(priv, "Initialization Alive received.\n");
	if (iwl4965_set_ucode_ptrs(priv)) {
		/* Runtime instruction load won't happen;
		 * take it all the way back down so we can try again */
		IWL_DEBUG_INFO(priv, "Couldn't set up uCode pointers.\n");
		goto restart;
	}
	return;

restart:
	queue_work(priv->workqueue, &priv->restart);
}

static bool is_ht40_channel(__le32 rxon_flags)
{
	int chan_mod = le32_to_cpu(rxon_flags & RXON_FLG_CHANNEL_MODE_MSK)
				    >> RXON_FLG_CHANNEL_MODE_POS;
	return ((chan_mod == CHANNEL_MODE_PURE_40) ||
		  (chan_mod == CHANNEL_MODE_MIXED));
}

/*
 * EEPROM handlers
 */
static u16 iwl4965_eeprom_calib_version(struct iwl_priv *priv)
{
	return iwl_eeprom_query16(priv, EEPROM_4965_CALIB_VERSION_OFFSET);
}

/*
 * Activate/Deactivate Tx DMA/FIFO channels according tx fifos mask
 * must be called under priv->lock and mac access
 */
static void iwl4965_txq_set_sched(struct iwl_priv *priv, u32 mask)
{
	iwl_write_prph(priv, IWL49_SCD_TXFACT, mask);
}

static void iwl4965_nic_config(struct iwl_priv *priv)
{
	unsigned long flags;
	u16 radio_cfg;

	spin_lock_irqsave(&priv->lock, flags);

	radio_cfg = iwl_eeprom_query16(priv, EEPROM_RADIO_CONFIG);

	/* write radio config values to register */
	if (EEPROM_RF_CFG_TYPE_MSK(radio_cfg) == EEPROM_4965_RF_CFG_TYPE_MAX)
		iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			    EEPROM_RF_CFG_TYPE_MSK(radio_cfg) |
			    EEPROM_RF_CFG_STEP_MSK(radio_cfg) |
			    EEPROM_RF_CFG_DASH_MSK(radio_cfg));

	/* set CSR_HW_CONFIG_REG for uCode use */
	iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
		    CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
		    CSR_HW_IF_CONFIG_REG_BIT_MAC_SI);

	priv->calib_info = (struct iwl_eeprom_calib_info *)
		iwl_eeprom_query_addr(priv, EEPROM_4965_CALIB_TXPOWER_OFFSET);

	spin_unlock_irqrestore(&priv->lock, flags);
}

/* Reset differential Rx gains in NIC to prepare for chain noise calibration.
 * Called after every association, but this runs only once!
 *  ... once chain noise is calibrated the first time, it's good forever.  */
static void iwl4965_chain_noise_reset(struct iwl_priv *priv)
{
	struct iwl_chain_noise_data *data = &(priv->chain_noise_data);

	if ((data->state == IWL_CHAIN_NOISE_ALIVE) && iwl_is_associated(priv)) {
		struct iwl_calib_diff_gain_cmd cmd;

		memset(&cmd, 0, sizeof(cmd));
		cmd.hdr.op_code = IWL_PHY_CALIBRATE_DIFF_GAIN_CMD;
		cmd.diff_gain_a = 0;
		cmd.diff_gain_b = 0;
		cmd.diff_gain_c = 0;
		if (iwl_send_cmd_pdu(priv, REPLY_PHY_CALIBRATION_CMD,
				 sizeof(cmd), &cmd))
			IWL_ERR(priv,
				"Could not send REPLY_PHY_CALIBRATION_CMD\n");
		data->state = IWL_CHAIN_NOISE_ACCUMULATE;
		IWL_DEBUG_CALIB(priv, "Run chain_noise_calibrate\n");
	}
}

static void iwl4965_gain_computation(struct iwl_priv *priv,
		u32 *average_noise,
		u16 min_average_noise_antenna_i,
		u32 min_average_noise,
		u8 default_chain)
{
	int i, ret;
	struct iwl_chain_noise_data *data = &priv->chain_noise_data;

	data->delta_gain_code[min_average_noise_antenna_i] = 0;

	for (i = default_chain; i < NUM_RX_CHAINS; i++) {
		s32 delta_g = 0;

		if (!(data->disconn_array[i]) &&
		    (data->delta_gain_code[i] ==
			     CHAIN_NOISE_DELTA_GAIN_INIT_VAL)) {
			delta_g = average_noise[i] - min_average_noise;
			data->delta_gain_code[i] = (u8)((delta_g * 10) / 15);
			data->delta_gain_code[i] =
				min(data->delta_gain_code[i],
				(u8) CHAIN_NOISE_MAX_DELTA_GAIN_CODE);

			data->delta_gain_code[i] =
				(data->delta_gain_code[i] | (1 << 2));
		} else {
			data->delta_gain_code[i] = 0;
		}
	}
	IWL_DEBUG_CALIB(priv, "delta_gain_codes: a %d b %d c %d\n",
		     data->delta_gain_code[0],
		     data->delta_gain_code[1],
		     data->delta_gain_code[2]);

	/* Differential gain gets sent to uCode only once */
	if (!data->radio_write) {
		struct iwl_calib_diff_gain_cmd cmd;
		data->radio_write = 1;

		memset(&cmd, 0, sizeof(cmd));
		cmd.hdr.op_code = IWL_PHY_CALIBRATE_DIFF_GAIN_CMD;
		cmd.diff_gain_a = data->delta_gain_code[0];
		cmd.diff_gain_b = data->delta_gain_code[1];
		cmd.diff_gain_c = data->delta_gain_code[2];
		ret = iwl_send_cmd_pdu(priv, REPLY_PHY_CALIBRATION_CMD,
				      sizeof(cmd), &cmd);
		if (ret)
			IWL_DEBUG_CALIB(priv, "fail sending cmd "
				     "REPLY_PHY_CALIBRATION_CMD \n");

		/* TODO we might want recalculate
		 * rx_chain in rxon cmd */

		/* Mark so we run this algo only once! */
		data->state = IWL_CHAIN_NOISE_CALIBRATED;
	}
	data->chain_noise_a = 0;
	data->chain_noise_b = 0;
	data->chain_noise_c = 0;
	data->chain_signal_a = 0;
	data->chain_signal_b = 0;
	data->chain_signal_c = 0;
	data->beacon_count = 0;
}

static void iwl4965_bg_txpower_work(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv,
			txpower_work);

	/* If a scan happened to start before we got here
	 * then just return; the statistics notification will
	 * kick off another scheduled work to compensate for
	 * any temperature delta we missed here. */
	if (test_bit(STATUS_EXIT_PENDING, &priv->status) ||
	    test_bit(STATUS_SCANNING, &priv->status))
		return;

	mutex_lock(&priv->mutex);

	/* Regardless of if we are associated, we must reconfigure the
	 * TX power since frames can be sent on non-radar channels while
	 * not associated */
	iwl4965_send_tx_power(priv);

	/* Update last_temperature to keep is_calib_needed from running
	 * when it isn't needed... */
	priv->last_temperature = priv->temperature;

	mutex_unlock(&priv->mutex);
}

/*
 * Acquire priv->lock before calling this function !
 */
static void iwl4965_set_wr_ptrs(struct iwl_priv *priv, int txq_id, u32 index)
{
	iwl_write_direct32(priv, HBUS_TARG_WRPTR,
			     (index & 0xff) | (txq_id << 8));
	iwl_write_prph(priv, IWL49_SCD_QUEUE_RDPTR(txq_id), index);
}

/**
 * iwl4965_tx_queue_set_status - (optionally) start Tx/Cmd queue
 * @tx_fifo_id: Tx DMA/FIFO channel (range 0-7) that the queue will feed
 * @scd_retry: (1) Indicates queue will be used in aggregation mode
 *
 * NOTE:  Acquire priv->lock before calling this function !
 */
static void iwl4965_tx_queue_set_status(struct iwl_priv *priv,
					struct iwl_tx_queue *txq,
					int tx_fifo_id, int scd_retry)
{
	int txq_id = txq->q.id;

	/* Find out whether to activate Tx queue */
	int active = test_bit(txq_id, &priv->txq_ctx_active_msk) ? 1 : 0;

	/* Set up and activate */
	iwl_write_prph(priv, IWL49_SCD_QUEUE_STATUS_BITS(txq_id),
			 (active << IWL49_SCD_QUEUE_STTS_REG_POS_ACTIVE) |
			 (tx_fifo_id << IWL49_SCD_QUEUE_STTS_REG_POS_TXF) |
			 (scd_retry << IWL49_SCD_QUEUE_STTS_REG_POS_WSL) |
			 (scd_retry << IWL49_SCD_QUEUE_STTS_REG_POS_SCD_ACK) |
			 IWL49_SCD_QUEUE_STTS_REG_MSK);

	txq->sched_retry = scd_retry;

	IWL_DEBUG_INFO(priv, "%s %s Queue %d on AC %d\n",
		       active ? "Activate" : "Deactivate",
		       scd_retry ? "BA" : "AC", txq_id, tx_fifo_id);
}

static const u16 default_queue_to_tx_fifo[] = {
	IWL_TX_FIFO_AC3,
	IWL_TX_FIFO_AC2,
	IWL_TX_FIFO_AC1,
	IWL_TX_FIFO_AC0,
	IWL49_CMD_FIFO_NUM,
	IWL_TX_FIFO_HCCA_1,
	IWL_TX_FIFO_HCCA_2
};

static int iwl4965_alive_notify(struct iwl_priv *priv)
{
	u32 a;
	unsigned long flags;
	int i, chan;
	u32 reg_val;

	spin_lock_irqsave(&priv->lock, flags);

	/* Clear 4965's internal Tx Scheduler data base */
	priv->scd_base_addr = iwl_read_prph(priv, IWL49_SCD_SRAM_BASE_ADDR);
	a = priv->scd_base_addr + IWL49_SCD_CONTEXT_DATA_OFFSET;
	for (; a < priv->scd_base_addr + IWL49_SCD_TX_STTS_BITMAP_OFFSET; a += 4)
		iwl_write_targ_mem(priv, a, 0);
	for (; a < priv->scd_base_addr + IWL49_SCD_TRANSLATE_TBL_OFFSET; a += 4)
		iwl_write_targ_mem(priv, a, 0);
	for (; a < priv->scd_base_addr +
	       IWL49_SCD_TRANSLATE_TBL_OFFSET_QUEUE(priv->hw_params.max_txq_num); a += 4)
		iwl_write_targ_mem(priv, a, 0);

	/* Tel 4965 where to find Tx byte count tables */
	iwl_write_prph(priv, IWL49_SCD_DRAM_BASE_ADDR,
			priv->scd_bc_tbls.dma >> 10);

	/* Enable DMA channel */
	for (chan = 0; chan < FH49_TCSR_CHNL_NUM ; chan++)
		iwl_write_direct32(priv, FH_TCSR_CHNL_TX_CONFIG_REG(chan),
				FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
				FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE);

	/* Update FH chicken bits */
	reg_val = iwl_read_direct32(priv, FH_TX_CHICKEN_BITS_REG);
	iwl_write_direct32(priv, FH_TX_CHICKEN_BITS_REG,
			   reg_val | FH_TX_CHICKEN_BITS_SCD_AUTO_RETRY_EN);

	/* Disable chain mode for all queues */
	iwl_write_prph(priv, IWL49_SCD_QUEUECHAIN_SEL, 0);

	/* Initialize each Tx queue (including the command queue) */
	for (i = 0; i < priv->hw_params.max_txq_num; i++) {

		/* TFD circular buffer read/write indexes */
		iwl_write_prph(priv, IWL49_SCD_QUEUE_RDPTR(i), 0);
		iwl_write_direct32(priv, HBUS_TARG_WRPTR, 0 | (i << 8));

		/* Max Tx Window size for Scheduler-ACK mode */
		iwl_write_targ_mem(priv, priv->scd_base_addr +
				IWL49_SCD_CONTEXT_QUEUE_OFFSET(i),
				(SCD_WIN_SIZE <<
				IWL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_POS) &
				IWL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_MSK);

		/* Frame limit */
		iwl_write_targ_mem(priv, priv->scd_base_addr +
				IWL49_SCD_CONTEXT_QUEUE_OFFSET(i) +
				sizeof(u32),
				(SCD_FRAME_LIMIT <<
				IWL49_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS) &
				IWL49_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK);

	}
	iwl_write_prph(priv, IWL49_SCD_INTERRUPT_MASK,
				 (1 << priv->hw_params.max_txq_num) - 1);

	/* Activate all Tx DMA/FIFO channels */
	priv->cfg->ops->lib->txq_set_sched(priv, IWL_MASK(0, 6));

	iwl4965_set_wr_ptrs(priv, IWL_CMD_QUEUE_NUM, 0);

	/* Map each Tx/cmd queue to its corresponding fifo */
	for (i = 0; i < ARRAY_SIZE(default_queue_to_tx_fifo); i++) {
		int ac = default_queue_to_tx_fifo[i];
		iwl_txq_ctx_activate(priv, i);
		iwl4965_tx_queue_set_status(priv, &priv->txq[i], ac, 0);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static struct iwl_sensitivity_ranges iwl4965_sensitivity = {
	.min_nrg_cck = 97,
	.max_nrg_cck = 0, /* not used, set to 0 */

	.auto_corr_min_ofdm = 85,
	.auto_corr_min_ofdm_mrc = 170,
	.auto_corr_min_ofdm_x1 = 105,
	.auto_corr_min_ofdm_mrc_x1 = 220,

	.auto_corr_max_ofdm = 120,
	.auto_corr_max_ofdm_mrc = 210,
	.auto_corr_max_ofdm_x1 = 140,
	.auto_corr_max_ofdm_mrc_x1 = 270,

	.auto_corr_min_cck = 125,
	.auto_corr_max_cck = 200,
	.auto_corr_min_cck_mrc = 200,
	.auto_corr_max_cck_mrc = 400,

	.nrg_th_cck = 100,
	.nrg_th_ofdm = 100,

	.barker_corr_th_min = 190,
	.barker_corr_th_min_mrc = 390,
	.nrg_th_cca = 62,
};

static void iwl4965_set_ct_threshold(struct iwl_priv *priv)
{
	/* want Kelvin */
	priv->hw_params.ct_kill_threshold =
		CELSIUS_TO_KELVIN(CT_KILL_THRESHOLD_LEGACY);
}

/**
 * iwl4965_hw_set_hw_params
 *
 * Called when initializing driver
 */
static int iwl4965_hw_set_hw_params(struct iwl_priv *priv)
{
	if (priv->cfg->mod_params->num_of_queues >= IWL_MIN_NUM_QUEUES &&
	    priv->cfg->mod_params->num_of_queues <= IWL49_NUM_QUEUES)
		priv->cfg->num_of_queues =
			priv->cfg->mod_params->num_of_queues;

	priv->hw_params.max_txq_num = priv->cfg->num_of_queues;
	priv->hw_params.dma_chnl_num = FH49_TCSR_CHNL_NUM;
	priv->hw_params.scd_bc_tbls_size =
			priv->cfg->num_of_queues *
			sizeof(struct iwl4965_scd_bc_tbl);
	priv->hw_params.tfd_size = sizeof(struct iwl_tfd);
	priv->hw_params.max_stations = IWL4965_STATION_COUNT;
	priv->hw_params.bcast_sta_id = IWL4965_BROADCAST_ID;
	priv->hw_params.max_data_size = IWL49_RTC_DATA_SIZE;
	priv->hw_params.max_inst_size = IWL49_RTC_INST_SIZE;
	priv->hw_params.max_bsm_size = BSM_SRAM_SIZE;
	priv->hw_params.ht40_channel = BIT(IEEE80211_BAND_5GHZ);

	priv->hw_params.rx_wrt_ptr_reg = FH_RSCSR_CHNL0_WPTR;

	priv->hw_params.tx_chains_num = num_of_ant(priv->cfg->valid_tx_ant);
	priv->hw_params.rx_chains_num = num_of_ant(priv->cfg->valid_rx_ant);
	priv->hw_params.valid_tx_ant = priv->cfg->valid_tx_ant;
	priv->hw_params.valid_rx_ant = priv->cfg->valid_rx_ant;
	if (priv->cfg->ops->lib->temp_ops.set_ct_kill)
		priv->cfg->ops->lib->temp_ops.set_ct_kill(priv);

	priv->hw_params.sens = &iwl4965_sensitivity;

	return 0;
}

static s32 iwl4965_math_div_round(s32 num, s32 denom, s32 *res)
{
	s32 sign = 1;

	if (num < 0) {
		sign = -sign;
		num = -num;
	}
	if (denom < 0) {
		sign = -sign;
		denom = -denom;
	}
	*res = 1;
	*res = ((num * 2 + denom) / (denom * 2)) * sign;

	return 1;
}

/**
 * iwl4965_get_voltage_compensation - Power supply voltage comp for txpower
 *
 * Determines power supply voltage compensation for txpower calculations.
 * Returns number of 1/2-dB steps to subtract from gain table index,
 * to compensate for difference between power supply voltage during
 * factory measurements, vs. current power supply voltage.
 *
 * Voltage indication is higher for lower voltage.
 * Lower voltage requires more gain (lower gain table index).
 */
static s32 iwl4965_get_voltage_compensation(s32 eeprom_voltage,
					    s32 current_voltage)
{
	s32 comp = 0;

	if ((TX_POWER_IWL_ILLEGAL_VOLTAGE == eeprom_voltage) ||
	    (TX_POWER_IWL_ILLEGAL_VOLTAGE == current_voltage))
		return 0;

	iwl4965_math_div_round(current_voltage - eeprom_voltage,
			       TX_POWER_IWL_VOLTAGE_CODES_PER_03V, &comp);

	if (current_voltage > eeprom_voltage)
		comp *= 2;
	if ((comp < -2) || (comp > 2))
		comp = 0;

	return comp;
}

static s32 iwl4965_get_tx_atten_grp(u16 channel)
{
	if (channel >= CALIB_IWL_TX_ATTEN_GR5_FCH &&
	    channel <= CALIB_IWL_TX_ATTEN_GR5_LCH)
		return CALIB_CH_GROUP_5;

	if (channel >= CALIB_IWL_TX_ATTEN_GR1_FCH &&
	    channel <= CALIB_IWL_TX_ATTEN_GR1_LCH)
		return CALIB_CH_GROUP_1;

	if (channel >= CALIB_IWL_TX_ATTEN_GR2_FCH &&
	    channel <= CALIB_IWL_TX_ATTEN_GR2_LCH)
		return CALIB_CH_GROUP_2;

	if (channel >= CALIB_IWL_TX_ATTEN_GR3_FCH &&
	    channel <= CALIB_IWL_TX_ATTEN_GR3_LCH)
		return CALIB_CH_GROUP_3;

	if (channel >= CALIB_IWL_TX_ATTEN_GR4_FCH &&
	    channel <= CALIB_IWL_TX_ATTEN_GR4_LCH)
		return CALIB_CH_GROUP_4;

	return -1;
}

static u32 iwl4965_get_sub_band(const struct iwl_priv *priv, u32 channel)
{
	s32 b = -1;

	for (b = 0; b < EEPROM_TX_POWER_BANDS; b++) {
		if (priv->calib_info->band_info[b].ch_from == 0)
			continue;

		if ((channel >= priv->calib_info->band_info[b].ch_from)
		    && (channel <= priv->calib_info->band_info[b].ch_to))
			break;
	}

	return b;
}

static s32 iwl4965_interpolate_value(s32 x, s32 x1, s32 y1, s32 x2, s32 y2)
{
	s32 val;

	if (x2 == x1)
		return y1;
	else {
		iwl4965_math_div_round((x2 - x) * (y1 - y2), (x2 - x1), &val);
		return val + y2;
	}
}

/**
 * iwl4965_interpolate_chan - Interpolate factory measurements for one channel
 *
 * Interpolates factory measurements from the two sample channels within a
 * sub-band, to apply to channel of interest.  Interpolation is proportional to
 * differences in channel frequencies, which is proportional to differences
 * in channel number.
 */
static int iwl4965_interpolate_chan(struct iwl_priv *priv, u32 channel,
				    struct iwl_eeprom_calib_ch_info *chan_info)
{
	s32 s = -1;
	u32 c;
	u32 m;
	const struct iwl_eeprom_calib_measure *m1;
	const struct iwl_eeprom_calib_measure *m2;
	struct iwl_eeprom_calib_measure *omeas;
	u32 ch_i1;
	u32 ch_i2;

	s = iwl4965_get_sub_band(priv, channel);
	if (s >= EEPROM_TX_POWER_BANDS) {
		IWL_ERR(priv, "Tx Power can not find channel %d\n", channel);
		return -1;
	}

	ch_i1 = priv->calib_info->band_info[s].ch1.ch_num;
	ch_i2 = priv->calib_info->band_info[s].ch2.ch_num;
	chan_info->ch_num = (u8) channel;

	IWL_DEBUG_TXPOWER(priv, "channel %d subband %d factory cal ch %d & %d\n",
			  channel, s, ch_i1, ch_i2);

	for (c = 0; c < EEPROM_TX_POWER_TX_CHAINS; c++) {
		for (m = 0; m < EEPROM_TX_POWER_MEASUREMENTS; m++) {
			m1 = &(priv->calib_info->band_info[s].ch1.
			       measurements[c][m]);
			m2 = &(priv->calib_info->band_info[s].ch2.
			       measurements[c][m]);
			omeas = &(chan_info->measurements[c][m]);

			omeas->actual_pow =
			    (u8) iwl4965_interpolate_value(channel, ch_i1,
							   m1->actual_pow,
							   ch_i2,
							   m2->actual_pow);
			omeas->gain_idx =
			    (u8) iwl4965_interpolate_value(channel, ch_i1,
							   m1->gain_idx, ch_i2,
							   m2->gain_idx);
			omeas->temperature =
			    (u8) iwl4965_interpolate_value(channel, ch_i1,
							   m1->temperature,
							   ch_i2,
							   m2->temperature);
			omeas->pa_det =
			    (s8) iwl4965_interpolate_value(channel, ch_i1,
							   m1->pa_det, ch_i2,
							   m2->pa_det);

			IWL_DEBUG_TXPOWER(priv,
				"chain %d meas %d AP1=%d AP2=%d AP=%d\n", c, m,
				m1->actual_pow, m2->actual_pow, omeas->actual_pow);
			IWL_DEBUG_TXPOWER(priv,
				"chain %d meas %d NI1=%d NI2=%d NI=%d\n", c, m,
				m1->gain_idx, m2->gain_idx, omeas->gain_idx);
			IWL_DEBUG_TXPOWER(priv,
				"chain %d meas %d PA1=%d PA2=%d PA=%d\n", c, m,
				m1->pa_det, m2->pa_det, omeas->pa_det);
			IWL_DEBUG_TXPOWER(priv,
				"chain %d meas %d  T1=%d  T2=%d  T=%d\n", c, m,
				m1->temperature, m2->temperature,
				omeas->temperature);
		}
	}

	return 0;
}

/* bit-rate-dependent table to prevent Tx distortion, in half-dB units,
 * for OFDM 6, 12, 18, 24, 36, 48, 54, 60 MBit, and CCK all rates. */
static s32 back_off_table[] = {
	10, 10, 10, 10, 10, 15, 17, 20,	/* OFDM SISO 20 MHz */
	10, 10, 10, 10, 10, 15, 17, 20,	/* OFDM MIMO 20 MHz */
	10, 10, 10, 10, 10, 15, 17, 20,	/* OFDM SISO 40 MHz */
	10, 10, 10, 10, 10, 15, 17, 20,	/* OFDM MIMO 40 MHz */
	10			/* CCK */
};

/* Thermal compensation values for txpower for various frequency ranges ...
 *   ratios from 3:1 to 4.5:1 of degrees (Celsius) per half-dB gain adjust */
static struct iwl4965_txpower_comp_entry {
	s32 degrees_per_05db_a;
	s32 degrees_per_05db_a_denom;
} tx_power_cmp_tble[CALIB_CH_GROUP_MAX] = {
	{9, 2},			/* group 0 5.2, ch  34-43 */
	{4, 1},			/* group 1 5.2, ch  44-70 */
	{4, 1},			/* group 2 5.2, ch  71-124 */
	{4, 1},			/* group 3 5.2, ch 125-200 */
	{3, 1}			/* group 4 2.4, ch   all */
};

static s32 get_min_power_index(s32 rate_power_index, u32 band)
{
	if (!band) {
		if ((rate_power_index & 7) <= 4)
			return MIN_TX_GAIN_INDEX_52GHZ_EXT;
	}
	return MIN_TX_GAIN_INDEX;
}

struct gain_entry {
	u8 dsp;
	u8 radio;
};

static const struct gain_entry gain_table[2][108] = {
	/* 5.2GHz power gain index table */
	{
	 {123, 0x3F},		/* highest txpower */
	 {117, 0x3F},
	 {110, 0x3F},
	 {104, 0x3F},
	 {98, 0x3F},
	 {110, 0x3E},
	 {104, 0x3E},
	 {98, 0x3E},
	 {110, 0x3D},
	 {104, 0x3D},
	 {98, 0x3D},
	 {110, 0x3C},
	 {104, 0x3C},
	 {98, 0x3C},
	 {110, 0x3B},
	 {104, 0x3B},
	 {98, 0x3B},
	 {110, 0x3A},
	 {104, 0x3A},
	 {98, 0x3A},
	 {110, 0x39},
	 {104, 0x39},
	 {98, 0x39},
	 {110, 0x38},
	 {104, 0x38},
	 {98, 0x38},
	 {110, 0x37},
	 {104, 0x37},
	 {98, 0x37},
	 {110, 0x36},
	 {104, 0x36},
	 {98, 0x36},
	 {110, 0x35},
	 {104, 0x35},
	 {98, 0x35},
	 {110, 0x34},
	 {104, 0x34},
	 {98, 0x34},
	 {110, 0x33},
	 {104, 0x33},
	 {98, 0x33},
	 {110, 0x32},
	 {104, 0x32},
	 {98, 0x32},
	 {110, 0x31},
	 {104, 0x31},
	 {98, 0x31},
	 {110, 0x30},
	 {104, 0x30},
	 {98, 0x30},
	 {110, 0x25},
	 {104, 0x25},
	 {98, 0x25},
	 {110, 0x24},
	 {104, 0x24},
	 {98, 0x24},
	 {110, 0x23},
	 {104, 0x23},
	 {98, 0x23},
	 {110, 0x22},
	 {104, 0x18},
	 {98, 0x18},
	 {110, 0x17},
	 {104, 0x17},
	 {98, 0x17},
	 {110, 0x16},
	 {104, 0x16},
	 {98, 0x16},
	 {110, 0x15},
	 {104, 0x15},
	 {98, 0x15},
	 {110, 0x14},
	 {104, 0x14},
	 {98, 0x14},
	 {110, 0x13},
	 {104, 0x13},
	 {98, 0x13},
	 {110, 0x12},
	 {104, 0x08},
	 {98, 0x08},
	 {110, 0x07},
	 {104, 0x07},
	 {98, 0x07},
	 {110, 0x06},
	 {104, 0x06},
	 {98, 0x06},
	 {110, 0x05},
	 {104, 0x05},
	 {98, 0x05},
	 {110, 0x04},
	 {104, 0x04},
	 {98, 0x04},
	 {110, 0x03},
	 {104, 0x03},
	 {98, 0x03},
	 {110, 0x02},
	 {104, 0x02},
	 {98, 0x02},
	 {110, 0x01},
	 {104, 0x01},
	 {98, 0x01},
	 {110, 0x00},
	 {104, 0x00},
	 {98, 0x00},
	 {93, 0x00},
	 {88, 0x00},
	 {83, 0x00},
	 {78, 0x00},
	 },
	/* 2.4GHz power gain index table */
	{
	 {110, 0x3f},		/* highest txpower */
	 {104, 0x3f},
	 {98, 0x3f},
	 {110, 0x3e},
	 {104, 0x3e},
	 {98, 0x3e},
	 {110, 0x3d},
	 {104, 0x3d},
	 {98, 0x3d},
	 {110, 0x3c},
	 {104, 0x3c},
	 {98, 0x3c},
	 {110, 0x3b},
	 {104, 0x3b},
	 {98, 0x3b},
	 {110, 0x3a},
	 {104, 0x3a},
	 {98, 0x3a},
	 {110, 0x39},
	 {104, 0x39},
	 {98, 0x39},
	 {110, 0x38},
	 {104, 0x38},
	 {98, 0x38},
	 {110, 0x37},
	 {104, 0x37},
	 {98, 0x37},
	 {110, 0x36},
	 {104, 0x36},
	 {98, 0x36},
	 {110, 0x35},
	 {104, 0x35},
	 {98, 0x35},
	 {110, 0x34},
	 {104, 0x34},
	 {98, 0x34},
	 {110, 0x33},
	 {104, 0x33},
	 {98, 0x33},
	 {110, 0x32},
	 {104, 0x32},
	 {98, 0x32},
	 {110, 0x31},
	 {104, 0x31},
	 {98, 0x31},
	 {110, 0x30},
	 {104, 0x30},
	 {98, 0x30},
	 {110, 0x6},
	 {104, 0x6},
	 {98, 0x6},
	 {110, 0x5},
	 {104, 0x5},
	 {98, 0x5},
	 {110, 0x4},
	 {104, 0x4},
	 {98, 0x4},
	 {110, 0x3},
	 {104, 0x3},
	 {98, 0x3},
	 {110, 0x2},
	 {104, 0x2},
	 {98, 0x2},
	 {110, 0x1},
	 {104, 0x1},
	 {98, 0x1},
	 {110, 0x0},
	 {104, 0x0},
	 {98, 0x0},
	 {97, 0},
	 {96, 0},
	 {95, 0},
	 {94, 0},
	 {93, 0},
	 {92, 0},
	 {91, 0},
	 {90, 0},
	 {89, 0},
	 {88, 0},
	 {87, 0},
	 {86, 0},
	 {85, 0},
	 {84, 0},
	 {83, 0},
	 {82, 0},
	 {81, 0},
	 {80, 0},
	 {79, 0},
	 {78, 0},
	 {77, 0},
	 {76, 0},
	 {75, 0},
	 {74, 0},
	 {73, 0},
	 {72, 0},
	 {71, 0},
	 {70, 0},
	 {69, 0},
	 {68, 0},
	 {67, 0},
	 {66, 0},
	 {65, 0},
	 {64, 0},
	 {63, 0},
	 {62, 0},
	 {61, 0},
	 {60, 0},
	 {59, 0},
	 }
};

static int iwl4965_fill_txpower_tbl(struct iwl_priv *priv, u8 band, u16 channel,
				    u8 is_ht40, u8 ctrl_chan_high,
				    struct iwl4965_tx_power_db *tx_power_tbl)
{
	u8 saturation_power;
	s32 target_power;
	s32 user_target_power;
	s32 power_limit;
	s32 current_temp;
	s32 reg_limit;
	s32 current_regulatory;
	s32 txatten_grp = CALIB_CH_GROUP_MAX;
	int i;
	int c;
	const struct iwl_channel_info *ch_info = NULL;
	struct iwl_eeprom_calib_ch_info ch_eeprom_info;
	const struct iwl_eeprom_calib_measure *measurement;
	s16 voltage;
	s32 init_voltage;
	s32 voltage_compensation;
	s32 degrees_per_05db_num;
	s32 degrees_per_05db_denom;
	s32 factory_temp;
	s32 temperature_comp[2];
	s32 factory_gain_index[2];
	s32 factory_actual_pwr[2];
	s32 power_index;

	/* tx_power_user_lmt is in dBm, convert to half-dBm (half-dB units
	 *   are used for indexing into txpower table) */
	user_target_power = 2 * priv->tx_power_user_lmt;

	/* Get current (RXON) channel, band, width */
	IWL_DEBUG_TXPOWER(priv, "chan %d band %d is_ht40 %d\n", channel, band,
			  is_ht40);

	ch_info = iwl_get_channel_info(priv, priv->band, channel);

	if (!is_channel_valid(ch_info))
		return -EINVAL;

	/* get txatten group, used to select 1) thermal txpower adjustment
	 *   and 2) mimo txpower balance between Tx chains. */
	txatten_grp = iwl4965_get_tx_atten_grp(channel);
	if (txatten_grp < 0) {
		IWL_ERR(priv, "Can't find txatten group for channel %d.\n",
			  channel);
		return -EINVAL;
	}

	IWL_DEBUG_TXPOWER(priv, "channel %d belongs to txatten group %d\n",
			  channel, txatten_grp);

	if (is_ht40) {
		if (ctrl_chan_high)
			channel -= 2;
		else
			channel += 2;
	}

	/* hardware txpower limits ...
	 * saturation (clipping distortion) txpowers are in half-dBm */
	if (band)
		saturation_power = priv->calib_info->saturation_power24;
	else
		saturation_power = priv->calib_info->saturation_power52;

	if (saturation_power < IWL_TX_POWER_SATURATION_MIN ||
	    saturation_power > IWL_TX_POWER_SATURATION_MAX) {
		if (band)
			saturation_power = IWL_TX_POWER_DEFAULT_SATURATION_24;
		else
			saturation_power = IWL_TX_POWER_DEFAULT_SATURATION_52;
	}

	/* regulatory txpower limits ... reg_limit values are in half-dBm,
	 *   max_power_avg values are in dBm, convert * 2 */
	if (is_ht40)
		reg_limit = ch_info->ht40_max_power_avg * 2;
	else
		reg_limit = ch_info->max_power_avg * 2;

	if ((reg_limit < IWL_TX_POWER_REGULATORY_MIN) ||
	    (reg_limit > IWL_TX_POWER_REGULATORY_MAX)) {
		if (band)
			reg_limit = IWL_TX_POWER_DEFAULT_REGULATORY_24;
		else
			reg_limit = IWL_TX_POWER_DEFAULT_REGULATORY_52;
	}

	/* Interpolate txpower calibration values for this channel,
	 *   based on factory calibration tests on spaced channels. */
	iwl4965_interpolate_chan(priv, channel, &ch_eeprom_info);

	/* calculate tx gain adjustment based on power supply voltage */
	voltage = priv->calib_info->voltage;
	init_voltage = (s32)le32_to_cpu(priv->card_alive_init.voltage);
	voltage_compensation =
	    iwl4965_get_voltage_compensation(voltage, init_voltage);

	IWL_DEBUG_TXPOWER(priv, "curr volt %d eeprom volt %d volt comp %d\n",
			  init_voltage,
			  voltage, voltage_compensation);

	/* get current temperature (Celsius) */
	current_temp = max(priv->temperature, IWL_TX_POWER_TEMPERATURE_MIN);
	current_temp = min(priv->temperature, IWL_TX_POWER_TEMPERATURE_MAX);
	current_temp = KELVIN_TO_CELSIUS(current_temp);

	/* select thermal txpower adjustment params, based on channel group
	 *   (same frequency group used for mimo txatten adjustment) */
	degrees_per_05db_num =
	    tx_power_cmp_tble[txatten_grp].degrees_per_05db_a;
	degrees_per_05db_denom =
	    tx_power_cmp_tble[txatten_grp].degrees_per_05db_a_denom;

	/* get per-chain txpower values from factory measurements */
	for (c = 0; c < 2; c++) {
		measurement = &ch_eeprom_info.measurements[c][1];

		/* txgain adjustment (in half-dB steps) based on difference
		 *   between factory and current temperature */
		factory_temp = measurement->temperature;
		iwl4965_math_div_round((current_temp - factory_temp) *
				       degrees_per_05db_denom,
				       degrees_per_05db_num,
				       &temperature_comp[c]);

		factory_gain_index[c] = measurement->gain_idx;
		factory_actual_pwr[c] = measurement->actual_pow;

		IWL_DEBUG_TXPOWER(priv, "chain = %d\n", c);
		IWL_DEBUG_TXPOWER(priv, "fctry tmp %d, "
				  "curr tmp %d, comp %d steps\n",
				  factory_temp, current_temp,
				  temperature_comp[c]);

		IWL_DEBUG_TXPOWER(priv, "fctry idx %d, fctry pwr %d\n",
				  factory_gain_index[c],
				  factory_actual_pwr[c]);
	}

	/* for each of 33 bit-rates (including 1 for CCK) */
	for (i = 0; i < POWER_TABLE_NUM_ENTRIES; i++) {
		u8 is_mimo_rate;
		union iwl4965_tx_power_dual_stream tx_power;

		/* for mimo, reduce each chain's txpower by half
		 * (3dB, 6 steps), so total output power is regulatory
		 * compliant. */
		if (i & 0x8) {
			current_regulatory = reg_limit -
			    IWL_TX_POWER_MIMO_REGULATORY_COMPENSATION;
			is_mimo_rate = 1;
		} else {
			current_regulatory = reg_limit;
			is_mimo_rate = 0;
		}

		/* find txpower limit, either hardware or regulatory */
		power_limit = saturation_power - back_off_table[i];
		if (power_limit > current_regulatory)
			power_limit = current_regulatory;

		/* reduce user's txpower request if necessary
		 * for this rate on this channel */
		target_power = user_target_power;
		if (target_power > power_limit)
			target_power = power_limit;

		IWL_DEBUG_TXPOWER(priv, "rate %d sat %d reg %d usr %d tgt %d\n",
				  i, saturation_power - back_off_table[i],
				  current_regulatory, user_target_power,
				  target_power);

		/* for each of 2 Tx chains (radio transmitters) */
		for (c = 0; c < 2; c++) {
			s32 atten_value;

			if (is_mimo_rate)
				atten_value =
				    (s32)le32_to_cpu(priv->card_alive_init.
				    tx_atten[txatten_grp][c]);
			else
				atten_value = 0;

			/* calculate index; higher index means lower txpower */
			power_index = (u8) (factory_gain_index[c] -
					    (target_power -
					     factory_actual_pwr[c]) -
					    temperature_comp[c] -
					    voltage_compensation +
					    atten_value);

/*			IWL_DEBUG_TXPOWER(priv, "calculated txpower index %d\n",
						power_index); */

			if (power_index < get_min_power_index(i, band))
				power_index = get_min_power_index(i, band);

			/* adjust 5 GHz index to support negative indexes */
			if (!band)
				power_index += 9;

			/* CCK, rate 32, reduce txpower for CCK */
			if (i == POWER_TABLE_CCK_ENTRY)
				power_index +=
				    IWL_TX_POWER_CCK_COMPENSATION_C_STEP;

			/* stay within the table! */
			if (power_index > 107) {
				IWL_WARN(priv, "txpower index %d > 107\n",
					    power_index);
				power_index = 107;
			}
			if (power_index < 0) {
				IWL_WARN(priv, "txpower index %d < 0\n",
					    power_index);
				power_index = 0;
			}

			/* fill txpower command for this rate/chain */
			tx_power.s.radio_tx_gain[c] =
				gain_table[band][power_index].radio;
			tx_power.s.dsp_predis_atten[c] =
				gain_table[band][power_index].dsp;

			IWL_DEBUG_TXPOWER(priv, "chain %d mimo %d index %d "
					  "gain 0x%02x dsp %d\n",
					  c, atten_value, power_index,
					tx_power.s.radio_tx_gain[c],
					tx_power.s.dsp_predis_atten[c]);
		} /* for each chain */

		tx_power_tbl->power_tbl[i].dw = cpu_to_le32(tx_power.dw);

	} /* for each rate */

	return 0;
}

/**
 * iwl4965_send_tx_power - Configure the TXPOWER level user limit
 *
 * Uses the active RXON for channel, band, and characteristics (ht40, high)
 * The power limit is taken from priv->tx_power_user_lmt.
 */
static int iwl4965_send_tx_power(struct iwl_priv *priv)
{
	struct iwl4965_txpowertable_cmd cmd = { 0 };
	int ret;
	u8 band = 0;
	bool is_ht40 = false;
	u8 ctrl_chan_high = 0;

	if (test_bit(STATUS_SCANNING, &priv->status)) {
		/* If this gets hit a lot, switch it to a BUG() and catch
		 * the stack trace to find out who is calling this during
		 * a scan. */
		IWL_WARN(priv, "TX Power requested while scanning!\n");
		return -EAGAIN;
	}

	band = priv->band == IEEE80211_BAND_2GHZ;

	is_ht40 =  is_ht40_channel(priv->active_rxon.flags);

	if (is_ht40 &&
	    (priv->active_rxon.flags & RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK))
		ctrl_chan_high = 1;

	cmd.band = band;
	cmd.channel = priv->active_rxon.channel;

	ret = iwl4965_fill_txpower_tbl(priv, band,
				le16_to_cpu(priv->active_rxon.channel),
				is_ht40, ctrl_chan_high, &cmd.tx_power);
	if (ret)
		goto out;

	ret = iwl_send_cmd_pdu(priv, REPLY_TX_PWR_TABLE_CMD, sizeof(cmd), &cmd);

out:
	return ret;
}

static int iwl4965_send_rxon_assoc(struct iwl_priv *priv)
{
	int ret = 0;
	struct iwl4965_rxon_assoc_cmd rxon_assoc;
	const struct iwl_rxon_cmd *rxon1 = &priv->staging_rxon;
	const struct iwl_rxon_cmd *rxon2 = &priv->active_rxon;

	if ((rxon1->flags == rxon2->flags) &&
	    (rxon1->filter_flags == rxon2->filter_flags) &&
	    (rxon1->cck_basic_rates == rxon2->cck_basic_rates) &&
	    (rxon1->ofdm_ht_single_stream_basic_rates ==
	     rxon2->ofdm_ht_single_stream_basic_rates) &&
	    (rxon1->ofdm_ht_dual_stream_basic_rates ==
	     rxon2->ofdm_ht_dual_stream_basic_rates) &&
	    (rxon1->rx_chain == rxon2->rx_chain) &&
	    (rxon1->ofdm_basic_rates == rxon2->ofdm_basic_rates)) {
		IWL_DEBUG_INFO(priv, "Using current RXON_ASSOC.  Not resending.\n");
		return 0;
	}

	rxon_assoc.flags = priv->staging_rxon.flags;
	rxon_assoc.filter_flags = priv->staging_rxon.filter_flags;
	rxon_assoc.ofdm_basic_rates = priv->staging_rxon.ofdm_basic_rates;
	rxon_assoc.cck_basic_rates = priv->staging_rxon.cck_basic_rates;
	rxon_assoc.reserved = 0;
	rxon_assoc.ofdm_ht_single_stream_basic_rates =
	    priv->staging_rxon.ofdm_ht_single_stream_basic_rates;
	rxon_assoc.ofdm_ht_dual_stream_basic_rates =
	    priv->staging_rxon.ofdm_ht_dual_stream_basic_rates;
	rxon_assoc.rx_chain_select_flags = priv->staging_rxon.rx_chain;

	ret = iwl_send_cmd_pdu_async(priv, REPLY_RXON_ASSOC,
				     sizeof(rxon_assoc), &rxon_assoc, NULL);
	if (ret)
		return ret;

	return ret;
}

static int iwl4965_hw_channel_switch(struct iwl_priv *priv, u16 channel)
{
	int rc;
	u8 band = 0;
	bool is_ht40 = false;
	u8 ctrl_chan_high = 0;
	struct iwl4965_channel_switch_cmd cmd;
	const struct iwl_channel_info *ch_info;

	band = priv->band == IEEE80211_BAND_2GHZ;

	ch_info = iwl_get_channel_info(priv, priv->band, channel);

	is_ht40 = is_ht40_channel(priv->staging_rxon.flags);

	if (is_ht40 &&
	    (priv->active_rxon.flags & RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK))
		ctrl_chan_high = 1;

	cmd.band = band;
	cmd.expect_beacon = 0;
	cmd.channel = cpu_to_le16(channel);
	cmd.rxon_flags = priv->active_rxon.flags;
	cmd.rxon_filter_flags = priv->active_rxon.filter_flags;
	cmd.switch_time = cpu_to_le32(priv->ucode_beacon_time);
	if (ch_info)
		cmd.expect_beacon = is_channel_radar(ch_info);
	else {
		IWL_ERR(priv, "invalid channel switch from %u to %u\n",
			priv->active_rxon.channel, channel);
		return -EFAULT;
	}

	rc = iwl4965_fill_txpower_tbl(priv, band, channel, is_ht40,
				      ctrl_chan_high, &cmd.tx_power);
	if (rc) {
		IWL_DEBUG_11H(priv, "error:%d  fill txpower_tbl\n", rc);
		return rc;
	}

	rc = iwl_send_cmd_pdu(priv, REPLY_CHANNEL_SWITCH, sizeof(cmd), &cmd);
	return rc;
}

/**
 * iwl4965_txq_update_byte_cnt_tbl - Set up entry in Tx byte-count array
 */
static void iwl4965_txq_update_byte_cnt_tbl(struct iwl_priv *priv,
					    struct iwl_tx_queue *txq,
					    u16 byte_cnt)
{
	struct iwl4965_scd_bc_tbl *scd_bc_tbl = priv->scd_bc_tbls.addr;
	int txq_id = txq->q.id;
	int write_ptr = txq->q.write_ptr;
	int len = byte_cnt + IWL_TX_CRC_SIZE + IWL_TX_DELIMITER_SIZE;
	__le16 bc_ent;

	WARN_ON(len > 0xFFF || write_ptr >= TFD_QUEUE_SIZE_MAX);

	bc_ent = cpu_to_le16(len & 0xFFF);
	/* Set up byte count within first 256 entries */
	scd_bc_tbl[txq_id].tfd_offset[write_ptr] = bc_ent;

	/* If within first 64 entries, duplicate at end */
	if (write_ptr < TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[txq_id].
			tfd_offset[TFD_QUEUE_SIZE_MAX + write_ptr] = bc_ent;
}

/**
 * sign_extend - Sign extend a value using specified bit as sign-bit
 *
 * Example: sign_extend(9, 3) would return -7 as bit3 of 1001b is 1
 * and bit0..2 is 001b which when sign extended to 1111111111111001b is -7.
 *
 * @param oper value to sign extend
 * @param index 0 based bit index (0<=index<32) to sign bit
 */
static s32 sign_extend(u32 oper, int index)
{
	u8 shift = 31 - index;

	return (s32)(oper << shift) >> shift;
}

/**
 * iwl4965_hw_get_temperature - return the calibrated temperature (in Kelvin)
 * @statistics: Provides the temperature reading from the uCode
 *
 * A return of <0 indicates bogus data in the statistics
 */
static int iwl4965_hw_get_temperature(struct iwl_priv *priv)
{
	s32 temperature;
	s32 vt;
	s32 R1, R2, R3;
	u32 R4;

	if (test_bit(STATUS_TEMPERATURE, &priv->status) &&
		(priv->statistics.flag & STATISTICS_REPLY_FLG_HT40_MODE_MSK)) {
		IWL_DEBUG_TEMP(priv, "Running HT40 temperature calibration\n");
		R1 = (s32)le32_to_cpu(priv->card_alive_init.therm_r1[1]);
		R2 = (s32)le32_to_cpu(priv->card_alive_init.therm_r2[1]);
		R3 = (s32)le32_to_cpu(priv->card_alive_init.therm_r3[1]);
		R4 = le32_to_cpu(priv->card_alive_init.therm_r4[1]);
	} else {
		IWL_DEBUG_TEMP(priv, "Running temperature calibration\n");
		R1 = (s32)le32_to_cpu(priv->card_alive_init.therm_r1[0]);
		R2 = (s32)le32_to_cpu(priv->card_alive_init.therm_r2[0]);
		R3 = (s32)le32_to_cpu(priv->card_alive_init.therm_r3[0]);
		R4 = le32_to_cpu(priv->card_alive_init.therm_r4[0]);
	}

	/*
	 * Temperature is only 23 bits, so sign extend out to 32.
	 *
	 * NOTE If we haven't received a statistics notification yet
	 * with an updated temperature, use R4 provided to us in the
	 * "initialize" ALIVE response.
	 */
	if (!test_bit(STATUS_TEMPERATURE, &priv->status))
		vt = sign_extend(R4, 23);
	else
		vt = sign_extend(
			le32_to_cpu(priv->statistics.general.temperature), 23);

	IWL_DEBUG_TEMP(priv, "Calib values R[1-3]: %d %d %d R4: %d\n", R1, R2, R3, vt);

	if (R3 == R1) {
		IWL_ERR(priv, "Calibration conflict R1 == R3\n");
		return -1;
	}

	/* Calculate temperature in degrees Kelvin, adjust by 97%.
	 * Add offset to center the adjustment around 0 degrees Centigrade. */
	temperature = TEMPERATURE_CALIB_A_VAL * (vt - R2);
	temperature /= (R3 - R1);
	temperature = (temperature * 97) / 100 + TEMPERATURE_CALIB_KELVIN_OFFSET;

	IWL_DEBUG_TEMP(priv, "Calibrated temperature: %dK, %dC\n",
			temperature, KELVIN_TO_CELSIUS(temperature));

	return temperature;
}

/* Adjust Txpower only if temperature variance is greater than threshold. */
#define IWL_TEMPERATURE_THRESHOLD   3

/**
 * iwl4965_is_temp_calib_needed - determines if new calibration is needed
 *
 * If the temperature changed has changed sufficiently, then a recalibration
 * is needed.
 *
 * Assumes caller will replace priv->last_temperature once calibration
 * executed.
 */
static int iwl4965_is_temp_calib_needed(struct iwl_priv *priv)
{
	int temp_diff;

	if (!test_bit(STATUS_STATISTICS, &priv->status)) {
		IWL_DEBUG_TEMP(priv, "Temperature not updated -- no statistics.\n");
		return 0;
	}

	temp_diff = priv->temperature - priv->last_temperature;

	/* get absolute value */
	if (temp_diff < 0) {
		IWL_DEBUG_POWER(priv, "Getting cooler, delta %d, \n", temp_diff);
		temp_diff = -temp_diff;
	} else if (temp_diff == 0)
		IWL_DEBUG_POWER(priv, "Same temp, \n");
	else
		IWL_DEBUG_POWER(priv, "Getting warmer, delta %d, \n", temp_diff);

	if (temp_diff < IWL_TEMPERATURE_THRESHOLD) {
		IWL_DEBUG_POWER(priv, "Thermal txpower calib not needed\n");
		return 0;
	}

	IWL_DEBUG_POWER(priv, "Thermal txpower calib needed\n");

	return 1;
}

static void iwl4965_temperature_calib(struct iwl_priv *priv)
{
	s32 temp;

	temp = iwl4965_hw_get_temperature(priv);
	if (temp < 0)
		return;

	if (priv->temperature != temp) {
		if (priv->temperature)
			IWL_DEBUG_TEMP(priv, "Temperature changed "
				       "from %dC to %dC\n",
				       KELVIN_TO_CELSIUS(priv->temperature),
				       KELVIN_TO_CELSIUS(temp));
		else
			IWL_DEBUG_TEMP(priv, "Temperature "
				       "initialized to %dC\n",
				       KELVIN_TO_CELSIUS(temp));
	}

	priv->temperature = temp;
	iwl_tt_handler(priv);
	set_bit(STATUS_TEMPERATURE, &priv->status);

	if (!priv->disable_tx_power_cal &&
	     unlikely(!test_bit(STATUS_SCANNING, &priv->status)) &&
	     iwl4965_is_temp_calib_needed(priv))
		queue_work(priv->workqueue, &priv->txpower_work);
}

/**
 * iwl4965_tx_queue_stop_scheduler - Stop queue, but keep configuration
 */
static void iwl4965_tx_queue_stop_scheduler(struct iwl_priv *priv,
					    u16 txq_id)
{
	/* Simply stop the queue, but don't change any configuration;
	 * the SCD_ACT_EN bit is the write-enable mask for the ACTIVE bit. */
	iwl_write_prph(priv,
		IWL49_SCD_QUEUE_STATUS_BITS(txq_id),
		(0 << IWL49_SCD_QUEUE_STTS_REG_POS_ACTIVE)|
		(1 << IWL49_SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN));
}

/**
 * txq_id must be greater than IWL49_FIRST_AMPDU_QUEUE
 * priv->lock must be held by the caller
 */
static int iwl4965_txq_agg_disable(struct iwl_priv *priv, u16 txq_id,
				   u16 ssn_idx, u8 tx_fifo)
{
	if ((IWL49_FIRST_AMPDU_QUEUE > txq_id) ||
	    (IWL49_FIRST_AMPDU_QUEUE + priv->cfg->num_of_ampdu_queues
	     <= txq_id)) {
		IWL_WARN(priv,
			"queue number out of range: %d, must be %d to %d\n",
			txq_id, IWL49_FIRST_AMPDU_QUEUE,
			IWL49_FIRST_AMPDU_QUEUE +
			priv->cfg->num_of_ampdu_queues - 1);
		return -EINVAL;
	}

	iwl4965_tx_queue_stop_scheduler(priv, txq_id);

	iwl_clear_bits_prph(priv, IWL49_SCD_QUEUECHAIN_SEL, (1 << txq_id));

	priv->txq[txq_id].q.read_ptr = (ssn_idx & 0xff);
	priv->txq[txq_id].q.write_ptr = (ssn_idx & 0xff);
	/* supposes that ssn_idx is valid (!= 0xFFF) */
	iwl4965_set_wr_ptrs(priv, txq_id, ssn_idx);

	iwl_clear_bits_prph(priv, IWL49_SCD_INTERRUPT_MASK, (1 << txq_id));
	iwl_txq_ctx_deactivate(priv, txq_id);
	iwl4965_tx_queue_set_status(priv, &priv->txq[txq_id], tx_fifo, 0);

	return 0;
}

/**
 * iwl4965_tx_queue_set_q2ratid - Map unique receiver/tid combination to a queue
 */
static int iwl4965_tx_queue_set_q2ratid(struct iwl_priv *priv, u16 ra_tid,
					u16 txq_id)
{
	u32 tbl_dw_addr;
	u32 tbl_dw;
	u16 scd_q2ratid;

	scd_q2ratid = ra_tid & IWL_SCD_QUEUE_RA_TID_MAP_RATID_MSK;

	tbl_dw_addr = priv->scd_base_addr +
			IWL49_SCD_TRANSLATE_TBL_OFFSET_QUEUE(txq_id);

	tbl_dw = iwl_read_targ_mem(priv, tbl_dw_addr);

	if (txq_id & 0x1)
		tbl_dw = (scd_q2ratid << 16) | (tbl_dw & 0x0000FFFF);
	else
		tbl_dw = scd_q2ratid | (tbl_dw & 0xFFFF0000);

	iwl_write_targ_mem(priv, tbl_dw_addr, tbl_dw);

	return 0;
}


/**
 * iwl4965_tx_queue_agg_enable - Set up & enable aggregation for selected queue
 *
 * NOTE:  txq_id must be greater than IWL49_FIRST_AMPDU_QUEUE,
 *        i.e. it must be one of the higher queues used for aggregation
 */
static int iwl4965_txq_agg_enable(struct iwl_priv *priv, int txq_id,
				  int tx_fifo, int sta_id, int tid, u16 ssn_idx)
{
	unsigned long flags;
	u16 ra_tid;

	if ((IWL49_FIRST_AMPDU_QUEUE > txq_id) ||
	    (IWL49_FIRST_AMPDU_QUEUE + priv->cfg->num_of_ampdu_queues
	     <= txq_id)) {
		IWL_WARN(priv,
			"queue number out of range: %d, must be %d to %d\n",
			txq_id, IWL49_FIRST_AMPDU_QUEUE,
			IWL49_FIRST_AMPDU_QUEUE +
			priv->cfg->num_of_ampdu_queues - 1);
		return -EINVAL;
	}

	ra_tid = BUILD_RAxTID(sta_id, tid);

	/* Modify device's station table to Tx this TID */
	iwl_sta_tx_modify_enable_tid(priv, sta_id, tid);

	spin_lock_irqsave(&priv->lock, flags);

	/* Stop this Tx queue before configuring it */
	iwl4965_tx_queue_stop_scheduler(priv, txq_id);

	/* Map receiver-address / traffic-ID to this queue */
	iwl4965_tx_queue_set_q2ratid(priv, ra_tid, txq_id);

	/* Set this queue as a chain-building queue */
	iwl_set_bits_prph(priv, IWL49_SCD_QUEUECHAIN_SEL, (1 << txq_id));

	/* Place first TFD at index corresponding to start sequence number.
	 * Assumes that ssn_idx is valid (!= 0xFFF) */
	priv->txq[txq_id].q.read_ptr = (ssn_idx & 0xff);
	priv->txq[txq_id].q.write_ptr = (ssn_idx & 0xff);
	iwl4965_set_wr_ptrs(priv, txq_id, ssn_idx);

	/* Set up Tx window size and frame limit for this queue */
	iwl_write_targ_mem(priv,
		priv->scd_base_addr + IWL49_SCD_CONTEXT_QUEUE_OFFSET(txq_id),
		(SCD_WIN_SIZE << IWL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_POS) &
		IWL49_SCD_QUEUE_CTX_REG1_WIN_SIZE_MSK);

	iwl_write_targ_mem(priv, priv->scd_base_addr +
		IWL49_SCD_CONTEXT_QUEUE_OFFSET(txq_id) + sizeof(u32),
		(SCD_FRAME_LIMIT << IWL49_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS)
		& IWL49_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK);

	iwl_set_bits_prph(priv, IWL49_SCD_INTERRUPT_MASK, (1 << txq_id));

	/* Set up Status area in SRAM, map to Tx DMA/FIFO, activate the queue */
	iwl4965_tx_queue_set_status(priv, &priv->txq[txq_id], tx_fifo, 1);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}


static u16 iwl4965_get_hcmd_size(u8 cmd_id, u16 len)
{
	switch (cmd_id) {
	case REPLY_RXON:
		return (u16) sizeof(struct iwl4965_rxon_cmd);
	default:
		return len;
	}
}

static u16 iwl4965_build_addsta_hcmd(const struct iwl_addsta_cmd *cmd, u8 *data)
{
	struct iwl4965_addsta_cmd *addsta = (struct iwl4965_addsta_cmd *)data;
	addsta->mode = cmd->mode;
	memcpy(&addsta->sta, &cmd->sta, sizeof(struct sta_id_modify));
	memcpy(&addsta->key, &cmd->key, sizeof(struct iwl4965_keyinfo));
	addsta->station_flags = cmd->station_flags;
	addsta->station_flags_msk = cmd->station_flags_msk;
	addsta->tid_disable_tx = cmd->tid_disable_tx;
	addsta->add_immediate_ba_tid = cmd->add_immediate_ba_tid;
	addsta->remove_immediate_ba_tid = cmd->remove_immediate_ba_tid;
	addsta->add_immediate_ba_ssn = cmd->add_immediate_ba_ssn;
	addsta->reserved1 = cpu_to_le16(0);
	addsta->reserved2 = cpu_to_le32(0);

	return (u16)sizeof(struct iwl4965_addsta_cmd);
}

static inline u32 iwl4965_get_scd_ssn(struct iwl4965_tx_resp *tx_resp)
{
	return le32_to_cpup(&tx_resp->u.status + tx_resp->frame_count) & MAX_SN;
}

/**
 * iwl4965_tx_status_reply_tx - Handle Tx response for frames in aggregation queue
 */
static int iwl4965_tx_status_reply_tx(struct iwl_priv *priv,
				      struct iwl_ht_agg *agg,
				      struct iwl4965_tx_resp *tx_resp,
				      int txq_id, u16 start_idx)
{
	u16 status;
	struct agg_tx_status *frame_status = tx_resp->u.agg_status;
	struct ieee80211_tx_info *info = NULL;
	struct ieee80211_hdr *hdr = NULL;
	u32 rate_n_flags = le32_to_cpu(tx_resp->rate_n_flags);
	int i, sh, idx;
	u16 seq;
	if (agg->wait_for_ba)
		IWL_DEBUG_TX_REPLY(priv, "got tx response w/o block-ack\n");

	agg->frame_count = tx_resp->frame_count;
	agg->start_idx = start_idx;
	agg->rate_n_flags = rate_n_flags;
	agg->bitmap = 0;

	/* num frames attempted by Tx command */
	if (agg->frame_count == 1) {
		/* Only one frame was attempted; no block-ack will arrive */
		status = le16_to_cpu(frame_status[0].status);
		idx = start_idx;

		/* FIXME: code repetition */
		IWL_DEBUG_TX_REPLY(priv, "FrameCnt = %d, StartIdx=%d idx=%d\n",
				   agg->frame_count, agg->start_idx, idx);

		info = IEEE80211_SKB_CB(priv->txq[txq_id].txb[idx].skb[0]);
		info->status.rates[0].count = tx_resp->failure_frame + 1;
		info->flags &= ~IEEE80211_TX_CTL_AMPDU;
		info->flags |= iwl_is_tx_success(status) ?
			IEEE80211_TX_STAT_ACK : 0;
		iwl_hwrate_to_tx_control(priv, rate_n_flags, info);
		/* FIXME: code repetition end */

		IWL_DEBUG_TX_REPLY(priv, "1 Frame 0x%x failure :%d\n",
				    status & 0xff, tx_resp->failure_frame);
		IWL_DEBUG_TX_REPLY(priv, "Rate Info rate_n_flags=%x\n", rate_n_flags);

		agg->wait_for_ba = 0;
	} else {
		/* Two or more frames were attempted; expect block-ack */
		u64 bitmap = 0;
		int start = agg->start_idx;

		/* Construct bit-map of pending frames within Tx window */
		for (i = 0; i < agg->frame_count; i++) {
			u16 sc;
			status = le16_to_cpu(frame_status[i].status);
			seq  = le16_to_cpu(frame_status[i].sequence);
			idx = SEQ_TO_INDEX(seq);
			txq_id = SEQ_TO_QUEUE(seq);

			if (status & (AGG_TX_STATE_FEW_BYTES_MSK |
				      AGG_TX_STATE_ABORT_MSK))
				continue;

			IWL_DEBUG_TX_REPLY(priv, "FrameCnt = %d, txq_id=%d idx=%d\n",
					   agg->frame_count, txq_id, idx);

			hdr = iwl_tx_queue_get_hdr(priv, txq_id, idx);
			if (!hdr) {
				IWL_ERR(priv,
					"BUG_ON idx doesn't point to valid skb"
					" idx=%d, txq_id=%d\n", idx, txq_id);
				return -1;
			}

			sc = le16_to_cpu(hdr->seq_ctrl);
			if (idx != (SEQ_TO_SN(sc) & 0xff)) {
				IWL_ERR(priv,
					"BUG_ON idx doesn't match seq control"
					" idx=%d, seq_idx=%d, seq=%d\n",
					idx, SEQ_TO_SN(sc), hdr->seq_ctrl);
				return -1;
			}

			IWL_DEBUG_TX_REPLY(priv, "AGG Frame i=%d idx %d seq=%d\n",
					   i, idx, SEQ_TO_SN(sc));

			sh = idx - start;
			if (sh > 64) {
				sh = (start - idx) + 0xff;
				bitmap = bitmap << sh;
				sh = 0;
				start = idx;
			} else if (sh < -64)
				sh  = 0xff - (start - idx);
			else if (sh < 0) {
				sh = start - idx;
				start = idx;
				bitmap = bitmap << sh;
				sh = 0;
			}
			bitmap |= 1ULL << sh;
			IWL_DEBUG_TX_REPLY(priv, "start=%d bitmap=0x%llx\n",
					   start, (unsigned long long)bitmap);
		}

		agg->bitmap = bitmap;
		agg->start_idx = start;
		IWL_DEBUG_TX_REPLY(priv, "Frames %d start_idx=%d bitmap=0x%llx\n",
				   agg->frame_count, agg->start_idx,
				   (unsigned long long)agg->bitmap);

		if (bitmap)
			agg->wait_for_ba = 1;
	}
	return 0;
}

/**
 * iwl4965_rx_reply_tx - Handle standard (non-aggregation) Tx response
 */
static void iwl4965_rx_reply_tx(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	int txq_id = SEQ_TO_QUEUE(sequence);
	int index = SEQ_TO_INDEX(sequence);
	struct iwl_tx_queue *txq = &priv->txq[txq_id];
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *info;
	struct iwl4965_tx_resp *tx_resp = (void *)&pkt->u.raw[0];
	u32  status = le32_to_cpu(tx_resp->u.status);
	int tid = MAX_TID_COUNT;
	int sta_id;
	int freed;
	u8 *qc = NULL;

	if ((index >= txq->q.n_bd) || (iwl_queue_used(&txq->q, index) == 0)) {
		IWL_ERR(priv, "Read index for DMA queue txq_id (%d) index %d "
			  "is out of range [0-%d] %d %d\n", txq_id,
			  index, txq->q.n_bd, txq->q.write_ptr,
			  txq->q.read_ptr);
		return;
	}

	info = IEEE80211_SKB_CB(txq->txb[txq->q.read_ptr].skb[0]);
	memset(&info->status, 0, sizeof(info->status));

	hdr = iwl_tx_queue_get_hdr(priv, txq_id, index);
	if (ieee80211_is_data_qos(hdr->frame_control)) {
		qc = ieee80211_get_qos_ctl(hdr);
		tid = qc[0] & 0xf;
	}

	sta_id = iwl_get_ra_sta_id(priv, hdr);
	if (txq->sched_retry && unlikely(sta_id == IWL_INVALID_STATION)) {
		IWL_ERR(priv, "Station not known\n");
		return;
	}

	if (txq->sched_retry) {
		const u32 scd_ssn = iwl4965_get_scd_ssn(tx_resp);
		struct iwl_ht_agg *agg = NULL;

		WARN_ON(!qc);

		agg = &priv->stations[sta_id].tid[tid].agg;

		iwl4965_tx_status_reply_tx(priv, agg, tx_resp, txq_id, index);

		/* check if BAR is needed */
		if ((tx_resp->frame_count == 1) && !iwl_is_tx_success(status))
			info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;

		if (txq->q.read_ptr != (scd_ssn & 0xff)) {
			index = iwl_queue_dec_wrap(scd_ssn & 0xff, txq->q.n_bd);
			IWL_DEBUG_TX_REPLY(priv, "Retry scheduler reclaim scd_ssn "
					   "%d index %d\n", scd_ssn , index);
			freed = iwl_tx_queue_reclaim(priv, txq_id, index);
			priv->stations[sta_id].tid[tid].tfds_in_queue -= freed;

			if (priv->mac80211_registered &&
			    (iwl_queue_space(&txq->q) > txq->q.low_mark) &&
			    (agg->state != IWL_EMPTYING_HW_QUEUE_DELBA)) {
				if (agg->state == IWL_AGG_OFF)
					iwl_wake_queue(priv, txq_id);
				else
					iwl_wake_queue(priv, txq->swq_id);
			}
		}
	} else {
		info->status.rates[0].count = tx_resp->failure_frame + 1;
		info->flags |= iwl_is_tx_success(status) ?
					IEEE80211_TX_STAT_ACK : 0;
		iwl_hwrate_to_tx_control(priv,
					le32_to_cpu(tx_resp->rate_n_flags),
					info);

		IWL_DEBUG_TX_REPLY(priv, "TXQ %d status %s (0x%08x) "
				   "rate_n_flags 0x%x retries %d\n",
				   txq_id,
				   iwl_get_tx_fail_reason(status), status,
				   le32_to_cpu(tx_resp->rate_n_flags),
				   tx_resp->failure_frame);

		freed = iwl_tx_queue_reclaim(priv, txq_id, index);
		if (qc && likely(sta_id != IWL_INVALID_STATION))
			priv->stations[sta_id].tid[tid].tfds_in_queue -= freed;

		if (priv->mac80211_registered &&
		    (iwl_queue_space(&txq->q) > txq->q.low_mark))
			iwl_wake_queue(priv, txq_id);
	}

	if (qc && likely(sta_id != IWL_INVALID_STATION))
		iwl_txq_check_empty(priv, sta_id, tid, txq_id);

	if (iwl_check_bits(status, TX_ABORT_REQUIRED_MSK))
		IWL_ERR(priv, "TODO:  Implement Tx ABORT REQUIRED!!!\n");
}

static int iwl4965_calc_rssi(struct iwl_priv *priv,
			     struct iwl_rx_phy_res *rx_resp)
{
	/* data from PHY/DSP regarding signal strength, etc.,
	 *   contents are always there, not configurable by host.  */
	struct iwl4965_rx_non_cfg_phy *ncphy =
	    (struct iwl4965_rx_non_cfg_phy *)rx_resp->non_cfg_phy_buf;
	u32 agc = (le16_to_cpu(ncphy->agc_info) & IWL49_AGC_DB_MASK)
			>> IWL49_AGC_DB_POS;

	u32 valid_antennae =
	    (le16_to_cpu(rx_resp->phy_flags) & IWL49_RX_PHY_FLAGS_ANTENNAE_MASK)
			>> IWL49_RX_PHY_FLAGS_ANTENNAE_OFFSET;
	u8 max_rssi = 0;
	u32 i;

	/* Find max rssi among 3 possible receivers.
	 * These values are measured by the digital signal processor (DSP).
	 * They should stay fairly constant even as the signal strength varies,
	 *   if the radio's automatic gain control (AGC) is working right.
	 * AGC value (see below) will provide the "interesting" info. */
	for (i = 0; i < 3; i++)
		if (valid_antennae & (1 << i))
			max_rssi = max(ncphy->rssi_info[i << 1], max_rssi);

	IWL_DEBUG_STATS(priv, "Rssi In A %d B %d C %d Max %d AGC dB %d\n",
		ncphy->rssi_info[0], ncphy->rssi_info[2], ncphy->rssi_info[4],
		max_rssi, agc);

	/* dBm = max_rssi dB - agc dB - constant.
	 * Higher AGC (higher radio gain) means lower signal. */
	return max_rssi - agc - IWL49_RSSI_OFFSET;
}


/* Set up 4965-specific Rx frame reply handlers */
static void iwl4965_rx_handler_setup(struct iwl_priv *priv)
{
	/* Legacy Rx frames */
	priv->rx_handlers[REPLY_RX] = iwl_rx_reply_rx;
	/* Tx response */
	priv->rx_handlers[REPLY_TX] = iwl4965_rx_reply_tx;
}

static void iwl4965_setup_deferred_work(struct iwl_priv *priv)
{
	INIT_WORK(&priv->txpower_work, iwl4965_bg_txpower_work);
}

static void iwl4965_cancel_deferred_work(struct iwl_priv *priv)
{
	cancel_work_sync(&priv->txpower_work);
}

#define IWL4965_UCODE_GET(item)						\
static u32 iwl4965_ucode_get_##item(const struct iwl_ucode_header *ucode,\
				    u32 api_ver)			\
{									\
	return le32_to_cpu(ucode->u.v1.item);				\
}

static u32 iwl4965_ucode_get_header_size(u32 api_ver)
{
	return UCODE_HEADER_SIZE(1);
}
static u32 iwl4965_ucode_get_build(const struct iwl_ucode_header *ucode,
				   u32 api_ver)
{
	return 0;
}
static u8 *iwl4965_ucode_get_data(const struct iwl_ucode_header *ucode,
				  u32 api_ver)
{
	return (u8 *) ucode->u.v1.data;
}

IWL4965_UCODE_GET(inst_size);
IWL4965_UCODE_GET(data_size);
IWL4965_UCODE_GET(init_size);
IWL4965_UCODE_GET(init_data_size);
IWL4965_UCODE_GET(boot_size);

static struct iwl_hcmd_ops iwl4965_hcmd = {
	.rxon_assoc = iwl4965_send_rxon_assoc,
	.commit_rxon = iwl_commit_rxon,
	.set_rxon_chain = iwl_set_rxon_chain,
};

static struct iwl_ucode_ops iwl4965_ucode = {
	.get_header_size = iwl4965_ucode_get_header_size,
	.get_build = iwl4965_ucode_get_build,
	.get_inst_size = iwl4965_ucode_get_inst_size,
	.get_data_size = iwl4965_ucode_get_data_size,
	.get_init_size = iwl4965_ucode_get_init_size,
	.get_init_data_size = iwl4965_ucode_get_init_data_size,
	.get_boot_size = iwl4965_ucode_get_boot_size,
	.get_data = iwl4965_ucode_get_data,
};
static struct iwl_hcmd_utils_ops iwl4965_hcmd_utils = {
	.get_hcmd_size = iwl4965_get_hcmd_size,
	.build_addsta_hcmd = iwl4965_build_addsta_hcmd,
	.chain_noise_reset = iwl4965_chain_noise_reset,
	.gain_computation = iwl4965_gain_computation,
	.rts_tx_cmd_flag = iwlcore_rts_tx_cmd_flag,
	.calc_rssi = iwl4965_calc_rssi,
};

static struct iwl_lib_ops iwl4965_lib = {
	.set_hw_params = iwl4965_hw_set_hw_params,
	.txq_update_byte_cnt_tbl = iwl4965_txq_update_byte_cnt_tbl,
	.txq_set_sched = iwl4965_txq_set_sched,
	.txq_agg_enable = iwl4965_txq_agg_enable,
	.txq_agg_disable = iwl4965_txq_agg_disable,
	.txq_attach_buf_to_tfd = iwl_hw_txq_attach_buf_to_tfd,
	.txq_free_tfd = iwl_hw_txq_free_tfd,
	.txq_init = iwl_hw_tx_queue_init,
	.rx_handler_setup = iwl4965_rx_handler_setup,
	.setup_deferred_work = iwl4965_setup_deferred_work,
	.cancel_deferred_work = iwl4965_cancel_deferred_work,
	.is_valid_rtc_data_addr = iwl4965_hw_valid_rtc_data_addr,
	.alive_notify = iwl4965_alive_notify,
	.init_alive_start = iwl4965_init_alive_start,
	.load_ucode = iwl4965_load_bsm,
	.dump_nic_event_log = iwl_dump_nic_event_log,
	.dump_nic_error_log = iwl_dump_nic_error_log,
	.set_channel_switch = iwl4965_hw_channel_switch,
	.apm_ops = {
		.init = iwl_apm_init,
		.stop = iwl_apm_stop,
		.config = iwl4965_nic_config,
		.set_pwr_src = iwl_set_pwr_src,
	},
	.eeprom_ops = {
		.regulatory_bands = {
			EEPROM_REGULATORY_BAND_1_CHANNELS,
			EEPROM_REGULATORY_BAND_2_CHANNELS,
			EEPROM_REGULATORY_BAND_3_CHANNELS,
			EEPROM_REGULATORY_BAND_4_CHANNELS,
			EEPROM_REGULATORY_BAND_5_CHANNELS,
			EEPROM_4965_REGULATORY_BAND_24_HT40_CHANNELS,
			EEPROM_4965_REGULATORY_BAND_52_HT40_CHANNELS
		},
		.verify_signature  = iwlcore_eeprom_verify_signature,
		.acquire_semaphore = iwlcore_eeprom_acquire_semaphore,
		.release_semaphore = iwlcore_eeprom_release_semaphore,
		.calib_version = iwl4965_eeprom_calib_version,
		.query_addr = iwlcore_eeprom_query_addr,
	},
	.send_tx_power	= iwl4965_send_tx_power,
	.update_chain_flags = iwl_update_chain_flags,
	.post_associate = iwl_post_associate,
	.config_ap = iwl_config_ap,
	.isr = iwl_isr_legacy,
	.temp_ops = {
		.temperature = iwl4965_temperature_calib,
		.set_ct_kill = iwl4965_set_ct_threshold,
	},
};

static struct iwl_ops iwl4965_ops = {
	.ucode = &iwl4965_ucode,
	.lib = &iwl4965_lib,
	.hcmd = &iwl4965_hcmd,
	.utils = &iwl4965_hcmd_utils,
	.led = &iwlagn_led_ops,
};

struct iwl_cfg iwl4965_agn_cfg = {
	.name = "4965AGN",
	.fw_name_pre = IWL4965_FW_PRE,
	.ucode_api_max = IWL4965_UCODE_API_MAX,
	.ucode_api_min = IWL4965_UCODE_API_MIN,
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.eeprom_size = IWL4965_EEPROM_IMG_SIZE,
	.eeprom_ver = EEPROM_4965_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_4965_TX_POWER_VERSION,
	.ops = &iwl4965_ops,
	.num_of_queues = IWL49_NUM_QUEUES,
	.num_of_ampdu_queues = IWL49_NUM_AMPDU_QUEUES,
	.mod_params = &iwl4965_mod_params,
	.valid_tx_ant = ANT_AB,
	.valid_rx_ant = ANT_AB,
	.pll_cfg_val = 0,
	.set_l0s = true,
	.use_bsm = true,
	.use_isr_legacy = true,
	.ht_greenfield_support = false,
	.broken_powersave = true,
	.led_compensation = 61,
	.chain_noise_num_beacons = IWL4965_CAL_NUM_BEACONS,
};

/* Module firmware */
MODULE_FIRMWARE(IWL4965_MODULE_FIRMWARE(IWL4965_UCODE_API_MAX));

module_param_named(antenna, iwl4965_mod_params.antenna, int, S_IRUGO);
MODULE_PARM_DESC(antenna, "select antenna (1=Main, 2=Aux, default 0 [both])");
module_param_named(swcrypto, iwl4965_mod_params.sw_crypto, int, S_IRUGO);
MODULE_PARM_DESC(swcrypto, "using crypto in software (default 0 [hardware])");
module_param_named(
	disable_hw_scan, iwl4965_mod_params.disable_hw_scan, int, S_IRUGO);
MODULE_PARM_DESC(disable_hw_scan, "disable hardware scanning (default 0)");

module_param_named(queues_num, iwl4965_mod_params.num_of_queues, int, S_IRUGO);
MODULE_PARM_DESC(queues_num, "number of hw queues.");
/* 11n */
module_param_named(11n_disable, iwl4965_mod_params.disable_11n, int, S_IRUGO);
MODULE_PARM_DESC(11n_disable, "disable 11n functionality");
module_param_named(amsdu_size_8K, iwl4965_mod_params.amsdu_size_8K,
		   int, S_IRUGO);
MODULE_PARM_DESC(amsdu_size_8K, "enable 8K amsdu size");

module_param_named(fw_restart4965, iwl4965_mod_params.restart_fw, int, S_IRUGO);
MODULE_PARM_DESC(fw_restart4965, "restart firmware in case of error");
