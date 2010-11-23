/*
 * Copyright (c) 2004-2008 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006-2008 Nick Kossifidis <mickflemm@gmail.com>
 * Copyright (c) 2007-2008 Matthew W. S. Bell  <mentor@madwifi.org>
 * Copyright (c) 2007-2008 Luis Rodriguez <mcgrof@winlab.rutgers.edu>
 * Copyright (c) 2007-2008 Pavel Roskin <proski@gnu.org>
 * Copyright (c) 2007-2008 Jiri Slaby <jirislaby@gmail.com>
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

/*********************************\
* Protocol Control Unit Functions *
\*********************************/

#include <asm/unaligned.h>

#include "ath5k.h"
#include "reg.h"
#include "debug.h"
#include "base.h"

/*
 * AR5212+ can use higher rates for ack transmition
 * based on current tx rate instead of the base rate.
 * It does this to better utilize channel usage.
 * This is a mapping between G rates (that cover both
 * CCK and OFDM) and ack rates that we use when setting
 * rate -> duration table. This mapping is hw-based so
 * don't change anything.
 *
 * To enable this functionality we must set
 * ah->ah_ack_bitrate_high to true else base rate is
 * used (1Mb for CCK, 6Mb for OFDM).
 */
static const unsigned int ack_rates_high[] =
/* Tx	-> ACK	*/
/* 1Mb	-> 1Mb	*/	{ 0,
/* 2MB	-> 2Mb	*/	1,
/* 5.5Mb -> 2Mb	*/	1,
/* 11Mb	-> 2Mb	*/	1,
/* 6Mb	-> 6Mb	*/	4,
/* 9Mb	-> 6Mb	*/	4,
/* 12Mb	-> 12Mb	*/	6,
/* 18Mb	-> 12Mb	*/	6,
/* 24Mb	-> 24Mb	*/	8,
/* 36Mb	-> 24Mb	*/	8,
/* 48Mb	-> 24Mb	*/	8,
/* 54Mb	-> 24Mb	*/	8 };

/*******************\
* Helper functions *
\*******************/

/**
 * ath5k_hw_get_frame_duration - Get tx time of a frame
 *
 * @ah: The &struct ath5k_hw
 * @len: Frame's length in bytes
 * @rate: The @struct ieee80211_rate
 *
 * Calculate tx duration of a frame given it's rate and length
 * It extends ieee80211_generic_frame_duration for non standard
 * bwmodes.
 */
int ath5k_hw_get_frame_duration(struct ath5k_hw *ah,
		int len, struct ieee80211_rate *rate)
{
	struct ath5k_softc *sc = ah->ah_sc;
	int sifs, preamble, plcp_bits, sym_time;
	int bitrate, bits, symbols, symbol_bits;
	int dur;

	/* Fallback */
	if (!ah->ah_bwmode) {
		dur = ieee80211_generic_frame_duration(sc->hw,
						NULL, len, rate);
		return dur;
	}

	bitrate = rate->bitrate;
	preamble = AR5K_INIT_OFDM_PREAMPLE_TIME;
	plcp_bits = AR5K_INIT_OFDM_PLCP_BITS;
	sym_time = AR5K_INIT_OFDM_SYMBOL_TIME;

	switch (ah->ah_bwmode) {
	case AR5K_BWMODE_40MHZ:
		sifs = AR5K_INIT_SIFS_TURBO;
		preamble = AR5K_INIT_OFDM_PREAMBLE_TIME_MIN;
		break;
	case AR5K_BWMODE_10MHZ:
		sifs = AR5K_INIT_SIFS_HALF_RATE;
		preamble *= 2;
		sym_time *= 2;
		break;
	case AR5K_BWMODE_5MHZ:
		sifs = AR5K_INIT_SIFS_QUARTER_RATE;
		preamble *= 4;
		sym_time *= 4;
		break;
	default:
		sifs = AR5K_INIT_SIFS_DEFAULT_BG;
		break;
	}

	bits = plcp_bits + (len << 3);
	/* Bit rate is in 100Kbits */
	symbol_bits = bitrate * sym_time;
	symbols = DIV_ROUND_UP(bits * 10, symbol_bits);

	dur = sifs + preamble + (sym_time * symbols);

	return dur;
}

/**
 * ath5k_hw_get_default_slottime - Get the default slot time for current mode
 *
 * @ah: The &struct ath5k_hw
 */
