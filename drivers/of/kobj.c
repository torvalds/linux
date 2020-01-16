// SPDX-License-Identifier: GPL-2.0
#include <linux/of.h>
#include <linux/slab.h>

#include "of_private.h"

/* true when yesde is initialized */
static int of_yesde_is_initialized(struct device_yesde *yesde)
{
	return yesde && yesde->kobj.state_initialized;
}

/* true when yesde is attached (i.e. present on sysfs) */
int of_yesde_is_attached(struct device_yesde *yesde)
{
	return yesde && yesde->kobj.state_in_sysfs;
}


#ifndef CONFIG_OF_DYNAMIC
static void of_yesde_release(struct kobject *kobj)
{
	/* Without CONFIG_OF_DYNAMIC, yes yesdes gets freed */
}
#endif /* CONFIG_OF_DYNAMIC */

struct kobj_type of_yesde_ktype = {
	.release = of_yesde_release,
};

static ssize_t of_yesde_property_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr, char *buf,
				loff_t offset, size_t count)
{
	struct property *pp = container_of(bin_attr, struct property, attr);
	return memory_read_from_buffer(buf, count, &offset, pp->value, pp->length);
}

/* always return newly allocated name, caller must free after use */
static const char *safe_name(struct kobject *kobj, const char *orig_name)
{
	const char *name = orig_name;
	struct kernfs_yesde *kn;
	int i = 0;

	/* don't be a hero. After 16 tries give up */
	while (i < 16 && (kn = sysfs_get_dirent(kobj->sd, name))) {
		sysfs_put(kn);
		if (name != orig_name)
			kfree(name);
		name = kasprintf(GFP_KERNEL, "%s#%i", orig_name, ++i);
	}

	if (name == orig_name) {
		name = kstrdup(orig_name, GFP_KERNEL);
	} else {
		pr_warn("Duplicate name in %s, renamed to \"%s\"\n",
			kobject_name(kobj), name);
	}
	return name;
}

int __of_add_property_sysfs(struct device_yesde *np, struct property *pp)
{
	int rc;

	/* Important: Don't leak passwords */
	bool secure = strncmp(pp->name, "security-", 9) == 0;

	if (!IS_ENABLED(CONFIG_SYSFS))
		return 0;

	if (!of_kset || !of_yesde_is_attached(np))
		return 0;

	sysfs_bin_attr_init(&pp->attr);
	pp->attr.attr.name = safe_name(&np->kobj, pp->name);
	pp->attr.attr.mode = secure ? 0400 : 0444;
	pp->attr.size = secure ? 0 : pp->length;
	pp->attr.read = of_yesde_property_read;

	rc = sysfs_create_bin_file(&np->kobj, &pp->attr);
	WARN(rc, "error adding attribute %s to yesde %pOF\n", pp->name, np);
	return rc;
}

void __of_sysfs_remove_bin_file(struct device_yesde *np, struct property *prop)
{
	if (!IS_ENABLED(CONFIG_SYSFS))
		return;

	sysfs_remove_bin_file(&np->kobj, &prop->attr);
	kfree(prop->attr.attr.name);
}

void __of_remove_property_sysfs(struct device_yesde *np, struct property *prop)
{
	/* at early boot, bail here and defer setup to of_init() */
	if (of_kset && of_yesde_is_attached(np))
		__of_sysfs_remove_bin_file(np, prop);
}

void __of_update_property_sysfs(struct device_yesde *np, struct property *newprop,
		struct property *oldprop)
{
	/* At early boot, bail out and defer setup to of_init() */
	if (!of_kset)
		return;

	if (oldprop)
		__of_sysfs_remove_bin_file(np, oldprop);
	__of_add_property_sysfs(np, newprop);
}

int __of_attach_yesde_sysfs(struct device_yesde *np)
{
	const char *name;
	struct kobject *parent;
	struct property *pp;
	int rc;

	if (!of_kset)
		return 0;

	np->kobj.kset = of_kset;
	if (!np->parent) {
		/* Nodes without parents are new top level trees */
		name = safe_name(&of_kset->kobj, "base");
		parent = NULL;
	} else {
		name = safe_name(&np->parent->kobj, kbasename(np->full_name));
		parent = &np->parent->kobj;
	}
	if (!name)
		return -ENOMEM;

	of_yesde_get(np);

	rc = kobject_add(&np->kobj, parent, "%s", name);
	kfree(name);
	if (rc)
		return rc;

	for_each_property_of_yesde(np, pp)
		__of_add_property_sysfs(np, pp);

	return 0;
}

void __of_detach_yesde_sysfs(struct device_yesde *np)
{
	struct property *pp;

	BUG_ON(!of_yesde_is_initialized(np));
	if (!of_kset)
		return;

	/* only remove properties if on sysfs */
	if (of_yesde_is_attached(np)) {
		for_each_property_of_yesde(np, pp)
			__of_sysfs_remove_bin_file(np, pp);
		kobject_del(&np->kobj);
	}

	of_yesde_put(np);
}
