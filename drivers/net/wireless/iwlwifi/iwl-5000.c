/******************************************************************************
 *
 * Copyright(c) 2007-2008 Intel Corporation. All rights reserved.
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
#include "iwl-5000-hw.h"

#define IWL5000_UCODE_API  "-1"

static int iwl5000_apm_init(struct iwl_priv *priv)
{
	int ret = 0;

	iwl_set_bit(priv, CSR_GIO_CHICKEN_BITS,
		    CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER);

	iwl_set_bit(priv, CSR_ANA_PLL_CFG, CSR50_ANA_PLL_CFG_VAL);

	/* set "initialization complete" bit to move adapter
	 * D0U* --> D0A* state */
	iwl_set_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/* wait for clock stabilization */
	ret = iwl_poll_bit(priv, CSR_GP_CNTRL,
			  CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			  CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);
	if (ret < 0) {
		IWL_DEBUG_INFO("Failed to init the card\n");
		return ret;
	}

	ret = iwl_grab_nic_access(priv);
	if (ret)
		return ret;

	/* enable DMA */
	iwl_write_prph(priv, APMG_CLK_EN_REG,
			APMG_CLK_VAL_DMA_CLK_RQT);

	udelay(20);

	iwl_set_bits_prph(priv, APMG_PCIDEV_STT_REG,
			APMG_PCIDEV_STT_VAL_L1_ACT_DIS);

	iwl_release_nic_access(priv);

	return ret;
}

static void iwl5000_nic_config(struct iwl_priv *priv)
{
	unsigned long flags;
	u16 radio_cfg;
	u8 val_link;

	spin_lock_irqsave(&priv->lock, flags);

	pci_read_config_byte(priv->pci_dev, PCI_LINK_CTRL, &val_link);

	/* disable L1 entry -- workaround for pre-B1 */
	pci_write_config_byte(priv->pci_dev, PCI_LINK_CTRL, val_link & ~0x02);

	radio_cfg = iwl_eeprom_query16(priv, EEPROM_RADIO_CONFIG);

	/* write radio config values to register */
	if (EEPROM_RF_CFG_TYPE_MSK(radio_cfg) < EEPROM_5000_RF_CFG_TYPE_MAX)
		iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			    EEPROM_RF_CFG_TYPE_MSK(radio_cfg) |
			    EEPROM_RF_CFG_STEP_MSK(radio_cfg) |
			    EEPROM_RF_CFG_DASH_MSK(radio_cfg));

	/* set CSR_HW_CONFIG_REG for uCode use */
	iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
		    CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
		    CSR_HW_IF_CONFIG_REG_BIT_MAC_SI);

	spin_unlock_irqrestore(&priv->lock, flags);
}



/*
 * EEPROM
 */
static u32 eeprom_indirect_address(const struct iwl_priv *priv, u32 address)
{
	u16 offset = 0;

	if ((address & INDIRECT_ADDRESS) == 0)
		return address;

	switch (address & INDIRECT_TYPE_MSK) {
	case INDIRECT_HOST:
		offset = iwl_eeprom_query16(priv, EEPROM_5000_LINK_HOST);
		break;
	case INDIRECT_GENERAL:
		offset = iwl_eeprom_query16(priv, EEPROM_5000_LINK_GENERAL);
		break;
	case INDIRECT_REGULATORY:
		offset = iwl_eeprom_query16(priv, EEPROM_5000_LINK_REGULATORY);
		break;
	case INDIRECT_CALIBRATION:
		offset = iwl_eeprom_query16(priv, EEPROM_5000_LINK_CALIBRATION);
		break;
	case INDIRECT_PROCESS_ADJST:
		offset = iwl_eeprom_query16(priv, EEPROM_5000_LINK_PROCESS_ADJST);
		break;
	case INDIRECT_OTHERS:
		offset = iwl_eeprom_query16(priv, EEPROM_5000_LINK_OTHERS);
		break;
	default:
		IWL_ERROR("illegal indirect type: 0x%X\n",
		address & INDIRECT_TYPE_MSK);
		break;
	}

	/* translate the offset from words to byte */
	return (address & ADDRESS_MSK) + (offset << 1);
}