static unsigned int ath5k_hw_get_default_slottime(struct ath5k_hw *ah)
{
	struct ieee80211_channel *channel = ah->ah_current_channel;
	unsigned int slot_time;

	switch (ah->ah_bwmode) {
	case AR5K_BWMODE_40MHZ:
		slot_time = AR5K_INIT_SLOT_TIME_TURBO;
		break;
	case AR5K_BWMODE_10MHZ:
		slot_time = AR5K_INIT_SLOT_TIME_HALF_RATE;
		break;
	case AR5K_BWMODE_5MHZ:
		slot_time = AR5K_INIT_SLOT_TIME_QUARTER_RATE;
		break;
	case AR5K_BWMODE_DEFAULT:
		slot_time = AR5K_INIT_SLOT_TIME_DEFAULT;
	default:
		if (channel->hw_value & CHANNEL_CCK)
			slot_time = AR5K_INIT_SLOT_TIME_B;
		break;
	}

	return slot_time;
}

/**
 * ath5k_hw_get_default_sifs - Get the default SIFS for current mode
 *
 * @ah: The &struct ath5k_hw
 */
unsigned int ath5k_hw_get_default_sifs(struct ath5k_hw *ah)
{
	struct ieee80211_channel *channel = ah->ah_current_channel;
	unsigned int sifs;

	switch (ah->ah_bwmode) {
	case AR5K_BWMODE_40MHZ:
		sifs = AR5K_INIT_SIFS_TURBO;
		break;
	case AR5K_BWMODE_10MHZ:
		sifs = AR5K_INIT_SIFS_HALF_RATE;
		break;
	case AR5K_BWMODE_5MHZ:
		sifs = AR5K_INIT_SIFS_QUARTER_RATE;
		break;
	case AR5K_BWMODE_DEFAULT:
		sifs = AR5K_INIT_SIFS_DEFAULT_BG;
	default:
		if (channel->hw_value & CHANNEL_5GHZ)
			sifs = AR5K_INIT_SIFS_DEFAULT_A;
		break;
	}

	return sifs;
}

/**
 * ath5k_hw_update_mib_counters - Update MIB counters (mac layer statistics)
 *
 * @ah: The &struct ath5k_hw
 *
 * Reads MIB counters from PCU and updates sw statistics. Is called after a
 * MIB interrupt, because one of these counters might have reached their maximum
 * and triggered the MIB interrupt, to let us read and clear the counter.
 *
 * Is called in interrupt context!
 */
void ath5k_hw_update_mib_counters(struct ath5k_hw *ah)
{
	struct ath5k_statistics *stats = &ah->ah_sc->stats;

	/* Read-And-Clear */
	stats->ack_fail += ath5k_hw_reg_read(ah, AR5K_ACK_FAIL);
	stats->rts_fail += ath5k_hw_reg_read(ah, AR5K_RTS_FAIL);
	stats->rts_ok += ath5k_hw_reg_read(ah, AR5K_RTS_OK);
	stats->fcs_error += ath5k_hw_reg_read(ah, AR5K_FCS_FAIL);
	stats->beacons += ath5k_hw_reg_read(ah, AR5K_BEACON_CNT);
}


/******************\
* ACK/CTS Timeouts *
\******************/

/**
 * ath5k_hw_write_rate_duration - fill rate code to duration table
 *
 * @ah: the &struct ath5k_hw
 * @mode: one of enum ath5k_driver_mode
 *
 * Write the rate code to duration table upon hw reset. This is a helper for
 * ath5k_hw_pcu_init(). It seems all this is doing is setting an ACK timeout on
 * the hardware, based on current mode, for each rate. The rates which are
 * capable of short preamble (802.11b rates 2Mbps, 5.5Mbps, and 11Mbps) have
 * different rate code so we write their value twice (one for long preamble
 * and one for short).
 *
 * Note: Band doesn't matter here, if we set the values for OFDM it works
 * on both a and g modes. So all we have to do is set values for all g rates
 * that include all OFDM and CCK rates.
 *
 */
