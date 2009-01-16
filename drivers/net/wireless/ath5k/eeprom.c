/*
 * Copyright (c) 2004-2008 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006-2008 Nick Kossifidis <mickflemm@gmail.com>
 * Copyright (c) 2008 Felix Fietkau <nbd@openwrt.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/*************************************\
* EEPROM access functions and helpers *
\*************************************/

#include "ath5k.h"
#include "reg.h"
#include "debug.h"
#include "base.h"

/*
 * Read from eeprom
 */
static int ath5k_hw_eeprom_read(struct ath5k_hw *ah, u32 offset, u16 *data)
{
	u32 status, timeout;

	ATH5K_TRACE(ah->ah_sc);
	/*
	 * Initialize EEPROM access
	 */
	if (ah->ah_version == AR5K_AR5210) {
		AR5K_REG_ENABLE_BITS(ah, AR5K_PCICFG, AR5K_PCICFG_EEAE);
		(void)ath5k_hw_reg_read(ah, AR5K_EEPROM_BASE + (4 * offset));
	} else {
		ath5k_hw_reg_write(ah, offset, AR5K_EEPROM_BASE);
		AR5K_REG_ENABLE_BITS(ah, AR5K_EEPROM_CMD,
				AR5K_EEPROM_CMD_READ);
	}

	for (timeout = AR5K_TUNE_REGISTER_TIMEOUT; timeout > 0; timeout--) {
		status = ath5k_hw_reg_read(ah, AR5K_EEPROM_STATUS);
		if (status & AR5K_EEPROM_STAT_RDDONE) {
			if (status & AR5K_EEPROM_STAT_RDERR)
				return -EIO;
			*data = (u16)(ath5k_hw_reg_read(ah, AR5K_EEPROM_DATA) &
					0xffff);
			return 0;
		}
		udelay(15);
	}

	return -ETIMEDOUT;
}

/*
 * Translate binary channel representation in EEPROM to frequency
 */
static u16 ath5k_eeprom_bin2freq(struct ath5k_eeprom_info *ee, u16 bin,
                                 unsigned int mode)
{
	u16 val;

	if (bin == AR5K_EEPROM_CHANNEL_DIS)
		return bin;

	if (mode == AR5K_EEPROM_MODE_11A) {
		if (ee->ee_version > AR5K_EEPROM_VERSION_3_2)
			val = (5 * bin) + 4800;
		else
			val = bin > 62 ? (10 * 62) + (5 * (bin - 62)) + 5100 :
				(bin * 10) + 5100;
	} else {
		if (ee->ee_version > AR5K_EEPROM_VERSION_3_2)
			val = bin + 2300;
		else
			val = bin + 2400;
	}

	return val;
}

/*
 * Initialize eeprom & capabilities structs
 */
static int
ath5k_eeprom_init_header(struct ath5k_hw *ah)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	int ret;
	u16 val;

	/* Initial TX thermal adjustment values */
	ee->ee_tx_clip = 4;
	ee->ee_pwd_84 = ee->ee_pwd_90 = 1;
	ee->ee_gain_select = 1;

	/*
	 * Read values from EEPROM and store them in the capability structure
	 */
	AR5K_EEPROM_READ_HDR(AR5K_EEPROM_MAGIC, ee_magic);
	AR5K_EEPROM_READ_HDR(AR5K_EEPROM_PROTECT, ee_protect);
	AR5K_EEPROM_READ_HDR(AR5K_EEPROM_REG_DOMAIN, ee_regdomain);
	AR5K_EEPROM_READ_HDR(AR5K_EEPROM_VERSION, ee_version);
	AR5K_EEPROM_READ_HDR(AR5K_EEPROM_HDR, ee_header);

	/* Return if we have an old EEPROM */
	if (ah->ah_ee_version < AR5K_EEPROM_VERSION_3_0)
		return 0;

#ifdef notyet
	/*
	 * Validate the checksum of the EEPROM date. There are some
	 * devices with invalid EEPROMs.
	 */
	for (cksum = 0, offset = 0; offset < AR5K_EEPROM_INFO_MAX; offset++) {
		AR5K_EEPROM_READ(AR5K_EEPROM_INFO(offset), val);
		cksum ^= val;
	}
	if (cksum != AR5K_EEPROM_INFO_CKSUM) {
		ATH5K_ERR(ah->ah_sc, "Invalid EEPROM checksum 0x%04x\n", cksum);
		return -EIO;
	}
#endif

	AR5K_EEPROM_READ_HDR(AR5K_EEPROM_ANT_GAIN(ah->ah_ee_version),
	    ee_ant_gain);

	if (ah->ah_ee_version >= AR5K_EEPROM_VERSION_4_0) {
		AR5K_EEPROM_READ_HDR(AR5K_EEPROM_MISC0, ee_misc0);
		AR5K_EEPROM_READ_HDR(AR5K_EEPROM_MISC1, ee_misc1);
	}

	if (ah->ah_ee_version < AR5K_EEPROM_VERSION_3_3) {
		AR5K_EEPROM_READ(AR5K_EEPROM_OBDB0_2GHZ, val);
		ee->ee_ob[AR5K_EEPROM_MODE_11B][0] = val & 0x7;
		ee->ee_db[AR5K_EEPROM_MODE_11B][0] = (val >> 3) & 0x7;

		AR5K_EEPROM_READ(AR5K_EEPROM_OBDB1_2GHZ, val);
		ee->ee_ob[AR5K_EEPROM_MODE_11G][0] = val & 0x7;
		ee->ee_db[AR5K_EEPROM_MODE_11G][0] = (val >> 3) & 0x7;
	}

	return 0;
}


/*
 * Read antenna infos from eeprom
 */
