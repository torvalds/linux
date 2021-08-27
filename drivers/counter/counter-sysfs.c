// SPDX-License-Identifier: GPL-2.0
/*
 * Generic Counter sysfs interface
 * Copyright (C) 2020 William Breathitt Gray
 */
#include <linux/counter.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include "counter-sysfs.h"

/**
 * struct counter_attribute - Counter sysfs attribute
 * @dev_attr:	device attribute for sysfs
 * @l:		node to add Counter attribute to attribute group list
 * @comp:	Counter component callbacks and data
 * @scope:	Counter scope of the attribute
 * @parent:	pointer to the parent component
 */
struct counter_attribute {
	struct device_attribute dev_attr;
	struct list_head l;

	struct counter_comp comp;
	enum counter_scope scope;
	void *parent;
};

#define to_counter_attribute(_dev_attr) \
	container_of(_dev_attr, struct counter_attribute, dev_attr)

/**
 * struct counter_attribute_group - container for attribute group
 * @name:	name of the attribute group
 * @attr_list:	list to keep track of created attributes
 * @num_attr:	number of attributes
 */
struct counter_attribute_group {
	const char *name;
	struct list_head attr_list;
	size_t num_attr;
};

static const char *const counter_function_str[] = {
	[COUNTER_FUNCTION_INCREASE] = "increase",
	[COUNTER_FUNCTION_DECREASE] = "decrease",
	[COUNTER_FUNCTION_PULSE_DIRECTION] = "pulse-direction",
	[COUNTER_FUNCTION_QUADRATURE_X1_A] = "quadrature x1 a",
	[COUNTER_FUNCTION_QUADRATURE_X1_B] = "quadrature x1 b",
	[COUNTER_FUNCTION_QUADRATURE_X2_A] = "quadrature x2 a",
	[COUNTER_FUNCTION_QUADRATURE_X2_B] = "quadrature x2 b",
	[COUNTER_FUNCTION_QUADRATURE_X4] = "quadrature x4"
};

static const char *const counter_signal_value_str[] = {
	[COUNTER_SIGNAL_LEVEL_LOW] = "low",
	[COUNTER_SIGNAL_LEVEL_HIGH] = "high"
};

static const char *const counter_synapse_action_str[] = {
	[COUNTER_SYNAPSE_ACTION_NONE] = "none",
	[COUNTER_SYNAPSE_ACTION_RISING_EDGE] = "rising edge",
	[COUNTER_SYNAPSE_ACTION_FALLING_EDGE] = "falling edge",
	[COUNTER_SYNAPSE_ACTION_BOTH_EDGES] = "both edges"
};

static const char *const counter_count_direction_str[] = {
	[COUNTER_COUNT_DIRECTION_FORWARD] = "forward",
	[COUNTER_COUNT_DIRECTION_BACKWARD] = "backward"
};

static const char *const counter_count_mode_str[] = {
	[COUNTER_COUNT_MODE_NORMAL] = "normal",
	[COUNTER_COUNT_MODE_RANGE_LIMIT] = "range limit",
	[COUNTER_COUNT_MODE_NON_RECYCLE] = "non-recycle",
	[COUNTER_COUNT_MODE_MODULO_N] = "modulo-n"
};

static ssize_t counter_comp_u8_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	const struct counter_attribute *const a = to_counter_attribute(attr);
	struct counter_device *const counter = dev_get_drvdata(dev);
	int err;
	u8 data = 0;

	switch (a->scope) {
	case COUNTER_SCOPE_DEVICE:
		err = a->comp.device_u8_read(counter, &data);
		break;
	case COUNTER_SCOPE_SIGNAL:
		err = a->comp.signal_u8_read(counter, a->parent, &data);
		break;
	case COUNTER_SCOPE_COUNT:
		err = a->comp.count_u8_read(counter, a->parent, &data);
		break;
	default:
		return -EINVAL;
	}
	if (err < 0)
		return err;

	if (a->comp.type == COUNTER_COMP_BOOL)
		/* data should already be boolean but ensure just to be safe */
		data = !!data;

	return sprintf(buf, "%u\n", (unsigned int)data);
}