static inline void ath5k_hw_write_rate_duration(struct ath5k_hw *ah)
{
	struct ath5k_softc *sc = ah->ah_sc;
	struct ieee80211_rate *rate;
	unsigned int i;
	/* 802.11g covers both OFDM and CCK */
	u8 band = IEEE80211_BAND_2GHZ;

	/* Write rate duration table */
	for (i = 0; i < sc->sbands[band].n_bitrates; i++) {
		u32 reg;
		u16 tx_time;

		if (ah->ah_ack_bitrate_high)
			rate = &sc->sbands[band].bitrates[ack_rates_high[i]];
		/* CCK -> 1Mb */
		else if (i < 4)
			rate = &sc->sbands[band].bitrates[0];
		/* OFDM -> 6Mb */
		else
			rate = &sc->sbands[band].bitrates[4];

		/* Set ACK timeout */
		reg = AR5K_RATE_DUR(rate->hw_value);

		/* An ACK frame consists of 10 bytes. If you add the FCS,
		 * which ieee80211_generic_frame_duration() adds,
		 * its 14 bytes. Note we use the control rate and not the
		 * actual rate for this rate. See mac80211 tx.c
		 * ieee80211_duration() for a brief description of
		 * what rate we should choose to TX ACKs. */
		tx_time = ath5k_hw_get_frame_duration(ah, 10, rate);

		tx_time = le16_to_cpu(tx_time);

		ath5k_hw_reg_write(ah, tx_time, reg);

		if (!(rate->flags & IEEE80211_RATE_SHORT_PREAMBLE))
			continue;

		/*
		 * We're not distinguishing short preamble here,
		 * This is true, all we'll get is a longer value here
		 * which is not necessarilly bad. We could use
		 * export ieee80211_frame_duration() but that needs to be
		 * fixed first to be properly used by mac802111 drivers:
		 *
		 *  - remove erp stuff and let the routine figure ofdm
		 *    erp rates
		 *  - remove passing argument ieee80211_local as
		 *    drivers don't have access to it
		 *  - move drivers using ieee80211_generic_frame_duration()
		 *    to this
		 */
		ath5k_hw_reg_write(ah, tx_time,
			reg + (AR5K_SET_SHORT_PREAMBLE << 2));
	}
}

/**
 * ath5k_hw_set_ack_timeout - Set ACK timeout on PCU
 *
 * @ah: The &struct ath5k_hw
 * @timeout: Timeout in usec
 */
static int ath5k_hw_set_ack_timeout(struct ath5k_hw *ah, unsigned int timeout)
{
	if (ath5k_hw_clocktoh(ah, AR5K_REG_MS(0xffffffff, AR5K_TIME_OUT_ACK))
			<= timeout)
		return -EINVAL;

	AR5K_REG_WRITE_BITS(ah, AR5K_TIME_OUT, AR5K_TIME_OUT_ACK,
		ath5k_hw_htoclock(ah, timeout));

	return 0;
}

/**
 * ath5k_hw_set_cts_timeout - Set CTS timeout on PCU
 *
 * @ah: The &struct ath5k_hw
 * @timeout: Timeout in usec
 */
static int ath5k_hw_set_cts_timeout(struct ath5k_hw *ah, unsigned int timeout)
{
	if (ath5k_hw_clocktoh(ah, AR5K_REG_MS(0xffffffff, AR5K_TIME_OUT_CTS))
			<= timeout)
		return -EINVAL;

	AR5K_REG_WRITE_BITS(ah, AR5K_TIME_OUT, AR5K_TIME_OUT_CTS,
			ath5k_hw_htoclock(ah, timeout));

	return 0;
}


/*******************\
* RX filter Control *
\*******************/

/**
 * ath5k_hw_set_lladdr - Set station id
 *
 * @ah: The &struct ath5k_hw
 * @mac: The card's mac address
 *
 * Set station id on hw using the provided mac address
 */
int ath5k_hw_set_lladdr(struct ath5k_hw *ah, const u8 *mac)
{
	struct ath_common *common = ath5k_hw_common(ah);
	u32 low_id, high_id;
	u32 pcu_reg;

	/* Set new station ID */
	memcpy(common->macaddr, mac, ETH_ALEN);

	pcu_reg = ath5k_hw_reg_read(ah, AR5K_STA_ID1) & 0xffff0000;

	low_id = get_unaligned_le32(mac);
	high_id = get_unaligned_le16(mac + 4);

	ath5k_hw_reg_write(ah, low_id, AR5K_STA_ID0);
	ath5k_hw_reg_write(ah, pcu_reg | high_id, AR5K_STA_ID1);

	return 0;
}

/**
 * ath5k_hw_set_bssid - Set current BSSID on hw
 *
 * @ah: The &struct ath5k_hw
 *
 * Sets the current BSSID and BSSID mask we have from the
 * common struct into the hardware
 */
