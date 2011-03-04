/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
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
#include <linux/sched.h>
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
#include "iwl-4965-calib.h"
#include "iwl-sta.h"
#include "iwl-4965-led.h"
#include "iwl-4965.h"
#include "iwl-4965-debugfs.h"

static int iwl4965_send_tx_power(struct iwl_priv *priv);
static int iwl4965_hw_get_temperature(struct iwl_priv *priv);

/* Highest firmware API version supported */
#define IWL4965_UCODE_API_MAX 2

/* Lowest firmware API version supported */
#define IWL4965_UCODE_API_MIN 2

#define IWL4965_FW_PRE "iwlwifi-4965-"
#define _IWL4965_MODULE_FIRMWARE(api) IWL4965_FW_PRE #api ".ucode"
#define IWL4965_MODULE_FIRMWARE(api) _IWL4965_MODULE_FIRMWARE(api)

/* check contents of special bootstrap uCode SRAM */
static int iwl4965_verify_bsm(struct iwl_priv *priv)
{
	__le32 *image = priv->ucode_boot.v_addr;
	u32 len = priv->ucode_boot.len;
	u32 reg;
	u32 val;

	IWL_DEBUG_INFO(priv, "Begin verify bsm\n");

	/* verify BSM SRAM contents */
	val = iwl_legacy_read_prph(priv, BSM_WR_DWCOUNT_REG);
	for (reg = BSM_SRAM_LOWER_BOUND;
	     reg < BSM_SRAM_LOWER_BOUND + len;
	     reg += sizeof(u32), image++) {
		val = iwl_legacy_read_prph(priv, reg);
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

	iwl_legacy_write_prph(priv, BSM_DRAM_INST_PTR_REG, pinst);
	iwl_legacy_write_prph(priv, BSM_DRAM_DATA_PTR_REG, pdata);
	iwl_legacy_write_prph(priv, BSM_DRAM_INST_BYTECOUNT_REG, inst_len);
	iwl_legacy_write_prph(priv, BSM_DRAM_DATA_BYTECOUNT_REG, data_len);

	/* Fill BSM memory with bootstrap instructions */
	for (reg_offset = BSM_SRAM_LOWER_BOUND;
	     reg_offset < BSM_SRAM_LOWER_BOUND + len;
	     reg_offset += sizeof(u32), image++)
		_iwl_legacy_write_prph(priv, reg_offset, le32_to_cpu(*image));

	ret = iwl4965_verify_bsm(priv);
	if (ret)
		return ret;

	/* Tell BSM to copy from BSM SRAM into instruction SRAM, when asked */
	iwl_legacy_write_prph(priv, BSM_WR_MEM_SRC_REG, 0x0);
	iwl_legacy_write_prph(priv,
			BSM_WR_MEM_DST_REG, IWL49_RTC_INST_LOWER_BOUND);
	iwl_legacy_write_prph(priv, BSM_WR_DWCOUNT_REG, len / sizeof(u32));

	/* Load bootstrap code into instruction SRAM now,
	 *   to prepare to load "initialize" uCode */
	iwl_legacy_write_prph(priv, BSM_WR_CTRL_REG, BSM_WR_CTRL_REG_BIT_START);

	/* Wait for load of bootstrap uCode to finish */
	for (i = 0; i < 100; i++) {
		done = iwl_legacy_read_prph(priv, BSM_WR_CTRL_REG);
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
	iwl_legacy_write_prph(priv,
			BSM_WR_CTRL_REG, BSM_WR_CTRL_REG_BIT_START_EN);


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
	iwl_legacy_write_prph(priv, BSM_DRAM_INST_PTR_REG, pinst);
	iwl_legacy_write_prph(priv, BSM_DRAM_DATA_PTR_REG, pdata);
	iwl_legacy_write_prph(priv, BSM_DRAM_DATA_BYTECOUNT_REG,
				 priv->ucode_data.len);

	/* Inst byte count must be last to set up, bit 31 signals uCode
	 *   that all new ptr/size info is in place */
	iwl_legacy_write_prph(priv, BSM_DRAM_INST_BYTECOUNT_REG,
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
	/* Bootstrap uCode has loaded initialize uCode ... verify inst image.
	 * This is a paranoid check, because we would not have gotten the
	 * "initialize" alive if code weren't properly loaded.  */
	if (iwl4965_verify_ucode(priv)) {
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

static bool iw4965_is_ht40_channel(__le32 rxon_flags)
{
	int chan_mod = le32_to_cpu(rxon_flags & RXON_FLG_CHANNEL_MODE_MSK)
				    >> RXON_FLG_CHANNEL_MODE_POS;
	return ((chan_mod == CHANNEL_MODE_PURE_40) ||
		  (chan_mod == CHANNEL_MODE_MIXED));
}

static void iwl4965_nic_config(struct iwl_priv *priv)
{
	unsigned long flags;
	u16 radio_cfg;

	spin_lock_irqsave(&priv->lock, flags);

	radio_cfg = iwl_legacy_eeprom_query16(priv, EEPROM_RADIO_CONFIG);

	/* write radio config values to register */
	if (EEPROM_RF_CFG_TYPE_MSK(radio_cfg) == EEPROM_4965_RF_CFG_TYPE_MAX)
		iwl_legacy_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			    EEPROM_RF_CFG_TYPE_MSK(radio_cfg) |
			    EEPROM_RF_CFG_STEP_MSK(radio_cfg) |
			    EEPROM_RF_CFG_DASH_MSK(radio_cfg));

	/* set CSR_HW_CONFIG_REG for uCode use */
	iwl_legacy_set_bit(priv, CSR_HW_IF_CONFIG_REG,
		    CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
		    CSR_HW_IF_CONFIG_REG_BIT_MAC_SI);

	priv->calib_info = (struct iwl_eeprom_calib_info *)
		iwl_legacy_eeprom_query_addr(priv,
				EEPROM_4965_CALIB_TXPOWER_OFFSET);

	spin_unlock_irqrestore(&priv->lock, flags);
}

/* Reset differential Rx gains in NIC to prepare for chain noise calibration.
 * Called after every association, but this runs only once!
 *  ... once chain noise is calibrated the first time, it's good forever.  */
static void iwl4965_chain_noise_reset(struct iwl_priv *priv)
{
	struct iwl_chain_noise_data *data = &(priv->chain_noise_data);

	if ((data->state == IWL_CHAIN_NOISE_ALIVE) &&
	    iwl_legacy_is_any_associated(priv)) {
		struct iwl_calib_diff_gain_cmd cmd;

		/* clear data for chain noise calibration algorithm */
		data->chain_noise_a = 0;
		data->chain_noise_b = 0;
		data->chain_noise_c = 0;
		data->chain_signal_a = 0;
		data->chain_signal_b = 0;
		data->chain_signal_c = 0;
		data->beacon_count = 0;

		memset(&cmd, 0, sizeof(cmd));
		cmd.hdr.op_code = IWL_PHY_CALIBRATE_DIFF_GAIN_CMD;
		cmd.diff_gain_a = 0;
		cmd.diff_gain_b = 0;
		cmd.diff_gain_c = 0;
		if (iwl_legacy_send_cmd_pdu(priv, REPLY_PHY_CALIBRATION_CMD,
				 sizeof(cmd), &cmd))
			IWL_ERR(priv,
				"Could not send REPLY_PHY_CALIBRATION_CMD\n");
		data->state = IWL_CHAIN_NOISE_ACCUMULATE;
		IWL_DEBUG_CALIB(priv, "Run chain_noise_calibrate\n");
	}
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
		priv->cfg->base_params->num_of_queues =
			priv->cfg->mod_params->num_of_queues;

	priv->hw_params.max_txq_num = priv->cfg->base_params->num_of_queues;
	priv->hw_params.dma_chnl_num = FH49_TCSR_CHNL_NUM;
	priv->hw_params.scd_bc_tbls_size =
			priv->cfg->base_params->num_of_queues *
			sizeof(struct iwl4965_scd_bc_tbl);
	priv->hw_params.tfd_size = sizeof(struct iwl_tfd);
	priv->hw_params.max_stations = IWL4965_STATION_COUNT;
	priv->contexts[IWL_RXON_CTX_BSS].bcast_sta_id = IWL4965_BROADCAST_ID;
	priv->hw_params.max_data_size = IWL49_RTC_DATA_SIZE;
	priv->hw_params.max_inst_size = IWL49_RTC_INST_SIZE;
	priv->hw_params.max_bsm_size = BSM_SRAM_SIZE;
	priv->hw_params.ht40_channel = BIT(IEEE80211_BAND_5GHZ);

	priv->hw_params.rx_wrt_ptr_reg = FH_RSCSR_CHNL0_WPTR;

	priv->hw_params.tx_chains_num = iwl4965_num_of_ant(priv->cfg->valid_tx_ant);
	priv->hw_params.rx_chains_num = iwl4965_num_of_ant(priv->cfg->valid_rx_ant);
	priv->hw_params.valid_tx_ant = priv->cfg->valid_tx_ant;
	priv->hw_params.valid_rx_ant = priv->cfg->valid_rx_ant;

	iwl4965_set_ct_threshold(priv);

	priv->hw_params.sens = &iwl4965_sensitivity;
	priv->hw_params.beacon_time_tsf_bits = IWL4965_EXT_BEACON_TIME_POS;

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

	ch_info = iwl_legacy_get_channel_info(priv, priv->band, channel);

	if (!iwl_legacy_is_channel_valid(ch_info))
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
	voltage = le16_to_cpu(priv->calib_info->voltage);
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
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	if (WARN_ONCE(test_bit(STATUS_SCAN_HW, &priv->status),
		      "TX Power requested while scanning!\n"))
		return -EAGAIN;

	band = priv->band == IEEE80211_BAND_2GHZ;

	is_ht40 = iw4965_is_ht40_channel(ctx->active.flags);

	if (is_ht40 && (ctx->active.flags & RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK))
		ctrl_chan_high = 1;

	cmd.band = band;
	cmd.channel = ctx->active.channel;

	ret = iwl4965_fill_txpower_tbl(priv, band,
				le16_to_cpu(ctx->active.channel),
				is_ht40, ctrl_chan_high, &cmd.tx_power);
	if (ret)
		goto out;

	ret = iwl_legacy_send_cmd_pdu(priv,
			 REPLY_TX_PWR_TABLE_CMD, sizeof(cmd), &cmd);

out:
	return ret;
}

static int iwl4965_send_rxon_assoc(struct iwl_priv *priv,
				   struct iwl_rxon_context *ctx)
{
	int ret = 0;
	struct iwl4965_rxon_assoc_cmd rxon_assoc;
	const struct iwl_legacy_rxon_cmd *rxon1 = &ctx->staging;
	const struct iwl_legacy_rxon_cmd *rxon2 = &ctx->active;

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

	rxon_assoc.flags = ctx->staging.flags;
	rxon_assoc.filter_flags = ctx->staging.filter_flags;
	rxon_assoc.ofdm_basic_rates = ctx->staging.ofdm_basic_rates;
	rxon_assoc.cck_basic_rates = ctx->staging.cck_basic_rates;
	rxon_assoc.reserved = 0;
	rxon_assoc.ofdm_ht_single_stream_basic_rates =
	    ctx->staging.ofdm_ht_single_stream_basic_rates;
	rxon_assoc.ofdm_ht_dual_stream_basic_rates =
	    ctx->staging.ofdm_ht_dual_stream_basic_rates;
	rxon_assoc.rx_chain_select_flags = ctx->staging.rx_chain;

	ret = iwl_legacy_send_cmd_pdu_async(priv, REPLY_RXON_ASSOC,
				     sizeof(rxon_assoc), &rxon_assoc, NULL);
	if (ret)
		return ret;

	return ret;
}

static int iwl4965_commit_rxon(struct iwl_priv *priv, struct iwl_rxon_context *ctx)
{
	/* cast away the const for active_rxon in this function */
	struct iwl_legacy_rxon_cmd *active_rxon = (void *)&ctx->active;
	int ret;
	bool new_assoc =
		!!(ctx->staging.filter_flags & RXON_FILTER_ASSOC_MSK);

	if (!iwl_legacy_is_alive(priv))
		return -EBUSY;

	if (!ctx->is_active)
		return 0;

	/* always get timestamp with Rx frame */
	ctx->staging.flags |= RXON_FLG_TSF2HOST_MSK;

	ret = iwl_legacy_check_rxon_cmd(priv, ctx);
	if (ret) {
		IWL_ERR(priv, "Invalid RXON configuration.  Not committing.\n");
		return -EINVAL;
	}

	/*
	 * receive commit_rxon request
	 * abort any previous channel switch if still in process
	 */
	if (priv->switch_rxon.switch_in_progress &&
	    (priv->switch_rxon.channel != ctx->staging.channel)) {
		IWL_DEBUG_11H(priv, "abort channel switch on %d\n",
		      le16_to_cpu(priv->switch_rxon.channel));
		iwl_legacy_chswitch_done(priv, false);
	}

	/* If we don't need to send a full RXON, we can use
	 * iwl_rxon_assoc_cmd which is used to reconfigure filter
	 * and other flags for the current radio configuration. */
	if (!iwl_legacy_full_rxon_required(priv, ctx)) {
		ret = iwl_legacy_send_rxon_assoc(priv, ctx);
		if (ret) {
			IWL_ERR(priv, "Error setting RXON_ASSOC (%d)\n", ret);
			return ret;
		}

		memcpy(active_rxon, &ctx->staging, sizeof(*active_rxon));
		iwl_legacy_print_rx_config_cmd(priv, ctx);
		return 0;
	}

	/* If we are currently associated and the new config requires
	 * an RXON_ASSOC and the new config wants the associated mask enabled,
	 * we must clear the associated from the active configuration
	 * before we apply the new config */
	if (iwl_legacy_is_associated_ctx(ctx) && new_assoc) {
		IWL_DEBUG_INFO(priv, "Toggling associated bit on current RXON\n");
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;

		ret = iwl_legacy_send_cmd_pdu(priv, ctx->rxon_cmd,
				       sizeof(struct iwl_legacy_rxon_cmd),
				       active_rxon);

		/* If the mask clearing failed then we set
		 * active_rxon back to what it was previously */
		if (ret) {
			active_rxon->filter_flags |= RXON_FILTER_ASSOC_MSK;
			IWL_ERR(priv, "Error clearing ASSOC_MSK (%d)\n", ret);
			return ret;
		}
		iwl_legacy_clear_ucode_stations(priv, ctx);
		iwl_legacy_restore_stations(priv, ctx);
		ret = iwl4965_restore_default_wep_keys(priv, ctx);
		if (ret) {
			IWL_ERR(priv, "Failed to restore WEP keys (%d)\n", ret);
			return ret;
		}
	}

	IWL_DEBUG_INFO(priv, "Sending RXON\n"
		       "* with%s RXON_FILTER_ASSOC_MSK\n"
		       "* channel = %d\n"
		       "* bssid = %pM\n",
		       (new_assoc ? "" : "out"),
		       le16_to_cpu(ctx->staging.channel),
		       ctx->staging.bssid_addr);

	iwl_legacy_set_rxon_hwcrypto(priv, ctx,
				!priv->cfg->mod_params->sw_crypto);

	/* Apply the new configuration
	 * RXON unassoc clears the station table in uCode so restoration of
	 * stations is needed after it (the RXON command) completes
	 */
	if (!new_assoc) {
		ret = iwl_legacy_send_cmd_pdu(priv, ctx->rxon_cmd,
			      sizeof(struct iwl_legacy_rxon_cmd), &ctx->staging);
		if (ret) {
			IWL_ERR(priv, "Error setting new RXON (%d)\n", ret);
			return ret;
		}
		IWL_DEBUG_INFO(priv, "Return from !new_assoc RXON.\n");
		memcpy(active_rxon, &ctx->staging, sizeof(*active_rxon));
		iwl_legacy_clear_ucode_stations(priv, ctx);
		iwl_legacy_restore_stations(priv, ctx);
		ret = iwl4965_restore_default_wep_keys(priv, ctx);
		if (ret) {
			IWL_ERR(priv, "Failed to restore WEP keys (%d)\n", ret);
			return ret;
		}
	}
	if (new_assoc) {
		priv->start_calib = 0;
		/* Apply the new configuration
		 * RXON assoc doesn't clear the station table in uCode,
		 */
		ret = iwl_legacy_send_cmd_pdu(priv, ctx->rxon_cmd,
			      sizeof(struct iwl_legacy_rxon_cmd), &ctx->staging);
		if (ret) {
			IWL_ERR(priv, "Error setting new RXON (%d)\n", ret);
			return ret;
		}
		memcpy(active_rxon, &ctx->staging, sizeof(*active_rxon));
	}
	iwl_legacy_print_rx_config_cmd(priv, ctx);

	iwl4965_init_sensitivity(priv);

	/* If we issue a new RXON command which required a tune then we must
	 * send a new TXPOWER command or we won't be able to Tx any frames */
	ret = iwl_legacy_set_tx_power(priv, priv->tx_power_next, true);
	if (ret) {
		IWL_ERR(priv, "Error sending TX power (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int iwl4965_hw_channel_switch(struct iwl_priv *priv,
				     struct ieee80211_channel_switch *ch_switch)
{
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	int rc;
	u8 band = 0;
	bool is_ht40 = false;
	u8 ctrl_chan_high = 0;
	struct iwl4965_channel_switch_cmd cmd;
	const struct iwl_channel_info *ch_info;
	u32 switch_time_in_usec, ucode_switch_time;
	u16 ch;
	u32 tsf_low;
	u8 switch_count;
	u16 beacon_interval = le16_to_cpu(ctx->timing.beacon_interval);
	struct ieee80211_vif *vif = ctx->vif;
	band = priv->band == IEEE80211_BAND_2GHZ;

	is_ht40 = iw4965_is_ht40_channel(ctx->staging.flags);

	if (is_ht40 &&
	    (ctx->staging.flags & RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK))
		ctrl_chan_high = 1;

	cmd.band = band;
	cmd.expect_beacon = 0;
	ch = ch_switch->channel->hw_value;
	cmd.channel = cpu_to_le16(ch);
	cmd.rxon_flags = ctx->staging.flags;
	cmd.rxon_filter_flags = ctx->staging.filter_flags;
	switch_count = ch_switch->count;
	tsf_low = ch_switch->timestamp & 0x0ffffffff;
	/*
	 * calculate the ucode channel switch time
	 * adding TSF as one of the factor for when to switch
	 */
	if ((priv->ucode_beacon_time > tsf_low) && beacon_interval) {
		if (switch_count > ((priv->ucode_beacon_time - tsf_low) /
		    beacon_interval)) {
			switch_count -= (priv->ucode_beacon_time -
				tsf_low) / beacon_interval;
		} else
			switch_count = 0;
	}
	if (switch_count <= 1)
		cmd.switch_time = cpu_to_le32(priv->ucode_beacon_time);
	else {
		switch_time_in_usec =
			vif->bss_conf.beacon_int * switch_count * TIME_UNIT;
		ucode_switch_time = iwl_legacy_usecs_to_beacons(priv,
							 switch_time_in_usec,
							 beacon_interval);
		cmd.switch_time = iwl_legacy_add_beacon_time(priv,
						      priv->ucode_beacon_time,
						      ucode_switch_time,
						      beacon_interval);
	}
	IWL_DEBUG_11H(priv, "uCode time for the switch is 0x%x\n",
		      cmd.switch_time);
	ch_info = iwl_legacy_get_channel_info(priv, priv->band, ch);
	if (ch_info)
		cmd.expect_beacon = iwl_legacy_is_channel_radar(ch_info);
	else {
		IWL_ERR(priv, "invalid channel switch from %u to %u\n",
			ctx->active.channel, ch);
		return -EFAULT;
	}

	rc = iwl4965_fill_txpower_tbl(priv, band, ch, is_ht40,
				      ctrl_chan_high, &cmd.tx_power);
	if (rc) {
		IWL_DEBUG_11H(priv, "error:%d  fill txpower_tbl\n", rc);
		return rc;
	}

	priv->switch_rxon.channel = cmd.channel;
	priv->switch_rxon.switch_in_progress = true;

	return iwl_legacy_send_cmd_pdu(priv,
			 REPLY_CHANNEL_SWITCH, sizeof(cmd), &cmd);
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
	    (priv->_4965.statistics.flag &
			STATISTICS_REPLY_FLG_HT40_MODE_MSK)) {
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
		vt = sign_extend32(R4, 23);
	else
		vt = sign_extend32(le32_to_cpu(priv->_4965.statistics.
				 general.common.temperature), 23);

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
		IWL_DEBUG_POWER(priv, "Getting cooler, delta %d\n", temp_diff);
		temp_diff = -temp_diff;
	} else if (temp_diff == 0)
		IWL_DEBUG_POWER(priv, "Temperature unchanged\n");
	else
		IWL_DEBUG_POWER(priv, "Getting warmer, delta %d\n", temp_diff);

	if (temp_diff < IWL_TEMPERATURE_THRESHOLD) {
		IWL_DEBUG_POWER(priv, " => thermal txpower calib not needed\n");
		return 0;
	}

	IWL_DEBUG_POWER(priv, " => thermal txpower calib needed\n");

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
	set_bit(STATUS_TEMPERATURE, &priv->status);

	if (!priv->disable_tx_power_cal &&
	     unlikely(!test_bit(STATUS_SCANNING, &priv->status)) &&
	     iwl4965_is_temp_calib_needed(priv))
		queue_work(priv->workqueue, &priv->txpower_work);
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

static u16 iwl4965_build_addsta_hcmd(const struct iwl_legacy_addsta_cmd *cmd,
								u8 *data)
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
	addsta->sleep_tx_count = cmd->sleep_tx_count;
	addsta->reserved1 = cpu_to_le16(0);
	addsta->reserved2 = cpu_to_le16(0);

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

		IWL_DEBUG_TX_REPLY(priv, "FrameCnt = %d, StartIdx=%d idx=%d\n",
				   agg->frame_count, agg->start_idx, idx);

		info = IEEE80211_SKB_CB(priv->txq[txq_id].txb[idx].skb);
		info->status.rates[0].count = tx_resp->failure_frame + 1;
		info->flags &= ~IEEE80211_TX_CTL_AMPDU;
		info->flags |= iwl4965_tx_status_to_mac80211(status);
		iwl4965_hwrate_to_tx_control(priv, rate_n_flags, info);

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

			hdr = iwl_legacy_tx_queue_get_hdr(priv, txq_id, idx);
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

static u8 iwl4965_find_station(struct iwl_priv *priv, const u8 *addr)
{
	int i;
	int start = 0;
	int ret = IWL_INVALID_STATION;
	unsigned long flags;

	if ((priv->iw_mode == NL80211_IFTYPE_ADHOC))
		start = IWL_STA_ID;

	if (is_broadcast_ether_addr(addr))
		return priv->contexts[IWL_RXON_CTX_BSS].bcast_sta_id;

	spin_lock_irqsave(&priv->sta_lock, flags);
	for (i = start; i < priv->hw_params.max_stations; i++)
		if (priv->stations[i].used &&
		    (!compare_ether_addr(priv->stations[i].sta.sta.addr,
					 addr))) {
			ret = i;
			goto out;
		}

	IWL_DEBUG_ASSOC_LIMIT(priv, "can not find STA %pM total %d\n",
			      addr, priv->num_stations);

 out:
	/*
	 * It may be possible that more commands interacting with stations
	 * arrive before we completed processing the adding of
	 * station
	 */
	if (ret != IWL_INVALID_STATION &&
	    (!(priv->stations[ret].used & IWL_STA_UCODE_ACTIVE) ||
	     ((priv->stations[ret].used & IWL_STA_UCODE_ACTIVE) &&
	      (priv->stations[ret].used & IWL_STA_UCODE_INPROGRESS)))) {
		IWL_ERR(priv, "Requested station info for sta %d before ready.\n",
			ret);
		ret = IWL_INVALID_STATION;
	}
	spin_unlock_irqrestore(&priv->sta_lock, flags);
	return ret;
}

static int iwl4965_get_ra_sta_id(struct iwl_priv *priv, struct ieee80211_hdr *hdr)
{
	if (priv->iw_mode == NL80211_IFTYPE_STATION) {
		return IWL_AP_ID;
	} else {
		u8 *da = ieee80211_get_DA(hdr);
		return iwl4965_find_station(priv, da);
	}
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
	int uninitialized_var(tid);
	int sta_id;
	int freed;
	u8 *qc = NULL;
	unsigned long flags;

	if ((index >= txq->q.n_bd) || (iwl_legacy_queue_used(&txq->q, index) == 0)) {
		IWL_ERR(priv, "Read index for DMA queue txq_id (%d) index %d "
			  "is out of range [0-%d] %d %d\n", txq_id,
			  index, txq->q.n_bd, txq->q.write_ptr,
			  txq->q.read_ptr);
		return;
	}

	txq->time_stamp = jiffies;
	info = IEEE80211_SKB_CB(txq->txb[txq->q.read_ptr].skb);
	memset(&info->status, 0, sizeof(info->status));

	hdr = iwl_legacy_tx_queue_get_hdr(priv, txq_id, index);
	if (ieee80211_is_data_qos(hdr->frame_control)) {
		qc = ieee80211_get_qos_ctl(hdr);
		tid = qc[0] & 0xf;
	}

	sta_id = iwl4965_get_ra_sta_id(priv, hdr);
	if (txq->sched_retry && unlikely(sta_id == IWL_INVALID_STATION)) {
		IWL_ERR(priv, "Station not known\n");
		return;
	}

	spin_lock_irqsave(&priv->sta_lock, flags);
	if (txq->sched_retry) {
		const u32 scd_ssn = iwl4965_get_scd_ssn(tx_resp);
		struct iwl_ht_agg *agg = NULL;
		WARN_ON(!qc);

		agg = &priv->stations[sta_id].tid[tid].agg;

		iwl4965_tx_status_reply_tx(priv, agg, tx_resp, txq_id, index);

		/* check if BAR is needed */
		if ((tx_resp->frame_count == 1) && !iwl4965_is_tx_success(status))
			info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;

		if (txq->q.read_ptr != (scd_ssn & 0xff)) {
			index = iwl_legacy_queue_dec_wrap(scd_ssn & 0xff,
								txq->q.n_bd);
			IWL_DEBUG_TX_REPLY(priv, "Retry scheduler reclaim scd_ssn "
					   "%d index %d\n", scd_ssn , index);
			freed = iwl4965_tx_queue_reclaim(priv, txq_id, index);
			if (qc)
				iwl4965_free_tfds_in_queue(priv, sta_id,
						       tid, freed);

			if (priv->mac80211_registered &&
			    (iwl_legacy_queue_space(&txq->q) > txq->q.low_mark)
				 && (agg->state != IWL_EMPTYING_HW_QUEUE_DELBA))
				iwl_legacy_wake_queue(priv, txq);
		}
	} else {
		info->status.rates[0].count = tx_resp->failure_frame + 1;
		info->flags |= iwl4965_tx_status_to_mac80211(status);
		iwl4965_hwrate_to_tx_control(priv,
					le32_to_cpu(tx_resp->rate_n_flags),
					info);

		IWL_DEBUG_TX_REPLY(priv, "TXQ %d status %s (0x%08x) "
				   "rate_n_flags 0x%x retries %d\n",
				   txq_id,
				   iwl4965_get_tx_fail_reason(status), status,
				   le32_to_cpu(tx_resp->rate_n_flags),
				   tx_resp->failure_frame);

		freed = iwl4965_tx_queue_reclaim(priv, txq_id, index);
		if (qc && likely(sta_id != IWL_INVALID_STATION))
			iwl4965_free_tfds_in_queue(priv, sta_id, tid, freed);
		else if (sta_id == IWL_INVALID_STATION)
			IWL_DEBUG_TX_REPLY(priv, "Station not known\n");

		if (priv->mac80211_registered &&
		    (iwl_legacy_queue_space(&txq->q) > txq->q.low_mark))
			iwl_legacy_wake_queue(priv, txq);
	}
	if (qc && likely(sta_id != IWL_INVALID_STATION))
		iwl4965_txq_check_empty(priv, sta_id, tid, txq_id);

	iwl4965_check_abort_status(priv, tx_resp->frame_count, status);

	spin_unlock_irqrestore(&priv->sta_lock, flags);
}

static void iwl4965_rx_beacon_notif(struct iwl_priv *priv,
				    struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl4965_beacon_notif *beacon = (void *)pkt->u.raw;
	u8 rate __maybe_unused =
		iwl4965_hw_get_rate(beacon->beacon_notify_hdr.rate_n_flags);

	IWL_DEBUG_RX(priv, "beacon status %#x, retries:%d ibssmgr:%d "
		"tsf:0x%.8x%.8x rate:%d\n",
		le32_to_cpu(beacon->beacon_notify_hdr.u.status) & TX_STATUS_MSK,
		beacon->beacon_notify_hdr.failure_frame,
		le32_to_cpu(beacon->ibss_mgr_status),
		le32_to_cpu(beacon->high_tsf),
		le32_to_cpu(beacon->low_tsf), rate);

	priv->ibss_manager = le32_to_cpu(beacon->ibss_mgr_status);
}

/* Set up 4965-specific Rx frame reply handlers */
static void iwl4965_rx_handler_setup(struct iwl_priv *priv)
{
	/* Legacy Rx frames */
	priv->rx_handlers[REPLY_RX] = iwl4965_rx_reply_rx;
	/* Tx response */
	priv->rx_handlers[REPLY_TX] = iwl4965_rx_reply_tx;
	priv->rx_handlers[BEACON_NOTIFICATION] = iwl4965_rx_beacon_notif;
}

static struct iwl_hcmd_ops iwl4965_hcmd = {
	.rxon_assoc = iwl4965_send_rxon_assoc,
	.commit_rxon = iwl4965_commit_rxon,
	.set_rxon_chain = iwl4965_set_rxon_chain,
};

static void iwl4965_post_scan(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	/*
	 * Since setting the RXON may have been deferred while
	 * performing the scan, fire one off if needed
	 */
	if (memcmp(&ctx->staging, &ctx->active, sizeof(ctx->staging)))
		iwl_legacy_commit_rxon(priv, ctx);
}

static void iwl4965_post_associate(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	struct ieee80211_vif *vif = ctx->vif;
	struct ieee80211_conf *conf = NULL;
	int ret = 0;

	if (!vif || !priv->is_open)
		return;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	iwl_legacy_scan_cancel_timeout(priv, 200);

	conf = iwl_legacy_ieee80211_get_hw_conf(priv->hw);

	ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	iwl_legacy_commit_rxon(priv, ctx);

	ret = iwl_legacy_send_rxon_timing(priv, ctx);
	if (ret)
		IWL_WARN(priv, "RXON timing - "
			    "Attempting to continue.\n");

	ctx->staging.filter_flags |= RXON_FILTER_ASSOC_MSK;

	iwl_legacy_set_rxon_ht(priv, &priv->current_ht_config);

	if (priv->cfg->ops->hcmd->set_rxon_chain)
		priv->cfg->ops->hcmd->set_rxon_chain(priv, ctx);

	ctx->staging.assoc_id = cpu_to_le16(vif->bss_conf.aid);

	IWL_DEBUG_ASSOC(priv, "assoc id %d beacon interval %d\n",
			vif->bss_conf.aid, vif->bss_conf.beacon_int);

	if (vif->bss_conf.use_short_preamble)
		ctx->staging.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
	else
		ctx->staging.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;

	if (ctx->staging.flags & RXON_FLG_BAND_24G_MSK) {
		if (vif->bss_conf.use_short_slot)
			ctx->staging.flags |= RXON_FLG_SHORT_SLOT_MSK;
		else
			ctx->staging.flags &= ~RXON_FLG_SHORT_SLOT_MSK;
	}

	iwl_legacy_commit_rxon(priv, ctx);

	IWL_DEBUG_ASSOC(priv, "Associated as %d to: %pM\n",
			vif->bss_conf.aid, ctx->active.bssid_addr);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		break;
	case NL80211_IFTYPE_ADHOC:
		iwl4965_send_beacon_cmd(priv);
		break;
	default:
		IWL_ERR(priv, "%s Should not be called in %d mode\n",
			  __func__, vif->type);
		break;
	}

	/* the chain noise calibration will enabled PM upon completion
	 * If chain noise has already been run, then we need to enable
	 * power management here */
	if (priv->chain_noise_data.state == IWL_CHAIN_NOISE_DONE)
		iwl_legacy_power_update_mode(priv, false);

	/* Enable Rx differential gain and sensitivity calibrations */
	iwl4965_chain_noise_reset(priv);
	priv->start_calib = 1;
}

static void iwl4965_config_ap(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	struct ieee80211_vif *vif = ctx->vif;
	int ret = 0;

	lockdep_assert_held(&priv->mutex);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	/* The following should be done only at AP bring up */
	if (!iwl_legacy_is_associated_ctx(ctx)) {

		/* RXON - unassoc (to set timing command) */
		ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		iwl_legacy_commit_rxon(priv, ctx);

		/* RXON Timing */
		ret = iwl_legacy_send_rxon_timing(priv, ctx);
		if (ret)
			IWL_WARN(priv, "RXON timing failed - "
					"Attempting to continue.\n");

		/* AP has all antennas */
		priv->chain_noise_data.active_chains =
			priv->hw_params.valid_rx_ant;
		iwl_legacy_set_rxon_ht(priv, &priv->current_ht_config);
		if (priv->cfg->ops->hcmd->set_rxon_chain)
			priv->cfg->ops->hcmd->set_rxon_chain(priv, ctx);

		ctx->staging.assoc_id = 0;

		if (vif->bss_conf.use_short_preamble)
			ctx->staging.flags |=
				RXON_FLG_SHORT_PREAMBLE_MSK;
		else
			ctx->staging.flags &=
				~RXON_FLG_SHORT_PREAMBLE_MSK;

		if (ctx->staging.flags & RXON_FLG_BAND_24G_MSK) {
			if (vif->bss_conf.use_short_slot)
				ctx->staging.flags |=
					RXON_FLG_SHORT_SLOT_MSK;
			else
				ctx->staging.flags &=
					~RXON_FLG_SHORT_SLOT_MSK;
		}
		/* need to send beacon cmd before committing assoc RXON! */
		iwl4965_send_beacon_cmd(priv);
		/* restore RXON assoc */
		ctx->staging.filter_flags |= RXON_FILTER_ASSOC_MSK;
		iwl_legacy_commit_rxon(priv, ctx);
	}
	iwl4965_send_beacon_cmd(priv);
}

static struct iwl_hcmd_utils_ops iwl4965_hcmd_utils = {
	.get_hcmd_size = iwl4965_get_hcmd_size,
	.build_addsta_hcmd = iwl4965_build_addsta_hcmd,
	.request_scan = iwl4965_request_scan,
	.post_scan = iwl4965_post_scan,
};

static struct iwl_lib_ops iwl4965_lib = {
	.set_hw_params = iwl4965_hw_set_hw_params,
	.txq_update_byte_cnt_tbl = iwl4965_txq_update_byte_cnt_tbl,
	.txq_attach_buf_to_tfd = iwl4965_hw_txq_attach_buf_to_tfd,
	.txq_free_tfd = iwl4965_hw_txq_free_tfd,
	.txq_init = iwl4965_hw_tx_queue_init,
	.rx_handler_setup = iwl4965_rx_handler_setup,
	.is_valid_rtc_data_addr = iwl4965_hw_valid_rtc_data_addr,
	.init_alive_start = iwl4965_init_alive_start,
	.load_ucode = iwl4965_load_bsm,
	.dump_nic_event_log = iwl4965_dump_nic_event_log,
	.dump_nic_error_log = iwl4965_dump_nic_error_log,
	.dump_fh = iwl4965_dump_fh,
	.set_channel_switch = iwl4965_hw_channel_switch,
	.apm_ops = {
		.init = iwl_legacy_apm_init,
		.config = iwl4965_nic_config,
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
		.acquire_semaphore = iwl4965_eeprom_acquire_semaphore,
		.release_semaphore = iwl4965_eeprom_release_semaphore,
	},
	.send_tx_power	= iwl4965_send_tx_power,
	.update_chain_flags = iwl4965_update_chain_flags,
	.temp_ops = {
		.temperature = iwl4965_temperature_calib,
	},
	.debugfs_ops = {
		.rx_stats_read = iwl4965_ucode_rx_stats_read,
		.tx_stats_read = iwl4965_ucode_tx_stats_read,
		.general_stats_read = iwl4965_ucode_general_stats_read,
	},
	.check_plcp_health = iwl4965_good_plcp_health,
};

static const struct iwl_legacy_ops iwl4965_legacy_ops = {
	.post_associate = iwl4965_post_associate,
	.config_ap = iwl4965_config_ap,
	.manage_ibss_station = iwl4965_manage_ibss_station,
	.update_bcast_stations = iwl4965_update_bcast_stations,
};

struct ieee80211_ops iwl4965_hw_ops = {
	.tx = iwl4965_mac_tx,
	.start = iwl4965_mac_start,
	.stop = iwl4965_mac_stop,
	.add_interface = iwl_legacy_mac_add_interface,
	.remove_interface = iwl_legacy_mac_remove_interface,
	.change_interface = iwl_legacy_mac_change_interface,
	.config = iwl_legacy_mac_config,
	.configure_filter = iwl4965_configure_filter,
	.set_key = iwl4965_mac_set_key,
	.update_tkip_key = iwl4965_mac_update_tkip_key,
	.conf_tx = iwl_legacy_mac_conf_tx,
	.reset_tsf = iwl_legacy_mac_reset_tsf,
	.bss_info_changed = iwl_legacy_mac_bss_info_changed,
	.ampdu_action = iwl4965_mac_ampdu_action,
	.hw_scan = iwl_legacy_mac_hw_scan,
	.sta_add = iwl4965_mac_sta_add,
	.sta_remove = iwl_legacy_mac_sta_remove,
	.channel_switch = iwl4965_mac_channel_switch,
	.tx_last_beacon = iwl_legacy_mac_tx_last_beacon,
};

static const struct iwl_ops iwl4965_ops = {
	.lib = &iwl4965_lib,
	.hcmd = &iwl4965_hcmd,
	.utils = &iwl4965_hcmd_utils,
	.led = &iwl4965_led_ops,
	.legacy = &iwl4965_legacy_ops,
	.ieee80211_ops = &iwl4965_hw_ops,
};

static struct iwl_base_params iwl4965_base_params = {
	.eeprom_size = IWL4965_EEPROM_IMG_SIZE,
	.num_of_queues = IWL49_NUM_QUEUES,
	.num_of_ampdu_queues = IWL49_NUM_AMPDU_QUEUES,
	.pll_cfg_val = 0,
	.set_l0s = true,
	.use_bsm = true,
	.led_compensation = 61,
	.chain_noise_num_beacons = IWL4965_CAL_NUM_BEACONS,
	.plcp_delta_threshold = IWL_MAX_PLCP_ERR_THRESHOLD_DEF,
	.wd_timeout = IWL_DEF_WD_TIMEOUT,
	.temperature_kelvin = true,
	.max_event_log_size = 512,
	.ucode_tracing = true,
	.sensitivity_calib_by_driver = true,
	.chain_noise_calib_by_driver = true,
};

struct iwl_cfg iwl4965_cfg = {
	.name = "Intel(R) Wireless WiFi Link 4965AGN",
	.fw_name_pre = IWL4965_FW_PRE,
	.ucode_api_max = IWL4965_UCODE_API_MAX,
	.ucode_api_min = IWL4965_UCODE_API_MIN,
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.valid_tx_ant = ANT_AB,
	.valid_rx_ant = ANT_ABC,
	.eeprom_ver = EEPROM_4965_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_4965_TX_POWER_VERSION,
	.ops = &iwl4965_ops,
	.mod_params = &iwl4965_mod_params,
	.base_params = &iwl4965_base_params,
	.led_mode = IWL_LED_BLINK,
	/*
	 * Force use of chains B and C for scan RX on 5 GHz band
	 * because the device has off-channel reception on chain A.
	 */
	.scan_rx_antennas[IEEE80211_BAND_5GHZ] = ANT_BC,
};

/* Module firmware */
MODULE_FIRMWARE(IWL4965_MODULE_FIRMWARE(IWL4965_UCODE_API_MAX));
