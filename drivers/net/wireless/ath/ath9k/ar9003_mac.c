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

static void ar9003_hw_rx_enable(struct ath_hw *hw)
{
	REG_WRITE(hw, AR_CR, 0);
}

static void ar9003_hw_set_desc_link(void *ds, u32 ds_link)
{
	((struct ar9003_txc *) ds)->link = ds_link;
}

static void ar9003_hw_get_desc_link(void *ds, u32 **ds_link)
{
	*ds_link = &((struct ar9003_txc *) ds)->link;
}

void ar9003_hw_attach_mac_ops(struct ath_hw *hw)
{
	struct ath_hw_ops *ops = ath9k_hw_ops(hw);

	ops->rx_enable = ar9003_hw_rx_enable;
	ops->set_desc_link = ar9003_hw_set_desc_link;
	ops->get_desc_link = ar9003_hw_get_desc_link;
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
		if (rxsp->status11 & AR_CRCErr) {
			rxs->rs_status |= ATH9K_RXERR_CRC;
		} else if (rxsp->status11 & AR_PHYErr) {
			rxs->rs_status |= ATH9K_RXERR_PHY;
			phyerr = MS(rxsp->status11, AR_PHYErrCode);
			rxs->rs_phyerr = phyerr;
		} else if (rxsp->status11 & AR_DecryptCRCErr) {
			rxs->rs_status |= ATH9K_RXERR_DECRYPT;
		} else if (rxsp->status11 & AR_MichaelErr) {
			rxs->rs_status |= ATH9K_RXERR_MIC;
		}
	}

	return 0;
}
EXPORT_SYMBOL(ath9k_hw_process_rxdesc_edma);
