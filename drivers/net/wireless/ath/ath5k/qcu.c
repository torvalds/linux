/*
 * Copyright (c) 2004-2008 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006-2008 Nick Kossifidis <mickflemm@gmail.com>
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

/********************************************\
Queue Control Unit, DCF Control Unit Functions
\********************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ath5k.h"
#include "reg.h"
#include "debug.h"
#include <linux/log2.h>

/**
 * DOC: Queue Control Unit (QCU)/DCF Control Unit (DCU) functions
 *
 * Here we setup parameters for the 12 available TX queues. Note that
 * on the various registers we can usually only map the first 10 of them so
 * basically we have 10 queues to play with. Each queue has a matching
 * QCU that controls when the queue will get triggered and multiple QCUs
 * can be mapped to a single DCU that controls the various DFS parameters
 * for the various queues. In our setup we have a 1:1 mapping between QCUs
 * and DCUs allowing us to have different DFS settings for each queue.
 *
 * When a frame goes into a TX queue, QCU decides when it'll trigger a
 * transmission based on various criteria (such as how many data we have inside
 * it's buffer or -if it's a beacon queue- if it's time to fire up the queue
 * based on TSF etc), DCU adds backoff, IFSes etc and then a scheduler
 * (arbitrator) decides the priority of each QCU based on it's configuration
 * (e.g. beacons are always transmitted when they leave DCU bypassing all other
 * frames from other queues waiting to be transmitted). After a frame leaves
 * the DCU it goes to PCU for further processing and then to PHY for
 * the actual transmission.
 */


/******************\
* Helper functions *
\******************/

/**
 * ath5k_hw_num_tx_pending() - Get number of pending frames for a  given queue
 * @ah: The &struct ath5k_hw
 * @queue: One of enum ath5k_tx_queue_id
 */
u32
ath5k_hw_num_tx_pending(struct ath5k_hw *ah, unsigned int queue)
{
	u32 pending;
	AR5K_ASSERT_ENTRY(queue, ah->ah_capabilities.cap_queues.q_tx_num);

	/* Return if queue is declared inactive */
	if (ah->ah_txq[queue].tqi_type == AR5K_TX_QUEUE_INACTIVE)
		return false;

	/* XXX: How about AR5K_CFG_TXCNT ? */
	if (ah->ah_version == AR5K_AR5210)
		return false;

	pending = ath5k_hw_reg_read(ah, AR5K_QUEUE_STATUS(queue));
	pending &= AR5K_QCU_STS_FRMPENDCNT;

	/* It's possible to have no frames pending even if TXE
	 * is set. To indicate that q has not stopped return
	 * true */
	if (!pending && AR5K_REG_READ_Q(ah, AR5K_QCU_TXE, queue))
		return true;

	return pending;
}

/**
 * ath5k_hw_release_tx_queue() - Set a transmit queue inactive
 * @ah: The &struct ath5k_hw
 * @queue: One of enum ath5k_tx_queue_id
 */
void
ath5k_hw_release_tx_queue(struct ath5k_hw *ah, unsigned int queue)
{
	if (WARN_ON(queue >= ah->ah_capabilities.cap_queues.q_tx_num))
		return;

	/* This queue will be skipped in further operations */
	ah->ah_txq[queue].tqi_type = AR5K_TX_QUEUE_INACTIVE;
	/*For SIMR setup*/
	AR5K_Q_DISABLE_BITS(ah->ah_txq_status, queue);
}

/**
 * ath5k_cw_validate() - Make sure the given cw is valid
 * @cw_req: The contention window value to check
 *
 * Make sure cw is a power of 2 minus 1 and smaller than 1024
 */
static u16
ath5k_cw_validate(u16 cw_req)
{
	cw_req = min(cw_req, (u16)1023);

	/* Check if cw_req + 1 a power of 2 */
	if (is_power_of_2(cw_req + 1))
		return cw_req;

	/* Check if cw_req is a power of 2 */
	if (is_power_of_2(cw_req))
		return cw_req - 1;

	/* If none of the above is correct
	 * find the closest power of 2 */
	cw_req = (u16) roundup_pow_of_two(cw_req) - 1;

	return cw_req;
}

/**
 * ath5k_hw_get_tx_queueprops() - Get properties for a transmit queue
 * @ah: The &struct ath5k_hw
 * @queue: One of enum ath5k_tx_queue_id
 * @queue_info: The &struct ath5k_txq_info to fill
 */
