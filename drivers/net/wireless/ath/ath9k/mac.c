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

#include "hw.h"
#include "hw-ops.h"

static void ath9k_hw_set_txq_interrupts(struct ath_hw *ah,
					struct ath9k_tx_queue_info *qi)
{
	ath_dbg(ath9k_hw_common(ah), ATH_DBG_INTERRUPT,
		"tx ok 0x%x err 0x%x desc 0x%x eol 0x%x urn 0x%x\n",
		ah->txok_interrupt_mask, ah->txerr_interrupt_mask,
		ah->txdesc_interrupt_mask, ah->txeol_interrupt_mask,
		ah->txurn_interrupt_mask);

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_IMR_S0,
		  SM(ah->txok_interrupt_mask, AR_IMR_S0_QCU_TXOK)
		  | SM(ah->txdesc_interrupt_mask, AR_IMR_S0_QCU_TXDESC));
	REG_WRITE(ah, AR_IMR_S1,
		  SM(ah->txerr_interrupt_mask, AR_IMR_S1_QCU_TXERR)
		  | SM(ah->txeol_interrupt_mask, AR_IMR_S1_QCU_TXEOL));

	ah->imrs2_reg &= ~AR_IMR_S2_QCU_TXURN;
	ah->imrs2_reg |= (ah->txurn_interrupt_mask & AR_IMR_S2_QCU_TXURN);
	REG_WRITE(ah, AR_IMR_S2, ah->imrs2_reg);

	REGWRITE_BUFFER_FLUSH(ah);
}

u32 ath9k_hw_gettxbuf(struct ath_hw *ah, u32 q)
{
	return REG_READ(ah, AR_QTXDP(q));
}
EXPORT_SYMBOL(ath9k_hw_gettxbuf);

void ath9k_hw_puttxbuf(struct ath_hw *ah, u32 q, u32 txdp)
{
	REG_WRITE(ah, AR_QTXDP(q), txdp);
}
EXPORT_SYMBOL(ath9k_hw_puttxbuf);

void ath9k_hw_txstart(struct ath_hw *ah, u32 q)
{
	ath_dbg(ath9k_hw_common(ah), ATH_DBG_QUEUE,
		"Enable TXE on queue: %u\n", q);
	REG_WRITE(ah, AR_Q_TXE, 1 << q);
}
EXPORT_SYMBOL(ath9k_hw_txstart);

u32 ath9k_hw_numtxpending(struct ath_hw *ah, u32 q)
{
	u32 npend;

	npend = REG_READ(ah, AR_QSTS(q)) & AR_Q_STS_PEND_FR_CNT;
	if (npend == 0) {

		if (REG_READ(ah, AR_Q_TXE) & (1 << q))
			npend = 1;
	}

	return npend;
}
EXPORT_SYMBOL(ath9k_hw_numtxpending);

/**
 * ath9k_hw_updatetxtriglevel - adjusts the frame trigger level
 *
 * @ah: atheros hardware struct
 * @bIncTrigLevel: whether or not the frame trigger level should be updated
 *
 * The frame trigger level specifies the minimum number of bytes,
 * in units of 64 bytes, that must be DMA'ed into the PCU TX FIFO
 * before the PCU will initiate sending the frame on the air. This can
 * mean we initiate transmit before a full frame is on the PCU TX FIFO.
 * Resets to 0x1 (meaning 64 bytes or a full frame, whichever occurs
 * first)
 *
 * Caution must be taken to ensure to set the frame trigger level based
 * on the DMA request size. For example if the DMA request size is set to
 * 128 bytes the trigger level cannot exceed 6 * 64 = 384. This is because
 * there need to be enough space in the tx FIFO for the requested transfer
 * size. Hence the tx FIFO will stop with 512 - 128 = 384 bytes. If we set
 * the threshold to a value beyond 6, then the transmit will hang.
 *
 * Current dual   stream devices have a PCU TX FIFO size of 8 KB.
 * Current single stream devices have a PCU TX FIFO size of 4 KB, however,
 * there is a hardware issue which forces us to use 2 KB instead so the
 * frame trigger level must not exceed 2 KB for these chipsets.
 */