static int ath5k_eeprom_read_ants(struct ath5k_hw *ah, u32 *offset,
		unsigned int mode)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	u32 o = *offset;
	u16 val;
	int ret, i = 0;

	AR5K_EEPROM_READ(o++, val);
	ee->ee_switch_settling[mode]	= (val >> 8) & 0x7f;
	ee->ee_atn_tx_rx[mode]		= (val >> 2) & 0x3f;
	ee->ee_ant_control[mode][i]	= (val << 4) & 0x3f;

	AR5K_EEPROM_READ(o++, val);
	ee->ee_ant_control[mode][i++]	|= (val >> 12) & 0xf;
	ee->ee_ant_control[mode][i++]	= (val >> 6) & 0x3f;
	ee->ee_ant_control[mode][i++]	= val & 0x3f;

	AR5K_EEPROM_READ(o++, val);
	ee->ee_ant_control[mode][i++]	= (val >> 10) & 0x3f;
	ee->ee_ant_control[mode][i++]	= (val >> 4) & 0x3f;
	ee->ee_ant_control[mode][i]	= (val << 2) & 0x3f;

	AR5K_EEPROM_READ(o++, val);
	ee->ee_ant_control[mode][i++]	|= (val >> 14) & 0x3;
	ee->ee_ant_control[mode][i++]	= (val >> 8) & 0x3f;
	ee->ee_ant_control[mode][i++]	= (val >> 2) & 0x3f;
	ee->ee_ant_control[mode][i]	= (val << 4) & 0x3f;

	AR5K_EEPROM_READ(o++, val);
	ee->ee_ant_control[mode][i++]	|= (val >> 12) & 0xf;
	ee->ee_ant_control[mode][i++]	= (val >> 6) & 0x3f;
	ee->ee_ant_control[mode][i++]	= val & 0x3f;

	/* Get antenna modes */
	ah->ah_antenna[mode][0] =
	    (ee->ee_ant_control[mode][0] << 4) | 0x1;
	ah->ah_antenna[mode][AR5K_ANT_FIXED_A] =
	     ee->ee_ant_control[mode][1] 	|
	    (ee->ee_ant_control[mode][2] << 6) 	|
	    (ee->ee_ant_control[mode][3] << 12) |
	    (ee->ee_ant_control[mode][4] << 18) |
	    (ee->ee_ant_control[mode][5] << 24);
	ah->ah_antenna[mode][AR5K_ANT_FIXED_B] =
	     ee->ee_ant_control[mode][6] 	|
	    (ee->ee_ant_control[mode][7] << 6) 	|
	    (ee->ee_ant_control[mode][8] << 12) |
	    (ee->ee_ant_control[mode][9] << 18) |
	    (ee->ee_ant_control[mode][10] << 24);

	/* return new offset */
	*offset = o;

	return 0;
}

/*
 * Read supported modes from eeprom
 */
static int ath5k_eeprom_read_modes(struct ath5k_hw *ah, u32 *offset,
		unsigned int mode)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	u32 o = *offset;
	u16 val;
	int ret;

	ee->ee_n_piers[mode] = 0;
	AR5K_EEPROM_READ(o++, val);
	ee->ee_adc_desired_size[mode]	= (s8)((val >> 8) & 0xff);
	switch(mode) {
	case AR5K_EEPROM_MODE_11A:
		ee->ee_ob[mode][3]		= (val >> 5) & 0x7;
		ee->ee_db[mode][3]		= (val >> 2) & 0x7;
		ee->ee_ob[mode][2]		= (val << 1) & 0x7;

		AR5K_EEPROM_READ(o++, val);
		ee->ee_ob[mode][2]		|= (val >> 15) & 0x1;
		ee->ee_db[mode][2]		= (val >> 12) & 0x7;
		ee->ee_ob[mode][1]		= (val >> 9) & 0x7;
		ee->ee_db[mode][1]		= (val >> 6) & 0x7;
		ee->ee_ob[mode][0]		= (val >> 3) & 0x7;
		ee->ee_db[mode][0]		= val & 0x7;
		break;
	case AR5K_EEPROM_MODE_11G:
	case AR5K_EEPROM_MODE_11B:
		ee->ee_ob[mode][1]		= (val >> 4) & 0x7;
		ee->ee_db[mode][1]		= val & 0x7;
		break;
	}

	AR5K_EEPROM_READ(o++, val);
	ee->ee_tx_end2xlna_enable[mode]	= (val >> 8) & 0xff;
	ee->ee_thr_62[mode]		= val & 0xff;

	if (ah->ah_ee_version <= AR5K_EEPROM_VERSION_3_2)
		ee->ee_thr_62[mode] = mode == AR5K_EEPROM_MODE_11A ? 15 : 28;

	AR5K_EEPROM_READ(o++, val);
	ee->ee_tx_end2xpa_disable[mode]	= (val >> 8) & 0xff;
	ee->ee_tx_frm2xpa_enable[mode]	= val & 0xff;

	AR5K_EEPROM_READ(o++, val);
	ee->ee_pga_desired_size[mode]	= (val >> 8) & 0xff;

	if ((val & 0xff) & 0x80)
		ee->ee_noise_floor_thr[mode] = -((((val & 0xff) ^ 0xff)) + 1);
	else
		ee->ee_noise_floor_thr[mode] = val & 0xff;

	if (ah->ah_ee_version <= AR5K_EEPROM_VERSION_3_2)
		ee->ee_noise_floor_thr[mode] =
		    mode == AR5K_EEPROM_MODE_11A ? -54 : -1;

	AR5K_EEPROM_READ(o++, val);
	ee->ee_xlna_gain[mode]		= (val >> 5) & 0xff;
	ee->ee_x_gain[mode]		= (val >> 1) & 0xf;
	ee->ee_xpd[mode]		= val & 0x1;

	if (ah->ah_ee_version >= AR5K_EEPROM_VERSION_4_0)
		ee->ee_fixed_bias[mode] = (val >> 13) & 0x1;

	if (ah->ah_ee_version >= AR5K_EEPROM_VERSION_3_3) {
		AR5K_EEPROM_READ(o++, val);
		ee->ee_false_detect[mode] = (val >> 6) & 0x7f;

		if (mode == AR5K_EEPROM_MODE_11A)
			ee->ee_xr_power[mode] = val & 0x3f;
		else {
			ee->ee_ob[mode][0] = val & 0x7;
			ee->ee_db[mode][0] = (val >> 3) & 0x7;
		}
	}

	if (ah->ah_ee_version < AR5K_EEPROM_VERSION_3_4) {
		ee->ee_i_gain[mode] = AR5K_EEPROM_I_GAIN;
		ee->ee_cck_ofdm_power_delta = AR5K_EEPROM_CCK_OFDM_DELTA;
	} else {
		ee->ee_i_gain[mode] = (val >> 13) & 0x7;

		AR5K_EEPROM_READ(o++, val);
		ee->ee_i_gain[mode] |= (val << 3) & 0x38;

		if (mode == AR5K_EEPROM_MODE_11G) {
			ee->ee_cck_ofdm_power_delta = (val >> 3) & 0xff;
			if (ah->ah_ee_version >= AR5K_EEPROM_VERSION_4_6)
				ee->ee_scaled_cck_delta = (val >> 11) & 0x1f;
		}
	}

	if (ah->ah_ee_version >= AR5K_EEPROM_VERSION_4_0 &&
			mode == AR5K_EEPROM_MODE_11A) {
		ee->ee_i_cal[mode] = (val >> 8) & 0x3f;
		ee->ee_q_cal[mode] = (val >> 3) & 0x1f;
	}

	if (ah->ah_ee_version < AR5K_EEPROM_VERSION_4_0)
		goto done;

	switch(mode) {
	case AR5K_EEPROM_MODE_11A:
		if (ah->ah_ee_version < AR5K_EEPROM_VERSION_4_1)
			break;

		AR5K_EEPROM_READ(o++, val);
		ee->ee_margin_tx_rx[mode] = val & 0x3f;
		break;
	case AR5K_EEPROM_MODE_11B:
		AR5K_EEPROM_READ(o++, val);

		ee->ee_pwr_cal_b[0].freq =
			ath5k_eeprom_bin2freq(ee, val & 0xff, mode);
		if (ee->ee_pwr_cal_b[0].freq != AR5K_EEPROM_CHANNEL_DIS)
			ee->ee_n_piers[mode]++;

		ee->ee_pwr_cal_b[1].freq =
			ath5k_eeprom_bin2freq(ee, (val >> 8) & 0xff, mode);
		if (ee->ee_pwr_cal_b[1].freq != AR5K_EEPROM_CHANNEL_DIS)
			ee->ee_n_piers[mode]++;

		AR5K_EEPROM_READ(o++, val);
		ee->ee_pwr_cal_b[2].freq =
			ath5k_eeprom_bin2freq(ee, val & 0xff, mode);
		if (ee->ee_pwr_cal_b[2].freq != AR5K_EEPROM_CHANNEL_DIS)
			ee->ee_n_piers[mode]++;

		if (ah->ah_ee_version >= AR5K_EEPROM_VERSION_4_1)
			ee->ee_margin_tx_rx[mode] = (val >> 8) & 0x3f;
		break;
	case AR5K_EEPROM_MODE_11G:
		AR5K_EEPROM_READ(o++, val);

		ee->ee_pwr_cal_g[0].freq =
			ath5k_eeprom_bin2freq(ee, val & 0xff, mode);
		if (ee->ee_pwr_cal_g[0].freq != AR5K_EEPROM_CHANNEL_DIS)
			ee->ee_n_piers[mode]++;

		ee->ee_pwr_cal_g[1].freq =
			ath5k_eeprom_bin2freq(ee, (val >> 8) & 0xff, mode);
		if (ee->ee_pwr_cal_g[1].freq != AR5K_EEPROM_CHANNEL_DIS)
			ee->ee_n_piers[mode]++;

		AR5K_EEPROM_READ(o++, val);
		ee->ee_turbo_max_power[mode] = val & 0x7f;
		ee->ee_xr_power[mode] = (val >> 7) & 0x3f;

		AR5K_EEPROM_READ(o++, val);
		ee->ee_pwr_cal_g[2].freq =
			ath5k_eeprom_bin2freq(ee, val & 0xff, mode);
		if (ee->ee_pwr_cal_g[2].freq != AR5K_EEPROM_CHANNEL_DIS)
			ee->ee_n_piers[mode]++;

		if (ah->ah_ee_version >= AR5K_EEPROM_VERSION_4_1)
			ee->ee_margin_tx_rx[mode] = (val >> 8) & 0x3f;

		AR5K_EEPROM_READ(o++, val);
		ee->ee_i_cal[mode] = (val >> 8) & 0x3f;
		ee->ee_q_cal[mode] = (val >> 3) & 0x1f;

		if (ah->ah_ee_version >= AR5K_EEPROM_VERSION_4_2) {
			AR5K_EEPROM_READ(o++, val);
			ee->ee_cck_ofdm_gain_delta = val & 0xff;
		}
		break;
	}

