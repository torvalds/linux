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

struct swnode {
	int id;
	struct kobject kobj;
	struct fwnode_handle fwnode;
	const struct software_node *node;

	/* hierarchy */
	struct ida child_ids;
	struct list_head entry;
	struct list_head children;
	struct swnode *parent;

	unsigned int allocated:1;
};

static DEFINE_IDA(swnode_root_ids);
static struct kset *swnode_kset;

#define kobj_to_swnode(_kobj_) container_of(_kobj_, struct swnode, kobj)

static const struct fwnode_operations software_node_ops;

bool is_software_node(const struct fwnode_handle *fwnode)
{
	return !IS_ERR_OR_NULL(fwnode) && fwnode->ops == &software_node_ops;
}
EXPORT_SYMBOL_GPL(is_software_node);

#define to_swnode(__fwnode)						\
	({								\
		typeof(__fwnode) __to_swnode_fwnode = __fwnode;		\
									\
		is_software_node(__to_swnode_fwnode) ?			\
			container_of(__to_swnode_fwnode,		\
				     struct swnode, fwnode) : NULL;	\
	})

static struct swnode *
software_node_to_swnode(const struct software_node *node)
{
	struct swnode *swnode = NULL;
	struct kobject *k;

	if (!node)
		return NULL;

	spin_lock(&swnode_kset->list_lock);

	list_for_each_entry(k, &swnode_kset->list, entry) {
		swnode = kobj_to_swnode(k);
		if (swnode->node == node)
			break;
		swnode = NULL;
	}

	spin_unlock(&swnode_kset->list_lock);

	return swnode;
}

const struct software_node *to_software_node(struct fwnode_handle *fwnode)
{
	struct swnode *swnode = to_swnode(fwnode);

	return swnode ? swnode->node : NULL;
}
EXPORT_SYMBOL_GPL(to_software_node);

struct fwnode_handle *software_node_fwnode(const struct software_node *node)
{
	struct swnode *swnode = software_node_to_swnode(node);

	return swnode ? &swnode->fwnode : NULL;
}
EXPORT_SYMBOL_GPL(software_node_fwnode);

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

	if (!properties)
		return NULL;

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
	struct swnode *swnode = to_swnode(fwnode);

	kobject_get(&swnode->kobj);

	return &swnode->fwnode;
}

static void software_node_put(struct fwnode_handle *fwnode)
{
	struct swnode *swnode = to_swnode(fwnode);

	kobject_put(&swnode->kobj);
}

static bool software_node_property_present(const struct fwnode_handle *fwnode,
					   const char *propname)
{
	struct swnode *swnode = to_swnode(fwnode);

	return !!property_entry_get(swnode->node->properties, propname);
}

static int software_node_read_int_array(const struct fwnode_handle *fwnode,
					const char *propname,
					unsigned int elem_size, void *val,
					size_t nval)
{
	struct swnode *swnode = to_swnode(fwnode);

	return property_entry_read_int_array(swnode->node->properties, propname,
					     elem_size, val, nval);
}

static int software_node_read_string_array(const struct fwnode_handle *fwnode,
					   const char *propname,
					   const char **val, size_t nval)
{
	struct swnode *swnode = to_swnode(fwnode);

	return property_entry_read_string_array(swnode->node->properties,
						propname, val, nval);
}

static struct fwnode_handle *
software_node_get_parent(const struct fwnode_handle *fwnode)
{
	struct swnode *swnode = to_swnode(fwnode);

	return swnode ? (swnode->parent ? &swnode->parent->fwnode : NULL) : NULL;
}

static struct fwnode_handle *
software_node_get_next_child(const struct fwnode_handle *fwnode,
			     struct fwnode_handle *child)
{
	struct swnode *p = to_swnode(fwnode);
	struct swnode *c = to_swnode(child);

	if (!p || list_empty(&p->children) ||
	    (c && list_is_last(&c->entry, &p->children)))
		return NULL;

	if (c)
		c = list_next_entry(c, entry);
	else
		c = list_first_entry(&p->children, struct swnode, entry);
	return &c->fwnode;
}

