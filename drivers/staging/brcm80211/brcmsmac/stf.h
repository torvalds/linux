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

#ifndef _BRCM_STF_H_
#define _BRCM_STF_H_

#include "types.h"

extern int brcms_c_stf_attach(struct brcms_c_info *wlc);
extern void brcms_c_stf_detach(struct brcms_c_info *wlc);

extern void brcms_c_tempsense_upd(struct brcms_c_info *wlc);
extern void brcms_c_stf_ss_algo_channel_get(struct brcms_c_info *wlc,
					u16 *ss_algo_channel,
					chanspec_t chanspec);
extern int brcms_c_stf_ss_update(struct brcms_c_info *wlc,
			     struct brcms_band *band);
extern void brcms_c_stf_phy_txant_upd(struct brcms_c_info *wlc);
extern int brcms_c_stf_txchain_set(struct brcms_c_info *wlc, s32 int_val,
			       bool force);
extern bool brcms_c_stf_stbc_rx_set(struct brcms_c_info *wlc, s32 int_val);
extern void brcms_c_stf_phy_txant_upd(struct brcms_c_info *wlc);
extern void brcms_c_stf_phy_chain_calc(struct brcms_c_info *wlc);
extern u16 brcms_c_stf_phytxchain_sel(struct brcms_c_info *wlc,
				      ratespec_t rspec);
extern u16 brcms_c_stf_d11hdrs_phyctl_txant(struct brcms_c_info *wlc,
					ratespec_t rspec);

#endif				/* _BRCM_STF_H_ */
