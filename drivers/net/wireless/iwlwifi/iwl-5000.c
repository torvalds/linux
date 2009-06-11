/******************************************************************************
 *
 * Copyright(c) 2007 - 2009 Intel Corporation. All rights reserved.
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
#include "iwl-sta.h"
#include "iwl-helpers.h"
#include "iwl-5000-hw.h"
#include "iwl-6000-hw.h"

/* Highest firmware API version supported */
#define IWL5000_UCODE_API_MAX 2
#define IWL5150_UCODE_API_MAX 2

/* Lowest firmware API version supported */
#define IWL5000_UCODE_API_MIN 1
#define IWL5150_UCODE_API_MIN 1

#define IWL5000_FW_PRE "iwlwifi-5000-"
#define _IWL5000_MODULE_FIRMWARE(api) IWL5000_FW_PRE #api ".ucode"
#define IWL5000_MODULE_FIRMWARE(api) _IWL5000_MODULE_FIRMWARE(api)

#define IWL5150_FW_PRE "iwlwifi-5150-"
#define _IWL5150_MODULE_FIRMWARE(api) IWL5150_FW_PRE #api ".ucode"
#define IWL5150_MODULE_FIRMWARE(api) _IWL5150_MODULE_FIRMWARE(api)

static const u16 iwl5000_default_queue_to_tx_fifo[] = {
	IWL_TX_FIFO_AC3,
	IWL_TX_FIFO_AC2,
	IWL_TX_FIFO_AC1,
	IWL_TX_FIFO_AC0,
	IWL50_CMD_FIFO_NUM,
	IWL_TX_FIFO_HCCA_1,
	IWL_TX_FIFO_HCCA_2
};

/* FIXME: same implementation as 4965 */
static int iwl5000_apm_stop_master(struct iwl_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	/* set stop master bit */
	iwl_set_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_STOP_MASTER);

	iwl_poll_direct_bit(priv, CSR_RESET,
				  CSR_RESET_REG_FLAG_MASTER_DISABLED, 100);

	spin_unlock_irqrestore(&priv->lock, flags);
	IWL_DEBUG_INFO(priv, "stop master\n");

	return 0;
}


static int iwl5000_apm_init(struct iwl_priv *priv)
{
	int ret = 0;

	iwl_set_bit(priv, CSR_GIO_CHICKEN_BITS,
		    CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER);

	/* disable L0s without affecting L1 :don't wait for ICH L0s bug W/A) */
	iwl_set_bit(priv, CSR_GIO_CHICKEN_BITS,
		    CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX);

	/* Set FH wait threshold to maximum (HW error during stress W/A) */
	iwl_set_bit(priv, CSR_DBG_HPET_MEM_REG, CSR_DBG_HPET_MEM_REG_VAL);

	/* enable HAP INTA to move device L1a -> L0s */
	iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
		    CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A);

	if (priv->cfg->need_pll_cfg)
		iwl_set_bit(priv, CSR_ANA_PLL_CFG, CSR50_ANA_PLL_CFG_VAL);

	/* set "initialization complete" bit to move adapter
	 * D0U* --> D0A* state */
	iwl_set_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/* wait for clock stabilization */
	ret = iwl_poll_direct_bit(priv, CSR_GP_CNTRL,
			CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);
	if (ret < 0) {
		IWL_DEBUG_INFO(priv, "Failed to init the card\n");
		return ret;
	}

	/* enable DMA */
	iwl_write_prph(priv, APMG_CLK_EN_REG, APMG_CLK_VAL_DMA_CLK_RQT);

	udelay(20);

	/* disable L1-Active */
	iwl_set_bits_prph(priv, APMG_PCIDEV_STT_REG,
			  APMG_PCIDEV_STT_VAL_L1_ACT_DIS);

	return ret;
}

/* FIXME: this is identical to 4965 */
static void iwl5000_apm_stop(struct iwl_priv *priv)
{
	unsigned long flags;

	iwl5000_apm_stop_master(priv);

	spin_lock_irqsave(&priv->lock, flags);

	iwl_set_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);

	udelay(10);

	/* clear "init complete"  move adapter D0A* --> D0U state */
	iwl_clear_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	spin_unlock_irqrestore(&priv->lock, flags);
}


static int iwl5000_apm_reset(struct iwl_priv *priv)
{
	int ret = 0;

	iwl5000_apm_stop_master(priv);

	iwl_set_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);

	udelay(10);


	/* FIXME: put here L1A -L0S w/a */

	if (priv->cfg->need_pll_cfg)
		iwl_set_bit(priv, CSR_ANA_PLL_CFG, CSR50_ANA_PLL_CFG_VAL);

	/* set "initialization complete" bit to move adapter
	 * D0U* --> D0A* state */
	iwl_set_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/* wait for clock stabilization */
	ret = iwl_poll_direct_bit(priv, CSR_GP_CNTRL,
			CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);
	if (ret < 0) {
		IWL_DEBUG_INFO(priv, "Failed to init the card\n");
		goto out;
	}

	/* enable DMA */
	iwl_write_prph(priv, APMG_CLK_EN_REG, APMG_CLK_VAL_DMA_CLK_RQT);

	udelay(20);

	/* disable L1-Active */
	iwl_set_bits_prph(priv, APMG_PCIDEV_STT_REG,
			  APMG_PCIDEV_STT_VAL_L1_ACT_DIS);
out:

	return ret;
}


static void iwl5000_nic_config(struct iwl_priv *priv)
{
	unsigned long flags;
	u16 radio_cfg;
	u16 lctl;

	spin_lock_irqsave(&priv->lock, flags);

	lctl = iwl_pcie_link_ctl(priv);

	/* HW bug W/A */
	/* L1-ASPM is enabled by BIOS */
	if ((lctl & PCI_CFG_LINK_CTRL_VAL_L1_EN) == PCI_CFG_LINK_CTRL_VAL_L1_EN)
		/* L1-APSM enabled: disable L0S  */
		iwl_set_bit(priv, CSR_GIO_REG, CSR_GIO_REG_VAL_L0S_ENABLED);
	else
		/* L1-ASPM disabled: enable L0S */
		iwl_clear_bit(priv, CSR_GIO_REG, CSR_GIO_REG_VAL_L0S_ENABLED);

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

	/* W/A : NIC is stuck in a reset state after Early PCIe power off
	 * (PCIe power is lost before PERST# is asserted),
	 * causing ME FW to lose ownership and not being able to obtain it back.
	 */
	iwl_set_bits_mask_prph(priv, APMG_PS_CTRL_REG,
				APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS,
				~APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS);

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
		IWL_ERR(priv, "illegal indirect type: 0x%X\n",
		address & INDIRECT_TYPE_MSK);
		break;
	}

	/* translate the offset from words to byte */
	return (address & ADDRESS_MSK) + (offset << 1);
}