void ath5k_hw_set_bssid(struct ath5k_hw *ah)
{
	struct ath_common *common = ath5k_hw_common(ah);
	u16 tim_offset = 0;

	/*
	 * Set BSSID mask on 5212
	 */
	if (ah->ah_version == AR5K_AR5212)
		ath_hw_setbssidmask(common);

	/*
	 * Set BSSID
	 */
	ath5k_hw_reg_write(ah,
			   get_unaligned_le32(common->curbssid),
			   AR5K_BSS_ID0);
	ath5k_hw_reg_write(ah,
			   get_unaligned_le16(common->curbssid + 4) |
			   ((common->curaid & 0x3fff) << AR5K_BSS_ID1_AID_S),
			   AR5K_BSS_ID1);

	if (common->curaid == 0) {
		ath5k_hw_disable_pspoll(ah);
		return;
	}

	AR5K_REG_WRITE_BITS(ah, AR5K_BEACON, AR5K_BEACON_TIM,
			    tim_offset ? tim_offset + 4 : 0);

	ath5k_hw_enable_pspoll(ah, NULL, 0);
}

void ath5k_hw_set_bssid_mask(struct ath5k_hw *ah, const u8 *mask)
{
	struct ath_common *common = ath5k_hw_common(ah);

	/* Cache bssid mask so that we can restore it
	 * on reset */
	memcpy(common->bssidmask, mask, ETH_ALEN);
	if (ah->ah_version == AR5K_AR5212)
		ath_hw_setbssidmask(common);
}

/*
 * Set multicast filter
 */
void ath5k_hw_set_mcast_filter(struct ath5k_hw *ah, u32 filter0, u32 filter1)
{
	ath5k_hw_reg_write(ah, filter0, AR5K_MCAST_FILTER0);
	ath5k_hw_reg_write(ah, filter1, AR5K_MCAST_FILTER1);
}

/**
 * ath5k_hw_get_rx_filter - Get current rx filter
 *
 * @ah: The &struct ath5k_hw
 *
 * Returns the RX filter by reading rx filter and
 * phy error filter registers. RX filter is used
 * to set the allowed frame types that PCU will accept
 * and pass to the driver. For a list of frame types
 * check out reg.h.
 */
u32 ath5k_hw_get_rx_filter(struct ath5k_hw *ah)
{
	u32 data, filter = 0;

	filter = ath5k_hw_reg_read(ah, AR5K_RX_FILTER);

	/*Radar detection for 5212*/
	if (ah->ah_version == AR5K_AR5212) {
		data = ath5k_hw_reg_read(ah, AR5K_PHY_ERR_FIL);

		if (data & AR5K_PHY_ERR_FIL_RADAR)
			filter |= AR5K_RX_FILTER_RADARERR;
		if (data & (AR5K_PHY_ERR_FIL_OFDM | AR5K_PHY_ERR_FIL_CCK))
			filter |= AR5K_RX_FILTER_PHYERR;
	}

	return filter;
}

/**
 * ath5k_hw_set_rx_filter - Set rx filter
 *
 * @ah: The &struct ath5k_hw
 * @filter: RX filter mask (see reg.h)
 *
 * Sets RX filter register and also handles PHY error filter
 * register on 5212 and newer chips so that we have proper PHY
 * error reporting.
 */
void ath5k_hw_set_rx_filter(struct ath5k_hw *ah, u32 filter)
{
	u32 data = 0;

	/* Set PHY error filter register on 5212*/
	if (ah->ah_version == AR5K_AR5212) {
		if (filter & AR5K_RX_FILTER_RADARERR)
			data |= AR5K_PHY_ERR_FIL_RADAR;
		if (filter & AR5K_RX_FILTER_PHYERR)
			data |= AR5K_PHY_ERR_FIL_OFDM | AR5K_PHY_ERR_FIL_CCK;
	}

	/*
	 * The AR5210 uses promiscous mode to detect radar activity
	 */
	if (ah->ah_version == AR5K_AR5210 &&
			(filter & AR5K_RX_FILTER_RADARERR)) {
		filter &= ~AR5K_RX_FILTER_RADARERR;
		filter |= AR5K_RX_FILTER_PROM;
	}

	/*Zero length DMA (phy error reporting) */
	if (data)
		AR5K_REG_ENABLE_BITS(ah, AR5K_RXCFG, AR5K_RXCFG_ZLFDMA);
	else
		AR5K_REG_DISABLE_BITS(ah, AR5K_RXCFG, AR5K_RXCFG_ZLFDMA);

	/*Write RX Filter register*/
	ath5k_hw_reg_write(ah, filter & 0xff, AR5K_RX_FILTER);

	/*Write PHY error filter register on 5212*/
	if (ah->ah_version == AR5K_AR5212)
		ath5k_hw_reg_write(ah, data, AR5K_PHY_ERR_FIL);

}


/****************\
* Beacon control *
\****************/

#define ATH5K_MAX_TSF_READ 10

