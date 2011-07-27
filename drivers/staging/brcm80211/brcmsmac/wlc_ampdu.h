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

#ifndef _wlc_ampdu_h_
#define _wlc_ampdu_h_

extern struct ampdu_info *wlc_ampdu_attach(struct wlc_info *wlc);
extern void wlc_ampdu_detach(struct ampdu_info *ampdu);
extern int wlc_sendampdu(struct ampdu_info *ampdu, struct wlc_txq_info *qi,
			 struct sk_buff **aggp, int prec);
extern void wlc_ampdu_dotxstatus(struct ampdu_info *ampdu, struct scb *scb,
				 struct sk_buff *p, tx_status_t *txs);
extern void wlc_ampdu_macaddr_upd(struct wlc_info *wlc);
extern void wlc_ampdu_shm_upd(struct ampdu_info *ampdu);

#endif				/* _wlc_ampdu_h_ */
