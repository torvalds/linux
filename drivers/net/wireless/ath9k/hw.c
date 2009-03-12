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

static const u8 CLOCK_RATE[] = { 40, 80, 22, 44, 88, 40 };

extern struct hal_percal_data iq_cal_multi_sample;
extern struct hal_percal_data iq_cal_single_sample;
extern struct hal_percal_data adc_gain_cal_multi_sample;
extern struct hal_percal_data adc_gain_cal_single_sample;
extern struct hal_percal_data adc_dc_cal_multi_sample;
extern struct hal_percal_data adc_dc_cal_single_sample;
extern struct hal_percal_data adc_init_dc_cal;

static bool ath9k_hw_set_reset_reg(struct ath_hal *ah, u32 type);
static void ath9k_hw_set_regs(struct ath_hal *ah, struct ath9k_channel *chan,
			      enum ath9k_ht_macmode macmode);
static u32 ath9k_hw_ini_fixup(struct ath_hal *ah,
			      struct ar5416_eeprom_def *pEepData,
			      u32 reg, u32 value);
static void ath9k_hw_9280_spur_mitigate(struct ath_hal *ah, struct ath9k_channel *chan);
static void ath9k_hw_spur_mitigate(struct ath_hal *ah, struct ath9k_channel *chan);

/********************/
/* Helper Functions */
/********************/

static u32 ath9k_hw_mac_usec(struct ath_hal *ah, u32 clks)
{
	if (ah->ah_curchan != NULL)
		return clks / CLOCK_RATE[ath9k_hw_chan2wmode(ah, ah->ah_curchan)];
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

enum wireless_mode ath9k_hw_chan2wmode(struct ath_hal *ah,
			       const struct ath9k_channel *chan)
{
	if (IS_CHAN_B(chan))
		return ATH9K_MODE_11B;
	if (IS_CHAN_G(chan))
		return ATH9K_MODE_11G;

	return ATH9K_MODE_11A;
}

bool ath9k_hw_wait(struct ath_hal *ah, u32 reg, u32 mask, u32 val)
{
	int i;

	for (i = 0; i < (AH_TIMEOUT / AH_TIME_QUANTUM); i++) {
		if ((REG_READ(ah, reg) & mask) == val)
			return true;

		udelay(AH_TIME_QUANTUM);
	}

	DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
		"timeout on reg 0x%x: 0x%08x & 0x%08x != 0x%08x\n",
		reg, REG_READ(ah, reg), mask, val);

	return false;
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

u16 ath9k_hw_computetxtime(struct ath_hal *ah,
			   struct ath_rate_table *rates,
			   u32 frameLen, u16 rateix,
			   bool shortPreamble)
{
	u32 bitsPerSymbol, numBits, numSymbols, phyTime, txTime;
	u32 kbps;

	kbps = rates->info[rateix].ratekbps;

	if (kbps == 0)
		return 0;

	switch (rates->info[rateix].phy) {
	case WLAN_RC_PHY_CCK:
		phyTime = CCK_PREAMBLE_BITS + CCK_PLCP_BITS;
		if (shortPreamble && rates->info[rateix].short_preamble)
			phyTime >>= 1;
		numBits = frameLen << 3;
		txTime = CCK_SIFS_TIME + phyTime + ((numBits * 1000) / kbps);
		break;
	case WLAN_RC_PHY_OFDM:
		if (ah->ah_curchan && IS_CHAN_QUARTER_RATE(ah->ah_curchan)) {
			bitsPerSymbol =	(kbps * OFDM_SYMBOL_TIME_QUARTER) / 1000;
			numBits = OFDM_PLCP_BITS + (frameLen << 3);
			numSymbols = DIV_ROUND_UP(numBits, bitsPerSymbol);
			txTime = OFDM_SIFS_TIME_QUARTER
				+ OFDM_PREAMBLE_TIME_QUARTER
				+ (numSymbols * OFDM_SYMBOL_TIME_QUARTER);
		} else if (ah->ah_curchan &&
			   IS_CHAN_HALF_RATE(ah->ah_curchan)) {
			bitsPerSymbol =	(kbps * OFDM_SYMBOL_TIME_HALF) / 1000;
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
		DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
			"Unknown phy %u (rate ix %u)\n",
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

void ath9k_hw_get_channel_centers(struct ath_hal *ah,
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

	centers->ctl_center =
		centers->synth_center - (extoff * HT40_CHANNEL_CENTER_SHIFT);
	centers->ext_center =
		centers->synth_center + (extoff *
			 ((ahp->ah_extprotspacing == ATH9K_HT_EXTPROTSPACING_20) ?
			  HT40_CHANNEL_CENTER_SHIFT : 15));

}

/******************/
/* Chip Revisions */
/******************/

static void ath9k_hw_read_revisions(struct ath_hal *ah)
{
	u32 val;

	val = REG_READ(ah, AR_SREV) & AR_SREV_ID;

	if (val == 0xFF) {
		val = REG_READ(ah, AR_SREV);
		ah->ah_macVersion = (val & AR_SREV_VERSION2) >> AR_SREV_TYPE2_S;
		ah->ah_macRev = MS(val, AR_SREV_REVISION2);
		ah->ah_isPciExpress = (val & AR_SREV_TYPE2_HOST_MODE) ? 0 : 1;
	} else {
		if (!AR_SREV_9100(ah))
			ah->ah_macVersion = MS(val, AR_SREV_VERSION);

		ah->ah_macRev = val & AR_SREV_REVISION;

		if (ah->ah_macVersion == AR_SREV_VERSION_5416_PCIE)
			ah->ah_isPciExpress = true;
	}
}

static int ath9k_hw_get_radiorev(struct ath_hal *ah)
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

/************************************/
/* HW Attach, Detach, Init Routines */
/************************************/

static void ath9k_hw_disablepcie(struct ath_hal *ah)
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
					"address test failed "
					"addr: 0x%08x - wr:0x%08x != rd:0x%08x\n",
					addr, wrData, rdData);
				return false;
			}
		}
		for (j = 0; j < 4; j++) {
			wrData = patternData[j];
			REG_WRITE(ah, addr, wrData);
			rdData = REG_READ(ah, addr);
			if (wrData != rdData) {
				DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
					"address test failed "
					"addr: 0x%08x - wr:0x%08x != rd:0x%08x\n",
					addr, wrData, rdData);
				return false;
			}
		}
		REG_WRITE(ah, regAddr[i], regHold[i]);
	}
	udelay(100);
	return true;
}