/**
 * ath5k_hw_get_tsf64 - Get the full 64bit TSF
 *
 * @ah: The &struct ath5k_hw
 *
 * Returns the current TSF
 */
u64 ath5k_hw_get_tsf64(struct ath5k_hw *ah)
{
	u32 tsf_lower, tsf_upper1, tsf_upper2;
	int i;
	unsigned long flags;

	/* This code is time critical - we don't want to be interrupted here */
	local_irq_save(flags);

	/*
	 * While reading TSF upper and then lower part, the clock is still
	 * counting (or jumping in case of IBSS merge) so we might get
	 * inconsistent values. To avoid this, we read the upper part again
	 * and check it has not been changed. We make the hypothesis that a
	 * maximum of 3 changes can happens in a row (we use 10 as a safe
	 * value).
	 *
	 * Impact on performance is pretty small, since in most cases, only
	 * 3 register reads are needed.
	 */

	tsf_upper1 = ath5k_hw_reg_read(ah, AR5K_TSF_U32);
	for (i = 0; i < ATH5K_MAX_TSF_READ; i++) {
		tsf_lower = ath5k_hw_reg_read(ah, AR5K_TSF_L32);
		tsf_upper2 = ath5k_hw_reg_read(ah, AR5K_TSF_U32);
		if (tsf_upper2 == tsf_upper1)
			break;
		tsf_upper1 = tsf_upper2;
	}

	local_irq_restore(flags);

	WARN_ON( i == ATH5K_MAX_TSF_READ );

	return (((u64)tsf_upper1 << 32) | tsf_lower);
}

/**
 * ath5k_hw_set_tsf64 - Set a new 64bit TSF
 *
 * @ah: The &struct ath5k_hw
 * @tsf64: The new 64bit TSF
 *
 * Sets the new TSF
 */
void ath5k_hw_set_tsf64(struct ath5k_hw *ah, u64 tsf64)
{
	ath5k_hw_reg_write(ah, tsf64 & 0xffffffff, AR5K_TSF_L32);
	ath5k_hw_reg_write(ah, (tsf64 >> 32) & 0xffffffff, AR5K_TSF_U32);
}

/**
 * ath5k_hw_reset_tsf - Force a TSF reset
 *
 * @ah: The &struct ath5k_hw
 *
 * Forces a TSF reset on PCU
 */
void ath5k_hw_reset_tsf(struct ath5k_hw *ah)
{
	u32 val;

	val = ath5k_hw_reg_read(ah, AR5K_BEACON) | AR5K_BEACON_RESET_TSF;

	/*
	 * Each write to the RESET_TSF bit toggles a hardware internal
	 * signal to reset TSF, but if left high it will cause a TSF reset
	 * on the next chip reset as well.  Thus we always write the value
	 * twice to clear the signal.
	 */
	ath5k_hw_reg_write(ah, val, AR5K_BEACON);
	ath5k_hw_reg_write(ah, val, AR5K_BEACON);
}

/*
 * Initialize beacon timers
 */