static struct fwnode_handle *
software_node_get_named_child_node(const struct fwnode_handle *fwnode,
				   const char *childname)
{
	struct swnode *swnode = to_swnode(fwnode);
	struct swnode *child;

	if (!swnode || list_empty(&swnode->children))
		return NULL;

	list_for_each_entry(child, &swnode->children, entry) {
		if (!strcmp(childname, kobject_name(&child->kobj))) {
			kobject_get(&child->kobj);
			return &child->fwnode;
		}
	}
	return NULL;
}

static int
software_node_get_reference_args(const struct fwnode_handle *fwnode,
				 const char *propname, const char *nargs_prop,
				 unsigned int nargs, unsigned int index,
				 struct fwnode_reference_args *args)
{
	struct swnode *swnode = to_swnode(fwnode);
	const struct software_node_reference *ref;
	const struct property_entry *prop;
	struct fwnode_handle *refnode;
	int i;

	if (!swnode || !swnode->node->references)
		return -ENOENT;

	for (ref = swnode->node->references; ref->name; ref++)
		if (!strcmp(ref->name, propname))
			break;

	if (!ref->name || index > (ref->nrefs - 1))
		return -ENOENT;

	refnode = software_node_fwnode(ref->refs[index].node);
	if (!refnode)
		return -ENOENT;

	if (nargs_prop) {
		prop = property_entry_get(swnode->node->properties, nargs_prop);
		if (!prop)
			return -EINVAL;

		nargs = prop->value.u32_data;
	}

	if (nargs > NR_FWNODE_REFERENCE_ARGS)
		return -EINVAL;

	args->fwnode = software_node_get(refnode);
	args->nargs = nargs;

	for (i = 0; i < nargs; i++)
		args->args[i] = ref->refs[index].args[i];

	return 0;
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
	.get_reference_args = software_node_get_reference_args
};

/* -------------------------------------------------------------------------- */

/**
 * software_node_find_by_name - Find software node by name
 * @parent: Parent of the software node
 * @name: Name of the software node
 *
 * The function will find a node that is child of @parent and that is named
 * @name. If no node is found, the function returns NULL.
 *
 * NOTE: you will need to drop the reference with fwnode_handle_put() after use.
 */
const struct software_node *
software_node_find_by_name(const struct software_node *parent, const char *name)
{
	struct swnode *swnode = NULL;
	struct kobject *k;

	if (!name)
		return NULL;

	spin_lock(&swnode_kset->list_lock);

	list_for_each_entry(k, &swnode_kset->list, entry) {
		swnode = kobj_to_swnode(k);
		if (parent == swnode->node->parent && swnode->node->name &&
		    !strcmp(name, swnode->node->name)) {
			kobject_get(&swnode->kobj);
			break;
		}
		swnode = NULL;
	}

	spin_unlock(&swnode_kset->list_lock);

	return swnode ? swnode->node : NULL;
}
EXPORT_SYMBOL_GPL(software_node_find_by_name);

static int
software_node_register_properties(struct software_node *node,
				  const struct property_entry *properties)
{
	struct property_entry *props;

	props = property_entries_dup(properties);
	if (IS_ERR(props))
		return PTR_ERR(props);

	node->properties = props;

	return 0;
}

static void software_node_release(struct kobject *kobj)
{
	struct swnode *swnode = kobj_to_swnode(kobj);

	if (swnode->allocated) {
		property_entries_free(swnode->node->properties);
		kfree(swnode->node);
	}
	ida_destroy(&swnode->child_ids);
	kfree(swnode);
}

