/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
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

#define AR_BufLen           0x00000fff

static void ar9002_hw_rx_enable(struct ath_hw *ah)
{
	REG_WRITE(ah, AR_CR, AR_CR_RXE);
}

static void ar9002_hw_set_desc_link(void *ds, u32 ds_link)
{
	((struct ath_desc*) ds)->ds_link = ds_link;
}

static void ar9002_hw_get_desc_link(void *ds, u32 **ds_link)
{
	*ds_link = &((struct ath_desc *)ds)->ds_link;
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
			ath_print(common, ATH_DBG_INTERRUPT,
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
				ath_print(common, ATH_DBG_ANY,
					  "received PCI FATAL interrupt\n");
			}
			if (sync_cause & AR_INTR_SYNC_HOST1_PERR) {
				ath_print(common, ATH_DBG_ANY,
					  "received PCI PERR interrupt\n");
			}
			*masked |= ATH9K_INT_FATAL;
		}
		if (sync_cause & AR_INTR_SYNC_RADM_CPL_TIMEOUT) {
			ath_print(common, ATH_DBG_INTERRUPT,
				  "AR_INTR_SYNC_RADM_CPL_TIMEOUT\n");
			REG_WRITE(ah, AR_RC, AR_RC_HOSTIF);
			REG_WRITE(ah, AR_RC, 0);
			*masked |= ATH9K_INT_FATAL;
		}
		if (sync_cause & AR_INTR_SYNC_LOCAL_TIMEOUT) {
			ath_print(common, ATH_DBG_INTERRUPT,
				  "AR_INTR_SYNC_LOCAL_TIMEOUT\n");
		}

		REG_WRITE(ah, AR_INTR_SYNC_CAUSE_CLR, sync_cause);
		(void) REG_READ(ah, AR_INTR_SYNC_CAUSE_CLR);
	}

	return true;
}

static void ar9002_hw_fill_txdesc(struct ath_hw *ah, void *ds, u32 seglen,
				  bool is_firstseg, bool is_lastseg,
				  const void *ds0, dma_addr_t buf_addr,
				  unsigned int qcu)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_data = buf_addr;

	if (is_firstseg) {
		ads->ds_ctl1 |= seglen | (is_lastseg ? 0 : AR_TxMore);
	} else if (is_lastseg) {
		ads->ds_ctl0 = 0;
		ads->ds_ctl1 = seglen;
		ads->ds_ctl2 = AR5416DESC_CONST(ds0)->ds_ctl2;
		ads->ds_ctl3 = AR5416DESC_CONST(ds0)->ds_ctl3;
	} else {
		ads->ds_ctl0 = 0;
		ads->ds_ctl1 = seglen | AR_TxMore;
		ads->ds_ctl2 = 0;
		ads->ds_ctl3 = 0;
	}
	ads->ds_txstatus0 = ads->ds_txstatus1 = 0;
	ads->ds_txstatus2 = ads->ds_txstatus3 = 0;
	ads->ds_txstatus4 = ads->ds_txstatus5 = 0;
	ads->ds_txstatus6 = ads->ds_txstatus7 = 0;
	ads->ds_txstatus8 = ads->ds_txstatus9 = 0;
}

