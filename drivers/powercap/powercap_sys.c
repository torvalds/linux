/*
 * Power capping class
 * Copyright (c) 2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/powercap.h>

#define to_powercap_zone(n) container_of(n, struct powercap_zone, dev)
#define to_powercap_control_type(n) \
			container_of(n, struct powercap_control_type, dev)

/* Power zone show function */
#define define_power_zone_show(_attr)		\
static ssize_t _attr##_show(struct device *dev, \
					struct device_attribute *dev_attr,\
					char *buf) \
{ \
	u64 value; \
	ssize_t len = -EINVAL; \
	struct powercap_zone *power_zone = to_powercap_zone(dev); \
	\
	if (power_zone->ops->get_##_attr) { \
		if (!power_zone->ops->get_##_attr(power_zone, &value)) \
			len = sprintf(buf, "%lld\n", value); \
	} \
	\
	return len; \
}

/* The only meaningful input is 0 (reset), others are silently ignored */
#define define_power_zone_store(_attr)		\
static ssize_t _attr##_store(struct device *dev,\
				struct device_attribute *dev_attr, \
				const char *buf, size_t count) \
{ \
	int err; \
	struct powercap_zone *power_zone = to_powercap_zone(dev); \
	u64 value; \
	\
	err = kstrtoull(buf, 10, &value); \
	if (err) \
		return -EINVAL; \
	if (value) \
		return count; \
	if (power_zone->ops->reset_##_attr) { \
		if (!power_zone->ops->reset_##_attr(power_zone)) \
			return count; \
	} \
	\
	return -EINVAL; \
}

/* Power zone constraint show function */
#define define_power_zone_constraint_show(_attr) \
static ssize_t show_constraint_##_attr(struct device *dev, \
				struct device_attribute *dev_attr,\
				char *buf) \
{ \
	u64 value; \
	ssize_t len = -ENODATA; \
	struct powercap_zone *power_zone = to_powercap_zone(dev); \
	int id; \
	struct powercap_zone_constraint *pconst;\
	\
	if (!sscanf(dev_attr->attr.name, "constraint_%d_", &id)) \
		return -EINVAL; \
	if (id >= power_zone->const_id_cnt)	\
		return -EINVAL; \
	pconst = &power_zone->constraints[id]; \
	if (pconst && pconst->ops && pconst->ops->get_##_attr) { \
		if (!pconst->ops->get_##_attr(power_zone, id, &value)) \
			len = sprintf(buf, "%lld\n", value); \
	} \
	\
	return len; \
}

/* Power zone constraint store function */
#define define_power_zone_constraint_store(_attr) \
static ssize_t store_constraint_##_attr(struct device *dev,\
				struct device_attribute *dev_attr, \
				const char *buf, size_t count) \
{ \
	int err; \
	u64 value; \
	struct powercap_zone *power_zone = to_powercap_zone(dev); \
	int id; \
	struct powercap_zone_constraint *pconst;\
	\
	if (!sscanf(dev_attr->attr.name, "constraint_%d_", &id)) \
		return -EINVAL; \
	if (id >= power_zone->const_id_cnt)	\
		return -EINVAL; \
	pconst = &power_zone->constraints[id]; \
	err = kstrtoull(buf, 10, &value); \
	if (err) \
		return -EINVAL; \
	if (pconst && pconst->ops && pconst->ops->set_##_attr) { \
		if (!pconst->ops->set_##_attr(power_zone, id, value)) \
			return count; \
	} \
	\
	return -ENODATA; \
}

/* Power zone information callbacks */
define_power_zone_show(power_uw);
define_power_zone_show(max_power_range_uw);
define_power_zone_show(energy_uj);
define_power_zone_store(energy_uj);
define_power_zone_show(max_energy_range_uj);

/* Power zone attributes */
static DEVICE_ATTR_RO(max_power_range_uw);
static DEVICE_ATTR_RO(power_uw);
static DEVICE_ATTR_RO(max_energy_range_uj);
static DEVICE_ATTR_RW(energy_uj);

/* Power zone constraint attributes callbacks */
define_power_zone_constraint_show(power_limit_uw);
define_power_zone_constraint_store(power_limit_uw);
define_power_zone_constraint_show(time_window_us);
define_power_zone_constraint_store(time_window_us);
define_power_zone_constraint_show(max_power_uw);
define_power_zone_constraint_show(min_power_uw);
define_power_zone_constraint_show(max_time_window_us);
define_power_zone_constraint_show(min_time_window_us);

/* For one time seeding of constraint device attributes */
struct powercap_constraint_attr {
	struct device_attribute power_limit_attr;
	struct device_attribute time_window_attr;
	struct device_attribute max_power_attr;
	struct device_attribute min_power_attr;
	struct device_attribute max_time_window_attr;
	struct device_attribute min_time_window_attr;
	struct device_attribute name_attr;
};

static struct powercap_constraint_attr
				constraint_attrs[MAX_CONSTRAINTS_PER_ZONE];

/* A list of powercap control_types */
static LIST_HEAD(powercap_cntrl_list);
/* Mutex to protect list of powercap control_types */
static DEFINE_MUTEX(powercap_cntrl_list_lock);

#define POWERCAP_CONSTRAINT_NAME_LEN	30 /* Some limit to avoid overflow */
static ssize_t show_constraint_name(struct device *dev,
				struct device_attribute *dev_attr,
				char *buf)
{
	const char *name;
	struct powercap_zone *power_zone = to_powercap_zone(dev);
	int id;
	ssize_t len = -ENODATA;
	struct powercap_zone_constraint *pconst;

	if (!sscanf(dev_attr->attr.name, "constraint_%d_", &id))
		return -EINVAL;
	if (id >= power_zone->const_id_cnt)
		return -EINVAL;
	pconst = &power_zone->constraints[id];

	if (pconst && pconst->ops && pconst->ops->get_name) {
		name = pconst->ops->get_name(power_zone, id);
		if (name) {
			snprintf(buf, POWERCAP_CONSTRAINT_NAME_LEN,
								"%s\n", name);
			buf[POWERCAP_CONSTRAINT_NAME_LEN] = '\0';
			len = strlen(buf);
		}
	}

	return len;
}

static int create_constraint_attribute(int id, const char *name,
				int mode,
				struct device_attribute *dev_attr,
				ssize_t (*show)(struct device *,
					struct device_attribute *, char *),
				ssize_t (*store)(struct device *,
					struct device_attribute *,
				const char *, size_t)
				)
{

	dev_attr->attr.name = kasprintf(GFP_KERNEL, "constraint_%d_%s",
								id, name);
	if (!dev_attr->attr.name)
		return -ENOMEM;
	dev_attr->attr.mode = mode;
	dev_attr->show = show;
	dev_attr->store = store;

	return 0;
}

static void free_constraint_attributes(void)
{
	int i;

	for (i = 0; i < MAX_CONSTRAINTS_PER_ZONE; ++i) {
		kfree(constraint_attrs[i].power_limit_attr.attr.name);
		kfree(constraint_attrs[i].time_window_attr.attr.name);
		kfree(constraint_attrs[i].name_attr.attr.name);
		kfree(constraint_attrs[i].max_power_attr.attr.name);
		kfree(constraint_attrs[i].min_power_attr.attr.name);
		kfree(constraint_attrs[i].max_time_window_attr.attr.name);
		kfree(constraint_attrs[i].min_time_window_attr.attr.name);
	}
}

static int seed_constraint_attributes(void)
{
	int i;
	int ret;

	for (i = 0; i < MAX_CONSTRAINTS_PER_ZONE; ++i) {
		ret = create_constraint_attribute(i, "power_limit_uw",
					S_IWUSR | S_IRUGO,
					&constraint_attrs[i].power_limit_attr,
					show_constraint_power_limit_uw,
					store_constraint_power_limit_uw);
		if (ret)
			goto err_alloc;
		ret = create_constraint_attribute(i, "time_window_us",
					S_IWUSR | S_IRUGO,
					&constraint_attrs[i].time_window_attr,
					show_constraint_time_window_us,
					store_constraint_time_window_us);
		if (ret)
			goto err_alloc;
		ret = create_constraint_attribute(i, "name", S_IRUGO,
				&constraint_attrs[i].name_attr,
				show_constraint_name,
				NULL);
		if (ret)
			goto err_alloc;
		ret = create_constraint_attribute(i, "max_power_uw", S_IRUGO,
				&constraint_attrs[i].max_power_attr,
				show_constraint_max_power_uw,
				NULL);
		if (ret)
			goto err_alloc;
		ret = create_constraint_attribute(i, "min_power_uw", S_IRUGO,
				&constraint_attrs[i].min_power_attr,
				show_constraint_min_power_uw,
				NULL);
		if (ret)
			goto err_alloc;
		ret = create_constraint_attribute(i, "max_time_window_us",
				S_IRUGO,
				&constraint_attrs[i].max_time_window_attr,
				show_constraint_max_time_window_us,
				NULL);
		if (ret)
			goto err_alloc;
		ret = create_constraint_attribute(i, "min_time_window_us",
				S_IRUGO,
				&constraint_attrs[i].min_time_window_attr,
				show_constraint_min_time_window_us,
				NULL);
		if (ret)
			goto err_alloc;

	}

	return 0;

err_alloc:
	free_constraint_attributes();

	return ret;
}

static int create_constraints(struct powercap_zone *power_zone,
			int nr_constraints,
			const struct powercap_zone_constraint_ops *const_ops)
{
	int i;
	int ret = 0;
	int count;
	struct powercap_zone_constraint *pconst;

	if (!power_zone || !const_ops || !const_ops->get_power_limit_uw ||
					!const_ops->set_power_limit_uw ||
					!const_ops->get_time_window_us ||
					!const_ops->set_time_window_us)
		return -EINVAL;

	count = power_zone->zone_attr_count;
	for (i = 0; i < nr_constraints; ++i) {
		pconst = &power_zone->constraints[i];
		pconst->ops = const_ops;
		pconst->id = power_zone->const_id_cnt;
		power_zone->const_id_cnt++;
		power_zone->zone_dev_attrs[count++] =
				&constraint_attrs[i].power_limit_attr.attr;
		power_zone->zone_dev_attrs[count++] =
				&constraint_attrs[i].time_window_attr.attr;
		if (pconst->ops->get_name)
			power_zone->zone_dev_attrs[count++] =
				&constraint_attrs[i].name_attr.attr;
		if (pconst->ops->get_max_power_uw)
			power_zone->zone_dev_attrs[count++] =
				&constraint_attrs[i].max_power_attr.attr;
		if (pconst->ops->get_min_power_uw)
			power_zone->zone_dev_attrs[count++] =
				&constraint_attrs[i].min_power_attr.attr;
		if (pconst->ops->get_max_time_window_us)
			power_zone->zone_dev_attrs[count++] =
				&constraint_attrs[i].max_time_window_attr.attr;
		if (pconst->ops->get_min_time_window_us)
			power_zone->zone_dev_attrs[count++] =
				&constraint_attrs[i].min_time_window_attr.attr;
	}
	power_zone->zone_attr_count = count;

	return ret;
}

static bool control_type_valid(void *control_type)
{
	struct powercap_control_type *pos = NULL;
	bool found = false;

	mutex_lock(&powercap_cntrl_list_lock);

	list_for_each_entry(pos, &powercap_cntrl_list, node) {
		if (pos == control_type) {
			found = true;
			break;
		}
	}
	mutex_unlock(&powercap_cntrl_list_lock);

	return found;
}

static ssize_t name_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct powercap_zone *power_zone = to_powercap_zone(dev);

	return sprintf(buf, "%s\n", power_zone->name);
}