done:
	/* return new offset */
	*offset = o;

	return 0;
}

/*
 * Read turbo mode information on newer EEPROM versions
 */
static int
ath5k_eeprom_read_turbo_modes(struct ath5k_hw *ah,
			      u32 *offset, unsigned int mode)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	u32 o = *offset;
	u16 val;
	int ret;

	if (ee->ee_version < AR5K_EEPROM_VERSION_5_0)
		return 0;

	switch (mode){
	case AR5K_EEPROM_MODE_11A:
		ee->ee_switch_settling_turbo[mode] = (val >> 6) & 0x7f;

		ee->ee_atn_tx_rx_turbo[mode] = (val >> 13) & 0x7;
		AR5K_EEPROM_READ(o++, val);
		ee->ee_atn_tx_rx_turbo[mode] |= (val & 0x7) << 3;
		ee->ee_margin_tx_rx_turbo[mode] = (val >> 3) & 0x3f;

		ee->ee_adc_desired_size_turbo[mode] = (val >> 9) & 0x7f;
		AR5K_EEPROM_READ(o++, val);
		ee->ee_adc_desired_size_turbo[mode] |= (val & 0x1) << 7;
		ee->ee_pga_desired_size_turbo[mode] = (val >> 1) & 0xff;

		if (AR5K_EEPROM_EEMAP(ee->ee_misc0) >=2)
			ee->ee_pd_gain_overlap = (val >> 9) & 0xf;
		break;
	case AR5K_EEPROM_MODE_11G:
		ee->ee_switch_settling_turbo[mode] = (val >> 8) & 0x7f;

		ee->ee_atn_tx_rx_turbo[mode] = (val >> 15) & 0x7;
		AR5K_EEPROM_READ(o++, val);
		ee->ee_atn_tx_rx_turbo[mode] |= (val & 0x1f) << 1;
		ee->ee_margin_tx_rx_turbo[mode] = (val >> 5) & 0x3f;

		ee->ee_adc_desired_size_turbo[mode] = (val >> 11) & 0x7f;
		AR5K_EEPROM_READ(o++, val);
		ee->ee_adc_desired_size_turbo[mode] |= (val & 0x7) << 5;
		ee->ee_pga_desired_size_turbo[mode] = (val >> 3) & 0xff;
		break;
	}

	/* return new offset */
	*offset = o;

	return 0;
}


static int
ath5k_eeprom_init_modes(struct ath5k_hw *ah)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	u32 mode_offset[3];
	unsigned int mode;
	u32 offset;
	int ret;

	/*
	 * Get values for all modes
	 */
	mode_offset[AR5K_EEPROM_MODE_11A] = AR5K_EEPROM_MODES_11A(ah->ah_ee_version);
	mode_offset[AR5K_EEPROM_MODE_11B] = AR5K_EEPROM_MODES_11B(ah->ah_ee_version);
	mode_offset[AR5K_EEPROM_MODE_11G] = AR5K_EEPROM_MODES_11G(ah->ah_ee_version);

	ee->ee_turbo_max_power[AR5K_EEPROM_MODE_11A] =
		AR5K_EEPROM_HDR_T_5GHZ_DBM(ee->ee_header);

	for (mode = AR5K_EEPROM_MODE_11A; mode <= AR5K_EEPROM_MODE_11G; mode++) {
		offset = mode_offset[mode];

		ret = ath5k_eeprom_read_ants(ah, &offset, mode);
		if (ret)
			return ret;

		ret = ath5k_eeprom_read_modes(ah, &offset, mode);
		if (ret)
			return ret;

		ret = ath5k_eeprom_read_turbo_modes(ah, &offset, mode);
		if (ret)
			return ret;
	}

	/* override for older eeprom versions for better performance */
	if (ah->ah_ee_version <= AR5K_EEPROM_VERSION_3_2) {
		ee->ee_thr_62[AR5K_EEPROM_MODE_11A] = 15;
		ee->ee_thr_62[AR5K_EEPROM_MODE_11B] = 28;
		ee->ee_thr_62[AR5K_EEPROM_MODE_11G] = 28;
	}

	return 0;
}

