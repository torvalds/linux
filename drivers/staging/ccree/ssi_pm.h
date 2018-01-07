/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

/* \file ssi_pm.h
 */

#ifndef __CC_POWER_MGR_H__
#define __CC_POWER_MGR_H__

#include "ssi_driver.h"

#define CC_SUSPEND_TIMEOUT 3000

int cc_pm_init(struct cc_drvdata *drvdata);

void cc_pm_fini(struct cc_drvdata *drvdata);

#if defined(CONFIG_PM)

extern const struct dev_pm_ops ccree_pm;

int cc_pm_suspend(struct device *dev);

int cc_pm_resume(struct device *dev);

int cc_pm_get(struct device *dev);

int cc_pm_put_suspend(struct device *dev);
#endif

#endif /*__POWER_MGR_H__*/