bool ath9k_hw_updatetxtriglevel(struct ath_hw *ah, bool bIncTrigLevel)
{
	u32 txcfg, curLevel, newLevel;

	if (ah->tx_trig_level >= ah->config.max_txtrig_level)
		return false;

	ath9k_hw_disable_interrupts(ah);

	txcfg = REG_READ(ah, AR_TXCFG);
	curLevel = MS(txcfg, AR_FTRIG);
	newLevel = curLevel;
	if (bIncTrigLevel) {
		if (curLevel < ah->config.max_txtrig_level)
			newLevel++;
	} else if (curLevel > MIN_TX_FIFO_THRESHOLD)
		newLevel--;
	if (newLevel != curLevel)
		REG_WRITE(ah, AR_TXCFG,
			  (txcfg & ~AR_FTRIG) | SM(newLevel, AR_FTRIG));

	ath9k_hw_enable_interrupts(ah);

	ah->tx_trig_level = newLevel;

	return newLevel != curLevel;
}
EXPORT_SYMBOL(ath9k_hw_updatetxtriglevel);

void ath9k_hw_abort_tx_dma(struct ath_hw *ah)
{
	int i, q;

	REG_WRITE(ah, AR_Q_TXD, AR_Q_TXD_M);

	REG_SET_BIT(ah, AR_PCU_MISC, AR_PCU_FORCE_QUIET_COLL | AR_PCU_CLEAR_VMF);
	REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_FORCE_CH_IDLE_HIGH);
	REG_SET_BIT(ah, AR_D_GBL_IFS_MISC, AR_D_GBL_IFS_MISC_IGNORE_BACKOFF);

	for (q = 0; q < AR_NUM_QCU; q++) {
		for (i = 0; i < 1000; i++) {
			if (i)
				udelay(5);

			if (!ath9k_hw_numtxpending(ah, q))
				break;
		}
	}

	REG_CLR_BIT(ah, AR_PCU_MISC, AR_PCU_FORCE_QUIET_COLL | AR_PCU_CLEAR_VMF);
	REG_CLR_BIT(ah, AR_DIAG_SW, AR_DIAG_FORCE_CH_IDLE_HIGH);
	REG_CLR_BIT(ah, AR_D_GBL_IFS_MISC, AR_D_GBL_IFS_MISC_IGNORE_BACKOFF);

	REG_WRITE(ah, AR_Q_TXD, 0);
}
EXPORT_SYMBOL(ath9k_hw_abort_tx_dma);

bool ath9k_hw_stop_dma_queue(struct ath_hw *ah, u32 q)
{
#define ATH9K_TX_STOP_DMA_TIMEOUT	1000    /* usec */
#define ATH9K_TIME_QUANTUM		100     /* usec */
	int wait_time = ATH9K_TX_STOP_DMA_TIMEOUT / ATH9K_TIME_QUANTUM;
	int wait;

	REG_WRITE(ah, AR_Q_TXD, 1 << q);

	for (wait = wait_time; wait != 0; wait--) {
		if (wait != wait_time)
			udelay(ATH9K_TIME_QUANTUM);

		if (ath9k_hw_numtxpending(ah, q) == 0)
			break;
	}

	REG_WRITE(ah, AR_Q_TXD, 0);

	return wait != 0;

#undef ATH9K_TX_STOP_DMA_TIMEOUT
#undef ATH9K_TIME_QUANTUM
}
EXPORT_SYMBOL(ath9k_hw_stop_dma_queue);

void ath9k_hw_gettxintrtxqs(struct ath_hw *ah, u32 *txqs)
{
	*txqs &= ah->intr_txqs;
	ah->intr_txqs &= ~(*txqs);
}
EXPORT_SYMBOL(ath9k_hw_gettxintrtxqs);

