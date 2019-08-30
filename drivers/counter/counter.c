// SPDX-License-Identifier: GPL-2.0
/*
 * Generic Counter interface
 * Copyright (C) 2018 William Breathitt Gray
 */
#include <linux/counter.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>

const char *const counter_count_direction_str[2] = {
	[COUNTER_COUNT_DIRECTION_FORWARD] = "forward",
	[COUNTER_COUNT_DIRECTION_BACKWARD] = "backward"
};
EXPORT_SYMBOL_GPL(counter_count_direction_str);

const char *const counter_count_mode_str[4] = {
	[COUNTER_COUNT_MODE_NORMAL] = "normal",
	[COUNTER_COUNT_MODE_RANGE_LIMIT] = "range limit",
	[COUNTER_COUNT_MODE_NON_RECYCLE] = "non-recycle",
	[COUNTER_COUNT_MODE_MODULO_N] = "modulo-n"
};
EXPORT_SYMBOL_GPL(counter_count_mode_str);

ssize_t counter_signal_enum_read(struct counter_device *counter,
				 struct counter_signal *signal, void *priv,
				 char *buf)
{
	const struct counter_signal_enum_ext *const e = priv;
	int err;
	size_t index;

	if (!e->get)
		return -EINVAL;

	err = e->get(counter, signal, &index);
	if (err)
		return err;

	if (index >= e->num_items)
		return -EINVAL;

	return sprintf(buf, "%s\n", e->items[index]);
}
EXPORT_SYMBOL_GPL(counter_signal_enum_read);

ssize_t counter_signal_enum_write(struct counter_device *counter,
				  struct counter_signal *signal, void *priv,
				  const char *buf, size_t len)
{
	const struct counter_signal_enum_ext *const e = priv;
	ssize_t index;
	int err;

	if (!e->set)
		return -EINVAL;

	index = __sysfs_match_string(e->items, e->num_items, buf);
	if (index < 0)
		return index;

	err = e->set(counter, signal, index);
	if (err)
		return err;

	return len;
}
EXPORT_SYMBOL_GPL(counter_signal_enum_write);

ssize_t counter_signal_enum_available_read(struct counter_device *counter,
					   struct counter_signal *signal,
					   void *priv, char *buf)
{
	const struct counter_signal_enum_ext *const e = priv;
	size_t i;
	size_t len = 0;

	if (!e->num_items)
		return 0;

	for (i = 0; i < e->num_items; i++)
		len += sprintf(buf + len, "%s\n", e->items[i]);

	return len;
}
EXPORT_SYMBOL_GPL(counter_signal_enum_available_read);

ssize_t counter_count_enum_read(struct counter_device *counter,
				struct counter_count *count, void *priv,
				char *buf)
{
	const struct counter_count_enum_ext *const e = priv;
	int err;
	size_t index;

	if (!e->get)
		return -EINVAL;

	err = e->get(counter, count, &index);
	if (err)
		return err;

	if (index >= e->num_items)
		return -EINVAL;

	return sprintf(buf, "%s\n", e->items[index]);
}
EXPORT_SYMBOL_GPL(counter_count_enum_read);

ssize_t counter_count_enum_write(struct counter_device *counter,
				 struct counter_count *count, void *priv,
				 const char *buf, size_t len)
{
	const struct counter_count_enum_ext *const e = priv;
	ssize_t index;
	int err;

	if (!e->set)
		return -EINVAL;

	index = __sysfs_match_string(e->items, e->num_items, buf);
	if (index < 0)
		return index;

	err = e->set(counter, count, index);
	if (err)
		return err;

	return len;
}
EXPORT_SYMBOL_GPL(counter_count_enum_write);

ssize_t counter_count_enum_available_read(struct counter_device *counter,
					  struct counter_count *count,
					  void *priv, char *buf)
{
	const struct counter_count_enum_ext *const e = priv;
	size_t i;
	size_t len = 0;

	if (!e->num_items)
		return 0;

	for (i = 0; i < e->num_items; i++)
		len += sprintf(buf + len, "%s\n", e->items[i]);

	return len;
}
EXPORT_SYMBOL_GPL(counter_count_enum_available_read);

ssize_t counter_device_enum_read(struct counter_device *counter, void *priv,
				 char *buf)
{
	const struct counter_device_enum_ext *const e = priv;
	int err;
	size_t index;

	if (!e->get)
		return -EINVAL;

	err = e->get(counter, &index);
	if (err)
		return err;

	if (index >= e->num_items)
		return -EINVAL;

	return sprintf(buf, "%s\n", e->items[index]);
}
EXPORT_SYMBOL_GPL(counter_device_enum_read);

ssize_t counter_device_enum_write(struct counter_device *counter, void *priv,
				  const char *buf, size_t len)
{
	const struct counter_device_enum_ext *const e = priv;
	ssize_t index;
	int err;

	if (!e->set)
		return -EINVAL;

	index = __sysfs_match_string(e->items, e->num_items, buf);
	if (index < 0)
		return index;

	err = e->set(counter, index);
	if (err)
		return err;

	return len;
}
EXPORT_SYMBOL_GPL(counter_device_enum_write);

