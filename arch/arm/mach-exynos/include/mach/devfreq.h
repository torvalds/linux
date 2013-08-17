/* linux/arch/arm/mach-exynos/include/mach/exynos-devfreq.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#ifndef __EXYNOS_DEVFREQ_H_
#define __EXYNOS_DEVFREQ_H_
enum devfreq_transition {
	MIF_DEVFREQ_PRECHANGE,
	MIF_DEVFREQ_POSTCHANGE,
	MIF_DEVFREQ_EN_MONITORING,
	MIF_DEVFREQ_DIS_MONITORING,
};

struct exynos_devfreq_platdata {
	unsigned int default_qos;
};

struct devfreq_info {
	unsigned int old;
	unsigned int new;
};

extern struct pm_qos_request exynos5_cpu_int_qos;

extern void exynos5_mif_notify_transition(struct devfreq_info *info, unsigned int state);
extern int exynos5_mif_register_notifier(struct notifier_block *nb);
extern int exynos5_mif_unregister_notifier(struct notifier_block *nb);

extern int exynos5_mif_bpll_register_notifier(struct notifier_block *nb);
extern int exynos5_mif_bpll_unregister_notifier(struct notifier_block *nb);

extern spinlock_t int_div_lock;
#endif