static const char *ath9k_hw_devname(u16 devid)
{
	switch (devid) {
	case AR5416_DEVID_PCI:
		return "Atheros 5416";
	case AR5416_DEVID_PCIE:
		return "Atheros 5418";
	case AR9160_DEVID_PCI:
		return "Atheros 9160";
	case AR9280_DEVID_PCI:
	case AR9280_DEVID_PCIE:
		return "Atheros 9280";
	case AR9285_DEVID_PCIE:
		return "Atheros 9285";
	}

	return NULL;
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
	ah->ah_config.enable_ani = 1;
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

	ah->ah_config.intr_mitigation = 1;

	/*
	 * We need this for PCI devices only (Cardbus, PCI, miniPCI)
	 * _and_ if on non-uniprocessor systems (Multiprocessor/HT).
	 * This means we use it for all AR5416 devices, and the few
	 * minor PCI AR9280 devices out there.
	 *
	 * Serialization is required because these devices do not handle
	 * well the case of two concurrent reads/writes due to the latency
	 * involved. During one read/write another read/write can be issued
	 * on another CPU while the previous read/write may still be working
	 * on our hardware, if we hit this case the hardware poops in a loop.
	 * We prevent this by serializing reads and writes.
	 *
	 * This issue is not present on PCI-Express devices or pre-AR5416
	 * devices (legacy, 802.11abg).
	 */
	if (num_possible_cpus() > 1)
		ah->ah_config.serialize_regmode = SER_REG_MODE_AUTO;
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
			"Cannot allocate memory for state block\n");
		*status = -ENOMEM;
		return NULL;
	}

	ah = &ahp->ah;
	ah->ah_sc = sc;
	ah->ah_sh = mem;
	ah->ah_magic = AR5416_MAGIC;
	ah->ah_countryCode = CTRY_DEFAULT;
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

static int ath9k_hw_rfattach(struct ath_hal *ah)
{
	bool rfStatus = false;
	int ecode = 0;

	rfStatus = ath9k_hw_init_rf(ah, &ecode);
	if (!rfStatus) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET,
			"RF setup failed, status %u\n", ecode);
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
			"5G Radio Chip Rev 0x%02X is not "
			"supported by this driver\n",
			ah->ah_analog5GhzRev);
		return -EOPNOTSUPP;
	}

	ah->ah_analog5GhzRev = val;

	return 0;
}

static int ath9k_hw_init_macaddr(struct ath_hal *ah)
{
	u32 sum;
	int i;
	u16 eeval;
	struct ath_hal_5416 *ahp = AH5416(ah);

	sum = 0;
	for (i = 0; i < 3; i++) {
		eeval = ath9k_hw_get_eeprom(ah, AR_EEPROM_MAC(i));
		sum += eeval;
		ahp->ah_macaddr[2 * i] = eeval >> 8;
		ahp->ah_macaddr[2 * i + 1] = eeval & 0xff;
	}
	if (sum == 0 || sum == 0xffff * 3) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			"mac address read failed: %pM\n",
			ahp->ah_macaddr);
		return -EADDRNOTAVAIL;
	}

	return 0;
}

static void ath9k_hw_init_rxgain_ini(struct ath_hal *ah)
{
	u32 rxgain_type;
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (ath9k_hw_get_eeprom(ah, EEP_MINOR_REV) >= AR5416_EEP_MINOR_VER_17) {
		rxgain_type = ath9k_hw_get_eeprom(ah, EEP_RXGAIN_TYPE);

		if (rxgain_type == AR5416_EEP_RXGAIN_13DB_BACKOFF)
			INIT_INI_ARRAY(&ahp->ah_iniModesRxGain,
			ar9280Modes_backoff_13db_rxgain_9280_2,
			ARRAY_SIZE(ar9280Modes_backoff_13db_rxgain_9280_2), 6);
		else if (rxgain_type == AR5416_EEP_RXGAIN_23DB_BACKOFF)
			INIT_INI_ARRAY(&ahp->ah_iniModesRxGain,
			ar9280Modes_backoff_23db_rxgain_9280_2,
			ARRAY_SIZE(ar9280Modes_backoff_23db_rxgain_9280_2), 6);
		else
			INIT_INI_ARRAY(&ahp->ah_iniModesRxGain,
			ar9280Modes_original_rxgain_9280_2,
			ARRAY_SIZE(ar9280Modes_original_rxgain_9280_2), 6);
	} else
		INIT_INI_ARRAY(&ahp->ah_iniModesRxGain,
			ar9280Modes_original_rxgain_9280_2,
			ARRAY_SIZE(ar9280Modes_original_rxgain_9280_2), 6);
}

static void ath9k_hw_init_txgain_ini(struct ath_hal *ah)
{
	u32 txgain_type;
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (ath9k_hw_get_eeprom(ah, EEP_MINOR_REV) >= AR5416_EEP_MINOR_VER_19) {
		txgain_type = ath9k_hw_get_eeprom(ah, EEP_TXGAIN_TYPE);

		if (txgain_type == AR5416_EEP_TXGAIN_HIGH_POWER)
			INIT_INI_ARRAY(&ahp->ah_iniModesTxGain,
			ar9280Modes_high_power_tx_gain_9280_2,
			ARRAY_SIZE(ar9280Modes_high_power_tx_gain_9280_2), 6);
		else
			INIT_INI_ARRAY(&ahp->ah_iniModesTxGain,
			ar9280Modes_original_tx_gain_9280_2,
			ARRAY_SIZE(ar9280Modes_original_tx_gain_9280_2), 6);
	} else
		INIT_INI_ARRAY(&ahp->ah_iniModesTxGain,
		ar9280Modes_original_tx_gain_9280_2,
		ARRAY_SIZE(ar9280Modes_original_tx_gain_9280_2), 6);
}

