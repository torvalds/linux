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

/*******************\
* Generic functions *
\*******************/

/**
 * ath5k_hw_set_opmode - Set PCU operating mode
 *
 * @ah: The &struct ath5k_hw
 *
 * Initialize PCU for the various operating modes (AP/STA etc)
 *
 * NOTE: ah->ah_op_mode must be set before calling this.
 */
int ath5k_hw_set_opmode(struct ath5k_hw *ah)
{
	struct ath_common *common = ath5k_hw_common(ah);
	u32 pcu_reg, beacon_reg, low_id, high_id;


	/* Preserve rest settings */
	pcu_reg = ath5k_hw_reg_read(ah, AR5K_STA_ID1) & 0xffff0000;
	pcu_reg &= ~(AR5K_STA_ID1_ADHOC | AR5K_STA_ID1_AP
			| AR5K_STA_ID1_KEYSRCH_MODE
			| (ah->ah_version == AR5K_AR5210 ?
			(AR5K_STA_ID1_PWR_SV | AR5K_STA_ID1_NO_PSPOLL) : 0));

	beacon_reg = 0;

	ATH5K_TRACE(ah->ah_sc);

	switch (ah->ah_op_mode) {
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

/**
 * ath5k_hw_update - Update mib counters (mac layer statistics)
 *
 * @ah: The &struct ath5k_hw
 * @stats: The &struct ieee80211_low_level_stats we use to track
 * statistics on the driver
 *
 * Reads MIB counters from PCU and updates sw statistics. Must be
 * called after a MIB interrupt.
 */
void ath5k_hw_update_mib_counters(struct ath5k_hw *ah,
		struct ieee80211_low_level_stats  *stats)
{
	ATH5K_TRACE(ah->ah_sc);

	/* Read-And-Clear */
	stats->dot11ACKFailureCount += ath5k_hw_reg_read(ah, AR5K_ACK_FAIL);
	stats->dot11RTSFailureCount += ath5k_hw_reg_read(ah, AR5K_RTS_FAIL);
	stats->dot11RTSSuccessCount += ath5k_hw_reg_read(ah, AR5K_RTS_OK);
	stats->dot11FCSErrorCount += ath5k_hw_reg_read(ah, AR5K_FCS_FAIL);

	/* XXX: Should we use this to track beacon count ?
	 * -we read it anyway to clear the register */
	ath5k_hw_reg_read(ah, AR5K_BEACON_CNT);

	/* Reset profile count registers on 5212*/
	if (ah->ah_version == AR5K_AR5212) {
		ath5k_hw_reg_write(ah, 0, AR5K_PROFCNT_TX);
		ath5k_hw_reg_write(ah, 0, AR5K_PROFCNT_RX);
		ath5k_hw_reg_write(ah, 0, AR5K_PROFCNT_RXCLR);
		ath5k_hw_reg_write(ah, 0, AR5K_PROFCNT_CYCLE);
	}

	/* TODO: Handle ANI stats */
}

/**
 * ath5k_hw_set_ack_bitrate - set bitrate for ACKs
 *
 * @ah: The &struct ath5k_hw
 * @high: Flag to determine if we want to use high transmition rate
 * for ACKs or not
 *
 * If high flag is set, we tell hw to use a set of control rates based on
 * the current transmition rate (check out control_rates array inside reset.c).
 * If not hw just uses the lowest rate available for the current modulation
 * scheme being used (1Mbit for CCK and 6Mbits for OFDM).
 */
void ath5k_hw_set_ack_bitrate_high(struct ath5k_hw *ah, bool high)
{
	if (ah->ah_version != AR5K_AR5212)
		return;
	else {
		u32 val = AR5K_STA_ID1_BASE_RATE_11B | AR5K_STA_ID1_ACKCTS_6MB;
		if (high)
			AR5K_REG_ENABLE_BITS(ah, AR5K_STA_ID1, val);
		else
			AR5K_REG_DISABLE_BITS(ah, AR5K_STA_ID1, val);
	}
}


/******************\
* ACK/CTS Timeouts *
\******************/

/**
 * ath5k_hw_het_ack_timeout - Get ACK timeout from PCU in usec
 *
 * @ah: The &struct ath5k_hw
 */
unsigned int ath5k_hw_get_ack_timeout(struct ath5k_hw *ah)
{
	ATH5K_TRACE(ah->ah_sc);

	return ath5k_hw_clocktoh(AR5K_REG_MS(ath5k_hw_reg_read(ah,
			AR5K_TIME_OUT), AR5K_TIME_OUT_ACK), ah->ah_turbo);
}

/**
 * ath5k_hw_set_ack_timeout - Set ACK timeout on PCU
 *
 * @ah: The &struct ath5k_hw
 * @timeout: Timeout in usec
 */
int ath5k_hw_set_ack_timeout(struct ath5k_hw *ah, unsigned int timeout)
{
	ATH5K_TRACE(ah->ah_sc);
	if (ath5k_hw_clocktoh(AR5K_REG_MS(0xffffffff, AR5K_TIME_OUT_ACK),
			ah->ah_turbo) <= timeout)
		return -EINVAL;

	AR5K_REG_WRITE_BITS(ah, AR5K_TIME_OUT, AR5K_TIME_OUT_ACK,
		ath5k_hw_htoclock(timeout, ah->ah_turbo));

	return 0;
}

/**
 * ath5k_hw_get_cts_timeout - Get CTS timeout from PCU in usec
 *
 * @ah: The &struct ath5k_hw
 */
unsigned int ath5k_hw_get_cts_timeout(struct ath5k_hw *ah)
{
	ATH5K_TRACE(ah->ah_sc);
	return ath5k_hw_clocktoh(AR5K_REG_MS(ath5k_hw_reg_read(ah,
			AR5K_TIME_OUT), AR5K_TIME_OUT_CTS), ah->ah_turbo);
}

/**
 * ath5k_hw_set_cts_timeout - Set CTS timeout on PCU
 *
 * @ah: The &struct ath5k_hw
 * @timeout: Timeout in usec
 */
int ath5k_hw_set_cts_timeout(struct ath5k_hw *ah, unsigned int timeout)
{
	ATH5K_TRACE(ah->ah_sc);
	if (ath5k_hw_clocktoh(AR5K_REG_MS(0xffffffff, AR5K_TIME_OUT_CTS),
			ah->ah_turbo) <= timeout)
		return -EINVAL;

	AR5K_REG_WRITE_BITS(ah, AR5K_TIME_OUT, AR5K_TIME_OUT_CTS,
			ath5k_hw_htoclock(timeout, ah->ah_turbo));

	return 0;
}

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

	ATH5K_TRACE(ah->ah_sc);
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
 * ath5k_hw_set_associd - Set BSSID for association
 *
 * @ah: The &struct ath5k_hw
 * @bssid: BSSID
 * @assoc_id: Assoc id
 *
 * Sets the BSSID which trigers the "SME Join" operation
 */
void ath5k_hw_set_associd(struct ath5k_hw *ah)
{
	struct ath_common *common = ath5k_hw_common(ah);
	u16 tim_offset = 0;

	/*
	 * Set simple BSSID mask on 5212
	 */
	if (ah->ah_version == AR5K_AR5212)
		ath_hw_setbssidmask(common);

	/*
	 * Set BSSID which triggers the "SME Join" operation
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
	ATH5K_TRACE(ah->ah_sc);

	/* Cache bssid mask so that we can restore it
	 * on reset */
	memcpy(common->bssidmask, mask, ETH_ALEN);
	if (ah->ah_version == AR5K_AR5212)
		ath_hw_setbssidmask(common);
}

/************\
* RX Control *
\************/

/**
 * ath5k_hw_start_rx_pcu - Start RX engine
 *
 * @ah: The &struct ath5k_hw
 *
 * Starts RX engine on PCU so that hw can process RXed frames
 * (ACK etc).
 *
 * NOTE: RX DMA should be already enabled using ath5k_hw_start_rx_dma
 * TODO: Init ANI here
 */
void ath5k_hw_start_rx_pcu(struct ath5k_hw *ah)
{
	ATH5K_TRACE(ah->ah_sc);
	AR5K_REG_DISABLE_BITS(ah, AR5K_DIAG_SW, AR5K_DIAG_SW_DIS_RX);
}

/**
 * at5k_hw_stop_rx_pcu - Stop RX engine
 *
 * @ah: The &struct ath5k_hw
 *
 * Stops RX engine on PCU
 *
 * TODO: Detach ANI here
 */
void ath5k_hw_stop_rx_pcu(struct ath5k_hw *ah)
{
	ATH5K_TRACE(ah->ah_sc);
	AR5K_REG_ENABLE_BITS(ah, AR5K_DIAG_SW, AR5K_DIAG_SW_DIS_RX);
}

/*
 * Set multicast filter
 */
void ath5k_hw_set_mcast_filter(struct ath5k_hw *ah, u32 filter0, u32 filter1)
{
	ATH5K_TRACE(ah->ah_sc);
	/* Set the multicat filter */
	ath5k_hw_reg_write(ah, filter0, AR5K_MCAST_FILTER0);
	ath5k_hw_reg_write(ah, filter1, AR5K_MCAST_FILTER1);
}

/*
 * Set multicast filter by index
 */
int ath5k_hw_set_mcast_filter_idx(struct ath5k_hw *ah, u32 index)
{

	ATH5K_TRACE(ah->ah_sc);
	if (index >= 64)
		return -EINVAL;
	else if (index >= 32)
		AR5K_REG_ENABLE_BITS(ah, AR5K_MCAST_FILTER1,
				(1 << (index - 32)));
	else
		AR5K_REG_ENABLE_BITS(ah, AR5K_MCAST_FILTER0, (1 << index));

	return 0;
}

/*
 * Clear Multicast filter by index
 */
int ath5k_hw_clear_mcast_filter_idx(struct ath5k_hw *ah, u32 index)
{

	ATH5K_TRACE(ah->ah_sc);
	if (index >= 64)
		return -EINVAL;
	else if (index >= 32)
		AR5K_REG_DISABLE_BITS(ah, AR5K_MCAST_FILTER1,
				(1 << (index - 32)));
	else
		AR5K_REG_DISABLE_BITS(ah, AR5K_MCAST_FILTER0, (1 << index));

	return 0;
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

	ATH5K_TRACE(ah->ah_sc);
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

	ATH5K_TRACE(ah->ah_sc);

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

/**
 * ath5k_hw_get_tsf32 - Get a 32bit TSF
 *
 * @ah: The &struct ath5k_hw
 *
 * Returns lower 32 bits of current TSF
 */
u32 ath5k_hw_get_tsf32(struct ath5k_hw *ah)
{
	ATH5K_TRACE(ah->ah_sc);
	return ath5k_hw_reg_read(ah, AR5K_TSF_L32);
}

/**
 * ath5k_hw_get_tsf64 - Get the full 64bit TSF
 *
 * @ah: The &struct ath5k_hw
 *
 * Returns the current TSF
 */
u64 ath5k_hw_get_tsf64(struct ath5k_hw *ah)
{
	u64 tsf = ath5k_hw_reg_read(ah, AR5K_TSF_U32);
	ATH5K_TRACE(ah->ah_sc);

	return ath5k_hw_reg_read(ah, AR5K_TSF_L32) | (tsf << 32);
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
	ATH5K_TRACE(ah->ah_sc);

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

	ATH5K_TRACE(ah->ah_sc);

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

	ATH5K_TRACE(ah->ah_sc);
	/*
	 * Set the additional timers by mode
	 */
	switch (ah->ah_op_mode) {
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
	timer3 = next_beacon + (ah->ah_atim_window ? ah->ah_atim_window : 1);

	/*
	 * Set the beacon register and enable all timers.
	 */
	/* When in AP or Mesh Point mode zero timer0 to start TSF */
	if (ah->ah_op_mode == NL80211_IFTYPE_AP ||
	    ah->ah_op_mode == NL80211_IFTYPE_MESH_POINT)
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

#if 0
/*
 * Set beacon timers
 */
int ath5k_hw_set_beacon_timers(struct ath5k_hw *ah,
		const struct ath5k_beacon_state *state)
{
	u32 cfp_period, next_cfp, dtim, interval, next_beacon;

	/*
	 * TODO: should be changed through *state
	 * review struct ath5k_beacon_state struct
	 *
	 * XXX: These are used for cfp period bellow, are they
	 * ok ? Is it O.K. for tsf here to be 0 or should we use
	 * get_tsf ?
	 */
	u32 dtim_count = 0; /* XXX */
	u32 cfp_count = 0; /* XXX */
	u32 tsf = 0; /* XXX */

	ATH5K_TRACE(ah->ah_sc);
	/* Return on an invalid beacon state */
	if (state->bs_interval < 1)
		return -EINVAL;

	interval = state->bs_interval;
	dtim = state->bs_dtim_period;

	/*
	 * PCF support?
	 */
	if (state->bs_cfp_period > 0) {
		/*
		 * Enable PCF mode and set the CFP
		 * (Contention Free Period) and timer registers
		 */
		cfp_period = state->bs_cfp_period * state->bs_dtim_period *
			state->bs_interval;
		next_cfp = (cfp_count * state->bs_dtim_period + dtim_count) *
			state->bs_interval;

		AR5K_REG_ENABLE_BITS(ah, AR5K_STA_ID1,
				AR5K_STA_ID1_DEFAULT_ANTENNA |
				AR5K_STA_ID1_PCF);
		ath5k_hw_reg_write(ah, cfp_period, AR5K_CFP_PERIOD);
		ath5k_hw_reg_write(ah, state->bs_cfp_max_duration,
				AR5K_CFP_DUR);
		ath5k_hw_reg_write(ah, (tsf + (next_cfp == 0 ? cfp_period :
						next_cfp)) << 3, AR5K_TIMER2);
	} else {
		/* Disable PCF mode */
		AR5K_REG_DISABLE_BITS(ah, AR5K_STA_ID1,
				AR5K_STA_ID1_DEFAULT_ANTENNA |
				AR5K_STA_ID1_PCF);
	}

	/*
	 * Enable the beacon timer register
	 */
	ath5k_hw_reg_write(ah, state->bs_next_beacon, AR5K_TIMER0);

	/*
	 * Start the beacon timers
	 */
	ath5k_hw_reg_write(ah, (ath5k_hw_reg_read(ah, AR5K_BEACON) &
		~(AR5K_BEACON_PERIOD | AR5K_BEACON_TIM)) |
		AR5K_REG_SM(state->bs_tim_offset ? state->bs_tim_offset + 4 : 0,
		AR5K_BEACON_TIM) | AR5K_REG_SM(state->bs_interval,
		AR5K_BEACON_PERIOD), AR5K_BEACON);

	/*
	 * Write new beacon miss threshold, if it appears to be valid
	 * XXX: Figure out right values for min <= bs_bmiss_threshold <= max
	 * and return if its not in range. We can test this by reading value and
	 * setting value to a largest value and seeing which values register.
	 */

	AR5K_REG_WRITE_BITS(ah, AR5K_RSSI_THR, AR5K_RSSI_THR_BMISS,
			state->bs_bmiss_threshold);

	/*
	 * Set sleep control register
	 * XXX: Didn't find this in 5210 code but since this register
	 * exists also in ar5k's 5210 headers i leave it as common code.
	 */
	AR5K_REG_WRITE_BITS(ah, AR5K_SLEEP_CTL, AR5K_SLEEP_CTL_SLDUR,
			(state->bs_sleep_duration - 3) << 3);

	/*
	 * Set enhanced sleep registers on 5212
	 */
	if (ah->ah_version == AR5K_AR5212) {
		if (state->bs_sleep_duration > state->bs_interval &&
				roundup(state->bs_sleep_duration, interval) ==
				state->bs_sleep_duration)
			interval = state->bs_sleep_duration;

		if (state->bs_sleep_duration > dtim && (dtim == 0 ||
				roundup(state->bs_sleep_duration, dtim) ==
				state->bs_sleep_duration))
			dtim = state->bs_sleep_duration;

		if (interval > dtim)
			return -EINVAL;

		next_beacon = interval == dtim ? state->bs_next_dtim :
			state->bs_next_beacon;

		ath5k_hw_reg_write(ah,
			AR5K_REG_SM((state->bs_next_dtim - 3) << 3,
			AR5K_SLEEP0_NEXT_DTIM) |
			AR5K_REG_SM(10, AR5K_SLEEP0_CABTO) |
			AR5K_SLEEP0_ENH_SLEEP_EN |
			AR5K_SLEEP0_ASSUME_DTIM, AR5K_SLEEP0);

		ath5k_hw_reg_write(ah, AR5K_REG_SM((next_beacon - 3) << 3,
			AR5K_SLEEP1_NEXT_TIM) |
			AR5K_REG_SM(10, AR5K_SLEEP1_BEACON_TO), AR5K_SLEEP1);

		ath5k_hw_reg_write(ah,
			AR5K_REG_SM(interval, AR5K_SLEEP2_TIM_PER) |
			AR5K_REG_SM(dtim, AR5K_SLEEP2_DTIM_PER), AR5K_SLEEP2);
	}

	return 0;
}

/*
 * Reset beacon timers
 */
void ath5k_hw_reset_beacon(struct ath5k_hw *ah)
{
	ATH5K_TRACE(ah->ah_sc);
	/*
	 * Disable beacon timer
	 */
	ath5k_hw_reg_write(ah, 0, AR5K_TIMER0);

	/*
	 * Disable some beacon register values
	 */
	AR5K_REG_DISABLE_BITS(ah, AR5K_STA_ID1,
			AR5K_STA_ID1_DEFAULT_ANTENNA | AR5K_STA_ID1_PCF);
	ath5k_hw_reg_write(ah, AR5K_BEACON_PERIOD, AR5K_BEACON);
}

/*
 * Wait for beacon queue to finish
 */
int ath5k_hw_beaconq_finish(struct ath5k_hw *ah, unsigned long phys_addr)
{
	unsigned int i;
	int ret;

	ATH5K_TRACE(ah->ah_sc);

	/* 5210 doesn't have QCU*/
	if (ah->ah_version == AR5K_AR5210) {
		/*
		 * Wait for beaconn queue to finish by checking
		 * Control Register and Beacon Status Register.
		 */
		for (i = AR5K_TUNE_BEACON_INTERVAL / 2; i > 0; i--) {
			if (!(ath5k_hw_reg_read(ah, AR5K_BSR) & AR5K_BSR_TXQ1F)
					||
			    !(ath5k_hw_reg_read(ah, AR5K_CR) & AR5K_BSR_TXQ1F))
				break;
			udelay(10);
		}

		/* Timeout... */
		if (i <= 0) {
			/*
			 * Re-schedule the beacon queue
			 */
			ath5k_hw_reg_write(ah, phys_addr, AR5K_NOQCU_TXDP1);
			ath5k_hw_reg_write(ah, AR5K_BCR_TQ1V | AR5K_BCR_BDMAE,
					AR5K_BCR);

			return -EIO;
		}
		ret = 0;
	} else {
	/*5211/5212*/
		ret = ath5k_hw_register_timeout(ah,
			AR5K_QUEUE_STATUS(AR5K_TX_QUEUE_ID_BEACON),
			AR5K_QCU_STS_FRMPENDCNT, 0, false);

		if (AR5K_REG_READ_Q(ah, AR5K_QCU_TXE, AR5K_TX_QUEUE_ID_BEACON))
			return -EIO;
	}

	return ret;
}
#endif


/*********************\
* Key table functions *
\*********************/

/*
 * Reset a key entry on the table
 */
int ath5k_hw_reset_key(struct ath5k_hw *ah, u16 entry)
{
	unsigned int i, type;
	u16 micentry = entry + AR5K_KEYTABLE_MIC_OFFSET;

	ATH5K_TRACE(ah->ah_sc);
	AR5K_ASSERT_ENTRY(entry, AR5K_KEYTABLE_SIZE);

	type = ath5k_hw_reg_read(ah, AR5K_KEYTABLE_TYPE(entry));

	for (i = 0; i < AR5K_KEYCACHE_SIZE; i++)
		ath5k_hw_reg_write(ah, 0, AR5K_KEYTABLE_OFF(entry, i));

	/* Reset associated MIC entry if TKIP
	 * is enabled located at offset (entry + 64) */
	if (type == AR5K_KEYTABLE_TYPE_TKIP) {
		AR5K_ASSERT_ENTRY(micentry, AR5K_KEYTABLE_SIZE);
		for (i = 0; i < AR5K_KEYCACHE_SIZE / 2 ; i++)
			ath5k_hw_reg_write(ah, 0,
				AR5K_KEYTABLE_OFF(micentry, i));
	}

	/*
	 * Set NULL encryption on AR5212+
	 *
	 * Note: AR5K_KEYTABLE_TYPE -> AR5K_KEYTABLE_OFF(entry, 5)
	 *       AR5K_KEYTABLE_TYPE_NULL -> 0x00000007
	 *
	 * Note2: Windows driver (ndiswrapper) sets this to
	 *        0x00000714 instead of 0x00000007
	 */
	if (ah->ah_version >= AR5K_AR5211) {
		ath5k_hw_reg_write(ah, AR5K_KEYTABLE_TYPE_NULL,
				AR5K_KEYTABLE_TYPE(entry));

		if (type == AR5K_KEYTABLE_TYPE_TKIP) {
			ath5k_hw_reg_write(ah, AR5K_KEYTABLE_TYPE_NULL,
				AR5K_KEYTABLE_TYPE(micentry));
		}
	}

	return 0;
}

/*
 * Check if a table entry is valid
 */
int ath5k_hw_is_key_valid(struct ath5k_hw *ah, u16 entry)
{
	ATH5K_TRACE(ah->ah_sc);
	AR5K_ASSERT_ENTRY(entry, AR5K_KEYTABLE_SIZE);

	/* Check the validation flag at the end of the entry */
	return ath5k_hw_reg_read(ah, AR5K_KEYTABLE_MAC1(entry)) &
		AR5K_KEYTABLE_VALID;
}

static
int ath5k_keycache_type(const struct ieee80211_key_conf *key)
{
	switch (key->alg) {
	case ALG_TKIP:
		return AR5K_KEYTABLE_TYPE_TKIP;
	case ALG_CCMP:
		return AR5K_KEYTABLE_TYPE_CCM;
	case ALG_WEP:
		if (key->keylen == WLAN_KEY_LEN_WEP40)
			return AR5K_KEYTABLE_TYPE_40;
		else if (key->keylen == WLAN_KEY_LEN_WEP104)
			return AR5K_KEYTABLE_TYPE_104;
		return -EINVAL;
	default:
		return -EINVAL;
	}
	return -EINVAL;
}

/*
 * Set a key entry on the table
 */
int ath5k_hw_set_key(struct ath5k_hw *ah, u16 entry,
		const struct ieee80211_key_conf *key, const u8 *mac)
{
	unsigned int i;
	int keylen;
	__le32 key_v[5] = {};
	__le32 key0 = 0, key1 = 0;
	__le32 *rxmic, *txmic;
	int keytype;
	u16 micentry = entry + AR5K_KEYTABLE_MIC_OFFSET;
	bool is_tkip;
	const u8 *key_ptr;

	ATH5K_TRACE(ah->ah_sc);

	is_tkip = (key->alg == ALG_TKIP);

	/*
	 * key->keylen comes in from mac80211 in bytes.
	 * TKIP is 128 bit + 128 bit mic
	 */
	keylen = (is_tkip) ? (128 / 8) : key->keylen;

	if (entry > AR5K_KEYTABLE_SIZE ||
		(is_tkip && micentry > AR5K_KEYTABLE_SIZE))
		return -EOPNOTSUPP;

	if (unlikely(keylen > 16))
		return -EOPNOTSUPP;

	keytype = ath5k_keycache_type(key);
	if (keytype < 0)
		return keytype;

	/*
	 * each key block is 6 bytes wide, written as pairs of
	 * alternating 32 and 16 bit le values.
	 */
	key_ptr = key->key;
	for (i = 0; keylen >= 6; keylen -= 6) {
		memcpy(&key_v[i], key_ptr, 6);
		i += 2;
		key_ptr += 6;
	}
	if (keylen)
		memcpy(&key_v[i], key_ptr, keylen);

	/* intentionally corrupt key until mic is installed */
	if (is_tkip) {
		key0 = key_v[0] = ~key_v[0];
		key1 = key_v[1] = ~key_v[1];
	}

	for (i = 0; i < ARRAY_SIZE(key_v); i++)
		ath5k_hw_reg_write(ah, le32_to_cpu(key_v[i]),
				AR5K_KEYTABLE_OFF(entry, i));

	ath5k_hw_reg_write(ah, keytype, AR5K_KEYTABLE_TYPE(entry));

	if (is_tkip) {
		/* Install rx/tx MIC */
		rxmic = (__le32 *) &key->key[16];
		txmic = (__le32 *) &key->key[24];

		if (ah->ah_combined_mic) {
			key_v[0] = rxmic[0];
			key_v[1] = cpu_to_le32(le32_to_cpu(txmic[0]) >> 16);
			key_v[2] = rxmic[1];
			key_v[3] = cpu_to_le32(le32_to_cpu(txmic[0]) & 0xffff);
			key_v[4] = txmic[1];
		} else {
			key_v[0] = rxmic[0];
			key_v[1] = 0;
			key_v[2] = rxmic[1];
			key_v[3] = 0;
			key_v[4] = 0;
		}
		for (i = 0; i < ARRAY_SIZE(key_v); i++)
			ath5k_hw_reg_write(ah, le32_to_cpu(key_v[i]),
				AR5K_KEYTABLE_OFF(micentry, i));

		ath5k_hw_reg_write(ah, AR5K_KEYTABLE_TYPE_NULL,
			AR5K_KEYTABLE_TYPE(micentry));
		ath5k_hw_reg_write(ah, 0, AR5K_KEYTABLE_MAC0(micentry));
		ath5k_hw_reg_write(ah, 0, AR5K_KEYTABLE_MAC1(micentry));

		/* restore first 2 words of key */
		ath5k_hw_reg_write(ah, le32_to_cpu(~key0),
			AR5K_KEYTABLE_OFF(entry, 0));
		ath5k_hw_reg_write(ah, le32_to_cpu(~key1),
			AR5K_KEYTABLE_OFF(entry, 1));
	}

	return ath5k_hw_set_key_lladdr(ah, entry, mac);
}

int ath5k_hw_set_key_lladdr(struct ath5k_hw *ah, u16 entry, const u8 *mac)
{
	u32 low_id, high_id;

	ATH5K_TRACE(ah->ah_sc);
	 /* Invalid entry (key table overflow) */
	AR5K_ASSERT_ENTRY(entry, AR5K_KEYTABLE_SIZE);

	/*
	 * MAC may be NULL if it's a broadcast key. In this case no need to
	 * to compute get_unaligned_le32 and get_unaligned_le16 as we
	 * already know it.
	 */
	if (!mac) {
		low_id = 0xffffffff;
		high_id = 0xffff | AR5K_KEYTABLE_VALID;
	} else {
		low_id = get_unaligned_le32(mac);
		high_id = get_unaligned_le16(mac + 4) | AR5K_KEYTABLE_VALID;
	}

	ath5k_hw_reg_write(ah, low_id, AR5K_KEYTABLE_MAC0(entry));
	ath5k_hw_reg_write(ah, high_id, AR5K_KEYTABLE_MAC1(entry));

	return 0;
}

