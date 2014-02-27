/*
 * Linux cfg80211 driver - Dongle Host Driver (DHD) related
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: wl_cfg80211.c,v 1.1.4.1.2.14 2011/02/09 01:40:07 Exp $
 */


#ifndef __DHD_CFG80211__
#define __DHD_CFG80211__

#include <wl_cfg80211.h>
#include <wl_cfgp2p.h>

s32 dhd_cfg80211_init(struct wl_priv *wl);
s32 dhd_cfg80211_deinit(struct wl_priv *wl);
s32 dhd_cfg80211_down(struct wl_priv *wl);
s32 dhd_cfg80211_set_p2p_info(struct wl_priv *wl, int val);
s32 dhd_cfg80211_clean_p2p_info(struct wl_priv *wl);
s32 dhd_config_dongle(struct wl_priv *wl, bool need_lock);

#ifdef CONFIG_NL80211_TESTMODE
int dhd_cfg80211_testmode_cmd(struct wiphy *wiphy, void *data, int len);
#else
static inline int dhd_cfg80211_testmode_cmd(struct wiphy *wiphy, void *data, int len)
{
	return 0;
}
#endif

int wl_cfg80211_btcoex_init(struct wl_priv *wl);
void wl_cfg80211_btcoex_deinit(struct wl_priv *wl);

#endif /* __DHD_CFG80211__ */