static inline void
ath5k_get_pcdac_intercepts(struct ath5k_hw *ah, u8 min, u8 max, u8 *vp)
{
	const static u16 intercepts3[] =
		{ 0, 5, 10, 20, 30, 50, 70, 85, 90, 95, 100 };
	const static u16 intercepts3_2[] =
		{ 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
	const u16 *ip;
	int i;

	if (ah->ah_ee_version >= AR5K_EEPROM_VERSION_3_2)
		ip = intercepts3_2;
	else
		ip = intercepts3;

	for (i = 0; i < ARRAY_SIZE(intercepts3); i++)
		*vp++ = (ip[i] * max + (100 - ip[i]) * min) / 100;
}

static inline int
ath5k_eeprom_read_freq_list(struct ath5k_hw *ah, int *offset, int max,
                            struct ath5k_chan_pcal_info *pc, u8 *count)
{
	int o = *offset;
	int i = 0;
	u8 f1, f2;
	int ret;
	u16 val;

	while(i < max) {
		AR5K_EEPROM_READ(o++, val);

		f1 = (val >> 8) & 0xff;
		f2 = val & 0xff;

		if (f1)
			pc[i++].freq = f1;

		if (f2)
			pc[i++].freq = f2;

		if (!f1 || !f2)
			break;
	}
	*offset = o;
	*count = i;

	return 0;
}

static int
ath5k_eeprom_init_11a_pcal_freq(struct ath5k_hw *ah, int offset)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	struct ath5k_chan_pcal_info *pcal = ee->ee_pwr_cal_a;
	int i, ret;
	u16 val;
	u8 mask;

	if (ee->ee_version >= AR5K_EEPROM_VERSION_3_3) {
		ath5k_eeprom_read_freq_list(ah, &offset,
			AR5K_EEPROM_N_5GHZ_CHAN, pcal,
			&ee->ee_n_piers[AR5K_EEPROM_MODE_11A]);
	} else {
		mask = AR5K_EEPROM_FREQ_M(ah->ah_ee_version);

		AR5K_EEPROM_READ(offset++, val);
		pcal[0].freq  = (val >> 9) & mask;
		pcal[1].freq  = (val >> 2) & mask;
		pcal[2].freq  = (val << 5) & mask;

		AR5K_EEPROM_READ(offset++, val);
		pcal[2].freq |= (val >> 11) & 0x1f;
		pcal[3].freq  = (val >> 4) & mask;
		pcal[4].freq  = (val << 3) & mask;

		AR5K_EEPROM_READ(offset++, val);
		pcal[4].freq |= (val >> 13) & 0x7;
		pcal[5].freq  = (val >> 6) & mask;
		pcal[6].freq  = (val << 1) & mask;

		AR5K_EEPROM_READ(offset++, val);
		pcal[6].freq |= (val >> 15) & 0x1;
		pcal[7].freq  = (val >> 8) & mask;
		pcal[8].freq  = (val >> 1) & mask;
		pcal[9].freq  = (val << 6) & mask;

		AR5K_EEPROM_READ(offset++, val);
		pcal[9].freq |= (val >> 10) & 0x3f;
		ee->ee_n_piers[AR5K_EEPROM_MODE_11A] = 10;
	}

	for(i = 0; i < AR5K_EEPROM_N_5GHZ_CHAN; i += 1) {
		pcal[i].freq = ath5k_eeprom_bin2freq(ee,
				pcal[i].freq, AR5K_EEPROM_MODE_11A);
	}

	return 0;
}

static inline int
ath5k_eeprom_init_11bg_2413(struct ath5k_hw *ah, unsigned int mode, int offset)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	struct ath5k_chan_pcal_info *pcal;
	int i;

	switch(mode) {
	case AR5K_EEPROM_MODE_11B:
		pcal = ee->ee_pwr_cal_b;
		break;
	case AR5K_EEPROM_MODE_11G:
		pcal = ee->ee_pwr_cal_g;
		break;
	default:
		return -EINVAL;
	}

	ath5k_eeprom_read_freq_list(ah, &offset,
		AR5K_EEPROM_N_2GHZ_CHAN_2413, pcal,
		&ee->ee_n_piers[mode]);
	for(i = 0; i < AR5K_EEPROM_N_2GHZ_CHAN_2413; i += 1) {
		pcal[i].freq = ath5k_eeprom_bin2freq(ee,
				pcal[i].freq, mode);
	}

	return 0;
}


static int
ath5k_eeprom_read_pcal_info_5111(struct ath5k_hw *ah, int mode)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	struct ath5k_chan_pcal_info *pcal;
	int offset, ret;
	int i, j;
	u16 val;

	offset = AR5K_EEPROM_GROUPS_START(ee->ee_version);
	switch(mode) {
	case AR5K_EEPROM_MODE_11A:
		if (!AR5K_EEPROM_HDR_11A(ee->ee_header))
			return 0;

		ret = ath5k_eeprom_init_11a_pcal_freq(ah,
			offset + AR5K_EEPROM_GROUP1_OFFSET);
		if (ret < 0)
			return ret;

		offset += AR5K_EEPROM_GROUP2_OFFSET;
		pcal = ee->ee_pwr_cal_a;
		break;
	case AR5K_EEPROM_MODE_11B:
		if (!AR5K_EEPROM_HDR_11B(ee->ee_header) &&
		    !AR5K_EEPROM_HDR_11G(ee->ee_header))
			return 0;

		pcal = ee->ee_pwr_cal_b;
		offset += AR5K_EEPROM_GROUP3_OFFSET;

		/* fixed piers */
		pcal[0].freq = 2412;
		pcal[1].freq = 2447;
		pcal[2].freq = 2484;
		ee->ee_n_piers[mode] = 3;
		break;
	case AR5K_EEPROM_MODE_11G:
		if (!AR5K_EEPROM_HDR_11G(ee->ee_header))
			return 0;

		pcal = ee->ee_pwr_cal_g;
		offset += AR5K_EEPROM_GROUP4_OFFSET;

		/* fixed piers */
		pcal[0].freq = 2312;
		pcal[1].freq = 2412;
		pcal[2].freq = 2484;
		ee->ee_n_piers[mode] = 3;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < ee->ee_n_piers[mode]; i++) {
		struct ath5k_chan_pcal_info_rf5111 *cdata =
			&pcal[i].rf5111_info;

		AR5K_EEPROM_READ(offset++, val);
		cdata->pcdac_max = ((val >> 10) & AR5K_EEPROM_PCDAC_M);
		cdata->pcdac_min = ((val >> 4) & AR5K_EEPROM_PCDAC_M);
		cdata->pwr[0] = ((val << 2) & AR5K_EEPROM_POWER_M);

		AR5K_EEPROM_READ(offset++, val);
		cdata->pwr[0] |= ((val >> 14) & 0x3);
		cdata->pwr[1] = ((val >> 8) & AR5K_EEPROM_POWER_M);
		cdata->pwr[2] = ((val >> 2) & AR5K_EEPROM_POWER_M);
		cdata->pwr[3] = ((val << 4) & AR5K_EEPROM_POWER_M);

		AR5K_EEPROM_READ(offset++, val);
		cdata->pwr[3] |= ((val >> 12) & 0xf);
		cdata->pwr[4] = ((val >> 6) & AR5K_EEPROM_POWER_M);
		cdata->pwr[5] = (val  & AR5K_EEPROM_POWER_M);

		AR5K_EEPROM_READ(offset++, val);
		cdata->pwr[6] = ((val >> 10) & AR5K_EEPROM_POWER_M);
		cdata->pwr[7] = ((val >> 4) & AR5K_EEPROM_POWER_M);
		cdata->pwr[8] = ((val << 2) & AR5K_EEPROM_POWER_M);

		AR5K_EEPROM_READ(offset++, val);
		cdata->pwr[8] |= ((val >> 14) & 0x3);
		cdata->pwr[9] = ((val >> 8) & AR5K_EEPROM_POWER_M);
		cdata->pwr[10] = ((val >> 2) & AR5K_EEPROM_POWER_M);

		ath5k_get_pcdac_intercepts(ah, cdata->pcdac_min,
			cdata->pcdac_max, cdata->pcdac);

		for (j = 0; j < AR5K_EEPROM_N_PCDAC; j++) {
			cdata->pwr[j] = (u16)
				(AR5K_EEPROM_POWER_STEP * cdata->pwr[j]);
		}
	}

	return 0;
}