static ssize_t counter_comp_u8_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	const struct counter_attribute *const a = to_counter_attribute(attr);
	struct counter_device *const counter = dev_get_drvdata(dev);
	int err;
	bool bool_data = 0;
	u8 data = 0;

	if (a->comp.type == COUNTER_COMP_BOOL) {
		err = kstrtobool(buf, &bool_data);
		data = bool_data;
	} else
		err = kstrtou8(buf, 0, &data);
	if (err < 0)
		return err;

	switch (a->scope) {
	case COUNTER_SCOPE_DEVICE:
		err = a->comp.device_u8_write(counter, data);
		break;
	case COUNTER_SCOPE_SIGNAL:
		err = a->comp.signal_u8_write(counter, a->parent, data);
		break;
	case COUNTER_SCOPE_COUNT:
		err = a->comp.count_u8_write(counter, a->parent, data);
		break;
	default:
		return -EINVAL;
	}
	if (err < 0)
		return err;

	return len;
}

static ssize_t counter_comp_u32_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	const struct counter_attribute *const a = to_counter_attribute(attr);
	struct counter_device *const counter = dev_get_drvdata(dev);
	const struct counter_available *const avail = a->comp.priv;
	int err;
	u32 data = 0;

	switch (a->scope) {
	case COUNTER_SCOPE_DEVICE:
		err = a->comp.device_u32_read(counter, &data);
		break;
	case COUNTER_SCOPE_SIGNAL:
		err = a->comp.signal_u32_read(counter, a->parent, &data);
		break;
	case COUNTER_SCOPE_COUNT:
		if (a->comp.type == COUNTER_COMP_SYNAPSE_ACTION)
			err = a->comp.action_read(counter, a->parent,
						  a->comp.priv, &data);
		else
			err = a->comp.count_u32_read(counter, a->parent, &data);
		break;
	default:
		return -EINVAL;
	}
	if (err < 0)
		return err;

	switch (a->comp.type) {
	case COUNTER_COMP_FUNCTION:
		return sysfs_emit(buf, "%s\n", counter_function_str[data]);
	case COUNTER_COMP_SIGNAL_LEVEL:
		return sysfs_emit(buf, "%s\n", counter_signal_value_str[data]);
	case COUNTER_COMP_SYNAPSE_ACTION:
		return sysfs_emit(buf, "%s\n", counter_synapse_action_str[data]);
	case COUNTER_COMP_ENUM:
		return sysfs_emit(buf, "%s\n", avail->strs[data]);
	case COUNTER_COMP_COUNT_DIRECTION:
		return sysfs_emit(buf, "%s\n", counter_count_direction_str[data]);
	case COUNTER_COMP_COUNT_MODE:
		return sysfs_emit(buf, "%s\n", counter_count_mode_str[data]);
	default:
		return sprintf(buf, "%u\n", (unsigned int)data);
	}
}

static int counter_find_enum(u32 *const enum_item, const u32 *const enums,
			     const size_t num_enums, const char *const buf,
			     const char *const string_array[])
{
	size_t index;

	for (index = 0; index < num_enums; index++) {
		*enum_item = enums[index];
		if (sysfs_streq(buf, string_array[*enum_item]))
			return 0;
	}

	return -EINVAL;
}

static ssize_t counter_comp_u32_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len)
{
	const struct counter_attribute *const a = to_counter_attribute(attr);
	struct counter_device *const counter = dev_get_drvdata(dev);
	struct counter_count *const count = a->parent;
	struct counter_synapse *const synapse = a->comp.priv;
	const struct counter_available *const avail = a->comp.priv;
	int err;
	u32 data = 0;

	switch (a->comp.type) {
	case COUNTER_COMP_FUNCTION:
		err = counter_find_enum(&data, count->functions_list,
					count->num_functions, buf,
					counter_function_str);
		break;
	case COUNTER_COMP_SYNAPSE_ACTION:
		err = counter_find_enum(&data, synapse->actions_list,
					synapse->num_actions, buf,
					counter_synapse_action_str);
		break;
	case COUNTER_COMP_ENUM:
		err = __sysfs_match_string(avail->strs, avail->num_items, buf);
		data = err;
		break;
	case COUNTER_COMP_COUNT_MODE:
		err = counter_find_enum(&data, avail->enums, avail->num_items,
					buf, counter_count_mode_str);
		break;
	default:
		err = kstrtou32(buf, 0, &data);
		break;
	}
	if (err < 0)
		return err;

	switch (a->scope) {
	case COUNTER_SCOPE_DEVICE:
		err = a->comp.device_u32_write(counter, data);
		break;
	case COUNTER_SCOPE_SIGNAL:
		err = a->comp.signal_u32_write(counter, a->parent, data);
		break;
	case COUNTER_SCOPE_COUNT:
		if (a->comp.type == COUNTER_COMP_SYNAPSE_ACTION)
			err = a->comp.action_write(counter, count, synapse,
						   data);
		else
			err = a->comp.count_u32_write(counter, count, data);
		break;
	default:
		return -EINVAL;
	}
	if (err < 0)
		return err;

	return len;
}

