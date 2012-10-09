/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
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
#include <linux/module.h>
#include <asm/unaligned.h>

#include "hw.h"
#include "hw-ops.h"
#include "rc.h"
#include "ar9003_mac.h"
#include "ar9003_mci.h"
#include "ar9003_phy.h"
#include "debug.h"
#include "ath9k.h"

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

static void ath9k_hw_ani_cache_ini_regs(struct ath_hw *ah)
{
	/* You will not have this callback if using the old ANI */
	if (!ath9k_hw_private_ops(ah)->ani_cache_ini_regs)
		return;

	ath9k_hw_private_ops(ah)->ani_cache_ini_regs(ah);
}

/********************/
/* Helper Functions */
/********************/

#ifdef CONFIG_ATH9K_DEBUGFS

void ath9k_debug_sync_cause(struct ath_common *common, u32 sync_cause)
{
	struct ath_softc *sc = common->priv;
	if (sync_cause)
		sc->debug.stats.istats.sync_cause_all++;
	if (sync_cause & AR_INTR_SYNC_RTC_IRQ)
		sc->debug.stats.istats.sync_rtc_irq++;
	if (sync_cause & AR_INTR_SYNC_MAC_IRQ)
		sc->debug.stats.istats.sync_mac_irq++;
	if (sync_cause & AR_INTR_SYNC_EEPROM_ILLEGAL_ACCESS)
		sc->debug.stats.istats.eeprom_illegal_access++;
	if (sync_cause & AR_INTR_SYNC_APB_TIMEOUT)
		sc->debug.stats.istats.apb_timeout++;
	if (sync_cause & AR_INTR_SYNC_PCI_MODE_CONFLICT)
		sc->debug.stats.istats.pci_mode_conflict++;
	if (sync_cause & AR_INTR_SYNC_HOST1_FATAL)
		sc->debug.stats.istats.host1_fatal++;
	if (sync_cause & AR_INTR_SYNC_HOST1_PERR)
		sc->debug.stats.istats.host1_perr++;
	if (sync_cause & AR_INTR_SYNC_TRCV_FIFO_PERR)
		sc->debug.stats.istats.trcv_fifo_perr++;
	if (sync_cause & AR_INTR_SYNC_RADM_CPL_EP)
		sc->debug.stats.istats.radm_cpl_ep++;
	if (sync_cause & AR_INTR_SYNC_RADM_CPL_DLLP_ABORT)
		sc->debug.stats.istats.radm_cpl_dllp_abort++;
	if (sync_cause & AR_INTR_SYNC_RADM_CPL_TLP_ABORT)
		sc->debug.stats.istats.radm_cpl_tlp_abort++;
	if (sync_cause & AR_INTR_SYNC_RADM_CPL_ECRC_ERR)
		sc->debug.stats.istats.radm_cpl_ecrc_err++;
	if (sync_cause & AR_INTR_SYNC_RADM_CPL_TIMEOUT)
		sc->debug.stats.istats.radm_cpl_timeout++;
	if (sync_cause & AR_INTR_SYNC_LOCAL_TIMEOUT)
		sc->debug.stats.istats.local_timeout++;
	if (sync_cause & AR_INTR_SYNC_PM_ACCESS)
		sc->debug.stats.istats.pm_access++;
	if (sync_cause & AR_INTR_SYNC_MAC_AWAKE)
		sc->debug.stats.istats.mac_awake++;
	if (sync_cause & AR_INTR_SYNC_MAC_ASLEEP)
		sc->debug.stats.istats.mac_asleep++;
	if (sync_cause & AR_INTR_SYNC_MAC_SLEEP_ACCESS)
		sc->debug.stats.istats.mac_sleep_access++;
}
#endif


static void ath9k_hw_set_clockrate(struct ath_hw *ah)
{
	struct ieee80211_conf *conf = &ath9k_hw_common(ah)->hw->conf;
	struct ath_common *common = ath9k_hw_common(ah);
	unsigned int clockrate;

	/* AR9287 v1.3+ uses async FIFO and runs the MAC at 117 MHz */
	if (AR_SREV_9287(ah) && AR_SREV_9287_13_OR_LATER(ah))
		clockrate = 117;
	else if (!ah->curchan) /* should really check for CCK instead */
		clockrate = ATH9K_CLOCK_RATE_CCK;
	else if (conf->channel->band == IEEE80211_BAND_2GHZ)
		clockrate = ATH9K_CLOCK_RATE_2GHZ_OFDM;
	else if (ah->caps.hw_caps & ATH9K_HW_CAP_FASTCLOCK)
		clockrate = ATH9K_CLOCK_FAST_RATE_5GHZ_OFDM;
	else
		clockrate = ATH9K_CLOCK_RATE_5GHZ_OFDM;

	if (conf_is_ht40(conf))
		clockrate *= 2;

	if (ah->curchan) {
		if (IS_CHAN_HALF_RATE(ah->curchan))
			clockrate /= 2;
		if (IS_CHAN_QUARTER_RATE(ah->curchan))
			clockrate /= 4;
	}

	common->clockrate = clockrate;
}

static u32 ath9k_hw_mac_to_clks(struct ath_hw *ah, u32 usecs)
{
	struct ath_common *common = ath9k_hw_common(ah);

	return usecs * common->clockrate;
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

	ath_dbg(ath9k_hw_common(ah), ANY,
		"timeout (%d us) on reg 0x%x: 0x%08x & 0x%08x != 0x%08x\n",
		timeout, reg, REG_READ(ah, reg), mask, val);

	return false;
}
EXPORT_SYMBOL(ath9k_hw_wait);

void ath9k_hw_synth_delay(struct ath_hw *ah, struct ath9k_channel *chan,
			  int hw_delay)
{
	if (IS_CHAN_B(chan))
		hw_delay = (4 * hw_delay) / 22;
	else
		hw_delay /= 10;

	if (IS_CHAN_HALF_RATE(chan))
		hw_delay *= 2;
	else if (IS_CHAN_QUARTER_RATE(chan))
		hw_delay *= 4;

	udelay(hw_delay + BASE_ACTIVATE_DELAY);
}

