/* drivers/gpu/vithar/kbase/src/platform/mali_kbase_runtime_pm.h
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC Mali-T604 runtime pm driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file mali_kbase_runtime_pm.h
 * Runtime PM
 */

#ifndef _KBASE_RUNTIME_PM_H_
#define _KBASE_RUNTIME_PM_H_

/* All things that are needed for the Linux port. */

int kbase_device_runtime_suspend(struct device *dev);
int kbase_device_runtime_resume(struct device *dev);
void kbase_device_runtime_init_timer(struct device *dev);
void kbase_device_runtime_disable(struct device *dev);
void kbase_device_runtime_get_sync(struct device *dev);
void kbase_device_runtime_put_sync(struct device *dev);

/* Delay time to enter into runtime-suspend */
#define RUNTIME_PM_RUNTIME_DELAY_TIME 500

#endif /* _KBASE_RUNTIME_PM_H_ */