ssize_t counter_device_enum_available_read(struct counter_device *counter,
					   void *priv, char *buf)
{
	const struct counter_device_enum_ext *const e = priv;
	size_t i;
	size_t len = 0;

	if (!e->num_items)
		return 0;

	for (i = 0; i < e->num_items; i++)
		len += sprintf(buf + len, "%s\n", e->items[i]);

	return len;
}
EXPORT_SYMBOL_GPL(counter_device_enum_available_read);

static const char *const counter_signal_level_str[] = {
	[COUNTER_SIGNAL_LEVEL_LOW] = "low",
	[COUNTER_SIGNAL_LEVEL_HIGH] = "high"
};

/**
 * counter_signal_read_value_set - set counter_signal_read_value data
 * @val:	counter_signal_read_value structure to set
 * @type:	property Signal data represents
 * @data:	Signal data
 *
 * This function sets an opaque counter_signal_read_value structure with the
 * provided Signal data.
 */
void counter_signal_read_value_set(struct counter_signal_read_value *const val,
				   const enum counter_signal_value_type type,
				   void *const data)
{
	if (type == COUNTER_SIGNAL_LEVEL)
		val->len = sprintf(val->buf, "%s\n",
				   counter_signal_level_str[*(enum counter_signal_level *)data]);
	else
		val->len = 0;
}
EXPORT_SYMBOL_GPL(counter_signal_read_value_set);

/**
 * counter_count_read_value_set - set counter_count_read_value data
 * @val:	counter_count_read_value structure to set
 * @type:	property Count data represents
 * @data:	Count data
 *
 * This function sets an opaque counter_count_read_value structure with the
 * provided Count data.
 */
void counter_count_read_value_set(struct counter_count_read_value *const val,
				  const enum counter_count_value_type type,
				  void *const data)
{
	switch (type) {
	case COUNTER_COUNT_POSITION:
		val->len = sprintf(val->buf, "%lu\n", *(unsigned long *)data);
		break;
	default:
		val->len = 0;
	}
}
EXPORT_SYMBOL_GPL(counter_count_read_value_set);

/**
 * counter_count_write_value_get - get counter_count_write_value data
 * @data:	Count data
 * @type:	property Count data represents
 * @val:	counter_count_write_value structure containing data
 *
 * This function extracts Count data from the provided opaque
 * counter_count_write_value structure and stores it at the address provided by
 * @data.
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int counter_count_write_value_get(void *const data,
				  const enum counter_count_value_type type,
				  const struct counter_count_write_value *const val)
{
	int err;

	switch (type) {
	case COUNTER_COUNT_POSITION:
		err = kstrtoul(val->buf, 0, data);
		if (err)
			return err;
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(counter_count_write_value_get);

struct counter_attr_parm {
	struct counter_device_attr_group *group;
	const char *prefix;
	const char *name;
	ssize_t (*show)(struct device *dev, struct device_attribute *attr,
			char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t len);
	void *component;
};

struct counter_device_attr {
	struct device_attribute dev_attr;
	struct list_head l;
	void *component;
};

static int counter_attribute_create(const struct counter_attr_parm *const parm)
{
	struct counter_device_attr *counter_attr;
	struct device_attribute *dev_attr;
	int err;
	struct list_head *const attr_list = &parm->group->attr_list;

	/* Allocate a Counter device attribute */
	counter_attr = kzalloc(sizeof(*counter_attr), GFP_KERNEL);
	if (!counter_attr)
		return -ENOMEM;
	dev_attr = &counter_attr->dev_attr;

	sysfs_attr_init(&dev_attr->attr);

	/* Configure device attribute */
	dev_attr->attr.name = kasprintf(GFP_KERNEL, "%s%s", parm->prefix,
					parm->name);
	if (!dev_attr->attr.name) {
		err = -ENOMEM;
		goto err_free_counter_attr;
	}
	if (parm->show) {
		dev_attr->attr.mode |= 0444;
		dev_attr->show = parm->show;
	}
	if (parm->store) {
		dev_attr->attr.mode |= 0200;
		dev_attr->store = parm->store;
	}

	/* Store associated Counter component with attribute */
	counter_attr->component = parm->component;

	/* Keep track of the attribute for later cleanup */
	list_add(&counter_attr->l, attr_list);
	parm->group->num_attr++;

	return 0;

err_free_counter_attr:
	kfree(counter_attr);
	return err;
}

#define to_counter_attr(_dev_attr) \
	container_of(_dev_attr, struct counter_device_attr, dev_attr)

struct counter_signal_unit {
	struct counter_signal *signal;
};

static ssize_t counter_signal_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct counter_device *const counter = dev_get_drvdata(dev);
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_signal_unit *const component = devattr->component;
	struct counter_signal *const signal = component->signal;
	int err;
	struct counter_signal_read_value val = { .buf = buf };

	err = counter->ops->signal_read(counter, signal, &val);
	if (err)
		return err;

	return val.len;
}

struct counter_name_unit {
	const char *name;
};

static ssize_t counter_device_attr_name_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	const struct counter_name_unit *const comp = to_counter_attr(attr)->component;

	return sprintf(buf, "%s\n", comp->name);
}

