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

#ifndef _wlc_antsel_h_
#define _wlc_antsel_h_
extern struct antsel_info *wlc_antsel_attach(struct wlc_info *wlc,
					struct osl_info *osh,
					struct wlc_pub *pub,
					struct wlc_hw_info *wlc_hw);
extern void wlc_antsel_detach(struct antsel_info *asi);
extern void wlc_antsel_init(struct antsel_info *asi);
extern void wlc_antsel_antcfg_get(struct antsel_info *asi, bool usedef,
				  bool sel,
				  u8 id, u8 fbid, u8 *antcfg,
				  u8 *fbantcfg);
extern u8 wlc_antsel_antsel2id(struct antsel_info *asi, u16 antsel);
#endif				/* _wlc_antsel_h_ */
