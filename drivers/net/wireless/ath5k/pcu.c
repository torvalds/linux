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
	u32 pcu_reg, beacon_reg, low_id, high_id;

	pcu_reg = 0;
	beacon_reg = 0;

	ATH5K_TRACE(ah->ah_sc);

	switch (ah->ah_op_mode) {
	case NL80211_IFTYPE_ADHOC:
		pcu_reg |= AR5K_STA_ID1_ADHOC | AR5K_STA_ID1_DESC_ANTENNA |
			(ah->ah_version == AR5K_AR5210 ?
				AR5K_STA_ID1_NO_PSPOLL : 0);
		beacon_reg |= AR5K_BCR_ADHOC;
		break;

	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		pcu_reg |= AR5K_STA_ID1_AP | AR5K_STA_ID1_RTS_DEF_ANTENNA |
			(ah->ah_version == AR5K_AR5210 ?
				AR5K_STA_ID1_NO_PSPOLL : 0);
		beacon_reg |= AR5K_BCR_AP;
		break;

	case NL80211_IFTYPE_STATION:
		pcu_reg |= AR5K_STA_ID1_DEFAULT_ANTENNA |
			(ah->ah_version == AR5K_AR5210 ?
				AR5K_STA_ID1_PWR_SV : 0);
	case NL80211_IFTYPE_MONITOR:
		pcu_reg |= AR5K_STA_ID1_DEFAULT_ANTENNA |
			(ah->ah_version == AR5K_AR5210 ?
				AR5K_STA_ID1_NO_PSPOLL : 0);
		break;

	default:
		return -EINVAL;
	}

	/*
	 * Set PCU registers
	 */
	low_id = AR5K_LOW_ID(ah->ah_sta_id);
	high_id = AR5K_HIGH_ID(ah->ah_sta_id);
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


/****************\
* BSSID handling *
\****************/

/**
 * ath5k_hw_get_lladdr - Get station id
 *
 * @ah: The &struct ath5k_hw
 * @mac: The card's mac address
 *
 * Initialize ah->ah_sta_id using the mac address provided
 * (just a memcpy).
 *
 * TODO: Remove it once we merge ath5k_softc and ath5k_hw
 */
