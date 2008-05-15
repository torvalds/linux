/******************************************************************************
 *
 * Copyright(c) 2003 - 2008 Intel Corporation. All rights reserved.
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
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
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

/* module parameters */
static struct iwl_mod_params iwl4965_mod_params = {
	.num_of_queues = IWL49_NUM_QUEUES,
	.enable_qos = 1,
	.amsdu_size_8K = 1,
	.restart_fw = 1,
	/* the rest are 0 by default */
};

#ifdef CONFIG_IWL4965_HT

static const u16 default_tid_to_tx_fifo[] = {
	IWL_TX_FIFO_AC1,
	IWL_TX_FIFO_AC0,
	IWL_TX_FIFO_AC0,
	IWL_TX_FIFO_AC1,
	IWL_TX_FIFO_AC2,
	IWL_TX_FIFO_AC2,
	IWL_TX_FIFO_AC3,
	IWL_TX_FIFO_AC3,
	IWL_TX_FIFO_NONE,
	IWL_TX_FIFO_NONE,
	IWL_TX_FIFO_NONE,
	IWL_TX_FIFO_NONE,
	IWL_TX_FIFO_NONE,
	IWL_TX_FIFO_NONE,
	IWL_TX_FIFO_NONE,
	IWL_TX_FIFO_NONE,
	IWL_TX_FIFO_AC3
};

#endif	/*CONFIG_IWL4965_HT */

/* check contents of special bootstrap uCode SRAM */
static int iwl4965_verify_bsm(struct iwl_priv *priv)
{
	__le32 *image = priv->ucode_boot.v_addr;
	u32 len = priv->ucode_boot.len;
	u32 reg;
	u32 val;

	IWL_DEBUG_INFO("Begin verify bsm\n");

	/* verify BSM SRAM contents */
	val = iwl_read_prph(priv, BSM_WR_DWCOUNT_REG);
	for (reg = BSM_SRAM_LOWER_BOUND;
	     reg < BSM_SRAM_LOWER_BOUND + len;
	     reg += sizeof(u32), image++) {
		val = iwl_read_prph(priv, reg);
		if (val != le32_to_cpu(*image)) {
			IWL_ERROR("BSM uCode verification failed at "
				  "addr 0x%08X+%u (of %u), is 0x%x, s/b 0x%x\n",
				  BSM_SRAM_LOWER_BOUND,
				  reg - BSM_SRAM_LOWER_BOUND, len,
				  val, le32_to_cpu(*image));
			return -EIO;
		}
	}

	IWL_DEBUG_INFO("BSM bootstrap uCode image OK\n");

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

	IWL_DEBUG_INFO("Begin load bsm\n");

	/* make sure bootstrap program is no larger than BSM's SRAM size */
	if (len > IWL_MAX_BSM_SIZE)
		return -EINVAL;

	/* Tell bootstrap uCode where to find the "Initialize" uCode
	 *   in host DRAM ... host DRAM physical address bits 35:4 for 4965.
	 * NOTE:  iwl4965_initialize_alive_start() will replace these values,
	 *        after the "initialize" uCode has run, to point to
	 *        runtime/protocol instructions and backup data cache. */
	pinst = priv->ucode_init.p_addr >> 4;
	pdata = priv->ucode_init_data.p_addr >> 4;
	inst_len = priv->ucode_init.len;
	data_len = priv->ucode_init_data.len;

	ret = iwl_grab_nic_access(priv);
	if (ret)
		return ret;

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
	if (ret) {
		iwl_release_nic_access(priv);
		return ret;
	}

	/* Tell BSM to copy from BSM SRAM into instruction SRAM, when asked */
	iwl_write_prph(priv, BSM_WR_MEM_SRC_REG, 0x0);
	iwl_write_prph(priv, BSM_WR_MEM_DST_REG, RTC_INST_LOWER_BOUND);
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
		IWL_DEBUG_INFO("BSM write complete, poll %d iterations\n", i);
	else {
		IWL_ERROR("BSM write did not complete!\n");
		return -EIO;
	}

	/* Enable future boot loads whenever power management unit triggers it
	 *   (e.g. when powering back up after power-save shutdown) */
	iwl_write_prph(priv, BSM_WR_CTRL_REG, BSM_WR_CTRL_REG_BIT_START_EN);

	iwl_release_nic_access(priv);

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
	unsigned long flags;
	int ret = 0;

	/* bits 35:4 for 4965 */
	pinst = priv->ucode_code.p_addr >> 4;
	pdata = priv->ucode_data_backup.p_addr >> 4;

	spin_lock_irqsave(&priv->lock, flags);
	ret = iwl_grab_nic_access(priv);
	if (ret) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return ret;
	}

	/* Tell bootstrap uCode where to find image to load */
	iwl_write_prph(priv, BSM_DRAM_INST_PTR_REG, pinst);
	iwl_write_prph(priv, BSM_DRAM_DATA_PTR_REG, pdata);
	iwl_write_prph(priv, BSM_DRAM_DATA_BYTECOUNT_REG,
				 priv->ucode_data.len);

	/* Inst bytecount must be last to set up, bit 31 signals uCode
	 *   that all new ptr/size info is in place */
	iwl_write_prph(priv, BSM_DRAM_INST_BYTECOUNT_REG,
				 priv->ucode_code.len | BSM_DRAM_INST_LOAD);
	iwl_release_nic_access(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

	IWL_DEBUG_INFO("Runtime uCode pointers are set.\n");

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
		IWL_DEBUG_INFO("Initialize Alive failed.\n");
		goto restart;
	}

	/* Bootstrap uCode has loaded initialize uCode ... verify inst image.
	 * This is a paranoid check, because we would not have gotten the
	 * "initialize" alive if code weren't properly loaded.  */
	if (iwl_verify_ucode(priv)) {
		/* Runtime instruction load was bad;
		 * take it all the way back down so we can try again */
		IWL_DEBUG_INFO("Bad \"initialize\" uCode load.\n");
		goto restart;
	}

	/* Calculate temperature */
	priv->temperature = iwl4965_get_temperature(priv);

	/* Send pointers to protocol/runtime uCode image ... init code will
	 * load and launch runtime uCode, which will send us another "Alive"
	 * notification. */
	IWL_DEBUG_INFO("Initialization Alive received.\n");
	if (iwl4965_set_ucode_ptrs(priv)) {
		/* Runtime instruction load won't happen;
		 * take it all the way back down so we can try again */
		IWL_DEBUG_INFO("Couldn't set up uCode pointers.\n");
		goto restart;
	}
	return;

restart:
	queue_work(priv->workqueue, &priv->restart);
}

static int is_fat_channel(__le32 rxon_flags)
{
	return (rxon_flags & RXON_FLG_CHANNEL_MODE_PURE_40_MSK) ||
		(rxon_flags & RXON_FLG_CHANNEL_MODE_MIXED_MSK);
}

int iwl4965_hwrate_to_plcp_idx(u32 rate_n_flags)
{
	int idx = 0;

	/* 4965 HT rate format */
	if (rate_n_flags & RATE_MCS_HT_MSK) {
		idx = (rate_n_flags & 0xff);

		if (idx >= IWL_RATE_MIMO2_6M_PLCP)
			idx = idx - IWL_RATE_MIMO2_6M_PLCP;

		idx += IWL_FIRST_OFDM_RATE;
		/* skip 9M not supported in ht*/
		if (idx >= IWL_RATE_9M_INDEX)
			idx += 1;
		if ((idx >= IWL_FIRST_OFDM_RATE) && (idx <= IWL_LAST_OFDM_RATE))
			return idx;

	/* 4965 legacy rate format, search for match in table */
	} else {
		for (idx = 0; idx < ARRAY_SIZE(iwl4965_rates); idx++)
			if (iwl4965_rates[idx].plcp == (rate_n_flags & 0xFF))
				return idx;
	}

	return -1;
}

/**
 * translate ucode response to mac80211 tx status control values
 */
void iwl4965_hwrate_to_tx_control(struct iwl_priv *priv, u32 rate_n_flags,
				  struct ieee80211_tx_control *control)
{
	int rate_index;

	control->antenna_sel_tx =
		((rate_n_flags & RATE_MCS_ANT_ABC_MSK) >> RATE_MCS_ANT_POS);
	if (rate_n_flags & RATE_MCS_HT_MSK)
		control->flags |= IEEE80211_TXCTL_OFDM_HT;
	if (rate_n_flags & RATE_MCS_GF_MSK)
		control->flags |= IEEE80211_TXCTL_GREEN_FIELD;
	if (rate_n_flags & RATE_MCS_FAT_MSK)
		control->flags |= IEEE80211_TXCTL_40_MHZ_WIDTH;
	if (rate_n_flags & RATE_MCS_DUP_MSK)
		control->flags |= IEEE80211_TXCTL_DUP_DATA;
	if (rate_n_flags & RATE_MCS_SGI_MSK)
		control->flags |= IEEE80211_TXCTL_SHORT_GI;
	/* since iwl4965_hwrate_to_plcp_idx is band indifferent, we always use
	 * IEEE80211_BAND_2GHZ band as it contains all the rates */
	rate_index = iwl4965_hwrate_to_plcp_idx(rate_n_flags);
	if (rate_index == -1)
		control->tx_rate = NULL;
	else
		control->tx_rate =
			&priv->bands[IEEE80211_BAND_2GHZ].bitrates[rate_index];
}

int iwl4965_hw_rxq_stop(struct iwl_priv *priv)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	rc = iwl_grab_nic_access(priv);
	if (rc) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return rc;
	}

	/* stop Rx DMA */
	iwl_write_direct32(priv, FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);
	rc = iwl_poll_direct_bit(priv, FH_MEM_RSSR_RX_STATUS_REG,
				     (1 << 24), 1000);
	if (rc < 0)
		IWL_ERROR("Can't stop Rx DMA.\n");

	iwl_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

/*
 * EEPROM handlers
 */

static int iwl4965_eeprom_check_version(struct iwl_priv *priv)
{
	u16 eeprom_ver;
	u16 calib_ver;

	eeprom_ver = iwl_eeprom_query16(priv, EEPROM_VERSION);

	calib_ver = iwl_eeprom_query16(priv, EEPROM_4965_CALIB_VERSION_OFFSET);

	if (eeprom_ver < EEPROM_4965_EEPROM_VERSION ||
	    calib_ver < EEPROM_4965_TX_POWER_VERSION)
		goto err;

	return 0;
err:
	IWL_ERROR("Unsuported EEPROM VER=0x%x < 0x%x CALIB=0x%x < 0x%x\n",
		  eeprom_ver, EEPROM_4965_EEPROM_VERSION,
		  calib_ver, EEPROM_4965_TX_POWER_VERSION);
	return -EINVAL;

}
int iwl4965_set_pwr_src(struct iwl_priv *priv, enum iwl_pwr_src src)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	ret = iwl_grab_nic_access(priv);
	if (ret) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return ret;
	}

	if (src == IWL_PWR_SRC_VAUX) {
		u32 val;
		ret = pci_read_config_dword(priv->pci_dev, PCI_POWER_SOURCE,
					    &val);

		if (val & PCI_CFG_PMC_PME_FROM_D3COLD_SUPPORT) {
			iwl_set_bits_mask_prph(priv, APMG_PS_CTRL_REG,
					       APMG_PS_CTRL_VAL_PWR_SRC_VAUX,
					       ~APMG_PS_CTRL_MSK_PWR_SRC);
		}
	} else {
		iwl_set_bits_mask_prph(priv, APMG_PS_CTRL_REG,
				       APMG_PS_CTRL_VAL_PWR_SRC_VMAIN,
				       ~APMG_PS_CTRL_MSK_PWR_SRC);
	}

	iwl_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

static int iwl4965_disable_tx_fifo(struct iwl_priv *priv)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&priv->lock, flags);

	ret = iwl_grab_nic_access(priv);
	if (unlikely(ret)) {
		IWL_ERROR("Tx fifo reset failed");
		spin_unlock_irqrestore(&priv->lock, flags);
		return ret;
	}

	iwl_write_prph(priv, IWL49_SCD_TXFACT, 0);
	iwl_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int iwl4965_apm_init(struct iwl_priv *priv)
{
	int ret = 0;

	iwl_set_bit(priv, CSR_GIO_CHICKEN_BITS,
			  CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER);

	/* set "initialization complete" bit to move adapter
	 * D0U* --> D0A* state */
	iwl_set_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/* wait for clock stabilization */
	ret = iwl_poll_bit(priv, CSR_GP_CNTRL,
			   CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			   CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);
	if (ret < 0) {
		IWL_DEBUG_INFO("Failed to init the card\n");
		goto out;
	}

	ret = iwl_grab_nic_access(priv);
	if (ret)
		goto out;

	/* enable DMA */
	iwl_write_prph(priv, APMG_CLK_CTRL_REG,
			APMG_CLK_VAL_DMA_CLK_RQT | APMG_CLK_VAL_BSM_CLK_RQT);

	udelay(20);

	iwl_set_bits_prph(priv, APMG_PCIDEV_STT_REG,
			  APMG_PCIDEV_STT_VAL_L1_ACT_DIS);

	iwl_release_nic_access(priv);
out:
	return ret;
}