static int counter_name_attribute_create(
	struct counter_device_attr_group *const group,
	const char *const name)
{
	struct counter_name_unit *name_comp;
	struct counter_attr_parm parm;
	int err;

	/* Skip if no name */
	if (!name)
		return 0;

	/* Allocate name attribute component */
	name_comp = kmalloc(sizeof(*name_comp), GFP_KERNEL);
	if (!name_comp)
		return -ENOMEM;
	name_comp->name = name;

	/* Allocate Signal name attribute */
	parm.group = group;
	parm.prefix = "";
	parm.name = "name";
	parm.show = counter_device_attr_name_show;
	parm.store = NULL;
	parm.component = name_comp;
	err = counter_attribute_create(&parm);
	if (err)
		goto err_free_name_comp;

	return 0;

err_free_name_comp:
	kfree(name_comp);
	return err;
}

struct counter_signal_ext_unit {
	struct counter_signal *signal;
	const struct counter_signal_ext *ext;
};

static ssize_t counter_signal_ext_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_signal_ext_unit *const comp = devattr->component;
	const struct counter_signal_ext *const ext = comp->ext;

	return ext->read(dev_get_drvdata(dev), comp->signal, ext->priv, buf);
}

static ssize_t counter_signal_ext_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_signal_ext_unit *const comp = devattr->component;
	const struct counter_signal_ext *const ext = comp->ext;

	return ext->write(dev_get_drvdata(dev), comp->signal, ext->priv, buf,
		len);
}

static void counter_device_attr_list_free(struct list_head *attr_list)
{
	struct counter_device_attr *p, *n;

	list_for_each_entry_safe(p, n, attr_list, l) {
		/* free attribute name and associated component memory */
		kfree(p->dev_attr.attr.name);
		kfree(p->component);
		list_del(&p->l);
		kfree(p);
	}
}

static int counter_signal_ext_register(
	struct counter_device_attr_group *const group,
	struct counter_signal *const signal)
{
	const size_t num_ext = signal->num_ext;
	size_t i;
	const struct counter_signal_ext *ext;
	struct counter_signal_ext_unit *signal_ext_comp;
	struct counter_attr_parm parm;
	int err;

	/* Create an attribute for each extension */
	for (i = 0 ; i < num_ext; i++) {
		ext = signal->ext + i;

		/* Allocate signal_ext attribute component */
		signal_ext_comp = kmalloc(sizeof(*signal_ext_comp), GFP_KERNEL);
		if (!signal_ext_comp) {
			err = -ENOMEM;
			goto err_free_attr_list;
		}
		signal_ext_comp->signal = signal;
		signal_ext_comp->ext = ext;

		/* Allocate a Counter device attribute */
		parm.group = group;
		parm.prefix = "";
		parm.name = ext->name;
		parm.show = (ext->read) ? counter_signal_ext_show : NULL;
		parm.store = (ext->write) ? counter_signal_ext_store : NULL;
		parm.component = signal_ext_comp;
		err = counter_attribute_create(&parm);
		if (err) {
			kfree(signal_ext_comp);
			goto err_free_attr_list;
		}
	}

	return 0;

err_free_attr_list:
	counter_device_attr_list_free(&group->attr_list);
	return err;
}

static int counter_signal_attributes_create(
	struct counter_device_attr_group *const group,
	const struct counter_device *const counter,
	struct counter_signal *const signal)
{
	struct counter_signal_unit *signal_comp;
	struct counter_attr_parm parm;
	int err;

	/* Allocate Signal attribute component */
	signal_comp = kmalloc(sizeof(*signal_comp), GFP_KERNEL);
	if (!signal_comp)
		return -ENOMEM;
	signal_comp->signal = signal;

	/* Create main Signal attribute */
	parm.group = group;
	parm.prefix = "";
	parm.name = "signal";
	parm.show = (counter->ops->signal_read) ? counter_signal_show : NULL;
	parm.store = NULL;
	parm.component = signal_comp;
	err = counter_attribute_create(&parm);
	if (err) {
		kfree(signal_comp);
		return err;
	}

	/* Create Signal name attribute */
	err = counter_name_attribute_create(group, signal->name);
	if (err)
		goto err_free_attr_list;

	/* Register Signal extension attributes */
	err = counter_signal_ext_register(group, signal);
	if (err)
		goto err_free_attr_list;

	return 0;

err_free_attr_list:
	counter_device_attr_list_free(&group->attr_list);
	return err;
}

static int counter_signals_register(
	struct counter_device_attr_group *const groups_list,
	const struct counter_device *const counter)
{
	const size_t num_signals = counter->num_signals;
	size_t i;
	struct counter_signal *signal;
	const char *name;
	int err;

	/* Register each Signal */
	for (i = 0; i < num_signals; i++) {
		signal = counter->signals + i;

		/* Generate Signal attribute directory name */
		name = kasprintf(GFP_KERNEL, "signal%d", signal->id);
		if (!name) {
			err = -ENOMEM;
			goto err_free_attr_groups;
		}
		groups_list[i].attr_group.name = name;

		/* Create all attributes associated with Signal */
		err = counter_signal_attributes_create(groups_list + i, counter,
						       signal);
		if (err)
			goto err_free_attr_groups;
	}

	return 0;

err_free_attr_groups:
	do {
		kfree(groups_list[i].attr_group.name);
		counter_device_attr_list_free(&groups_list[i].attr_list);
	} while (i--);
	return err;
}