void ath5k_hw_get_lladdr(struct ath5k_hw *ah, u8 *mac)
{
	ATH5K_TRACE(ah->ah_sc);
	memcpy(mac, ah->ah_sta_id, ETH_ALEN);
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
	u32 low_id, high_id;

	ATH5K_TRACE(ah->ah_sc);
	/* Set new station ID */
	memcpy(ah->ah_sta_id, mac, ETH_ALEN);

	low_id = AR5K_LOW_ID(mac);
	high_id = AR5K_HIGH_ID(mac);

	ath5k_hw_reg_write(ah, low_id, AR5K_STA_ID0);
	ath5k_hw_reg_write(ah, high_id, AR5K_STA_ID1);

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
void ath5k_hw_set_associd(struct ath5k_hw *ah, const u8 *bssid, u16 assoc_id)
{
	u32 low_id, high_id;
	u16 tim_offset = 0;

	/*
	 * Set simple BSSID mask on 5212
	 */
	if (ah->ah_version == AR5K_AR5212) {
		ath5k_hw_reg_write(ah, 0xffffffff, AR5K_BSS_IDM0);
		ath5k_hw_reg_write(ah, 0xffffffff, AR5K_BSS_IDM1);
	}

	/*
	 * Set BSSID which triggers the "SME Join" operation
	 */
	low_id = AR5K_LOW_ID(bssid);
	high_id = AR5K_HIGH_ID(bssid);
	ath5k_hw_reg_write(ah, low_id, AR5K_BSS_ID0);
	ath5k_hw_reg_write(ah, high_id | ((assoc_id & 0x3fff) <<
				AR5K_BSS_ID1_AID_S), AR5K_BSS_ID1);

	if (assoc_id == 0) {
		ath5k_hw_disable_pspoll(ah);
		return;
	}

	AR5K_REG_WRITE_BITS(ah, AR5K_BEACON, AR5K_BEACON_TIM,
			tim_offset ? tim_offset + 4 : 0);

	ath5k_hw_enable_pspoll(ah, NULL, 0);
}

/**
 * ath5k_hw_set_bssid_mask - filter out bssids we listen
 *
 * @ah: the &struct ath5k_hw
 * @mask: the bssid_mask, a u8 array of size ETH_ALEN
 *
 * BSSID masking is a method used by AR5212 and newer hardware to inform PCU
 * which bits of the interface's MAC address should be looked at when trying
 * to decide which packets to ACK. In station mode and AP mode with a single
 * BSS every bit matters since we lock to only one BSS. In AP mode with
 * multiple BSSes (virtual interfaces) not every bit matters because hw must
 * accept frames for all BSSes and so we tweak some bits of our mac address
 * in order to have multiple BSSes.
 *
 * NOTE: This is a simple filter and does *not* filter out all
 * relevant frames. Some frames that are not for us might get ACKed from us
 * by PCU because they just match the mask.
 *
 * When handling multiple BSSes you can get the BSSID mask by computing the
 * set of  ~ ( MAC XOR BSSID ) for all bssids we handle.
 *
 * When you do this you are essentially computing the common bits of all your
 * BSSes. Later it is assumed the harware will "and" (&) the BSSID mask with
 * the MAC address to obtain the relevant bits and compare the result with
 * (frame's BSSID & mask) to see if they match.
 */
/*
 * Simple example: on your card you have have two BSSes you have created with
 * BSSID-01 and BSSID-02. Lets assume BSSID-01 will not use the MAC address.
 * There is another BSSID-03 but you are not part of it. For simplicity's sake,
 * assuming only 4 bits for a mac address and for BSSIDs you can then have:
 *
 *                  \
 * MAC:                0001 |
 * BSSID-01:   0100 | --> Belongs to us
 * BSSID-02:   1001 |
 *                  /
 * -------------------
 * BSSID-03:   0110  | --> External
 * -------------------
 *
 * Our bssid_mask would then be:
 *
 *             On loop iteration for BSSID-01:
 *             ~(0001 ^ 0100)  -> ~(0101)
 *                             ->   1010
 *             bssid_mask      =    1010
 *
 *             On loop iteration for BSSID-02:
 *             bssid_mask &= ~(0001   ^   1001)
 *             bssid_mask =   (1010)  & ~(0001 ^ 1001)
 *             bssid_mask =   (1010)  & ~(1001)
 *             bssid_mask =   (1010)  &  (0110)
 *             bssid_mask =   0010
 *
 * A bssid_mask of 0010 means "only pay attention to the second least
 * significant bit". This is because its the only bit common
 * amongst the MAC and all BSSIDs we support. To findout what the real
 * common bit is we can simply "&" the bssid_mask now with any BSSID we have
 * or our MAC address (we assume the hardware uses the MAC address).
 *
 * Now, suppose there's an incoming frame for BSSID-03:
 *
 * IFRAME-01:  0110
 *
 * An easy eye-inspeciton of this already should tell you that this frame
 * will not pass our check. This is beacuse the bssid_mask tells the
 * hardware to only look at the second least significant bit and the
 * common bit amongst the MAC and BSSIDs is 0, this frame has the 2nd LSB
 * as 1, which does not match 0.
 *
 * So with IFRAME-01 we *assume* the hardware will do:
 *
 *     allow = (IFRAME-01 & bssid_mask) == (bssid_mask & MAC) ? 1 : 0;
 *  --> allow = (0110 & 0010) == (0010 & 0001) ? 1 : 0;
 *  --> allow = (0010) == 0000 ? 1 : 0;
 *  --> allow = 0
 *
 *  Lets now test a frame that should work:
 *
 * IFRAME-02:  0001 (we should allow)
 *
 *     allow = (0001 & 1010) == 1010
 *
 *     allow = (IFRAME-02 & bssid_mask) == (bssid_mask & MAC) ? 1 : 0;
 *  --> allow = (0001 & 0010) ==  (0010 & 0001) ? 1 :0;
 *  --> allow = (0010) == (0010)
 *  --> allow = 1
 *
 * Other examples:
 *
 * IFRAME-03:  0100 --> allowed
 * IFRAME-04:  1001 --> allowed
 * IFRAME-05:  1101 --> allowed but its not for us!!!
 *
 */
int ath5k_hw_set_bssid_mask(struct ath5k_hw *ah, const u8 *mask)
{
	u32 low_id, high_id;
	ATH5K_TRACE(ah->ah_sc);

	if (ah->ah_version == AR5K_AR5212) {
		low_id = AR5K_LOW_ID(mask);
		high_id = AR5K_HIGH_ID(mask);

		ath5k_hw_reg_write(ah, low_id, AR5K_BSS_IDM0);
		ath5k_hw_reg_write(ah, high_id, AR5K_BSS_IDM1);

		return 0;
	}

	return -EIO;
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

	/*Zero length DMA*/
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
	case NL80211_IFTYPE_STATION:
		if (ah->ah_version == AR5K_AR5210) {
			timer1 = 0xffffffff;
			timer2 = 0xffffffff;
		} else {
			timer1 = 0x0000ffff;
			timer2 = 0x0007ffff;
		}
		break;

	default:
		timer1 = (next_beacon - AR5K_TUNE_DMA_BEACON_RESP) << 3;
		timer2 = (next_beacon - AR5K_TUNE_SW_BEACON_RESP) << 3;
	}

	timer3 = next_beacon + (ah->ah_atim_window ? ah->ah_atim_window : 1);

	/*
	 * Set the beacon register and enable all timers.
	 * (next beacon, DMA beacon, software beacon, ATIM window time)
	 */
	ath5k_hw_reg_write(ah, next_beacon, AR5K_TIMER0);
	ath5k_hw_reg_write(ah, timer1, AR5K_TIMER1);
	ath5k_hw_reg_write(ah, timer2, AR5K_TIMER2);
	ath5k_hw_reg_write(ah, timer3, AR5K_TIMER3);

	ath5k_hw_reg_write(ah, interval & (AR5K_BEACON_PERIOD |
			AR5K_BEACON_RESET_TSF | AR5K_BEACON_ENABLE),
		AR5K_BEACON);
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
	unsigned int i;

	ATH5K_TRACE(ah->ah_sc);
	AR5K_ASSERT_ENTRY(entry, AR5K_KEYTABLE_SIZE);

	for (i = 0; i < AR5K_KEYCACHE_SIZE; i++)
		ath5k_hw_reg_write(ah, 0, AR5K_KEYTABLE_OFF(entry, i));

	/*
	 * Set NULL encryption on AR5212+
	 *
	 * Note: AR5K_KEYTABLE_TYPE -> AR5K_KEYTABLE_OFF(entry, 5)
	 *       AR5K_KEYTABLE_TYPE_NULL -> 0x00000007
	 *
	 * Note2: Windows driver (ndiswrapper) sets this to
	 *        0x00000714 instead of 0x00000007
	 */
	if (ah->ah_version > AR5K_AR5211)
		ath5k_hw_reg_write(ah, AR5K_KEYTABLE_TYPE_NULL,
				AR5K_KEYTABLE_TYPE(entry));

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

/*
 * Set a key entry on the table
 */
int ath5k_hw_set_key(struct ath5k_hw *ah, u16 entry,
		const struct ieee80211_key_conf *key, const u8 *mac)
{
	unsigned int i;
	__le32 key_v[5] = {};
	u32 keytype;

	ATH5K_TRACE(ah->ah_sc);

	/* key->keylen comes in from mac80211 in bytes */

	if (key->keylen > AR5K_KEYTABLE_SIZE / 8)
		return -EOPNOTSUPP;

	switch (key->keylen) {
	/* WEP 40-bit   = 40-bit  entered key + 24 bit IV = 64-bit */
	case 40 / 8:
		memcpy(&key_v[0], key->key, 5);
		keytype = AR5K_KEYTABLE_TYPE_40;
		break;

	/* WEP 104-bit  = 104-bit entered key + 24-bit IV = 128-bit */
	case 104 / 8:
		memcpy(&key_v[0], &key->key[0], 6);
		memcpy(&key_v[2], &key->key[6], 6);
		memcpy(&key_v[4], &key->key[12], 1);
		keytype = AR5K_KEYTABLE_TYPE_104;
		break;
	/* WEP 128-bit  = 128-bit entered key + 24 bit IV = 152-bit */
	case 128 / 8:
		memcpy(&key_v[0], &key->key[0], 6);
		memcpy(&key_v[2], &key->key[6], 6);
		memcpy(&key_v[4], &key->key[12], 4);
		keytype = AR5K_KEYTABLE_TYPE_128;
		break;

	default:
		return -EINVAL; /* shouldn't happen */
	}

	for (i = 0; i < ARRAY_SIZE(key_v); i++)
		ath5k_hw_reg_write(ah, le32_to_cpu(key_v[i]),
				AR5K_KEYTABLE_OFF(entry, i));

	ath5k_hw_reg_write(ah, keytype, AR5K_KEYTABLE_TYPE(entry));

	return ath5k_hw_set_key_lladdr(ah, entry, mac);
}

int ath5k_hw_set_key_lladdr(struct ath5k_hw *ah, u16 entry, const u8 *mac)
{
	u32 low_id, high_id;

	ATH5K_TRACE(ah->ah_sc);
	 /* Invalid entry (key table overflow) */
	AR5K_ASSERT_ENTRY(entry, AR5K_KEYTABLE_SIZE);

	/* MAC may be NULL if it's a broadcast key. In this case no need to
	 * to compute AR5K_LOW_ID and AR5K_HIGH_ID as we already know it. */
	if (unlikely(mac == NULL)) {
		low_id = 0xffffffff;
		high_id = 0xffff | AR5K_KEYTABLE_VALID;
	} else {
		low_id = AR5K_LOW_ID(mac);
		high_id = AR5K_HIGH_ID(mac) | AR5K_KEYTABLE_VALID;
	}

	ath5k_hw_reg_write(ah, low_id, AR5K_KEYTABLE_MAC0(entry));
	ath5k_hw_reg_write(ah, high_id, AR5K_KEYTABLE_MAC1(entry));

	return 0;
}

