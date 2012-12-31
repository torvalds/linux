/* linux/arch/arm/mach-exynos/dev.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * EXYNOS4 Device List support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <mach/dev.h>
#ifdef CONFIG_ARCH_EXYNOS4
#include <mach/busfreq_exynos4.h>
#else
#include <mach/busfreq_exynos5.h>
#endif

static LIST_HEAD(domains_list);
static DEFINE_MUTEX(domains_mutex);

static struct device_domain *find_device_domain(struct device *dev)
{
	struct device_domain *tmp_domain, *domain = ERR_PTR(-ENODEV);

	mutex_lock(&domains_mutex);
	list_for_each_entry(tmp_domain, &domains_list, node) {
		if (tmp_domain->device == dev) {
			domain = tmp_domain;
			break;
		}
	}

	mutex_unlock(&domains_mutex);
	return domain;
}

int dev_add(struct device_domain *dev, struct device *device)
{
	if (!dev || !device)
		return -EINVAL;

	mutex_lock(&domains_mutex);
	INIT_LIST_HEAD(&dev->domain_list);
	dev->device = device;
	list_add(&dev->node, &domains_list);
	mutex_unlock(&domains_mutex);

	return 0;
}

struct device *dev_get(const char *name)
{
	struct device_domain *domain;

	mutex_lock(&domains_mutex);
	list_for_each_entry(domain, &domains_list, node)
		if (strcmp(name, dev_name(domain->device)) == 0)
			goto found;

	mutex_unlock(&domains_mutex);
	return ERR_PTR(-ENODEV);
found:
	mutex_unlock(&domains_mutex);
	return domain->device;
}

void dev_put(const char *name)
{
	return;
}

static int _dev_lock(struct device *device, struct device *dev,
		unsigned long freq, bool fix)
{
	struct device_domain *domain;
	struct domain_lock *lock;
	int ret = 0;

	domain = find_device_domain(device);

	if (IS_ERR(domain)) {
		dev_err(dev, "Can't find device domain.\n");
		return -EINVAL;
	}

	mutex_lock(&domains_mutex);
	list_for_each_entry(lock, &domain->domain_list, node) {
		if (lock->device == dev) {
			/* If the lock already exist, only update the freq */
			lock->freq = freq;
			goto out;
		}
	}

	lock = kzalloc(sizeof(struct domain_lock), GFP_KERNEL);
	if (!lock) {
		dev_err(device, "Unable to create domain_lock");
		ret = -ENOMEM;
		goto out;
	}

	lock->device = dev;
	lock->freq = freq;
	list_add(&lock->node, &domain->domain_list);

out:
	mutex_unlock(&domains_mutex);
	exynos_request_apply(freq, fix, false);
	return ret;
}

int dev_lock(struct device *device, struct device *dev, unsigned long freq)
{
	return _dev_lock(device, dev, freq, false);
}

int dev_lock_fix(struct device *device, struct device *dev, unsigned long freq)
{
	return _dev_lock(device, dev, freq, true);
}

int dev_unlock(struct device *device, struct device *dev)
{
	struct device_domain *domain;
	struct domain_lock *lock;

	domain = find_device_domain(device);

	if (IS_ERR(domain)) {
		dev_err(dev, "Can't find device domain.\n");
		return -EINVAL;
	}

	mutex_lock(&domains_mutex);
	list_for_each_entry(lock, &domain->domain_list, node) {
		if (lock->device == dev) {
			list_del(&lock->node);
			kfree(lock);
			break;
		}
	}

	mutex_unlock(&domains_mutex);

	return 0;
}

void dev_unlock_fix(struct device *device, struct device *dev)
{
	dev_unlock(device, dev);
	exynos_request_apply(1, true, true);
}

unsigned long dev_max_freq(struct device *device)
{
	struct device_domain *domain;
	struct domain_lock *lock;
	unsigned long freq = 0;

	domain = find_device_domain(device);
	if (IS_ERR(domain)) {
		dev_dbg(device, "Can't find device domain.\n");
		return freq;
	}

	mutex_lock(&domains_mutex);
	list_for_each_entry(lock, &domain->domain_list, node)
		if (lock->freq > freq)
			freq = lock->freq;

	mutex_unlock(&domains_mutex);

	return freq;
}

int dev_lock_list(struct device *device, char *buf)
{
	struct device_domain *domain;
	struct domain_lock *lock;
	int count = 0;

	domain = find_device_domain(device);
	if (IS_ERR(domain)) {
		dev_dbg(device, "Can't find device domain.\n");
		return 0;
	}

	mutex_lock(&domains_mutex);
	count = sprintf(buf, "Lock List\n");
	list_for_each_entry(lock, &domain->domain_list, node)
		count += sprintf(buf + count, "%s : %lu\n", dev_name(lock->device), lock->freq);

	mutex_unlock(&domains_mutex);

	return count;
}
