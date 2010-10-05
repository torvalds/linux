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

#ifndef _WLC_EVENT_H_
#define _WLC_EVENT_H_

typedef struct wlc_eventq wlc_eventq_t;

typedef void (*wlc_eventq_cb_t) (void *arg);

extern wlc_eventq_t *wlc_eventq_attach(wlc_pub_t *pub, struct wlc_info *wlc,
				       void *wl, wlc_eventq_cb_t cb);
extern int wlc_eventq_detach(wlc_eventq_t *eq);
extern int wlc_eventq_down(wlc_eventq_t *eq);
extern void wlc_event_free(wlc_eventq_t *eq, wlc_event_t *e);
extern wlc_event_t *wlc_eventq_next(wlc_eventq_t *eq, wlc_event_t *e);
extern int wlc_eventq_cnt(wlc_eventq_t *eq);
extern bool wlc_eventq_avail(wlc_eventq_t *eq);
extern wlc_event_t *wlc_eventq_deq(wlc_eventq_t *eq);
extern void wlc_eventq_enq(wlc_eventq_t *eq, wlc_event_t *e);
extern wlc_event_t *wlc_event_alloc(wlc_eventq_t *eq);

extern int wlc_eventq_register_ind(wlc_eventq_t *eq, void *bitvect);
extern int wlc_eventq_query_ind(wlc_eventq_t *eq, void *bitvect);
extern int wlc_eventq_test_ind(wlc_eventq_t *eq, int et);
extern int wlc_eventq_set_ind(wlc_eventq_t *eq, uint et, bool on);
extern void wlc_eventq_flush(wlc_eventq_t *eq);
extern void wlc_assign_event_msg(wlc_info_t *wlc, wl_event_msg_t *msg,
				 const wlc_event_t *e, u8 *data,
				 uint32 len);

#ifdef MSGTRACE
extern void wlc_event_sendup_trace(struct wlc_info *wlc, hndrte_dev_t *bus,
				   u8 *hdr, uint16 hdrlen, u8 *buf,
				   uint16 buflen);
#endif

#endif				/* _WLC_EVENT_H_ */
