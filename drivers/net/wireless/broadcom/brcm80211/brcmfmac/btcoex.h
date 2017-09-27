/*
 * Copyright (c) 2013 Broadcom Corporation
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
#ifndef WL_BTCOEX_H_
#define WL_BTCOEX_H_

enum brcmf_btcoex_mode {
	BRCMF_BTCOEX_DISABLED,
	BRCMF_BTCOEX_ENABLED
};

int brcmf_btcoex_attach(struct brcmf_cfg80211_info *cfg);
void brcmf_btcoex_detach(struct brcmf_cfg80211_info *cfg);
int brcmf_btcoex_set_mode(struct brcmf_cfg80211_vif *vif,
			  enum brcmf_btcoex_mode mode, u16 duration);

#endif /* WL_BTCOEX_H_ */
