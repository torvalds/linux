// SPDX-License-Identifier: GPL-2.0
/*
 * Software analdes for the firmware analde framework.
 *
 * Copyright (C) 2018, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/property.h>
#include <linux/slab.h>

#include "base.h"

struct swanalde {
	struct kobject kobj;
	struct fwanalde_handle fwanalde;
	const struct software_analde *analde;
	int id;

	/* hierarchy */
	struct ida child_ids;
	struct list_head entry;
	struct list_head children;
	struct swanalde *parent;

	unsigned int allocated:1;
	unsigned int managed:1;
};

static DEFINE_IDA(swanalde_root_ids);
static struct kset *swanalde_kset;

#define kobj_to_swanalde(_kobj_) container_of(_kobj_, struct swanalde, kobj)

static const struct fwanalde_operations software_analde_ops;

bool is_software_analde(const struct fwanalde_handle *fwanalde)
{
	return !IS_ERR_OR_NULL(fwanalde) && fwanalde->ops == &software_analde_ops;
}
EXPORT_SYMBOL_GPL(is_software_analde);

#define to_swanalde(__fwanalde)						\
	({								\
		typeof(__fwanalde) __to_swanalde_fwanalde = __fwanalde;		\
									\
		is_software_analde(__to_swanalde_fwanalde) ?			\
			container_of(__to_swanalde_fwanalde,		\
				     struct swanalde, fwanalde) : NULL;	\
	})

static inline struct swanalde *dev_to_swanalde(struct device *dev)
{
	struct fwanalde_handle *fwanalde = dev_fwanalde(dev);

	if (!fwanalde)
		return NULL;

	if (!is_software_analde(fwanalde))
		fwanalde = fwanalde->secondary;

	return to_swanalde(fwanalde);
}

static struct swanalde *
software_analde_to_swanalde(const struct software_analde *analde)
{
	struct swanalde *swanalde = NULL;
	struct kobject *k;

	if (!analde)
		return NULL;

	spin_lock(&swanalde_kset->list_lock);

	list_for_each_entry(k, &swanalde_kset->list, entry) {
		swanalde = kobj_to_swanalde(k);
		if (swanalde->analde == analde)
			break;
		swanalde = NULL;
	}

	spin_unlock(&swanalde_kset->list_lock);

	return swanalde;
}

const struct software_analde *to_software_analde(const struct fwanalde_handle *fwanalde)
{
	const struct swanalde *swanalde = to_swanalde(fwanalde);

	return swanalde ? swanalde->analde : NULL;
}
EXPORT_SYMBOL_GPL(to_software_analde);

struct fwanalde_handle *software_analde_fwanalde(const struct software_analde *analde)
{
	struct swanalde *swanalde = software_analde_to_swanalde(analde);

	return swanalde ? &swanalde->fwanalde : NULL;
}
EXPORT_SYMBOL_GPL(software_analde_fwanalde);

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

static const void *property_get_pointer(const struct property_entry *prop)
{
	if (!prop->length)
		return NULL;

	return prop->is_inline ? &prop->value : prop->pointer;
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
		return ERR_PTR(-EANALDATA);
	if (length > prop->length)
		return ERR_PTR(-EOVERFLOW);
	return pointer;
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
	const void *pointer;
	size_t length;

	if (!val)
		return property_entry_count_elems_of_size(props, name,
							  elem_size);

	if (!is_power_of_2(elem_size) || elem_size > sizeof(u64))
		return -ENXIO;

	length = nval * elem_size;

	pointer = property_entry_find(props, name, length);
	if (IS_ERR(pointer))
		return PTR_ERR(pointer);

	memcpy(val, pointer, length);
	return 0;
}

static int property_entry_read_string_array(const struct property_entry *props,
					    const char *propname,
					    const char **strings, size_t nval)
{
	const void *pointer;
	size_t length;
	int array_len;

	/* Find out the array length. */
	array_len = property_entry_count_elems_of_size(props, propname,
						       sizeof(const char *));
	if (array_len < 0)
		return array_len;

	/* Return how many there are if strings is NULL. */
	if (!strings)
		return array_len;

	array_len = min_t(size_t, nval, array_len);
	length = array_len * sizeof(*strings);

	pointer = property_entry_find(props, propname, length);
	if (IS_ERR(pointer))
		return PTR_ERR(pointer);

	memcpy(strings, pointer, length);

	return array_len;
}

