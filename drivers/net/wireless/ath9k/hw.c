/*
 * Copyright (c) 2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
 */

#include <linux/io.h>
#include <asm/unaligned.h>

#include "core.h"
#include "hw.h"
#include "reg.h"
#include "phy.h"
#include "initvals.h"

static void ath9k_hw_iqcal_collect(struct ath_hal *ah);
static void ath9k_hw_iqcalibrate(struct ath_hal *ah, u8 numChains);
static void ath9k_hw_adc_gaincal_collect(struct ath_hal *ah);
static void ath9k_hw_adc_gaincal_calibrate(struct ath_hal *ah,
					   u8 numChains);
static void ath9k_hw_adc_dccal_collect(struct ath_hal *ah);
static void ath9k_hw_adc_dccal_calibrate(struct ath_hal *ah,
					 u8 numChains);

static const u8 CLOCK_RATE[] = { 40, 80, 22, 44, 88, 40 };
static const int16_t NOISE_FLOOR[] = { -96, -93, -98, -96, -93, -96 };

static const struct hal_percal_data iq_cal_multi_sample = {
	IQ_MISMATCH_CAL,
	MAX_CAL_SAMPLES,
	PER_MIN_LOG_COUNT,
	ath9k_hw_iqcal_collect,
	ath9k_hw_iqcalibrate
};
static const struct hal_percal_data iq_cal_single_sample = {
	IQ_MISMATCH_CAL,
	MIN_CAL_SAMPLES,
	PER_MAX_LOG_COUNT,
	ath9k_hw_iqcal_collect,
	ath9k_hw_iqcalibrate
};
static const struct hal_percal_data adc_gain_cal_multi_sample = {
	ADC_GAIN_CAL,
	MAX_CAL_SAMPLES,
	PER_MIN_LOG_COUNT,
	ath9k_hw_adc_gaincal_collect,
	ath9k_hw_adc_gaincal_calibrate
};
static const struct hal_percal_data adc_gain_cal_single_sample = {
	ADC_GAIN_CAL,
	MIN_CAL_SAMPLES,
	PER_MAX_LOG_COUNT,
	ath9k_hw_adc_gaincal_collect,
	ath9k_hw_adc_gaincal_calibrate
};
static const struct hal_percal_data adc_dc_cal_multi_sample = {
	ADC_DC_CAL,
	MAX_CAL_SAMPLES,
	PER_MIN_LOG_COUNT,
	ath9k_hw_adc_dccal_collect,
	ath9k_hw_adc_dccal_calibrate
};
static const struct hal_percal_data adc_dc_cal_single_sample = {
	ADC_DC_CAL,
	MIN_CAL_SAMPLES,
	PER_MAX_LOG_COUNT,
	ath9k_hw_adc_dccal_collect,
	ath9k_hw_adc_dccal_calibrate
};
static const struct hal_percal_data adc_init_dc_cal = {
	ADC_DC_INIT_CAL,
	MIN_CAL_SAMPLES,
	INIT_LOG_COUNT,
	ath9k_hw_adc_dccal_collect,
	ath9k_hw_adc_dccal_calibrate
};

static const struct ath_hal ar5416hal = {
	AR5416_MAGIC,
	0,
	0,
	NULL,
	NULL,
	CTRY_DEFAULT,
	0,
	0,
	0,
	0,
	0,
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	},
};

static struct ath9k_rate_table ar5416_11a_table = {
	8,
	{0},
	{
		{true, PHY_OFDM, 6000, 0x0b, 0x00, (0x80 | 12), 0},
		{true, PHY_OFDM, 9000, 0x0f, 0x00, 18, 0},
		{true, PHY_OFDM, 12000, 0x0a, 0x00, (0x80 | 24), 2},
		{true, PHY_OFDM, 18000, 0x0e, 0x00, 36, 2},
		{true, PHY_OFDM, 24000, 0x09, 0x00, (0x80 | 48), 4},
		{true, PHY_OFDM, 36000, 0x0d, 0x00, 72, 4},
		{true, PHY_OFDM, 48000, 0x08, 0x00, 96, 4},
		{true, PHY_OFDM, 54000, 0x0c, 0x00, 108, 4}
	},
};

static struct ath9k_rate_table ar5416_11b_table = {
	4,
	{0},
	{
		{true, PHY_CCK, 1000, 0x1b, 0x00, (0x80 | 2), 0},
		{true, PHY_CCK, 2000, 0x1a, 0x04, (0x80 | 4), 1},
		{true, PHY_CCK, 5500, 0x19, 0x04, (0x80 | 11), 1},
		{true, PHY_CCK, 11000, 0x18, 0x04, (0x80 | 22), 1}
	},
};

static struct ath9k_rate_table ar5416_11g_table = {
	12,
	{0},
	{
		{true, PHY_CCK, 1000, 0x1b, 0x00, (0x80 | 2), 0},
		{true, PHY_CCK, 2000, 0x1a, 0x04, (0x80 | 4), 1},
		{true, PHY_CCK, 5500, 0x19, 0x04, (0x80 | 11), 2},
		{true, PHY_CCK, 11000, 0x18, 0x04, (0x80 | 22), 3},

		{false, PHY_OFDM, 6000, 0x0b, 0x00, 12, 4},
		{false, PHY_OFDM, 9000, 0x0f, 0x00, 18, 4},
		{true, PHY_OFDM, 12000, 0x0a, 0x00, 24, 6},
		{true, PHY_OFDM, 18000, 0x0e, 0x00, 36, 6},
		{true, PHY_OFDM, 24000, 0x09, 0x00, 48, 8},
		{true, PHY_OFDM, 36000, 0x0d, 0x00, 72, 8},
		{true, PHY_OFDM, 48000, 0x08, 0x00, 96, 8},
		{true, PHY_OFDM, 54000, 0x0c, 0x00, 108, 8}
	},
};

static struct ath9k_rate_table ar5416_11ng_table = {
	28,
	{0},
	{
		{true, PHY_CCK, 1000, 0x1b, 0x00, (0x80 | 2), 0},
		{true, PHY_CCK, 2000, 0x1a, 0x04, (0x80 | 4), 1},
		{true, PHY_CCK, 5500, 0x19, 0x04, (0x80 | 11), 2},
		{true, PHY_CCK, 11000, 0x18, 0x04, (0x80 | 22), 3},

		{false, PHY_OFDM, 6000, 0x0b, 0x00, 12, 4},
		{false, PHY_OFDM, 9000, 0x0f, 0x00, 18, 4},
		{true, PHY_OFDM, 12000, 0x0a, 0x00, 24, 6},
		{true, PHY_OFDM, 18000, 0x0e, 0x00, 36, 6},
		{true, PHY_OFDM, 24000, 0x09, 0x00, 48, 8},
		{true, PHY_OFDM, 36000, 0x0d, 0x00, 72, 8},
		{true, PHY_OFDM, 48000, 0x08, 0x00, 96, 8},
		{true, PHY_OFDM, 54000, 0x0c, 0x00, 108, 8},
		{true, PHY_HT, 6500, 0x80, 0x00, 0, 4},
		{true, PHY_HT, 13000, 0x81, 0x00, 1, 6},
		{true, PHY_HT, 19500, 0x82, 0x00, 2, 6},
		{true, PHY_HT, 26000, 0x83, 0x00, 3, 8},
		{true, PHY_HT, 39000, 0x84, 0x00, 4, 8},
		{true, PHY_HT, 52000, 0x85, 0x00, 5, 8},
		{true, PHY_HT, 58500, 0x86, 0x00, 6, 8},
		{true, PHY_HT, 65000, 0x87, 0x00, 7, 8},
		{true, PHY_HT, 13000, 0x88, 0x00, 8, 4},
		{true, PHY_HT, 26000, 0x89, 0x00, 9, 6},
		{true, PHY_HT, 39000, 0x8a, 0x00, 10, 6},
		{true, PHY_HT, 52000, 0x8b, 0x00, 11, 8},
		{true, PHY_HT, 78000, 0x8c, 0x00, 12, 8},
		{true, PHY_HT, 104000, 0x8d, 0x00, 13, 8},
		{true, PHY_HT, 117000, 0x8e, 0x00, 14, 8},
		{true, PHY_HT, 130000, 0x8f, 0x00, 15, 8},
	},
};

static struct ath9k_rate_table ar5416_11na_table = {
	24,
	{0},
	{
		{true, PHY_OFDM, 6000, 0x0b, 0x00, (0x80 | 12), 0},
		{true, PHY_OFDM, 9000, 0x0f, 0x00, 18, 0},
		{true, PHY_OFDM, 12000, 0x0a, 0x00, (0x80 | 24), 2},
		{true, PHY_OFDM, 18000, 0x0e, 0x00, 36, 2},
		{true, PHY_OFDM, 24000, 0x09, 0x00, (0x80 | 48), 4},
		{true, PHY_OFDM, 36000, 0x0d, 0x00, 72, 4},
		{true, PHY_OFDM, 48000, 0x08, 0x00, 96, 4},
		{true, PHY_OFDM, 54000, 0x0c, 0x00, 108, 4},
		{true, PHY_HT, 6500, 0x80, 0x00, 0, 0},
		{true, PHY_HT, 13000, 0x81, 0x00, 1, 2},
		{true, PHY_HT, 19500, 0x82, 0x00, 2, 2},
		{true, PHY_HT, 26000, 0x83, 0x00, 3, 4},
		{true, PHY_HT, 39000, 0x84, 0x00, 4, 4},
		{true, PHY_HT, 52000, 0x85, 0x00, 5, 4},
		{true, PHY_HT, 58500, 0x86, 0x00, 6, 4},
		{true, PHY_HT, 65000, 0x87, 0x00, 7, 4},
		{true, PHY_HT, 13000, 0x88, 0x00, 8, 0},
		{true, PHY_HT, 26000, 0x89, 0x00, 9, 2},
		{true, PHY_HT, 39000, 0x8a, 0x00, 10, 2},
		{true, PHY_HT, 52000, 0x8b, 0x00, 11, 4},
		{true, PHY_HT, 78000, 0x8c, 0x00, 12, 4},
		{true, PHY_HT, 104000, 0x8d, 0x00, 13, 4},
		{true, PHY_HT, 117000, 0x8e, 0x00, 14, 4},
		{true, PHY_HT, 130000, 0x8f, 0x00, 15, 4},
	},
};

static enum wireless_mode ath9k_hw_chan2wmode(struct ath_hal *ah,
				       const struct ath9k_channel *chan)
{
	if (IS_CHAN_CCK(chan))
		return ATH9K_MODE_11A;
	if (IS_CHAN_G(chan))
		return ATH9K_MODE_11G;
	return ATH9K_MODE_11A;
}

static bool ath9k_hw_wait(struct ath_hal *ah,
			  u32 reg,
			  u32 mask,
			  u32 val)
{
	int i;

	for (i = 0; i < (AH_TIMEOUT / AH_TIME_QUANTUM); i++) {
		if ((REG_READ(ah, reg) & mask) == val)
			return true;

		udelay(AH_TIME_QUANTUM);
	}
	DPRINTF(ah->ah_sc, ATH_DBG_PHY_IO,
		 "%s: timeout on reg 0x%x: 0x%08x & 0x%08x != 0x%08x\n",
		 __func__, reg, REG_READ(ah, reg), mask, val);
	return false;
}

static bool ath9k_hw_eeprom_read(struct ath_hal *ah, u32 off,
				 u16 *data)
{
	(void) REG_READ(ah, AR5416_EEPROM_OFFSET + (off << AR5416_EEPROM_S));

	if (!ath9k_hw_wait(ah,
			   AR_EEPROM_STATUS_DATA,
			   AR_EEPROM_STATUS_DATA_BUSY |
			   AR_EEPROM_STATUS_DATA_PROT_ACCESS, 0)) {
		return false;
	}

	*data = MS(REG_READ(ah, AR_EEPROM_STATUS_DATA),
		   AR_EEPROM_STATUS_DATA_VAL);

	return true;
}

static int ath9k_hw_flash_map(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	ahp->ah_cal_mem = ioremap(AR5416_EEPROM_START_ADDR, AR5416_EEPROM_MAX);

	if (!ahp->ah_cal_mem) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			 "%s: cannot remap eeprom region \n", __func__);
		return -EIO;
	}

	return 0;
}

static bool ath9k_hw_flash_read(struct ath_hal *ah, u32 off,
				u16 *data)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	*data = ioread16(ahp->ah_cal_mem + off);
	return true;
}

static void ath9k_hw_read_revisions(struct ath_hal *ah)
{
	u32 val;

	val = REG_READ(ah, AR_SREV) & AR_SREV_ID;

	if (val == 0xFF) {
		val = REG_READ(ah, AR_SREV);

		ah->ah_macVersion =
			(val & AR_SREV_VERSION2) >> AR_SREV_TYPE2_S;

		ah->ah_macRev = MS(val, AR_SREV_REVISION2);
		ah->ah_isPciExpress =
			(val & AR_SREV_TYPE2_HOST_MODE) ? 0 : 1;

	} else {
		if (!AR_SREV_9100(ah))
			ah->ah_macVersion = MS(val, AR_SREV_VERSION);

		ah->ah_macRev = val & AR_SREV_REVISION;

		if (ah->ah_macVersion == AR_SREV_VERSION_5416_PCIE)
			ah->ah_isPciExpress = true;
	}
}

u32 ath9k_hw_reverse_bits(u32 val, u32 n)
{
	u32 retval;
	int i;

	for (i = 0, retval = 0; i < n; i++) {
		retval = (retval << 1) | (val & 1);
		val >>= 1;
	}
	return retval;
}

static void ath9k_hw_set_defaults(struct ath_hal *ah)
{
	int i;

	ah->ah_config.dma_beacon_response_time = 2;
	ah->ah_config.sw_beacon_response_time = 10;
	ah->ah_config.additional_swba_backoff = 0;
	ah->ah_config.ack_6mb = 0x0;
	ah->ah_config.cwm_ignore_extcca = 0;
	ah->ah_config.pcie_powersave_enable = 0;
	ah->ah_config.pcie_l1skp_enable = 0;
	ah->ah_config.pcie_clock_req = 0;
	ah->ah_config.pcie_power_reset = 0x100;
	ah->ah_config.pcie_restore = 0;
	ah->ah_config.pcie_waen = 0;
	ah->ah_config.analog_shiftreg = 1;
	ah->ah_config.ht_enable = 1;
	ah->ah_config.ofdm_trig_low = 200;
	ah->ah_config.ofdm_trig_high = 500;
	ah->ah_config.cck_trig_high = 200;
	ah->ah_config.cck_trig_low = 100;
	ah->ah_config.enable_ani = 0;
	ah->ah_config.noise_immunity_level = 4;
	ah->ah_config.ofdm_weaksignal_det = 1;
	ah->ah_config.cck_weaksignal_thr = 0;
	ah->ah_config.spur_immunity_level = 2;
	ah->ah_config.firstep_level = 0;
	ah->ah_config.rssi_thr_high = 40;
	ah->ah_config.rssi_thr_low = 7;
	ah->ah_config.diversity_control = 0;
	ah->ah_config.antenna_switch_swap = 0;

	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		ah->ah_config.spurchans[i][0] = AR_NO_SPUR;
		ah->ah_config.spurchans[i][1] = AR_NO_SPUR;
	}

	ah->ah_config.intr_mitigation = 0;
}

static inline void ath9k_hw_override_ini(struct ath_hal *ah,
					 struct ath9k_channel *chan)
{
	if (!AR_SREV_5416_V20_OR_LATER(ah)
	    || AR_SREV_9280_10_OR_LATER(ah))
		return;

	REG_WRITE(ah, 0x9800 + (651 << 2), 0x11);
}

static inline void ath9k_hw_init_bb(struct ath_hal *ah,
				    struct ath9k_channel *chan)
{
	u32 synthDelay;

	synthDelay = REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_DELAY;
	if (IS_CHAN_CCK(chan))
		synthDelay = (4 * synthDelay) / 22;
	else
		synthDelay /= 10;

	REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);

	udelay(synthDelay + BASE_ACTIVATE_DELAY);
}

static inline void ath9k_hw_init_interrupt_masks(struct ath_hal *ah,
						 enum ath9k_opmode opmode)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	ahp->ah_maskReg = AR_IMR_TXERR |
		AR_IMR_TXURN |
		AR_IMR_RXERR |
		AR_IMR_RXORN |
		AR_IMR_BCNMISC;

	if (ahp->ah_intrMitigation)
		ahp->ah_maskReg |= AR_IMR_RXINTM | AR_IMR_RXMINTR;
	else
		ahp->ah_maskReg |= AR_IMR_RXOK;

	ahp->ah_maskReg |= AR_IMR_TXOK;

	if (opmode == ATH9K_M_HOSTAP)
		ahp->ah_maskReg |= AR_IMR_MIB;

	REG_WRITE(ah, AR_IMR, ahp->ah_maskReg);
	REG_WRITE(ah, AR_IMR_S2, REG_READ(ah, AR_IMR_S2) | AR_IMR_S2_GTT);

	if (!AR_SREV_9100(ah)) {
		REG_WRITE(ah, AR_INTR_SYNC_CAUSE, 0xFFFFFFFF);
		REG_WRITE(ah, AR_INTR_SYNC_ENABLE, AR_INTR_SYNC_DEFAULT);
		REG_WRITE(ah, AR_INTR_SYNC_MASK, 0);
	}
}

static inline void ath9k_hw_init_qos(struct ath_hal *ah)
{
	REG_WRITE(ah, AR_MIC_QOS_CONTROL, 0x100aa);
	REG_WRITE(ah, AR_MIC_QOS_SELECT, 0x3210);

	REG_WRITE(ah, AR_QOS_NO_ACK,
		  SM(2, AR_QOS_NO_ACK_TWO_BIT) |
		  SM(5, AR_QOS_NO_ACK_BIT_OFF) |
		  SM(0, AR_QOS_NO_ACK_BYTE_OFF));

	REG_WRITE(ah, AR_TXOP_X, AR_TXOP_X_VAL);
	REG_WRITE(ah, AR_TXOP_0_3, 0xFFFFFFFF);
	REG_WRITE(ah, AR_TXOP_4_7, 0xFFFFFFFF);
	REG_WRITE(ah, AR_TXOP_8_11, 0xFFFFFFFF);
	REG_WRITE(ah, AR_TXOP_12_15, 0xFFFFFFFF);
}

static void ath9k_hw_analog_shift_rmw(struct ath_hal *ah,
				      u32 reg,
				      u32 mask,
				      u32 shift,
				      u32 val)
{
	u32 regVal;

	regVal = REG_READ(ah, reg) & ~mask;
	regVal |= (val << shift) & mask;

	REG_WRITE(ah, reg, regVal);

	if (ah->ah_config.analog_shiftreg)
		udelay(100);

	return;
}

static u8 ath9k_hw_get_num_ant_config(struct ath_hal_5416 *ahp,
				      enum ieee80211_band freq_band)
{
	struct ar5416_eeprom *eep = &ahp->ah_eeprom;
	struct modal_eep_header *pModal =
		&(eep->modalHeader[IEEE80211_BAND_5GHZ == freq_band]);
	struct base_eep_header *pBase = &eep->baseEepHeader;
	u8 num_ant_config;

	num_ant_config = 1;

	if (pBase->version >= 0x0E0D)
		if (pModal->useAnt1)
			num_ant_config += 1;

	return num_ant_config;
}

static int
ath9k_hw_get_eeprom_antenna_cfg(struct ath_hal_5416 *ahp,
				struct ath9k_channel *chan,
				u8 index,
				u16 *config)
{
	struct ar5416_eeprom *eep = &ahp->ah_eeprom;
	struct modal_eep_header *pModal =
		&(eep->modalHeader[IS_CHAN_2GHZ(chan)]);
	struct base_eep_header *pBase = &eep->baseEepHeader;

	switch (index) {
	case 0:
		*config = pModal->antCtrlCommon & 0xFFFF;
		return 0;
	case 1:
		if (pBase->version >= 0x0E0D) {
			if (pModal->useAnt1) {
				*config =
				((pModal->antCtrlCommon & 0xFFFF0000) >> 16);
				return 0;
			}
		}
		break;
	default:
		break;
	}

	return -EINVAL;
}

static inline bool ath9k_hw_nvram_read(struct ath_hal *ah,
				       u32 off,
				       u16 *data)
{
	if (ath9k_hw_use_flash(ah))
		return ath9k_hw_flash_read(ah, off, data);
	else
		return ath9k_hw_eeprom_read(ah, off, data);
}

static inline bool ath9k_hw_fill_eeprom(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *eep = &ahp->ah_eeprom;
	u16 *eep_data;
	int addr, ar5416_eep_start_loc = 0;

	if (!ath9k_hw_use_flash(ah)) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			 "%s: Reading from EEPROM, not flash\n", __func__);
		ar5416_eep_start_loc = 256;
	}
	if (AR_SREV_9100(ah))
		ar5416_eep_start_loc = 256;

	eep_data = (u16 *) eep;
	for (addr = 0;
	     addr < sizeof(struct ar5416_eeprom) / sizeof(u16);
	     addr++) {
		if (!ath9k_hw_nvram_read(ah, addr + ar5416_eep_start_loc,
					 eep_data)) {
			DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
				 "%s: Unable to read eeprom region \n",
				 __func__);
			return false;
		}
		eep_data++;
	}
	return true;
}

/* XXX: Clean me up, make me more legible */
static bool
ath9k_hw_eeprom_set_board_values(struct ath_hal *ah,
				 struct ath9k_channel *chan)
{
	struct modal_eep_header *pModal;
	int i, regChainOffset;
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *eep = &ahp->ah_eeprom;
	u8 txRxAttenLocal;
	u16 ant_config;

	pModal = &(eep->modalHeader[IS_CHAN_2GHZ(chan)]);

	txRxAttenLocal = IS_CHAN_2GHZ(chan) ? 23 : 44;

	ath9k_hw_get_eeprom_antenna_cfg(ahp, chan, 1, &ant_config);
	REG_WRITE(ah, AR_PHY_SWITCH_COM, ant_config);

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		if (AR_SREV_9280(ah)) {
			if (i >= 2)
				break;
		}

		if (AR_SREV_5416_V20_OR_LATER(ah) &&
		    (ahp->ah_rxchainmask == 5 || ahp->ah_txchainmask == 5)
		    && (i != 0))
			regChainOffset = (i == 1) ? 0x2000 : 0x1000;
		else
			regChainOffset = i * 0x1000;

		REG_WRITE(ah, AR_PHY_SWITCH_CHAIN_0 + regChainOffset,
			  pModal->antCtrlChain[i]);

		REG_WRITE(ah, AR_PHY_TIMING_CTRL4(0) + regChainOffset,
			  (REG_READ(ah,
				    AR_PHY_TIMING_CTRL4(0) +
				    regChainOffset) &
			   ~(AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF |
			     AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF)) |
			  SM(pModal->iqCalICh[i],
			     AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF) |
			  SM(pModal->iqCalQCh[i],
			     AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF));

		if ((i == 0) || AR_SREV_5416_V20_OR_LATER(ah)) {
			if ((eep->baseEepHeader.version &
			     AR5416_EEP_VER_MINOR_MASK) >=
			    AR5416_EEP_MINOR_VER_3) {
				txRxAttenLocal = pModal->txRxAttenCh[i];
				if (AR_SREV_9280_10_OR_LATER(ah)) {
					REG_RMW_FIELD(ah,
						AR_PHY_GAIN_2GHZ +
						regChainOffset,
						AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN,
						pModal->
						bswMargin[i]);
					REG_RMW_FIELD(ah,
						AR_PHY_GAIN_2GHZ +
						regChainOffset,
						AR_PHY_GAIN_2GHZ_XATTEN1_DB,
						pModal->
						bswAtten[i]);
					REG_RMW_FIELD(ah,
						AR_PHY_GAIN_2GHZ +
						regChainOffset,
						AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN,
						pModal->
						xatten2Margin[i]);
					REG_RMW_FIELD(ah,
						AR_PHY_GAIN_2GHZ +
						regChainOffset,
						AR_PHY_GAIN_2GHZ_XATTEN2_DB,
						pModal->
						xatten2Db[i]);
				} else {
					REG_WRITE(ah,
						  AR_PHY_GAIN_2GHZ +
						  regChainOffset,
						  (REG_READ(ah,
							    AR_PHY_GAIN_2GHZ +
							    regChainOffset) &
						   ~AR_PHY_GAIN_2GHZ_BSW_MARGIN)
						  | SM(pModal->
						  bswMargin[i],
						  AR_PHY_GAIN_2GHZ_BSW_MARGIN));
					REG_WRITE(ah,
						  AR_PHY_GAIN_2GHZ +
						  regChainOffset,
						  (REG_READ(ah,
							    AR_PHY_GAIN_2GHZ +
							    regChainOffset) &
						   ~AR_PHY_GAIN_2GHZ_BSW_ATTEN)
						  | SM(pModal->bswAtten[i],
						  AR_PHY_GAIN_2GHZ_BSW_ATTEN));
				}
			}
			if (AR_SREV_9280_10_OR_LATER(ah)) {
				REG_RMW_FIELD(ah,
					      AR_PHY_RXGAIN +
					      regChainOffset,
					      AR9280_PHY_RXGAIN_TXRX_ATTEN,
					      txRxAttenLocal);
				REG_RMW_FIELD(ah,
					      AR_PHY_RXGAIN +
					      regChainOffset,
					      AR9280_PHY_RXGAIN_TXRX_MARGIN,
					      pModal->rxTxMarginCh[i]);
			} else {
				REG_WRITE(ah,
					  AR_PHY_RXGAIN + regChainOffset,
					  (REG_READ(ah,
						    AR_PHY_RXGAIN +
						    regChainOffset) &
					   ~AR_PHY_RXGAIN_TXRX_ATTEN) |
					  SM(txRxAttenLocal,
					     AR_PHY_RXGAIN_TXRX_ATTEN));
				REG_WRITE(ah,
					  AR_PHY_GAIN_2GHZ +
					  regChainOffset,
					  (REG_READ(ah,
						    AR_PHY_GAIN_2GHZ +
						    regChainOffset) &
					   ~AR_PHY_GAIN_2GHZ_RXTX_MARGIN) |
					  SM(pModal->rxTxMarginCh[i],
					     AR_PHY_GAIN_2GHZ_RXTX_MARGIN));
			}
		}
	}

	if (AR_SREV_9280_10_OR_LATER(ah)) {
		if (IS_CHAN_2GHZ(chan)) {
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF2G1_CH0,
						  AR_AN_RF2G1_CH0_OB,
						  AR_AN_RF2G1_CH0_OB_S,
						  pModal->ob);
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF2G1_CH0,
						  AR_AN_RF2G1_CH0_DB,
						  AR_AN_RF2G1_CH0_DB_S,
						  pModal->db);
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF2G1_CH1,
						  AR_AN_RF2G1_CH1_OB,
						  AR_AN_RF2G1_CH1_OB_S,
						  pModal->ob_ch1);
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF2G1_CH1,
						  AR_AN_RF2G1_CH1_DB,
						  AR_AN_RF2G1_CH1_DB_S,
						  pModal->db_ch1);
		} else {
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF5G1_CH0,
						  AR_AN_RF5G1_CH0_OB5,
						  AR_AN_RF5G1_CH0_OB5_S,
						  pModal->ob);
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF5G1_CH0,
						  AR_AN_RF5G1_CH0_DB5,
						  AR_AN_RF5G1_CH0_DB5_S,
						  pModal->db);
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF5G1_CH1,
						  AR_AN_RF5G1_CH1_OB5,
						  AR_AN_RF5G1_CH1_OB5_S,
						  pModal->ob_ch1);
			ath9k_hw_analog_shift_rmw(ah, AR_AN_RF5G1_CH1,
						  AR_AN_RF5G1_CH1_DB5,
						  AR_AN_RF5G1_CH1_DB5_S,
						  pModal->db_ch1);
		}
		ath9k_hw_analog_shift_rmw(ah, AR_AN_TOP2,
					  AR_AN_TOP2_XPABIAS_LVL,
					  AR_AN_TOP2_XPABIAS_LVL_S,
					  pModal->xpaBiasLvl);
		ath9k_hw_analog_shift_rmw(ah, AR_AN_TOP2,
					  AR_AN_TOP2_LOCALBIAS,
					  AR_AN_TOP2_LOCALBIAS_S,
					  pModal->local_bias);
		DPRINTF(ah->ah_sc, ATH_DBG_ANY, "ForceXPAon: %d\n",
			pModal->force_xpaon);
		REG_RMW_FIELD(ah, AR_PHY_XPA_CFG, AR_PHY_FORCE_XPA_CFG,
			      pModal->force_xpaon);
	}

	REG_RMW_FIELD(ah, AR_PHY_SETTLING, AR_PHY_SETTLING_SWITCH,
		      pModal->switchSettling);
	REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ, AR_PHY_DESIRED_SZ_ADC,
		      pModal->adcDesiredSize);

	if (!AR_SREV_9280_10_OR_LATER(ah))
		REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ,
			      AR_PHY_DESIRED_SZ_PGA,
			      pModal->pgaDesiredSize);

	REG_WRITE(ah, AR_PHY_RF_CTL4,
		  SM(pModal->txEndToXpaOff, AR_PHY_RF_CTL4_TX_END_XPAA_OFF)
		  | SM(pModal->txEndToXpaOff,
		       AR_PHY_RF_CTL4_TX_END_XPAB_OFF)
		  | SM(pModal->txFrameToXpaOn,
		       AR_PHY_RF_CTL4_FRAME_XPAA_ON)
		  | SM(pModal->txFrameToXpaOn,
		       AR_PHY_RF_CTL4_FRAME_XPAB_ON));

	REG_RMW_FIELD(ah, AR_PHY_RF_CTL3, AR_PHY_TX_END_TO_A2_RX_ON,
		      pModal->txEndToRxOn);
	if (AR_SREV_9280_10_OR_LATER(ah)) {
		REG_RMW_FIELD(ah, AR_PHY_CCA, AR9280_PHY_CCA_THRESH62,
			      pModal->thresh62);
		REG_RMW_FIELD(ah, AR_PHY_EXT_CCA0,
			      AR_PHY_EXT_CCA0_THRESH62,
			      pModal->thresh62);
	} else {
		REG_RMW_FIELD(ah, AR_PHY_CCA, AR_PHY_CCA_THRESH62,
			      pModal->thresh62);
		REG_RMW_FIELD(ah, AR_PHY_EXT_CCA,
			      AR_PHY_EXT_CCA_THRESH62,
			      pModal->thresh62);
	}

	if ((eep->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_2) {
		REG_RMW_FIELD(ah, AR_PHY_RF_CTL2,
			      AR_PHY_TX_END_DATA_START,
			      pModal->txFrameToDataStart);
		REG_RMW_FIELD(ah, AR_PHY_RF_CTL2, AR_PHY_TX_END_PA_ON,
			      pModal->txFrameToPaOn);
	}

	if ((eep->baseEepHeader.version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_3) {
		if (IS_CHAN_HT40(chan))
			REG_RMW_FIELD(ah, AR_PHY_SETTLING,
				      AR_PHY_SETTLING_SWITCH,
				      pModal->swSettleHt40);
	}

	return true;
}

static inline int ath9k_hw_check_eeprom(struct ath_hal *ah)
{
	u32 sum = 0, el;
	u16 *eepdata;
	int i;
	struct ath_hal_5416 *ahp = AH5416(ah);
	bool need_swap = false;
	struct ar5416_eeprom *eep =
		(struct ar5416_eeprom *) &ahp->ah_eeprom;

	if (!ath9k_hw_use_flash(ah)) {
		u16 magic, magic2;
		int addr;

		if (!ath9k_hw_nvram_read(ah, AR5416_EEPROM_MAGIC_OFFSET,
					&magic)) {
			DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
				 "%s: Reading Magic # failed\n", __func__);
			return false;
		}
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM, "%s: Read Magic = 0x%04X\n",
			 __func__, magic);

		if (magic != AR5416_EEPROM_MAGIC) {
			magic2 = swab16(magic);

			if (magic2 == AR5416_EEPROM_MAGIC) {
				need_swap = true;
				eepdata = (u16 *) (&ahp->ah_eeprom);

				for (addr = 0;
				     addr <
					     sizeof(struct ar5416_eeprom) /
					     sizeof(u16); addr++) {
					u16 temp;

					temp = swab16(*eepdata);
					*eepdata = temp;
					eepdata++;

					DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
						 "0x%04X  ", *eepdata);
					if (((addr + 1) % 6) == 0)
						DPRINTF(ah->ah_sc,
							 ATH_DBG_EEPROM,
							 "\n");
				}
			} else {
				DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
					 "Invalid EEPROM Magic. "
					"endianness missmatch.\n");
				return -EINVAL;
			}
		}
	}
	DPRINTF(ah->ah_sc, ATH_DBG_EEPROM, "need_swap = %s.\n",
		 need_swap ? "True" : "False");

	if (need_swap)
		el = swab16(ahp->ah_eeprom.baseEepHeader.length);
	else
		el = ahp->ah_eeprom.baseEepHeader.length;

	if (el > sizeof(struct ar5416_eeprom))
		el = sizeof(struct ar5416_eeprom) / sizeof(u16);
	else
		el = el / sizeof(u16);

	eepdata = (u16 *) (&ahp->ah_eeprom);

	for (i = 0; i < el; i++)
		sum ^= *eepdata++;

	if (need_swap) {
		u32 integer, j;
		u16 word;

		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			 "EEPROM Endianness is not native.. Changing \n");

		word = swab16(eep->baseEepHeader.length);
		eep->baseEepHeader.length = word;

		word = swab16(eep->baseEepHeader.checksum);
		eep->baseEepHeader.checksum = word;

		word = swab16(eep->baseEepHeader.version);
		eep->baseEepHeader.version = word;

		word = swab16(eep->baseEepHeader.regDmn[0]);
		eep->baseEepHeader.regDmn[0] = word;

		word = swab16(eep->baseEepHeader.regDmn[1]);
		eep->baseEepHeader.regDmn[1] = word;

		word = swab16(eep->baseEepHeader.rfSilent);
		eep->baseEepHeader.rfSilent = word;

		word = swab16(eep->baseEepHeader.blueToothOptions);
		eep->baseEepHeader.blueToothOptions = word;

		word = swab16(eep->baseEepHeader.deviceCap);
		eep->baseEepHeader.deviceCap = word;

		for (j = 0; j < ARRAY_SIZE(eep->modalHeader); j++) {
			struct modal_eep_header *pModal =
				&eep->modalHeader[j];
			integer = swab32(pModal->antCtrlCommon);
			pModal->antCtrlCommon = integer;

			for (i = 0; i < AR5416_MAX_CHAINS; i++) {
				integer = swab32(pModal->antCtrlChain[i]);
				pModal->antCtrlChain[i] = integer;
			}

			for (i = 0; i < AR5416_EEPROM_MODAL_SPURS; i++) {
				word = swab16(pModal->spurChans[i].spurChan);
				pModal->spurChans[i].spurChan = word;
			}
		}
	}

	if (sum != 0xffff || ar5416_get_eep_ver(ahp) != AR5416_EEP_VER ||
	    ar5416_get_eep_rev(ahp) < AR5416_EEP_NO_BACK_VER) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			 "Bad EEPROM checksum 0x%x or revision 0x%04x\n",
			 sum, ar5416_get_eep_ver(ahp));
		return -EINVAL;
	}

	return 0;
}

static bool ath9k_hw_chip_test(struct ath_hal *ah)
{
	u32 regAddr[2] = { AR_STA_ID0, AR_PHY_BASE + (8 << 2) };
	u32 regHold[2];
	u32 patternData[4] = { 0x55555555,
				     0xaaaaaaaa,
				     0x66666666,
				     0x99999999 };
	int i, j;

	for (i = 0; i < 2; i++) {
		u32 addr = regAddr[i];
		u32 wrData, rdData;

		regHold[i] = REG_READ(ah, addr);
		for (j = 0; j < 0x100; j++) {
			wrData = (j << 16) | j;
			REG_WRITE(ah, addr, wrData);
			rdData = REG_READ(ah, addr);
			if (rdData != wrData) {
				DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
				 "%s: address test failed "
				"addr: 0x%08x - wr:0x%08x != rd:0x%08x\n",
				 __func__, addr, wrData, rdData);
				return false;
			}
		}
		for (j = 0; j < 4; j++) {
			wrData = patternData[j];
			REG_WRITE(ah, addr, wrData);
			rdData = REG_READ(ah, addr);
			if (wrData != rdData) {
				DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
				 "%s: address test failed "
				"addr: 0x%08x - wr:0x%08x != rd:0x%08x\n",
				 __func__, addr, wrData, rdData);
				return false;
			}
		}
		REG_WRITE(ah, regAddr[i], regHold[i]);
	}
	udelay(100);
	return true;
}

u32 ath9k_hw_getrxfilter(struct ath_hal *ah)
{
	u32 bits = REG_READ(ah, AR_RX_FILTER);
	u32 phybits = REG_READ(ah, AR_PHY_ERR);

	if (phybits & AR_PHY_ERR_RADAR)
		bits |= ATH9K_RX_FILTER_PHYRADAR;
	if (phybits & (AR_PHY_ERR_OFDM_TIMING | AR_PHY_ERR_CCK_TIMING))
		bits |= ATH9K_RX_FILTER_PHYERR;
	return bits;
}

void ath9k_hw_setrxfilter(struct ath_hal *ah, u32 bits)
{
	u32 phybits;

	REG_WRITE(ah, AR_RX_FILTER, (bits & 0xffff) | AR_RX_COMPR_BAR);
	phybits = 0;
	if (bits & ATH9K_RX_FILTER_PHYRADAR)
		phybits |= AR_PHY_ERR_RADAR;
	if (bits & ATH9K_RX_FILTER_PHYERR)
		phybits |= AR_PHY_ERR_OFDM_TIMING | AR_PHY_ERR_CCK_TIMING;
	REG_WRITE(ah, AR_PHY_ERR, phybits);

	if (phybits)
		REG_WRITE(ah, AR_RXCFG,
			  REG_READ(ah, AR_RXCFG) | AR_RXCFG_ZLFDMA);
	else
		REG_WRITE(ah, AR_RXCFG,
			  REG_READ(ah, AR_RXCFG) & ~AR_RXCFG_ZLFDMA);
}

bool ath9k_hw_setcapability(struct ath_hal *ah,
			    enum ath9k_capability_type type,
			    u32 capability,
			    u32 setting,
			    int *status)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	u32 v;

	switch (type) {
	case ATH9K_CAP_TKIP_MIC:
		if (setting)
			ahp->ah_staId1Defaults |=
				AR_STA_ID1_CRPT_MIC_ENABLE;
		else
			ahp->ah_staId1Defaults &=
				~AR_STA_ID1_CRPT_MIC_ENABLE;
		return true;
	case ATH9K_CAP_DIVERSITY:
		v = REG_READ(ah, AR_PHY_CCK_DETECT);
		if (setting)
			v |= AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV;
		else
			v &= ~AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV;
		REG_WRITE(ah, AR_PHY_CCK_DETECT, v);
		return true;
	case ATH9K_CAP_MCAST_KEYSRCH:
		if (setting)
			ahp->ah_staId1Defaults |= AR_STA_ID1_MCAST_KSRCH;
		else
			ahp->ah_staId1Defaults &= ~AR_STA_ID1_MCAST_KSRCH;
		return true;
	case ATH9K_CAP_TSF_ADJUST:
		if (setting)
			ahp->ah_miscMode |= AR_PCU_TX_ADD_TSF;
		else
			ahp->ah_miscMode &= ~AR_PCU_TX_ADD_TSF;
		return true;
	default:
		return false;
	}
}

void ath9k_hw_dmaRegDump(struct ath_hal *ah)
{
	u32 val[ATH9K_NUM_DMA_DEBUG_REGS];
	int qcuOffset = 0, dcuOffset = 0;
	u32 *qcuBase = &val[0], *dcuBase = &val[4];
	int i;

	REG_WRITE(ah, AR_MACMISC,
		  ((AR_MACMISC_DMA_OBS_LINE_8 << AR_MACMISC_DMA_OBS_S) |
		   (AR_MACMISC_MISC_OBS_BUS_1 <<
		    AR_MACMISC_MISC_OBS_BUS_MSB_S)));

	DPRINTF(ah->ah_sc, ATH_DBG_REG_IO, "Raw DMA Debug values:\n");
	for (i = 0; i < ATH9K_NUM_DMA_DEBUG_REGS; i++) {
		if (i % 4 == 0)
			DPRINTF(ah->ah_sc, ATH_DBG_REG_IO, "\n");

		val[i] = REG_READ(ah, AR_DMADBG_0 + (i * sizeof(u32)));
		DPRINTF(ah->ah_sc, ATH_DBG_REG_IO, "%d: %08x ", i, val[i]);
	}

	DPRINTF(ah->ah_sc, ATH_DBG_REG_IO, "\n\n");
	DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
		 "Num QCU: chain_st fsp_ok fsp_st DCU: chain_st\n");

	for (i = 0; i < ATH9K_NUM_QUEUES;
	     i++, qcuOffset += 4, dcuOffset += 5) {
		if (i == 8) {
			qcuOffset = 0;
			qcuBase++;
		}

		if (i == 6) {
			dcuOffset = 0;
			dcuBase++;
		}

		DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
			 "%2d          %2x      %1x     %2x           %2x\n",
			 i, (*qcuBase & (0x7 << qcuOffset)) >> qcuOffset,
			 (*qcuBase & (0x8 << qcuOffset)) >> (qcuOffset +
							     3),
			 val[2] & (0x7 << (i * 3)) >> (i * 3),
			 (*dcuBase & (0x1f << dcuOffset)) >> dcuOffset);
	}

	DPRINTF(ah->ah_sc, ATH_DBG_REG_IO, "\n");
	DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
		 "qcu_stitch state:   %2x    qcu_fetch state:        %2x\n",
		 (val[3] & 0x003c0000) >> 18, (val[3] & 0x03c00000) >> 22);
	DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
		 "qcu_complete state: %2x    dcu_complete state:     %2x\n",
		 (val[3] & 0x1c000000) >> 26, (val[6] & 0x3));
	DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
		 "dcu_arb state:      %2x    dcu_fp state:           %2x\n",
		 (val[5] & 0x06000000) >> 25, (val[5] & 0x38000000) >> 27);
	DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
		 "chan_idle_dur:     %3d    chan_idle_dur_valid:     %1d\n",
		 (val[6] & 0x000003fc) >> 2, (val[6] & 0x00000400) >> 10);
	DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
		 "txfifo_valid_0:      %1d    txfifo_valid_1:          %1d\n",
		 (val[6] & 0x00000800) >> 11, (val[6] & 0x00001000) >> 12);
	DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
		 "txfifo_dcu_num_0:   %2d    txfifo_dcu_num_1:       %2d\n",
		 (val[6] & 0x0001e000) >> 13, (val[6] & 0x001e0000) >> 17);

	DPRINTF(ah->ah_sc, ATH_DBG_REG_IO, "pcu observe 0x%x \n",
		REG_READ(ah, AR_OBS_BUS_1));
	DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
		"AR_CR 0x%x \n", REG_READ(ah, AR_CR));
}

u32 ath9k_hw_GetMibCycleCountsPct(struct ath_hal *ah,
					u32 *rxc_pcnt,
					u32 *rxf_pcnt,
					u32 *txf_pcnt)
{
	static u32 cycles, rx_clear, rx_frame, tx_frame;
	u32 good = 1;

	u32 rc = REG_READ(ah, AR_RCCNT);
	u32 rf = REG_READ(ah, AR_RFCNT);
	u32 tf = REG_READ(ah, AR_TFCNT);
	u32 cc = REG_READ(ah, AR_CCCNT);

	if (cycles == 0 || cycles > cc) {
		DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
			 "%s: cycle counter wrap. ExtBusy = 0\n",
			 __func__);
		good = 0;
	} else {
		u32 cc_d = cc - cycles;
		u32 rc_d = rc - rx_clear;
		u32 rf_d = rf - rx_frame;
		u32 tf_d = tf - tx_frame;

		if (cc_d != 0) {
			*rxc_pcnt = rc_d * 100 / cc_d;
			*rxf_pcnt = rf_d * 100 / cc_d;
			*txf_pcnt = tf_d * 100 / cc_d;
		} else {
			good = 0;
		}
	}

	cycles = cc;
	rx_frame = rf;
	rx_clear = rc;
	tx_frame = tf;

	return good;
}

void ath9k_hw_set11nmac2040(struct ath_hal *ah, enum ath9k_ht_macmode mode)
{
	u32 macmode;

	if (mode == ATH9K_HT_MACMODE_2040 &&
	    !ah->ah_config.cwm_ignore_extcca)
		macmode = AR_2040_JOINED_RX_CLEAR;
	else
		macmode = 0;

	REG_WRITE(ah, AR_2040_MODE, macmode);
}

static void ath9k_hw_mark_phy_inactive(struct ath_hal *ah)
{
	REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
}


static struct ath_hal_5416 *ath9k_hw_newstate(u16 devid,
					      struct ath_softc *sc,
					      void __iomem *mem,
					      int *status)
{
	static const u8 defbssidmask[ETH_ALEN] =
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	struct ath_hal_5416 *ahp;
	struct ath_hal *ah;

	ahp = kzalloc(sizeof(struct ath_hal_5416), GFP_KERNEL);
	if (ahp == NULL) {
		DPRINTF(sc, ATH_DBG_FATAL,
			 "%s: cannot allocate memory for state block\n",
			 __func__);
		*status = -ENOMEM;
		return NULL;
	}

	ah = &ahp->ah;

	memcpy(&ahp->ah, &ar5416hal, sizeof(struct ath_hal));

	ah->ah_sc = sc;
	ah->ah_sh = mem;

	ah->ah_devid = devid;
	ah->ah_subvendorid = 0;

	ah->ah_flags = 0;
	if ((devid == AR5416_AR9100_DEVID))
		ah->ah_macVersion = AR_SREV_VERSION_9100;
	if (!AR_SREV_9100(ah))
		ah->ah_flags = AH_USE_EEPROM;

	ah->ah_powerLimit = MAX_RATE_POWER;
	ah->ah_tpScale = ATH9K_TP_SCALE_MAX;

	ahp->ah_atimWindow = 0;
	ahp->ah_diversityControl = ah->ah_config.diversity_control;
	ahp->ah_antennaSwitchSwap =
		ah->ah_config.antenna_switch_swap;

	ahp->ah_staId1Defaults = AR_STA_ID1_CRPT_MIC_ENABLE;
	ahp->ah_beaconInterval = 100;
	ahp->ah_enable32kHzClock = DONT_USE_32KHZ;
	ahp->ah_slottime = (u32) -1;
	ahp->ah_acktimeout = (u32) -1;
	ahp->ah_ctstimeout = (u32) -1;
	ahp->ah_globaltxtimeout = (u32) -1;
	memcpy(&ahp->ah_bssidmask, defbssidmask, ETH_ALEN);

	ahp->ah_gBeaconRate = 0;

	return ahp;
}

static int ath9k_hw_eeprom_attach(struct ath_hal *ah)
{
	int status;

	if (ath9k_hw_use_flash(ah))
		ath9k_hw_flash_map(ah);

	if (!ath9k_hw_fill_eeprom(ah))
		return -EIO;

	status = ath9k_hw_check_eeprom(ah);

	return status;
}

u32 ath9k_hw_get_eeprom(struct ath_hal_5416 *ahp,
			      enum eeprom_param param)
{
	struct ar5416_eeprom *eep = &ahp->ah_eeprom;
	struct modal_eep_header *pModal = eep->modalHeader;
	struct base_eep_header *pBase = &eep->baseEepHeader;

	switch (param) {
	case EEP_NFTHRESH_5:
		return -pModal[0].noiseFloorThreshCh[0];
	case EEP_NFTHRESH_2:
		return -pModal[1].noiseFloorThreshCh[0];
	case AR_EEPROM_MAC(0):
		return pBase->macAddr[0] << 8 | pBase->macAddr[1];
	case AR_EEPROM_MAC(1):
		return pBase->macAddr[2] << 8 | pBase->macAddr[3];
	case AR_EEPROM_MAC(2):
		return pBase->macAddr[4] << 8 | pBase->macAddr[5];
	case EEP_REG_0:
		return pBase->regDmn[0];
	case EEP_REG_1:
		return pBase->regDmn[1];
	case EEP_OP_CAP:
		return pBase->deviceCap;
	case EEP_OP_MODE:
		return pBase->opCapFlags;
	case EEP_RF_SILENT:
		return pBase->rfSilent;
	case EEP_OB_5:
		return pModal[0].ob;
	case EEP_DB_5:
		return pModal[0].db;
	case EEP_OB_2:
		return pModal[1].ob;
	case EEP_DB_2:
		return pModal[1].db;
	case EEP_MINOR_REV:
		return pBase->version & AR5416_EEP_VER_MINOR_MASK;
	case EEP_TX_MASK:
		return pBase->txMask;
	case EEP_RX_MASK:
		return pBase->rxMask;
	default:
		return 0;
	}
}

static inline int ath9k_hw_get_radiorev(struct ath_hal *ah)
{
	u32 val;
	int i;

	REG_WRITE(ah, AR_PHY(0x36), 0x00007058);
	for (i = 0; i < 8; i++)
		REG_WRITE(ah, AR_PHY(0x20), 0x00010000);
	val = (REG_READ(ah, AR_PHY(256)) >> 24) & 0xff;
	val = ((val & 0xf0) >> 4) | ((val & 0x0f) << 4);
	return ath9k_hw_reverse_bits(val, 8);
}

static inline int ath9k_hw_init_macaddr(struct ath_hal *ah)
{
	u32 sum;
	int i;
	u16 eeval;
	struct ath_hal_5416 *ahp = AH5416(ah);
	DECLARE_MAC_BUF(mac);

	sum = 0;
	for (i = 0; i < 3; i++) {
		eeval = ath9k_hw_get_eeprom(ahp, AR_EEPROM_MAC(i));
		sum += eeval;
		ahp->ah_macaddr[2 * i] = eeval >> 8;
		ahp->ah_macaddr[2 * i + 1] = eeval & 0xff;
	}
	if (sum == 0 || sum == 0xffff * 3) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			 "%s: mac address read failed: %s\n", __func__,
			 print_mac(mac, ahp->ah_macaddr));
		return -EADDRNOTAVAIL;
	}

	return 0;
}

static inline int16_t ath9k_hw_interpolate(u16 target,
					   u16 srcLeft,
					   u16 srcRight,
					   int16_t targetLeft,
					   int16_t targetRight)
{
	int16_t rv;

	if (srcRight == srcLeft) {
		rv = targetLeft;
	} else {
		rv = (int16_t) (((target - srcLeft) * targetRight +
				 (srcRight - target) * targetLeft) /
				(srcRight - srcLeft));
	}
	return rv;
}

static inline u16 ath9k_hw_fbin2freq(u8 fbin,
					   bool is2GHz)
{

	if (fbin == AR5416_BCHAN_UNUSED)
		return fbin;

	return (u16) ((is2GHz) ? (2300 + fbin) : (4800 + 5 * fbin));
}

static u16 ath9k_hw_eeprom_get_spur_chan(struct ath_hal *ah,
					       u16 i,
					       bool is2GHz)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *eep =
		(struct ar5416_eeprom *) &ahp->ah_eeprom;
	u16 spur_val = AR_NO_SPUR;

	DPRINTF(ah->ah_sc, ATH_DBG_ANI,
		 "Getting spur idx %d is2Ghz. %d val %x\n",
		 i, is2GHz, ah->ah_config.spurchans[i][is2GHz]);

	switch (ah->ah_config.spurmode) {
	case SPUR_DISABLE:
		break;
	case SPUR_ENABLE_IOCTL:
		spur_val = ah->ah_config.spurchans[i][is2GHz];
		DPRINTF(ah->ah_sc, ATH_DBG_ANI,
			 "Getting spur val from new loc. %d\n", spur_val);
		break;
	case SPUR_ENABLE_EEPROM:
		spur_val = eep->modalHeader[is2GHz].spurChans[i].spurChan;
		break;

	}
	return spur_val;
}

static inline int ath9k_hw_rfattach(struct ath_hal *ah)
{
	bool rfStatus = false;
	int ecode = 0;

	rfStatus = ath9k_hw_init_rf(ah, &ecode);
	if (!rfStatus) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET,
			 "%s: RF setup failed, status %u\n", __func__,
			 ecode);
		return ecode;
	}

	return 0;
}

static int ath9k_hw_rf_claim(struct ath_hal *ah)
{
	u32 val;

	REG_WRITE(ah, AR_PHY(0), 0x00000007);

	val = ath9k_hw_get_radiorev(ah);
	switch (val & AR_RADIO_SREV_MAJOR) {
	case 0:
		val = AR_RAD5133_SREV_MAJOR;
		break;
	case AR_RAD5133_SREV_MAJOR:
	case AR_RAD5122_SREV_MAJOR:
	case AR_RAD2133_SREV_MAJOR:
	case AR_RAD2122_SREV_MAJOR:
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
			 "%s: 5G Radio Chip Rev 0x%02X is not "
			"supported by this driver\n",
			 __func__, ah->ah_analog5GhzRev);
		return -EOPNOTSUPP;
	}

	ah->ah_analog5GhzRev = val;

	return 0;
}

static inline void ath9k_hw_init_pll(struct ath_hal *ah,
				     struct ath9k_channel *chan)
{
	u32 pll;

	if (AR_SREV_9100(ah)) {
		if (chan && IS_CHAN_5GHZ(chan))
			pll = 0x1450;
		else
			pll = 0x1458;
	} else {
		if (AR_SREV_9280_10_OR_LATER(ah)) {
			pll = SM(0x5, AR_RTC_9160_PLL_REFDIV);

			if (chan && IS_CHAN_HALF_RATE(chan))
				pll |= SM(0x1, AR_RTC_9160_PLL_CLKSEL);
			else if (chan && IS_CHAN_QUARTER_RATE(chan))
				pll |= SM(0x2, AR_RTC_9160_PLL_CLKSEL);

			if (chan && IS_CHAN_5GHZ(chan)) {
				pll |= SM(0x28, AR_RTC_9160_PLL_DIV);


				if (AR_SREV_9280_20(ah)) {
					if (((chan->channel % 20) == 0)
					    || ((chan->channel % 10) == 0))
						pll = 0x2850;
					else
						pll = 0x142c;
				}
			} else {
				pll |= SM(0x2c, AR_RTC_9160_PLL_DIV);
			}

		} else if (AR_SREV_9160_10_OR_LATER(ah)) {

			pll = SM(0x5, AR_RTC_9160_PLL_REFDIV);

			if (chan && IS_CHAN_HALF_RATE(chan))
				pll |= SM(0x1, AR_RTC_9160_PLL_CLKSEL);
			else if (chan && IS_CHAN_QUARTER_RATE(chan))
				pll |= SM(0x2, AR_RTC_9160_PLL_CLKSEL);

			if (chan && IS_CHAN_5GHZ(chan))
				pll |= SM(0x50, AR_RTC_9160_PLL_DIV);
			else
				pll |= SM(0x58, AR_RTC_9160_PLL_DIV);
		} else {
			pll = AR_RTC_PLL_REFDIV_5 | AR_RTC_PLL_DIV2;

			if (chan && IS_CHAN_HALF_RATE(chan))
				pll |= SM(0x1, AR_RTC_PLL_CLKSEL);
			else if (chan && IS_CHAN_QUARTER_RATE(chan))
				pll |= SM(0x2, AR_RTC_PLL_CLKSEL);

			if (chan && IS_CHAN_5GHZ(chan))
				pll |= SM(0xa, AR_RTC_PLL_DIV);
			else
				pll |= SM(0xb, AR_RTC_PLL_DIV);
		}
	}
	REG_WRITE(ah, (u16) (AR_RTC_PLL_CONTROL), pll);

	udelay(RTC_PLL_SETTLE_DELAY);

	REG_WRITE(ah, AR_RTC_SLEEP_CLK, AR_RTC_FORCE_DERIVED_CLK);
}

static void ath9k_hw_set_regs(struct ath_hal *ah, struct ath9k_channel *chan,
			      enum ath9k_ht_macmode macmode)
{
	u32 phymode;
	struct ath_hal_5416 *ahp = AH5416(ah);

	phymode = AR_PHY_FC_HT_EN | AR_PHY_FC_SHORT_GI_40
		| AR_PHY_FC_SINGLE_HT_LTF1 | AR_PHY_FC_WALSH;

	if (IS_CHAN_HT40(chan)) {
		phymode |= AR_PHY_FC_DYN2040_EN;

		if ((chan->chanmode == CHANNEL_A_HT40PLUS) ||
		    (chan->chanmode == CHANNEL_G_HT40PLUS))
			phymode |= AR_PHY_FC_DYN2040_PRI_CH;

		if (ahp->ah_extprotspacing == ATH9K_HT_EXTPROTSPACING_25)
			phymode |= AR_PHY_FC_DYN2040_EXT_CH;
	}
	REG_WRITE(ah, AR_PHY_TURBO, phymode);

	ath9k_hw_set11nmac2040(ah, macmode);

	REG_WRITE(ah, AR_GTXTO, 25 << AR_GTXTO_TIMEOUT_LIMIT_S);
	REG_WRITE(ah, AR_CST, 0xF << AR_CST_TIMEOUT_LIMIT_S);
}

static void ath9k_hw_set_operating_mode(struct ath_hal *ah, int opmode)
{
	u32 val;

	val = REG_READ(ah, AR_STA_ID1);
	val &= ~(AR_STA_ID1_STA_AP | AR_STA_ID1_ADHOC);
	switch (opmode) {
	case ATH9K_M_HOSTAP:
		REG_WRITE(ah, AR_STA_ID1, val | AR_STA_ID1_STA_AP
			  | AR_STA_ID1_KSRCH_MODE);
		REG_CLR_BIT(ah, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
		break;
	case ATH9K_M_IBSS:
		REG_WRITE(ah, AR_STA_ID1, val | AR_STA_ID1_ADHOC
			  | AR_STA_ID1_KSRCH_MODE);
		REG_SET_BIT(ah, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
		break;
	case ATH9K_M_STA:
	case ATH9K_M_MONITOR:
		REG_WRITE(ah, AR_STA_ID1, val | AR_STA_ID1_KSRCH_MODE);
		break;
	}
}

static inline void
ath9k_hw_set_rfmode(struct ath_hal *ah, struct ath9k_channel *chan)
{
	u32 rfMode = 0;

	if (chan == NULL)
		return;

	rfMode |= (IS_CHAN_B(chan) || IS_CHAN_G(chan))
		? AR_PHY_MODE_DYNAMIC : AR_PHY_MODE_OFDM;

	if (!AR_SREV_9280_10_OR_LATER(ah))
		rfMode |= (IS_CHAN_5GHZ(chan)) ? AR_PHY_MODE_RF5GHZ :
			AR_PHY_MODE_RF2GHZ;

	if (AR_SREV_9280_20(ah) && IS_CHAN_A_5MHZ_SPACED(chan))
		rfMode |= (AR_PHY_MODE_DYNAMIC | AR_PHY_MODE_DYN_CCK_DISABLE);

	REG_WRITE(ah, AR_PHY_MODE, rfMode);
}

static bool ath9k_hw_set_reset(struct ath_hal *ah, int type)
{
	u32 rst_flags;
	u32 tmpReg;

	REG_WRITE(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN |
		  AR_RTC_FORCE_WAKE_ON_INT);

	if (AR_SREV_9100(ah)) {
		rst_flags = AR_RTC_RC_MAC_WARM | AR_RTC_RC_MAC_COLD |
			AR_RTC_RC_COLD_RESET | AR_RTC_RC_WARM_RESET;
	} else {
		tmpReg = REG_READ(ah, AR_INTR_SYNC_CAUSE);
		if (tmpReg &
		    (AR_INTR_SYNC_LOCAL_TIMEOUT |
		     AR_INTR_SYNC_RADM_CPL_TIMEOUT)) {
			REG_WRITE(ah, AR_INTR_SYNC_ENABLE, 0);
			REG_WRITE(ah, AR_RC, AR_RC_AHB | AR_RC_HOSTIF);
		} else {
			REG_WRITE(ah, AR_RC, AR_RC_AHB);
		}

		rst_flags = AR_RTC_RC_MAC_WARM;
		if (type == ATH9K_RESET_COLD)
			rst_flags |= AR_RTC_RC_MAC_COLD;
	}

	REG_WRITE(ah, (u16) (AR_RTC_RC), rst_flags);
	udelay(50);

	REG_WRITE(ah, (u16) (AR_RTC_RC), 0);
	if (!ath9k_hw_wait(ah, (u16) (AR_RTC_RC), AR_RTC_RC_M, 0)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET,
			"%s: RTC stuck in MAC reset\n",
			__func__);
		return false;
	}

	if (!AR_SREV_9100(ah))
		REG_WRITE(ah, AR_RC, 0);

	ath9k_hw_init_pll(ah, NULL);

	if (AR_SREV_9100(ah))
		udelay(50);

	return true;
}

static inline bool ath9k_hw_set_reset_power_on(struct ath_hal *ah)
{
	REG_WRITE(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN |
		  AR_RTC_FORCE_WAKE_ON_INT);

	REG_WRITE(ah, (u16) (AR_RTC_RESET), 0);
	REG_WRITE(ah, (u16) (AR_RTC_RESET), 1);

	if (!ath9k_hw_wait(ah,
			   AR_RTC_STATUS,
			   AR_RTC_STATUS_M,
			   AR_RTC_STATUS_ON)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "%s: RTC not waking up\n",
			 __func__);
		return false;
	}

	ath9k_hw_read_revisions(ah);

	return ath9k_hw_set_reset(ah, ATH9K_RESET_WARM);
}

static bool ath9k_hw_set_reset_reg(struct ath_hal *ah,
				   u32 type)
{
	REG_WRITE(ah, AR_RTC_FORCE_WAKE,
		  AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);

	switch (type) {
	case ATH9K_RESET_POWER_ON:
		return ath9k_hw_set_reset_power_on(ah);
		break;
	case ATH9K_RESET_WARM:
	case ATH9K_RESET_COLD:
		return ath9k_hw_set_reset(ah, type);
		break;
	default:
		return false;
	}
}

static inline
struct ath9k_channel *ath9k_hw_check_chan(struct ath_hal *ah,
					  struct ath9k_channel *chan)
{
	if (!(IS_CHAN_2GHZ(chan) ^ IS_CHAN_5GHZ(chan))) {
		DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
			 "%s: invalid channel %u/0x%x; not marked as "
			 "2GHz or 5GHz\n", __func__, chan->channel,
			 chan->channelFlags);
		return NULL;
	}

	if (!IS_CHAN_OFDM(chan) &&
	      !IS_CHAN_CCK(chan) &&
	      !IS_CHAN_HT20(chan) &&
	      !IS_CHAN_HT40(chan)) {
		DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
			"%s: invalid channel %u/0x%x; not marked as "
			"OFDM or CCK or HT20 or HT40PLUS or HT40MINUS\n",
			__func__, chan->channel, chan->channelFlags);
		return NULL;
	}

	return ath9k_regd_check_channel(ah, chan);
}

static inline bool
ath9k_hw_get_lower_upper_index(u8 target,
			       u8 *pList,
			       u16 listSize,
			       u16 *indexL,
			       u16 *indexR)
{
	u16 i;

	if (target <= pList[0]) {
		*indexL = *indexR = 0;
		return true;
	}
	if (target >= pList[listSize - 1]) {
		*indexL = *indexR = (u16) (listSize - 1);
		return true;
	}

	for (i = 0; i < listSize - 1; i++) {
		if (pList[i] == target) {
			*indexL = *indexR = i;
			return true;
		}
		if (target < pList[i + 1]) {
			*indexL = i;
			*indexR = (u16) (i + 1);
			return false;
		}
	}
	return false;
}

static int16_t ath9k_hw_get_nf_hist_mid(int16_t *nfCalBuffer)
{
	int16_t nfval;
	int16_t sort[ATH9K_NF_CAL_HIST_MAX];
	int i, j;

	for (i = 0; i < ATH9K_NF_CAL_HIST_MAX; i++)
		sort[i] = nfCalBuffer[i];

	for (i = 0; i < ATH9K_NF_CAL_HIST_MAX - 1; i++) {
		for (j = 1; j < ATH9K_NF_CAL_HIST_MAX - i; j++) {
			if (sort[j] > sort[j - 1]) {
				nfval = sort[j];
				sort[j] = sort[j - 1];
				sort[j - 1] = nfval;
			}
		}
	}
	nfval = sort[(ATH9K_NF_CAL_HIST_MAX - 1) >> 1];

	return nfval;
}

static void ath9k_hw_update_nfcal_hist_buffer(struct ath9k_nfcal_hist *h,
					      int16_t *nfarray)
{
	int i;

	for (i = 0; i < NUM_NF_READINGS; i++) {
		h[i].nfCalBuffer[h[i].currIndex] = nfarray[i];

		if (++h[i].currIndex >= ATH9K_NF_CAL_HIST_MAX)
			h[i].currIndex = 0;

		if (h[i].invalidNFcount > 0) {
			if (nfarray[i] < AR_PHY_CCA_MIN_BAD_VALUE
			    || nfarray[i] > AR_PHY_CCA_MAX_HIGH_VALUE) {
				h[i].invalidNFcount = ATH9K_NF_CAL_HIST_MAX;
			} else {
				h[i].invalidNFcount--;
				h[i].privNF = nfarray[i];
			}
		} else {
			h[i].privNF =
				ath9k_hw_get_nf_hist_mid(h[i].nfCalBuffer);
		}
	}
	return;
}

static void ar5416GetNoiseFloor(struct ath_hal *ah,
				int16_t nfarray[NUM_NF_READINGS])
{
	int16_t nf;

	if (AR_SREV_9280_10_OR_LATER(ah))
		nf = MS(REG_READ(ah, AR_PHY_CCA), AR9280_PHY_MINCCA_PWR);
	else
		nf = MS(REG_READ(ah, AR_PHY_CCA), AR_PHY_MINCCA_PWR);

	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
		 "NF calibrated [ctl] [chain 0] is %d\n", nf);
	nfarray[0] = nf;

	if (AR_SREV_9280_10_OR_LATER(ah))
		nf = MS(REG_READ(ah, AR_PHY_CH1_CCA),
			AR9280_PHY_CH1_MINCCA_PWR);
	else
		nf = MS(REG_READ(ah, AR_PHY_CH1_CCA),
			AR_PHY_CH1_MINCCA_PWR);

	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	DPRINTF(ah->ah_sc, ATH_DBG_NF_CAL,
		 "NF calibrated [ctl] [chain 1] is %d\n", nf);
	nfarray[1] = nf;

	if (!AR_SREV_9280(ah)) {
		nf = MS(REG_READ(ah, AR_PHY_CH2_CCA),
			AR_PHY_CH2_MINCCA_PWR);
		if (nf & 0x100)
			nf = 0 - ((nf ^ 0x1ff) + 1);
		DPRINTF(ah->ah_sc, ATH_DBG_NF_CAL,
			 "NF calibrated [ctl] [chain 2] is %d\n", nf);
		nfarray[2] = nf;
	}

	if (AR_SREV_9280_10_OR_LATER(ah))
		nf = MS(REG_READ(ah, AR_PHY_EXT_CCA),
			AR9280_PHY_EXT_MINCCA_PWR);
	else
		nf = MS(REG_READ(ah, AR_PHY_EXT_CCA),
			AR_PHY_EXT_MINCCA_PWR);

	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	DPRINTF(ah->ah_sc, ATH_DBG_NF_CAL,
		 "NF calibrated [ext] [chain 0] is %d\n", nf);
	nfarray[3] = nf;

	if (AR_SREV_9280_10_OR_LATER(ah))
		nf = MS(REG_READ(ah, AR_PHY_CH1_EXT_CCA),
			AR9280_PHY_CH1_EXT_MINCCA_PWR);
	else
		nf = MS(REG_READ(ah, AR_PHY_CH1_EXT_CCA),
			AR_PHY_CH1_EXT_MINCCA_PWR);

	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
		 "NF calibrated [ext] [chain 1] is %d\n", nf);
	nfarray[4] = nf;

	if (!AR_SREV_9280(ah)) {
		nf = MS(REG_READ(ah, AR_PHY_CH2_EXT_CCA),
			AR_PHY_CH2_EXT_MINCCA_PWR);
		if (nf & 0x100)
			nf = 0 - ((nf ^ 0x1ff) + 1);
		DPRINTF(ah->ah_sc, ATH_DBG_NF_CAL,
			 "NF calibrated [ext] [chain 2] is %d\n", nf);
		nfarray[5] = nf;
	}
}

static bool
getNoiseFloorThresh(struct ath_hal *ah,
		    const struct ath9k_channel *chan,
		    int16_t *nft)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	switch (chan->chanmode) {
	case CHANNEL_A:
	case CHANNEL_A_HT20:
	case CHANNEL_A_HT40PLUS:
	case CHANNEL_A_HT40MINUS:
		*nft = (int16_t) ath9k_hw_get_eeprom(ahp, EEP_NFTHRESH_5);
		break;
	case CHANNEL_B:
	case CHANNEL_G:
	case CHANNEL_G_HT20:
	case CHANNEL_G_HT40PLUS:
	case CHANNEL_G_HT40MINUS:
		*nft = (int16_t) ath9k_hw_get_eeprom(ahp, EEP_NFTHRESH_2);
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
			 "%s: invalid channel flags 0x%x\n", __func__,
			 chan->channelFlags);
		return false;
	}
	return true;
}

static void ath9k_hw_start_nfcal(struct ath_hal *ah)
{
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_ENABLE_NF);
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
}

static void
ath9k_hw_loadnf(struct ath_hal *ah, struct ath9k_channel *chan)
{
	struct ath9k_nfcal_hist *h;
	int i, j;
	int32_t val;
	const u32 ar5416_cca_regs[6] = {
		AR_PHY_CCA,
		AR_PHY_CH1_CCA,
		AR_PHY_CH2_CCA,
		AR_PHY_EXT_CCA,
		AR_PHY_CH1_EXT_CCA,
		AR_PHY_CH2_EXT_CCA
	};
	u8 chainmask;

	if (AR_SREV_9280(ah))
		chainmask = 0x1B;
	else
		chainmask = 0x3F;

#ifdef ATH_NF_PER_CHAN
	h = chan->nfCalHist;
#else
	h = ah->nfCalHist;
#endif

	for (i = 0; i < NUM_NF_READINGS; i++) {
		if (chainmask & (1 << i)) {
			val = REG_READ(ah, ar5416_cca_regs[i]);
			val &= 0xFFFFFE00;
			val |= (((u32) (h[i].privNF) << 1) & 0x1ff);
			REG_WRITE(ah, ar5416_cca_regs[i], val);
		}
	}

	REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_ENABLE_NF);
	REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);

	for (j = 0; j < 1000; j++) {
		if ((REG_READ(ah, AR_PHY_AGC_CONTROL) &
		     AR_PHY_AGC_CONTROL_NF) == 0)
			break;
		udelay(10);
	}

	for (i = 0; i < NUM_NF_READINGS; i++) {
		if (chainmask & (1 << i)) {
			val = REG_READ(ah, ar5416_cca_regs[i]);
			val &= 0xFFFFFE00;
			val |= (((u32) (-50) << 1) & 0x1ff);
			REG_WRITE(ah, ar5416_cca_regs[i], val);
		}
	}
}

static int16_t ath9k_hw_getnf(struct ath_hal *ah,
			      struct ath9k_channel *chan)
{
	int16_t nf, nfThresh;
	int16_t nfarray[NUM_NF_READINGS] = { 0 };
	struct ath9k_nfcal_hist *h;
	u8 chainmask;

	if (AR_SREV_9280(ah))
		chainmask = 0x1B;
	else
		chainmask = 0x3F;

	chan->channelFlags &= (~CHANNEL_CW_INT);
	if (REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) {
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "%s: NF did not complete in calibration window\n",
			 __func__);
		nf = 0;
		chan->rawNoiseFloor = nf;
		return chan->rawNoiseFloor;
	} else {
		ar5416GetNoiseFloor(ah, nfarray);
		nf = nfarray[0];
		if (getNoiseFloorThresh(ah, chan, &nfThresh)
		    && nf > nfThresh) {
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				 "%s: noise floor failed detected; "
				 "detected %d, threshold %d\n", __func__,
				 nf, nfThresh);
			chan->channelFlags |= CHANNEL_CW_INT;
		}
	}

#ifdef ATH_NF_PER_CHAN
	h = chan->nfCalHist;
#else
	h = ah->nfCalHist;
#endif

	ath9k_hw_update_nfcal_hist_buffer(h, nfarray);
	chan->rawNoiseFloor = h[0].privNF;

	return chan->rawNoiseFloor;
}

static void ath9k_hw_update_mibstats(struct ath_hal *ah,
			      struct ath9k_mib_stats *stats)
{
	stats->ackrcv_bad += REG_READ(ah, AR_ACK_FAIL);
	stats->rts_bad += REG_READ(ah, AR_RTS_FAIL);
	stats->fcs_bad += REG_READ(ah, AR_FCS_FAIL);
	stats->rts_good += REG_READ(ah, AR_RTS_OK);
	stats->beacons += REG_READ(ah, AR_BEACON_CNT);
}

static void ath9k_enable_mib_counters(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	DPRINTF(ah->ah_sc, ATH_DBG_ANI, "Enable mib counters\n");

	ath9k_hw_update_mibstats(ah, &ahp->ah_mibStats);

	REG_WRITE(ah, AR_FILT_OFDM, 0);
	REG_WRITE(ah, AR_FILT_CCK, 0);
	REG_WRITE(ah, AR_MIBC,
		  ~(AR_MIBC_COW | AR_MIBC_FMC | AR_MIBC_CMC | AR_MIBC_MCS)
		  & 0x0f);
	REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
	REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);
}

static void ath9k_hw_disable_mib_counters(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	DPRINTF(ah->ah_sc, ATH_DBG_ANI, "Disabling MIB counters\n");

	REG_WRITE(ah, AR_MIBC, AR_MIBC_FMC | AR_MIBC_CMC);

	ath9k_hw_update_mibstats(ah, &ahp->ah_mibStats);

	REG_WRITE(ah, AR_FILT_OFDM, 0);
	REG_WRITE(ah, AR_FILT_CCK, 0);
}

static int ath9k_hw_get_ani_channel_idx(struct ath_hal *ah,
					struct ath9k_channel *chan)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	for (i = 0; i < ARRAY_SIZE(ahp->ah_ani); i++) {
		if (ahp->ah_ani[i].c.channel == chan->channel)
			return i;
		if (ahp->ah_ani[i].c.channel == 0) {
			ahp->ah_ani[i].c.channel = chan->channel;
			ahp->ah_ani[i].c.channelFlags = chan->channelFlags;
			return i;
		}
	}

	DPRINTF(ah->ah_sc, ATH_DBG_ANI,
		 "No more channel states left. Using channel 0\n");
	return 0;
}

static void ath9k_hw_ani_attach(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	ahp->ah_hasHwPhyCounters = 1;

	memset(ahp->ah_ani, 0, sizeof(ahp->ah_ani));
	for (i = 0; i < ARRAY_SIZE(ahp->ah_ani); i++) {
		ahp->ah_ani[i].ofdmTrigHigh = ATH9K_ANI_OFDM_TRIG_HIGH;
		ahp->ah_ani[i].ofdmTrigLow = ATH9K_ANI_OFDM_TRIG_LOW;
		ahp->ah_ani[i].cckTrigHigh = ATH9K_ANI_CCK_TRIG_HIGH;
		ahp->ah_ani[i].cckTrigLow = ATH9K_ANI_CCK_TRIG_LOW;
		ahp->ah_ani[i].rssiThrHigh = ATH9K_ANI_RSSI_THR_HIGH;
		ahp->ah_ani[i].rssiThrLow = ATH9K_ANI_RSSI_THR_LOW;
		ahp->ah_ani[i].ofdmWeakSigDetectOff =
			!ATH9K_ANI_USE_OFDM_WEAK_SIG;
		ahp->ah_ani[i].cckWeakSigThreshold =
			ATH9K_ANI_CCK_WEAK_SIG_THR;
		ahp->ah_ani[i].spurImmunityLevel = ATH9K_ANI_SPUR_IMMUNE_LVL;
		ahp->ah_ani[i].firstepLevel = ATH9K_ANI_FIRSTEP_LVL;
		if (ahp->ah_hasHwPhyCounters) {
			ahp->ah_ani[i].ofdmPhyErrBase =
				AR_PHY_COUNTMAX - ATH9K_ANI_OFDM_TRIG_HIGH;
			ahp->ah_ani[i].cckPhyErrBase =
				AR_PHY_COUNTMAX - ATH9K_ANI_CCK_TRIG_HIGH;
		}
	}
	if (ahp->ah_hasHwPhyCounters) {
		DPRINTF(ah->ah_sc, ATH_DBG_ANI,
			"Setting OfdmErrBase = 0x%08x\n",
			ahp->ah_ani[0].ofdmPhyErrBase);
		DPRINTF(ah->ah_sc, ATH_DBG_ANI, "Setting cckErrBase = 0x%08x\n",
			ahp->ah_ani[0].cckPhyErrBase);

		REG_WRITE(ah, AR_PHY_ERR_1, ahp->ah_ani[0].ofdmPhyErrBase);
		REG_WRITE(ah, AR_PHY_ERR_2, ahp->ah_ani[0].cckPhyErrBase);
		ath9k_enable_mib_counters(ah);
	}
	ahp->ah_aniPeriod = ATH9K_ANI_PERIOD;
	if (ah->ah_config.enable_ani)
		ahp->ah_procPhyErr |= HAL_PROCESS_ANI;
}

static inline void ath9k_hw_ani_setup(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	const int totalSizeDesired[] = { -55, -55, -55, -55, -62 };
	const int coarseHigh[] = { -14, -14, -14, -14, -12 };
	const int coarseLow[] = { -64, -64, -64, -64, -70 };
	const int firpwr[] = { -78, -78, -78, -78, -80 };

	for (i = 0; i < 5; i++) {
		ahp->ah_totalSizeDesired[i] = totalSizeDesired[i];
		ahp->ah_coarseHigh[i] = coarseHigh[i];
		ahp->ah_coarseLow[i] = coarseLow[i];
		ahp->ah_firpwr[i] = firpwr[i];
	}
}

static void ath9k_hw_ani_detach(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	DPRINTF(ah->ah_sc, ATH_DBG_ANI, "Detaching Ani\n");
	if (ahp->ah_hasHwPhyCounters) {
		ath9k_hw_disable_mib_counters(ah);
		REG_WRITE(ah, AR_PHY_ERR_1, 0);
		REG_WRITE(ah, AR_PHY_ERR_2, 0);
	}
}


static bool ath9k_hw_ani_control(struct ath_hal *ah,
				 enum ath9k_ani_cmd cmd, int param)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416AniState *aniState = ahp->ah_curani;

	switch (cmd & ahp->ah_ani_function) {
	case ATH9K_ANI_NOISE_IMMUNITY_LEVEL:{
		u32 level = param;

		if (level >= ARRAY_SIZE(ahp->ah_totalSizeDesired)) {
			DPRINTF(ah->ah_sc, ATH_DBG_ANI,
				 "%s: level out of range (%u > %u)\n",
				 __func__, level,
				 (unsigned) ARRAY_SIZE(ahp->
						       ah_totalSizeDesired));
			return false;
		}

		REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ,
			      AR_PHY_DESIRED_SZ_TOT_DES,
			      ahp->ah_totalSizeDesired[level]);
		REG_RMW_FIELD(ah, AR_PHY_AGC_CTL1,
			      AR_PHY_AGC_CTL1_COARSE_LOW,
			      ahp->ah_coarseLow[level]);
		REG_RMW_FIELD(ah, AR_PHY_AGC_CTL1,
			      AR_PHY_AGC_CTL1_COARSE_HIGH,
			      ahp->ah_coarseHigh[level]);
		REG_RMW_FIELD(ah, AR_PHY_FIND_SIG,
			      AR_PHY_FIND_SIG_FIRPWR,
			      ahp->ah_firpwr[level]);

		if (level > aniState->noiseImmunityLevel)
			ahp->ah_stats.ast_ani_niup++;
		else if (level < aniState->noiseImmunityLevel)
			ahp->ah_stats.ast_ani_nidown++;
		aniState->noiseImmunityLevel = level;
		break;
	}
	case ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION:{
		const int m1ThreshLow[] = { 127, 50 };
		const int m2ThreshLow[] = { 127, 40 };
		const int m1Thresh[] = { 127, 0x4d };
		const int m2Thresh[] = { 127, 0x40 };
		const int m2CountThr[] = { 31, 16 };
		const int m2CountThrLow[] = { 63, 48 };
		u32 on = param ? 1 : 0;

		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M1_THRESH_LOW,
			      m1ThreshLow[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M2_THRESH_LOW,
			      m2ThreshLow[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M1_THRESH,
			      m1Thresh[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M2_THRESH,
			      m2Thresh[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR,
			      AR_PHY_SFCORR_M2COUNT_THR,
			      m2CountThr[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
			      AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW,
			      m2CountThrLow[on]);

		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M1_THRESH_LOW,
			      m1ThreshLow[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M2_THRESH_LOW,
			      m2ThreshLow[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M1_THRESH,
			      m1Thresh[on]);
		REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
			      AR_PHY_SFCORR_EXT_M2_THRESH,
			      m2Thresh[on]);

		if (on)
			REG_SET_BIT(ah, AR_PHY_SFCORR_LOW,
				    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
		else
			REG_CLR_BIT(ah, AR_PHY_SFCORR_LOW,
				    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);

		if (!on != aniState->ofdmWeakSigDetectOff) {
			if (on)
				ahp->ah_stats.ast_ani_ofdmon++;
			else
				ahp->ah_stats.ast_ani_ofdmoff++;
			aniState->ofdmWeakSigDetectOff = !on;
		}
		break;
	}
	case ATH9K_ANI_CCK_WEAK_SIGNAL_THR:{
		const int weakSigThrCck[] = { 8, 6 };
		u32 high = param ? 1 : 0;

		REG_RMW_FIELD(ah, AR_PHY_CCK_DETECT,
			      AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK,
			      weakSigThrCck[high]);
		if (high != aniState->cckWeakSigThreshold) {
			if (high)
				ahp->ah_stats.ast_ani_cckhigh++;
			else
				ahp->ah_stats.ast_ani_ccklow++;
			aniState->cckWeakSigThreshold = high;
		}
		break;
	}
	case ATH9K_ANI_FIRSTEP_LEVEL:{
		const int firstep[] = { 0, 4, 8 };
		u32 level = param;

		if (level >= ARRAY_SIZE(firstep)) {
			DPRINTF(ah->ah_sc, ATH_DBG_ANI,
				 "%s: level out of range (%u > %u)\n",
				 __func__, level,
				(unsigned) ARRAY_SIZE(firstep));
			return false;
		}
		REG_RMW_FIELD(ah, AR_PHY_FIND_SIG,
			      AR_PHY_FIND_SIG_FIRSTEP,
			      firstep[level]);
		if (level > aniState->firstepLevel)
			ahp->ah_stats.ast_ani_stepup++;
		else if (level < aniState->firstepLevel)
			ahp->ah_stats.ast_ani_stepdown++;
		aniState->firstepLevel = level;
		break;
	}
	case ATH9K_ANI_SPUR_IMMUNITY_LEVEL:{
		const int cycpwrThr1[] =
			{ 2, 4, 6, 8, 10, 12, 14, 16 };
		u32 level = param;

		if (level >= ARRAY_SIZE(cycpwrThr1)) {
			DPRINTF(ah->ah_sc, ATH_DBG_ANI,
				 "%s: level out of range (%u > %u)\n",
				 __func__, level,
				 (unsigned)
				ARRAY_SIZE(cycpwrThr1));
			return false;
		}
		REG_RMW_FIELD(ah, AR_PHY_TIMING5,
			      AR_PHY_TIMING5_CYCPWR_THR1,
			      cycpwrThr1[level]);
		if (level > aniState->spurImmunityLevel)
			ahp->ah_stats.ast_ani_spurup++;
		else if (level < aniState->spurImmunityLevel)
			ahp->ah_stats.ast_ani_spurdown++;
		aniState->spurImmunityLevel = level;
		break;
	}
	case ATH9K_ANI_PRESENT:
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_ANI,
			"%s: invalid cmd %u\n", __func__, cmd);
		return false;
	}

	DPRINTF(ah->ah_sc, ATH_DBG_ANI, "%s: ANI parameters:\n", __func__);
	DPRINTF(ah->ah_sc, ATH_DBG_ANI,
		"noiseImmunityLevel=%d, spurImmunityLevel=%d, "
		"ofdmWeakSigDetectOff=%d\n",
		 aniState->noiseImmunityLevel, aniState->spurImmunityLevel,
		 !aniState->ofdmWeakSigDetectOff);
	DPRINTF(ah->ah_sc, ATH_DBG_ANI,
		"cckWeakSigThreshold=%d, "
		"firstepLevel=%d, listenTime=%d\n",
		 aniState->cckWeakSigThreshold, aniState->firstepLevel,
		 aniState->listenTime);
	DPRINTF(ah->ah_sc, ATH_DBG_ANI,
		 "cycleCount=%d, ofdmPhyErrCount=%d, cckPhyErrCount=%d\n\n",
		 aniState->cycleCount, aniState->ofdmPhyErrCount,
		 aniState->cckPhyErrCount);
	return true;
}

static void ath9k_ani_restart(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416AniState *aniState;

	if (!DO_ANI(ah))
		return;

	aniState = ahp->ah_curani;

	aniState->listenTime = 0;
	if (ahp->ah_hasHwPhyCounters) {
		if (aniState->ofdmTrigHigh > AR_PHY_COUNTMAX) {
			aniState->ofdmPhyErrBase = 0;
			DPRINTF(ah->ah_sc, ATH_DBG_ANI,
				 "OFDM Trigger is too high for hw counters\n");
		} else {
			aniState->ofdmPhyErrBase =
				AR_PHY_COUNTMAX - aniState->ofdmTrigHigh;
		}
		if (aniState->cckTrigHigh > AR_PHY_COUNTMAX) {
			aniState->cckPhyErrBase = 0;
			DPRINTF(ah->ah_sc, ATH_DBG_ANI,
				 "CCK Trigger is too high for hw counters\n");
		} else {
			aniState->cckPhyErrBase =
				AR_PHY_COUNTMAX - aniState->cckTrigHigh;
		}
		DPRINTF(ah->ah_sc, ATH_DBG_ANI,
			 "%s: Writing ofdmbase=%u   cckbase=%u\n",
			 __func__, aniState->ofdmPhyErrBase,
			 aniState->cckPhyErrBase);
		REG_WRITE(ah, AR_PHY_ERR_1, aniState->ofdmPhyErrBase);
		REG_WRITE(ah, AR_PHY_ERR_2, aniState->cckPhyErrBase);
		REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
		REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

		ath9k_hw_update_mibstats(ah, &ahp->ah_mibStats);
	}
	aniState->ofdmPhyErrCount = 0;
	aniState->cckPhyErrCount = 0;
}

static void ath9k_hw_ani_ofdm_err_trigger(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_channel *chan = ah->ah_curchan;
	struct ar5416AniState *aniState;
	enum wireless_mode mode;
	int32_t rssi;

	if (!DO_ANI(ah))
		return;

	aniState = ahp->ah_curani;

	if (aniState->noiseImmunityLevel < HAL_NOISE_IMMUNE_MAX) {
		if (ath9k_hw_ani_control(ah, ATH9K_ANI_NOISE_IMMUNITY_LEVEL,
					 aniState->noiseImmunityLevel + 1)) {
			return;
		}
	}

	if (aniState->spurImmunityLevel < HAL_SPUR_IMMUNE_MAX) {
		if (ath9k_hw_ani_control(ah, ATH9K_ANI_SPUR_IMMUNITY_LEVEL,
					 aniState->spurImmunityLevel + 1)) {
			return;
		}
	}

	if (ah->ah_opmode == ATH9K_M_HOSTAP) {
		if (aniState->firstepLevel < HAL_FIRST_STEP_MAX) {
			ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
					     aniState->firstepLevel + 1);
		}
		return;
	}
	rssi = BEACON_RSSI(ahp);
	if (rssi > aniState->rssiThrHigh) {
		if (!aniState->ofdmWeakSigDetectOff) {
			if (ath9k_hw_ani_control(ah,
					 ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
					 false)) {
				ath9k_hw_ani_control(ah,
					ATH9K_ANI_SPUR_IMMUNITY_LEVEL,
					0);
				return;
			}
		}
		if (aniState->firstepLevel < HAL_FIRST_STEP_MAX) {
			ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
					     aniState->firstepLevel + 1);
			return;
		}
	} else if (rssi > aniState->rssiThrLow) {
		if (aniState->ofdmWeakSigDetectOff)
			ath9k_hw_ani_control(ah,
				     ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				     true);
		if (aniState->firstepLevel < HAL_FIRST_STEP_MAX)
			ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
					     aniState->firstepLevel + 1);
		return;
	} else {
		mode = ath9k_hw_chan2wmode(ah, chan);
		if (mode == ATH9K_MODE_11G || mode == ATH9K_MODE_11B) {
			if (!aniState->ofdmWeakSigDetectOff)
				ath9k_hw_ani_control(ah,
				     ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				     false);
			if (aniState->firstepLevel > 0)
				ath9k_hw_ani_control(ah,
						     ATH9K_ANI_FIRSTEP_LEVEL,
						     0);
			return;
		}
	}
}

static void ath9k_hw_ani_cck_err_trigger(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_channel *chan = ah->ah_curchan;
	struct ar5416AniState *aniState;
	enum wireless_mode mode;
	int32_t rssi;

	if (!DO_ANI(ah))
		return;

	aniState = ahp->ah_curani;
	if (aniState->noiseImmunityLevel < HAL_NOISE_IMMUNE_MAX) {
		if (ath9k_hw_ani_control(ah, ATH9K_ANI_NOISE_IMMUNITY_LEVEL,
					 aniState->noiseImmunityLevel + 1)) {
			return;
		}
	}
	if (ah->ah_opmode == ATH9K_M_HOSTAP) {
		if (aniState->firstepLevel < HAL_FIRST_STEP_MAX) {
			ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
					     aniState->firstepLevel + 1);
		}
		return;
	}
	rssi = BEACON_RSSI(ahp);
	if (rssi > aniState->rssiThrLow) {
		if (aniState->firstepLevel < HAL_FIRST_STEP_MAX)
			ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
					     aniState->firstepLevel + 1);
	} else {
		mode = ath9k_hw_chan2wmode(ah, chan);
		if (mode == ATH9K_MODE_11G || mode == ATH9K_MODE_11B) {
			if (aniState->firstepLevel > 0)
				ath9k_hw_ani_control(ah,
						     ATH9K_ANI_FIRSTEP_LEVEL,
						     0);
		}
	}
}

static void ath9k_ani_reset(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416AniState *aniState;
	struct ath9k_channel *chan = ah->ah_curchan;
	int index;

	if (!DO_ANI(ah))
		return;

	index = ath9k_hw_get_ani_channel_idx(ah, chan);
	aniState = &ahp->ah_ani[index];
	ahp->ah_curani = aniState;

	if (DO_ANI(ah) && ah->ah_opmode != ATH9K_M_STA
	    && ah->ah_opmode != ATH9K_M_IBSS) {
		DPRINTF(ah->ah_sc, ATH_DBG_ANI,
			 "%s: Reset ANI state opmode %u\n", __func__,
			 ah->ah_opmode);
		ahp->ah_stats.ast_ani_reset++;
		ath9k_hw_ani_control(ah, ATH9K_ANI_NOISE_IMMUNITY_LEVEL, 0);
		ath9k_hw_ani_control(ah, ATH9K_ANI_SPUR_IMMUNITY_LEVEL, 0);
		ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL, 0);
		ath9k_hw_ani_control(ah,
				     ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				     !ATH9K_ANI_USE_OFDM_WEAK_SIG);
		ath9k_hw_ani_control(ah, ATH9K_ANI_CCK_WEAK_SIGNAL_THR,
				     ATH9K_ANI_CCK_WEAK_SIG_THR);
		ath9k_hw_setrxfilter(ah,
				     ath9k_hw_getrxfilter(ah) |
				     ATH9K_RX_FILTER_PHYERR);
		if (ah->ah_opmode == ATH9K_M_HOSTAP) {
			ahp->ah_curani->ofdmTrigHigh =
				ah->ah_config.ofdm_trig_high;
			ahp->ah_curani->ofdmTrigLow =
				ah->ah_config.ofdm_trig_low;
			ahp->ah_curani->cckTrigHigh =
				ah->ah_config.cck_trig_high;
			ahp->ah_curani->cckTrigLow =
				ah->ah_config.cck_trig_low;
		}
		ath9k_ani_restart(ah);
		return;
	}

	if (aniState->noiseImmunityLevel != 0)
		ath9k_hw_ani_control(ah, ATH9K_ANI_NOISE_IMMUNITY_LEVEL,
				     aniState->noiseImmunityLevel);
	if (aniState->spurImmunityLevel != 0)
		ath9k_hw_ani_control(ah, ATH9K_ANI_SPUR_IMMUNITY_LEVEL,
				     aniState->spurImmunityLevel);
	if (aniState->ofdmWeakSigDetectOff)
		ath9k_hw_ani_control(ah,
				     ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
				     !aniState->ofdmWeakSigDetectOff);
	if (aniState->cckWeakSigThreshold)
		ath9k_hw_ani_control(ah, ATH9K_ANI_CCK_WEAK_SIGNAL_THR,
				     aniState->cckWeakSigThreshold);
	if (aniState->firstepLevel != 0)
		ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
				     aniState->firstepLevel);
	if (ahp->ah_hasHwPhyCounters) {
		ath9k_hw_setrxfilter(ah,
				     ath9k_hw_getrxfilter(ah) &
				     ~ATH9K_RX_FILTER_PHYERR);
		ath9k_ani_restart(ah);
		REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
		REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

	} else {
		ath9k_ani_restart(ah);
		ath9k_hw_setrxfilter(ah,
				     ath9k_hw_getrxfilter(ah) |
				     ATH9K_RX_FILTER_PHYERR);
	}
}

void ath9k_hw_procmibevent(struct ath_hal *ah,
			   const struct ath9k_node_stats *stats)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	u32 phyCnt1, phyCnt2;

	DPRINTF(ah->ah_sc, ATH_DBG_ANI, "Processing Mib Intr\n");

	REG_WRITE(ah, AR_FILT_OFDM, 0);
	REG_WRITE(ah, AR_FILT_CCK, 0);
	if (!(REG_READ(ah, AR_SLP_MIB_CTRL) & AR_SLP_MIB_PENDING))
		REG_WRITE(ah, AR_SLP_MIB_CTRL, AR_SLP_MIB_CLEAR);

	ath9k_hw_update_mibstats(ah, &ahp->ah_mibStats);
	ahp->ah_stats.ast_nodestats = *stats;

	if (!DO_ANI(ah))
		return;

	phyCnt1 = REG_READ(ah, AR_PHY_ERR_1);
	phyCnt2 = REG_READ(ah, AR_PHY_ERR_2);
	if (((phyCnt1 & AR_MIBCNT_INTRMASK) == AR_MIBCNT_INTRMASK) ||
	    ((phyCnt2 & AR_MIBCNT_INTRMASK) == AR_MIBCNT_INTRMASK)) {
		struct ar5416AniState *aniState = ahp->ah_curani;
		u32 ofdmPhyErrCnt, cckPhyErrCnt;

		ofdmPhyErrCnt = phyCnt1 - aniState->ofdmPhyErrBase;
		ahp->ah_stats.ast_ani_ofdmerrs +=
			ofdmPhyErrCnt - aniState->ofdmPhyErrCount;
		aniState->ofdmPhyErrCount = ofdmPhyErrCnt;

		cckPhyErrCnt = phyCnt2 - aniState->cckPhyErrBase;
		ahp->ah_stats.ast_ani_cckerrs +=
			cckPhyErrCnt - aniState->cckPhyErrCount;
		aniState->cckPhyErrCount = cckPhyErrCnt;

		if (aniState->ofdmPhyErrCount > aniState->ofdmTrigHigh)
			ath9k_hw_ani_ofdm_err_trigger(ah);
		if (aniState->cckPhyErrCount > aniState->cckTrigHigh)
			ath9k_hw_ani_cck_err_trigger(ah);

		ath9k_ani_restart(ah);
	}
}

static void ath9k_hw_ani_lower_immunity(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416AniState *aniState;
	int32_t rssi;

	aniState = ahp->ah_curani;

	if (ah->ah_opmode == ATH9K_M_HOSTAP) {
		if (aniState->firstepLevel > 0) {
			if (ath9k_hw_ani_control(ah, ATH9K_ANI_FIRSTEP_LEVEL,
						 aniState->firstepLevel - 1)) {
				return;
			}
		}
	} else {
		rssi = BEACON_RSSI(ahp);
		if (rssi > aniState->rssiThrHigh) {
			/* XXX: Handle me */
		} else if (rssi > aniState->rssiThrLow) {
			if (aniState->ofdmWeakSigDetectOff) {
				if (ath9k_hw_ani_control(ah,
					 ATH9K_ANI_OFDM_WEAK_SIGNAL_DETECTION,
					 true) ==
				    true) {
					return;
				}
			}
			if (aniState->firstepLevel > 0) {
				if (ath9k_hw_ani_control
				    (ah, ATH9K_ANI_FIRSTEP_LEVEL,
				     aniState->firstepLevel - 1) ==
				    true) {
					return;
				}
			}
		} else {
			if (aniState->firstepLevel > 0) {
				if (ath9k_hw_ani_control
				    (ah, ATH9K_ANI_FIRSTEP_LEVEL,
				     aniState->firstepLevel - 1) ==
				    true) {
					return;
				}
			}
		}
	}

	if (aniState->spurImmunityLevel > 0) {
		if (ath9k_hw_ani_control(ah, ATH9K_ANI_SPUR_IMMUNITY_LEVEL,
					 aniState->spurImmunityLevel - 1)) {
			return;
		}
	}

	if (aniState->noiseImmunityLevel > 0) {
		ath9k_hw_ani_control(ah, ATH9K_ANI_NOISE_IMMUNITY_LEVEL,
				     aniState->noiseImmunityLevel - 1);
		return;
	}
}

static int32_t ath9k_hw_ani_get_listen_time(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416AniState *aniState;
	u32 txFrameCount, rxFrameCount, cycleCount;
	int32_t listenTime;

	txFrameCount = REG_READ(ah, AR_TFCNT);
	rxFrameCount = REG_READ(ah, AR_RFCNT);
	cycleCount = REG_READ(ah, AR_CCCNT);

	aniState = ahp->ah_curani;
	if (aniState->cycleCount == 0 || aniState->cycleCount > cycleCount) {

		listenTime = 0;
		ahp->ah_stats.ast_ani_lzero++;
	} else {
		int32_t ccdelta = cycleCount - aniState->cycleCount;
		int32_t rfdelta = rxFrameCount - aniState->rxFrameCount;
		int32_t tfdelta = txFrameCount - aniState->txFrameCount;
		listenTime = (ccdelta - rfdelta - tfdelta) / 44000;
	}
	aniState->cycleCount = cycleCount;
	aniState->txFrameCount = txFrameCount;
	aniState->rxFrameCount = rxFrameCount;

	return listenTime;
}

void ath9k_hw_ani_monitor(struct ath_hal *ah,
			  const struct ath9k_node_stats *stats,
			  struct ath9k_channel *chan)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416AniState *aniState;
	int32_t listenTime;

	aniState = ahp->ah_curani;
	ahp->ah_stats.ast_nodestats = *stats;

	listenTime = ath9k_hw_ani_get_listen_time(ah);
	if (listenTime < 0) {
		ahp->ah_stats.ast_ani_lneg++;
		ath9k_ani_restart(ah);
		return;
	}

	aniState->listenTime += listenTime;

	if (ahp->ah_hasHwPhyCounters) {
		u32 phyCnt1, phyCnt2;
		u32 ofdmPhyErrCnt, cckPhyErrCnt;

		ath9k_hw_update_mibstats(ah, &ahp->ah_mibStats);

		phyCnt1 = REG_READ(ah, AR_PHY_ERR_1);
		phyCnt2 = REG_READ(ah, AR_PHY_ERR_2);

		if (phyCnt1 < aniState->ofdmPhyErrBase ||
		    phyCnt2 < aniState->cckPhyErrBase) {
			if (phyCnt1 < aniState->ofdmPhyErrBase) {
				DPRINTF(ah->ah_sc, ATH_DBG_ANI,
					 "%s: phyCnt1 0x%x, resetting "
					 "counter value to 0x%x\n",
					 __func__, phyCnt1,
					 aniState->ofdmPhyErrBase);
				REG_WRITE(ah, AR_PHY_ERR_1,
					  aniState->ofdmPhyErrBase);
				REG_WRITE(ah, AR_PHY_ERR_MASK_1,
					  AR_PHY_ERR_OFDM_TIMING);
			}
			if (phyCnt2 < aniState->cckPhyErrBase) {
				DPRINTF(ah->ah_sc, ATH_DBG_ANI,
					 "%s: phyCnt2 0x%x, resetting "
					 "counter value to 0x%x\n",
					 __func__, phyCnt2,
					 aniState->cckPhyErrBase);
				REG_WRITE(ah, AR_PHY_ERR_2,
					  aniState->cckPhyErrBase);
				REG_WRITE(ah, AR_PHY_ERR_MASK_2,
					  AR_PHY_ERR_CCK_TIMING);
			}
			return;
		}

		ofdmPhyErrCnt = phyCnt1 - aniState->ofdmPhyErrBase;
		ahp->ah_stats.ast_ani_ofdmerrs +=
			ofdmPhyErrCnt - aniState->ofdmPhyErrCount;
		aniState->ofdmPhyErrCount = ofdmPhyErrCnt;

		cckPhyErrCnt = phyCnt2 - aniState->cckPhyErrBase;
		ahp->ah_stats.ast_ani_cckerrs +=
			cckPhyErrCnt - aniState->cckPhyErrCount;
		aniState->cckPhyErrCount = cckPhyErrCnt;
	}

	if (!DO_ANI(ah))
		return;

	if (aniState->listenTime > 5 * ahp->ah_aniPeriod) {
		if (aniState->ofdmPhyErrCount <= aniState->listenTime *
		    aniState->ofdmTrigLow / 1000 &&
		    aniState->cckPhyErrCount <= aniState->listenTime *
		    aniState->cckTrigLow / 1000)
			ath9k_hw_ani_lower_immunity(ah);
		ath9k_ani_restart(ah);
	} else if (aniState->listenTime > ahp->ah_aniPeriod) {
		if (aniState->ofdmPhyErrCount > aniState->listenTime *
		    aniState->ofdmTrigHigh / 1000) {
			ath9k_hw_ani_ofdm_err_trigger(ah);
			ath9k_ani_restart(ah);
		} else if (aniState->cckPhyErrCount >
			   aniState->listenTime * aniState->cckTrigHigh /
			   1000) {
			ath9k_hw_ani_cck_err_trigger(ah);
			ath9k_ani_restart(ah);
		}
	}
}

#ifndef ATH_NF_PER_CHAN
static void ath9k_init_nfcal_hist_buffer(struct ath_hal *ah)
{
	int i, j;

	for (i = 0; i < NUM_NF_READINGS; i++) {
		ah->nfCalHist[i].currIndex = 0;
		ah->nfCalHist[i].privNF = AR_PHY_CCA_MAX_GOOD_VALUE;
		ah->nfCalHist[i].invalidNFcount =
			AR_PHY_CCA_FILTERWINDOW_LENGTH;
		for (j = 0; j < ATH9K_NF_CAL_HIST_MAX; j++) {
			ah->nfCalHist[i].nfCalBuffer[j] =
				AR_PHY_CCA_MAX_GOOD_VALUE;
		}
	}
	return;
}
#endif

static void ath9k_hw_gpio_cfg_output_mux(struct ath_hal *ah,
					 u32 gpio, u32 type)
{
	int addr;
	u32 gpio_shift, tmp;

	if (gpio > 11)
		addr = AR_GPIO_OUTPUT_MUX3;
	else if (gpio > 5)
		addr = AR_GPIO_OUTPUT_MUX2;
	else
		addr = AR_GPIO_OUTPUT_MUX1;

	gpio_shift = (gpio % 6) * 5;

	if (AR_SREV_9280_20_OR_LATER(ah)
	    || (addr != AR_GPIO_OUTPUT_MUX1)) {
		REG_RMW(ah, addr, (type << gpio_shift),
			(0x1f << gpio_shift));
	} else {
		tmp = REG_READ(ah, addr);
		tmp = ((tmp & 0x1F0) << 1) | (tmp & ~0x1F0);
		tmp &= ~(0x1f << gpio_shift);
		tmp |= (type << gpio_shift);
		REG_WRITE(ah, addr, tmp);
	}
}

static bool ath9k_hw_cfg_output(struct ath_hal *ah, u32 gpio,
				enum ath9k_gpio_output_mux_type
				halSignalType)
{
	u32 ah_signal_type;
	u32 gpio_shift;

	static u32 MuxSignalConversionTable[] = {

		AR_GPIO_OUTPUT_MUX_AS_OUTPUT,

		AR_GPIO_OUTPUT_MUX_AS_PCIE_ATTENTION_LED,

		AR_GPIO_OUTPUT_MUX_AS_PCIE_POWER_LED,

		AR_GPIO_OUTPUT_MUX_AS_MAC_NETWORK_LED,

		AR_GPIO_OUTPUT_MUX_AS_MAC_POWER_LED,
	};

	if ((halSignalType >= 0)
	    && (halSignalType < ARRAY_SIZE(MuxSignalConversionTable)))
		ah_signal_type = MuxSignalConversionTable[halSignalType];
	else
		return false;

	ath9k_hw_gpio_cfg_output_mux(ah, gpio, ah_signal_type);

	gpio_shift = 2 * gpio;

	REG_RMW(ah,
		AR_GPIO_OE_OUT,
		(AR_GPIO_OE_OUT_DRV_ALL << gpio_shift),
		(AR_GPIO_OE_OUT_DRV << gpio_shift));

	return true;
}

static bool ath9k_hw_set_gpio(struct ath_hal *ah, u32 gpio,
			      u32 val)
{
	REG_RMW(ah, AR_GPIO_IN_OUT, ((val & 1) << gpio),
		AR_GPIO_BIT(gpio));
	return true;
}

static u32 ath9k_hw_gpio_get(struct ath_hal *ah, u32 gpio)
{
	if (gpio >= ah->ah_caps.num_gpio_pins)
		return 0xffffffff;

	if (AR_SREV_9280_10_OR_LATER(ah)) {
		return (MS
			(REG_READ(ah, AR_GPIO_IN_OUT),
			 AR928X_GPIO_IN_VAL) & AR_GPIO_BIT(gpio)) != 0;
	} else {
		return (MS(REG_READ(ah, AR_GPIO_IN_OUT), AR_GPIO_IN_VAL) &
			AR_GPIO_BIT(gpio)) != 0;
	}
}

static inline int ath9k_hw_post_attach(struct ath_hal *ah)
{
	int ecode;

	if (!ath9k_hw_chip_test(ah)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
			 "%s: hardware self-test failed\n", __func__);
		return -ENODEV;
	}

	ecode = ath9k_hw_rf_claim(ah);
	if (ecode != 0)
		return ecode;

	ecode = ath9k_hw_eeprom_attach(ah);
	if (ecode != 0)
		return ecode;
	ecode = ath9k_hw_rfattach(ah);
	if (ecode != 0)
		return ecode;

	if (!AR_SREV_9100(ah)) {
		ath9k_hw_ani_setup(ah);
		ath9k_hw_ani_attach(ah);
	}
	return 0;
}

static u32 ath9k_hw_ini_fixup(struct ath_hal *ah,
				    struct ar5416_eeprom *pEepData,
				    u32 reg, u32 value)
{
	struct base_eep_header *pBase = &(pEepData->baseEepHeader);

	switch (ah->ah_devid) {
	case AR9280_DEVID_PCI:
		if (reg == 0x7894) {
			DPRINTF(ah->ah_sc, ATH_DBG_ANY,
				 "ini VAL: %x  EEPROM: %x\n", value,
				 (pBase->version & 0xff));

			if ((pBase->version & 0xff) > 0x0a) {
				DPRINTF(ah->ah_sc, ATH_DBG_ANY,
					 "PWDCLKIND: %d\n",
					 pBase->pwdclkind);
				value &= ~AR_AN_TOP2_PWDCLKIND;
				value |= AR_AN_TOP2_PWDCLKIND & (pBase->
					 pwdclkind << AR_AN_TOP2_PWDCLKIND_S);
			} else {
				DPRINTF(ah->ah_sc, ATH_DBG_ANY,
					 "PWDCLKIND Earlier Rev\n");
			}

			DPRINTF(ah->ah_sc, ATH_DBG_ANY,
				 "final ini VAL: %x\n", value);
		}
		break;
	}
	return value;
}

static bool ath9k_hw_fill_cap_info(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	u16 capField = 0, eeval;

	eeval = ath9k_hw_get_eeprom(ahp, EEP_REG_0);

	ah->ah_currentRD = eeval;

	eeval = ath9k_hw_get_eeprom(ahp, EEP_REG_1);
	ah->ah_currentRDExt = eeval;

	capField = ath9k_hw_get_eeprom(ahp, EEP_OP_CAP);

	if (ah->ah_opmode != ATH9K_M_HOSTAP &&
	    ah->ah_subvendorid == AR_SUBVENDOR_ID_NEW_A) {
		if (ah->ah_currentRD == 0x64 || ah->ah_currentRD == 0x65)
			ah->ah_currentRD += 5;
		else if (ah->ah_currentRD == 0x41)
			ah->ah_currentRD = 0x43;
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			 "%s: regdomain mapped to 0x%x\n", __func__,
			 ah->ah_currentRD);
	}

	eeval = ath9k_hw_get_eeprom(ahp, EEP_OP_MODE);
	bitmap_zero(pCap->wireless_modes, ATH9K_MODE_MAX);

	if (eeval & AR5416_OPFLAGS_11A) {
		set_bit(ATH9K_MODE_11A, pCap->wireless_modes);
		if (ah->ah_config.ht_enable) {
			if (!(eeval & AR5416_OPFLAGS_N_5G_HT20))
				set_bit(ATH9K_MODE_11NA_HT20,
					pCap->wireless_modes);
			if (!(eeval & AR5416_OPFLAGS_N_5G_HT40)) {
				set_bit(ATH9K_MODE_11NA_HT40PLUS,
					pCap->wireless_modes);
				set_bit(ATH9K_MODE_11NA_HT40MINUS,
					pCap->wireless_modes);
			}
		}
	}

	if (eeval & AR5416_OPFLAGS_11G) {
		set_bit(ATH9K_MODE_11B, pCap->wireless_modes);
		set_bit(ATH9K_MODE_11G, pCap->wireless_modes);
		if (ah->ah_config.ht_enable) {
			if (!(eeval & AR5416_OPFLAGS_N_2G_HT20))
				set_bit(ATH9K_MODE_11NG_HT20,
					pCap->wireless_modes);
			if (!(eeval & AR5416_OPFLAGS_N_2G_HT40)) {
				set_bit(ATH9K_MODE_11NG_HT40PLUS,
					pCap->wireless_modes);
				set_bit(ATH9K_MODE_11NG_HT40MINUS,
					pCap->wireless_modes);
			}
		}
	}

	pCap->tx_chainmask = ath9k_hw_get_eeprom(ahp, EEP_TX_MASK);
	if ((ah->ah_isPciExpress)
	    || (eeval & AR5416_OPFLAGS_11A)) {
		pCap->rx_chainmask =
			ath9k_hw_get_eeprom(ahp, EEP_RX_MASK);
	} else {
		pCap->rx_chainmask =
			(ath9k_hw_gpio_get(ah, 0)) ? 0x5 : 0x7;
	}

	if (!(AR_SREV_9280(ah) && (ah->ah_macRev == 0)))
		ahp->ah_miscMode |= AR_PCU_MIC_NEW_LOC_ENA;

	pCap->low_2ghz_chan = 2312;
	pCap->high_2ghz_chan = 2732;

	pCap->low_5ghz_chan = 4920;
	pCap->high_5ghz_chan = 6100;

	pCap->hw_caps &= ~ATH9K_HW_CAP_CIPHER_CKIP;
	pCap->hw_caps |= ATH9K_HW_CAP_CIPHER_TKIP;
	pCap->hw_caps |= ATH9K_HW_CAP_CIPHER_AESCCM;

	pCap->hw_caps &= ~ATH9K_HW_CAP_MIC_CKIP;
	pCap->hw_caps |= ATH9K_HW_CAP_MIC_TKIP;
	pCap->hw_caps |= ATH9K_HW_CAP_MIC_AESCCM;

	pCap->hw_caps |= ATH9K_HW_CAP_CHAN_SPREAD;

	if (ah->ah_config.ht_enable)
		pCap->hw_caps |= ATH9K_HW_CAP_HT;
	else
		pCap->hw_caps &= ~ATH9K_HW_CAP_HT;

	pCap->hw_caps |= ATH9K_HW_CAP_GTT;
	pCap->hw_caps |= ATH9K_HW_CAP_VEOL;
	pCap->hw_caps |= ATH9K_HW_CAP_BSSIDMASK;
	pCap->hw_caps &= ~ATH9K_HW_CAP_MCAST_KEYSEARCH;

	if (capField & AR_EEPROM_EEPCAP_MAXQCU)
		pCap->total_queues =
			MS(capField, AR_EEPROM_EEPCAP_MAXQCU);
	else
		pCap->total_queues = ATH9K_NUM_TX_QUEUES;

	if (capField & AR_EEPROM_EEPCAP_KC_ENTRIES)
		pCap->keycache_size =
			1 << MS(capField, AR_EEPROM_EEPCAP_KC_ENTRIES);
	else
		pCap->keycache_size = AR_KEYTABLE_SIZE;

	pCap->hw_caps |= ATH9K_HW_CAP_FASTCC;
	pCap->num_mr_retries = 4;
	pCap->tx_triglevel_max = MAX_TX_FIFO_THRESHOLD;

	if (AR_SREV_9280_10_OR_LATER(ah))
		pCap->num_gpio_pins = AR928X_NUM_GPIO;
	else
		pCap->num_gpio_pins = AR_NUM_GPIO;

	if (AR_SREV_9280_10_OR_LATER(ah)) {
		pCap->hw_caps |= ATH9K_HW_CAP_WOW;
		pCap->hw_caps |= ATH9K_HW_CAP_WOW_MATCHPATTERN_EXACT;
	} else {
		pCap->hw_caps &= ~ATH9K_HW_CAP_WOW;
		pCap->hw_caps &= ~ATH9K_HW_CAP_WOW_MATCHPATTERN_EXACT;
	}

	if (AR_SREV_9160_10_OR_LATER(ah) || AR_SREV_9100(ah)) {
		pCap->hw_caps |= ATH9K_HW_CAP_CST;
		pCap->rts_aggr_limit = ATH_AMPDU_LIMIT_MAX;
	} else {
		pCap->rts_aggr_limit = (8 * 1024);
	}

	pCap->hw_caps |= ATH9K_HW_CAP_ENHANCEDPM;

	ah->ah_rfsilent = ath9k_hw_get_eeprom(ahp, EEP_RF_SILENT);
	if (ah->ah_rfsilent & EEP_RFSILENT_ENABLED) {
		ahp->ah_gpioSelect =
			MS(ah->ah_rfsilent, EEP_RFSILENT_GPIO_SEL);
		ahp->ah_polarity =
			MS(ah->ah_rfsilent, EEP_RFSILENT_POLARITY);

		ath9k_hw_setcapability(ah, ATH9K_CAP_RFSILENT, 1, true,
				       NULL);
		pCap->hw_caps |= ATH9K_HW_CAP_RFSILENT;
	}

	if ((ah->ah_macVersion == AR_SREV_VERSION_5416_PCI) ||
	    (ah->ah_macVersion == AR_SREV_VERSION_5416_PCIE) ||
	    (ah->ah_macVersion == AR_SREV_VERSION_9160) ||
	    (ah->ah_macVersion == AR_SREV_VERSION_9100) ||
	    (ah->ah_macVersion == AR_SREV_VERSION_9280))
		pCap->hw_caps &= ~ATH9K_HW_CAP_AUTOSLEEP;
	else
		pCap->hw_caps |= ATH9K_HW_CAP_AUTOSLEEP;

	if (AR_SREV_9280(ah))
		pCap->hw_caps &= ~ATH9K_HW_CAP_4KB_SPLITTRANS;
	else
		pCap->hw_caps |= ATH9K_HW_CAP_4KB_SPLITTRANS;

	if (ah->ah_currentRDExt & (1 << REG_EXT_JAPAN_MIDBAND)) {
		pCap->reg_cap =
			AR_EEPROM_EEREGCAP_EN_KK_NEW_11A |
			AR_EEPROM_EEREGCAP_EN_KK_U1_EVEN |
			AR_EEPROM_EEREGCAP_EN_KK_U2 |
			AR_EEPROM_EEREGCAP_EN_KK_MIDBAND;
	} else {
		pCap->reg_cap =
			AR_EEPROM_EEREGCAP_EN_KK_NEW_11A |
			AR_EEPROM_EEREGCAP_EN_KK_U1_EVEN;
	}

	pCap->reg_cap |= AR_EEPROM_EEREGCAP_EN_FCC_MIDBAND;

	pCap->num_antcfg_5ghz =
		ath9k_hw_get_num_ant_config(ahp, IEEE80211_BAND_5GHZ);
	pCap->num_antcfg_2ghz =
		ath9k_hw_get_num_ant_config(ahp, IEEE80211_BAND_2GHZ);

	return true;
}

static void ar5416DisablePciePhy(struct ath_hal *ah)
{
	if (!AR_SREV_9100(ah))
		return;

	REG_WRITE(ah, AR_PCIE_SERDES, 0x9248fc00);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x24924924);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x28000029);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x57160824);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x25980579);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x00000000);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x1aaabe40);
	REG_WRITE(ah, AR_PCIE_SERDES, 0xbe105554);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x000e1007);

	REG_WRITE(ah, AR_PCIE_SERDES2, 0x00000000);
}

static void ath9k_set_power_sleep(struct ath_hal *ah, int setChip)
{
	REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	if (setChip) {
		REG_CLR_BIT(ah, AR_RTC_FORCE_WAKE,
			    AR_RTC_FORCE_WAKE_EN);
		if (!AR_SREV_9100(ah))
			REG_WRITE(ah, AR_RC, AR_RC_AHB | AR_RC_HOSTIF);

		REG_CLR_BIT(ah, (u16) (AR_RTC_RESET),
			    AR_RTC_RESET_EN);
	}
}

static void ath9k_set_power_network_sleep(struct ath_hal *ah, int setChip)
{
	REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	if (setChip) {
		struct ath9k_hw_capabilities *pCap = &ah->ah_caps;

		if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP)) {
			REG_WRITE(ah, AR_RTC_FORCE_WAKE,
				  AR_RTC_FORCE_WAKE_ON_INT);
		} else {
			REG_CLR_BIT(ah, AR_RTC_FORCE_WAKE,
				    AR_RTC_FORCE_WAKE_EN);
		}
	}
}

static bool ath9k_hw_set_power_awake(struct ath_hal *ah,
				     int setChip)
{
	u32 val;
	int i;

	if (setChip) {
		if ((REG_READ(ah, AR_RTC_STATUS) & AR_RTC_STATUS_M) ==
		    AR_RTC_STATUS_SHUTDOWN) {
			if (ath9k_hw_set_reset_reg(ah, ATH9K_RESET_POWER_ON)
			    != true) {
				return false;
			}
		}
		if (AR_SREV_9100(ah))
			REG_SET_BIT(ah, AR_RTC_RESET,
				       AR_RTC_RESET_EN);

		REG_SET_BIT(ah, AR_RTC_FORCE_WAKE,
			    AR_RTC_FORCE_WAKE_EN);
		udelay(50);

		for (i = POWER_UP_TIME / 50; i > 0; i--) {
			val = REG_READ(ah, AR_RTC_STATUS) & AR_RTC_STATUS_M;
			if (val == AR_RTC_STATUS_ON)
				break;
			udelay(50);
			REG_SET_BIT(ah, AR_RTC_FORCE_WAKE,
				       AR_RTC_FORCE_WAKE_EN);
		}
		if (i == 0) {
			DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT,
				 "%s: Failed to wakeup in %uus\n",
				 __func__, POWER_UP_TIME / 20);
			return false;
		}
	}

	REG_CLR_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	return true;
}

bool ath9k_hw_setpower(struct ath_hal *ah,
		       enum ath9k_power_mode mode)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	static const char *modes[] = {
		"AWAKE",
		"FULL-SLEEP",
		"NETWORK SLEEP",
		"UNDEFINED"
	};
	int status = true, setChip = true;

	DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT, "%s: %s -> %s (%s)\n", __func__,
		 modes[ahp->ah_powerMode], modes[mode],
		 setChip ? "set chip " : "");

	switch (mode) {
	case ATH9K_PM_AWAKE:
		status = ath9k_hw_set_power_awake(ah, setChip);
		break;
	case ATH9K_PM_FULL_SLEEP:
		ath9k_set_power_sleep(ah, setChip);
		ahp->ah_chipFullSleep = true;
		break;
	case ATH9K_PM_NETWORK_SLEEP:
		ath9k_set_power_network_sleep(ah, setChip);
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT,
			 "%s: unknown power mode %u\n", __func__, mode);
		return false;
	}
	ahp->ah_powerMode = mode;
	return status;
}

static struct ath_hal *ath9k_hw_do_attach(u16 devid,
					  struct ath_softc *sc,
					  void __iomem *mem,
					  int *status)
{
	struct ath_hal_5416 *ahp;
	struct ath_hal *ah;
	int ecode;
#ifndef CONFIG_SLOW_ANT_DIV
	u32 i;
	u32 j;
#endif

	ahp = ath9k_hw_newstate(devid, sc, mem, status);
	if (ahp == NULL)
		return NULL;

	ah = &ahp->ah;

	ath9k_hw_set_defaults(ah);

	if (ah->ah_config.intr_mitigation != 0)
		ahp->ah_intrMitigation = true;

	if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_POWER_ON)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "%s: couldn't reset chip\n",
			 __func__);
		ecode = -EIO;
		goto bad;
	}

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "%s: couldn't wakeup chip\n",
			 __func__);
		ecode = -EIO;
		goto bad;
	}

	if (ah->ah_config.serialize_regmode == SER_REG_MODE_AUTO) {
		if (ah->ah_macVersion == AR_SREV_VERSION_5416_PCI) {
			ah->ah_config.serialize_regmode =
				SER_REG_MODE_ON;
		} else {
			ah->ah_config.serialize_regmode =
				SER_REG_MODE_OFF;
		}
	}
	DPRINTF(ah->ah_sc, ATH_DBG_RESET,
		"%s: serialize_regmode is %d\n",
		__func__, ah->ah_config.serialize_regmode);

	if ((ah->ah_macVersion != AR_SREV_VERSION_5416_PCI) &&
	    (ah->ah_macVersion != AR_SREV_VERSION_5416_PCIE) &&
	    (ah->ah_macVersion != AR_SREV_VERSION_9160) &&
	    (!AR_SREV_9100(ah)) && (!AR_SREV_9280(ah))) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET,
			 "%s: Mac Chip Rev 0x%02x.%x is not supported by "
			 "this driver\n", __func__,
			 ah->ah_macVersion, ah->ah_macRev);
		ecode = -EOPNOTSUPP;
		goto bad;
	}

	if (AR_SREV_9100(ah)) {
		ahp->ah_iqCalData.calData = &iq_cal_multi_sample;
		ahp->ah_suppCals = IQ_MISMATCH_CAL;
		ah->ah_isPciExpress = false;
	}
	ah->ah_phyRev = REG_READ(ah, AR_PHY_CHIP_ID);

	if (AR_SREV_9160_10_OR_LATER(ah)) {
		if (AR_SREV_9280_10_OR_LATER(ah)) {
			ahp->ah_iqCalData.calData = &iq_cal_single_sample;
			ahp->ah_adcGainCalData.calData =
				&adc_gain_cal_single_sample;
			ahp->ah_adcDcCalData.calData =
				&adc_dc_cal_single_sample;
			ahp->ah_adcDcCalInitData.calData =
				&adc_init_dc_cal;
		} else {
			ahp->ah_iqCalData.calData = &iq_cal_multi_sample;
			ahp->ah_adcGainCalData.calData =
				&adc_gain_cal_multi_sample;
			ahp->ah_adcDcCalData.calData =
				&adc_dc_cal_multi_sample;
			ahp->ah_adcDcCalInitData.calData =
				&adc_init_dc_cal;
		}
		ahp->ah_suppCals =
			ADC_GAIN_CAL | ADC_DC_CAL | IQ_MISMATCH_CAL;
	}

	if (AR_SREV_9160(ah)) {
		ah->ah_config.enable_ani = 1;
		ahp->ah_ani_function = (ATH9K_ANI_SPUR_IMMUNITY_LEVEL |
					ATH9K_ANI_FIRSTEP_LEVEL);
	} else {
		ahp->ah_ani_function = ATH9K_ANI_ALL;
		if (AR_SREV_9280_10_OR_LATER(ah)) {
			ahp->ah_ani_function &=
				~ATH9K_ANI_NOISE_IMMUNITY_LEVEL;
		}
	}

	DPRINTF(ah->ah_sc, ATH_DBG_RESET,
		 "%s: This Mac Chip Rev 0x%02x.%x is \n", __func__,
		 ah->ah_macVersion, ah->ah_macRev);

	if (AR_SREV_9280_20_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ahp->ah_iniModes, ar9280Modes_9280_2,
			       ARRAY_SIZE(ar9280Modes_9280_2), 6);
		INIT_INI_ARRAY(&ahp->ah_iniCommon, ar9280Common_9280_2,
			       ARRAY_SIZE(ar9280Common_9280_2), 2);

		if (ah->ah_config.pcie_clock_req) {
			INIT_INI_ARRAY(&ahp->ah_iniPcieSerdes,
				       ar9280PciePhy_clkreq_off_L1_9280,
				       ARRAY_SIZE
				       (ar9280PciePhy_clkreq_off_L1_9280),
				       2);
		} else {
			INIT_INI_ARRAY(&ahp->ah_iniPcieSerdes,
				       ar9280PciePhy_clkreq_always_on_L1_9280,
				       ARRAY_SIZE
				       (ar9280PciePhy_clkreq_always_on_L1_9280),
				       2);
		}
		INIT_INI_ARRAY(&ahp->ah_iniModesAdditional,
			       ar9280Modes_fast_clock_9280_2,
			       ARRAY_SIZE(ar9280Modes_fast_clock_9280_2),
			       3);
	} else if (AR_SREV_9280_10_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ahp->ah_iniModes, ar9280Modes_9280,
			       ARRAY_SIZE(ar9280Modes_9280), 6);
		INIT_INI_ARRAY(&ahp->ah_iniCommon, ar9280Common_9280,
			       ARRAY_SIZE(ar9280Common_9280), 2);
	} else if (AR_SREV_9160_10_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ahp->ah_iniModes, ar5416Modes_9160,
			       ARRAY_SIZE(ar5416Modes_9160), 6);
		INIT_INI_ARRAY(&ahp->ah_iniCommon, ar5416Common_9160,
			       ARRAY_SIZE(ar5416Common_9160), 2);
		INIT_INI_ARRAY(&ahp->ah_iniBank0, ar5416Bank0_9160,
			       ARRAY_SIZE(ar5416Bank0_9160), 2);
		INIT_INI_ARRAY(&ahp->ah_iniBB_RfGain, ar5416BB_RfGain_9160,
			       ARRAY_SIZE(ar5416BB_RfGain_9160), 3);
		INIT_INI_ARRAY(&ahp->ah_iniBank1, ar5416Bank1_9160,
			       ARRAY_SIZE(ar5416Bank1_9160), 2);
		INIT_INI_ARRAY(&ahp->ah_iniBank2, ar5416Bank2_9160,
			       ARRAY_SIZE(ar5416Bank2_9160), 2);
		INIT_INI_ARRAY(&ahp->ah_iniBank3, ar5416Bank3_9160,
			       ARRAY_SIZE(ar5416Bank3_9160), 3);
		INIT_INI_ARRAY(&ahp->ah_iniBank6, ar5416Bank6_9160,
			       ARRAY_SIZE(ar5416Bank6_9160), 3);
		INIT_INI_ARRAY(&ahp->ah_iniBank6TPC, ar5416Bank6TPC_9160,
			       ARRAY_SIZE(ar5416Bank6TPC_9160), 3);
		INIT_INI_ARRAY(&ahp->ah_iniBank7, ar5416Bank7_9160,
			       ARRAY_SIZE(ar5416Bank7_9160), 2);
		if (AR_SREV_9160_11(ah)) {
			INIT_INI_ARRAY(&ahp->ah_iniAddac,
				       ar5416Addac_91601_1,
				       ARRAY_SIZE(ar5416Addac_91601_1), 2);
		} else {
			INIT_INI_ARRAY(&ahp->ah_iniAddac, ar5416Addac_9160,
				       ARRAY_SIZE(ar5416Addac_9160), 2);
		}
	} else if (AR_SREV_9100_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ahp->ah_iniModes, ar5416Modes_9100,
			       ARRAY_SIZE(ar5416Modes_9100), 6);
		INIT_INI_ARRAY(&ahp->ah_iniCommon, ar5416Common_9100,
			       ARRAY_SIZE(ar5416Common_9100), 2);
		INIT_INI_ARRAY(&ahp->ah_iniBank0, ar5416Bank0_9100,
			       ARRAY_SIZE(ar5416Bank0_9100), 2);
		INIT_INI_ARRAY(&ahp->ah_iniBB_RfGain, ar5416BB_RfGain_9100,
			       ARRAY_SIZE(ar5416BB_RfGain_9100), 3);
		INIT_INI_ARRAY(&ahp->ah_iniBank1, ar5416Bank1_9100,
			       ARRAY_SIZE(ar5416Bank1_9100), 2);
		INIT_INI_ARRAY(&ahp->ah_iniBank2, ar5416Bank2_9100,
			       ARRAY_SIZE(ar5416Bank2_9100), 2);
		INIT_INI_ARRAY(&ahp->ah_iniBank3, ar5416Bank3_9100,
			       ARRAY_SIZE(ar5416Bank3_9100), 3);
		INIT_INI_ARRAY(&ahp->ah_iniBank6, ar5416Bank6_9100,
			       ARRAY_SIZE(ar5416Bank6_9100), 3);
		INIT_INI_ARRAY(&ahp->ah_iniBank6TPC, ar5416Bank6TPC_9100,
			       ARRAY_SIZE(ar5416Bank6TPC_9100), 3);
		INIT_INI_ARRAY(&ahp->ah_iniBank7, ar5416Bank7_9100,
			       ARRAY_SIZE(ar5416Bank7_9100), 2);
		INIT_INI_ARRAY(&ahp->ah_iniAddac, ar5416Addac_9100,
			       ARRAY_SIZE(ar5416Addac_9100), 2);
	} else {
		INIT_INI_ARRAY(&ahp->ah_iniModes, ar5416Modes,
			       ARRAY_SIZE(ar5416Modes), 6);
		INIT_INI_ARRAY(&ahp->ah_iniCommon, ar5416Common,
			       ARRAY_SIZE(ar5416Common), 2);
		INIT_INI_ARRAY(&ahp->ah_iniBank0, ar5416Bank0,
			       ARRAY_SIZE(ar5416Bank0), 2);
		INIT_INI_ARRAY(&ahp->ah_iniBB_RfGain, ar5416BB_RfGain,
			       ARRAY_SIZE(ar5416BB_RfGain), 3);
		INIT_INI_ARRAY(&ahp->ah_iniBank1, ar5416Bank1,
			       ARRAY_SIZE(ar5416Bank1), 2);
		INIT_INI_ARRAY(&ahp->ah_iniBank2, ar5416Bank2,
			       ARRAY_SIZE(ar5416Bank2), 2);
		INIT_INI_ARRAY(&ahp->ah_iniBank3, ar5416Bank3,
			       ARRAY_SIZE(ar5416Bank3), 3);
		INIT_INI_ARRAY(&ahp->ah_iniBank6, ar5416Bank6,
			       ARRAY_SIZE(ar5416Bank6), 3);
		INIT_INI_ARRAY(&ahp->ah_iniBank6TPC, ar5416Bank6TPC,
			       ARRAY_SIZE(ar5416Bank6TPC), 3);
		INIT_INI_ARRAY(&ahp->ah_iniBank7, ar5416Bank7,
			       ARRAY_SIZE(ar5416Bank7), 2);
		INIT_INI_ARRAY(&ahp->ah_iniAddac, ar5416Addac,
			       ARRAY_SIZE(ar5416Addac), 2);
	}

	if (ah->ah_isPciExpress)
		ath9k_hw_configpcipowersave(ah, 0);
	else
		ar5416DisablePciePhy(ah);

	ecode = ath9k_hw_post_attach(ah);
	if (ecode != 0)
		goto bad;

#ifndef CONFIG_SLOW_ANT_DIV
	if (ah->ah_devid == AR9280_DEVID_PCI) {
		for (i = 0; i < ahp->ah_iniModes.ia_rows; i++) {
			u32 reg = INI_RA(&ahp->ah_iniModes, i, 0);

			for (j = 1; j < ahp->ah_iniModes.ia_columns; j++) {
				u32 val = INI_RA(&ahp->ah_iniModes, i, j);

				INI_RA(&ahp->ah_iniModes, i, j) =
					ath9k_hw_ini_fixup(ah, &ahp->ah_eeprom,
							   reg, val);
			}
		}
	}
#endif

	if (!ath9k_hw_fill_cap_info(ah)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET,
			 "%s:failed ath9k_hw_fill_cap_info\n", __func__);
		ecode = -EINVAL;
		goto bad;
	}

	ecode = ath9k_hw_init_macaddr(ah);
	if (ecode != 0) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET,
			 "%s: failed initializing mac address\n",
			 __func__);
		goto bad;
	}

	if (AR_SREV_9285(ah))
		ah->ah_txTrigLevel = (AR_FTRIG_256B >> AR_FTRIG_S);
	else
		ah->ah_txTrigLevel = (AR_FTRIG_512B >> AR_FTRIG_S);

#ifndef ATH_NF_PER_CHAN

	ath9k_init_nfcal_hist_buffer(ah);
#endif

	return ah;

bad:
	if (ahp)
		ath9k_hw_detach((struct ath_hal *) ahp);
	if (status)
		*status = ecode;
	return NULL;
}

void ath9k_hw_detach(struct ath_hal *ah)
{
	if (!AR_SREV_9100(ah))
		ath9k_hw_ani_detach(ah);
	ath9k_hw_rfdetach(ah);

	ath9k_hw_setpower(ah, ATH9K_PM_FULL_SLEEP);
	kfree(ah);
}

bool ath9k_get_channel_edges(struct ath_hal *ah,
			     u16 flags, u16 *low,
			     u16 *high)
{
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;

	if (flags & CHANNEL_5GHZ) {
		*low = pCap->low_5ghz_chan;
		*high = pCap->high_5ghz_chan;
		return true;
	}
	if ((flags & CHANNEL_2GHZ)) {
		*low = pCap->low_2ghz_chan;
		*high = pCap->high_2ghz_chan;

		return true;
	}
	return false;
}

static inline bool ath9k_hw_fill_vpd_table(u8 pwrMin,
					   u8 pwrMax,
					   u8 *pPwrList,
					   u8 *pVpdList,
					   u16
					   numIntercepts,
					   u8 *pRetVpdList)
{
	u16 i, k;
	u8 currPwr = pwrMin;
	u16 idxL = 0, idxR = 0;

	for (i = 0; i <= (pwrMax - pwrMin) / 2; i++) {
		ath9k_hw_get_lower_upper_index(currPwr, pPwrList,
					       numIntercepts, &(idxL),
					       &(idxR));
		if (idxR < 1)
			idxR = 1;
		if (idxL == numIntercepts - 1)
			idxL = (u16) (numIntercepts - 2);
		if (pPwrList[idxL] == pPwrList[idxR])
			k = pVpdList[idxL];
		else
			k = (u16) (((currPwr -
					   pPwrList[idxL]) *
					  pVpdList[idxR] +
					  (pPwrList[idxR] -
					   currPwr) * pVpdList[idxL]) /
					 (pPwrList[idxR] -
					  pPwrList[idxL]));
		pRetVpdList[i] = (u8) k;
		currPwr += 2;
	}

	return true;
}

static inline void
ath9k_hw_get_gain_boundaries_pdadcs(struct ath_hal *ah,
				    struct ath9k_channel *chan,
				    struct cal_data_per_freq *pRawDataSet,
				    u8 *bChans,
				    u16 availPiers,
				    u16 tPdGainOverlap,
				    int16_t *pMinCalPower,
				    u16 *pPdGainBoundaries,
				    u8 *pPDADCValues,
				    u16 numXpdGains)
{
	int i, j, k;
	int16_t ss;
	u16 idxL = 0, idxR = 0, numPiers;
	static u8 vpdTableL[AR5416_NUM_PD_GAINS]
		[AR5416_MAX_PWR_RANGE_IN_HALF_DB];
	static u8 vpdTableR[AR5416_NUM_PD_GAINS]
		[AR5416_MAX_PWR_RANGE_IN_HALF_DB];
	static u8 vpdTableI[AR5416_NUM_PD_GAINS]
		[AR5416_MAX_PWR_RANGE_IN_HALF_DB];

	u8 *pVpdL, *pVpdR, *pPwrL, *pPwrR;
	u8 minPwrT4[AR5416_NUM_PD_GAINS];
	u8 maxPwrT4[AR5416_NUM_PD_GAINS];
	int16_t vpdStep;
	int16_t tmpVal;
	u16 sizeCurrVpdTable, maxIndex, tgtIndex;
	bool match;
	int16_t minDelta = 0;
	struct chan_centers centers;

	ath9k_hw_get_channel_centers(ah, chan, &centers);

	for (numPiers = 0; numPiers < availPiers; numPiers++) {
		if (bChans[numPiers] == AR5416_BCHAN_UNUSED)
			break;
	}

	match = ath9k_hw_get_lower_upper_index((u8)
					       FREQ2FBIN(centers.
							 synth_center,
							 IS_CHAN_2GHZ
							 (chan)), bChans,
					       numPiers, &idxL, &idxR);

	if (match) {
		for (i = 0; i < numXpdGains; i++) {
			minPwrT4[i] = pRawDataSet[idxL].pwrPdg[i][0];
			maxPwrT4[i] = pRawDataSet[idxL].pwrPdg[i][4];
			ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
						pRawDataSet[idxL].
						pwrPdg[i],
						pRawDataSet[idxL].
						vpdPdg[i],
						AR5416_PD_GAIN_ICEPTS,
						vpdTableI[i]);
		}
	} else {
		for (i = 0; i < numXpdGains; i++) {
			pVpdL = pRawDataSet[idxL].vpdPdg[i];
			pPwrL = pRawDataSet[idxL].pwrPdg[i];
			pVpdR = pRawDataSet[idxR].vpdPdg[i];
			pPwrR = pRawDataSet[idxR].pwrPdg[i];

			minPwrT4[i] = max(pPwrL[0], pPwrR[0]);

			maxPwrT4[i] =
				min(pPwrL[AR5416_PD_GAIN_ICEPTS - 1],
				    pPwrR[AR5416_PD_GAIN_ICEPTS - 1]);


			ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
						pPwrL, pVpdL,
						AR5416_PD_GAIN_ICEPTS,
						vpdTableL[i]);
			ath9k_hw_fill_vpd_table(minPwrT4[i], maxPwrT4[i],
						pPwrR, pVpdR,
						AR5416_PD_GAIN_ICEPTS,
						vpdTableR[i]);

			for (j = 0; j <= (maxPwrT4[i] - minPwrT4[i]) / 2; j++) {
				vpdTableI[i][j] =
					(u8) (ath9k_hw_interpolate
						    ((u16)
						     FREQ2FBIN(centers.
							       synth_center,
							       IS_CHAN_2GHZ
							       (chan)),
						     bChans[idxL],
						     bChans[idxR], vpdTableL[i]
						     [j], vpdTableR[i]
						     [j]));
			}
		}
	}

	*pMinCalPower = (int16_t) (minPwrT4[0] / 2);

	k = 0;
	for (i = 0; i < numXpdGains; i++) {
		if (i == (numXpdGains - 1))
			pPdGainBoundaries[i] =
				(u16) (maxPwrT4[i] / 2);
		else
			pPdGainBoundaries[i] =
				(u16) ((maxPwrT4[i] +
					      minPwrT4[i + 1]) / 4);

		pPdGainBoundaries[i] =
			min((u16) AR5416_MAX_RATE_POWER,
			    pPdGainBoundaries[i]);

		if ((i == 0) && !AR_SREV_5416_V20_OR_LATER(ah)) {
			minDelta = pPdGainBoundaries[0] - 23;
			pPdGainBoundaries[0] = 23;
		} else {
			minDelta = 0;
		}

		if (i == 0) {
			if (AR_SREV_9280_10_OR_LATER(ah))
				ss = (int16_t) (0 - (minPwrT4[i] / 2));
			else
				ss = 0;
		} else {
			ss = (int16_t) ((pPdGainBoundaries[i - 1] -
					 (minPwrT4[i] / 2)) -
					tPdGainOverlap + 1 + minDelta);
		}
		vpdStep = (int16_t) (vpdTableI[i][1] - vpdTableI[i][0]);
		vpdStep = (int16_t) ((vpdStep < 1) ? 1 : vpdStep);

		while ((ss < 0) && (k < (AR5416_NUM_PDADC_VALUES - 1))) {
			tmpVal = (int16_t) (vpdTableI[i][0] + ss * vpdStep);
			pPDADCValues[k++] =
				(u8) ((tmpVal < 0) ? 0 : tmpVal);
			ss++;
		}

		sizeCurrVpdTable =
			(u8) ((maxPwrT4[i] - minPwrT4[i]) / 2 + 1);
		tgtIndex = (u8) (pPdGainBoundaries[i] + tPdGainOverlap -
				       (minPwrT4[i] / 2));
		maxIndex = (tgtIndex <
			    sizeCurrVpdTable) ? tgtIndex : sizeCurrVpdTable;

		while ((ss < maxIndex)
		       && (k < (AR5416_NUM_PDADC_VALUES - 1))) {
			pPDADCValues[k++] = vpdTableI[i][ss++];
		}

		vpdStep = (int16_t) (vpdTableI[i][sizeCurrVpdTable - 1] -
				     vpdTableI[i][sizeCurrVpdTable - 2]);
		vpdStep = (int16_t) ((vpdStep < 1) ? 1 : vpdStep);

		if (tgtIndex > maxIndex) {
			while ((ss <= tgtIndex)
			       && (k < (AR5416_NUM_PDADC_VALUES - 1))) {
				tmpVal = (int16_t) ((vpdTableI[i]
						     [sizeCurrVpdTable -
						      1] + (ss - maxIndex +
							    1) * vpdStep));
				pPDADCValues[k++] = (u8) ((tmpVal >
						 255) ? 255 : tmpVal);
				ss++;
			}
		}
	}

	while (i < AR5416_PD_GAINS_IN_MASK) {
		pPdGainBoundaries[i] = pPdGainBoundaries[i - 1];
		i++;
	}

	while (k < AR5416_NUM_PDADC_VALUES) {
		pPDADCValues[k] = pPDADCValues[k - 1];
		k++;
	}
	return;
}

static inline bool
ath9k_hw_set_power_cal_table(struct ath_hal *ah,
			     struct ar5416_eeprom *pEepData,
			     struct ath9k_channel *chan,
			     int16_t *pTxPowerIndexOffset)
{
	struct cal_data_per_freq *pRawDataset;
	u8 *pCalBChans = NULL;
	u16 pdGainOverlap_t2;
	static u8 pdadcValues[AR5416_NUM_PDADC_VALUES];
	u16 gainBoundaries[AR5416_PD_GAINS_IN_MASK];
	u16 numPiers, i, j;
	int16_t tMinCalPower;
	u16 numXpdGain, xpdMask;
	u16 xpdGainValues[AR5416_NUM_PD_GAINS] = { 0, 0, 0, 0 };
	u32 reg32, regOffset, regChainOffset;
	int16_t modalIdx;
	struct ath_hal_5416 *ahp = AH5416(ah);

	modalIdx = IS_CHAN_2GHZ(chan) ? 1 : 0;
	xpdMask = pEepData->modalHeader[modalIdx].xpdGain;

	if ((pEepData->baseEepHeader.
	     version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_2) {
		pdGainOverlap_t2 =
			pEepData->modalHeader[modalIdx].pdGainOverlap;
	} else {
		pdGainOverlap_t2 =
			(u16) (MS
				     (REG_READ(ah, AR_PHY_TPCRG5),
				      AR_PHY_TPCRG5_PD_GAIN_OVERLAP));
	}

	if (IS_CHAN_2GHZ(chan)) {
		pCalBChans = pEepData->calFreqPier2G;
		numPiers = AR5416_NUM_2G_CAL_PIERS;
	} else {
		pCalBChans = pEepData->calFreqPier5G;
		numPiers = AR5416_NUM_5G_CAL_PIERS;
	}

	numXpdGain = 0;

	for (i = 1; i <= AR5416_PD_GAINS_IN_MASK; i++) {
		if ((xpdMask >> (AR5416_PD_GAINS_IN_MASK - i)) & 1) {
			if (numXpdGain >= AR5416_NUM_PD_GAINS)
				break;
			xpdGainValues[numXpdGain] =
				(u16) (AR5416_PD_GAINS_IN_MASK - i);
			numXpdGain++;
		}
	}

	REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_NUM_PD_GAIN,
		      (numXpdGain - 1) & 0x3);
	REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_1,
		      xpdGainValues[0]);
	REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_2,
		      xpdGainValues[1]);
	REG_RMW_FIELD(ah, AR_PHY_TPCRG1, AR_PHY_TPCRG1_PD_GAIN_3,
		      xpdGainValues[2]);

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		if (AR_SREV_5416_V20_OR_LATER(ah) &&
		    (ahp->ah_rxchainmask == 5 || ahp->ah_txchainmask == 5)
		    && (i != 0)) {
			regChainOffset = (i == 1) ? 0x2000 : 0x1000;
		} else
			regChainOffset = i * 0x1000;
		if (pEepData->baseEepHeader.txMask & (1 << i)) {
			if (IS_CHAN_2GHZ(chan))
				pRawDataset = pEepData->calPierData2G[i];
			else
				pRawDataset = pEepData->calPierData5G[i];

			ath9k_hw_get_gain_boundaries_pdadcs(ah, chan,
							    pRawDataset,
							    pCalBChans,
							    numPiers,
							    pdGainOverlap_t2,
							    &tMinCalPower,
							    gainBoundaries,
							    pdadcValues,
							    numXpdGain);

			if ((i == 0) || AR_SREV_5416_V20_OR_LATER(ah)) {

				REG_WRITE(ah,
					  AR_PHY_TPCRG5 + regChainOffset,
					  SM(pdGainOverlap_t2,
					     AR_PHY_TPCRG5_PD_GAIN_OVERLAP)
					  | SM(gainBoundaries[0],
					       AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1)
					  | SM(gainBoundaries[1],
					       AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2)
					  | SM(gainBoundaries[2],
					       AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3)
					  | SM(gainBoundaries[3],
				       AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4));
			}

			regOffset =
				AR_PHY_BASE + (672 << 2) + regChainOffset;
			for (j = 0; j < 32; j++) {
				reg32 =
					((pdadcValues[4 * j + 0] & 0xFF) << 0)
					| ((pdadcValues[4 * j + 1] & 0xFF) <<
					   8) | ((pdadcValues[4 * j + 2] &
						  0xFF) << 16) |
					((pdadcValues[4 * j + 3] & 0xFF) <<
					 24);
				REG_WRITE(ah, regOffset, reg32);

				DPRINTF(ah->ah_sc, ATH_DBG_PHY_IO,
					 "PDADC (%d,%4x): %4.4x %8.8x\n",
					 i, regChainOffset, regOffset,
					 reg32);
				DPRINTF(ah->ah_sc, ATH_DBG_PHY_IO,
				"PDADC: Chain %d | PDADC %3d Value %3d | "
				"PDADC %3d Value %3d | PDADC %3d Value %3d | "
				"PDADC %3d Value %3d |\n",
					 i, 4 * j, pdadcValues[4 * j],
					 4 * j + 1, pdadcValues[4 * j + 1],
					 4 * j + 2, pdadcValues[4 * j + 2],
					 4 * j + 3,
					 pdadcValues[4 * j + 3]);

				regOffset += 4;
			}
		}
	}
	*pTxPowerIndexOffset = 0;

	return true;
}

void ath9k_hw_configpcipowersave(struct ath_hal *ah, int restore)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	u8 i;

	if (ah->ah_isPciExpress != true)
		return;

	if (ah->ah_config.pcie_powersave_enable == 2)
		return;

	if (restore)
		return;

	if (AR_SREV_9280_20_OR_LATER(ah)) {
		for (i = 0; i < ahp->ah_iniPcieSerdes.ia_rows; i++) {
			REG_WRITE(ah, INI_RA(&ahp->ah_iniPcieSerdes, i, 0),
				  INI_RA(&ahp->ah_iniPcieSerdes, i, 1));
		}
		udelay(1000);
	} else if (AR_SREV_9280(ah)
		   && (ah->ah_macRev == AR_SREV_REVISION_9280_10)) {
		REG_WRITE(ah, AR_PCIE_SERDES, 0x9248fd00);
		REG_WRITE(ah, AR_PCIE_SERDES, 0x24924924);

		REG_WRITE(ah, AR_PCIE_SERDES, 0xa8000019);
		REG_WRITE(ah, AR_PCIE_SERDES, 0x13160820);
		REG_WRITE(ah, AR_PCIE_SERDES, 0xe5980560);

		if (ah->ah_config.pcie_clock_req)
			REG_WRITE(ah, AR_PCIE_SERDES, 0x401deffc);
		else
			REG_WRITE(ah, AR_PCIE_SERDES, 0x401deffd);

		REG_WRITE(ah, AR_PCIE_SERDES, 0x1aaabe40);
		REG_WRITE(ah, AR_PCIE_SERDES, 0xbe105554);
		REG_WRITE(ah, AR_PCIE_SERDES, 0x00043007);

		REG_WRITE(ah, AR_PCIE_SERDES2, 0x00000000);

		udelay(1000);
	} else {
		REG_WRITE(ah, AR_PCIE_SERDES, 0x9248fc00);
		REG_WRITE(ah, AR_PCIE_SERDES, 0x24924924);
		REG_WRITE(ah, AR_PCIE_SERDES, 0x28000039);
		REG_WRITE(ah, AR_PCIE_SERDES, 0x53160824);
		REG_WRITE(ah, AR_PCIE_SERDES, 0xe5980579);
		REG_WRITE(ah, AR_PCIE_SERDES, 0x001defff);
		REG_WRITE(ah, AR_PCIE_SERDES, 0x1aaabe40);
		REG_WRITE(ah, AR_PCIE_SERDES, 0xbe105554);
		REG_WRITE(ah, AR_PCIE_SERDES, 0x000e3007);
		REG_WRITE(ah, AR_PCIE_SERDES2, 0x00000000);
	}

	REG_SET_BIT(ah, AR_PCIE_PM_CTRL, AR_PCIE_PM_CTRL_ENA);

	if (ah->ah_config.pcie_waen) {
		REG_WRITE(ah, AR_WA, ah->ah_config.pcie_waen);
	} else {
		if (AR_SREV_9280(ah))
			REG_WRITE(ah, AR_WA, 0x0040073f);
		else
			REG_WRITE(ah, AR_WA, 0x0000073f);
	}
}

static inline void
ath9k_hw_get_legacy_target_powers(struct ath_hal *ah,
				  struct ath9k_channel *chan,
				  struct cal_target_power_leg *powInfo,
				  u16 numChannels,
				  struct cal_target_power_leg *pNewPower,
				  u16 numRates,
				  bool isExtTarget)
{
	u16 clo, chi;
	int i;
	int matchIndex = -1, lowIndex = -1;
	u16 freq;
	struct chan_centers centers;

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = (isExtTarget) ? centers.ext_center : centers.ctl_center;

	if (freq <= ath9k_hw_fbin2freq(powInfo[0].bChannel,
		IS_CHAN_2GHZ(chan))) {
		matchIndex = 0;
	} else {
		for (i = 0; (i < numChannels)
		     && (powInfo[i].bChannel != AR5416_BCHAN_UNUSED); i++) {
			if (freq ==
			    ath9k_hw_fbin2freq(powInfo[i].bChannel,
					       IS_CHAN_2GHZ(chan))) {
				matchIndex = i;
				break;
			} else if ((freq <
				    ath9k_hw_fbin2freq(powInfo[i].bChannel,
						       IS_CHAN_2GHZ(chan)))
				   && (freq >
				       ath9k_hw_fbin2freq(powInfo[i - 1].
							  bChannel,
							  IS_CHAN_2GHZ
							  (chan)))) {
				lowIndex = i - 1;
				break;
			}
		}
		if ((matchIndex == -1) && (lowIndex == -1))
			matchIndex = i - 1;
	}

	if (matchIndex != -1) {
		*pNewPower = powInfo[matchIndex];
	} else {
		clo = ath9k_hw_fbin2freq(powInfo[lowIndex].bChannel,
					 IS_CHAN_2GHZ(chan));
		chi = ath9k_hw_fbin2freq(powInfo[lowIndex + 1].bChannel,
					 IS_CHAN_2GHZ(chan));

		for (i = 0; i < numRates; i++) {
			pNewPower->tPow2x[i] =
				(u8) ath9k_hw_interpolate(freq, clo, chi,
								powInfo
								[lowIndex].
								tPow2x[i],
								powInfo
								[lowIndex +
								 1].tPow2x[i]);
		}
	}
}

static inline void
ath9k_hw_get_target_powers(struct ath_hal *ah,
			   struct ath9k_channel *chan,
			   struct cal_target_power_ht *powInfo,
			   u16 numChannels,
			   struct cal_target_power_ht *pNewPower,
			   u16 numRates,
			   bool isHt40Target)
{
	u16 clo, chi;
	int i;
	int matchIndex = -1, lowIndex = -1;
	u16 freq;
	struct chan_centers centers;

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = isHt40Target ? centers.synth_center : centers.ctl_center;

	if (freq <=
		ath9k_hw_fbin2freq(powInfo[0].bChannel, IS_CHAN_2GHZ(chan))) {
		matchIndex = 0;
	} else {
		for (i = 0; (i < numChannels)
		     && (powInfo[i].bChannel != AR5416_BCHAN_UNUSED); i++) {
			if (freq ==
			    ath9k_hw_fbin2freq(powInfo[i].bChannel,
					       IS_CHAN_2GHZ(chan))) {
				matchIndex = i;
				break;
			} else
				if ((freq <
				     ath9k_hw_fbin2freq(powInfo[i].bChannel,
							IS_CHAN_2GHZ(chan)))
				    && (freq >
					ath9k_hw_fbin2freq(powInfo[i - 1].
							   bChannel,
							   IS_CHAN_2GHZ
							   (chan)))) {
					lowIndex = i - 1;
					break;
				}
		}
		if ((matchIndex == -1) && (lowIndex == -1))
			matchIndex = i - 1;
	}

	if (matchIndex != -1) {
		*pNewPower = powInfo[matchIndex];
	} else {
		clo = ath9k_hw_fbin2freq(powInfo[lowIndex].bChannel,
					 IS_CHAN_2GHZ(chan));
		chi = ath9k_hw_fbin2freq(powInfo[lowIndex + 1].bChannel,
					 IS_CHAN_2GHZ(chan));

		for (i = 0; i < numRates; i++) {
			pNewPower->tPow2x[i] =
				(u8) ath9k_hw_interpolate(freq, clo, chi,
								powInfo
								[lowIndex].
								tPow2x[i],
								powInfo
								[lowIndex +
								 1].tPow2x[i]);
		}
	}
}

static inline u16
ath9k_hw_get_max_edge_power(u16 freq,
			    struct cal_ctl_edges *pRdEdgesPower,
			    bool is2GHz)
{
	u16 twiceMaxEdgePower = AR5416_MAX_RATE_POWER;
	int i;

	for (i = 0; (i < AR5416_NUM_BAND_EDGES)
	     && (pRdEdgesPower[i].bChannel != AR5416_BCHAN_UNUSED); i++) {
		if (freq == ath9k_hw_fbin2freq(pRdEdgesPower[i].bChannel,
					       is2GHz)) {
			twiceMaxEdgePower = pRdEdgesPower[i].tPower;
			break;
		} else if ((i > 0)
			   && (freq <
			       ath9k_hw_fbin2freq(pRdEdgesPower[i].
						  bChannel, is2GHz))) {
			if (ath9k_hw_fbin2freq
			    (pRdEdgesPower[i - 1].bChannel, is2GHz) < freq
			    && pRdEdgesPower[i - 1].flag) {
				twiceMaxEdgePower =
					pRdEdgesPower[i - 1].tPower;
			}
			break;
		}
	}
	return twiceMaxEdgePower;
}

static inline bool
ath9k_hw_set_power_per_rate_table(struct ath_hal *ah,
				  struct ar5416_eeprom *pEepData,
				  struct ath9k_channel *chan,
				  int16_t *ratesArray,
				  u16 cfgCtl,
				  u8 AntennaReduction,
				  u8 twiceMaxRegulatoryPower,
				  u8 powerLimit)
{
	u8 twiceMaxEdgePower = AR5416_MAX_RATE_POWER;
	static const u16 tpScaleReductionTable[5] =
		{ 0, 3, 6, 9, AR5416_MAX_RATE_POWER };

	int i;
	int8_t twiceLargestAntenna;
	struct cal_ctl_data *rep;
	struct cal_target_power_leg targetPowerOfdm, targetPowerCck = {
		0, { 0, 0, 0, 0}
	};
	struct cal_target_power_leg targetPowerOfdmExt = {
		0, { 0, 0, 0, 0} }, targetPowerCckExt = {
		0, { 0, 0, 0, 0 }
	};
	struct cal_target_power_ht targetPowerHt20, targetPowerHt40 = {
		0, {0, 0, 0, 0}
	};
	u8 scaledPower = 0, minCtlPower, maxRegAllowedPower;
	u16 ctlModesFor11a[] =
		{ CTL_11A, CTL_5GHT20, CTL_11A_EXT, CTL_5GHT40 };
	u16 ctlModesFor11g[] =
		{ CTL_11B, CTL_11G, CTL_2GHT20, CTL_11B_EXT, CTL_11G_EXT,
		  CTL_2GHT40
		};
	u16 numCtlModes, *pCtlMode, ctlMode, freq;
	struct chan_centers centers;
	int tx_chainmask;
	u8 twiceMinEdgePower;
	struct ath_hal_5416 *ahp = AH5416(ah);

	tx_chainmask = ahp->ah_txchainmask;

	ath9k_hw_get_channel_centers(ah, chan, &centers);

	twiceLargestAntenna = max(
		pEepData->modalHeader
			[IS_CHAN_2GHZ(chan)].antennaGainCh[0],
		pEepData->modalHeader
			[IS_CHAN_2GHZ(chan)].antennaGainCh[1]);

	twiceLargestAntenna = max((u8) twiceLargestAntenna,
		pEepData->modalHeader
			[IS_CHAN_2GHZ(chan)].antennaGainCh[2]);

	twiceLargestAntenna =
		(int8_t) min(AntennaReduction - twiceLargestAntenna, 0);

	maxRegAllowedPower = twiceMaxRegulatoryPower + twiceLargestAntenna;

	if (ah->ah_tpScale != ATH9K_TP_SCALE_MAX) {
		maxRegAllowedPower -=
			(tpScaleReductionTable[(ah->ah_tpScale)] * 2);
	}

	scaledPower = min(powerLimit, maxRegAllowedPower);

	switch (ar5416_get_ntxchains(tx_chainmask)) {
	case 1:
		break;
	case 2:
		scaledPower -=
			pEepData->modalHeader[IS_CHAN_2GHZ(chan)].
			pwrDecreaseFor2Chain;
		break;
	case 3:
		scaledPower -=
			pEepData->modalHeader[IS_CHAN_2GHZ(chan)].
			pwrDecreaseFor3Chain;
		break;
	}

	scaledPower = max(0, (int32_t) scaledPower);

	if (IS_CHAN_2GHZ(chan)) {
		numCtlModes =
			ARRAY_SIZE(ctlModesFor11g) -
			SUB_NUM_CTL_MODES_AT_2G_40;
		pCtlMode = ctlModesFor11g;

		ath9k_hw_get_legacy_target_powers(ah, chan,
			pEepData->
			calTargetPowerCck,
			AR5416_NUM_2G_CCK_TARGET_POWERS,
			&targetPowerCck, 4,
			false);
		ath9k_hw_get_legacy_target_powers(ah, chan,
			pEepData->
			calTargetPower2G,
			AR5416_NUM_2G_20_TARGET_POWERS,
			&targetPowerOfdm, 4,
			false);
		ath9k_hw_get_target_powers(ah, chan,
			pEepData->calTargetPower2GHT20,
			AR5416_NUM_2G_20_TARGET_POWERS,
			&targetPowerHt20, 8, false);

		if (IS_CHAN_HT40(chan)) {
			numCtlModes = ARRAY_SIZE(ctlModesFor11g);
			ath9k_hw_get_target_powers(ah, chan,
				pEepData->
				calTargetPower2GHT40,
				AR5416_NUM_2G_40_TARGET_POWERS,
				&targetPowerHt40, 8,
				true);
			ath9k_hw_get_legacy_target_powers(ah, chan,
				pEepData->
				calTargetPowerCck,
				AR5416_NUM_2G_CCK_TARGET_POWERS,
				&targetPowerCckExt,
				4, true);
			ath9k_hw_get_legacy_target_powers(ah, chan,
				pEepData->
				calTargetPower2G,
				AR5416_NUM_2G_20_TARGET_POWERS,
				&targetPowerOfdmExt,
				4, true);
		}
	} else {

		numCtlModes =
			ARRAY_SIZE(ctlModesFor11a) -
			SUB_NUM_CTL_MODES_AT_5G_40;
		pCtlMode = ctlModesFor11a;

		ath9k_hw_get_legacy_target_powers(ah, chan,
			pEepData->
			calTargetPower5G,
			AR5416_NUM_5G_20_TARGET_POWERS,
			&targetPowerOfdm, 4,
			false);
		ath9k_hw_get_target_powers(ah, chan,
			pEepData->calTargetPower5GHT20,
			AR5416_NUM_5G_20_TARGET_POWERS,
			&targetPowerHt20, 8, false);

		if (IS_CHAN_HT40(chan)) {
			numCtlModes = ARRAY_SIZE(ctlModesFor11a);
			ath9k_hw_get_target_powers(ah, chan,
				pEepData->
				calTargetPower5GHT40,
				AR5416_NUM_5G_40_TARGET_POWERS,
				&targetPowerHt40, 8,
				true);
			ath9k_hw_get_legacy_target_powers(ah, chan,
				pEepData->
				calTargetPower5G,
				AR5416_NUM_5G_20_TARGET_POWERS,
				&targetPowerOfdmExt,
				4, true);
		}
	}

	for (ctlMode = 0; ctlMode < numCtlModes; ctlMode++) {
		bool isHt40CtlMode =
			(pCtlMode[ctlMode] == CTL_5GHT40)
			|| (pCtlMode[ctlMode] == CTL_2GHT40);
		if (isHt40CtlMode)
			freq = centers.synth_center;
		else if (pCtlMode[ctlMode] & EXT_ADDITIVE)
			freq = centers.ext_center;
		else
			freq = centers.ctl_center;

		if (ar5416_get_eep_ver(ahp) == 14
		    && ar5416_get_eep_rev(ahp) <= 2)
			twiceMaxEdgePower = AR5416_MAX_RATE_POWER;

		DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT,
			"LOOP-Mode ctlMode %d < %d, isHt40CtlMode %d, "
			"EXT_ADDITIVE %d\n",
			 ctlMode, numCtlModes, isHt40CtlMode,
			 (pCtlMode[ctlMode] & EXT_ADDITIVE));

		for (i = 0; (i < AR5416_NUM_CTLS) && pEepData->ctlIndex[i];
		     i++) {
			DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT,
				"  LOOP-Ctlidx %d: cfgCtl 0x%2.2x "
				"pCtlMode 0x%2.2x ctlIndex 0x%2.2x "
				"chan %d\n",
				i, cfgCtl, pCtlMode[ctlMode],
				pEepData->ctlIndex[i], chan->channel);

			if ((((cfgCtl & ~CTL_MODE_M) |
			      (pCtlMode[ctlMode] & CTL_MODE_M)) ==
			     pEepData->ctlIndex[i])
			    ||
			    (((cfgCtl & ~CTL_MODE_M) |
			      (pCtlMode[ctlMode] & CTL_MODE_M)) ==
			     ((pEepData->
			       ctlIndex[i] & CTL_MODE_M) | SD_NO_CTL))) {
				rep = &(pEepData->ctlData[i]);

				twiceMinEdgePower =
					ath9k_hw_get_max_edge_power(freq,
						rep->
						ctlEdges
						[ar5416_get_ntxchains
						(tx_chainmask)
						- 1],
						IS_CHAN_2GHZ
						(chan));

				DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT,
					"    MATCH-EE_IDX %d: ch %d is2 %d "
					"2xMinEdge %d chainmask %d chains %d\n",
					 i, freq, IS_CHAN_2GHZ(chan),
					 twiceMinEdgePower, tx_chainmask,
					 ar5416_get_ntxchains
					 (tx_chainmask));
				if ((cfgCtl & ~CTL_MODE_M) == SD_NO_CTL) {
					twiceMaxEdgePower =
						min(twiceMaxEdgePower,
						    twiceMinEdgePower);
				} else {
					twiceMaxEdgePower =
						twiceMinEdgePower;
					break;
				}
			}
		}

		minCtlPower = min(twiceMaxEdgePower, scaledPower);

		DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT,
				"    SEL-Min ctlMode %d pCtlMode %d "
				"2xMaxEdge %d sP %d minCtlPwr %d\n",
			 ctlMode, pCtlMode[ctlMode], twiceMaxEdgePower,
			 scaledPower, minCtlPower);

		switch (pCtlMode[ctlMode]) {
		case CTL_11B:
			for (i = 0; i < ARRAY_SIZE(targetPowerCck.tPow2x);
			     i++) {
				targetPowerCck.tPow2x[i] =
					min(targetPowerCck.tPow2x[i],
					    minCtlPower);
			}
			break;
		case CTL_11A:
		case CTL_11G:
			for (i = 0; i < ARRAY_SIZE(targetPowerOfdm.tPow2x);
			     i++) {
				targetPowerOfdm.tPow2x[i] =
					min(targetPowerOfdm.tPow2x[i],
					    minCtlPower);
			}
			break;
		case CTL_5GHT20:
		case CTL_2GHT20:
			for (i = 0; i < ARRAY_SIZE(targetPowerHt20.tPow2x);
			     i++) {
				targetPowerHt20.tPow2x[i] =
					min(targetPowerHt20.tPow2x[i],
					    minCtlPower);
			}
			break;
		case CTL_11B_EXT:
			targetPowerCckExt.tPow2x[0] =
				min(targetPowerCckExt.tPow2x[0], minCtlPower);
			break;
		case CTL_11A_EXT:
		case CTL_11G_EXT:
			targetPowerOfdmExt.tPow2x[0] =
				min(targetPowerOfdmExt.tPow2x[0], minCtlPower);
			break;
		case CTL_5GHT40:
		case CTL_2GHT40:
			for (i = 0; i < ARRAY_SIZE(targetPowerHt40.tPow2x);
			     i++) {
				targetPowerHt40.tPow2x[i] =
					min(targetPowerHt40.tPow2x[i],
					    minCtlPower);
			}
			break;
		default:
			break;
		}
	}

	ratesArray[rate6mb] = ratesArray[rate9mb] = ratesArray[rate12mb] =
		ratesArray[rate18mb] = ratesArray[rate24mb] =
		targetPowerOfdm.tPow2x[0];
	ratesArray[rate36mb] = targetPowerOfdm.tPow2x[1];
	ratesArray[rate48mb] = targetPowerOfdm.tPow2x[2];
	ratesArray[rate54mb] = targetPowerOfdm.tPow2x[3];
	ratesArray[rateXr] = targetPowerOfdm.tPow2x[0];

	for (i = 0; i < ARRAY_SIZE(targetPowerHt20.tPow2x); i++)
		ratesArray[rateHt20_0 + i] = targetPowerHt20.tPow2x[i];

	if (IS_CHAN_2GHZ(chan)) {
		ratesArray[rate1l] = targetPowerCck.tPow2x[0];
		ratesArray[rate2s] = ratesArray[rate2l] =
			targetPowerCck.tPow2x[1];
		ratesArray[rate5_5s] = ratesArray[rate5_5l] =
			targetPowerCck.tPow2x[2];
		;
		ratesArray[rate11s] = ratesArray[rate11l] =
			targetPowerCck.tPow2x[3];
		;
	}
	if (IS_CHAN_HT40(chan)) {
		for (i = 0; i < ARRAY_SIZE(targetPowerHt40.tPow2x); i++) {
			ratesArray[rateHt40_0 + i] =
				targetPowerHt40.tPow2x[i];
		}
		ratesArray[rateDupOfdm] = targetPowerHt40.tPow2x[0];
		ratesArray[rateDupCck] = targetPowerHt40.tPow2x[0];
		ratesArray[rateExtOfdm] = targetPowerOfdmExt.tPow2x[0];
		if (IS_CHAN_2GHZ(chan)) {
			ratesArray[rateExtCck] =
				targetPowerCckExt.tPow2x[0];
		}
	}
	return true;
}

static int
ath9k_hw_set_txpower(struct ath_hal *ah,
		     struct ar5416_eeprom *pEepData,
		     struct ath9k_channel *chan,
		     u16 cfgCtl,
		     u8 twiceAntennaReduction,
		     u8 twiceMaxRegulatoryPower,
		     u8 powerLimit)
{
	struct modal_eep_header *pModal =
		&(pEepData->modalHeader[IS_CHAN_2GHZ(chan)]);
	int16_t ratesArray[Ar5416RateSize];
	int16_t txPowerIndexOffset = 0;
	u8 ht40PowerIncForPdadc = 2;
	int i;

	memset(ratesArray, 0, sizeof(ratesArray));

	if ((pEepData->baseEepHeader.
	     version & AR5416_EEP_VER_MINOR_MASK) >=
	    AR5416_EEP_MINOR_VER_2) {
		ht40PowerIncForPdadc = pModal->ht40PowerIncForPdadc;
	}

	if (!ath9k_hw_set_power_per_rate_table(ah, pEepData, chan,
					       &ratesArray[0], cfgCtl,
					       twiceAntennaReduction,
					       twiceMaxRegulatoryPower,
					       powerLimit)) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			"ath9k_hw_set_txpower: unable to set "
			"tx power per rate table\n");
		return -EIO;
	}

	if (!ath9k_hw_set_power_cal_table
	    (ah, pEepData, chan, &txPowerIndexOffset)) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			 "ath9k_hw_set_txpower: unable to set power table\n");
		return -EIO;
	}

	for (i = 0; i < ARRAY_SIZE(ratesArray); i++) {
		ratesArray[i] =
			(int16_t) (txPowerIndexOffset + ratesArray[i]);
		if (ratesArray[i] > AR5416_MAX_RATE_POWER)
			ratesArray[i] = AR5416_MAX_RATE_POWER;
	}

	if (AR_SREV_9280_10_OR_LATER(ah)) {
		for (i = 0; i < Ar5416RateSize; i++)
			ratesArray[i] -= AR5416_PWR_TABLE_OFFSET * 2;
	}

	REG_WRITE(ah, AR_PHY_POWER_TX_RATE1,
		  ATH9K_POW_SM(ratesArray[rate18mb], 24)
		  | ATH9K_POW_SM(ratesArray[rate12mb], 16)
		  | ATH9K_POW_SM(ratesArray[rate9mb], 8)
		  | ATH9K_POW_SM(ratesArray[rate6mb], 0)
		);
	REG_WRITE(ah, AR_PHY_POWER_TX_RATE2,
		  ATH9K_POW_SM(ratesArray[rate54mb], 24)
		  | ATH9K_POW_SM(ratesArray[rate48mb], 16)
		  | ATH9K_POW_SM(ratesArray[rate36mb], 8)
		  | ATH9K_POW_SM(ratesArray[rate24mb], 0)
		);

	if (IS_CHAN_2GHZ(chan)) {
		REG_WRITE(ah, AR_PHY_POWER_TX_RATE3,
			  ATH9K_POW_SM(ratesArray[rate2s], 24)
			  | ATH9K_POW_SM(ratesArray[rate2l], 16)
			  | ATH9K_POW_SM(ratesArray[rateXr], 8)
			  | ATH9K_POW_SM(ratesArray[rate1l], 0)
			);
		REG_WRITE(ah, AR_PHY_POWER_TX_RATE4,
			  ATH9K_POW_SM(ratesArray[rate11s], 24)
			  | ATH9K_POW_SM(ratesArray[rate11l], 16)
			  | ATH9K_POW_SM(ratesArray[rate5_5s], 8)
			  | ATH9K_POW_SM(ratesArray[rate5_5l], 0)
			);
	}

	REG_WRITE(ah, AR_PHY_POWER_TX_RATE5,
		  ATH9K_POW_SM(ratesArray[rateHt20_3], 24)
		  | ATH9K_POW_SM(ratesArray[rateHt20_2], 16)
		  | ATH9K_POW_SM(ratesArray[rateHt20_1], 8)
		  | ATH9K_POW_SM(ratesArray[rateHt20_0], 0)
		);
	REG_WRITE(ah, AR_PHY_POWER_TX_RATE6,
		  ATH9K_POW_SM(ratesArray[rateHt20_7], 24)
		  | ATH9K_POW_SM(ratesArray[rateHt20_6], 16)
		  | ATH9K_POW_SM(ratesArray[rateHt20_5], 8)
		  | ATH9K_POW_SM(ratesArray[rateHt20_4], 0)
		);

	if (IS_CHAN_HT40(chan)) {
		REG_WRITE(ah, AR_PHY_POWER_TX_RATE7,
			  ATH9K_POW_SM(ratesArray[rateHt40_3] +
				       ht40PowerIncForPdadc, 24)
			  | ATH9K_POW_SM(ratesArray[rateHt40_2] +
					 ht40PowerIncForPdadc, 16)
			  | ATH9K_POW_SM(ratesArray[rateHt40_1] +
					 ht40PowerIncForPdadc, 8)
			  | ATH9K_POW_SM(ratesArray[rateHt40_0] +
					 ht40PowerIncForPdadc, 0)
			);
		REG_WRITE(ah, AR_PHY_POWER_TX_RATE8,
			  ATH9K_POW_SM(ratesArray[rateHt40_7] +
				       ht40PowerIncForPdadc, 24)
			  | ATH9K_POW_SM(ratesArray[rateHt40_6] +
					 ht40PowerIncForPdadc, 16)
			  | ATH9K_POW_SM(ratesArray[rateHt40_5] +
					 ht40PowerIncForPdadc, 8)
			  | ATH9K_POW_SM(ratesArray[rateHt40_4] +
					 ht40PowerIncForPdadc, 0)
			);

		REG_WRITE(ah, AR_PHY_POWER_TX_RATE9,
			  ATH9K_POW_SM(ratesArray[rateExtOfdm], 24)
			  | ATH9K_POW_SM(ratesArray[rateExtCck], 16)
			  | ATH9K_POW_SM(ratesArray[rateDupOfdm], 8)
			  | ATH9K_POW_SM(ratesArray[rateDupCck], 0)
			);
	}

	REG_WRITE(ah, AR_PHY_POWER_TX_SUB,
		  ATH9K_POW_SM(pModal->pwrDecreaseFor3Chain, 6)
		  | ATH9K_POW_SM(pModal->pwrDecreaseFor2Chain, 0)
		);

	i = rate6mb;
	if (IS_CHAN_HT40(chan))
		i = rateHt40_0;
	else if (IS_CHAN_HT20(chan))
		i = rateHt20_0;

	if (AR_SREV_9280_10_OR_LATER(ah))
		ah->ah_maxPowerLevel =
			ratesArray[i] + AR5416_PWR_TABLE_OFFSET * 2;
	else
		ah->ah_maxPowerLevel = ratesArray[i];

	return 0;
}

static inline void ath9k_hw_get_delta_slope_vals(struct ath_hal *ah,
						 u32 coef_scaled,
						 u32 *coef_mantissa,
						 u32 *coef_exponent)
{
	u32 coef_exp, coef_man;

	for (coef_exp = 31; coef_exp > 0; coef_exp--)
		if ((coef_scaled >> coef_exp) & 0x1)
			break;

	coef_exp = 14 - (coef_exp - COEF_SCALE_S);

	coef_man = coef_scaled + (1 << (COEF_SCALE_S - coef_exp - 1));

	*coef_mantissa = coef_man >> (COEF_SCALE_S - coef_exp);
	*coef_exponent = coef_exp - 16;
}

static void
ath9k_hw_set_delta_slope(struct ath_hal *ah,
			 struct ath9k_channel *chan)
{
	u32 coef_scaled, ds_coef_exp, ds_coef_man;
	u32 clockMhzScaled = 0x64000000;
	struct chan_centers centers;

	if (IS_CHAN_HALF_RATE(chan))
		clockMhzScaled = clockMhzScaled >> 1;
	else if (IS_CHAN_QUARTER_RATE(chan))
		clockMhzScaled = clockMhzScaled >> 2;

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	coef_scaled = clockMhzScaled / centers.synth_center;

	ath9k_hw_get_delta_slope_vals(ah, coef_scaled, &ds_coef_man,
				      &ds_coef_exp);

	REG_RMW_FIELD(ah, AR_PHY_TIMING3,
		      AR_PHY_TIMING3_DSC_MAN, ds_coef_man);
	REG_RMW_FIELD(ah, AR_PHY_TIMING3,
		      AR_PHY_TIMING3_DSC_EXP, ds_coef_exp);

	coef_scaled = (9 * coef_scaled) / 10;

	ath9k_hw_get_delta_slope_vals(ah, coef_scaled, &ds_coef_man,
				      &ds_coef_exp);

	REG_RMW_FIELD(ah, AR_PHY_HALFGI,
		      AR_PHY_HALFGI_DSC_MAN, ds_coef_man);
	REG_RMW_FIELD(ah, AR_PHY_HALFGI,
		      AR_PHY_HALFGI_DSC_EXP, ds_coef_exp);
}

static void ath9k_hw_9280_spur_mitigate(struct ath_hal *ah,
					struct ath9k_channel *chan)
{
	int bb_spur = AR_NO_SPUR;
	int freq;
	int bin, cur_bin;
	int bb_spur_off, spur_subchannel_sd;
	int spur_freq_sd;
	int spur_delta_phase;
	int denominator;
	int upper, lower, cur_vit_mask;
	int tmp, newVal;
	int i;
	int pilot_mask_reg[4] = { AR_PHY_TIMING7, AR_PHY_TIMING8,
			  AR_PHY_PILOT_MASK_01_30, AR_PHY_PILOT_MASK_31_60
	};
	int chan_mask_reg[4] = { AR_PHY_TIMING9, AR_PHY_TIMING10,
			 AR_PHY_CHANNEL_MASK_01_30, AR_PHY_CHANNEL_MASK_31_60
	};
	int inc[4] = { 0, 100, 0, 0 };
	struct chan_centers centers;

	int8_t mask_m[123];
	int8_t mask_p[123];
	int8_t mask_amt;
	int tmp_mask;
	int cur_bb_spur;
	bool is2GHz = IS_CHAN_2GHZ(chan);

	memset(&mask_m, 0, sizeof(int8_t) * 123);
	memset(&mask_p, 0, sizeof(int8_t) * 123);

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = centers.synth_center;

	ah->ah_config.spurmode = SPUR_ENABLE_EEPROM;
	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		cur_bb_spur = ath9k_hw_eeprom_get_spur_chan(ah, i, is2GHz);

		if (is2GHz)
			cur_bb_spur = (cur_bb_spur / 10) + AR_BASE_FREQ_2GHZ;
		else
			cur_bb_spur = (cur_bb_spur / 10) + AR_BASE_FREQ_5GHZ;

		if (AR_NO_SPUR == cur_bb_spur)
			break;
		cur_bb_spur = cur_bb_spur - freq;

		if (IS_CHAN_HT40(chan)) {
			if ((cur_bb_spur > -AR_SPUR_FEEQ_BOUND_HT40) &&
			    (cur_bb_spur < AR_SPUR_FEEQ_BOUND_HT40)) {
				bb_spur = cur_bb_spur;
				break;
			}
		} else if ((cur_bb_spur > -AR_SPUR_FEEQ_BOUND_HT20) &&
			   (cur_bb_spur < AR_SPUR_FEEQ_BOUND_HT20)) {
			bb_spur = cur_bb_spur;
			break;
		}
	}

	if (AR_NO_SPUR == bb_spur) {
		REG_CLR_BIT(ah, AR_PHY_FORCE_CLKEN_CCK,
			    AR_PHY_FORCE_CLKEN_CCK_MRC_MUX);
		return;
	} else {
		REG_CLR_BIT(ah, AR_PHY_FORCE_CLKEN_CCK,
			    AR_PHY_FORCE_CLKEN_CCK_MRC_MUX);
	}

	bin = bb_spur * 320;

	tmp = REG_READ(ah, AR_PHY_TIMING_CTRL4(0));

	newVal = tmp | (AR_PHY_TIMING_CTRL4_ENABLE_SPUR_RSSI |
			AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER |
			AR_PHY_TIMING_CTRL4_ENABLE_CHAN_MASK |
			AR_PHY_TIMING_CTRL4_ENABLE_PILOT_MASK);
	REG_WRITE(ah, AR_PHY_TIMING_CTRL4(0), newVal);

	newVal = (AR_PHY_SPUR_REG_MASK_RATE_CNTL |
		  AR_PHY_SPUR_REG_ENABLE_MASK_PPM |
		  AR_PHY_SPUR_REG_MASK_RATE_SELECT |
		  AR_PHY_SPUR_REG_ENABLE_VIT_SPUR_RSSI |
		  SM(SPUR_RSSI_THRESH, AR_PHY_SPUR_REG_SPUR_RSSI_THRESH));
	REG_WRITE(ah, AR_PHY_SPUR_REG, newVal);

	if (IS_CHAN_HT40(chan)) {
		if (bb_spur < 0) {
			spur_subchannel_sd = 1;
			bb_spur_off = bb_spur + 10;
		} else {
			spur_subchannel_sd = 0;
			bb_spur_off = bb_spur - 10;
		}
	} else {
		spur_subchannel_sd = 0;
		bb_spur_off = bb_spur;
	}

	if (IS_CHAN_HT40(chan))
		spur_delta_phase =
			((bb_spur * 262144) /
			 10) & AR_PHY_TIMING11_SPUR_DELTA_PHASE;
	else
		spur_delta_phase =
			((bb_spur * 524288) /
			 10) & AR_PHY_TIMING11_SPUR_DELTA_PHASE;

	denominator = IS_CHAN_2GHZ(chan) ? 44 : 40;
	spur_freq_sd = ((bb_spur_off * 2048) / denominator) & 0x3ff;

	newVal = (AR_PHY_TIMING11_USE_SPUR_IN_AGC |
		  SM(spur_freq_sd, AR_PHY_TIMING11_SPUR_FREQ_SD) |
		  SM(spur_delta_phase, AR_PHY_TIMING11_SPUR_DELTA_PHASE));
	REG_WRITE(ah, AR_PHY_TIMING11, newVal);

	newVal = spur_subchannel_sd << AR_PHY_SFCORR_SPUR_SUBCHNL_SD_S;
	REG_WRITE(ah, AR_PHY_SFCORR_EXT, newVal);

	cur_bin = -6000;
	upper = bin + 100;
	lower = bin - 100;

	for (i = 0; i < 4; i++) {
		int pilot_mask = 0;
		int chan_mask = 0;
		int bp = 0;
		for (bp = 0; bp < 30; bp++) {
			if ((cur_bin > lower) && (cur_bin < upper)) {
				pilot_mask = pilot_mask | 0x1 << bp;
				chan_mask = chan_mask | 0x1 << bp;
			}
			cur_bin += 100;
		}
		cur_bin += inc[i];
		REG_WRITE(ah, pilot_mask_reg[i], pilot_mask);
		REG_WRITE(ah, chan_mask_reg[i], chan_mask);
	}

	cur_vit_mask = 6100;
	upper = bin + 120;
	lower = bin - 120;

	for (i = 0; i < 123; i++) {
		if ((cur_vit_mask > lower) && (cur_vit_mask < upper)) {

			/* workaround for gcc bug #37014 */
			volatile int tmp = abs(cur_vit_mask - bin);

			if (tmp < 75)
				mask_amt = 1;
			else
				mask_amt = 0;
			if (cur_vit_mask < 0)
				mask_m[abs(cur_vit_mask / 100)] = mask_amt;
			else
				mask_p[cur_vit_mask / 100] = mask_amt;
		}
		cur_vit_mask -= 100;
	}

	tmp_mask = (mask_m[46] << 30) | (mask_m[47] << 28)
		| (mask_m[48] << 26) | (mask_m[49] << 24)
		| (mask_m[50] << 22) | (mask_m[51] << 20)
		| (mask_m[52] << 18) | (mask_m[53] << 16)
		| (mask_m[54] << 14) | (mask_m[55] << 12)
		| (mask_m[56] << 10) | (mask_m[57] << 8)
		| (mask_m[58] << 6) | (mask_m[59] << 4)
		| (mask_m[60] << 2) | (mask_m[61] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_1, tmp_mask);
	REG_WRITE(ah, AR_PHY_VIT_MASK2_M_46_61, tmp_mask);

	tmp_mask = (mask_m[31] << 28)
		| (mask_m[32] << 26) | (mask_m[33] << 24)
		| (mask_m[34] << 22) | (mask_m[35] << 20)
		| (mask_m[36] << 18) | (mask_m[37] << 16)
		| (mask_m[48] << 14) | (mask_m[39] << 12)
		| (mask_m[40] << 10) | (mask_m[41] << 8)
		| (mask_m[42] << 6) | (mask_m[43] << 4)
		| (mask_m[44] << 2) | (mask_m[45] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_2, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_31_45, tmp_mask);

	tmp_mask = (mask_m[16] << 30) | (mask_m[16] << 28)
		| (mask_m[18] << 26) | (mask_m[18] << 24)
		| (mask_m[20] << 22) | (mask_m[20] << 20)
		| (mask_m[22] << 18) | (mask_m[22] << 16)
		| (mask_m[24] << 14) | (mask_m[24] << 12)
		| (mask_m[25] << 10) | (mask_m[26] << 8)
		| (mask_m[27] << 6) | (mask_m[28] << 4)
		| (mask_m[29] << 2) | (mask_m[30] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_3, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_16_30, tmp_mask);

	tmp_mask = (mask_m[0] << 30) | (mask_m[1] << 28)
		| (mask_m[2] << 26) | (mask_m[3] << 24)
		| (mask_m[4] << 22) | (mask_m[5] << 20)
		| (mask_m[6] << 18) | (mask_m[7] << 16)
		| (mask_m[8] << 14) | (mask_m[9] << 12)
		| (mask_m[10] << 10) | (mask_m[11] << 8)
		| (mask_m[12] << 6) | (mask_m[13] << 4)
		| (mask_m[14] << 2) | (mask_m[15] << 0);
	REG_WRITE(ah, AR_PHY_MASK_CTL, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_00_15, tmp_mask);

	tmp_mask = (mask_p[15] << 28)
		| (mask_p[14] << 26) | (mask_p[13] << 24)
		| (mask_p[12] << 22) | (mask_p[11] << 20)
		| (mask_p[10] << 18) | (mask_p[9] << 16)
		| (mask_p[8] << 14) | (mask_p[7] << 12)
		| (mask_p[6] << 10) | (mask_p[5] << 8)
		| (mask_p[4] << 6) | (mask_p[3] << 4)
		| (mask_p[2] << 2) | (mask_p[1] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_1, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_15_01, tmp_mask);

	tmp_mask = (mask_p[30] << 28)
		| (mask_p[29] << 26) | (mask_p[28] << 24)
		| (mask_p[27] << 22) | (mask_p[26] << 20)
		| (mask_p[25] << 18) | (mask_p[24] << 16)
		| (mask_p[23] << 14) | (mask_p[22] << 12)
		| (mask_p[21] << 10) | (mask_p[20] << 8)
		| (mask_p[19] << 6) | (mask_p[18] << 4)
		| (mask_p[17] << 2) | (mask_p[16] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_2, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_30_16, tmp_mask);

	tmp_mask = (mask_p[45] << 28)
		| (mask_p[44] << 26) | (mask_p[43] << 24)
		| (mask_p[42] << 22) | (mask_p[41] << 20)
		| (mask_p[40] << 18) | (mask_p[39] << 16)
		| (mask_p[38] << 14) | (mask_p[37] << 12)
		| (mask_p[36] << 10) | (mask_p[35] << 8)
		| (mask_p[34] << 6) | (mask_p[33] << 4)
		| (mask_p[32] << 2) | (mask_p[31] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_3, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_45_31, tmp_mask);

	tmp_mask = (mask_p[61] << 30) | (mask_p[60] << 28)
		| (mask_p[59] << 26) | (mask_p[58] << 24)
		| (mask_p[57] << 22) | (mask_p[56] << 20)
		| (mask_p[55] << 18) | (mask_p[54] << 16)
		| (mask_p[53] << 14) | (mask_p[52] << 12)
		| (mask_p[51] << 10) | (mask_p[50] << 8)
		| (mask_p[49] << 6) | (mask_p[48] << 4)
		| (mask_p[47] << 2) | (mask_p[46] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_4, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_61_45, tmp_mask);
}

static void ath9k_hw_spur_mitigate(struct ath_hal *ah,
				   struct ath9k_channel *chan)
{
	int bb_spur = AR_NO_SPUR;
	int bin, cur_bin;
	int spur_freq_sd;
	int spur_delta_phase;
	int denominator;
	int upper, lower, cur_vit_mask;
	int tmp, new;
	int i;
	int pilot_mask_reg[4] = { AR_PHY_TIMING7, AR_PHY_TIMING8,
			  AR_PHY_PILOT_MASK_01_30, AR_PHY_PILOT_MASK_31_60
	};
	int chan_mask_reg[4] = { AR_PHY_TIMING9, AR_PHY_TIMING10,
			 AR_PHY_CHANNEL_MASK_01_30, AR_PHY_CHANNEL_MASK_31_60
	};
	int inc[4] = { 0, 100, 0, 0 };

	int8_t mask_m[123];
	int8_t mask_p[123];
	int8_t mask_amt;
	int tmp_mask;
	int cur_bb_spur;
	bool is2GHz = IS_CHAN_2GHZ(chan);

	memset(&mask_m, 0, sizeof(int8_t) * 123);
	memset(&mask_p, 0, sizeof(int8_t) * 123);

	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		cur_bb_spur = ath9k_hw_eeprom_get_spur_chan(ah, i, is2GHz);
		if (AR_NO_SPUR == cur_bb_spur)
			break;
		cur_bb_spur = cur_bb_spur - (chan->channel * 10);
		if ((cur_bb_spur > -95) && (cur_bb_spur < 95)) {
			bb_spur = cur_bb_spur;
			break;
		}
	}

	if (AR_NO_SPUR == bb_spur)
		return;

	bin = bb_spur * 32;

	tmp = REG_READ(ah, AR_PHY_TIMING_CTRL4(0));
	new = tmp | (AR_PHY_TIMING_CTRL4_ENABLE_SPUR_RSSI |
		     AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER |
		     AR_PHY_TIMING_CTRL4_ENABLE_CHAN_MASK |
		     AR_PHY_TIMING_CTRL4_ENABLE_PILOT_MASK);

	REG_WRITE(ah, AR_PHY_TIMING_CTRL4(0), new);

	new = (AR_PHY_SPUR_REG_MASK_RATE_CNTL |
	       AR_PHY_SPUR_REG_ENABLE_MASK_PPM |
	       AR_PHY_SPUR_REG_MASK_RATE_SELECT |
	       AR_PHY_SPUR_REG_ENABLE_VIT_SPUR_RSSI |
	       SM(SPUR_RSSI_THRESH, AR_PHY_SPUR_REG_SPUR_RSSI_THRESH));
	REG_WRITE(ah, AR_PHY_SPUR_REG, new);

	spur_delta_phase = ((bb_spur * 524288) / 100) &
		AR_PHY_TIMING11_SPUR_DELTA_PHASE;

	denominator = IS_CHAN_2GHZ(chan) ? 440 : 400;
	spur_freq_sd = ((bb_spur * 2048) / denominator) & 0x3ff;

	new = (AR_PHY_TIMING11_USE_SPUR_IN_AGC |
	       SM(spur_freq_sd, AR_PHY_TIMING11_SPUR_FREQ_SD) |
	       SM(spur_delta_phase, AR_PHY_TIMING11_SPUR_DELTA_PHASE));
	REG_WRITE(ah, AR_PHY_TIMING11, new);

	cur_bin = -6000;
	upper = bin + 100;
	lower = bin - 100;

	for (i = 0; i < 4; i++) {
		int pilot_mask = 0;
		int chan_mask = 0;
		int bp = 0;
		for (bp = 0; bp < 30; bp++) {
			if ((cur_bin > lower) && (cur_bin < upper)) {
				pilot_mask = pilot_mask | 0x1 << bp;
				chan_mask = chan_mask | 0x1 << bp;
			}
			cur_bin += 100;
		}
		cur_bin += inc[i];
		REG_WRITE(ah, pilot_mask_reg[i], pilot_mask);
		REG_WRITE(ah, chan_mask_reg[i], chan_mask);
	}

	cur_vit_mask = 6100;
	upper = bin + 120;
	lower = bin - 120;

	for (i = 0; i < 123; i++) {
		if ((cur_vit_mask > lower) && (cur_vit_mask < upper)) {

			/* workaround for gcc bug #37014 */
			volatile int tmp = abs(cur_vit_mask - bin);

			if (tmp < 75)
				mask_amt = 1;
			else
				mask_amt = 0;
			if (cur_vit_mask < 0)
				mask_m[abs(cur_vit_mask / 100)] = mask_amt;
			else
				mask_p[cur_vit_mask / 100] = mask_amt;
		}
		cur_vit_mask -= 100;
	}

	tmp_mask = (mask_m[46] << 30) | (mask_m[47] << 28)
		| (mask_m[48] << 26) | (mask_m[49] << 24)
		| (mask_m[50] << 22) | (mask_m[51] << 20)
		| (mask_m[52] << 18) | (mask_m[53] << 16)
		| (mask_m[54] << 14) | (mask_m[55] << 12)
		| (mask_m[56] << 10) | (mask_m[57] << 8)
		| (mask_m[58] << 6) | (mask_m[59] << 4)
		| (mask_m[60] << 2) | (mask_m[61] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_1, tmp_mask);
	REG_WRITE(ah, AR_PHY_VIT_MASK2_M_46_61, tmp_mask);

	tmp_mask = (mask_m[31] << 28)
		| (mask_m[32] << 26) | (mask_m[33] << 24)
		| (mask_m[34] << 22) | (mask_m[35] << 20)
		| (mask_m[36] << 18) | (mask_m[37] << 16)
		| (mask_m[48] << 14) | (mask_m[39] << 12)
		| (mask_m[40] << 10) | (mask_m[41] << 8)
		| (mask_m[42] << 6) | (mask_m[43] << 4)
		| (mask_m[44] << 2) | (mask_m[45] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_2, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_31_45, tmp_mask);

	tmp_mask = (mask_m[16] << 30) | (mask_m[16] << 28)
		| (mask_m[18] << 26) | (mask_m[18] << 24)
		| (mask_m[20] << 22) | (mask_m[20] << 20)
		| (mask_m[22] << 18) | (mask_m[22] << 16)
		| (mask_m[24] << 14) | (mask_m[24] << 12)
		| (mask_m[25] << 10) | (mask_m[26] << 8)
		| (mask_m[27] << 6) | (mask_m[28] << 4)
		| (mask_m[29] << 2) | (mask_m[30] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_3, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_16_30, tmp_mask);

	tmp_mask = (mask_m[0] << 30) | (mask_m[1] << 28)
		| (mask_m[2] << 26) | (mask_m[3] << 24)
		| (mask_m[4] << 22) | (mask_m[5] << 20)
		| (mask_m[6] << 18) | (mask_m[7] << 16)
		| (mask_m[8] << 14) | (mask_m[9] << 12)
		| (mask_m[10] << 10) | (mask_m[11] << 8)
		| (mask_m[12] << 6) | (mask_m[13] << 4)
		| (mask_m[14] << 2) | (mask_m[15] << 0);
	REG_WRITE(ah, AR_PHY_MASK_CTL, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_00_15, tmp_mask);

	tmp_mask = (mask_p[15] << 28)
		| (mask_p[14] << 26) | (mask_p[13] << 24)
		| (mask_p[12] << 22) | (mask_p[11] << 20)
		| (mask_p[10] << 18) | (mask_p[9] << 16)
		| (mask_p[8] << 14) | (mask_p[7] << 12)
		| (mask_p[6] << 10) | (mask_p[5] << 8)
		| (mask_p[4] << 6) | (mask_p[3] << 4)
		| (mask_p[2] << 2) | (mask_p[1] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_1, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_15_01, tmp_mask);

	tmp_mask = (mask_p[30] << 28)
		| (mask_p[29] << 26) | (mask_p[28] << 24)
		| (mask_p[27] << 22) | (mask_p[26] << 20)
		| (mask_p[25] << 18) | (mask_p[24] << 16)
		| (mask_p[23] << 14) | (mask_p[22] << 12)
		| (mask_p[21] << 10) | (mask_p[20] << 8)
		| (mask_p[19] << 6) | (mask_p[18] << 4)
		| (mask_p[17] << 2) | (mask_p[16] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_2, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_30_16, tmp_mask);

	tmp_mask = (mask_p[45] << 28)
		| (mask_p[44] << 26) | (mask_p[43] << 24)
		| (mask_p[42] << 22) | (mask_p[41] << 20)
		| (mask_p[40] << 18) | (mask_p[39] << 16)
		| (mask_p[38] << 14) | (mask_p[37] << 12)
		| (mask_p[36] << 10) | (mask_p[35] << 8)
		| (mask_p[34] << 6) | (mask_p[33] << 4)
		| (mask_p[32] << 2) | (mask_p[31] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_3, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_45_31, tmp_mask);

	tmp_mask = (mask_p[61] << 30) | (mask_p[60] << 28)
		| (mask_p[59] << 26) | (mask_p[58] << 24)
		| (mask_p[57] << 22) | (mask_p[56] << 20)
		| (mask_p[55] << 18) | (mask_p[54] << 16)
		| (mask_p[53] << 14) | (mask_p[52] << 12)
		| (mask_p[51] << 10) | (mask_p[50] << 8)
		| (mask_p[49] << 6) | (mask_p[48] << 4)
		| (mask_p[47] << 2) | (mask_p[46] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_4, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_61_45, tmp_mask);
}

static inline void ath9k_hw_init_chain_masks(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int rx_chainmask, tx_chainmask;

	rx_chainmask = ahp->ah_rxchainmask;
	tx_chainmask = ahp->ah_txchainmask;

	switch (rx_chainmask) {
	case 0x5:
		REG_SET_BIT(ah, AR_PHY_ANALOG_SWAP,
			    AR_PHY_SWAP_ALT_CHAIN);
	case 0x3:
		if (((ah)->ah_macVersion <= AR_SREV_VERSION_9160)) {
			REG_WRITE(ah, AR_PHY_RX_CHAINMASK, 0x7);
			REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, 0x7);
			break;
		}
	case 0x1:
	case 0x2:
		if (!AR_SREV_9280(ah))
			break;
	case 0x7:
		REG_WRITE(ah, AR_PHY_RX_CHAINMASK, rx_chainmask);
		REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, rx_chainmask);
		break;
	default:
		break;
	}

	REG_WRITE(ah, AR_SELFGEN_MASK, tx_chainmask);
	if (tx_chainmask == 0x5) {
		REG_SET_BIT(ah, AR_PHY_ANALOG_SWAP,
			    AR_PHY_SWAP_ALT_CHAIN);
	}
	if (AR_SREV_9100(ah))
		REG_WRITE(ah, AR_PHY_ANALOG_SWAP,
			  REG_READ(ah, AR_PHY_ANALOG_SWAP) | 0x00000001);
}

static void ath9k_hw_set_addac(struct ath_hal *ah,
			       struct ath9k_channel *chan)
{
	struct modal_eep_header *pModal;
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ar5416_eeprom *eep = &ahp->ah_eeprom;
	u8 biaslevel;

	if (ah->ah_macVersion != AR_SREV_VERSION_9160)
		return;

	if (ar5416_get_eep_rev(ahp) < AR5416_EEP_MINOR_VER_7)
		return;

	pModal = &(eep->modalHeader[IS_CHAN_2GHZ(chan)]);

	if (pModal->xpaBiasLvl != 0xff) {
		biaslevel = pModal->xpaBiasLvl;
	} else {

		u16 resetFreqBin, freqBin, freqCount = 0;
		struct chan_centers centers;

		ath9k_hw_get_channel_centers(ah, chan, &centers);

		resetFreqBin =
			FREQ2FBIN(centers.synth_center, IS_CHAN_2GHZ(chan));
		freqBin = pModal->xpaBiasLvlFreq[0] & 0xff;
		biaslevel = (u8) (pModal->xpaBiasLvlFreq[0] >> 14);

		freqCount++;

		while (freqCount < 3) {
			if (pModal->xpaBiasLvlFreq[freqCount] == 0x0)
				break;

			freqBin = pModal->xpaBiasLvlFreq[freqCount] & 0xff;
			if (resetFreqBin >= freqBin) {
				biaslevel =
					(u8) (pModal->
						    xpaBiasLvlFreq[freqCount]
						    >> 14);
			} else {
				break;
			}
			freqCount++;
		}
	}

	if (IS_CHAN_2GHZ(chan)) {
		INI_RA(&ahp->ah_iniAddac, 7, 1) =
			(INI_RA(&ahp->ah_iniAddac, 7, 1) & (~0x18)) | biaslevel
			<< 3;
	} else {
		INI_RA(&ahp->ah_iniAddac, 6, 1) =
			(INI_RA(&ahp->ah_iniAddac, 6, 1) & (~0xc0)) | biaslevel
			<< 6;
	}
}

static u32 ath9k_hw_mac_usec(struct ath_hal *ah, u32 clks)
{
	if (ah->ah_curchan != NULL)
		return clks /
		CLOCK_RATE[ath9k_hw_chan2wmode(ah, ah->ah_curchan)];
	else
		return clks / CLOCK_RATE[ATH9K_MODE_11B];
}

static u32 ath9k_hw_mac_to_usec(struct ath_hal *ah, u32 clks)
{
	struct ath9k_channel *chan = ah->ah_curchan;

	if (chan && IS_CHAN_HT40(chan))
		return ath9k_hw_mac_usec(ah, clks) / 2;
	else
		return ath9k_hw_mac_usec(ah, clks);
}

static u32 ath9k_hw_mac_clks(struct ath_hal *ah, u32 usecs)
{
	if (ah->ah_curchan != NULL)
		return usecs * CLOCK_RATE[ath9k_hw_chan2wmode(ah,
			ah->ah_curchan)];
	else
		return usecs * CLOCK_RATE[ATH9K_MODE_11B];
}

static u32 ath9k_hw_mac_to_clks(struct ath_hal *ah, u32 usecs)
{
	struct ath9k_channel *chan = ah->ah_curchan;

	if (chan && IS_CHAN_HT40(chan))
		return ath9k_hw_mac_clks(ah, usecs) * 2;
	else
		return ath9k_hw_mac_clks(ah, usecs);
}

static bool ath9k_hw_set_ack_timeout(struct ath_hal *ah, u32 us)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (us > ath9k_hw_mac_to_usec(ah, MS(0xffffffff, AR_TIME_OUT_ACK))) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "%s: bad ack timeout %u\n",
			 __func__, us);
		ahp->ah_acktimeout = (u32) -1;
		return false;
	} else {
		REG_RMW_FIELD(ah, AR_TIME_OUT,
			      AR_TIME_OUT_ACK, ath9k_hw_mac_to_clks(ah, us));
		ahp->ah_acktimeout = us;
		return true;
	}
}

static bool ath9k_hw_set_cts_timeout(struct ath_hal *ah, u32 us)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (us > ath9k_hw_mac_to_usec(ah, MS(0xffffffff, AR_TIME_OUT_CTS))) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "%s: bad cts timeout %u\n",
			 __func__, us);
		ahp->ah_ctstimeout = (u32) -1;
		return false;
	} else {
		REG_RMW_FIELD(ah, AR_TIME_OUT,
			      AR_TIME_OUT_CTS, ath9k_hw_mac_to_clks(ah, us));
		ahp->ah_ctstimeout = us;
		return true;
	}
}
static bool ath9k_hw_set_global_txtimeout(struct ath_hal *ah,
					  u32 tu)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (tu > 0xFFFF) {
		DPRINTF(ah->ah_sc, ATH_DBG_XMIT,
			"%s: bad global tx timeout %u\n", __func__, tu);
		ahp->ah_globaltxtimeout = (u32) -1;
		return false;
	} else {
		REG_RMW_FIELD(ah, AR_GTXTO, AR_GTXTO_TIMEOUT_LIMIT, tu);
		ahp->ah_globaltxtimeout = tu;
		return true;
	}
}

bool ath9k_hw_setslottime(struct ath_hal *ah, u32 us)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (us < ATH9K_SLOT_TIME_9 || us > ath9k_hw_mac_to_usec(ah, 0xffff)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "%s: bad slot time %u\n",
			 __func__, us);
		ahp->ah_slottime = (u32) -1;
		return false;
	} else {
		REG_WRITE(ah, AR_D_GBL_IFS_SLOT, ath9k_hw_mac_to_clks(ah, us));
		ahp->ah_slottime = us;
		return true;
	}
}

static inline void ath9k_hw_init_user_settings(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	DPRINTF(ah->ah_sc, ATH_DBG_RESET, "--AP %s ahp->ah_miscMode 0x%x\n",
		 __func__, ahp->ah_miscMode);
	if (ahp->ah_miscMode != 0)
		REG_WRITE(ah, AR_PCU_MISC,
			  REG_READ(ah, AR_PCU_MISC) | ahp->ah_miscMode);
	if (ahp->ah_slottime != (u32) -1)
		ath9k_hw_setslottime(ah, ahp->ah_slottime);
	if (ahp->ah_acktimeout != (u32) -1)
		ath9k_hw_set_ack_timeout(ah, ahp->ah_acktimeout);
	if (ahp->ah_ctstimeout != (u32) -1)
		ath9k_hw_set_cts_timeout(ah, ahp->ah_ctstimeout);
	if (ahp->ah_globaltxtimeout != (u32) -1)
		ath9k_hw_set_global_txtimeout(ah, ahp->ah_globaltxtimeout);
}

static inline int
ath9k_hw_process_ini(struct ath_hal *ah,
		     struct ath9k_channel *chan,
		     enum ath9k_ht_macmode macmode)
{
	int i, regWrites = 0;
	struct ath_hal_5416 *ahp = AH5416(ah);
	u32 modesIndex, freqIndex;
	int status;

	switch (chan->chanmode) {
	case CHANNEL_A:
	case CHANNEL_A_HT20:
		modesIndex = 1;
		freqIndex = 1;
		break;
	case CHANNEL_A_HT40PLUS:
	case CHANNEL_A_HT40MINUS:
		modesIndex = 2;
		freqIndex = 1;
		break;
	case CHANNEL_G:
	case CHANNEL_G_HT20:
	case CHANNEL_B:
		modesIndex = 4;
		freqIndex = 2;
		break;
	case CHANNEL_G_HT40PLUS:
	case CHANNEL_G_HT40MINUS:
		modesIndex = 3;
		freqIndex = 2;
		break;

	default:
		return -EINVAL;
	}

	REG_WRITE(ah, AR_PHY(0), 0x00000007);

	REG_WRITE(ah, AR_PHY_ADC_SERIAL_CTL, AR_PHY_SEL_EXTERNAL_RADIO);

	ath9k_hw_set_addac(ah, chan);

	if (AR_SREV_5416_V22_OR_LATER(ah)) {
		REG_WRITE_ARRAY(&ahp->ah_iniAddac, 1, regWrites);
	} else {
		struct ar5416IniArray temp;
		u32 addacSize =
			sizeof(u32) * ahp->ah_iniAddac.ia_rows *
			ahp->ah_iniAddac.ia_columns;

		memcpy(ahp->ah_addac5416_21,
		       ahp->ah_iniAddac.ia_array, addacSize);

		(ahp->ah_addac5416_21)[31 *
				       ahp->ah_iniAddac.ia_columns + 1] = 0;

		temp.ia_array = ahp->ah_addac5416_21;
		temp.ia_columns = ahp->ah_iniAddac.ia_columns;
		temp.ia_rows = ahp->ah_iniAddac.ia_rows;
		REG_WRITE_ARRAY(&temp, 1, regWrites);
	}
	REG_WRITE(ah, AR_PHY_ADC_SERIAL_CTL, AR_PHY_SEL_INTERNAL_ADDAC);

	for (i = 0; i < ahp->ah_iniModes.ia_rows; i++) {
		u32 reg = INI_RA(&ahp->ah_iniModes, i, 0);
		u32 val = INI_RA(&ahp->ah_iniModes, i, modesIndex);

#ifdef CONFIG_SLOW_ANT_DIV
		if (ah->ah_devid == AR9280_DEVID_PCI)
			val = ath9k_hw_ini_fixup(ah, &ahp->ah_eeprom, reg,
						 val);
#endif

		REG_WRITE(ah, reg, val);

		if (reg >= 0x7800 && reg < 0x78a0
		    && ah->ah_config.analog_shiftreg) {
			udelay(100);
		}

		DO_DELAY(regWrites);
	}

	for (i = 0; i < ahp->ah_iniCommon.ia_rows; i++) {
		u32 reg = INI_RA(&ahp->ah_iniCommon, i, 0);
		u32 val = INI_RA(&ahp->ah_iniCommon, i, 1);

		REG_WRITE(ah, reg, val);

		if (reg >= 0x7800 && reg < 0x78a0
		    && ah->ah_config.analog_shiftreg) {
			udelay(100);
		}

		DO_DELAY(regWrites);
	}

	ath9k_hw_write_regs(ah, modesIndex, freqIndex, regWrites);

	if (AR_SREV_9280_20(ah) && IS_CHAN_A_5MHZ_SPACED(chan)) {
		REG_WRITE_ARRAY(&ahp->ah_iniModesAdditional, modesIndex,
				regWrites);
	}

	ath9k_hw_override_ini(ah, chan);
	ath9k_hw_set_regs(ah, chan, macmode);
	ath9k_hw_init_chain_masks(ah);

	status = ath9k_hw_set_txpower(ah, &ahp->ah_eeprom, chan,
				      ath9k_regd_get_ctl(ah, chan),
				      ath9k_regd_get_antenna_allowed(ah,
								     chan),
				      chan->maxRegTxPower * 2,
				      min((u32) MAX_RATE_POWER,
					  (u32) ah->ah_powerLimit));
	if (status != 0) {
		DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT,
			 "%s: error init'ing transmit power\n", __func__);
		return -EIO;
	}

	if (!ath9k_hw_set_rf_regs(ah, chan, freqIndex)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
			 "%s: ar5416SetRfRegs failed\n", __func__);
		return -EIO;
	}

	return 0;
}

static inline void ath9k_hw_setup_calibration(struct ath_hal *ah,
					      struct hal_cal_list *currCal)
{
	REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4(0),
		      AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX,
		      currCal->calData->calCountMax);

	switch (currCal->calData->calType) {
	case IQ_MISMATCH_CAL:
		REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_IQ);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "%s: starting IQ Mismatch Calibration\n",
			 __func__);
		break;
	case ADC_GAIN_CAL:
		REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_ADC_GAIN);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "%s: starting ADC Gain Calibration\n", __func__);
		break;
	case ADC_DC_CAL:
		REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_ADC_DC_PER);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "%s: starting ADC DC Calibration\n", __func__);
		break;
	case ADC_DC_INIT_CAL:
		REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_ADC_DC_INIT);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "%s: starting Init ADC DC Calibration\n",
			 __func__);
		break;
	}

	REG_SET_BIT(ah, AR_PHY_TIMING_CTRL4(0),
		    AR_PHY_TIMING_CTRL4_DO_CAL);
}

static inline void ath9k_hw_reset_calibration(struct ath_hal *ah,
					      struct hal_cal_list *currCal)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	ath9k_hw_setup_calibration(ah, currCal);

	currCal->calState = CAL_RUNNING;

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		ahp->ah_Meas0.sign[i] = 0;
		ahp->ah_Meas1.sign[i] = 0;
		ahp->ah_Meas2.sign[i] = 0;
		ahp->ah_Meas3.sign[i] = 0;
	}

	ahp->ah_CalSamples = 0;
}

static inline void
ath9k_hw_per_calibration(struct ath_hal *ah,
			 struct ath9k_channel *ichan,
			 u8 rxchainmask,
			 struct hal_cal_list *currCal,
			 bool *isCalDone)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	*isCalDone = false;

	if (currCal->calState == CAL_RUNNING) {
		if (!(REG_READ(ah,
			       AR_PHY_TIMING_CTRL4(0)) &
		      AR_PHY_TIMING_CTRL4_DO_CAL)) {

			currCal->calData->calCollect(ah);

			ahp->ah_CalSamples++;

			if (ahp->ah_CalSamples >=
			    currCal->calData->calNumSamples) {
				int i, numChains = 0;
				for (i = 0; i < AR5416_MAX_CHAINS; i++) {
					if (rxchainmask & (1 << i))
						numChains++;
				}

				currCal->calData->calPostProc(ah,
							      numChains);

				ichan->CalValid |=
					currCal->calData->calType;
				currCal->calState = CAL_DONE;
				*isCalDone = true;
			} else {
				ath9k_hw_setup_calibration(ah, currCal);
			}
		}
	} else if (!(ichan->CalValid & currCal->calData->calType)) {
		ath9k_hw_reset_calibration(ah, currCal);
	}
}

static inline bool ath9k_hw_run_init_cals(struct ath_hal *ah,
					  int init_cal_count)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_channel ichan;
	bool isCalDone;
	struct hal_cal_list *currCal = ahp->ah_cal_list_curr;
	const struct hal_percal_data *calData = currCal->calData;
	int i;

	if (currCal == NULL)
		return false;

	ichan.CalValid = 0;

	for (i = 0; i < init_cal_count; i++) {
		ath9k_hw_reset_calibration(ah, currCal);

		if (!ath9k_hw_wait(ah, AR_PHY_TIMING_CTRL4(0),
				   AR_PHY_TIMING_CTRL4_DO_CAL, 0)) {
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				 "%s: Cal %d failed to complete in 100ms.\n",
				 __func__, calData->calType);

			ahp->ah_cal_list = ahp->ah_cal_list_last =
				ahp->ah_cal_list_curr = NULL;
			return false;
		}

		ath9k_hw_per_calibration(ah, &ichan, ahp->ah_rxchainmask,
					 currCal, &isCalDone);
		if (!isCalDone) {
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				 "%s: Not able to run Init Cal %d.\n",
				 __func__, calData->calType);
		}
		if (currCal->calNext) {
			currCal = currCal->calNext;
			calData = currCal->calData;
		}
	}

	ahp->ah_cal_list = ahp->ah_cal_list_last = ahp->ah_cal_list_curr = NULL;
	return true;
}

static inline bool
ath9k_hw_channel_change(struct ath_hal *ah,
			struct ath9k_channel *chan,
			enum ath9k_ht_macmode macmode)
{
	u32 synthDelay, qnum;
	struct ath_hal_5416 *ahp = AH5416(ah);

	for (qnum = 0; qnum < AR_NUM_QCU; qnum++) {
		if (ath9k_hw_numtxpending(ah, qnum)) {
			DPRINTF(ah->ah_sc, ATH_DBG_QUEUE,
				 "%s: Transmit frames pending on queue %d\n",
				 __func__, qnum);
			return false;
		}
	}

	REG_WRITE(ah, AR_PHY_RFBUS_REQ, AR_PHY_RFBUS_REQ_EN);
	if (!ath9k_hw_wait(ah, AR_PHY_RFBUS_GRANT, AR_PHY_RFBUS_GRANT_EN,
			   AR_PHY_RFBUS_GRANT_EN)) {
		DPRINTF(ah->ah_sc, ATH_DBG_PHY_IO,
			 "%s: Could not kill baseband RX\n", __func__);
		return false;
	}

	ath9k_hw_set_regs(ah, chan, macmode);

	if (AR_SREV_9280_10_OR_LATER(ah)) {
		if (!(ath9k_hw_ar9280_set_channel(ah, chan))) {
			DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
				 "%s: failed to set channel\n", __func__);
			return false;
		}
	} else {
		if (!(ath9k_hw_set_channel(ah, chan))) {
			DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
				 "%s: failed to set channel\n", __func__);
			return false;
		}
	}

	if (ath9k_hw_set_txpower(ah, &ahp->ah_eeprom, chan,
				 ath9k_regd_get_ctl(ah, chan),
				 ath9k_regd_get_antenna_allowed(ah, chan),
				 chan->maxRegTxPower * 2,
				 min((u32) MAX_RATE_POWER,
				     (u32) ah->ah_powerLimit)) != 0) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			 "%s: error init'ing transmit power\n", __func__);
		return false;
	}

	synthDelay = REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_DELAY;
	if (IS_CHAN_CCK(chan))
		synthDelay = (4 * synthDelay) / 22;
	else
		synthDelay /= 10;

	udelay(synthDelay + BASE_ACTIVATE_DELAY);

	REG_WRITE(ah, AR_PHY_RFBUS_REQ, 0);

	if (IS_CHAN_OFDM(chan) || IS_CHAN_HT(chan))
		ath9k_hw_set_delta_slope(ah, chan);

	if (AR_SREV_9280_10_OR_LATER(ah))
		ath9k_hw_9280_spur_mitigate(ah, chan);
	else
		ath9k_hw_spur_mitigate(ah, chan);

	if (!chan->oneTimeCalsDone)
		chan->oneTimeCalsDone = true;

	return true;
}

static bool ath9k_hw_chip_reset(struct ath_hal *ah,
				struct ath9k_channel *chan)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_WARM))
		return false;

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE))
		return false;

	ahp->ah_chipFullSleep = false;

	ath9k_hw_init_pll(ah, chan);

	ath9k_hw_set_rfmode(ah, chan);

	return true;
}

static inline void ath9k_hw_set_dma(struct ath_hal *ah)
{
	u32 regval;

	regval = REG_READ(ah, AR_AHB_MODE);
	REG_WRITE(ah, AR_AHB_MODE, regval | AR_AHB_PREFETCH_RD_EN);

	regval = REG_READ(ah, AR_TXCFG) & ~AR_TXCFG_DMASZ_MASK;
	REG_WRITE(ah, AR_TXCFG, regval | AR_TXCFG_DMASZ_128B);

	REG_RMW_FIELD(ah, AR_TXCFG, AR_FTRIG, ah->ah_txTrigLevel);

	regval = REG_READ(ah, AR_RXCFG) & ~AR_RXCFG_DMASZ_MASK;
	REG_WRITE(ah, AR_RXCFG, regval | AR_RXCFG_DMASZ_128B);

	REG_WRITE(ah, AR_RXFIFO_CFG, 0x200);

	if (AR_SREV_9285(ah)) {
		REG_WRITE(ah, AR_PCU_TXBUF_CTRL,
			  AR_9285_PCU_TXBUF_CTRL_USABLE_SIZE);
	} else {
		REG_WRITE(ah, AR_PCU_TXBUF_CTRL,
			  AR_PCU_TXBUF_CTRL_USABLE_SIZE);
	}
}

bool ath9k_hw_stopdmarecv(struct ath_hal *ah)
{
	REG_WRITE(ah, AR_CR, AR_CR_RXD);
	if (!ath9k_hw_wait(ah, AR_CR, AR_CR_RXE, 0)) {
		DPRINTF(ah->ah_sc, ATH_DBG_QUEUE,
			"%s: dma failed to stop in 10ms\n"
			"AR_CR=0x%08x\nAR_DIAG_SW=0x%08x\n",
			__func__,
			REG_READ(ah, AR_CR), REG_READ(ah, AR_DIAG_SW));
		return false;
	} else {
		return true;
	}
}

void ath9k_hw_startpcureceive(struct ath_hal *ah)
{
	REG_CLR_BIT(ah, AR_DIAG_SW,
		    (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));

	ath9k_enable_mib_counters(ah);

	ath9k_ani_reset(ah);
}

void ath9k_hw_stoppcurecv(struct ath_hal *ah)
{
	REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_DIS);

	ath9k_hw_disable_mib_counters(ah);
}

static bool ath9k_hw_iscal_supported(struct ath_hal *ah,
				     struct ath9k_channel *chan,
				     enum hal_cal_types calType)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	bool retval = false;

	switch (calType & ahp->ah_suppCals) {
	case IQ_MISMATCH_CAL:
		if (!IS_CHAN_B(chan))
			retval = true;
		break;
	case ADC_GAIN_CAL:
	case ADC_DC_CAL:
		if (!IS_CHAN_B(chan)
		    && !(IS_CHAN_2GHZ(chan) && IS_CHAN_HT20(chan)))
			retval = true;
		break;
	}

	return retval;
}

static inline bool ath9k_hw_init_cal(struct ath_hal *ah,
				     struct ath9k_channel *chan)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_channel *ichan =
		ath9k_regd_check_channel(ah, chan);

	REG_WRITE(ah, AR_PHY_AGC_CONTROL,
		  REG_READ(ah, AR_PHY_AGC_CONTROL) |
		  AR_PHY_AGC_CONTROL_CAL);

	if (!ath9k_hw_wait
	    (ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL, 0)) {
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "%s: offset calibration failed to complete in 1ms; "
			 "noisy environment?\n", __func__);
		return false;
	}

	REG_WRITE(ah, AR_PHY_AGC_CONTROL,
		  REG_READ(ah, AR_PHY_AGC_CONTROL) |
		  AR_PHY_AGC_CONTROL_NF);

	ahp->ah_cal_list = ahp->ah_cal_list_last = ahp->ah_cal_list_curr =
		NULL;

	if (AR_SREV_9100(ah) || AR_SREV_9160_10_OR_LATER(ah)) {
		if (ath9k_hw_iscal_supported(ah, chan, ADC_GAIN_CAL)) {
			INIT_CAL(&ahp->ah_adcGainCalData);
			INSERT_CAL(ahp, &ahp->ah_adcGainCalData);
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				 "%s: enabling ADC Gain Calibration.\n",
				 __func__);
		}
		if (ath9k_hw_iscal_supported(ah, chan, ADC_DC_CAL)) {
			INIT_CAL(&ahp->ah_adcDcCalData);
			INSERT_CAL(ahp, &ahp->ah_adcDcCalData);
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				 "%s: enabling ADC DC Calibration.\n",
				 __func__);
		}
		if (ath9k_hw_iscal_supported(ah, chan, IQ_MISMATCH_CAL)) {
			INIT_CAL(&ahp->ah_iqCalData);
			INSERT_CAL(ahp, &ahp->ah_iqCalData);
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				 "%s: enabling IQ Calibration.\n",
				 __func__);
		}

		ahp->ah_cal_list_curr = ahp->ah_cal_list;

		if (ahp->ah_cal_list_curr)
			ath9k_hw_reset_calibration(ah,
						   ahp->ah_cal_list_curr);
	}

	ichan->CalValid = 0;

	return true;
}


bool ath9k_hw_reset(struct ath_hal *ah, enum ath9k_opmode opmode,
		    struct ath9k_channel *chan,
		    enum ath9k_ht_macmode macmode,
		    u8 txchainmask, u8 rxchainmask,
		    enum ath9k_ht_extprotspacing extprotspacing,
		    bool bChannelChange,
		    int *status)
{
#define FAIL(_code)     do { ecode = _code; goto bad; } while (0)
	u32 saveLedState;
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_channel *curchan = ah->ah_curchan;
	u32 saveDefAntenna;
	u32 macStaId1;
	int ecode;
	int i, rx_chainmask;

	ahp->ah_extprotspacing = extprotspacing;
	ahp->ah_txchainmask = txchainmask;
	ahp->ah_rxchainmask = rxchainmask;

	if (AR_SREV_9280(ah)) {
		ahp->ah_txchainmask &= 0x3;
		ahp->ah_rxchainmask &= 0x3;
	}

	if (ath9k_hw_check_chan(ah, chan) == NULL) {
		DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
			 "%s: invalid channel %u/0x%x; no mapping\n",
			 __func__, chan->channel, chan->channelFlags);
		FAIL(-EINVAL);
	}

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE))
		return false;

	if (curchan)
		ath9k_hw_getnf(ah, curchan);

	if (bChannelChange &&
	    (ahp->ah_chipFullSleep != true) &&
	    (ah->ah_curchan != NULL) &&
	    (chan->channel != ah->ah_curchan->channel) &&
	    ((chan->channelFlags & CHANNEL_ALL) ==
	     (ah->ah_curchan->channelFlags & CHANNEL_ALL)) &&
	    (!AR_SREV_9280(ah) || (!IS_CHAN_A_5MHZ_SPACED(chan) &&
				   !IS_CHAN_A_5MHZ_SPACED(ah->
							  ah_curchan)))) {

		if (ath9k_hw_channel_change(ah, chan, macmode)) {
			ath9k_hw_loadnf(ah, ah->ah_curchan);
			ath9k_hw_start_nfcal(ah);
			return true;
		}
	}

	saveDefAntenna = REG_READ(ah, AR_DEF_ANTENNA);
	if (saveDefAntenna == 0)
		saveDefAntenna = 1;

	macStaId1 = REG_READ(ah, AR_STA_ID1) & AR_STA_ID1_BASE_RATE_11B;

	saveLedState = REG_READ(ah, AR_CFG_LED) &
		(AR_CFG_LED_ASSOC_CTL | AR_CFG_LED_MODE_SEL |
		 AR_CFG_LED_BLINK_THRESH_SEL | AR_CFG_LED_BLINK_SLOW);

	ath9k_hw_mark_phy_inactive(ah);

	if (!ath9k_hw_chip_reset(ah, chan)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "%s: chip reset failed\n",
			 __func__);
		FAIL(-EIO);
	}

	if (AR_SREV_9280(ah)) {
		REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL,
			    AR_GPIO_JTAG_DISABLE);

		if (test_bit(ATH9K_MODE_11A, ah->ah_caps.wireless_modes)) {
			if (IS_CHAN_5GHZ(chan))
				ath9k_hw_set_gpio(ah, 9, 0);
			else
				ath9k_hw_set_gpio(ah, 9, 1);
		}
		ath9k_hw_cfg_output(ah, 9, ATH9K_GPIO_OUTPUT_MUX_AS_OUTPUT);
	}

	ecode = ath9k_hw_process_ini(ah, chan, macmode);
	if (ecode != 0)
		goto bad;

	if (IS_CHAN_OFDM(chan) || IS_CHAN_HT(chan))
		ath9k_hw_set_delta_slope(ah, chan);

	if (AR_SREV_9280_10_OR_LATER(ah))
		ath9k_hw_9280_spur_mitigate(ah, chan);
	else
		ath9k_hw_spur_mitigate(ah, chan);

	if (!ath9k_hw_eeprom_set_board_values(ah, chan)) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			 "%s: error setting board options\n", __func__);
		FAIL(-EIO);
	}

	ath9k_hw_decrease_chain_power(ah, chan);

	REG_WRITE(ah, AR_STA_ID0, get_unaligned_le32(ahp->ah_macaddr));
	REG_WRITE(ah, AR_STA_ID1, get_unaligned_le16(ahp->ah_macaddr + 4)
		  | macStaId1
		  | AR_STA_ID1_RTS_USE_DEF
		  | (ah->ah_config.
		     ack_6mb ? AR_STA_ID1_ACKCTS_6MB : 0)
		  | ahp->ah_staId1Defaults);
	ath9k_hw_set_operating_mode(ah, opmode);

	REG_WRITE(ah, AR_BSSMSKL, get_unaligned_le32(ahp->ah_bssidmask));
	REG_WRITE(ah, AR_BSSMSKU, get_unaligned_le16(ahp->ah_bssidmask + 4));

	REG_WRITE(ah, AR_DEF_ANTENNA, saveDefAntenna);

	REG_WRITE(ah, AR_BSS_ID0, get_unaligned_le32(ahp->ah_bssid));
	REG_WRITE(ah, AR_BSS_ID1, get_unaligned_le16(ahp->ah_bssid + 4) |
		  ((ahp->ah_assocId & 0x3fff) << AR_BSS_ID1_AID_S));

	REG_WRITE(ah, AR_ISR, ~0);

	REG_WRITE(ah, AR_RSSI_THR, INIT_RSSI_THR);

	if (AR_SREV_9280_10_OR_LATER(ah)) {
		if (!(ath9k_hw_ar9280_set_channel(ah, chan)))
			FAIL(-EIO);
	} else {
		if (!(ath9k_hw_set_channel(ah, chan)))
			FAIL(-EIO);
	}

	for (i = 0; i < AR_NUM_DCU; i++)
		REG_WRITE(ah, AR_DQCUMASK(i), 1 << i);

	ahp->ah_intrTxqs = 0;
	for (i = 0; i < ah->ah_caps.total_queues; i++)
		ath9k_hw_resettxqueue(ah, i);

	ath9k_hw_init_interrupt_masks(ah, opmode);
	ath9k_hw_init_qos(ah);

	ath9k_hw_init_user_settings(ah);

	ah->ah_opmode = opmode;

	REG_WRITE(ah, AR_STA_ID1,
		  REG_READ(ah, AR_STA_ID1) | AR_STA_ID1_PRESERVE_SEQNUM);

	ath9k_hw_set_dma(ah);

	REG_WRITE(ah, AR_OBS, 8);

	if (ahp->ah_intrMitigation) {

		REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_LAST, 500);
		REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_FIRST, 2000);
	}

	ath9k_hw_init_bb(ah, chan);

	if (!ath9k_hw_init_cal(ah, chan))
		FAIL(-ENODEV);

	rx_chainmask = ahp->ah_rxchainmask;
	if ((rx_chainmask == 0x5) || (rx_chainmask == 0x3)) {
		REG_WRITE(ah, AR_PHY_RX_CHAINMASK, rx_chainmask);
		REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, rx_chainmask);
	}

	REG_WRITE(ah, AR_CFG_LED, saveLedState | AR_CFG_SCLK_32KHZ);

	if (AR_SREV_9100(ah)) {
		u32 mask;
		mask = REG_READ(ah, AR_CFG);
		if (mask & (AR_CFG_SWRB | AR_CFG_SWTB | AR_CFG_SWRG)) {
			DPRINTF(ah->ah_sc, ATH_DBG_RESET,
				 "%s CFG Byte Swap Set 0x%x\n", __func__,
				 mask);
		} else {
			mask =
				INIT_CONFIG_STATUS | AR_CFG_SWRB | AR_CFG_SWTB;
			REG_WRITE(ah, AR_CFG, mask);
			DPRINTF(ah->ah_sc, ATH_DBG_RESET,
				 "%s Setting CFG 0x%x\n", __func__,
				 REG_READ(ah, AR_CFG));
		}
	} else {
#ifdef __BIG_ENDIAN
		REG_WRITE(ah, AR_CFG, AR_CFG_SWTD | AR_CFG_SWRD);
#endif
	}

	return true;
bad:
	if (status)
		*status = ecode;
	return false;
#undef FAIL
}

bool ath9k_hw_phy_disable(struct ath_hal *ah)
{
	return ath9k_hw_set_reset_reg(ah, ATH9K_RESET_WARM);
}

bool ath9k_hw_disable(struct ath_hal *ah)
{
	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE))
		return false;

	return ath9k_hw_set_reset_reg(ah, ATH9K_RESET_COLD);
}

bool
ath9k_hw_calibrate(struct ath_hal *ah, struct ath9k_channel *chan,
		   u8 rxchainmask, bool longcal,
		   bool *isCalDone)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct hal_cal_list *currCal = ahp->ah_cal_list_curr;
	struct ath9k_channel *ichan =
		ath9k_regd_check_channel(ah, chan);

	*isCalDone = true;

	if (ichan == NULL) {
		DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
			 "%s: invalid channel %u/0x%x; no mapping\n",
			 __func__, chan->channel, chan->channelFlags);
		return false;
	}

	if (currCal &&
	    (currCal->calState == CAL_RUNNING ||
	     currCal->calState == CAL_WAITING)) {
		ath9k_hw_per_calibration(ah, ichan, rxchainmask, currCal,
					 isCalDone);
		if (*isCalDone) {
			ahp->ah_cal_list_curr = currCal = currCal->calNext;

			if (currCal->calState == CAL_WAITING) {
				*isCalDone = false;
				ath9k_hw_reset_calibration(ah, currCal);
			}
		}
	}

	if (longcal) {
		ath9k_hw_getnf(ah, ichan);
		ath9k_hw_loadnf(ah, ah->ah_curchan);
		ath9k_hw_start_nfcal(ah);

		if ((ichan->channelFlags & CHANNEL_CW_INT) != 0) {

			chan->channelFlags |= CHANNEL_CW_INT;
			ichan->channelFlags &= ~CHANNEL_CW_INT;
		}
	}

	return true;
}

static void ath9k_hw_iqcal_collect(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		ahp->ah_totalPowerMeasI[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_0(i));
		ahp->ah_totalPowerMeasQ[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_1(i));
		ahp->ah_totalIqCorrMeas[i] +=
			(int32_t) REG_READ(ah, AR_PHY_CAL_MEAS_2(i));
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "%d: Chn %d pmi=0x%08x;pmq=0x%08x;iqcm=0x%08x;\n",
			 ahp->ah_CalSamples, i, ahp->ah_totalPowerMeasI[i],
			 ahp->ah_totalPowerMeasQ[i],
			 ahp->ah_totalIqCorrMeas[i]);
	}
}

static void ath9k_hw_adc_gaincal_collect(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		ahp->ah_totalAdcIOddPhase[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_0(i));
		ahp->ah_totalAdcIEvenPhase[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_1(i));
		ahp->ah_totalAdcQOddPhase[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_2(i));
		ahp->ah_totalAdcQEvenPhase[i] +=
			REG_READ(ah, AR_PHY_CAL_MEAS_3(i));

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"%d: Chn %d oddi=0x%08x; eveni=0x%08x; "
			"oddq=0x%08x; evenq=0x%08x;\n",
			 ahp->ah_CalSamples, i,
			 ahp->ah_totalAdcIOddPhase[i],
			 ahp->ah_totalAdcIEvenPhase[i],
			 ahp->ah_totalAdcQOddPhase[i],
			 ahp->ah_totalAdcQEvenPhase[i]);
	}
}

static void ath9k_hw_adc_dccal_collect(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int i;

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		ahp->ah_totalAdcDcOffsetIOddPhase[i] +=
			(int32_t) REG_READ(ah, AR_PHY_CAL_MEAS_0(i));
		ahp->ah_totalAdcDcOffsetIEvenPhase[i] +=
			(int32_t) REG_READ(ah, AR_PHY_CAL_MEAS_1(i));
		ahp->ah_totalAdcDcOffsetQOddPhase[i] +=
			(int32_t) REG_READ(ah, AR_PHY_CAL_MEAS_2(i));
		ahp->ah_totalAdcDcOffsetQEvenPhase[i] +=
			(int32_t) REG_READ(ah, AR_PHY_CAL_MEAS_3(i));

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			"%d: Chn %d oddi=0x%08x; eveni=0x%08x; "
			"oddq=0x%08x; evenq=0x%08x;\n",
			 ahp->ah_CalSamples, i,
			 ahp->ah_totalAdcDcOffsetIOddPhase[i],
			 ahp->ah_totalAdcDcOffsetIEvenPhase[i],
			 ahp->ah_totalAdcDcOffsetQOddPhase[i],
			 ahp->ah_totalAdcDcOffsetQEvenPhase[i]);
	}
}

static void ath9k_hw_iqcalibrate(struct ath_hal *ah, u8 numChains)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	u32 powerMeasQ, powerMeasI, iqCorrMeas;
	u32 qCoffDenom, iCoffDenom;
	int32_t qCoff, iCoff;
	int iqCorrNeg, i;

	for (i = 0; i < numChains; i++) {
		powerMeasI = ahp->ah_totalPowerMeasI[i];
		powerMeasQ = ahp->ah_totalPowerMeasQ[i];
		iqCorrMeas = ahp->ah_totalIqCorrMeas[i];

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Starting IQ Cal and Correction for Chain %d\n",
			 i);

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Orignal: Chn %diq_corr_meas = 0x%08x\n",
			 i, ahp->ah_totalIqCorrMeas[i]);

		iqCorrNeg = 0;


		if (iqCorrMeas > 0x80000000) {
			iqCorrMeas = (0xffffffff - iqCorrMeas) + 1;
			iqCorrNeg = 1;
		}

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Chn %d pwr_meas_i = 0x%08x\n", i, powerMeasI);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Chn %d pwr_meas_q = 0x%08x\n", i, powerMeasQ);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE, "iqCorrNeg is 0x%08x\n",
			 iqCorrNeg);

		iCoffDenom = (powerMeasI / 2 + powerMeasQ / 2) / 128;
		qCoffDenom = powerMeasQ / 64;

		if (powerMeasQ != 0) {

			iCoff = iqCorrMeas / iCoffDenom;
			qCoff = powerMeasI / qCoffDenom - 64;
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				 "Chn %d iCoff = 0x%08x\n", i, iCoff);
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				 "Chn %d qCoff = 0x%08x\n", i, qCoff);


			iCoff = iCoff & 0x3f;
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				 "New: Chn %d iCoff = 0x%08x\n", i, iCoff);
			if (iqCorrNeg == 0x0)
				iCoff = 0x40 - iCoff;

			if (qCoff > 15)
				qCoff = 15;
			else if (qCoff <= -16)
				qCoff = 16;

			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				 "Chn %d : iCoff = 0x%x  qCoff = 0x%x\n",
				i, iCoff, qCoff);

			REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4(i),
				      AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF,
				      iCoff);
			REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4(i),
				      AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF,
				      qCoff);
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				"IQ Cal and Correction done for Chain %d\n",
				i);
		}
	}

	REG_SET_BIT(ah, AR_PHY_TIMING_CTRL4(0),
		    AR_PHY_TIMING_CTRL4_IQCORR_ENABLE);
}

static void
ath9k_hw_adc_gaincal_calibrate(struct ath_hal *ah, u8 numChains)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	u32 iOddMeasOffset, iEvenMeasOffset, qOddMeasOffset,
		qEvenMeasOffset;
	u32 qGainMismatch, iGainMismatch, val, i;

	for (i = 0; i < numChains; i++) {
		iOddMeasOffset = ahp->ah_totalAdcIOddPhase[i];
		iEvenMeasOffset = ahp->ah_totalAdcIEvenPhase[i];
		qOddMeasOffset = ahp->ah_totalAdcQOddPhase[i];
		qEvenMeasOffset = ahp->ah_totalAdcQEvenPhase[i];

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Starting ADC Gain Cal for Chain %d\n", i);

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Chn %d pwr_meas_odd_i = 0x%08x\n", i,
			 iOddMeasOffset);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Chn %d pwr_meas_even_i = 0x%08x\n", i,
			 iEvenMeasOffset);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Chn %d pwr_meas_odd_q = 0x%08x\n", i,
			 qOddMeasOffset);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Chn %d pwr_meas_even_q = 0x%08x\n", i,
			 qEvenMeasOffset);

		if (iOddMeasOffset != 0 && qEvenMeasOffset != 0) {
			iGainMismatch =
				((iEvenMeasOffset * 32) /
				 iOddMeasOffset) & 0x3f;
			qGainMismatch =
				((qOddMeasOffset * 32) /
				 qEvenMeasOffset) & 0x3f;

			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				 "Chn %d gain_mismatch_i = 0x%08x\n", i,
				 iGainMismatch);
			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				 "Chn %d gain_mismatch_q = 0x%08x\n", i,
				 qGainMismatch);

			val = REG_READ(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(i));
			val &= 0xfffff000;
			val |= (qGainMismatch) | (iGainMismatch << 6);
			REG_WRITE(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(i), val);

			DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
				 "ADC Gain Cal done for Chain %d\n", i);
		}
	}

	REG_WRITE(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(0),
		  REG_READ(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(0)) |
		  AR_PHY_NEW_ADC_GAIN_CORR_ENABLE);
}

static void
ath9k_hw_adc_dccal_calibrate(struct ath_hal *ah, u8 numChains)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	u32 iOddMeasOffset, iEvenMeasOffset, val, i;
	int32_t qOddMeasOffset, qEvenMeasOffset, qDcMismatch, iDcMismatch;
	const struct hal_percal_data *calData =
		ahp->ah_cal_list_curr->calData;
	u32 numSamples =
		(1 << (calData->calCountMax + 5)) * calData->calNumSamples;

	for (i = 0; i < numChains; i++) {
		iOddMeasOffset = ahp->ah_totalAdcDcOffsetIOddPhase[i];
		iEvenMeasOffset = ahp->ah_totalAdcDcOffsetIEvenPhase[i];
		qOddMeasOffset = ahp->ah_totalAdcDcOffsetQOddPhase[i];
		qEvenMeasOffset = ahp->ah_totalAdcDcOffsetQEvenPhase[i];

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Starting ADC DC Offset Cal for Chain %d\n", i);

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Chn %d pwr_meas_odd_i = %d\n", i,
			 iOddMeasOffset);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Chn %d pwr_meas_even_i = %d\n", i,
			 iEvenMeasOffset);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Chn %d pwr_meas_odd_q = %d\n", i,
			 qOddMeasOffset);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Chn %d pwr_meas_even_q = %d\n", i,
			 qEvenMeasOffset);

		iDcMismatch = (((iEvenMeasOffset - iOddMeasOffset) * 2) /
			       numSamples) & 0x1ff;
		qDcMismatch = (((qOddMeasOffset - qEvenMeasOffset) * 2) /
			       numSamples) & 0x1ff;

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Chn %d dc_offset_mismatch_i = 0x%08x\n", i,
			 iDcMismatch);
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "Chn %d dc_offset_mismatch_q = 0x%08x\n", i,
			 qDcMismatch);

		val = REG_READ(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(i));
		val &= 0xc0000fff;
		val |= (qDcMismatch << 12) | (iDcMismatch << 21);
		REG_WRITE(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(i), val);

		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "ADC DC Offset Cal done for Chain %d\n", i);
	}

	REG_WRITE(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(0),
		  REG_READ(ah, AR_PHY_NEW_ADC_DC_GAIN_CORR(0)) |
		  AR_PHY_NEW_ADC_DC_OFFSET_CORR_ENABLE);
}

bool ath9k_hw_set_txpowerlimit(struct ath_hal *ah, u32 limit)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_channel *chan = ah->ah_curchan;

	ah->ah_powerLimit = min(limit, (u32) MAX_RATE_POWER);

	if (ath9k_hw_set_txpower(ah, &ahp->ah_eeprom, chan,
				 ath9k_regd_get_ctl(ah, chan),
				 ath9k_regd_get_antenna_allowed(ah,
								chan),
				 chan->maxRegTxPower * 2,
				 min((u32) MAX_RATE_POWER,
				     (u32) ah->ah_powerLimit)) != 0)
		return false;

	return true;
}

void
ath9k_hw_get_channel_centers(struct ath_hal *ah,
			     struct ath9k_channel *chan,
			     struct chan_centers *centers)
{
	int8_t extoff;
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (!IS_CHAN_HT40(chan)) {
		centers->ctl_center = centers->ext_center =
			centers->synth_center = chan->channel;
		return;
	}

	if ((chan->chanmode == CHANNEL_A_HT40PLUS) ||
	    (chan->chanmode == CHANNEL_G_HT40PLUS)) {
		centers->synth_center =
			chan->channel + HT40_CHANNEL_CENTER_SHIFT;
		extoff = 1;
	} else {
		centers->synth_center =
			chan->channel - HT40_CHANNEL_CENTER_SHIFT;
		extoff = -1;
	}

	centers->ctl_center = centers->synth_center - (extoff *
		HT40_CHANNEL_CENTER_SHIFT);
	centers->ext_center = centers->synth_center + (extoff *
		((ahp->
		ah_extprotspacing
		==
		ATH9K_HT_EXTPROTSPACING_20)
		?
		HT40_CHANNEL_CENTER_SHIFT
		: 15));

}

void
ath9k_hw_reset_calvalid(struct ath_hal *ah, struct ath9k_channel *chan,
			bool *isCalDone)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_channel *ichan =
		ath9k_regd_check_channel(ah, chan);
	struct hal_cal_list *currCal = ahp->ah_cal_list_curr;

	*isCalDone = true;

	if (!AR_SREV_9100(ah) && !AR_SREV_9160_10_OR_LATER(ah))
		return;

	if (currCal == NULL)
		return;

	if (ichan == NULL) {
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "%s: invalid channel %u/0x%x; no mapping\n",
			 __func__, chan->channel, chan->channelFlags);
		return;
	}


	if (currCal->calState != CAL_DONE) {
		DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
			 "%s: Calibration state incorrect, %d\n",
			 __func__, currCal->calState);
		return;
	}


	if (!ath9k_hw_iscal_supported(ah, chan, currCal->calData->calType))
		return;

	DPRINTF(ah->ah_sc, ATH_DBG_CALIBRATE,
		 "%s: Resetting Cal %d state for channel %u/0x%x\n",
		 __func__, currCal->calData->calType, chan->channel,
		 chan->channelFlags);

	ichan->CalValid &= ~currCal->calData->calType;
	currCal->calState = CAL_WAITING;

	*isCalDone = false;
}

void ath9k_hw_getmac(struct ath_hal *ah, u8 *mac)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	memcpy(mac, ahp->ah_macaddr, ETH_ALEN);
}

bool ath9k_hw_setmac(struct ath_hal *ah, const u8 *mac)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	memcpy(ahp->ah_macaddr, mac, ETH_ALEN);
	return true;
}

void ath9k_hw_getbssidmask(struct ath_hal *ah, u8 *mask)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	memcpy(mask, ahp->ah_bssidmask, ETH_ALEN);
}

bool
ath9k_hw_setbssidmask(struct ath_hal *ah, const u8 *mask)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	memcpy(ahp->ah_bssidmask, mask, ETH_ALEN);

	REG_WRITE(ah, AR_BSSMSKL, get_unaligned_le32(ahp->ah_bssidmask));
	REG_WRITE(ah, AR_BSSMSKU, get_unaligned_le16(ahp->ah_bssidmask + 4));

	return true;
}

#ifdef CONFIG_ATH9K_RFKILL
static void ath9k_enable_rfkill(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL,
		    AR_GPIO_INPUT_EN_VAL_RFSILENT_BB);

	REG_CLR_BIT(ah, AR_GPIO_INPUT_MUX2,
		    AR_GPIO_INPUT_MUX2_RFSILENT);

	ath9k_hw_cfg_gpio_input(ah, ahp->ah_gpioSelect);
	REG_SET_BIT(ah, AR_PHY_TEST, RFSILENT_BB);

	if (ahp->ah_gpioBit == ath9k_hw_gpio_get(ah, ahp->ah_gpioSelect)) {

		ath9k_hw_set_gpio_intr(ah, ahp->ah_gpioSelect,
				       !ahp->ah_gpioBit);
	} else {
		ath9k_hw_set_gpio_intr(ah, ahp->ah_gpioSelect,
				       ahp->ah_gpioBit);
	}
}
#endif

void
ath9k_hw_write_associd(struct ath_hal *ah, const u8 *bssid,
		       u16 assocId)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	memcpy(ahp->ah_bssid, bssid, ETH_ALEN);
	ahp->ah_assocId = assocId;

	REG_WRITE(ah, AR_BSS_ID0, get_unaligned_le32(ahp->ah_bssid));
	REG_WRITE(ah, AR_BSS_ID1, get_unaligned_le16(ahp->ah_bssid + 4) |
		  ((assocId & 0x3fff) << AR_BSS_ID1_AID_S));
}

u64 ath9k_hw_gettsf64(struct ath_hal *ah)
{
	u64 tsf;

	tsf = REG_READ(ah, AR_TSF_U32);
	tsf = (tsf << 32) | REG_READ(ah, AR_TSF_L32);
	return tsf;
}

void ath9k_hw_reset_tsf(struct ath_hal *ah)
{
	int count;

	count = 0;
	while (REG_READ(ah, AR_SLP32_MODE) & AR_SLP32_TSF_WRITE_STATUS) {
		count++;
		if (count > 10) {
			DPRINTF(ah->ah_sc, ATH_DBG_RESET,
			 "%s: AR_SLP32_TSF_WRITE_STATUS limit exceeded\n",
				 __func__);
			break;
		}
		udelay(10);
	}
	REG_WRITE(ah, AR_RESET_TSF, AR_RESET_TSF_ONCE);
}

u32 ath9k_hw_getdefantenna(struct ath_hal *ah)
{
	return REG_READ(ah, AR_DEF_ANTENNA) & 0x7;
}

void ath9k_hw_setantenna(struct ath_hal *ah, u32 antenna)
{
	REG_WRITE(ah, AR_DEF_ANTENNA, (antenna & 0x7));
}

bool
ath9k_hw_setantennaswitch(struct ath_hal *ah,
			  enum ath9k_ant_setting settings,
			  struct ath9k_channel *chan,
			  u8 *tx_chainmask,
			  u8 *rx_chainmask,
			  u8 *antenna_cfgd)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	static u8 tx_chainmask_cfg, rx_chainmask_cfg;

	if (AR_SREV_9280(ah)) {
		if (!tx_chainmask_cfg) {

			tx_chainmask_cfg = *tx_chainmask;
			rx_chainmask_cfg = *rx_chainmask;
		}

		switch (settings) {
		case ATH9K_ANT_FIXED_A:
			*tx_chainmask = ATH9K_ANTENNA0_CHAINMASK;
			*rx_chainmask = ATH9K_ANTENNA0_CHAINMASK;
			*antenna_cfgd = true;
			break;
		case ATH9K_ANT_FIXED_B:
			if (ah->ah_caps.tx_chainmask >
			    ATH9K_ANTENNA1_CHAINMASK) {
				*tx_chainmask = ATH9K_ANTENNA1_CHAINMASK;
			}
			*rx_chainmask = ATH9K_ANTENNA1_CHAINMASK;
			*antenna_cfgd = true;
			break;
		case ATH9K_ANT_VARIABLE:
			*tx_chainmask = tx_chainmask_cfg;
			*rx_chainmask = rx_chainmask_cfg;
			*antenna_cfgd = true;
			break;
		default:
			break;
		}
	} else {
		ahp->ah_diversityControl = settings;
	}

	return true;
}

void ath9k_hw_setopmode(struct ath_hal *ah)
{
	ath9k_hw_set_operating_mode(ah, ah->ah_opmode);
}

bool
ath9k_hw_getcapability(struct ath_hal *ah, enum ath9k_capability_type type,
		       u32 capability, u32 *result)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	const struct ath9k_hw_capabilities *pCap = &ah->ah_caps;

	switch (type) {
	case ATH9K_CAP_CIPHER:
		switch (capability) {
		case ATH9K_CIPHER_AES_CCM:
		case ATH9K_CIPHER_AES_OCB:
		case ATH9K_CIPHER_TKIP:
		case ATH9K_CIPHER_WEP:
		case ATH9K_CIPHER_MIC:
		case ATH9K_CIPHER_CLR:
			return true;
		default:
			return false;
		}
	case ATH9K_CAP_TKIP_MIC:
		switch (capability) {
		case 0:
			return true;
		case 1:
			return (ahp->ah_staId1Defaults &
				AR_STA_ID1_CRPT_MIC_ENABLE) ? true :
			false;
		}
	case ATH9K_CAP_TKIP_SPLIT:
		return (ahp->ah_miscMode & AR_PCU_MIC_NEW_LOC_ENA) ?
			false : true;
	case ATH9K_CAP_WME_TKIPMIC:
		return 0;
	case ATH9K_CAP_PHYCOUNTERS:
		return ahp->ah_hasHwPhyCounters ? 0 : -ENXIO;
	case ATH9K_CAP_DIVERSITY:
		return (REG_READ(ah, AR_PHY_CCK_DETECT) &
			AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV) ?
			true : false;
	case ATH9K_CAP_PHYDIAG:
		return true;
	case ATH9K_CAP_MCAST_KEYSRCH:
		switch (capability) {
		case 0:
			return true;
		case 1:
			if (REG_READ(ah, AR_STA_ID1) & AR_STA_ID1_ADHOC) {
				return false;
			} else {
				return (ahp->ah_staId1Defaults &
					AR_STA_ID1_MCAST_KSRCH) ? true :
					false;
			}
		}
		return false;
	case ATH9K_CAP_TSF_ADJUST:
		return (ahp->ah_miscMode & AR_PCU_TX_ADD_TSF) ?
			true : false;
	case ATH9K_CAP_RFSILENT:
		if (capability == 3)
			return false;
	case ATH9K_CAP_ANT_CFG_2GHZ:
		*result = pCap->num_antcfg_2ghz;
		return true;
	case ATH9K_CAP_ANT_CFG_5GHZ:
		*result = pCap->num_antcfg_5ghz;
		return true;
	case ATH9K_CAP_TXPOW:
		switch (capability) {
		case 0:
			return 0;
		case 1:
			*result = ah->ah_powerLimit;
			return 0;
		case 2:
			*result = ah->ah_maxPowerLevel;
			return 0;
		case 3:
			*result = ah->ah_tpScale;
			return 0;
		}
		return false;
	default:
		return false;
	}
}

int
ath9k_hw_select_antconfig(struct ath_hal *ah, u32 cfg)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_channel *chan = ah->ah_curchan;
	const struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	u16 ant_config;
	u32 halNumAntConfig;

	halNumAntConfig =
		IS_CHAN_2GHZ(chan) ? pCap->num_antcfg_2ghz : pCap->
		num_antcfg_5ghz;

	if (cfg < halNumAntConfig) {
		if (!ath9k_hw_get_eeprom_antenna_cfg(ahp, chan,
						     cfg, &ant_config)) {
			REG_WRITE(ah, AR_PHY_SWITCH_COM, ant_config);
			return 0;
		}
	}

	return -EINVAL;
}

bool ath9k_hw_intrpend(struct ath_hal *ah)
{
	u32 host_isr;

	if (AR_SREV_9100(ah))
		return true;

	host_isr = REG_READ(ah, AR_INTR_ASYNC_CAUSE);
	if ((host_isr & AR_INTR_MAC_IRQ) && (host_isr != AR_INTR_SPURIOUS))
		return true;

	host_isr = REG_READ(ah, AR_INTR_SYNC_CAUSE);
	if ((host_isr & AR_INTR_SYNC_DEFAULT)
	    && (host_isr != AR_INTR_SPURIOUS))
		return true;

	return false;
}

bool ath9k_hw_getisr(struct ath_hal *ah, enum ath9k_int *masked)
{
	u32 isr = 0;
	u32 mask2 = 0;
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	u32 sync_cause = 0;
	bool fatal_int = false;

	if (!AR_SREV_9100(ah)) {
		if (REG_READ(ah, AR_INTR_ASYNC_CAUSE) & AR_INTR_MAC_IRQ) {
			if ((REG_READ(ah, AR_RTC_STATUS) & AR_RTC_STATUS_M)
			    == AR_RTC_STATUS_ON) {
				isr = REG_READ(ah, AR_ISR);
			}
		}

		sync_cause =
			REG_READ(ah,
				 AR_INTR_SYNC_CAUSE) & AR_INTR_SYNC_DEFAULT;

		*masked = 0;

		if (!isr && !sync_cause)
			return false;
	} else {
		*masked = 0;
		isr = REG_READ(ah, AR_ISR);
	}

	if (isr) {
		struct ath_hal_5416 *ahp = AH5416(ah);

		if (isr & AR_ISR_BCNMISC) {
			u32 isr2;
			isr2 = REG_READ(ah, AR_ISR_S2);
			if (isr2 & AR_ISR_S2_TIM)
				mask2 |= ATH9K_INT_TIM;
			if (isr2 & AR_ISR_S2_DTIM)
				mask2 |= ATH9K_INT_DTIM;
			if (isr2 & AR_ISR_S2_DTIMSYNC)
				mask2 |= ATH9K_INT_DTIMSYNC;
			if (isr2 & (AR_ISR_S2_CABEND))
				mask2 |= ATH9K_INT_CABEND;
			if (isr2 & AR_ISR_S2_GTT)
				mask2 |= ATH9K_INT_GTT;
			if (isr2 & AR_ISR_S2_CST)
				mask2 |= ATH9K_INT_CST;
		}

		isr = REG_READ(ah, AR_ISR_RAC);
		if (isr == 0xffffffff) {
			*masked = 0;
			return false;
		}

		*masked = isr & ATH9K_INT_COMMON;

		if (ahp->ah_intrMitigation) {

			if (isr & (AR_ISR_RXMINTR | AR_ISR_RXINTM))
				*masked |= ATH9K_INT_RX;
		}

		if (isr & (AR_ISR_RXOK | AR_ISR_RXERR))
			*masked |= ATH9K_INT_RX;
		if (isr &
		    (AR_ISR_TXOK | AR_ISR_TXDESC | AR_ISR_TXERR |
		     AR_ISR_TXEOL)) {
			u32 s0_s, s1_s;

			*masked |= ATH9K_INT_TX;

			s0_s = REG_READ(ah, AR_ISR_S0_S);
			ahp->ah_intrTxqs |= MS(s0_s, AR_ISR_S0_QCU_TXOK);
			ahp->ah_intrTxqs |= MS(s0_s, AR_ISR_S0_QCU_TXDESC);

			s1_s = REG_READ(ah, AR_ISR_S1_S);
			ahp->ah_intrTxqs |= MS(s1_s, AR_ISR_S1_QCU_TXERR);
			ahp->ah_intrTxqs |= MS(s1_s, AR_ISR_S1_QCU_TXEOL);
		}

		if (isr & AR_ISR_RXORN) {
			DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT,
				 "%s: receive FIFO overrun interrupt\n",
				 __func__);
		}

		if (!AR_SREV_9100(ah)) {
			if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP)) {
				u32 isr5 = REG_READ(ah, AR_ISR_S5_S);
				if (isr5 & AR_ISR_S5_TIM_TIMER)
					*masked |= ATH9K_INT_TIM_TIMER;
			}
		}

		*masked |= mask2;
	}
	if (AR_SREV_9100(ah))
		return true;
	if (sync_cause) {
		fatal_int =
			(sync_cause &
			 (AR_INTR_SYNC_HOST1_FATAL | AR_INTR_SYNC_HOST1_PERR))
			? true : false;

		if (fatal_int) {
			if (sync_cause & AR_INTR_SYNC_HOST1_FATAL) {
				DPRINTF(ah->ah_sc, ATH_DBG_ANY,
					 "%s: received PCI FATAL interrupt\n",
					 __func__);
			}
			if (sync_cause & AR_INTR_SYNC_HOST1_PERR) {
				DPRINTF(ah->ah_sc, ATH_DBG_ANY,
					 "%s: received PCI PERR interrupt\n",
					 __func__);
			}
		}
		if (sync_cause & AR_INTR_SYNC_RADM_CPL_TIMEOUT) {
			DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT,
				 "%s: AR_INTR_SYNC_RADM_CPL_TIMEOUT\n",
				 __func__);
			REG_WRITE(ah, AR_RC, AR_RC_HOSTIF);
			REG_WRITE(ah, AR_RC, 0);
			*masked |= ATH9K_INT_FATAL;
		}
		if (sync_cause & AR_INTR_SYNC_LOCAL_TIMEOUT) {
			DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT,
				 "%s: AR_INTR_SYNC_LOCAL_TIMEOUT\n",
				 __func__);
		}

		REG_WRITE(ah, AR_INTR_SYNC_CAUSE_CLR, sync_cause);
		(void) REG_READ(ah, AR_INTR_SYNC_CAUSE_CLR);
	}
	return true;
}

enum ath9k_int ath9k_hw_intrget(struct ath_hal *ah)
{
	return AH5416(ah)->ah_maskReg;
}

enum ath9k_int ath9k_hw_set_interrupts(struct ath_hal *ah, enum ath9k_int ints)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	u32 omask = ahp->ah_maskReg;
	u32 mask, mask2;
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;

	DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "%s: 0x%x => 0x%x\n", __func__,
		 omask, ints);

	if (omask & ATH9K_INT_GLOBAL) {
		DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "%s: disable IER\n",
			 __func__);
		REG_WRITE(ah, AR_IER, AR_IER_DISABLE);
		(void) REG_READ(ah, AR_IER);
		if (!AR_SREV_9100(ah)) {
			REG_WRITE(ah, AR_INTR_ASYNC_ENABLE, 0);
			(void) REG_READ(ah, AR_INTR_ASYNC_ENABLE);

			REG_WRITE(ah, AR_INTR_SYNC_ENABLE, 0);
			(void) REG_READ(ah, AR_INTR_SYNC_ENABLE);
		}
	}

	mask = ints & ATH9K_INT_COMMON;
	mask2 = 0;

	if (ints & ATH9K_INT_TX) {
		if (ahp->ah_txOkInterruptMask)
			mask |= AR_IMR_TXOK;
		if (ahp->ah_txDescInterruptMask)
			mask |= AR_IMR_TXDESC;
		if (ahp->ah_txErrInterruptMask)
			mask |= AR_IMR_TXERR;
		if (ahp->ah_txEolInterruptMask)
			mask |= AR_IMR_TXEOL;
	}
	if (ints & ATH9K_INT_RX) {
		mask |= AR_IMR_RXERR;
		if (ahp->ah_intrMitigation)
			mask |= AR_IMR_RXMINTR | AR_IMR_RXINTM;
		else
			mask |= AR_IMR_RXOK | AR_IMR_RXDESC;
		if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP))
			mask |= AR_IMR_GENTMR;
	}

	if (ints & (ATH9K_INT_BMISC)) {
		mask |= AR_IMR_BCNMISC;
		if (ints & ATH9K_INT_TIM)
			mask2 |= AR_IMR_S2_TIM;
		if (ints & ATH9K_INT_DTIM)
			mask2 |= AR_IMR_S2_DTIM;
		if (ints & ATH9K_INT_DTIMSYNC)
			mask2 |= AR_IMR_S2_DTIMSYNC;
		if (ints & ATH9K_INT_CABEND)
			mask2 |= (AR_IMR_S2_CABEND);
	}

	if (ints & (ATH9K_INT_GTT | ATH9K_INT_CST)) {
		mask |= AR_IMR_BCNMISC;
		if (ints & ATH9K_INT_GTT)
			mask2 |= AR_IMR_S2_GTT;
		if (ints & ATH9K_INT_CST)
			mask2 |= AR_IMR_S2_CST;
	}

	DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "%s: new IMR 0x%x\n", __func__,
		 mask);
	REG_WRITE(ah, AR_IMR, mask);
	mask = REG_READ(ah, AR_IMR_S2) & ~(AR_IMR_S2_TIM |
					   AR_IMR_S2_DTIM |
					   AR_IMR_S2_DTIMSYNC |
					   AR_IMR_S2_CABEND |
					   AR_IMR_S2_CABTO |
					   AR_IMR_S2_TSFOOR |
					   AR_IMR_S2_GTT | AR_IMR_S2_CST);
	REG_WRITE(ah, AR_IMR_S2, mask | mask2);
	ahp->ah_maskReg = ints;

	if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP)) {
		if (ints & ATH9K_INT_TIM_TIMER)
			REG_SET_BIT(ah, AR_IMR_S5, AR_IMR_S5_TIM_TIMER);
		else
			REG_CLR_BIT(ah, AR_IMR_S5, AR_IMR_S5_TIM_TIMER);
	}

	if (ints & ATH9K_INT_GLOBAL) {
		DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "%s: enable IER\n",
			 __func__);
		REG_WRITE(ah, AR_IER, AR_IER_ENABLE);
		if (!AR_SREV_9100(ah)) {
			REG_WRITE(ah, AR_INTR_ASYNC_ENABLE,
				  AR_INTR_MAC_IRQ);
			REG_WRITE(ah, AR_INTR_ASYNC_MASK, AR_INTR_MAC_IRQ);


			REG_WRITE(ah, AR_INTR_SYNC_ENABLE,
				  AR_INTR_SYNC_DEFAULT);
			REG_WRITE(ah, AR_INTR_SYNC_MASK,
				  AR_INTR_SYNC_DEFAULT);
		}
		DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "AR_IMR 0x%x IER 0x%x\n",
			 REG_READ(ah, AR_IMR), REG_READ(ah, AR_IER));
	}

	return omask;
}

void
ath9k_hw_beaconinit(struct ath_hal *ah,
		    u32 next_beacon, u32 beacon_period)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int flags = 0;

	ahp->ah_beaconInterval = beacon_period;

	switch (ah->ah_opmode) {
	case ATH9K_M_STA:
	case ATH9K_M_MONITOR:
		REG_WRITE(ah, AR_NEXT_TBTT_TIMER, TU_TO_USEC(next_beacon));
		REG_WRITE(ah, AR_NEXT_DMA_BEACON_ALERT, 0xffff);
		REG_WRITE(ah, AR_NEXT_SWBA, 0x7ffff);
		flags |= AR_TBTT_TIMER_EN;
		break;
	case ATH9K_M_IBSS:
		REG_SET_BIT(ah, AR_TXCFG,
			    AR_TXCFG_ADHOC_BEACON_ATIM_TX_POLICY);
		REG_WRITE(ah, AR_NEXT_NDP_TIMER,
			  TU_TO_USEC(next_beacon +
				     (ahp->ah_atimWindow ? ahp->
				      ah_atimWindow : 1)));
		flags |= AR_NDP_TIMER_EN;
	case ATH9K_M_HOSTAP:
		REG_WRITE(ah, AR_NEXT_TBTT_TIMER, TU_TO_USEC(next_beacon));
		REG_WRITE(ah, AR_NEXT_DMA_BEACON_ALERT,
			  TU_TO_USEC(next_beacon -
				     ah->ah_config.
				     dma_beacon_response_time));
		REG_WRITE(ah, AR_NEXT_SWBA,
			  TU_TO_USEC(next_beacon -
				     ah->ah_config.
				     sw_beacon_response_time));
		flags |=
			AR_TBTT_TIMER_EN | AR_DBA_TIMER_EN | AR_SWBA_TIMER_EN;
		break;
	}

	REG_WRITE(ah, AR_BEACON_PERIOD, TU_TO_USEC(beacon_period));
	REG_WRITE(ah, AR_DMA_BEACON_PERIOD, TU_TO_USEC(beacon_period));
	REG_WRITE(ah, AR_SWBA_PERIOD, TU_TO_USEC(beacon_period));
	REG_WRITE(ah, AR_NDP_PERIOD, TU_TO_USEC(beacon_period));

	beacon_period &= ~ATH9K_BEACON_ENA;
	if (beacon_period & ATH9K_BEACON_RESET_TSF) {
		beacon_period &= ~ATH9K_BEACON_RESET_TSF;
		ath9k_hw_reset_tsf(ah);
	}

	REG_SET_BIT(ah, AR_TIMER_MODE, flags);
}

void
ath9k_hw_set_sta_beacon_timers(struct ath_hal *ah,
			       const struct ath9k_beacon_state *bs)
{
	u32 nextTbtt, beaconintval, dtimperiod, beacontimeout;
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;

	REG_WRITE(ah, AR_NEXT_TBTT_TIMER, TU_TO_USEC(bs->bs_nexttbtt));

	REG_WRITE(ah, AR_BEACON_PERIOD,
		  TU_TO_USEC(bs->bs_intval & ATH9K_BEACON_PERIOD));
	REG_WRITE(ah, AR_DMA_BEACON_PERIOD,
		  TU_TO_USEC(bs->bs_intval & ATH9K_BEACON_PERIOD));

	REG_RMW_FIELD(ah, AR_RSSI_THR,
		      AR_RSSI_THR_BM_THR, bs->bs_bmissthreshold);

	beaconintval = bs->bs_intval & ATH9K_BEACON_PERIOD;

	if (bs->bs_sleepduration > beaconintval)
		beaconintval = bs->bs_sleepduration;

	dtimperiod = bs->bs_dtimperiod;
	if (bs->bs_sleepduration > dtimperiod)
		dtimperiod = bs->bs_sleepduration;

	if (beaconintval == dtimperiod)
		nextTbtt = bs->bs_nextdtim;
	else
		nextTbtt = bs->bs_nexttbtt;

	DPRINTF(ah->ah_sc, ATH_DBG_BEACON, "%s: next DTIM %d\n", __func__,
		 bs->bs_nextdtim);
	DPRINTF(ah->ah_sc, ATH_DBG_BEACON, "%s: next beacon %d\n", __func__,
		 nextTbtt);
	DPRINTF(ah->ah_sc, ATH_DBG_BEACON, "%s: beacon period %d\n", __func__,
		 beaconintval);
	DPRINTF(ah->ah_sc, ATH_DBG_BEACON, "%s: DTIM period %d\n", __func__,
		 dtimperiod);

	REG_WRITE(ah, AR_NEXT_DTIM,
		  TU_TO_USEC(bs->bs_nextdtim - SLEEP_SLOP));
	REG_WRITE(ah, AR_NEXT_TIM, TU_TO_USEC(nextTbtt - SLEEP_SLOP));

	REG_WRITE(ah, AR_SLEEP1,
		  SM((CAB_TIMEOUT_VAL << 3), AR_SLEEP1_CAB_TIMEOUT)
		  | AR_SLEEP1_ASSUME_DTIM);

	if (pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP)
		beacontimeout = (BEACON_TIMEOUT_VAL << 3);
	else
		beacontimeout = MIN_BEACON_TIMEOUT_VAL;

	REG_WRITE(ah, AR_SLEEP2,
		  SM(beacontimeout, AR_SLEEP2_BEACON_TIMEOUT));

	REG_WRITE(ah, AR_TIM_PERIOD, TU_TO_USEC(beaconintval));
	REG_WRITE(ah, AR_DTIM_PERIOD, TU_TO_USEC(dtimperiod));

	REG_SET_BIT(ah, AR_TIMER_MODE,
		    AR_TBTT_TIMER_EN | AR_TIM_TIMER_EN |
		    AR_DTIM_TIMER_EN);

}

bool ath9k_hw_keyisvalid(struct ath_hal *ah, u16 entry)
{
	if (entry < ah->ah_caps.keycache_size) {
		u32 val = REG_READ(ah, AR_KEYTABLE_MAC1(entry));
		if (val & AR_KEYTABLE_VALID)
			return true;
	}
	return false;
}

bool ath9k_hw_keyreset(struct ath_hal *ah, u16 entry)
{
	u32 keyType;

	if (entry >= ah->ah_caps.keycache_size) {
		DPRINTF(ah->ah_sc, ATH_DBG_KEYCACHE,
			 "%s: entry %u out of range\n", __func__, entry);
		return false;
	}
	keyType = REG_READ(ah, AR_KEYTABLE_TYPE(entry));

	REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), 0);
	REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), 0);
	REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), 0);
	REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), 0);
	REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), 0);
	REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), AR_KEYTABLE_TYPE_CLR);
	REG_WRITE(ah, AR_KEYTABLE_MAC0(entry), 0);
	REG_WRITE(ah, AR_KEYTABLE_MAC1(entry), 0);

	if (keyType == AR_KEYTABLE_TYPE_TKIP && ATH9K_IS_MIC_ENABLED(ah)) {
		u16 micentry = entry + 64;

		REG_WRITE(ah, AR_KEYTABLE_KEY0(micentry), 0);
		REG_WRITE(ah, AR_KEYTABLE_KEY1(micentry), 0);
		REG_WRITE(ah, AR_KEYTABLE_KEY2(micentry), 0);
		REG_WRITE(ah, AR_KEYTABLE_KEY3(micentry), 0);

	}

	if (ah->ah_curchan == NULL)
		return true;

	return true;
}

bool
ath9k_hw_keysetmac(struct ath_hal *ah, u16 entry,
		   const u8 *mac)
{
	u32 macHi, macLo;

	if (entry >= ah->ah_caps.keycache_size) {
		DPRINTF(ah->ah_sc, ATH_DBG_KEYCACHE,
			 "%s: entry %u out of range\n", __func__, entry);
		return false;
	}

	if (mac != NULL) {
		macHi = (mac[5] << 8) | mac[4];
		macLo = (mac[3] << 24) | (mac[2] << 16)
			| (mac[1] << 8) | mac[0];
		macLo >>= 1;
		macLo |= (macHi & 1) << 31;
		macHi >>= 1;
	} else {
		macLo = macHi = 0;
	}
	REG_WRITE(ah, AR_KEYTABLE_MAC0(entry), macLo);
	REG_WRITE(ah, AR_KEYTABLE_MAC1(entry), macHi | AR_KEYTABLE_VALID);

	return true;
}

bool
ath9k_hw_set_keycache_entry(struct ath_hal *ah, u16 entry,
			    const struct ath9k_keyval *k,
			    const u8 *mac, int xorKey)
{
	const struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	u32 key0, key1, key2, key3, key4;
	u32 keyType;
	u32 xorMask = xorKey ?
		(ATH9K_KEY_XOR << 24 | ATH9K_KEY_XOR << 16 | ATH9K_KEY_XOR << 8
		 | ATH9K_KEY_XOR) : 0;
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (entry >= pCap->keycache_size) {
		DPRINTF(ah->ah_sc, ATH_DBG_KEYCACHE,
			 "%s: entry %u out of range\n", __func__, entry);
		return false;
	}
	switch (k->kv_type) {
	case ATH9K_CIPHER_AES_OCB:
		keyType = AR_KEYTABLE_TYPE_AES;
		break;
	case ATH9K_CIPHER_AES_CCM:
		if (!(pCap->hw_caps & ATH9K_HW_CAP_CIPHER_AESCCM)) {
			DPRINTF(ah->ah_sc, ATH_DBG_KEYCACHE,
				 "%s: AES-CCM not supported by "
				 "mac rev 0x%x\n", __func__,
				 ah->ah_macRev);
			return false;
		}
		keyType = AR_KEYTABLE_TYPE_CCM;
		break;
	case ATH9K_CIPHER_TKIP:
		keyType = AR_KEYTABLE_TYPE_TKIP;
		if (ATH9K_IS_MIC_ENABLED(ah)
		    && entry + 64 >= pCap->keycache_size) {
			DPRINTF(ah->ah_sc, ATH_DBG_KEYCACHE,
				 "%s: entry %u inappropriate for TKIP\n",
				 __func__, entry);
			return false;
		}
		break;
	case ATH9K_CIPHER_WEP:
		if (k->kv_len < 40 / NBBY) {
			DPRINTF(ah->ah_sc, ATH_DBG_KEYCACHE,
				 "%s: WEP key length %u too small\n",
				 __func__, k->kv_len);
			return false;
		}
		if (k->kv_len <= 40 / NBBY)
			keyType = AR_KEYTABLE_TYPE_40;
		else if (k->kv_len <= 104 / NBBY)
			keyType = AR_KEYTABLE_TYPE_104;
		else
			keyType = AR_KEYTABLE_TYPE_128;
		break;
	case ATH9K_CIPHER_CLR:
		keyType = AR_KEYTABLE_TYPE_CLR;
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_KEYCACHE,
			 "%s: cipher %u not supported\n", __func__,
			 k->kv_type);
		return false;
	}

	key0 = get_unaligned_le32(k->kv_val + 0) ^ xorMask;
	key1 = (get_unaligned_le16(k->kv_val + 4) ^ xorMask) & 0xffff;
	key2 = get_unaligned_le32(k->kv_val + 6) ^ xorMask;
	key3 = (get_unaligned_le16(k->kv_val + 10) ^ xorMask) & 0xffff;
	key4 = get_unaligned_le32(k->kv_val + 12) ^ xorMask;
	if (k->kv_len <= 104 / NBBY)
		key4 &= 0xff;

	if (keyType == AR_KEYTABLE_TYPE_TKIP && ATH9K_IS_MIC_ENABLED(ah)) {
		u16 micentry = entry + 64;

		REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), ~key0);
		REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), ~key1);
		REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), key2);
		REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), key3);
		REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), key4);
		REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), keyType);
		(void) ath9k_hw_keysetmac(ah, entry, mac);

		if (ahp->ah_miscMode & AR_PCU_MIC_NEW_LOC_ENA) {
			u32 mic0, mic1, mic2, mic3, mic4;

			mic0 = get_unaligned_le32(k->kv_mic + 0);
			mic2 = get_unaligned_le32(k->kv_mic + 4);
			mic1 = get_unaligned_le16(k->kv_txmic + 2) & 0xffff;
			mic3 = get_unaligned_le16(k->kv_txmic + 0) & 0xffff;
			mic4 = get_unaligned_le32(k->kv_txmic + 4);
			REG_WRITE(ah, AR_KEYTABLE_KEY0(micentry), mic0);
			REG_WRITE(ah, AR_KEYTABLE_KEY1(micentry), mic1);
			REG_WRITE(ah, AR_KEYTABLE_KEY2(micentry), mic2);
			REG_WRITE(ah, AR_KEYTABLE_KEY3(micentry), mic3);
			REG_WRITE(ah, AR_KEYTABLE_KEY4(micentry), mic4);
			REG_WRITE(ah, AR_KEYTABLE_TYPE(micentry),
				  AR_KEYTABLE_TYPE_CLR);

		} else {
			u32 mic0, mic2;

			mic0 = get_unaligned_le32(k->kv_mic + 0);
			mic2 = get_unaligned_le32(k->kv_mic + 4);
			REG_WRITE(ah, AR_KEYTABLE_KEY0(micentry), mic0);
			REG_WRITE(ah, AR_KEYTABLE_KEY1(micentry), 0);
			REG_WRITE(ah, AR_KEYTABLE_KEY2(micentry), mic2);
			REG_WRITE(ah, AR_KEYTABLE_KEY3(micentry), 0);
			REG_WRITE(ah, AR_KEYTABLE_KEY4(micentry), 0);
			REG_WRITE(ah, AR_KEYTABLE_TYPE(micentry),
				  AR_KEYTABLE_TYPE_CLR);
		}
		REG_WRITE(ah, AR_KEYTABLE_MAC0(micentry), 0);
		REG_WRITE(ah, AR_KEYTABLE_MAC1(micentry), 0);
		REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), key0);
		REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), key1);
	} else {
		REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), key0);
		REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), key1);
		REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), key2);
		REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), key3);
		REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), key4);
		REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), keyType);

		(void) ath9k_hw_keysetmac(ah, entry, mac);
	}

	if (ah->ah_curchan == NULL)
		return true;

	return true;
}

bool
ath9k_hw_updatetxtriglevel(struct ath_hal *ah, bool bIncTrigLevel)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	u32 txcfg, curLevel, newLevel;
	enum ath9k_int omask;

	if (ah->ah_txTrigLevel >= MAX_TX_FIFO_THRESHOLD)
		return false;

	omask = ath9k_hw_set_interrupts(ah,
					ahp->ah_maskReg & ~ATH9K_INT_GLOBAL);

	txcfg = REG_READ(ah, AR_TXCFG);
	curLevel = MS(txcfg, AR_FTRIG);
	newLevel = curLevel;
	if (bIncTrigLevel) {
		if (curLevel < MAX_TX_FIFO_THRESHOLD)
			newLevel++;
	} else if (curLevel > MIN_TX_FIFO_THRESHOLD)
		newLevel--;
	if (newLevel != curLevel)
		REG_WRITE(ah, AR_TXCFG,
			  (txcfg & ~AR_FTRIG) | SM(newLevel, AR_FTRIG));

	ath9k_hw_set_interrupts(ah, omask);

	ah->ah_txTrigLevel = newLevel;

	return newLevel != curLevel;
}

bool ath9k_hw_set_txq_props(struct ath_hal *ah, int q,
			    const struct ath9k_tx_queue_info *qinfo)
{
	u32 cw;
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	struct ath9k_tx_queue_info *qi;

	if (q >= pCap->total_queues) {
		DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: invalid queue num %u\n",
			 __func__, q);
		return false;
	}

	qi = &ahp->ah_txq[q];
	if (qi->tqi_type == ATH9K_TX_QUEUE_INACTIVE) {
		DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: inactive queue\n",
			 __func__);
		return false;
	}

	DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: queue %p\n", __func__, qi);

	qi->tqi_ver = qinfo->tqi_ver;
	qi->tqi_subtype = qinfo->tqi_subtype;
	qi->tqi_qflags = qinfo->tqi_qflags;
	qi->tqi_priority = qinfo->tqi_priority;
	if (qinfo->tqi_aifs != ATH9K_TXQ_USEDEFAULT)
		qi->tqi_aifs = min(qinfo->tqi_aifs, 255U);
	else
		qi->tqi_aifs = INIT_AIFS;
	if (qinfo->tqi_cwmin != ATH9K_TXQ_USEDEFAULT) {
		cw = min(qinfo->tqi_cwmin, 1024U);
		qi->tqi_cwmin = 1;
		while (qi->tqi_cwmin < cw)
			qi->tqi_cwmin = (qi->tqi_cwmin << 1) | 1;
	} else
		qi->tqi_cwmin = qinfo->tqi_cwmin;
	if (qinfo->tqi_cwmax != ATH9K_TXQ_USEDEFAULT) {
		cw = min(qinfo->tqi_cwmax, 1024U);
		qi->tqi_cwmax = 1;
		while (qi->tqi_cwmax < cw)
			qi->tqi_cwmax = (qi->tqi_cwmax << 1) | 1;
	} else
		qi->tqi_cwmax = INIT_CWMAX;

	if (qinfo->tqi_shretry != 0)
		qi->tqi_shretry = min((u32) qinfo->tqi_shretry, 15U);
	else
		qi->tqi_shretry = INIT_SH_RETRY;
	if (qinfo->tqi_lgretry != 0)
		qi->tqi_lgretry = min((u32) qinfo->tqi_lgretry, 15U);
	else
		qi->tqi_lgretry = INIT_LG_RETRY;
	qi->tqi_cbrPeriod = qinfo->tqi_cbrPeriod;
	qi->tqi_cbrOverflowLimit = qinfo->tqi_cbrOverflowLimit;
	qi->tqi_burstTime = qinfo->tqi_burstTime;
	qi->tqi_readyTime = qinfo->tqi_readyTime;

	switch (qinfo->tqi_subtype) {
	case ATH9K_WME_UPSD:
		if (qi->tqi_type == ATH9K_TX_QUEUE_DATA)
			qi->tqi_intFlags = ATH9K_TXQ_USE_LOCKOUT_BKOFF_DIS;
		break;
	default:
		break;
	}
	return true;
}

bool ath9k_hw_get_txq_props(struct ath_hal *ah, int q,
			    struct ath9k_tx_queue_info *qinfo)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	struct ath9k_tx_queue_info *qi;

	if (q >= pCap->total_queues) {
		DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: invalid queue num %u\n",
			 __func__, q);
		return false;
	}

	qi = &ahp->ah_txq[q];
	if (qi->tqi_type == ATH9K_TX_QUEUE_INACTIVE) {
		DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: inactive queue\n",
			 __func__);
		return false;
	}

	qinfo->tqi_qflags = qi->tqi_qflags;
	qinfo->tqi_ver = qi->tqi_ver;
	qinfo->tqi_subtype = qi->tqi_subtype;
	qinfo->tqi_qflags = qi->tqi_qflags;
	qinfo->tqi_priority = qi->tqi_priority;
	qinfo->tqi_aifs = qi->tqi_aifs;
	qinfo->tqi_cwmin = qi->tqi_cwmin;
	qinfo->tqi_cwmax = qi->tqi_cwmax;
	qinfo->tqi_shretry = qi->tqi_shretry;
	qinfo->tqi_lgretry = qi->tqi_lgretry;
	qinfo->tqi_cbrPeriod = qi->tqi_cbrPeriod;
	qinfo->tqi_cbrOverflowLimit = qi->tqi_cbrOverflowLimit;
	qinfo->tqi_burstTime = qi->tqi_burstTime;
	qinfo->tqi_readyTime = qi->tqi_readyTime;

	return true;
}

int
ath9k_hw_setuptxqueue(struct ath_hal *ah, enum ath9k_tx_queue type,
		      const struct ath9k_tx_queue_info *qinfo)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_tx_queue_info *qi;
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	int q;

	switch (type) {
	case ATH9K_TX_QUEUE_BEACON:
		q = pCap->total_queues - 1;
		break;
	case ATH9K_TX_QUEUE_CAB:
		q = pCap->total_queues - 2;
		break;
	case ATH9K_TX_QUEUE_PSPOLL:
		q = 1;
		break;
	case ATH9K_TX_QUEUE_UAPSD:
		q = pCap->total_queues - 3;
		break;
	case ATH9K_TX_QUEUE_DATA:
		for (q = 0; q < pCap->total_queues; q++)
			if (ahp->ah_txq[q].tqi_type ==
			    ATH9K_TX_QUEUE_INACTIVE)
				break;
		if (q == pCap->total_queues) {
			DPRINTF(ah->ah_sc, ATH_DBG_QUEUE,
				 "%s: no available tx queue\n", __func__);
			return -1;
		}
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: bad tx queue type %u\n",
			 __func__, type);
		return -1;
	}

	DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: queue %u\n", __func__, q);

	qi = &ahp->ah_txq[q];
	if (qi->tqi_type != ATH9K_TX_QUEUE_INACTIVE) {
		DPRINTF(ah->ah_sc, ATH_DBG_QUEUE,
			 "%s: tx queue %u already active\n", __func__, q);
		return -1;
	}
	memset(qi, 0, sizeof(struct ath9k_tx_queue_info));
	qi->tqi_type = type;
	if (qinfo == NULL) {
		qi->tqi_qflags =
			TXQ_FLAG_TXOKINT_ENABLE
			| TXQ_FLAG_TXERRINT_ENABLE
			| TXQ_FLAG_TXDESCINT_ENABLE | TXQ_FLAG_TXURNINT_ENABLE;
		qi->tqi_aifs = INIT_AIFS;
		qi->tqi_cwmin = ATH9K_TXQ_USEDEFAULT;
		qi->tqi_cwmax = INIT_CWMAX;
		qi->tqi_shretry = INIT_SH_RETRY;
		qi->tqi_lgretry = INIT_LG_RETRY;
		qi->tqi_physCompBuf = 0;
	} else {
		qi->tqi_physCompBuf = qinfo->tqi_physCompBuf;
		(void) ath9k_hw_set_txq_props(ah, q, qinfo);
	}

	return q;
}

static void
ath9k_hw_set_txq_interrupts(struct ath_hal *ah,
			    struct ath9k_tx_queue_info *qi)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT,
		 "%s: tx ok 0x%x err 0x%x desc 0x%x eol 0x%x urn 0x%x\n",
		 __func__, ahp->ah_txOkInterruptMask,
		 ahp->ah_txErrInterruptMask, ahp->ah_txDescInterruptMask,
		 ahp->ah_txEolInterruptMask, ahp->ah_txUrnInterruptMask);

	REG_WRITE(ah, AR_IMR_S0,
		  SM(ahp->ah_txOkInterruptMask, AR_IMR_S0_QCU_TXOK)
		  | SM(ahp->ah_txDescInterruptMask, AR_IMR_S0_QCU_TXDESC));
	REG_WRITE(ah, AR_IMR_S1,
		  SM(ahp->ah_txErrInterruptMask, AR_IMR_S1_QCU_TXERR)
		  | SM(ahp->ah_txEolInterruptMask, AR_IMR_S1_QCU_TXEOL));
	REG_RMW_FIELD(ah, AR_IMR_S2,
		      AR_IMR_S2_QCU_TXURN, ahp->ah_txUrnInterruptMask);
}

bool ath9k_hw_releasetxqueue(struct ath_hal *ah, u32 q)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	struct ath9k_tx_queue_info *qi;

	if (q >= pCap->total_queues) {
		DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: invalid queue num %u\n",
			 __func__, q);
		return false;
	}
	qi = &ahp->ah_txq[q];
	if (qi->tqi_type == ATH9K_TX_QUEUE_INACTIVE) {
		DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: inactive queue %u\n",
			 __func__, q);
		return false;
	}

	DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: release queue %u\n",
		__func__, q);

	qi->tqi_type = ATH9K_TX_QUEUE_INACTIVE;
	ahp->ah_txOkInterruptMask &= ~(1 << q);
	ahp->ah_txErrInterruptMask &= ~(1 << q);
	ahp->ah_txDescInterruptMask &= ~(1 << q);
	ahp->ah_txEolInterruptMask &= ~(1 << q);
	ahp->ah_txUrnInterruptMask &= ~(1 << q);
	ath9k_hw_set_txq_interrupts(ah, qi);

	return true;
}

bool ath9k_hw_resettxqueue(struct ath_hal *ah, u32 q)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	struct ath9k_channel *chan = ah->ah_curchan;
	struct ath9k_tx_queue_info *qi;
	u32 cwMin, chanCwMin, value;

	if (q >= pCap->total_queues) {
		DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: invalid queue num %u\n",
			 __func__, q);
		return false;
	}
	qi = &ahp->ah_txq[q];
	if (qi->tqi_type == ATH9K_TX_QUEUE_INACTIVE) {
		DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: inactive queue %u\n",
			 __func__, q);
		return true;
	}

	DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: reset queue %u\n", __func__, q);

	if (qi->tqi_cwmin == ATH9K_TXQ_USEDEFAULT) {
		if (chan && IS_CHAN_B(chan))
			chanCwMin = INIT_CWMIN_11B;
		else
			chanCwMin = INIT_CWMIN;

		for (cwMin = 1; cwMin < chanCwMin; cwMin = (cwMin << 1) | 1);
	} else
		cwMin = qi->tqi_cwmin;

	REG_WRITE(ah, AR_DLCL_IFS(q), SM(cwMin, AR_D_LCL_IFS_CWMIN)
		  | SM(qi->tqi_cwmax, AR_D_LCL_IFS_CWMAX)
		  | SM(qi->tqi_aifs, AR_D_LCL_IFS_AIFS));

	REG_WRITE(ah, AR_DRETRY_LIMIT(q),
		  SM(INIT_SSH_RETRY, AR_D_RETRY_LIMIT_STA_SH)
		  | SM(INIT_SLG_RETRY, AR_D_RETRY_LIMIT_STA_LG)
		  | SM(qi->tqi_shretry, AR_D_RETRY_LIMIT_FR_SH)
		);

	REG_WRITE(ah, AR_QMISC(q), AR_Q_MISC_DCU_EARLY_TERM_REQ);
	REG_WRITE(ah, AR_DMISC(q),
		  AR_D_MISC_CW_BKOFF_EN | AR_D_MISC_FRAG_WAIT_EN | 0x2);

	if (qi->tqi_cbrPeriod) {
		REG_WRITE(ah, AR_QCBRCFG(q),
			  SM(qi->tqi_cbrPeriod, AR_Q_CBRCFG_INTERVAL)
			  | SM(qi->tqi_cbrOverflowLimit,
			       AR_Q_CBRCFG_OVF_THRESH));
		REG_WRITE(ah, AR_QMISC(q),
			  REG_READ(ah,
				   AR_QMISC(q)) | AR_Q_MISC_FSP_CBR | (qi->
					tqi_cbrOverflowLimit
					?
					AR_Q_MISC_CBR_EXP_CNTR_LIMIT_EN
					:
					0));
	}
	if (qi->tqi_readyTime && (qi->tqi_type != ATH9K_TX_QUEUE_CAB)) {
		REG_WRITE(ah, AR_QRDYTIMECFG(q),
			  SM(qi->tqi_readyTime, AR_Q_RDYTIMECFG_DURATION) |
			  AR_Q_RDYTIMECFG_EN);
	}

	REG_WRITE(ah, AR_DCHNTIME(q),
		  SM(qi->tqi_burstTime, AR_D_CHNTIME_DUR) |
		  (qi->tqi_burstTime ? AR_D_CHNTIME_EN : 0));

	if (qi->tqi_burstTime
	    && (qi->tqi_qflags & TXQ_FLAG_RDYTIME_EXP_POLICY_ENABLE)) {
		REG_WRITE(ah, AR_QMISC(q),
			  REG_READ(ah,
				   AR_QMISC(q)) |
			  AR_Q_MISC_RDYTIME_EXP_POLICY);

	}

	if (qi->tqi_qflags & TXQ_FLAG_BACKOFF_DISABLE) {
		REG_WRITE(ah, AR_DMISC(q),
			  REG_READ(ah, AR_DMISC(q)) |
			  AR_D_MISC_POST_FR_BKOFF_DIS);
	}
	if (qi->tqi_qflags & TXQ_FLAG_FRAG_BURST_BACKOFF_ENABLE) {
		REG_WRITE(ah, AR_DMISC(q),
			  REG_READ(ah, AR_DMISC(q)) |
			  AR_D_MISC_FRAG_BKOFF_EN);
	}
	switch (qi->tqi_type) {
	case ATH9K_TX_QUEUE_BEACON:
		REG_WRITE(ah, AR_QMISC(q), REG_READ(ah, AR_QMISC(q))
			  | AR_Q_MISC_FSP_DBA_GATED
			  | AR_Q_MISC_BEACON_USE
			  | AR_Q_MISC_CBR_INCR_DIS1);

		REG_WRITE(ah, AR_DMISC(q), REG_READ(ah, AR_DMISC(q))
			  | (AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL <<
			     AR_D_MISC_ARB_LOCKOUT_CNTRL_S)
			  | AR_D_MISC_BEACON_USE
			  | AR_D_MISC_POST_FR_BKOFF_DIS);
		break;
	case ATH9K_TX_QUEUE_CAB:
		REG_WRITE(ah, AR_QMISC(q), REG_READ(ah, AR_QMISC(q))
			  | AR_Q_MISC_FSP_DBA_GATED
			  | AR_Q_MISC_CBR_INCR_DIS1
			  | AR_Q_MISC_CBR_INCR_DIS0);
		value = (qi->tqi_readyTime
			 - (ah->ah_config.sw_beacon_response_time -
			    ah->ah_config.dma_beacon_response_time)
			 -
			 ah->ah_config.additional_swba_backoff) *
			1024;
		REG_WRITE(ah, AR_QRDYTIMECFG(q),
			  value | AR_Q_RDYTIMECFG_EN);
		REG_WRITE(ah, AR_DMISC(q), REG_READ(ah, AR_DMISC(q))
			  | (AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL <<
			     AR_D_MISC_ARB_LOCKOUT_CNTRL_S));
		break;
	case ATH9K_TX_QUEUE_PSPOLL:
		REG_WRITE(ah, AR_QMISC(q),
			  REG_READ(ah,
				   AR_QMISC(q)) | AR_Q_MISC_CBR_INCR_DIS1);
		break;
	case ATH9K_TX_QUEUE_UAPSD:
		REG_WRITE(ah, AR_DMISC(q), REG_READ(ah, AR_DMISC(q))
			  | AR_D_MISC_POST_FR_BKOFF_DIS);
		break;
	default:
		break;
	}

	if (qi->tqi_intFlags & ATH9K_TXQ_USE_LOCKOUT_BKOFF_DIS) {
		REG_WRITE(ah, AR_DMISC(q),
			  REG_READ(ah, AR_DMISC(q)) |
			  SM(AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL,
			     AR_D_MISC_ARB_LOCKOUT_CNTRL) |
			  AR_D_MISC_POST_FR_BKOFF_DIS);
	}

	if (qi->tqi_qflags & TXQ_FLAG_TXOKINT_ENABLE)
		ahp->ah_txOkInterruptMask |= 1 << q;
	else
		ahp->ah_txOkInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & TXQ_FLAG_TXERRINT_ENABLE)
		ahp->ah_txErrInterruptMask |= 1 << q;
	else
		ahp->ah_txErrInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & TXQ_FLAG_TXDESCINT_ENABLE)
		ahp->ah_txDescInterruptMask |= 1 << q;
	else
		ahp->ah_txDescInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & TXQ_FLAG_TXEOLINT_ENABLE)
		ahp->ah_txEolInterruptMask |= 1 << q;
	else
		ahp->ah_txEolInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & TXQ_FLAG_TXURNINT_ENABLE)
		ahp->ah_txUrnInterruptMask |= 1 << q;
	else
		ahp->ah_txUrnInterruptMask &= ~(1 << q);
	ath9k_hw_set_txq_interrupts(ah, qi);

	return true;
}

void ath9k_hw_gettxintrtxqs(struct ath_hal *ah, u32 *txqs)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	*txqs &= ahp->ah_intrTxqs;
	ahp->ah_intrTxqs &= ~(*txqs);
}

bool
ath9k_hw_filltxdesc(struct ath_hal *ah, struct ath_desc *ds,
		    u32 segLen, bool firstSeg,
		    bool lastSeg, const struct ath_desc *ds0)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	if (firstSeg) {
		ads->ds_ctl1 |= segLen | (lastSeg ? 0 : AR_TxMore);
	} else if (lastSeg) {
		ads->ds_ctl0 = 0;
		ads->ds_ctl1 = segLen;
		ads->ds_ctl2 = AR5416DESC_CONST(ds0)->ds_ctl2;
		ads->ds_ctl3 = AR5416DESC_CONST(ds0)->ds_ctl3;
	} else {
		ads->ds_ctl0 = 0;
		ads->ds_ctl1 = segLen | AR_TxMore;
		ads->ds_ctl2 = 0;
		ads->ds_ctl3 = 0;
	}
	ads->ds_txstatus0 = ads->ds_txstatus1 = 0;
	ads->ds_txstatus2 = ads->ds_txstatus3 = 0;
	ads->ds_txstatus4 = ads->ds_txstatus5 = 0;
	ads->ds_txstatus6 = ads->ds_txstatus7 = 0;
	ads->ds_txstatus8 = ads->ds_txstatus9 = 0;
	return true;
}

void ath9k_hw_cleartxdesc(struct ath_hal *ah, struct ath_desc *ds)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_txstatus0 = ads->ds_txstatus1 = 0;
	ads->ds_txstatus2 = ads->ds_txstatus3 = 0;
	ads->ds_txstatus4 = ads->ds_txstatus5 = 0;
	ads->ds_txstatus6 = ads->ds_txstatus7 = 0;
	ads->ds_txstatus8 = ads->ds_txstatus9 = 0;
}

int
ath9k_hw_txprocdesc(struct ath_hal *ah, struct ath_desc *ds)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	if ((ads->ds_txstatus9 & AR_TxDone) == 0)
		return -EINPROGRESS;

	ds->ds_txstat.ts_seqnum = MS(ads->ds_txstatus9, AR_SeqNum);
	ds->ds_txstat.ts_tstamp = ads->AR_SendTimestamp;
	ds->ds_txstat.ts_status = 0;
	ds->ds_txstat.ts_flags = 0;

	if (ads->ds_txstatus1 & AR_ExcessiveRetries)
		ds->ds_txstat.ts_status |= ATH9K_TXERR_XRETRY;
	if (ads->ds_txstatus1 & AR_Filtered)
		ds->ds_txstat.ts_status |= ATH9K_TXERR_FILT;
	if (ads->ds_txstatus1 & AR_FIFOUnderrun)
		ds->ds_txstat.ts_status |= ATH9K_TXERR_FIFO;
	if (ads->ds_txstatus9 & AR_TxOpExceeded)
		ds->ds_txstat.ts_status |= ATH9K_TXERR_XTXOP;
	if (ads->ds_txstatus1 & AR_TxTimerExpired)
		ds->ds_txstat.ts_status |= ATH9K_TXERR_TIMER_EXPIRED;

	if (ads->ds_txstatus1 & AR_DescCfgErr)
		ds->ds_txstat.ts_flags |= ATH9K_TX_DESC_CFG_ERR;
	if (ads->ds_txstatus1 & AR_TxDataUnderrun) {
		ds->ds_txstat.ts_flags |= ATH9K_TX_DATA_UNDERRUN;
		ath9k_hw_updatetxtriglevel(ah, true);
	}
	if (ads->ds_txstatus1 & AR_TxDelimUnderrun) {
		ds->ds_txstat.ts_flags |= ATH9K_TX_DELIM_UNDERRUN;
		ath9k_hw_updatetxtriglevel(ah, true);
	}
	if (ads->ds_txstatus0 & AR_TxBaStatus) {
		ds->ds_txstat.ts_flags |= ATH9K_TX_BA;
		ds->ds_txstat.ba_low = ads->AR_BaBitmapLow;
		ds->ds_txstat.ba_high = ads->AR_BaBitmapHigh;
	}

	ds->ds_txstat.ts_rateindex = MS(ads->ds_txstatus9, AR_FinalTxIdx);
	switch (ds->ds_txstat.ts_rateindex) {
	case 0:
		ds->ds_txstat.ts_ratecode = MS(ads->ds_ctl3, AR_XmitRate0);
		break;
	case 1:
		ds->ds_txstat.ts_ratecode = MS(ads->ds_ctl3, AR_XmitRate1);
		break;
	case 2:
		ds->ds_txstat.ts_ratecode = MS(ads->ds_ctl3, AR_XmitRate2);
		break;
	case 3:
		ds->ds_txstat.ts_ratecode = MS(ads->ds_ctl3, AR_XmitRate3);
		break;
	}

	ds->ds_txstat.ts_rssi = MS(ads->ds_txstatus5, AR_TxRSSICombined);
	ds->ds_txstat.ts_rssi_ctl0 = MS(ads->ds_txstatus0, AR_TxRSSIAnt00);
	ds->ds_txstat.ts_rssi_ctl1 = MS(ads->ds_txstatus0, AR_TxRSSIAnt01);
	ds->ds_txstat.ts_rssi_ctl2 = MS(ads->ds_txstatus0, AR_TxRSSIAnt02);
	ds->ds_txstat.ts_rssi_ext0 = MS(ads->ds_txstatus5, AR_TxRSSIAnt10);
	ds->ds_txstat.ts_rssi_ext1 = MS(ads->ds_txstatus5, AR_TxRSSIAnt11);
	ds->ds_txstat.ts_rssi_ext2 = MS(ads->ds_txstatus5, AR_TxRSSIAnt12);
	ds->ds_txstat.evm0 = ads->AR_TxEVM0;
	ds->ds_txstat.evm1 = ads->AR_TxEVM1;
	ds->ds_txstat.evm2 = ads->AR_TxEVM2;
	ds->ds_txstat.ts_shortretry = MS(ads->ds_txstatus1, AR_RTSFailCnt);
	ds->ds_txstat.ts_longretry = MS(ads->ds_txstatus1, AR_DataFailCnt);
	ds->ds_txstat.ts_virtcol = MS(ads->ds_txstatus1, AR_VirtRetryCnt);
	ds->ds_txstat.ts_antenna = 1;

	return 0;
}

void
ath9k_hw_set11n_txdesc(struct ath_hal *ah, struct ath_desc *ds,
		       u32 pktLen, enum ath9k_pkt_type type, u32 txPower,
		       u32 keyIx, enum ath9k_key_type keyType, u32 flags)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	struct ath_hal_5416 *ahp = AH5416(ah);

	txPower += ahp->ah_txPowerIndexOffset;
	if (txPower > 63)
		txPower = 63;

	ads->ds_ctl0 = (pktLen & AR_FrameLen)
		| (flags & ATH9K_TXDESC_VMF ? AR_VirtMoreFrag : 0)
		| SM(txPower, AR_XmitPower)
		| (flags & ATH9K_TXDESC_VEOL ? AR_VEOL : 0)
		| (flags & ATH9K_TXDESC_CLRDMASK ? AR_ClrDestMask : 0)
		| (flags & ATH9K_TXDESC_INTREQ ? AR_TxIntrReq : 0)
		| (keyIx != ATH9K_TXKEYIX_INVALID ? AR_DestIdxValid : 0);

	ads->ds_ctl1 =
		(keyIx != ATH9K_TXKEYIX_INVALID ? SM(keyIx, AR_DestIdx) : 0)
		| SM(type, AR_FrameType)
		| (flags & ATH9K_TXDESC_NOACK ? AR_NoAck : 0)
		| (flags & ATH9K_TXDESC_EXT_ONLY ? AR_ExtOnly : 0)
		| (flags & ATH9K_TXDESC_EXT_AND_CTL ? AR_ExtAndCtl : 0);

	ads->ds_ctl6 = SM(keyType, AR_EncrType);

	if (AR_SREV_9285(ah)) {

		ads->ds_ctl8 = 0;
		ads->ds_ctl9 = 0;
		ads->ds_ctl10 = 0;
		ads->ds_ctl11 = 0;
	}
}

void
ath9k_hw_set11n_ratescenario(struct ath_hal *ah, struct ath_desc *ds,
			     struct ath_desc *lastds,
			     u32 durUpdateEn, u32 rtsctsRate,
			     u32 rtsctsDuration,
			     struct ath9k_11n_rate_series series[],
			     u32 nseries, u32 flags)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	struct ar5416_desc *last_ads = AR5416DESC(lastds);
	u32 ds_ctl0;

	(void) nseries;
	(void) rtsctsDuration;

	if (flags & (ATH9K_TXDESC_RTSENA | ATH9K_TXDESC_CTSENA)) {
		ds_ctl0 = ads->ds_ctl0;

		if (flags & ATH9K_TXDESC_RTSENA) {
			ds_ctl0 &= ~AR_CTSEnable;
			ds_ctl0 |= AR_RTSEnable;
		} else {
			ds_ctl0 &= ~AR_RTSEnable;
			ds_ctl0 |= AR_CTSEnable;
		}

		ads->ds_ctl0 = ds_ctl0;
	} else {
		ads->ds_ctl0 =
			(ads->ds_ctl0 & ~(AR_RTSEnable | AR_CTSEnable));
	}

	ads->ds_ctl2 = set11nTries(series, 0)
		| set11nTries(series, 1)
		| set11nTries(series, 2)
		| set11nTries(series, 3)
		| (durUpdateEn ? AR_DurUpdateEna : 0)
		| SM(0, AR_BurstDur);

	ads->ds_ctl3 = set11nRate(series, 0)
		| set11nRate(series, 1)
		| set11nRate(series, 2)
		| set11nRate(series, 3);

	ads->ds_ctl4 = set11nPktDurRTSCTS(series, 0)
		| set11nPktDurRTSCTS(series, 1);

	ads->ds_ctl5 = set11nPktDurRTSCTS(series, 2)
		| set11nPktDurRTSCTS(series, 3);

	ads->ds_ctl7 = set11nRateFlags(series, 0)
		| set11nRateFlags(series, 1)
		| set11nRateFlags(series, 2)
		| set11nRateFlags(series, 3)
		| SM(rtsctsRate, AR_RTSCTSRate);
	last_ads->ds_ctl2 = ads->ds_ctl2;
	last_ads->ds_ctl3 = ads->ds_ctl3;
}

void
ath9k_hw_set11n_aggr_first(struct ath_hal *ah, struct ath_desc *ds,
			   u32 aggrLen)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl1 |= (AR_IsAggr | AR_MoreAggr);

	ads->ds_ctl6 &= ~AR_AggrLen;
	ads->ds_ctl6 |= SM(aggrLen, AR_AggrLen);
}

void
ath9k_hw_set11n_aggr_middle(struct ath_hal *ah, struct ath_desc *ds,
			    u32 numDelims)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	unsigned int ctl6;

	ads->ds_ctl1 |= (AR_IsAggr | AR_MoreAggr);

	ctl6 = ads->ds_ctl6;
	ctl6 &= ~AR_PadDelim;
	ctl6 |= SM(numDelims, AR_PadDelim);
	ads->ds_ctl6 = ctl6;
}

void ath9k_hw_set11n_aggr_last(struct ath_hal *ah, struct ath_desc *ds)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl1 |= AR_IsAggr;
	ads->ds_ctl1 &= ~AR_MoreAggr;
	ads->ds_ctl6 &= ~AR_PadDelim;
}

void ath9k_hw_clr11n_aggr(struct ath_hal *ah, struct ath_desc *ds)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl1 &= (~AR_IsAggr & ~AR_MoreAggr);
}

void
ath9k_hw_set11n_burstduration(struct ath_hal *ah, struct ath_desc *ds,
			      u32 burstDuration)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl2 &= ~AR_BurstDur;
	ads->ds_ctl2 |= SM(burstDuration, AR_BurstDur);
}

void
ath9k_hw_set11n_virtualmorefrag(struct ath_hal *ah, struct ath_desc *ds,
				u32 vmf)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	if (vmf)
		ads->ds_ctl0 |= AR_VirtMoreFrag;
	else
		ads->ds_ctl0 &= ~AR_VirtMoreFrag;
}

void ath9k_hw_putrxbuf(struct ath_hal *ah, u32 rxdp)
{
	REG_WRITE(ah, AR_RXDP, rxdp);
}

void ath9k_hw_rxena(struct ath_hal *ah)
{
	REG_WRITE(ah, AR_CR, AR_CR_RXE);
}

bool ath9k_hw_setrxabort(struct ath_hal *ah, bool set)
{
	if (set) {

		REG_SET_BIT(ah, AR_DIAG_SW,
			    (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));

		if (!ath9k_hw_wait
		    (ah, AR_OBS_BUS_1, AR_OBS_BUS_1_RX_STATE, 0)) {
			u32 reg;

			REG_CLR_BIT(ah, AR_DIAG_SW,
				    (AR_DIAG_RX_DIS |
				     AR_DIAG_RX_ABORT));

			reg = REG_READ(ah, AR_OBS_BUS_1);
			DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
				"%s: rx failed to go idle in 10 ms RXSM=0x%x\n",
				__func__, reg);

			return false;
		}
	} else {
		REG_CLR_BIT(ah, AR_DIAG_SW,
			    (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));
	}

	return true;
}

void
ath9k_hw_setmcastfilter(struct ath_hal *ah, u32 filter0,
			u32 filter1)
{
	REG_WRITE(ah, AR_MCAST_FIL0, filter0);
	REG_WRITE(ah, AR_MCAST_FIL1, filter1);
}

bool
ath9k_hw_setuprxdesc(struct ath_hal *ah, struct ath_desc *ds,
		     u32 size, u32 flags)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;

	ads->ds_ctl1 = size & AR_BufLen;
	if (flags & ATH9K_RXDESC_INTREQ)
		ads->ds_ctl1 |= AR_RxIntrReq;

	ads->ds_rxstatus8 &= ~AR_RxDone;
	if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP))
		memset(&(ads->u), 0, sizeof(ads->u));
	return true;
}

int
ath9k_hw_rxprocdesc(struct ath_hal *ah, struct ath_desc *ds,
		    u32 pa, struct ath_desc *nds, u64 tsf)
{
	struct ar5416_desc ads;
	struct ar5416_desc *adsp = AR5416DESC(ds);

	if ((adsp->ds_rxstatus8 & AR_RxDone) == 0)
		return -EINPROGRESS;

	ads.u.rx = adsp->u.rx;

	ds->ds_rxstat.rs_status = 0;
	ds->ds_rxstat.rs_flags = 0;

	ds->ds_rxstat.rs_datalen = ads.ds_rxstatus1 & AR_DataLen;
	ds->ds_rxstat.rs_tstamp = ads.AR_RcvTimestamp;

	ds->ds_rxstat.rs_rssi = MS(ads.ds_rxstatus4, AR_RxRSSICombined);
	ds->ds_rxstat.rs_rssi_ctl0 = MS(ads.ds_rxstatus0, AR_RxRSSIAnt00);
	ds->ds_rxstat.rs_rssi_ctl1 = MS(ads.ds_rxstatus0, AR_RxRSSIAnt01);
	ds->ds_rxstat.rs_rssi_ctl2 = MS(ads.ds_rxstatus0, AR_RxRSSIAnt02);
	ds->ds_rxstat.rs_rssi_ext0 = MS(ads.ds_rxstatus4, AR_RxRSSIAnt10);
	ds->ds_rxstat.rs_rssi_ext1 = MS(ads.ds_rxstatus4, AR_RxRSSIAnt11);
	ds->ds_rxstat.rs_rssi_ext2 = MS(ads.ds_rxstatus4, AR_RxRSSIAnt12);
	if (ads.ds_rxstatus8 & AR_RxKeyIdxValid)
		ds->ds_rxstat.rs_keyix = MS(ads.ds_rxstatus8, AR_KeyIdx);
	else
		ds->ds_rxstat.rs_keyix = ATH9K_RXKEYIX_INVALID;

	ds->ds_rxstat.rs_rate = RXSTATUS_RATE(ah, (&ads));
	ds->ds_rxstat.rs_more = (ads.ds_rxstatus1 & AR_RxMore) ? 1 : 0;

	ds->ds_rxstat.rs_isaggr = (ads.ds_rxstatus8 & AR_RxAggr) ? 1 : 0;
	ds->ds_rxstat.rs_moreaggr =
		(ads.ds_rxstatus8 & AR_RxMoreAggr) ? 1 : 0;
	ds->ds_rxstat.rs_antenna = MS(ads.ds_rxstatus3, AR_RxAntenna);
	ds->ds_rxstat.rs_flags =
		(ads.ds_rxstatus3 & AR_GI) ? ATH9K_RX_GI : 0;
	ds->ds_rxstat.rs_flags |=
		(ads.ds_rxstatus3 & AR_2040) ? ATH9K_RX_2040 : 0;

	if (ads.ds_rxstatus8 & AR_PreDelimCRCErr)
		ds->ds_rxstat.rs_flags |= ATH9K_RX_DELIM_CRC_PRE;
	if (ads.ds_rxstatus8 & AR_PostDelimCRCErr)
		ds->ds_rxstat.rs_flags |= ATH9K_RX_DELIM_CRC_POST;
	if (ads.ds_rxstatus8 & AR_DecryptBusyErr)
		ds->ds_rxstat.rs_flags |= ATH9K_RX_DECRYPT_BUSY;

	if ((ads.ds_rxstatus8 & AR_RxFrameOK) == 0) {

		if (ads.ds_rxstatus8 & AR_CRCErr)
			ds->ds_rxstat.rs_status |= ATH9K_RXERR_CRC;
		else if (ads.ds_rxstatus8 & AR_PHYErr) {
			u32 phyerr;

			ds->ds_rxstat.rs_status |= ATH9K_RXERR_PHY;
			phyerr = MS(ads.ds_rxstatus8, AR_PHYErrCode);
			ds->ds_rxstat.rs_phyerr = phyerr;
		} else if (ads.ds_rxstatus8 & AR_DecryptCRCErr)
			ds->ds_rxstat.rs_status |= ATH9K_RXERR_DECRYPT;
		else if (ads.ds_rxstatus8 & AR_MichaelErr)
			ds->ds_rxstat.rs_status |= ATH9K_RXERR_MIC;
	}

	return 0;
}

static void ath9k_hw_setup_rate_table(struct ath_hal *ah,
				      struct ath9k_rate_table *rt)
{
	int i;

	if (rt->rateCodeToIndex[0] != 0)
		return;
	for (i = 0; i < 256; i++)
		rt->rateCodeToIndex[i] = (u8) -1;
	for (i = 0; i < rt->rateCount; i++) {
		u8 code = rt->info[i].rateCode;
		u8 cix = rt->info[i].controlRate;

		rt->rateCodeToIndex[code] = i;
		rt->rateCodeToIndex[code | rt->info[i].shortPreamble] = i;

		rt->info[i].lpAckDuration =
			ath9k_hw_computetxtime(ah, rt,
					       WLAN_CTRL_FRAME_SIZE,
					       cix,
					       false);
		rt->info[i].spAckDuration =
			ath9k_hw_computetxtime(ah, rt,
					       WLAN_CTRL_FRAME_SIZE,
					       cix,
					       true);
	}
}

const struct ath9k_rate_table *ath9k_hw_getratetable(struct ath_hal *ah,
						   u32 mode)
{
	struct ath9k_rate_table *rt;
	switch (mode) {
	case ATH9K_MODE_11A:
		rt = &ar5416_11a_table;
		break;
	case ATH9K_MODE_11B:
		rt = &ar5416_11b_table;
		break;
	case ATH9K_MODE_11G:
		rt = &ar5416_11g_table;
		break;
	case ATH9K_MODE_11NG_HT20:
	case ATH9K_MODE_11NG_HT40PLUS:
	case ATH9K_MODE_11NG_HT40MINUS:
		rt = &ar5416_11ng_table;
		break;
	case ATH9K_MODE_11NA_HT20:
	case ATH9K_MODE_11NA_HT40PLUS:
	case ATH9K_MODE_11NA_HT40MINUS:
		rt = &ar5416_11na_table;
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL, "%s: invalid mode 0x%x\n",
			 __func__, mode);
		return NULL;
	}
	ath9k_hw_setup_rate_table(ah, rt);
	return rt;
}

static const char *ath9k_hw_devname(u16 devid)
{
	switch (devid) {
	case AR5416_DEVID_PCI:
	case AR5416_DEVID_PCIE:
		return "Atheros 5416";
	case AR9160_DEVID_PCI:
		return "Atheros 9160";
	case AR9280_DEVID_PCI:
	case AR9280_DEVID_PCIE:
		return "Atheros 9280";
	}
	return NULL;
}

const char *ath9k_hw_probe(u16 vendorid, u16 devid)
{
	return vendorid == ATHEROS_VENDOR_ID ?
		ath9k_hw_devname(devid) : NULL;
}

struct ath_hal *ath9k_hw_attach(u16 devid,
				struct ath_softc *sc,
				void __iomem *mem,
				int *error)
{
	struct ath_hal *ah = NULL;

	switch (devid) {
	case AR5416_DEVID_PCI:
	case AR5416_DEVID_PCIE:
	case AR9160_DEVID_PCI:
	case AR9280_DEVID_PCI:
	case AR9280_DEVID_PCIE:
		ah = ath9k_hw_do_attach(devid, sc, mem, error);
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_ANY,
			 "devid=0x%x not supported.\n", devid);
		ah = NULL;
		*error = -ENXIO;
		break;
	}
	if (ah != NULL) {
		ah->ah_devid = ah->ah_devid;
		ah->ah_subvendorid = ah->ah_subvendorid;
		ah->ah_macVersion = ah->ah_macVersion;
		ah->ah_macRev = ah->ah_macRev;
		ah->ah_phyRev = ah->ah_phyRev;
		ah->ah_analog5GhzRev = ah->ah_analog5GhzRev;
		ah->ah_analog2GhzRev = ah->ah_analog2GhzRev;
	}
	return ah;
}

u16
ath9k_hw_computetxtime(struct ath_hal *ah,
		       const struct ath9k_rate_table *rates,
		       u32 frameLen, u16 rateix,
		       bool shortPreamble)
{
	u32 bitsPerSymbol, numBits, numSymbols, phyTime, txTime;
	u32 kbps;

	kbps = rates->info[rateix].rateKbps;

	if (kbps == 0)
		return 0;
	switch (rates->info[rateix].phy) {

	case PHY_CCK:
		phyTime = CCK_PREAMBLE_BITS + CCK_PLCP_BITS;
		if (shortPreamble && rates->info[rateix].shortPreamble)
			phyTime >>= 1;
		numBits = frameLen << 3;
		txTime = CCK_SIFS_TIME + phyTime
			+ ((numBits * 1000) / kbps);
		break;
	case PHY_OFDM:
		if (ah->ah_curchan && IS_CHAN_QUARTER_RATE(ah->ah_curchan)) {
			bitsPerSymbol =
				(kbps * OFDM_SYMBOL_TIME_QUARTER) / 1000;

			numBits = OFDM_PLCP_BITS + (frameLen << 3);
			numSymbols = DIV_ROUND_UP(numBits, bitsPerSymbol);
			txTime = OFDM_SIFS_TIME_QUARTER
				+ OFDM_PREAMBLE_TIME_QUARTER
				+ (numSymbols * OFDM_SYMBOL_TIME_QUARTER);
		} else if (ah->ah_curchan &&
			   IS_CHAN_HALF_RATE(ah->ah_curchan)) {
			bitsPerSymbol =
				(kbps * OFDM_SYMBOL_TIME_HALF) / 1000;

			numBits = OFDM_PLCP_BITS + (frameLen << 3);
			numSymbols = DIV_ROUND_UP(numBits, bitsPerSymbol);
			txTime = OFDM_SIFS_TIME_HALF +
				OFDM_PREAMBLE_TIME_HALF
				+ (numSymbols * OFDM_SYMBOL_TIME_HALF);
		} else {
			bitsPerSymbol = (kbps * OFDM_SYMBOL_TIME) / 1000;

			numBits = OFDM_PLCP_BITS + (frameLen << 3);
			numSymbols = DIV_ROUND_UP(numBits, bitsPerSymbol);
			txTime = OFDM_SIFS_TIME + OFDM_PREAMBLE_TIME
				+ (numSymbols * OFDM_SYMBOL_TIME);
		}
		break;

	default:
		DPRINTF(ah->ah_sc, ATH_DBG_PHY_IO,
			 "%s: unknown phy %u (rate ix %u)\n", __func__,
			 rates->info[rateix].phy, rateix);
		txTime = 0;
		break;
	}
	return txTime;
}

u32 ath9k_hw_mhz2ieee(struct ath_hal *ah, u32 freq, u32 flags)
{
	if (flags & CHANNEL_2GHZ) {
		if (freq == 2484)
			return 14;
		if (freq < 2484)
			return (freq - 2407) / 5;
		else
			return 15 + ((freq - 2512) / 20);
	} else if (flags & CHANNEL_5GHZ) {
		if (ath9k_regd_is_public_safety_sku(ah) &&
		    IS_CHAN_IN_PUBLIC_SAFETY_BAND(freq)) {
			return ((freq * 10) +
				(((freq % 5) == 2) ? 5 : 0) - 49400) / 5;
		} else if ((flags & CHANNEL_A) && (freq <= 5000)) {
			return (freq - 4000) / 5;
		} else {
			return (freq - 5000) / 5;
		}
	} else {
		if (freq == 2484)
			return 14;
		if (freq < 2484)
			return (freq - 2407) / 5;
		if (freq < 5000) {
			if (ath9k_regd_is_public_safety_sku(ah)
			    && IS_CHAN_IN_PUBLIC_SAFETY_BAND(freq)) {
				return ((freq * 10) +
					(((freq % 5) ==
					  2) ? 5 : 0) - 49400) / 5;
			} else if (freq > 4900) {
				return (freq - 4000) / 5;
			} else {
				return 15 + ((freq - 2512) / 20);
			}
		}
		return (freq - 5000) / 5;
	}
}

int16_t
ath9k_hw_getchan_noise(struct ath_hal *ah, struct ath9k_channel *chan)
{
	struct ath9k_channel *ichan;

	ichan = ath9k_regd_check_channel(ah, chan);
	if (ichan == NULL) {
		DPRINTF(ah->ah_sc, ATH_DBG_NF_CAL,
			 "%s: invalid channel %u/0x%x; no mapping\n",
			 __func__, chan->channel, chan->channelFlags);
		return 0;
	}
	if (ichan->rawNoiseFloor == 0) {
		enum wireless_mode mode = ath9k_hw_chan2wmode(ah, chan);
		return NOISE_FLOOR[mode];
	} else
		return ichan->rawNoiseFloor;
}

bool ath9k_hw_set_tsfadjust(struct ath_hal *ah, u32 setting)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (setting)
		ahp->ah_miscMode |= AR_PCU_TX_ADD_TSF;
	else
		ahp->ah_miscMode &= ~AR_PCU_TX_ADD_TSF;
	return true;
}

bool ath9k_hw_phycounters(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	return ahp->ah_hasHwPhyCounters ? true : false;
}

u32 ath9k_hw_gettxbuf(struct ath_hal *ah, u32 q)
{
	return REG_READ(ah, AR_QTXDP(q));
}

bool ath9k_hw_puttxbuf(struct ath_hal *ah, u32 q,
		       u32 txdp)
{
	REG_WRITE(ah, AR_QTXDP(q), txdp);

	return true;
}

bool ath9k_hw_txstart(struct ath_hal *ah, u32 q)
{
	DPRINTF(ah->ah_sc, ATH_DBG_QUEUE, "%s: queue %u\n", __func__, q);

	REG_WRITE(ah, AR_Q_TXE, 1 << q);

	return true;
}

u32 ath9k_hw_numtxpending(struct ath_hal *ah, u32 q)
{
	u32 npend;

	npend = REG_READ(ah, AR_QSTS(q)) & AR_Q_STS_PEND_FR_CNT;
	if (npend == 0) {

		if (REG_READ(ah, AR_Q_TXE) & (1 << q))
			npend = 1;
	}
	return npend;
}

bool ath9k_hw_stoptxdma(struct ath_hal *ah, u32 q)
{
	u32 wait;

	REG_WRITE(ah, AR_Q_TXD, 1 << q);

	for (wait = 1000; wait != 0; wait--) {
		if (ath9k_hw_numtxpending(ah, q) == 0)
			break;
		udelay(100);
	}

	if (ath9k_hw_numtxpending(ah, q)) {
		u32 tsfLow, j;

		DPRINTF(ah->ah_sc, ATH_DBG_QUEUE,
			 "%s: Num of pending TX Frames %d on Q %d\n",
			 __func__, ath9k_hw_numtxpending(ah, q), q);

		for (j = 0; j < 2; j++) {
			tsfLow = REG_READ(ah, AR_TSF_L32);
			REG_WRITE(ah, AR_QUIET2,
				  SM(10, AR_QUIET2_QUIET_DUR));
			REG_WRITE(ah, AR_QUIET_PERIOD, 100);
			REG_WRITE(ah, AR_NEXT_QUIET_TIMER, tsfLow >> 10);
			REG_SET_BIT(ah, AR_TIMER_MODE,
				       AR_QUIET_TIMER_EN);

			if ((REG_READ(ah, AR_TSF_L32) >> 10) ==
			    (tsfLow >> 10)) {
				break;
			}
			DPRINTF(ah->ah_sc, ATH_DBG_QUEUE,
				"%s: TSF have moved while trying to set "
				"quiet time TSF: 0x%08x\n",
				__func__, tsfLow);
		}

		REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_FORCE_CH_IDLE_HIGH);

		udelay(200);
		REG_CLR_BIT(ah, AR_TIMER_MODE, AR_QUIET_TIMER_EN);

		wait = 1000;

		while (ath9k_hw_numtxpending(ah, q)) {
			if ((--wait) == 0) {
				DPRINTF(ah->ah_sc, ATH_DBG_XMIT,
					"%s: Failed to stop Tx DMA in 100 "
					"msec after killing last frame\n",
					__func__);
				break;
			}
			udelay(100);
		}

		REG_CLR_BIT(ah, AR_DIAG_SW, AR_DIAG_FORCE_CH_IDLE_HIGH);
	}

	REG_WRITE(ah, AR_Q_TXD, 0);
	return wait != 0;
}
