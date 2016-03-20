/*
 * linux/arch/unicore/include/mach/pm.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __PUV3_PM_H__
#define __PUV3_PM_H__

#include <linux/suspend.h>

struct puv3_cpu_pm_fns {
	int	save_count;
	void	(*save)(unsigned long *);
	void	(*restore)(unsigned long *);
	int	(*valid)(suspend_state_t state);
	void	(*enter)(suspend_state_t state);
	int	(*prepare)(void);
	void	(*finish)(void);
};

extern struct puv3_cpu_pm_fns *puv3_cpu_pm_fns;

/* sleep.S */
extern void puv3_cpu_suspend(unsigned int);

extern void puv3_cpu_resume(void);

extern int puv3_pm_enter(suspend_state_t state);

/* Defined in hibernate_asm.S */
extern int restore_image(pgd_t *resume_pg_dir, struct pbe *restore_pblist);

extern struct pbe *restore_pblist;
#endif
