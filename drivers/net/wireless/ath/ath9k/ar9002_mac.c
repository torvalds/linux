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
#include <linux/export.h>

#define AR_BufLen           0x00000fff

static void ar9002_hw_rx_enable(struct ath_hw *ah)
{
	REG_WRITE(ah, AR_CR, AR_CR_RXE);
}

static void ar9002_hw_set_desc_link(void *ds, u32 ds_link)
{
	((struct ath_desc*) ds)->ds_link = ds_link;
}

static bool ar9002_hw_get_isr(struct ath_hw *ah, enum ath9k_int *masked)
{
	u32 isr = 0;
	u32 mask2 = 0;
	struct ath9k_hw_capabilities *pCap = &ah->caps;
	u32 sync_cause = 0;
	bool fatal_int = false;
	struct ath_common *common = ath9k_hw_common(ah);

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
			if (isr2 & AR_ISR_S2_TSFOOR)
				mask2 |= ATH9K_INT_TSFOOR;
		}

		isr = REG_READ(ah, AR_ISR_RAC);
		if (isr == 0xffffffff) {
			*masked = 0;
			return false;
		}

		*masked = isr & ATH9K_INT_COMMON;

		if (isr & (AR_ISR_RXMINTR | AR_ISR_RXINTM |
			   AR_ISR_RXOK | AR_ISR_RXERR))
			*masked |= ATH9K_INT_RX;

		if (isr &
		    (AR_ISR_TXOK | AR_ISR_TXDESC | AR_ISR_TXERR |
		     AR_ISR_TXEOL)) {
			u32 s0_s, s1_s;

			*masked |= ATH9K_INT_TX;

			s0_s = REG_READ(ah, AR_ISR_S0_S);
			ah->intr_txqs |= MS(s0_s, AR_ISR_S0_QCU_TXOK);
			ah->intr_txqs |= MS(s0_s, AR_ISR_S0_QCU_TXDESC);

			s1_s = REG_READ(ah, AR_ISR_S1_S);
			ah->intr_txqs |= MS(s1_s, AR_ISR_S1_QCU_TXERR);
			ah->intr_txqs |= MS(s1_s, AR_ISR_S1_QCU_TXEOL);
		}

		if (isr & AR_ISR_RXORN) {
			ath_dbg(common, INTERRUPT,
				"receive FIFO overrun interrupt\n");
		}

		*masked |= mask2;
	}

	if (AR_SREV_9100(ah))
		return true;

	if (isr & AR_ISR_GENTMR) {
		u32 s5_s;

		s5_s = REG_READ(ah, AR_ISR_S5_S);
		ah->intr_gen_timer_trigger =
				MS(s5_s, AR_ISR_S5_GENTIMER_TRIG);

		ah->intr_gen_timer_thresh =
			MS(s5_s, AR_ISR_S5_GENTIMER_THRESH);

		if (ah->intr_gen_timer_trigger)
			*masked |= ATH9K_INT_GENTIMER;

		if ((s5_s & AR_ISR_S5_TIM_TIMER) &&
		    !(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP))
			*masked |= ATH9K_INT_TIM_TIMER;
	}

	if (sync_cause) {
		fatal_int =
			(sync_cause &
			 (AR_INTR_SYNC_HOST1_FATAL | AR_INTR_SYNC_HOST1_PERR))
			? true : false;

		if (fatal_int) {
			if (sync_cause & AR_INTR_SYNC_HOST1_FATAL) {
				ath_dbg(common, ANY,
					"received PCI FATAL interrupt\n");
			}
			if (sync_cause & AR_INTR_SYNC_HOST1_PERR) {
				ath_dbg(common, ANY,
					"received PCI PERR interrupt\n");
			}
			*masked |= ATH9K_INT_FATAL;
		}
		if (sync_cause & AR_INTR_SYNC_RADM_CPL_TIMEOUT) {
			ath_dbg(common, INTERRUPT,
				"AR_INTR_SYNC_RADM_CPL_TIMEOUT\n");
			REG_WRITE(ah, AR_RC, AR_RC_HOSTIF);
			REG_WRITE(ah, AR_RC, 0);
			*masked |= ATH9K_INT_FATAL;
		}
		if (sync_cause & AR_INTR_SYNC_LOCAL_TIMEOUT) {
			ath_dbg(common, INTERRUPT,
				"AR_INTR_SYNC_LOCAL_TIMEOUT\n");
		}

		REG_WRITE(ah, AR_INTR_SYNC_CAUSE_CLR, sync_cause);
		(void) REG_READ(ah, AR_INTR_SYNC_CAUSE_CLR);
	}

	return true;
}