static struct kobj_type software_node_type = {
	.release = software_node_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

static struct fwnode_handle *
swnode_register(const struct software_node *node, struct swnode *parent,
		unsigned int allocated)
{
	struct swnode *swnode;
	int ret;

	swnode = kzalloc(sizeof(*swnode), GFP_KERNEL);
	if (!swnode) {
		ret = -ENOMEM;
		goto out_err;
	}

	ret = ida_simple_get(parent ? &parent->child_ids : &swnode_root_ids,
			     0, 0, GFP_KERNEL);
	if (ret < 0) {
		kfree(swnode);
		goto out_err;
	}

	swnode->id = ret;
	swnode->node = node;
	swnode->parent = parent;
	swnode->allocated = allocated;
	swnode->kobj.kset = swnode_kset;
	swnode->fwnode.ops = &software_node_ops;

	ida_init(&swnode->child_ids);
	INIT_LIST_HEAD(&swnode->entry);
	INIT_LIST_HEAD(&swnode->children);

	if (node->name)
		ret = kobject_init_and_add(&swnode->kobj, &software_node_type,
					   parent ? &parent->kobj : NULL,
					   "%s", node->name);
	else
		ret = kobject_init_and_add(&swnode->kobj, &software_node_type,
					   parent ? &parent->kobj : NULL,
					   "node%d", swnode->id);
	if (ret) {
		kobject_put(&swnode->kobj);
		return ERR_PTR(ret);
	}

	if (parent)
		list_add_tail(&swnode->entry, &parent->children);

	kobject_uevent(&swnode->kobj, KOBJ_ADD);
	return &swnode->fwnode;

out_err:
	if (allocated)
		property_entries_free(node->properties);
	return ERR_PTR(ret);
}

/**
 * software_node_register_nodes - Register an array of software nodes
 * @nodes: Zero terminated array of software nodes to be registered
 *
 * Register multiple software nodes at once.
 */
int software_node_register_nodes(const struct software_node *nodes)
{
	int ret;
	int i;

	for (i = 0; nodes[i].name; i++) {
		ret = software_node_register(&nodes[i]);
		if (ret) {
			software_node_unregister_nodes(nodes);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(software_node_register_nodes);

/**
 * software_node_unregister_nodes - Unregister an array of software nodes
 * @nodes: Zero terminated array of software nodes to be unregistered
 *
 * Unregister multiple software nodes at once.
 */
void software_node_unregister_nodes(const struct software_node *nodes)
{
	struct swnode *swnode;
	int i;

	for (i = 0; nodes[i].name; i++) {
		swnode = software_node_to_swnode(&nodes[i]);
		if (swnode)
			fwnode_remove_software_node(&swnode->fwnode);
	}
}
EXPORT_SYMBOL_GPL(software_node_unregister_nodes);

/**
 * software_node_register - Register static software node
 * @node: The software node to be registered
 */
int software_node_register(const struct software_node *node)
{
	struct swnode *parent = software_node_to_swnode(node->parent);

	if (software_node_to_swnode(node))
		return -EEXIST;

	return PTR_ERR_OR_ZERO(swnode_register(node, parent, 0));
}
EXPORT_SYMBOL_GPL(software_node_register);

struct fwnode_handle *
fwnode_create_software_node(const struct property_entry *properties,
			    const struct fwnode_handle *parent)
{
	struct software_node *node;
	struct swnode *p = NULL;
	int ret;

	if (parent) {
		if (IS_ERR(parent))
			return ERR_CAST(parent);
		if (!is_software_node(parent))
			return ERR_PTR(-EINVAL);
		p = to_swnode(parent);
	}

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return ERR_PTR(-ENOMEM);

	ret = software_node_register_properties(node, properties);
	if (ret) {
		kfree(node);
		return ERR_PTR(ret);
	}

	node->parent = p ? p->node : NULL;

	return swnode_register(node, p, 1);
}
EXPORT_SYMBOL_GPL(fwnode_create_software_node);

void fwnode_remove_software_node(struct fwnode_handle *fwnode)
{
	struct swnode *swnode = to_swnode(fwnode);

	if (!swnode)
		return;

	if (swnode->parent) {
		ida_simple_remove(&swnode->parent->child_ids, swnode->id);
		list_del(&swnode->entry);
	} else {
		ida_simple_remove(&swnode_root_ids, swnode->id);
	}

	kobject_put(&swnode->kobj);
}
EXPORT_SYMBOL_GPL(fwnode_remove_software_node);

int software_node_notify(struct device *dev, unsigned long action)
{
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct swnode *swnode;
	int ret;

	if (!fwnode)
		return 0;

	if (!is_software_node(fwnode))
		fwnode = fwnode->secondary;
	if (!is_software_node(fwnode))
		return 0;

	swnode = to_swnode(fwnode);

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
