// SPDX-License-Identifier: GPL-2.0
/*
 * Software nodes for the firmware node framework.
 *
 * Copyright (C) 2018, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/property.h>
#include <linux/slab.h>

struct software_node {
	int id;
	struct kobject kobj;
	struct fwnode_handle fwnode;

	/* hierarchy */
	struct ida child_ids;
	struct list_head entry;
	struct list_head children;
	struct software_node *parent;

	/* properties */
	const struct property_entry *properties;
};

static DEFINE_IDA(swnode_root_ids);
static struct kset *swnode_kset;

#define kobj_to_swnode(_kobj_) container_of(_kobj_, struct software_node, kobj)

static const struct fwnode_operations software_node_ops;

bool is_software_node(const struct fwnode_handle *fwnode)
{
	return !IS_ERR_OR_NULL(fwnode) && fwnode->ops == &software_node_ops;
}

#define to_software_node(__fwnode)					\
	({								\
		typeof(__fwnode) __to_software_node_fwnode = __fwnode;	\
									\
		is_software_node(__to_software_node_fwnode) ?		\
			container_of(__to_software_node_fwnode,		\
				     struct software_node, fwnode) :	\
			NULL;						\
	})

/* -------------------------------------------------------------------------- */
/* property_entry processing */

static const struct property_entry *
property_entry_get(const struct property_entry *prop, const char *name)
{
	if (!prop)
		return NULL;

	for (; prop->name; prop++)
		if (!strcmp(name, prop->name))
			return prop;

	return NULL;
}

static void
property_set_pointer(struct property_entry *prop, const void *pointer)
{
	switch (prop->type) {
	case DEV_PROP_U8:
		if (prop->is_array)
			prop->pointer.u8_data = pointer;
		else
			prop->value.u8_data = *((u8 *)pointer);
		break;
	case DEV_PROP_U16:
		if (prop->is_array)
			prop->pointer.u16_data = pointer;
		else
			prop->value.u16_data = *((u16 *)pointer);
		break;
	case DEV_PROP_U32:
		if (prop->is_array)
			prop->pointer.u32_data = pointer;
		else
			prop->value.u32_data = *((u32 *)pointer);
		break;
	case DEV_PROP_U64:
		if (prop->is_array)
			prop->pointer.u64_data = pointer;
		else
			prop->value.u64_data = *((u64 *)pointer);
		break;
	case DEV_PROP_STRING:
		if (prop->is_array)
			prop->pointer.str = pointer;
		else
			prop->value.str = pointer;
		break;
	default:
		break;
	}
}

static const void *property_get_pointer(const struct property_entry *prop)
{
	switch (prop->type) {
	case DEV_PROP_U8:
		if (prop->is_array)
			return prop->pointer.u8_data;
		return &prop->value.u8_data;
	case DEV_PROP_U16:
		if (prop->is_array)
			return prop->pointer.u16_data;
		return &prop->value.u16_data;
	case DEV_PROP_U32:
		if (prop->is_array)
			return prop->pointer.u32_data;
		return &prop->value.u32_data;
	case DEV_PROP_U64:
		if (prop->is_array)
			return prop->pointer.u64_data;
		return &prop->value.u64_data;
	case DEV_PROP_STRING:
		if (prop->is_array)
			return prop->pointer.str;
		return &prop->value.str;
	default:
		return NULL;
	}
}

static const void *property_entry_find(const struct property_entry *props,
				       const char *propname, size_t length)
{
	const struct property_entry *prop;
	const void *pointer;

	prop = property_entry_get(props, propname);
	if (!prop)
		return ERR_PTR(-EINVAL);
	pointer = property_get_pointer(prop);
	if (!pointer)
		return ERR_PTR(-ENODATA);
	if (length > prop->length)
		return ERR_PTR(-EOVERFLOW);
	return pointer;
}

static int property_entry_read_u8_array(const struct property_entry *props,
					const char *propname,
					u8 *values, size_t nval)
{
	const void *pointer;
	size_t length = nval * sizeof(*values);

	pointer = property_entry_find(props, propname, length);
	if (IS_ERR(pointer))
		return PTR_ERR(pointer);

	memcpy(values, pointer, length);
	return 0;
}

static int property_entry_read_u16_array(const struct property_entry *props,
					 const char *propname,
					 u16 *values, size_t nval)
{
	const void *pointer;
	size_t length = nval * sizeof(*values);

	pointer = property_entry_find(props, propname, length);
	if (IS_ERR(pointer))
		return PTR_ERR(pointer);

	memcpy(values, pointer, length);
	return 0;
}

