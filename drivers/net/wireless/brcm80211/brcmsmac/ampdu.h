/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _BRCM_AMPDU_H_
#define _BRCM_AMPDU_H_

extern struct ampdu_info *brcms_c_ampdu_attach(struct brcms_c_info *wlc);
extern void brcms_c_ampdu_detach(struct ampdu_info *ampdu);
extern int brcms_c_sendampdu(struct ampdu_info *ampdu,
			     struct brcms_txq_info *qi,
			     struct sk_buff **aggp, int prec);
extern void brcms_c_ampdu_dotxstatus(struct ampdu_info *ampdu, struct scb *scb,
				 struct sk_buff *p, struct tx_status *txs);
extern void brcms_c_ampdu_macaddr_upd(struct brcms_c_info *wlc);
extern void brcms_c_ampdu_shm_upd(struct ampdu_info *ampdu);

#endif				/* _BRCM_AMPDU_H_ */