static int ath9k_hw_post_attach(struct ath_hal *ah)
{
	int ecode;

	if (!ath9k_hw_chip_test(ah)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
			"hardware self-test failed\n");
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

static struct ath_hal *ath9k_hw_do_attach(u16 devid, struct ath_softc *sc,
					  void __iomem *mem, int *status)
{
	struct ath_hal_5416 *ahp;
	struct ath_hal *ah;
	int ecode;
	u32 i, j;

	ahp = ath9k_hw_newstate(devid, sc, mem, status);
	if (ahp == NULL)
		return NULL;

	ah = &ahp->ah;

	ath9k_hw_set_defaults(ah);

	if (ah->ah_config.intr_mitigation != 0)
		ahp->ah_intrMitigation = true;

	if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_POWER_ON)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "Couldn't reset chip\n");
		ecode = -EIO;
		goto bad;
	}

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "Couldn't wakeup chip\n");
		ecode = -EIO;
		goto bad;
	}

	if (ah->ah_config.serialize_regmode == SER_REG_MODE_AUTO) {
		if (ah->ah_macVersion == AR_SREV_VERSION_5416_PCI ||
		    (AR_SREV_9280(ah) && !ah->ah_isPciExpress)) {
			ah->ah_config.serialize_regmode =
				SER_REG_MODE_ON;
		} else {
			ah->ah_config.serialize_regmode =
				SER_REG_MODE_OFF;
		}
	}

	DPRINTF(ah->ah_sc, ATH_DBG_RESET,
		"serialize_regmode is %d\n",
		ah->ah_config.serialize_regmode);

	if ((ah->ah_macVersion != AR_SREV_VERSION_5416_PCI) &&
	    (ah->ah_macVersion != AR_SREV_VERSION_5416_PCIE) &&
	    (ah->ah_macVersion != AR_SREV_VERSION_9160) &&
	    (!AR_SREV_9100(ah)) && (!AR_SREV_9280(ah)) && (!AR_SREV_9285(ah))) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET,
			"Mac Chip Rev 0x%02x.%x is not supported by "
			"this driver\n", ah->ah_macVersion, ah->ah_macRev);
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
		ahp->ah_suppCals = ADC_GAIN_CAL | ADC_DC_CAL | IQ_MISMATCH_CAL;
	}

	if (AR_SREV_9160(ah)) {
		ah->ah_config.enable_ani = 1;
		ahp->ah_ani_function = (ATH9K_ANI_SPUR_IMMUNITY_LEVEL |
					ATH9K_ANI_FIRSTEP_LEVEL);
	} else {
		ahp->ah_ani_function = ATH9K_ANI_ALL;
		if (AR_SREV_9280_10_OR_LATER(ah)) {
			ahp->ah_ani_function &=	~ATH9K_ANI_NOISE_IMMUNITY_LEVEL;
		}
	}

	DPRINTF(ah->ah_sc, ATH_DBG_RESET,
		"This Mac Chip Rev 0x%02x.%x is \n",
		ah->ah_macVersion, ah->ah_macRev);

	if (AR_SREV_9285_12_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ahp->ah_iniModes, ar9285Modes_9285_1_2,
			       ARRAY_SIZE(ar9285Modes_9285_1_2), 6);
		INIT_INI_ARRAY(&ahp->ah_iniCommon, ar9285Common_9285_1_2,
			       ARRAY_SIZE(ar9285Common_9285_1_2), 2);

		if (ah->ah_config.pcie_clock_req) {
			INIT_INI_ARRAY(&ahp->ah_iniPcieSerdes,
			ar9285PciePhy_clkreq_off_L1_9285_1_2,
			ARRAY_SIZE(ar9285PciePhy_clkreq_off_L1_9285_1_2), 2);
		} else {
			INIT_INI_ARRAY(&ahp->ah_iniPcieSerdes,
			ar9285PciePhy_clkreq_always_on_L1_9285_1_2,
			ARRAY_SIZE(ar9285PciePhy_clkreq_always_on_L1_9285_1_2),
				  2);
		}
	} else if (AR_SREV_9285_10_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ahp->ah_iniModes, ar9285Modes_9285,
			       ARRAY_SIZE(ar9285Modes_9285), 6);
		INIT_INI_ARRAY(&ahp->ah_iniCommon, ar9285Common_9285,
			       ARRAY_SIZE(ar9285Common_9285), 2);

		if (ah->ah_config.pcie_clock_req) {
			INIT_INI_ARRAY(&ahp->ah_iniPcieSerdes,
			ar9285PciePhy_clkreq_off_L1_9285,
			ARRAY_SIZE(ar9285PciePhy_clkreq_off_L1_9285), 2);
		} else {
			INIT_INI_ARRAY(&ahp->ah_iniPcieSerdes,
			ar9285PciePhy_clkreq_always_on_L1_9285,
			ARRAY_SIZE(ar9285PciePhy_clkreq_always_on_L1_9285), 2);
		}
	} else if (AR_SREV_9280_20_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ahp->ah_iniModes, ar9280Modes_9280_2,
			       ARRAY_SIZE(ar9280Modes_9280_2), 6);
		INIT_INI_ARRAY(&ahp->ah_iniCommon, ar9280Common_9280_2,
			       ARRAY_SIZE(ar9280Common_9280_2), 2);

		if (ah->ah_config.pcie_clock_req) {
			INIT_INI_ARRAY(&ahp->ah_iniPcieSerdes,
			       ar9280PciePhy_clkreq_off_L1_9280,
			       ARRAY_SIZE(ar9280PciePhy_clkreq_off_L1_9280),2);
		} else {
			INIT_INI_ARRAY(&ahp->ah_iniPcieSerdes,
			       ar9280PciePhy_clkreq_always_on_L1_9280,
			       ARRAY_SIZE(ar9280PciePhy_clkreq_always_on_L1_9280), 2);
		}
		INIT_INI_ARRAY(&ahp->ah_iniModesAdditional,
			       ar9280Modes_fast_clock_9280_2,
			       ARRAY_SIZE(ar9280Modes_fast_clock_9280_2), 3);
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
		ath9k_hw_disablepcie(ah);

	ecode = ath9k_hw_post_attach(ah);
	if (ecode != 0)
		goto bad;

	/* rxgain table */
	if (AR_SREV_9280_20(ah))
		ath9k_hw_init_rxgain_ini(ah);

	/* txgain table */
	if (AR_SREV_9280_20(ah))
		ath9k_hw_init_txgain_ini(ah);

	if (ah->ah_devid == AR9280_DEVID_PCI) {
		for (i = 0; i < ahp->ah_iniModes.ia_rows; i++) {
			u32 reg = INI_RA(&ahp->ah_iniModes, i, 0);

			for (j = 1; j < ahp->ah_iniModes.ia_columns; j++) {
				u32 val = INI_RA(&ahp->ah_iniModes, i, j);

				INI_RA(&ahp->ah_iniModes, i, j) =
					ath9k_hw_ini_fixup(ah,
							   &ahp->ah_eeprom.def,
							   reg, val);
			}
		}
	}

	if (!ath9k_hw_fill_cap_info(ah)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET,
			"failed ath9k_hw_fill_cap_info\n");
		ecode = -EINVAL;
		goto bad;
	}

	ecode = ath9k_hw_init_macaddr(ah);
	if (ecode != 0) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET,
			"failed initializing mac address\n");
		goto bad;
	}

	if (AR_SREV_9285(ah))
		ah->ah_txTrigLevel = (AR_FTRIG_256B >> AR_FTRIG_S);
	else
		ah->ah_txTrigLevel = (AR_FTRIG_512B >> AR_FTRIG_S);

	ath9k_init_nfcal_hist_buffer(ah);

	return ah;
bad:
	if (ahp)
		ath9k_hw_detach((struct ath_hal *) ahp);
	if (status)
		*status = ecode;

	return NULL;
}

static void ath9k_hw_init_bb(struct ath_hal *ah,
			     struct ath9k_channel *chan)
{
	u32 synthDelay;

	synthDelay = REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_DELAY;
	if (IS_CHAN_B(chan))
		synthDelay = (4 * synthDelay) / 22;
	else
		synthDelay /= 10;

	REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);

	udelay(synthDelay + BASE_ACTIVATE_DELAY);
}

static void ath9k_hw_init_qos(struct ath_hal *ah)
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

static void ath9k_hw_init_pll(struct ath_hal *ah,
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

static void ath9k_hw_init_chain_masks(struct ath_hal *ah)
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

static void ath9k_hw_init_interrupt_masks(struct ath_hal *ah,
					  enum nl80211_iftype opmode)
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

	if (opmode == NL80211_IFTYPE_AP)
		ahp->ah_maskReg |= AR_IMR_MIB;

	REG_WRITE(ah, AR_IMR, ahp->ah_maskReg);
	REG_WRITE(ah, AR_IMR_S2, REG_READ(ah, AR_IMR_S2) | AR_IMR_S2_GTT);

	if (!AR_SREV_9100(ah)) {
		REG_WRITE(ah, AR_INTR_SYNC_CAUSE, 0xFFFFFFFF);
		REG_WRITE(ah, AR_INTR_SYNC_ENABLE, AR_INTR_SYNC_DEFAULT);
		REG_WRITE(ah, AR_INTR_SYNC_MASK, 0);
	}
}