static const char *const counter_synapse_action_str[] = {
	[COUNTER_SYNAPSE_ACTION_NONE] = "none",
	[COUNTER_SYNAPSE_ACTION_RISING_EDGE] = "rising edge",
	[COUNTER_SYNAPSE_ACTION_FALLING_EDGE] = "falling edge",
	[COUNTER_SYNAPSE_ACTION_BOTH_EDGES] = "both edges"
};

struct counter_action_unit {
	struct counter_synapse *synapse;
	struct counter_count *count;
};

static ssize_t counter_action_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	int err;
	struct counter_device *const counter = dev_get_drvdata(dev);
	const struct counter_action_unit *const component = devattr->component;
	struct counter_count *const count = component->count;
	struct counter_synapse *const synapse = component->synapse;
	size_t action_index;
	enum counter_synapse_action action;

	err = counter->ops->action_get(counter, count, synapse, &action_index);
	if (err)
		return err;

	synapse->action = action_index;

	action = synapse->actions_list[action_index];
	return sprintf(buf, "%s\n", counter_synapse_action_str[action]);
}

static ssize_t counter_action_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_action_unit *const component = devattr->component;
	struct counter_synapse *const synapse = component->synapse;
	size_t action_index;
	const size_t num_actions = synapse->num_actions;
	enum counter_synapse_action action;
	int err;
	struct counter_device *const counter = dev_get_drvdata(dev);
	struct counter_count *const count = component->count;

	/* Find requested action mode */
	for (action_index = 0; action_index < num_actions; action_index++) {
		action = synapse->actions_list[action_index];
		if (sysfs_streq(buf, counter_synapse_action_str[action]))
			break;
	}
	/* If requested action mode not found */
	if (action_index >= num_actions)
		return -EINVAL;

	err = counter->ops->action_set(counter, count, synapse, action_index);
	if (err)
		return err;

	synapse->action = action_index;

	return len;
}

struct counter_action_avail_unit {
	const enum counter_synapse_action *actions_list;
	size_t num_actions;
};

static ssize_t counter_synapse_action_available_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_action_avail_unit *const component = devattr->component;
	size_t i;
	enum counter_synapse_action action;
	ssize_t len = 0;

	for (i = 0; i < component->num_actions; i++) {
		action = component->actions_list[i];
		len += sprintf(buf + len, "%s\n",
			       counter_synapse_action_str[action]);
	}

	return len;
}

static int counter_synapses_register(
	struct counter_device_attr_group *const group,
	const struct counter_device *const counter,
	struct counter_count *const count, const char *const count_attr_name)
{
	size_t i;
	struct counter_synapse *synapse;
	const char *prefix;
	struct counter_action_unit *action_comp;
	struct counter_attr_parm parm;
	int err;
	struct counter_action_avail_unit *avail_comp;

	/* Register each Synapse */
	for (i = 0; i < count->num_synapses; i++) {
		synapse = count->synapses + i;

		/* Generate attribute prefix */
		prefix = kasprintf(GFP_KERNEL, "signal%d_",
				   synapse->signal->id);
		if (!prefix) {
			err = -ENOMEM;
			goto err_free_attr_list;
		}

		/* Allocate action attribute component */
		action_comp = kmalloc(sizeof(*action_comp), GFP_KERNEL);
		if (!action_comp) {
			err = -ENOMEM;
			goto err_free_prefix;
		}
		action_comp->synapse = synapse;
		action_comp->count = count;

		/* Create action attribute */
		parm.group = group;
		parm.prefix = prefix;
		parm.name = "action";
		parm.show = (counter->ops->action_get) ? counter_action_show : NULL;
		parm.store = (counter->ops->action_set) ? counter_action_store : NULL;
		parm.component = action_comp;
		err = counter_attribute_create(&parm);
		if (err) {
			kfree(action_comp);
			goto err_free_prefix;
		}

		/* Allocate action available attribute component */
		avail_comp = kmalloc(sizeof(*avail_comp), GFP_KERNEL);
		if (!avail_comp) {
			err = -ENOMEM;
			goto err_free_prefix;
		}
		avail_comp->actions_list = synapse->actions_list;
		avail_comp->num_actions = synapse->num_actions;

		/* Create action_available attribute */
		parm.group = group;
		parm.prefix = prefix;
		parm.name = "action_available";
		parm.show = counter_synapse_action_available_show;
		parm.store = NULL;
		parm.component = avail_comp;
		err = counter_attribute_create(&parm);
		if (err) {
			kfree(avail_comp);
			goto err_free_prefix;
		}

		kfree(prefix);
	}

	return 0;

err_free_prefix:
	kfree(prefix);
err_free_attr_list:
	counter_device_attr_list_free(&group->attr_list);
	return err;
}

struct counter_count_unit {
	struct counter_count *count;
};

static ssize_t counter_count_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct counter_device *const counter = dev_get_drvdata(dev);
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_count_unit *const component = devattr->component;
	struct counter_count *const count = component->count;
	int err;
	struct counter_count_read_value val = { .buf = buf };

	err = counter->ops->count_read(counter, count, &val);
	if (err)
		return err;

	return val.len;
}

