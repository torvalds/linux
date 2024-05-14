/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2019 ARM Limited (or its affiliates). */

/* \file cc_pm.h
 */

#ifndef __CC_POWER_MGR_H__
#define __CC_POWER_MGR_H__

#include "cc_driver.h"

#define CC_SUSPEND_TIMEOUT 3000

#if defined(CONFIG_PM)

extern const struct dev_pm_ops ccree_pm;

int cc_pm_get(struct device *dev);
void cc_pm_put_suspend(struct device *dev);

#else

static inline int cc_pm_get(struct device *dev)
{
	return 0;
}

static inline void cc_pm_put_suspend(struct device *dev) {}

#endif

#endif /*__POWER_MGR_H__*/