void ath5k_hw_init_beacon(struct ath5k_hw *ah, u32 next_beacon, u32 interval)
{
	u32 timer1, timer2, timer3;

	/*
	 * Set the additional timers by mode
	 */
	switch (ah->ah_sc->opmode) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_STATION:
		/* In STA mode timer1 is used as next wakeup
		 * timer and timer2 as next CFP duration start
		 * timer. Both in 1/8TUs. */
		/* TODO: PCF handling */
		if (ah->ah_version == AR5K_AR5210) {
			timer1 = 0xffffffff;
			timer2 = 0xffffffff;
		} else {
			timer1 = 0x0000ffff;
			timer2 = 0x0007ffff;
		}
		/* Mark associated AP as PCF incapable for now */
		AR5K_REG_DISABLE_BITS(ah, AR5K_STA_ID1, AR5K_STA_ID1_PCF);
		break;
	case NL80211_IFTYPE_ADHOC:
		AR5K_REG_ENABLE_BITS(ah, AR5K_TXCFG, AR5K_TXCFG_ADHOC_BCN_ATIM);
	default:
		/* On non-STA modes timer1 is used as next DMA
		 * beacon alert (DBA) timer and timer2 as next
		 * software beacon alert. Both in 1/8TUs. */
		timer1 = (next_beacon - AR5K_TUNE_DMA_BEACON_RESP) << 3;
		timer2 = (next_beacon - AR5K_TUNE_SW_BEACON_RESP) << 3;
		break;
	}

	/* Timer3 marks the end of our ATIM window
	 * a zero length window is not allowed because
	 * we 'll get no beacons */
	timer3 = next_beacon + 1;

	/*
	 * Set the beacon register and enable all timers.
	 */
	/* When in AP or Mesh Point mode zero timer0 to start TSF */
	if (ah->ah_sc->opmode == NL80211_IFTYPE_AP ||
	    ah->ah_sc->opmode == NL80211_IFTYPE_MESH_POINT)
		ath5k_hw_reg_write(ah, 0, AR5K_TIMER0);

	ath5k_hw_reg_write(ah, next_beacon, AR5K_TIMER0);
	ath5k_hw_reg_write(ah, timer1, AR5K_TIMER1);
	ath5k_hw_reg_write(ah, timer2, AR5K_TIMER2);
	ath5k_hw_reg_write(ah, timer3, AR5K_TIMER3);

	/* Force a TSF reset if requested and enable beacons */
	if (interval & AR5K_BEACON_RESET_TSF)
		ath5k_hw_reset_tsf(ah);

	ath5k_hw_reg_write(ah, interval & (AR5K_BEACON_PERIOD |
					AR5K_BEACON_ENABLE),
						AR5K_BEACON);

	/* Flush any pending BMISS interrupts on ISR by
	 * performing a clear-on-write operation on PISR
	 * register for the BMISS bit (writing a bit on
	 * ISR togles a reset for that bit and leaves
	 * the rest bits intact) */
	if (ah->ah_version == AR5K_AR5210)
		ath5k_hw_reg_write(ah, AR5K_ISR_BMISS, AR5K_ISR);
	else
		ath5k_hw_reg_write(ah, AR5K_ISR_BMISS, AR5K_PISR);

	/* TODO: Set enchanced sleep registers on AR5212
	 * based on vif->bss_conf params, until then
	 * disable power save reporting.*/
	AR5K_REG_DISABLE_BITS(ah, AR5K_STA_ID1, AR5K_STA_ID1_PWR_SV);

}

/**
 * ath5k_check_timer_win - Check if timer B is timer A + window
 *
 * @a: timer a (before b)
 * @b: timer b (after a)
 * @window: difference between a and b
 * @intval: timers are increased by this interval
 *
 * This helper function checks if timer B is timer A + window and covers
 * cases where timer A or B might have already been updated or wrapped
 * around (Timers are 16 bit).
 *
 * Returns true if O.K.
 */
static inline bool
ath5k_check_timer_win(int a, int b, int window, int intval)
{
	/*
	 * 1.) usually B should be A + window
	 * 2.) A already updated, B not updated yet
	 * 3.) A already updated and has wrapped around
	 * 4.) B has wrapped around
	 */
	if ((b - a == window) ||				/* 1.) */
	    (a - b == intval - window) ||			/* 2.) */
	    ((a | 0x10000) - b == intval - window) ||		/* 3.) */
	    ((b | 0x10000) - a == window))			/* 4.) */
		return true; /* O.K. */
	return false;
}

/**
 * ath5k_hw_check_beacon_timers - Check if the beacon timers are correct
 *
 * @ah: The &struct ath5k_hw
 * @intval: beacon interval
 *
 * This is a workaround for IBSS mode:
 *
 * The need for this function arises from the fact that we have 4 separate
 * HW timer registers (TIMER0 - TIMER3), which are closely related to the
 * next beacon target time (NBTT), and that the HW updates these timers
 * seperately based on the current TSF value. The hardware increments each
 * timer by the beacon interval, when the local TSF coverted to TU is equal
 * to the value stored in the timer.
 *
 * The reception of a beacon with the same BSSID can update the local HW TSF
 * at any time - this is something we can't avoid. If the TSF jumps to a
 * time which is later than the time stored in a timer, this timer will not
 * be updated until the TSF in TU wraps around at 16 bit (the size of the
 * timers) and reaches the time which is stored in the timer.
 *
 * The problem is that these timers are closely related to TIMER0 (NBTT) and
 * that they define a time "window". When the TSF jumps between two timers
 * (e.g. ATIM and NBTT), the one in the past will be left behind (not
 * updated), while the one in the future will be updated every beacon
 * interval. This causes the window to get larger, until the TSF wraps
 * around as described above and the timer which was left behind gets
 * updated again. But - because the beacon interval is usually not an exact
 * divisor of the size of the timers (16 bit), an unwanted "window" between
 * these timers has developed!
 *
 * This is especially important with the ATIM window, because during
 * the ATIM window only ATIM frames and no data frames are allowed to be
 * sent, which creates transmission pauses after each beacon. This symptom
 * has been described as "ramping ping" because ping times increase linearly
 * for some time and then drop down again. A wrong window on the DMA beacon
 * timer has the same effect, so we check for these two conditions.
 *
 * Returns true if O.K.
 */