static void
ar9002_set_txdesc(struct ath_hw *ah, void *ds, struct ath_tx_info *i)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	u32 ctl1, ctl6;

	ads->ds_txstatus0 = ads->ds_txstatus1 = 0;
	ads->ds_txstatus2 = ads->ds_txstatus3 = 0;
	ads->ds_txstatus4 = ads->ds_txstatus5 = 0;
	ads->ds_txstatus6 = ads->ds_txstatus7 = 0;
	ads->ds_txstatus8 = ads->ds_txstatus9 = 0;

	ACCESS_ONCE(ads->ds_link) = i->link;
	ACCESS_ONCE(ads->ds_data) = i->buf_addr[0];

	ctl1 = i->buf_len[0] | (i->is_last ? 0 : AR_TxMore);
	ctl6 = SM(i->keytype, AR_EncrType);

	if (AR_SREV_9285(ah)) {
		ads->ds_ctl8 = 0;
		ads->ds_ctl9 = 0;
		ads->ds_ctl10 = 0;
		ads->ds_ctl11 = 0;
	}

	if ((i->is_first || i->is_last) &&
	    i->aggr != AGGR_BUF_MIDDLE && i->aggr != AGGR_BUF_LAST) {
		ACCESS_ONCE(ads->ds_ctl2) = set11nTries(i->rates, 0)
			| set11nTries(i->rates, 1)
			| set11nTries(i->rates, 2)
			| set11nTries(i->rates, 3)
			| (i->dur_update ? AR_DurUpdateEna : 0)
			| SM(0, AR_BurstDur);

		ACCESS_ONCE(ads->ds_ctl3) = set11nRate(i->rates, 0)
			| set11nRate(i->rates, 1)
			| set11nRate(i->rates, 2)
			| set11nRate(i->rates, 3);
	} else {
		ACCESS_ONCE(ads->ds_ctl2) = 0;
		ACCESS_ONCE(ads->ds_ctl3) = 0;
	}

	if (!i->is_first) {
		ACCESS_ONCE(ads->ds_ctl0) = 0;
		ACCESS_ONCE(ads->ds_ctl1) = ctl1;
		ACCESS_ONCE(ads->ds_ctl6) = ctl6;
		return;
	}

	ctl1 |= (i->keyix != ATH9K_TXKEYIX_INVALID ? SM(i->keyix, AR_DestIdx) : 0)
		| SM(i->type, AR_FrameType)
		| (i->flags & ATH9K_TXDESC_NOACK ? AR_NoAck : 0)
		| (i->flags & ATH9K_TXDESC_EXT_ONLY ? AR_ExtOnly : 0)
		| (i->flags & ATH9K_TXDESC_EXT_AND_CTL ? AR_ExtAndCtl : 0);

	switch (i->aggr) {
	case AGGR_BUF_FIRST:
		ctl6 |= SM(i->aggr_len, AR_AggrLen);
		/* fall through */
	case AGGR_BUF_MIDDLE:
		ctl1 |= AR_IsAggr | AR_MoreAggr;
		ctl6 |= SM(i->ndelim, AR_PadDelim);
		break;
	case AGGR_BUF_LAST:
		ctl1 |= AR_IsAggr;
		break;
	case AGGR_BUF_NONE:
		break;
	}

	ACCESS_ONCE(ads->ds_ctl0) = (i->pkt_len & AR_FrameLen)
		| (i->flags & ATH9K_TXDESC_VMF ? AR_VirtMoreFrag : 0)
		| SM(i->txpower, AR_XmitPower)
		| (i->flags & ATH9K_TXDESC_VEOL ? AR_VEOL : 0)
		| (i->flags & ATH9K_TXDESC_INTREQ ? AR_TxIntrReq : 0)
		| (i->keyix != ATH9K_TXKEYIX_INVALID ? AR_DestIdxValid : 0)
		| (i->flags & ATH9K_TXDESC_CLRDMASK ? AR_ClrDestMask : 0)
		| (i->flags & ATH9K_TXDESC_RTSENA ? AR_RTSEnable :
		   (i->flags & ATH9K_TXDESC_CTSENA ? AR_CTSEnable : 0));

	ACCESS_ONCE(ads->ds_ctl1) = ctl1;
	ACCESS_ONCE(ads->ds_ctl6) = ctl6;

	if (i->aggr == AGGR_BUF_MIDDLE || i->aggr == AGGR_BUF_LAST)
		return;

	ACCESS_ONCE(ads->ds_ctl4) = set11nPktDurRTSCTS(i->rates, 0)
		| set11nPktDurRTSCTS(i->rates, 1);

	ACCESS_ONCE(ads->ds_ctl5) = set11nPktDurRTSCTS(i->rates, 2)
		| set11nPktDurRTSCTS(i->rates, 3);

	ACCESS_ONCE(ads->ds_ctl7) = set11nRateFlags(i->rates, 0)
		| set11nRateFlags(i->rates, 1)
		| set11nRateFlags(i->rates, 2)
		| set11nRateFlags(i->rates, 3)
		| SM(i->rtscts_rate, AR_RTSCTSRate);
}

