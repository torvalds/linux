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

#ifndef WL_ERR
#define WL_ERR CFG80211_ERR
#endif
#ifndef WL_TRACE
#define WL_TRACE CFG80211_TRACE
#endif

s32 dhd_cfg80211_init(struct bcm_cfg80211 *cfg);
s32 dhd_cfg80211_deinit(struct bcm_cfg80211 *cfg);
s32 dhd_cfg80211_down(struct bcm_cfg80211 *cfg);
s32 dhd_cfg80211_set_p2p_info(struct bcm_cfg80211 *cfg, int val);
s32 dhd_cfg80211_clean_p2p_info(struct bcm_cfg80211 *cfg);
s32 dhd_config_dongle(struct bcm_cfg80211 *cfg);

#endif /* __DHD_CFG80211__ */
