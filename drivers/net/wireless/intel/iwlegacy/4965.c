// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/mac80211.h>
#include <linux/etherdevice.h>
#include <asm/unaligned.h>

#include "common.h"
#include "4965.h"

/**
 * il_verify_inst_sparse - verify runtime uCode image in card vs. host,
 *   using sample data 100 bytes apart.  If these sample points are good,
 *   it's a pretty good bet that everything between them is good, too.
 */
static int
il4965_verify_inst_sparse(struct il_priv *il, __le32 * image, u32 len)
{
	u32 val;
	int ret = 0;
	u32 errcnt = 0;
	u32 i;

	D_INFO("ucode inst image size is %u\n", len);

	for (i = 0; i < len; i += 100, image += 100 / sizeof(u32)) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IL_DL_IO is set */
		il_wr(il, HBUS_TARG_MEM_RADDR, i + IL4965_RTC_INST_LOWER_BOUND);
		val = _il_rd(il, HBUS_TARG_MEM_RDAT);
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
 * il4965_verify_inst_full - verify runtime uCode image in card vs. host,
 *     looking at all data.
 */
static int
il4965_verify_inst_full(struct il_priv *il, __le32 * image, u32 len)
{
	u32 val;
	u32 save_len = len;
	int ret = 0;
	u32 errcnt;

	D_INFO("ucode inst image size is %u\n", len);

	il_wr(il, HBUS_TARG_MEM_RADDR, IL4965_RTC_INST_LOWER_BOUND);

	errcnt = 0;
	for (; len > 0; len -= sizeof(u32), image++) {
		/* read data comes through single port, auto-incr addr */
		/* NOTE: Use the debugless read so we don't flood kernel log
		 * if IL_DL_IO is set */
		val = _il_rd(il, HBUS_TARG_MEM_RDAT);
		if (val != le32_to_cpu(*image)) {
			IL_ERR("uCode INST section is invalid at "
			       "offset 0x%x, is 0x%x, s/b 0x%x\n",
			       save_len - len, val, le32_to_cpu(*image));
			ret = -EIO;
			errcnt++;
			if (errcnt >= 20)
				break;
		}
	}

	if (!errcnt)
		D_INFO("ucode image in INSTRUCTION memory is good\n");

	return ret;
}

/**
 * il4965_verify_ucode - determine which instruction image is in SRAM,
 *    and verify its contents
 */
int
il4965_verify_ucode(struct il_priv *il)
{
	__le32 *image;
	u32 len;
	int ret;

	/* Try bootstrap */
	image = (__le32 *) il->ucode_boot.v_addr;
	len = il->ucode_boot.len;
	ret = il4965_verify_inst_sparse(il, image, len);
	if (!ret) {
		D_INFO("Bootstrap uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try initialize */
	image = (__le32 *) il->ucode_init.v_addr;
	len = il->ucode_init.len;
	ret = il4965_verify_inst_sparse(il, image, len);
	if (!ret) {
		D_INFO("Initialize uCode is good in inst SRAM\n");
		return 0;
	}

	/* Try runtime/protocol */
	image = (__le32 *) il->ucode_code.v_addr;
	len = il->ucode_code.len;
	ret = il4965_verify_inst_sparse(il, image, len);
	if (!ret) {
		D_INFO("Runtime uCode is good in inst SRAM\n");
		return 0;
	}

	IL_ERR("NO VALID UCODE IMAGE IN INSTRUCTION SRAM!!\n");

	/* Since nothing seems to match, show first several data entries in
	 * instruction SRAM, so maybe visual inspection will give a clue.
	 * Selection of bootstrap image (vs. other images) is arbitrary. */
	image = (__le32 *) il->ucode_boot.v_addr;
	len = il->ucode_boot.len;
	ret = il4965_verify_inst_full(il, image, len);

	return ret;
}

/******************************************************************************
 *
 * EEPROM related functions
 *
******************************************************************************/

/*
 * The device's EEPROM semaphore prevents conflicts between driver and uCode
 * when accessing the EEPROM; each access is a series of pulses to/from the
 * EEPROM chip, not a single event, so even reads could conflict if they
 * weren't arbitrated by the semaphore.
 */
int
il4965_eeprom_acquire_semaphore(struct il_priv *il)
{
	u16 count;
	int ret;

	for (count = 0; count < EEPROM_SEM_RETRY_LIMIT; count++) {
		/* Request semaphore */
		il_set_bit(il, CSR_HW_IF_CONFIG_REG,
			   CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM);

		/* See if we got it */
		ret =
		    _il_poll_bit(il, CSR_HW_IF_CONFIG_REG,
				 CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM,
				 CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM,
				 EEPROM_SEM_TIMEOUT);
		if (ret >= 0)
			return ret;
	}

	return ret;
}

void
il4965_eeprom_release_semaphore(struct il_priv *il)
{
	il_clear_bit(il, CSR_HW_IF_CONFIG_REG,
		     CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM);

}

int
il4965_eeprom_check_version(struct il_priv *il)
{
	u16 eeprom_ver;
	u16 calib_ver;

	eeprom_ver = il_eeprom_query16(il, EEPROM_VERSION);
	calib_ver = il_eeprom_query16(il, EEPROM_4965_CALIB_VERSION_OFFSET);

	if (eeprom_ver < il->cfg->eeprom_ver ||
	    calib_ver < il->cfg->eeprom_calib_ver)
		goto err;

	IL_INFO("device EEPROM VER=0x%x, CALIB=0x%x\n", eeprom_ver, calib_ver);

	return 0;
err:
	IL_ERR("Unsupported (too old) EEPROM VER=0x%x < 0x%x "
	       "CALIB=0x%x < 0x%x\n", eeprom_ver, il->cfg->eeprom_ver,
	       calib_ver, il->cfg->eeprom_calib_ver);
	return -EINVAL;

}

void
il4965_eeprom_get_mac(const struct il_priv *il, u8 * mac)
{
	const u8 *addr = il_eeprom_query_addr(il,
					      EEPROM_MAC_ADDRESS);
	memcpy(mac, addr, ETH_ALEN);
}

/* Send led command */
static int
il4965_send_led_cmd(struct il_priv *il, struct il_led_cmd *led_cmd)
{
	struct il_host_cmd cmd = {
		.id = C_LEDS,
		.len = sizeof(struct il_led_cmd),
		.data = led_cmd,
		.flags = CMD_ASYNC,
		.callback = NULL,
	};
	u32 reg;

	reg = _il_rd(il, CSR_LED_REG);
	if (reg != (reg & CSR_LED_BSM_CTRL_MSK))
		_il_wr(il, CSR_LED_REG, reg & CSR_LED_BSM_CTRL_MSK);

	return il_send_cmd(il, &cmd);
}

/* Set led register off */
void
il4965_led_enable(struct il_priv *il)
{
	_il_wr(il, CSR_LED_REG, CSR_LED_REG_TRUN_ON);
}

static int il4965_send_tx_power(struct il_priv *il);
static int il4965_hw_get_temperature(struct il_priv *il);

/* Highest firmware API version supported */
#define IL4965_UCODE_API_MAX 2

/* Lowest firmware API version supported */
#define IL4965_UCODE_API_MIN 2

#define IL4965_FW_PRE "iwlwifi-4965-"
#define _IL4965_MODULE_FIRMWARE(api) IL4965_FW_PRE #api ".ucode"
#define IL4965_MODULE_FIRMWARE(api) _IL4965_MODULE_FIRMWARE(api)

/* check contents of special bootstrap uCode SRAM */
static int
il4965_verify_bsm(struct il_priv *il)
{
	__le32 *image = il->ucode_boot.v_addr;
	u32 len = il->ucode_boot.len;
	u32 reg;
	u32 val;

	D_INFO("Begin verify bsm\n");

	/* verify BSM SRAM contents */
	val = il_rd_prph(il, BSM_WR_DWCOUNT_REG);
	for (reg = BSM_SRAM_LOWER_BOUND; reg < BSM_SRAM_LOWER_BOUND + len;
	     reg += sizeof(u32), image++) {
		val = il_rd_prph(il, reg);
		if (val != le32_to_cpu(*image)) {
			IL_ERR("BSM uCode verification failed at "
			       "addr 0x%08X+%u (of %u), is 0x%x, s/b 0x%x\n",
			       BSM_SRAM_LOWER_BOUND, reg - BSM_SRAM_LOWER_BOUND,
			       len, val, le32_to_cpu(*image));
			return -EIO;
		}
	}

	D_INFO("BSM bootstrap uCode image OK\n");

	return 0;
}

/**
 * il4965_load_bsm - Load bootstrap instructions
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
static int
il4965_load_bsm(struct il_priv *il)
{
	__le32 *image = il->ucode_boot.v_addr;
	u32 len = il->ucode_boot.len;
	dma_addr_t pinst;
	dma_addr_t pdata;
	u32 inst_len;
	u32 data_len;
	int i;
	u32 done;
	u32 reg_offset;
	int ret;

	D_INFO("Begin load bsm\n");

	il->ucode_type = UCODE_RT;

	/* make sure bootstrap program is no larger than BSM's SRAM size */
	if (len > IL49_MAX_BSM_SIZE)
		return -EINVAL;

	/* Tell bootstrap uCode where to find the "Initialize" uCode
	 *   in host DRAM ... host DRAM physical address bits 35:4 for 4965.
	 * NOTE:  il_init_alive_start() will replace these values,
	 *        after the "initialize" uCode has run, to point to
	 *        runtime/protocol instructions and backup data cache.
	 */
	pinst = il->ucode_init.p_addr >> 4;
	pdata = il->ucode_init_data.p_addr >> 4;
	inst_len = il->ucode_init.len;
	data_len = il->ucode_init_data.len;

	il_wr_prph(il, BSM_DRAM_INST_PTR_REG, pinst);
	il_wr_prph(il, BSM_DRAM_DATA_PTR_REG, pdata);
	il_wr_prph(il, BSM_DRAM_INST_BYTECOUNT_REG, inst_len);
	il_wr_prph(il, BSM_DRAM_DATA_BYTECOUNT_REG, data_len);

	/* Fill BSM memory with bootstrap instructions */
	for (reg_offset = BSM_SRAM_LOWER_BOUND;
	     reg_offset < BSM_SRAM_LOWER_BOUND + len;
	     reg_offset += sizeof(u32), image++)
		_il_wr_prph(il, reg_offset, le32_to_cpu(*image));

	ret = il4965_verify_bsm(il);
	if (ret)
		return ret;

	/* Tell BSM to copy from BSM SRAM into instruction SRAM, when asked */
	il_wr_prph(il, BSM_WR_MEM_SRC_REG, 0x0);
	il_wr_prph(il, BSM_WR_MEM_DST_REG, IL49_RTC_INST_LOWER_BOUND);
	il_wr_prph(il, BSM_WR_DWCOUNT_REG, len / sizeof(u32));

	/* Load bootstrap code into instruction SRAM now,
	 *   to prepare to load "initialize" uCode */
	il_wr_prph(il, BSM_WR_CTRL_REG, BSM_WR_CTRL_REG_BIT_START);

	/* Wait for load of bootstrap uCode to finish */
	for (i = 0; i < 100; i++) {
		done = il_rd_prph(il, BSM_WR_CTRL_REG);
		if (!(done & BSM_WR_CTRL_REG_BIT_START))
			break;
		udelay(10);
	}
	if (i < 100)
		D_INFO("BSM write complete, poll %d iterations\n", i);
	else {
		IL_ERR("BSM write did not complete!\n");
		return -EIO;
	}

	/* Enable future boot loads whenever power management unit triggers it
	 *   (e.g. when powering back up after power-save shutdown) */
	il_wr_prph(il, BSM_WR_CTRL_REG, BSM_WR_CTRL_REG_BIT_START_EN);

	return 0;
}

/**
 * il4965_set_ucode_ptrs - Set uCode address location
 *
 * Tell initialization uCode where to find runtime uCode.
 *
 * BSM registers initially contain pointers to initialization uCode.
 * We need to replace them to load runtime uCode inst and data,
 * and to save runtime data when powering down.
 */
static int
il4965_set_ucode_ptrs(struct il_priv *il)
{
	dma_addr_t pinst;
	dma_addr_t pdata;
	int ret = 0;

	/* bits 35:4 for 4965 */
	pinst = il->ucode_code.p_addr >> 4;
	pdata = il->ucode_data_backup.p_addr >> 4;

	/* Tell bootstrap uCode where to find image to load */
	il_wr_prph(il, BSM_DRAM_INST_PTR_REG, pinst);
	il_wr_prph(il, BSM_DRAM_DATA_PTR_REG, pdata);
	il_wr_prph(il, BSM_DRAM_DATA_BYTECOUNT_REG, il->ucode_data.len);

	/* Inst byte count must be last to set up, bit 31 signals uCode
	 *   that all new ptr/size info is in place */
	il_wr_prph(il, BSM_DRAM_INST_BYTECOUNT_REG,
		   il->ucode_code.len | BSM_DRAM_INST_LOAD);
	D_INFO("Runtime uCode pointers are set.\n");

	return ret;
}

/**
 * il4965_init_alive_start - Called after N_ALIVE notification received
 *
 * Called after N_ALIVE notification received from "initialize" uCode.
 *
 * The 4965 "initialize" ALIVE reply contains calibration data for:
 *   Voltage, temperature, and MIMO tx gain correction, now stored in il
 *   (3945 does not contain this data).
 *
 * Tell "initialize" uCode to go ahead and load the runtime uCode.
*/
static void
il4965_init_alive_start(struct il_priv *il)
{
	/* Bootstrap uCode has loaded initialize uCode ... verify inst image.
	 * This is a paranoid check, because we would not have gotten the
	 * "initialize" alive if code weren't properly loaded.  */
	if (il4965_verify_ucode(il)) {
		/* Runtime instruction load was bad;
		 * take it all the way back down so we can try again */
		D_INFO("Bad \"initialize\" uCode load.\n");
		goto restart;
	}

	/* Calculate temperature */
	il->temperature = il4965_hw_get_temperature(il);

	/* Send pointers to protocol/runtime uCode image ... init code will
	 * load and launch runtime uCode, which will send us another "Alive"
	 * notification. */
	D_INFO("Initialization Alive received.\n");
	if (il4965_set_ucode_ptrs(il)) {
		/* Runtime instruction load won't happen;
		 * take it all the way back down so we can try again */
		D_INFO("Couldn't set up uCode pointers.\n");
		goto restart;
	}
	return;

restart:
	queue_work(il->workqueue, &il->restart);
}

static bool
iw4965_is_ht40_channel(__le32 rxon_flags)
{
	int chan_mod =
	    le32_to_cpu(rxon_flags & RXON_FLG_CHANNEL_MODE_MSK) >>
	    RXON_FLG_CHANNEL_MODE_POS;
	return (chan_mod == CHANNEL_MODE_PURE_40 ||
		chan_mod == CHANNEL_MODE_MIXED);
}

void
il4965_nic_config(struct il_priv *il)
{
	unsigned long flags;
	u16 radio_cfg;

	spin_lock_irqsave(&il->lock, flags);

	radio_cfg = il_eeprom_query16(il, EEPROM_RADIO_CONFIG);

	/* write radio config values to register */
	if (EEPROM_RF_CFG_TYPE_MSK(radio_cfg) == EEPROM_4965_RF_CFG_TYPE_MAX)
		il_set_bit(il, CSR_HW_IF_CONFIG_REG,
			   EEPROM_RF_CFG_TYPE_MSK(radio_cfg) |
			   EEPROM_RF_CFG_STEP_MSK(radio_cfg) |
			   EEPROM_RF_CFG_DASH_MSK(radio_cfg));

	/* set CSR_HW_CONFIG_REG for uCode use */
	il_set_bit(il, CSR_HW_IF_CONFIG_REG,
		   CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
		   CSR_HW_IF_CONFIG_REG_BIT_MAC_SI);

	il->calib_info =
	    (struct il_eeprom_calib_info *)
	    il_eeprom_query_addr(il, EEPROM_4965_CALIB_TXPOWER_OFFSET);

	spin_unlock_irqrestore(&il->lock, flags);
}

/* Reset differential Rx gains in NIC to prepare for chain noise calibration.
 * Called after every association, but this runs only once!
 *  ... once chain noise is calibrated the first time, it's good forever.  */
static void
il4965_chain_noise_reset(struct il_priv *il)
{
	struct il_chain_noise_data *data = &(il->chain_noise_data);

	if (data->state == IL_CHAIN_NOISE_ALIVE && il_is_any_associated(il)) {
		struct il_calib_diff_gain_cmd cmd;

		/* clear data for chain noise calibration algorithm */
		data->chain_noise_a = 0;
		data->chain_noise_b = 0;
		data->chain_noise_c = 0;
		data->chain_signal_a = 0;
		data->chain_signal_b = 0;
		data->chain_signal_c = 0;
		data->beacon_count = 0;

		memset(&cmd, 0, sizeof(cmd));
		cmd.hdr.op_code = IL_PHY_CALIBRATE_DIFF_GAIN_CMD;
		cmd.diff_gain_a = 0;
		cmd.diff_gain_b = 0;
		cmd.diff_gain_c = 0;
		if (il_send_cmd_pdu(il, C_PHY_CALIBRATION, sizeof(cmd), &cmd))
			IL_ERR("Could not send C_PHY_CALIBRATION\n");
		data->state = IL_CHAIN_NOISE_ACCUMULATE;
		D_CALIB("Run chain_noise_calibrate\n");
	}
}

static s32
il4965_math_div_round(s32 num, s32 denom, s32 * res)
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
	*res = ((num * 2 + denom) / (denom * 2)) * sign;

	return 1;
}

/**
 * il4965_get_voltage_compensation - Power supply voltage comp for txpower
 *
 * Determines power supply voltage compensation for txpower calculations.
 * Returns number of 1/2-dB steps to subtract from gain table idx,
 * to compensate for difference between power supply voltage during
 * factory measurements, vs. current power supply voltage.
 *
 * Voltage indication is higher for lower voltage.
 * Lower voltage requires more gain (lower gain table idx).
 */
static s32
il4965_get_voltage_compensation(s32 eeprom_voltage, s32 current_voltage)
{
	s32 comp = 0;

	if (TX_POWER_IL_ILLEGAL_VOLTAGE == eeprom_voltage ||
	    TX_POWER_IL_ILLEGAL_VOLTAGE == current_voltage)
		return 0;

	il4965_math_div_round(current_voltage - eeprom_voltage,
			      TX_POWER_IL_VOLTAGE_CODES_PER_03V, &comp);

	if (current_voltage > eeprom_voltage)
		comp *= 2;
	if ((comp < -2) || (comp > 2))
		comp = 0;

	return comp;
}

static s32
il4965_get_tx_atten_grp(u16 channel)
{
	if (channel >= CALIB_IL_TX_ATTEN_GR5_FCH &&
	    channel <= CALIB_IL_TX_ATTEN_GR5_LCH)
		return CALIB_CH_GROUP_5;

	if (channel >= CALIB_IL_TX_ATTEN_GR1_FCH &&
	    channel <= CALIB_IL_TX_ATTEN_GR1_LCH)
		return CALIB_CH_GROUP_1;

	if (channel >= CALIB_IL_TX_ATTEN_GR2_FCH &&
	    channel <= CALIB_IL_TX_ATTEN_GR2_LCH)
		return CALIB_CH_GROUP_2;

	if (channel >= CALIB_IL_TX_ATTEN_GR3_FCH &&
	    channel <= CALIB_IL_TX_ATTEN_GR3_LCH)
		return CALIB_CH_GROUP_3;

	if (channel >= CALIB_IL_TX_ATTEN_GR4_FCH &&
	    channel <= CALIB_IL_TX_ATTEN_GR4_LCH)
		return CALIB_CH_GROUP_4;

	return -EINVAL;
}

static u32
il4965_get_sub_band(const struct il_priv *il, u32 channel)
{
	s32 b = -1;

	for (b = 0; b < EEPROM_TX_POWER_BANDS; b++) {
		if (il->calib_info->band_info[b].ch_from == 0)
			continue;

		if (channel >= il->calib_info->band_info[b].ch_from &&
		    channel <= il->calib_info->band_info[b].ch_to)
			break;
	}

	return b;
}

static s32
il4965_interpolate_value(s32 x, s32 x1, s32 y1, s32 x2, s32 y2)
{
	s32 val;

	if (x2 == x1)
		return y1;
	else {
		il4965_math_div_round((x2 - x) * (y1 - y2), (x2 - x1), &val);
		return val + y2;
	}
}

/**
 * il4965_interpolate_chan - Interpolate factory measurements for one channel
 *
 * Interpolates factory measurements from the two sample channels within a
 * sub-band, to apply to channel of interest.  Interpolation is proportional to
 * differences in channel frequencies, which is proportional to differences
 * in channel number.
 */
static int
il4965_interpolate_chan(struct il_priv *il, u32 channel,
			struct il_eeprom_calib_ch_info *chan_info)
{
	s32 s = -1;
	u32 c;
	u32 m;
	const struct il_eeprom_calib_measure *m1;
	const struct il_eeprom_calib_measure *m2;
	struct il_eeprom_calib_measure *omeas;
	u32 ch_i1;
	u32 ch_i2;

	s = il4965_get_sub_band(il, channel);
	if (s >= EEPROM_TX_POWER_BANDS) {
		IL_ERR("Tx Power can not find channel %d\n", channel);
		return -1;
	}

	ch_i1 = il->calib_info->band_info[s].ch1.ch_num;
	ch_i2 = il->calib_info->band_info[s].ch2.ch_num;
	chan_info->ch_num = (u8) channel;

	D_TXPOWER("channel %d subband %d factory cal ch %d & %d\n", channel, s,
		  ch_i1, ch_i2);

	for (c = 0; c < EEPROM_TX_POWER_TX_CHAINS; c++) {
		for (m = 0; m < EEPROM_TX_POWER_MEASUREMENTS; m++) {
			m1 = &(il->calib_info->band_info[s].ch1.
			       measurements[c][m]);
			m2 = &(il->calib_info->band_info[s].ch2.
			       measurements[c][m]);
			omeas = &(chan_info->measurements[c][m]);

			omeas->actual_pow =
			    (u8) il4965_interpolate_value(channel, ch_i1,
							  m1->actual_pow, ch_i2,
							  m2->actual_pow);
			omeas->gain_idx =
			    (u8) il4965_interpolate_value(channel, ch_i1,
							  m1->gain_idx, ch_i2,
							  m2->gain_idx);
			omeas->temperature =
			    (u8) il4965_interpolate_value(channel, ch_i1,
							  m1->temperature,
							  ch_i2,
							  m2->temperature);
			omeas->pa_det =
			    (s8) il4965_interpolate_value(channel, ch_i1,
							  m1->pa_det, ch_i2,
							  m2->pa_det);

			D_TXPOWER("chain %d meas %d AP1=%d AP2=%d AP=%d\n", c,
				  m, m1->actual_pow, m2->actual_pow,
				  omeas->actual_pow);
			D_TXPOWER("chain %d meas %d NI1=%d NI2=%d NI=%d\n", c,
				  m, m1->gain_idx, m2->gain_idx,
				  omeas->gain_idx);
			D_TXPOWER("chain %d meas %d PA1=%d PA2=%d PA=%d\n", c,
				  m, m1->pa_det, m2->pa_det, omeas->pa_det);
			D_TXPOWER("chain %d meas %d  T1=%d  T2=%d  T=%d\n", c,
				  m, m1->temperature, m2->temperature,
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
static struct il4965_txpower_comp_entry {
	s32 degrees_per_05db_a;
	s32 degrees_per_05db_a_denom;
} tx_power_cmp_tble[CALIB_CH_GROUP_MAX] = {
	{
	9, 2},			/* group 0 5.2, ch  34-43 */
	{
	4, 1},			/* group 1 5.2, ch  44-70 */
	{
	4, 1},			/* group 2 5.2, ch  71-124 */
	{
	4, 1},			/* group 3 5.2, ch 125-200 */
	{
	3, 1}			/* group 4 2.4, ch   all */
};

static s32
get_min_power_idx(s32 rate_power_idx, u32 band)
{
	if (!band) {
		if ((rate_power_idx & 7) <= 4)
			return MIN_TX_GAIN_IDX_52GHZ_EXT;
	}
	return MIN_TX_GAIN_IDX;
}

struct gain_entry {
	u8 dsp;
	u8 radio;
};

static const struct gain_entry gain_table[2][108] = {
	/* 5.2GHz power gain idx table */
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
	/* 2.4GHz power gain idx table */
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

static int
il4965_fill_txpower_tbl(struct il_priv *il, u8 band, u16 channel, u8 is_ht40,
			u8 ctrl_chan_high,
			struct il4965_tx_power_db *tx_power_tbl)
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
	const struct il_channel_info *ch_info = NULL;
	struct il_eeprom_calib_ch_info ch_eeprom_info;
	const struct il_eeprom_calib_measure *measurement;
	s16 voltage;
	s32 init_voltage;
	s32 voltage_compensation;
	s32 degrees_per_05db_num;
	s32 degrees_per_05db_denom;
	s32 factory_temp;
	s32 temperature_comp[2];
	s32 factory_gain_idx[2];
	s32 factory_actual_pwr[2];
	s32 power_idx;

	/* tx_power_user_lmt is in dBm, convert to half-dBm (half-dB units
	 *   are used for idxing into txpower table) */
	user_target_power = 2 * il->tx_power_user_lmt;

	/* Get current (RXON) channel, band, width */
	D_TXPOWER("chan %d band %d is_ht40 %d\n", channel, band, is_ht40);

	ch_info = il_get_channel_info(il, il->band, channel);

	if (!il_is_channel_valid(ch_info))
		return -EINVAL;

	/* get txatten group, used to select 1) thermal txpower adjustment
	 *   and 2) mimo txpower balance between Tx chains. */
	txatten_grp = il4965_get_tx_atten_grp(channel);
	if (txatten_grp < 0) {
		IL_ERR("Can't find txatten group for channel %d.\n", channel);
		return txatten_grp;
	}

	D_TXPOWER("channel %d belongs to txatten group %d\n", channel,
		  txatten_grp);

	if (is_ht40) {
		if (ctrl_chan_high)
			channel -= 2;
		else
			channel += 2;
	}

	/* hardware txpower limits ...
	 * saturation (clipping distortion) txpowers are in half-dBm */
	if (band)
		saturation_power = il->calib_info->saturation_power24;
	else
		saturation_power = il->calib_info->saturation_power52;

	if (saturation_power < IL_TX_POWER_SATURATION_MIN ||
	    saturation_power > IL_TX_POWER_SATURATION_MAX) {
		if (band)
			saturation_power = IL_TX_POWER_DEFAULT_SATURATION_24;
		else
			saturation_power = IL_TX_POWER_DEFAULT_SATURATION_52;
	}

	/* regulatory txpower limits ... reg_limit values are in half-dBm,
	 *   max_power_avg values are in dBm, convert * 2 */
	if (is_ht40)
		reg_limit = ch_info->ht40_max_power_avg * 2;
	else
		reg_limit = ch_info->max_power_avg * 2;

	if ((reg_limit < IL_TX_POWER_REGULATORY_MIN) ||
	    (reg_limit > IL_TX_POWER_REGULATORY_MAX)) {
		if (band)
			reg_limit = IL_TX_POWER_DEFAULT_REGULATORY_24;
		else
			reg_limit = IL_TX_POWER_DEFAULT_REGULATORY_52;
	}

	/* Interpolate txpower calibration values for this channel,
	 *   based on factory calibration tests on spaced channels. */
	il4965_interpolate_chan(il, channel, &ch_eeprom_info);

	/* calculate tx gain adjustment based on power supply voltage */
	voltage = le16_to_cpu(il->calib_info->voltage);
	init_voltage = (s32) le32_to_cpu(il->card_alive_init.voltage);
	voltage_compensation =
	    il4965_get_voltage_compensation(voltage, init_voltage);

	D_TXPOWER("curr volt %d eeprom volt %d volt comp %d\n", init_voltage,
		  voltage, voltage_compensation);

	/* get current temperature (Celsius) */
	current_temp = max(il->temperature, IL_TX_POWER_TEMPERATURE_MIN);
	current_temp = min(il->temperature, IL_TX_POWER_TEMPERATURE_MAX);
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
		il4965_math_div_round((current_temp -
				       factory_temp) * degrees_per_05db_denom,
				      degrees_per_05db_num,
				      &temperature_comp[c]);

		factory_gain_idx[c] = measurement->gain_idx;
		factory_actual_pwr[c] = measurement->actual_pow;

		D_TXPOWER("chain = %d\n", c);
		D_TXPOWER("fctry tmp %d, " "curr tmp %d, comp %d steps\n",
			  factory_temp, current_temp, temperature_comp[c]);

		D_TXPOWER("fctry idx %d, fctry pwr %d\n", factory_gain_idx[c],
			  factory_actual_pwr[c]);
	}

	/* for each of 33 bit-rates (including 1 for CCK) */
	for (i = 0; i < POWER_TBL_NUM_ENTRIES; i++) {
		u8 is_mimo_rate;
		union il4965_tx_power_dual_stream tx_power;

		/* for mimo, reduce each chain's txpower by half
		 * (3dB, 6 steps), so total output power is regulatory
		 * compliant. */
		if (i & 0x8) {
			current_regulatory =
			    reg_limit -
			    IL_TX_POWER_MIMO_REGULATORY_COMPENSATION;
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

		D_TXPOWER("rate %d sat %d reg %d usr %d tgt %d\n", i,
			  saturation_power - back_off_table[i],
			  current_regulatory, user_target_power, target_power);

		/* for each of 2 Tx chains (radio transmitters) */
		for (c = 0; c < 2; c++) {
			s32 atten_value;

			if (is_mimo_rate)
				atten_value =
				    (s32) le32_to_cpu(il->card_alive_init.
						      tx_atten[txatten_grp][c]);
			else
				atten_value = 0;

			/* calculate idx; higher idx means lower txpower */
			power_idx =
			    (u8) (factory_gain_idx[c] -
				  (target_power - factory_actual_pwr[c]) -
				  temperature_comp[c] - voltage_compensation +
				  atten_value);

/*			D_TXPOWER("calculated txpower idx %d\n",
						power_idx); */

			if (power_idx < get_min_power_idx(i, band))
				power_idx = get_min_power_idx(i, band);

			/* adjust 5 GHz idx to support negative idxes */
			if (!band)
				power_idx += 9;

			/* CCK, rate 32, reduce txpower for CCK */
			if (i == POWER_TBL_CCK_ENTRY)
				power_idx +=
				    IL_TX_POWER_CCK_COMPENSATION_C_STEP;

			/* stay within the table! */
			if (power_idx > 107) {
				IL_WARN("txpower idx %d > 107\n", power_idx);
				power_idx = 107;
			}
			if (power_idx < 0) {
				IL_WARN("txpower idx %d < 0\n", power_idx);
				power_idx = 0;
			}

			/* fill txpower command for this rate/chain */
			tx_power.s.radio_tx_gain[c] =
			    gain_table[band][power_idx].radio;
			tx_power.s.dsp_predis_atten[c] =
			    gain_table[band][power_idx].dsp;

			D_TXPOWER("chain %d mimo %d idx %d "
				  "gain 0x%02x dsp %d\n", c, atten_value,
				  power_idx, tx_power.s.radio_tx_gain[c],
				  tx_power.s.dsp_predis_atten[c]);
		}		/* for each chain */

		tx_power_tbl->power_tbl[i].dw = cpu_to_le32(tx_power.dw);

	}			/* for each rate */

	return 0;
}

/**
 * il4965_send_tx_power - Configure the TXPOWER level user limit
 *
 * Uses the active RXON for channel, band, and characteristics (ht40, high)
 * The power limit is taken from il->tx_power_user_lmt.
 */
static int
il4965_send_tx_power(struct il_priv *il)
{
	struct il4965_txpowertable_cmd cmd = { 0 };
	int ret;
	u8 band = 0;
	bool is_ht40 = false;
	u8 ctrl_chan_high = 0;

	if (WARN_ONCE
	    (test_bit(S_SCAN_HW, &il->status),
	     "TX Power requested while scanning!\n"))
		return -EAGAIN;

	band = il->band == NL80211_BAND_2GHZ;

	is_ht40 = iw4965_is_ht40_channel(il->active.flags);

	if (is_ht40 && (il->active.flags & RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK))
		ctrl_chan_high = 1;

	cmd.band = band;
	cmd.channel = il->active.channel;

	ret =
	    il4965_fill_txpower_tbl(il, band, le16_to_cpu(il->active.channel),
				    is_ht40, ctrl_chan_high, &cmd.tx_power);
	if (ret)
		goto out;

	ret = il_send_cmd_pdu(il, C_TX_PWR_TBL, sizeof(cmd), &cmd);

out:
	return ret;
}

static int
il4965_send_rxon_assoc(struct il_priv *il)
{
	int ret = 0;
	struct il4965_rxon_assoc_cmd rxon_assoc;
	const struct il_rxon_cmd *rxon1 = &il->staging;
	const struct il_rxon_cmd *rxon2 = &il->active;

	lockdep_assert_held(&il->mutex);

	if (rxon1->flags == rxon2->flags &&
	    rxon1->filter_flags == rxon2->filter_flags &&
	    rxon1->cck_basic_rates == rxon2->cck_basic_rates &&
	    rxon1->ofdm_ht_single_stream_basic_rates ==
	    rxon2->ofdm_ht_single_stream_basic_rates &&
	    rxon1->ofdm_ht_dual_stream_basic_rates ==
	    rxon2->ofdm_ht_dual_stream_basic_rates &&
	    rxon1->rx_chain == rxon2->rx_chain &&
	    rxon1->ofdm_basic_rates == rxon2->ofdm_basic_rates) {
		D_INFO("Using current RXON_ASSOC.  Not resending.\n");
		return 0;
	}

	rxon_assoc.flags = il->staging.flags;
	rxon_assoc.filter_flags = il->staging.filter_flags;
	rxon_assoc.ofdm_basic_rates = il->staging.ofdm_basic_rates;
	rxon_assoc.cck_basic_rates = il->staging.cck_basic_rates;
	rxon_assoc.reserved = 0;
	rxon_assoc.ofdm_ht_single_stream_basic_rates =
	    il->staging.ofdm_ht_single_stream_basic_rates;
	rxon_assoc.ofdm_ht_dual_stream_basic_rates =
	    il->staging.ofdm_ht_dual_stream_basic_rates;
	rxon_assoc.rx_chain_select_flags = il->staging.rx_chain;

	ret =
	    il_send_cmd_pdu_async(il, C_RXON_ASSOC, sizeof(rxon_assoc),
				  &rxon_assoc, NULL);

	return ret;
}

static int
il4965_commit_rxon(struct il_priv *il)
{
	/* cast away the const for active_rxon in this function */
	struct il_rxon_cmd *active_rxon = (void *)&il->active;
	int ret;
	bool new_assoc = !!(il->staging.filter_flags & RXON_FILTER_ASSOC_MSK);

	if (!il_is_alive(il))
		return -EBUSY;

	/* always get timestamp with Rx frame */
	il->staging.flags |= RXON_FLG_TSF2HOST_MSK;

	ret = il_check_rxon_cmd(il);
	if (ret) {
		IL_ERR("Invalid RXON configuration.  Not committing.\n");
		return -EINVAL;
	}

	/*
	 * receive commit_rxon request
	 * abort any previous channel switch if still in process
	 */
	if (test_bit(S_CHANNEL_SWITCH_PENDING, &il->status) &&
	    il->switch_channel != il->staging.channel) {
		D_11H("abort channel switch on %d\n",
		      le16_to_cpu(il->switch_channel));
		il_chswitch_done(il, false);
	}

	/* If we don't need to send a full RXON, we can use
	 * il_rxon_assoc_cmd which is used to reconfigure filter
	 * and other flags for the current radio configuration. */
	if (!il_full_rxon_required(il)) {
		ret = il_send_rxon_assoc(il);
		if (ret) {
			IL_ERR("Error setting RXON_ASSOC (%d)\n", ret);
			return ret;
		}

		memcpy(active_rxon, &il->staging, sizeof(*active_rxon));
		il_print_rx_config_cmd(il);
		/*
		 * We do not commit tx power settings while channel changing,
		 * do it now if tx power changed.
		 */
		il_set_tx_power(il, il->tx_power_next, false);
		return 0;
	}

	/* If we are currently associated and the new config requires
	 * an RXON_ASSOC and the new config wants the associated mask enabled,
	 * we must clear the associated from the active configuration
	 * before we apply the new config */
	if (il_is_associated(il) && new_assoc) {
		D_INFO("Toggling associated bit on current RXON\n");
		active_rxon->filter_flags &= ~RXON_FILTER_ASSOC_MSK;

		ret =
		    il_send_cmd_pdu(il, C_RXON,
				    sizeof(struct il_rxon_cmd), active_rxon);

		/* If the mask clearing failed then we set
		 * active_rxon back to what it was previously */
		if (ret) {
			active_rxon->filter_flags |= RXON_FILTER_ASSOC_MSK;
			IL_ERR("Error clearing ASSOC_MSK (%d)\n", ret);
			return ret;
		}
		il_clear_ucode_stations(il);
		il_restore_stations(il);
		ret = il4965_restore_default_wep_keys(il);
		if (ret) {
			IL_ERR("Failed to restore WEP keys (%d)\n", ret);
			return ret;
		}
	}

	D_INFO("Sending RXON\n" "* with%s RXON_FILTER_ASSOC_MSK\n"
	       "* channel = %d\n" "* bssid = %pM\n", (new_assoc ? "" : "out"),
	       le16_to_cpu(il->staging.channel), il->staging.bssid_addr);

	il_set_rxon_hwcrypto(il, !il->cfg->mod_params->sw_crypto);

	/* Apply the new configuration
	 * RXON unassoc clears the station table in uCode so restoration of
	 * stations is needed after it (the RXON command) completes
	 */
	if (!new_assoc) {
		ret =
		    il_send_cmd_pdu(il, C_RXON,
				    sizeof(struct il_rxon_cmd), &il->staging);
		if (ret) {
			IL_ERR("Error setting new RXON (%d)\n", ret);
			return ret;
		}
		D_INFO("Return from !new_assoc RXON.\n");
		memcpy(active_rxon, &il->staging, sizeof(*active_rxon));
		il_clear_ucode_stations(il);
		il_restore_stations(il);
		ret = il4965_restore_default_wep_keys(il);
		if (ret) {
			IL_ERR("Failed to restore WEP keys (%d)\n", ret);
			return ret;
		}
	}
	if (new_assoc) {
		il->start_calib = 0;
		/* Apply the new configuration
		 * RXON assoc doesn't clear the station table in uCode,
		 */
		ret =
		    il_send_cmd_pdu(il, C_RXON,
				    sizeof(struct il_rxon_cmd), &il->staging);
		if (ret) {
			IL_ERR("Error setting new RXON (%d)\n", ret);
			return ret;
		}
		memcpy(active_rxon, &il->staging, sizeof(*active_rxon));
	}
	il_print_rx_config_cmd(il);

	il4965_init_sensitivity(il);

	/* If we issue a new RXON command which required a tune then we must
	 * send a new TXPOWER command or we won't be able to Tx any frames */
	ret = il_set_tx_power(il, il->tx_power_next, true);
	if (ret) {
		IL_ERR("Error sending TX power (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int
il4965_hw_channel_switch(struct il_priv *il,
			 struct ieee80211_channel_switch *ch_switch)
{
	int rc;
	u8 band = 0;
	bool is_ht40 = false;
	u8 ctrl_chan_high = 0;
	struct il4965_channel_switch_cmd cmd;
	const struct il_channel_info *ch_info;
	u32 switch_time_in_usec, ucode_switch_time;
	u16 ch;
	u32 tsf_low;
	u8 switch_count;
	u16 beacon_interval = le16_to_cpu(il->timing.beacon_interval);
	struct ieee80211_vif *vif = il->vif;
	band = (il->band == NL80211_BAND_2GHZ);

	if (WARN_ON_ONCE(vif == NULL))
		return -EIO;

	is_ht40 = iw4965_is_ht40_channel(il->staging.flags);

	if (is_ht40 && (il->staging.flags & RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK))
		ctrl_chan_high = 1;

	cmd.band = band;
	cmd.expect_beacon = 0;
	ch = ch_switch->chandef.chan->hw_value;
	cmd.channel = cpu_to_le16(ch);
	cmd.rxon_flags = il->staging.flags;
	cmd.rxon_filter_flags = il->staging.filter_flags;
	switch_count = ch_switch->count;
	tsf_low = ch_switch->timestamp & 0x0ffffffff;
	/*
	 * calculate the ucode channel switch time
	 * adding TSF as one of the factor for when to switch
	 */
	if (il->ucode_beacon_time > tsf_low && beacon_interval) {
		if (switch_count >
		    ((il->ucode_beacon_time - tsf_low) / beacon_interval)) {
			switch_count -=
			    (il->ucode_beacon_time - tsf_low) / beacon_interval;
		} else
			switch_count = 0;
	}
	if (switch_count <= 1)
		cmd.switch_time = cpu_to_le32(il->ucode_beacon_time);
	else {
		switch_time_in_usec =
		    vif->bss_conf.beacon_int * switch_count * TIME_UNIT;
		ucode_switch_time =
		    il_usecs_to_beacons(il, switch_time_in_usec,
					beacon_interval);
		cmd.switch_time =
		    il_add_beacon_time(il, il->ucode_beacon_time,
				       ucode_switch_time, beacon_interval);
	}
	D_11H("uCode time for the switch is 0x%x\n", cmd.switch_time);
	ch_info = il_get_channel_info(il, il->band, ch);
	if (ch_info)
		cmd.expect_beacon = il_is_channel_radar(ch_info);
	else {
		IL_ERR("invalid channel switch from %u to %u\n",
		       il->active.channel, ch);
		return -EFAULT;
	}

	rc = il4965_fill_txpower_tbl(il, band, ch, is_ht40, ctrl_chan_high,
				     &cmd.tx_power);
	if (rc) {
		D_11H("error:%d  fill txpower_tbl\n", rc);
		return rc;
	}

	return il_send_cmd_pdu(il, C_CHANNEL_SWITCH, sizeof(cmd), &cmd);
}

/**
 * il4965_txq_update_byte_cnt_tbl - Set up entry in Tx byte-count array
 */
static void
il4965_txq_update_byte_cnt_tbl(struct il_priv *il, struct il_tx_queue *txq,
			       u16 byte_cnt)
{
	struct il4965_scd_bc_tbl *scd_bc_tbl = il->scd_bc_tbls.addr;
	int txq_id = txq->q.id;
	int write_ptr = txq->q.write_ptr;
	int len = byte_cnt + IL_TX_CRC_SIZE + IL_TX_DELIMITER_SIZE;
	__le16 bc_ent;

	WARN_ON(len > 0xFFF || write_ptr >= TFD_QUEUE_SIZE_MAX);

	bc_ent = cpu_to_le16(len & 0xFFF);
	/* Set up byte count within first 256 entries */
	scd_bc_tbl[txq_id].tfd_offset[write_ptr] = bc_ent;

	/* If within first 64 entries, duplicate at end */
	if (write_ptr < TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[txq_id].tfd_offset[TFD_QUEUE_SIZE_MAX + write_ptr] =
		    bc_ent;
}

/**
 * il4965_hw_get_temperature - return the calibrated temperature (in Kelvin)
 * @stats: Provides the temperature reading from the uCode
 *
 * A return of <0 indicates bogus data in the stats
 */
static int
il4965_hw_get_temperature(struct il_priv *il)
{
	s32 temperature;
	s32 vt;
	s32 R1, R2, R3;
	u32 R4;

	if (test_bit(S_TEMPERATURE, &il->status) &&
	    (il->_4965.stats.flag & STATS_REPLY_FLG_HT40_MODE_MSK)) {
		D_TEMP("Running HT40 temperature calibration\n");
		R1 = (s32) le32_to_cpu(il->card_alive_init.therm_r1[1]);
		R2 = (s32) le32_to_cpu(il->card_alive_init.therm_r2[1]);
		R3 = (s32) le32_to_cpu(il->card_alive_init.therm_r3[1]);
		R4 = le32_to_cpu(il->card_alive_init.therm_r4[1]);
	} else {
		D_TEMP("Running temperature calibration\n");
		R1 = (s32) le32_to_cpu(il->card_alive_init.therm_r1[0]);
		R2 = (s32) le32_to_cpu(il->card_alive_init.therm_r2[0]);
		R3 = (s32) le32_to_cpu(il->card_alive_init.therm_r3[0]);
		R4 = le32_to_cpu(il->card_alive_init.therm_r4[0]);
	}

	/*
	 * Temperature is only 23 bits, so sign extend out to 32.
	 *
	 * NOTE If we haven't received a stats notification yet
	 * with an updated temperature, use R4 provided to us in the
	 * "initialize" ALIVE response.
	 */
	if (!test_bit(S_TEMPERATURE, &il->status))
		vt = sign_extend32(R4, 23);
	else
		vt = sign_extend32(le32_to_cpu
				   (il->_4965.stats.general.common.temperature),
				   23);

	D_TEMP("Calib values R[1-3]: %d %d %d R4: %d\n", R1, R2, R3, vt);

	if (R3 == R1) {
		IL_ERR("Calibration conflict R1 == R3\n");
		return -1;
	}

	/* Calculate temperature in degrees Kelvin, adjust by 97%.
	 * Add offset to center the adjustment around 0 degrees Centigrade. */
	temperature = TEMPERATURE_CALIB_A_VAL * (vt - R2);
	temperature /= (R3 - R1);
	temperature =
	    (temperature * 97) / 100 + TEMPERATURE_CALIB_KELVIN_OFFSET;

	D_TEMP("Calibrated temperature: %dK, %dC\n", temperature,
	       KELVIN_TO_CELSIUS(temperature));

	return temperature;
}

/* Adjust Txpower only if temperature variance is greater than threshold. */
#define IL_TEMPERATURE_THRESHOLD   3

/**
 * il4965_is_temp_calib_needed - determines if new calibration is needed
 *
 * If the temperature changed has changed sufficiently, then a recalibration
 * is needed.
 *
 * Assumes caller will replace il->last_temperature once calibration
 * executed.
 */
static int
il4965_is_temp_calib_needed(struct il_priv *il)
{
	int temp_diff;

	if (!test_bit(S_STATS, &il->status)) {
		D_TEMP("Temperature not updated -- no stats.\n");
		return 0;
	}

	temp_diff = il->temperature - il->last_temperature;

	/* get absolute value */
	if (temp_diff < 0) {
		D_POWER("Getting cooler, delta %d\n", temp_diff);
		temp_diff = -temp_diff;
	} else if (temp_diff == 0)
		D_POWER("Temperature unchanged\n");
	else
		D_POWER("Getting warmer, delta %d\n", temp_diff);

	if (temp_diff < IL_TEMPERATURE_THRESHOLD) {
		D_POWER(" => thermal txpower calib not needed\n");
		return 0;
	}

	D_POWER(" => thermal txpower calib needed\n");

	return 1;
}

void
il4965_temperature_calib(struct il_priv *il)
{
	s32 temp;

	temp = il4965_hw_get_temperature(il);
	if (IL_TX_POWER_TEMPERATURE_OUT_OF_RANGE(temp))
		return;

	if (il->temperature != temp) {
		if (il->temperature)
			D_TEMP("Temperature changed " "from %dC to %dC\n",
			       KELVIN_TO_CELSIUS(il->temperature),
			       KELVIN_TO_CELSIUS(temp));
		else
			D_TEMP("Temperature " "initialized to %dC\n",
			       KELVIN_TO_CELSIUS(temp));
	}

	il->temperature = temp;
	set_bit(S_TEMPERATURE, &il->status);

	if (!il->disable_tx_power_cal &&
	    unlikely(!test_bit(S_SCANNING, &il->status)) &&
	    il4965_is_temp_calib_needed(il))
		queue_work(il->workqueue, &il->txpower_work);
}

static u16
il4965_get_hcmd_size(u8 cmd_id, u16 len)
{
	switch (cmd_id) {
	case C_RXON:
		return (u16) sizeof(struct il4965_rxon_cmd);
	default:
		return len;
	}
}

static u16
il4965_build_addsta_hcmd(const struct il_addsta_cmd *cmd, u8 * data)
{
	struct il4965_addsta_cmd *addsta = (struct il4965_addsta_cmd *)data;
	addsta->mode = cmd->mode;
	memcpy(&addsta->sta, &cmd->sta, sizeof(struct sta_id_modify));
	memcpy(&addsta->key, &cmd->key, sizeof(struct il4965_keyinfo));
	addsta->station_flags = cmd->station_flags;
	addsta->station_flags_msk = cmd->station_flags_msk;
	addsta->tid_disable_tx = cmd->tid_disable_tx;
	addsta->add_immediate_ba_tid = cmd->add_immediate_ba_tid;
	addsta->remove_immediate_ba_tid = cmd->remove_immediate_ba_tid;
	addsta->add_immediate_ba_ssn = cmd->add_immediate_ba_ssn;
	addsta->sleep_tx_count = cmd->sleep_tx_count;
	addsta->reserved1 = cpu_to_le16(0);
	addsta->reserved2 = cpu_to_le16(0);

	return (u16) sizeof(struct il4965_addsta_cmd);
}

static void
il4965_post_scan(struct il_priv *il)
{
	/*
	 * Since setting the RXON may have been deferred while
	 * performing the scan, fire one off if needed
	 */
	if (memcmp(&il->staging, &il->active, sizeof(il->staging)))
		il_commit_rxon(il);
}

static void
il4965_post_associate(struct il_priv *il)
{
	struct ieee80211_vif *vif = il->vif;
	int ret = 0;

	if (!vif || !il->is_open)
		return;

	if (test_bit(S_EXIT_PENDING, &il->status))
		return;

	il_scan_cancel_timeout(il, 200);

	il->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	il_commit_rxon(il);

	ret = il_send_rxon_timing(il);
	if (ret)
		IL_WARN("RXON timing - " "Attempting to continue.\n");

	il->staging.filter_flags |= RXON_FILTER_ASSOC_MSK;

	il_set_rxon_ht(il, &il->current_ht_config);

	if (il->ops->set_rxon_chain)
		il->ops->set_rxon_chain(il);

	il->staging.assoc_id = cpu_to_le16(vif->bss_conf.aid);

	D_ASSOC("assoc id %d beacon interval %d\n", vif->bss_conf.aid,
		vif->bss_conf.beacon_int);

	if (vif->bss_conf.use_short_preamble)
		il->staging.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
	else
		il->staging.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;

	if (il->staging.flags & RXON_FLG_BAND_24G_MSK) {
		if (vif->bss_conf.use_short_slot)
			il->staging.flags |= RXON_FLG_SHORT_SLOT_MSK;
		else
			il->staging.flags &= ~RXON_FLG_SHORT_SLOT_MSK;
	}

	il_commit_rxon(il);

	D_ASSOC("Associated as %d to: %pM\n", vif->bss_conf.aid,
		il->active.bssid_addr);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		break;
	case NL80211_IFTYPE_ADHOC:
		il4965_send_beacon_cmd(il);
		break;
	default:
		IL_ERR("%s Should not be called in %d mode\n", __func__,
		       vif->type);
		break;
	}

	/* the chain noise calibration will enabled PM upon completion
	 * If chain noise has already been run, then we need to enable
	 * power management here */
	if (il->chain_noise_data.state == IL_CHAIN_NOISE_DONE)
		il_power_update_mode(il, false);

	/* Enable Rx differential gain and sensitivity calibrations */
	il4965_chain_noise_reset(il);
	il->start_calib = 1;
}

static void
il4965_config_ap(struct il_priv *il)
{
	struct ieee80211_vif *vif = il->vif;
	int ret = 0;

	lockdep_assert_held(&il->mutex);

	if (test_bit(S_EXIT_PENDING, &il->status))
		return;

	/* The following should be done only at AP bring up */
	if (!il_is_associated(il)) {

		/* RXON - unassoc (to set timing command) */
		il->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
		il_commit_rxon(il);

		/* RXON Timing */
		ret = il_send_rxon_timing(il);
		if (ret)
			IL_WARN("RXON timing failed - "
				"Attempting to continue.\n");

		/* AP has all antennas */
		il->chain_noise_data.active_chains = il->hw_params.valid_rx_ant;
		il_set_rxon_ht(il, &il->current_ht_config);
		if (il->ops->set_rxon_chain)
			il->ops->set_rxon_chain(il);

		il->staging.assoc_id = 0;

		if (vif->bss_conf.use_short_preamble)
			il->staging.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
		else
			il->staging.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;

		if (il->staging.flags & RXON_FLG_BAND_24G_MSK) {
			if (vif->bss_conf.use_short_slot)
				il->staging.flags |= RXON_FLG_SHORT_SLOT_MSK;
			else
				il->staging.flags &= ~RXON_FLG_SHORT_SLOT_MSK;
		}
		/* need to send beacon cmd before committing assoc RXON! */
		il4965_send_beacon_cmd(il);
		/* restore RXON assoc */
		il->staging.filter_flags |= RXON_FILTER_ASSOC_MSK;
		il_commit_rxon(il);
	}
	il4965_send_beacon_cmd(il);
}

const struct il_ops il4965_ops = {
	.txq_update_byte_cnt_tbl = il4965_txq_update_byte_cnt_tbl,
	.txq_attach_buf_to_tfd = il4965_hw_txq_attach_buf_to_tfd,
	.txq_free_tfd = il4965_hw_txq_free_tfd,
	.txq_init = il4965_hw_tx_queue_init,
	.is_valid_rtc_data_addr = il4965_hw_valid_rtc_data_addr,
	.init_alive_start = il4965_init_alive_start,
	.load_ucode = il4965_load_bsm,
	.dump_nic_error_log = il4965_dump_nic_error_log,
	.dump_fh = il4965_dump_fh,
	.set_channel_switch = il4965_hw_channel_switch,
	.apm_init = il_apm_init,
	.send_tx_power = il4965_send_tx_power,
	.update_chain_flags = il4965_update_chain_flags,
	.eeprom_acquire_semaphore = il4965_eeprom_acquire_semaphore,
	.eeprom_release_semaphore = il4965_eeprom_release_semaphore,

	.rxon_assoc = il4965_send_rxon_assoc,
	.commit_rxon = il4965_commit_rxon,
	.set_rxon_chain = il4965_set_rxon_chain,

	.get_hcmd_size = il4965_get_hcmd_size,
	.build_addsta_hcmd = il4965_build_addsta_hcmd,
	.request_scan = il4965_request_scan,
	.post_scan = il4965_post_scan,

	.post_associate = il4965_post_associate,
	.config_ap = il4965_config_ap,
	.manage_ibss_station = il4965_manage_ibss_station,
	.update_bcast_stations = il4965_update_bcast_stations,

	.send_led_cmd = il4965_send_led_cmd,
};

struct il_cfg il4965_cfg = {
	.name = "Intel(R) Wireless WiFi Link 4965AGN",
	.fw_name_pre = IL4965_FW_PRE,
	.ucode_api_max = IL4965_UCODE_API_MAX,
	.ucode_api_min = IL4965_UCODE_API_MIN,
	.sku = IL_SKU_A | IL_SKU_G | IL_SKU_N,
	.valid_tx_ant = ANT_AB,
	.valid_rx_ant = ANT_ABC,
	.eeprom_ver = EEPROM_4965_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_4965_TX_POWER_VERSION,
	.mod_params = &il4965_mod_params,
	.led_mode = IL_LED_BLINK,
	/*
	 * Force use of chains B and C for scan RX on 5 GHz band
	 * because the device has off-channel reception on chain A.
	 */
	.scan_rx_antennas[NL80211_BAND_5GHZ] = ANT_BC,

	.eeprom_size = IL4965_EEPROM_IMG_SIZE,
	.num_of_queues = IL49_NUM_QUEUES,
	.num_of_ampdu_queues = IL49_NUM_AMPDU_QUEUES,
	.pll_cfg_val = 0,
	.set_l0s = true,
	.use_bsm = true,
	.led_compensation = 61,
	.chain_noise_num_beacons = IL4965_CAL_NUM_BEACONS,
	.wd_timeout = IL_DEF_WD_TIMEOUT,
	.temperature_kelvin = true,
	.ucode_tracing = true,
	.sensitivity_calib_by_driver = true,
	.chain_noise_calib_by_driver = true,

	.regulatory_bands = {
		EEPROM_REGULATORY_BAND_1_CHANNELS,
		EEPROM_REGULATORY_BAND_2_CHANNELS,
		EEPROM_REGULATORY_BAND_3_CHANNELS,
		EEPROM_REGULATORY_BAND_4_CHANNELS,
		EEPROM_REGULATORY_BAND_5_CHANNELS,
		EEPROM_4965_REGULATORY_BAND_24_HT40_CHANNELS,
		EEPROM_4965_REGULATORY_BAND_52_HT40_CHANNELS
	},

};

/* Module firmware */
MODULE_FIRMWARE(IL4965_MODULE_FIRMWARE(IL4965_UCODE_API_MAX));
