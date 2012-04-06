/*
 * Copyright (c) 2004-2008 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006-2008 Nick Kossifidis <mickflemm@gmail.com>
 * Copyright (c) 2007-2008 Pavel Roskin <proski@gnu.org>
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

/******************************\
 Hardware Descriptor Functions
\******************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ath5k.h"
#include "reg.h"
#include "debug.h"


/**
 * DOC: Hardware descriptor functions
 *
 * Here we handle the processing of the low-level hw descriptors
 * that hw reads and writes via DMA for each TX and RX attempt (that means
 * we can also have descriptors for failed TX/RX tries). We have two kind of
 * descriptors for RX and TX, control descriptors tell the hw how to send or
 * receive a packet where to read/write it from/to etc and status descriptors
 * that contain information about how the packet was sent or received (errors
 * included).
 *
 * Descriptor format is not exactly the same for each MAC chip version so we
 * have function pointers on &struct ath5k_hw we initialize at runtime based on
 * the chip used.
 */


/************************\
* TX Control descriptors *
\************************/

/**
 * ath5k_hw_setup_2word_tx_desc() - Initialize a 2-word tx control descriptor
 * @ah: The &struct ath5k_hw
 * @desc: The &struct ath5k_desc
 * @pkt_len: Frame length in bytes
 * @hdr_len: Header length in bytes (only used on AR5210)
 * @padsize: Any padding we've added to the frame length
 * @type: One of enum ath5k_pkt_type
 * @tx_power: Tx power in 0.5dB steps
 * @tx_rate0: HW idx for transmission rate
 * @tx_tries0: Max number of retransmissions
 * @key_index: Index on key table to use for encryption
 * @antenna_mode: Which antenna to use (0 for auto)
 * @flags: One of AR5K_TXDESC_* flags (desc.h)
 * @rtscts_rate: HW idx for RTS/CTS transmission rate
 * @rtscts_duration: What to put on duration field on the header of RTS/CTS
 *
 * Internal function to initialize a 2-Word TX control descriptor
 * found on AR5210 and AR5211 MACs chips.
 *
 * Returns 0 on success or -EINVAL on false input
 */
