/* drivers/gpu/vithar/kbase/src/platform/mali_kbase_platform.h
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC Mali-T604 platform-dependent codes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file mali_kbase_platform.h
 * Platform-dependent init
 */

#ifndef _KBASE_PLATFORM_H_
#define _KBASE_PLATFORM_H_

/* All things that are needed for the Linux port. */
int kbase_platform_cmu_pmu_control(struct device *dev, int control);
void kbase_platform_remove_sysfs_file(struct device *dev);
int kbase_platform_init(struct device *dev);
int kbase_platform_is_power_on(void);
void kbase_platform_term(struct device *dev);
#endif /* _KBASE_PLATFORM_H_ */