bool ath9k_hw_set_txq_props(struct ath_hw *ah, int q,
			    const struct ath9k_tx_queue_info *qinfo)
{
	u32 cw;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_tx_queue_info *qi;

	qi = &ah->txq[q];
	if (qi->tqi_type == ATH9K_TX_QUEUE_INACTIVE) {
		ath_dbg(common, ATH_DBG_QUEUE,
			"Set TXQ properties, inactive queue: %u\n", q);
		return false;
	}

	ath_dbg(common, ATH_DBG_QUEUE, "Set queue properties for: %u\n", q);

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
EXPORT_SYMBOL(ath9k_hw_set_txq_props);

bool ath9k_hw_get_txq_props(struct ath_hw *ah, int q,
			    struct ath9k_tx_queue_info *qinfo)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_tx_queue_info *qi;

	qi = &ah->txq[q];
	if (qi->tqi_type == ATH9K_TX_QUEUE_INACTIVE) {
		ath_dbg(common, ATH_DBG_QUEUE,
			"Get TXQ properties, inactive queue: %u\n", q);
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
EXPORT_SYMBOL(ath9k_hw_get_txq_props);

int ath9k_hw_setuptxqueue(struct ath_hw *ah, enum ath9k_tx_queue type,
			  const struct ath9k_tx_queue_info *qinfo)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_tx_queue_info *qi;
	int q;

	switch (type) {
	case ATH9K_TX_QUEUE_BEACON:
		q = ATH9K_NUM_TX_QUEUES - 1;
		break;
	case ATH9K_TX_QUEUE_CAB:
		q = ATH9K_NUM_TX_QUEUES - 2;
		break;
	case ATH9K_TX_QUEUE_PSPOLL:
		q = 1;
		break;
	case ATH9K_TX_QUEUE_UAPSD:
		q = ATH9K_NUM_TX_QUEUES - 3;
		break;
	case ATH9K_TX_QUEUE_DATA:
		for (q = 0; q < ATH9K_NUM_TX_QUEUES; q++)
			if (ah->txq[q].tqi_type ==
			    ATH9K_TX_QUEUE_INACTIVE)
				break;
		if (q == ATH9K_NUM_TX_QUEUES) {
			ath_err(common, "No available TX queue\n");
			return -1;
		}
		break;
	default:
		ath_err(common, "Invalid TX queue type: %u\n", type);
		return -1;
	}

	ath_dbg(common, ATH_DBG_QUEUE, "Setup TX queue: %u\n", q);

	qi = &ah->txq[q];
	if (qi->tqi_type != ATH9K_TX_QUEUE_INACTIVE) {
		ath_err(common, "TX queue: %u already active\n", q);
		return -1;
	}
	memset(qi, 0, sizeof(struct ath9k_tx_queue_info));
	qi->tqi_type = type;
	qi->tqi_physCompBuf = qinfo->tqi_physCompBuf;
	(void) ath9k_hw_set_txq_props(ah, q, qinfo);

	return q;
}
EXPORT_SYMBOL(ath9k_hw_setuptxqueue);

bool ath9k_hw_releasetxqueue(struct ath_hw *ah, u32 q)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_tx_queue_info *qi;

	qi = &ah->txq[q];
	if (qi->tqi_type == ATH9K_TX_QUEUE_INACTIVE) {
		ath_dbg(common, ATH_DBG_QUEUE,
			"Release TXQ, inactive queue: %u\n", q);
		return false;
	}

	ath_dbg(common, ATH_DBG_QUEUE, "Release TX queue: %u\n", q);

	qi->tqi_type = ATH9K_TX_QUEUE_INACTIVE;
	ah->txok_interrupt_mask &= ~(1 << q);
	ah->txerr_interrupt_mask &= ~(1 << q);
	ah->txdesc_interrupt_mask &= ~(1 << q);
	ah->txeol_interrupt_mask &= ~(1 << q);
	ah->txurn_interrupt_mask &= ~(1 << q);
	ath9k_hw_set_txq_interrupts(ah, qi);

	return true;
}
EXPORT_SYMBOL(ath9k_hw_releasetxqueue);