static int
ath5k_hw_setup_2word_tx_desc(struct ath5k_hw *ah,
			struct ath5k_desc *desc,
			unsigned int pkt_len, unsigned int hdr_len,
			int padsize,
			enum ath5k_pkt_type type,
			unsigned int tx_power,
			unsigned int tx_rate0, unsigned int tx_tries0,
			unsigned int key_index,
			unsigned int antenna_mode,
			unsigned int flags,
			unsigned int rtscts_rate, unsigned int rtscts_duration)
{
	u32 frame_type;
	struct ath5k_hw_2w_tx_ctl *tx_ctl;
	unsigned int frame_len;

	tx_ctl = &desc->ud.ds_tx5210.tx_ctl;

	/*
	 * Validate input
	 * - Zero retries don't make sense.
	 * - A zero rate will put the HW into a mode where it continuously sends
	 *   noise on the channel, so it is important to avoid this.
	 */
	if (unlikely(tx_tries0 == 0)) {
		ATH5K_ERR(ah, "zero retries\n");
		WARN_ON(1);
		return -EINVAL;
	}
	if (unlikely(tx_rate0 == 0)) {
		ATH5K_ERR(ah, "zero rate\n");
		WARN_ON(1);
		return -EINVAL;
	}

	/* Clear descriptor */
	memset(&desc->ud.ds_tx5210, 0, sizeof(struct ath5k_hw_5210_tx_desc));

	/* Setup control descriptor */

	/* Verify and set frame length */

	/* remove padding we might have added before */
	frame_len = pkt_len - padsize + FCS_LEN;

	if (frame_len & ~AR5K_2W_TX_DESC_CTL0_FRAME_LEN)
		return -EINVAL;

	tx_ctl->tx_control_0 = frame_len & AR5K_2W_TX_DESC_CTL0_FRAME_LEN;

	/* Verify and set buffer length */

	/* NB: beacon's BufLen must be a multiple of 4 bytes */
	if (type == AR5K_PKT_TYPE_BEACON)
		pkt_len = roundup(pkt_len, 4);

	if (pkt_len & ~AR5K_2W_TX_DESC_CTL1_BUF_LEN)
		return -EINVAL;

	tx_ctl->tx_control_1 = pkt_len & AR5K_2W_TX_DESC_CTL1_BUF_LEN;

	/*
	 * Verify and set header length (only 5210)
	 */
	if (ah->ah_version == AR5K_AR5210) {
		if (hdr_len & ~AR5K_2W_TX_DESC_CTL0_HEADER_LEN_5210)
			return -EINVAL;
		tx_ctl->tx_control_0 |=
			AR5K_REG_SM(hdr_len, AR5K_2W_TX_DESC_CTL0_HEADER_LEN_5210);
	}

	/*Differences between 5210-5211*/
	if (ah->ah_version == AR5K_AR5210) {
		switch (type) {
		case AR5K_PKT_TYPE_BEACON:
		case AR5K_PKT_TYPE_PROBE_RESP:
			frame_type = AR5K_AR5210_TX_DESC_FRAME_TYPE_NO_DELAY;
			break;
		case AR5K_PKT_TYPE_PIFS:
			frame_type = AR5K_AR5210_TX_DESC_FRAME_TYPE_PIFS;
			break;
		default:
			frame_type = type;
			break;
		}

		tx_ctl->tx_control_0 |=
		AR5K_REG_SM(frame_type, AR5K_2W_TX_DESC_CTL0_FRAME_TYPE_5210) |
		AR5K_REG_SM(tx_rate0, AR5K_2W_TX_DESC_CTL0_XMIT_RATE);

	} else {
		tx_ctl->tx_control_0 |=
			AR5K_REG_SM(tx_rate0, AR5K_2W_TX_DESC_CTL0_XMIT_RATE) |
			AR5K_REG_SM(antenna_mode,
				AR5K_2W_TX_DESC_CTL0_ANT_MODE_XMIT);
		tx_ctl->tx_control_1 |=
			AR5K_REG_SM(type, AR5K_2W_TX_DESC_CTL1_FRAME_TYPE_5211);
	}

#define _TX_FLAGS(_c, _flag)					\
	if (flags & AR5K_TXDESC_##_flag) {			\
		tx_ctl->tx_control_##_c |=			\
			AR5K_2W_TX_DESC_CTL##_c##_##_flag;	\
	}
#define _TX_FLAGS_5211(_c, _flag)					\
	if (flags & AR5K_TXDESC_##_flag) {				\
		tx_ctl->tx_control_##_c |=				\
			AR5K_2W_TX_DESC_CTL##_c##_##_flag##_5211;	\
	}
	_TX_FLAGS(0, CLRDMASK);
	_TX_FLAGS(0, INTREQ);
	_TX_FLAGS(0, RTSENA);

	if (ah->ah_version == AR5K_AR5211) {
		_TX_FLAGS_5211(0, VEOL);
		_TX_FLAGS_5211(1, NOACK);
	}

#undef _TX_FLAGS
#undef _TX_FLAGS_5211

	/*
	 * WEP crap
	 */
	if (key_index != AR5K_TXKEYIX_INVALID) {
		tx_ctl->tx_control_0 |=
			AR5K_2W_TX_DESC_CTL0_ENCRYPT_KEY_VALID;
		tx_ctl->tx_control_1 |=
			AR5K_REG_SM(key_index,
			AR5K_2W_TX_DESC_CTL1_ENC_KEY_IDX);
	}

	/*
	 * RTS/CTS Duration [5210 ?]
	 */
	if ((ah->ah_version == AR5K_AR5210) &&
			(flags & (AR5K_TXDESC_RTSENA | AR5K_TXDESC_CTSENA)))
		tx_ctl->tx_control_1 |= rtscts_duration &
				AR5K_2W_TX_DESC_CTL1_RTS_DURATION_5210;

	return 0;
}