static u16 iwl5000_eeprom_calib_version(struct iwl_priv *priv)
{
	struct iwl_eeprom_calib_hdr {
		u8 version;
		u8 pa_type;
		u16 voltage;
	} *hdr;

	hdr = (struct iwl_eeprom_calib_hdr *)iwl_eeprom_query_addr(priv,
							EEPROM_5000_CALIB_ALL);
	return hdr->version;

}

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

	IWL_DEBUG_CALIB(priv, "Delta gains: ANT_B = %d  ANT_C = %d\n",
			data->delta_gain_code[1], data->delta_gain_code[2]);

	if (!data->radio_write) {
		struct iwl_calib_chain_noise_gain_cmd cmd;

		memset(&cmd, 0, sizeof(cmd));

		cmd.hdr.op_code = IWL_PHY_CALIBRATE_CHAIN_NOISE_GAIN_CMD;
		cmd.hdr.first_group = 0;
		cmd.hdr.groups_num = 1;
		cmd.hdr.data_valid = 1;
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
	int ret;

	if ((data->state == IWL_CHAIN_NOISE_ALIVE) && iwl_is_associated(priv)) {
		struct iwl_calib_chain_noise_reset_cmd cmd;
		memset(&cmd, 0, sizeof(cmd));

		cmd.hdr.op_code = IWL_PHY_CALIBRATE_CHAIN_NOISE_RESET_CMD;
		cmd.hdr.first_group = 0;
		cmd.hdr.groups_num = 1;
		cmd.hdr.data_valid = 1;
		ret = iwl_send_cmd_pdu(priv, REPLY_PHY_CALIBRATION_CMD,
					sizeof(cmd), &cmd);
		if (ret)
			IWL_ERR(priv,
				"Could not send REPLY_PHY_CALIBRATION_CMD\n");
		data->state = IWL_CHAIN_NOISE_ACCUMULATE;
		IWL_DEBUG_CALIB(priv, "Run chain_noise_calibrate\n");
	}
}