int
ath5k_hw_get_tx_queueprops(struct ath5k_hw *ah, int queue,
		struct ath5k_txq_info *queue_info)
{
	memcpy(queue_info, &ah->ah_txq[queue], sizeof(struct ath5k_txq_info));
	return 0;
}

/**
 * ath5k_hw_set_tx_queueprops() - Set properties for a transmit queue
 * @ah: The &struct ath5k_hw
 * @queue: One of enum ath5k_tx_queue_id
 * @qinfo: The &struct ath5k_txq_info to use
 *
 * Returns 0 on success or -EIO if queue is inactive
 */
int
ath5k_hw_set_tx_queueprops(struct ath5k_hw *ah, int queue,
				const struct ath5k_txq_info *qinfo)
{
	struct ath5k_txq_info *qi;

	AR5K_ASSERT_ENTRY(queue, ah->ah_capabilities.cap_queues.q_tx_num);

	qi = &ah->ah_txq[queue];

	if (qi->tqi_type == AR5K_TX_QUEUE_INACTIVE)
		return -EIO;

	/* copy and validate values */
	qi->tqi_type = qinfo->tqi_type;
	qi->tqi_subtype = qinfo->tqi_subtype;
	qi->tqi_flags = qinfo->tqi_flags;
	/*
	 * According to the docs: Although the AIFS field is 8 bit wide,
	 * the maximum supported value is 0xFC. Setting it higher than that
	 * will cause the DCU to hang.
	 */
	qi->tqi_aifs = min(qinfo->tqi_aifs, (u8)0xFC);
	qi->tqi_cw_min = ath5k_cw_validate(qinfo->tqi_cw_min);
	qi->tqi_cw_max = ath5k_cw_validate(qinfo->tqi_cw_max);
	qi->tqi_cbr_period = qinfo->tqi_cbr_period;
	qi->tqi_cbr_overflow_limit = qinfo->tqi_cbr_overflow_limit;
	qi->tqi_burst_time = qinfo->tqi_burst_time;
	qi->tqi_ready_time = qinfo->tqi_ready_time;

	/*XXX: Is this supported on 5210 ?*/
	/*XXX: Is this correct for AR5K_WME_AC_VI,VO ???*/
	if ((qinfo->tqi_type == AR5K_TX_QUEUE_DATA &&
		((qinfo->tqi_subtype == AR5K_WME_AC_VI) ||
		 (qinfo->tqi_subtype == AR5K_WME_AC_VO))) ||
	     qinfo->tqi_type == AR5K_TX_QUEUE_UAPSD)
		qi->tqi_flags |= AR5K_TXQ_FLAG_POST_FR_BKOFF_DIS;

	return 0;
}

/**
 * ath5k_hw_setup_tx_queue() - Initialize a transmit queue
 * @ah: The &struct ath5k_hw
 * @queue_type: One of enum ath5k_tx_queue
 * @queue_info: The &struct ath5k_txq_info to use
 *
 * Returns 0 on success, -EINVAL on invalid arguments
 */
int
ath5k_hw_setup_tx_queue(struct ath5k_hw *ah, enum ath5k_tx_queue queue_type,
		struct ath5k_txq_info *queue_info)
{
	unsigned int queue;
	int ret;