bool ath9k_hw_resettxqueue(struct ath_hw *ah, u32 q)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_channel *chan = ah->curchan;
	struct ath9k_tx_queue_info *qi;
	u32 cwMin, chanCwMin, value;

	qi = &ah->txq[q];
	if (qi->tqi_type == ATH9K_TX_QUEUE_INACTIVE) {
		ath_dbg(common, ATH_DBG_QUEUE,
			"Reset TXQ, inactive queue: %u\n", q);
		return true;
	}

	ath_dbg(common, ATH_DBG_QUEUE, "Reset TX queue: %u\n", q);

	if (qi->tqi_cwmin == ATH9K_TXQ_USEDEFAULT) {
		if (chan && IS_CHAN_B(chan))
			chanCwMin = INIT_CWMIN_11B;
		else
			chanCwMin = INIT_CWMIN;

		for (cwMin = 1; cwMin < chanCwMin; cwMin = (cwMin << 1) | 1);
	} else
		cwMin = qi->tqi_cwmin;

	ENABLE_REGWRITE_BUFFER(ah);

	REG_WRITE(ah, AR_DLCL_IFS(q),
		  SM(cwMin, AR_D_LCL_IFS_CWMIN) |
		  SM(qi->tqi_cwmax, AR_D_LCL_IFS_CWMAX) |
		  SM(qi->tqi_aifs, AR_D_LCL_IFS_AIFS));

	REG_WRITE(ah, AR_DRETRY_LIMIT(q),
		  SM(INIT_SSH_RETRY, AR_D_RETRY_LIMIT_STA_SH) |
		  SM(INIT_SLG_RETRY, AR_D_RETRY_LIMIT_STA_LG) |
		  SM(qi->tqi_shretry, AR_D_RETRY_LIMIT_FR_SH));

	REG_WRITE(ah, AR_QMISC(q), AR_Q_MISC_DCU_EARLY_TERM_REQ);

	if (AR_SREV_9340(ah))
		REG_WRITE(ah, AR_DMISC(q),
			  AR_D_MISC_CW_BKOFF_EN | AR_D_MISC_FRAG_WAIT_EN | 0x1);
	else
		REG_WRITE(ah, AR_DMISC(q),
			  AR_D_MISC_CW_BKOFF_EN | AR_D_MISC_FRAG_WAIT_EN | 0x2);

	if (qi->tqi_cbrPeriod) {
		REG_WRITE(ah, AR_QCBRCFG(q),
			  SM(qi->tqi_cbrPeriod, AR_Q_CBRCFG_INTERVAL) |
			  SM(qi->tqi_cbrOverflowLimit, AR_Q_CBRCFG_OVF_THRESH));
		REG_SET_BIT(ah, AR_QMISC(q), AR_Q_MISC_FSP_CBR |
			    (qi->tqi_cbrOverflowLimit ?
			     AR_Q_MISC_CBR_EXP_CNTR_LIMIT_EN : 0));
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
	    && (qi->tqi_qflags & TXQ_FLAG_RDYTIME_EXP_POLICY_ENABLE))
		REG_SET_BIT(ah, AR_QMISC(q), AR_Q_MISC_RDYTIME_EXP_POLICY);

	if (qi->tqi_qflags & TXQ_FLAG_BACKOFF_DISABLE)
		REG_SET_BIT(ah, AR_DMISC(q), AR_D_MISC_POST_FR_BKOFF_DIS);

	REGWRITE_BUFFER_FLUSH(ah);

	if (qi->tqi_qflags & TXQ_FLAG_FRAG_BURST_BACKOFF_ENABLE)
		REG_SET_BIT(ah, AR_DMISC(q), AR_D_MISC_FRAG_BKOFF_EN);

	switch (qi->tqi_type) {
	case ATH9K_TX_QUEUE_BEACON:
		ENABLE_REGWRITE_BUFFER(ah);

		REG_SET_BIT(ah, AR_QMISC(q),
			    AR_Q_MISC_FSP_DBA_GATED
			    | AR_Q_MISC_BEACON_USE
			    | AR_Q_MISC_CBR_INCR_DIS1);

		REG_SET_BIT(ah, AR_DMISC(q),
			    (AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL <<
			     AR_D_MISC_ARB_LOCKOUT_CNTRL_S)
			    | AR_D_MISC_BEACON_USE
			    | AR_D_MISC_POST_FR_BKOFF_DIS);

		REGWRITE_BUFFER_FLUSH(ah);

		/*
		 * cwmin and cwmax should be 0 for beacon queue
		 * but not for IBSS as we would create an imbalance
		 * on beaconing fairness for participating nodes.
		 */
		if (AR_SREV_9300_20_OR_LATER(ah) &&
		    ah->opmode != NL80211_IFTYPE_ADHOC) {
			REG_WRITE(ah, AR_DLCL_IFS(q), SM(0, AR_D_LCL_IFS_CWMIN)
				  | SM(0, AR_D_LCL_IFS_CWMAX)
				  | SM(qi->tqi_aifs, AR_D_LCL_IFS_AIFS));
		}
		break;
	case ATH9K_TX_QUEUE_CAB:
		ENABLE_REGWRITE_BUFFER(ah);

		REG_SET_BIT(ah, AR_QMISC(q),
			    AR_Q_MISC_FSP_DBA_GATED
			    | AR_Q_MISC_CBR_INCR_DIS1
			    | AR_Q_MISC_CBR_INCR_DIS0);
		value = (qi->tqi_readyTime -
			 (ah->config.sw_beacon_response_time -
			  ah->config.dma_beacon_response_time) -
			 ah->config.additional_swba_backoff) * 1024;
		REG_WRITE(ah, AR_QRDYTIMECFG(q),
			  value | AR_Q_RDYTIMECFG_EN);
		REG_SET_BIT(ah, AR_DMISC(q),
			    (AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL <<
			     AR_D_MISC_ARB_LOCKOUT_CNTRL_S));

		REGWRITE_BUFFER_FLUSH(ah);

		break;
	case ATH9K_TX_QUEUE_PSPOLL:
		REG_SET_BIT(ah, AR_QMISC(q), AR_Q_MISC_CBR_INCR_DIS1);
		break;
	case ATH9K_TX_QUEUE_UAPSD:
		REG_SET_BIT(ah, AR_DMISC(q), AR_D_MISC_POST_FR_BKOFF_DIS);
		break;
	default:
		break;
	}

	if (qi->tqi_intFlags & ATH9K_TXQ_USE_LOCKOUT_BKOFF_DIS) {
		REG_SET_BIT(ah, AR_DMISC(q),
			    SM(AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL,
			       AR_D_MISC_ARB_LOCKOUT_CNTRL) |
			    AR_D_MISC_POST_FR_BKOFF_DIS);
	}

	if (AR_SREV_9300_20_OR_LATER(ah))
		REG_WRITE(ah, AR_Q_DESC_CRCCHK, AR_Q_DESC_CRCCHK_EN);

	if (qi->tqi_qflags & TXQ_FLAG_TXOKINT_ENABLE)
		ah->txok_interrupt_mask |= 1 << q;
	else
		ah->txok_interrupt_mask &= ~(1 << q);
	if (qi->tqi_qflags & TXQ_FLAG_TXERRINT_ENABLE)
		ah->txerr_interrupt_mask |= 1 << q;
	else
		ah->txerr_interrupt_mask &= ~(1 << q);
	if (qi->tqi_qflags & TXQ_FLAG_TXDESCINT_ENABLE)
		ah->txdesc_interrupt_mask |= 1 << q;
	else
		ah->txdesc_interrupt_mask &= ~(1 << q);
	if (qi->tqi_qflags & TXQ_FLAG_TXEOLINT_ENABLE)
		ah->txeol_interrupt_mask |= 1 << q;
	else
		ah->txeol_interrupt_mask &= ~(1 << q);
	if (qi->tqi_qflags & TXQ_FLAG_TXURNINT_ENABLE)
		ah->txurn_interrupt_mask |= 1 << q;
	else
		ah->txurn_interrupt_mask &= ~(1 << q);
	ath9k_hw_set_txq_interrupts(ah, qi);

	return true;
}
EXPORT_SYMBOL(ath9k_hw_resettxqueue);