bool
ath5k_hw_check_beacon_timers(struct ath5k_hw *ah, int intval)
{
	unsigned int nbtt, atim, dma;

	nbtt = ath5k_hw_reg_read(ah, AR5K_TIMER0);
	atim = ath5k_hw_reg_read(ah, AR5K_TIMER3);
	dma = ath5k_hw_reg_read(ah, AR5K_TIMER1) >> 3;

	/* NOTE: SWBA is different. Having a wrong window there does not
	 * stop us from sending data and this condition is catched thru
	 * other means (SWBA interrupt) */

	if (ath5k_check_timer_win(nbtt, atim, 1, intval) &&
	    ath5k_check_timer_win(dma, nbtt, AR5K_TUNE_DMA_BEACON_RESP,
				  intval))
		return true; /* O.K. */
	return false;
}

/**
 * ath5k_hw_set_coverage_class - Set IEEE 802.11 coverage class
 *
 * @ah: The &struct ath5k_hw
 * @coverage_class: IEEE 802.11 coverage class number
 *
 * Sets IFS intervals and ACK/CTS timeouts for given coverage class.
 */
void ath5k_hw_set_coverage_class(struct ath5k_hw *ah, u8 coverage_class)
{
	/* As defined by IEEE 802.11-2007 17.3.8.6 */
	int slot_time = ath5k_hw_get_default_slottime(ah) + 3 * coverage_class;
	int ack_timeout = ath5k_hw_get_default_sifs(ah) + slot_time;
	int cts_timeout = ack_timeout;

	ath5k_hw_set_ifs_intervals(ah, slot_time);
	ath5k_hw_set_ack_timeout(ah, ack_timeout);
	ath5k_hw_set_cts_timeout(ah, cts_timeout);

	ah->ah_coverage_class = coverage_class;
}

/***************************\
* Init/Start/Stop functions *
\***************************/

/**
 * ath5k_hw_start_rx_pcu - Start RX engine
 *
 * @ah: The &struct ath5k_hw
 *
 * Starts RX engine on PCU so that hw can process RXed frames
 * (ACK etc).
 *
 * NOTE: RX DMA should be already enabled using ath5k_hw_start_rx_dma
 */
void ath5k_hw_start_rx_pcu(struct ath5k_hw *ah)
{
	AR5K_REG_DISABLE_BITS(ah, AR5K_DIAG_SW, AR5K_DIAG_SW_DIS_RX);
}

/**
 * at5k_hw_stop_rx_pcu - Stop RX engine
 *
 * @ah: The &struct ath5k_hw
 *
 * Stops RX engine on PCU
 */
void ath5k_hw_stop_rx_pcu(struct ath5k_hw *ah)
{
	AR5K_REG_ENABLE_BITS(ah, AR5K_DIAG_SW, AR5K_DIAG_SW_DIS_RX);
}

/**
 * ath5k_hw_set_opmode - Set PCU operating mode
 *
 * @ah: The &struct ath5k_hw
 * @op_mode: &enum nl80211_iftype operating mode
 *
 * Configure PCU for the various operating modes (AP/STA etc)
 */
