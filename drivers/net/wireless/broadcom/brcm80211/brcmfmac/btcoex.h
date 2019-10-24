// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2013 Broadcom Corporation
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