static int ar9002_hw_proc_txdesc(struct ath_hw *ah, void *ds,
				 struct ath_tx_status *ts)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	u32 status;

	status = ACCESS_ONCE(ads->ds_txstatus9);
	if ((status & AR_TxDone) == 0)
		return -EINPROGRESS;

	ts->ts_tstamp = ads->AR_SendTimestamp;
	ts->ts_status = 0;
	ts->ts_flags = 0;

	if (status & AR_TxOpExceeded)
		ts->ts_status |= ATH9K_TXERR_XTXOP;
	ts->tid = MS(status, AR_TxTid);
	ts->ts_rateindex = MS(status, AR_FinalTxIdx);
	ts->ts_seqnum = MS(status, AR_SeqNum);

	status = ACCESS_ONCE(ads->ds_txstatus0);
	ts->ts_rssi_ctl0 = MS(status, AR_TxRSSIAnt00);
	ts->ts_rssi_ctl1 = MS(status, AR_TxRSSIAnt01);
	ts->ts_rssi_ctl2 = MS(status, AR_TxRSSIAnt02);
	if (status & AR_TxBaStatus) {
		ts->ts_flags |= ATH9K_TX_BA;
		ts->ba_low = ads->AR_BaBitmapLow;
		ts->ba_high = ads->AR_BaBitmapHigh;
	}

	status = ACCESS_ONCE(ads->ds_txstatus1);
	if (status & AR_FrmXmitOK)
		ts->ts_status |= ATH9K_TX_ACKED;
	else {
		if (status & AR_ExcessiveRetries)
			ts->ts_status |= ATH9K_TXERR_XRETRY;
		if (status & AR_Filtered)
			ts->ts_status |= ATH9K_TXERR_FILT;
		if (status & AR_FIFOUnderrun) {
			ts->ts_status |= ATH9K_TXERR_FIFO;
			ath9k_hw_updatetxtriglevel(ah, true);
		}
	}
	if (status & AR_TxTimerExpired)
		ts->ts_status |= ATH9K_TXERR_TIMER_EXPIRED;
	if (status & AR_DescCfgErr)
		ts->ts_flags |= ATH9K_TX_DESC_CFG_ERR;
	if (status & AR_TxDataUnderrun) {
		ts->ts_flags |= ATH9K_TX_DATA_UNDERRUN;
		ath9k_hw_updatetxtriglevel(ah, true);
	}
	if (status & AR_TxDelimUnderrun) {
		ts->ts_flags |= ATH9K_TX_DELIM_UNDERRUN;
		ath9k_hw_updatetxtriglevel(ah, true);
	}
	ts->ts_shortretry = MS(status, AR_RTSFailCnt);
	ts->ts_longretry = MS(status, AR_DataFailCnt);
	ts->ts_virtcol = MS(status, AR_VirtRetryCnt);

	status = ACCESS_ONCE(ads->ds_txstatus5);
	ts->ts_rssi = MS(status, AR_TxRSSICombined);
	ts->ts_rssi_ext0 = MS(status, AR_TxRSSIAnt10);
	ts->ts_rssi_ext1 = MS(status, AR_TxRSSIAnt11);
	ts->ts_rssi_ext2 = MS(status, AR_TxRSSIAnt12);

	ts->evm0 = ads->AR_TxEVM0;
	ts->evm1 = ads->AR_TxEVM1;
	ts->evm2 = ads->AR_TxEVM2;

	return 0;
}

void ath9k_hw_setuprxdesc(struct ath_hw *ah, struct ath_desc *ds,
			  u32 size, u32 flags)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	struct ath9k_hw_capabilities *pCap = &ah->caps;

	ads->ds_ctl1 = size & AR_BufLen;
	if (flags & ATH9K_RXDESC_INTREQ)
		ads->ds_ctl1 |= AR_RxIntrReq;

	ads->ds_rxstatus8 &= ~AR_RxDone;
	if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP))
		memset(&(ads->u), 0, sizeof(ads->u));
}
EXPORT_SYMBOL(ath9k_hw_setuprxdesc);

void ar9002_hw_attach_mac_ops(struct ath_hw *ah)
{
	struct ath_hw_ops *ops = ath9k_hw_ops(ah);

	ops->rx_enable = ar9002_hw_rx_enable;
	ops->set_desc_link = ar9002_hw_set_desc_link;
	ops->get_isr = ar9002_hw_get_isr;
	ops->set_txdesc = ar9002_set_txdesc;
	ops->proc_txdesc = ar9002_hw_proc_txdesc;
}