void ath9k_hw_write_array(struct ath_hw *ah, struct ar5416IniArray *array,
			  int column, unsigned int *writecnt)
{
	int r;

	ENABLE_REGWRITE_BUFFER(ah);
	for (r = 0; r < array->ia_rows; r++) {
		REG_WRITE(ah, INI_RA(array, r, 0),
			  INI_RA(array, r, column));
		DO_DELAY(*writecnt);
	}
	REGWRITE_BUFFER_FLUSH(ah);
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
		ath_err(ath9k_hw_common(ah),
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

	switch (ah->hw_version.devid) {
	case AR5416_AR9100_DEVID:
		ah->hw_version.macVersion = AR_SREV_VERSION_9100;
		break;
	case AR9300_DEVID_AR9330:
		ah->hw_version.macVersion = AR_SREV_VERSION_9330;
		if (ah->get_mac_revision) {
			ah->hw_version.macRev = ah->get_mac_revision();
		} else {
			val = REG_READ(ah, AR_SREV);
			ah->hw_version.macRev = MS(val, AR_SREV_REVISION2);
		}
		return;
	case AR9300_DEVID_AR9340:
		ah->hw_version.macVersion = AR_SREV_VERSION_9340;
		val = REG_READ(ah, AR_SREV);
		ah->hw_version.macRev = MS(val, AR_SREV_REVISION2);
		return;
	case AR9300_DEVID_QCA955X:
		ah->hw_version.macVersion = AR_SREV_VERSION_9550;
		return;
	}

	val = REG_READ(ah, AR_SREV) & AR_SREV_ID;

	if (val == 0xFF) {
		val = REG_READ(ah, AR_SREV);
		ah->hw_version.macVersion =
			(val & AR_SREV_VERSION2) >> AR_SREV_TYPE2_S;
		ah->hw_version.macRev = MS(val, AR_SREV_REVISION2);

		if (AR_SREV_9462(ah) || AR_SREV_9565(ah))
			ah->is_pciexpress = true;
		else
			ah->is_pciexpress = (val &
					     AR_SREV_TYPE2_HOST_MODE) ? 0 : 1;
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
	if (!AR_SREV_5416(ah))
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

/* This should work for all families including legacy */
static bool ath9k_hw_chip_test(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 regAddr[2] = { AR_STA_ID0 };
	u32 regHold[2];
	static const u32 patternData[4] = {
		0x55555555, 0xaaaaaaaa, 0x66666666, 0x99999999
	};
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
				ath_err(common,
					"address test failed addr: 0x%08x - wr:0x%08x != rd:0x%08x\n",
					addr, wrData, rdData);
				return false;
			}
		}
		for (j = 0; j < 4; j++) {
			wrData = patternData[j];
			REG_WRITE(ah, addr, wrData);
			rdData = REG_READ(ah, addr);
			if (wrData != rdData) {
				ath_err(common,
					"address test failed addr: 0x%08x - wr:0x%08x != rd:0x%08x\n",
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

	ah->config.dma_beacon_response_time = 1;
	ah->config.sw_beacon_response_time = 6;
	ah->config.additional_swba_backoff = 0;
	ah->config.ack_6mb = 0x0;
	ah->config.cwm_ignore_extcca = 0;
	ah->config.pcie_clock_req = 0;
	ah->config.pcie_waen = 0;
	ah->config.analog_shiftreg = 1;
	ah->config.enable_ani = true;

	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		ah->config.spurchans[i][0] = AR_NO_SPUR;
		ah->config.spurchans[i][1] = AR_NO_SPUR;
	}

	ah->config.rx_intr_mitigation = true;
	ah->config.pcieSerDesWrite = true;

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

	ah->hw_version.magic = AR5416_MAGIC;
	ah->hw_version.subvendorid = 0;

	ah->atim_window = 0;
	ah->sta_id1_defaults =
		AR_STA_ID1_CRPT_MIC_ENABLE |
		AR_STA_ID1_MCAST_KSRCH;
	if (AR_SREV_9100(ah))
		ah->sta_id1_defaults |= AR_STA_ID1_AR9100_BA_FIX;
	ah->slottime = ATH9K_SLOT_TIME_9;
	ah->globaltxtimeout = (u32) -1;
	ah->power_mode = ATH9K_PM_UNDEFINED;
	ah->htc_reset_init = true;
}

static int ath9k_hw_init_macaddr(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 sum;
	int i;
	u16 eeval;
	static const u32 EEP_MAC[] = { EEP_MAC_LSW, EEP_MAC_MID, EEP_MAC_MSW };

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
	struct ath_common *common = ath9k_hw_common(ah);
	int ecode;

	if (common->bus_ops->ath_bus_type != ATH_USB) {
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

	ath_dbg(ath9k_hw_common(ah), CONFIG, "Eeprom VER: %d, REV: %d\n",
		ah->eep_ops->get_eeprom_ver(ah),
		ah->eep_ops->get_eeprom_rev(ah));

	ecode = ath9k_hw_rf_alloc_ext_banks(ah);
	if (ecode) {
		ath_err(ath9k_hw_common(ah),
			"Failed allocating banks for external radio\n");
		ath9k_hw_rf_free_ext_banks(ah);
		return ecode;
	}

	if (ah->config.enable_ani) {
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

	ath9k_hw_read_revisions(ah);

	/*
	 * Read back AR_WA into a permanent copy and set bits 14 and 17.
	 * We need to do this to avoid RMW of this register. We cannot
	 * read the reg when chip is asleep.
	 */
	ah->WARegVal = REG_READ(ah, AR_WA);
	ah->WARegVal |= (AR_WA_D3_L1_DISABLE |
			 AR_WA_ASPM_TIMER_BASED_DISABLE);

	if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_POWER_ON)) {
		ath_err(common, "Couldn't reset chip\n");
		return -EIO;
	}

	if (AR_SREV_9462(ah))
		ah->WARegVal &= ~AR_WA_D3_L1_DISABLE;

	if (AR_SREV_9565(ah)) {
		ah->WARegVal |= AR_WA_BIT22;
		REG_WRITE(ah, AR_WA, ah->WARegVal);
	}

	ath9k_hw_init_defaults(ah);
	ath9k_hw_init_config(ah);

	ath9k_hw_attach_ops(ah);

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE)) {
		ath_err(common, "Couldn't wakeup chip\n");
		return -EIO;
	}

	if (NR_CPUS > 1 && ah->config.serialize_regmode == SER_REG_MODE_AUTO) {
		if (ah->hw_version.macVersion == AR_SREV_VERSION_5416_PCI ||
		    ((AR_SREV_9160(ah) || AR_SREV_9280(ah) || AR_SREV_9287(ah)) &&
		     !ah->is_pciexpress)) {
			ah->config.serialize_regmode =
				SER_REG_MODE_ON;
		} else {
			ah->config.serialize_regmode =
				SER_REG_MODE_OFF;
		}
	}

	ath_dbg(common, RESET, "serialize_regmode is %d\n",
		ah->config.serialize_regmode);

	if (AR_SREV_9285(ah) || AR_SREV_9271(ah))
		ah->config.max_txtrig_level = MAX_TX_FIFO_THRESHOLD >> 1;
	else
		ah->config.max_txtrig_level = MAX_TX_FIFO_THRESHOLD;

	switch (ah->hw_version.macVersion) {
	case AR_SREV_VERSION_5416_PCI:
	case AR_SREV_VERSION_5416_PCIE:
	case AR_SREV_VERSION_9160:
	case AR_SREV_VERSION_9100:
	case AR_SREV_VERSION_9280:
	case AR_SREV_VERSION_9285:
	case AR_SREV_VERSION_9287:
	case AR_SREV_VERSION_9271:
	case AR_SREV_VERSION_9300:
	case AR_SREV_VERSION_9330:
	case AR_SREV_VERSION_9485:
	case AR_SREV_VERSION_9340:
	case AR_SREV_VERSION_9462:
	case AR_SREV_VERSION_9550:
	case AR_SREV_VERSION_9565:
		break;
	default:
		ath_err(common,
			"Mac Chip Rev 0x%02x.%x is not supported by this driver\n",
			ah->hw_version.macVersion, ah->hw_version.macRev);
		return -EOPNOTSUPP;
	}

	if (AR_SREV_9271(ah) || AR_SREV_9100(ah) || AR_SREV_9340(ah) ||
	    AR_SREV_9330(ah) || AR_SREV_9550(ah))
		ah->is_pciexpress = false;

	ah->hw_version.phyRev = REG_READ(ah, AR_PHY_CHIP_ID);
	ath9k_hw_init_cal_settings(ah);

	ah->ani_function = ATH9K_ANI_ALL;
	if (AR_SREV_9280_20_OR_LATER(ah) && !AR_SREV_9300_20_OR_LATER(ah))
		ah->ani_function &= ~ATH9K_ANI_NOISE_IMMUNITY_LEVEL;
	if (!AR_SREV_9300_20_OR_LATER(ah))
		ah->ani_function &= ~ATH9K_ANI_MRC_CCK;

	ath9k_hw_init_mode_regs(ah);

	if (!ah->is_pciexpress)
		ath9k_hw_disablepcie(ah);

	r = ath9k_hw_post_init(ah);
	if (r)
		return r;

	ath9k_hw_init_mode_gain_regs(ah);
	r = ath9k_hw_fill_cap_info(ah);
	if (r)
		return r;

	r = ath9k_hw_init_macaddr(ah);
	if (r) {
		ath_err(common, "Failed to initialize MAC address\n");
		return r;
	}

	if (AR_SREV_9285(ah) || AR_SREV_9271(ah))
		ah->tx_trig_level = (AR_FTRIG_256B >> AR_FTRIG_S);
	else
		ah->tx_trig_level = (AR_FTRIG_512B >> AR_FTRIG_S);

	if (AR_SREV_9330(ah))
		ah->bb_watchdog_timeout_ms = 85;
	else
		ah->bb_watchdog_timeout_ms = 25;

	common->state = ATH_HW_INITIALIZED;

	return 0;
}

int ath9k_hw_init(struct ath_hw *ah)
{
	int ret;
	struct ath_common *common = ath9k_hw_common(ah);

	/* These are all the AR5008/AR9001/AR9002/AR9003 hardware family of chipsets */
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
	case AR9300_DEVID_AR9485_PCIE:
	case AR9300_DEVID_AR9330:
	case AR9300_DEVID_AR9340:
	case AR9300_DEVID_QCA955X:
	case AR9300_DEVID_AR9580:
	case AR9300_DEVID_AR9462:
	case AR9485_DEVID_AR1111:
	case AR9300_DEVID_AR9565:
		break;
	default:
		if (common->bus_ops->ath_bus_type == ATH_USB)
			break;
		ath_err(common, "Hardware device ID 0x%04x not supported\n",
			ah->hw_version.devid);
		return -EOPNOTSUPP;
	}

	ret = __ath9k_hw_init(ah);
	if (ret) {
		ath_err(common,
			"Unable to initialize hardware; initialization status: %d\n",
			ret);
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
}

u32 ar9003_get_pll_sqsum_dvc(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	int i = 0;

	REG_CLR_BIT(ah, PLL3, PLL3_DO_MEAS_MASK);
	udelay(100);
	REG_SET_BIT(ah, PLL3, PLL3_DO_MEAS_MASK);

	while ((REG_READ(ah, PLL4) & PLL4_MEAS_DONE) == 0) {

		udelay(100);

		if (WARN_ON_ONCE(i >= 100)) {
			ath_err(common, "PLL4 meaurement not done\n");
			break;
		}

		i++;
	}

	return (REG_READ(ah, PLL3) & SQSUM_DVC_MASK) >> 3;
}
EXPORT_SYMBOL(ar9003_get_pll_sqsum_dvc);

static void ath9k_hw_init_pll(struct ath_hw *ah,
			      struct ath9k_channel *chan)
{
	u32 pll;

	if (AR_SREV_9485(ah) || AR_SREV_9565(ah)) {
		/* program BB PLL ki and kd value, ki=0x4, kd=0x40 */
		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL2,
			      AR_CH0_BB_DPLL2_PLL_PWD, 0x1);
		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL2,
			      AR_CH0_DPLL2_KD, 0x40);
		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL2,
			      AR_CH0_DPLL2_KI, 0x4);

		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL1,
			      AR_CH0_BB_DPLL1_REFDIV, 0x5);
		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL1,
			      AR_CH0_BB_DPLL1_NINI, 0x58);
		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL1,
			      AR_CH0_BB_DPLL1_NFRAC, 0x0);

		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL2,
			      AR_CH0_BB_DPLL2_OUTDIV, 0x1);
		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL2,
			      AR_CH0_BB_DPLL2_LOCAL_PLL, 0x1);
		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL2,
			      AR_CH0_BB_DPLL2_EN_NEGTRIG, 0x1);

		/* program BB PLL phase_shift to 0x6 */
		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL3,
			      AR_CH0_BB_DPLL3_PHASE_SHIFT, 0x6);

		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL2,
			      AR_CH0_BB_DPLL2_PLL_PWD, 0x0);
		udelay(1000);
	} else if (AR_SREV_9330(ah)) {
		u32 ddr_dpll2, pll_control2, kd;

		if (ah->is_clk_25mhz) {
			ddr_dpll2 = 0x18e82f01;
			pll_control2 = 0xe04a3d;
			kd = 0x1d;
		} else {
			ddr_dpll2 = 0x19e82f01;
			pll_control2 = 0x886666;
			kd = 0x3d;
		}

		/* program DDR PLL ki and kd value */
		REG_WRITE(ah, AR_CH0_DDR_DPLL2, ddr_dpll2);

		/* program DDR PLL phase_shift */
		REG_RMW_FIELD(ah, AR_CH0_DDR_DPLL3,
			      AR_CH0_DPLL3_PHASE_SHIFT, 0x1);

		REG_WRITE(ah, AR_RTC_PLL_CONTROL, 0x1142c);
		udelay(1000);

		/* program refdiv, nint, frac to RTC register */
		REG_WRITE(ah, AR_RTC_PLL_CONTROL2, pll_control2);

		/* program BB PLL kd and ki value */
		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL2, AR_CH0_DPLL2_KD, kd);
		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL2, AR_CH0_DPLL2_KI, 0x06);

		/* program BB PLL phase_shift */
		REG_RMW_FIELD(ah, AR_CH0_BB_DPLL3,
			      AR_CH0_BB_DPLL3_PHASE_SHIFT, 0x1);
	} else if (AR_SREV_9340(ah) || AR_SREV_9550(ah)) {
		u32 regval, pll2_divint, pll2_divfrac, refdiv;

		REG_WRITE(ah, AR_RTC_PLL_CONTROL, 0x1142c);
		udelay(1000);

		REG_SET_BIT(ah, AR_PHY_PLL_MODE, 0x1 << 16);
		udelay(100);

		if (ah->is_clk_25mhz) {
			pll2_divint = 0x54;
			pll2_divfrac = 0x1eb85;
			refdiv = 3;
		} else {
			if (AR_SREV_9340(ah)) {
				pll2_divint = 88;
				pll2_divfrac = 0;
				refdiv = 5;
			} else {
				pll2_divint = 0x11;
				pll2_divfrac = 0x26666;
				refdiv = 1;
			}
		}

		regval = REG_READ(ah, AR_PHY_PLL_MODE);
		regval |= (0x1 << 16);
		REG_WRITE(ah, AR_PHY_PLL_MODE, regval);
		udelay(100);

		REG_WRITE(ah, AR_PHY_PLL_CONTROL, (refdiv << 27) |
			  (pll2_divint << 18) | pll2_divfrac);
		udelay(100);

		regval = REG_READ(ah, AR_PHY_PLL_MODE);
		if (AR_SREV_9340(ah))
			regval = (regval & 0x80071fff) | (0x1 << 30) |
				 (0x1 << 13) | (0x4 << 26) | (0x18 << 19);
		else
			regval = (regval & 0x80071fff) | (0x3 << 30) |
				 (0x1 << 13) | (0x4 << 26) | (0x60 << 19);
		REG_WRITE(ah, AR_PHY_PLL_MODE, regval);
		REG_WRITE(ah, AR_PHY_PLL_MODE,
			  REG_READ(ah, AR_PHY_PLL_MODE) & 0xfffeffff);
		udelay(1000);
	}

	pll = ath9k_hw_compute_pll_control(ah, chan);
	if (AR_SREV_9565(ah))
		pll |= 0x40000;
	REG_WRITE(ah, AR_RTC_PLL_CONTROL, pll);

	if (AR_SREV_9485(ah) || AR_SREV_9340(ah) || AR_SREV_9330(ah) ||
	    AR_SREV_9550(ah))
		udelay(1000);

	/* Switch the core clock for ar9271 to 117Mhz */
	if (AR_SREV_9271(ah)) {
		udelay(500);
		REG_WRITE(ah, 0x50040, 0x304);
	}

	udelay(RTC_PLL_SETTLE_DELAY);

	REG_WRITE(ah, AR_RTC_SLEEP_CLK, AR_RTC_FORCE_DERIVED_CLK);

	if (AR_SREV_9340(ah) || AR_SREV_9550(ah)) {
		if (ah->is_clk_25mhz) {
			REG_WRITE(ah, AR_RTC_DERIVED_CLK, 0x17c << 1);
			REG_WRITE(ah, AR_SLP32_MODE, 0x0010f3d7);
			REG_WRITE(ah,  AR_SLP32_INC, 0x0001e7ae);
		} else {
			REG_WRITE(ah, AR_RTC_DERIVED_CLK, 0x261 << 1);
			REG_WRITE(ah, AR_SLP32_MODE, 0x0010f400);
			REG_WRITE(ah,  AR_SLP32_INC, 0x0001e800);
		}
		udelay(100);
	}
}