static int
ath5k_eeprom_read_pcal_info_5112(struct ath5k_hw *ah, int mode)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	struct ath5k_chan_pcal_info_rf5112 *chan_pcal_info;
	struct ath5k_chan_pcal_info *gen_chan_info;
	u32 offset;
	unsigned int i, c;
	u16 val;
	int ret;

	switch (mode) {
	case AR5K_EEPROM_MODE_11A:
		/*
		 * Read 5GHz EEPROM channels
		 */
		offset = AR5K_EEPROM_GROUPS_START(ee->ee_version);
		ath5k_eeprom_init_11a_pcal_freq(ah, offset);

		offset += AR5K_EEPROM_GROUP2_OFFSET;
		gen_chan_info = ee->ee_pwr_cal_a;
		break;
	case AR5K_EEPROM_MODE_11B:
		offset = AR5K_EEPROM_GROUPS_START(ee->ee_version);
		if (AR5K_EEPROM_HDR_11A(ee->ee_header))
			offset += AR5K_EEPROM_GROUP3_OFFSET;

		/* NB: frequency piers parsed during mode init */
		gen_chan_info = ee->ee_pwr_cal_b;
		break;
	case AR5K_EEPROM_MODE_11G:
		offset = AR5K_EEPROM_GROUPS_START(ee->ee_version);
		if (AR5K_EEPROM_HDR_11A(ee->ee_header))
			offset += AR5K_EEPROM_GROUP4_OFFSET;
		else if (AR5K_EEPROM_HDR_11B(ee->ee_header))
			offset += AR5K_EEPROM_GROUP2_OFFSET;

		/* NB: frequency piers parsed during mode init */
		gen_chan_info = ee->ee_pwr_cal_g;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < ee->ee_n_piers[mode]; i++) {
		chan_pcal_info = &gen_chan_info[i].rf5112_info;

		/* Power values in dBm * 4
		 * for the lower xpd gain curve
		 * (0 dBm -> higher output power) */
		for (c = 0; c < AR5K_EEPROM_N_XPD0_POINTS; c++) {
			AR5K_EEPROM_READ(offset++, val);
			chan_pcal_info->pwr_x0[c] = (val & 0xff);
			chan_pcal_info->pwr_x0[++c] = ((val >> 8) & 0xff);
		}

		/* PCDAC steps
		 * corresponding to the above power
		 * measurements */
		AR5K_EEPROM_READ(offset++, val);
		chan_pcal_info->pcdac_x0[1] = (val & 0x1f);
		chan_pcal_info->pcdac_x0[2] = ((val >> 5) & 0x1f);
		chan_pcal_info->pcdac_x0[3] = ((val >> 10) & 0x1f);

		/* Power values in dBm * 4
		 * for the higher xpd gain curve
		 * (18 dBm -> lower output power) */
		AR5K_EEPROM_READ(offset++, val);
		chan_pcal_info->pwr_x3[0] = (val & 0xff);
		chan_pcal_info->pwr_x3[1] = ((val >> 8) & 0xff);

		AR5K_EEPROM_READ(offset++, val);
		chan_pcal_info->pwr_x3[2] = (val & 0xff);

		/* PCDAC steps
		 * corresponding to the above power
		 * measurements (static) */
		chan_pcal_info->pcdac_x3[0] = 20;
		chan_pcal_info->pcdac_x3[1] = 35;
		chan_pcal_info->pcdac_x3[2] = 63;

		if (ee->ee_version >= AR5K_EEPROM_VERSION_4_3) {
			chan_pcal_info->pcdac_x0[0] = ((val >> 8) & 0xff);

			/* Last xpd0 power level is also channel maximum */
			gen_chan_info[i].max_pwr = chan_pcal_info->pwr_x0[3];
		} else {
			chan_pcal_info->pcdac_x0[0] = 1;
			gen_chan_info[i].max_pwr = ((val >> 8) & 0xff);
		}

		/* Recreate pcdac_x0 table for this channel using pcdac steps */
		chan_pcal_info->pcdac_x0[1] += chan_pcal_info->pcdac_x0[0];
		chan_pcal_info->pcdac_x0[2] += chan_pcal_info->pcdac_x0[1];
		chan_pcal_info->pcdac_x0[3] += chan_pcal_info->pcdac_x0[2];
	}

	return 0;
}

static inline unsigned int
ath5k_pdgains_size_2413(struct ath5k_eeprom_info *ee, unsigned int mode)
{
	static const unsigned int pdgains_size[] = { 4, 6, 9, 12 };
	unsigned int sz;

	sz = pdgains_size[ee->ee_pd_gains[mode] - 1];
	sz *= ee->ee_n_piers[mode];

	return sz;
}

static unsigned int
ath5k_cal_data_offset_2413(struct ath5k_eeprom_info *ee, int mode)
{
	u32 offset = AR5K_EEPROM_CAL_DATA_START(ee->ee_misc4);

	switch(mode) {
	case AR5K_EEPROM_MODE_11G:
		if (AR5K_EEPROM_HDR_11B(ee->ee_header))
			offset += ath5k_pdgains_size_2413(ee, AR5K_EEPROM_MODE_11B) + 2;
		/* fall through */
	case AR5K_EEPROM_MODE_11B:
		if (AR5K_EEPROM_HDR_11A(ee->ee_header))
			offset += ath5k_pdgains_size_2413(ee, AR5K_EEPROM_MODE_11A) + 5;
		/* fall through */
	case AR5K_EEPROM_MODE_11A:
		break;
	default:
		break;
	}

	return offset;
}