void iwl5000_rts_tx_cmd_flag(struct ieee80211_tx_info *info,
			__le32 *tx_flags)
{
	if ((info->control.rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS) ||
	    (info->control.rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT))
		*tx_flags |= TX_CMD_FLG_RTS_CTS_MSK;
	else
		*tx_flags &= ~TX_CMD_FLG_RTS_CTS_MSK;
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

static const u8 *iwl5000_eeprom_query_addr(const struct iwl_priv *priv,
					   size_t offset)
{
	u32 address = eeprom_indirect_address(priv, offset);
	BUG_ON(address >= priv->cfg->eeprom_size);
	return &priv->eeprom[address];
}

static void iwl5150_set_ct_threshold(struct iwl_priv *priv)
{
	const s32 volt2temp_coef = IWL_5150_VOLTAGE_TO_TEMPERATURE_COEFF;
	s32 threshold = (s32)CELSIUS_TO_KELVIN(CT_KILL_THRESHOLD) -
			iwl_temp_calib_to_offset(priv);

	priv->hw_params.ct_kill_threshold = threshold * volt2temp_coef;
}

static void iwl5000_set_ct_threshold(struct iwl_priv *priv)
{
	/* want Celsius */
	priv->hw_params.ct_kill_threshold = CT_KILL_THRESHOLD;
}

/*
 *  Calibration
 */
static int iwl5000_set_Xtal_calib(struct iwl_priv *priv)
{
	struct iwl_calib_xtal_freq_cmd cmd;
	u16 *xtal_calib = (u16 *)iwl_eeprom_query_addr(priv, EEPROM_5000_XTAL);

	cmd.hdr.op_code = IWL_PHY_CALIBRATE_CRYSTAL_FRQ_CMD;
	cmd.hdr.first_group = 0;
	cmd.hdr.groups_num = 1;
	cmd.hdr.data_valid = 1;
	cmd.cap_pin1 = (u8)xtal_calib[0];
	cmd.cap_pin2 = (u8)xtal_calib[1];
	return iwl_calib_set(&priv->calib_results[IWL_CALIB_XTAL],
			     (u8 *)&cmd, sizeof(cmd));
}

static int iwl5000_send_calib_cfg(struct iwl_priv *priv)
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

static void iwl5000_rx_calib_result(struct iwl_priv *priv,
			     struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (void *)rxb->skb->data;
	struct iwl_calib_hdr *hdr = (struct iwl_calib_hdr *)pkt->u.raw;
	int len = le32_to_cpu(pkt->len) & FH_RSCSR_FRAME_SIZE_MSK;
	int index;

	/* reduce the size of the length field itself */
	len -= 4;

	/* Define the order in which the results will be sent to the runtime
	 * uCode. iwl_send_calib_results sends them in a row according to their
	 * index. We sort them here */
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

static void iwl5000_rx_calib_complete(struct iwl_priv *priv,
			       struct iwl_rx_mem_buffer *rxb)
{
	IWL_DEBUG_INFO(priv, "Init. calibration is completed, restarting fw.\n");
	queue_work(priv->workqueue, &priv->restart);
}

/*
 * ucode
 */
static int iwl5000_load_section(struct iwl_priv *priv,
				struct fw_desc *image,
				u32 dst_addr)
{
	dma_addr_t phy_addr = image->p_addr;
	u32 byte_cnt = image->len;

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

	return 0;
}

static int iwl5000_load_given_ucode(struct iwl_priv *priv,
		struct fw_desc *inst_image,
		struct fw_desc *data_image)
{
	int ret = 0;

	ret = iwl5000_load_section(priv, inst_image,
				   IWL50_RTC_INST_LOWER_BOUND);
	if (ret)
		return ret;

	IWL_DEBUG_INFO(priv, "INST uCode section being loaded...\n");
	ret = wait_event_interruptible_timeout(priv->wait_command_queue,
					priv->ucode_write_complete, 5 * HZ);
	if (ret == -ERESTARTSYS) {
		IWL_ERR(priv, "Could not load the INST uCode section due "
			"to interrupt\n");
		return ret;
	}
	if (!ret) {
		IWL_ERR(priv, "Could not load the INST uCode section\n");
		return -ETIMEDOUT;
	}

	priv->ucode_write_complete = 0;

	ret = iwl5000_load_section(
		priv, data_image, IWL50_RTC_DATA_LOWER_BOUND);
	if (ret)
		return ret;

	IWL_DEBUG_INFO(priv, "DATA uCode section being loaded...\n");

	ret = wait_event_interruptible_timeout(priv->wait_command_queue,
				priv->ucode_write_complete, 5 * HZ);
	if (ret == -ERESTARTSYS) {
		IWL_ERR(priv, "Could not load the INST uCode section due "
			"to interrupt\n");
		return ret;
	} else if (!ret) {
		IWL_ERR(priv, "Could not load the DATA uCode section\n");
		return -ETIMEDOUT;
	} else
		ret = 0;

	priv->ucode_write_complete = 0;

	return ret;
}

static int iwl5000_load_ucode(struct iwl_priv *priv)
{
	int ret = 0;

	/* check whether init ucode should be loaded, or rather runtime ucode */
	if (priv->ucode_init.len && (priv->ucode_type == UCODE_NONE)) {
		IWL_DEBUG_INFO(priv, "Init ucode found. Loading init ucode...\n");
		ret = iwl5000_load_given_ucode(priv,
			&priv->ucode_init, &priv->ucode_init_data);
		if (!ret) {
			IWL_DEBUG_INFO(priv, "Init ucode load complete.\n");
			priv->ucode_type = UCODE_INIT;
		}
	} else {
		IWL_DEBUG_INFO(priv, "Init ucode not found, or already loaded. "
			"Loading runtime ucode...\n");
		ret = iwl5000_load_given_ucode(priv,
			&priv->ucode_code, &priv->ucode_data);
		if (!ret) {
			IWL_DEBUG_INFO(priv, "Runtime ucode load complete.\n");
			priv->ucode_type = UCODE_RT;
		}
	}

	return ret;
}

static void iwl5000_init_alive_start(struct iwl_priv *priv)
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

	iwl_clear_stations_table(priv);
	ret = priv->cfg->ops->lib->alive_notify(priv);
	if (ret) {
		IWL_WARN(priv,
			"Could not complete ALIVE transition: %d\n", ret);
		goto restart;
	}

	iwl5000_send_calib_cfg(priv);
	return;

restart:
	/* real restart (first load init_ucode) */
	queue_work(priv->workqueue, &priv->restart);
}

static void iwl5000_set_wr_ptrs(struct iwl_priv *priv,
				int txq_id, u32 index)
{
	iwl_write_direct32(priv, HBUS_TARG_WRPTR,
			(index & 0xff) | (txq_id << 8));
	iwl_write_prph(priv, IWL50_SCD_QUEUE_RDPTR(txq_id), index);
}

static void iwl5000_tx_queue_set_status(struct iwl_priv *priv,
					struct iwl_tx_queue *txq,
					int tx_fifo_id, int scd_retry)
{
	int txq_id = txq->q.id;
	int active = test_bit(txq_id, &priv->txq_ctx_active_msk) ? 1 : 0;

	iwl_write_prph(priv, IWL50_SCD_QUEUE_STATUS_BITS(txq_id),
			(active << IWL50_SCD_QUEUE_STTS_REG_POS_ACTIVE) |
			(tx_fifo_id << IWL50_SCD_QUEUE_STTS_REG_POS_TXF) |
			(1 << IWL50_SCD_QUEUE_STTS_REG_POS_WSL) |
			IWL50_SCD_QUEUE_STTS_REG_MSK);

	txq->sched_retry = scd_retry;

	IWL_DEBUG_INFO(priv, "%s %s Queue %d on AC %d\n",
		       active ? "Activate" : "Deactivate",
		       scd_retry ? "BA" : "AC", txq_id, tx_fifo_id);
}

static int iwl5000_send_wimax_coex(struct iwl_priv *priv)
{
	struct iwl_wimax_coex_cmd coex_cmd;

	memset(&coex_cmd, 0, sizeof(coex_cmd));

	return iwl_send_cmd_pdu(priv, COEX_PRIORITY_TABLE_CMD,
				sizeof(coex_cmd), &coex_cmd);
}

static int iwl5000_alive_notify(struct iwl_priv *priv)
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
	for (; a < sizeof(u16) * priv->hw_params.max_txq_num; a += 4)
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

	iwl5000_set_wr_ptrs(priv, IWL_CMD_QUEUE_NUM, 0);

	/* map qos queues to fifos one-to-one */
	for (i = 0; i < ARRAY_SIZE(iwl5000_default_queue_to_tx_fifo); i++) {
		int ac = iwl5000_default_queue_to_tx_fifo[i];
		iwl_txq_ctx_activate(priv, i);
		iwl5000_tx_queue_set_status(priv, &priv->txq[i], ac, 0);
	}
	/* TODO - need to initialize those FIFOs inside the loop above,
	 * not only mark them as active */
	iwl_txq_ctx_activate(priv, 4);
	iwl_txq_ctx_activate(priv, 7);
	iwl_txq_ctx_activate(priv, 8);
	iwl_txq_ctx_activate(priv, 9);

	spin_unlock_irqrestore(&priv->lock, flags);


	iwl5000_send_wimax_coex(priv);

	iwl5000_set_Xtal_calib(priv);
	iwl_send_calib_results(priv);

	return 0;
}

