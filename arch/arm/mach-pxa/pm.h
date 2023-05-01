/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2005 Richard Purdie
 */

#include <linux/suspend.h>

struct pxa_cpu_pm_fns {
	int	save_count;
	void	(*save)(unsigned long *);
	void	(*restore)(unsigned long *);
	int	(*valid)(suspend_state_t state);
	void	(*enter)(suspend_state_t state);
	int	(*prepare)(void);
	void	(*finish)(void);
};

extern struct pxa_cpu_pm_fns *pxa_cpu_pm_fns;

/* sleep.S */
extern int pxa25x_finish_suspend(unsigned long);
extern int pxa27x_finish_suspend(unsigned long);

extern int pxa_pm_enter(suspend_state_t state);
extern int pxa_pm_prepare(void);
extern void pxa_pm_finish(void);

extern const char pm_enter_standby_start[], pm_enter_standby_end[];
extern int pxa3xx_finish_suspend(unsigned long);