static DEVICE_ATTR_RO(name);

/* Create zone and attributes in sysfs */
static void create_power_zone_common_attributes(
					struct powercap_zone *power_zone)
{
	int count = 0;

	power_zone->zone_dev_attrs[count++] = &dev_attr_name.attr;
	if (power_zone->ops->get_max_energy_range_uj)
		power_zone->zone_dev_attrs[count++] =
					&dev_attr_max_energy_range_uj.attr;
	if (power_zone->ops->get_energy_uj) {
		if (power_zone->ops->reset_energy_uj)
			dev_attr_energy_uj.attr.mode = S_IWUSR | S_IRUGO;
		else
			dev_attr_energy_uj.attr.mode = S_IRUGO;
		power_zone->zone_dev_attrs[count++] =
					&dev_attr_energy_uj.attr;
	}
	if (power_zone->ops->get_power_uw)
		power_zone->zone_dev_attrs[count++] =
					&dev_attr_power_uw.attr;
	if (power_zone->ops->get_max_power_range_uw)
		power_zone->zone_dev_attrs[count++] =
					&dev_attr_max_power_range_uw.attr;
	power_zone->zone_dev_attrs[count] = NULL;
	power_zone->zone_attr_count = count;
}

static void powercap_release(struct device *dev)
{
	bool allocated;

	if (dev->parent) {
		struct powercap_zone *power_zone = to_powercap_zone(dev);

		/* Store flag as the release() may free memory */
		allocated = power_zone->allocated;
		/* Remove id from parent idr struct */
		idr_remove(power_zone->parent_idr, power_zone->id);
		/* Destroy idrs allocated for this zone */
		idr_destroy(&power_zone->idr);
		kfree(power_zone->name);
		kfree(power_zone->zone_dev_attrs);
		kfree(power_zone->constraints);
		if (power_zone->ops->release)
			power_zone->ops->release(power_zone);
		if (allocated)
			kfree(power_zone);
	} else {
		struct powercap_control_type *control_type =
						to_powercap_control_type(dev);

		/* Store flag as the release() may free memory */
		allocated = control_type->allocated;
		idr_destroy(&control_type->idr);
		mutex_destroy(&control_type->lock);
		if (control_type->ops && control_type->ops->release)
			control_type->ops->release(control_type);
		if (allocated)
			kfree(control_type);
	}
}

