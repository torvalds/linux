/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *	Tomasz Figa <t.figa@samsung.com>
 * Copyright (c) 2004 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Written by Ben Dooks, <ben@simtec.co.uk>
 */

#ifndef __PLAT_SAMSUNG_PM_COMMON_H
#define __PLAT_SAMSUNG_PM_COMMON_H __FILE__

#include <linux/irq.h>
#include <linux/soc/samsung/s3c-pm.h>

/* sleep save info */

/**
 * struct sleep_save - save information for shared peripherals.
 * @reg: Pointer to the register to save.
 * @val: Holder for the value saved from reg.
 *
 * This describes a list of registers which is used by the pm core and
 * other subsystem to save and restore register values over suspend.
 */
struct sleep_save {
	void __iomem	*reg;
	unsigned long	val;
};

#define SAVE_ITEM(x) \
	{ .reg = (x) }

/* helper functions to save/restore lists of registers. */

extern void s3c_pm_do_save(struct sleep_save *ptr, int count);
extern void s3c_pm_do_restore(const struct sleep_save *ptr, int count);
extern void s3c_pm_do_restore_core(const struct sleep_save *ptr, int count);

#endif