int ath9k_hw_rxprocdesc(struct ath_hw *ah, struct ath_desc *ds,
			struct ath_rx_status *rs)
{
	struct ar5416_desc ads;
	struct ar5416_desc *adsp = AR5416DESC(ds);
	u32 phyerr;

	if ((adsp->ds_rxstatus8 & AR_RxDone) == 0)
		return -EINPROGRESS;

	ads.u.rx = adsp->u.rx;

	rs->rs_status = 0;
	rs->rs_flags = 0;

	rs->rs_datalen = ads.ds_rxstatus1 & AR_DataLen;
	rs->rs_tstamp = ads.AR_RcvTimestamp;

	if (ads.ds_rxstatus8 & AR_PostDelimCRCErr) {
		rs->rs_rssi = ATH9K_RSSI_BAD;
		rs->rs_rssi_ctl0 = ATH9K_RSSI_BAD;
		rs->rs_rssi_ctl1 = ATH9K_RSSI_BAD;
		rs->rs_rssi_ctl2 = ATH9K_RSSI_BAD;
		rs->rs_rssi_ext0 = ATH9K_RSSI_BAD;
		rs->rs_rssi_ext1 = ATH9K_RSSI_BAD;
		rs->rs_rssi_ext2 = ATH9K_RSSI_BAD;
	} else {
		rs->rs_rssi = MS(ads.ds_rxstatus4, AR_RxRSSICombined);
		rs->rs_rssi_ctl0 = MS(ads.ds_rxstatus0,
						AR_RxRSSIAnt00);
		rs->rs_rssi_ctl1 = MS(ads.ds_rxstatus0,
						AR_RxRSSIAnt01);
		rs->rs_rssi_ctl2 = MS(ads.ds_rxstatus0,
						AR_RxRSSIAnt02);
		rs->rs_rssi_ext0 = MS(ads.ds_rxstatus4,
						AR_RxRSSIAnt10);
		rs->rs_rssi_ext1 = MS(ads.ds_rxstatus4,
						AR_RxRSSIAnt11);
		rs->rs_rssi_ext2 = MS(ads.ds_rxstatus4,
						AR_RxRSSIAnt12);
	}
	if (ads.ds_rxstatus8 & AR_RxKeyIdxValid)
		rs->rs_keyix = MS(ads.ds_rxstatus8, AR_KeyIdx);
	else
		rs->rs_keyix = ATH9K_RXKEYIX_INVALID;

	rs->rs_rate = MS(ads.ds_rxstatus0, AR_RxRate);
	rs->rs_more = (ads.ds_rxstatus1 & AR_RxMore) ? 1 : 0;