static int property_entry_read_u32_array(const struct property_entry *props,
					 const char *propname,
					 u32 *values, size_t nval)
{
	const void *pointer;
	size_t length = nval * sizeof(*values);

	pointer = property_entry_find(props, propname, length);
	if (IS_ERR(pointer))
		return PTR_ERR(pointer);

	memcpy(values, pointer, length);
	return 0;
}

static int property_entry_read_u64_array(const struct property_entry *props,
					 const char *propname,
					 u64 *values, size_t nval)
{
	const void *pointer;
	size_t length = nval * sizeof(*values);

	pointer = property_entry_find(props, propname, length);
	if (IS_ERR(pointer))
		return PTR_ERR(pointer);

	memcpy(values, pointer, length);
	return 0;
}

static int
property_entry_count_elems_of_size(const struct property_entry *props,
				   const char *propname, size_t length)
{
	const struct property_entry *prop;

	prop = property_entry_get(props, propname);
	if (!prop)
		return -EINVAL;

	return prop->length / length;
}

static int property_entry_read_int_array(const struct property_entry *props,
					 const char *name,
					 unsigned int elem_size, void *val,
					 size_t nval)
{
	if (!val)
		return property_entry_count_elems_of_size(props, name,
							  elem_size);
	switch (elem_size) {
	case sizeof(u8):
		return property_entry_read_u8_array(props, name, val, nval);
	case sizeof(u16):
		return property_entry_read_u16_array(props, name, val, nval);
	case sizeof(u32):
		return property_entry_read_u32_array(props, name, val, nval);
	case sizeof(u64):
		return property_entry_read_u64_array(props, name, val, nval);
	}

	return -ENXIO;
}

static int property_entry_read_string_array(const struct property_entry *props,
					    const char *propname,
					    const char **strings, size_t nval)
{
	const struct property_entry *prop;
	const void *pointer;
	size_t array_len, length;

	/* Find out the array length. */
	prop = property_entry_get(props, propname);
	if (!prop)
		return -EINVAL;

	if (prop->is_array)
		/* Find the length of an array. */
		array_len = property_entry_count_elems_of_size(props, propname,
							  sizeof(const char *));
	else
		/* The array length for a non-array string property is 1. */
		array_len = 1;

	/* Return how many there are if strings is NULL. */
	if (!strings)
		return array_len;

	array_len = min(nval, array_len);
	length = array_len * sizeof(*strings);

	pointer = property_entry_find(props, propname, length);
	if (IS_ERR(pointer))
		return PTR_ERR(pointer);

	memcpy(strings, pointer, length);

	return array_len;
}

static void property_entry_free_data(const struct property_entry *p)
{
	const void *pointer = property_get_pointer(p);
	size_t i, nval;

	if (p->is_array) {
		if (p->type == DEV_PROP_STRING && p->pointer.str) {
			nval = p->length / sizeof(const char *);
			for (i = 0; i < nval; i++)
				kfree(p->pointer.str[i]);
		}
		kfree(pointer);
	} else if (p->type == DEV_PROP_STRING) {
		kfree(p->value.str);
	}
	kfree(p->name);
}

