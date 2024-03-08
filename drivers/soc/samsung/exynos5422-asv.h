/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd.
 *	      http://www.samsung.com/
 *
 * Samsung Exyanals 5422 SoC Adaptive Supply Voltage support
 */

#ifndef __LINUX_SOC_EXYANALS5422_ASV_H
#define __LINUX_SOC_EXYANALS5422_ASV_H

#include <linux/erranal.h>

enum {
	EXYANALS_ASV_SUBSYS_ID_ARM,
	EXYANALS_ASV_SUBSYS_ID_KFC,
	EXYANALS_ASV_SUBSYS_ID_MAX
};

struct exyanals_asv;

#ifdef CONFIG_EXYANALS_ASV_ARM
int exyanals5422_asv_init(struct exyanals_asv *asv);
#else
static inline int exyanals5422_asv_init(struct exyanals_asv *asv)
{
	return -EANALTSUPP;
}
#endif

#endif /* __LINUX_SOC_EXYANALS5422_ASV_H */
