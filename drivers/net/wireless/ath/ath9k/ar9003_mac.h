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

#ifndef AR9003_MAC_H
#define AR9003_MAC_H

#define AR_DescId	0xffff0000
#define AR_DescId_S	16
#define AR_CtrlStat	0x00004000
#define AR_CtrlStat_S	14
#define AR_TxRxDesc	0x00008000
#define AR_TxRxDesc_S	15
#define AR_TxQcuNum	0x00000f00
#define AR_TxQcuNum_S	8

#define AR_BufLen	0x0fff0000
#define AR_BufLen_S	16

#define AR_TxDescId	0xffff0000
#define AR_TxDescId_S	16
#define AR_TxPtrChkSum	0x0000ffff

#define AR_LowRxChain	0x00004000

#define AR_Not_Sounding	0x20000000

/* ctl 12 */
#define AR_PAPRDChainMask	0x00000e00
#define AR_PAPRDChainMask_S	9

#define MAP_ISR_S2_CST          6
#define MAP_ISR_S2_GTT          6
#define MAP_ISR_S2_TIM          3
#define MAP_ISR_S2_CABEND       0
#define MAP_ISR_S2_DTIMSYNC     7
#define MAP_ISR_S2_DTIM         7
#define MAP_ISR_S2_TSFOOR       4
#define MAP_ISR_S2_BB_WATCHDOG  6

#define AR9003TXC_CONST(_ds) ((const struct ar9003_txc *) _ds)

struct ar9003_rxs {
	u32 ds_info;
	u32 status1;
	u32 status2;
	u32 status3;
	u32 status4;
	u32 status5;
	u32 status6;
	u32 status7;
	u32 status8;
	u32 status9;
	u32 status10;
	u32 status11;
} __packed;

/* Transmit Control Descriptor */
struct ar9003_txc {
	u32 info;   /* descriptor information */
	u32 link;   /* link pointer */
	u32 data0;  /* data pointer to 1st buffer */
	u32 ctl3;   /* DMA control 3  */
	u32 data1;  /* data pointer to 2nd buffer */
	u32 ctl5;   /* DMA control 5  */
	u32 data2;  /* data pointer to 3rd buffer */
	u32 ctl7;   /* DMA control 7  */
	u32 data3;  /* data pointer to 4th buffer */
	u32 ctl9;   /* DMA control 9  */
	u32 ctl10;  /* DMA control 10 */
	u32 ctl11;  /* DMA control 11 */
	u32 ctl12;  /* DMA control 12 */
	u32 ctl13;  /* DMA control 13 */
	u32 ctl14;  /* DMA control 14 */
	u32 ctl15;  /* DMA control 15 */
	u32 ctl16;  /* DMA control 16 */
	u32 ctl17;  /* DMA control 17 */
	u32 ctl18;  /* DMA control 18 */
	u32 ctl19;  /* DMA control 19 */
	u32 ctl20;  /* DMA control 20 */
	u32 ctl21;  /* DMA control 21 */
	u32 ctl22;  /* DMA control 22 */
	u32 pad[9]; /* pad to cache line (128 bytes/32 dwords) */
} __packed;

struct ar9003_txs {
	u32 ds_info;
	u32 status1;
	u32 status2;
	u32 status3;
	u32 status4;
	u32 status5;
	u32 status6;
	u32 status7;
	u32 status8;
} __packed;

void ar9003_hw_attach_mac_ops(struct ath_hw *hw);
void ath9k_hw_set_rx_bufsize(struct ath_hw *ah, u16 buf_size);
void ath9k_hw_addrxbuf_edma(struct ath_hw *ah, u32 rxdp,
			    enum ath9k_rx_qtype qtype);

int ath9k_hw_process_rxdesc_edma(struct ath_hw *ah,
				 struct ath_rx_status *rxs,
				 void *buf_addr);
void ath9k_hw_reset_txstatus_ring(struct ath_hw *ah);
void ath9k_hw_setup_statusring(struct ath_hw *ah, void *ts_start,
			       u32 ts_paddr_start,
			       u8 size);
#endif