static bool ath9k_hw_set_ack_timeout(struct ath_hal *ah, u32 us)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (us > ath9k_hw_mac_to_usec(ah, MS(0xffffffff, AR_TIME_OUT_ACK))) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "bad ack timeout %u\n", us);
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
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "bad cts timeout %u\n", us);
		ahp->ah_ctstimeout = (u32) -1;
		return false;
	} else {
		REG_RMW_FIELD(ah, AR_TIME_OUT,
			      AR_TIME_OUT_CTS, ath9k_hw_mac_to_clks(ah, us));
		ahp->ah_ctstimeout = us;
		return true;
	}
}

static bool ath9k_hw_set_global_txtimeout(struct ath_hal *ah, u32 tu)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (tu > 0xFFFF) {
		DPRINTF(ah->ah_sc, ATH_DBG_XMIT,
			"bad global tx timeout %u\n", tu);
		ahp->ah_globaltxtimeout = (u32) -1;
		return false;
	} else {
		REG_RMW_FIELD(ah, AR_GTXTO, AR_GTXTO_TIMEOUT_LIMIT, tu);
		ahp->ah_globaltxtimeout = tu;
		return true;
	}
}

static void ath9k_hw_init_user_settings(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	DPRINTF(ah->ah_sc, ATH_DBG_RESET, "ahp->ah_miscMode 0x%x\n",
		ahp->ah_miscMode);

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

const char *ath9k_hw_probe(u16 vendorid, u16 devid)
{
	return vendorid == ATHEROS_VENDOR_ID ?
		ath9k_hw_devname(devid) : NULL;
}

void ath9k_hw_detach(struct ath_hal *ah)
{
	if (!AR_SREV_9100(ah))
		ath9k_hw_ani_detach(ah);

	ath9k_hw_rfdetach(ah);
	ath9k_hw_setpower(ah, ATH9K_PM_FULL_SLEEP);
	kfree(ah);
}

struct ath_hal *ath9k_hw_attach(u16 devid, struct ath_softc *sc,
				void __iomem *mem, int *error)
{
	struct ath_hal *ah = NULL;

	switch (devid) {
	case AR5416_DEVID_PCI:
	case AR5416_DEVID_PCIE:
	case AR9160_DEVID_PCI:
	case AR9280_DEVID_PCI:
	case AR9280_DEVID_PCIE:
	case AR9285_DEVID_PCIE:
		ah = ath9k_hw_do_attach(devid, sc, mem, error);
		break;
	default:
		*error = -ENXIO;
		break;
	}

	return ah;
}

/*******/
/* INI */
/*******/

static void ath9k_hw_override_ini(struct ath_hal *ah,
				  struct ath9k_channel *chan)
{
	/*
	 * Set the RX_ABORT and RX_DIS and clear if off only after
	 * RXE is set for MAC. This prevents frames with corrupted
	 * descriptor status.
	 */
	REG_SET_BIT(ah, AR_DIAG_SW, (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));


	if (!AR_SREV_5416_V20_OR_LATER(ah) ||
	    AR_SREV_9280_10_OR_LATER(ah))
		return;

	REG_WRITE(ah, 0x9800 + (651 << 2), 0x11);
}

static u32 ath9k_hw_def_ini_fixup(struct ath_hal *ah,
			      struct ar5416_eeprom_def *pEepData,
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
				value |= AR_AN_TOP2_PWDCLKIND &
					(pBase->pwdclkind << AR_AN_TOP2_PWDCLKIND_S);
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

static u32 ath9k_hw_ini_fixup(struct ath_hal *ah,
			      struct ar5416_eeprom_def *pEepData,
			      u32 reg, u32 value)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (ahp->ah_eep_map == EEP_MAP_4KBITS)
		return value;
	else
		return ath9k_hw_def_ini_fixup(ah, pEepData, reg, value);
}

static int ath9k_hw_process_ini(struct ath_hal *ah,
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

		(ahp->ah_addac5416_21)[31 * ahp->ah_iniAddac.ia_columns + 1] = 0;

		temp.ia_array = ahp->ah_addac5416_21;
		temp.ia_columns = ahp->ah_iniAddac.ia_columns;
		temp.ia_rows = ahp->ah_iniAddac.ia_rows;
		REG_WRITE_ARRAY(&temp, 1, regWrites);
	}

	REG_WRITE(ah, AR_PHY_ADC_SERIAL_CTL, AR_PHY_SEL_INTERNAL_ADDAC);

	for (i = 0; i < ahp->ah_iniModes.ia_rows; i++) {
		u32 reg = INI_RA(&ahp->ah_iniModes, i, 0);
		u32 val = INI_RA(&ahp->ah_iniModes, i, modesIndex);

		REG_WRITE(ah, reg, val);

		if (reg >= 0x7800 && reg < 0x78a0
		    && ah->ah_config.analog_shiftreg) {
			udelay(100);
		}

		DO_DELAY(regWrites);
	}

	if (AR_SREV_9280(ah))
		REG_WRITE_ARRAY(&ahp->ah_iniModesRxGain, modesIndex, regWrites);

	if (AR_SREV_9280(ah))
		REG_WRITE_ARRAY(&ahp->ah_iniModesTxGain, modesIndex, regWrites);

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

	status = ath9k_hw_set_txpower(ah, chan,
				      ath9k_regd_get_ctl(ah, chan),
				      ath9k_regd_get_antenna_allowed(ah,
								     chan),
				      chan->maxRegTxPower * 2,
				      min((u32) MAX_RATE_POWER,
					  (u32) ah->ah_powerLimit));
	if (status != 0) {
		DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT,
			"error init'ing transmit power\n");
		return -EIO;
	}

	if (!ath9k_hw_set_rf_regs(ah, chan, freqIndex)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
			"ar5416SetRfRegs failed\n");
		return -EIO;
	}

	return 0;
}

/****************************************/
/* Reset and Channel Switching Routines */
/****************************************/

static void ath9k_hw_set_rfmode(struct ath_hal *ah, struct ath9k_channel *chan)
{
	u32 rfMode = 0;

	if (chan == NULL)
		return;

	rfMode |= (IS_CHAN_B(chan) || IS_CHAN_G(chan))
		? AR_PHY_MODE_DYNAMIC : AR_PHY_MODE_OFDM;

	if (!AR_SREV_9280_10_OR_LATER(ah))
		rfMode |= (IS_CHAN_5GHZ(chan)) ?
			AR_PHY_MODE_RF5GHZ : AR_PHY_MODE_RF2GHZ;

	if (AR_SREV_9280_20(ah) && IS_CHAN_A_5MHZ_SPACED(chan))
		rfMode |= (AR_PHY_MODE_DYNAMIC | AR_PHY_MODE_DYN_CCK_DISABLE);

	REG_WRITE(ah, AR_PHY_MODE, rfMode);
}