static int ar9002_hw_proc_txdesc(struct ath_hw *ah, void *ds,
				 struct ath_tx_status *ts)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	if ((ads->ds_txstatus9 & AR_TxDone) == 0)
		return -EINPROGRESS;

	ts->ts_seqnum = MS(ads->ds_txstatus9, AR_SeqNum);
	ts->ts_tstamp = ads->AR_SendTimestamp;
	ts->ts_status = 0;
	ts->ts_flags = 0;

	if (ads->ds_txstatus1 & AR_FrmXmitOK)
		ts->ts_status |= ATH9K_TX_ACKED;
	if (ads->ds_txstatus1 & AR_ExcessiveRetries)
		ts->ts_status |= ATH9K_TXERR_XRETRY;
	if (ads->ds_txstatus1 & AR_Filtered)
		ts->ts_status |= ATH9K_TXERR_FILT;
	if (ads->ds_txstatus1 & AR_FIFOUnderrun) {
		ts->ts_status |= ATH9K_TXERR_FIFO;
		ath9k_hw_updatetxtriglevel(ah, true);
	}
	if (ads->ds_txstatus9 & AR_TxOpExceeded)
		ts->ts_status |= ATH9K_TXERR_XTXOP;
	if (ads->ds_txstatus1 & AR_TxTimerExpired)
		ts->ts_status |= ATH9K_TXERR_TIMER_EXPIRED;

	if (ads->ds_txstatus1 & AR_DescCfgErr)
		ts->ts_flags |= ATH9K_TX_DESC_CFG_ERR;
	if (ads->ds_txstatus1 & AR_TxDataUnderrun) {
		ts->ts_flags |= ATH9K_TX_DATA_UNDERRUN;
		ath9k_hw_updatetxtriglevel(ah, true);
	}
	if (ads->ds_txstatus1 & AR_TxDelimUnderrun) {
		ts->ts_flags |= ATH9K_TX_DELIM_UNDERRUN;
		ath9k_hw_updatetxtriglevel(ah, true);
	}
	if (ads->ds_txstatus0 & AR_TxBaStatus) {
		ts->ts_flags |= ATH9K_TX_BA;
		ts->ba_low = ads->AR_BaBitmapLow;
		ts->ba_high = ads->AR_BaBitmapHigh;
	}

	ts->ts_rateindex = MS(ads->ds_txstatus9, AR_FinalTxIdx);
	switch (ts->ts_rateindex) {
	case 0:
		ts->ts_ratecode = MS(ads->ds_ctl3, AR_XmitRate0);
		break;
	case 1:
		ts->ts_ratecode = MS(ads->ds_ctl3, AR_XmitRate1);
		break;
	case 2:
		ts->ts_ratecode = MS(ads->ds_ctl3, AR_XmitRate2);
		break;
	case 3:
		ts->ts_ratecode = MS(ads->ds_ctl3, AR_XmitRate3);
		break;
	}

	ts->ts_rssi = MS(ads->ds_txstatus5, AR_TxRSSICombined);
	ts->ts_rssi_ctl0 = MS(ads->ds_txstatus0, AR_TxRSSIAnt00);
	ts->ts_rssi_ctl1 = MS(ads->ds_txstatus0, AR_TxRSSIAnt01);
	ts->ts_rssi_ctl2 = MS(ads->ds_txstatus0, AR_TxRSSIAnt02);
	ts->ts_rssi_ext0 = MS(ads->ds_txstatus5, AR_TxRSSIAnt10);
	ts->ts_rssi_ext1 = MS(ads->ds_txstatus5, AR_TxRSSIAnt11);
	ts->ts_rssi_ext2 = MS(ads->ds_txstatus5, AR_TxRSSIAnt12);
	ts->evm0 = ads->AR_TxEVM0;
	ts->evm1 = ads->AR_TxEVM1;
	ts->evm2 = ads->AR_TxEVM2;
	ts->ts_shortretry = MS(ads->ds_txstatus1, AR_RTSFailCnt);
	ts->ts_longretry = MS(ads->ds_txstatus1, AR_DataFailCnt);
	ts->ts_virtcol = MS(ads->ds_txstatus1, AR_VirtRetryCnt);
	ts->tid = MS(ads->ds_txstatus9, AR_TxTid);
	ts->ts_antenna = 0;

	return 0;
}

static void ar9002_hw_set11n_txdesc(struct ath_hw *ah, void *ds,
				    u32 pktLen, enum ath9k_pkt_type type,
				    u32 txPower, u32 keyIx,
				    enum ath9k_key_type keyType, u32 flags)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	txPower += ah->txpower_indexoffset;
	if (txPower > 63)
		txPower = 63;

	ads->ds_ctl0 = (pktLen & AR_FrameLen)
		| (flags & ATH9K_TXDESC_VMF ? AR_VirtMoreFrag : 0)
		| SM(txPower, AR_XmitPower)
		| (flags & ATH9K_TXDESC_VEOL ? AR_VEOL : 0)
		| (flags & ATH9K_TXDESC_CLRDMASK ? AR_ClrDestMask : 0)
		| (flags & ATH9K_TXDESC_INTREQ ? AR_TxIntrReq : 0)
		| (keyIx != ATH9K_TXKEYIX_INVALID ? AR_DestIdxValid : 0);

	ads->ds_ctl1 =
		(keyIx != ATH9K_TXKEYIX_INVALID ? SM(keyIx, AR_DestIdx) : 0)
		| SM(type, AR_FrameType)
		| (flags & ATH9K_TXDESC_NOACK ? AR_NoAck : 0)
		| (flags & ATH9K_TXDESC_EXT_ONLY ? AR_ExtOnly : 0)
		| (flags & ATH9K_TXDESC_EXT_AND_CTL ? AR_ExtAndCtl : 0);

	ads->ds_ctl6 = SM(keyType, AR_EncrType);

	if (AR_SREV_9285(ah) || AR_SREV_9271(ah)) {
		ads->ds_ctl8 = 0;
		ads->ds_ctl9 = 0;
		ads->ds_ctl10 = 0;
		ads->ds_ctl11 = 0;
	}
}