	rs->rs_isaggr = (ads.ds_rxstatus8 & AR_RxAggr) ? 1 : 0;
	rs->rs_moreaggr =
		(ads.ds_rxstatus8 & AR_RxMoreAggr) ? 1 : 0;
	rs->rs_antenna = MS(ads.ds_rxstatus3, AR_RxAntenna);
	rs->rs_flags =
		(ads.ds_rxstatus3 & AR_GI) ? ATH9K_RX_GI : 0;
	rs->rs_flags |=
		(ads.ds_rxstatus3 & AR_2040) ? ATH9K_RX_2040 : 0;

	if (ads.ds_rxstatus8 & AR_PreDelimCRCErr)
		rs->rs_flags |= ATH9K_RX_DELIM_CRC_PRE;
	if (ads.ds_rxstatus8 & AR_PostDelimCRCErr)
		rs->rs_flags |= ATH9K_RX_DELIM_CRC_POST;
	if (ads.ds_rxstatus8 & AR_DecryptBusyErr)
		rs->rs_flags |= ATH9K_RX_DECRYPT_BUSY;

	if ((ads.ds_rxstatus8 & AR_RxFrameOK) == 0) {
		/*
		 * Treat these errors as mutually exclusive to avoid spurious
		 * extra error reports from the hardware. If a CRC error is
		 * reported, then decryption and MIC errors are irrelevant,
		 * the frame is going to be dropped either way
		 */
		if (ads.ds_rxstatus8 & AR_CRCErr)
			rs->rs_status |= ATH9K_RXERR_CRC;
		else if (ads.ds_rxstatus8 & AR_PHYErr) {
			rs->rs_status |= ATH9K_RXERR_PHY;
			phyerr = MS(ads.ds_rxstatus8, AR_PHYErrCode);
			rs->rs_phyerr = phyerr;
		} else if (ads.ds_rxstatus8 & AR_DecryptCRCErr)
			rs->rs_status |= ATH9K_RXERR_DECRYPT;
		else if (ads.ds_rxstatus8 & AR_MichaelErr)
			rs->rs_status |= ATH9K_RXERR_MIC;
		if (ads.ds_rxstatus8 & AR_KeyMiss)
			rs->rs_status |= ATH9K_RXERR_KEYMISS;
	}

	return 0;
}
EXPORT_SYMBOL(ath9k_hw_rxprocdesc);

/*
 * This can stop or re-enables RX.
 *
 * If bool is set this will kill any frame which is currently being
 * transferred between the MAC and baseband and also prevent any new
 * frames from getting started.
 */
bool ath9k_hw_setrxabort(struct ath_hw *ah, bool set)
{
	u32 reg;

	if (set) {
		REG_SET_BIT(ah, AR_DIAG_SW,
			    (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));

		if (!ath9k_hw_wait(ah, AR_OBS_BUS_1, AR_OBS_BUS_1_RX_STATE,
				   0, AH_WAIT_TIMEOUT)) {
			REG_CLR_BIT(ah, AR_DIAG_SW,
				    (AR_DIAG_RX_DIS |
				     AR_DIAG_RX_ABORT));

			reg = REG_READ(ah, AR_OBS_BUS_1);
			ath_err(ath9k_hw_common(ah),
				"RX failed to go idle in 10 ms RXSM=0x%x\n",
				reg);

			return false;
		}
	} else {
		REG_CLR_BIT(ah, AR_DIAG_SW,
			    (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));
	}

	return true;
}
EXPORT_SYMBOL(ath9k_hw_setrxabort);

void ath9k_hw_putrxbuf(struct ath_hw *ah, u32 rxdp)
{
	REG_WRITE(ah, AR_RXDP, rxdp);
}
EXPORT_SYMBOL(ath9k_hw_putrxbuf);

void ath9k_hw_startpcureceive(struct ath_hw *ah, bool is_scanning)
{
	ath9k_enable_mib_counters(ah);

	ath9k_ani_reset(ah, is_scanning);

	REG_CLR_BIT(ah, AR_DIAG_SW, (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));
}
EXPORT_SYMBOL(ath9k_hw_startpcureceive);

void ath9k_hw_abortpcurecv(struct ath_hw *ah)
{
	REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_ABORT | AR_DIAG_RX_DIS);

	ath9k_hw_disable_mib_counters(ah);
}
EXPORT_SYMBOL(ath9k_hw_abortpcurecv);