static int property_copy_string_array(struct property_entry *dst,
				      const struct property_entry *src)
{
	const char **d;
	size_t nval = src->length / sizeof(*d);
	int i;

	d = kcalloc(nval, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	for (i = 0; i < nval; i++) {
		d[i] = kstrdup(src->pointer.str[i], GFP_KERNEL);
		if (!d[i] && src->pointer.str[i]) {
			while (--i >= 0)
				kfree(d[i]);
			kfree(d);
			return -ENOMEM;
		}
	}

	dst->pointer.str = d;
	return 0;
}

static int property_entry_copy_data(struct property_entry *dst,
				    const struct property_entry *src)
{
	const void *pointer = property_get_pointer(src);
	const void *new;
	int error;

	if (src->is_array) {
		if (!src->length)
			return -ENODATA;

		if (src->type == DEV_PROP_STRING) {
			error = property_copy_string_array(dst, src);
			if (error)
				return error;
			new = dst->pointer.str;
		} else {
			new = kmemdup(pointer, src->length, GFP_KERNEL);
			if (!new)
				return -ENOMEM;
		}
	} else if (src->type == DEV_PROP_STRING) {
		new = kstrdup(src->value.str, GFP_KERNEL);
		if (!new && src->value.str)
			return -ENOMEM;
	} else {
		new = pointer;
	}

	dst->length = src->length;
	dst->is_array = src->is_array;
	dst->type = src->type;

	property_set_pointer(dst, new);

	dst->name = kstrdup(src->name, GFP_KERNEL);
	if (!dst->name)
		goto out_free_data;

	return 0;

out_free_data:
	property_entry_free_data(dst);
	return -ENOMEM;
}

/**
 * property_entries_dup - duplicate array of properties
 * @properties: array of properties to copy
 *
 * This function creates a deep copy of the given NULL-terminated array
 * of property entries.
 */
struct property_entry *
property_entries_dup(const struct property_entry *properties)
{
	struct property_entry *p;
	int i, n = 0;
	int ret;

	while (properties[n].name)
		n++;

	p = kcalloc(n + 1, sizeof(*p), GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < n; i++) {
		ret = property_entry_copy_data(&p[i], &properties[i]);
		if (ret) {
			while (--i >= 0)
				property_entry_free_data(&p[i]);
			kfree(p);
			return ERR_PTR(ret);
		}
	}

	return p;
}
EXPORT_SYMBOL_GPL(property_entries_dup);

/**
 * property_entries_free - free previously allocated array of properties
 * @properties: array of properties to destroy
 *
 * This function frees given NULL-terminated array of property entries,
 * along with their data.
 */
void property_entries_free(const struct property_entry *properties)
{
	const struct property_entry *p;

	if (!properties)
		return;

	for (p = properties; p->name; p++)
		property_entry_free_data(p);

	kfree(properties);
}
EXPORT_SYMBOL_GPL(property_entries_free);

/* -------------------------------------------------------------------------- */
/* fwnode operations */

static struct fwnode_handle *software_node_get(struct fwnode_handle *fwnode)
{
	struct software_node *swnode = to_software_node(fwnode);

	kobject_get(&swnode->kobj);

	return &swnode->fwnode;
}

static void software_node_put(struct fwnode_handle *fwnode)
{
	struct software_node *swnode = to_software_node(fwnode);

	kobject_put(&swnode->kobj);
}

static bool software_node_property_present(const struct fwnode_handle *fwnode,
					   const char *propname)
{
	return !!property_entry_get(to_software_node(fwnode)->properties,
				    propname);
}

static int software_node_read_int_array(const struct fwnode_handle *fwnode,
					const char *propname,
					unsigned int elem_size, void *val,
					size_t nval)
{
	struct software_node *swnode = to_software_node(fwnode);

	return property_entry_read_int_array(swnode->properties, propname,
					     elem_size, val, nval);
}

static int software_node_read_string_array(const struct fwnode_handle *fwnode,
					   const char *propname,
					   const char **val, size_t nval)
{
	struct software_node *swnode = to_software_node(fwnode);

	return property_entry_read_string_array(swnode->properties, propname,
						val, nval);
}

static struct fwnode_handle *
software_node_get_parent(const struct fwnode_handle *fwnode)
{
	struct software_node *swnode = to_software_node(fwnode);

	return swnode ? (swnode->parent ? &swnode->parent->fwnode : NULL) :
			NULL;
}

static struct fwnode_handle *
software_node_get_next_child(const struct fwnode_handle *fwnode,
			     struct fwnode_handle *child)
{
	struct software_node *p = to_software_node(fwnode);
	struct software_node *c = to_software_node(child);

	if (!p || list_empty(&p->children) ||
	    (c && list_is_last(&c->entry, &p->children)))
		return NULL;

	if (c)
		c = list_next_entry(c, entry);
	else
		c = list_first_entry(&p->children, struct software_node, entry);
	return &c->fwnode;
}

static struct fwnode_handle *
software_node_get_named_child_node(const struct fwnode_handle *fwnode,
				   const char *childname)
{
	struct software_node *swnode = to_software_node(fwnode);
	const struct property_entry *prop;
	struct software_node *child;

	if (!swnode || list_empty(&swnode->children))
		return NULL;

	list_for_each_entry(child, &swnode->children, entry) {
		prop = property_entry_get(child->properties, "name");
		if (!prop)
			continue;
		if (!strcmp(childname, prop->value.str)) {
			kobject_get(&child->kobj);
			return &child->fwnode;
		}
	}
	return NULL;
}

static const struct fwnode_operations software_node_ops = {
	.get = software_node_get,
	.put = software_node_put,
	.property_present = software_node_property_present,
	.property_read_int_array = software_node_read_int_array,
	.property_read_string_array = software_node_read_string_array,
	.get_parent = software_node_get_parent,
	.get_next_child_node = software_node_get_next_child,
	.get_named_child_node = software_node_get_named_child_node,
};

/* -------------------------------------------------------------------------- */

static int
software_node_register_properties(struct software_node *swnode,
				  const struct property_entry *properties)
{
	struct property_entry *props;

	props = property_entries_dup(properties);
	if (IS_ERR(props))
		return PTR_ERR(props);

	swnode->properties = props;

	return 0;
}

static void software_node_release(struct kobject *kobj)
{
	struct software_node *swnode = kobj_to_swnode(kobj);

	if (swnode->parent) {
		ida_simple_remove(&swnode->parent->child_ids, swnode->id);
		list_del(&swnode->entry);
	} else {
		ida_simple_remove(&swnode_root_ids, swnode->id);
	}

	ida_destroy(&swnode->child_ids);
	property_entries_free(swnode->properties);
	kfree(swnode);
}

static struct kobj_type software_node_type = {
	.release = software_node_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

struct fwnode_handle *
fwnode_create_software_node(const struct property_entry *properties,
			    const struct fwnode_handle *parent)
{
	struct software_node *p = NULL;
	struct software_node *swnode;
	int ret;

	if (parent) {
		if (IS_ERR(parent))
			return ERR_CAST(parent);
		if (!is_software_node(parent))
			return ERR_PTR(-EINVAL);
		p = to_software_node(parent);
	}

	swnode = kzalloc(sizeof(*swnode), GFP_KERNEL);
	if (!swnode)
		return ERR_PTR(-ENOMEM);

	ret = ida_simple_get(p ? &p->child_ids : &swnode_root_ids, 0, 0,
			     GFP_KERNEL);
	if (ret < 0) {
		kfree(swnode);
		return ERR_PTR(ret);
	}

	swnode->id = ret;
	swnode->kobj.kset = swnode_kset;
	swnode->fwnode.ops = &software_node_ops;

	ida_init(&swnode->child_ids);
	INIT_LIST_HEAD(&swnode->entry);
	INIT_LIST_HEAD(&swnode->children);
	swnode->parent = p;

	if (p)
		list_add_tail(&swnode->entry, &p->children);

	ret = kobject_init_and_add(&swnode->kobj, &software_node_type,
				   p ? &p->kobj : NULL, "node%d", swnode->id);
	if (ret) {
		kobject_put(&swnode->kobj);
		return ERR_PTR(ret);
	}

	ret = software_node_register_properties(swnode, properties);
	if (ret) {
		kobject_put(&swnode->kobj);
		return ERR_PTR(ret);
	}

	kobject_uevent(&swnode->kobj, KOBJ_ADD);
	return &swnode->fwnode;
}
EXPORT_SYMBOL_GPL(fwnode_create_software_node);

void fwnode_remove_software_node(struct fwnode_handle *fwnode)
{
	struct software_node *swnode = to_software_node(fwnode);

	if (!swnode)
		return;

	kobject_put(&swnode->kobj);
}
EXPORT_SYMBOL_GPL(fwnode_remove_software_node);

int software_node_notify(struct device *dev, unsigned long action)
{
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct software_node *swnode;
	int ret;

	if (!fwnode)
		return 0;

	if (!is_software_node(fwnode))
		fwnode = fwnode->secondary;
	if (!is_software_node(fwnode))
		return 0;

	swnode = to_software_node(fwnode);

	switch (action) {
	case KOBJ_ADD:
		ret = sysfs_create_link(&dev->kobj, &swnode->kobj,
					"software_node");
		if (ret)
			break;

		ret = sysfs_create_link(&swnode->kobj, &dev->kobj,
					dev_name(dev));
		if (ret) {
			sysfs_remove_link(&dev->kobj, "software_node");
			break;
		}
		kobject_get(&swnode->kobj);
		break;
	case KOBJ_REMOVE:
		sysfs_remove_link(&swnode->kobj, dev_name(dev));
		sysfs_remove_link(&dev->kobj, "software_node");
		kobject_put(&swnode->kobj);
		break;
	default:
		break;
	}

	return 0;
}

static int __init software_node_init(void)
{
	swnode_kset = kset_create_and_add("software_nodes", NULL, kernel_kobj);
	if (!swnode_kset)
		return -ENOMEM;
	return 0;
}
postcore_initcall(software_node_init);

static void __exit software_node_exit(void)
{
	ida_destroy(&swnode_root_ids);
	kset_unregister(swnode_kset);
}
__exitcall(software_node_exit);
