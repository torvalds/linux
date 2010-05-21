/*
 * Copyright (c) 2008-2010 Atheros Communications Inc.
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
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "hw.h"
#include "hw-ops.h"
#include "rc.h"
#include "ar9003_mac.h"

#define ATH9K_CLOCK_RATE_CCK		22
#define ATH9K_CLOCK_RATE_5GHZ_OFDM	40
#define ATH9K_CLOCK_RATE_2GHZ_OFDM	44
#define ATH9K_CLOCK_FAST_RATE_5GHZ_OFDM 44

static bool ath9k_hw_set_reset_reg(struct ath_hw *ah, u32 type);

MODULE_AUTHOR("Atheros Communications");
MODULE_DESCRIPTION("Support for Atheros 802.11n wireless LAN cards.");
MODULE_SUPPORTED_DEVICE("Atheros 802.11n WLAN cards");
MODULE_LICENSE("Dual BSD/GPL");

static int __init ath9k_init(void)
{
	return 0;
}
module_init(ath9k_init);

static void __exit ath9k_exit(void)
{
	return;
}
module_exit(ath9k_exit);

/* Private hardware callbacks */

static void ath9k_hw_init_cal_settings(struct ath_hw *ah)
{
	ath9k_hw_private_ops(ah)->init_cal_settings(ah);
}

static void ath9k_hw_init_mode_regs(struct ath_hw *ah)
{
	ath9k_hw_private_ops(ah)->init_mode_regs(ah);
}

static bool ath9k_hw_macversion_supported(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);

	return priv_ops->macversion_supported(ah->hw_version.macVersion);
}

static u32 ath9k_hw_compute_pll_control(struct ath_hw *ah,
					struct ath9k_channel *chan)
{
	return ath9k_hw_private_ops(ah)->compute_pll_control(ah, chan);
}

static void ath9k_hw_init_mode_gain_regs(struct ath_hw *ah)
{
	if (!ath9k_hw_private_ops(ah)->init_mode_gain_regs)
		return;

	ath9k_hw_private_ops(ah)->init_mode_gain_regs(ah);
}

/********************/
/* Helper Functions */
/********************/

static u32 ath9k_hw_mac_clks(struct ath_hw *ah, u32 usecs)
{
	struct ieee80211_conf *conf = &ath9k_hw_common(ah)->hw->conf;

	if (!ah->curchan) /* should really check for CCK instead */
		return usecs *ATH9K_CLOCK_RATE_CCK;
	if (conf->channel->band == IEEE80211_BAND_2GHZ)
		return usecs *ATH9K_CLOCK_RATE_2GHZ_OFDM;

	if (ah->caps.hw_caps & ATH9K_HW_CAP_FASTCLOCK)
		return usecs * ATH9K_CLOCK_FAST_RATE_5GHZ_OFDM;
	else
		return usecs * ATH9K_CLOCK_RATE_5GHZ_OFDM;
}

static u32 ath9k_hw_mac_to_clks(struct ath_hw *ah, u32 usecs)
{
	struct ieee80211_conf *conf = &ath9k_hw_common(ah)->hw->conf;

	if (conf_is_ht40(conf))
		return ath9k_hw_mac_clks(ah, usecs) * 2;
	else
		return ath9k_hw_mac_clks(ah, usecs);
}

bool ath9k_hw_wait(struct ath_hw *ah, u32 reg, u32 mask, u32 val, u32 timeout)
{
	int i;

	BUG_ON(timeout < AH_TIME_QUANTUM);

	for (i = 0; i < (timeout / AH_TIME_QUANTUM); i++) {
		if ((REG_READ(ah, reg) & mask) == val)
			return true;

		udelay(AH_TIME_QUANTUM);
	}

	ath_print(ath9k_hw_common(ah), ATH_DBG_ANY,
		  "timeout (%d us) on reg 0x%x: 0x%08x & 0x%08x != 0x%08x\n",
		  timeout, reg, REG_READ(ah, reg), mask, val);

	return false;
}
EXPORT_SYMBOL(ath9k_hw_wait);

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

bool ath9k_get_channel_edges(struct ath_hw *ah,
			     u16 flags, u16 *low,
			     u16 *high)
{
	struct ath9k_hw_capabilities *pCap = &ah->caps;

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

u16 ath9k_hw_computetxtime(struct ath_hw *ah,
			   u8 phy, int kbps,
			   u32 frameLen, u16 rateix,
			   bool shortPreamble)
{
	u32 bitsPerSymbol, numBits, numSymbols, phyTime, txTime;

	if (kbps == 0)
		return 0;

	switch (phy) {
	case WLAN_RC_PHY_CCK:
		phyTime = CCK_PREAMBLE_BITS + CCK_PLCP_BITS;
		if (shortPreamble)
			phyTime >>= 1;
		numBits = frameLen << 3;
		txTime = CCK_SIFS_TIME + phyTime + ((numBits * 1000) / kbps);
		break;
	case WLAN_RC_PHY_OFDM:
		if (ah->curchan && IS_CHAN_QUARTER_RATE(ah->curchan)) {
			bitsPerSymbol =	(kbps * OFDM_SYMBOL_TIME_QUARTER) / 1000;
			numBits = OFDM_PLCP_BITS + (frameLen << 3);
			numSymbols = DIV_ROUND_UP(numBits, bitsPerSymbol);
			txTime = OFDM_SIFS_TIME_QUARTER
				+ OFDM_PREAMBLE_TIME_QUARTER
				+ (numSymbols * OFDM_SYMBOL_TIME_QUARTER);
		} else if (ah->curchan &&
			   IS_CHAN_HALF_RATE(ah->curchan)) {
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
		ath_print(ath9k_hw_common(ah), ATH_DBG_FATAL,
			  "Unknown phy %u (rate ix %u)\n", phy, rateix);
		txTime = 0;
		break;
	}

	return txTime;
}
EXPORT_SYMBOL(ath9k_hw_computetxtime);

void ath9k_hw_get_channel_centers(struct ath_hw *ah,
				  struct ath9k_channel *chan,
				  struct chan_centers *centers)
{
	int8_t extoff;

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
	/* 25 MHz spacing is supported by hw but not on upper layers */
	centers->ext_center =
		centers->synth_center + (extoff * HT40_CHANNEL_CENTER_SHIFT);
}

/******************/
/* Chip Revisions */
/******************/

static void ath9k_hw_read_revisions(struct ath_hw *ah)
{
	u32 val;

	val = REG_READ(ah, AR_SREV) & AR_SREV_ID;

	if (val == 0xFF) {
		val = REG_READ(ah, AR_SREV);
		ah->hw_version.macVersion =
			(val & AR_SREV_VERSION2) >> AR_SREV_TYPE2_S;
		ah->hw_version.macRev = MS(val, AR_SREV_REVISION2);
		ah->is_pciexpress = (val & AR_SREV_TYPE2_HOST_MODE) ? 0 : 1;
	} else {
		if (!AR_SREV_9100(ah))
			ah->hw_version.macVersion = MS(val, AR_SREV_VERSION);

		ah->hw_version.macRev = val & AR_SREV_REVISION;

		if (ah->hw_version.macVersion == AR_SREV_VERSION_5416_PCIE)
			ah->is_pciexpress = true;
	}
}

/************************************/
/* HW Attach, Detach, Init Routines */
/************************************/

static void ath9k_hw_disablepcie(struct ath_hw *ah)
{
	if (AR_SREV_9100(ah))
		return;

	ENABLE_REGWRITE_BUFFER(ah);

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

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);
}

/* This should work for all families including legacy */
static bool ath9k_hw_chip_test(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 regAddr[2] = { AR_STA_ID0 };
	u32 regHold[2];
	u32 patternData[4] = { 0x55555555,
			       0xaaaaaaaa,
			       0x66666666,
			       0x99999999 };
	int i, j, loop_max;

	if (!AR_SREV_9300_20_OR_LATER(ah)) {
		loop_max = 2;
		regAddr[1] = AR_PHY_BASE + (8 << 2);
	} else
		loop_max = 1;

	for (i = 0; i < loop_max; i++) {
		u32 addr = regAddr[i];
		u32 wrData, rdData;

		regHold[i] = REG_READ(ah, addr);
		for (j = 0; j < 0x100; j++) {
			wrData = (j << 16) | j;
			REG_WRITE(ah, addr, wrData);
			rdData = REG_READ(ah, addr);
			if (rdData != wrData) {
				ath_print(common, ATH_DBG_FATAL,
					  "address test failed "
					  "addr: 0x%08x - wr:0x%08x != "
					  "rd:0x%08x\n",
					  addr, wrData, rdData);
				return false;
			}
		}
		for (j = 0; j < 4; j++) {
			wrData = patternData[j];
			REG_WRITE(ah, addr, wrData);
			rdData = REG_READ(ah, addr);
			if (wrData != rdData) {
				ath_print(common, ATH_DBG_FATAL,
					  "address test failed "
					  "addr: 0x%08x - wr:0x%08x != "
					  "rd:0x%08x\n",
					  addr, wrData, rdData);
				return false;
			}
		}
		REG_WRITE(ah, regAddr[i], regHold[i]);
	}
	udelay(100);

	return true;
}

static void ath9k_hw_init_config(struct ath_hw *ah)
{
	int i;

	ah->config.dma_beacon_response_time = 2;
	ah->config.sw_beacon_response_time = 10;
	ah->config.additional_swba_backoff = 0;
	ah->config.ack_6mb = 0x0;
	ah->config.cwm_ignore_extcca = 0;
	ah->config.pcie_powersave_enable = 0;
	ah->config.pcie_clock_req = 0;
	ah->config.pcie_waen = 0;
	ah->config.analog_shiftreg = 1;
	ah->config.ofdm_trig_low = 200;
	ah->config.ofdm_trig_high = 500;
	ah->config.cck_trig_high = 200;
	ah->config.cck_trig_low = 100;

	/*
	 * For now ANI is disabled for AR9003, it is still
	 * being tested.
	 */
	if (!AR_SREV_9300_20_OR_LATER(ah))
		ah->config.enable_ani = 1;

	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		ah->config.spurchans[i][0] = AR_NO_SPUR;
		ah->config.spurchans[i][1] = AR_NO_SPUR;
	}

	if (ah->hw_version.devid != AR2427_DEVID_PCIE)
		ah->config.ht_enable = 1;
	else
		ah->config.ht_enable = 0;

	ah->config.rx_intr_mitigation = true;

	/*
	 * Tx IQ Calibration (ah->config.tx_iq_calibration) is only
	 * used by AR9003, but it is showing reliability issues.
	 * It will take a while to fix so this is currently disabled.
	 */

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
		ah->config.serialize_regmode = SER_REG_MODE_AUTO;
}

static void ath9k_hw_init_defaults(struct ath_hw *ah)
{
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);

	regulatory->country_code = CTRY_DEFAULT;
	regulatory->power_limit = MAX_RATE_POWER;
	regulatory->tp_scale = ATH9K_TP_SCALE_MAX;

	ah->hw_version.magic = AR5416_MAGIC;
	ah->hw_version.subvendorid = 0;

	ah->ah_flags = 0;
	if (!AR_SREV_9100(ah))
		ah->ah_flags = AH_USE_EEPROM;

	ah->atim_window = 0;
	ah->sta_id1_defaults = AR_STA_ID1_CRPT_MIC_ENABLE;
	ah->beacon_interval = 100;
	ah->enable_32kHz_clock = DONT_USE_32KHZ;
	ah->slottime = (u32) -1;
	ah->globaltxtimeout = (u32) -1;
	ah->power_mode = ATH9K_PM_UNDEFINED;
}