static void ar9002_hw_set11n_ratescenario(struct ath_hw *ah, void *ds,
					  void *lastds,
					  u32 durUpdateEn, u32 rtsctsRate,
					  u32 rtsctsDuration,
					  struct ath9k_11n_rate_series series[],
					  u32 nseries, u32 flags)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	struct ar5416_desc *last_ads = AR5416DESC(lastds);
	u32 ds_ctl0;

	if (flags & (ATH9K_TXDESC_RTSENA | ATH9K_TXDESC_CTSENA)) {
		ds_ctl0 = ads->ds_ctl0;

		if (flags & ATH9K_TXDESC_RTSENA) {
			ds_ctl0 &= ~AR_CTSEnable;
			ds_ctl0 |= AR_RTSEnable;
		} else {
			ds_ctl0 &= ~AR_RTSEnable;
			ds_ctl0 |= AR_CTSEnable;
		}

		ads->ds_ctl0 = ds_ctl0;
	} else {
		ads->ds_ctl0 =
			(ads->ds_ctl0 & ~(AR_RTSEnable | AR_CTSEnable));
	}

	ads->ds_ctl2 = set11nTries(series, 0)
		| set11nTries(series, 1)
		| set11nTries(series, 2)
		| set11nTries(series, 3)
		| (durUpdateEn ? AR_DurUpdateEna : 0)
		| SM(0, AR_BurstDur);

	ads->ds_ctl3 = set11nRate(series, 0)
		| set11nRate(series, 1)
		| set11nRate(series, 2)
		| set11nRate(series, 3);

	ads->ds_ctl4 = set11nPktDurRTSCTS(series, 0)
		| set11nPktDurRTSCTS(series, 1);

	ads->ds_ctl5 = set11nPktDurRTSCTS(series, 2)
		| set11nPktDurRTSCTS(series, 3);

	ads->ds_ctl7 = set11nRateFlags(series, 0)
		| set11nRateFlags(series, 1)
		| set11nRateFlags(series, 2)
		| set11nRateFlags(series, 3)
		| SM(rtsctsRate, AR_RTSCTSRate);
	last_ads->ds_ctl2 = ads->ds_ctl2;
	last_ads->ds_ctl3 = ads->ds_ctl3;
}

static void ar9002_hw_set11n_aggr_first(struct ath_hw *ah, void *ds,
					u32 aggrLen)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl1 |= (AR_IsAggr | AR_MoreAggr);
	ads->ds_ctl6 &= ~AR_AggrLen;
	ads->ds_ctl6 |= SM(aggrLen, AR_AggrLen);
}

static void ar9002_hw_set11n_aggr_middle(struct ath_hw *ah, void *ds,
					 u32 numDelims)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	unsigned int ctl6;

	ads->ds_ctl1 |= (AR_IsAggr | AR_MoreAggr);

	ctl6 = ads->ds_ctl6;
	ctl6 &= ~AR_PadDelim;
	ctl6 |= SM(numDelims, AR_PadDelim);
	ads->ds_ctl6 = ctl6;
}

static void ar9002_hw_set11n_aggr_last(struct ath_hw *ah, void *ds)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl1 |= AR_IsAggr;
	ads->ds_ctl1 &= ~AR_MoreAggr;
	ads->ds_ctl6 &= ~AR_PadDelim;
}

static void ar9002_hw_clr11n_aggr(struct ath_hw *ah, void *ds)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl1 &= (~AR_IsAggr & ~AR_MoreAggr);
}

static void ar9002_hw_set11n_burstduration(struct ath_hw *ah, void *ds,
					   u32 burstDuration)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl2 &= ~AR_BurstDur;
	ads->ds_ctl2 |= SM(burstDuration, AR_BurstDur);
}

static void ar9002_hw_set11n_virtualmorefrag(struct ath_hw *ah, void *ds,
					    u32 vmf)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	if (vmf)
		ads->ds_ctl0 |= AR_VirtMoreFrag;
	else
		ads->ds_ctl0 &= ~AR_VirtMoreFrag;
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
	ops->get_desc_link = ar9002_hw_get_desc_link;
	ops->get_isr = ar9002_hw_get_isr;
	ops->fill_txdesc = ar9002_hw_fill_txdesc;
	ops->proc_txdesc = ar9002_hw_proc_txdesc;
	ops->set11n_txdesc = ar9002_hw_set11n_txdesc;
	ops->set11n_ratescenario = ar9002_hw_set11n_ratescenario;
	ops->set11n_aggr_first = ar9002_hw_set11n_aggr_first;
	ops->set11n_aggr_middle = ar9002_hw_set11n_aggr_middle;
	ops->set11n_aggr_last = ar9002_hw_set11n_aggr_last;
	ops->clr11n_aggr = ar9002_hw_clr11n_aggr;
	ops->set11n_burstduration = ar9002_hw_set11n_burstduration;
	ops->set11n_virtualmorefrag = ar9002_hw_set11n_virtualmorefrag;
}
