/*
 * Copyright (c) 2010 Atheros Communications Inc.
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
#include "ar9003_mac.h"

static void ar9003_hw_rx_enable(struct ath_hw *hw)
{
	REG_WRITE(hw, AR_CR, 0);
}

static u16 ar9003_calc_ptr_chksum(struct ar9003_txc *ads)
{
	int checksum;

	checksum = ads->info + ads->link
		+ ads->data0 + ads->ctl3
		+ ads->data1 + ads->ctl5
		+ ads->data2 + ads->ctl7
		+ ads->data3 + ads->ctl9;

	return ((checksum & 0xffff) + (checksum >> 16)) & AR_TxPtrChkSum;
}

static void ar9003_hw_set_desc_link(void *ds, u32 ds_link)
{
	struct ar9003_txc *ads = ds;

	ads->link = ds_link;
	ads->ctl10 &= ~AR_TxPtrChkSum;
	ads->ctl10 |= ar9003_calc_ptr_chksum(ads);
}

static void ar9003_hw_get_desc_link(void *ds, u32 **ds_link)
{
	struct ar9003_txc *ads = ds;

	*ds_link = &ads->link;
}

static bool ar9003_hw_get_isr(struct ath_hw *ah, enum ath9k_int *masked)
{
	u32 isr = 0;
	u32 mask2 = 0;
	struct ath9k_hw_capabilities *pCap = &ah->caps;
	u32 sync_cause = 0;
	struct ath_common *common = ath9k_hw_common(ah);

	if (REG_READ(ah, AR_INTR_ASYNC_CAUSE) & AR_INTR_MAC_IRQ) {
		if ((REG_READ(ah, AR_RTC_STATUS) & AR_RTC_STATUS_M)
				== AR_RTC_STATUS_ON)
			isr = REG_READ(ah, AR_ISR);
	}

	sync_cause = REG_READ(ah, AR_INTR_SYNC_CAUSE) & AR_INTR_SYNC_DEFAULT;

	*masked = 0;

	if (!isr && !sync_cause)
		return false;

	if (isr) {
		if (isr & AR_ISR_BCNMISC) {
			u32 isr2;
			isr2 = REG_READ(ah, AR_ISR_S2);

			mask2 |= ((isr2 & AR_ISR_S2_TIM) >>
				  MAP_ISR_S2_TIM);
			mask2 |= ((isr2 & AR_ISR_S2_DTIM) >>
				  MAP_ISR_S2_DTIM);
			mask2 |= ((isr2 & AR_ISR_S2_DTIMSYNC) >>
				  MAP_ISR_S2_DTIMSYNC);
			mask2 |= ((isr2 & AR_ISR_S2_CABEND) >>
				  MAP_ISR_S2_CABEND);
			mask2 |= ((isr2 & AR_ISR_S2_GTT) <<
				  MAP_ISR_S2_GTT);
			mask2 |= ((isr2 & AR_ISR_S2_CST) <<
				  MAP_ISR_S2_CST);
			mask2 |= ((isr2 & AR_ISR_S2_TSFOOR) >>
				  MAP_ISR_S2_TSFOOR);
			mask2 |= ((isr2 & AR_ISR_S2_BB_WATCHDOG) >>
				  MAP_ISR_S2_BB_WATCHDOG);

			if (!(pCap->hw_caps & ATH9K_HW_CAP_RAC_SUPPORTED)) {
				REG_WRITE(ah, AR_ISR_S2, isr2);
				isr &= ~AR_ISR_BCNMISC;
			}
		}

		if ((pCap->hw_caps & ATH9K_HW_CAP_RAC_SUPPORTED))
			isr = REG_READ(ah, AR_ISR_RAC);

		if (isr == 0xffffffff) {
			*masked = 0;
			return false;
		}

		*masked = isr & ATH9K_INT_COMMON;

		if (ah->config.rx_intr_mitigation)
			if (isr & (AR_ISR_RXMINTR | AR_ISR_RXINTM))
				*masked |= ATH9K_INT_RXLP;

		if (ah->config.tx_intr_mitigation)
			if (isr & (AR_ISR_TXMINTR | AR_ISR_TXINTM))
				*masked |= ATH9K_INT_TX;

		if (isr & (AR_ISR_LP_RXOK | AR_ISR_RXERR))
			*masked |= ATH9K_INT_RXLP;

		if (isr & AR_ISR_HP_RXOK)
			*masked |= ATH9K_INT_RXHP;

		if (isr & (AR_ISR_TXOK | AR_ISR_TXERR | AR_ISR_TXEOL)) {
			*masked |= ATH9K_INT_TX;

			if (!(pCap->hw_caps & ATH9K_HW_CAP_RAC_SUPPORTED)) {
				u32 s0, s1;
				s0 = REG_READ(ah, AR_ISR_S0);
				REG_WRITE(ah, AR_ISR_S0, s0);
				s1 = REG_READ(ah, AR_ISR_S1);
				REG_WRITE(ah, AR_ISR_S1, s1);

				isr &= ~(AR_ISR_TXOK | AR_ISR_TXERR |
					 AR_ISR_TXEOL);
			}
		}

		if (isr & AR_ISR_GENTMR) {
			u32 s5;

			if (pCap->hw_caps & ATH9K_HW_CAP_RAC_SUPPORTED)
				s5 = REG_READ(ah, AR_ISR_S5_S);
			else
				s5 = REG_READ(ah, AR_ISR_S5);

			ah->intr_gen_timer_trigger =
				MS(s5, AR_ISR_S5_GENTIMER_TRIG);

			ah->intr_gen_timer_thresh =
				MS(s5, AR_ISR_S5_GENTIMER_THRESH);

			if (ah->intr_gen_timer_trigger)
				*masked |= ATH9K_INT_GENTIMER;

			if (!(pCap->hw_caps & ATH9K_HW_CAP_RAC_SUPPORTED)) {
				REG_WRITE(ah, AR_ISR_S5, s5);
				isr &= ~AR_ISR_GENTMR;
			}

		}

		*masked |= mask2;

		if (!(pCap->hw_caps & ATH9K_HW_CAP_RAC_SUPPORTED)) {
			REG_WRITE(ah, AR_ISR, isr);

			(void) REG_READ(ah, AR_ISR);
		}

		if (*masked & ATH9K_INT_BB_WATCHDOG)
			ar9003_hw_bb_watchdog_read(ah);
	}

	if (sync_cause) {
		if (sync_cause & AR_INTR_SYNC_RADM_CPL_TIMEOUT) {
			REG_WRITE(ah, AR_RC, AR_RC_HOSTIF);
			REG_WRITE(ah, AR_RC, 0);
			*masked |= ATH9K_INT_FATAL;
		}

		if (sync_cause & AR_INTR_SYNC_LOCAL_TIMEOUT)
			ath_dbg(common, ATH_DBG_INTERRUPT,
				"AR_INTR_SYNC_LOCAL_TIMEOUT\n");

		REG_WRITE(ah, AR_INTR_SYNC_CAUSE_CLR, sync_cause);
		(void) REG_READ(ah, AR_INTR_SYNC_CAUSE_CLR);

	}
	return true;
}

static void ar9003_hw_fill_txdesc(struct ath_hw *ah, void *ds, u32 seglen,
				  bool is_firstseg, bool is_lastseg,
				  const void *ds0, dma_addr_t buf_addr,
				  unsigned int qcu)
{
	struct ar9003_txc *ads = (struct ar9003_txc *) ds;
	unsigned int descid = 0;

	ads->info = (ATHEROS_VENDOR_ID << AR_DescId_S) |
				     (1 << AR_TxRxDesc_S) |
				     (1 << AR_CtrlStat_S) |
				     (qcu << AR_TxQcuNum_S) | 0x17;

	ads->data0 = buf_addr;
	ads->data1 = 0;
	ads->data2 = 0;
	ads->data3 = 0;

	ads->ctl3 = (seglen << AR_BufLen_S);
	ads->ctl3 &= AR_BufLen;

	/* Fill in pointer checksum and descriptor id */
	ads->ctl10 = ar9003_calc_ptr_chksum(ads);
	ads->ctl10 |= (descid << AR_TxDescId_S);

	if (is_firstseg) {
		ads->ctl12 |= (is_lastseg ? 0 : AR_TxMore);
	} else if (is_lastseg) {
		ads->ctl11 = 0;
		ads->ctl12 = 0;
		ads->ctl13 = AR9003TXC_CONST(ds0)->ctl13;
		ads->ctl14 = AR9003TXC_CONST(ds0)->ctl14;
	} else {
		/* XXX Intermediate descriptor in a multi-descriptor frame.*/
		ads->ctl11 = 0;
		ads->ctl12 = AR_TxMore;
		ads->ctl13 = 0;
		ads->ctl14 = 0;
	}
}