static ssize_t counter_comp_u64_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	const struct counter_attribute *const a = to_counter_attribute(attr);
	struct counter_device *const counter = dev_get_drvdata(dev);
	int err;
	u64 data = 0;

	switch (a->scope) {
	case COUNTER_SCOPE_DEVICE:
		err = a->comp.device_u64_read(counter, &data);
		break;
	case COUNTER_SCOPE_SIGNAL:
		err = a->comp.signal_u64_read(counter, a->parent, &data);
		break;
	case COUNTER_SCOPE_COUNT:
		err = a->comp.count_u64_read(counter, a->parent, &data);
		break;
	default:
		return -EINVAL;
	}
	if (err < 0)
		return err;

	return sprintf(buf, "%llu\n", (unsigned long long)data);
}

static ssize_t counter_comp_u64_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len)
{
	const struct counter_attribute *const a = to_counter_attribute(attr);
	struct counter_device *const counter = dev_get_drvdata(dev);
	int err;
	u64 data = 0;

	err = kstrtou64(buf, 0, &data);
	if (err < 0)
		return err;

	switch (a->scope) {
	case COUNTER_SCOPE_DEVICE:
		err = a->comp.device_u64_write(counter, data);
		break;
	case COUNTER_SCOPE_SIGNAL:
		err = a->comp.signal_u64_write(counter, a->parent, data);
		break;
	case COUNTER_SCOPE_COUNT:
		err = a->comp.count_u64_write(counter, a->parent, data);
		break;
	default:
		return -EINVAL;
	}
	if (err < 0)
		return err;

	return len;
}

static ssize_t enums_available_show(const u32 *const enums,
				    const size_t num_enums,
				    const char *const strs[], char *buf)
{
	size_t len = 0;
	size_t index;

	for (index = 0; index < num_enums; index++)
		len += sysfs_emit_at(buf, len, "%s\n", strs[enums[index]]);

	return len;
}

static ssize_t strs_available_show(const struct counter_available *const avail,
				   char *buf)
{
	size_t len = 0;
	size_t index;

	for (index = 0; index < avail->num_items; index++)
		len += sysfs_emit_at(buf, len, "%s\n", avail->strs[index]);

	return len;
}

static ssize_t counter_comp_available_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	const struct counter_attribute *const a = to_counter_attribute(attr);
	const struct counter_count *const count = a->parent;
	const struct counter_synapse *const synapse = a->comp.priv;
	const struct counter_available *const avail = a->comp.priv;

	switch (a->comp.type) {
	case COUNTER_COMP_FUNCTION:
		return enums_available_show(count->functions_list,
					    count->num_functions,
					    counter_function_str, buf);
	case COUNTER_COMP_SYNAPSE_ACTION:
		return enums_available_show(synapse->actions_list,
					    synapse->num_actions,
					    counter_synapse_action_str, buf);
	case COUNTER_COMP_ENUM:
		return strs_available_show(avail, buf);
	case COUNTER_COMP_COUNT_MODE:
		return enums_available_show(avail->enums, avail->num_items,
					    counter_count_mode_str, buf);
	default:
		return -EINVAL;
	}
}

static int counter_avail_attr_create(struct device *const dev,
	struct counter_attribute_group *const group,
	const struct counter_comp *const comp, void *const parent)
{
	struct counter_attribute *counter_attr;
	struct device_attribute *dev_attr;

	counter_attr = devm_kzalloc(dev, sizeof(*counter_attr), GFP_KERNEL);
	if (!counter_attr)
		return -ENOMEM;

	/* Configure Counter attribute */
	counter_attr->comp.type = comp->type;
	counter_attr->comp.priv = comp->priv;
	counter_attr->parent = parent;

	/* Initialize sysfs attribute */
	dev_attr = &counter_attr->dev_attr;
	sysfs_attr_init(&dev_attr->attr);

	/* Configure device attribute */
	dev_attr->attr.name = devm_kasprintf(dev, GFP_KERNEL, "%s_available",
					     comp->name);
	if (!dev_attr->attr.name)
		return -ENOMEM;
	dev_attr->attr.mode = 0444;
	dev_attr->show = counter_comp_available_show;

	/* Store list node */
	list_add(&counter_attr->l, &group->attr_list);
	group->num_attr++;

	return 0;
}