static int ath9k_hw_init_macaddr(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 sum;
	int i;
	u16 eeval;
	u32 EEP_MAC[] = { EEP_MAC_LSW, EEP_MAC_MID, EEP_MAC_MSW };

	sum = 0;
	for (i = 0; i < 3; i++) {
		eeval = ah->eep_ops->get_eeprom(ah, EEP_MAC[i]);
		sum += eeval;
		common->macaddr[2 * i] = eeval >> 8;
		common->macaddr[2 * i + 1] = eeval & 0xff;
	}
	if (sum == 0 || sum == 0xffff * 3)
		return -EADDRNOTAVAIL;

	return 0;
}

static int ath9k_hw_post_init(struct ath_hw *ah)
{
	int ecode;

	if (!AR_SREV_9271(ah)) {
		if (!ath9k_hw_chip_test(ah))
			return -ENODEV;
	}

	if (!AR_SREV_9300_20_OR_LATER(ah)) {
		ecode = ar9002_hw_rf_claim(ah);
		if (ecode != 0)
			return ecode;
	}

	ecode = ath9k_hw_eeprom_init(ah);
	if (ecode != 0)
		return ecode;

	ath_print(ath9k_hw_common(ah), ATH_DBG_CONFIG,
		  "Eeprom VER: %d, REV: %d\n",
		  ah->eep_ops->get_eeprom_ver(ah),
		  ah->eep_ops->get_eeprom_rev(ah));

	ecode = ath9k_hw_rf_alloc_ext_banks(ah);
	if (ecode) {
		ath_print(ath9k_hw_common(ah), ATH_DBG_FATAL,
			  "Failed allocating banks for "
			  "external radio\n");
		return ecode;
	}

	if (!AR_SREV_9100(ah)) {
		ath9k_hw_ani_setup(ah);
		ath9k_hw_ani_init(ah);
	}

	return 0;
}

static void ath9k_hw_attach_ops(struct ath_hw *ah)
{
	if (AR_SREV_9300_20_OR_LATER(ah))
		ar9003_hw_attach_ops(ah);
	else
		ar9002_hw_attach_ops(ah);
}

/* Called for all hardware families */
static int __ath9k_hw_init(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	int r = 0;

	if (ah->hw_version.devid == AR5416_AR9100_DEVID)
		ah->hw_version.macVersion = AR_SREV_VERSION_9100;

	if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_POWER_ON)) {
		ath_print(common, ATH_DBG_FATAL,
			  "Couldn't reset chip\n");
		return -EIO;
	}

	ath9k_hw_init_defaults(ah);
	ath9k_hw_init_config(ah);

	ath9k_hw_attach_ops(ah);

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE)) {
		ath_print(common, ATH_DBG_FATAL, "Couldn't wakeup chip\n");
		return -EIO;
	}

	if (ah->config.serialize_regmode == SER_REG_MODE_AUTO) {
		if (ah->hw_version.macVersion == AR_SREV_VERSION_5416_PCI ||
		    (AR_SREV_9280(ah) && !ah->is_pciexpress)) {
			ah->config.serialize_regmode =
				SER_REG_MODE_ON;
		} else {
			ah->config.serialize_regmode =
				SER_REG_MODE_OFF;
		}
	}

	ath_print(common, ATH_DBG_RESET, "serialize_regmode is %d\n",
		ah->config.serialize_regmode);

	if (AR_SREV_9285(ah) || AR_SREV_9271(ah))
		ah->config.max_txtrig_level = MAX_TX_FIFO_THRESHOLD >> 1;
	else
		ah->config.max_txtrig_level = MAX_TX_FIFO_THRESHOLD;

	if (!ath9k_hw_macversion_supported(ah)) {
		ath_print(common, ATH_DBG_FATAL,
			  "Mac Chip Rev 0x%02x.%x is not supported by "
			  "this driver\n", ah->hw_version.macVersion,
			  ah->hw_version.macRev);
		return -EOPNOTSUPP;
	}

	if (AR_SREV_9271(ah) || AR_SREV_9100(ah))
		ah->is_pciexpress = false;

	ah->hw_version.phyRev = REG_READ(ah, AR_PHY_CHIP_ID);
	ath9k_hw_init_cal_settings(ah);

	ah->ani_function = ATH9K_ANI_ALL;
	if (AR_SREV_9280_10_OR_LATER(ah) && !AR_SREV_9300_20_OR_LATER(ah))
		ah->ani_function &= ~ATH9K_ANI_NOISE_IMMUNITY_LEVEL;

	ath9k_hw_init_mode_regs(ah);

	/*
	 * Configire PCIE after Ini init. SERDES values now come from ini file
	 * This enables PCIe low power mode.
	 */
	if (AR_SREV_9300_20_OR_LATER(ah)) {
		u32 regval;
		unsigned int i;

		/* Set Bits 16 and 17 in the AR_WA register. */
		regval = REG_READ(ah, AR_WA);
		regval |= 0x00030000;
		REG_WRITE(ah, AR_WA, regval);

		for (i = 0; i < ah->iniPcieSerdesLowPower.ia_rows; i++) {
			REG_WRITE(ah,
				  INI_RA(&ah->iniPcieSerdesLowPower, i, 0),
				  INI_RA(&ah->iniPcieSerdesLowPower, i, 1));
		}
	}

	if (ah->is_pciexpress)
		ath9k_hw_configpcipowersave(ah, 0, 0);
	else
		ath9k_hw_disablepcie(ah);

	if (!AR_SREV_9300_20_OR_LATER(ah))
		ar9002_hw_cck_chan14_spread(ah);

	r = ath9k_hw_post_init(ah);
	if (r)
		return r;

	ath9k_hw_init_mode_gain_regs(ah);
	r = ath9k_hw_fill_cap_info(ah);
	if (r)
		return r;

	r = ath9k_hw_init_macaddr(ah);
	if (r) {
		ath_print(common, ATH_DBG_FATAL,
			  "Failed to initialize MAC address\n");
		return r;
	}

	if (AR_SREV_9285(ah) || AR_SREV_9271(ah))
		ah->tx_trig_level = (AR_FTRIG_256B >> AR_FTRIG_S);
	else
		ah->tx_trig_level = (AR_FTRIG_512B >> AR_FTRIG_S);

	if (AR_SREV_9300_20_OR_LATER(ah))
		ar9003_hw_set_nf_limits(ah);

	ath9k_init_nfcal_hist_buffer(ah);

	common->state = ATH_HW_INITIALIZED;

	return 0;
}

int ath9k_hw_init(struct ath_hw *ah)
{
	int ret;
	struct ath_common *common = ath9k_hw_common(ah);

	/* These are all the AR5008/AR9001/AR9002 hardware family of chipsets */
	switch (ah->hw_version.devid) {
	case AR5416_DEVID_PCI:
	case AR5416_DEVID_PCIE:
	case AR5416_AR9100_DEVID:
	case AR9160_DEVID_PCI:
	case AR9280_DEVID_PCI:
	case AR9280_DEVID_PCIE:
	case AR9285_DEVID_PCIE:
	case AR9287_DEVID_PCI:
	case AR9287_DEVID_PCIE:
	case AR2427_DEVID_PCIE:
	case AR9300_DEVID_PCIE:
		break;
	default:
		if (common->bus_ops->ath_bus_type == ATH_USB)
			break;
		ath_print(common, ATH_DBG_FATAL,
			  "Hardware device ID 0x%04x not supported\n",
			  ah->hw_version.devid);
		return -EOPNOTSUPP;
	}

	ret = __ath9k_hw_init(ah);
	if (ret) {
		ath_print(common, ATH_DBG_FATAL,
			  "Unable to initialize hardware; "
			  "initialization status: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ath9k_hw_init);

static void ath9k_hw_init_qos(struct ath_hw *ah)
{
	ENABLE_REGWRITE_BUFFER(ah);

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

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);
}

static void ath9k_hw_init_pll(struct ath_hw *ah,
			      struct ath9k_channel *chan)
{
	u32 pll = ath9k_hw_compute_pll_control(ah, chan);

	REG_WRITE(ah, AR_RTC_PLL_CONTROL, pll);

	/* Switch the core clock for ar9271 to 117Mhz */
	if (AR_SREV_9271(ah)) {
		udelay(500);
		REG_WRITE(ah, 0x50040, 0x304);
	}

	udelay(RTC_PLL_SETTLE_DELAY);

	REG_WRITE(ah, AR_RTC_SLEEP_CLK, AR_RTC_FORCE_DERIVED_CLK);
}

static void ath9k_hw_init_interrupt_masks(struct ath_hw *ah,
					  enum nl80211_iftype opmode)
{
	u32 imr_reg = AR_IMR_TXERR |
		AR_IMR_TXURN |
		AR_IMR_RXERR |
		AR_IMR_RXORN |
		AR_IMR_BCNMISC;

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		imr_reg |= AR_IMR_RXOK_HP;
		if (ah->config.rx_intr_mitigation)
			imr_reg |= AR_IMR_RXINTM | AR_IMR_RXMINTR;
		else
			imr_reg |= AR_IMR_RXOK_LP;

	} else {
		if (ah->config.rx_intr_mitigation)
			imr_reg |= AR_IMR_RXINTM | AR_IMR_RXMINTR;
		else
			imr_reg |= AR_IMR_RXOK;
	}

	if (ah->config.tx_intr_mitigation)
		imr_reg |= AR_IMR_TXINTM | AR_IMR_TXMINTR;
	else
		imr_reg |= AR_IMR_TXOK;

	if (opmode == NL80211_IFTYPE_AP)
		imr_reg |= AR_IMR_MIB;

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_IMR, imr_reg);
	ah->imrs2_reg |= AR_IMR_S2_GTT;
	REG_WRITE(ah, AR_IMR_S2, ah->imrs2_reg);

	if (!AR_SREV_9100(ah)) {
		REG_WRITE(ah, AR_INTR_SYNC_CAUSE, 0xFFFFFFFF);
		REG_WRITE(ah, AR_INTR_SYNC_ENABLE, AR_INTR_SYNC_DEFAULT);
		REG_WRITE(ah, AR_INTR_SYNC_MASK, 0);
	}

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		REG_WRITE(ah, AR_INTR_PRIO_ASYNC_ENABLE, 0);
		REG_WRITE(ah, AR_INTR_PRIO_ASYNC_MASK, 0);
		REG_WRITE(ah, AR_INTR_PRIO_SYNC_ENABLE, 0);
		REG_WRITE(ah, AR_INTR_PRIO_SYNC_MASK, 0);
	}
}

static void ath9k_hw_setslottime(struct ath_hw *ah, u32 us)
{
	u32 val = ath9k_hw_mac_to_clks(ah, us);
	val = min(val, (u32) 0xFFFF);
	REG_WRITE(ah, AR_D_GBL_IFS_SLOT, val);
}

static void ath9k_hw_set_ack_timeout(struct ath_hw *ah, u32 us)
{
	u32 val = ath9k_hw_mac_to_clks(ah, us);
	val = min(val, (u32) MS(0xFFFFFFFF, AR_TIME_OUT_ACK));
	REG_RMW_FIELD(ah, AR_TIME_OUT, AR_TIME_OUT_ACK, val);
}

static void ath9k_hw_set_cts_timeout(struct ath_hw *ah, u32 us)
{
	u32 val = ath9k_hw_mac_to_clks(ah, us);
	val = min(val, (u32) MS(0xFFFFFFFF, AR_TIME_OUT_CTS));
	REG_RMW_FIELD(ah, AR_TIME_OUT, AR_TIME_OUT_CTS, val);
}