static ssize_t enabled_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	bool mode = true;

	/* Default is enabled */
	if (dev->parent) {
		struct powercap_zone *power_zone = to_powercap_zone(dev);
		if (power_zone->ops->get_enable)
			if (power_zone->ops->get_enable(power_zone, &mode))
				mode = false;
	} else {
		struct powercap_control_type *control_type =
						to_powercap_control_type(dev);
		if (control_type->ops && control_type->ops->get_enable)
			if (control_type->ops->get_enable(control_type, &mode))
				mode = false;
	}

	return sprintf(buf, "%d\n", mode);
}

static ssize_t enabled_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf,  size_t len)
{
	bool mode;

	if (strtobool(buf, &mode))
		return -EINVAL;
	if (dev->parent) {
		struct powercap_zone *power_zone = to_powercap_zone(dev);
		if (power_zone->ops->set_enable)
			if (!power_zone->ops->set_enable(power_zone, mode))
				return len;
	} else {
		struct powercap_control_type *control_type =
						to_powercap_control_type(dev);
		if (control_type->ops && control_type->ops->set_enable)
			if (!control_type->ops->set_enable(control_type, mode))
				return len;
	}

	return -ENOSYS;
}

static DEVICE_ATTR_RW(enabled);

static struct attribute *powercap_attrs[] = {
	&dev_attr_enabled.attr,
	NULL,
};
ATTRIBUTE_GROUPS(powercap);

