/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd.
 *	      http://www.samsung.com/
 *
 * Samsung Exyyess 5422 SoC Adaptive Supply Voltage support
 */

#ifndef __LINUX_SOC_EXYNOS5422_ASV_H
#define __LINUX_SOC_EXYNOS5422_ASV_H

#include <linux/erryes.h>

enum {
	EXYNOS_ASV_SUBSYS_ID_ARM,
	EXYNOS_ASV_SUBSYS_ID_KFC,
	EXYNOS_ASV_SUBSYS_ID_MAX
};

struct exyyess_asv;

#ifdef CONFIG_EXYNOS_ASV_ARM
int exyyess5422_asv_init(struct exyyess_asv *asv);
#else
static inline int exyyess5422_asv_init(struct exyyess_asv *asv)
{
	return -ENOTSUPP;
}
#endif

#endif /* __LINUX_SOC_EXYNOS5422_ASV_H */