static ssize_t counter_count_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	struct counter_device *const counter = dev_get_drvdata(dev);
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_count_unit *const component = devattr->component;
	struct counter_count *const count = component->count;
	int err;
	struct counter_count_write_value val = { .buf = buf };

	err = counter->ops->count_write(counter, count, &val);
	if (err)
		return err;

	return len;
}

static const char *const counter_count_function_str[] = {
	[COUNTER_COUNT_FUNCTION_INCREASE] = "increase",
	[COUNTER_COUNT_FUNCTION_DECREASE] = "decrease",
	[COUNTER_COUNT_FUNCTION_PULSE_DIRECTION] = "pulse-direction",
	[COUNTER_COUNT_FUNCTION_QUADRATURE_X1_A] = "quadrature x1 a",
	[COUNTER_COUNT_FUNCTION_QUADRATURE_X1_B] = "quadrature x1 b",
	[COUNTER_COUNT_FUNCTION_QUADRATURE_X2_A] = "quadrature x2 a",
	[COUNTER_COUNT_FUNCTION_QUADRATURE_X2_B] = "quadrature x2 b",
	[COUNTER_COUNT_FUNCTION_QUADRATURE_X4] = "quadrature x4"
};

static ssize_t counter_function_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int err;
	struct counter_device *const counter = dev_get_drvdata(dev);
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_count_unit *const component = devattr->component;
	struct counter_count *const count = component->count;
	size_t func_index;
	enum counter_count_function function;

	err = counter->ops->function_get(counter, count, &func_index);
	if (err)
		return err;

	count->function = func_index;

	function = count->functions_list[func_index];
	return sprintf(buf, "%s\n", counter_count_function_str[function]);
}

static ssize_t counter_function_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len)
{
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_count_unit *const component = devattr->component;
	struct counter_count *const count = component->count;
	const size_t num_functions = count->num_functions;
	size_t func_index;
	enum counter_count_function function;
	int err;
	struct counter_device *const counter = dev_get_drvdata(dev);

	/* Find requested Count function mode */
	for (func_index = 0; func_index < num_functions; func_index++) {
		function = count->functions_list[func_index];
		if (sysfs_streq(buf, counter_count_function_str[function]))
			break;
	}
	/* Return error if requested Count function mode not found */
	if (func_index >= num_functions)
		return -EINVAL;

	err = counter->ops->function_set(counter, count, func_index);
	if (err)
		return err;

	count->function = func_index;

	return len;
}

struct counter_count_ext_unit {
	struct counter_count *count;
	const struct counter_count_ext *ext;
};

static ssize_t counter_count_ext_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_count_ext_unit *const comp = devattr->component;
	const struct counter_count_ext *const ext = comp->ext;

	return ext->read(dev_get_drvdata(dev), comp->count, ext->priv, buf);
}

static ssize_t counter_count_ext_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_count_ext_unit *const comp = devattr->component;
	const struct counter_count_ext *const ext = comp->ext;

	return ext->write(dev_get_drvdata(dev), comp->count, ext->priv, buf,
		len);
}

static int counter_count_ext_register(
	struct counter_device_attr_group *const group,
	struct counter_count *const count)
{
	size_t i;
	const struct counter_count_ext *ext;
	struct counter_count_ext_unit *count_ext_comp;
	struct counter_attr_parm parm;
	int err;

	/* Create an attribute for each extension */
	for (i = 0 ; i < count->num_ext; i++) {
		ext = count->ext + i;

		/* Allocate count_ext attribute component */
		count_ext_comp = kmalloc(sizeof(*count_ext_comp), GFP_KERNEL);
		if (!count_ext_comp) {
			err = -ENOMEM;
			goto err_free_attr_list;
		}
		count_ext_comp->count = count;
		count_ext_comp->ext = ext;

		/* Allocate count_ext attribute */
		parm.group = group;
		parm.prefix = "";
		parm.name = ext->name;
		parm.show = (ext->read) ? counter_count_ext_show : NULL;
		parm.store = (ext->write) ? counter_count_ext_store : NULL;
		parm.component = count_ext_comp;
		err = counter_attribute_create(&parm);
		if (err) {
			kfree(count_ext_comp);
			goto err_free_attr_list;
		}
	}

	return 0;

err_free_attr_list:
	counter_device_attr_list_free(&group->attr_list);
	return err;
}

struct counter_func_avail_unit {
	const enum counter_count_function *functions_list;
	size_t num_functions;
};

static ssize_t counter_count_function_available_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_func_avail_unit *const component = devattr->component;
	const enum counter_count_function *const func_list = component->functions_list;
	const size_t num_functions = component->num_functions;
	size_t i;
	enum counter_count_function function;
	ssize_t len = 0;

	for (i = 0; i < num_functions; i++) {
		function = func_list[i];
		len += sprintf(buf + len, "%s\n",
			       counter_count_function_str[function]);
	}

	return len;
}

static int counter_count_attributes_create(
	struct counter_device_attr_group *const group,
	const struct counter_device *const counter,
	struct counter_count *const count)
{
	struct counter_count_unit *count_comp;
	struct counter_attr_parm parm;
	int err;
	struct counter_count_unit *func_comp;
	struct counter_func_avail_unit *avail_comp;