static bool ath9k_hw_set_global_txtimeout(struct ath_hw *ah, u32 tu)
{
	if (tu > 0xFFFF) {
		ath_print(ath9k_hw_common(ah), ATH_DBG_XMIT,
			  "bad global tx timeout %u\n", tu);
		ah->globaltxtimeout = (u32) -1;
		return false;
	} else {
		REG_RMW_FIELD(ah, AR_GTXTO, AR_GTXTO_TIMEOUT_LIMIT, tu);
		ah->globaltxtimeout = tu;
		return true;
	}
}

void ath9k_hw_init_global_settings(struct ath_hw *ah)
{
	struct ieee80211_conf *conf = &ath9k_hw_common(ah)->hw->conf;
	int acktimeout;
	int slottime;
	int sifstime;

	ath_print(ath9k_hw_common(ah), ATH_DBG_RESET, "ah->misc_mode 0x%x\n",
		  ah->misc_mode);

	if (ah->misc_mode != 0)
		REG_WRITE(ah, AR_PCU_MISC,
			  REG_READ(ah, AR_PCU_MISC) | ah->misc_mode);

	if (conf->channel && conf->channel->band == IEEE80211_BAND_5GHZ)
		sifstime = 16;
	else
		sifstime = 10;

	/* As defined by IEEE 802.11-2007 17.3.8.6 */
	slottime = ah->slottime + 3 * ah->coverage_class;
	acktimeout = slottime + sifstime;

	/*
	 * Workaround for early ACK timeouts, add an offset to match the
	 * initval's 64us ack timeout value.
	 * This was initially only meant to work around an issue with delayed
	 * BA frames in some implementations, but it has been found to fix ACK
	 * timeout issues in other cases as well.
	 */
	if (conf->channel && conf->channel->band == IEEE80211_BAND_2GHZ)
		acktimeout += 64 - sifstime - ah->slottime;

	ath9k_hw_setslottime(ah, slottime);
	ath9k_hw_set_ack_timeout(ah, acktimeout);
	ath9k_hw_set_cts_timeout(ah, acktimeout);
	if (ah->globaltxtimeout != (u32) -1)
		ath9k_hw_set_global_txtimeout(ah, ah->globaltxtimeout);
}
EXPORT_SYMBOL(ath9k_hw_init_global_settings);

void ath9k_hw_deinit(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);

	if (common->state < ATH_HW_INITIALIZED)
		goto free_hw;

	ath9k_hw_setpower(ah, ATH9K_PM_FULL_SLEEP);

free_hw:
	ath9k_hw_rf_free_ext_banks(ah);
}
EXPORT_SYMBOL(ath9k_hw_deinit);

/*******/
/* INI */
/*******/

u32 ath9k_regd_get_ctl(struct ath_regulatory *reg, struct ath9k_channel *chan)
{
	u32 ctl = ath_regd_get_band_ctl(reg, chan->chan->band);

	if (IS_CHAN_B(chan))
		ctl |= CTL_11B;
	else if (IS_CHAN_G(chan))
		ctl |= CTL_11G;
	else
		ctl |= CTL_11A;

	return ctl;
}

/****************************************/
/* Reset and Channel Switching Routines */
/****************************************/

static inline void ath9k_hw_set_dma(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 regval;

	ENABLE_REGWRITE_BUFFER(ah);

	/*
	 * set AHB_MODE not to do cacheline prefetches
	*/
	if (!AR_SREV_9300_20_OR_LATER(ah)) {
		regval = REG_READ(ah, AR_AHB_MODE);
		REG_WRITE(ah, AR_AHB_MODE, regval | AR_AHB_PREFETCH_RD_EN);
	}

	/*
	 * let mac dma reads be in 128 byte chunks
	 */
	regval = REG_READ(ah, AR_TXCFG) & ~AR_TXCFG_DMASZ_MASK;
	REG_WRITE(ah, AR_TXCFG, regval | AR_TXCFG_DMASZ_128B);

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

	/*
	 * Restore TX Trigger Level to its pre-reset value.
	 * The initial value depends on whether aggregation is enabled, and is
	 * adjusted whenever underruns are detected.
	 */
	if (!AR_SREV_9300_20_OR_LATER(ah))
		REG_RMW_FIELD(ah, AR_TXCFG, AR_FTRIG, ah->tx_trig_level);

	ENABLE_REGWRITE_BUFFER(ah);

	/*
	 * let mac dma writes be in 128 byte chunks
	 */
	regval = REG_READ(ah, AR_RXCFG) & ~AR_RXCFG_DMASZ_MASK;
	REG_WRITE(ah, AR_RXCFG, regval | AR_RXCFG_DMASZ_128B);

	/*
	 * Setup receive FIFO threshold to hold off TX activities
	 */
	REG_WRITE(ah, AR_RXFIFO_CFG, 0x200);

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		REG_RMW_FIELD(ah, AR_RXBP_THRESH, AR_RXBP_THRESH_HP, 0x1);
		REG_RMW_FIELD(ah, AR_RXBP_THRESH, AR_RXBP_THRESH_LP, 0x1);

		ath9k_hw_set_rx_bufsize(ah, common->rx_bufsize -
			ah->caps.rx_status_len);
	}

	/*
	 * reduce the number of usable entries in PCU TXBUF to avoid
	 * wrap around issues.
	 */
	if (AR_SREV_9285(ah)) {
		/* For AR9285 the number of Fifos are reduced to half.
		 * So set the usable tx buf size also to half to
		 * avoid data/delimiter underruns
		 */
		REG_WRITE(ah, AR_PCU_TXBUF_CTRL,
			  AR_9285_PCU_TXBUF_CTRL_USABLE_SIZE);
	} else if (!AR_SREV_9271(ah)) {
		REG_WRITE(ah, AR_PCU_TXBUF_CTRL,
			  AR_PCU_TXBUF_CTRL_USABLE_SIZE);
	}

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

	if (AR_SREV_9300_20_OR_LATER(ah))
		ath9k_hw_reset_txstatus_ring(ah);
}

static void ath9k_hw_set_operating_mode(struct ath_hw *ah, int opmode)
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
	case NL80211_IFTYPE_MESH_POINT:
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

void ath9k_hw_get_delta_slope_vals(struct ath_hw *ah, u32 coef_scaled,
				   u32 *coef_mantissa, u32 *coef_exponent)
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

static bool ath9k_hw_set_reset(struct ath_hw *ah, int type)
{
	u32 rst_flags;
	u32 tmpReg;

	if (AR_SREV_9100(ah)) {
		u32 val = REG_READ(ah, AR_RTC_DERIVED_CLK);
		val &= ~AR_RTC_DERIVED_CLK_PERIOD;
		val |= SM(1, AR_RTC_DERIVED_CLK_PERIOD);
		REG_WRITE(ah, AR_RTC_DERIVED_CLK, val);
		(void)REG_READ(ah, AR_RTC_DERIVED_CLK);
	}

	ENABLE_REGWRITE_BUFFER(ah);

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
			u32 val;
			REG_WRITE(ah, AR_INTR_SYNC_ENABLE, 0);

			val = AR_RC_HOSTIF;
			if (!AR_SREV_9300_20_OR_LATER(ah))
				val |= AR_RC_AHB;
			REG_WRITE(ah, AR_RC, val);

		} else if (!AR_SREV_9300_20_OR_LATER(ah))
			REG_WRITE(ah, AR_RC, AR_RC_AHB);

		rst_flags = AR_RTC_RC_MAC_WARM;
		if (type == ATH9K_RESET_COLD)
			rst_flags |= AR_RTC_RC_MAC_COLD;
	}

	REG_WRITE(ah, AR_RTC_RC, rst_flags);

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

	udelay(50);

	REG_WRITE(ah, AR_RTC_RC, 0);
	if (!ath9k_hw_wait(ah, AR_RTC_RC, AR_RTC_RC_M, 0, AH_WAIT_TIMEOUT)) {
		ath_print(ath9k_hw_common(ah), ATH_DBG_RESET,
			  "RTC stuck in MAC reset\n");
		return false;
	}

	if (!AR_SREV_9100(ah))
		REG_WRITE(ah, AR_RC, 0);

	if (AR_SREV_9100(ah))
		udelay(50);

	return true;
}

static bool ath9k_hw_set_reset_power_on(struct ath_hw *ah)
{
	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN |
		  AR_RTC_FORCE_WAKE_ON_INT);

	if (!AR_SREV_9100(ah) && !AR_SREV_9300_20_OR_LATER(ah))
		REG_WRITE(ah, AR_RC, AR_RC_AHB);

	REG_WRITE(ah, AR_RTC_RESET, 0);

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

	if (!AR_SREV_9300_20_OR_LATER(ah))
		udelay(2);

	if (!AR_SREV_9100(ah) && !AR_SREV_9300_20_OR_LATER(ah))
		REG_WRITE(ah, AR_RC, 0);

	REG_WRITE(ah, AR_RTC_RESET, 1);

	if (!ath9k_hw_wait(ah,
			   AR_RTC_STATUS,
			   AR_RTC_STATUS_M,
			   AR_RTC_STATUS_ON,
			   AH_WAIT_TIMEOUT)) {
		ath_print(ath9k_hw_common(ah), ATH_DBG_RESET,
			  "RTC not waking up\n");
		return false;
	}

	ath9k_hw_read_revisions(ah);

	return ath9k_hw_set_reset(ah, ATH9K_RESET_WARM);
}

static bool ath9k_hw_set_reset_reg(struct ath_hw *ah, u32 type)
{
	REG_WRITE(ah, AR_RTC_FORCE_WAKE,
		  AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);

	switch (type) {
	case ATH9K_RESET_POWER_ON:
		return ath9k_hw_set_reset_power_on(ah);
	case ATH9K_RESET_WARM:
	case ATH9K_RESET_COLD:
		return ath9k_hw_set_reset(ah, type);
	default:
		return false;
	}
}

static bool ath9k_hw_chip_reset(struct ath_hw *ah,
				struct ath9k_channel *chan)
{
	if (AR_SREV_9280(ah) && ah->eep_ops->get_eeprom(ah, EEP_OL_PWRCTRL)) {
		if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_POWER_ON))
			return false;
	} else if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_WARM))
		return false;

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE))
		return false;

	ah->chip_fullsleep = false;
	ath9k_hw_init_pll(ah, chan);
	ath9k_hw_set_rfmode(ah, chan);

	return true;
}

static bool ath9k_hw_channel_change(struct ath_hw *ah,
				    struct ath9k_channel *chan)
{
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_channel *channel = chan->chan;
	u32 qnum;
	int r;

	for (qnum = 0; qnum < AR_NUM_QCU; qnum++) {
		if (ath9k_hw_numtxpending(ah, qnum)) {
			ath_print(common, ATH_DBG_QUEUE,
				  "Transmit frames pending on "
				  "queue %d\n", qnum);
			return false;
		}
	}

	if (!ath9k_hw_rfbus_req(ah)) {
		ath_print(common, ATH_DBG_FATAL,
			  "Could not kill baseband RX\n");
		return false;
	}

	ath9k_hw_set_channel_regs(ah, chan);

	r = ath9k_hw_rf_set_freq(ah, chan);
	if (r) {
		ath_print(common, ATH_DBG_FATAL,
			  "Failed to set channel\n");
		return false;
	}

