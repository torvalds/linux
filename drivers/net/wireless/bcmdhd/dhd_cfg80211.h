/*
 * Linux cfg80211 driver - Dongle Host Driver (DHD) related
 *
 * Copyright (C) 1999-2014, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: wl_cfg80211.c,v 1.1.4.1.2.14 2011/02/09 01:40:07 Exp $
 */


#ifndef __DHD_CFG80211__
#define __DHD_CFG80211__

#include <wl_cfg80211.h>
#include <wl_cfgp2p.h>

s32 dhd_cfg80211_init(struct bcm_cfg80211 *cfg);
s32 dhd_cfg80211_deinit(struct bcm_cfg80211 *cfg);
s32 dhd_cfg80211_down(struct bcm_cfg80211 *cfg);
s32 dhd_cfg80211_set_p2p_info(struct bcm_cfg80211 *cfg, int val);
s32 dhd_cfg80211_clean_p2p_info(struct bcm_cfg80211 *cfg);
s32 dhd_config_dongle(struct bcm_cfg80211 *cfg);

#ifdef CONFIG_NL80211_TESTMODE
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
int dhd_cfg80211_testmode_cmd(struct wiphy *wiphy, struct wireless_dev *wdev, void *data, int len);
#else
int dhd_cfg80211_testmode_cmd(struct wiphy *wiphy, void *data, int len);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) */
#else
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
static inline int
dhd_cfg80211_testmode_cmd(struct wiphy *wiphy, struct wireless_dev *wdev, void *data, int len)
#else
static inline int dhd_cfg80211_testmode_cmd(struct wiphy *wiphy, void *data, int len)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) */
{
	return 0;
}
#endif /* CONFIG_NL80211_TESTMODE */

#endif /* __DHD_CFG80211__ */
