/*
 * Blackfin bf609 power management
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2
 */

#ifndef __PM_H__
#define __PM_H__

#include <linux/suspend.h>

struct bfin_cpu_pm_fns {
	void    (*save)(unsigned long *);
	void    (*restore)(unsigned long *);
	int     (*valid)(suspend_state_t state);
	void    (*enter)(suspend_state_t state);
	int     (*prepare)(void);
	void    (*finish)(void);
};

extern struct bfin_cpu_pm_fns *bfin_cpu_pm;

# ifdef CONFIG_BFIN_COREB
void bfin_coreb_start(void);
void bfin_coreb_stop(void);
void bfin_coreb_reset(void);
# endif

#endif