	/* Allocate count attribute component */
	count_comp = kmalloc(sizeof(*count_comp), GFP_KERNEL);
	if (!count_comp)
		return -ENOMEM;
	count_comp->count = count;

	/* Create main Count attribute */
	parm.group = group;
	parm.prefix = "";
	parm.name = "count";
	parm.show = (counter->ops->count_read) ? counter_count_show : NULL;
	parm.store = (counter->ops->count_write) ? counter_count_store : NULL;
	parm.component = count_comp;
	err = counter_attribute_create(&parm);
	if (err) {
		kfree(count_comp);
		return err;
	}

	/* Allocate function attribute component */
	func_comp = kmalloc(sizeof(*func_comp), GFP_KERNEL);
	if (!func_comp) {
		err = -ENOMEM;
		goto err_free_attr_list;
	}
	func_comp->count = count;

	/* Create Count function attribute */
	parm.group = group;
	parm.prefix = "";
	parm.name = "function";
	parm.show = (counter->ops->function_get) ? counter_function_show : NULL;
	parm.store = (counter->ops->function_set) ? counter_function_store : NULL;
	parm.component = func_comp;
	err = counter_attribute_create(&parm);
	if (err) {
		kfree(func_comp);
		goto err_free_attr_list;
	}

	/* Allocate function available attribute component */
	avail_comp = kmalloc(sizeof(*avail_comp), GFP_KERNEL);
	if (!avail_comp) {
		err = -ENOMEM;
		goto err_free_attr_list;
	}
	avail_comp->functions_list = count->functions_list;
	avail_comp->num_functions = count->num_functions;

	/* Create Count function_available attribute */
	parm.group = group;
	parm.prefix = "";
	parm.name = "function_available";
	parm.show = counter_count_function_available_show;
	parm.store = NULL;
	parm.component = avail_comp;
	err = counter_attribute_create(&parm);
	if (err) {
		kfree(avail_comp);
		goto err_free_attr_list;
	}

	/* Create Count name attribute */
	err = counter_name_attribute_create(group, count->name);
	if (err)
		goto err_free_attr_list;

	/* Register Count extension attributes */
	err = counter_count_ext_register(group, count);
	if (err)
		goto err_free_attr_list;

	return 0;

err_free_attr_list:
	counter_device_attr_list_free(&group->attr_list);
	return err;
}

static int counter_counts_register(
	struct counter_device_attr_group *const groups_list,
	const struct counter_device *const counter)
{
	size_t i;
	struct counter_count *count;
	const char *name;
	int err;

	/* Register each Count */
	for (i = 0; i < counter->num_counts; i++) {
		count = counter->counts + i;

		/* Generate Count attribute directory name */
		name = kasprintf(GFP_KERNEL, "count%d", count->id);
		if (!name) {
			err = -ENOMEM;
			goto err_free_attr_groups;
		}
		groups_list[i].attr_group.name = name;

		/* Register the Synapses associated with each Count */
		err = counter_synapses_register(groups_list + i, counter, count,
						name);
		if (err)
			goto err_free_attr_groups;

		/* Create all attributes associated with Count */
		err = counter_count_attributes_create(groups_list + i, counter,
						      count);
		if (err)
			goto err_free_attr_groups;
	}

	return 0;

err_free_attr_groups:
	do {
		kfree(groups_list[i].attr_group.name);
		counter_device_attr_list_free(&groups_list[i].attr_list);
	} while (i--);
	return err;
}

struct counter_size_unit {
	size_t size;
};

static ssize_t counter_device_attr_size_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	const struct counter_size_unit *const comp = to_counter_attr(attr)->component;

	return sprintf(buf, "%zu\n", comp->size);
}

static int counter_size_attribute_create(
	struct counter_device_attr_group *const group,
	const size_t size, const char *const name)
{
	struct counter_size_unit *size_comp;
	struct counter_attr_parm parm;
	int err;

	/* Allocate size attribute component */
	size_comp = kmalloc(sizeof(*size_comp), GFP_KERNEL);
	if (!size_comp)
		return -ENOMEM;
	size_comp->size = size;

	parm.group = group;
	parm.prefix = "";
	parm.name = name;
	parm.show = counter_device_attr_size_show;
	parm.store = NULL;
	parm.component = size_comp;
	err = counter_attribute_create(&parm);
	if (err)
		goto err_free_size_comp;

	return 0;

err_free_size_comp:
	kfree(size_comp);
	return err;
}

struct counter_ext_unit {
	const struct counter_device_ext *ext;
};

static ssize_t counter_device_ext_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_ext_unit *const component = devattr->component;
	const struct counter_device_ext *const ext = component->ext;

	return ext->read(dev_get_drvdata(dev), ext->priv, buf);
}

static ssize_t counter_device_ext_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	const struct counter_device_attr *const devattr = to_counter_attr(attr);
	const struct counter_ext_unit *const component = devattr->component;
	const struct counter_device_ext *const ext = component->ext;

	return ext->write(dev_get_drvdata(dev), ext->priv, buf, len);
}