static int iwl5000_hw_set_hw_params(struct iwl_priv *priv)
{
	if ((priv->cfg->mod_params->num_of_queues > IWL50_NUM_QUEUES) ||
	    (priv->cfg->mod_params->num_of_queues < IWL_MIN_NUM_QUEUES)) {
		IWL_ERR(priv,
			"invalid queues_num, should be between %d and %d\n",
			IWL_MIN_NUM_QUEUES, IWL50_NUM_QUEUES);
		return -EINVAL;
	}

	priv->hw_params.max_txq_num = priv->cfg->mod_params->num_of_queues;
	priv->hw_params.dma_chnl_num = FH50_TCSR_CHNL_NUM;
	priv->hw_params.scd_bc_tbls_size =
			IWL50_NUM_QUEUES * sizeof(struct iwl5000_scd_bc_tbl);
	priv->hw_params.tfd_size = sizeof(struct iwl_tfd);
	priv->hw_params.max_stations = IWL5000_STATION_COUNT;
	priv->hw_params.bcast_sta_id = IWL5000_BROADCAST_ID;

	switch (priv->hw_rev & CSR_HW_REV_TYPE_MSK) {
	case CSR_HW_REV_TYPE_6x00:
	case CSR_HW_REV_TYPE_6x50:
		priv->hw_params.max_data_size = IWL60_RTC_DATA_SIZE;
		priv->hw_params.max_inst_size = IWL60_RTC_INST_SIZE;
		break;
	default:
		priv->hw_params.max_data_size = IWL50_RTC_DATA_SIZE;
		priv->hw_params.max_inst_size = IWL50_RTC_INST_SIZE;
	}

	priv->hw_params.max_bsm_size = 0;
	priv->hw_params.fat_channel =  BIT(IEEE80211_BAND_2GHZ) |
					BIT(IEEE80211_BAND_5GHZ);
	priv->hw_params.rx_wrt_ptr_reg = FH_RSCSR_CHNL0_WPTR;

	priv->hw_params.sens = &iwl5000_sensitivity;

	priv->hw_params.tx_chains_num = num_of_ant(priv->cfg->valid_tx_ant);
	priv->hw_params.rx_chains_num = num_of_ant(priv->cfg->valid_rx_ant);
	priv->hw_params.valid_tx_ant = priv->cfg->valid_tx_ant;
	priv->hw_params.valid_rx_ant = priv->cfg->valid_rx_ant;

	if (priv->cfg->ops->lib->temp_ops.set_ct_kill)
		priv->cfg->ops->lib->temp_ops.set_ct_kill(priv);

	/* Set initial calibration set */
	switch (priv->hw_rev & CSR_HW_REV_TYPE_MSK) {
	case CSR_HW_REV_TYPE_5150:
		priv->hw_params.calib_init_cfg =
			BIT(IWL_CALIB_DC)		|
			BIT(IWL_CALIB_LO)		|
			BIT(IWL_CALIB_TX_IQ) 		|
			BIT(IWL_CALIB_BASE_BAND);

		break;
	default:
		priv->hw_params.calib_init_cfg =
			BIT(IWL_CALIB_XTAL)		|
			BIT(IWL_CALIB_LO)		|
			BIT(IWL_CALIB_TX_IQ) 		|
			BIT(IWL_CALIB_TX_IQ_PERD)	|
			BIT(IWL_CALIB_BASE_BAND);
		break;
	}

	return 0;
}

/**
 * iwl5000_txq_update_byte_cnt_tbl - Set up entry in Tx byte-count array
 */