/**
 * ath5k_hw_setup_4word_tx_desc() - Initialize a 4-word tx control descriptor
 * @ah: The &struct ath5k_hw
 * @desc: The &struct ath5k_desc
 * @pkt_len: Frame length in bytes
 * @hdr_len: Header length in bytes (only used on AR5210)
 * @padsize: Any padding we've added to the frame length
 * @type: One of enum ath5k_pkt_type
 * @tx_power: Tx power in 0.5dB steps
 * @tx_rate0: HW idx for transmission rate
 * @tx_tries0: Max number of retransmissions
 * @key_index: Index on key table to use for encryption
 * @antenna_mode: Which antenna to use (0 for auto)
 * @flags: One of AR5K_TXDESC_* flags (desc.h)
 * @rtscts_rate: HW idx for RTS/CTS transmission rate
 * @rtscts_duration: What to put on duration field on the header of RTS/CTS
 *
 * Internal function to initialize a 4-Word TX control descriptor
 * found on AR5212 and later MACs chips.
 *
 * Returns 0 on success or -EINVAL on false input
 */
static int
ath5k_hw_setup_4word_tx_desc(struct ath5k_hw *ah,
			struct ath5k_desc *desc,
			unsigned int pkt_len, unsigned int hdr_len,
			int padsize,
			enum ath5k_pkt_type type,
			unsigned int tx_power,
			unsigned int tx_rate0, unsigned int tx_tries0,
			unsigned int key_index,
			unsigned int antenna_mode,
			unsigned int flags,
			unsigned int rtscts_rate, unsigned int rtscts_duration)
{
	struct ath5k_hw_4w_tx_ctl *tx_ctl;
	unsigned int frame_len;

	/*
	 * Use local variables for these to reduce load/store access on
	 * uncached memory
	 */
	u32 txctl0 = 0, txctl1 = 0, txctl2 = 0, txctl3 = 0;

	tx_ctl = &desc->ud.ds_tx5212.tx_ctl;

	/*
	 * Validate input
	 * - Zero retries don't make sense.
	 * - A zero rate will put the HW into a mode where it continuously sends
	 *   noise on the channel, so it is important to avoid this.
	 */
	if (unlikely(tx_tries0 == 0)) {
		ATH5K_ERR(ah, "zero retries\n");
		WARN_ON(1);
		return -EINVAL;
	}
	if (unlikely(tx_rate0 == 0)) {
		ATH5K_ERR(ah, "zero rate\n");
		WARN_ON(1);
		return -EINVAL;
	}

	tx_power += ah->ah_txpower.txp_offset;
	if (tx_power > AR5K_TUNE_MAX_TXPOWER)
		tx_power = AR5K_TUNE_MAX_TXPOWER;

	/* Clear descriptor status area */
	memset(&desc->ud.ds_tx5212.tx_stat, 0,
	       sizeof(desc->ud.ds_tx5212.tx_stat));

	/* Setup control descriptor */

	/* Verify and set frame length */

	/* remove padding we might have added before */
	frame_len = pkt_len - padsize + FCS_LEN;

	if (frame_len & ~AR5K_4W_TX_DESC_CTL0_FRAME_LEN)
		return -EINVAL;

	txctl0 = frame_len & AR5K_4W_TX_DESC_CTL0_FRAME_LEN;

	/* Verify and set buffer length */

	/* NB: beacon's BufLen must be a multiple of 4 bytes */
	if (type == AR5K_PKT_TYPE_BEACON)
		pkt_len = roundup(pkt_len, 4);

	if (pkt_len & ~AR5K_4W_TX_DESC_CTL1_BUF_LEN)
		return -EINVAL;

	txctl1 = pkt_len & AR5K_4W_TX_DESC_CTL1_BUF_LEN;

	txctl0 |= AR5K_REG_SM(tx_power, AR5K_4W_TX_DESC_CTL0_XMIT_POWER) |
		  AR5K_REG_SM(antenna_mode, AR5K_4W_TX_DESC_CTL0_ANT_MODE_XMIT);
	txctl1 |= AR5K_REG_SM(type, AR5K_4W_TX_DESC_CTL1_FRAME_TYPE);
	txctl2 = AR5K_REG_SM(tx_tries0, AR5K_4W_TX_DESC_CTL2_XMIT_TRIES0);
	txctl3 = tx_rate0 & AR5K_4W_TX_DESC_CTL3_XMIT_RATE0;

#define _TX_FLAGS(_c, _flag)					\
	if (flags & AR5K_TXDESC_##_flag) {			\
		txctl##_c |= AR5K_4W_TX_DESC_CTL##_c##_##_flag;	\
	}

	_TX_FLAGS(0, CLRDMASK);
	_TX_FLAGS(0, VEOL);
	_TX_FLAGS(0, INTREQ);
	_TX_FLAGS(0, RTSENA);
	_TX_FLAGS(0, CTSENA);
	_TX_FLAGS(1, NOACK);

#undef _TX_FLAGS

	/*
	 * WEP crap
	 */
	if (key_index != AR5K_TXKEYIX_INVALID) {
		txctl0 |= AR5K_4W_TX_DESC_CTL0_ENCRYPT_KEY_VALID;
		txctl1 |= AR5K_REG_SM(key_index,
				AR5K_4W_TX_DESC_CTL1_ENCRYPT_KEY_IDX);
	}

	/*
	 * RTS/CTS
	 */
	if (flags & (AR5K_TXDESC_RTSENA | AR5K_TXDESC_CTSENA)) {
		if ((flags & AR5K_TXDESC_RTSENA) &&
				(flags & AR5K_TXDESC_CTSENA))
			return -EINVAL;
		txctl2 |= rtscts_duration & AR5K_4W_TX_DESC_CTL2_RTS_DURATION;
		txctl3 |= AR5K_REG_SM(rtscts_rate,
				AR5K_4W_TX_DESC_CTL3_RTS_CTS_RATE);
	}

	tx_ctl->tx_control_0 = txctl0;
	tx_ctl->tx_control_1 = txctl1;
	tx_ctl->tx_control_2 = txctl2;
	tx_ctl->tx_control_3 = txctl3;

	return 0;
}

