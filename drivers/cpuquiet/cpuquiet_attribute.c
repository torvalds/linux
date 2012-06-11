/*
 * Copyright (c) 2012 NVIDIA CORPORATION.  All rights reserved.
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
 */

#include <linux/cpuquiet.h>

ssize_t show_int_attribute(struct cpuquiet_attribute *cattr, char *buf)
{
	return sprintf(buf, "%d\n", *((int *)cattr->param));
}

ssize_t store_int_attribute(struct cpuquiet_attribute *cattr,
					const char *buf, size_t count)
{
	int err, val;

	err = kstrtoint(buf, 0, &val);
	if (err < 0)
		return err;

	*((int *)(cattr->param)) = val;

	if (cattr->store_callback)
		cattr->store_callback(cattr);

	return count;
}

ssize_t show_bool_attribute(struct cpuquiet_attribute *cattr, char *buf)
{
	return sprintf(buf, "%d\n", *((bool *)cattr->param));
}

ssize_t store_bool_attribute(struct cpuquiet_attribute *cattr,
					const char *buf, size_t count)
{
	int err, val;

	err = kstrtoint(buf, 0, &val);
	if (err < 0)
		return err;

	if (val < 0 || val > 1)
		return -EINVAL;

	*((bool *)(cattr->param)) = val;

	if (cattr->store_callback)
		cattr->store_callback(cattr);

	return count;
}

ssize_t show_uint_attribute(struct cpuquiet_attribute *cattr, char *buf)
{
	return sprintf(buf, "%u\n", *((unsigned int *)cattr->param));
}

ssize_t store_uint_attribute(struct cpuquiet_attribute *cattr,
					const char *buf, size_t count)
{
	int err;
	unsigned int val;

	err = kstrtouint(buf, 0, &val);
	if (err < 0)
		return err;

	*((unsigned int *)(cattr->param)) = val;

	if (cattr->store_callback)
		cattr->store_callback(cattr);

	return count;
}

ssize_t store_ulong_attribute(struct cpuquiet_attribute *cattr,
					const char *buf, size_t count)
{
	int err;
	unsigned long val;

	err = kstrtoul(buf, 0, &val);
	if (err < 0)
		return err;

	*((unsigned long *)(cattr->param)) = val;

	if (cattr->store_callback)
		cattr->store_callback(cattr);

	return count;
}

ssize_t show_ulong_attribute(struct cpuquiet_attribute *cattr,
					char *buf)
{
	return sprintf(buf, "%lu\n", *((unsigned long *)cattr->param));
}

ssize_t cpuquiet_auto_sysfs_store(struct kobject *kobj,
	struct attribute *attr, const char *buf, size_t count)
{
	struct cpuquiet_attribute *cattr =
		container_of(attr, struct cpuquiet_attribute, attr);

	if (cattr->store)
		return cattr->store(cattr, buf, count);

	return -EINVAL;
}

ssize_t cpuquiet_auto_sysfs_show(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	struct cpuquiet_attribute *cattr =
		container_of(attr, struct cpuquiet_attribute, attr);

	return cattr->show(cattr, buf);
}
