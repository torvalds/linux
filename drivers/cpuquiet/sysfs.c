/*
 * Copyright (c) 2012-2013 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/cpuquiet.h>

#include "cpuquiet.h"

struct cpuquiet_dev {
	unsigned int cpu;
	struct kobject kobj;
};

struct cpuquiet_sysfs_attr {
	struct attribute attr;
	ssize_t (*show)(char *);
	ssize_t (*store)(const char *, size_t count);
};

static struct kobject *cpuquiet_global_kobject;
struct cpuquiet_dev *cpuquiet_cpu_devices[CONFIG_NR_CPUS];

static ssize_t show_current_governor(char *buf)
{
	ssize_t ret;

	mutex_lock(&cpuquiet_lock);

	if (cpuquiet_curr_governor)
		ret = sprintf(buf, "%s\n", cpuquiet_curr_governor->name);
	else
		ret = sprintf(buf, "none\n");

	mutex_unlock(&cpuquiet_lock);

	return ret;

}

static ssize_t store_current_governor(const char *buf, size_t count)
{
	char name[CPUQUIET_NAME_LEN];
	struct cpuquiet_governor *gov;
	int len = count, ret = -EINVAL;

	if (!len || len >= sizeof(name))
		return -EINVAL;

	memcpy(name, buf, count);
	name[len] = '\0';
	if (name[len - 1] == '\n')
		name[--len] = '\0';

	mutex_lock(&cpuquiet_lock);
	gov = cpuquiet_find_governor(name);

	if (gov)
		ret = cpuquiet_switch_governor(gov);
	mutex_unlock(&cpuquiet_lock);

	if (ret)
		return ret;
	else
		return count;
}

static ssize_t available_governors_show(char *buf)
{
	ssize_t ret = 0, len;
	struct cpuquiet_governor *gov;

	mutex_lock(&cpuquiet_lock);
	if (!list_empty(&cpuquiet_governors)) {
		list_for_each_entry(gov, &cpuquiet_governors, governor_list) {
			len = sprintf(buf, "%s ", gov->name);
			buf += len;
			ret += len;
		}
		buf--;
		*buf = '\n';
	} else
		ret = sprintf(buf, "none\n");

	mutex_unlock(&cpuquiet_lock);

	return ret;
}

struct cpuquiet_sysfs_attr attr_current_governor = __ATTR(current_governor,
			0644, show_current_governor, store_current_governor);
struct cpuquiet_sysfs_attr attr_governors = __ATTR_RO(available_governors);


static struct attribute *cpuquiet_default_attrs[] = {
	&attr_current_governor.attr,
	&attr_governors.attr,
	NULL
};

static ssize_t cpuquiet_sysfs_show(struct kobject *kobj,
			struct attribute *attr, char *buf)
{
	struct cpuquiet_sysfs_attr *cattr =
			container_of(attr, struct cpuquiet_sysfs_attr, attr);

	return cattr->show(buf);
}

static ssize_t cpuquiet_sysfs_store(struct kobject *kobj,
			struct attribute *attr, const char *buf, size_t count)
{
	struct cpuquiet_sysfs_attr *cattr =
			container_of(attr, struct cpuquiet_sysfs_attr, attr);

	if (cattr->store)
		return cattr->store(buf, count);

	return -EINVAL;
}

static const struct sysfs_ops cpuquiet_sysfs_ops = {
	.show = cpuquiet_sysfs_show,
	.store = cpuquiet_sysfs_store,
};

static struct kobj_type ktype_cpuquiet_sysfs = {
	.sysfs_ops = &cpuquiet_sysfs_ops,
	.default_attrs = cpuquiet_default_attrs,
};

int cpuquiet_add_group(struct attribute_group *attrs)
{
	return sysfs_create_group(cpuquiet_global_kobject, attrs);
}

void cpuquiet_remove_group(struct attribute_group *attrs)
{
	sysfs_remove_group(cpuquiet_global_kobject, attrs);
}

int cpuquiet_kobject_init(struct kobject *kobj, struct kobj_type *type,
				char *name)
{
	int err;

	err = kobject_init_and_add(kobj, type, cpuquiet_global_kobject, name);
	if (!err)
		kobject_uevent(kobj, KOBJ_ADD);

	return err;
}

int cpuquiet_cpu_kobject_init(struct kobject *kobj, struct kobj_type *type,
				char *name, int cpu)
{
	int err;

	err = kobject_init_and_add(kobj, type, &cpuquiet_cpu_devices[cpu]->kobj,
					name);
	if (!err)
		kobject_uevent(kobj, KOBJ_ADD);

	return err;
}

int cpuquiet_add_interface(struct device *dev)
{
	int err;

	cpuquiet_global_kobject = kzalloc(sizeof(*cpuquiet_global_kobject),
						GFP_KERNEL);
	if (!cpuquiet_global_kobject)
		return -ENOMEM;

	err = kobject_init_and_add(cpuquiet_global_kobject,
			&ktype_cpuquiet_sysfs, &dev->kobj, "cpuquiet");
	if (!err)
		kobject_uevent(cpuquiet_global_kobject, KOBJ_ADD);

	return err;
}


struct cpuquiet_attr {
	struct attribute attr;
	ssize_t (*show)(unsigned int, char *);
	ssize_t (*store)(unsigned int, const char *, size_t count);
};


static ssize_t cpuquiet_state_show(struct kobject *kobj,
	struct attribute *attr, char *buf)
{
	struct cpuquiet_attr *cattr = container_of(attr,
					struct cpuquiet_attr, attr);
	struct cpuquiet_dev *dev = container_of(kobj,
					struct cpuquiet_dev, kobj);

	return cattr->show(dev->cpu, buf);
}

static ssize_t cpuquiet_state_store(struct kobject *kobj,
	struct attribute *attr, const char *buf, size_t count)
{
	struct cpuquiet_attr *cattr = container_of(attr,
					struct cpuquiet_attr, attr);
	struct cpuquiet_dev *dev = container_of(kobj,
					struct cpuquiet_dev, kobj);

	if (cattr->store)
		return cattr->store(dev->cpu, buf, count);

	return -EINVAL;
}

static ssize_t show_active(unsigned int cpu, char *buf)
{
	return sprintf(buf, "%u\n", cpu_online(cpu));
}

static ssize_t store_active(unsigned int cpu, const char *value, size_t count)
{
	unsigned int active;
	int ret;

	if (!cpuquiet_curr_governor->store_active)
		return -EINVAL;

	ret = sscanf(value, "%u", &active);
	if (ret != 1)
		return -EINVAL;

	cpuquiet_curr_governor->store_active(cpu, active);

	return count;
}

struct cpuquiet_attr attr_active = __ATTR(active, 0644, show_active,
						store_active);

static struct attribute *cpuquiet_default_cpu_attrs[] = {
	&attr_active.attr,
	NULL
};

static const struct sysfs_ops cpuquiet_cpu_sysfs_ops = {
	.show = cpuquiet_state_show,
	.store = cpuquiet_state_store,
};

static struct kobj_type ktype_cpuquiet = {
	.sysfs_ops = &cpuquiet_cpu_sysfs_ops,
	.default_attrs = cpuquiet_default_cpu_attrs,
};

void cpuquiet_add_dev(struct device *device, unsigned int cpu)
{
	struct cpuquiet_dev *dev;
	int err;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	dev->cpu = cpu;
	cpuquiet_cpu_devices[cpu] = dev;
	err = kobject_init_and_add(&dev->kobj, &ktype_cpuquiet,
				&device->kobj, "cpuquiet");
	if (!err)
		kobject_uevent(&dev->kobj, KOBJ_ADD);
}

void cpuquiet_remove_dev(unsigned int cpu)
{
	if (cpu < CONFIG_NR_CPUS && cpuquiet_cpu_devices[cpu])
		kobject_put(&cpuquiet_cpu_devices[cpu]->kobj);
}
