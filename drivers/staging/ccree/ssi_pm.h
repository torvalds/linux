/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

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