static void ath9k_hw_init_interrupt_masks(struct ath_hw *ah,
					  enum nl80211_iftype opmode)
{
	u32 sync_default = AR_INTR_SYNC_DEFAULT;
	u32 imr_reg = AR_IMR_TXERR |
		AR_IMR_TXURN |
		AR_IMR_RXERR |
		AR_IMR_RXORN |
		AR_IMR_BCNMISC;

	if (AR_SREV_9340(ah) || AR_SREV_9550(ah))
		sync_default &= ~AR_INTR_SYNC_HOST1_FATAL;

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

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_IMR, imr_reg);
	ah->imrs2_reg |= AR_IMR_S2_GTT;
	REG_WRITE(ah, AR_IMR_S2, ah->imrs2_reg);

	if (!AR_SREV_9100(ah)) {
		REG_WRITE(ah, AR_INTR_SYNC_CAUSE, 0xFFFFFFFF);
		REG_WRITE(ah, AR_INTR_SYNC_ENABLE, sync_default);
		REG_WRITE(ah, AR_INTR_SYNC_MASK, 0);
	}

	REGWRITE_BUFFER_FLUSH(ah);

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		REG_WRITE(ah, AR_INTR_PRIO_ASYNC_ENABLE, 0);
		REG_WRITE(ah, AR_INTR_PRIO_ASYNC_MASK, 0);
		REG_WRITE(ah, AR_INTR_PRIO_SYNC_ENABLE, 0);
		REG_WRITE(ah, AR_INTR_PRIO_SYNC_MASK, 0);
	}
}

static void ath9k_hw_set_sifs_time(struct ath_hw *ah, u32 us)
{
	u32 val = ath9k_hw_mac_to_clks(ah, us - 2);
	val = min(val, (u32) 0xFFFF);
	REG_WRITE(ah, AR_D_GBL_IFS_SIFS, val);
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
		ath_dbg(ath9k_hw_common(ah), XMIT, "bad global tx timeout %u\n",
			tu);
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
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_conf *conf = &common->hw->conf;
	const struct ath9k_channel *chan = ah->curchan;
	int acktimeout, ctstimeout, ack_offset = 0;
	int slottime;
	int sifstime;
	int rx_lat = 0, tx_lat = 0, eifs = 0;
	u32 reg;

	ath_dbg(ath9k_hw_common(ah), RESET, "ah->misc_mode 0x%x\n",
		ah->misc_mode);

	if (!chan)
		return;

	if (ah->misc_mode != 0)
		REG_SET_BIT(ah, AR_PCU_MISC, ah->misc_mode);

	if (IS_CHAN_A_FAST_CLOCK(ah, chan))
		rx_lat = 41;
	else
		rx_lat = 37;
	tx_lat = 54;

	if (IS_CHAN_5GHZ(chan))
		sifstime = 16;
	else
		sifstime = 10;

	if (IS_CHAN_HALF_RATE(chan)) {
		eifs = 175;
		rx_lat *= 2;
		tx_lat *= 2;
		if (IS_CHAN_A_FAST_CLOCK(ah, chan))
		    tx_lat += 11;

		sifstime *= 2;
		ack_offset = 16;
		slottime = 13;
	} else if (IS_CHAN_QUARTER_RATE(chan)) {
		eifs = 340;
		rx_lat = (rx_lat * 4) - 1;
		tx_lat *= 4;
		if (IS_CHAN_A_FAST_CLOCK(ah, chan))
		    tx_lat += 22;

		sifstime *= 4;
		ack_offset = 32;
		slottime = 21;
	} else {
		if (AR_SREV_9287(ah) && AR_SREV_9287_13_OR_LATER(ah)) {
			eifs = AR_D_GBL_IFS_EIFS_ASYNC_FIFO;
			reg = AR_USEC_ASYNC_FIFO;
		} else {
			eifs = REG_READ(ah, AR_D_GBL_IFS_EIFS)/
				common->clockrate;
			reg = REG_READ(ah, AR_USEC);
		}
		rx_lat = MS(reg, AR_USEC_RX_LAT);
		tx_lat = MS(reg, AR_USEC_TX_LAT);

		slottime = ah->slottime;
	}

	/* As defined by IEEE 802.11-2007 17.3.8.6 */
	acktimeout = slottime + sifstime + 3 * ah->coverage_class + ack_offset;
	ctstimeout = acktimeout;

	/*
	 * Workaround for early ACK timeouts, add an offset to match the
	 * initval's 64us ack timeout value. Use 48us for the CTS timeout.
	 * This was initially only meant to work around an issue with delayed
	 * BA frames in some implementations, but it has been found to fix ACK
	 * timeout issues in other cases as well.
	 */
	if (conf->channel && conf->channel->band == IEEE80211_BAND_2GHZ &&
	    !IS_CHAN_HALF_RATE(chan) && !IS_CHAN_QUARTER_RATE(chan)) {
		acktimeout += 64 - sifstime - ah->slottime;
		ctstimeout += 48 - sifstime - ah->slottime;
	}


	ath9k_hw_set_sifs_time(ah, sifstime);
	ath9k_hw_setslottime(ah, slottime);
	ath9k_hw_set_ack_timeout(ah, acktimeout);
	ath9k_hw_set_cts_timeout(ah, ctstimeout);
	if (ah->globaltxtimeout != (u32) -1)
		ath9k_hw_set_global_txtimeout(ah, ah->globaltxtimeout);

	REG_WRITE(ah, AR_D_GBL_IFS_EIFS, ath9k_hw_mac_to_clks(ah, eifs));
	REG_RMW(ah, AR_USEC,
		(common->clockrate - 1) |
		SM(rx_lat, AR_USEC_RX_LAT) |
		SM(tx_lat, AR_USEC_TX_LAT),
		AR_USEC_TX_LAT | AR_USEC_RX_LAT | AR_USEC_USEC);

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

	ENABLE_REGWRITE_BUFFER(ah);

	/*
	 * set AHB_MODE not to do cacheline prefetches
	*/
	if (!AR_SREV_9300_20_OR_LATER(ah))
		REG_SET_BIT(ah, AR_AHB_MODE, AR_AHB_PREFETCH_RD_EN);

	/*
	 * let mac dma reads be in 128 byte chunks
	 */
	REG_RMW(ah, AR_TXCFG, AR_TXCFG_DMASZ_128B, AR_TXCFG_DMASZ_MASK);

	REGWRITE_BUFFER_FLUSH(ah);

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
	REG_RMW(ah, AR_RXCFG, AR_RXCFG_DMASZ_128B, AR_RXCFG_DMASZ_MASK);

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

	if (AR_SREV_9300_20_OR_LATER(ah))
		ath9k_hw_reset_txstatus_ring(ah);
}

