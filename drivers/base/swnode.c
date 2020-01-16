// SPDX-License-Identifier: GPL-2.0
/*
 * Software yesdes for the firmware yesde framework.
 *
 * Copyright (C) 2018, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/property.h>
#include <linux/slab.h>

struct swyesde {
	int id;
	struct kobject kobj;
	struct fwyesde_handle fwyesde;
	const struct software_yesde *yesde;

	/* hierarchy */
	struct ida child_ids;
	struct list_head entry;
	struct list_head children;
	struct swyesde *parent;

	unsigned int allocated:1;
};

static DEFINE_IDA(swyesde_root_ids);
static struct kset *swyesde_kset;

#define kobj_to_swyesde(_kobj_) container_of(_kobj_, struct swyesde, kobj)

static const struct fwyesde_operations software_yesde_ops;

bool is_software_yesde(const struct fwyesde_handle *fwyesde)
{
	return !IS_ERR_OR_NULL(fwyesde) && fwyesde->ops == &software_yesde_ops;
}
EXPORT_SYMBOL_GPL(is_software_yesde);

#define to_swyesde(__fwyesde)						\
	({								\
		typeof(__fwyesde) __to_swyesde_fwyesde = __fwyesde;		\
									\
		is_software_yesde(__to_swyesde_fwyesde) ?			\
			container_of(__to_swyesde_fwyesde,		\
				     struct swyesde, fwyesde) : NULL;	\
	})

static struct swyesde *
software_yesde_to_swyesde(const struct software_yesde *yesde)
{
	struct swyesde *swyesde = NULL;
	struct kobject *k;

	if (!yesde)
		return NULL;

	spin_lock(&swyesde_kset->list_lock);

	list_for_each_entry(k, &swyesde_kset->list, entry) {
		swyesde = kobj_to_swyesde(k);
		if (swyesde->yesde == yesde)
			break;
		swyesde = NULL;
	}

	spin_unlock(&swyesde_kset->list_lock);

	return swyesde;
}

const struct software_yesde *to_software_yesde(const struct fwyesde_handle *fwyesde)
{
	const struct swyesde *swyesde = to_swyesde(fwyesde);

	return swyesde ? swyesde->yesde : NULL;
}
EXPORT_SYMBOL_GPL(to_software_yesde);

struct fwyesde_handle *software_yesde_fwyesde(const struct software_yesde *yesde)
{
	struct swyesde *swyesde = software_yesde_to_swyesde(yesde);

	return swyesde ? &swyesde->fwyesde : NULL;
}
EXPORT_SYMBOL_GPL(software_yesde_fwyesde);

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

	if (prop->is_array)
		return prop->pointer;

	return &prop->value;
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
	const void *pointer = property_get_pointer(p);
	const char * const *src_str;
	size_t i, nval;

	if (p->is_array) {
		if (p->type == DEV_PROP_STRING && p->pointer) {
			src_str = p->pointer;
			nval = p->length / sizeof(const char *);
			for (i = 0; i < nval; i++)
				kfree(src_str[i]);
		}
		kfree(pointer);
	} else if (p->type == DEV_PROP_STRING) {
		kfree(p->value.str);
	}
	kfree(p->name);
}

static const char * const *
property_copy_string_array(const struct property_entry *src)
{
	const char **d;
	const char * const *src_str = src->pointer;
	size_t nval = src->length / sizeof(*d);
	int i;

	d = kcalloc(nval, sizeof(*d), GFP_KERNEL);
	if (!d)
		return NULL;

	for (i = 0; i < nval; i++) {
		d[i] = kstrdup(src_str[i], GFP_KERNEL);
		if (!d[i] && src_str[i]) {
			while (--i >= 0)
				kfree(d[i]);
			kfree(d);
			return NULL;
		}
	}

	return d;
}

