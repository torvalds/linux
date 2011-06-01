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

#ifndef _wl_export_h_
#define _wl_export_h_

/* misc callbacks */
struct brcms_info;
struct brcms_if;
struct wlc_if;
extern void brcms_init(struct brcms_info *wl);
extern uint brcms_reset(struct brcms_info *wl);
extern void brcms_intrson(struct brcms_info *wl);
extern u32 brcms_intrsoff(struct brcms_info *wl);
extern void brcms_intrsrestore(struct brcms_info *wl, u32 macintmask);
extern int brcms_up(struct brcms_info *wl);
extern void brcms_down(struct brcms_info *wl);
extern void brcms_txflowcontrol(struct brcms_info *wl, struct brcms_if *wlif,
				bool state, int prio);
extern bool wl_alloc_dma_resources(struct brcms_info *wl, uint dmaddrwidth);
extern bool brcms_rfkill_set_hw_state(struct brcms_info *wl);

/* timer functions */
struct brcms_timer;
extern struct brcms_timer *brcms_init_timer(struct brcms_info *wl,
				      void (*fn) (void *arg), void *arg,
				      const char *name);
extern void brcms_free_timer(struct brcms_info *wl, struct brcms_timer *timer);
extern void brcms_add_timer(struct brcms_info *wl, struct brcms_timer *timer,
			    uint ms, int periodic);
extern bool brcms_del_timer(struct brcms_info *wl, struct brcms_timer *timer);
extern void brcms_msleep(struct brcms_info *wl, uint ms);

#endif				/* _wl_export_h_ */