	ah->eep_ops->set_txpower(ah, chan,
			     ath9k_regd_get_ctl(regulatory, chan),
			     channel->max_antenna_gain * 2,
			     channel->max_power * 2,
			     min((u32) MAX_RATE_POWER,
			     (u32) regulatory->power_limit));

	ath9k_hw_rfbus_done(ah);

	if (IS_CHAN_OFDM(chan) || IS_CHAN_HT(chan))
		ath9k_hw_set_delta_slope(ah, chan);

	ath9k_hw_spur_mitigate_freq(ah, chan);

	if (!chan->oneTimeCalsDone)
		chan->oneTimeCalsDone = true;

	return true;
}

bool ath9k_hw_check_alive(struct ath_hw *ah)
{
	int count = 50;
	u32 reg;

	if (AR_SREV_9285_10_OR_LATER(ah))
		return true;

	do {
		reg = REG_READ(ah, AR_OBS_BUS_1);

		if ((reg & 0x7E7FFFEF) == 0x00702400)
			continue;

		switch (reg & 0x7E000B00) {
		case 0x1E000000:
		case 0x52000B00:
		case 0x18000B00:
			continue;
		default:
			return true;
		}
	} while (count-- > 0);

	return false;
}
EXPORT_SYMBOL(ath9k_hw_check_alive);

int ath9k_hw_reset(struct ath_hw *ah, struct ath9k_channel *chan,
		    bool bChannelChange)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 saveLedState;
	struct ath9k_channel *curchan = ah->curchan;
	u32 saveDefAntenna;
	u32 macStaId1;
	u64 tsf = 0;
	int i, r;

	ah->txchainmask = common->tx_chainmask;
	ah->rxchainmask = common->rx_chainmask;

	if (!ah->chip_fullsleep) {
		ath9k_hw_abortpcurecv(ah);
		if (!ath9k_hw_stopdmarecv(ah))
			ath_print(common, ATH_DBG_XMIT,
				"Failed to stop receive dma\n");
	}

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE))
		return -EIO;

	if (curchan && !ah->chip_fullsleep)
		ath9k_hw_getnf(ah, curchan);

	if (bChannelChange &&
	    (ah->chip_fullsleep != true) &&
	    (ah->curchan != NULL) &&
	    (chan->channel != ah->curchan->channel) &&
	    ((chan->channelFlags & CHANNEL_ALL) ==
	     (ah->curchan->channelFlags & CHANNEL_ALL)) &&
	    !AR_SREV_9280(ah)) {

		if (ath9k_hw_channel_change(ah, chan)) {
			ath9k_hw_loadnf(ah, ah->curchan);
			ath9k_hw_start_nfcal(ah);
			return 0;
		}
	}

	saveDefAntenna = REG_READ(ah, AR_DEF_ANTENNA);
	if (saveDefAntenna == 0)
		saveDefAntenna = 1;

	macStaId1 = REG_READ(ah, AR_STA_ID1) & AR_STA_ID1_BASE_RATE_11B;

	/* For chips on which RTC reset is done, save TSF before it gets cleared */
	if (AR_SREV_9280(ah) && ah->eep_ops->get_eeprom(ah, EEP_OL_PWRCTRL))
		tsf = ath9k_hw_gettsf64(ah);

	saveLedState = REG_READ(ah, AR_CFG_LED) &
		(AR_CFG_LED_ASSOC_CTL | AR_CFG_LED_MODE_SEL |
		 AR_CFG_LED_BLINK_THRESH_SEL | AR_CFG_LED_BLINK_SLOW);

	ath9k_hw_mark_phy_inactive(ah);

	/* Only required on the first reset */
	if (AR_SREV_9271(ah) && ah->htc_reset_init) {
		REG_WRITE(ah,
			  AR9271_RESET_POWER_DOWN_CONTROL,
			  AR9271_RADIO_RF_RST);
		udelay(50);
	}

	if (!ath9k_hw_chip_reset(ah, chan)) {
		ath_print(common, ATH_DBG_FATAL, "Chip reset failed\n");
		return -EINVAL;
	}

	/* Only required on the first reset */
	if (AR_SREV_9271(ah) && ah->htc_reset_init) {
		ah->htc_reset_init = false;
		REG_WRITE(ah,
			  AR9271_RESET_POWER_DOWN_CONTROL,
			  AR9271_GATE_MAC_CTL);
		udelay(50);
	}

	/* Restore TSF */
	if (tsf && AR_SREV_9280(ah) && ah->eep_ops->get_eeprom(ah, EEP_OL_PWRCTRL))
		ath9k_hw_settsf64(ah, tsf);

	if (AR_SREV_9280_10_OR_LATER(ah))
		REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL, AR_GPIO_JTAG_DISABLE);

	r = ath9k_hw_process_ini(ah, chan);
	if (r)
		return r;

	/* Setup MFP options for CCMP */
	if (AR_SREV_9280_20_OR_LATER(ah)) {
		/* Mask Retry(b11), PwrMgt(b12), MoreData(b13) to 0 in mgmt
		 * frames when constructing CCMP AAD. */
		REG_RMW_FIELD(ah, AR_AES_MUTE_MASK1, AR_AES_MUTE_MASK1_FC_MGMT,
			      0xc7ff);
		ah->sw_mgmt_crypto = false;
	} else if (AR_SREV_9160_10_OR_LATER(ah)) {
		/* Disable hardware crypto for management frames */
		REG_CLR_BIT(ah, AR_PCU_MISC_MODE2,
			    AR_PCU_MISC_MODE2_MGMT_CRYPTO_ENABLE);
		REG_SET_BIT(ah, AR_PCU_MISC_MODE2,
			    AR_PCU_MISC_MODE2_NO_CRYPTO_FOR_NON_DATA_PKT);
		ah->sw_mgmt_crypto = true;
	} else
		ah->sw_mgmt_crypto = true;

	if (IS_CHAN_OFDM(chan) || IS_CHAN_HT(chan))
		ath9k_hw_set_delta_slope(ah, chan);

	ath9k_hw_spur_mitigate_freq(ah, chan);
	ah->eep_ops->set_board_values(ah, chan);

	ath9k_hw_set_operating_mode(ah, ah->opmode);

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_STA_ID0, get_unaligned_le32(common->macaddr));
	REG_WRITE(ah, AR_STA_ID1, get_unaligned_le16(common->macaddr + 4)
		  | macStaId1
		  | AR_STA_ID1_RTS_USE_DEF
		  | (ah->config.
		     ack_6mb ? AR_STA_ID1_ACKCTS_6MB : 0)
		  | ah->sta_id1_defaults);
	ath_hw_setbssidmask(common);
	REG_WRITE(ah, AR_DEF_ANTENNA, saveDefAntenna);
	ath9k_hw_write_associd(ah);
	REG_WRITE(ah, AR_ISR, ~0);
	REG_WRITE(ah, AR_RSSI_THR, INIT_RSSI_THR);

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

	r = ath9k_hw_rf_set_freq(ah, chan);
	if (r)
		return r;

	ENABLE_REGWRITE_BUFFER(ah);

	for (i = 0; i < AR_NUM_DCU; i++)
		REG_WRITE(ah, AR_DQCUMASK(i), 1 << i);

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

	ah->intr_txqs = 0;
	for (i = 0; i < ah->caps.total_queues; i++)
		ath9k_hw_resettxqueue(ah, i);

	ath9k_hw_init_interrupt_masks(ah, ah->opmode);
	ath9k_hw_init_qos(ah);

	if (ah->caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		ath9k_enable_rfkill(ah);

	ath9k_hw_init_global_settings(ah);

	if (!AR_SREV_9300_20_OR_LATER(ah)) {
		ar9002_hw_enable_async_fifo(ah);
		ar9002_hw_enable_wep_aggregation(ah);
	}

	REG_WRITE(ah, AR_STA_ID1,
		  REG_READ(ah, AR_STA_ID1) | AR_STA_ID1_PRESERVE_SEQNUM);

	ath9k_hw_set_dma(ah);

	REG_WRITE(ah, AR_OBS, 8);

	if (ah->config.rx_intr_mitigation) {
		REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_LAST, 500);
		REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_FIRST, 2000);
	}

	if (ah->config.tx_intr_mitigation) {
		REG_RMW_FIELD(ah, AR_TIMT, AR_TIMT_LAST, 300);
		REG_RMW_FIELD(ah, AR_TIMT, AR_TIMT_FIRST, 750);
	}

	ath9k_hw_init_bb(ah, chan);

	if (!ath9k_hw_init_cal(ah, chan))
		return -EIO;

	ENABLE_REGWRITE_BUFFER(ah);

	ath9k_hw_restore_chainmask(ah);
	REG_WRITE(ah, AR_CFG_LED, saveLedState | AR_CFG_SCLK_32KHZ);

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

	/*
	 * For big endian systems turn on swapping for descriptors
	 */
	if (AR_SREV_9100(ah)) {
		u32 mask;
		mask = REG_READ(ah, AR_CFG);
		if (mask & (AR_CFG_SWRB | AR_CFG_SWTB | AR_CFG_SWRG)) {
			ath_print(common, ATH_DBG_RESET,
				"CFG Byte Swap Set 0x%x\n", mask);
		} else {
			mask =
				INIT_CONFIG_STATUS | AR_CFG_SWRB | AR_CFG_SWTB;
			REG_WRITE(ah, AR_CFG, mask);
			ath_print(common, ATH_DBG_RESET,
				"Setting CFG 0x%x\n", REG_READ(ah, AR_CFG));
		}
	} else {
		/* Configure AR9271 target WLAN */
                if (AR_SREV_9271(ah))
			REG_WRITE(ah, AR_CFG, AR_CFG_SWRB | AR_CFG_SWTB);
#ifdef __BIG_ENDIAN
                else
			REG_WRITE(ah, AR_CFG, AR_CFG_SWTD | AR_CFG_SWRD);
#endif
	}

	if (ah->btcoex_hw.enabled)
		ath9k_hw_btcoex_enable(ah);

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		ath9k_hw_loadnf(ah, curchan);
		ath9k_hw_start_nfcal(ah);
	}

	return 0;
}
EXPORT_SYMBOL(ath9k_hw_reset);

/************************/
/* Key Cache Management */
/************************/

