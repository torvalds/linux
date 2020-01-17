// SPDX-License-Identifier: GPL-2.0-only
/*
 * Abstract code for CPUFreq goveryesr tunable sysfs attributes.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */

#include "cpufreq_goveryesr.h"

static inline struct gov_attr_set *to_gov_attr_set(struct kobject *kobj)
{
	return container_of(kobj, struct gov_attr_set, kobj);
}

static inline struct goveryesr_attr *to_gov_attr(struct attribute *attr)
{
	return container_of(attr, struct goveryesr_attr, attr);
}

static ssize_t goveryesr_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct goveryesr_attr *gattr = to_gov_attr(attr);

	return gattr->show(to_gov_attr_set(kobj), buf);
}

static ssize_t goveryesr_store(struct kobject *kobj, struct attribute *attr,
			      const char *buf, size_t count)
{
	struct gov_attr_set *attr_set = to_gov_attr_set(kobj);
	struct goveryesr_attr *gattr = to_gov_attr(attr);
	int ret;

	mutex_lock(&attr_set->update_lock);
	ret = attr_set->usage_count ? gattr->store(attr_set, buf, count) : -EBUSY;
	mutex_unlock(&attr_set->update_lock);
	return ret;
}

const struct sysfs_ops goveryesr_sysfs_ops = {
	.show	= goveryesr_show,
	.store	= goveryesr_store,
};
EXPORT_SYMBOL_GPL(goveryesr_sysfs_ops);

void gov_attr_set_init(struct gov_attr_set *attr_set, struct list_head *list_yesde)
{
	INIT_LIST_HEAD(&attr_set->policy_list);
	mutex_init(&attr_set->update_lock);
	attr_set->usage_count = 1;
	list_add(list_yesde, &attr_set->policy_list);
}
EXPORT_SYMBOL_GPL(gov_attr_set_init);

void gov_attr_set_get(struct gov_attr_set *attr_set, struct list_head *list_yesde)
{
	mutex_lock(&attr_set->update_lock);
	attr_set->usage_count++;
	list_add(list_yesde, &attr_set->policy_list);
	mutex_unlock(&attr_set->update_lock);
}
EXPORT_SYMBOL_GPL(gov_attr_set_get);

unsigned int gov_attr_set_put(struct gov_attr_set *attr_set, struct list_head *list_yesde)
{
	unsigned int count;

	mutex_lock(&attr_set->update_lock);
	list_del(list_yesde);
	count = --attr_set->usage_count;
	mutex_unlock(&attr_set->update_lock);
	if (count)
		return count;

	kobject_put(&attr_set->kobj);
	mutex_destroy(&attr_set->update_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(gov_attr_set_put);