static int
ath5k_eeprom_read_pcal_info_2413(struct ath5k_hw *ah, int mode)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	struct ath5k_chan_pcal_info_rf2413 *chan_pcal_info;
	struct ath5k_chan_pcal_info *gen_chan_info;
	unsigned int i, c;
	u32 offset;
	int ret;
	u16 val;
	u8 pd_gains = 0;

	if (ee->ee_x_gain[mode] & 0x1) pd_gains++;
	if ((ee->ee_x_gain[mode] >> 1) & 0x1) pd_gains++;
	if ((ee->ee_x_gain[mode] >> 2) & 0x1) pd_gains++;
	if ((ee->ee_x_gain[mode] >> 3) & 0x1) pd_gains++;
	ee->ee_pd_gains[mode] = pd_gains;

	offset = ath5k_cal_data_offset_2413(ee, mode);
	switch (mode) {
	case AR5K_EEPROM_MODE_11A:
		if (!AR5K_EEPROM_HDR_11A(ee->ee_header))
			return 0;

		ath5k_eeprom_init_11a_pcal_freq(ah, offset);
		offset += AR5K_EEPROM_N_5GHZ_CHAN / 2;
		gen_chan_info = ee->ee_pwr_cal_a;
		break;
	case AR5K_EEPROM_MODE_11B:
		if (!AR5K_EEPROM_HDR_11B(ee->ee_header))
			return 0;

		ath5k_eeprom_init_11bg_2413(ah, mode, offset);
		offset += AR5K_EEPROM_N_2GHZ_CHAN_2413 / 2;
		gen_chan_info = ee->ee_pwr_cal_b;
		break;
	case AR5K_EEPROM_MODE_11G:
		if (!AR5K_EEPROM_HDR_11G(ee->ee_header))
			return 0;

		ath5k_eeprom_init_11bg_2413(ah, mode, offset);
		offset += AR5K_EEPROM_N_2GHZ_CHAN_2413 / 2;
		gen_chan_info = ee->ee_pwr_cal_g;
		break;
	default:
		return -EINVAL;
	}

	if (pd_gains == 0)
		return 0;

	for (i = 0; i < ee->ee_n_piers[mode]; i++) {
		chan_pcal_info = &gen_chan_info[i].rf2413_info;

		/*
		 * Read pwr_i, pddac_i and the first
		 * 2 pd points (pwr, pddac)
		 */
		AR5K_EEPROM_READ(offset++, val);
		chan_pcal_info->pwr_i[0] = val & 0x1f;
		chan_pcal_info->pddac_i[0] = (val >> 5) & 0x7f;
		chan_pcal_info->pwr[0][0] =
					(val >> 12) & 0xf;

		AR5K_EEPROM_READ(offset++, val);
		chan_pcal_info->pddac[0][0] = val & 0x3f;
		chan_pcal_info->pwr[0][1] = (val >> 6) & 0xf;
		chan_pcal_info->pddac[0][1] =
					(val >> 10) & 0x3f;

		AR5K_EEPROM_READ(offset++, val);
		chan_pcal_info->pwr[0][2] = val & 0xf;
		chan_pcal_info->pddac[0][2] =
					(val >> 4) & 0x3f;

		chan_pcal_info->pwr[0][3] = 0;
		chan_pcal_info->pddac[0][3] = 0;

		if (pd_gains > 1) {
			/*
			 * Pd gain 0 is not the last pd gain
			 * so it only has 2 pd points.
			 * Continue wih pd gain 1.
			 */
			chan_pcal_info->pwr_i[1] = (val >> 10) & 0x1f;

			chan_pcal_info->pddac_i[1] = (val >> 15) & 0x1;
			AR5K_EEPROM_READ(offset++, val);
			chan_pcal_info->pddac_i[1] |= (val & 0x3F) << 1;

			chan_pcal_info->pwr[1][0] = (val >> 6) & 0xf;
			chan_pcal_info->pddac[1][0] =
						(val >> 10) & 0x3f;

			AR5K_EEPROM_READ(offset++, val);
			chan_pcal_info->pwr[1][1] = val & 0xf;
			chan_pcal_info->pddac[1][1] =
						(val >> 4) & 0x3f;
			chan_pcal_info->pwr[1][2] =
						(val >> 10) & 0xf;

			chan_pcal_info->pddac[1][2] =
						(val >> 14) & 0x3;
			AR5K_EEPROM_READ(offset++, val);
			chan_pcal_info->pddac[1][2] |=
						(val & 0xF) << 2;

			chan_pcal_info->pwr[1][3] = 0;
			chan_pcal_info->pddac[1][3] = 0;
		} else if (pd_gains == 1) {
			/*
			 * Pd gain 0 is the last one so
			 * read the extra point.
			 */
			chan_pcal_info->pwr[0][3] =
						(val >> 10) & 0xf;

			chan_pcal_info->pddac[0][3] =
						(val >> 14) & 0x3;
			AR5K_EEPROM_READ(offset++, val);
			chan_pcal_info->pddac[0][3] |=
						(val & 0xF) << 2;
		}

		/*
		 * Proceed with the other pd_gains
		 * as above.
		 */
		if (pd_gains > 2) {
			chan_pcal_info->pwr_i[2] = (val >> 4) & 0x1f;
			chan_pcal_info->pddac_i[2] = (val >> 9) & 0x7f;

			AR5K_EEPROM_READ(offset++, val);
			chan_pcal_info->pwr[2][0] =
						(val >> 0) & 0xf;
			chan_pcal_info->pddac[2][0] =
						(val >> 4) & 0x3f;
			chan_pcal_info->pwr[2][1] =
						(val >> 10) & 0xf;

			chan_pcal_info->pddac[2][1] =
						(val >> 14) & 0x3;
			AR5K_EEPROM_READ(offset++, val);
			chan_pcal_info->pddac[2][1] |=
						(val & 0xF) << 2;

			chan_pcal_info->pwr[2][2] =
						(val >> 4) & 0xf;
			chan_pcal_info->pddac[2][2] =
						(val >> 8) & 0x3f;

			chan_pcal_info->pwr[2][3] = 0;
			chan_pcal_info->pddac[2][3] = 0;
		} else if (pd_gains == 2) {
			chan_pcal_info->pwr[1][3] =
						(val >> 4) & 0xf;
			chan_pcal_info->pddac[1][3] =
						(val >> 8) & 0x3f;
		}

		if (pd_gains > 3) {
			chan_pcal_info->pwr_i[3] = (val >> 14) & 0x3;
			AR5K_EEPROM_READ(offset++, val);
			chan_pcal_info->pwr_i[3] |= ((val >> 0) & 0x7) << 2;

			chan_pcal_info->pddac_i[3] = (val >> 3) & 0x7f;
			chan_pcal_info->pwr[3][0] =
						(val >> 10) & 0xf;
			chan_pcal_info->pddac[3][0] =
						(val >> 14) & 0x3;

			AR5K_EEPROM_READ(offset++, val);
			chan_pcal_info->pddac[3][0] |=
						(val & 0xF) << 2;
			chan_pcal_info->pwr[3][1] =
						(val >> 4) & 0xf;
			chan_pcal_info->pddac[3][1] =
						(val >> 8) & 0x3f;

			chan_pcal_info->pwr[3][2] =
						(val >> 14) & 0x3;
			AR5K_EEPROM_READ(offset++, val);
			chan_pcal_info->pwr[3][2] |=
						((val >> 0) & 0x3) << 2;

			chan_pcal_info->pddac[3][2] =
						(val >> 2) & 0x3f;
			chan_pcal_info->pwr[3][3] =
						(val >> 8) & 0xf;

			chan_pcal_info->pddac[3][3] =
						(val >> 12) & 0xF;
			AR5K_EEPROM_READ(offset++, val);
			chan_pcal_info->pddac[3][3] |=
						((val >> 0) & 0x3) << 4;
		} else if (pd_gains == 3) {
			chan_pcal_info->pwr[2][3] =
						(val >> 14) & 0x3;
			AR5K_EEPROM_READ(offset++, val);
			chan_pcal_info->pwr[2][3] |=
						((val >> 0) & 0x3) << 2;

			chan_pcal_info->pddac[2][3] =
						(val >> 2) & 0x3f;
		}

		for (c = 0; c < pd_gains; c++) {
			/* Recreate pwr table for this channel using pwr steps */
			chan_pcal_info->pwr[c][0] += chan_pcal_info->pwr_i[c] * 2;
			chan_pcal_info->pwr[c][1] += chan_pcal_info->pwr[c][0];
			chan_pcal_info->pwr[c][2] += chan_pcal_info->pwr[c][1];
			chan_pcal_info->pwr[c][3] += chan_pcal_info->pwr[c][2];
			if (chan_pcal_info->pwr[c][3] == chan_pcal_info->pwr[c][2])
				chan_pcal_info->pwr[c][3] = 0;

			/* Recreate pddac table for this channel using pddac steps */
			chan_pcal_info->pddac[c][0] += chan_pcal_info->pddac_i[c];
			chan_pcal_info->pddac[c][1] += chan_pcal_info->pddac[c][0];
			chan_pcal_info->pddac[c][2] += chan_pcal_info->pddac[c][1];
			chan_pcal_info->pddac[c][3] += chan_pcal_info->pddac[c][2];
			if (chan_pcal_info->pddac[c][3] == chan_pcal_info->pddac[c][2])
				chan_pcal_info->pddac[c][3] = 0;
		}
	}

	return 0;
}