static int counter_attr_create(struct device *const dev,
			       struct counter_attribute_group *const group,
			       const struct counter_comp *const comp,
			       const enum counter_scope scope,
			       void *const parent)
{
	struct counter_attribute *counter_attr;
	struct device_attribute *dev_attr;

	counter_attr = devm_kzalloc(dev, sizeof(*counter_attr), GFP_KERNEL);
	if (!counter_attr)
		return -ENOMEM;

	/* Configure Counter attribute */
	counter_attr->comp = *comp;
	counter_attr->scope = scope;
	counter_attr->parent = parent;

	/* Configure device attribute */
	dev_attr = &counter_attr->dev_attr;
	sysfs_attr_init(&dev_attr->attr);
	dev_attr->attr.name = comp->name;
	switch (comp->type) {
	case COUNTER_COMP_U8:
	case COUNTER_COMP_BOOL:
		if (comp->device_u8_read) {
			dev_attr->attr.mode |= 0444;
			dev_attr->show = counter_comp_u8_show;
		}
		if (comp->device_u8_write) {
			dev_attr->attr.mode |= 0200;
			dev_attr->store = counter_comp_u8_store;
		}
		break;
	case COUNTER_COMP_SIGNAL_LEVEL:
	case COUNTER_COMP_FUNCTION:
	case COUNTER_COMP_SYNAPSE_ACTION:
	case COUNTER_COMP_ENUM:
	case COUNTER_COMP_COUNT_DIRECTION:
	case COUNTER_COMP_COUNT_MODE:
		if (comp->device_u32_read) {
			dev_attr->attr.mode |= 0444;
			dev_attr->show = counter_comp_u32_show;
		}
		if (comp->device_u32_write) {
			dev_attr->attr.mode |= 0200;
			dev_attr->store = counter_comp_u32_store;
		}
		break;
	case COUNTER_COMP_U64:
		if (comp->device_u64_read) {
			dev_attr->attr.mode |= 0444;
			dev_attr->show = counter_comp_u64_show;
		}
		if (comp->device_u64_write) {
			dev_attr->attr.mode |= 0200;
			dev_attr->store = counter_comp_u64_store;
		}
		break;
	default:
		return -EINVAL;
	}

	/* Store list node */
	list_add(&counter_attr->l, &group->attr_list);
	group->num_attr++;

	/* Create "*_available" attribute if needed */
	switch (comp->type) {
	case COUNTER_COMP_FUNCTION:
	case COUNTER_COMP_SYNAPSE_ACTION:
	case COUNTER_COMP_ENUM:
	case COUNTER_COMP_COUNT_MODE:
		return counter_avail_attr_create(dev, group, comp, parent);
	default:
		return 0;
	}
}

static ssize_t counter_comp_name_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", to_counter_attribute(attr)->comp.name);
}

static int counter_name_attr_create(struct device *const dev,
				    struct counter_attribute_group *const group,
				    const char *const name)
{
	struct counter_attribute *counter_attr;

	counter_attr = devm_kzalloc(dev, sizeof(*counter_attr), GFP_KERNEL);
	if (!counter_attr)
		return -ENOMEM;

	/* Configure Counter attribute */
	counter_attr->comp.name = name;

	/* Configure device attribute */
	sysfs_attr_init(&counter_attr->dev_attr.attr);
	counter_attr->dev_attr.attr.name = "name";
	counter_attr->dev_attr.attr.mode = 0444;
	counter_attr->dev_attr.show = counter_comp_name_show;

	/* Store list node */
	list_add(&counter_attr->l, &group->attr_list);
	group->num_attr++;

	return 0;
}

static struct counter_comp counter_signal_comp = {
	.type = COUNTER_COMP_SIGNAL_LEVEL,
	.name = "signal",
};

static int counter_signal_attrs_create(struct counter_device *const counter,
	struct counter_attribute_group *const cattr_group,
	struct counter_signal *const signal)
{
	const enum counter_scope scope = COUNTER_SCOPE_SIGNAL;
	struct device *const dev = &counter->dev;
	int err;
	struct counter_comp comp;
	size_t i;

	/* Create main Signal attribute */
	comp = counter_signal_comp;
	comp.signal_u32_read = counter->ops->signal_read;
	err = counter_attr_create(dev, cattr_group, &comp, scope, signal);
	if (err < 0)
		return err;

