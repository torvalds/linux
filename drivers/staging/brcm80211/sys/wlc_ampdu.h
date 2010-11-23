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

extern ampdu_info_t *wlc_ampdu_attach(wlc_info_t *wlc);
extern void wlc_ampdu_detach(ampdu_info_t *ampdu);
extern bool wlc_ampdu_cap(ampdu_info_t *ampdu);
extern int wlc_ampdu_set(ampdu_info_t *ampdu, bool on);
extern int wlc_sendampdu(ampdu_info_t *ampdu, wlc_txq_info_t *qi,
			 struct sk_buff **aggp, int prec);
extern void wlc_ampdu_dotxstatus(ampdu_info_t *ampdu, struct scb *scb,
				 struct sk_buff *p, tx_status_t *txs);
extern void wlc_ampdu_reset(ampdu_info_t *ampdu);
extern void wlc_ampdu_macaddr_upd(wlc_info_t *wlc);
extern void wlc_ampdu_shm_upd(ampdu_info_t *ampdu);

extern u8 wlc_ampdu_null_delim_cnt(ampdu_info_t *ampdu, struct scb *scb,
				      ratespec_t rspec, int phylen);
extern void scb_ampdu_cleanup(ampdu_info_t *ampdu, struct scb *scb);

#endif				/* _wlc_ampdu_h_ */