/*
 * Read per rate target power (this is the maximum tx power
 * supported by the card). This info is used when setting
 * tx power, no matter the channel.
 *
 * This also works for v5 EEPROMs.
 */
static int ath5k_eeprom_read_target_rate_pwr_info(struct ath5k_hw *ah, unsigned int mode)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	struct ath5k_rate_pcal_info *rate_pcal_info;
	u16 *rate_target_pwr_num;
	u32 offset;
	u16 val;
	int ret, i;

	offset = AR5K_EEPROM_TARGET_PWRSTART(ee->ee_misc1);
	rate_target_pwr_num = &ee->ee_rate_target_pwr_num[mode];
	switch (mode) {
	case AR5K_EEPROM_MODE_11A:
		offset += AR5K_EEPROM_TARGET_PWR_OFF_11A(ee->ee_version);
		rate_pcal_info = ee->ee_rate_tpwr_a;
		ee->ee_rate_target_pwr_num[mode] = AR5K_EEPROM_N_5GHZ_CHAN;
		break;
	case AR5K_EEPROM_MODE_11B:
		offset += AR5K_EEPROM_TARGET_PWR_OFF_11B(ee->ee_version);
		rate_pcal_info = ee->ee_rate_tpwr_b;
		ee->ee_rate_target_pwr_num[mode] = 2; /* 3rd is g mode's 1st */
		break;
	case AR5K_EEPROM_MODE_11G:
		offset += AR5K_EEPROM_TARGET_PWR_OFF_11G(ee->ee_version);
		rate_pcal_info = ee->ee_rate_tpwr_g;
		ee->ee_rate_target_pwr_num[mode] = AR5K_EEPROM_N_2GHZ_CHAN;
		break;
	default:
		return -EINVAL;
	}

	/* Different freq mask for older eeproms (<= v3.2) */
	if (ee->ee_version <= AR5K_EEPROM_VERSION_3_2) {
		for (i = 0; i < (*rate_target_pwr_num); i++) {
			AR5K_EEPROM_READ(offset++, val);
			rate_pcal_info[i].freq =
			    ath5k_eeprom_bin2freq(ee, (val >> 9) & 0x7f, mode);

			rate_pcal_info[i].target_power_6to24 = ((val >> 3) & 0x3f);
			rate_pcal_info[i].target_power_36 = (val << 3) & 0x3f;

			AR5K_EEPROM_READ(offset++, val);

			if (rate_pcal_info[i].freq == AR5K_EEPROM_CHANNEL_DIS ||
			    val == 0) {
				(*rate_target_pwr_num) = i;
				break;
			}

			rate_pcal_info[i].target_power_36 |= ((val >> 13) & 0x7);
			rate_pcal_info[i].target_power_48 = ((val >> 7) & 0x3f);
			rate_pcal_info[i].target_power_54 = ((val >> 1) & 0x3f);
		}
	} else {
		for (i = 0; i < (*rate_target_pwr_num); i++) {
			AR5K_EEPROM_READ(offset++, val);
			rate_pcal_info[i].freq =
			    ath5k_eeprom_bin2freq(ee, (val >> 8) & 0xff, mode);

			rate_pcal_info[i].target_power_6to24 = ((val >> 2) & 0x3f);
			rate_pcal_info[i].target_power_36 = (val << 4) & 0x3f;

			AR5K_EEPROM_READ(offset++, val);

			if (rate_pcal_info[i].freq == AR5K_EEPROM_CHANNEL_DIS ||
			    val == 0) {
				(*rate_target_pwr_num) = i;
				break;
			}

			rate_pcal_info[i].target_power_36 |= (val >> 12) & 0xf;
			rate_pcal_info[i].target_power_48 = ((val >> 6) & 0x3f);
			rate_pcal_info[i].target_power_54 = (val & 0x3f);
		}
	}

	return 0;
}