static void iwl4965_nic_config(struct iwl_priv *priv)
{
	unsigned long flags;
	u32 val;
	u16 radio_cfg;
	u8 val_link;

	spin_lock_irqsave(&priv->lock, flags);

	if ((priv->rev_id & 0x80) == 0x80 && (priv->rev_id & 0x7f) < 8) {
		pci_read_config_dword(priv->pci_dev, PCI_REG_WUM8, &val);
		/* Enable No Snoop field */
		pci_write_config_dword(priv->pci_dev, PCI_REG_WUM8,
				       val & ~(1 << 11));
	}

	pci_read_config_byte(priv->pci_dev, PCI_LINK_CTRL, &val_link);

	/* disable L1 entry -- workaround for pre-B1 */
	pci_write_config_byte(priv->pci_dev, PCI_LINK_CTRL, val_link & ~0x02);

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

int iwl4965_hw_nic_stop_master(struct iwl_priv *priv)
{
	int rc = 0;
	u32 reg_val;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	/* set stop master bit */
	iwl_set_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_STOP_MASTER);

	reg_val = iwl_read32(priv, CSR_GP_CNTRL);

	if (CSR_GP_CNTRL_REG_FLAG_MAC_POWER_SAVE ==
	    (reg_val & CSR_GP_CNTRL_REG_MSK_POWER_SAVE_TYPE))
		IWL_DEBUG_INFO("Card in power save, master is already "
			       "stopped\n");
	else {
		rc = iwl_poll_bit(priv, CSR_RESET,
				  CSR_RESET_REG_FLAG_MASTER_DISABLED,
				  CSR_RESET_REG_FLAG_MASTER_DISABLED, 100);
		if (rc < 0) {
			spin_unlock_irqrestore(&priv->lock, flags);
			return rc;
		}
	}

	spin_unlock_irqrestore(&priv->lock, flags);
	IWL_DEBUG_INFO("stop master\n");

	return rc;
}

/**
 * iwl4965_hw_txq_ctx_stop - Stop all Tx DMA channels, free Tx queue memory
 */
void iwl4965_hw_txq_ctx_stop(struct iwl_priv *priv)
{

	int txq_id;
	unsigned long flags;

	/* Stop each Tx DMA channel, and wait for it to be idle */
	for (txq_id = 0; txq_id < priv->hw_params.max_txq_num; txq_id++) {
		spin_lock_irqsave(&priv->lock, flags);
		if (iwl_grab_nic_access(priv)) {
			spin_unlock_irqrestore(&priv->lock, flags);
			continue;
		}

		iwl_write_direct32(priv,
				   FH_TCSR_CHNL_TX_CONFIG_REG(txq_id), 0x0);
		iwl_poll_direct_bit(priv, FH_TSSR_TX_STATUS_REG,
				    FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE
				    (txq_id), 200);
		iwl_release_nic_access(priv);
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	/* Deallocate memory for all Tx queues */
	iwl_hw_txq_ctx_free(priv);
}

int iwl4965_hw_nic_reset(struct iwl_priv *priv)
{
	int rc = 0;
	unsigned long flags;

	iwl4965_hw_nic_stop_master(priv);

	spin_lock_irqsave(&priv->lock, flags);

	iwl_set_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);

	udelay(10);

	iwl_set_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
	rc = iwl_poll_bit(priv, CSR_RESET,
			  CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			  CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25);

	udelay(10);

	rc = iwl_grab_nic_access(priv);
	if (!rc) {
		iwl_write_prph(priv, APMG_CLK_EN_REG,
				APMG_CLK_VAL_DMA_CLK_RQT |
				APMG_CLK_VAL_BSM_CLK_RQT);

		udelay(10);

		iwl_set_bits_prph(priv, APMG_PCIDEV_STT_REG,
					APMG_PCIDEV_STT_VAL_L1_ACT_DIS);

		iwl_release_nic_access(priv);
	}

	clear_bit(STATUS_HCMD_ACTIVE, &priv->status);
	wake_up_interruptible(&priv->wait_command_queue);

	spin_unlock_irqrestore(&priv->lock, flags);

	return rc;

}

#define REG_RECALIB_PERIOD (60)

/**
 * iwl4965_bg_statistics_periodic - Timer callback to queue statistics
 *
 * This callback is provided in order to send a statistics request.
 *
 * This timer function is continually reset to execute within
 * REG_RECALIB_PERIOD seconds since the last STATISTICS_NOTIFICATION
 * was received.  We need to ensure we receive the statistics in order
 * to update the temperature used for calibrating the TXPOWER.
 */
static void iwl4965_bg_statistics_periodic(unsigned long data)
{
	struct iwl_priv *priv = (struct iwl_priv *)data;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	iwl_send_statistics_request(priv, CMD_ASYNC);
}

void iwl4965_rf_kill_ct_config(struct iwl_priv *priv)
{
	struct iwl4965_ct_kill_config cmd;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&priv->lock, flags);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT);
	spin_unlock_irqrestore(&priv->lock, flags);

	cmd.critical_temperature_R =
		cpu_to_le32(priv->hw_params.ct_kill_threshold);

	ret = iwl_send_cmd_pdu(priv, REPLY_CT_KILL_CONFIG_CMD,
			       sizeof(cmd), &cmd);
	if (ret)
		IWL_ERROR("REPLY_CT_KILL_CONFIG_CMD failed\n");
	else
		IWL_DEBUG_INFO("REPLY_CT_KILL_CONFIG_CMD succeeded, "
			"critical temperature is %d\n",
			cmd.critical_temperature_R);
}

#ifdef CONFIG_IWL4965_RUN_TIME_CALIB

/* Reset differential Rx gains in NIC to prepare for chain noise calibration.
 * Called after every association, but this runs only once!
 *  ... once chain noise is calibrated the first time, it's good forever.  */
static void iwl4965_chain_noise_reset(struct iwl_priv *priv)
{
	struct iwl_chain_noise_data *data = &(priv->chain_noise_data);

	if ((data->state == IWL_CHAIN_NOISE_ALIVE) && iwl_is_associated(priv)) {
		struct iwl4965_calibration_cmd cmd;

		memset(&cmd, 0, sizeof(cmd));
		cmd.opCode = PHY_CALIBRATE_DIFF_GAIN_CMD;
		cmd.diff_gain_a = 0;
		cmd.diff_gain_b = 0;
		cmd.diff_gain_c = 0;
		if (iwl_send_cmd_pdu(priv, REPLY_PHY_CALIBRATION_CMD,
				 sizeof(cmd), &cmd))
			IWL_ERROR("Could not send REPLY_PHY_CALIBRATION_CMD\n");
		data->state = IWL_CHAIN_NOISE_ACCUMULATE;
		IWL_DEBUG_CALIB("Run chain_noise_calibrate\n");
	}
}

static void iwl4965_gain_computation(struct iwl_priv *priv,
		u32 *average_noise,
		u16 min_average_noise_antenna_i,
		u32 min_average_noise)
{
	int i, ret;
	struct iwl_chain_noise_data *data = &priv->chain_noise_data;

	data->delta_gain_code[min_average_noise_antenna_i] = 0;

	for (i = 0; i < NUM_RX_CHAINS; i++) {
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
	IWL_DEBUG_CALIB("delta_gain_codes: a %d b %d c %d\n",
		     data->delta_gain_code[0],
		     data->delta_gain_code[1],
		     data->delta_gain_code[2]);

	/* Differential gain gets sent to uCode only once */
	if (!data->radio_write) {
		struct iwl4965_calibration_cmd cmd;
		data->radio_write = 1;

		memset(&cmd, 0, sizeof(cmd));
		cmd.opCode = PHY_CALIBRATE_DIFF_GAIN_CMD;
		cmd.diff_gain_a = data->delta_gain_code[0];
		cmd.diff_gain_b = data->delta_gain_code[1];
		cmd.diff_gain_c = data->delta_gain_code[2];
		ret = iwl_send_cmd_pdu(priv, REPLY_PHY_CALIBRATION_CMD,
				      sizeof(cmd), &cmd);
		if (ret)
			IWL_DEBUG_CALIB("fail sending cmd "
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

static void iwl4965_bg_sensitivity_work(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv,
			sensitivity_work);

	mutex_lock(&priv->mutex);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status) ||
	    test_bit(STATUS_SCANNING, &priv->status)) {
		mutex_unlock(&priv->mutex);
		return;
	}

	if (priv->start_calib) {
		iwl_chain_noise_calibration(priv, &priv->statistics);

		iwl_sensitivity_calibration(priv, &priv->statistics);
	}

	mutex_unlock(&priv->mutex);
	return;
}
#endif /*CONFIG_IWL4965_RUN_TIME_CALIB*/

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

	/* Regardless of if we are assocaited, we must reconfigure the
	 * TX power since frames can be sent on non-radar channels while
	 * not associated */
	iwl4965_hw_reg_send_txpower(priv);

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
	int active = test_bit(txq_id, &priv->txq_ctx_active_msk)?1:0;

	/* Set up and activate */
	iwl_write_prph(priv, IWL49_SCD_QUEUE_STATUS_BITS(txq_id),
			 (active << IWL49_SCD_QUEUE_STTS_REG_POS_ACTIVE) |
			 (tx_fifo_id << IWL49_SCD_QUEUE_STTS_REG_POS_TXF) |
			 (scd_retry << IWL49_SCD_QUEUE_STTS_REG_POS_WSL) |
			 (scd_retry << IWL49_SCD_QUEUE_STTS_REG_POS_SCD_ACK) |
			 IWL49_SCD_QUEUE_STTS_REG_MSK);

	txq->sched_retry = scd_retry;

	IWL_DEBUG_INFO("%s %s Queue %d on AC %d\n",
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

static inline void iwl4965_txq_ctx_activate(struct iwl_priv *priv, int txq_id)
{
	set_bit(txq_id, &priv->txq_ctx_active_msk);
}

static inline void iwl4965_txq_ctx_deactivate(struct iwl_priv *priv, int txq_id)
{
	clear_bit(txq_id, &priv->txq_ctx_active_msk);
}

int iwl4965_alive_notify(struct iwl_priv *priv)
{
	u32 a;
	int i = 0;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&priv->lock, flags);

#ifdef CONFIG_IWL4965_RUN_TIME_CALIB
	memset(&(priv->sensitivity_data), 0,
	       sizeof(struct iwl_sensitivity_data));
	memset(&(priv->chain_noise_data), 0,
	       sizeof(struct iwl_chain_noise_data));
	for (i = 0; i < NUM_RX_CHAINS; i++)
		priv->chain_noise_data.delta_gain_code[i] =
				CHAIN_NOISE_DELTA_GAIN_INIT_VAL;
#endif /* CONFIG_IWL4965_RUN_TIME_CALIB*/
	ret = iwl_grab_nic_access(priv);
	if (ret) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return ret;
	}

	/* Clear 4965's internal Tx Scheduler data base */
	priv->scd_base_addr = iwl_read_prph(priv, IWL49_SCD_SRAM_BASE_ADDR);
	a = priv->scd_base_addr + IWL49_SCD_CONTEXT_DATA_OFFSET;
	for (; a < priv->scd_base_addr + IWL49_SCD_TX_STTS_BITMAP_OFFSET; a += 4)
		iwl_write_targ_mem(priv, a, 0);
	for (; a < priv->scd_base_addr + IWL49_SCD_TRANSLATE_TBL_OFFSET; a += 4)
		iwl_write_targ_mem(priv, a, 0);
	for (; a < sizeof(u16) * priv->hw_params.max_txq_num; a += 4)
		iwl_write_targ_mem(priv, a, 0);

	/* Tel 4965 where to find Tx byte count tables */
	iwl_write_prph(priv, IWL49_SCD_DRAM_BASE_ADDR,
		(priv->shared_phys +
		 offsetof(struct iwl4965_shared, queues_byte_cnt_tbls)) >> 10);

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
	iwl_write_prph(priv, IWL49_SCD_TXFACT,
				 SCD_TXFACT_REG_TXFIFO_MASK(0, 7));

	iwl4965_set_wr_ptrs(priv, IWL_CMD_QUEUE_NUM, 0);

	/* Map each Tx/cmd queue to its corresponding fifo */
	for (i = 0; i < ARRAY_SIZE(default_queue_to_tx_fifo); i++) {
		int ac = default_queue_to_tx_fifo[i];
		iwl4965_txq_ctx_activate(priv, i);
		iwl4965_tx_queue_set_status(priv, &priv->txq[i], ac, 0);
	}

	iwl_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	/* Ask for statistics now, the uCode will send statistics notification
	 * periodically after association */
	iwl_send_statistics_request(priv, CMD_ASYNC);
	return ret;
}

#ifdef CONFIG_IWL4965_RUN_TIME_CALIB
static struct iwl_sensitivity_ranges iwl4965_sensitivity = {
	.min_nrg_cck = 97,
	.max_nrg_cck = 0,

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
};
#endif

/**
 * iwl4965_hw_set_hw_params
 *
 * Called when initializing driver
 */
int iwl4965_hw_set_hw_params(struct iwl_priv *priv)
{

	if ((priv->cfg->mod_params->num_of_queues > IWL49_NUM_QUEUES) ||
	    (priv->cfg->mod_params->num_of_queues < IWL_MIN_NUM_QUEUES)) {
		IWL_ERROR("invalid queues_num, should be between %d and %d\n",
			  IWL_MIN_NUM_QUEUES, IWL49_NUM_QUEUES);
		return -EINVAL;
	}

	priv->hw_params.max_txq_num = priv->cfg->mod_params->num_of_queues;
	priv->hw_params.sw_crypto = priv->cfg->mod_params->sw_crypto;
	priv->hw_params.tx_cmd_len = sizeof(struct iwl4965_tx_cmd);
	priv->hw_params.max_rxq_size = RX_QUEUE_SIZE;
	priv->hw_params.max_rxq_log = RX_QUEUE_SIZE_LOG;
	if (priv->cfg->mod_params->amsdu_size_8K)
		priv->hw_params.rx_buf_size = IWL_RX_BUF_SIZE_8K;
	else
		priv->hw_params.rx_buf_size = IWL_RX_BUF_SIZE_4K;
	priv->hw_params.max_pkt_size = priv->hw_params.rx_buf_size - 256;
	priv->hw_params.max_stations = IWL4965_STATION_COUNT;
	priv->hw_params.bcast_sta_id = IWL4965_BROADCAST_ID;

	priv->hw_params.max_data_size = IWL49_RTC_DATA_SIZE;
	priv->hw_params.max_inst_size = IWL49_RTC_INST_SIZE;
	priv->hw_params.max_bsm_size = BSM_SRAM_SIZE;
	priv->hw_params.fat_channel = BIT(IEEE80211_BAND_5GHZ);

	priv->hw_params.tx_chains_num = 2;
	priv->hw_params.rx_chains_num = 2;
	priv->hw_params.valid_tx_ant = ANT_A | ANT_B;
	priv->hw_params.valid_rx_ant = ANT_A | ANT_B;
	priv->hw_params.ct_kill_threshold = CELSIUS_TO_KELVIN(CT_KILL_THRESHOLD);

#ifdef CONFIG_IWL4965_RUN_TIME_CALIB
	priv->hw_params.sens = &iwl4965_sensitivity;
#endif

	return 0;
}

