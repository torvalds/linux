/*
 * Mac80211 power management interface for ST-Ericsson CW1200 mac80211 drivers
 *
 * Copyright (c) 2011, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PM_H_INCLUDED
#define PM_H_INCLUDED

/* ******************************************************************** */
/* mac80211 API								*/

/* extern */  struct cw1200_common;
/* private */ struct cw1200_suspend_state;

struct cw1200_pm_state {
	struct cw1200_suspend_state *suspend_state;
	struct timer_list stay_awake;
	struct platform_device *pm_dev;
	spinlock_t lock; /* Protect access */
};

#ifdef CONFIG_PM
int cw1200_pm_init(struct cw1200_pm_state *pm,
		    struct cw1200_common *priv);
void cw1200_pm_deinit(struct cw1200_pm_state *pm);
int cw1200_wow_suspend(struct ieee80211_hw *hw,
		       struct cfg80211_wowlan *wowlan);
int cw1200_can_suspend(struct cw1200_common *priv);
int cw1200_wow_resume(struct ieee80211_hw *hw);
void cw1200_pm_stay_awake(struct cw1200_pm_state *pm,
			  unsigned long tmo);
#else
static inline void cw1200_pm_stay_awake(struct cw1200_pm_state *pm,
					unsigned long tmo)
{
}
static inline int cw1200_can_suspend(struct cw1200_common *priv)
{
	return 0;
}
#endif
#endif