bool ath9k_hw_keyreset(struct ath_hw *ah, u16 entry)
{
	u32 keyType;

	if (entry >= ah->caps.keycache_size) {
		ath_print(ath9k_hw_common(ah), ATH_DBG_FATAL,
			  "keychache entry %u out of range\n", entry);
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

	return true;
}
EXPORT_SYMBOL(ath9k_hw_keyreset);

bool ath9k_hw_keysetmac(struct ath_hw *ah, u16 entry, const u8 *mac)
{
	u32 macHi, macLo;

	if (entry >= ah->caps.keycache_size) {
		ath_print(ath9k_hw_common(ah), ATH_DBG_FATAL,
			  "keychache entry %u out of range\n", entry);
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
EXPORT_SYMBOL(ath9k_hw_keysetmac);

bool ath9k_hw_set_keycache_entry(struct ath_hw *ah, u16 entry,
				 const struct ath9k_keyval *k,
				 const u8 *mac)
{
	const struct ath9k_hw_capabilities *pCap = &ah->caps;
	struct ath_common *common = ath9k_hw_common(ah);
	u32 key0, key1, key2, key3, key4;
	u32 keyType;

	if (entry >= pCap->keycache_size) {
		ath_print(common, ATH_DBG_FATAL,
			  "keycache entry %u out of range\n", entry);
		return false;
	}

	switch (k->kv_type) {
	case ATH9K_CIPHER_AES_OCB:
		keyType = AR_KEYTABLE_TYPE_AES;
		break;
	case ATH9K_CIPHER_AES_CCM:
		if (!(pCap->hw_caps & ATH9K_HW_CAP_CIPHER_AESCCM)) {
			ath_print(common, ATH_DBG_ANY,
				  "AES-CCM not supported by mac rev 0x%x\n",
				  ah->hw_version.macRev);
			return false;
		}
		keyType = AR_KEYTABLE_TYPE_CCM;
		break;
	case ATH9K_CIPHER_TKIP:
		keyType = AR_KEYTABLE_TYPE_TKIP;
		if (ATH9K_IS_MIC_ENABLED(ah)
		    && entry + 64 >= pCap->keycache_size) {
			ath_print(common, ATH_DBG_ANY,
				  "entry %u inappropriate for TKIP\n", entry);
			return false;
		}
		break;
	case ATH9K_CIPHER_WEP:
		if (k->kv_len < WLAN_KEY_LEN_WEP40) {
			ath_print(common, ATH_DBG_ANY,
				  "WEP key length %u too small\n", k->kv_len);
			return false;
		}
		if (k->kv_len <= WLAN_KEY_LEN_WEP40)
			keyType = AR_KEYTABLE_TYPE_40;
		else if (k->kv_len <= WLAN_KEY_LEN_WEP104)
			keyType = AR_KEYTABLE_TYPE_104;
		else
			keyType = AR_KEYTABLE_TYPE_128;
		break;
	case ATH9K_CIPHER_CLR:
		keyType = AR_KEYTABLE_TYPE_CLR;
		break;
	default:
		ath_print(common, ATH_DBG_FATAL,
			  "cipher %u not supported\n", k->kv_type);
		return false;
	}

	key0 = get_unaligned_le32(k->kv_val + 0);
	key1 = get_unaligned_le16(k->kv_val + 4);
	key2 = get_unaligned_le32(k->kv_val + 6);
	key3 = get_unaligned_le16(k->kv_val + 10);
	key4 = get_unaligned_le32(k->kv_val + 12);
	if (k->kv_len <= WLAN_KEY_LEN_WEP104)
		key4 &= 0xff;

	/*
	 * Note: Key cache registers access special memory area that requires
	 * two 32-bit writes to actually update the values in the internal
	 * memory. Consequently, the exact order and pairs used here must be
	 * maintained.
	 */

	if (keyType == AR_KEYTABLE_TYPE_TKIP && ATH9K_IS_MIC_ENABLED(ah)) {
		u16 micentry = entry + 64;

		/*
		 * Write inverted key[47:0] first to avoid Michael MIC errors
		 * on frames that could be sent or received at the same time.
		 * The correct key will be written in the end once everything
		 * else is ready.
		 */
		REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), ~key0);
		REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), ~key1);

		/* Write key[95:48] */
		REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), key2);
		REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), key3);

		/* Write key[127:96] and key type */
		REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), key4);
		REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), keyType);

		/* Write MAC address for the entry */
		(void) ath9k_hw_keysetmac(ah, entry, mac);

		if (ah->misc_mode & AR_PCU_MIC_NEW_LOC_ENA) {
			/*
			 * TKIP uses two key cache entries:
			 * Michael MIC TX/RX keys in the same key cache entry
			 * (idx = main index + 64):
			 * key0 [31:0] = RX key [31:0]
			 * key1 [15:0] = TX key [31:16]
			 * key1 [31:16] = reserved
			 * key2 [31:0] = RX key [63:32]
			 * key3 [15:0] = TX key [15:0]
			 * key3 [31:16] = reserved
			 * key4 [31:0] = TX key [63:32]
			 */
			u32 mic0, mic1, mic2, mic3, mic4;

			mic0 = get_unaligned_le32(k->kv_mic + 0);
			mic2 = get_unaligned_le32(k->kv_mic + 4);
			mic1 = get_unaligned_le16(k->kv_txmic + 2) & 0xffff;
			mic3 = get_unaligned_le16(k->kv_txmic + 0) & 0xffff;
			mic4 = get_unaligned_le32(k->kv_txmic + 4);

			/* Write RX[31:0] and TX[31:16] */
			REG_WRITE(ah, AR_KEYTABLE_KEY0(micentry), mic0);
			REG_WRITE(ah, AR_KEYTABLE_KEY1(micentry), mic1);

			/* Write RX[63:32] and TX[15:0] */
			REG_WRITE(ah, AR_KEYTABLE_KEY2(micentry), mic2);
			REG_WRITE(ah, AR_KEYTABLE_KEY3(micentry), mic3);

			/* Write TX[63:32] and keyType(reserved) */
			REG_WRITE(ah, AR_KEYTABLE_KEY4(micentry), mic4);
			REG_WRITE(ah, AR_KEYTABLE_TYPE(micentry),
				  AR_KEYTABLE_TYPE_CLR);

		} else {
			/*
			 * TKIP uses four key cache entries (two for group
			 * keys):
			 * Michael MIC TX/RX keys are in different key cache
			 * entries (idx = main index + 64 for TX and
			 * main index + 32 + 96 for RX):
			 * key0 [31:0] = TX/RX MIC key [31:0]
			 * key1 [31:0] = reserved
			 * key2 [31:0] = TX/RX MIC key [63:32]
			 * key3 [31:0] = reserved
			 * key4 [31:0] = reserved
			 *
			 * Upper layer code will call this function separately
			 * for TX and RX keys when these registers offsets are
			 * used.
			 */
			u32 mic0, mic2;

			mic0 = get_unaligned_le32(k->kv_mic + 0);
			mic2 = get_unaligned_le32(k->kv_mic + 4);

			/* Write MIC key[31:0] */
			REG_WRITE(ah, AR_KEYTABLE_KEY0(micentry), mic0);
			REG_WRITE(ah, AR_KEYTABLE_KEY1(micentry), 0);

			/* Write MIC key[63:32] */
			REG_WRITE(ah, AR_KEYTABLE_KEY2(micentry), mic2);
			REG_WRITE(ah, AR_KEYTABLE_KEY3(micentry), 0);

			/* Write TX[63:32] and keyType(reserved) */
			REG_WRITE(ah, AR_KEYTABLE_KEY4(micentry), 0);
			REG_WRITE(ah, AR_KEYTABLE_TYPE(micentry),
				  AR_KEYTABLE_TYPE_CLR);
		}

		/* MAC address registers are reserved for the MIC entry */
		REG_WRITE(ah, AR_KEYTABLE_MAC0(micentry), 0);
		REG_WRITE(ah, AR_KEYTABLE_MAC1(micentry), 0);

		/*
		 * Write the correct (un-inverted) key[47:0] last to enable
		 * TKIP now that all other registers are set with correct
		 * values.
		 */
		REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), key0);
		REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), key1);
	} else {
		/* Write key[47:0] */
		REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), key0);
		REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), key1);

		/* Write key[95:48] */
		REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), key2);
		REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), key3);

		/* Write key[127:96] and key type */
		REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), key4);
		REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), keyType);

		/* Write MAC address for the entry */
		(void) ath9k_hw_keysetmac(ah, entry, mac);
	}

	return true;
}
EXPORT_SYMBOL(ath9k_hw_set_keycache_entry);

bool ath9k_hw_keyisvalid(struct ath_hw *ah, u16 entry)
{
	if (entry < ah->caps.keycache_size) {
		u32 val = REG_READ(ah, AR_KEYTABLE_MAC1(entry));
		if (val & AR_KEYTABLE_VALID)
			return true;
	}
	return false;
}
EXPORT_SYMBOL(ath9k_hw_keyisvalid);

/******************************/
/* Power Management (Chipset) */
/******************************/

/*
 * Notify Power Mgt is disabled in self-generated frames.
 * If requested, force chip to sleep.
 */
static void ath9k_set_power_sleep(struct ath_hw *ah, int setChip)
{
	REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	if (setChip) {
		/*
		 * Clear the RTC force wake bit to allow the
		 * mac to go to sleep.
		 */
		REG_CLR_BIT(ah, AR_RTC_FORCE_WAKE,
			    AR_RTC_FORCE_WAKE_EN);
		if (!AR_SREV_9100(ah) && !AR_SREV_9300_20_OR_LATER(ah))
			REG_WRITE(ah, AR_RC, AR_RC_AHB | AR_RC_HOSTIF);

		/* Shutdown chip. Active low */
		if (!AR_SREV_5416(ah) && !AR_SREV_9271(ah))
			REG_CLR_BIT(ah, (AR_RTC_RESET),
				    AR_RTC_RESET_EN);
	}
}

/*
 * Notify Power Management is enabled in self-generating
 * frames. If request, set power mode of chip to
 * auto/normal.  Duration in units of 128us (1/8 TU).
 */
static void ath9k_set_power_network_sleep(struct ath_hw *ah, int setChip)
{
	REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	if (setChip) {
		struct ath9k_hw_capabilities *pCap = &ah->caps;

		if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP)) {
			/* Set WakeOnInterrupt bit; clear ForceWake bit */
			REG_WRITE(ah, AR_RTC_FORCE_WAKE,
				  AR_RTC_FORCE_WAKE_ON_INT);
		} else {
			/*
			 * Clear the RTC force wake bit to allow the
			 * mac to go to sleep.
			 */
			REG_CLR_BIT(ah, AR_RTC_FORCE_WAKE,
				    AR_RTC_FORCE_WAKE_EN);
		}
	}
}

static bool ath9k_hw_set_power_awake(struct ath_hw *ah, int setChip)
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
			if (!AR_SREV_9300_20_OR_LATER(ah))
				ath9k_hw_init_pll(ah, NULL);
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
			ath_print(ath9k_hw_common(ah), ATH_DBG_FATAL,
				  "Failed to wakeup in %uus\n",
				  POWER_UP_TIME / 20);
			return false;
		}
	}

	REG_CLR_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);

	return true;
}

bool ath9k_hw_setpower(struct ath_hw *ah, enum ath9k_power_mode mode)
{
	struct ath_common *common = ath9k_hw_common(ah);
	int status = true, setChip = true;
	static const char *modes[] = {
		"AWAKE",
		"FULL-SLEEP",
		"NETWORK SLEEP",
		"UNDEFINED"
	};

	if (ah->power_mode == mode)
		return status;

	ath_print(common, ATH_DBG_RESET, "%s -> %s\n",
		  modes[ah->power_mode], modes[mode]);

	switch (mode) {
	case ATH9K_PM_AWAKE:
		status = ath9k_hw_set_power_awake(ah, setChip);
		break;
	case ATH9K_PM_FULL_SLEEP:
		ath9k_set_power_sleep(ah, setChip);
		ah->chip_fullsleep = true;
		break;
	case ATH9K_PM_NETWORK_SLEEP:
		ath9k_set_power_network_sleep(ah, setChip);
		break;
	default:
		ath_print(common, ATH_DBG_FATAL,
			  "Unknown power mode %u\n", mode);
		return false;
	}
	ah->power_mode = mode;

	return status;
}
EXPORT_SYMBOL(ath9k_hw_setpower);

/*******************/
/* Beacon Handling */
/*******************/