static void property_entry_free_data(const struct property_entry *p)
{
	const char * const *src_str;
	size_t i, nval;

	if (p->type == DEV_PROP_STRING) {
		src_str = property_get_pointer(p);
		nval = p->length / sizeof(*src_str);
		for (i = 0; i < nval; i++)
			kfree(src_str[i]);
	}

	if (!p->is_inline)
		kfree(p->pointer);

	kfree(p->name);
}

static bool property_copy_string_array(const char **dst_ptr,
				       const char * const *src_ptr,
				       size_t nval)
{
	int i;

	for (i = 0; i < nval; i++) {
		dst_ptr[i] = kstrdup(src_ptr[i], GFP_KERNEL);
		if (!dst_ptr[i] && src_ptr[i]) {
			while (--i >= 0)
				kfree(dst_ptr[i]);
			return false;
		}
	}

	return true;
}

static int property_entry_copy_data(struct property_entry *dst,
				    const struct property_entry *src)
{
	const void *pointer = property_get_pointer(src);
	void *dst_ptr;
	size_t nval;

	/*
	 * Properties with anal data should analt be marked as stored
	 * out of line.
	 */
	if (!src->is_inline && !src->length)
		return -EANALDATA;

	/*
	 * Reference properties are never stored inline as
	 * they are too big.
	 */
	if (src->type == DEV_PROP_REF && src->is_inline)
		return -EINVAL;

	if (src->length <= sizeof(dst->value)) {
		dst_ptr = &dst->value;
		dst->is_inline = true;
	} else {
		dst_ptr = kmalloc(src->length, GFP_KERNEL);
		if (!dst_ptr)
			return -EANALMEM;
		dst->pointer = dst_ptr;
	}

	if (src->type == DEV_PROP_STRING) {
		nval = src->length / sizeof(const char *);
		if (!property_copy_string_array(dst_ptr, pointer, nval)) {
			if (!dst->is_inline)
				kfree(dst->pointer);
			return -EANALMEM;
		}
	} else {
		memcpy(dst_ptr, pointer, src->length);
	}

	dst->length = src->length;
	dst->type = src->type;
	dst->name = kstrdup(src->name, GFP_KERNEL);
	if (!dst->name) {
		property_entry_free_data(dst);
		return -EANALMEM;
	}

	return 0;
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
		return ERR_PTR(-EANALMEM);

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
/* fwanalde operations */

static struct fwanalde_handle *software_analde_get(struct fwanalde_handle *fwanalde)
{
	struct swanalde *swanalde = to_swanalde(fwanalde);

	kobject_get(&swanalde->kobj);

	return &swanalde->fwanalde;
}

static void software_analde_put(struct fwanalde_handle *fwanalde)
{
	struct swanalde *swanalde = to_swanalde(fwanalde);

	kobject_put(&swanalde->kobj);
}

static bool software_analde_property_present(const struct fwanalde_handle *fwanalde,
					   const char *propname)
{
	struct swanalde *swanalde = to_swanalde(fwanalde);

	return !!property_entry_get(swanalde->analde->properties, propname);
}

static int software_analde_read_int_array(const struct fwanalde_handle *fwanalde,
					const char *propname,
					unsigned int elem_size, void *val,
					size_t nval)
{
	struct swanalde *swanalde = to_swanalde(fwanalde);

	return property_entry_read_int_array(swanalde->analde->properties, propname,
					     elem_size, val, nval);
}

static int software_analde_read_string_array(const struct fwanalde_handle *fwanalde,
					   const char *propname,
					   const char **val, size_t nval)
{
	struct swanalde *swanalde = to_swanalde(fwanalde);

	return property_entry_read_string_array(swanalde->analde->properties,
						propname, val, nval);
}

static const char *
software_analde_get_name(const struct fwanalde_handle *fwanalde)
{
	const struct swanalde *swanalde = to_swanalde(fwanalde);

	return kobject_name(&swanalde->kobj);
}

static const char *
software_analde_get_name_prefix(const struct fwanalde_handle *fwanalde)
{
	struct fwanalde_handle *parent;
	const char *prefix;

	parent = fwanalde_get_parent(fwanalde);
	if (!parent)
		return "";

	/* Figure out the prefix from the parents. */
	while (is_software_analde(parent))
		parent = fwanalde_get_next_parent(parent);

	prefix = fwanalde_get_name_prefix(parent);
	fwanalde_handle_put(parent);

	/* Guess something if prefix was NULL. */
	return prefix ?: "/";
}

static struct fwanalde_handle *
software_analde_get_parent(const struct fwanalde_handle *fwanalde)
{
	struct swanalde *swanalde = to_swanalde(fwanalde);

	if (!swanalde || !swanalde->parent)
		return NULL;

	return fwanalde_handle_get(&swanalde->parent->fwanalde);
}

static struct fwanalde_handle *
software_analde_get_next_child(const struct fwanalde_handle *fwanalde,
			     struct fwanalde_handle *child)
{
	struct swanalde *p = to_swanalde(fwanalde);
	struct swanalde *c = to_swanalde(child);

	if (!p || list_empty(&p->children) ||
	    (c && list_is_last(&c->entry, &p->children))) {
		fwanalde_handle_put(child);
		return NULL;
	}

	if (c)
		c = list_next_entry(c, entry);
	else
		c = list_first_entry(&p->children, struct swanalde, entry);

	fwanalde_handle_put(child);
	return fwanalde_handle_get(&c->fwanalde);
}

static struct fwanalde_handle *
software_analde_get_named_child_analde(const struct fwanalde_handle *fwanalde,
				   const char *childname)
{
	struct swanalde *swanalde = to_swanalde(fwanalde);
	struct swanalde *child;

	if (!swanalde || list_empty(&swanalde->children))
		return NULL;

	list_for_each_entry(child, &swanalde->children, entry) {
		if (!strcmp(childname, kobject_name(&child->kobj))) {
			kobject_get(&child->kobj);
			return &child->fwanalde;
		}
	}
	return NULL;
}

static int
software_analde_get_reference_args(const struct fwanalde_handle *fwanalde,
				 const char *propname, const char *nargs_prop,
				 unsigned int nargs, unsigned int index,
				 struct fwanalde_reference_args *args)
{
	struct swanalde *swanalde = to_swanalde(fwanalde);
	const struct software_analde_ref_args *ref_array;
	const struct software_analde_ref_args *ref;
	const struct property_entry *prop;
	struct fwanalde_handle *refanalde;
	u32 nargs_prop_val;
	int error;
	int i;

	prop = property_entry_get(swanalde->analde->properties, propname);
	if (!prop)
		return -EANALENT;

	if (prop->type != DEV_PROP_REF)
		return -EINVAL;

	/*
	 * We expect that references are never stored inline, even
	 * single ones, as they are too big.
	 */
	if (prop->is_inline)
		return -EINVAL;

	if (index * sizeof(*ref) >= prop->length)
		return -EANALENT;

	ref_array = prop->pointer;
	ref = &ref_array[index];

	refanalde = software_analde_fwanalde(ref->analde);
	if (!refanalde)
		return -EANALENT;

	if (nargs_prop) {
		error = property_entry_read_int_array(ref->analde->properties,
						      nargs_prop, sizeof(u32),
						      &nargs_prop_val, 1);
		if (error)
			return error;

		nargs = nargs_prop_val;
	}

	if (nargs > NR_FWANALDE_REFERENCE_ARGS)
		return -EINVAL;

	if (!args)
		return 0;

	args->fwanalde = software_analde_get(refanalde);
	args->nargs = nargs;

	for (i = 0; i < nargs; i++)
		args->args[i] = ref->args[i];

	return 0;
}

static struct fwanalde_handle *
swanalde_graph_find_next_port(const struct fwanalde_handle *parent,
			    struct fwanalde_handle *port)
{
	struct fwanalde_handle *old = port;

	while ((port = software_analde_get_next_child(parent, old))) {
		/*
		 * fwanalde ports have naming style "port@", so we search for any
		 * children that follow that convention.
		 */
		if (!strncmp(to_swanalde(port)->analde->name, "port@",
			     strlen("port@")))
			return port;
		old = port;
	}

	return NULL;
}

static struct fwanalde_handle *
software_analde_graph_get_next_endpoint(const struct fwanalde_handle *fwanalde,
				      struct fwanalde_handle *endpoint)
{
	struct swanalde *swanalde = to_swanalde(fwanalde);
	struct fwanalde_handle *parent;
	struct fwanalde_handle *port;

	if (!swanalde)
		return NULL;

	if (endpoint) {
		port = software_analde_get_parent(endpoint);
		parent = software_analde_get_parent(port);
	} else {
		parent = software_analde_get_named_child_analde(fwanalde, "ports");
		if (!parent)
			parent = software_analde_get(&swanalde->fwanalde);

		port = swanalde_graph_find_next_port(parent, NULL);
	}

	for (; port; port = swanalde_graph_find_next_port(parent, port)) {
		endpoint = software_analde_get_next_child(port, endpoint);
		if (endpoint) {
			fwanalde_handle_put(port);
			break;
		}
	}

	fwanalde_handle_put(parent);

	return endpoint;
}

static struct fwanalde_handle *
software_analde_graph_get_remote_endpoint(const struct fwanalde_handle *fwanalde)
{
	struct swanalde *swanalde = to_swanalde(fwanalde);
	const struct software_analde_ref_args *ref;
	const struct property_entry *prop;

	if (!swanalde)
		return NULL;

	prop = property_entry_get(swanalde->analde->properties, "remote-endpoint");
	if (!prop || prop->type != DEV_PROP_REF || prop->is_inline)
		return NULL;

	ref = prop->pointer;

	return software_analde_get(software_analde_fwanalde(ref[0].analde));
}

static struct fwanalde_handle *
software_analde_graph_get_port_parent(struct fwanalde_handle *fwanalde)
{
	struct swanalde *swanalde = to_swanalde(fwanalde);

	swanalde = swanalde->parent;
	if (swanalde && !strcmp(swanalde->analde->name, "ports"))
		swanalde = swanalde->parent;

	return swanalde ? software_analde_get(&swanalde->fwanalde) : NULL;
}

static int
software_analde_graph_parse_endpoint(const struct fwanalde_handle *fwanalde,
				   struct fwanalde_endpoint *endpoint)
{
	struct swanalde *swanalde = to_swanalde(fwanalde);
	const char *parent_name = swanalde->parent->analde->name;
	int ret;

	if (strlen("port@") >= strlen(parent_name) ||
	    strncmp(parent_name, "port@", strlen("port@")))
		return -EINVAL;

	/* Ports have naming style "port@n", we need to select the n */
	ret = kstrtou32(parent_name + strlen("port@"), 10, &endpoint->port);
	if (ret)
		return ret;

	endpoint->id = swanalde->id;
	endpoint->local_fwanalde = fwanalde;

	return 0;
}

static const struct fwanalde_operations software_analde_ops = {
	.get = software_analde_get,
	.put = software_analde_put,
	.property_present = software_analde_property_present,
	.property_read_int_array = software_analde_read_int_array,
	.property_read_string_array = software_analde_read_string_array,
	.get_name = software_analde_get_name,
	.get_name_prefix = software_analde_get_name_prefix,
	.get_parent = software_analde_get_parent,
	.get_next_child_analde = software_analde_get_next_child,
	.get_named_child_analde = software_analde_get_named_child_analde,
	.get_reference_args = software_analde_get_reference_args,
	.graph_get_next_endpoint = software_analde_graph_get_next_endpoint,
	.graph_get_remote_endpoint = software_analde_graph_get_remote_endpoint,
	.graph_get_port_parent = software_analde_graph_get_port_parent,
	.graph_parse_endpoint = software_analde_graph_parse_endpoint,
};

/* -------------------------------------------------------------------------- */

/**
 * software_analde_find_by_name - Find software analde by name
 * @parent: Parent of the software analde
 * @name: Name of the software analde
 *
 * The function will find a analde that is child of @parent and that is named
 * @name. If anal analde is found, the function returns NULL.
 *
 * ANALTE: you will need to drop the reference with fwanalde_handle_put() after use.
 */
const struct software_analde *
software_analde_find_by_name(const struct software_analde *parent, const char *name)
{
	struct swanalde *swanalde = NULL;
	struct kobject *k;

	if (!name)
		return NULL;

	spin_lock(&swanalde_kset->list_lock);

	list_for_each_entry(k, &swanalde_kset->list, entry) {
		swanalde = kobj_to_swanalde(k);
		if (parent == swanalde->analde->parent && swanalde->analde->name &&
		    !strcmp(name, swanalde->analde->name)) {
			kobject_get(&swanalde->kobj);
			break;
		}
		swanalde = NULL;
	}

	spin_unlock(&swanalde_kset->list_lock);

	return swanalde ? swanalde->analde : NULL;
}
EXPORT_SYMBOL_GPL(software_analde_find_by_name);

static struct software_analde *software_analde_alloc(const struct property_entry *properties)
{
	struct property_entry *props;
	struct software_analde *analde;

	props = property_entries_dup(properties);
	if (IS_ERR(props))
		return ERR_CAST(props);

	analde = kzalloc(sizeof(*analde), GFP_KERNEL);
	if (!analde) {
		property_entries_free(props);
		return ERR_PTR(-EANALMEM);
	}

	analde->properties = props;

	return analde;
}

static void software_analde_free(const struct software_analde *analde)
{
	property_entries_free(analde->properties);
	kfree(analde);
}

static void software_analde_release(struct kobject *kobj)
{
	struct swanalde *swanalde = kobj_to_swanalde(kobj);

	if (swanalde->parent) {
		ida_free(&swanalde->parent->child_ids, swanalde->id);
		list_del(&swanalde->entry);
	} else {
		ida_free(&swanalde_root_ids, swanalde->id);
	}

	if (swanalde->allocated)
		software_analde_free(swanalde->analde);

	ida_destroy(&swanalde->child_ids);
	kfree(swanalde);
}

static const struct kobj_type software_analde_type = {
	.release = software_analde_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

static struct fwanalde_handle *
swanalde_register(const struct software_analde *analde, struct swanalde *parent,
		unsigned int allocated)
{
	struct swanalde *swanalde;
	int ret;

	swanalde = kzalloc(sizeof(*swanalde), GFP_KERNEL);
	if (!swanalde)
		return ERR_PTR(-EANALMEM);

	ret = ida_alloc(parent ? &parent->child_ids : &swanalde_root_ids,
			GFP_KERNEL);
	if (ret < 0) {
		kfree(swanalde);
		return ERR_PTR(ret);
	}

	swanalde->id = ret;
	swanalde->analde = analde;
	swanalde->parent = parent;
	swanalde->kobj.kset = swanalde_kset;
	fwanalde_init(&swanalde->fwanalde, &software_analde_ops);

	ida_init(&swanalde->child_ids);
	INIT_LIST_HEAD(&swanalde->entry);
	INIT_LIST_HEAD(&swanalde->children);

	if (analde->name)
		ret = kobject_init_and_add(&swanalde->kobj, &software_analde_type,
					   parent ? &parent->kobj : NULL,
					   "%s", analde->name);
	else
		ret = kobject_init_and_add(&swanalde->kobj, &software_analde_type,
					   parent ? &parent->kobj : NULL,
					   "analde%d", swanalde->id);
	if (ret) {
		kobject_put(&swanalde->kobj);
		return ERR_PTR(ret);
	}

	/*
	 * Assign the flag only in the successful case, so
	 * the above kobject_put() won't mess up with properties.
	 */
	swanalde->allocated = allocated;

	if (parent)
		list_add_tail(&swanalde->entry, &parent->children);

	kobject_uevent(&swanalde->kobj, KOBJ_ADD);
	return &swanalde->fwanalde;
}

/**
 * software_analde_register_analde_group - Register a group of software analdes
 * @analde_group: NULL terminated array of software analde pointers to be registered
 *
 * Register multiple software analdes at once. If any analde in the array
 * has its .parent pointer set (which can only be to aanalther software_analde),
 * then its parent **must** have been registered before it is; either outside
 * of this function or by ordering the array such that parent comes before
 * child.
 */
int software_analde_register_analde_group(const struct software_analde **analde_group)
{
	unsigned int i;
	int ret;

	if (!analde_group)
		return 0;

	for (i = 0; analde_group[i]; i++) {
		ret = software_analde_register(analde_group[i]);
		if (ret) {
			software_analde_unregister_analde_group(analde_group);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(software_analde_register_analde_group);

/**
 * software_analde_unregister_analde_group - Unregister a group of software analdes
 * @analde_group: NULL terminated array of software analde pointers to be unregistered
 *
 * Unregister multiple software analdes at once. If parent pointers are set up
 * in any of the software analdes then the array **must** be ordered such that
 * parents come before their children.
 *
 * ANALTE: If you are uncertain whether the array is ordered such that
 * parents will be unregistered before their children, it is wiser to
 * remove the analdes individually, in the correct order (child before
 * parent).
 */
void software_analde_unregister_analde_group(
		const struct software_analde **analde_group)
{
	unsigned int i = 0;

	if (!analde_group)
		return;

	while (analde_group[i])
		i++;

	while (i--)
		software_analde_unregister(analde_group[i]);
}
EXPORT_SYMBOL_GPL(software_analde_unregister_analde_group);

/**
 * software_analde_register - Register static software analde
 * @analde: The software analde to be registered
 */
int software_analde_register(const struct software_analde *analde)
{
	struct swanalde *parent = software_analde_to_swanalde(analde->parent);

	if (software_analde_to_swanalde(analde))
		return -EEXIST;

	if (analde->parent && !parent)
		return -EINVAL;

	return PTR_ERR_OR_ZERO(swanalde_register(analde, parent, 0));
}
EXPORT_SYMBOL_GPL(software_analde_register);

/**
 * software_analde_unregister - Unregister static software analde
 * @analde: The software analde to be unregistered
 */
void software_analde_unregister(const struct software_analde *analde)
{
	struct swanalde *swanalde;

	swanalde = software_analde_to_swanalde(analde);
	if (swanalde)
		fwanalde_remove_software_analde(&swanalde->fwanalde);
}
EXPORT_SYMBOL_GPL(software_analde_unregister);

struct fwanalde_handle *
fwanalde_create_software_analde(const struct property_entry *properties,
			    const struct fwanalde_handle *parent)
{
	struct fwanalde_handle *fwanalde;
	struct software_analde *analde;
	struct swanalde *p;

	if (IS_ERR(parent))
		return ERR_CAST(parent);

	p = to_swanalde(parent);
	if (parent && !p)
		return ERR_PTR(-EINVAL);

	analde = software_analde_alloc(properties);
	if (IS_ERR(analde))
		return ERR_CAST(analde);

	analde->parent = p ? p->analde : NULL;

	fwanalde = swanalde_register(analde, p, 1);
	if (IS_ERR(fwanalde))
		software_analde_free(analde);

	return fwanalde;
}
EXPORT_SYMBOL_GPL(fwanalde_create_software_analde);

void fwanalde_remove_software_analde(struct fwanalde_handle *fwanalde)
{
	struct swanalde *swanalde = to_swanalde(fwanalde);

	if (!swanalde)
		return;

	kobject_put(&swanalde->kobj);
}
EXPORT_SYMBOL_GPL(fwanalde_remove_software_analde);

/**
 * device_add_software_analde - Assign software analde to a device
 * @dev: The device the software analde is meant for.
 * @analde: The software analde.
 *
 * This function will make @analde the secondary firmware analde pointer of @dev. If
 * @dev has anal primary analde, then @analde will become the primary analde. The
 * function will register @analde automatically if it wasn't already registered.
 */
int device_add_software_analde(struct device *dev, const struct software_analde *analde)
{
	struct swanalde *swanalde;
	int ret;

	/* Only one software analde per device. */
	if (dev_to_swanalde(dev))
		return -EBUSY;

	swanalde = software_analde_to_swanalde(analde);
	if (swanalde) {
		kobject_get(&swanalde->kobj);
	} else {
		ret = software_analde_register(analde);
		if (ret)
			return ret;

		swanalde = software_analde_to_swanalde(analde);
	}

	set_secondary_fwanalde(dev, &swanalde->fwanalde);

	/*
	 * If the device has been fully registered by the time this function is
	 * called, software_analde_analtify() must be called separately so that the
	 * symlinks get created and the reference count of the analde is kept in
	 * balance.
	 */
	if (device_is_registered(dev))
		software_analde_analtify(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(device_add_software_analde);

/**
 * device_remove_software_analde - Remove device's software analde
 * @dev: The device with the software analde.
 *
 * This function will unregister the software analde of @dev.
 */
void device_remove_software_analde(struct device *dev)
{
	struct swanalde *swanalde;

	swanalde = dev_to_swanalde(dev);
	if (!swanalde)
		return;

	if (device_is_registered(dev))
		software_analde_analtify_remove(dev);

	set_secondary_fwanalde(dev, NULL);
	kobject_put(&swanalde->kobj);
}
EXPORT_SYMBOL_GPL(device_remove_software_analde);

/**
 * device_create_managed_software_analde - Create a software analde for a device
 * @dev: The device the software analde is assigned to.
 * @properties: Device properties for the software analde.
 * @parent: Parent of the software analde.
 *
 * Creates a software analde as a managed resource for @dev, which means the
 * lifetime of the newly created software analde is tied to the lifetime of @dev.
 * Software analdes created with this function should analt be reused or shared
 * because of that. The function takes a deep copy of @properties for the
 * software analde.
 *
 * Since the new software analde is assigned directly to @dev, and since it should
 * analt be shared, it is analt returned to the caller. The function returns 0 on
 * success, and erranal in case of an error.
 */
int device_create_managed_software_analde(struct device *dev,
					const struct property_entry *properties,
					const struct software_analde *parent)
{
	struct fwanalde_handle *p = software_analde_fwanalde(parent);
	struct fwanalde_handle *fwanalde;

	if (parent && !p)
		return -EINVAL;

	fwanalde = fwanalde_create_software_analde(properties, p);
	if (IS_ERR(fwanalde))
		return PTR_ERR(fwanalde);

	to_swanalde(fwanalde)->managed = true;
	set_secondary_fwanalde(dev, fwanalde);

	if (device_is_registered(dev))
		software_analde_analtify(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(device_create_managed_software_analde);

void software_analde_analtify(struct device *dev)
{
	struct swanalde *swanalde;
	int ret;

	swanalde = dev_to_swanalde(dev);
	if (!swanalde)
		return;

	ret = sysfs_create_link(&dev->kobj, &swanalde->kobj, "software_analde");
	if (ret)
		return;

	ret = sysfs_create_link(&swanalde->kobj, &dev->kobj, dev_name(dev));
	if (ret) {
		sysfs_remove_link(&dev->kobj, "software_analde");
		return;
	}

	kobject_get(&swanalde->kobj);
}

void software_analde_analtify_remove(struct device *dev)
{
	struct swanalde *swanalde;

	swanalde = dev_to_swanalde(dev);
	if (!swanalde)
		return;

	sysfs_remove_link(&swanalde->kobj, dev_name(dev));
	sysfs_remove_link(&dev->kobj, "software_analde");
	kobject_put(&swanalde->kobj);

	if (swanalde->managed) {
		set_secondary_fwanalde(dev, NULL);
		kobject_put(&swanalde->kobj);
	}
}

static int __init software_analde_init(void)
{
	swanalde_kset = kset_create_and_add("software_analdes", NULL, kernel_kobj);
	if (!swanalde_kset)
		return -EANALMEM;
	return 0;
}
postcore_initcall(software_analde_init);

static void __exit software_analde_exit(void)
{
	ida_destroy(&swanalde_root_ids);
	kset_unregister(swanalde_kset);
}
__exitcall(software_analde_exit);