static int counter_device_ext_register(
	struct counter_device_attr_group *const group,
	struct counter_device *const counter)
{
	size_t i;
	struct counter_ext_unit *ext_comp;
	struct counter_attr_parm parm;
	int err;

	/* Create an attribute for each extension */
	for (i = 0 ; i < counter->num_ext; i++) {
		/* Allocate extension attribute component */
		ext_comp = kmalloc(sizeof(*ext_comp), GFP_KERNEL);
		if (!ext_comp) {
			err = -ENOMEM;
			goto err_free_attr_list;
		}

		ext_comp->ext = counter->ext + i;

		/* Allocate extension attribute */
		parm.group = group;
		parm.prefix = "";
		parm.name = counter->ext[i].name;
		parm.show = (counter->ext[i].read) ? counter_device_ext_show : NULL;
		parm.store = (counter->ext[i].write) ? counter_device_ext_store : NULL;
		parm.component = ext_comp;
		err = counter_attribute_create(&parm);
		if (err) {
			kfree(ext_comp);
			goto err_free_attr_list;
		}
	}

	return 0;

err_free_attr_list:
	counter_device_attr_list_free(&group->attr_list);
	return err;
}

static int counter_global_attr_register(
	struct counter_device_attr_group *const group,
	struct counter_device *const counter)
{
	int err;

	/* Create name attribute */
	err = counter_name_attribute_create(group, counter->name);
	if (err)
		return err;

	/* Create num_counts attribute */
	err = counter_size_attribute_create(group, counter->num_counts,
					    "num_counts");
	if (err)
		goto err_free_attr_list;

	/* Create num_signals attribute */
	err = counter_size_attribute_create(group, counter->num_signals,
					    "num_signals");
	if (err)
		goto err_free_attr_list;

	/* Register Counter device extension attributes */
	err = counter_device_ext_register(group, counter);
	if (err)
		goto err_free_attr_list;

	return 0;

err_free_attr_list:
	counter_device_attr_list_free(&group->attr_list);
	return err;
}

static void counter_device_groups_list_free(
	struct counter_device_attr_group *const groups_list,
	const size_t num_groups)
{
	struct counter_device_attr_group *group;
	size_t i;

	/* loop through all attribute groups (signals, counts, global, etc.) */
	for (i = 0; i < num_groups; i++) {
		group = groups_list + i;

		/* free all attribute group and associated attributes memory */
		kfree(group->attr_group.name);
		kfree(group->attr_group.attrs);
		counter_device_attr_list_free(&group->attr_list);
	}

	kfree(groups_list);
}

static int counter_device_groups_list_prepare(
	struct counter_device *const counter)
{
	const size_t total_num_groups =
		counter->num_signals + counter->num_counts + 1;
	struct counter_device_attr_group *groups_list;
	size_t i;
	int err;
	size_t num_groups = 0;

	/* Allocate space for attribute groups (signals, counts, and ext) */
	groups_list = kcalloc(total_num_groups, sizeof(*groups_list),
			      GFP_KERNEL);
	if (!groups_list)
		return -ENOMEM;

	/* Initialize attribute lists */
	for (i = 0; i < total_num_groups; i++)
		INIT_LIST_HEAD(&groups_list[i].attr_list);

	/* Register Signals */
	err = counter_signals_register(groups_list, counter);
	if (err)
		goto err_free_groups_list;
	num_groups += counter->num_signals;

	/* Register Counts and respective Synapses */
	err = counter_counts_register(groups_list + num_groups, counter);
	if (err)
		goto err_free_groups_list;
	num_groups += counter->num_counts;

	/* Register Counter global attributes */
	err = counter_global_attr_register(groups_list + num_groups, counter);
	if (err)
		goto err_free_groups_list;
	num_groups++;

	/* Store groups_list in device_state */
	counter->device_state->groups_list = groups_list;
	counter->device_state->num_groups = num_groups;

	return 0;

err_free_groups_list:
	counter_device_groups_list_free(groups_list, num_groups);
	return err;
}

static int counter_device_groups_prepare(
	struct counter_device_state *const device_state)
{
	size_t i, j;
	struct counter_device_attr_group *group;
	int err;
	struct counter_device_attr *p;

	/* Allocate attribute groups for association with device */
	device_state->groups = kcalloc(device_state->num_groups + 1,
				       sizeof(*device_state->groups),
				       GFP_KERNEL);
	if (!device_state->groups)
		return -ENOMEM;

	/* Prepare each group of attributes for association */
	for (i = 0; i < device_state->num_groups; i++) {
		group = device_state->groups_list + i;

		/* Allocate space for attribute pointers in attribute group */
		group->attr_group.attrs = kcalloc(group->num_attr + 1,
			sizeof(*group->attr_group.attrs), GFP_KERNEL);
		if (!group->attr_group.attrs) {
			err = -ENOMEM;
			goto err_free_groups;
		}

		/* Add attribute pointers to attribute group */
		j = 0;
		list_for_each_entry(p, &group->attr_list, l)
			group->attr_group.attrs[j++] = &p->dev_attr.attr;

		/* Group attributes in attribute group */
		device_state->groups[i] = &group->attr_group;
	}
	/* Associate attributes with device */
	device_state->dev.groups = device_state->groups;