static void ath9k_hw_set_operating_mode(struct ath_hw *ah, int opmode)
{
	u32 mask = AR_STA_ID1_STA_AP | AR_STA_ID1_ADHOC;
	u32 set = AR_STA_ID1_KSRCH_MODE;

	switch (opmode) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		set |= AR_STA_ID1_ADHOC;
		REG_SET_BIT(ah, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
		break;
	case NL80211_IFTYPE_AP:
		set |= AR_STA_ID1_STA_AP;
		/* fall through */
	case NL80211_IFTYPE_STATION:
		REG_CLR_BIT(ah, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
		break;
	default:
		if (!ah->is_monitoring)
			set = 0;
		break;
	}
	REG_RMW(ah, AR_STA_ID1, set, mask);
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
		REG_RMW_FIELD(ah, AR_RTC_DERIVED_CLK,
			      AR_RTC_DERIVED_CLK_PERIOD, 1);
		(void)REG_READ(ah, AR_RTC_DERIVED_CLK);
	}

	ENABLE_REGWRITE_BUFFER(ah);

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		REG_WRITE(ah, AR_WA, ah->WARegVal);
		udelay(10);
	}

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

	if (AR_SREV_9330(ah)) {
		int npend = 0;
		int i;

		/* AR9330 WAR:
		 * call external reset function to reset WMAC if:
		 * - doing a cold reset
		 * - we have pending frames in the TX queues
		 */

		for (i = 0; i < AR_NUM_QCU; i++) {
			npend = ath9k_hw_numtxpending(ah, i);
			if (npend)
				break;
		}

		if (ah->external_reset &&
		    (npend || type == ATH9K_RESET_COLD)) {
			int reset_err = 0;

			ath_dbg(ath9k_hw_common(ah), RESET,
				"reset MAC via external reset\n");

			reset_err = ah->external_reset();
			if (reset_err) {
				ath_err(ath9k_hw_common(ah),
					"External reset failed, err=%d\n",
					reset_err);
				return false;
			}

			REG_WRITE(ah, AR_RTC_RESET, 1);
		}
	}

	if (ath9k_hw_mci_is_enabled(ah))
		ar9003_mci_check_gpm_offset(ah);

	REG_WRITE(ah, AR_RTC_RC, rst_flags);

	REGWRITE_BUFFER_FLUSH(ah);

	udelay(50);

	REG_WRITE(ah, AR_RTC_RC, 0);
	if (!ath9k_hw_wait(ah, AR_RTC_RC, AR_RTC_RC_M, 0, AH_WAIT_TIMEOUT)) {
		ath_dbg(ath9k_hw_common(ah), RESET, "RTC stuck in MAC reset\n");
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

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		REG_WRITE(ah, AR_WA, ah->WARegVal);
		udelay(10);
	}

	REG_WRITE(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN |
		  AR_RTC_FORCE_WAKE_ON_INT);

	if (!AR_SREV_9100(ah) && !AR_SREV_9300_20_OR_LATER(ah))
		REG_WRITE(ah, AR_RC, AR_RC_AHB);

	REG_WRITE(ah, AR_RTC_RESET, 0);

	REGWRITE_BUFFER_FLUSH(ah);

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
		ath_dbg(ath9k_hw_common(ah), RESET, "RTC not waking up\n");
		return false;
	}

	return ath9k_hw_set_reset(ah, ATH9K_RESET_WARM);
}

static bool ath9k_hw_set_reset_reg(struct ath_hw *ah, u32 type)
{
	bool ret = false;

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		REG_WRITE(ah, AR_WA, ah->WARegVal);
		udelay(10);
	}

	REG_WRITE(ah, AR_RTC_FORCE_WAKE,
		  AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);

	switch (type) {
	case ATH9K_RESET_POWER_ON:
		ret = ath9k_hw_set_reset_power_on(ah);
		break;
	case ATH9K_RESET_WARM:
	case ATH9K_RESET_COLD:
		ret = ath9k_hw_set_reset(ah, type);
		break;
	default:
		break;
	}

	return ret;
}

static bool ath9k_hw_chip_reset(struct ath_hw *ah,
				struct ath9k_channel *chan)
{
	int reset_type = ATH9K_RESET_WARM;

	if (AR_SREV_9280(ah)) {
		if (ah->eep_ops->get_eeprom(ah, EEP_OL_PWRCTRL))
			reset_type = ATH9K_RESET_POWER_ON;
		else
			reset_type = ATH9K_RESET_COLD;
	}

	if (!ath9k_hw_set_reset_reg(ah, reset_type))
		return false;

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE))
		return false;

	ah->chip_fullsleep = false;

	if (AR_SREV_9330(ah))
		ar9003_hw_internal_regulator_apply(ah);
	ath9k_hw_init_pll(ah, chan);
	ath9k_hw_set_rfmode(ah, chan);

	return true;
}

static bool ath9k_hw_channel_change(struct ath_hw *ah,
				    struct ath9k_channel *chan)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 qnum;
	int r;
	bool edma = !!(ah->caps.hw_caps & ATH9K_HW_CAP_EDMA);
	bool band_switch, mode_diff;
	u8 ini_reloaded;

	band_switch = (chan->channelFlags & (CHANNEL_2GHZ | CHANNEL_5GHZ)) !=
		      (ah->curchan->channelFlags & (CHANNEL_2GHZ |
						    CHANNEL_5GHZ));
	mode_diff = (chan->chanmode != ah->curchan->chanmode);

	for (qnum = 0; qnum < AR_NUM_QCU; qnum++) {
		if (ath9k_hw_numtxpending(ah, qnum)) {
			ath_dbg(common, QUEUE,
				"Transmit frames pending on queue %d\n", qnum);
			return false;
		}
	}

	if (!ath9k_hw_rfbus_req(ah)) {
		ath_err(common, "Could not kill baseband RX\n");
		return false;
	}

	if (edma && (band_switch || mode_diff)) {
		ath9k_hw_mark_phy_inactive(ah);
		udelay(5);

		ath9k_hw_init_pll(ah, NULL);

		if (ath9k_hw_fast_chan_change(ah, chan, &ini_reloaded)) {
			ath_err(common, "Failed to do fast channel change\n");
			return false;
		}
	}

	ath9k_hw_set_channel_regs(ah, chan);

	r = ath9k_hw_rf_set_freq(ah, chan);
	if (r) {
		ath_err(common, "Failed to set channel\n");
		return false;
	}
	ath9k_hw_set_clockrate(ah);
	ath9k_hw_apply_txpower(ah, chan, false);
	ath9k_hw_rfbus_done(ah);

	if (IS_CHAN_OFDM(chan) || IS_CHAN_HT(chan))
		ath9k_hw_set_delta_slope(ah, chan);

	ath9k_hw_spur_mitigate_freq(ah, chan);

	if (edma && (band_switch || mode_diff)) {
		ah->ah_flags |= AH_FASTCC;
		if (band_switch || ini_reloaded)
			ah->eep_ops->set_board_values(ah, chan);

		ath9k_hw_init_bb(ah, chan);

		if (band_switch || ini_reloaded)
			ath9k_hw_init_cal(ah, chan);
		ah->ah_flags &= ~AH_FASTCC;
	}

	return true;
}

static void ath9k_hw_apply_gpio_override(struct ath_hw *ah)
{
	u32 gpio_mask = ah->gpio_mask;
	int i;

	for (i = 0; gpio_mask; i++, gpio_mask >>= 1) {
		if (!(gpio_mask & 1))
			continue;

		ath9k_hw_cfg_output(ah, i, AR_GPIO_OUTPUT_MUX_AS_OUTPUT);
		ath9k_hw_set_gpio(ah, i, !!(ah->gpio_val & BIT(i)));
	}
}

static bool ath9k_hw_check_dcs(u32 dma_dbg, u32 num_dcu_states,
			       int *hang_state, int *hang_pos)
{
	static u32 dcu_chain_state[] = {5, 6, 9}; /* DCU chain stuck states */
	u32 chain_state, dcs_pos, i;

	for (dcs_pos = 0; dcs_pos < num_dcu_states; dcs_pos++) {
		chain_state = (dma_dbg >> (5 * dcs_pos)) & 0x1f;
		for (i = 0; i < 3; i++) {
			if (chain_state == dcu_chain_state[i]) {
				*hang_state = chain_state;
				*hang_pos = dcs_pos;
				return true;
			}
		}
	}
	return false;
}

#define DCU_COMPLETE_STATE        1
#define DCU_COMPLETE_STATE_MASK 0x3
#define NUM_STATUS_READS         50
static bool ath9k_hw_detect_mac_hang(struct ath_hw *ah)
{
	u32 chain_state, comp_state, dcs_reg = AR_DMADBG_4;
	u32 i, hang_pos, hang_state, num_state = 6;

	comp_state = REG_READ(ah, AR_DMADBG_6);

	if ((comp_state & DCU_COMPLETE_STATE_MASK) != DCU_COMPLETE_STATE) {
		ath_dbg(ath9k_hw_common(ah), RESET,
			"MAC Hang signature not found at DCU complete\n");
		return false;
	}

	chain_state = REG_READ(ah, dcs_reg);
	if (ath9k_hw_check_dcs(chain_state, num_state, &hang_state, &hang_pos))
		goto hang_check_iter;

	dcs_reg = AR_DMADBG_5;
	num_state = 4;
	chain_state = REG_READ(ah, dcs_reg);
	if (ath9k_hw_check_dcs(chain_state, num_state, &hang_state, &hang_pos))
		goto hang_check_iter;

	ath_dbg(ath9k_hw_common(ah), RESET,
		"MAC Hang signature 1 not found\n");
	return false;

hang_check_iter:
	ath_dbg(ath9k_hw_common(ah), RESET,
		"DCU registers: chain %08x complete %08x Hang: state %d pos %d\n",
		chain_state, comp_state, hang_state, hang_pos);

	for (i = 0; i < NUM_STATUS_READS; i++) {
		chain_state = REG_READ(ah, dcs_reg);
		chain_state = (chain_state >> (5 * hang_pos)) & 0x1f;
		comp_state = REG_READ(ah, AR_DMADBG_6);

		if (((comp_state & DCU_COMPLETE_STATE_MASK) !=
					DCU_COMPLETE_STATE) ||
		    (chain_state != hang_state))
			return false;
	}

	ath_dbg(ath9k_hw_common(ah), RESET, "MAC Hang signature 1 found\n");

	return true;
}