static int
ath5k_eeprom_read_pcal_info(struct ath5k_hw *ah)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	int (*read_pcal)(struct ath5k_hw *hw, int mode);
	int mode;
	int err;

	if ((ah->ah_ee_version >= AR5K_EEPROM_VERSION_4_0) &&
			(AR5K_EEPROM_EEMAP(ee->ee_misc0) == 1))
		read_pcal = ath5k_eeprom_read_pcal_info_5112;
	else if ((ah->ah_ee_version >= AR5K_EEPROM_VERSION_5_0) &&
			(AR5K_EEPROM_EEMAP(ee->ee_misc0) == 2))
		read_pcal = ath5k_eeprom_read_pcal_info_2413;
	else
		read_pcal = ath5k_eeprom_read_pcal_info_5111;

	for (mode = AR5K_EEPROM_MODE_11A; mode <= AR5K_EEPROM_MODE_11G; mode++) {
		err = read_pcal(ah, mode);
		if (err)
			return err;

		err = ath5k_eeprom_read_target_rate_pwr_info(ah, mode);
		if (err < 0)
			return err;
	}

	return 0;
}

/* Read conformance test limits */
static int
ath5k_eeprom_read_ctl_info(struct ath5k_hw *ah)
{
	struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
	struct ath5k_edge_power *rep;
	unsigned int fmask, pmask;
	unsigned int ctl_mode;
	int ret, i, j;
	u32 offset;
	u16 val;

	pmask = AR5K_EEPROM_POWER_M;
	fmask = AR5K_EEPROM_FREQ_M(ee->ee_version);
	offset = AR5K_EEPROM_CTL(ee->ee_version);
	ee->ee_ctls = AR5K_EEPROM_N_CTLS(ee->ee_version);
	for (i = 0; i < ee->ee_ctls; i += 2) {
		AR5K_EEPROM_READ(offset++, val);
		ee->ee_ctl[i] = (val >> 8) & 0xff;
		ee->ee_ctl[i + 1] = val & 0xff;
	}

	offset = AR5K_EEPROM_GROUP8_OFFSET;
	if (ee->ee_version >= AR5K_EEPROM_VERSION_4_0)
		offset += AR5K_EEPROM_TARGET_PWRSTART(ee->ee_misc1) -
			AR5K_EEPROM_GROUP5_OFFSET;
	else
		offset += AR5K_EEPROM_GROUPS_START(ee->ee_version);

	rep = ee->ee_ctl_pwr;
	for(i = 0; i < ee->ee_ctls; i++) {
		switch(ee->ee_ctl[i] & AR5K_CTL_MODE_M) {
		case AR5K_CTL_11A:
		case AR5K_CTL_TURBO:
			ctl_mode = AR5K_EEPROM_MODE_11A;
			break;
		default:
			ctl_mode = AR5K_EEPROM_MODE_11G;
			break;
		}
		if (ee->ee_ctl[i] == 0) {
			if (ee->ee_version >= AR5K_EEPROM_VERSION_3_3)
				offset += 8;
			else
				offset += 7;
			rep += AR5K_EEPROM_N_EDGES;
			continue;
		}
		if (ee->ee_version >= AR5K_EEPROM_VERSION_3_3) {
			for (j = 0; j < AR5K_EEPROM_N_EDGES; j += 2) {
				AR5K_EEPROM_READ(offset++, val);
				rep[j].freq = (val >> 8) & fmask;
				rep[j + 1].freq = val & fmask;
			}
			for (j = 0; j < AR5K_EEPROM_N_EDGES; j += 2) {
				AR5K_EEPROM_READ(offset++, val);
				rep[j].edge = (val >> 8) & pmask;
				rep[j].flag = (val >> 14) & 1;
				rep[j + 1].edge = val & pmask;
				rep[j + 1].flag = (val >> 6) & 1;
			}
		} else {
			AR5K_EEPROM_READ(offset++, val);
			rep[0].freq = (val >> 9) & fmask;
			rep[1].freq = (val >> 2) & fmask;
			rep[2].freq = (val << 5) & fmask;

			AR5K_EEPROM_READ(offset++, val);
			rep[2].freq |= (val >> 11) & 0x1f;
			rep[3].freq = (val >> 4) & fmask;
			rep[4].freq = (val << 3) & fmask;

			AR5K_EEPROM_READ(offset++, val);
			rep[4].freq |= (val >> 13) & 0x7;
			rep[5].freq = (val >> 6) & fmask;
			rep[6].freq = (val << 1) & fmask;

			AR5K_EEPROM_READ(offset++, val);
			rep[6].freq |= (val >> 15) & 0x1;
			rep[7].freq = (val >> 8) & fmask;

			rep[0].edge = (val >> 2) & pmask;
			rep[1].edge = (val << 4) & pmask;

			AR5K_EEPROM_READ(offset++, val);
			rep[1].edge |= (val >> 12) & 0xf;
			rep[2].edge = (val >> 6) & pmask;
			rep[3].edge = val & pmask;

			AR5K_EEPROM_READ(offset++, val);
			rep[4].edge = (val >> 10) & pmask;
			rep[5].edge = (val >> 4) & pmask;
			rep[6].edge = (val << 2) & pmask;

			AR5K_EEPROM_READ(offset++, val);
			rep[6].edge |= (val >> 14) & 0x3;
			rep[7].edge = (val >> 8) & pmask;
		}
		for (j = 0; j < AR5K_EEPROM_N_EDGES; j++) {
			rep[j].freq = ath5k_eeprom_bin2freq(ee,
				rep[j].freq, ctl_mode);
		}
		rep += AR5K_EEPROM_N_EDGES;
	}

	return 0;
}


/*
 * Initialize eeprom power tables
 */
int
ath5k_eeprom_init(struct ath5k_hw *ah)
{
	int err;

	err = ath5k_eeprom_init_header(ah);
	if (err < 0)
		return err;

	err = ath5k_eeprom_init_modes(ah);
	if (err < 0)
		return err;

	err = ath5k_eeprom_read_pcal_info(ah);
	if (err < 0)
		return err;

	err = ath5k_eeprom_read_ctl_info(ah);
	if (err < 0)
		return err;

	return 0;
}
/*
 * Read the MAC address from eeprom
 */
int ath5k_eeprom_read_mac(struct ath5k_hw *ah, u8 *mac)
{
	u8 mac_d[ETH_ALEN];
	u32 total, offset;
	u16 data;
	int octet, ret;

	memset(mac, 0, ETH_ALEN);
	memset(mac_d, 0, ETH_ALEN);

	ret = ath5k_hw_eeprom_read(ah, 0x20, &data);
	if (ret)
		return ret;

	for (offset = 0x1f, octet = 0, total = 0; offset >= 0x1d; offset--) {
		ret = ath5k_hw_eeprom_read(ah, offset, &data);
		if (ret)
			return ret;

		total += data;
		mac_d[octet + 1] = data & 0xff;
		mac_d[octet] = data >> 8;
		octet += 2;
	}

	memcpy(mac, mac_d, ETH_ALEN);

	if (!total || total == 3 * 0xffff)
		return -EINVAL;

	return 0;
}