	/* Create Signal name attribute */
	err = counter_name_attr_create(dev, cattr_group, signal->name);
	if (err < 0)
		return err;

	/* Create an attribute for each extension */
	for (i = 0; i < signal->num_ext; i++) {
		err = counter_attr_create(dev, cattr_group, signal->ext + i,
					  scope, signal);
		if (err < 0)
			return err;
	}

	return 0;
}

static int counter_sysfs_signals_add(struct counter_device *const counter,
	struct counter_attribute_group *const groups)
{
	size_t i;
	int err;

	/* Add each Signal */
	for (i = 0; i < counter->num_signals; i++) {
		/* Generate Signal attribute directory name */
		groups[i].name = devm_kasprintf(&counter->dev, GFP_KERNEL,
						"signal%zu", i);
		if (!groups[i].name)
			return -ENOMEM;

		/* Create all attributes associated with Signal */
		err = counter_signal_attrs_create(counter, groups + i,
						  counter->signals + i);
		if (err < 0)
			return err;
	}

	return 0;
}

static int counter_sysfs_synapses_add(struct counter_device *const counter,
	struct counter_attribute_group *const group,
	struct counter_count *const count)
{
	size_t i;

	/* Add each Synapse */
	for (i = 0; i < count->num_synapses; i++) {
		struct device *const dev = &counter->dev;
		struct counter_synapse *synapse;
		size_t id;
		struct counter_comp comp;
		int err;

		synapse = count->synapses + i;

		/* Generate Synapse action name */
		id = synapse->signal - counter->signals;
		comp.name = devm_kasprintf(dev, GFP_KERNEL, "signal%zu_action",
					   id);
		if (!comp.name)
			return -ENOMEM;

		/* Create action attribute */
		comp.type = COUNTER_COMP_SYNAPSE_ACTION;
		comp.action_read = counter->ops->action_read;
		comp.action_write = counter->ops->action_write;
		comp.priv = synapse;
		err = counter_attr_create(dev, group, &comp,
					  COUNTER_SCOPE_COUNT, count);
		if (err < 0)
			return err;
	}

	return 0;
}

static struct counter_comp counter_count_comp =
	COUNTER_COMP_COUNT_U64("count", NULL, NULL);

static struct counter_comp counter_function_comp = {
	.type = COUNTER_COMP_FUNCTION,
	.name = "function",
};

static int counter_count_attrs_create(struct counter_device *const counter,
	struct counter_attribute_group *const cattr_group,
	struct counter_count *const count)
{
	const enum counter_scope scope = COUNTER_SCOPE_COUNT;
	struct device *const dev = &counter->dev;
	int err;
	struct counter_comp comp;
	size_t i;

	/* Create main Count attribute */
	comp = counter_count_comp;
	comp.count_u64_read = counter->ops->count_read;
	comp.count_u64_write = counter->ops->count_write;
	err = counter_attr_create(dev, cattr_group, &comp, scope, count);
	if (err < 0)
		return err;

	/* Create Count name attribute */
	err = counter_name_attr_create(dev, cattr_group, count->name);
	if (err < 0)
		return err;

	/* Create Count function attribute */
	comp = counter_function_comp;
	comp.count_u32_read = counter->ops->function_read;
	comp.count_u32_write = counter->ops->function_write;
	err = counter_attr_create(dev, cattr_group, &comp, scope, count);
	if (err < 0)
		return err;

	/* Create an attribute for each extension */
	for (i = 0; i < count->num_ext; i++) {
		err = counter_attr_create(dev, cattr_group, count->ext + i,
					  scope, count);
		if (err < 0)
			return err;
	}

	return 0;
}

static int counter_sysfs_counts_add(struct counter_device *const counter,
	struct counter_attribute_group *const groups)
{
	size_t i;
	struct counter_count *count;
	int err;

	/* Add each Count */
	for (i = 0; i < counter->num_counts; i++) {
		count = counter->counts + i;

		/* Generate Count attribute directory name */
		groups[i].name = devm_kasprintf(&counter->dev, GFP_KERNEL,
						"count%zu", i);
		if (!groups[i].name)
			return -ENOMEM;

		/* Add sysfs attributes of the Synapses */
		err = counter_sysfs_synapses_add(counter, groups + i, count);
		if (err < 0)
			return err;

		/* Create all attributes associated with Count */
		err = counter_count_attrs_create(counter, groups + i, count);
		if (err < 0)
			return err;
	}