static struct class powercap_class = {
	.name = "powercap",
	.dev_release = powercap_release,
	.dev_groups = powercap_groups,
};

struct powercap_zone *powercap_register_zone(
			struct powercap_zone *power_zone,
			struct powercap_control_type *control_type,
			const char *name,
			struct powercap_zone *parent,
			const struct powercap_zone_ops *ops,
			int nr_constraints,
			const struct powercap_zone_constraint_ops *const_ops)
{
	int result;
	int nr_attrs;

	if (!name || !control_type || !ops ||
			nr_constraints > MAX_CONSTRAINTS_PER_ZONE ||
			(!ops->get_energy_uj && !ops->get_power_uw) ||
			!control_type_valid(control_type))
		return ERR_PTR(-EINVAL);

	if (power_zone) {
		if (!ops->release)
			return ERR_PTR(-EINVAL);
		memset(power_zone, 0, sizeof(*power_zone));
	} else {
		power_zone = kzalloc(sizeof(*power_zone), GFP_KERNEL);
		if (!power_zone)
			return ERR_PTR(-ENOMEM);
		power_zone->allocated = true;
	}
	power_zone->ops = ops;
	power_zone->control_type_inst = control_type;
	if (!parent) {
		power_zone->dev.parent = &control_type->dev;
		power_zone->parent_idr = &control_type->idr;
	} else {
		power_zone->dev.parent = &parent->dev;
		power_zone->parent_idr = &parent->idr;
	}
	power_zone->dev.class = &powercap_class;

	mutex_lock(&control_type->lock);
	/* Using idr to get the unique id */
	result = idr_alloc(power_zone->parent_idr, NULL, 0, 0, GFP_KERNEL);
	if (result < 0)
		goto err_idr_alloc;

	power_zone->id = result;
	idr_init(&power_zone->idr);
	power_zone->name = kstrdup(name, GFP_KERNEL);
	if (!power_zone->name)
		goto err_name_alloc;
	dev_set_name(&power_zone->dev, "%s:%x",
					dev_name(power_zone->dev.parent),
					power_zone->id);
	power_zone->constraints = kzalloc(sizeof(*power_zone->constraints) *
					 nr_constraints, GFP_KERNEL);
	if (!power_zone->constraints)
		goto err_const_alloc;

	nr_attrs = nr_constraints * POWERCAP_CONSTRAINTS_ATTRS +
						POWERCAP_ZONE_MAX_ATTRS + 1;
	power_zone->zone_dev_attrs = kzalloc(sizeof(void *) *
						nr_attrs, GFP_KERNEL);
	if (!power_zone->zone_dev_attrs)
		goto err_attr_alloc;
	create_power_zone_common_attributes(power_zone);
	result = create_constraints(power_zone, nr_constraints, const_ops);
	if (result)
		goto err_dev_ret;

	power_zone->zone_dev_attrs[power_zone->zone_attr_count] = NULL;
	power_zone->dev_zone_attr_group.attrs = power_zone->zone_dev_attrs;
	power_zone->dev_attr_groups[0] = &power_zone->dev_zone_attr_group;
	power_zone->dev_attr_groups[1] = NULL;
	power_zone->dev.groups = power_zone->dev_attr_groups;
	result = device_register(&power_zone->dev);
	if (result)
		goto err_dev_ret;

	control_type->nr_zones++;
	mutex_unlock(&control_type->lock);

	return power_zone;

err_dev_ret:
	kfree(power_zone->zone_dev_attrs);
err_attr_alloc:
	kfree(power_zone->constraints);
err_const_alloc:
	kfree(power_zone->name);
err_name_alloc:
	idr_remove(power_zone->parent_idr, power_zone->id);
err_idr_alloc:
	if (power_zone->allocated)
		kfree(power_zone);
	mutex_unlock(&control_type->lock);

	return ERR_PTR(result);
}
EXPORT_SYMBOL_GPL(powercap_register_zone);

