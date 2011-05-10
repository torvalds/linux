/*
 * Copyright (c) 2009 Atheros Communications Inc.
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

#include <asm/unaligned.h>

#include "ath.h"
#include "reg.h"

#define REG_READ	(common->ops->read)
#define REG_WRITE	(common->ops->write)

/**
 * ath_hw_set_bssid_mask - filter out bssids we listen
 *
 * @common: the ath_common struct for the device.
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
 *
 * Simple example: on your card you have have two BSSes you have created with
 * BSSID-01 and BSSID-02. Lets assume BSSID-01 will not use the MAC address.
 * There is another BSSID-03 but you are not part of it. For simplicity's sake,
 * assuming only 4 bits for a mac address and for BSSIDs you can then have:
 *
 *                  \
 * MAC:        0001 |
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
 * will not pass our check. This is because the bssid_mask tells the
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
void ath_hw_setbssidmask(struct ath_common *common)
{
	void *ah = common->ah;

	REG_WRITE(ah, get_unaligned_le32(common->bssidmask), AR_BSSMSKL);
	REG_WRITE(ah, get_unaligned_le16(common->bssidmask + 4), AR_BSSMSKU);
}
EXPORT_SYMBOL(ath_hw_setbssidmask);


/**
 * ath_hw_cycle_counters_update - common function to update cycle counters
 *
 * @common: the ath_common struct for the device.
 *
 * This function is used to update all cycle counters in one place.
 * It has to be called while holding common->cc_lock!
 */
void ath_hw_cycle_counters_update(struct ath_common *common)
{
	u32 cycles, busy, rx, tx;
	void *ah = common->ah;

	/* freeze */
	REG_WRITE(ah, AR_MIBC_FMC, AR_MIBC);

	/* read */
	cycles = REG_READ(ah, AR_CCCNT);
	busy = REG_READ(ah, AR_RCCNT);
	rx = REG_READ(ah, AR_RFCNT);
	tx = REG_READ(ah, AR_TFCNT);

	/* clear */
	REG_WRITE(ah, 0, AR_CCCNT);
	REG_WRITE(ah, 0, AR_RFCNT);
	REG_WRITE(ah, 0, AR_RCCNT);
	REG_WRITE(ah, 0, AR_TFCNT);

	/* unfreeze */
	REG_WRITE(ah, 0, AR_MIBC);

	/* update all cycle counters here */
	common->cc_ani.cycles += cycles;
	common->cc_ani.rx_busy += busy;
	common->cc_ani.rx_frame += rx;
	common->cc_ani.tx_frame += tx;

	common->cc_survey.cycles += cycles;
	common->cc_survey.rx_busy += busy;
	common->cc_survey.rx_frame += rx;
	common->cc_survey.tx_frame += tx;
}
EXPORT_SYMBOL(ath_hw_cycle_counters_update);

int32_t ath_hw_get_listen_time(struct ath_common *common)
{
	struct ath_cycle_counters *cc = &common->cc_ani;
	int32_t listen_time;

	listen_time = (cc->cycles - cc->rx_frame - cc->tx_frame) /
		      (common->clockrate * 1000);

	memset(cc, 0, sizeof(*cc));

	return listen_time;
}
EXPORT_SYMBOL(ath_hw_get_listen_time);