bool ath9k_hw_stopdmarecv(struct ath_hw *ah, bool *reset)
{
#define AH_RX_STOP_DMA_TIMEOUT 10000   /* usec */
	struct ath_common *common = ath9k_hw_common(ah);
	u32 mac_status, last_mac_status = 0;
	int i;

	/* Enable access to the DMA observation bus */
	REG_WRITE(ah, AR_MACMISC,
		  ((AR_MACMISC_DMA_OBS_LINE_8 << AR_MACMISC_DMA_OBS_S) |
		   (AR_MACMISC_MISC_OBS_BUS_1 <<
		    AR_MACMISC_MISC_OBS_BUS_MSB_S)));

	REG_WRITE(ah, AR_CR, AR_CR_RXD);

	/* Wait for rx enable bit to go low */
	for (i = AH_RX_STOP_DMA_TIMEOUT / AH_TIME_QUANTUM; i != 0; i--) {
		if ((REG_READ(ah, AR_CR) & AR_CR_RXE) == 0)
			break;

		if (!AR_SREV_9300_20_OR_LATER(ah)) {
			mac_status = REG_READ(ah, AR_DMADBG_7) & 0x7f0;
			if (mac_status == 0x1c0 && mac_status == last_mac_status) {
				*reset = true;
				break;
			}

			last_mac_status = mac_status;
		}

		udelay(AH_TIME_QUANTUM);
	}

	if (i == 0) {
		ath_err(common,
			"DMA failed to stop in %d ms AR_CR=0x%08x AR_DIAG_SW=0x%08x DMADBG_7=0x%08x\n",
			AH_RX_STOP_DMA_TIMEOUT / 1000,
			REG_READ(ah, AR_CR),
			REG_READ(ah, AR_DIAG_SW),
			REG_READ(ah, AR_DMADBG_7));
		return false;
	} else {
		return true;
	}

#undef AH_RX_STOP_DMA_TIMEOUT
}
EXPORT_SYMBOL(ath9k_hw_stopdmarecv);

int ath9k_hw_beaconq_setup(struct ath_hw *ah)
{
	struct ath9k_tx_queue_info qi;

	memset(&qi, 0, sizeof(qi));
	qi.tqi_aifs = 1;
	qi.tqi_cwmin = 0;
	qi.tqi_cwmax = 0;
	/* NB: don't enable any interrupts */
	return ath9k_hw_setuptxqueue(ah, ATH9K_TX_QUEUE_BEACON, &qi);
}
EXPORT_SYMBOL(ath9k_hw_beaconq_setup);

bool ath9k_hw_intrpend(struct ath_hw *ah)
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
EXPORT_SYMBOL(ath9k_hw_intrpend);

void ath9k_hw_disable_interrupts(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);

	if (!(ah->imask & ATH9K_INT_GLOBAL))
		atomic_set(&ah->intr_ref_cnt, -1);
	else
		atomic_dec(&ah->intr_ref_cnt);

	ath_dbg(common, ATH_DBG_INTERRUPT, "disable IER\n");
	REG_WRITE(ah, AR_IER, AR_IER_DISABLE);
	(void) REG_READ(ah, AR_IER);
	if (!AR_SREV_9100(ah)) {
		REG_WRITE(ah, AR_INTR_ASYNC_ENABLE, 0);
		(void) REG_READ(ah, AR_INTR_ASYNC_ENABLE);

		REG_WRITE(ah, AR_INTR_SYNC_ENABLE, 0);
		(void) REG_READ(ah, AR_INTR_SYNC_ENABLE);
	}
}
EXPORT_SYMBOL(ath9k_hw_disable_interrupts);

void ath9k_hw_enable_interrupts(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	u32 sync_default = AR_INTR_SYNC_DEFAULT;

	if (!(ah->imask & ATH9K_INT_GLOBAL))
		return;

	if (!atomic_inc_and_test(&ah->intr_ref_cnt)) {
		ath_dbg(common, ATH_DBG_INTERRUPT,
			"Do not enable IER ref count %d\n",
			atomic_read(&ah->intr_ref_cnt));
		return;
	}

	if (AR_SREV_9340(ah))
		sync_default &= ~AR_INTR_SYNC_HOST1_FATAL;

	ath_dbg(common, ATH_DBG_INTERRUPT, "enable IER\n");
	REG_WRITE(ah, AR_IER, AR_IER_ENABLE);
	if (!AR_SREV_9100(ah)) {
		REG_WRITE(ah, AR_INTR_ASYNC_ENABLE,
			  AR_INTR_MAC_IRQ);
		REG_WRITE(ah, AR_INTR_ASYNC_MASK, AR_INTR_MAC_IRQ);


		REG_WRITE(ah, AR_INTR_SYNC_ENABLE, sync_default);
		REG_WRITE(ah, AR_INTR_SYNC_MASK, sync_default);
	}
	ath_dbg(common, ATH_DBG_INTERRUPT, "AR_IMR 0x%x IER 0x%x\n",
		REG_READ(ah, AR_IMR), REG_READ(ah, AR_IER));
}
EXPORT_SYMBOL(ath9k_hw_enable_interrupts);