int powercap_unregister_zone(struct powercap_control_type *control_type,
				struct powercap_zone *power_zone)
{
	if (!power_zone || !control_type)
		return -EINVAL;

	mutex_lock(&control_type->lock);
	control_type->nr_zones--;
	mutex_unlock(&control_type->lock);

	device_unregister(&power_zone->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(powercap_unregister_zone);

struct powercap_control_type *powercap_register_control_type(
				struct powercap_control_type *control_type,
				const char *name,
				const struct powercap_control_type_ops *ops)
{
	int result;

	if (!name)
		return ERR_PTR(-EINVAL);
	if (control_type) {
		if (!ops || !ops->release)
			return ERR_PTR(-EINVAL);
		memset(control_type, 0, sizeof(*control_type));
	} else {
		control_type = kzalloc(sizeof(*control_type), GFP_KERNEL);
		if (!control_type)
			return ERR_PTR(-ENOMEM);
		control_type->allocated = true;
	}
	mutex_init(&control_type->lock);
	control_type->ops = ops;
	INIT_LIST_HEAD(&control_type->node);
	control_type->dev.class = &powercap_class;
	dev_set_name(&control_type->dev, "%s", name);
	result = device_register(&control_type->dev);
	if (result) {
		if (control_type->allocated)
			kfree(control_type);
		return ERR_PTR(result);
	}
	idr_init(&control_type->idr);

	mutex_lock(&powercap_cntrl_list_lock);
	list_add_tail(&control_type->node, &powercap_cntrl_list);
	mutex_unlock(&powercap_cntrl_list_lock);

	return control_type;
}
EXPORT_SYMBOL_GPL(powercap_register_control_type);

int powercap_unregister_control_type(struct powercap_control_type *control_type)
{
	struct powercap_control_type *pos = NULL;

	if (control_type->nr_zones) {
		dev_err(&control_type->dev, "Zones of this type still not freed\n");
		return -EINVAL;
	}
	mutex_lock(&powercap_cntrl_list_lock);
	list_for_each_entry(pos, &powercap_cntrl_list, node) {
		if (pos == control_type) {
			list_del(&control_type->node);
			mutex_unlock(&powercap_cntrl_list_lock);
			device_unregister(&control_type->dev);
			return 0;
		}
	}
	mutex_unlock(&powercap_cntrl_list_lock);

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(powercap_unregister_control_type);

static int __init powercap_init(void)
{
	int result = 0;

	result = seed_constraint_attributes();
	if (result)
		return result;

	result = class_register(&powercap_class);

	return result;
}

device_initcall(powercap_init);

MODULE_DESCRIPTION("PowerCap sysfs Driver");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