static int iwl5000_eeprom_check_version(struct iwl_priv *priv)
{
	u16 eeprom_ver;
	struct iwl_eeprom_calib_hdr {
		u8 version;
		u8 pa_type;
		u16 voltage;
	} *hdr;

	eeprom_ver = iwl_eeprom_query16(priv, EEPROM_VERSION);

	hdr = (struct iwl_eeprom_calib_hdr *)iwl_eeprom_query_addr(priv,
							EEPROM_5000_CALIB_ALL);

	if (eeprom_ver < EEPROM_5000_EEPROM_VERSION ||
	    hdr->version < EEPROM_5000_TX_POWER_VERSION)
		goto err;

	return 0;
err:
	IWL_ERROR("Unsuported EEPROM VER=0x%x < 0x%x CALIB=0x%x < 0x%x\n",
		  eeprom_ver, EEPROM_5000_EEPROM_VERSION,
		  hdr->version, EEPROM_5000_TX_POWER_VERSION);
	return -EINVAL;

}

#ifdef CONFIG_IWL5000_RUN_TIME_CALIB

static void iwl5000_gain_computation(struct iwl_priv *priv,
		u32 average_noise[NUM_RX_CHAINS],
		u16 min_average_noise_antenna_i,
		u32 min_average_noise)
{
	int i;
	s32 delta_g;
	struct iwl_chain_noise_data *data = &priv->chain_noise_data;

	/* Find Gain Code for the antennas B and C */
	for (i = 1; i < NUM_RX_CHAINS; i++) {
		if ((data->disconn_array[i])) {
			data->delta_gain_code[i] = 0;
			continue;
		}
		delta_g = (1000 * ((s32)average_noise[0] -
			(s32)average_noise[i])) / 1500;
		/* bound gain by 2 bits value max, 3rd bit is sign */
		data->delta_gain_code[i] =
			min(abs(delta_g), CHAIN_NOISE_MAX_DELTA_GAIN_CODE);

		if (delta_g < 0)
			/* set negative sign */
			data->delta_gain_code[i] |= (1 << 2);
	}

	IWL_DEBUG_CALIB("Delta gains: ANT_B = %d  ANT_C = %d\n",
			data->delta_gain_code[1], data->delta_gain_code[2]);

	if (!data->radio_write) {
		struct iwl5000_calibration_chain_noise_gain_cmd cmd;
		memset(&cmd, 0, sizeof(cmd));

		cmd.op_code = IWL5000_PHY_CALIBRATE_CHAIN_NOISE_GAIN_CMD;
		cmd.delta_gain_1 = data->delta_gain_code[1];
		cmd.delta_gain_2 = data->delta_gain_code[2];
		iwl_send_cmd_pdu_async(priv, REPLY_PHY_CALIBRATION_CMD,
			sizeof(cmd), &cmd, NULL);

		data->radio_write = 1;
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


static void iwl5000_chain_noise_reset(struct iwl_priv *priv)
{
	struct iwl_chain_noise_data *data = &priv->chain_noise_data;

	if ((data->state == IWL_CHAIN_NOISE_ALIVE) && iwl_is_associated(priv)) {
		struct iwl5000_calibration_chain_noise_reset_cmd cmd;

		memset(&cmd, 0, sizeof(cmd));
		cmd.op_code = IWL5000_PHY_CALIBRATE_CHAIN_NOISE_RESET_CMD;
		if (iwl_send_cmd_pdu(priv, REPLY_PHY_CALIBRATION_CMD,
			sizeof(cmd), &cmd))
			IWL_ERROR("Could not send REPLY_PHY_CALIBRATION_CMD\n");
		data->state = IWL_CHAIN_NOISE_ACCUMULATE;
		IWL_DEBUG_CALIB("Run chain_noise_calibrate\n");
	}
}

static struct iwl_sensitivity_ranges iwl5000_sensitivity = {
	.min_nrg_cck = 95,
	.max_nrg_cck = 0,
	.auto_corr_min_ofdm = 90,
	.auto_corr_min_ofdm_mrc = 170,
	.auto_corr_min_ofdm_x1 = 120,
	.auto_corr_min_ofdm_mrc_x1 = 240,

	.auto_corr_max_ofdm = 120,
	.auto_corr_max_ofdm_mrc = 210,
	.auto_corr_max_ofdm_x1 = 155,
	.auto_corr_max_ofdm_mrc_x1 = 290,

	.auto_corr_min_cck = 125,
	.auto_corr_max_cck = 200,
	.auto_corr_min_cck_mrc = 170,
	.auto_corr_max_cck_mrc = 400,
	.nrg_th_cck = 95,
	.nrg_th_ofdm = 95,
};

#endif /* CONFIG_IWL5000_RUN_TIME_CALIB */

static const u8 *iwl5000_eeprom_query_addr(const struct iwl_priv *priv,
					   size_t offset)
{
	u32 address = eeprom_indirect_address(priv, offset);
	BUG_ON(address >= priv->cfg->eeprom_size);
	return &priv->eeprom[address];
}

static int iwl5000_hw_set_hw_params(struct iwl_priv *priv)
{
	if ((priv->cfg->mod_params->num_of_queues > IWL50_NUM_QUEUES) ||
	    (priv->cfg->mod_params->num_of_queues < IWL_MIN_NUM_QUEUES)) {
		IWL_ERROR("invalid queues_num, should be between %d and %d\n",
			  IWL_MIN_NUM_QUEUES, IWL50_NUM_QUEUES);
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
	priv->hw_params.max_stations = IWL5000_STATION_COUNT;
	priv->hw_params.bcast_sta_id = IWL5000_BROADCAST_ID;
	priv->hw_params.max_data_size = IWL50_RTC_DATA_SIZE;
	priv->hw_params.max_inst_size = IWL50_RTC_INST_SIZE;
	priv->hw_params.max_bsm_size = BSM_SRAM_SIZE;
	priv->hw_params.fat_channel =  BIT(IEEE80211_BAND_2GHZ) |
					BIT(IEEE80211_BAND_5GHZ);
#ifdef CONFIG_IWL5000_RUN_TIME_CALIB
	priv->hw_params.sens = &iwl5000_sensitivity;
#endif

	switch (priv->hw_rev & CSR_HW_REV_TYPE_MSK) {
	case CSR_HW_REV_TYPE_5100:
	case CSR_HW_REV_TYPE_5150:
		priv->hw_params.tx_chains_num = 1;
		priv->hw_params.rx_chains_num = 2;
		/* FIXME: move to ANT_A, ANT_B, ANT_C enum */
		priv->hw_params.valid_tx_ant = ANT_A;
		priv->hw_params.valid_rx_ant = ANT_AB;
		break;
	case CSR_HW_REV_TYPE_5300:
	case CSR_HW_REV_TYPE_5350:
		priv->hw_params.tx_chains_num = 3;
		priv->hw_params.rx_chains_num = 3;
		priv->hw_params.valid_tx_ant = ANT_ABC;
		priv->hw_params.valid_rx_ant = ANT_ABC;
		break;
	}

	switch (priv->hw_rev & CSR_HW_REV_TYPE_MSK) {
	case CSR_HW_REV_TYPE_5100:
	case CSR_HW_REV_TYPE_5300:
		/* 5X00 wants in Celsius */
		priv->hw_params.ct_kill_threshold = CT_KILL_THRESHOLD;
		break;
	case CSR_HW_REV_TYPE_5150:
	case CSR_HW_REV_TYPE_5350:
		/* 5X50 wants in Kelvin */
		priv->hw_params.ct_kill_threshold =
				CELSIUS_TO_KELVIN(CT_KILL_THRESHOLD);
		break;
	}

	return 0;
}

static int iwl5000_alloc_shared_mem(struct iwl_priv *priv)
{
	priv->shared_virt = pci_alloc_consistent(priv->pci_dev,
					sizeof(struct iwl5000_shared),
					&priv->shared_phys);
	if (!priv->shared_virt)
		return -ENOMEM;

	memset(priv->shared_virt, 0, sizeof(struct iwl5000_shared));

	priv->rb_closed_offset = offsetof(struct iwl5000_shared, rb_closed);

	return 0;
}

static void iwl5000_free_shared_mem(struct iwl_priv *priv)
{
	if (priv->shared_virt)
		pci_free_consistent(priv->pci_dev,
				    sizeof(struct iwl5000_shared),
				    priv->shared_virt,
				    priv->shared_phys);
}

static int iwl5000_shared_mem_rx_idx(struct iwl_priv *priv)
{
	struct iwl5000_shared *s = priv->shared_virt;
	return le32_to_cpu(s->rb_closed) & 0xFFF;
}

/**
 * iwl5000_txq_update_byte_cnt_tbl - Set up entry in Tx byte-count array
 */
static void iwl5000_txq_update_byte_cnt_tbl(struct iwl_priv *priv,
					    struct iwl_tx_queue *txq,
					    u16 byte_cnt)
{
	struct iwl5000_shared *shared_data = priv->shared_virt;
	int txq_id = txq->q.id;
	u8 sec_ctl = 0;
	u8 sta = 0;
	int len;

	len = byte_cnt + IWL_TX_CRC_SIZE + IWL_TX_DELIMITER_SIZE;

	if (txq_id != IWL_CMD_QUEUE_NUM) {
		sta = txq->cmd[txq->q.write_ptr].cmd.tx.sta_id;
		sec_ctl = txq->cmd[txq->q.write_ptr].cmd.tx.sec_ctl;

		switch (sec_ctl & TX_CMD_SEC_MSK) {
		case TX_CMD_SEC_CCM:
			len += CCMP_MIC_LEN;
			break;
		case TX_CMD_SEC_TKIP:
			len += TKIP_ICV_LEN;
			break;
		case TX_CMD_SEC_WEP:
			len += WEP_IV_LEN + WEP_ICV_LEN;
			break;
		}
	}

	IWL_SET_BITS16(shared_data->queues_byte_cnt_tbls[txq_id].
		       tfd_offset[txq->q.write_ptr], byte_cnt, len);

	IWL_SET_BITS16(shared_data->queues_byte_cnt_tbls[txq_id].
		       tfd_offset[txq->q.write_ptr], sta_id, sta);

	if (txq->q.write_ptr < IWL50_MAX_WIN_SIZE) {
		IWL_SET_BITS16(shared_data->queues_byte_cnt_tbls[txq_id].
			tfd_offset[IWL50_QUEUE_SIZE + txq->q.write_ptr],
			byte_cnt, len);
		IWL_SET_BITS16(shared_data->queues_byte_cnt_tbls[txq_id].
			tfd_offset[IWL50_QUEUE_SIZE + txq->q.write_ptr],
			sta_id, sta);
	}
}

static u16 iwl5000_build_addsta_hcmd(const struct iwl_addsta_cmd *cmd, u8 *data)
{
	u16 size = (u16)sizeof(struct iwl_addsta_cmd);
	memcpy(data, cmd, size);
	return size;
}


static int iwl5000_disable_tx_fifo(struct iwl_priv *priv)
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

	iwl_write_prph(priv, IWL50_SCD_TXFACT, 0);
	iwl_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static struct iwl_hcmd_ops iwl5000_hcmd = {
};

static struct iwl_hcmd_utils_ops iwl5000_hcmd_utils = {
	.build_addsta_hcmd = iwl5000_build_addsta_hcmd,
#ifdef CONFIG_IWL5000_RUN_TIME_CALIB
	.gain_computation = iwl5000_gain_computation,
	.chain_noise_reset = iwl5000_chain_noise_reset,
#endif
};

static struct iwl_lib_ops iwl5000_lib = {
	.set_hw_params = iwl5000_hw_set_hw_params,
	.alloc_shared_mem = iwl5000_alloc_shared_mem,
	.free_shared_mem = iwl5000_free_shared_mem,
	.shared_mem_rx_idx = iwl5000_shared_mem_rx_idx,
	.txq_update_byte_cnt_tbl = iwl5000_txq_update_byte_cnt_tbl,
	.disable_tx_fifo = iwl5000_disable_tx_fifo,
	.apm_ops = {
		.init =	iwl5000_apm_init,
		.config = iwl5000_nic_config,
		.set_pwr_src = iwl4965_set_pwr_src,
	},
	.eeprom_ops = {
		.regulatory_bands = {
			EEPROM_5000_REG_BAND_1_CHANNELS,
			EEPROM_5000_REG_BAND_2_CHANNELS,
			EEPROM_5000_REG_BAND_3_CHANNELS,
			EEPROM_5000_REG_BAND_4_CHANNELS,
			EEPROM_5000_REG_BAND_5_CHANNELS,
			EEPROM_5000_REG_BAND_24_FAT_CHANNELS,
			EEPROM_5000_REG_BAND_52_FAT_CHANNELS
		},
		.verify_signature  = iwlcore_eeprom_verify_signature,
		.acquire_semaphore = iwlcore_eeprom_acquire_semaphore,
		.release_semaphore = iwlcore_eeprom_release_semaphore,
		.check_version	= iwl5000_eeprom_check_version,
		.query_addr = iwl5000_eeprom_query_addr,
	},
};

static struct iwl_ops iwl5000_ops = {
	.lib = &iwl5000_lib,
	.hcmd = &iwl5000_hcmd,
	.utils = &iwl5000_hcmd_utils,
};

static struct iwl_mod_params iwl50_mod_params = {
	.num_of_queues = IWL50_NUM_QUEUES,
	.enable_qos = 1,
	.amsdu_size_8K = 1,
	.restart_fw = 1,
	/* the rest are 0 by default */
};


struct iwl_cfg iwl5300_agn_cfg = {
	.name = "5300AGN",
	.fw_name = "iwlwifi-5000" IWL5000_UCODE_API ".ucode",
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl5000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.mod_params = &iwl50_mod_params,
};

struct iwl_cfg iwl5100_agn_cfg = {
	.name = "5100AGN",
	.fw_name = "iwlwifi-5000" IWL5000_UCODE_API ".ucode",
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl5000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.mod_params = &iwl50_mod_params,
};

struct iwl_cfg iwl5350_agn_cfg = {
	.name = "5350AGN",
	.fw_name = "iwlwifi-5000" IWL5000_UCODE_API ".ucode",
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl5000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.mod_params = &iwl50_mod_params,
};

module_param_named(disable50, iwl50_mod_params.disable, int, 0444);
MODULE_PARM_DESC(disable50,
		  "manually disable the 50XX radio (default 0 [radio on])");
module_param_named(swcrypto50, iwl50_mod_params.sw_crypto, bool, 0444);
MODULE_PARM_DESC(swcrypto50,
		  "using software crypto engine (default 0 [hardware])\n");
module_param_named(debug50, iwl50_mod_params.debug, int, 0444);
MODULE_PARM_DESC(debug50, "50XX debug output mask");
module_param_named(queues_num50, iwl50_mod_params.num_of_queues, int, 0444);
MODULE_PARM_DESC(queues_num50, "number of hw queues in 50xx series");
module_param_named(qos_enable50, iwl50_mod_params.enable_qos, int, 0444);
MODULE_PARM_DESC(qos_enable50, "enable all 50XX QoS functionality");
module_param_named(amsdu_size_8K50, iwl50_mod_params.amsdu_size_8K, int, 0444);
MODULE_PARM_DESC(amsdu_size_8K50, "enable 8K amsdu size in 50XX series");
module_param_named(fw_restart50, iwl50_mod_params.restart_fw, int, 0444);
MODULE_PARM_DESC(fw_restart50, "restart firmware in case of error");