	/*
	 * Get queue by type
	 */
	/* 5210 only has 2 queues */
	if (ah->ah_capabilities.cap_queues.q_tx_num == 2) {
		switch (queue_type) {
		case AR5K_TX_QUEUE_DATA:
			queue = AR5K_TX_QUEUE_ID_NOQCU_DATA;
			break;
		case AR5K_TX_QUEUE_BEACON:
		case AR5K_TX_QUEUE_CAB:
			queue = AR5K_TX_QUEUE_ID_NOQCU_BEACON;
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (queue_type) {
		case AR5K_TX_QUEUE_DATA:
			for (queue = AR5K_TX_QUEUE_ID_DATA_MIN;
				ah->ah_txq[queue].tqi_type !=
				AR5K_TX_QUEUE_INACTIVE; queue++) {

				if (queue > AR5K_TX_QUEUE_ID_DATA_MAX)
					return -EINVAL;
			}
			break;
		case AR5K_TX_QUEUE_UAPSD:
			queue = AR5K_TX_QUEUE_ID_UAPSD;
			break;
		case AR5K_TX_QUEUE_BEACON:
			queue = AR5K_TX_QUEUE_ID_BEACON;
			break;
		case AR5K_TX_QUEUE_CAB:
			queue = AR5K_TX_QUEUE_ID_CAB;
			break;
		default:
			return -EINVAL;
		}
	}

	/*
	 * Setup internal queue structure
	 */
	memset(&ah->ah_txq[queue], 0, sizeof(struct ath5k_txq_info));
	ah->ah_txq[queue].tqi_type = queue_type;

	if (queue_info != NULL) {
		queue_info->tqi_type = queue_type;
		ret = ath5k_hw_set_tx_queueprops(ah, queue, queue_info);
		if (ret)
			return ret;
	}

	/*
	 * We use ah_txq_status to hold a temp value for
	 * the Secondary interrupt mask registers on 5211+
	 * check out ath5k_hw_reset_tx_queue
	 */
	AR5K_Q_ENABLE_BITS(ah->ah_txq_status, queue);

	return queue;
}


/*******************************\
* Single QCU/DCU initialization *
\*******************************/

/**
 * ath5k_hw_set_tx_retry_limits() - Set tx retry limits on DCU
 * @ah: The &struct ath5k_hw
 * @queue: One of enum ath5k_tx_queue_id
 *
 * This function is used when initializing a queue, to set
 * retry limits based on ah->ah_retry_* and the chipset used.
 */
void
ath5k_hw_set_tx_retry_limits(struct ath5k_hw *ah,
				  unsigned int queue)
{
	/* Single data queue on AR5210 */
	if (ah->ah_version == AR5K_AR5210) {
		struct ath5k_txq_info *tq = &ah->ah_txq[queue];

		if (queue > 0)
			return;

		ath5k_hw_reg_write(ah,
			(tq->tqi_cw_min << AR5K_NODCU_RETRY_LMT_CW_MIN_S)
			| AR5K_REG_SM(ah->ah_retry_long,
				      AR5K_NODCU_RETRY_LMT_SLG_RETRY)
			| AR5K_REG_SM(ah->ah_retry_short,
				      AR5K_NODCU_RETRY_LMT_SSH_RETRY)
			| AR5K_REG_SM(ah->ah_retry_long,
				      AR5K_NODCU_RETRY_LMT_LG_RETRY)
			| AR5K_REG_SM(ah->ah_retry_short,
				      AR5K_NODCU_RETRY_LMT_SH_RETRY),
			AR5K_NODCU_RETRY_LMT);
	/* DCU on AR5211+ */
	} else {
		ath5k_hw_reg_write(ah,
			AR5K_REG_SM(ah->ah_retry_long,
				    AR5K_DCU_RETRY_LMT_RTS)
			| AR5K_REG_SM(ah->ah_retry_long,
				      AR5K_DCU_RETRY_LMT_STA_RTS)
			| AR5K_REG_SM(max(ah->ah_retry_long, ah->ah_retry_short),
				      AR5K_DCU_RETRY_LMT_STA_DATA),
			AR5K_QUEUE_DFS_RETRY_LIMIT(queue));
	}
}

/**
 * ath5k_hw_reset_tx_queue() - Initialize a single hw queue
 * @ah: The &struct ath5k_hw
 * @queue: One of enum ath5k_tx_queue_id
 *
 * Set DCF properties for the given transmit queue on DCU
 * and configures all queue-specific parameters.
 */
int
ath5k_hw_reset_tx_queue(struct ath5k_hw *ah, unsigned int queue)
{
	struct ath5k_txq_info *tq = &ah->ah_txq[queue];

	AR5K_ASSERT_ENTRY(queue, ah->ah_capabilities.cap_queues.q_tx_num);

	tq = &ah->ah_txq[queue];

	/* Skip if queue inactive or if we are on AR5210
	 * that doesn't have QCU/DCU */
	if ((ah->ah_version == AR5K_AR5210) ||
	(tq->tqi_type == AR5K_TX_QUEUE_INACTIVE))
		return 0;

	/*
	 * Set contention window (cw_min/cw_max)
	 * and arbitrated interframe space (aifs)...
	 */
	ath5k_hw_reg_write(ah,
		AR5K_REG_SM(tq->tqi_cw_min, AR5K_DCU_LCL_IFS_CW_MIN) |
		AR5K_REG_SM(tq->tqi_cw_max, AR5K_DCU_LCL_IFS_CW_MAX) |
		AR5K_REG_SM(tq->tqi_aifs, AR5K_DCU_LCL_IFS_AIFS),
		AR5K_QUEUE_DFS_LOCAL_IFS(queue));

	/*
	 * Set tx retry limits for this queue
	 */
	ath5k_hw_set_tx_retry_limits(ah, queue);


	/*
	 * Set misc registers
	 */

	/* Enable DCU to wait for next fragment from QCU */
	AR5K_REG_ENABLE_BITS(ah, AR5K_QUEUE_DFS_MISC(queue),
				AR5K_DCU_MISC_FRAG_WAIT);

	/* On Maui and Spirit use the global seqnum on DCU */
	if (ah->ah_mac_version < AR5K_SREV_AR5211)
		AR5K_REG_ENABLE_BITS(ah, AR5K_QUEUE_DFS_MISC(queue),
					AR5K_DCU_MISC_SEQNUM_CTL);

	/* Constant bit rate period */
	if (tq->tqi_cbr_period) {
		ath5k_hw_reg_write(ah, AR5K_REG_SM(tq->tqi_cbr_period,
					AR5K_QCU_CBRCFG_INTVAL) |
					AR5K_REG_SM(tq->tqi_cbr_overflow_limit,
					AR5K_QCU_CBRCFG_ORN_THRES),
					AR5K_QUEUE_CBRCFG(queue));

		AR5K_REG_ENABLE_BITS(ah, AR5K_QUEUE_MISC(queue),
					AR5K_QCU_MISC_FRSHED_CBR);

		if (tq->tqi_cbr_overflow_limit)
			AR5K_REG_ENABLE_BITS(ah, AR5K_QUEUE_MISC(queue),
					AR5K_QCU_MISC_CBR_THRES_ENABLE);
	}

	/* Ready time interval */
	if (tq->tqi_ready_time && (tq->tqi_type != AR5K_TX_QUEUE_CAB))
		ath5k_hw_reg_write(ah, AR5K_REG_SM(tq->tqi_ready_time,
					AR5K_QCU_RDYTIMECFG_INTVAL) |
					AR5K_QCU_RDYTIMECFG_ENABLE,
					AR5K_QUEUE_RDYTIMECFG(queue));

	if (tq->tqi_burst_time) {
		ath5k_hw_reg_write(ah, AR5K_REG_SM(tq->tqi_burst_time,
					AR5K_DCU_CHAN_TIME_DUR) |
					AR5K_DCU_CHAN_TIME_ENABLE,
					AR5K_QUEUE_DFS_CHANNEL_TIME(queue));

		if (tq->tqi_flags & AR5K_TXQ_FLAG_RDYTIME_EXP_POLICY_ENABLE)
			AR5K_REG_ENABLE_BITS(ah, AR5K_QUEUE_MISC(queue),
					AR5K_QCU_MISC_RDY_VEOL_POLICY);
	}

	/* Enable/disable Post frame backoff */
	if (tq->tqi_flags & AR5K_TXQ_FLAG_BACKOFF_DISABLE)
		ath5k_hw_reg_write(ah, AR5K_DCU_MISC_POST_FR_BKOFF_DIS,
					AR5K_QUEUE_DFS_MISC(queue));

	/* Enable/disable fragmentation burst backoff */
	if (tq->tqi_flags & AR5K_TXQ_FLAG_FRAG_BURST_BACKOFF_ENABLE)
		ath5k_hw_reg_write(ah, AR5K_DCU_MISC_BACKOFF_FRAG,
					AR5K_QUEUE_DFS_MISC(queue));

	/*
	 * Set registers by queue type
	 */
	switch (tq->tqi_type) {
	case AR5K_TX_QUEUE_BEACON:
		AR5K_REG_ENABLE_BITS(ah, AR5K_QUEUE_MISC(queue),
				AR5K_QCU_MISC_FRSHED_DBA_GT |
				AR5K_QCU_MISC_CBREXP_BCN_DIS |
				AR5K_QCU_MISC_BCN_ENABLE);

		AR5K_REG_ENABLE_BITS(ah, AR5K_QUEUE_DFS_MISC(queue),
				(AR5K_DCU_MISC_ARBLOCK_CTL_GLOBAL <<
				AR5K_DCU_MISC_ARBLOCK_CTL_S) |
				AR5K_DCU_MISC_ARBLOCK_IGNORE |
				AR5K_DCU_MISC_POST_FR_BKOFF_DIS |
				AR5K_DCU_MISC_BCN_ENABLE);
		break;

	case AR5K_TX_QUEUE_CAB:
		/* XXX: use BCN_SENT_GT, if we can figure out how */
		AR5K_REG_ENABLE_BITS(ah, AR5K_QUEUE_MISC(queue),
					AR5K_QCU_MISC_FRSHED_DBA_GT |
					AR5K_QCU_MISC_CBREXP_DIS |
					AR5K_QCU_MISC_CBREXP_BCN_DIS);

		ath5k_hw_reg_write(ah, ((tq->tqi_ready_time -
					(AR5K_TUNE_SW_BEACON_RESP -
					AR5K_TUNE_DMA_BEACON_RESP) -
				AR5K_TUNE_ADDITIONAL_SWBA_BACKOFF) * 1024) |
					AR5K_QCU_RDYTIMECFG_ENABLE,
					AR5K_QUEUE_RDYTIMECFG(queue));

		AR5K_REG_ENABLE_BITS(ah, AR5K_QUEUE_DFS_MISC(queue),
					(AR5K_DCU_MISC_ARBLOCK_CTL_GLOBAL <<
					AR5K_DCU_MISC_ARBLOCK_CTL_S));
		break;

	case AR5K_TX_QUEUE_UAPSD:
		AR5K_REG_ENABLE_BITS(ah, AR5K_QUEUE_MISC(queue),
					AR5K_QCU_MISC_CBREXP_DIS);
		break;

	case AR5K_TX_QUEUE_DATA:
	default:
			break;
	}

	/* TODO: Handle frame compression */

	/*
	 * Enable interrupts for this tx queue
	 * in the secondary interrupt mask registers
	 */
	if (tq->tqi_flags & AR5K_TXQ_FLAG_TXOKINT_ENABLE)
		AR5K_Q_ENABLE_BITS(ah->ah_txq_imr_txok, queue);

	if (tq->tqi_flags & AR5K_TXQ_FLAG_TXERRINT_ENABLE)
		AR5K_Q_ENABLE_BITS(ah->ah_txq_imr_txerr, queue);

	if (tq->tqi_flags & AR5K_TXQ_FLAG_TXURNINT_ENABLE)
		AR5K_Q_ENABLE_BITS(ah->ah_txq_imr_txurn, queue);

	if (tq->tqi_flags & AR5K_TXQ_FLAG_TXDESCINT_ENABLE)
		AR5K_Q_ENABLE_BITS(ah->ah_txq_imr_txdesc, queue);

	if (tq->tqi_flags & AR5K_TXQ_FLAG_TXEOLINT_ENABLE)
		AR5K_Q_ENABLE_BITS(ah->ah_txq_imr_txeol, queue);

	if (tq->tqi_flags & AR5K_TXQ_FLAG_CBRORNINT_ENABLE)
		AR5K_Q_ENABLE_BITS(ah->ah_txq_imr_cbrorn, queue);

	if (tq->tqi_flags & AR5K_TXQ_FLAG_CBRURNINT_ENABLE)
		AR5K_Q_ENABLE_BITS(ah->ah_txq_imr_cbrurn, queue);

	if (tq->tqi_flags & AR5K_TXQ_FLAG_QTRIGINT_ENABLE)
		AR5K_Q_ENABLE_BITS(ah->ah_txq_imr_qtrig, queue);

	if (tq->tqi_flags & AR5K_TXQ_FLAG_TXNOFRMINT_ENABLE)
		AR5K_Q_ENABLE_BITS(ah->ah_txq_imr_nofrm, queue);

	/* Update secondary interrupt mask registers */

	/* Filter out inactive queues */
	ah->ah_txq_imr_txok &= ah->ah_txq_status;
	ah->ah_txq_imr_txerr &= ah->ah_txq_status;
	ah->ah_txq_imr_txurn &= ah->ah_txq_status;
	ah->ah_txq_imr_txdesc &= ah->ah_txq_status;
	ah->ah_txq_imr_txeol &= ah->ah_txq_status;
	ah->ah_txq_imr_cbrorn &= ah->ah_txq_status;
	ah->ah_txq_imr_cbrurn &= ah->ah_txq_status;
	ah->ah_txq_imr_qtrig &= ah->ah_txq_status;
	ah->ah_txq_imr_nofrm &= ah->ah_txq_status;

	ath5k_hw_reg_write(ah, AR5K_REG_SM(ah->ah_txq_imr_txok,
					AR5K_SIMR0_QCU_TXOK) |
					AR5K_REG_SM(ah->ah_txq_imr_txdesc,
					AR5K_SIMR0_QCU_TXDESC),
					AR5K_SIMR0);

	ath5k_hw_reg_write(ah, AR5K_REG_SM(ah->ah_txq_imr_txerr,
					AR5K_SIMR1_QCU_TXERR) |
					AR5K_REG_SM(ah->ah_txq_imr_txeol,
					AR5K_SIMR1_QCU_TXEOL),
					AR5K_SIMR1);

	/* Update SIMR2 but don't overwrite rest simr2 settings */
	AR5K_REG_DISABLE_BITS(ah, AR5K_SIMR2, AR5K_SIMR2_QCU_TXURN);
	AR5K_REG_ENABLE_BITS(ah, AR5K_SIMR2,
				AR5K_REG_SM(ah->ah_txq_imr_txurn,
				AR5K_SIMR2_QCU_TXURN));

	ath5k_hw_reg_write(ah, AR5K_REG_SM(ah->ah_txq_imr_cbrorn,
				AR5K_SIMR3_QCBRORN) |
				AR5K_REG_SM(ah->ah_txq_imr_cbrurn,
				AR5K_SIMR3_QCBRURN),
				AR5K_SIMR3);

	ath5k_hw_reg_write(ah, AR5K_REG_SM(ah->ah_txq_imr_qtrig,
				AR5K_SIMR4_QTRIG), AR5K_SIMR4);

	/* Set TXNOFRM_QCU for the queues with TXNOFRM enabled */
	ath5k_hw_reg_write(ah, AR5K_REG_SM(ah->ah_txq_imr_nofrm,
				AR5K_TXNOFRM_QCU), AR5K_TXNOFRM);

	/* No queue has TXNOFRM enabled, disable the interrupt
	 * by setting AR5K_TXNOFRM to zero */
	if (ah->ah_txq_imr_nofrm == 0)
		ath5k_hw_reg_write(ah, 0, AR5K_TXNOFRM);

	/* Set QCU mask for this DCU to save power */
	AR5K_REG_WRITE_Q(ah, AR5K_QUEUE_QCUMASK(queue), queue);

	return 0;
}


/**************************\
* Global QCU/DCU functions *
\**************************/

/**
 * ath5k_hw_set_ifs_intervals()  - Set global inter-frame spaces on DCU
 * @ah: The &struct ath5k_hw
 * @slot_time: Slot time in us
 *
 * Sets the global IFS intervals on DCU (also works on AR5210) for
 * the given slot time and the current bwmode.
 */
int ath5k_hw_set_ifs_intervals(struct ath5k_hw *ah, unsigned int slot_time)
{
	struct ieee80211_channel *channel = ah->ah_current_channel;
	enum ieee80211_band band;
	struct ieee80211_rate *rate;
	u32 ack_tx_time, eifs, eifs_clock, sifs, sifs_clock;
	u32 slot_time_clock = ath5k_hw_htoclock(ah, slot_time);

	if (slot_time < 6 || slot_time_clock > AR5K_SLOT_TIME_MAX)
		return -EINVAL;

	sifs = ath5k_hw_get_default_sifs(ah);
	sifs_clock = ath5k_hw_htoclock(ah, sifs - 2);

	/* EIFS
	 * Txtime of ack at lowest rate + SIFS + DIFS
	 * (DIFS = SIFS + 2 * Slot time)
	 *
	 * Note: HAL has some predefined values for EIFS
	 * Turbo:   (37 + 2 * 6)
	 * Default: (74 + 2 * 9)
	 * Half:    (149 + 2 * 13)
	 * Quarter: (298 + 2 * 21)
	 *
	 * (74 + 2 * 6) for AR5210 default and turbo !
	 *
	 * According to the formula we have
	 * ack_tx_time = 25 for turbo and
	 * ack_tx_time = 42.5 * clock multiplier
	 * for default/half/quarter.
	 *
	 * This can't be right, 42 is what we would get
	 * from ath5k_hw_get_frame_dur_for_bwmode or
	 * ieee80211_generic_frame_duration for zero frame
	 * length and without SIFS !
	 *
	 * Also we have different lowest rate for 802.11a
	 */
	if (channel->band == IEEE80211_BAND_5GHZ)
		band = IEEE80211_BAND_5GHZ;
	else
		band = IEEE80211_BAND_2GHZ;

	rate = &ah->sbands[band].bitrates[0];
	ack_tx_time = ath5k_hw_get_frame_duration(ah, band, 10, rate, false);

	/* ack_tx_time includes an SIFS already */
	eifs = ack_tx_time + sifs + 2 * slot_time;
	eifs_clock = ath5k_hw_htoclock(ah, eifs);

	/* Set IFS settings on AR5210 */
	if (ah->ah_version == AR5K_AR5210) {
		u32 pifs, pifs_clock, difs, difs_clock;

		/* Set slot time */
		ath5k_hw_reg_write(ah, slot_time_clock, AR5K_SLOT_TIME);

		/* Set EIFS */
		eifs_clock = AR5K_REG_SM(eifs_clock, AR5K_IFS1_EIFS);

		/* PIFS = Slot time + SIFS */
		pifs = slot_time + sifs;
		pifs_clock = ath5k_hw_htoclock(ah, pifs);
		pifs_clock = AR5K_REG_SM(pifs_clock, AR5K_IFS1_PIFS);

		/* DIFS = SIFS + 2 * Slot time */
		difs = sifs + 2 * slot_time;
		difs_clock = ath5k_hw_htoclock(ah, difs);

		/* Set SIFS/DIFS */
		ath5k_hw_reg_write(ah, (difs_clock <<
				AR5K_IFS0_DIFS_S) | sifs_clock,
				AR5K_IFS0);

		/* Set PIFS/EIFS and preserve AR5K_INIT_CARR_SENSE_EN */
		ath5k_hw_reg_write(ah, pifs_clock | eifs_clock |
				(AR5K_INIT_CARR_SENSE_EN << AR5K_IFS1_CS_EN_S),
				AR5K_IFS1);

		return 0;
	}

	/* Set IFS slot time */
	ath5k_hw_reg_write(ah, slot_time_clock, AR5K_DCU_GBL_IFS_SLOT);

	/* Set EIFS interval */
	ath5k_hw_reg_write(ah, eifs_clock, AR5K_DCU_GBL_IFS_EIFS);

	/* Set SIFS interval in usecs */
	AR5K_REG_WRITE_BITS(ah, AR5K_DCU_GBL_IFS_MISC,
				AR5K_DCU_GBL_IFS_MISC_SIFS_DUR_USEC,
				sifs);

	/* Set SIFS interval in clock cycles */
	ath5k_hw_reg_write(ah, sifs_clock, AR5K_DCU_GBL_IFS_SIFS);

	return 0;
}


/**
 * ath5k_hw_init_queues() - Initialize tx queues
 * @ah: The &struct ath5k_hw
 *
 * Initializes all tx queues based on information on
 * ah->ah_txq* set by the driver
 */
int
ath5k_hw_init_queues(struct ath5k_hw *ah)
{
	int i, ret;

	/* TODO: HW Compression support for data queues */
	/* TODO: Burst prefetch for data queues */

	/*
	 * Reset queues and start beacon timers at the end of the reset routine
	 * This also sets QCU mask on each DCU for 1:1 qcu to dcu mapping
	 * Note: If we want we can assign multiple qcus on one dcu.
	 */
	if (ah->ah_version != AR5K_AR5210)
		for (i = 0; i < ah->ah_capabilities.cap_queues.q_tx_num; i++) {
			ret = ath5k_hw_reset_tx_queue(ah, i);
			if (ret) {
				ATH5K_ERR(ah,
					"failed to reset TX queue #%d\n", i);
				return ret;
			}
		}
	else
		/* No QCU/DCU on AR5210, just set tx
		 * retry limits. We set IFS parameters
		 * on ath5k_hw_set_ifs_intervals */
		ath5k_hw_set_tx_retry_limits(ah, 0);

	/* Set the turbo flag when operating on 40MHz */
	if (ah->ah_bwmode == AR5K_BWMODE_40MHZ)
		AR5K_REG_ENABLE_BITS(ah, AR5K_DCU_GBL_IFS_MISC,
				AR5K_DCU_GBL_IFS_MISC_TURBO_MODE);

	/* If we didn't set IFS timings through
	 * ath5k_hw_set_coverage_class make sure
	 * we set them here */
	if (!ah->ah_coverage_class) {
		unsigned int slot_time = ath5k_hw_get_default_slottime(ah);
		ath5k_hw_set_ifs_intervals(ah, slot_time);
	}

	return 0;
}