void ath9k_hw_beaconinit(struct ath_hw *ah, u32 next_beacon, u32 beacon_period)
{
	int flags = 0;

	ah->beacon_interval = beacon_period;

	ENABLE_REGWRITE_BUFFER(ah);

	switch (ah->opmode) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_MONITOR:
		REG_WRITE(ah, AR_NEXT_TBTT_TIMER, TU_TO_USEC(next_beacon));
		REG_WRITE(ah, AR_NEXT_DMA_BEACON_ALERT, 0xffff);
		REG_WRITE(ah, AR_NEXT_SWBA, 0x7ffff);
		flags |= AR_TBTT_TIMER_EN;
		break;
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		REG_SET_BIT(ah, AR_TXCFG,
			    AR_TXCFG_ADHOC_BEACON_ATIM_TX_POLICY);
		REG_WRITE(ah, AR_NEXT_NDP_TIMER,
			  TU_TO_USEC(next_beacon +
				     (ah->atim_window ? ah->
				      atim_window : 1)));
		flags |= AR_NDP_TIMER_EN;
	case NL80211_IFTYPE_AP:
		REG_WRITE(ah, AR_NEXT_TBTT_TIMER, TU_TO_USEC(next_beacon));
		REG_WRITE(ah, AR_NEXT_DMA_BEACON_ALERT,
			  TU_TO_USEC(next_beacon -
				     ah->config.
				     dma_beacon_response_time));
		REG_WRITE(ah, AR_NEXT_SWBA,
			  TU_TO_USEC(next_beacon -
				     ah->config.
				     sw_beacon_response_time));
		flags |=
			AR_TBTT_TIMER_EN | AR_DBA_TIMER_EN | AR_SWBA_TIMER_EN;
		break;
	default:
		ath_print(ath9k_hw_common(ah), ATH_DBG_BEACON,
			  "%s: unsupported opmode: %d\n",
			  __func__, ah->opmode);
		return;
		break;
	}

	REG_WRITE(ah, AR_BEACON_PERIOD, TU_TO_USEC(beacon_period));
	REG_WRITE(ah, AR_DMA_BEACON_PERIOD, TU_TO_USEC(beacon_period));
	REG_WRITE(ah, AR_SWBA_PERIOD, TU_TO_USEC(beacon_period));
	REG_WRITE(ah, AR_NDP_PERIOD, TU_TO_USEC(beacon_period));

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

	beacon_period &= ~ATH9K_BEACON_ENA;
	if (beacon_period & ATH9K_BEACON_RESET_TSF) {
		ath9k_hw_reset_tsf(ah);
	}

	REG_SET_BIT(ah, AR_TIMER_MODE, flags);
}
EXPORT_SYMBOL(ath9k_hw_beaconinit);

void ath9k_hw_set_sta_beacon_timers(struct ath_hw *ah,
				    const struct ath9k_beacon_state *bs)
{
	u32 nextTbtt, beaconintval, dtimperiod, beacontimeout;
	struct ath9k_hw_capabilities *pCap = &ah->caps;
	struct ath_common *common = ath9k_hw_common(ah);

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_NEXT_TBTT_TIMER, TU_TO_USEC(bs->bs_nexttbtt));

	REG_WRITE(ah, AR_BEACON_PERIOD,
		  TU_TO_USEC(bs->bs_intval & ATH9K_BEACON_PERIOD));
	REG_WRITE(ah, AR_DMA_BEACON_PERIOD,
		  TU_TO_USEC(bs->bs_intval & ATH9K_BEACON_PERIOD));

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

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

	ath_print(common, ATH_DBG_BEACON, "next DTIM %d\n", bs->bs_nextdtim);
	ath_print(common, ATH_DBG_BEACON, "next beacon %d\n", nextTbtt);
	ath_print(common, ATH_DBG_BEACON, "beacon period %d\n", beaconintval);
	ath_print(common, ATH_DBG_BEACON, "DTIM period %d\n", dtimperiod);

	ENABLE_REGWRITE_BUFFER(ah);

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

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);

	REG_SET_BIT(ah, AR_TIMER_MODE,
		    AR_TBTT_TIMER_EN | AR_TIM_TIMER_EN |
		    AR_DTIM_TIMER_EN);

	/* TSF Out of Range Threshold */
	REG_WRITE(ah, AR_TSFOOR_THRESHOLD, bs->bs_tsfoor_threshold);
}
EXPORT_SYMBOL(ath9k_hw_set_sta_beacon_timers);

/*******************/
/* HW Capabilities */
/*******************/