	return 0;
}

static int counter_num_signals_read(struct counter_device *counter, u8 *val)
{
	*val = counter->num_signals;
	return 0;
}

static int counter_num_counts_read(struct counter_device *counter, u8 *val)
{
	*val = counter->num_counts;
	return 0;
}

static struct counter_comp counter_num_signals_comp =
	COUNTER_COMP_DEVICE_U8("num_signals", counter_num_signals_read, NULL);

static struct counter_comp counter_num_counts_comp =
	COUNTER_COMP_DEVICE_U8("num_counts", counter_num_counts_read, NULL);

static int counter_sysfs_attr_add(struct counter_device *const counter,
				  struct counter_attribute_group *cattr_group)
{
	const enum counter_scope scope = COUNTER_SCOPE_DEVICE;
	struct device *const dev = &counter->dev;
	int err;
	size_t i;

	/* Add Signals sysfs attributes */
	err = counter_sysfs_signals_add(counter, cattr_group);
	if (err < 0)
		return err;
	cattr_group += counter->num_signals;

	/* Add Counts sysfs attributes */
	err = counter_sysfs_counts_add(counter, cattr_group);
	if (err < 0)
		return err;
	cattr_group += counter->num_counts;

	/* Create name attribute */
	err = counter_name_attr_create(dev, cattr_group, counter->name);
	if (err < 0)
		return err;

	/* Create num_signals attribute */
	err = counter_attr_create(dev, cattr_group, &counter_num_signals_comp,
				  scope, NULL);
	if (err < 0)
		return err;

	/* Create num_counts attribute */
	err = counter_attr_create(dev, cattr_group, &counter_num_counts_comp,
				  scope, NULL);
	if (err < 0)
		return err;

	/* Create an attribute for each extension */
	for (i = 0; i < counter->num_ext; i++) {
		err = counter_attr_create(dev, cattr_group, counter->ext + i,
					  scope, NULL);
		if (err < 0)
			return err;
	}

	return 0;
}

/**
 * counter_sysfs_add - Adds Counter sysfs attributes to the device structure
 * @counter:	Pointer to the Counter device structure
 *
 * Counter sysfs attributes are created and added to the respective device
 * structure for later registration to the system. Resource-managed memory
 * allocation is performed by this function, and this memory should be freed
 * when no longer needed (automatically by a device_unregister call, or
 * manually by a devres_release_all call).
 */
int counter_sysfs_add(struct counter_device *const counter)
{
	struct device *const dev = &counter->dev;
	const size_t num_groups = counter->num_signals + counter->num_counts + 1;
	struct counter_attribute_group *cattr_groups;
	size_t i, j;
	int err;
	struct attribute_group *groups;
	struct counter_attribute *p;

	/* Allocate space for attribute groups (signals, counts, and ext) */
	cattr_groups = devm_kcalloc(dev, num_groups, sizeof(*cattr_groups),
				    GFP_KERNEL);
	if (!cattr_groups)
		return -ENOMEM;

	/* Initialize attribute lists */
	for (i = 0; i < num_groups; i++)
		INIT_LIST_HEAD(&cattr_groups[i].attr_list);

	/* Add Counter device sysfs attributes */
	err = counter_sysfs_attr_add(counter, cattr_groups);
	if (err < 0)
		return err;

	/* Allocate attribute group pointers for association with device */
	dev->groups = devm_kcalloc(dev, num_groups + 1, sizeof(*dev->groups),
				   GFP_KERNEL);
	if (!dev->groups)
		return -ENOMEM;

	/* Allocate space for attribute groups */
	groups = devm_kcalloc(dev, num_groups, sizeof(*groups), GFP_KERNEL);
	if (!groups)
		return -ENOMEM;

	/* Prepare each group of attributes for association */
	for (i = 0; i < num_groups; i++) {
		groups[i].name = cattr_groups[i].name;

		/* Allocate space for attribute pointers */
		groups[i].attrs = devm_kcalloc(dev,
					       cattr_groups[i].num_attr + 1,
					       sizeof(*groups[i].attrs),
					       GFP_KERNEL);
		if (!groups[i].attrs)
			return -ENOMEM;

		/* Add attribute pointers to attribute group */
		j = 0;
		list_for_each_entry(p, &cattr_groups[i].attr_list, l)
			groups[i].attrs[j++] = &p->dev_attr.attr;

		/* Associate attribute group */
		dev->groups[i] = &groups[i];
	}

	return 0;
}