static void iwl5000_txq_update_byte_cnt_tbl(struct iwl_priv *priv,
					    struct iwl_tx_queue *txq,
					    u16 byte_cnt)
{
	struct iwl5000_scd_bc_tbl *scd_bc_tbl = priv->scd_bc_tbls.addr;
	int write_ptr = txq->q.write_ptr;
	int txq_id = txq->q.id;
	u8 sec_ctl = 0;
	u8 sta_id = 0;
	u16 len = byte_cnt + IWL_TX_CRC_SIZE + IWL_TX_DELIMITER_SIZE;
	__le16 bc_ent;

	WARN_ON(len > 0xFFF || write_ptr >= TFD_QUEUE_SIZE_MAX);

	if (txq_id != IWL_CMD_QUEUE_NUM) {
		sta_id = txq->cmd[txq->q.write_ptr]->cmd.tx.sta_id;
		sec_ctl = txq->cmd[txq->q.write_ptr]->cmd.tx.sec_ctl;

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

	bc_ent = cpu_to_le16((len & 0xFFF) | (sta_id << 12));

	scd_bc_tbl[txq_id].tfd_offset[write_ptr] = bc_ent;

	if (txq->q.write_ptr < TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[txq_id].
			tfd_offset[TFD_QUEUE_SIZE_MAX + write_ptr] = bc_ent;
}

static void iwl5000_txq_inval_byte_cnt_tbl(struct iwl_priv *priv,
					   struct iwl_tx_queue *txq)
{
	struct iwl5000_scd_bc_tbl *scd_bc_tbl = priv->scd_bc_tbls.addr;
	int txq_id = txq->q.id;
	int read_ptr = txq->q.read_ptr;
	u8 sta_id = 0;
	__le16 bc_ent;

	WARN_ON(read_ptr >= TFD_QUEUE_SIZE_MAX);

	if (txq_id != IWL_CMD_QUEUE_NUM)
		sta_id = txq->cmd[read_ptr]->cmd.tx.sta_id;

	bc_ent =  cpu_to_le16(1 | (sta_id << 12));
	scd_bc_tbl[txq_id].tfd_offset[read_ptr] = bc_ent;

	if (txq->q.write_ptr < TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[txq_id].
			tfd_offset[TFD_QUEUE_SIZE_MAX + read_ptr] =  bc_ent;
}

static int iwl5000_tx_queue_set_q2ratid(struct iwl_priv *priv, u16 ra_tid,
					u16 txq_id)
{
	u32 tbl_dw_addr;
	u32 tbl_dw;
	u16 scd_q2ratid;

	scd_q2ratid = ra_tid & IWL_SCD_QUEUE_RA_TID_MAP_RATID_MSK;

	tbl_dw_addr = priv->scd_base_addr +
			IWL50_SCD_TRANSLATE_TBL_OFFSET_QUEUE(txq_id);

	tbl_dw = iwl_read_targ_mem(priv, tbl_dw_addr);

	if (txq_id & 0x1)
		tbl_dw = (scd_q2ratid << 16) | (tbl_dw & 0x0000FFFF);
	else
		tbl_dw = scd_q2ratid | (tbl_dw & 0xFFFF0000);

	iwl_write_targ_mem(priv, tbl_dw_addr, tbl_dw);

	return 0;
}
static void iwl5000_tx_queue_stop_scheduler(struct iwl_priv *priv, u16 txq_id)
{
	/* Simply stop the queue, but don't change any configuration;
	 * the SCD_ACT_EN bit is the write-enable mask for the ACTIVE bit. */
	iwl_write_prph(priv,
		IWL50_SCD_QUEUE_STATUS_BITS(txq_id),
		(0 << IWL50_SCD_QUEUE_STTS_REG_POS_ACTIVE)|
		(1 << IWL50_SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN));
}

static int iwl5000_txq_agg_enable(struct iwl_priv *priv, int txq_id,
				  int tx_fifo, int sta_id, int tid, u16 ssn_idx)
{
	unsigned long flags;
	u16 ra_tid;

	if ((IWL50_FIRST_AMPDU_QUEUE > txq_id) ||
	    (IWL50_FIRST_AMPDU_QUEUE + IWL50_NUM_AMPDU_QUEUES <= txq_id)) {
		IWL_WARN(priv,
			"queue number out of range: %d, must be %d to %d\n",
			txq_id, IWL50_FIRST_AMPDU_QUEUE,
			IWL50_FIRST_AMPDU_QUEUE + IWL50_NUM_AMPDU_QUEUES - 1);
		return -EINVAL;
	}

	ra_tid = BUILD_RAxTID(sta_id, tid);

	/* Modify device's station table to Tx this TID */
	iwl_sta_tx_modify_enable_tid(priv, sta_id, tid);

	spin_lock_irqsave(&priv->lock, flags);

	/* Stop this Tx queue before configuring it */
	iwl5000_tx_queue_stop_scheduler(priv, txq_id);

	/* Map receiver-address / traffic-ID to this queue */
	iwl5000_tx_queue_set_q2ratid(priv, ra_tid, txq_id);

	/* Set this queue as a chain-building queue */
	iwl_set_bits_prph(priv, IWL50_SCD_QUEUECHAIN_SEL, (1<<txq_id));

	/* enable aggregations for the queue */
	iwl_set_bits_prph(priv, IWL50_SCD_AGGR_SEL, (1<<txq_id));

	/* Place first TFD at index corresponding to start sequence number.
	 * Assumes that ssn_idx is valid (!= 0xFFF) */
	priv->txq[txq_id].q.read_ptr = (ssn_idx & 0xff);
	priv->txq[txq_id].q.write_ptr = (ssn_idx & 0xff);
	iwl5000_set_wr_ptrs(priv, txq_id, ssn_idx);

	/* Set up Tx window size and frame limit for this queue */
	iwl_write_targ_mem(priv, priv->scd_base_addr +
			IWL50_SCD_CONTEXT_QUEUE_OFFSET(txq_id) +
			sizeof(u32),
			((SCD_WIN_SIZE <<
			IWL50_SCD_QUEUE_CTX_REG2_WIN_SIZE_POS) &
			IWL50_SCD_QUEUE_CTX_REG2_WIN_SIZE_MSK) |
			((SCD_FRAME_LIMIT <<
			IWL50_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS) &
			IWL50_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK));

	iwl_set_bits_prph(priv, IWL50_SCD_INTERRUPT_MASK, (1 << txq_id));

	/* Set up Status area in SRAM, map to Tx DMA/FIFO, activate the queue */
	iwl5000_tx_queue_set_status(priv, &priv->txq[txq_id], tx_fifo, 1);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int iwl5000_txq_agg_disable(struct iwl_priv *priv, u16 txq_id,
				   u16 ssn_idx, u8 tx_fifo)
{
	if ((IWL50_FIRST_AMPDU_QUEUE > txq_id) ||
	    (IWL50_FIRST_AMPDU_QUEUE + IWL50_NUM_AMPDU_QUEUES <= txq_id)) {
		IWL_ERR(priv,
			"queue number out of range: %d, must be %d to %d\n",
			txq_id, IWL50_FIRST_AMPDU_QUEUE,
			IWL50_FIRST_AMPDU_QUEUE + IWL50_NUM_AMPDU_QUEUES - 1);
		return -EINVAL;
	}

	iwl5000_tx_queue_stop_scheduler(priv, txq_id);

	iwl_clear_bits_prph(priv, IWL50_SCD_AGGR_SEL, (1 << txq_id));

	priv->txq[txq_id].q.read_ptr = (ssn_idx & 0xff);
	priv->txq[txq_id].q.write_ptr = (ssn_idx & 0xff);
	/* supposes that ssn_idx is valid (!= 0xFFF) */
	iwl5000_set_wr_ptrs(priv, txq_id, ssn_idx);

	iwl_clear_bits_prph(priv, IWL50_SCD_INTERRUPT_MASK, (1 << txq_id));
	iwl_txq_ctx_deactivate(priv, txq_id);
	iwl5000_tx_queue_set_status(priv, &priv->txq[txq_id], tx_fifo, 0);

	return 0;
}

u16 iwl5000_build_addsta_hcmd(const struct iwl_addsta_cmd *cmd, u8 *data)
{
	u16 size = (u16)sizeof(struct iwl_addsta_cmd);
	struct iwl_addsta_cmd *addsta = (struct iwl_addsta_cmd *)data;
	memcpy(addsta, cmd, size);
	/* resrved in 5000 */
	addsta->rate_n_flags = cpu_to_le16(0);
	return size;
}


/*
 * Activate/Deactivate Tx DMA/FIFO channels according tx fifos mask
 * must be called under priv->lock and mac access
 */
static void iwl5000_txq_set_sched(struct iwl_priv *priv, u32 mask)
{
	iwl_write_prph(priv, IWL50_SCD_TXFACT, mask);
}


static inline u32 iwl5000_get_scd_ssn(struct iwl5000_tx_resp *tx_resp)
{
	return le32_to_cpup((__le32 *)&tx_resp->status +
			    tx_resp->frame_count) & MAX_SN;
}

static int iwl5000_tx_status_reply_tx(struct iwl_priv *priv,
				      struct iwl_ht_agg *agg,
				      struct iwl5000_tx_resp *tx_resp,
				      int txq_id, u16 start_idx)
{
	u16 status;
	struct agg_tx_status *frame_status = &tx_resp->status;
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

	/* # frames attempted by Tx command */
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

			sc = le16_to_cpu(hdr->seq_ctrl);
			if (idx != (SEQ_TO_SN(sc) & 0xff)) {
				IWL_ERR(priv,
					"BUG_ON idx doesn't match seq control"
					" idx=%d, seq_idx=%d, seq=%d\n",
					  idx, SEQ_TO_SN(sc),
					  hdr->seq_ctrl);
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

static void iwl5000_rx_reply_tx(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	int txq_id = SEQ_TO_QUEUE(sequence);
	int index = SEQ_TO_INDEX(sequence);
	struct iwl_tx_queue *txq = &priv->txq[txq_id];
	struct ieee80211_tx_info *info;
	struct iwl5000_tx_resp *tx_resp = (void *)&pkt->u.raw[0];
	u32  status = le16_to_cpu(tx_resp->status.status);
	int tid;
	int sta_id;
	int freed;

	if ((index >= txq->q.n_bd) || (iwl_queue_used(&txq->q, index) == 0)) {
		IWL_ERR(priv, "Read index for DMA queue txq_id (%d) index %d "
			  "is out of range [0-%d] %d %d\n", txq_id,
			  index, txq->q.n_bd, txq->q.write_ptr,
			  txq->q.read_ptr);
		return;
	}

	info = IEEE80211_SKB_CB(txq->txb[txq->q.read_ptr].skb[0]);
	memset(&info->status, 0, sizeof(info->status));

	tid = (tx_resp->ra_tid & IWL50_TX_RES_TID_MSK) >> IWL50_TX_RES_TID_POS;
	sta_id = (tx_resp->ra_tid & IWL50_TX_RES_RA_MSK) >> IWL50_TX_RES_RA_POS;

	if (txq->sched_retry) {
		const u32 scd_ssn = iwl5000_get_scd_ssn(tx_resp);
		struct iwl_ht_agg *agg = NULL;

		agg = &priv->stations[sta_id].tid[tid].agg;

		iwl5000_tx_status_reply_tx(priv, agg, tx_resp, txq_id, index);

		/* check if BAR is needed */
		if ((tx_resp->frame_count == 1) && !iwl_is_tx_success(status))
			info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;

		if (txq->q.read_ptr != (scd_ssn & 0xff)) {
			index = iwl_queue_dec_wrap(scd_ssn & 0xff, txq->q.n_bd);
			IWL_DEBUG_TX_REPLY(priv, "Retry scheduler reclaim "
					"scd_ssn=%d idx=%d txq=%d swq=%d\n",
					scd_ssn , index, txq_id, txq->swq_id);

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
		BUG_ON(txq_id != txq->swq_id);

		info->status.rates[0].count = tx_resp->failure_frame + 1;
		info->flags |= iwl_is_tx_success(status) ?
					IEEE80211_TX_STAT_ACK : 0;
		iwl_hwrate_to_tx_control(priv,
					le32_to_cpu(tx_resp->rate_n_flags),
					info);

		IWL_DEBUG_TX_REPLY(priv, "TXQ %d status %s (0x%08x) rate_n_flags "
				   "0x%x retries %d\n",
				   txq_id,
				   iwl_get_tx_fail_reason(status), status,
				   le32_to_cpu(tx_resp->rate_n_flags),
				   tx_resp->failure_frame);

		freed = iwl_tx_queue_reclaim(priv, txq_id, index);
		if (ieee80211_is_data_qos(tx_resp->frame_ctrl))
			priv->stations[sta_id].tid[tid].tfds_in_queue -= freed;

		if (priv->mac80211_registered &&
		    (iwl_queue_space(&txq->q) > txq->q.low_mark))
			iwl_wake_queue(priv, txq_id);
	}

	if (ieee80211_is_data_qos(tx_resp->frame_ctrl))
		iwl_txq_check_empty(priv, sta_id, tid, txq_id);

	if (iwl_check_bits(status, TX_ABORT_REQUIRED_MSK))
		IWL_ERR(priv, "TODO:  Implement Tx ABORT REQUIRED!!!\n");
}

/* Currently 5000 is the superset of everything */
u16 iwl5000_get_hcmd_size(u8 cmd_id, u16 len)
{
	return len;
}

static void iwl5000_setup_deferred_work(struct iwl_priv *priv)
{
	/* in 5000 the tx power calibration is done in uCode */
	priv->disable_tx_power_cal = 1;
}

static void iwl5000_rx_handler_setup(struct iwl_priv *priv)
{
	/* init calibration handlers */
	priv->rx_handlers[CALIBRATION_RES_NOTIFICATION] =
					iwl5000_rx_calib_result;
	priv->rx_handlers[CALIBRATION_COMPLETE_NOTIFICATION] =
					iwl5000_rx_calib_complete;
	priv->rx_handlers[REPLY_TX] = iwl5000_rx_reply_tx;
}


static int iwl5000_hw_valid_rtc_data_addr(u32 addr)
{
	return (addr >= IWL50_RTC_DATA_LOWER_BOUND) &&
		(addr < IWL50_RTC_DATA_UPPER_BOUND);
}

static int iwl5000_send_rxon_assoc(struct iwl_priv *priv)
{
	int ret = 0;
	struct iwl5000_rxon_assoc_cmd rxon_assoc;
	const struct iwl_rxon_cmd *rxon1 = &priv->staging_rxon;
	const struct iwl_rxon_cmd *rxon2 = &priv->active_rxon;

	if ((rxon1->flags == rxon2->flags) &&
	    (rxon1->filter_flags == rxon2->filter_flags) &&
	    (rxon1->cck_basic_rates == rxon2->cck_basic_rates) &&
	    (rxon1->ofdm_ht_single_stream_basic_rates ==
	     rxon2->ofdm_ht_single_stream_basic_rates) &&
	    (rxon1->ofdm_ht_dual_stream_basic_rates ==
	     rxon2->ofdm_ht_dual_stream_basic_rates) &&
	    (rxon1->ofdm_ht_triple_stream_basic_rates ==
	     rxon2->ofdm_ht_triple_stream_basic_rates) &&
	    (rxon1->acquisition_data == rxon2->acquisition_data) &&
	    (rxon1->rx_chain == rxon2->rx_chain) &&
	    (rxon1->ofdm_basic_rates == rxon2->ofdm_basic_rates)) {
		IWL_DEBUG_INFO(priv, "Using current RXON_ASSOC.  Not resending.\n");
		return 0;
	}

	rxon_assoc.flags = priv->staging_rxon.flags;
	rxon_assoc.filter_flags = priv->staging_rxon.filter_flags;
	rxon_assoc.ofdm_basic_rates = priv->staging_rxon.ofdm_basic_rates;
	rxon_assoc.cck_basic_rates = priv->staging_rxon.cck_basic_rates;
	rxon_assoc.reserved1 = 0;
	rxon_assoc.reserved2 = 0;
	rxon_assoc.reserved3 = 0;
	rxon_assoc.ofdm_ht_single_stream_basic_rates =
	    priv->staging_rxon.ofdm_ht_single_stream_basic_rates;
	rxon_assoc.ofdm_ht_dual_stream_basic_rates =
	    priv->staging_rxon.ofdm_ht_dual_stream_basic_rates;
	rxon_assoc.rx_chain_select_flags = priv->staging_rxon.rx_chain;
	rxon_assoc.ofdm_ht_triple_stream_basic_rates =
		 priv->staging_rxon.ofdm_ht_triple_stream_basic_rates;
	rxon_assoc.acquisition_data = priv->staging_rxon.acquisition_data;

	ret = iwl_send_cmd_pdu_async(priv, REPLY_RXON_ASSOC,
				     sizeof(rxon_assoc), &rxon_assoc, NULL);
	if (ret)
		return ret;

	return ret;
}
static int  iwl5000_send_tx_power(struct iwl_priv *priv)
{
	struct iwl5000_tx_power_dbm_cmd tx_power_cmd;
	u8 tx_ant_cfg_cmd;

	/* half dBm need to multiply */
	tx_power_cmd.global_lmt = (s8)(2 * priv->tx_power_user_lmt);
	tx_power_cmd.flags = IWL50_TX_POWER_NO_CLOSED;
	tx_power_cmd.srv_chan_lmt = IWL50_TX_POWER_AUTO;

	if (IWL_UCODE_API(priv->ucode_ver) == 1)
		tx_ant_cfg_cmd = REPLY_TX_POWER_DBM_CMD_V1;
	else
		tx_ant_cfg_cmd = REPLY_TX_POWER_DBM_CMD;

	return  iwl_send_cmd_pdu_async(priv, tx_ant_cfg_cmd,
				       sizeof(tx_power_cmd), &tx_power_cmd,
				       NULL);
}

static void iwl5000_temperature(struct iwl_priv *priv)
{
	/* store temperature from statistics (in Celsius) */
	priv->temperature = le32_to_cpu(priv->statistics.general.temperature);
}

static void iwl5150_temperature(struct iwl_priv *priv)
{
	u32 vt = 0;
	s32 offset =  iwl_temp_calib_to_offset(priv);

	vt = le32_to_cpu(priv->statistics.general.temperature);
	vt = vt / IWL_5150_VOLTAGE_TO_TEMPERATURE_COEFF + offset;
	/* now vt hold the temperature in Kelvin */
	priv->temperature = KELVIN_TO_CELSIUS(vt);
}

/* Calc max signal level (dBm) among 3 possible receivers */
int iwl5000_calc_rssi(struct iwl_priv *priv,
			     struct iwl_rx_phy_res *rx_resp)
{
	/* data from PHY/DSP regarding signal strength, etc.,
	 *   contents are always there, not configurable by host
	 */
	struct iwl5000_non_cfg_phy *ncphy =
		(struct iwl5000_non_cfg_phy *)rx_resp->non_cfg_phy_buf;
	u32 val, rssi_a, rssi_b, rssi_c, max_rssi;
	u8 agc;

	val  = le32_to_cpu(ncphy->non_cfg_phy[IWL50_RX_RES_AGC_IDX]);
	agc = (val & IWL50_OFDM_AGC_MSK) >> IWL50_OFDM_AGC_BIT_POS;

	/* Find max rssi among 3 possible receivers.
	 * These values are measured by the digital signal processor (DSP).
	 * They should stay fairly constant even as the signal strength varies,
	 *   if the radio's automatic gain control (AGC) is working right.
	 * AGC value (see below) will provide the "interesting" info.
	 */
	val = le32_to_cpu(ncphy->non_cfg_phy[IWL50_RX_RES_RSSI_AB_IDX]);
	rssi_a = (val & IWL50_OFDM_RSSI_A_MSK) >> IWL50_OFDM_RSSI_A_BIT_POS;
	rssi_b = (val & IWL50_OFDM_RSSI_B_MSK) >> IWL50_OFDM_RSSI_B_BIT_POS;
	val = le32_to_cpu(ncphy->non_cfg_phy[IWL50_RX_RES_RSSI_C_IDX]);
	rssi_c = (val & IWL50_OFDM_RSSI_C_MSK) >> IWL50_OFDM_RSSI_C_BIT_POS;

	max_rssi = max_t(u32, rssi_a, rssi_b);
	max_rssi = max_t(u32, max_rssi, rssi_c);

	IWL_DEBUG_STATS(priv, "Rssi In A %d B %d C %d Max %d AGC dB %d\n",
		rssi_a, rssi_b, rssi_c, max_rssi, agc);

	/* dBm = max_rssi dB - agc dB - constant.
	 * Higher AGC (higher radio gain) means lower signal. */
	return max_rssi - agc - IWL49_RSSI_OFFSET;
}

struct iwl_hcmd_ops iwl5000_hcmd = {
	.rxon_assoc = iwl5000_send_rxon_assoc,
	.commit_rxon = iwl_commit_rxon,
	.set_rxon_chain = iwl_set_rxon_chain,
};

struct iwl_hcmd_utils_ops iwl5000_hcmd_utils = {
	.get_hcmd_size = iwl5000_get_hcmd_size,
	.build_addsta_hcmd = iwl5000_build_addsta_hcmd,
	.gain_computation = iwl5000_gain_computation,
	.chain_noise_reset = iwl5000_chain_noise_reset,
	.rts_tx_cmd_flag = iwl5000_rts_tx_cmd_flag,
	.calc_rssi = iwl5000_calc_rssi,
};

struct iwl_lib_ops iwl5000_lib = {
	.set_hw_params = iwl5000_hw_set_hw_params,
	.txq_update_byte_cnt_tbl = iwl5000_txq_update_byte_cnt_tbl,
	.txq_inval_byte_cnt_tbl = iwl5000_txq_inval_byte_cnt_tbl,
	.txq_set_sched = iwl5000_txq_set_sched,
	.txq_agg_enable = iwl5000_txq_agg_enable,
	.txq_agg_disable = iwl5000_txq_agg_disable,
	.txq_attach_buf_to_tfd = iwl_hw_txq_attach_buf_to_tfd,
	.txq_free_tfd = iwl_hw_txq_free_tfd,
	.txq_init = iwl_hw_tx_queue_init,
	.rx_handler_setup = iwl5000_rx_handler_setup,
	.setup_deferred_work = iwl5000_setup_deferred_work,
	.is_valid_rtc_data_addr = iwl5000_hw_valid_rtc_data_addr,
	.load_ucode = iwl5000_load_ucode,
	.init_alive_start = iwl5000_init_alive_start,
	.alive_notify = iwl5000_alive_notify,
	.send_tx_power = iwl5000_send_tx_power,
	.update_chain_flags = iwl_update_chain_flags,
	.apm_ops = {
		.init =	iwl5000_apm_init,
		.reset = iwl5000_apm_reset,
		.stop = iwl5000_apm_stop,
		.config = iwl5000_nic_config,
		.set_pwr_src = iwl_set_pwr_src,
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
		.calib_version	= iwl5000_eeprom_calib_version,
		.query_addr = iwl5000_eeprom_query_addr,
	},
	.post_associate = iwl_post_associate,
	.isr = iwl_isr_ict,
	.config_ap = iwl_config_ap,
	.temp_ops = {
		.temperature = iwl5000_temperature,
		.set_ct_kill = iwl5000_set_ct_threshold,
	 },
};

static struct iwl_lib_ops iwl5150_lib = {
	.set_hw_params = iwl5000_hw_set_hw_params,
	.txq_update_byte_cnt_tbl = iwl5000_txq_update_byte_cnt_tbl,
	.txq_inval_byte_cnt_tbl = iwl5000_txq_inval_byte_cnt_tbl,
	.txq_set_sched = iwl5000_txq_set_sched,
	.txq_agg_enable = iwl5000_txq_agg_enable,
	.txq_agg_disable = iwl5000_txq_agg_disable,
	.txq_attach_buf_to_tfd = iwl_hw_txq_attach_buf_to_tfd,
	.txq_free_tfd = iwl_hw_txq_free_tfd,
	.txq_init = iwl_hw_tx_queue_init,
	.rx_handler_setup = iwl5000_rx_handler_setup,
	.setup_deferred_work = iwl5000_setup_deferred_work,
	.is_valid_rtc_data_addr = iwl5000_hw_valid_rtc_data_addr,
	.load_ucode = iwl5000_load_ucode,
	.init_alive_start = iwl5000_init_alive_start,
	.alive_notify = iwl5000_alive_notify,
	.send_tx_power = iwl5000_send_tx_power,
	.update_chain_flags = iwl_update_chain_flags,
	.apm_ops = {
		.init =	iwl5000_apm_init,
		.reset = iwl5000_apm_reset,
		.stop = iwl5000_apm_stop,
		.config = iwl5000_nic_config,
		.set_pwr_src = iwl_set_pwr_src,
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
		.calib_version	= iwl5000_eeprom_calib_version,
		.query_addr = iwl5000_eeprom_query_addr,
	},
	.post_associate = iwl_post_associate,
	.isr = iwl_isr_ict,
	.config_ap = iwl_config_ap,
	.temp_ops = {
		.temperature = iwl5150_temperature,
		.set_ct_kill = iwl5150_set_ct_threshold,
	 },
};

struct iwl_ops iwl5000_ops = {
	.lib = &iwl5000_lib,
	.hcmd = &iwl5000_hcmd,
	.utils = &iwl5000_hcmd_utils,
};

static struct iwl_ops iwl5150_ops = {
	.lib = &iwl5150_lib,
	.hcmd = &iwl5000_hcmd,
	.utils = &iwl5000_hcmd_utils,
};

struct iwl_mod_params iwl50_mod_params = {
	.num_of_queues = IWL50_NUM_QUEUES,
	.num_of_ampdu_queues = IWL50_NUM_AMPDU_QUEUES,
	.amsdu_size_8K = 1,
	.restart_fw = 1,
	/* the rest are 0 by default */
};


struct iwl_cfg iwl5300_agn_cfg = {
	.name = "5300AGN",
	.fw_name_pre = IWL5000_FW_PRE,
	.ucode_api_max = IWL5000_UCODE_API_MAX,
	.ucode_api_min = IWL5000_UCODE_API_MIN,
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl5000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.eeprom_ver = EEPROM_5000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5000_TX_POWER_VERSION,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_ABC,
	.valid_rx_ant = ANT_ABC,
	.need_pll_cfg = true,
};

struct iwl_cfg iwl5100_bg_cfg = {
	.name = "5100BG",
	.fw_name_pre = IWL5000_FW_PRE,
	.ucode_api_max = IWL5000_UCODE_API_MAX,
	.ucode_api_min = IWL5000_UCODE_API_MIN,
	.sku = IWL_SKU_G,
	.ops = &iwl5000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.eeprom_ver = EEPROM_5000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5000_TX_POWER_VERSION,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_B,
	.valid_rx_ant = ANT_AB,
	.need_pll_cfg = true,
};

struct iwl_cfg iwl5100_abg_cfg = {
	.name = "5100ABG",
	.fw_name_pre = IWL5000_FW_PRE,
	.ucode_api_max = IWL5000_UCODE_API_MAX,
	.ucode_api_min = IWL5000_UCODE_API_MIN,
	.sku = IWL_SKU_A|IWL_SKU_G,
	.ops = &iwl5000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.eeprom_ver = EEPROM_5000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5000_TX_POWER_VERSION,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_B,
	.valid_rx_ant = ANT_AB,
	.need_pll_cfg = true,
};

struct iwl_cfg iwl5100_agn_cfg = {
	.name = "5100AGN",
	.fw_name_pre = IWL5000_FW_PRE,
	.ucode_api_max = IWL5000_UCODE_API_MAX,
	.ucode_api_min = IWL5000_UCODE_API_MIN,
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl5000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.eeprom_ver = EEPROM_5000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5000_TX_POWER_VERSION,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_B,
	.valid_rx_ant = ANT_AB,
	.need_pll_cfg = true,
};

struct iwl_cfg iwl5350_agn_cfg = {
	.name = "5350AGN",
	.fw_name_pre = IWL5000_FW_PRE,
	.ucode_api_max = IWL5000_UCODE_API_MAX,
	.ucode_api_min = IWL5000_UCODE_API_MIN,
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl5000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.eeprom_ver = EEPROM_5050_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5050_TX_POWER_VERSION,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_ABC,
	.valid_rx_ant = ANT_ABC,
	.need_pll_cfg = true,
};

struct iwl_cfg iwl5150_agn_cfg = {
	.name = "5150AGN",
	.fw_name_pre = IWL5150_FW_PRE,
	.ucode_api_max = IWL5150_UCODE_API_MAX,
	.ucode_api_min = IWL5150_UCODE_API_MIN,
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl5150_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.eeprom_ver = EEPROM_5050_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5050_TX_POWER_VERSION,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_A,
	.valid_rx_ant = ANT_AB,
	.need_pll_cfg = true,
};

MODULE_FIRMWARE(IWL5000_MODULE_FIRMWARE(IWL5000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL5150_MODULE_FIRMWARE(IWL5150_UCODE_API_MAX));

module_param_named(swcrypto50, iwl50_mod_params.sw_crypto, bool, 0444);
MODULE_PARM_DESC(swcrypto50,
		  "using software crypto engine (default 0 [hardware])\n");
module_param_named(debug50, iwl50_mod_params.debug, uint, 0444);
MODULE_PARM_DESC(debug50, "50XX debug output mask");
module_param_named(queues_num50, iwl50_mod_params.num_of_queues, int, 0444);
MODULE_PARM_DESC(queues_num50, "number of hw queues in 50xx series");
module_param_named(11n_disable50, iwl50_mod_params.disable_11n, int, 0444);
MODULE_PARM_DESC(11n_disable50, "disable 50XX 11n functionality");
module_param_named(amsdu_size_8K50, iwl50_mod_params.amsdu_size_8K, int, 0444);
MODULE_PARM_DESC(amsdu_size_8K50, "enable 8K amsdu size in 50XX series");
module_param_named(fw_restart50, iwl50_mod_params.restart_fw, int, 0444);
MODULE_PARM_DESC(fw_restart50, "restart firmware in case of error");