/**
 * ath5k_hw_setup_mrr_tx_desc() - Initialize an MRR tx control descriptor
 * @ah: The &struct ath5k_hw
 * @desc: The &struct ath5k_desc
 * @tx_rate1: HW idx for rate used on transmission series 1
 * @tx_tries1: Max number of retransmissions for transmission series 1
 * @tx_rate2: HW idx for rate used on transmission series 2
 * @tx_tries2: Max number of retransmissions for transmission series 2
 * @tx_rate3: HW idx for rate used on transmission series 3
 * @tx_tries3: Max number of retransmissions for transmission series 3
 *
 * Multi rate retry (MRR) tx control descriptors are available only on AR5212
 * MACs, they are part of the normal 4-word tx control descriptor (see above)
 * but we handle them through a separate function for better abstraction.
 *
 * Returns 0 on success or -EINVAL on invalid input
 */
int
ath5k_hw_setup_mrr_tx_desc(struct ath5k_hw *ah,
			struct ath5k_desc *desc,
			u_int tx_rate1, u_int tx_tries1,
			u_int tx_rate2, u_int tx_tries2,
			u_int tx_rate3, u_int tx_tries3)
{
	struct ath5k_hw_4w_tx_ctl *tx_ctl;

	/* no mrr support for cards older than 5212 */
	if (ah->ah_version < AR5K_AR5212)
		return 0;

	/*
	 * Rates can be 0 as long as the retry count is 0 too.
	 * A zero rate and nonzero retry count will put the HW into a mode where
	 * it continuously sends noise on the channel, so it is important to
	 * avoid this.
	 */
	if (unlikely((tx_rate1 == 0 && tx_tries1 != 0) ||
		     (tx_rate2 == 0 && tx_tries2 != 0) ||
		     (tx_rate3 == 0 && tx_tries3 != 0))) {
		ATH5K_ERR(ah, "zero rate\n");
		WARN_ON(1);
		return -EINVAL;
	}

	if (ah->ah_version == AR5K_AR5212) {
		tx_ctl = &desc->ud.ds_tx5212.tx_ctl;

#define _XTX_TRIES(_n)							\
	if (tx_tries##_n) {						\
		tx_ctl->tx_control_2 |=					\
		    AR5K_REG_SM(tx_tries##_n,				\
		    AR5K_4W_TX_DESC_CTL2_XMIT_TRIES##_n);		\
		tx_ctl->tx_control_3 |=					\
		    AR5K_REG_SM(tx_rate##_n,				\
		    AR5K_4W_TX_DESC_CTL3_XMIT_RATE##_n);		\
	}

		_XTX_TRIES(1);
		_XTX_TRIES(2);
		_XTX_TRIES(3);

#undef _XTX_TRIES

		return 1;
	}

	return 0;
}


/***********************\
* TX Status descriptors *
\***********************/

/**
 * ath5k_hw_proc_2word_tx_status() - Process a tx status descriptor on 5210/1
 * @ah: The &struct ath5k_hw
 * @desc: The &struct ath5k_desc
 * @ts: The &struct ath5k_tx_status
 */
static int
ath5k_hw_proc_2word_tx_status(struct ath5k_hw *ah,
				struct ath5k_desc *desc,
				struct ath5k_tx_status *ts)
{
	struct ath5k_hw_tx_status *tx_status;

	tx_status = &desc->ud.ds_tx5210.tx_stat;

	/* No frame has been send or error */
	if (unlikely((tx_status->tx_status_1 & AR5K_DESC_TX_STATUS1_DONE) == 0))
		return -EINPROGRESS;

	/*
	 * Get descriptor status
	 */
	ts->ts_tstamp = AR5K_REG_MS(tx_status->tx_status_0,
		AR5K_DESC_TX_STATUS0_SEND_TIMESTAMP);
	ts->ts_shortretry = AR5K_REG_MS(tx_status->tx_status_0,
		AR5K_DESC_TX_STATUS0_SHORT_RETRY_COUNT);
	ts->ts_final_retry = AR5K_REG_MS(tx_status->tx_status_0,
		AR5K_DESC_TX_STATUS0_LONG_RETRY_COUNT);
	/*TODO: ts->ts_virtcol + test*/
	ts->ts_seqnum = AR5K_REG_MS(tx_status->tx_status_1,
		AR5K_DESC_TX_STATUS1_SEQ_NUM);
	ts->ts_rssi = AR5K_REG_MS(tx_status->tx_status_1,
		AR5K_DESC_TX_STATUS1_ACK_SIG_STRENGTH);
	ts->ts_antenna = 1;
	ts->ts_status = 0;
	ts->ts_final_idx = 0;

	if (!(tx_status->tx_status_0 & AR5K_DESC_TX_STATUS0_FRAME_XMIT_OK)) {
		if (tx_status->tx_status_0 &
				AR5K_DESC_TX_STATUS0_EXCESSIVE_RETRIES)
			ts->ts_status |= AR5K_TXERR_XRETRY;

		if (tx_status->tx_status_0 & AR5K_DESC_TX_STATUS0_FIFO_UNDERRUN)
			ts->ts_status |= AR5K_TXERR_FIFO;

		if (tx_status->tx_status_0 & AR5K_DESC_TX_STATUS0_FILTERED)
			ts->ts_status |= AR5K_TXERR_FILT;
	}

	return 0;
}

/**
 * ath5k_hw_proc_4word_tx_status() - Process a tx status descriptor on 5212
 * @ah: The &struct ath5k_hw
 * @desc: The &struct ath5k_desc
 * @ts: The &struct ath5k_tx_status
 */
static int
ath5k_hw_proc_4word_tx_status(struct ath5k_hw *ah,
				struct ath5k_desc *desc,
				struct ath5k_tx_status *ts)
{
	struct ath5k_hw_tx_status *tx_status;
	u32 txstat0, txstat1;

	tx_status = &desc->ud.ds_tx5212.tx_stat;

	txstat1 = ACCESS_ONCE(tx_status->tx_status_1);

	/* No frame has been send or error */
	if (unlikely(!(txstat1 & AR5K_DESC_TX_STATUS1_DONE)))
		return -EINPROGRESS;

	txstat0 = ACCESS_ONCE(tx_status->tx_status_0);

	/*
	 * Get descriptor status
	 */
	ts->ts_tstamp = AR5K_REG_MS(txstat0,
		AR5K_DESC_TX_STATUS0_SEND_TIMESTAMP);
	ts->ts_shortretry = AR5K_REG_MS(txstat0,
		AR5K_DESC_TX_STATUS0_SHORT_RETRY_COUNT);
	ts->ts_final_retry = AR5K_REG_MS(txstat0,
		AR5K_DESC_TX_STATUS0_LONG_RETRY_COUNT);
	ts->ts_seqnum = AR5K_REG_MS(txstat1,
		AR5K_DESC_TX_STATUS1_SEQ_NUM);
	ts->ts_rssi = AR5K_REG_MS(txstat1,
		AR5K_DESC_TX_STATUS1_ACK_SIG_STRENGTH);
	ts->ts_antenna = (txstat1 &
		AR5K_DESC_TX_STATUS1_XMIT_ANTENNA_5212) ? 2 : 1;
	ts->ts_status = 0;

	ts->ts_final_idx = AR5K_REG_MS(txstat1,
			AR5K_DESC_TX_STATUS1_FINAL_TS_IX_5212);

	/* TX error */
	if (!(txstat0 & AR5K_DESC_TX_STATUS0_FRAME_XMIT_OK)) {
		if (txstat0 & AR5K_DESC_TX_STATUS0_EXCESSIVE_RETRIES)
			ts->ts_status |= AR5K_TXERR_XRETRY;

		if (txstat0 & AR5K_DESC_TX_STATUS0_FIFO_UNDERRUN)
			ts->ts_status |= AR5K_TXERR_FIFO;

		if (txstat0 & AR5K_DESC_TX_STATUS0_FILTERED)
			ts->ts_status |= AR5K_TXERR_FILT;
	}

	return 0;
}


/****************\
* RX Descriptors *
\****************/

/**
 * ath5k_hw_setup_rx_desc() - Initialize an rx control descriptor
 * @ah: The &struct ath5k_hw
 * @desc: The &struct ath5k_desc
 * @size: RX buffer length in bytes
 * @flags: One of AR5K_RXDESC_* flags
 */
int
ath5k_hw_setup_rx_desc(struct ath5k_hw *ah,
			struct ath5k_desc *desc,
			u32 size, unsigned int flags)
{
	struct ath5k_hw_rx_ctl *rx_ctl;

	rx_ctl = &desc->ud.ds_rx.rx_ctl;

	/*
	 * Clear the descriptor
	 * If we don't clean the status descriptor,
	 * while scanning we get too many results,
	 * most of them virtual, after some secs
	 * of scanning system hangs. M.F.
	*/
	memset(&desc->ud.ds_rx, 0, sizeof(struct ath5k_hw_all_rx_desc));

	if (unlikely(size & ~AR5K_DESC_RX_CTL1_BUF_LEN))
		return -EINVAL;

	/* Setup descriptor */
	rx_ctl->rx_control_1 = size & AR5K_DESC_RX_CTL1_BUF_LEN;

	if (flags & AR5K_RXDESC_INTREQ)
		rx_ctl->rx_control_1 |= AR5K_DESC_RX_CTL1_INTREQ;

	return 0;
}

/**
 * ath5k_hw_proc_5210_rx_status() - Process the rx status descriptor on 5210/1
 * @ah: The &struct ath5k_hw
 * @desc: The &struct ath5k_desc
 * @rs: The &struct ath5k_rx_status
 *
 * Internal function used to process an RX status descriptor
 * on AR5210/5211 MAC.
 *
 * Returns 0 on success or -EINPROGRESS in case we haven't received the who;e
 * frame yet.
 */
static int
ath5k_hw_proc_5210_rx_status(struct ath5k_hw *ah,
				struct ath5k_desc *desc,
				struct ath5k_rx_status *rs)
{
	struct ath5k_hw_rx_status *rx_status;

	rx_status = &desc->ud.ds_rx.rx_stat;

	/* No frame received / not ready */
	if (unlikely(!(rx_status->rx_status_1 &
			AR5K_5210_RX_DESC_STATUS1_DONE)))
		return -EINPROGRESS;

	memset(rs, 0, sizeof(struct ath5k_rx_status));

	/*
	 * Frame receive status
	 */
	rs->rs_datalen = rx_status->rx_status_0 &
		AR5K_5210_RX_DESC_STATUS0_DATA_LEN;
	rs->rs_rssi = AR5K_REG_MS(rx_status->rx_status_0,
		AR5K_5210_RX_DESC_STATUS0_RECEIVE_SIGNAL);
	rs->rs_rate = AR5K_REG_MS(rx_status->rx_status_0,
		AR5K_5210_RX_DESC_STATUS0_RECEIVE_RATE);
	rs->rs_more = !!(rx_status->rx_status_0 &
		AR5K_5210_RX_DESC_STATUS0_MORE);
	/* TODO: this timestamp is 13 bit, later on we assume 15 bit!
	 * also the HAL code for 5210 says the timestamp is bits [10..22] of the
	 * TSF, and extends the timestamp here to 15 bit.
	 * we need to check on 5210...
	 */
	rs->rs_tstamp = AR5K_REG_MS(rx_status->rx_status_1,
		AR5K_5210_RX_DESC_STATUS1_RECEIVE_TIMESTAMP);

	if (ah->ah_version == AR5K_AR5211)
		rs->rs_antenna = AR5K_REG_MS(rx_status->rx_status_0,
				AR5K_5210_RX_DESC_STATUS0_RECEIVE_ANT_5211);
	else
		rs->rs_antenna = (rx_status->rx_status_0 &
				AR5K_5210_RX_DESC_STATUS0_RECEIVE_ANT_5210)
				? 2 : 1;

	/*
	 * Key table status
	 */
	if (rx_status->rx_status_1 & AR5K_5210_RX_DESC_STATUS1_KEY_INDEX_VALID)
		rs->rs_keyix = AR5K_REG_MS(rx_status->rx_status_1,
			AR5K_5210_RX_DESC_STATUS1_KEY_INDEX);
	else
		rs->rs_keyix = AR5K_RXKEYIX_INVALID;

	/*
	 * Receive/descriptor errors
	 */
	if (!(rx_status->rx_status_1 &
			AR5K_5210_RX_DESC_STATUS1_FRAME_RECEIVE_OK)) {
		if (rx_status->rx_status_1 &
				AR5K_5210_RX_DESC_STATUS1_CRC_ERROR)
			rs->rs_status |= AR5K_RXERR_CRC;

		/* only on 5210 */
		if ((ah->ah_version == AR5K_AR5210) &&
		    (rx_status->rx_status_1 &
				AR5K_5210_RX_DESC_STATUS1_FIFO_OVERRUN_5210))
			rs->rs_status |= AR5K_RXERR_FIFO;

		if (rx_status->rx_status_1 &
				AR5K_5210_RX_DESC_STATUS1_PHY_ERROR) {
			rs->rs_status |= AR5K_RXERR_PHY;
			rs->rs_phyerr = AR5K_REG_MS(rx_status->rx_status_1,
				AR5K_5210_RX_DESC_STATUS1_PHY_ERROR);
		}

		if (rx_status->rx_status_1 &
				AR5K_5210_RX_DESC_STATUS1_DECRYPT_CRC_ERROR)
			rs->rs_status |= AR5K_RXERR_DECRYPT;
	}

	return 0;
}

/**
 * ath5k_hw_proc_5212_rx_status() - Process the rx status descriptor on 5212
 * @ah: The &struct ath5k_hw
 * @desc: The &struct ath5k_desc
 * @rs: The &struct ath5k_rx_status
 *
 * Internal function used to process an RX status descriptor
 * on AR5212 and later MAC.
 *
 * Returns 0 on success or -EINPROGRESS in case we haven't received the who;e
 * frame yet.
 */
static int
ath5k_hw_proc_5212_rx_status(struct ath5k_hw *ah,
				struct ath5k_desc *desc,
				struct ath5k_rx_status *rs)
{
	struct ath5k_hw_rx_status *rx_status;
	u32 rxstat0, rxstat1;

	rx_status = &desc->ud.ds_rx.rx_stat;
	rxstat1 = ACCESS_ONCE(rx_status->rx_status_1);

	/* No frame received / not ready */
	if (unlikely(!(rxstat1 & AR5K_5212_RX_DESC_STATUS1_DONE)))
		return -EINPROGRESS;

	memset(rs, 0, sizeof(struct ath5k_rx_status));
	rxstat0 = ACCESS_ONCE(rx_status->rx_status_0);

	/*
	 * Frame receive status
	 */
	rs->rs_datalen = rxstat0 & AR5K_5212_RX_DESC_STATUS0_DATA_LEN;
	rs->rs_rssi = AR5K_REG_MS(rxstat0,
		AR5K_5212_RX_DESC_STATUS0_RECEIVE_SIGNAL);
	rs->rs_rate = AR5K_REG_MS(rxstat0,
		AR5K_5212_RX_DESC_STATUS0_RECEIVE_RATE);
	rs->rs_antenna = AR5K_REG_MS(rxstat0,
		AR5K_5212_RX_DESC_STATUS0_RECEIVE_ANTENNA);
	rs->rs_more = !!(rxstat0 & AR5K_5212_RX_DESC_STATUS0_MORE);
	rs->rs_tstamp = AR5K_REG_MS(rxstat1,
		AR5K_5212_RX_DESC_STATUS1_RECEIVE_TIMESTAMP);

	/*
	 * Key table status
	 */
	if (rxstat1 & AR5K_5212_RX_DESC_STATUS1_KEY_INDEX_VALID)
		rs->rs_keyix = AR5K_REG_MS(rxstat1,
					   AR5K_5212_RX_DESC_STATUS1_KEY_INDEX);
	else
		rs->rs_keyix = AR5K_RXKEYIX_INVALID;

	/*
	 * Receive/descriptor errors
	 */
	if (!(rxstat1 & AR5K_5212_RX_DESC_STATUS1_FRAME_RECEIVE_OK)) {
		if (rxstat1 & AR5K_5212_RX_DESC_STATUS1_CRC_ERROR)
			rs->rs_status |= AR5K_RXERR_CRC;

		if (rxstat1 & AR5K_5212_RX_DESC_STATUS1_PHY_ERROR) {
			rs->rs_status |= AR5K_RXERR_PHY;
			rs->rs_phyerr = AR5K_REG_MS(rxstat1,
				AR5K_5212_RX_DESC_STATUS1_PHY_ERROR_CODE);
			if (!ah->ah_capabilities.cap_has_phyerr_counters)
				ath5k_ani_phy_error_report(ah, rs->rs_phyerr);
		}

		if (rxstat1 & AR5K_5212_RX_DESC_STATUS1_DECRYPT_CRC_ERROR)
			rs->rs_status |= AR5K_RXERR_DECRYPT;

		if (rxstat1 & AR5K_5212_RX_DESC_STATUS1_MIC_ERROR)
			rs->rs_status |= AR5K_RXERR_MIC;
	}
	return 0;
}


/********\
* Attach *
\********/

/**
 * ath5k_hw_init_desc_functions() - Init function pointers inside ah
 * @ah: The &struct ath5k_hw
 *
 * Maps the internal descriptor functions to the function pointers on ah, used
 * from above. This is used as an abstraction layer to handle the various chips
 * the same way.
 */
int
ath5k_hw_init_desc_functions(struct ath5k_hw *ah)
{
	if (ah->ah_version == AR5K_AR5212) {
		ah->ah_setup_tx_desc = ath5k_hw_setup_4word_tx_desc;
		ah->ah_proc_tx_desc = ath5k_hw_proc_4word_tx_status;
		ah->ah_proc_rx_desc = ath5k_hw_proc_5212_rx_status;
	} else if (ah->ah_version <= AR5K_AR5211) {
		ah->ah_setup_tx_desc = ath5k_hw_setup_2word_tx_desc;
		ah->ah_proc_tx_desc = ath5k_hw_proc_2word_tx_status;
		ah->ah_proc_rx_desc = ath5k_hw_proc_5210_rx_status;
	} else
		return -ENOTSUPP;
	return 0;
}