bool ath9k_hw_check_alive(struct ath_hw *ah)
{
	int count = 50;
	u32 reg;

	if (AR_SREV_9300(ah))
		return !ath9k_hw_detect_mac_hang(ah);

	if (AR_SREV_9285_12_OR_LATER(ah))
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

/*
 * Fast channel change:
 * (Change synthesizer based on channel freq without resetting chip)
 *
 * Don't do FCC when
 *   - Flag is not set
 *   - Chip is just coming out of full sleep
 *   - Channel to be set is same as current channel
 *   - Channel flags are different, (eg.,moving from 2GHz to 5GHz channel)
 */
static int ath9k_hw_do_fastcc(struct ath_hw *ah, struct ath9k_channel *chan)
{
	struct ath_common *common = ath9k_hw_common(ah);
	int ret;

	if (AR_SREV_9280(ah) && common->bus_ops->ath_bus_type == ATH_PCI)
		goto fail;

	if (ah->chip_fullsleep)
		goto fail;

	if (!ah->curchan)
		goto fail;

	if (chan->channel == ah->curchan->channel)
		goto fail;

	if ((ah->curchan->channelFlags | chan->channelFlags) &
	    (CHANNEL_HALF | CHANNEL_QUARTER))
		goto fail;

	if ((chan->channelFlags & CHANNEL_ALL) !=
	    (ah->curchan->channelFlags & CHANNEL_ALL))
		goto fail;

	if (!ath9k_hw_check_alive(ah))
		goto fail;

	/*
	 * For AR9462, make sure that calibration data for
	 * re-using are present.
	 */
	if (AR_SREV_9462(ah) && (ah->caldata &&
				 (!ah->caldata->done_txiqcal_once ||
				  !ah->caldata->done_txclcal_once ||
				  !ah->caldata->rtt_done)))
		goto fail;

	ath_dbg(common, RESET, "FastChannelChange for %d -> %d\n",
		ah->curchan->channel, chan->channel);

	ret = ath9k_hw_channel_change(ah, chan);
	if (!ret)
		goto fail;

	if (ath9k_hw_mci_is_enabled(ah))
		ar9003_mci_2g5g_switch(ah, false);

	ath9k_hw_loadnf(ah, ah->curchan);
	ath9k_hw_start_nfcal(ah, true);

	if (AR_SREV_9271(ah))
		ar9002_hw_load_ani_reg(ah, chan);

	return 0;
fail:
	return -EINVAL;
}

int ath9k_hw_reset(struct ath_hw *ah, struct ath9k_channel *chan,
		   struct ath9k_hw_cal_data *caldata, bool fastcc)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 saveLedState;
	u32 saveDefAntenna;
	u32 macStaId1;
	u64 tsf = 0;
	int i, r;
	bool start_mci_reset = false;
	bool save_fullsleep = ah->chip_fullsleep;

	if (ath9k_hw_mci_is_enabled(ah)) {
		start_mci_reset = ar9003_mci_start_reset(ah, chan);
		if (start_mci_reset)
			return 0;
	}

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE))
		return -EIO;

	if (ah->curchan && !ah->chip_fullsleep)
		ath9k_hw_getnf(ah, ah->curchan);

	ah->caldata = caldata;
	if (caldata &&
	    (chan->channel != caldata->channel ||
	     (chan->channelFlags & ~CHANNEL_CW_INT) !=
	     (caldata->channelFlags & ~CHANNEL_CW_INT))) {
		/* Operating channel changed, reset channel calibration data */
		memset(caldata, 0, sizeof(*caldata));
		ath9k_init_nfcal_hist_buffer(ah, chan);
	} else if (caldata) {
		caldata->paprd_packet_sent = false;
	}
	ah->noise = ath9k_hw_getchan_noise(ah, chan);

	if (fastcc) {
		r = ath9k_hw_do_fastcc(ah, chan);
		if (!r)
			return r;
	}

	if (ath9k_hw_mci_is_enabled(ah))
		ar9003_mci_stop_bt(ah, save_fullsleep);

	saveDefAntenna = REG_READ(ah, AR_DEF_ANTENNA);
	if (saveDefAntenna == 0)
		saveDefAntenna = 1;

	macStaId1 = REG_READ(ah, AR_STA_ID1) & AR_STA_ID1_BASE_RATE_11B;

	/* For chips on which RTC reset is done, save TSF before it gets cleared */
	if (AR_SREV_9100(ah) ||
	    (AR_SREV_9280(ah) && ah->eep_ops->get_eeprom(ah, EEP_OL_PWRCTRL)))
		tsf = ath9k_hw_gettsf64(ah);

	saveLedState = REG_READ(ah, AR_CFG_LED) &
		(AR_CFG_LED_ASSOC_CTL | AR_CFG_LED_MODE_SEL |
		 AR_CFG_LED_BLINK_THRESH_SEL | AR_CFG_LED_BLINK_SLOW);

	ath9k_hw_mark_phy_inactive(ah);

	ah->paprd_table_write_done = false;

	/* Only required on the first reset */
	if (AR_SREV_9271(ah) && ah->htc_reset_init) {
		REG_WRITE(ah,
			  AR9271_RESET_POWER_DOWN_CONTROL,
			  AR9271_RADIO_RF_RST);
		udelay(50);
	}

	if (!ath9k_hw_chip_reset(ah, chan)) {
		ath_err(common, "Chip reset failed\n");
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
	if (tsf)
		ath9k_hw_settsf64(ah, tsf);

	if (AR_SREV_9280_20_OR_LATER(ah))
		REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL, AR_GPIO_JTAG_DISABLE);

	if (!AR_SREV_9300_20_OR_LATER(ah))
		ar9002_hw_enable_async_fifo(ah);

	r = ath9k_hw_process_ini(ah, chan);
	if (r)
		return r;

	if (ath9k_hw_mci_is_enabled(ah))
		ar9003_mci_reset(ah, false, IS_CHAN_2GHZ(chan), save_fullsleep);

	/*
	 * Some AR91xx SoC devices frequently fail to accept TSF writes
	 * right after the chip reset. When that happens, write a new
	 * value after the initvals have been applied, with an offset
	 * based on measured time difference
	 */
	if (AR_SREV_9100(ah) && (ath9k_hw_gettsf64(ah) < tsf)) {
		tsf += 1500;
		ath9k_hw_settsf64(ah, tsf);
	}

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

	ath9k_hw_set_operating_mode(ah, ah->opmode);

	r = ath9k_hw_rf_set_freq(ah, chan);
	if (r)
		return r;

	ath9k_hw_set_clockrate(ah);

	ENABLE_REGWRITE_BUFFER(ah);

	for (i = 0; i < AR_NUM_DCU; i++)
		REG_WRITE(ah, AR_DQCUMASK(i), 1 << i);

	REGWRITE_BUFFER_FLUSH(ah);

	ah->intr_txqs = 0;
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		ath9k_hw_resettxqueue(ah, i);

	ath9k_hw_init_interrupt_masks(ah, ah->opmode);
	ath9k_hw_ani_cache_ini_regs(ah);
	ath9k_hw_init_qos(ah);

	if (ah->caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		ath9k_hw_cfg_gpio_input(ah, ah->rfkill_gpio);

	ath9k_hw_init_global_settings(ah);

	if (AR_SREV_9287(ah) && AR_SREV_9287_13_OR_LATER(ah)) {
		REG_SET_BIT(ah, AR_MAC_PCU_LOGIC_ANALYZER,
			    AR_MAC_PCU_LOGIC_ANALYZER_DISBUG20768);
		REG_RMW_FIELD(ah, AR_AHB_MODE, AR_AHB_CUSTOM_BURST_EN,
			      AR_AHB_CUSTOM_BURST_ASYNC_FIFO_VAL);
		REG_SET_BIT(ah, AR_PCU_MISC_MODE2,
			    AR_PCU_MISC_MODE2_ENABLE_AGGWEP);
	}

	REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PRESERVE_SEQNUM);

	ath9k_hw_set_dma(ah);

	if (!ath9k_hw_mci_is_enabled(ah))
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

	if (caldata) {
		caldata->done_txiqcal_once = false;
		caldata->done_txclcal_once = false;
	}
	if (!ath9k_hw_init_cal(ah, chan))
		return -EIO;

	if (ath9k_hw_mci_is_enabled(ah) && ar9003_mci_end_reset(ah, chan, caldata))
		return -EIO;

	ENABLE_REGWRITE_BUFFER(ah);

	ath9k_hw_restore_chainmask(ah);
	REG_WRITE(ah, AR_CFG_LED, saveLedState | AR_CFG_SCLK_32KHZ);

	REGWRITE_BUFFER_FLUSH(ah);

	/*
	 * For big endian systems turn on swapping for descriptors
	 */
	if (AR_SREV_9100(ah)) {
		u32 mask;
		mask = REG_READ(ah, AR_CFG);
		if (mask & (AR_CFG_SWRB | AR_CFG_SWTB | AR_CFG_SWRG)) {
			ath_dbg(common, RESET, "CFG Byte Swap Set 0x%x\n",
				mask);
		} else {
			mask =
				INIT_CONFIG_STATUS | AR_CFG_SWRB | AR_CFG_SWTB;
			REG_WRITE(ah, AR_CFG, mask);
			ath_dbg(common, RESET, "Setting CFG 0x%x\n",
				REG_READ(ah, AR_CFG));
		}
	} else {
		if (common->bus_ops->ath_bus_type == ATH_USB) {
			/* Configure AR9271 target WLAN */
			if (AR_SREV_9271(ah))
				REG_WRITE(ah, AR_CFG, AR_CFG_SWRB | AR_CFG_SWTB);
			else
				REG_WRITE(ah, AR_CFG, AR_CFG_SWTD | AR_CFG_SWRD);
		}
#ifdef __BIG_ENDIAN
		else if (AR_SREV_9330(ah) || AR_SREV_9340(ah) ||
			 AR_SREV_9550(ah))
			REG_RMW(ah, AR_CFG, AR_CFG_SWRB | AR_CFG_SWTB, 0);
		else
			REG_WRITE(ah, AR_CFG, AR_CFG_SWTD | AR_CFG_SWRD);
#endif
	}

	if (ath9k_hw_btcoex_is_enabled(ah))
		ath9k_hw_btcoex_enable(ah);

	if (ath9k_hw_mci_is_enabled(ah))
		ar9003_mci_check_bt(ah);

	ath9k_hw_loadnf(ah, chan);
	ath9k_hw_start_nfcal(ah, true);

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		ar9003_hw_bb_watchdog_config(ah);

		ar9003_hw_disable_phy_restart(ah);
	}

	ath9k_hw_apply_gpio_override(ah);

	if (AR_SREV_9565(ah) && ah->shared_chain_lnadiv)
		REG_SET_BIT(ah, AR_BTCOEX_WL_LNADIV, AR_BTCOEX_WL_LNADIV_FORCE_ON);

	return 0;
}
EXPORT_SYMBOL(ath9k_hw_reset);