int ath5k_hw_set_opmode(struct ath5k_hw *ah, enum nl80211_iftype op_mode)
{
	struct ath_common *common = ath5k_hw_common(ah);
	u32 pcu_reg, beacon_reg, low_id, high_id;

	ATH5K_DBG(ah->ah_sc, ATH5K_DEBUG_MODE, "mode %d\n", op_mode);

	/* Preserve rest settings */
	pcu_reg = ath5k_hw_reg_read(ah, AR5K_STA_ID1) & 0xffff0000;
	pcu_reg &= ~(AR5K_STA_ID1_ADHOC | AR5K_STA_ID1_AP
			| AR5K_STA_ID1_KEYSRCH_MODE
			| (ah->ah_version == AR5K_AR5210 ?
			(AR5K_STA_ID1_PWR_SV | AR5K_STA_ID1_NO_PSPOLL) : 0));

	beacon_reg = 0;

	switch (op_mode) {
	case NL80211_IFTYPE_ADHOC:
		pcu_reg |= AR5K_STA_ID1_ADHOC | AR5K_STA_ID1_KEYSRCH_MODE;
		beacon_reg |= AR5K_BCR_ADHOC;
		if (ah->ah_version == AR5K_AR5210)
			pcu_reg |= AR5K_STA_ID1_NO_PSPOLL;
		else
			AR5K_REG_ENABLE_BITS(ah, AR5K_CFG, AR5K_CFG_IBSS);
		break;

	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		pcu_reg |= AR5K_STA_ID1_AP | AR5K_STA_ID1_KEYSRCH_MODE;
		beacon_reg |= AR5K_BCR_AP;
		if (ah->ah_version == AR5K_AR5210)
			pcu_reg |= AR5K_STA_ID1_NO_PSPOLL;
		else
			AR5K_REG_DISABLE_BITS(ah, AR5K_CFG, AR5K_CFG_IBSS);
		break;

	case NL80211_IFTYPE_STATION:
		pcu_reg |= AR5K_STA_ID1_KEYSRCH_MODE
			| (ah->ah_version == AR5K_AR5210 ?
				AR5K_STA_ID1_PWR_SV : 0);
	case NL80211_IFTYPE_MONITOR:
		pcu_reg |= AR5K_STA_ID1_KEYSRCH_MODE
			| (ah->ah_version == AR5K_AR5210 ?
				AR5K_STA_ID1_NO_PSPOLL : 0);
		break;

	default:
		return -EINVAL;
	}

	/*
	 * Set PCU registers
	 */
	low_id = get_unaligned_le32(common->macaddr);
	high_id = get_unaligned_le16(common->macaddr + 4);
	ath5k_hw_reg_write(ah, low_id, AR5K_STA_ID0);
	ath5k_hw_reg_write(ah, pcu_reg | high_id, AR5K_STA_ID1);

	/*
	 * Set Beacon Control Register on 5210
	 */
	if (ah->ah_version == AR5K_AR5210)
		ath5k_hw_reg_write(ah, beacon_reg, AR5K_BCR);

	return 0;
}

void ath5k_hw_pcu_init(struct ath5k_hw *ah, enum nl80211_iftype op_mode,
								u8 mode)
{
	/* Set bssid and bssid mask */
	ath5k_hw_set_bssid(ah);

	/* Set PCU config */
	ath5k_hw_set_opmode(ah, op_mode);

	/* Write rate duration table only on AR5212 and if
	 * virtual interface has already been brought up
	 * XXX: rethink this after new mode changes to
	 * mac80211 are integrated */
	if (ah->ah_version == AR5K_AR5212 &&
		ah->ah_sc->nvifs)
		ath5k_hw_write_rate_duration(ah);

	/* Set RSSI/BRSSI thresholds
	 *
	 * Note: If we decide to set this value
	 * dynamicaly, have in mind that when AR5K_RSSI_THR
	 * register is read it might return 0x40 if we haven't
	 * wrote anything to it plus BMISS RSSI threshold is zeroed.
	 * So doing a save/restore procedure here isn't the right
	 * choice. Instead store it on ath5k_hw */
	ath5k_hw_reg_write(ah, (AR5K_TUNE_RSSI_THRES |
				AR5K_TUNE_BMISS_THRES <<
				AR5K_RSSI_THR_BMISS_S),
				AR5K_RSSI_THR);

	/* MIC QoS support */
	if (ah->ah_mac_srev >= AR5K_SREV_AR2413) {
		ath5k_hw_reg_write(ah, 0x000100aa, AR5K_MIC_QOS_CTL);
		ath5k_hw_reg_write(ah, 0x00003210, AR5K_MIC_QOS_SEL);
	}

	/* QoS NOACK Policy */
	if (ah->ah_version == AR5K_AR5212) {
		ath5k_hw_reg_write(ah,
			AR5K_REG_SM(2, AR5K_QOS_NOACK_2BIT_VALUES) |
			AR5K_REG_SM(5, AR5K_QOS_NOACK_BIT_OFFSET)  |
			AR5K_REG_SM(0, AR5K_QOS_NOACK_BYTE_OFFSET),
			AR5K_QOS_NOACK);
	}

	/* Restore slot time and ACK timeouts */
	if (ah->ah_coverage_class > 0)
		ath5k_hw_set_coverage_class(ah, ah->ah_coverage_class);

	/* Set ACK bitrate mode (see ack_rates_high) */
	if (ah->ah_version == AR5K_AR5212) {
		u32 val = AR5K_STA_ID1_BASE_RATE_11B | AR5K_STA_ID1_ACKCTS_6MB;
		if (ah->ah_ack_bitrate_high)
			AR5K_REG_DISABLE_BITS(ah, AR5K_STA_ID1, val);
		else
			AR5K_REG_ENABLE_BITS(ah, AR5K_STA_ID1, val);
	}
	return;
}
