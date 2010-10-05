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
struct wl_info;
struct wl_if;
struct wlc_if;
extern void wl_init(struct wl_info *wl);
extern uint wl_reset(struct wl_info *wl);
extern void wl_intrson(struct wl_info *wl);
extern uint32 wl_intrsoff(struct wl_info *wl);
extern void wl_intrsrestore(struct wl_info *wl, uint32 macintmask);
extern void wl_event(struct wl_info *wl, char *ifname, wlc_event_t *e);
extern void wl_event_sendup(struct wl_info *wl, const wlc_event_t *e,
			    u8 *data, uint32 len);
extern int wl_up(struct wl_info *wl);
extern void wl_down(struct wl_info *wl);
extern void wl_txflowcontrol(struct wl_info *wl, struct wl_if *wlif, bool state,
			     int prio);
extern bool wl_alloc_dma_resources(struct wl_info *wl, uint dmaddrwidth);

/* timer functions */
struct wl_timer;
extern struct wl_timer *wl_init_timer(struct wl_info *wl,
				      void (*fn) (void *arg), void *arg,
				      const char *name);
extern void wl_free_timer(struct wl_info *wl, struct wl_timer *timer);
extern void wl_add_timer(struct wl_info *wl, struct wl_timer *timer, uint ms,
			 int periodic);
extern bool wl_del_timer(struct wl_info *wl, struct wl_timer *timer);

extern uint wl_buf_to_pktcopy(osl_t *osh, void *p, unsigned char *buf, int len,
			      uint offset);
extern void *wl_get_pktbuffer(osl_t *osh, int len);
extern int wl_set_pktlen(osl_t *osh, void *p, int len);

#define wl_sort_bsslist(a, b) FALSE

extern int wl_tkip_miccheck(struct wl_info *wl, void *p, int hdr_len,
			    bool group_key, int id);
extern int wl_tkip_micadd(struct wl_info *wl, void *p, int hdr_len);
extern int wl_tkip_encrypt(struct wl_info *wl, void *p, int hdr_len);
extern int wl_tkip_decrypt(struct wl_info *wl, void *p, int hdr_len,
			   bool group_key);
extern void wl_tkip_printstats(struct wl_info *wl, bool group_key);
extern int wl_tkip_keyset(struct wl_info *wl, wsec_key_t *key);
#endif				/* _wl_export_h_ */