/******************************/
/* Power Management (Chipset) */
/******************************/

/*
 * Notify Power Mgt is disabled in self-generated frames.
 * If requested, force chip to sleep.
 */
static void ath9k_set_power_sleep(struct ath_hw *ah)
{
	REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);

	if (AR_SREV_9462(ah) || AR_SREV_9565(ah)) {
		REG_CLR_BIT(ah, AR_TIMER_MODE, 0xff);
		REG_CLR_BIT(ah, AR_NDP2_TIMER_MODE, 0xff);
		REG_CLR_BIT(ah, AR_SLP32_INC, 0xfffff);
		/* xxx Required for WLAN only case ? */
		REG_WRITE(ah, AR_MCI_INTERRUPT_RX_MSG_EN, 0);
		udelay(100);
	}

	/*
	 * Clear the RTC force wake bit to allow the
	 * mac to go to sleep.
	 */
	REG_CLR_BIT(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN);

	if (ath9k_hw_mci_is_enabled(ah))
		udelay(100);

	if (!AR_SREV_9100(ah) && !AR_SREV_9300_20_OR_LATER(ah))
		REG_WRITE(ah, AR_RC, AR_RC_AHB | AR_RC_HOSTIF);

	/* Shutdown chip. Active low */
	if (!AR_SREV_5416(ah) && !AR_SREV_9271(ah)) {
		REG_CLR_BIT(ah, AR_RTC_RESET, AR_RTC_RESET_EN);
		udelay(2);
	}

	/* Clear Bit 14 of AR_WA after putting chip into Full Sleep mode. */
	if (AR_SREV_9300_20_OR_LATER(ah))
		REG_WRITE(ah, AR_WA, ah->WARegVal & ~AR_WA_D3_L1_DISABLE);
}

/*
 * Notify Power Management is enabled in self-generating
 * frames. If request, set power mode of chip to
 * auto/normal.  Duration in units of 128us (1/8 TU).
 */
static void ath9k_set_power_network_sleep(struct ath_hw *ah)
{
	struct ath9k_hw_capabilities *pCap = &ah->caps;

	REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);

	if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP)) {
		/* Set WakeOnInterrupt bit; clear ForceWake bit */
		REG_WRITE(ah, AR_RTC_FORCE_WAKE,
			  AR_RTC_FORCE_WAKE_ON_INT);
	} else {

		/* When chip goes into network sleep, it could be waken
		 * up by MCI_INT interrupt caused by BT's HW messages
		 * (LNA_xxx, CONT_xxx) which chould be in a very fast
		 * rate (~100us). This will cause chip to leave and
		 * re-enter network sleep mode frequently, which in
		 * consequence will have WLAN MCI HW to generate lots of
		 * SYS_WAKING and SYS_SLEEPING messages which will make
		 * BT CPU to busy to process.
		 */
		if (ath9k_hw_mci_is_enabled(ah))
			REG_CLR_BIT(ah, AR_MCI_INTERRUPT_RX_MSG_EN,
				    AR_MCI_INTERRUPT_RX_HW_MSG_MASK);
		/*
		 * Clear the RTC force wake bit to allow the
		 * mac to go to sleep.
		 */
		REG_CLR_BIT(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN);

		if (ath9k_hw_mci_is_enabled(ah))
			udelay(30);
	}

	/* Clear Bit 14 of AR_WA after putting chip into Net Sleep mode. */
	if (AR_SREV_9300_20_OR_LATER(ah))
		REG_WRITE(ah, AR_WA, ah->WARegVal & ~AR_WA_D3_L1_DISABLE);
}

static bool ath9k_hw_set_power_awake(struct ath_hw *ah)
{
	u32 val;
	int i;

	/* Set Bits 14 and 17 of AR_WA before powering on the chip. */
	if (AR_SREV_9300_20_OR_LATER(ah)) {
		REG_WRITE(ah, AR_WA, ah->WARegVal);
		udelay(10);
	}

	if ((REG_READ(ah, AR_RTC_STATUS) &
	     AR_RTC_STATUS_M) == AR_RTC_STATUS_SHUTDOWN) {
		if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_POWER_ON)) {
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

	if (ath9k_hw_mci_is_enabled(ah))
		ar9003_mci_set_power_awake(ah);

	for (i = POWER_UP_TIME / 50; i > 0; i--) {
		val = REG_READ(ah, AR_RTC_STATUS) & AR_RTC_STATUS_M;
		if (val == AR_RTC_STATUS_ON)
			break;
		udelay(50);
		REG_SET_BIT(ah, AR_RTC_FORCE_WAKE,
			    AR_RTC_FORCE_WAKE_EN);
	}
	if (i == 0) {
		ath_err(ath9k_hw_common(ah),
			"Failed to wakeup in %uus\n",
			POWER_UP_TIME / 20);
		return false;
	}

	REG_CLR_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);

	return true;
}

bool ath9k_hw_setpower(struct ath_hw *ah, enum ath9k_power_mode mode)
{
	struct ath_common *common = ath9k_hw_common(ah);
	int status = true;
	static const char *modes[] = {
		"AWAKE",
		"FULL-SLEEP",
		"NETWORK SLEEP",
		"UNDEFINED"
	};

	if (ah->power_mode == mode)
		return status;

	ath_dbg(common, RESET, "%s -> %s\n",
		modes[ah->power_mode], modes[mode]);

	switch (mode) {
	case ATH9K_PM_AWAKE:
		status = ath9k_hw_set_power_awake(ah);
		break;
	case ATH9K_PM_FULL_SLEEP:
		if (ath9k_hw_mci_is_enabled(ah))
			ar9003_mci_set_full_sleep(ah);

		ath9k_set_power_sleep(ah);
		ah->chip_fullsleep = true;
		break;
	case ATH9K_PM_NETWORK_SLEEP:
		ath9k_set_power_network_sleep(ah);
		break;
	default:
		ath_err(common, "Unknown power mode %u\n", mode);
		return false;
	}
	ah->power_mode = mode;

	/*
	 * XXX: If this warning never comes up after a while then
	 * simply keep the ATH_DBG_WARN_ON_ONCE() but make
	 * ath9k_hw_setpower() return type void.
	 */

	if (!(ah->ah_flags & AH_UNPLUGGED))
		ATH_DBG_WARN_ON_ONCE(!status);

	return status;
}
EXPORT_SYMBOL(ath9k_hw_setpower);

/*******************/
/* Beacon Handling */
/*******************/

void ath9k_hw_beaconinit(struct ath_hw *ah, u32 next_beacon, u32 beacon_period)
{
	int flags = 0;

	ENABLE_REGWRITE_BUFFER(ah);

	switch (ah->opmode) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		REG_SET_BIT(ah, AR_TXCFG,
			    AR_TXCFG_ADHOC_BEACON_ATIM_TX_POLICY);
		REG_WRITE(ah, AR_NEXT_NDP_TIMER, next_beacon +
			  TU_TO_USEC(ah->atim_window ? ah->atim_window : 1));
		flags |= AR_NDP_TIMER_EN;
	case NL80211_IFTYPE_AP:
		REG_WRITE(ah, AR_NEXT_TBTT_TIMER, next_beacon);
		REG_WRITE(ah, AR_NEXT_DMA_BEACON_ALERT, next_beacon -
			  TU_TO_USEC(ah->config.dma_beacon_response_time));
		REG_WRITE(ah, AR_NEXT_SWBA, next_beacon -
			  TU_TO_USEC(ah->config.sw_beacon_response_time));
		flags |=
			AR_TBTT_TIMER_EN | AR_DBA_TIMER_EN | AR_SWBA_TIMER_EN;
		break;
	default:
		ath_dbg(ath9k_hw_common(ah), BEACON,
			"%s: unsupported opmode: %d\n", __func__, ah->opmode);
		return;
		break;
	}

	REG_WRITE(ah, AR_BEACON_PERIOD, beacon_period);
	REG_WRITE(ah, AR_DMA_BEACON_PERIOD, beacon_period);
	REG_WRITE(ah, AR_SWBA_PERIOD, beacon_period);
	REG_WRITE(ah, AR_NDP_PERIOD, beacon_period);

	REGWRITE_BUFFER_FLUSH(ah);

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
		  TU_TO_USEC(bs->bs_intval));
	REG_WRITE(ah, AR_DMA_BEACON_PERIOD,
		  TU_TO_USEC(bs->bs_intval));

	REGWRITE_BUFFER_FLUSH(ah);

	REG_RMW_FIELD(ah, AR_RSSI_THR,
		      AR_RSSI_THR_BM_THR, bs->bs_bmissthreshold);

	beaconintval = bs->bs_intval;

	if (bs->bs_sleepduration > beaconintval)
		beaconintval = bs->bs_sleepduration;

	dtimperiod = bs->bs_dtimperiod;
	if (bs->bs_sleepduration > dtimperiod)
		dtimperiod = bs->bs_sleepduration;

	if (beaconintval == dtimperiod)
		nextTbtt = bs->bs_nextdtim;
	else
		nextTbtt = bs->bs_nexttbtt;

	ath_dbg(common, BEACON, "next DTIM %d\n", bs->bs_nextdtim);
	ath_dbg(common, BEACON, "next beacon %d\n", nextTbtt);
	ath_dbg(common, BEACON, "beacon period %d\n", beaconintval);
	ath_dbg(common, BEACON, "DTIM period %d\n", dtimperiod);

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