static void ath9k_hw_mark_phy_inactive(struct ath_hal *ah)
{
	REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
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

static void ath9k_hw_set_operating_mode(struct ath_hal *ah, int opmode)
{
	u32 val;

	val = REG_READ(ah, AR_STA_ID1);
	val &= ~(AR_STA_ID1_STA_AP | AR_STA_ID1_ADHOC);
	switch (opmode) {
	case NL80211_IFTYPE_AP:
		REG_WRITE(ah, AR_STA_ID1, val | AR_STA_ID1_STA_AP
			  | AR_STA_ID1_KSRCH_MODE);
		REG_CLR_BIT(ah, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
		break;
	case NL80211_IFTYPE_ADHOC:
		REG_WRITE(ah, AR_STA_ID1, val | AR_STA_ID1_ADHOC
			  | AR_STA_ID1_KSRCH_MODE);
		REG_SET_BIT(ah, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_MONITOR:
		REG_WRITE(ah, AR_STA_ID1, val | AR_STA_ID1_KSRCH_MODE);
		break;
	}
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

static void ath9k_hw_set_delta_slope(struct ath_hal *ah,
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
			"RTC stuck in MAC reset\n");
		return false;
	}

	if (!AR_SREV_9100(ah))
		REG_WRITE(ah, AR_RC, 0);

	ath9k_hw_init_pll(ah, NULL);

	if (AR_SREV_9100(ah))
		udelay(50);

	return true;
}

static bool ath9k_hw_set_reset_power_on(struct ath_hal *ah)
{
	REG_WRITE(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN |
		  AR_RTC_FORCE_WAKE_ON_INT);

	REG_WRITE(ah, (u16) (AR_RTC_RESET), 0);
	REG_WRITE(ah, (u16) (AR_RTC_RESET), 1);

	if (!ath9k_hw_wait(ah,
			   AR_RTC_STATUS,
			   AR_RTC_STATUS_M,
			   AR_RTC_STATUS_ON)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "RTC not waking up\n");
		return false;
	}

	ath9k_hw_read_revisions(ah);

	return ath9k_hw_set_reset(ah, ATH9K_RESET_WARM);
}

static bool ath9k_hw_set_reset_reg(struct ath_hal *ah, u32 type)
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

static void ath9k_hw_set_regs(struct ath_hal *ah, struct ath9k_channel *chan,
			      enum ath9k_ht_macmode macmode)
{
	u32 phymode;
	u32 enableDacFifo = 0;
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (AR_SREV_9285_10_OR_LATER(ah))
		enableDacFifo = (REG_READ(ah, AR_PHY_TURBO) &
					 AR_PHY_FC_ENABLE_DAC_FIFO);

	phymode = AR_PHY_FC_HT_EN | AR_PHY_FC_SHORT_GI_40
		| AR_PHY_FC_SINGLE_HT_LTF1 | AR_PHY_FC_WALSH | enableDacFifo;

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

static struct ath9k_channel *ath9k_hw_check_chan(struct ath_hal *ah,
						 struct ath9k_channel *chan)
{
	if (!(IS_CHAN_2GHZ(chan) ^ IS_CHAN_5GHZ(chan))) {
		DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
			"invalid channel %u/0x%x; not marked as "
			"2GHz or 5GHz\n", chan->channel, chan->channelFlags);
		return NULL;
	}

	if (!IS_CHAN_OFDM(chan) &&
	    !IS_CHAN_B(chan) &&
	    !IS_CHAN_HT20(chan) &&
	    !IS_CHAN_HT40(chan)) {
		DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
			"invalid channel %u/0x%x; not marked as "
			"OFDM or CCK or HT20 or HT40PLUS or HT40MINUS\n",
			chan->channel, chan->channelFlags);
		return NULL;
	}

	return ath9k_regd_check_channel(ah, chan);
}

static bool ath9k_hw_channel_change(struct ath_hal *ah,
				    struct ath9k_channel *chan,
				    enum ath9k_ht_macmode macmode)
{
	u32 synthDelay, qnum;

	for (qnum = 0; qnum < AR_NUM_QCU; qnum++) {
		if (ath9k_hw_numtxpending(ah, qnum)) {
			DPRINTF(ah->ah_sc, ATH_DBG_QUEUE,
				"Transmit frames pending on queue %d\n", qnum);
			return false;
		}
	}

	REG_WRITE(ah, AR_PHY_RFBUS_REQ, AR_PHY_RFBUS_REQ_EN);
	if (!ath9k_hw_wait(ah, AR_PHY_RFBUS_GRANT, AR_PHY_RFBUS_GRANT_EN,
			   AR_PHY_RFBUS_GRANT_EN)) {
		DPRINTF(ah->ah_sc, ATH_DBG_REG_IO,
			"Could not kill baseband RX\n");
		return false;
	}

	ath9k_hw_set_regs(ah, chan, macmode);

	if (AR_SREV_9280_10_OR_LATER(ah)) {
		if (!(ath9k_hw_ar9280_set_channel(ah, chan))) {
			DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
				"failed to set channel\n");
			return false;
		}
	} else {
		if (!(ath9k_hw_set_channel(ah, chan))) {
			DPRINTF(ah->ah_sc, ATH_DBG_CHANNEL,
				"failed to set channel\n");
			return false;
		}
	}

	if (ath9k_hw_set_txpower(ah, chan,
				 ath9k_regd_get_ctl(ah, chan),
				 ath9k_regd_get_antenna_allowed(ah, chan),
				 chan->maxRegTxPower * 2,
				 min((u32) MAX_RATE_POWER,
				     (u32) ah->ah_powerLimit)) != 0) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			"error init'ing transmit power\n");
		return false;
	}

	synthDelay = REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_DELAY;
	if (IS_CHAN_B(chan))
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

static void ath9k_hw_9280_spur_mitigate(struct ath_hal *ah, struct ath9k_channel *chan)
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

static void ath9k_hw_spur_mitigate(struct ath_hal *ah, struct ath9k_channel *chan)
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

