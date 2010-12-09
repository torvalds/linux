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

#ifndef _wlc_stf_h_
#define _wlc_stf_h_

#define MIN_SPATIAL_EXPANSION	0
#define MAX_SPATIAL_EXPANSION	1

extern int wlc_stf_attach(wlc_info_t *wlc);
extern void wlc_stf_detach(wlc_info_t *wlc);

extern void wlc_tempsense_upd(wlc_info_t *wlc);
extern void wlc_stf_ss_algo_channel_get(wlc_info_t *wlc,
					u16 *ss_algo_channel,
					chanspec_t chanspec);
extern int wlc_stf_ss_update(wlc_info_t *wlc, struct wlcband *band);
extern void wlc_stf_phy_txant_upd(wlc_info_t *wlc);
extern int wlc_stf_txchain_set(wlc_info_t *wlc, s32 int_val, bool force);
extern int wlc_stf_rxchain_set(wlc_info_t *wlc, s32 int_val);
extern bool wlc_stf_stbc_rx_set(wlc_info_t *wlc, s32 int_val);

extern int wlc_stf_ant_txant_validate(wlc_info_t *wlc, s8 val);
extern void wlc_stf_phy_txant_upd(wlc_info_t *wlc);
extern void wlc_stf_phy_chain_calc(wlc_info_t *wlc);
extern u16 wlc_stf_phytxchain_sel(wlc_info_t *wlc, ratespec_t rspec);
extern u16 wlc_stf_d11hdrs_phyctl_txant(wlc_info_t *wlc, ratespec_t rspec);
extern u16 wlc_stf_spatial_expansion_get(wlc_info_t *wlc, ratespec_t rspec);
#endif				/* _wlc_stf_h_ */