static u8 fixup_chainmask(u8 chip_chainmask, u8 eeprom_chainmask)
{
	eeprom_chainmask &= chip_chainmask;
	if (eeprom_chainmask)
		return eeprom_chainmask;
	else
		return chip_chainmask;
}

/**
 * ath9k_hw_dfs_tested - checks if DFS has been tested with used chipset
 * @ah: the atheros hardware data structure
 *
 * We enable DFS support upstream on chipsets which have passed a series
 * of tests. The testing requirements are going to be documented. Desired
 * test requirements are documented at:
 *
 * http://wireless.kernel.org/en/users/Drivers/ath9k/dfs
 *
 * Once a new chipset gets properly tested an individual commit can be used
 * to document the testing for DFS for that chipset.
 */
static bool ath9k_hw_dfs_tested(struct ath_hw *ah)
{

	switch (ah->hw_version.macVersion) {
	/* AR9580 will likely be our first target to get testing on */
	case AR_SREV_VERSION_9580:
	default:
		return false;
	}
}

int ath9k_hw_fill_cap_info(struct ath_hw *ah)
{
	struct ath9k_hw_capabilities *pCap = &ah->caps;
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	struct ath_common *common = ath9k_hw_common(ah);
	unsigned int chip_chainmask;

	u16 eeval;
	u8 ant_div_ctl1, tx_chainmask, rx_chainmask;

	eeval = ah->eep_ops->get_eeprom(ah, EEP_REG_0);
	regulatory->current_rd = eeval;

	if (ah->opmode != NL80211_IFTYPE_AP &&
	    ah->hw_version.subvendorid == AR_SUBVENDOR_ID_NEW_A) {
		if (regulatory->current_rd == 0x64 ||
		    regulatory->current_rd == 0x65)
			regulatory->current_rd += 5;
		else if (regulatory->current_rd == 0x41)
			regulatory->current_rd = 0x43;
		ath_dbg(common, REGULATORY, "regdomain mapped to 0x%x\n",
			regulatory->current_rd);
	}

	eeval = ah->eep_ops->get_eeprom(ah, EEP_OP_MODE);
	if ((eeval & (AR5416_OPFLAGS_11G | AR5416_OPFLAGS_11A)) == 0) {
		ath_err(common,
			"no band has been marked as supported in EEPROM\n");
		return -EINVAL;
	}

	if (eeval & AR5416_OPFLAGS_11A)
		pCap->hw_caps |= ATH9K_HW_CAP_5GHZ;

	if (eeval & AR5416_OPFLAGS_11G)
		pCap->hw_caps |= ATH9K_HW_CAP_2GHZ;

	if (AR_SREV_9485(ah) ||
	    AR_SREV_9285(ah) ||
	    AR_SREV_9330(ah) ||
	    AR_SREV_9565(ah))
		chip_chainmask = 1;
	else if (AR_SREV_9462(ah))
		chip_chainmask = 3;
	else if (!AR_SREV_9280_20_OR_LATER(ah))
		chip_chainmask = 7;
	else if (!AR_SREV_9300_20_OR_LATER(ah) || AR_SREV_9340(ah))
		chip_chainmask = 3;
	else
		chip_chainmask = 7;

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
	else if (AR_SREV_9100(ah))
		pCap->rx_chainmask = 0x7;
	else
		/* Use rx_chainmask from EEPROM. */
		pCap->rx_chainmask = ah->eep_ops->get_eeprom(ah, EEP_RX_MASK);

	pCap->tx_chainmask = fixup_chainmask(chip_chainmask, pCap->tx_chainmask);
	pCap->rx_chainmask = fixup_chainmask(chip_chainmask, pCap->rx_chainmask);
	ah->txchainmask = pCap->tx_chainmask;
	ah->rxchainmask = pCap->rx_chainmask;

	ah->misc_mode |= AR_PCU_MIC_NEW_LOC_ENA;

	/* enable key search for every frame in an aggregate */
	if (AR_SREV_9300_20_OR_LATER(ah))
		ah->misc_mode |= AR_PCU_ALWAYS_PERFORM_KEYSEARCH;

	common->crypt_caps |= ATH_CRYPT_CAP_CIPHER_AESCCM;

	if (ah->hw_version.devid != AR2427_DEVID_PCIE)
		pCap->hw_caps |= ATH9K_HW_CAP_HT;
	else
		pCap->hw_caps &= ~ATH9K_HW_CAP_HT;

	if (AR_SREV_9271(ah))
		pCap->num_gpio_pins = AR9271_NUM_GPIO;
	else if (AR_DEVID_7010(ah))
		pCap->num_gpio_pins = AR7010_NUM_GPIO;
	else if (AR_SREV_9300_20_OR_LATER(ah))
		pCap->num_gpio_pins = AR9300_NUM_GPIO;
	else if (AR_SREV_9287_11_OR_LATER(ah))
		pCap->num_gpio_pins = AR9287_NUM_GPIO;
	else if (AR_SREV_9285_12_OR_LATER(ah))
		pCap->num_gpio_pins = AR9285_NUM_GPIO;
	else if (AR_SREV_9280_20_OR_LATER(ah))
		pCap->num_gpio_pins = AR928X_NUM_GPIO;
	else
		pCap->num_gpio_pins = AR_NUM_GPIO;

	if (AR_SREV_9160_10_OR_LATER(ah) || AR_SREV_9100(ah))
		pCap->rts_aggr_limit = ATH_AMPDU_LIMIT_MAX;
	else
		pCap->rts_aggr_limit = (8 * 1024);

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
	if (AR_SREV_9271(ah) || AR_SREV_9300_20_OR_LATER(ah))
		pCap->hw_caps |= ATH9K_HW_CAP_AUTOSLEEP;
	else
		pCap->hw_caps &= ~ATH9K_HW_CAP_AUTOSLEEP;

	if (AR_SREV_9280(ah) || AR_SREV_9285(ah))
		pCap->hw_caps &= ~ATH9K_HW_CAP_4KB_SPLITTRANS;
	else
		pCap->hw_caps |= ATH9K_HW_CAP_4KB_SPLITTRANS;

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		pCap->hw_caps |= ATH9K_HW_CAP_EDMA | ATH9K_HW_CAP_FASTCLOCK;
		if (!AR_SREV_9330(ah) && !AR_SREV_9485(ah) && !AR_SREV_9565(ah))
			pCap->hw_caps |= ATH9K_HW_CAP_LDPC;

		pCap->rx_hp_qdepth = ATH9K_HW_RX_HP_QDEPTH;
		pCap->rx_lp_qdepth = ATH9K_HW_RX_LP_QDEPTH;
		pCap->rx_status_len = sizeof(struct ar9003_rxs);
		pCap->tx_desc_len = sizeof(struct ar9003_txc);
		pCap->txs_len = sizeof(struct ar9003_txs);
	} else {
		pCap->tx_desc_len = sizeof(struct ath_desc);
		if (AR_SREV_9280_20(ah))
			pCap->hw_caps |= ATH9K_HW_CAP_FASTCLOCK;
	}

	if (AR_SREV_9300_20_OR_LATER(ah))
		pCap->hw_caps |= ATH9K_HW_CAP_RAC_SUPPORTED;

	if (AR_SREV_9300_20_OR_LATER(ah))
		ah->ent_mode = REG_READ(ah, AR_ENT_OTP);

	if (AR_SREV_9287_11_OR_LATER(ah) || AR_SREV_9271(ah))
		pCap->hw_caps |= ATH9K_HW_CAP_SGI_20;

	if (AR_SREV_9285(ah))
		if (ah->eep_ops->get_eeprom(ah, EEP_MODAL_VER) >= 3) {
			ant_div_ctl1 =
				ah->eep_ops->get_eeprom(ah, EEP_ANT_DIV_CTL1);
			if ((ant_div_ctl1 & 0x1) && ((ant_div_ctl1 >> 3) & 0x1))
				pCap->hw_caps |= ATH9K_HW_CAP_ANT_DIV_COMB;
		}
	if (AR_SREV_9300_20_OR_LATER(ah)) {
		if (ah->eep_ops->get_eeprom(ah, EEP_CHAIN_MASK_REDUCE))
			pCap->hw_caps |= ATH9K_HW_CAP_APM;
	}


	if (AR_SREV_9330(ah) || AR_SREV_9485(ah) || AR_SREV_9565(ah)) {
		ant_div_ctl1 = ah->eep_ops->get_eeprom(ah, EEP_ANT_DIV_CTL1);
		/*
		 * enable the diversity-combining algorithm only when
		 * both enable_lna_div and enable_fast_div are set
		 *		Table for Diversity
		 * ant_div_alt_lnaconf		bit 0-1
		 * ant_div_main_lnaconf		bit 2-3
		 * ant_div_alt_gaintb		bit 4
		 * ant_div_main_gaintb		bit 5
		 * enable_ant_div_lnadiv	bit 6
		 * enable_ant_fast_div		bit 7
		 */
		if ((ant_div_ctl1 >> 0x6) == 0x3)
			pCap->hw_caps |= ATH9K_HW_CAP_ANT_DIV_COMB;
	}

	if (AR_SREV_9485_10(ah)) {
		pCap->pcie_lcr_extsync_en = true;
		pCap->pcie_lcr_offset = 0x80;
	}

	if (ath9k_hw_dfs_tested(ah))
		pCap->hw_caps |= ATH9K_HW_CAP_DFS;

	tx_chainmask = pCap->tx_chainmask;
	rx_chainmask = pCap->rx_chainmask;
	while (tx_chainmask || rx_chainmask) {
		if (tx_chainmask & BIT(0))
			pCap->max_txchains++;
		if (rx_chainmask & BIT(0))
			pCap->max_rxchains++;

		tx_chainmask >>= 1;
		rx_chainmask >>= 1;
	}

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		ah->enabled_cals |= TX_IQ_CAL;
		if (AR_SREV_9485_OR_LATER(ah))
			ah->enabled_cals |= TX_IQ_ON_AGC_CAL;
	}

	if (AR_SREV_9462(ah) || AR_SREV_9565(ah)) {
		if (!(ah->ent_mode & AR_ENT_OTP_49GHZ_DISABLE))
			pCap->hw_caps |= ATH9K_HW_CAP_MCI;

		if (AR_SREV_9462_20(ah))
			pCap->hw_caps |= ATH9K_HW_CAP_RTT;
	}


	if (AR_SREV_9280_20_OR_LATER(ah)) {
		pCap->hw_caps |= ATH9K_HW_WOW_DEVICE_CAPABLE |
				 ATH9K_HW_WOW_PATTERN_MATCH_EXACT;

		if (AR_SREV_9280(ah))
			pCap->hw_caps |= ATH9K_HW_WOW_PATTERN_MATCH_DWORD;
	}

	return 0;
}

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

	if (AR_DEVID_7010(ah)) {
		gpio_shift = gpio;
		REG_RMW(ah, AR7010_GPIO_OE,
			(AR7010_GPIO_OE_AS_INPUT << gpio_shift),
			(AR7010_GPIO_OE_MASK << gpio_shift));
		return;
	}

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

	if (AR_DEVID_7010(ah)) {
		u32 val;
		val = REG_READ(ah, AR7010_GPIO_IN);
		return (MS(val, AR7010_GPIO_IN_VAL) & AR_GPIO_BIT(gpio)) == 0;
	} else if (AR_SREV_9300_20_OR_LATER(ah))
		return (MS(REG_READ(ah, AR_GPIO_IN), AR9300_GPIO_IN_VAL) &
			AR_GPIO_BIT(gpio)) != 0;
	else if (AR_SREV_9271(ah))
		return MS_REG_READ(AR9271, gpio) != 0;
	else if (AR_SREV_9287_11_OR_LATER(ah))
		return MS_REG_READ(AR9287, gpio) != 0;
	else if (AR_SREV_9285_12_OR_LATER(ah))
		return MS_REG_READ(AR9285, gpio) != 0;
	else if (AR_SREV_9280_20_OR_LATER(ah))
		return MS_REG_READ(AR928X, gpio) != 0;
	else
		return MS_REG_READ(AR, gpio) != 0;
}
EXPORT_SYMBOL(ath9k_hw_gpio_get);