bool ath9k_hw_reset(struct ath_hal *ah, struct ath9k_channel *chan,
		    enum ath9k_ht_macmode macmode,
		    u8 txchainmask, u8 rxchainmask,
		    enum ath9k_ht_extprotspacing extprotspacing,
		    bool bChannelChange, int *status)
{
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
			"invalid channel %u/0x%x; no mapping\n",
			chan->channel, chan->channelFlags);
		ecode = -EINVAL;
		goto bad;
	}

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE)) {
		ecode = -EIO;
		goto bad;
	}

	if (curchan)
		ath9k_hw_getnf(ah, curchan);

	if (bChannelChange &&
	    (ahp->ah_chipFullSleep != true) &&
	    (ah->ah_curchan != NULL) &&
	    (chan->channel != ah->ah_curchan->channel) &&
	    ((chan->channelFlags & CHANNEL_ALL) ==
	     (ah->ah_curchan->channelFlags & CHANNEL_ALL)) &&
	    (!AR_SREV_9280(ah) || (!IS_CHAN_A_5MHZ_SPACED(chan) &&
				   !IS_CHAN_A_5MHZ_SPACED(ah->ah_curchan)))) {

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
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "chip reset failed\n");
		ecode = -EINVAL;
		goto bad;
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
		ath9k_hw_cfg_output(ah, 9, AR_GPIO_OUTPUT_MUX_AS_OUTPUT);
	}

	ecode = ath9k_hw_process_ini(ah, chan, macmode);
	if (ecode != 0) {
		ecode = -EINVAL;
		goto bad;
	}

	if (IS_CHAN_OFDM(chan) || IS_CHAN_HT(chan))
		ath9k_hw_set_delta_slope(ah, chan);

	if (AR_SREV_9280_10_OR_LATER(ah))
		ath9k_hw_9280_spur_mitigate(ah, chan);
	else
		ath9k_hw_spur_mitigate(ah, chan);

	if (!ath9k_hw_eeprom_set_board_values(ah, chan)) {
		DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
			"error setting board options\n");
		ecode = -EIO;
		goto bad;
	}

	ath9k_hw_decrease_chain_power(ah, chan);

	REG_WRITE(ah, AR_STA_ID0, get_unaligned_le32(ahp->ah_macaddr));
	REG_WRITE(ah, AR_STA_ID1, get_unaligned_le16(ahp->ah_macaddr + 4)
		  | macStaId1
		  | AR_STA_ID1_RTS_USE_DEF
		  | (ah->ah_config.
		     ack_6mb ? AR_STA_ID1_ACKCTS_6MB : 0)
		  | ahp->ah_staId1Defaults);
	ath9k_hw_set_operating_mode(ah, ah->ah_opmode);

	REG_WRITE(ah, AR_BSSMSKL, get_unaligned_le32(ahp->ah_bssidmask));
	REG_WRITE(ah, AR_BSSMSKU, get_unaligned_le16(ahp->ah_bssidmask + 4));

	REG_WRITE(ah, AR_DEF_ANTENNA, saveDefAntenna);

	REG_WRITE(ah, AR_BSS_ID0, get_unaligned_le32(ahp->ah_bssid));
	REG_WRITE(ah, AR_BSS_ID1, get_unaligned_le16(ahp->ah_bssid + 4) |
		  ((ahp->ah_assocId & 0x3fff) << AR_BSS_ID1_AID_S));

	REG_WRITE(ah, AR_ISR, ~0);

	REG_WRITE(ah, AR_RSSI_THR, INIT_RSSI_THR);

	if (AR_SREV_9280_10_OR_LATER(ah)) {
		if (!(ath9k_hw_ar9280_set_channel(ah, chan))) {
			ecode = -EIO;
			goto bad;
		}
	} else {
		if (!(ath9k_hw_set_channel(ah, chan))) {
			ecode = -EIO;
			goto bad;
		}
	}

	for (i = 0; i < AR_NUM_DCU; i++)
		REG_WRITE(ah, AR_DQCUMASK(i), 1 << i);

	ahp->ah_intrTxqs = 0;
	for (i = 0; i < ah->ah_caps.total_queues; i++)
		ath9k_hw_resettxqueue(ah, i);

	ath9k_hw_init_interrupt_masks(ah, ah->ah_opmode);
	ath9k_hw_init_qos(ah);

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
	if (ah->ah_caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		ath9k_enable_rfkill(ah);
#endif
	ath9k_hw_init_user_settings(ah);

	REG_WRITE(ah, AR_STA_ID1,
		  REG_READ(ah, AR_STA_ID1) | AR_STA_ID1_PRESERVE_SEQNUM);

	ath9k_hw_set_dma(ah);

	REG_WRITE(ah, AR_OBS, 8);

	if (ahp->ah_intrMitigation) {

		REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_LAST, 500);
		REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_FIRST, 2000);
	}

	ath9k_hw_init_bb(ah, chan);

	if (!ath9k_hw_init_cal(ah, chan)){
		ecode = -EIO;;
		goto bad;
	}

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
				"CFG Byte Swap Set 0x%x\n", mask);
		} else {
			mask =
				INIT_CONFIG_STATUS | AR_CFG_SWRB | AR_CFG_SWTB;
			REG_WRITE(ah, AR_CFG, mask);
			DPRINTF(ah->ah_sc, ATH_DBG_RESET,
				"Setting CFG 0x%x\n", REG_READ(ah, AR_CFG));
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
}

/************************/
/* Key Cache Management */
/************************/

bool ath9k_hw_keyreset(struct ath_hal *ah, u16 entry)
{
	u32 keyType;

	if (entry >= ah->ah_caps.keycache_size) {
		DPRINTF(ah->ah_sc, ATH_DBG_KEYCACHE,
			"entry %u out of range\n", entry);
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

bool ath9k_hw_keysetmac(struct ath_hal *ah, u16 entry, const u8 *mac)
{
	u32 macHi, macLo;

	if (entry >= ah->ah_caps.keycache_size) {
		DPRINTF(ah->ah_sc, ATH_DBG_KEYCACHE,
			"entry %u out of range\n", entry);
		return false;
	}

	if (mac != NULL) {
		macHi = (mac[5] << 8) | mac[4];
		macLo = (mac[3] << 24) |
			(mac[2] << 16) |
			(mac[1] << 8) |
			mac[0];
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

bool ath9k_hw_set_keycache_entry(struct ath_hal *ah, u16 entry,
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
			"entry %u out of range\n", entry);
		return false;
	}

	switch (k->kv_type) {
	case ATH9K_CIPHER_AES_OCB:
		keyType = AR_KEYTABLE_TYPE_AES;
		break;
	case ATH9K_CIPHER_AES_CCM:
		if (!(pCap->hw_caps & ATH9K_HW_CAP_CIPHER_AESCCM)) {
			DPRINTF(ah->ah_sc, ATH_DBG_KEYCACHE,
				"AES-CCM not supported by mac rev 0x%x\n",
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
				"entry %u inappropriate for TKIP\n", entry);
			return false;
		}
		break;
	case ATH9K_CIPHER_WEP:
		if (k->kv_len < LEN_WEP40) {
			DPRINTF(ah->ah_sc, ATH_DBG_KEYCACHE,
				"WEP key length %u too small\n", k->kv_len);
			return false;
		}
		if (k->kv_len <= LEN_WEP40)
			keyType = AR_KEYTABLE_TYPE_40;
		else if (k->kv_len <= LEN_WEP104)
			keyType = AR_KEYTABLE_TYPE_104;
		else
			keyType = AR_KEYTABLE_TYPE_128;
		break;
	case ATH9K_CIPHER_CLR:
		keyType = AR_KEYTABLE_TYPE_CLR;
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_KEYCACHE,
			"cipher %u not supported\n", k->kv_type);
		return false;
	}

	key0 = get_unaligned_le32(k->kv_val + 0) ^ xorMask;
	key1 = (get_unaligned_le16(k->kv_val + 4) ^ xorMask) & 0xffff;
	key2 = get_unaligned_le32(k->kv_val + 6) ^ xorMask;
	key3 = (get_unaligned_le16(k->kv_val + 10) ^ xorMask) & 0xffff;
	key4 = get_unaligned_le32(k->kv_val + 12) ^ xorMask;
	if (k->kv_len <= LEN_WEP104)
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

bool ath9k_hw_keyisvalid(struct ath_hal *ah, u16 entry)
{
	if (entry < ah->ah_caps.keycache_size) {
		u32 val = REG_READ(ah, AR_KEYTABLE_MAC1(entry));
		if (val & AR_KEYTABLE_VALID)
			return true;
	}
	return false;
}

/******************************/
/* Power Management (Chipset) */
/******************************/

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
		if ((REG_READ(ah, AR_RTC_STATUS) &
		     AR_RTC_STATUS_M) == AR_RTC_STATUS_SHUTDOWN) {
			if (ath9k_hw_set_reset_reg(ah,
					   ATH9K_RESET_POWER_ON) != true) {
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
				"Failed to wakeup in %uus\n", POWER_UP_TIME / 20);
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

	DPRINTF(ah->ah_sc, ATH_DBG_POWER_MGMT, "%s -> %s (%s)\n",
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
			"Unknown power mode %u\n", mode);
		return false;
	}
	ahp->ah_powerMode = mode;

	return status;
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
	} else if (AR_SREV_9280(ah) &&
		   (ah->ah_macRev == AR_SREV_REVISION_9280_10)) {
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
		if (AR_SREV_9285(ah))
			REG_WRITE(ah, AR_WA, AR9285_WA_DEFAULT);
		else if (AR_SREV_9280(ah))
			REG_WRITE(ah, AR_WA, AR9280_WA_DEFAULT);
		else
			REG_WRITE(ah, AR_WA, AR_WA_DEFAULT);
	}

}