	return 0;

err_free_groups:
	do {
		group = device_state->groups_list + i;
		kfree(group->attr_group.attrs);
		group->attr_group.attrs = NULL;
	} while (i--);
	kfree(device_state->groups);
	return err;
}

/* Provides a unique ID for each counter device */
static DEFINE_IDA(counter_ida);

static void counter_device_release(struct device *dev)
{
	struct counter_device *const counter = dev_get_drvdata(dev);
	struct counter_device_state *const device_state = counter->device_state;

	kfree(device_state->groups);
	counter_device_groups_list_free(device_state->groups_list,
					device_state->num_groups);
	ida_simple_remove(&counter_ida, device_state->id);
	kfree(device_state);
}

static struct device_type counter_device_type = {
	.name = "counter_device",
	.release = counter_device_release
};

static struct bus_type counter_bus_type = {
	.name = "counter"
};

/**
 * counter_register - register Counter to the system
 * @counter:	pointer to Counter to register
 *
 * This function registers a Counter to the system. A sysfs "counter" directory
 * will be created and populated with sysfs attributes correlating with the
 * Counter Signals, Synapses, and Counts respectively.
 */
int counter_register(struct counter_device *const counter)
{
	struct counter_device_state *device_state;
	int err;

	/* Allocate internal state container for Counter device */
	device_state = kzalloc(sizeof(*device_state), GFP_KERNEL);
	if (!device_state)
		return -ENOMEM;
	counter->device_state = device_state;

	/* Acquire unique ID */
	device_state->id = ida_simple_get(&counter_ida, 0, 0, GFP_KERNEL);
	if (device_state->id < 0) {
		err = device_state->id;
		goto err_free_device_state;
	}

	/* Configure device structure for Counter */
	device_state->dev.type = &counter_device_type;
	device_state->dev.bus = &counter_bus_type;
	if (counter->parent) {
		device_state->dev.parent = counter->parent;
		device_state->dev.of_node = counter->parent->of_node;
	}
	dev_set_name(&device_state->dev, "counter%d", device_state->id);
	device_initialize(&device_state->dev);
	dev_set_drvdata(&device_state->dev, counter);

	/* Prepare device attributes */
	err = counter_device_groups_list_prepare(counter);
	if (err)
		goto err_free_id;

	/* Organize device attributes to groups and match to device */
	err = counter_device_groups_prepare(device_state);
	if (err)
		goto err_free_groups_list;

	/* Add device to system */
	err = device_add(&device_state->dev);
	if (err)
		goto err_free_groups;

	return 0;

err_free_groups:
	kfree(device_state->groups);
err_free_groups_list:
	counter_device_groups_list_free(device_state->groups_list,
					device_state->num_groups);
err_free_id:
	ida_simple_remove(&counter_ida, device_state->id);
err_free_device_state:
	kfree(device_state);
	return err;
}
EXPORT_SYMBOL_GPL(counter_register);

/**
 * counter_unregister - unregister Counter from the system
 * @counter:	pointer to Counter to unregister
 *
 * The Counter is unregistered from the system; all allocated memory is freed.
 */
void counter_unregister(struct counter_device *const counter)
{
	if (counter)
		device_del(&counter->device_state->dev);
}
EXPORT_SYMBOL_GPL(counter_unregister);

static void devm_counter_unreg(struct device *dev, void *res)
{
	counter_unregister(*(struct counter_device **)res);
}

/**
 * devm_counter_register - Resource-managed counter_register
 * @dev:	device to allocate counter_device for
 * @counter:	pointer to Counter to register
 *
 * Managed counter_register. The Counter registered with this function is
 * automatically unregistered on driver detach. This function calls
 * counter_register internally. Refer to that function for more information.
 *
 * If an Counter registered with this function needs to be unregistered
 * separately, devm_counter_unregister must be used.
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int devm_counter_register(struct device *dev,
			  struct counter_device *const counter)
{
	struct counter_device **ptr;
	int ret;

	ptr = devres_alloc(devm_counter_unreg, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = counter_register(counter);
	if (!ret) {
		*ptr = counter;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_counter_register);

static int devm_counter_match(struct device *dev, void *res, void *data)
{
	struct counter_device **r = res;

	if (!r || !*r) {
		WARN_ON(!r || !*r);
		return 0;
	}

	return *r == data;
}

/**
 * devm_counter_unregister - Resource-managed counter_unregister
 * @dev:	device this counter_device belongs to
 * @counter:	pointer to Counter associated with the device
 *
 * Unregister Counter registered with devm_counter_register.
 */
void devm_counter_unregister(struct device *dev,
			     struct counter_device *const counter)
{
	int rc;

	rc = devres_release(dev, devm_counter_unreg, devm_counter_match,
			    counter);
	WARN_ON(rc);
}
EXPORT_SYMBOL_GPL(devm_counter_unregister);

static int __init counter_init(void)
{
	return bus_register(&counter_bus_type);
}

static void __exit counter_exit(void)
{
	bus_unregister(&counter_bus_type);
}

subsys_initcall(counter_init);
module_exit(counter_exit);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("Generic Counter interface");
MODULE_LICENSE("GPL v2");