void ath9k_hw_cfg_output(struct ath_hw *ah, u32 gpio,
			 u32 ah_signal_type)
{
	u32 gpio_shift;

	if (AR_DEVID_7010(ah)) {
		gpio_shift = gpio;
		REG_RMW(ah, AR7010_GPIO_OE,
			(AR7010_GPIO_OE_AS_OUTPUT << gpio_shift),
			(AR7010_GPIO_OE_MASK << gpio_shift));
		return;
	}

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
	if (AR_DEVID_7010(ah)) {
		val = val ? 0 : 1;
		REG_RMW(ah, AR7010_GPIO_OUT, ((val&1) << gpio),
			AR_GPIO_BIT(gpio));
		return;
	}

	if (AR_SREV_9271(ah))
		val = ~val;

	REG_RMW(ah, AR_GPIO_IN_OUT, ((val & 1) << gpio),
		AR_GPIO_BIT(gpio));
}
EXPORT_SYMBOL(ath9k_hw_set_gpio);

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

	if (AR_SREV_9462(ah) || AR_SREV_9565(ah))
		bits |= ATH9K_RX_FILTER_CONTROL_WRAPPER;

	REG_WRITE(ah, AR_RX_FILTER, bits);

	phybits = 0;
	if (bits & ATH9K_RX_FILTER_PHYRADAR)
		phybits |= AR_PHY_ERR_RADAR;
	if (bits & ATH9K_RX_FILTER_PHYERR)
		phybits |= AR_PHY_ERR_OFDM_TIMING | AR_PHY_ERR_CCK_TIMING;
	REG_WRITE(ah, AR_PHY_ERR, phybits);

	if (phybits)
		REG_SET_BIT(ah, AR_RXCFG, AR_RXCFG_ZLFDMA);
	else
		REG_CLR_BIT(ah, AR_RXCFG, AR_RXCFG_ZLFDMA);

	REGWRITE_BUFFER_FLUSH(ah);
}
EXPORT_SYMBOL(ath9k_hw_setrxfilter);

bool ath9k_hw_phy_disable(struct ath_hw *ah)
{
	if (ath9k_hw_mci_is_enabled(ah))
		ar9003_mci_bt_gain_ctrl(ah);

	if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_WARM))
		return false;

	ath9k_hw_init_pll(ah, NULL);
	ah->htc_reset_init = true;
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

static int get_antenna_gain(struct ath_hw *ah, struct ath9k_channel *chan)
{
	enum eeprom_param gain_param;

	if (IS_CHAN_2GHZ(chan))
		gain_param = EEP_ANTENNA_GAIN_2G;
	else
		gain_param = EEP_ANTENNA_GAIN_5G;

	return ah->eep_ops->get_eeprom(ah, gain_param);
}

void ath9k_hw_apply_txpower(struct ath_hw *ah, struct ath9k_channel *chan,
			    bool test)
{
	struct ath_regulatory *reg = ath9k_hw_regulatory(ah);
	struct ieee80211_channel *channel;
	int chan_pwr, new_pwr, max_gain;
	int ant_gain, ant_reduction = 0;

	if (!chan)
		return;

	channel = chan->chan;
	chan_pwr = min_t(int, channel->max_power * 2, MAX_RATE_POWER);
	new_pwr = min_t(int, chan_pwr, reg->power_limit);
	max_gain = chan_pwr - new_pwr + channel->max_antenna_gain * 2;

	ant_gain = get_antenna_gain(ah, chan);
	if (ant_gain > max_gain)
		ant_reduction = ant_gain - max_gain;

	ah->eep_ops->set_txpower(ah, chan,
				 ath9k_regd_get_ctl(reg, chan),
				 ant_reduction, new_pwr, test);
}

void ath9k_hw_set_txpowerlimit(struct ath_hw *ah, u32 limit, bool test)
{
	struct ath_regulatory *reg = ath9k_hw_regulatory(ah);
	struct ath9k_channel *chan = ah->curchan;
	struct ieee80211_channel *channel = chan->chan;

	reg->power_limit = min_t(u32, limit, MAX_RATE_POWER);
	if (test)
		channel->max_power = MAX_RATE_POWER / 2;

	ath9k_hw_apply_txpower(ah, chan, test);

	if (test)
		channel->max_power = DIV_ROUND_UP(reg->max_power_level, 2);
}
EXPORT_SYMBOL(ath9k_hw_set_txpowerlimit);

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
		ath_dbg(ath9k_hw_common(ah), RESET,
			"AR_SLP32_TSF_WRITE_STATUS limit exceeded\n");

	REG_WRITE(ah, AR_RESET_TSF, AR_RESET_TSF_ONCE);
}
EXPORT_SYMBOL(ath9k_hw_reset_tsf);

void ath9k_hw_set_tsfadjust(struct ath_hw *ah, bool set)
{
	if (set)
		ah->misc_mode |= AR_PCU_TX_ADD_TSF;
	else
		ah->misc_mode &= ~AR_PCU_TX_ADD_TSF;
}
EXPORT_SYMBOL(ath9k_hw_set_tsfadjust);

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
		ath_err(ath9k_hw_common(ah),
			"Failed to allocate memory for hw timer[%d]\n",
			timer_index);
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
			      u32 trig_timeout,
			      u32 timer_period)
{
	struct ath_gen_timer_table *timer_table = &ah->hw_gen_timers;
	u32 tsf, timer_next;

	BUG_ON(!timer_period);

	set_bit(timer->index, &timer_table->timer_mask.timer_bits);

	tsf = ath9k_hw_gettsf32(ah);

	timer_next = tsf + trig_timeout;

	ath_dbg(ath9k_hw_common(ah), HWTIMER,
		"current tsf %x period %x timer_next %x\n",
		tsf, timer_period, timer_next);

	/*
	 * Program generic timer registers
	 */
	REG_WRITE(ah, gen_tmr_configuration[timer->index].next_addr,
		 timer_next);
	REG_WRITE(ah, gen_tmr_configuration[timer->index].period_addr,
		  timer_period);
	REG_SET_BIT(ah, gen_tmr_configuration[timer->index].mode_addr,
		    gen_tmr_configuration[timer->index].mode_mask);

	if (AR_SREV_9462(ah) || AR_SREV_9565(ah)) {
		/*
		 * Starting from AR9462, each generic timer can select which tsf
		 * to use. But we still follow the old rule, 0 - 7 use tsf and
		 * 8 - 15  use tsf2.
		 */
		if ((timer->index < AR_GEN_TIMER_BANK_1_LEN))
			REG_CLR_BIT(ah, AR_MAC_PCU_GEN_TIMER_TSF_SEL,
				       (1 << timer->index));
		else
			REG_SET_BIT(ah, AR_MAC_PCU_GEN_TIMER_TSF_SEL,
				       (1 << timer->index));
	}

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

	if (AR_SREV_9462(ah) || AR_SREV_9565(ah)) {
		/*
		 * Need to switch back to TSF if it was using TSF2.
		 */
		if ((timer->index >= AR_GEN_TIMER_BANK_1_LEN)) {
			REG_CLR_BIT(ah, AR_MAC_PCU_GEN_TIMER_TSF_SEL,
				    (1 << timer->index));
		}
	}

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
		ath_dbg(common, HWTIMER, "TSF overflow for Gen timer %d\n",
			index);
		timer->overflow(timer->arg);
	}

	while (trigger_mask) {
		index = rightmost_index(timer_table, &trigger_mask);
		timer = timer_table->timers[index];
		BUG_ON(!timer);
		ath_dbg(common, HWTIMER,
			"Gen timer[%d] trigger\n", index);
		timer->trigger(timer->arg);
	}
}
EXPORT_SYMBOL(ath_gen_timer_isr);

/********/
/* HTC  */
/********/

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
	{ AR_SREV_VERSION_9330,         "9330" },
	{ AR_SREV_VERSION_9340,		"9340" },
	{ AR_SREV_VERSION_9485,         "9485" },
	{ AR_SREV_VERSION_9462,         "9462" },
	{ AR_SREV_VERSION_9550,         "9550" },
	{ AR_SREV_VERSION_9565,         "9565" },
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
	if (AR_SREV_9280_20_OR_LATER(ah)) {
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