void ath9k_hw_set_interrupts(struct ath_hw *ah)
{
	enum ath9k_int ints = ah->imask;
	u32 mask, mask2;
	struct ath9k_hw_capabilities *pCap = &ah->caps;
	struct ath_common *common = ath9k_hw_common(ah);

	if (!(ints & ATH9K_INT_GLOBAL))
		ath9k_hw_disable_interrupts(ah);

	ath_dbg(common, ATH_DBG_INTERRUPT, "New interrupt mask 0x%x\n", ints);

	mask = ints & ATH9K_INT_COMMON;
	mask2 = 0;

	if (ints & ATH9K_INT_TX) {
		if (ah->config.tx_intr_mitigation)
			mask |= AR_IMR_TXMINTR | AR_IMR_TXINTM;
		else {
			if (ah->txok_interrupt_mask)
				mask |= AR_IMR_TXOK;
			if (ah->txdesc_interrupt_mask)
				mask |= AR_IMR_TXDESC;
		}
		if (ah->txerr_interrupt_mask)
			mask |= AR_IMR_TXERR;
		if (ah->txeol_interrupt_mask)
			mask |= AR_IMR_TXEOL;
	}
	if (ints & ATH9K_INT_RX) {
		if (AR_SREV_9300_20_OR_LATER(ah)) {
			mask |= AR_IMR_RXERR | AR_IMR_RXOK_HP;
			if (ah->config.rx_intr_mitigation) {
				mask &= ~AR_IMR_RXOK_LP;
				mask |=  AR_IMR_RXMINTR | AR_IMR_RXINTM;
			} else {
				mask |= AR_IMR_RXOK_LP;
			}
		} else {
			if (ah->config.rx_intr_mitigation)
				mask |= AR_IMR_RXMINTR | AR_IMR_RXINTM;
			else
				mask |= AR_IMR_RXOK | AR_IMR_RXDESC;
		}
		if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP))
			mask |= AR_IMR_GENTMR;
	}

	if (ints & ATH9K_INT_GENTIMER)
		mask |= AR_IMR_GENTMR;

	if (ints & (ATH9K_INT_BMISC)) {
		mask |= AR_IMR_BCNMISC;
		if (ints & ATH9K_INT_TIM)
			mask2 |= AR_IMR_S2_TIM;
		if (ints & ATH9K_INT_DTIM)
			mask2 |= AR_IMR_S2_DTIM;
		if (ints & ATH9K_INT_DTIMSYNC)
			mask2 |= AR_IMR_S2_DTIMSYNC;
		if (ints & ATH9K_INT_CABEND)
			mask2 |= AR_IMR_S2_CABEND;
		if (ints & ATH9K_INT_TSFOOR)
			mask2 |= AR_IMR_S2_TSFOOR;
	}

	if (ints & (ATH9K_INT_GTT | ATH9K_INT_CST)) {
		mask |= AR_IMR_BCNMISC;
		if (ints & ATH9K_INT_GTT)
			mask2 |= AR_IMR_S2_GTT;
		if (ints & ATH9K_INT_CST)
			mask2 |= AR_IMR_S2_CST;
	}

	ath_dbg(common, ATH_DBG_INTERRUPT, "new IMR 0x%x\n", mask);
	REG_WRITE(ah, AR_IMR, mask);
	ah->imrs2_reg &= ~(AR_IMR_S2_TIM | AR_IMR_S2_DTIM | AR_IMR_S2_DTIMSYNC |
			   AR_IMR_S2_CABEND | AR_IMR_S2_CABTO |
			   AR_IMR_S2_TSFOOR | AR_IMR_S2_GTT | AR_IMR_S2_CST);
	ah->imrs2_reg |= mask2;
	REG_WRITE(ah, AR_IMR_S2, ah->imrs2_reg);

	if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP)) {
		if (ints & ATH9K_INT_TIM_TIMER)
			REG_SET_BIT(ah, AR_IMR_S5, AR_IMR_S5_TIM_TIMER);
		else
			REG_CLR_BIT(ah, AR_IMR_S5, AR_IMR_S5_TIM_TIMER);
	}

	return;
}
EXPORT_SYMBOL(ath9k_hw_set_interrupts);