static int ar9003_hw_proc_txdesc(struct ath_hw *ah, void *ds,
				 struct ath_tx_status *ts)
{
	struct ar9003_txs *ads;
	u32 status;

	ads = &ah->ts_ring[ah->ts_tail];

	status = ACCESS_ONCE(ads->status8);
	if ((status & AR_TxDone) == 0)
		return -EINPROGRESS;

	ah->ts_tail = (ah->ts_tail + 1) % ah->ts_size;

	if ((MS(ads->ds_info, AR_DescId) != ATHEROS_VENDOR_ID) ||
	    (MS(ads->ds_info, AR_TxRxDesc) != 1)) {
		ath_dbg(ath9k_hw_common(ah), ATH_DBG_XMIT,
			"Tx Descriptor error %x\n", ads->ds_info);
		memset(ads, 0, sizeof(*ads));
		return -EIO;
	}

	if (status & AR_TxOpExceeded)
		ts->ts_status |= ATH9K_TXERR_XTXOP;
	ts->ts_rateindex = MS(status, AR_FinalTxIdx);
	ts->ts_seqnum = MS(status, AR_SeqNum);
	ts->tid = MS(status, AR_TxTid);

	ts->qid = MS(ads->ds_info, AR_TxQcuNum);
	ts->desc_id = MS(ads->status1, AR_TxDescId);
	ts->ts_tstamp = ads->status4;
	ts->ts_status = 0;
	ts->ts_flags  = 0;

