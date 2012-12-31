/* linux/arch/arm/mach-exynos/include/mach/dev.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * EXYNOS4 - Device List support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_DEV_H
#define __ASM_ARCH_DEV_H __FILE__

struct device;

struct domain_lock {
	struct list_head node;

	struct device_domain *domain;
	struct device *device;
	unsigned long freq;
};

struct device_domain {
	struct list_head node;

	struct device *device;
	struct list_head domain_list;
};

int dev_add(struct device_domain *domain, struct device *device);
struct device *dev_get(const char *name);
void dev_put(const char *name);
int dev_lock(struct device *device, struct device *dev, unsigned long freq);
int dev_lock_fix(struct device *device, struct device *dev, unsigned long freq);
int dev_unlock(struct device *device, struct device *dev);
void dev_unlock_fix(struct device *device, struct device *dev);
unsigned long dev_max_freq(struct device *device);
int dev_lock_list(struct device *dev, char *buf);

#endif /* __ASM_ARCH_DEV_H */