int ath9k_hw_fill_cap_info(struct ath_hw *ah)
{
	struct ath9k_hw_capabilities *pCap = &ah->caps;
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_btcoex_hw *btcoex_hw = &ah->btcoex_hw;

	u16 capField = 0, eeval;

	eeval = ah->eep_ops->get_eeprom(ah, EEP_REG_0);
	regulatory->current_rd = eeval;

	eeval = ah->eep_ops->get_eeprom(ah, EEP_REG_1);
	if (AR_SREV_9285_10_OR_LATER(ah))
		eeval |= AR9285_RDEXT_DEFAULT;
	regulatory->current_rd_ext = eeval;

	capField = ah->eep_ops->get_eeprom(ah, EEP_OP_CAP);

	if (ah->opmode != NL80211_IFTYPE_AP &&
	    ah->hw_version.subvendorid == AR_SUBVENDOR_ID_NEW_A) {
		if (regulatory->current_rd == 0x64 ||
		    regulatory->current_rd == 0x65)
			regulatory->current_rd += 5;
		else if (regulatory->current_rd == 0x41)
			regulatory->current_rd = 0x43;
		ath_print(common, ATH_DBG_REGULATORY,
			  "regdomain mapped to 0x%x\n", regulatory->current_rd);
	}

	eeval = ah->eep_ops->get_eeprom(ah, EEP_OP_MODE);
	if ((eeval & (AR5416_OPFLAGS_11G | AR5416_OPFLAGS_11A)) == 0) {
		ath_print(common, ATH_DBG_FATAL,
			  "no band has been marked as supported in EEPROM.\n");
		return -EINVAL;
	}

	bitmap_zero(pCap->wireless_modes, ATH9K_MODE_MAX);

	if (eeval & AR5416_OPFLAGS_11A) {
		set_bit(ATH9K_MODE_11A, pCap->wireless_modes);
		if (ah->config.ht_enable) {
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
		set_bit(ATH9K_MODE_11G, pCap->wireless_modes);
		if (ah->config.ht_enable) {
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

	pCap->tx_chainmask = ah->eep_ops->get_eeprom(ah, EEP_TX_MASK);
	/*
	 * For AR9271 we will temporarilly uses the rx chainmax as read from
	 * the EEPROM.
	 */
	if ((ah->hw_version.devid == AR5416_DEVID_PCI) &&
	    !(eeval & AR5416_OPFLAGS_11A) &&
	    !(AR_SREV_9271(ah)))
		/* CB71: GPIO 0 is pulled down to indicate 3 rx chains */
		pCap->rx_chainmask = ath9k_hw_gpio_get(ah, 0) ? 0x5 : 0x7;
	else
		/* Use rx_chainmask from EEPROM. */
		pCap->rx_chainmask = ah->eep_ops->get_eeprom(ah, EEP_RX_MASK);

	if (!(AR_SREV_9280(ah) && (ah->hw_version.macRev == 0)))
		ah->misc_mode |= AR_PCU_MIC_NEW_LOC_ENA;

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

	if (ah->config.ht_enable)
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

	if (AR_SREV_9285(ah) || AR_SREV_9271(ah))
		pCap->tx_triglevel_max = MAX_TX_FIFO_THRESHOLD >> 1;
	else
		pCap->tx_triglevel_max = MAX_TX_FIFO_THRESHOLD;

	if (AR_SREV_9271(ah))
		pCap->num_gpio_pins = AR9271_NUM_GPIO;
	else if (AR_SREV_9285_10_OR_LATER(ah))
		pCap->num_gpio_pins = AR9285_NUM_GPIO;
	else if (AR_SREV_9280_10_OR_LATER(ah))
		pCap->num_gpio_pins = AR928X_NUM_GPIO;
	else
		pCap->num_gpio_pins = AR_NUM_GPIO;

	if (AR_SREV_9160_10_OR_LATER(ah) || AR_SREV_9100(ah)) {
		pCap->hw_caps |= ATH9K_HW_CAP_CST;
		pCap->rts_aggr_limit = ATH_AMPDU_LIMIT_MAX;
	} else {
		pCap->rts_aggr_limit = (8 * 1024);
	}

	pCap->hw_caps |= ATH9K_HW_CAP_ENHANCEDPM;

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
	ah->rfsilent = ah->eep_ops->get_eeprom(ah, EEP_RF_SILENT);
	if (ah->rfsilent & EEP_RFSILENT_ENABLED) {
		ah->rfkill_gpio =
			MS(ah->rfsilent, EEP_RFSILENT_GPIO_SEL);
		ah->rfkill_polarity =
			MS(ah->rfsilent, EEP_RFSILENT_POLARITY);

		pCap->hw_caps |= ATH9K_HW_CAP_RFSILENT;
	}
#endif
	if (AR_SREV_9271(ah))
		pCap->hw_caps |= ATH9K_HW_CAP_AUTOSLEEP;
	else
		pCap->hw_caps &= ~ATH9K_HW_CAP_AUTOSLEEP;

	if (AR_SREV_9280(ah) || AR_SREV_9285(ah))
		pCap->hw_caps &= ~ATH9K_HW_CAP_4KB_SPLITTRANS;
	else
		pCap->hw_caps |= ATH9K_HW_CAP_4KB_SPLITTRANS;

	if (regulatory->current_rd_ext & (1 << REG_EXT_JAPAN_MIDBAND)) {
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

	/* Advertise midband for AR5416 with FCC midband set in eeprom */
	if (regulatory->current_rd_ext & (1 << REG_EXT_FCC_MIDBAND) &&
	    AR_SREV_5416(ah))
		pCap->reg_cap |= AR_EEPROM_EEREGCAP_EN_FCC_MIDBAND;

	pCap->num_antcfg_5ghz =
		ah->eep_ops->get_num_ant_config(ah, ATH9K_HAL_FREQ_BAND_5GHZ);
	pCap->num_antcfg_2ghz =
		ah->eep_ops->get_num_ant_config(ah, ATH9K_HAL_FREQ_BAND_2GHZ);

	if (AR_SREV_9280_10_OR_LATER(ah) &&
	    ath9k_hw_btcoex_supported(ah)) {
		btcoex_hw->btactive_gpio = ATH_BTACTIVE_GPIO;
		btcoex_hw->wlanactive_gpio = ATH_WLANACTIVE_GPIO;

		if (AR_SREV_9285(ah)) {
			btcoex_hw->scheme = ATH_BTCOEX_CFG_3WIRE;
			btcoex_hw->btpriority_gpio = ATH_BTPRIORITY_GPIO;
		} else {
			btcoex_hw->scheme = ATH_BTCOEX_CFG_2WIRE;
		}
	} else {
		btcoex_hw->scheme = ATH_BTCOEX_CFG_NONE;
	}

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		pCap->hw_caps |= ATH9K_HW_CAP_EDMA | ATH9K_HW_CAP_LDPC |
				 ATH9K_HW_CAP_FASTCLOCK;
		pCap->rx_hp_qdepth = ATH9K_HW_RX_HP_QDEPTH;
		pCap->rx_lp_qdepth = ATH9K_HW_RX_LP_QDEPTH;
		pCap->rx_status_len = sizeof(struct ar9003_rxs);
		pCap->tx_desc_len = sizeof(struct ar9003_txc);
		pCap->txs_len = sizeof(struct ar9003_txs);
	} else {
		pCap->tx_desc_len = sizeof(struct ath_desc);
		if (AR_SREV_9280_20(ah) &&
		    ((ah->eep_ops->get_eeprom(ah, EEP_MINOR_REV) <=
		      AR5416_EEP_MINOR_VER_16) ||
		     ah->eep_ops->get_eeprom(ah, EEP_FSTCLK_5G)))
			pCap->hw_caps |= ATH9K_HW_CAP_FASTCLOCK;
	}

	if (AR_SREV_9300_20_OR_LATER(ah))
		pCap->hw_caps |= ATH9K_HW_CAP_RAC_SUPPORTED;

	return 0;
}

bool ath9k_hw_getcapability(struct ath_hw *ah, enum ath9k_capability_type type,
			    u32 capability, u32 *result)
{
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
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
			return (ah->sta_id1_defaults &
				AR_STA_ID1_CRPT_MIC_ENABLE) ? true :
			false;
		}
	case ATH9K_CAP_TKIP_SPLIT:
		return (ah->misc_mode & AR_PCU_MIC_NEW_LOC_ENA) ?
			false : true;
	case ATH9K_CAP_MCAST_KEYSRCH:
		switch (capability) {
		case 0:
			return true;
		case 1:
			if (REG_READ(ah, AR_STA_ID1) & AR_STA_ID1_ADHOC) {
				return false;
			} else {
				return (ah->sta_id1_defaults &
					AR_STA_ID1_MCAST_KSRCH) ? true :
					false;
			}
		}
		return false;
	case ATH9K_CAP_TXPOW:
		switch (capability) {
		case 0:
			return 0;
		case 1:
			*result = regulatory->power_limit;
			return 0;
		case 2:
			*result = regulatory->max_power_level;
			return 0;
		case 3:
			*result = regulatory->tp_scale;
			return 0;
		}
		return false;
	case ATH9K_CAP_DS:
		return (AR_SREV_9280_20_OR_LATER(ah) &&
			(ah->eep_ops->get_eeprom(ah, EEP_RC_CHAIN_MASK) == 1))
			? false : true;
	default:
		return false;
	}
}
EXPORT_SYMBOL(ath9k_hw_getcapability);

bool ath9k_hw_setcapability(struct ath_hw *ah, enum ath9k_capability_type type,
			    u32 capability, u32 setting, int *status)
{
	switch (type) {
	case ATH9K_CAP_TKIP_MIC:
		if (setting)
			ah->sta_id1_defaults |=
				AR_STA_ID1_CRPT_MIC_ENABLE;
		else
			ah->sta_id1_defaults &=
				~AR_STA_ID1_CRPT_MIC_ENABLE;
		return true;
	case ATH9K_CAP_MCAST_KEYSRCH:
		if (setting)
			ah->sta_id1_defaults |= AR_STA_ID1_MCAST_KSRCH;
		else
			ah->sta_id1_defaults &= ~AR_STA_ID1_MCAST_KSRCH;
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL(ath9k_hw_setcapability);

/****************************/
/* GPIO / RFKILL / Antennae */
/****************************/

static void ath9k_hw_gpio_cfg_output_mux(struct ath_hw *ah,
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

void ath9k_hw_cfg_gpio_input(struct ath_hw *ah, u32 gpio)
{
	u32 gpio_shift;

	BUG_ON(gpio >= ah->caps.num_gpio_pins);

	gpio_shift = gpio << 1;

	REG_RMW(ah,
		AR_GPIO_OE_OUT,
		(AR_GPIO_OE_OUT_DRV_NO << gpio_shift),
		(AR_GPIO_OE_OUT_DRV << gpio_shift));
}
EXPORT_SYMBOL(ath9k_hw_cfg_gpio_input);

u32 ath9k_hw_gpio_get(struct ath_hw *ah, u32 gpio)
{
#define MS_REG_READ(x, y) \
	(MS(REG_READ(ah, AR_GPIO_IN_OUT), x##_GPIO_IN_VAL) & (AR_GPIO_BIT(y)))

	if (gpio >= ah->caps.num_gpio_pins)
		return 0xffffffff;

	if (AR_SREV_9300_20_OR_LATER(ah))
		return MS_REG_READ(AR9300, gpio) != 0;
	else if (AR_SREV_9271(ah))
		return MS_REG_READ(AR9271, gpio) != 0;
	else if (AR_SREV_9287_10_OR_LATER(ah))
		return MS_REG_READ(AR9287, gpio) != 0;
	else if (AR_SREV_9285_10_OR_LATER(ah))
		return MS_REG_READ(AR9285, gpio) != 0;
	else if (AR_SREV_9280_10_OR_LATER(ah))
		return MS_REG_READ(AR928X, gpio) != 0;
	else
		return MS_REG_READ(AR, gpio) != 0;
}
EXPORT_SYMBOL(ath9k_hw_gpio_get);

void ath9k_hw_cfg_output(struct ath_hw *ah, u32 gpio,
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
EXPORT_SYMBOL(ath9k_hw_cfg_output);

void ath9k_hw_set_gpio(struct ath_hw *ah, u32 gpio, u32 val)
{
	if (AR_SREV_9271(ah))
		val = ~val;

	REG_RMW(ah, AR_GPIO_IN_OUT, ((val & 1) << gpio),
		AR_GPIO_BIT(gpio));
}
EXPORT_SYMBOL(ath9k_hw_set_gpio);

u32 ath9k_hw_getdefantenna(struct ath_hw *ah)
{
	return REG_READ(ah, AR_DEF_ANTENNA) & 0x7;
}
EXPORT_SYMBOL(ath9k_hw_getdefantenna);

void ath9k_hw_setantenna(struct ath_hw *ah, u32 antenna)
{
	REG_WRITE(ah, AR_DEF_ANTENNA, (antenna & 0x7));
}
EXPORT_SYMBOL(ath9k_hw_setantenna);

/*********************/
/* General Operation */
/*********************/

u32 ath9k_hw_getrxfilter(struct ath_hw *ah)
{
	u32 bits = REG_READ(ah, AR_RX_FILTER);
	u32 phybits = REG_READ(ah, AR_PHY_ERR);

	if (phybits & AR_PHY_ERR_RADAR)
		bits |= ATH9K_RX_FILTER_PHYRADAR;
	if (phybits & (AR_PHY_ERR_OFDM_TIMING | AR_PHY_ERR_CCK_TIMING))
		bits |= ATH9K_RX_FILTER_PHYERR;

	return bits;
}
EXPORT_SYMBOL(ath9k_hw_getrxfilter);

void ath9k_hw_setrxfilter(struct ath_hw *ah, u32 bits)
{
	u32 phybits;

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_RX_FILTER, bits);

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

	REGWRITE_BUFFER_FLUSH(ah);
	DISABLE_REGWRITE_BUFFER(ah);
}
EXPORT_SYMBOL(ath9k_hw_setrxfilter);

bool ath9k_hw_phy_disable(struct ath_hw *ah)
{
	if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_WARM))
		return false;

	ath9k_hw_init_pll(ah, NULL);
	return true;
}
EXPORT_SYMBOL(ath9k_hw_phy_disable);

bool ath9k_hw_disable(struct ath_hw *ah)
{
	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE))
		return false;

	if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_COLD))
		return false;

	ath9k_hw_init_pll(ah, NULL);
	return true;
}
EXPORT_SYMBOL(ath9k_hw_disable);

void ath9k_hw_set_txpowerlimit(struct ath_hw *ah, u32 limit)
{
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	struct ath9k_channel *chan = ah->curchan;
	struct ieee80211_channel *channel = chan->chan;

	regulatory->power_limit = min(limit, (u32) MAX_RATE_POWER);

	ah->eep_ops->set_txpower(ah, chan,
				 ath9k_regd_get_ctl(regulatory, chan),
				 channel->max_antenna_gain * 2,
				 channel->max_power * 2,
				 min((u32) MAX_RATE_POWER,
				 (u32) regulatory->power_limit));
}
EXPORT_SYMBOL(ath9k_hw_set_txpowerlimit);

void ath9k_hw_setmac(struct ath_hw *ah, const u8 *mac)
{
	memcpy(ath9k_hw_common(ah)->macaddr, mac, ETH_ALEN);
}
EXPORT_SYMBOL(ath9k_hw_setmac);

void ath9k_hw_setopmode(struct ath_hw *ah)
{
	ath9k_hw_set_operating_mode(ah, ah->opmode);
}
EXPORT_SYMBOL(ath9k_hw_setopmode);

void ath9k_hw_setmcastfilter(struct ath_hw *ah, u32 filter0, u32 filter1)
{
	REG_WRITE(ah, AR_MCAST_FIL0, filter0);
	REG_WRITE(ah, AR_MCAST_FIL1, filter1);
}
EXPORT_SYMBOL(ath9k_hw_setmcastfilter);

void ath9k_hw_write_associd(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);

	REG_WRITE(ah, AR_BSS_ID0, get_unaligned_le32(common->curbssid));
	REG_WRITE(ah, AR_BSS_ID1, get_unaligned_le16(common->curbssid + 4) |
		  ((common->curaid & 0x3fff) << AR_BSS_ID1_AID_S));
}
EXPORT_SYMBOL(ath9k_hw_write_associd);

#define ATH9K_MAX_TSF_READ 10

u64 ath9k_hw_gettsf64(struct ath_hw *ah)
{
	u32 tsf_lower, tsf_upper1, tsf_upper2;
	int i;

	tsf_upper1 = REG_READ(ah, AR_TSF_U32);
	for (i = 0; i < ATH9K_MAX_TSF_READ; i++) {
		tsf_lower = REG_READ(ah, AR_TSF_L32);
		tsf_upper2 = REG_READ(ah, AR_TSF_U32);
		if (tsf_upper2 == tsf_upper1)
			break;
		tsf_upper1 = tsf_upper2;
	}

	WARN_ON( i == ATH9K_MAX_TSF_READ );

	return (((u64)tsf_upper1 << 32) | tsf_lower);
}
EXPORT_SYMBOL(ath9k_hw_gettsf64);

void ath9k_hw_settsf64(struct ath_hw *ah, u64 tsf64)
{
	REG_WRITE(ah, AR_TSF_L32, tsf64 & 0xffffffff);
	REG_WRITE(ah, AR_TSF_U32, (tsf64 >> 32) & 0xffffffff);
}
EXPORT_SYMBOL(ath9k_hw_settsf64);

void ath9k_hw_reset_tsf(struct ath_hw *ah)
{
	if (!ath9k_hw_wait(ah, AR_SLP32_MODE, AR_SLP32_TSF_WRITE_STATUS, 0,
			   AH_TSF_WRITE_TIMEOUT))
		ath_print(ath9k_hw_common(ah), ATH_DBG_RESET,
			  "AR_SLP32_TSF_WRITE_STATUS limit exceeded\n");

	REG_WRITE(ah, AR_RESET_TSF, AR_RESET_TSF_ONCE);
}
EXPORT_SYMBOL(ath9k_hw_reset_tsf);

void ath9k_hw_set_tsfadjust(struct ath_hw *ah, u32 setting)
{
	if (setting)
		ah->misc_mode |= AR_PCU_TX_ADD_TSF;
	else
		ah->misc_mode &= ~AR_PCU_TX_ADD_TSF;
}
EXPORT_SYMBOL(ath9k_hw_set_tsfadjust);