/**********************/
/* Interrupt Handling */
/**********************/

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
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (!AR_SREV_9100(ah)) {
		if (REG_READ(ah, AR_INTR_ASYNC_CAUSE) & AR_INTR_MAC_IRQ) {
			if ((REG_READ(ah, AR_RTC_STATUS) & AR_RTC_STATUS_M)
			    == AR_RTC_STATUS_ON) {
				isr = REG_READ(ah, AR_ISR);
			}
		}

		sync_cause = REG_READ(ah, AR_INTR_SYNC_CAUSE) &
			AR_INTR_SYNC_DEFAULT;

		*masked = 0;

		if (!isr && !sync_cause)
			return false;
	} else {
		*masked = 0;
		isr = REG_READ(ah, AR_ISR);
	}

	if (isr) {
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
				"receive FIFO overrun interrupt\n");
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
					"received PCI FATAL interrupt\n");
			}
			if (sync_cause & AR_INTR_SYNC_HOST1_PERR) {
				DPRINTF(ah->ah_sc, ATH_DBG_ANY,
					"received PCI PERR interrupt\n");
			}
		}
		if (sync_cause & AR_INTR_SYNC_RADM_CPL_TIMEOUT) {
			DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT,
				"AR_INTR_SYNC_RADM_CPL_TIMEOUT\n");
			REG_WRITE(ah, AR_RC, AR_RC_HOSTIF);
			REG_WRITE(ah, AR_RC, 0);
			*masked |= ATH9K_INT_FATAL;
		}
		if (sync_cause & AR_INTR_SYNC_LOCAL_TIMEOUT) {
			DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT,
				"AR_INTR_SYNC_LOCAL_TIMEOUT\n");
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

	DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "0x%x => 0x%x\n", omask, ints);

	if (omask & ATH9K_INT_GLOBAL) {
		DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "disable IER\n");
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

	DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "new IMR 0x%x\n", mask);
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
		DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "enable IER\n");
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

/*******************/
/* Beacon Handling */
/*******************/

void ath9k_hw_beaconinit(struct ath_hal *ah, u32 next_beacon, u32 beacon_period)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	int flags = 0;

	ahp->ah_beaconInterval = beacon_period;

	switch (ah->ah_opmode) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_MONITOR:
		REG_WRITE(ah, AR_NEXT_TBTT_TIMER, TU_TO_USEC(next_beacon));
		REG_WRITE(ah, AR_NEXT_DMA_BEACON_ALERT, 0xffff);
		REG_WRITE(ah, AR_NEXT_SWBA, 0x7ffff);
		flags |= AR_TBTT_TIMER_EN;
		break;
	case NL80211_IFTYPE_ADHOC:
		REG_SET_BIT(ah, AR_TXCFG,
			    AR_TXCFG_ADHOC_BEACON_ATIM_TX_POLICY);
		REG_WRITE(ah, AR_NEXT_NDP_TIMER,
			  TU_TO_USEC(next_beacon +
				     (ahp->ah_atimWindow ? ahp->
				      ah_atimWindow : 1)));
		flags |= AR_NDP_TIMER_EN;
	case NL80211_IFTYPE_AP:
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
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_BEACON,
			"%s: unsupported opmode: %d\n",
			__func__, ah->ah_opmode);
		return;
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