static int property_entry_copy_data(struct property_entry *dst,
				    const struct property_entry *src)
{
	const void *pointer = property_get_pointer(src);
	const void *new;

	if (src->is_array) {
		if (!src->length)
			return -ENODATA;

		if (src->type == DEV_PROP_STRING) {
			new = property_copy_string_array(src);
			if (!new)
				return -ENOMEM;
		} else {
			new = kmemdup(pointer, src->length, GFP_KERNEL);
			if (!new)
				return -ENOMEM;
		}

		dst->is_array = true;
		dst->pointer = new;
	} else if (src->type == DEV_PROP_STRING) {
		new = kstrdup(src->value.str, GFP_KERNEL);
		if (!new && src->value.str)
			return -ENOMEM;

		dst->value.str = new;
	} else {
		dst->value = src->value;
	}

	dst->length = src->length;
	dst->type = src->type;
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
/* fwyesde operations */

static struct fwyesde_handle *software_yesde_get(struct fwyesde_handle *fwyesde)
{
	struct swyesde *swyesde = to_swyesde(fwyesde);

	kobject_get(&swyesde->kobj);

	return &swyesde->fwyesde;
}

static void software_yesde_put(struct fwyesde_handle *fwyesde)
{
	struct swyesde *swyesde = to_swyesde(fwyesde);

	kobject_put(&swyesde->kobj);
}

static bool software_yesde_property_present(const struct fwyesde_handle *fwyesde,
					   const char *propname)
{
	struct swyesde *swyesde = to_swyesde(fwyesde);

	return !!property_entry_get(swyesde->yesde->properties, propname);
}

static int software_yesde_read_int_array(const struct fwyesde_handle *fwyesde,
					const char *propname,
					unsigned int elem_size, void *val,
					size_t nval)
{
	struct swyesde *swyesde = to_swyesde(fwyesde);

	return property_entry_read_int_array(swyesde->yesde->properties, propname,
					     elem_size, val, nval);
}

static int software_yesde_read_string_array(const struct fwyesde_handle *fwyesde,
					   const char *propname,
					   const char **val, size_t nval)
{
	struct swyesde *swyesde = to_swyesde(fwyesde);

	return property_entry_read_string_array(swyesde->yesde->properties,
						propname, val, nval);
}

static const char *
software_yesde_get_name(const struct fwyesde_handle *fwyesde)
{
	const struct swyesde *swyesde = to_swyesde(fwyesde);

	if (!swyesde)
		return "(null)";

	return kobject_name(&swyesde->kobj);
}

static const char *
software_yesde_get_name_prefix(const struct fwyesde_handle *fwyesde)
{
	struct fwyesde_handle *parent;
	const char *prefix;

	parent = fwyesde_get_parent(fwyesde);
	if (!parent)
		return "";

	/* Figure out the prefix from the parents. */
	while (is_software_yesde(parent))
		parent = fwyesde_get_next_parent(parent);

	prefix = fwyesde_get_name_prefix(parent);
	fwyesde_handle_put(parent);

	/* Guess something if prefix was NULL. */
	return prefix ?: "/";
}

static struct fwyesde_handle *
software_yesde_get_parent(const struct fwyesde_handle *fwyesde)
{
	struct swyesde *swyesde = to_swyesde(fwyesde);

	if (!swyesde || !swyesde->parent)
		return NULL;

	return fwyesde_handle_get(&swyesde->parent->fwyesde);
}

static struct fwyesde_handle *
software_yesde_get_next_child(const struct fwyesde_handle *fwyesde,
			     struct fwyesde_handle *child)
{
	struct swyesde *p = to_swyesde(fwyesde);
	struct swyesde *c = to_swyesde(child);

	if (!p || list_empty(&p->children) ||
	    (c && list_is_last(&c->entry, &p->children)))
		return NULL;

	if (c)
		c = list_next_entry(c, entry);
	else
		c = list_first_entry(&p->children, struct swyesde, entry);
	return &c->fwyesde;
}

static struct fwyesde_handle *
software_yesde_get_named_child_yesde(const struct fwyesde_handle *fwyesde,
				   const char *childname)
{
	struct swyesde *swyesde = to_swyesde(fwyesde);
	struct swyesde *child;

	if (!swyesde || list_empty(&swyesde->children))
		return NULL;

	list_for_each_entry(child, &swyesde->children, entry) {
		if (!strcmp(childname, kobject_name(&child->kobj))) {
			kobject_get(&child->kobj);
			return &child->fwyesde;
		}
	}
	return NULL;
}

static int
software_yesde_get_reference_args(const struct fwyesde_handle *fwyesde,
				 const char *propname, const char *nargs_prop,
				 unsigned int nargs, unsigned int index,
				 struct fwyesde_reference_args *args)
{
	struct swyesde *swyesde = to_swyesde(fwyesde);
	const struct software_yesde_reference *ref;
	const struct property_entry *prop;
	struct fwyesde_handle *refyesde;
	int i;

	if (!swyesde || !swyesde->yesde->references)
		return -ENOENT;

	for (ref = swyesde->yesde->references; ref->name; ref++)
		if (!strcmp(ref->name, propname))
			break;

	if (!ref->name || index > (ref->nrefs - 1))
		return -ENOENT;

	refyesde = software_yesde_fwyesde(ref->refs[index].yesde);
	if (!refyesde)
		return -ENOENT;

	if (nargs_prop) {
		prop = property_entry_get(swyesde->yesde->properties, nargs_prop);
		if (!prop)
			return -EINVAL;

		nargs = prop->value.u32_data;
	}

	if (nargs > NR_FWNODE_REFERENCE_ARGS)
		return -EINVAL;

	args->fwyesde = software_yesde_get(refyesde);
	args->nargs = nargs;

	for (i = 0; i < nargs; i++)
		args->args[i] = ref->refs[index].args[i];

	return 0;
}

static const struct fwyesde_operations software_yesde_ops = {
	.get = software_yesde_get,
	.put = software_yesde_put,
	.property_present = software_yesde_property_present,
	.property_read_int_array = software_yesde_read_int_array,
	.property_read_string_array = software_yesde_read_string_array,
	.get_name = software_yesde_get_name,
	.get_name_prefix = software_yesde_get_name_prefix,
	.get_parent = software_yesde_get_parent,
	.get_next_child_yesde = software_yesde_get_next_child,
	.get_named_child_yesde = software_yesde_get_named_child_yesde,
	.get_reference_args = software_yesde_get_reference_args
};

/* -------------------------------------------------------------------------- */

/**
 * software_yesde_find_by_name - Find software yesde by name
 * @parent: Parent of the software yesde
 * @name: Name of the software yesde
 *
 * The function will find a yesde that is child of @parent and that is named
 * @name. If yes yesde is found, the function returns NULL.
 *
 * NOTE: you will need to drop the reference with fwyesde_handle_put() after use.
 */
const struct software_yesde *
software_yesde_find_by_name(const struct software_yesde *parent, const char *name)
{
	struct swyesde *swyesde = NULL;
	struct kobject *k;

	if (!name)
		return NULL;

	spin_lock(&swyesde_kset->list_lock);

	list_for_each_entry(k, &swyesde_kset->list, entry) {
		swyesde = kobj_to_swyesde(k);
		if (parent == swyesde->yesde->parent && swyesde->yesde->name &&
		    !strcmp(name, swyesde->yesde->name)) {
			kobject_get(&swyesde->kobj);
			break;
		}
		swyesde = NULL;
	}

	spin_unlock(&swyesde_kset->list_lock);

	return swyesde ? swyesde->yesde : NULL;
}
EXPORT_SYMBOL_GPL(software_yesde_find_by_name);

static int
software_yesde_register_properties(struct software_yesde *yesde,
				  const struct property_entry *properties)
{
	struct property_entry *props;

	props = property_entries_dup(properties);
	if (IS_ERR(props))
		return PTR_ERR(props);

	yesde->properties = props;

	return 0;
}

static void software_yesde_release(struct kobject *kobj)
{
	struct swyesde *swyesde = kobj_to_swyesde(kobj);

	if (swyesde->allocated) {
		property_entries_free(swyesde->yesde->properties);
		kfree(swyesde->yesde);
	}
	ida_destroy(&swyesde->child_ids);
	kfree(swyesde);
}

static struct kobj_type software_yesde_type = {
	.release = software_yesde_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

static struct fwyesde_handle *
swyesde_register(const struct software_yesde *yesde, struct swyesde *parent,
		unsigned int allocated)
{
	struct swyesde *swyesde;
	int ret;

	swyesde = kzalloc(sizeof(*swyesde), GFP_KERNEL);
	if (!swyesde) {
		ret = -ENOMEM;
		goto out_err;
	}

	ret = ida_simple_get(parent ? &parent->child_ids : &swyesde_root_ids,
			     0, 0, GFP_KERNEL);
	if (ret < 0) {
		kfree(swyesde);
		goto out_err;
	}

	swyesde->id = ret;
	swyesde->yesde = yesde;
	swyesde->parent = parent;
	swyesde->allocated = allocated;
	swyesde->kobj.kset = swyesde_kset;
	swyesde->fwyesde.ops = &software_yesde_ops;

	ida_init(&swyesde->child_ids);
	INIT_LIST_HEAD(&swyesde->entry);
	INIT_LIST_HEAD(&swyesde->children);

	if (yesde->name)
		ret = kobject_init_and_add(&swyesde->kobj, &software_yesde_type,
					   parent ? &parent->kobj : NULL,
					   "%s", yesde->name);
	else
		ret = kobject_init_and_add(&swyesde->kobj, &software_yesde_type,
					   parent ? &parent->kobj : NULL,
					   "yesde%d", swyesde->id);
	if (ret) {
		kobject_put(&swyesde->kobj);
		return ERR_PTR(ret);
	}

	if (parent)
		list_add_tail(&swyesde->entry, &parent->children);

	kobject_uevent(&swyesde->kobj, KOBJ_ADD);
	return &swyesde->fwyesde;

out_err:
	if (allocated)
		property_entries_free(yesde->properties);
	return ERR_PTR(ret);
}

/**
 * software_yesde_register_yesdes - Register an array of software yesdes
 * @yesdes: Zero terminated array of software yesdes to be registered
 *
 * Register multiple software yesdes at once.
 */
int software_yesde_register_yesdes(const struct software_yesde *yesdes)
{
	int ret;
	int i;

	for (i = 0; yesdes[i].name; i++) {
		ret = software_yesde_register(&yesdes[i]);
		if (ret) {
			software_yesde_unregister_yesdes(yesdes);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(software_yesde_register_yesdes);

/**
 * software_yesde_unregister_yesdes - Unregister an array of software yesdes
 * @yesdes: Zero terminated array of software yesdes to be unregistered
 *
 * Unregister multiple software yesdes at once.
 */
void software_yesde_unregister_yesdes(const struct software_yesde *yesdes)
{
	struct swyesde *swyesde;
	int i;

	for (i = 0; yesdes[i].name; i++) {
		swyesde = software_yesde_to_swyesde(&yesdes[i]);
		if (swyesde)
			fwyesde_remove_software_yesde(&swyesde->fwyesde);
	}
}
EXPORT_SYMBOL_GPL(software_yesde_unregister_yesdes);

/**
 * software_yesde_register - Register static software yesde
 * @yesde: The software yesde to be registered
 */
int software_yesde_register(const struct software_yesde *yesde)
{
	struct swyesde *parent = software_yesde_to_swyesde(yesde->parent);

	if (software_yesde_to_swyesde(yesde))
		return -EEXIST;

	return PTR_ERR_OR_ZERO(swyesde_register(yesde, parent, 0));
}
EXPORT_SYMBOL_GPL(software_yesde_register);

struct fwyesde_handle *
fwyesde_create_software_yesde(const struct property_entry *properties,
			    const struct fwyesde_handle *parent)
{
	struct software_yesde *yesde;
	struct swyesde *p = NULL;
	int ret;

	if (parent) {
		if (IS_ERR(parent))
			return ERR_CAST(parent);
		if (!is_software_yesde(parent))
			return ERR_PTR(-EINVAL);
		p = to_swyesde(parent);
	}

	yesde = kzalloc(sizeof(*yesde), GFP_KERNEL);
	if (!yesde)
		return ERR_PTR(-ENOMEM);

	ret = software_yesde_register_properties(yesde, properties);
	if (ret) {
		kfree(yesde);
		return ERR_PTR(ret);
	}

	yesde->parent = p ? p->yesde : NULL;

	return swyesde_register(yesde, p, 1);
}
EXPORT_SYMBOL_GPL(fwyesde_create_software_yesde);

void fwyesde_remove_software_yesde(struct fwyesde_handle *fwyesde)
{
	struct swyesde *swyesde = to_swyesde(fwyesde);

	if (!swyesde)
		return;

	if (swyesde->parent) {
		ida_simple_remove(&swyesde->parent->child_ids, swyesde->id);
		list_del(&swyesde->entry);
	} else {
		ida_simple_remove(&swyesde_root_ids, swyesde->id);
	}

	kobject_put(&swyesde->kobj);
}
EXPORT_SYMBOL_GPL(fwyesde_remove_software_yesde);

int software_yesde_yestify(struct device *dev, unsigned long action)
{
	struct fwyesde_handle *fwyesde = dev_fwyesde(dev);
	struct swyesde *swyesde;
	int ret;

	if (!fwyesde)
		return 0;

	if (!is_software_yesde(fwyesde))
		fwyesde = fwyesde->secondary;
	if (!is_software_yesde(fwyesde))
		return 0;

	swyesde = to_swyesde(fwyesde);

	switch (action) {
	case KOBJ_ADD:
		ret = sysfs_create_link(&dev->kobj, &swyesde->kobj,
					"software_yesde");
		if (ret)
			break;

		ret = sysfs_create_link(&swyesde->kobj, &dev->kobj,
					dev_name(dev));
		if (ret) {
			sysfs_remove_link(&dev->kobj, "software_yesde");
			break;
		}
		kobject_get(&swyesde->kobj);
		break;
	case KOBJ_REMOVE:
		sysfs_remove_link(&swyesde->kobj, dev_name(dev));
		sysfs_remove_link(&dev->kobj, "software_yesde");
		kobject_put(&swyesde->kobj);
		break;
	default:
		break;
	}

	return 0;
}

static int __init software_yesde_init(void)
{
	swyesde_kset = kset_create_and_add("software_yesdes", NULL, kernel_kobj);
	if (!swyesde_kset)
		return -ENOMEM;
	return 0;
}
postcore_initcall(software_yesde_init);

static void __exit software_yesde_exit(void)
{
	ida_destroy(&swyesde_root_ids);
	kset_unregister(swyesde_kset);
}
__exitcall(software_yesde_exit);
