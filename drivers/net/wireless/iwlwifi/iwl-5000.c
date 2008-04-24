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
#include "iwl-4965.h"
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

	switch (priv->hw_rev & CSR_HW_REV_TYPE_MSK) {
	case CSR_HW_REV_TYPE_5100:
	case CSR_HW_REV_TYPE_5150:
		priv->hw_params.tx_chains_num = 1;
		priv->hw_params.rx_chains_num = 2;
		/* FIXME: move to ANT_A, ANT_B, ANT_C enum */
		priv->hw_params.valid_tx_ant = IWL_ANTENNA_MAIN;
		priv->hw_params.valid_rx_ant = (IWL_ANTENNA_MAIN |
						IWL_ANTENNA_AUX);
		break;
	case CSR_HW_REV_TYPE_5300:
	case CSR_HW_REV_TYPE_5350:
		priv->hw_params.tx_chains_num = 3;
		priv->hw_params.rx_chains_num = 3;
		/* FIXME: move to ANT_A, ANT_B, ANT_C enum */
		priv->hw_params.valid_tx_ant = (IWL_ANTENNA_MAIN |
						IWL_ANTENNA_AUX | 0x04);
		priv->hw_params.valid_rx_ant = (IWL_ANTENNA_MAIN |
						IWL_ANTENNA_AUX | 0x04);
		break;
	}
	return 0;
}
static struct iwl_hcmd_ops iwl5000_hcmd = {
};

static struct iwl_hcmd_utils_ops iwl5000_hcmd_utils = {
};

static struct iwl_lib_ops iwl5000_lib = {
	.set_hw_params = iwl5000_hw_set_hw_params,
	.apm_ops = {
		.init =	iwl5000_apm_init,
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