void ath9k_hw_set_sta_beacon_timers(struct ath_hal *ah,
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

	DPRINTF(ah->ah_sc, ATH_DBG_BEACON, "next DTIM %d\n", bs->bs_nextdtim);
	DPRINTF(ah->ah_sc, ATH_DBG_BEACON, "next beacon %d\n", nextTbtt);
	DPRINTF(ah->ah_sc, ATH_DBG_BEACON, "beacon period %d\n", beaconintval);
	DPRINTF(ah->ah_sc, ATH_DBG_BEACON, "DTIM period %d\n", dtimperiod);

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

/*******************/
/* HW Capabilities */
/*******************/

bool ath9k_hw_fill_cap_info(struct ath_hal *ah)
{
	struct ath_hal_5416 *ahp = AH5416(ah);
	struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	u16 capField = 0, eeval;

	eeval = ath9k_hw_get_eeprom(ah, EEP_REG_0);

	ah->ah_currentRD = eeval;

	eeval = ath9k_hw_get_eeprom(ah, EEP_REG_1);
	ah->ah_currentRDExt = eeval;

	capField = ath9k_hw_get_eeprom(ah, EEP_OP_CAP);

	if (ah->ah_opmode != NL80211_IFTYPE_AP &&
	    ah->ah_subvendorid == AR_SUBVENDOR_ID_NEW_A) {
		if (ah->ah_currentRD == 0x64 || ah->ah_currentRD == 0x65)
			ah->ah_currentRD += 5;
		else if (ah->ah_currentRD == 0x41)
			ah->ah_currentRD = 0x43;
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"regdomain mapped to 0x%x\n", ah->ah_currentRD);
	}

	eeval = ath9k_hw_get_eeprom(ah, EEP_OP_MODE);
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

	pCap->tx_chainmask = ath9k_hw_get_eeprom(ah, EEP_TX_MASK);
	if ((ah->ah_isPciExpress)
	    || (eeval & AR5416_OPFLAGS_11A)) {
		pCap->rx_chainmask =
			ath9k_hw_get_eeprom(ah, EEP_RX_MASK);
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

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
	ah->ah_rfsilent = ath9k_hw_get_eeprom(ah, EEP_RF_SILENT);
	if (ah->ah_rfsilent & EEP_RFSILENT_ENABLED) {
		ah->ah_rfkill_gpio =
			MS(ah->ah_rfsilent, EEP_RFSILENT_GPIO_SEL);
		ah->ah_rfkill_polarity =
			MS(ah->ah_rfsilent, EEP_RFSILENT_POLARITY);

		pCap->hw_caps |= ATH9K_HW_CAP_RFSILENT;
	}
#endif

	if ((ah->ah_macVersion == AR_SREV_VERSION_5416_PCI) ||
	    (ah->ah_macVersion == AR_SREV_VERSION_5416_PCIE) ||
	    (ah->ah_macVersion == AR_SREV_VERSION_9160) ||
	    (ah->ah_macVersion == AR_SREV_VERSION_9100) ||
	    (ah->ah_macVersion == AR_SREV_VERSION_9280))
		pCap->hw_caps &= ~ATH9K_HW_CAP_AUTOSLEEP;
	else
		pCap->hw_caps |= ATH9K_HW_CAP_AUTOSLEEP;

	if (AR_SREV_9280(ah) || AR_SREV_9285(ah))
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
		ath9k_hw_get_num_ant_config(ah, ATH9K_HAL_FREQ_BAND_5GHZ);
	pCap->num_antcfg_2ghz =
		ath9k_hw_get_num_ant_config(ah, ATH9K_HAL_FREQ_BAND_2GHZ);

	return true;
}

bool ath9k_hw_getcapability(struct ath_hal *ah, enum ath9k_capability_type type,
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

bool ath9k_hw_setcapability(struct ath_hal *ah, enum ath9k_capability_type type,
			    u32 capability, u32 setting, int *status)
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

/****************************/
/* GPIO / RFKILL / Antennae */
/****************************/

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

void ath9k_hw_cfg_gpio_input(struct ath_hal *ah, u32 gpio)
{
	u32 gpio_shift;

	ASSERT(gpio < ah->ah_caps.num_gpio_pins);

	gpio_shift = gpio << 1;

	REG_RMW(ah,
		AR_GPIO_OE_OUT,
		(AR_GPIO_OE_OUT_DRV_NO << gpio_shift),
		(AR_GPIO_OE_OUT_DRV << gpio_shift));
}

u32 ath9k_hw_gpio_get(struct ath_hal *ah, u32 gpio)
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

void ath9k_hw_cfg_output(struct ath_hal *ah, u32 gpio,
			 u32 ah_signal_type)
{
	u32 gpio_shift;

	ath9k_hw_gpio_cfg_output_mux(ah, gpio, ah_signal_type);

	gpio_shift = 2 * gpio;

	REG_RMW(ah,
		AR_GPIO_OE_OUT,
		(AR_GPIO_OE_OUT_DRV_ALL << gpio_shift),
		(AR_GPIO_OE_OUT_DRV << gpio_shift));
}

void ath9k_hw_set_gpio(struct ath_hal *ah, u32 gpio, u32 val)
{
	REG_RMW(ah, AR_GPIO_IN_OUT, ((val & 1) << gpio),
		AR_GPIO_BIT(gpio));
}

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
void ath9k_enable_rfkill(struct ath_hal *ah)
{
	REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL,
		    AR_GPIO_INPUT_EN_VAL_RFSILENT_BB);

	REG_CLR_BIT(ah, AR_GPIO_INPUT_MUX2,
		    AR_GPIO_INPUT_MUX2_RFSILENT);

	ath9k_hw_cfg_gpio_input(ah, ah->ah_rfkill_gpio);
	REG_SET_BIT(ah, AR_PHY_TEST, RFSILENT_BB);
}
#endif

int ath9k_hw_select_antconfig(struct ath_hal *ah, u32 cfg)
{
	struct ath9k_channel *chan = ah->ah_curchan;
	const struct ath9k_hw_capabilities *pCap = &ah->ah_caps;
	u16 ant_config;
	u32 halNumAntConfig;

	halNumAntConfig = IS_CHAN_2GHZ(chan) ?
		pCap->num_antcfg_2ghz : pCap->num_antcfg_5ghz;

	if (cfg < halNumAntConfig) {
		if (!ath9k_hw_get_eeprom_antenna_cfg(ah, chan,
						     cfg, &ant_config)) {
			REG_WRITE(ah, AR_PHY_SWITCH_COM, ant_config);
			return 0;
		}
	}

	return -EINVAL;
}

u32 ath9k_hw_getdefantenna(struct ath_hal *ah)
{
	return REG_READ(ah, AR_DEF_ANTENNA) & 0x7;
}

void ath9k_hw_setantenna(struct ath_hal *ah, u32 antenna)
{
	REG_WRITE(ah, AR_DEF_ANTENNA, (antenna & 0x7));
}

bool ath9k_hw_setantennaswitch(struct ath_hal *ah,
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

/*********************/
/* General Operation */
/*********************/

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

bool ath9k_hw_set_txpowerlimit(struct ath_hal *ah, u32 limit)
{
	struct ath9k_channel *chan = ah->ah_curchan;

	ah->ah_powerLimit = min(limit, (u32) MAX_RATE_POWER);

	if (ath9k_hw_set_txpower(ah, chan,
				 ath9k_regd_get_ctl(ah, chan),
				 ath9k_regd_get_antenna_allowed(ah, chan),
				 chan->maxRegTxPower * 2,
				 min((u32) MAX_RATE_POWER,
				     (u32) ah->ah_powerLimit)) != 0)
		return false;

	return true;
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

void ath9k_hw_setopmode(struct ath_hal *ah)
{
	ath9k_hw_set_operating_mode(ah, ah->ah_opmode);
}

void ath9k_hw_setmcastfilter(struct ath_hal *ah, u32 filter0, u32 filter1)
{
	REG_WRITE(ah, AR_MCAST_FIL0, filter0);
	REG_WRITE(ah, AR_MCAST_FIL1, filter1);
}

void ath9k_hw_getbssidmask(struct ath_hal *ah, u8 *mask)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	memcpy(mask, ahp->ah_bssidmask, ETH_ALEN);
}

bool ath9k_hw_setbssidmask(struct ath_hal *ah, const u8 *mask)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	memcpy(ahp->ah_bssidmask, mask, ETH_ALEN);

	REG_WRITE(ah, AR_BSSMSKL, get_unaligned_le32(ahp->ah_bssidmask));
	REG_WRITE(ah, AR_BSSMSKU, get_unaligned_le16(ahp->ah_bssidmask + 4));

	return true;
}

void ath9k_hw_write_associd(struct ath_hal *ah, const u8 *bssid, u16 assocId)
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
				"AR_SLP32_TSF_WRITE_STATUS limit exceeded\n");
			break;
		}
		udelay(10);
	}
	REG_WRITE(ah, AR_RESET_TSF, AR_RESET_TSF_ONCE);
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

bool ath9k_hw_setslottime(struct ath_hal *ah, u32 us)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (us < ATH9K_SLOT_TIME_9 || us > ath9k_hw_mac_to_usec(ah, 0xffff)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "bad slot time %u\n", us);
		ahp->ah_slottime = (u32) -1;
		return false;
	} else {
		REG_WRITE(ah, AR_D_GBL_IFS_SLOT, ath9k_hw_mac_to_clks(ah, us));
		ahp->ah_slottime = us;
		return true;
	}
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