/*
 *  Extend 15-bit time stamp from rx descriptor to
 *  a full 64-bit TSF using the current h/w TSF.
*/
u64 ath9k_hw_extend_tsf(struct ath_hw *ah, u32 rstamp)
{
	u64 tsf;

	tsf = ath9k_hw_gettsf64(ah);
	if ((tsf & 0x7fff) < rstamp)
		tsf -= 0x8000;
	return (tsf & ~0x7fff) | rstamp;
}
EXPORT_SYMBOL(ath9k_hw_extend_tsf);

void ath9k_hw_set11nmac2040(struct ath_hw *ah)
{
	struct ieee80211_conf *conf = &ath9k_hw_common(ah)->hw->conf;
	u32 macmode;

	if (conf_is_ht40(conf) && !ah->config.cwm_ignore_extcca)
		macmode = AR_2040_JOINED_RX_CLEAR;
	else
		macmode = 0;

	REG_WRITE(ah, AR_2040_MODE, macmode);
}

/* HW Generic timers configuration */

static const struct ath_gen_timer_configuration gen_tmr_configuration[] =
{
	{AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
	{AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
	{AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
	{AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
	{AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
	{AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
	{AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
	{AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
	{AR_NEXT_NDP2_TIMER, AR_NDP2_PERIOD, AR_NDP2_TIMER_MODE, 0x0001},
	{AR_NEXT_NDP2_TIMER + 1*4, AR_NDP2_PERIOD + 1*4,
				AR_NDP2_TIMER_MODE, 0x0002},
	{AR_NEXT_NDP2_TIMER + 2*4, AR_NDP2_PERIOD + 2*4,
				AR_NDP2_TIMER_MODE, 0x0004},
	{AR_NEXT_NDP2_TIMER + 3*4, AR_NDP2_PERIOD + 3*4,
				AR_NDP2_TIMER_MODE, 0x0008},
	{AR_NEXT_NDP2_TIMER + 4*4, AR_NDP2_PERIOD + 4*4,
				AR_NDP2_TIMER_MODE, 0x0010},
	{AR_NEXT_NDP2_TIMER + 5*4, AR_NDP2_PERIOD + 5*4,
				AR_NDP2_TIMER_MODE, 0x0020},
	{AR_NEXT_NDP2_TIMER + 6*4, AR_NDP2_PERIOD + 6*4,
				AR_NDP2_TIMER_MODE, 0x0040},
	{AR_NEXT_NDP2_TIMER + 7*4, AR_NDP2_PERIOD + 7*4,
				AR_NDP2_TIMER_MODE, 0x0080}
};

/* HW generic timer primitives */

/* compute and clear index of rightmost 1 */
static u32 rightmost_index(struct ath_gen_timer_table *timer_table, u32 *mask)
{
	u32 b;

	b = *mask;
	b &= (0-b);
	*mask &= ~b;
	b *= debruijn32;
	b >>= 27;

	return timer_table->gen_timer_index[b];
}

u32 ath9k_hw_gettsf32(struct ath_hw *ah)
{
	return REG_READ(ah, AR_TSF_L32);
}
EXPORT_SYMBOL(ath9k_hw_gettsf32);

struct ath_gen_timer *ath_gen_timer_alloc(struct ath_hw *ah,
					  void (*trigger)(void *),
					  void (*overflow)(void *),
					  void *arg,
					  u8 timer_index)
{
	struct ath_gen_timer_table *timer_table = &ah->hw_gen_timers;
	struct ath_gen_timer *timer;

	timer = kzalloc(sizeof(struct ath_gen_timer), GFP_KERNEL);

	if (timer == NULL) {
		ath_print(ath9k_hw_common(ah), ATH_DBG_FATAL,
			  "Failed to allocate memory"
			  "for hw timer[%d]\n", timer_index);
		return NULL;
	}

	/* allocate a hardware generic timer slot */
	timer_table->timers[timer_index] = timer;
	timer->index = timer_index;
	timer->trigger = trigger;
	timer->overflow = overflow;
	timer->arg = arg;

	return timer;
}
EXPORT_SYMBOL(ath_gen_timer_alloc);

void ath9k_hw_gen_timer_start(struct ath_hw *ah,
			      struct ath_gen_timer *timer,
			      u32 timer_next,
			      u32 timer_period)
{
	struct ath_gen_timer_table *timer_table = &ah->hw_gen_timers;
	u32 tsf;

	BUG_ON(!timer_period);

	set_bit(timer->index, &timer_table->timer_mask.timer_bits);

	tsf = ath9k_hw_gettsf32(ah);

	ath_print(ath9k_hw_common(ah), ATH_DBG_HWTIMER,
		  "curent tsf %x period %x"
		  "timer_next %x\n", tsf, timer_period, timer_next);

	/*
	 * Pull timer_next forward if the current TSF already passed it
	 * because of software latency
	 */
	if (timer_next < tsf)
		timer_next = tsf + timer_period;

	/*
	 * Program generic timer registers
	 */
	REG_WRITE(ah, gen_tmr_configuration[timer->index].next_addr,
		 timer_next);
	REG_WRITE(ah, gen_tmr_configuration[timer->index].period_addr,
		  timer_period);
	REG_SET_BIT(ah, gen_tmr_configuration[timer->index].mode_addr,
		    gen_tmr_configuration[timer->index].mode_mask);

	/* Enable both trigger and thresh interrupt masks */
	REG_SET_BIT(ah, AR_IMR_S5,
		(SM(AR_GENTMR_BIT(timer->index), AR_IMR_S5_GENTIMER_THRESH) |
		SM(AR_GENTMR_BIT(timer->index), AR_IMR_S5_GENTIMER_TRIG)));
}
EXPORT_SYMBOL(ath9k_hw_gen_timer_start);

void ath9k_hw_gen_timer_stop(struct ath_hw *ah, struct ath_gen_timer *timer)
{
	struct ath_gen_timer_table *timer_table = &ah->hw_gen_timers;

	if ((timer->index < AR_FIRST_NDP_TIMER) ||
		(timer->index >= ATH_MAX_GEN_TIMER)) {
		return;
	}

	/* Clear generic timer enable bits. */
	REG_CLR_BIT(ah, gen_tmr_configuration[timer->index].mode_addr,
			gen_tmr_configuration[timer->index].mode_mask);

	/* Disable both trigger and thresh interrupt masks */
	REG_CLR_BIT(ah, AR_IMR_S5,
		(SM(AR_GENTMR_BIT(timer->index), AR_IMR_S5_GENTIMER_THRESH) |
		SM(AR_GENTMR_BIT(timer->index), AR_IMR_S5_GENTIMER_TRIG)));

	clear_bit(timer->index, &timer_table->timer_mask.timer_bits);
}
EXPORT_SYMBOL(ath9k_hw_gen_timer_stop);

void ath_gen_timer_free(struct ath_hw *ah, struct ath_gen_timer *timer)
{
	struct ath_gen_timer_table *timer_table = &ah->hw_gen_timers;

	/* free the hardware generic timer slot */
	timer_table->timers[timer->index] = NULL;
	kfree(timer);
}
EXPORT_SYMBOL(ath_gen_timer_free);

/*
 * Generic Timer Interrupts handling
 */
void ath_gen_timer_isr(struct ath_hw *ah)
{
	struct ath_gen_timer_table *timer_table = &ah->hw_gen_timers;
	struct ath_gen_timer *timer;
	struct ath_common *common = ath9k_hw_common(ah);
	u32 trigger_mask, thresh_mask, index;

	/* get hardware generic timer interrupt status */
	trigger_mask = ah->intr_gen_timer_trigger;
	thresh_mask = ah->intr_gen_timer_thresh;
	trigger_mask &= timer_table->timer_mask.val;
	thresh_mask &= timer_table->timer_mask.val;

	trigger_mask &= ~thresh_mask;

	while (thresh_mask) {
		index = rightmost_index(timer_table, &thresh_mask);
		timer = timer_table->timers[index];
		BUG_ON(!timer);
		ath_print(common, ATH_DBG_HWTIMER,
			  "TSF overflow for Gen timer %d\n", index);
		timer->overflow(timer->arg);
	}

	while (trigger_mask) {
		index = rightmost_index(timer_table, &trigger_mask);
		timer = timer_table->timers[index];
		BUG_ON(!timer);
		ath_print(common, ATH_DBG_HWTIMER,
			  "Gen timer[%d] trigger\n", index);
		timer->trigger(timer->arg);
	}
}
EXPORT_SYMBOL(ath_gen_timer_isr);

/********/
/* HTC  */
/********/

void ath9k_hw_htc_resetinit(struct ath_hw *ah)
{
	ah->htc_reset_init = true;
}
EXPORT_SYMBOL(ath9k_hw_htc_resetinit);

static struct {
	u32 version;
	const char * name;
} ath_mac_bb_names[] = {
	/* Devices with external radios */
	{ AR_SREV_VERSION_5416_PCI,	"5416" },
	{ AR_SREV_VERSION_5416_PCIE,	"5418" },
	{ AR_SREV_VERSION_9100,		"9100" },
	{ AR_SREV_VERSION_9160,		"9160" },
	/* Single-chip solutions */
	{ AR_SREV_VERSION_9280,		"9280" },
	{ AR_SREV_VERSION_9285,		"9285" },
	{ AR_SREV_VERSION_9287,         "9287" },
	{ AR_SREV_VERSION_9271,         "9271" },
	{ AR_SREV_VERSION_9300,         "9300" },
};

/* For devices with external radios */
static struct {
	u16 version;
	const char * name;
} ath_rf_names[] = {
	{ 0,				"5133" },
	{ AR_RAD5133_SREV_MAJOR,	"5133" },
	{ AR_RAD5122_SREV_MAJOR,	"5122" },
	{ AR_RAD2133_SREV_MAJOR,	"2133" },
	{ AR_RAD2122_SREV_MAJOR,	"2122" }
};

/*
 * Return the MAC/BB name. "????" is returned if the MAC/BB is unknown.
 */
static const char *ath9k_hw_mac_bb_name(u32 mac_bb_version)
{
	int i;

	for (i=0; i<ARRAY_SIZE(ath_mac_bb_names); i++) {
		if (ath_mac_bb_names[i].version == mac_bb_version) {
			return ath_mac_bb_names[i].name;
		}
	}

	return "????";
}

/*
 * Return the RF name. "????" is returned if the RF is unknown.
 * Used for devices with external radios.
 */
static const char *ath9k_hw_rf_name(u16 rf_version)
{
	int i;

	for (i=0; i<ARRAY_SIZE(ath_rf_names); i++) {
		if (ath_rf_names[i].version == rf_version) {
			return ath_rf_names[i].name;
		}
	}

	return "????";
}

void ath9k_hw_name(struct ath_hw *ah, char *hw_name, size_t len)
{
	int used;

	/* chipsets >= AR9280 are single-chip */
	if (AR_SREV_9280_10_OR_LATER(ah)) {
		used = snprintf(hw_name, len,
			       "Atheros AR%s Rev:%x",
			       ath9k_hw_mac_bb_name(ah->hw_version.macVersion),
			       ah->hw_version.macRev);
	}
	else {
		used = snprintf(hw_name, len,
			       "Atheros AR%s MAC/BB Rev:%x AR%s RF Rev:%x",
			       ath9k_hw_mac_bb_name(ah->hw_version.macVersion),
			       ah->hw_version.macRev,
			       ath9k_hw_rf_name((ah->hw_version.analog5GhzRev &
						AR_RADIO_SREV_MAJOR)),
			       ah->hw_version.phyRev);
	}

	hw_name[used] = '\0';
}
EXPORT_SYMBOL(ath9k_hw_name);
