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
#define AR_TxRxDesc	0x00008000

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

void ar9003_hw_attach_mac_ops(struct ath_hw *hw);
void ath9k_hw_set_rx_bufsize(struct ath_hw *ah, u16 buf_size);
void ath9k_hw_addrxbuf_edma(struct ath_hw *ah, u32 rxdp,
			    enum ath9k_rx_qtype qtype);

int ath9k_hw_process_rxdesc_edma(struct ath_hw *ah,
				 struct ath_rx_status *rxs,
				 void *buf_addr);

#endif