	status = ACCESS_ONCE(ads->status2);
	ts->ts_rssi_ctl0 = MS(status, AR_TxRSSIAnt00);
	ts->ts_rssi_ctl1 = MS(status, AR_TxRSSIAnt01);
	ts->ts_rssi_ctl2 = MS(status, AR_TxRSSIAnt02);
	if (status & AR_TxBaStatus) {
		ts->ts_flags |= ATH9K_TX_BA;
		ts->ba_low = ads->status5;
		ts->ba_high = ads->status6;
	}

	status = ACCESS_ONCE(ads->status3);
	if (status & AR_ExcessiveRetries)
		ts->ts_status |= ATH9K_TXERR_XRETRY;
	if (status & AR_Filtered)
		ts->ts_status |= ATH9K_TXERR_FILT;
	if (status & AR_FIFOUnderrun) {
		ts->ts_status |= ATH9K_TXERR_FIFO;
		ath9k_hw_updatetxtriglevel(ah, true);
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

	status = ACCESS_ONCE(ads->status7);
	ts->ts_rssi = MS(status, AR_TxRSSICombined);
	ts->ts_rssi_ext0 = MS(status, AR_TxRSSIAnt10);
	ts->ts_rssi_ext1 = MS(status, AR_TxRSSIAnt11);
	ts->ts_rssi_ext2 = MS(status, AR_TxRSSIAnt12);

	memset(ads, 0, sizeof(*ads));

	return 0;
}

static void ar9003_hw_set11n_txdesc(struct ath_hw *ah, void *ds,
		u32 pktlen, enum ath9k_pkt_type type, u32 txpower,
		u32 keyIx, enum ath9k_key_type keyType, u32 flags)
{
	struct ar9003_txc *ads = (struct ar9003_txc *) ds;

	if (txpower > ah->txpower_limit)
		txpower = ah->txpower_limit;

	if (txpower > 63)
		txpower = 63;

	ads->ctl11 = (pktlen & AR_FrameLen)
		| (flags & ATH9K_TXDESC_VMF ? AR_VirtMoreFrag : 0)
		| SM(txpower, AR_XmitPower)
		| (flags & ATH9K_TXDESC_VEOL ? AR_VEOL : 0)
		| (flags & ATH9K_TXDESC_CLRDMASK ? AR_ClrDestMask : 0)
		| (keyIx != ATH9K_TXKEYIX_INVALID ? AR_DestIdxValid : 0)
		| (flags & ATH9K_TXDESC_LOWRXCHAIN ? AR_LowRxChain : 0);

	ads->ctl12 =
		(keyIx != ATH9K_TXKEYIX_INVALID ? SM(keyIx, AR_DestIdx) : 0)
		| SM(type, AR_FrameType)
		| (flags & ATH9K_TXDESC_NOACK ? AR_NoAck : 0)
		| (flags & ATH9K_TXDESC_EXT_ONLY ? AR_ExtOnly : 0)
		| (flags & ATH9K_TXDESC_EXT_AND_CTL ? AR_ExtAndCtl : 0);

	ads->ctl17 = SM(keyType, AR_EncrType) |
		     (flags & ATH9K_TXDESC_LDPC ? AR_LDPC : 0);
	ads->ctl18 = 0;
	ads->ctl19 = AR_Not_Sounding;

	ads->ctl20 = 0;
	ads->ctl21 = 0;
	ads->ctl22 = 0;
}

static void ar9003_hw_set11n_ratescenario(struct ath_hw *ah, void *ds,
					  void *lastds,
					  u32 durUpdateEn, u32 rtsctsRate,
					  u32 rtsctsDuration,
					  struct ath9k_11n_rate_series series[],
					  u32 nseries, u32 flags)
{
	struct ar9003_txc *ads = (struct ar9003_txc *) ds;
	struct ar9003_txc *last_ads = (struct ar9003_txc *) lastds;
	u_int32_t ctl11;

	if (flags & (ATH9K_TXDESC_RTSENA | ATH9K_TXDESC_CTSENA)) {
		ctl11 = ads->ctl11;

		if (flags & ATH9K_TXDESC_RTSENA) {
			ctl11 &= ~AR_CTSEnable;
			ctl11 |= AR_RTSEnable;
		} else {
			ctl11 &= ~AR_RTSEnable;
			ctl11 |= AR_CTSEnable;
		}

		ads->ctl11 = ctl11;
	} else {
		ads->ctl11 = (ads->ctl11 & ~(AR_RTSEnable | AR_CTSEnable));
	}

	ads->ctl13 = set11nTries(series, 0)
		|  set11nTries(series, 1)
		|  set11nTries(series, 2)
		|  set11nTries(series, 3)
		|  (durUpdateEn ? AR_DurUpdateEna : 0)
		|  SM(0, AR_BurstDur);

	ads->ctl14 = set11nRate(series, 0)
		|  set11nRate(series, 1)
		|  set11nRate(series, 2)
		|  set11nRate(series, 3);

	ads->ctl15 = set11nPktDurRTSCTS(series, 0)
		|  set11nPktDurRTSCTS(series, 1);

	ads->ctl16 = set11nPktDurRTSCTS(series, 2)
		|  set11nPktDurRTSCTS(series, 3);

	ads->ctl18 = set11nRateFlags(series, 0)
		|  set11nRateFlags(series, 1)
		|  set11nRateFlags(series, 2)
		|  set11nRateFlags(series, 3)
		| SM(rtsctsRate, AR_RTSCTSRate);
	ads->ctl19 = AR_Not_Sounding;

	last_ads->ctl13 = ads->ctl13;
	last_ads->ctl14 = ads->ctl14;
}

static void ar9003_hw_set11n_aggr_first(struct ath_hw *ah, void *ds,
					u32 aggrLen)
{
#define FIRST_DESC_NDELIMS 60
	struct ar9003_txc *ads = (struct ar9003_txc *) ds;

	ads->ctl12 |= (AR_IsAggr | AR_MoreAggr);

	if (ah->ent_mode & AR_ENT_OTP_MPSD) {
		u32 ctl17, ndelim;
		/*
		 * Add delimiter when using RTS/CTS with aggregation
		 * and non enterprise AR9003 card
		 */
		ctl17 = ads->ctl17;
		ndelim = MS(ctl17, AR_PadDelim);

		if (ndelim < FIRST_DESC_NDELIMS) {
			aggrLen += (FIRST_DESC_NDELIMS - ndelim) * 4;
			ndelim = FIRST_DESC_NDELIMS;
		}

		ctl17 &= ~AR_AggrLen;
		ctl17 |= SM(aggrLen, AR_AggrLen);

		ctl17 &= ~AR_PadDelim;
		ctl17 |= SM(ndelim, AR_PadDelim);

		ads->ctl17 = ctl17;
	} else {
		ads->ctl17 &= ~AR_AggrLen;
		ads->ctl17 |= SM(aggrLen, AR_AggrLen);
	}
}

static void ar9003_hw_set11n_aggr_middle(struct ath_hw *ah, void *ds,
					 u32 numDelims)
{
	struct ar9003_txc *ads = (struct ar9003_txc *) ds;
	unsigned int ctl17;

	ads->ctl12 |= (AR_IsAggr | AR_MoreAggr);

	/*
	 * We use a stack variable to manipulate ctl6 to reduce uncached
	 * read modify, modfiy, write.
	 */
	ctl17 = ads->ctl17;
	ctl17 &= ~AR_PadDelim;
	ctl17 |= SM(numDelims, AR_PadDelim);
	ads->ctl17 = ctl17;
}

static void ar9003_hw_set11n_aggr_last(struct ath_hw *ah, void *ds)
{
	struct ar9003_txc *ads = (struct ar9003_txc *) ds;

	ads->ctl12 |= AR_IsAggr;
	ads->ctl12 &= ~AR_MoreAggr;
	ads->ctl17 &= ~AR_PadDelim;
}

static void ar9003_hw_clr11n_aggr(struct ath_hw *ah, void *ds)
{
	struct ar9003_txc *ads = (struct ar9003_txc *) ds;

	ads->ctl12 &= (~AR_IsAggr & ~AR_MoreAggr);
}

static void ar9003_hw_set11n_burstduration(struct ath_hw *ah, void *ds,
					   u32 burstDuration)
{
	struct ar9003_txc *ads = (struct ar9003_txc *) ds;

	ads->ctl13 &= ~AR_BurstDur;
	ads->ctl13 |= SM(burstDuration, AR_BurstDur);

}

static void ar9003_hw_set11n_virtualmorefrag(struct ath_hw *ah, void *ds,
					     u32 vmf)
{
	struct ar9003_txc *ads = (struct ar9003_txc *) ds;

	if (vmf)
		ads->ctl11 |=  AR_VirtMoreFrag;
	else
		ads->ctl11 &= ~AR_VirtMoreFrag;
}

void ar9003_hw_set_paprd_txdesc(struct ath_hw *ah, void *ds, u8 chains)
{
	struct ar9003_txc *ads = ds;

	ads->ctl12 |= SM(chains, AR_PAPRDChainMask);
}
EXPORT_SYMBOL(ar9003_hw_set_paprd_txdesc);

void ar9003_hw_attach_mac_ops(struct ath_hw *hw)
{
	struct ath_hw_ops *ops = ath9k_hw_ops(hw);

	ops->rx_enable = ar9003_hw_rx_enable;
	ops->set_desc_link = ar9003_hw_set_desc_link;
	ops->get_desc_link = ar9003_hw_get_desc_link;
	ops->get_isr = ar9003_hw_get_isr;
	ops->fill_txdesc = ar9003_hw_fill_txdesc;
	ops->proc_txdesc = ar9003_hw_proc_txdesc;
	ops->set11n_txdesc = ar9003_hw_set11n_txdesc;
	ops->set11n_ratescenario = ar9003_hw_set11n_ratescenario;
	ops->set11n_aggr_first = ar9003_hw_set11n_aggr_first;
	ops->set11n_aggr_middle = ar9003_hw_set11n_aggr_middle;
	ops->set11n_aggr_last = ar9003_hw_set11n_aggr_last;
	ops->clr11n_aggr = ar9003_hw_clr11n_aggr;
	ops->set11n_burstduration = ar9003_hw_set11n_burstduration;
	ops->set11n_virtualmorefrag = ar9003_hw_set11n_virtualmorefrag;
}

void ath9k_hw_set_rx_bufsize(struct ath_hw *ah, u16 buf_size)
{
	REG_WRITE(ah, AR_DATABUF_SIZE, buf_size & AR_DATABUF_SIZE_MASK);
}
EXPORT_SYMBOL(ath9k_hw_set_rx_bufsize);

void ath9k_hw_addrxbuf_edma(struct ath_hw *ah, u32 rxdp,
			    enum ath9k_rx_qtype qtype)
{
	if (qtype == ATH9K_RX_QUEUE_HP)
		REG_WRITE(ah, AR_HP_RXDP, rxdp);
	else
		REG_WRITE(ah, AR_LP_RXDP, rxdp);
}
EXPORT_SYMBOL(ath9k_hw_addrxbuf_edma);

int ath9k_hw_process_rxdesc_edma(struct ath_hw *ah, struct ath_rx_status *rxs,
				 void *buf_addr)
{
	struct ar9003_rxs *rxsp = (struct ar9003_rxs *) buf_addr;
	unsigned int phyerr;

	/* TODO: byte swap on big endian for ar9300_10 */

	if ((rxsp->status11 & AR_RxDone) == 0)
		return -EINPROGRESS;

	if (MS(rxsp->ds_info, AR_DescId) != 0x168c)
		return -EINVAL;

	if ((rxsp->ds_info & (AR_TxRxDesc | AR_CtrlStat)) != 0)
		return -EINPROGRESS;

	if (!rxs)
		return 0;

	rxs->rs_status = 0;
	rxs->rs_flags =  0;

	rxs->rs_datalen = rxsp->status2 & AR_DataLen;
	rxs->rs_tstamp =  rxsp->status3;

	/* XXX: Keycache */
	rxs->rs_rssi = MS(rxsp->status5, AR_RxRSSICombined);
	rxs->rs_rssi_ctl0 = MS(rxsp->status1, AR_RxRSSIAnt00);
	rxs->rs_rssi_ctl1 = MS(rxsp->status1, AR_RxRSSIAnt01);
	rxs->rs_rssi_ctl2 = MS(rxsp->status1, AR_RxRSSIAnt02);
	rxs->rs_rssi_ext0 = MS(rxsp->status5, AR_RxRSSIAnt10);
	rxs->rs_rssi_ext1 = MS(rxsp->status5, AR_RxRSSIAnt11);
	rxs->rs_rssi_ext2 = MS(rxsp->status5, AR_RxRSSIAnt12);

	if (rxsp->status11 & AR_RxKeyIdxValid)
		rxs->rs_keyix = MS(rxsp->status11, AR_KeyIdx);
	else
		rxs->rs_keyix = ATH9K_RXKEYIX_INVALID;

	rxs->rs_rate = MS(rxsp->status1, AR_RxRate);
	rxs->rs_more = (rxsp->status2 & AR_RxMore) ? 1 : 0;

	rxs->rs_isaggr = (rxsp->status11 & AR_RxAggr) ? 1 : 0;
	rxs->rs_moreaggr = (rxsp->status11 & AR_RxMoreAggr) ? 1 : 0;
	rxs->rs_antenna = (MS(rxsp->status4, AR_RxAntenna) & 0x7);
	rxs->rs_flags  = (rxsp->status4 & AR_GI) ? ATH9K_RX_GI : 0;
	rxs->rs_flags  |= (rxsp->status4 & AR_2040) ? ATH9K_RX_2040 : 0;

	rxs->evm0 = rxsp->status6;
	rxs->evm1 = rxsp->status7;
	rxs->evm2 = rxsp->status8;
	rxs->evm3 = rxsp->status9;
	rxs->evm4 = (rxsp->status10 & 0xffff);

	if (rxsp->status11 & AR_PreDelimCRCErr)
		rxs->rs_flags |= ATH9K_RX_DELIM_CRC_PRE;

	if (rxsp->status11 & AR_PostDelimCRCErr)
		rxs->rs_flags |= ATH9K_RX_DELIM_CRC_POST;

	if (rxsp->status11 & AR_DecryptBusyErr)
		rxs->rs_flags |= ATH9K_RX_DECRYPT_BUSY;

	if ((rxsp->status11 & AR_RxFrameOK) == 0) {
		/*
		 * AR_CRCErr will bet set to true if we're on the last
		 * subframe and the AR_PostDelimCRCErr is caught.
		 * In a way this also gives us a guarantee that when
		 * (!(AR_CRCErr) && (AR_PostDelimCRCErr)) we cannot
		 * possibly be reviewing the last subframe. AR_CRCErr
		 * is the CRC of the actual data.
		 */
		if (rxsp->status11 & AR_CRCErr) {
			rxs->rs_status |= ATH9K_RXERR_CRC;
		} else if (rxsp->status11 & AR_PHYErr) {
			phyerr = MS(rxsp->status11, AR_PHYErrCode);
			/*
			 * If we reach a point here where AR_PostDelimCRCErr is
			 * true it implies we're *not* on the last subframe. In
			 * in that case that we know already that the CRC of
			 * the frame was OK, and MAC would send an ACK for that
			 * subframe, even if we did get a phy error of type
			 * ATH9K_PHYERR_OFDM_RESTART. This is only applicable
			 * to frame that are prior to the last subframe.
			 * The AR_PostDelimCRCErr is the CRC for the MPDU
			 * delimiter, which contains the 4 reserved bits,
			 * the MPDU length (12 bits), and follows the MPDU
			 * delimiter for an A-MPDU subframe (0x4E = 'N' ASCII).
			 */
			if ((phyerr == ATH9K_PHYERR_OFDM_RESTART) &&
			    (rxsp->status11 & AR_PostDelimCRCErr)) {
				rxs->rs_phyerr = 0;
			} else {
				rxs->rs_status |= ATH9K_RXERR_PHY;
				rxs->rs_phyerr = phyerr;
			}

		} else if (rxsp->status11 & AR_DecryptCRCErr) {
			rxs->rs_status |= ATH9K_RXERR_DECRYPT;
		} else if (rxsp->status11 & AR_MichaelErr) {
			rxs->rs_status |= ATH9K_RXERR_MIC;
		} else if (rxsp->status11 & AR_KeyMiss)
			rxs->rs_status |= ATH9K_RXERR_DECRYPT;
	}

	return 0;
}
EXPORT_SYMBOL(ath9k_hw_process_rxdesc_edma);

void ath9k_hw_reset_txstatus_ring(struct ath_hw *ah)
{
	ah->ts_tail = 0;

	memset((void *) ah->ts_ring, 0,
		ah->ts_size * sizeof(struct ar9003_txs));

	ath_dbg(ath9k_hw_common(ah), ATH_DBG_XMIT,
		"TS Start 0x%x End 0x%x Virt %p, Size %d\n",
		ah->ts_paddr_start, ah->ts_paddr_end,
		ah->ts_ring, ah->ts_size);

	REG_WRITE(ah, AR_Q_STATUS_RING_START, ah->ts_paddr_start);
	REG_WRITE(ah, AR_Q_STATUS_RING_END, ah->ts_paddr_end);
}

void ath9k_hw_setup_statusring(struct ath_hw *ah, void *ts_start,
			       u32 ts_paddr_start,
			       u8 size)
{

	ah->ts_paddr_start = ts_paddr_start;
	ah->ts_paddr_end = ts_paddr_start + (size * sizeof(struct ar9003_txs));
	ah->ts_size = size;
	ah->ts_ring = (struct ar9003_txs *) ts_start;

	ath9k_hw_reset_txstatus_ring(ah);
}
EXPORT_SYMBOL(ath9k_hw_setup_statusring);