/* set card power command */
static int iwl4965_set_power(struct iwl_priv *priv,
		      void *cmd)
{
	int ret = 0;

	ret = iwl_send_cmd_pdu_async(priv, POWER_TABLE_CMD,
				    sizeof(struct iwl4965_powertable_cmd),
				    cmd, NULL);
	return ret;
}
int iwl4965_hw_reg_set_txpower(struct iwl_priv *priv, s8 power)
{
	IWL_ERROR("TODO: Implement iwl4965_hw_reg_set_txpower!\n");
	return -EINVAL;
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

static const struct iwl_channel_info *
iwl4965_get_channel_txpower_info(struct iwl_priv *priv,
				 enum ieee80211_band band, u16 channel)
{
	const struct iwl_channel_info *ch_info;

	ch_info = iwl_get_channel_info(priv, band, channel);

	if (!is_channel_valid(ch_info))
		return NULL;

	return ch_info;
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

	IWL_ERROR("Can't find txatten group for channel %d.\n", channel);
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
		IWL_ERROR("Tx Power can not find channel %d ", channel);
		return -1;
	}

	ch_i1 = priv->calib_info->band_info[s].ch1.ch_num;
	ch_i2 = priv->calib_info->band_info[s].ch2.ch_num;
	chan_info->ch_num = (u8) channel;

	IWL_DEBUG_TXPOWER("channel %d subband %d factory cal ch %d & %d\n",
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

			IWL_DEBUG_TXPOWER
			    ("chain %d meas %d AP1=%d AP2=%d AP=%d\n", c, m,
			     m1->actual_pow, m2->actual_pow, omeas->actual_pow);
			IWL_DEBUG_TXPOWER
			    ("chain %d meas %d NI1=%d NI2=%d NI=%d\n", c, m,
			     m1->gain_idx, m2->gain_idx, omeas->gain_idx);
			IWL_DEBUG_TXPOWER
			    ("chain %d meas %d PA1=%d PA2=%d PA=%d\n", c, m,
			     m1->pa_det, m2->pa_det, omeas->pa_det);
			IWL_DEBUG_TXPOWER
			    ("chain %d meas %d  T1=%d  T2=%d  T=%d\n", c, m,
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
				    u8 is_fat, u8 ctrl_chan_high,
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

	/* Sanity check requested level (dBm) */
	if (priv->user_txpower_limit < IWL_TX_POWER_TARGET_POWER_MIN) {
		IWL_WARNING("Requested user TXPOWER %d below limit.\n",
			    priv->user_txpower_limit);
		return -EINVAL;
	}
	if (priv->user_txpower_limit > IWL_TX_POWER_TARGET_POWER_MAX) {
		IWL_WARNING("Requested user TXPOWER %d above limit.\n",
			    priv->user_txpower_limit);
		return -EINVAL;
	}

	/* user_txpower_limit is in dBm, convert to half-dBm (half-dB units
	 *   are used for indexing into txpower table) */
	user_target_power = 2 * priv->user_txpower_limit;

	/* Get current (RXON) channel, band, width */
	ch_info =
		iwl4965_get_channel_txpower_info(priv, priv->band, channel);

	IWL_DEBUG_TXPOWER("chan %d band %d is_fat %d\n", channel, band,
			  is_fat);

	if (!ch_info)
		return -EINVAL;

	/* get txatten group, used to select 1) thermal txpower adjustment
	 *   and 2) mimo txpower balance between Tx chains. */
	txatten_grp = iwl4965_get_tx_atten_grp(channel);
	if (txatten_grp < 0)
		return -EINVAL;

	IWL_DEBUG_TXPOWER("channel %d belongs to txatten group %d\n",
			  channel, txatten_grp);

	if (is_fat) {
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
	if (is_fat)
		reg_limit = ch_info->fat_max_power_avg * 2;
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

	IWL_DEBUG_TXPOWER("curr volt %d eeprom volt %d volt comp %d\n",
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

		IWL_DEBUG_TXPOWER("chain = %d\n", c);
		IWL_DEBUG_TXPOWER("fctry tmp %d, "
				  "curr tmp %d, comp %d steps\n",
				  factory_temp, current_temp,
				  temperature_comp[c]);

		IWL_DEBUG_TXPOWER("fctry idx %d, fctry pwr %d\n",
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

		IWL_DEBUG_TXPOWER("rate %d sat %d reg %d usr %d tgt %d\n",
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

/*			IWL_DEBUG_TXPOWER("calculated txpower index %d\n",
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
				IWL_WARNING("txpower index %d > 107\n",
					    power_index);
				power_index = 107;
			}
			if (power_index < 0) {
				IWL_WARNING("txpower index %d < 0\n",
					    power_index);
				power_index = 0;
			}

			/* fill txpower command for this rate/chain */
			tx_power.s.radio_tx_gain[c] =
				gain_table[band][power_index].radio;
			tx_power.s.dsp_predis_atten[c] =
				gain_table[band][power_index].dsp;

			IWL_DEBUG_TXPOWER("chain %d mimo %d index %d "
					  "gain 0x%02x dsp %d\n",
					  c, atten_value, power_index,
					tx_power.s.radio_tx_gain[c],
					tx_power.s.dsp_predis_atten[c]);
		}/* for each chain */

		tx_power_tbl->power_tbl[i].dw = cpu_to_le32(tx_power.dw);

	}/* for each rate */

	return 0;
}

/**
 * iwl4965_hw_reg_send_txpower - Configure the TXPOWER level user limit
 *
 * Uses the active RXON for channel, band, and characteristics (fat, high)
 * The power limit is taken from priv->user_txpower_limit.
 */
int iwl4965_hw_reg_send_txpower(struct iwl_priv *priv)
{
	struct iwl4965_txpowertable_cmd cmd = { 0 };
	int ret;
	u8 band = 0;
	u8 is_fat = 0;
	u8 ctrl_chan_high = 0;

	if (test_bit(STATUS_SCANNING, &priv->status)) {
		/* If this gets hit a lot, switch it to a BUG() and catch
		 * the stack trace to find out who is calling this during
		 * a scan. */
		IWL_WARNING("TX Power requested while scanning!\n");
		return -EAGAIN;
	}

	band = priv->band == IEEE80211_BAND_2GHZ;

	is_fat =  is_fat_channel(priv->active_rxon.flags);

	if (is_fat &&
	    (priv->active_rxon.flags & RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK))
		ctrl_chan_high = 1;

	cmd.band = band;
	cmd.channel = priv->active_rxon.channel;

	ret = iwl4965_fill_txpower_tbl(priv, band,
				le16_to_cpu(priv->active_rxon.channel),
				is_fat, ctrl_chan_high, &cmd.tx_power);
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
	const struct iwl4965_rxon_cmd *rxon1 = &priv->staging_rxon;
	const struct iwl4965_rxon_cmd *rxon2 = &priv->active_rxon;

	if ((rxon1->flags == rxon2->flags) &&
	    (rxon1->filter_flags == rxon2->filter_flags) &&
	    (rxon1->cck_basic_rates == rxon2->cck_basic_rates) &&
	    (rxon1->ofdm_ht_single_stream_basic_rates ==
	     rxon2->ofdm_ht_single_stream_basic_rates) &&
	    (rxon1->ofdm_ht_dual_stream_basic_rates ==
	     rxon2->ofdm_ht_dual_stream_basic_rates) &&
	    (rxon1->rx_chain == rxon2->rx_chain) &&
	    (rxon1->ofdm_basic_rates == rxon2->ofdm_basic_rates)) {
		IWL_DEBUG_INFO("Using current RXON_ASSOC.  Not resending.\n");
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


int iwl4965_hw_channel_switch(struct iwl_priv *priv, u16 channel)
{
	int rc;
	u8 band = 0;
	u8 is_fat = 0;
	u8 ctrl_chan_high = 0;
	struct iwl4965_channel_switch_cmd cmd = { 0 };
	const struct iwl_channel_info *ch_info;

	band = priv->band == IEEE80211_BAND_2GHZ;

	ch_info = iwl_get_channel_info(priv, priv->band, channel);

	is_fat = is_fat_channel(priv->staging_rxon.flags);

	if (is_fat &&
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
	else
		cmd.expect_beacon = 1;

	rc = iwl4965_fill_txpower_tbl(priv, band, channel, is_fat,
				      ctrl_chan_high, &cmd.tx_power);
	if (rc) {
		IWL_DEBUG_11H("error:%d  fill txpower_tbl\n", rc);
		return rc;
	}

	rc = iwl_send_cmd_pdu(priv, REPLY_CHANNEL_SWITCH, sizeof(cmd), &cmd);
	return rc;
}

#define RTS_HCCA_RETRY_LIMIT		3
#define RTS_DFAULT_RETRY_LIMIT		60

void iwl4965_hw_build_tx_cmd_rate(struct iwl_priv *priv,
			      struct iwl_cmd *cmd,
			      struct ieee80211_tx_control *ctrl,
			      struct ieee80211_hdr *hdr, int sta_id,
			      int is_hcca)
{
	struct iwl4965_tx_cmd *tx = &cmd->cmd.tx;
	u8 rts_retry_limit = 0;
	u8 data_retry_limit = 0;
	u16 fc = le16_to_cpu(hdr->frame_control);
	u8 rate_plcp;
	u16 rate_flags = 0;
	int rate_idx = min(ctrl->tx_rate->hw_value & 0xffff, IWL_RATE_COUNT - 1);

	rate_plcp = iwl4965_rates[rate_idx].plcp;

	rts_retry_limit = (is_hcca) ?
	    RTS_HCCA_RETRY_LIMIT : RTS_DFAULT_RETRY_LIMIT;

	if ((rate_idx >= IWL_FIRST_CCK_RATE) && (rate_idx <= IWL_LAST_CCK_RATE))
		rate_flags |= RATE_MCS_CCK_MSK;


	if (ieee80211_is_probe_response(fc)) {
		data_retry_limit = 3;
		if (data_retry_limit < rts_retry_limit)
			rts_retry_limit = data_retry_limit;
	} else
		data_retry_limit = IWL_DEFAULT_TX_RETRY;

	if (priv->data_retry_limit != -1)
		data_retry_limit = priv->data_retry_limit;


	if (ieee80211_is_data(fc)) {
		tx->initial_rate_index = 0;
		tx->tx_flags |= TX_CMD_FLG_STA_RATE_MSK;
	} else {
		switch (fc & IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_AUTH:
		case IEEE80211_STYPE_DEAUTH:
		case IEEE80211_STYPE_ASSOC_REQ:
		case IEEE80211_STYPE_REASSOC_REQ:
			if (tx->tx_flags & TX_CMD_FLG_RTS_MSK) {
				tx->tx_flags &= ~TX_CMD_FLG_RTS_MSK;
				tx->tx_flags |= TX_CMD_FLG_CTS_MSK;
			}
			break;
		default:
			break;
		}

		/* Alternate between antenna A and B for successive frames */
		if (priv->use_ant_b_for_management_frame) {
			priv->use_ant_b_for_management_frame = 0;
			rate_flags |= RATE_MCS_ANT_B_MSK;
		} else {
			priv->use_ant_b_for_management_frame = 1;
			rate_flags |= RATE_MCS_ANT_A_MSK;
		}
	}

	tx->rts_retry_limit = rts_retry_limit;
	tx->data_retry_limit = data_retry_limit;
	tx->rate_n_flags = iwl4965_hw_set_rate_n_flags(rate_plcp, rate_flags);
}

static int iwl4965_shared_mem_rx_idx(struct iwl_priv *priv)
{
	struct iwl4965_shared *s = priv->shared_virt;
	return le32_to_cpu(s->rb_closed) & 0xFFF;
}

int iwl4965_hw_get_temperature(struct iwl_priv *priv)
{
	return priv->temperature;
}

unsigned int iwl4965_hw_get_beacon_cmd(struct iwl_priv *priv,
			  struct iwl4965_frame *frame, u8 rate)
{
	struct iwl4965_tx_beacon_cmd *tx_beacon_cmd;
	unsigned int frame_size;

	tx_beacon_cmd = &frame->u.beacon;
	memset(tx_beacon_cmd, 0, sizeof(*tx_beacon_cmd));

	tx_beacon_cmd->tx.sta_id = priv->hw_params.bcast_sta_id;
	tx_beacon_cmd->tx.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;

	frame_size = iwl4965_fill_beacon_frame(priv,
				tx_beacon_cmd->frame,
				iwl4965_broadcast_addr,
				sizeof(frame->u) - sizeof(*tx_beacon_cmd));

	BUG_ON(frame_size > MAX_MPDU_SIZE);
	tx_beacon_cmd->tx.len = cpu_to_le16((u16)frame_size);

	if ((rate == IWL_RATE_1M_PLCP) || (rate >= IWL_RATE_2M_PLCP))
		tx_beacon_cmd->tx.rate_n_flags =
			iwl4965_hw_set_rate_n_flags(rate, RATE_MCS_CCK_MSK);
	else
		tx_beacon_cmd->tx.rate_n_flags =
			iwl4965_hw_set_rate_n_flags(rate, 0);

	tx_beacon_cmd->tx.tx_flags = (TX_CMD_FLG_SEQ_CTL_MSK |
				TX_CMD_FLG_TSF_MSK | TX_CMD_FLG_STA_RATE_MSK);
	return (sizeof(*tx_beacon_cmd) + frame_size);
}

int iwl4965_hw_txq_attach_buf_to_tfd(struct iwl_priv *priv, void *ptr,
				 dma_addr_t addr, u16 len)
{
	int index, is_odd;
	struct iwl_tfd_frame *tfd = ptr;
	u32 num_tbs = IWL_GET_BITS(*tfd, num_tbs);

	/* Each TFD can point to a maximum 20 Tx buffers */
	if ((num_tbs >= MAX_NUM_OF_TBS) || (num_tbs < 0)) {
		IWL_ERROR("Error can not send more than %d chunks\n",
			  MAX_NUM_OF_TBS);
		return -EINVAL;
	}

	index = num_tbs / 2;
	is_odd = num_tbs & 0x1;

	if (!is_odd) {
		tfd->pa[index].tb1_addr = cpu_to_le32(addr);
		IWL_SET_BITS(tfd->pa[index], tb1_addr_hi,
			     iwl_get_dma_hi_address(addr));
		IWL_SET_BITS(tfd->pa[index], tb1_len, len);
	} else {
		IWL_SET_BITS(tfd->pa[index], tb2_addr_lo16,
			     (u32) (addr & 0xffff));
		IWL_SET_BITS(tfd->pa[index], tb2_addr_hi20, addr >> 16);
		IWL_SET_BITS(tfd->pa[index], tb2_len, len);
	}

	IWL_SET_BITS(*tfd, num_tbs, num_tbs + 1);

	return 0;
}

static int iwl4965_alloc_shared_mem(struct iwl_priv *priv)
{
	priv->shared_virt = pci_alloc_consistent(priv->pci_dev,
					sizeof(struct iwl4965_shared),
					&priv->shared_phys);
	if (!priv->shared_virt)
		return -ENOMEM;

	memset(priv->shared_virt, 0, sizeof(struct iwl4965_shared));

	priv->rb_closed_offset = offsetof(struct iwl4965_shared, rb_closed);

	return 0;
}

static void iwl4965_free_shared_mem(struct iwl_priv *priv)
{
	if (priv->shared_virt)
		pci_free_consistent(priv->pci_dev,
				    sizeof(struct iwl4965_shared),
				    priv->shared_virt,
				    priv->shared_phys);
}

/**
 * iwl4965_txq_update_byte_cnt_tbl - Set up entry in Tx byte-count array
 */
static void iwl4965_txq_update_byte_cnt_tbl(struct iwl_priv *priv,
					    struct iwl_tx_queue *txq,
					    u16 byte_cnt)
{
	int len;
	int txq_id = txq->q.id;
	struct iwl4965_shared *shared_data = priv->shared_virt;

	len = byte_cnt + IWL_TX_CRC_SIZE + IWL_TX_DELIMITER_SIZE;

	/* Set up byte count within first 256 entries */
	IWL_SET_BITS16(shared_data->queues_byte_cnt_tbls[txq_id].
		       tfd_offset[txq->q.write_ptr], byte_cnt, len);

	/* If within first 64 entries, duplicate at end */
	if (txq->q.write_ptr < IWL49_MAX_WIN_SIZE)
		IWL_SET_BITS16(shared_data->queues_byte_cnt_tbls[txq_id].
			tfd_offset[IWL49_QUEUE_SIZE + txq->q.write_ptr],
			byte_cnt, len);
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
 * iwl4965_get_temperature - return the calibrated temperature (in Kelvin)
 * @statistics: Provides the temperature reading from the uCode
 *
 * A return of <0 indicates bogus data in the statistics
 */
int iwl4965_get_temperature(const struct iwl_priv *priv)
{
	s32 temperature;
	s32 vt;
	s32 R1, R2, R3;
	u32 R4;

	if (test_bit(STATUS_TEMPERATURE, &priv->status) &&
		(priv->statistics.flag & STATISTICS_REPLY_FLG_FAT_MODE_MSK)) {
		IWL_DEBUG_TEMP("Running FAT temperature calibration\n");
		R1 = (s32)le32_to_cpu(priv->card_alive_init.therm_r1[1]);
		R2 = (s32)le32_to_cpu(priv->card_alive_init.therm_r2[1]);
		R3 = (s32)le32_to_cpu(priv->card_alive_init.therm_r3[1]);
		R4 = le32_to_cpu(priv->card_alive_init.therm_r4[1]);
	} else {
		IWL_DEBUG_TEMP("Running temperature calibration\n");
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

	IWL_DEBUG_TEMP("Calib values R[1-3]: %d %d %d R4: %d\n",
		       R1, R2, R3, vt);

	if (R3 == R1) {
		IWL_ERROR("Calibration conflict R1 == R3\n");
		return -1;
	}

	/* Calculate temperature in degrees Kelvin, adjust by 97%.
	 * Add offset to center the adjustment around 0 degrees Centigrade. */
	temperature = TEMPERATURE_CALIB_A_VAL * (vt - R2);
	temperature /= (R3 - R1);
	temperature = (temperature * 97) / 100 +
	    TEMPERATURE_CALIB_KELVIN_OFFSET;

	IWL_DEBUG_TEMP("Calibrated temperature: %dK, %dC\n", temperature,
	    KELVIN_TO_CELSIUS(temperature));

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
		IWL_DEBUG_TEMP("Temperature not updated -- no statistics.\n");
		return 0;
	}

	temp_diff = priv->temperature - priv->last_temperature;

	/* get absolute value */
	if (temp_diff < 0) {
		IWL_DEBUG_POWER("Getting cooler, delta %d, \n", temp_diff);
		temp_diff = -temp_diff;
	} else if (temp_diff == 0)
		IWL_DEBUG_POWER("Same temp, \n");
	else
		IWL_DEBUG_POWER("Getting warmer, delta %d, \n", temp_diff);

	if (temp_diff < IWL_TEMPERATURE_THRESHOLD) {
		IWL_DEBUG_POWER("Thermal txpower calib not needed\n");
		return 0;
	}

	IWL_DEBUG_POWER("Thermal txpower calib needed\n");

	return 1;
}

/* Calculate noise level, based on measurements during network silence just
 *   before arriving beacon.  This measurement can be done only if we know
 *   exactly when to expect beacons, therefore only when we're associated. */
static void iwl4965_rx_calc_noise(struct iwl_priv *priv)
{
	struct statistics_rx_non_phy *rx_info
				= &(priv->statistics.rx.general);
	int num_active_rx = 0;
	int total_silence = 0;
	int bcn_silence_a =
		le32_to_cpu(rx_info->beacon_silence_rssi_a) & IN_BAND_FILTER;
	int bcn_silence_b =
		le32_to_cpu(rx_info->beacon_silence_rssi_b) & IN_BAND_FILTER;
	int bcn_silence_c =
		le32_to_cpu(rx_info->beacon_silence_rssi_c) & IN_BAND_FILTER;

	if (bcn_silence_a) {
		total_silence += bcn_silence_a;
		num_active_rx++;
	}
	if (bcn_silence_b) {
		total_silence += bcn_silence_b;
		num_active_rx++;
	}
	if (bcn_silence_c) {
		total_silence += bcn_silence_c;
		num_active_rx++;
	}

	/* Average among active antennas */
	if (num_active_rx)
		priv->last_rx_noise = (total_silence / num_active_rx) - 107;
	else
		priv->last_rx_noise = IWL_NOISE_MEAS_NOT_AVAILABLE;

	IWL_DEBUG_CALIB("inband silence a %u, b %u, c %u, dBm %d\n",
			bcn_silence_a, bcn_silence_b, bcn_silence_c,
			priv->last_rx_noise);
}

void iwl4965_hw_rx_statistics(struct iwl_priv *priv,
			      struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	int change;
	s32 temp;

	IWL_DEBUG_RX("Statistics notification received (%d vs %d).\n",
		     (int)sizeof(priv->statistics), pkt->len);

	change = ((priv->statistics.general.temperature !=
		   pkt->u.stats.general.temperature) ||
		  ((priv->statistics.flag &
		    STATISTICS_REPLY_FLG_FAT_MODE_MSK) !=
		   (pkt->u.stats.flag & STATISTICS_REPLY_FLG_FAT_MODE_MSK)));

	memcpy(&priv->statistics, &pkt->u.stats, sizeof(priv->statistics));

	set_bit(STATUS_STATISTICS, &priv->status);

	/* Reschedule the statistics timer to occur in
	 * REG_RECALIB_PERIOD seconds to ensure we get a
	 * thermal update even if the uCode doesn't give
	 * us one */
	mod_timer(&priv->statistics_periodic, jiffies +
		  msecs_to_jiffies(REG_RECALIB_PERIOD * 1000));

	if (unlikely(!test_bit(STATUS_SCANNING, &priv->status)) &&
	    (pkt->hdr.cmd == STATISTICS_NOTIFICATION)) {
		iwl4965_rx_calc_noise(priv);
#ifdef CONFIG_IWL4965_RUN_TIME_CALIB
		queue_work(priv->workqueue, &priv->sensitivity_work);
#endif
	}

	iwl_leds_background(priv);

	/* If the hardware hasn't reported a change in
	 * temperature then don't bother computing a
	 * calibrated temperature value */
	if (!change)
		return;

	temp = iwl4965_get_temperature(priv);
	if (temp < 0)
		return;

	if (priv->temperature != temp) {
		if (priv->temperature)
			IWL_DEBUG_TEMP("Temperature changed "
				       "from %dC to %dC\n",
				       KELVIN_TO_CELSIUS(priv->temperature),
				       KELVIN_TO_CELSIUS(temp));
		else
			IWL_DEBUG_TEMP("Temperature "
				       "initialized to %dC\n",
				       KELVIN_TO_CELSIUS(temp));
	}

	priv->temperature = temp;
	set_bit(STATUS_TEMPERATURE, &priv->status);

	if (unlikely(!test_bit(STATUS_SCANNING, &priv->status)) &&
		     iwl4965_is_temp_calib_needed(priv))
		queue_work(priv->workqueue, &priv->txpower_work);
}

static void iwl4965_add_radiotap(struct iwl_priv *priv,
				 struct sk_buff *skb,
				 struct iwl4965_rx_phy_res *rx_start,
				 struct ieee80211_rx_status *stats,
				 u32 ampdu_status)
{
	s8 signal = stats->signal;
	s8 noise = 0;
	int rate = stats->rate_idx;
	u64 tsf = stats->mactime;
	__le16 antenna;
	__le16 phy_flags_hw = rx_start->phy_flags;
	struct iwl4965_rt_rx_hdr {
		struct ieee80211_radiotap_header rt_hdr;
		__le64 rt_tsf;		/* TSF */
		u8 rt_flags;		/* radiotap packet flags */
		u8 rt_rate;		/* rate in 500kb/s */
		__le16 rt_channelMHz;	/* channel in MHz */
		__le16 rt_chbitmask;	/* channel bitfield */
		s8 rt_dbmsignal;	/* signal in dBm, kluged to signed */
		s8 rt_dbmnoise;
		u8 rt_antenna;		/* antenna number */
	} __attribute__ ((packed)) *iwl4965_rt;

	/* TODO: We won't have enough headroom for HT frames. Fix it later. */
	if (skb_headroom(skb) < sizeof(*iwl4965_rt)) {
		if (net_ratelimit())
			printk(KERN_ERR "not enough headroom [%d] for "
			       "radiotap head [%zd]\n",
			       skb_headroom(skb), sizeof(*iwl4965_rt));
		return;
	}

	/* put radiotap header in front of 802.11 header and data */
	iwl4965_rt = (void *)skb_push(skb, sizeof(*iwl4965_rt));

	/* initialise radiotap header */
	iwl4965_rt->rt_hdr.it_version = PKTHDR_RADIOTAP_VERSION;
	iwl4965_rt->rt_hdr.it_pad = 0;

	/* total header + data */
	put_unaligned(cpu_to_le16(sizeof(*iwl4965_rt)),
		      &iwl4965_rt->rt_hdr.it_len);

	/* Indicate all the fields we add to the radiotap header */
	put_unaligned(cpu_to_le32((1 << IEEE80211_RADIOTAP_TSFT) |
				  (1 << IEEE80211_RADIOTAP_FLAGS) |
				  (1 << IEEE80211_RADIOTAP_RATE) |
				  (1 << IEEE80211_RADIOTAP_CHANNEL) |
				  (1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL) |
				  (1 << IEEE80211_RADIOTAP_DBM_ANTNOISE) |
				  (1 << IEEE80211_RADIOTAP_ANTENNA)),
		      &iwl4965_rt->rt_hdr.it_present);

	/* Zero the flags, we'll add to them as we go */
	iwl4965_rt->rt_flags = 0;

	put_unaligned(cpu_to_le64(tsf), &iwl4965_rt->rt_tsf);

	iwl4965_rt->rt_dbmsignal = signal;
	iwl4965_rt->rt_dbmnoise = noise;

	/* Convert the channel frequency and set the flags */
	put_unaligned(cpu_to_le16(stats->freq), &iwl4965_rt->rt_channelMHz);
	if (!(phy_flags_hw & RX_RES_PHY_FLAGS_BAND_24_MSK))
		put_unaligned(cpu_to_le16(IEEE80211_CHAN_OFDM |
					  IEEE80211_CHAN_5GHZ),
			      &iwl4965_rt->rt_chbitmask);
	else if (phy_flags_hw & RX_RES_PHY_FLAGS_MOD_CCK_MSK)
		put_unaligned(cpu_to_le16(IEEE80211_CHAN_CCK |
					  IEEE80211_CHAN_2GHZ),
			      &iwl4965_rt->rt_chbitmask);
	else	/* 802.11g */
		put_unaligned(cpu_to_le16(IEEE80211_CHAN_OFDM |
					  IEEE80211_CHAN_2GHZ),
			      &iwl4965_rt->rt_chbitmask);

	if (rate == -1)
		iwl4965_rt->rt_rate = 0;
	else
		iwl4965_rt->rt_rate = iwl4965_rates[rate].ieee;

	/*
	 * "antenna number"
	 *
	 * It seems that the antenna field in the phy flags value
	 * is actually a bitfield. This is undefined by radiotap,
	 * it wants an actual antenna number but I always get "7"
	 * for most legacy frames I receive indicating that the
	 * same frame was received on all three RX chains.
	 *
	 * I think this field should be removed in favour of a
	 * new 802.11n radiotap field "RX chains" that is defined
	 * as a bitmask.
	 */
	antenna = phy_flags_hw & RX_RES_PHY_FLAGS_ANTENNA_MSK;
	iwl4965_rt->rt_antenna = le16_to_cpu(antenna) >> 4;

	/* set the preamble flag if appropriate */
	if (phy_flags_hw & RX_RES_PHY_FLAGS_SHORT_PREAMBLE_MSK)
		iwl4965_rt->rt_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;

	stats->flag |= RX_FLAG_RADIOTAP;
}

static void iwl_update_rx_stats(struct iwl_priv *priv, u16 fc, u16 len)
{
	/* 0 - mgmt, 1 - cnt, 2 - data */
	int idx = (fc & IEEE80211_FCTL_FTYPE) >> 2;
	priv->rx_stats[idx].cnt++;
	priv->rx_stats[idx].bytes += len;
}

/*
 * returns non-zero if packet should be dropped
 */
static int iwl4965_set_decrypted_flag(struct iwl_priv *priv,
				      struct ieee80211_hdr *hdr,
				      u32 decrypt_res,
				      struct ieee80211_rx_status *stats)
{
	u16 fc = le16_to_cpu(hdr->frame_control);

	if (priv->active_rxon.filter_flags & RXON_FILTER_DIS_DECRYPT_MSK)
		return 0;

	if (!(fc & IEEE80211_FCTL_PROTECTED))
		return 0;

	IWL_DEBUG_RX("decrypt_res:0x%x\n", decrypt_res);
	switch (decrypt_res & RX_RES_STATUS_SEC_TYPE_MSK) {
	case RX_RES_STATUS_SEC_TYPE_TKIP:
		/* The uCode has got a bad phase 1 Key, pushes the packet.
		 * Decryption will be done in SW. */
		if ((decrypt_res & RX_RES_STATUS_DECRYPT_TYPE_MSK) ==
		    RX_RES_STATUS_BAD_KEY_TTAK)
			break;

		if ((decrypt_res & RX_RES_STATUS_DECRYPT_TYPE_MSK) ==
		    RX_RES_STATUS_BAD_ICV_MIC) {
			/* bad ICV, the packet is destroyed since the
			 * decryption is inplace, drop it */
			IWL_DEBUG_RX("Packet destroyed\n");
			return -1;
		}
	case RX_RES_STATUS_SEC_TYPE_WEP:
	case RX_RES_STATUS_SEC_TYPE_CCMP:
		if ((decrypt_res & RX_RES_STATUS_DECRYPT_TYPE_MSK) ==
		    RX_RES_STATUS_DECRYPT_OK) {
			IWL_DEBUG_RX("hw decrypt successfully!!!\n");
			stats->flag |= RX_FLAG_DECRYPTED;
		}
		break;

	default:
		break;
	}
	return 0;
}

static u32 iwl4965_translate_rx_status(struct iwl_priv *priv, u32 decrypt_in)
{
	u32 decrypt_out = 0;

	if ((decrypt_in & RX_RES_STATUS_STATION_FOUND) ==
					RX_RES_STATUS_STATION_FOUND)
		decrypt_out |= (RX_RES_STATUS_STATION_FOUND |
				RX_RES_STATUS_NO_STATION_INFO_MISMATCH);

	decrypt_out |= (decrypt_in & RX_RES_STATUS_SEC_TYPE_MSK);

	/* packet was not encrypted */
	if ((decrypt_in & RX_RES_STATUS_SEC_TYPE_MSK) ==
					RX_RES_STATUS_SEC_TYPE_NONE)
		return decrypt_out;

	/* packet was encrypted with unknown alg */
	if ((decrypt_in & RX_RES_STATUS_SEC_TYPE_MSK) ==
					RX_RES_STATUS_SEC_TYPE_ERR)
		return decrypt_out;

	/* decryption was not done in HW */
	if ((decrypt_in & RX_MPDU_RES_STATUS_DEC_DONE_MSK) !=
					RX_MPDU_RES_STATUS_DEC_DONE_MSK)
		return decrypt_out;

	switch (decrypt_in & RX_RES_STATUS_SEC_TYPE_MSK) {

	case RX_RES_STATUS_SEC_TYPE_CCMP:
		/* alg is CCM: check MIC only */
		if (!(decrypt_in & RX_MPDU_RES_STATUS_MIC_OK))
			/* Bad MIC */
			decrypt_out |= RX_RES_STATUS_BAD_ICV_MIC;
		else
			decrypt_out |= RX_RES_STATUS_DECRYPT_OK;

		break;

	case RX_RES_STATUS_SEC_TYPE_TKIP:
		if (!(decrypt_in & RX_MPDU_RES_STATUS_TTAK_OK)) {
			/* Bad TTAK */
			decrypt_out |= RX_RES_STATUS_BAD_KEY_TTAK;
			break;
		}
		/* fall through if TTAK OK */
	default:
		if (!(decrypt_in & RX_MPDU_RES_STATUS_ICV_OK))
			decrypt_out |= RX_RES_STATUS_BAD_ICV_MIC;
		else
			decrypt_out |= RX_RES_STATUS_DECRYPT_OK;
		break;
	};

	IWL_DEBUG_RX("decrypt_in:0x%x  decrypt_out = 0x%x\n",
					decrypt_in, decrypt_out);

	return decrypt_out;
}

static void iwl4965_handle_data_packet(struct iwl_priv *priv, int is_data,
				       int include_phy,
				       struct iwl_rx_mem_buffer *rxb,
				       struct ieee80211_rx_status *stats)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl4965_rx_phy_res *rx_start = (include_phy) ?
	    (struct iwl4965_rx_phy_res *)&(pkt->u.raw[0]) : NULL;
	struct ieee80211_hdr *hdr;
	u16 len;
	__le32 *rx_end;
	unsigned int skblen;
	u32 ampdu_status;
	u32 ampdu_status_legacy;

	if (!include_phy && priv->last_phy_res[0])
		rx_start = (struct iwl4965_rx_phy_res *)&priv->last_phy_res[1];

	if (!rx_start) {
		IWL_ERROR("MPDU frame without a PHY data\n");
		return;
	}
	if (include_phy) {
		hdr = (struct ieee80211_hdr *)((u8 *) & rx_start[1] +
					       rx_start->cfg_phy_cnt);

		len = le16_to_cpu(rx_start->byte_count);

		rx_end = (__le32 *) ((u8 *) & pkt->u.raw[0] +
				  sizeof(struct iwl4965_rx_phy_res) +
				  rx_start->cfg_phy_cnt + len);

	} else {
		struct iwl4965_rx_mpdu_res_start *amsdu =
		    (struct iwl4965_rx_mpdu_res_start *)pkt->u.raw;

		hdr = (struct ieee80211_hdr *)(pkt->u.raw +
			       sizeof(struct iwl4965_rx_mpdu_res_start));
		len =  le16_to_cpu(amsdu->byte_count);
		rx_start->byte_count = amsdu->byte_count;
		rx_end = (__le32 *) (((u8 *) hdr) + len);
	}
	/* In monitor mode allow 802.11 ACk frames (10 bytes) */
	if (len > priv->hw_params.max_pkt_size ||
	    len < ((priv->iw_mode == IEEE80211_IF_TYPE_MNTR) ? 10 : 16)) {
		IWL_WARNING("byte count out of range [16,4K] : %d\n", len);
		return;
	}

	ampdu_status = le32_to_cpu(*rx_end);
	skblen = ((u8 *) rx_end - (u8 *) & pkt->u.raw[0]) + sizeof(u32);

	if (!include_phy) {
		/* New status scheme, need to translate */
		ampdu_status_legacy = ampdu_status;
		ampdu_status = iwl4965_translate_rx_status(priv, ampdu_status);
	}

	/* start from MAC */
	skb_reserve(rxb->skb, (void *)hdr - (void *)pkt);
	skb_put(rxb->skb, len);	/* end where data ends */

	/* We only process data packets if the interface is open */
	if (unlikely(!priv->is_open)) {
		IWL_DEBUG_DROP_LIMIT
		    ("Dropping packet while interface is not open.\n");
		return;
	}

	stats->flag = 0;
	hdr = (struct ieee80211_hdr *)rxb->skb->data;

	/*  in case of HW accelerated crypto and bad decryption, drop */
	if (!priv->hw_params.sw_crypto &&
	    iwl4965_set_decrypted_flag(priv, hdr, ampdu_status, stats))
		return;

	if (priv->add_radiotap)
		iwl4965_add_radiotap(priv, rxb->skb, rx_start, stats, ampdu_status);

	iwl_update_rx_stats(priv, le16_to_cpu(hdr->frame_control), len);
	ieee80211_rx_irqsafe(priv->hw, rxb->skb, stats);
	priv->alloc_rxb_skb--;
	rxb->skb = NULL;
}

/* Calc max signal level (dBm) among 3 possible receivers */
static int iwl4965_calc_rssi(struct iwl_priv *priv,
			     struct iwl4965_rx_phy_res *rx_resp)
{
	/* data from PHY/DSP regarding signal strength, etc.,
	 *   contents are always there, not configurable by host.  */
	struct iwl4965_rx_non_cfg_phy *ncphy =
	    (struct iwl4965_rx_non_cfg_phy *)rx_resp->non_cfg_phy;
	u32 agc = (le16_to_cpu(ncphy->agc_info) & IWL_AGC_DB_MASK)
			>> IWL_AGC_DB_POS;

	u32 valid_antennae =
	    (le16_to_cpu(rx_resp->phy_flags) & RX_PHY_FLAGS_ANTENNAE_MASK)
			>> RX_PHY_FLAGS_ANTENNAE_OFFSET;
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

	IWL_DEBUG_STATS("Rssi In A %d B %d C %d Max %d AGC dB %d\n",
		ncphy->rssi_info[0], ncphy->rssi_info[2], ncphy->rssi_info[4],
		max_rssi, agc);

	/* dBm = max_rssi dB - agc dB - constant.
	 * Higher AGC (higher radio gain) means lower signal. */
	return (max_rssi - agc - IWL_RSSI_OFFSET);
}

static void iwl4965_sta_modify_ps_wake(struct iwl_priv *priv, int sta_id)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags &= ~STA_FLG_PWR_SAVE_MSK;
	priv->stations[sta_id].sta.station_flags_msk = STA_FLG_PWR_SAVE_MSK;
	priv->stations[sta_id].sta.sta.modify_mask = 0;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);
}

static void iwl4965_update_ps_mode(struct iwl_priv *priv, u16 ps_bit, u8 *addr)
{
	/* FIXME: need locking over ps_status ??? */
	u8 sta_id = iwl_find_station(priv, addr);

	if (sta_id != IWL_INVALID_STATION) {
		u8 sta_awake = priv->stations[sta_id].
				ps_status == STA_PS_STATUS_WAKE;

		if (sta_awake && ps_bit)
			priv->stations[sta_id].ps_status = STA_PS_STATUS_SLEEP;
		else if (!sta_awake && !ps_bit) {
			iwl4965_sta_modify_ps_wake(priv, sta_id);
			priv->stations[sta_id].ps_status = STA_PS_STATUS_WAKE;
		}
	}
}
#ifdef CONFIG_IWLWIFI_DEBUG

/**
 * iwl4965_dbg_report_frame - dump frame to syslog during debug sessions
 *
 * You may hack this function to show different aspects of received frames,
 * including selective frame dumps.
 * group100 parameter selects whether to show 1 out of 100 good frames.
 *
 * TODO:  This was originally written for 3945, need to audit for
 *        proper operation with 4965.
 */
static void iwl4965_dbg_report_frame(struct iwl_priv *priv,
		      struct iwl_rx_packet *pkt,
		      struct ieee80211_hdr *header, int group100)
{
	u32 to_us;
	u32 print_summary = 0;
	u32 print_dump = 0;	/* set to 1 to dump all frames' contents */
	u32 hundred = 0;
	u32 dataframe = 0;
	u16 fc;
	u16 seq_ctl;
	u16 channel;
	u16 phy_flags;
	int rate_sym;
	u16 length;
	u16 status;
	u16 bcn_tmr;
	u32 tsf_low;
	u64 tsf;
	u8 rssi;
	u8 agc;
	u16 sig_avg;
	u16 noise_diff;
	struct iwl4965_rx_frame_stats *rx_stats = IWL_RX_STATS(pkt);
	struct iwl4965_rx_frame_hdr *rx_hdr = IWL_RX_HDR(pkt);
	struct iwl4965_rx_frame_end *rx_end = IWL_RX_END(pkt);
	u8 *data = IWL_RX_DATA(pkt);

	if (likely(!(priv->debug_level & IWL_DL_RX)))
		return;

	/* MAC header */
	fc = le16_to_cpu(header->frame_control);
	seq_ctl = le16_to_cpu(header->seq_ctrl);

	/* metadata */
	channel = le16_to_cpu(rx_hdr->channel);
	phy_flags = le16_to_cpu(rx_hdr->phy_flags);
	rate_sym = rx_hdr->rate;
	length = le16_to_cpu(rx_hdr->len);

	/* end-of-frame status and timestamp */
	status = le32_to_cpu(rx_end->status);
	bcn_tmr = le32_to_cpu(rx_end->beacon_timestamp);
	tsf_low = le64_to_cpu(rx_end->timestamp) & 0x0ffffffff;
	tsf = le64_to_cpu(rx_end->timestamp);

	/* signal statistics */
	rssi = rx_stats->rssi;
	agc = rx_stats->agc;
	sig_avg = le16_to_cpu(rx_stats->sig_avg);
	noise_diff = le16_to_cpu(rx_stats->noise_diff);

	to_us = !compare_ether_addr(header->addr1, priv->mac_addr);

	/* if data frame is to us and all is good,
	 *   (optionally) print summary for only 1 out of every 100 */
	if (to_us && (fc & ~IEEE80211_FCTL_PROTECTED) ==
	    (IEEE80211_FCTL_FROMDS | IEEE80211_FTYPE_DATA)) {
		dataframe = 1;
		if (!group100)
			print_summary = 1;	/* print each frame */
		else if (priv->framecnt_to_us < 100) {
			priv->framecnt_to_us++;
			print_summary = 0;
		} else {
			priv->framecnt_to_us = 0;
			print_summary = 1;
			hundred = 1;
		}
	} else {
		/* print summary for all other frames */
		print_summary = 1;
	}

	if (print_summary) {
		char *title;
		int rate_idx;
		u32 bitrate;

		if (hundred)
			title = "100Frames";
		else if (fc & IEEE80211_FCTL_RETRY)
			title = "Retry";
		else if (ieee80211_is_assoc_response(fc))
			title = "AscRsp";
		else if (ieee80211_is_reassoc_response(fc))
			title = "RasRsp";
		else if (ieee80211_is_probe_response(fc)) {
			title = "PrbRsp";
			print_dump = 1;	/* dump frame contents */
		} else if (ieee80211_is_beacon(fc)) {
			title = "Beacon";
			print_dump = 1;	/* dump frame contents */
		} else if (ieee80211_is_atim(fc))
			title = "ATIM";
		else if (ieee80211_is_auth(fc))
			title = "Auth";
		else if (ieee80211_is_deauth(fc))
			title = "DeAuth";
		else if (ieee80211_is_disassoc(fc))
			title = "DisAssoc";
		else
			title = "Frame";

		rate_idx = iwl4965_hwrate_to_plcp_idx(rate_sym);
		if (unlikely(rate_idx == -1))
			bitrate = 0;
		else
			bitrate = iwl4965_rates[rate_idx].ieee / 2;

		/* print frame summary.
		 * MAC addresses show just the last byte (for brevity),
		 *    but you can hack it to show more, if you'd like to. */
		if (dataframe)
			IWL_DEBUG_RX("%s: mhd=0x%04x, dst=0x%02x, "
				     "len=%u, rssi=%d, chnl=%d, rate=%u, \n",
				     title, fc, header->addr1[5],
				     length, rssi, channel, bitrate);
		else {
			/* src/dst addresses assume managed mode */
			IWL_DEBUG_RX("%s: 0x%04x, dst=0x%02x, "
				     "src=0x%02x, rssi=%u, tim=%lu usec, "
				     "phy=0x%02x, chnl=%d\n",
				     title, fc, header->addr1[5],
				     header->addr3[5], rssi,
				     tsf_low - priv->scan_start_tsf,
				     phy_flags, channel);
		}
	}
	if (print_dump)
		iwl_print_hex_dump(priv, IWL_DL_RX, data, length);
}
#else
static inline void iwl4965_dbg_report_frame(struct iwl_priv *priv,
					    struct iwl_rx_packet *pkt,
					    struct ieee80211_hdr *header,
					    int group100)
{
}
#endif



/* Called for REPLY_RX (legacy ABG frames), or
 * REPLY_RX_MPDU_CMD (HT high-throughput N frames). */
static void iwl4965_rx_reply_rx(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)
{
	struct ieee80211_hdr *header;
	struct ieee80211_rx_status rx_status;
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	/* Use phy data (Rx signal strength, etc.) contained within
	 *   this rx packet for legacy frames,
	 *   or phy data cached from REPLY_RX_PHY_CMD for HT frames. */
	int include_phy = (pkt->hdr.cmd == REPLY_RX);
	struct iwl4965_rx_phy_res *rx_start = (include_phy) ?
		(struct iwl4965_rx_phy_res *)&(pkt->u.raw[0]) :
		(struct iwl4965_rx_phy_res *)&priv->last_phy_res[1];
	__le32 *rx_end;
	unsigned int len = 0;
	u16 fc;
	u8 network_packet;

	rx_status.mactime = le64_to_cpu(rx_start->timestamp);
	rx_status.freq =
		ieee80211_channel_to_frequency(le16_to_cpu(rx_start->channel));
	rx_status.band = (rx_start->phy_flags & RX_RES_PHY_FLAGS_BAND_24_MSK) ?
				IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
	rx_status.rate_idx =
		iwl4965_hwrate_to_plcp_idx(le32_to_cpu(rx_start->rate_n_flags));
	if (rx_status.band == IEEE80211_BAND_5GHZ)
		rx_status.rate_idx -= IWL_FIRST_OFDM_RATE;

	rx_status.antenna = 0;
	rx_status.flag = 0;

	if ((unlikely(rx_start->cfg_phy_cnt > 20))) {
		IWL_DEBUG_DROP("dsp size out of range [0,20]: %d/n",
				rx_start->cfg_phy_cnt);
		return;
	}

	if (!include_phy) {
		if (priv->last_phy_res[0])
			rx_start = (struct iwl4965_rx_phy_res *)
				&priv->last_phy_res[1];
		else
			rx_start = NULL;
	}

	if (!rx_start) {
		IWL_ERROR("MPDU frame without a PHY data\n");
		return;
	}

	if (include_phy) {
		header = (struct ieee80211_hdr *)((u8 *) & rx_start[1]
						  + rx_start->cfg_phy_cnt);

		len = le16_to_cpu(rx_start->byte_count);
		rx_end = (__le32 *)(pkt->u.raw + rx_start->cfg_phy_cnt +
				  sizeof(struct iwl4965_rx_phy_res) + len);
	} else {
		struct iwl4965_rx_mpdu_res_start *amsdu =
			(struct iwl4965_rx_mpdu_res_start *)pkt->u.raw;

		header = (void *)(pkt->u.raw +
			sizeof(struct iwl4965_rx_mpdu_res_start));
		len = le16_to_cpu(amsdu->byte_count);
		rx_end = (__le32 *) (pkt->u.raw +
			sizeof(struct iwl4965_rx_mpdu_res_start) + len);
	}

	if (!(*rx_end & RX_RES_STATUS_NO_CRC32_ERROR) ||
	    !(*rx_end & RX_RES_STATUS_NO_RXE_OVERFLOW)) {
		IWL_DEBUG_RX("Bad CRC or FIFO: 0x%08X.\n",
				le32_to_cpu(*rx_end));
		return;
	}

	priv->ucode_beacon_time = le32_to_cpu(rx_start->beacon_time_stamp);

	/* Find max signal strength (dBm) among 3 antenna/receiver chains */
	rx_status.signal = iwl4965_calc_rssi(priv, rx_start);

	/* Meaningful noise values are available only from beacon statistics,
	 *   which are gathered only when associated, and indicate noise
	 *   only for the associated network channel ...
	 * Ignore these noise values while scanning (other channels) */
	if (iwl_is_associated(priv) &&
	    !test_bit(STATUS_SCANNING, &priv->status)) {
		rx_status.noise = priv->last_rx_noise;
		rx_status.qual = iwl4965_calc_sig_qual(rx_status.signal,
							 rx_status.noise);
	} else {
		rx_status.noise = IWL_NOISE_MEAS_NOT_AVAILABLE;
		rx_status.qual = iwl4965_calc_sig_qual(rx_status.signal, 0);
	}

	/* Reset beacon noise level if not associated. */
	if (!iwl_is_associated(priv))
		priv->last_rx_noise = IWL_NOISE_MEAS_NOT_AVAILABLE;

	/* Set "1" to report good data frames in groups of 100 */
	/* FIXME: need to optimze the call: */
	iwl4965_dbg_report_frame(priv, pkt, header, 1);

	IWL_DEBUG_STATS_LIMIT("Rssi %d, noise %d, qual %d, TSF %llu\n",
			      rx_status.signal, rx_status.noise, rx_status.signal,
			      (unsigned long long)rx_status.mactime);


	if (priv->iw_mode == IEEE80211_IF_TYPE_MNTR) {
		iwl4965_handle_data_packet(priv, 1, include_phy,
						 rxb, &rx_status);
		return;
	}

	network_packet = iwl4965_is_network_packet(priv, header);
	if (network_packet) {
		priv->last_rx_rssi = rx_status.signal;
		priv->last_beacon_time =  priv->ucode_beacon_time;
		priv->last_tsf = le64_to_cpu(rx_start->timestamp);
	}

	fc = le16_to_cpu(header->frame_control);
	switch (fc & IEEE80211_FCTL_FTYPE) {
	case IEEE80211_FTYPE_MGMT:
		if (priv->iw_mode == IEEE80211_IF_TYPE_AP)
			iwl4965_update_ps_mode(priv, fc  & IEEE80211_FCTL_PM,
						header->addr2);
		iwl4965_handle_data_packet(priv, 0, include_phy, rxb, &rx_status);
		break;

	case IEEE80211_FTYPE_CTL:
#ifdef CONFIG_IWL4965_HT
		switch (fc & IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_BACK_REQ:
			IWL_DEBUG_HT("IEEE80211_STYPE_BACK_REQ arrived\n");
			iwl4965_handle_data_packet(priv, 0, include_phy,
						rxb, &rx_status);
			break;
		default:
			break;
		}
#endif
		break;

	case IEEE80211_FTYPE_DATA: {
		DECLARE_MAC_BUF(mac1);
		DECLARE_MAC_BUF(mac2);
		DECLARE_MAC_BUF(mac3);

		if (priv->iw_mode == IEEE80211_IF_TYPE_AP)
			iwl4965_update_ps_mode(priv, fc  & IEEE80211_FCTL_PM,
						header->addr2);

		if (unlikely(!network_packet))
			IWL_DEBUG_DROP("Dropping (non network): "
				       "%s, %s, %s\n",
				       print_mac(mac1, header->addr1),
				       print_mac(mac2, header->addr2),
				       print_mac(mac3, header->addr3));
		else if (unlikely(iwl4965_is_duplicate_packet(priv, header)))
			IWL_DEBUG_DROP("Dropping (dup): %s, %s, %s\n",
				       print_mac(mac1, header->addr1),
				       print_mac(mac2, header->addr2),
				       print_mac(mac3, header->addr3));
		else
			iwl4965_handle_data_packet(priv, 1, include_phy, rxb,
						   &rx_status);
		break;
	}
	default:
		break;

	}
}

/* Cache phy data (Rx signal strength, etc) for HT frame (REPLY_RX_PHY_CMD).
 * This will be used later in iwl4965_rx_reply_rx() for REPLY_RX_MPDU_CMD. */
static void iwl4965_rx_reply_rx_phy(struct iwl_priv *priv,
				    struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	priv->last_phy_res[0] = 1;
	memcpy(&priv->last_phy_res[1], &(pkt->u.raw[0]),
	       sizeof(struct iwl4965_rx_phy_res));
}
static void iwl4965_rx_missed_beacon_notif(struct iwl_priv *priv,
					   struct iwl_rx_mem_buffer *rxb)

{
#ifdef CONFIG_IWL4965_RUN_TIME_CALIB
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl4965_missed_beacon_notif *missed_beacon;

	missed_beacon = &pkt->u.missed_beacon;
	if (le32_to_cpu(missed_beacon->consequtive_missed_beacons) > 5) {
		IWL_DEBUG_CALIB("missed bcn cnsq %d totl %d rcd %d expctd %d\n",
		    le32_to_cpu(missed_beacon->consequtive_missed_beacons),
		    le32_to_cpu(missed_beacon->total_missed_becons),
		    le32_to_cpu(missed_beacon->num_recvd_beacons),
		    le32_to_cpu(missed_beacon->num_expected_beacons));
		if (!test_bit(STATUS_SCANNING, &priv->status))
			iwl_init_sensitivity(priv);
	}
#endif /*CONFIG_IWL4965_RUN_TIME_CALIB*/
}
#ifdef CONFIG_IWL4965_HT

/**
 * iwl4965_sta_modify_enable_tid_tx - Enable Tx for this TID in station table
 */
static void iwl4965_sta_modify_enable_tid_tx(struct iwl_priv *priv,
					 int sta_id, int tid)
{
	unsigned long flags;

	/* Remove "disable" flag, to enable Tx for this TID */
	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_TID_DISABLE_TX;
	priv->stations[sta_id].sta.tid_disable_tx &= cpu_to_le16(~(1 << tid));
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	iwl_send_add_sta(priv, &priv->stations[sta_id].sta, CMD_ASYNC);
}

/**
 * iwl4965_tx_status_reply_compressed_ba - Update tx status from block-ack
 *
 * Go through block-ack's bitmap of ACK'd frames, update driver's record of
 * ACK vs. not.  This gets sent to mac80211, then to rate scaling algo.
 */
static int iwl4965_tx_status_reply_compressed_ba(struct iwl_priv *priv,
						 struct iwl_ht_agg *agg,
						 struct iwl4965_compressed_ba_resp*
						 ba_resp)

{
	int i, sh, ack;
	u16 seq_ctl = le16_to_cpu(ba_resp->seq_ctl);
	u16 scd_flow = le16_to_cpu(ba_resp->scd_flow);
	u64 bitmap;
	int successes = 0;
	struct ieee80211_tx_status *tx_status;

	if (unlikely(!agg->wait_for_ba))  {
		IWL_ERROR("Received BA when not expected\n");
		return -EINVAL;
	}

	/* Mark that the expected block-ack response arrived */
	agg->wait_for_ba = 0;
	IWL_DEBUG_TX_REPLY("BA %d %d\n", agg->start_idx, ba_resp->seq_ctl);

	/* Calculate shift to align block-ack bits with our Tx window bits */
	sh = agg->start_idx - SEQ_TO_INDEX(seq_ctl>>4);
	if (sh < 0) /* tbw something is wrong with indices */
		sh += 0x100;

	/* don't use 64-bit values for now */
	bitmap = le64_to_cpu(ba_resp->bitmap) >> sh;

	if (agg->frame_count > (64 - sh)) {
		IWL_DEBUG_TX_REPLY("more frames than bitmap size");
		return -1;
	}

	/* check for success or failure according to the
	 * transmitted bitmap and block-ack bitmap */
	bitmap &= agg->bitmap;

	/* For each frame attempted in aggregation,
	 * update driver's record of tx frame's status. */
	for (i = 0; i < agg->frame_count ; i++) {
		ack = bitmap & (1 << i);
		successes += !!ack;
		IWL_DEBUG_TX_REPLY("%s ON i=%d idx=%d raw=%d\n",
			ack? "ACK":"NACK", i, (agg->start_idx + i) & 0xff,
			agg->start_idx + i);
	}

	tx_status = &priv->txq[scd_flow].txb[agg->start_idx].status;
	tx_status->flags = IEEE80211_TX_STATUS_ACK;
	tx_status->flags |= IEEE80211_TX_STATUS_AMPDU;
	tx_status->ampdu_ack_map = successes;
	tx_status->ampdu_ack_len = agg->frame_count;
	iwl4965_hwrate_to_tx_control(priv, agg->rate_n_flags,
				     &tx_status->control);

	IWL_DEBUG_TX_REPLY("Bitmap %llx\n", (unsigned long long)bitmap);

	return 0;
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
 * txq_id must be greater than IWL_BACK_QUEUE_FIRST_ID
 * priv->lock must be held by the caller
 */
static int iwl4965_tx_queue_agg_disable(struct iwl_priv *priv, u16 txq_id,
					u16 ssn_idx, u8 tx_fifo)
{
	int ret = 0;

	if (IWL_BACK_QUEUE_FIRST_ID > txq_id) {
		IWL_WARNING("queue number too small: %d, must be > %d\n",
				txq_id, IWL_BACK_QUEUE_FIRST_ID);
		return -EINVAL;
	}

	ret = iwl_grab_nic_access(priv);
	if (ret)
		return ret;

	iwl4965_tx_queue_stop_scheduler(priv, txq_id);

	iwl_clear_bits_prph(priv, IWL49_SCD_QUEUECHAIN_SEL, (1 << txq_id));

	priv->txq[txq_id].q.read_ptr = (ssn_idx & 0xff);
	priv->txq[txq_id].q.write_ptr = (ssn_idx & 0xff);
	/* supposes that ssn_idx is valid (!= 0xFFF) */
	iwl4965_set_wr_ptrs(priv, txq_id, ssn_idx);

	iwl_clear_bits_prph(priv, IWL49_SCD_INTERRUPT_MASK, (1 << txq_id));
	iwl4965_txq_ctx_deactivate(priv, txq_id);
	iwl4965_tx_queue_set_status(priv, &priv->txq[txq_id], tx_fifo, 0);

	iwl_release_nic_access(priv);

	return 0;
}

int iwl4965_check_empty_hw_queue(struct iwl_priv *priv, int sta_id,
					 u8 tid, int txq_id)
{
	struct iwl4965_queue *q = &priv->txq[txq_id].q;
	u8 *addr = priv->stations[sta_id].sta.sta.addr;
	struct iwl_tid_data *tid_data = &priv->stations[sta_id].tid[tid];

	switch (priv->stations[sta_id].tid[tid].agg.state) {
	case IWL_EMPTYING_HW_QUEUE_DELBA:
		/* We are reclaiming the last packet of the */
		/* aggregated HW queue */
		if (txq_id  == tid_data->agg.txq_id &&
		    q->read_ptr == q->write_ptr) {
			u16 ssn = SEQ_TO_SN(tid_data->seq_number);
			int tx_fifo = default_tid_to_tx_fifo[tid];
			IWL_DEBUG_HT("HW queue empty: continue DELBA flow\n");
			iwl4965_tx_queue_agg_disable(priv, txq_id,
						     ssn, tx_fifo);
			tid_data->agg.state = IWL_AGG_OFF;
			ieee80211_stop_tx_ba_cb_irqsafe(priv->hw, addr, tid);
		}
		break;
	case IWL_EMPTYING_HW_QUEUE_ADDBA:
		/* We are reclaiming the last packet of the queue */
		if (tid_data->tfds_in_queue == 0) {
			IWL_DEBUG_HT("HW queue empty: continue ADDBA flow\n");
			tid_data->agg.state = IWL_AGG_ON;
			ieee80211_start_tx_ba_cb_irqsafe(priv->hw, addr, tid);
		}
		break;
	}
	return 0;
}

/**
 * iwl4965_queue_dec_wrap - Decrement queue index, wrap back to end if needed
 * @index -- current index
 * @n_bd -- total number of entries in queue (s/b power of 2)
 */
static inline int iwl4965_queue_dec_wrap(int index, int n_bd)
{
	return (index == 0) ? n_bd - 1 : index - 1;
}

/**
 * iwl4965_rx_reply_compressed_ba - Handler for REPLY_COMPRESSED_BA
 *
 * Handles block-acknowledge notification from device, which reports success
 * of frames sent via aggregation.
 */
static void iwl4965_rx_reply_compressed_ba(struct iwl_priv *priv,
					   struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl4965_compressed_ba_resp *ba_resp = &pkt->u.compressed_ba;
	int index;
	struct iwl_tx_queue *txq = NULL;
	struct iwl_ht_agg *agg;
	DECLARE_MAC_BUF(mac);

	/* "flow" corresponds to Tx queue */
	u16 scd_flow = le16_to_cpu(ba_resp->scd_flow);

	/* "ssn" is start of block-ack Tx window, corresponds to index
	 * (in Tx queue's circular buffer) of first TFD/frame in window */
	u16 ba_resp_scd_ssn = le16_to_cpu(ba_resp->scd_ssn);

	if (scd_flow >= priv->hw_params.max_txq_num) {
		IWL_ERROR("BUG_ON scd_flow is bigger than number of queues");
		return;
	}

	txq = &priv->txq[scd_flow];
	agg = &priv->stations[ba_resp->sta_id].tid[ba_resp->tid].agg;

	/* Find index just before block-ack window */
	index = iwl4965_queue_dec_wrap(ba_resp_scd_ssn & 0xff, txq->q.n_bd);

	/* TODO: Need to get this copy more safely - now good for debug */

	IWL_DEBUG_TX_REPLY("REPLY_COMPRESSED_BA [%d]Received from %s, "
			   "sta_id = %d\n",
			   agg->wait_for_ba,
			   print_mac(mac, (u8*) &ba_resp->sta_addr_lo32),
			   ba_resp->sta_id);
	IWL_DEBUG_TX_REPLY("TID = %d, SeqCtl = %d, bitmap = 0x%llx, scd_flow = "
			   "%d, scd_ssn = %d\n",
			   ba_resp->tid,
			   ba_resp->seq_ctl,
			   (unsigned long long)le64_to_cpu(ba_resp->bitmap),
			   ba_resp->scd_flow,
			   ba_resp->scd_ssn);
	IWL_DEBUG_TX_REPLY("DAT start_idx = %d, bitmap = 0x%llx \n",
			   agg->start_idx,
			   (unsigned long long)agg->bitmap);

	/* Update driver's record of ACK vs. not for each frame in window */
	iwl4965_tx_status_reply_compressed_ba(priv, agg, ba_resp);

	/* Release all TFDs before the SSN, i.e. all TFDs in front of
	 * block-ack window (we assume that they've been successfully
	 * transmitted ... if not, it's too late anyway). */
	if (txq->q.read_ptr != (ba_resp_scd_ssn & 0xff)) {
		/* calculate mac80211 ampdu sw queue to wake */
		int ampdu_q =
		   scd_flow - IWL_BACK_QUEUE_FIRST_ID + priv->hw->queues;
		int freed = iwl4965_tx_queue_reclaim(priv, scd_flow, index);
		priv->stations[ba_resp->sta_id].
			tid[ba_resp->tid].tfds_in_queue -= freed;
		if (iwl4965_queue_space(&txq->q) > txq->q.low_mark &&
			priv->mac80211_registered &&
			agg->state != IWL_EMPTYING_HW_QUEUE_DELBA)
			ieee80211_wake_queue(priv->hw, ampdu_q);
		iwl4965_check_empty_hw_queue(priv, ba_resp->sta_id,
			ba_resp->tid, scd_flow);
	}
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

	scd_q2ratid = ra_tid & IWL49_SCD_QUEUE_RA_TID_MAP_RATID_MSK;

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
 * NOTE:  txq_id must be greater than IWL_BACK_QUEUE_FIRST_ID,
 *        i.e. it must be one of the higher queues used for aggregation
 */
static int iwl4965_tx_queue_agg_enable(struct iwl_priv *priv, int txq_id,
				       int tx_fifo, int sta_id, int tid,
				       u16 ssn_idx)
{
	unsigned long flags;
	int rc;
	u16 ra_tid;

	if (IWL_BACK_QUEUE_FIRST_ID > txq_id)
		IWL_WARNING("queue number too small: %d, must be > %d\n",
			txq_id, IWL_BACK_QUEUE_FIRST_ID);

	ra_tid = BUILD_RAxTID(sta_id, tid);

	/* Modify device's station table to Tx this TID */
	iwl4965_sta_modify_enable_tid_tx(priv, sta_id, tid);

	spin_lock_irqsave(&priv->lock, flags);
	rc = iwl_grab_nic_access(priv);
	if (rc) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return rc;
	}

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

	iwl_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

#endif /* CONFIG_IWL4965_HT */

/**
 * iwl4965_add_station - Initialize a station's hardware rate table
 *
 * The uCode's station table contains a table of fallback rates
 * for automatic fallback during transmission.
 *
 * NOTE: This sets up a default set of values.  These will be replaced later
 *       if the driver's iwl-4965-rs rate scaling algorithm is used, instead of
 *       rc80211_simple.
 *
 * NOTE: Run REPLY_ADD_STA command to set up station table entry, before
 *       calling this function (which runs REPLY_TX_LINK_QUALITY_CMD,
 *       which requires station table entry to exist).
 */
void iwl4965_add_station(struct iwl_priv *priv, const u8 *addr, int is_ap)
{
	int i, r;
	struct iwl_link_quality_cmd link_cmd = {
		.reserved1 = 0,
	};
	u16 rate_flags;

	/* Set up the rate scaling to start at selected rate, fall back
	 * all the way down to 1M in IEEE order, and then spin on 1M */
	if (is_ap)
		r = IWL_RATE_54M_INDEX;
	else if (priv->band == IEEE80211_BAND_5GHZ)
		r = IWL_RATE_6M_INDEX;
	else
		r = IWL_RATE_1M_INDEX;

	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++) {
		rate_flags = 0;
		if (r >= IWL_FIRST_CCK_RATE && r <= IWL_LAST_CCK_RATE)
			rate_flags |= RATE_MCS_CCK_MSK;

		/* Use Tx antenna B only */
		rate_flags |= RATE_MCS_ANT_B_MSK; /*FIXME:RS*/

		link_cmd.rs_table[i].rate_n_flags =
			iwl4965_hw_set_rate_n_flags(iwl4965_rates[r].plcp, rate_flags);
		r = iwl4965_get_prev_ieee_rate(r);
	}

	link_cmd.general_params.single_stream_ant_msk = 2;
	link_cmd.general_params.dual_stream_ant_msk = 3;
	link_cmd.agg_params.agg_dis_start_th = 3;
	link_cmd.agg_params.agg_time_limit = cpu_to_le16(4000);

	/* Update the rate scaling for control frame Tx to AP */
	link_cmd.sta_id = is_ap ? IWL_AP_ID : priv->hw_params.bcast_sta_id;

	iwl_send_cmd_pdu_async(priv, REPLY_TX_LINK_QUALITY_CMD,
			       sizeof(link_cmd), &link_cmd, NULL);
}

#ifdef CONFIG_IWL4965_HT

void iwl4965_set_ht_add_station(struct iwl_priv *priv, u8 index,
				struct ieee80211_ht_info *sta_ht_inf)
{
	__le32 sta_flags;
	u8 mimo_ps_mode;

	if (!sta_ht_inf || !sta_ht_inf->ht_supported)
		goto done;

	mimo_ps_mode = (sta_ht_inf->cap & IEEE80211_HT_CAP_MIMO_PS) >> 2;

	sta_flags = priv->stations[index].sta.station_flags;

	sta_flags &= ~(STA_FLG_RTS_MIMO_PROT_MSK | STA_FLG_MIMO_DIS_MSK);

	switch (mimo_ps_mode) {
	case WLAN_HT_CAP_MIMO_PS_STATIC:
		sta_flags |= STA_FLG_MIMO_DIS_MSK;
		break;
	case WLAN_HT_CAP_MIMO_PS_DYNAMIC:
		sta_flags |= STA_FLG_RTS_MIMO_PROT_MSK;
		break;
	case WLAN_HT_CAP_MIMO_PS_DISABLED:
		break;
	default:
		IWL_WARNING("Invalid MIMO PS mode %d", mimo_ps_mode);
		break;
	}

	sta_flags |= cpu_to_le32(
	      (u32)sta_ht_inf->ampdu_factor << STA_FLG_MAX_AGG_SIZE_POS);

	sta_flags |= cpu_to_le32(
	      (u32)sta_ht_inf->ampdu_density << STA_FLG_AGG_MPDU_DENSITY_POS);

	if (iwl_is_fat_tx_allowed(priv, sta_ht_inf))
		sta_flags |= STA_FLG_FAT_EN_MSK;
	else
		sta_flags &= ~STA_FLG_FAT_EN_MSK;

	priv->stations[index].sta.station_flags = sta_flags;
 done:
	return;
}

static int iwl4965_rx_agg_start(struct iwl_priv *priv,
				const u8 *addr, int tid, u16 ssn)
{
	unsigned long flags;
	int sta_id;

	sta_id = iwl_find_station(priv, addr);
	if (sta_id == IWL_INVALID_STATION)
		return -ENXIO;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags_msk = 0;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_ADDBA_TID_MSK;
	priv->stations[sta_id].sta.add_immediate_ba_tid = (u8)tid;
	priv->stations[sta_id].sta.add_immediate_ba_ssn = cpu_to_le16(ssn);
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &priv->stations[sta_id].sta,
					CMD_ASYNC);
}

static int iwl4965_rx_agg_stop(struct iwl_priv *priv,
			       const u8 *addr, int tid)
{
	unsigned long flags;
	int sta_id;

	sta_id = iwl_find_station(priv, addr);
	if (sta_id == IWL_INVALID_STATION)
		return -ENXIO;

	spin_lock_irqsave(&priv->sta_lock, flags);
	priv->stations[sta_id].sta.station_flags_msk = 0;
	priv->stations[sta_id].sta.sta.modify_mask = STA_MODIFY_DELBA_TID_MSK;
	priv->stations[sta_id].sta.remove_immediate_ba_tid = (u8)tid;
	priv->stations[sta_id].sta.mode = STA_CONTROL_MODIFY_MSK;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	return iwl_send_add_sta(priv, &priv->stations[sta_id].sta,
					CMD_ASYNC);
}

/*
 * Find first available (lowest unused) Tx Queue, mark it "active".
 * Called only when finding queue for aggregation.
 * Should never return anything < 7, because they should already
 * be in use as EDCA AC (0-3), Command (4), HCCA (5, 6).
 */
static int iwl4965_txq_ctx_activate_free(struct iwl_priv *priv)
{
	int txq_id;

	for (txq_id = 0; txq_id < priv->hw_params.max_txq_num; txq_id++)
		if (!test_and_set_bit(txq_id, &priv->txq_ctx_active_msk))
			return txq_id;
	return -1;
}

static int iwl4965_tx_agg_start(struct ieee80211_hw *hw, const u8 *ra,
				u16 tid, u16 *start_seq_num)
{
	struct iwl_priv *priv = hw->priv;
	int sta_id;
	int tx_fifo;
	int txq_id;
	int ssn = -1;
	int ret = 0;
	unsigned long flags;
	struct iwl_tid_data *tid_data;
	DECLARE_MAC_BUF(mac);

	if (likely(tid < ARRAY_SIZE(default_tid_to_tx_fifo)))
		tx_fifo = default_tid_to_tx_fifo[tid];
	else
		return -EINVAL;

	IWL_WARNING("%s on ra = %s tid = %d\n",
			__func__, print_mac(mac, ra), tid);

	sta_id = iwl_find_station(priv, ra);
	if (sta_id == IWL_INVALID_STATION)
		return -ENXIO;

	if (priv->stations[sta_id].tid[tid].agg.state != IWL_AGG_OFF) {
		IWL_ERROR("Start AGG when state is not IWL_AGG_OFF !\n");
		return -ENXIO;
	}

	txq_id = iwl4965_txq_ctx_activate_free(priv);
	if (txq_id == -1)
		return -ENXIO;

	spin_lock_irqsave(&priv->sta_lock, flags);
	tid_data = &priv->stations[sta_id].tid[tid];
	ssn = SEQ_TO_SN(tid_data->seq_number);
	tid_data->agg.txq_id = txq_id;
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	*start_seq_num = ssn;
	ret = iwl4965_tx_queue_agg_enable(priv, txq_id, tx_fifo,
					  sta_id, tid, ssn);
	if (ret)
		return ret;

	ret = 0;
	if (tid_data->tfds_in_queue == 0) {
		printk(KERN_ERR "HW queue is empty\n");
		tid_data->agg.state = IWL_AGG_ON;
		ieee80211_start_tx_ba_cb_irqsafe(hw, ra, tid);
	} else {
		IWL_DEBUG_HT("HW queue is NOT empty: %d packets in HW queue\n",
				tid_data->tfds_in_queue);
		tid_data->agg.state = IWL_EMPTYING_HW_QUEUE_ADDBA;
	}
	return ret;
}

static int iwl4965_tx_agg_stop(struct ieee80211_hw *hw, const u8 *ra, u16 tid)
{
	struct iwl_priv *priv = hw->priv;
	int tx_fifo_id, txq_id, sta_id, ssn = -1;
	struct iwl_tid_data *tid_data;
	int ret, write_ptr, read_ptr;
	unsigned long flags;
	DECLARE_MAC_BUF(mac);

	if (!ra) {
		IWL_ERROR("ra = NULL\n");
		return -EINVAL;
	}

	if (likely(tid < ARRAY_SIZE(default_tid_to_tx_fifo)))
		tx_fifo_id = default_tid_to_tx_fifo[tid];
	else
		return -EINVAL;

	sta_id = iwl_find_station(priv, ra);

	if (sta_id == IWL_INVALID_STATION)
		return -ENXIO;

	if (priv->stations[sta_id].tid[tid].agg.state != IWL_AGG_ON)
		IWL_WARNING("Stopping AGG while state not IWL_AGG_ON\n");

	tid_data = &priv->stations[sta_id].tid[tid];
	ssn = (tid_data->seq_number & IEEE80211_SCTL_SEQ) >> 4;
	txq_id = tid_data->agg.txq_id;
	write_ptr = priv->txq[txq_id].q.write_ptr;
	read_ptr = priv->txq[txq_id].q.read_ptr;

	/* The queue is not empty */
	if (write_ptr != read_ptr) {
		IWL_DEBUG_HT("Stopping a non empty AGG HW QUEUE\n");
		priv->stations[sta_id].tid[tid].agg.state =
				IWL_EMPTYING_HW_QUEUE_DELBA;
		return 0;
	}

	IWL_DEBUG_HT("HW queue is empty\n");
	priv->stations[sta_id].tid[tid].agg.state = IWL_AGG_OFF;

	spin_lock_irqsave(&priv->lock, flags);
	ret = iwl4965_tx_queue_agg_disable(priv, txq_id, ssn, tx_fifo_id);
	spin_unlock_irqrestore(&priv->lock, flags);

	if (ret)
		return ret;

	ieee80211_stop_tx_ba_cb_irqsafe(priv->hw, ra, tid);

	return 0;
}

int iwl4965_mac_ampdu_action(struct ieee80211_hw *hw,
			     enum ieee80211_ampdu_mlme_action action,
			     const u8 *addr, u16 tid, u16 *ssn)
{
	struct iwl_priv *priv = hw->priv;
	DECLARE_MAC_BUF(mac);

	IWL_DEBUG_HT("A-MPDU action on addr %s tid %d\n",
		     print_mac(mac, addr), tid);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		IWL_DEBUG_HT("start Rx\n");
		return iwl4965_rx_agg_start(priv, addr, tid, *ssn);
	case IEEE80211_AMPDU_RX_STOP:
		IWL_DEBUG_HT("stop Rx\n");
		return iwl4965_rx_agg_stop(priv, addr, tid);
	case IEEE80211_AMPDU_TX_START:
		IWL_DEBUG_HT("start Tx\n");
		return iwl4965_tx_agg_start(hw, addr, tid, ssn);
	case IEEE80211_AMPDU_TX_STOP:
		IWL_DEBUG_HT("stop Tx\n");
		return iwl4965_tx_agg_stop(hw, addr, tid);
	default:
		IWL_DEBUG_HT("unknown\n");
		return -EINVAL;
		break;
	}
	return 0;
}
#endif /* CONFIG_IWL4965_HT */


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
	addsta->reserved1 = __constant_cpu_to_le16(0);
	addsta->reserved2 = __constant_cpu_to_le32(0);

	return (u16)sizeof(struct iwl4965_addsta_cmd);
}
/* Set up 4965-specific Rx frame reply handlers */
static void iwl4965_rx_handler_setup(struct iwl_priv *priv)
{
	/* Legacy Rx frames */
	priv->rx_handlers[REPLY_RX] = iwl4965_rx_reply_rx;

	/* High-throughput (HT) Rx frames */
	priv->rx_handlers[REPLY_RX_PHY_CMD] = iwl4965_rx_reply_rx_phy;
	priv->rx_handlers[REPLY_RX_MPDU_CMD] = iwl4965_rx_reply_rx;

	priv->rx_handlers[MISSED_BEACONS_NOTIFICATION] =
	    iwl4965_rx_missed_beacon_notif;

#ifdef CONFIG_IWL4965_HT
	priv->rx_handlers[REPLY_COMPRESSED_BA] = iwl4965_rx_reply_compressed_ba;
#endif /* CONFIG_IWL4965_HT */
}

void iwl4965_hw_setup_deferred_work(struct iwl_priv *priv)
{
	INIT_WORK(&priv->txpower_work, iwl4965_bg_txpower_work);
#ifdef CONFIG_IWL4965_RUN_TIME_CALIB
	INIT_WORK(&priv->sensitivity_work, iwl4965_bg_sensitivity_work);
#endif
	init_timer(&priv->statistics_periodic);
	priv->statistics_periodic.data = (unsigned long)priv;
	priv->statistics_periodic.function = iwl4965_bg_statistics_periodic;
}

void iwl4965_hw_cancel_deferred_work(struct iwl_priv *priv)
{
	del_timer_sync(&priv->statistics_periodic);

	cancel_delayed_work(&priv->init_alive_start);
}


static struct iwl_hcmd_ops iwl4965_hcmd = {
	.rxon_assoc = iwl4965_send_rxon_assoc,
};

static struct iwl_hcmd_utils_ops iwl4965_hcmd_utils = {
	.enqueue_hcmd = iwl4965_enqueue_hcmd,
	.build_addsta_hcmd = iwl4965_build_addsta_hcmd,
#ifdef CONFIG_IWL4965_RUN_TIME_CALIB
	.chain_noise_reset = iwl4965_chain_noise_reset,
	.gain_computation = iwl4965_gain_computation,
#endif
};

static struct iwl_lib_ops iwl4965_lib = {
	.set_hw_params = iwl4965_hw_set_hw_params,
	.alloc_shared_mem = iwl4965_alloc_shared_mem,
	.free_shared_mem = iwl4965_free_shared_mem,
	.shared_mem_rx_idx = iwl4965_shared_mem_rx_idx,
	.txq_update_byte_cnt_tbl = iwl4965_txq_update_byte_cnt_tbl,
	.disable_tx_fifo = iwl4965_disable_tx_fifo,
	.rx_handler_setup = iwl4965_rx_handler_setup,
	.is_valid_rtc_data_addr = iwl4965_hw_valid_rtc_data_addr,
	.alive_notify = iwl4965_alive_notify,
	.init_alive_start = iwl4965_init_alive_start,
	.load_ucode = iwl4965_load_bsm,
	.apm_ops = {
		.init = iwl4965_apm_init,
		.config = iwl4965_nic_config,
		.set_pwr_src = iwl4965_set_pwr_src,
	},
	.eeprom_ops = {
		.regulatory_bands = {
			EEPROM_REGULATORY_BAND_1_CHANNELS,
			EEPROM_REGULATORY_BAND_2_CHANNELS,
			EEPROM_REGULATORY_BAND_3_CHANNELS,
			EEPROM_REGULATORY_BAND_4_CHANNELS,
			EEPROM_REGULATORY_BAND_5_CHANNELS,
			EEPROM_4965_REGULATORY_BAND_24_FAT_CHANNELS,
			EEPROM_4965_REGULATORY_BAND_52_FAT_CHANNELS
		},
		.verify_signature  = iwlcore_eeprom_verify_signature,
		.acquire_semaphore = iwlcore_eeprom_acquire_semaphore,
		.release_semaphore = iwlcore_eeprom_release_semaphore,
		.check_version = iwl4965_eeprom_check_version,
		.query_addr = iwlcore_eeprom_query_addr,
	},
	.radio_kill_sw = iwl4965_radio_kill_sw,
	.set_power = iwl4965_set_power,
	.update_chain_flags = iwl4965_update_chain_flags,
};

static struct iwl_ops iwl4965_ops = {
	.lib = &iwl4965_lib,
	.hcmd = &iwl4965_hcmd,
	.utils = &iwl4965_hcmd_utils,
};

struct iwl_cfg iwl4965_agn_cfg = {
	.name = "4965AGN",
	.fw_name = "iwlwifi-4965" IWL4965_UCODE_API ".ucode",
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.eeprom_size = IWL4965_EEPROM_IMG_SIZE,
	.ops = &iwl4965_ops,
	.mod_params = &iwl4965_mod_params,
};

module_param_named(antenna, iwl4965_mod_params.antenna, int, 0444);
MODULE_PARM_DESC(antenna, "select antenna (1=Main, 2=Aux, default 0 [both])");
module_param_named(disable, iwl4965_mod_params.disable, int, 0444);
MODULE_PARM_DESC(disable, "manually disable the radio (default 0 [radio on])");
module_param_named(swcrypto, iwl4965_mod_params.sw_crypto, int, 0444);
MODULE_PARM_DESC(swcrypto, "using crypto in software (default 0 [hardware])\n");
module_param_named(debug, iwl4965_mod_params.debug, int, 0444);
MODULE_PARM_DESC(debug, "debug output mask");
module_param_named(
	disable_hw_scan, iwl4965_mod_params.disable_hw_scan, int, 0444);
MODULE_PARM_DESC(disable_hw_scan, "disable hardware scanning (default 0)");

module_param_named(queues_num, iwl4965_mod_params.num_of_queues, int, 0444);
MODULE_PARM_DESC(queues_num, "number of hw queues.");

/* QoS */
module_param_named(qos_enable, iwl4965_mod_params.enable_qos, int, 0444);
MODULE_PARM_DESC(qos_enable, "enable all QoS functionality");
module_param_named(amsdu_size_8K, iwl4965_mod_params.amsdu_size_8K, int, 0444);
MODULE_PARM_DESC(amsdu_size_8K, "enable 8K amsdu size");
module_param_named(fw_restart4965, iwl4965_mod_params.restart_fw, int, 0444);
MODULE_PARM_DESC(fw_restart4965, "restart firmware in case of error");
