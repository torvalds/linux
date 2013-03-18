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

/*
 * Data structure representing an in-progress session for accumulating
 * frames for AMPDU.
 *
 * wlc: pointer to common driver data
 * skb_list: queue of skb's for AMPDU
 * max_ampdu_len: maximum length for this AMPDU
 * max_ampdu_frames: maximum number of frames for this AMPDU
 * ampdu_len: total number of bytes accumulated for this AMPDU
 * dma_len: DMA length of this AMPDU
 */
struct brcms_ampdu_session {
	struct brcms_c_info *wlc;
	struct sk_buff_head skb_list;
	unsigned max_ampdu_len;
	u16 max_ampdu_frames;
	u16 ampdu_len;
	u16 dma_len;
};

extern void brcms_c_ampdu_reset_session(struct brcms_ampdu_session *session,
					struct brcms_c_info *wlc);
extern int brcms_c_ampdu_add_frame(struct brcms_ampdu_session *session,
				   struct sk_buff *p);
extern void brcms_c_ampdu_finalize(struct brcms_ampdu_session *session);

extern struct ampdu_info *brcms_c_ampdu_attach(struct brcms_c_info *wlc);
extern void brcms_c_ampdu_detach(struct ampdu_info *ampdu);
extern void brcms_c_ampdu_dotxstatus(struct ampdu_info *ampdu, struct scb *scb,
				 struct sk_buff *p, struct tx_status *txs);
extern void brcms_c_ampdu_macaddr_upd(struct brcms_c_info *wlc);
extern void brcms_c_ampdu_shm_upd(struct ampdu_info *ampdu);

#endif				/* _BRCM_AMPDU_H_ */
