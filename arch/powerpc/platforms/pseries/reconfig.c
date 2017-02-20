/*
 * pSeries_reconfig.c - support for dynamic reconfiguration (including PCI
 * Hotplug and Dynamic Logical Partitioning on RPA platforms).
 *
 * Copyright (C) 2005 Nathan Lynch
 * Copyright (C) 2005 IBM Corporation
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/of.h>

#include <asm/prom.h>
#include <asm/machdep.h>
#include <linux/uaccess.h>
#include <asm/mmu.h>

#include "of_helpers.h"

static int pSeries_reconfig_add_node(const char *path, struct property *proplist)
{
	struct device_node *np;
	int err = -ENOMEM;

	np = kzalloc(sizeof(*np), GFP_KERNEL);
	if (!np)
		goto out_err;

	np->full_name = kstrdup(path, GFP_KERNEL);
	if (!np->full_name)
		goto out_err;

	np->properties = proplist;
	of_node_set_flag(np, OF_DYNAMIC);
	of_node_init(np);

	np->parent = pseries_of_derive_parent(path);
	if (IS_ERR(np->parent)) {
		err = PTR_ERR(np->parent);
		goto out_err;
	}

	err = of_attach_node(np);
	if (err) {
		printk(KERN_ERR "Failed to add device node %s\n", path);
		goto out_err;
	}

	of_node_put(np->parent);

	return 0;

out_err:
	if (np) {
		of_node_put(np->parent);
		kfree(np->full_name);
		kfree(np);
	}
	return err;
}

static int pSeries_reconfig_remove_node(struct device_node *np)
{
	struct device_node *parent, *child;

	parent = of_get_parent(np);
	if (!parent)
		return -EINVAL;

	if ((child = of_get_next_child(np, NULL))) {
		of_node_put(child);
		of_node_put(parent);
		return -EBUSY;
	}

	of_detach_node(np);
	of_node_put(parent);
	of_node_put(np); /* Must decrement the refcount */
	return 0;
}

/*
 * /proc/powerpc/ofdt - yucky binary interface for adding and removing
 * OF device nodes.  Should be deprecated as soon as we get an
 * in-kernel wrapper for the RTAS ibm,configure-connector call.
 */

static void release_prop_list(const struct property *prop)
{
	struct property *next;
	for (; prop; prop = next) {
		next = prop->next;
		kfree(prop->name);
		kfree(prop->value);
		kfree(prop);
	}

}

/**
 * parse_next_property - process the next property from raw input buffer
 * @buf: input buffer, must be nul-terminated
 * @end: end of the input buffer + 1, for validation
 * @name: return value; set to property name in buf
 * @length: return value; set to length of value
 * @value: return value; set to the property value in buf
 *
 * Note that the caller must make copies of the name and value returned,
 * this function does no allocation or copying of the data.  Return value
 * is set to the next name in buf, or NULL on error.
 */
static char * parse_next_property(char *buf, char *end, char **name, int *length,
				  unsigned char **value)
{
	char *tmp;

	*name = buf;

	tmp = strchr(buf, ' ');
	if (!tmp) {
		printk(KERN_ERR "property parse failed in %s at line %d\n",
		       __func__, __LINE__);
		return NULL;
	}
	*tmp = '\0';

	if (++tmp >= end) {
		printk(KERN_ERR "property parse failed in %s at line %d\n",
		       __func__, __LINE__);
		return NULL;
	}

	/* now we're on the length */
	*length = -1;
	*length = simple_strtoul(tmp, &tmp, 10);
	if (*length == -1) {
		printk(KERN_ERR "property parse failed in %s at line %d\n",
		       __func__, __LINE__);
		return NULL;
	}
	if (*tmp != ' ' || ++tmp >= end) {
		printk(KERN_ERR "property parse failed in %s at line %d\n",
		       __func__, __LINE__);
		return NULL;
	}

	/* now we're on the value */
	*value = tmp;
	tmp += *length;
	if (tmp > end) {
		printk(KERN_ERR "property parse failed in %s at line %d\n",
		       __func__, __LINE__);
		return NULL;
	}
	else if (tmp < end && *tmp != ' ' && *tmp != '\0') {
		printk(KERN_ERR "property parse failed in %s at line %d\n",
		       __func__, __LINE__);
		return NULL;
	}
	tmp++;

	/* and now we should be on the next name, or the end */
	return tmp;
}

static struct property *new_property(const char *name, const int length,
				     const unsigned char *value, struct property *last)
{
	struct property *new = kzalloc(sizeof(*new), GFP_KERNEL);

	if (!new)
		return NULL;

	if (!(new->name = kstrdup(name, GFP_KERNEL)))
		goto cleanup;
	if (!(new->value = kmalloc(length + 1, GFP_KERNEL)))
		goto cleanup;

	memcpy(new->value, value, length);
	*(((char *)new->value) + length) = 0;
	new->length = length;
	new->next = last;
	return new;

cleanup:
	kfree(new->name);
	kfree(new->value);
	kfree(new);
	return NULL;
}

static int do_add_node(char *buf, size_t bufsize)
{
	char *path, *end, *name;
	struct device_node *np;
	struct property *prop = NULL;
	unsigned char* value;
	int length, rv = 0;

	end = buf + bufsize;
	path = buf;
	buf = strchr(buf, ' ');
	if (!buf)
		return -EINVAL;
	*buf = '\0';
	buf++;

	if ((np = of_find_node_by_path(path))) {
		of_node_put(np);
		return -EINVAL;
	}

	/* rv = build_prop_list(tmp, bufsize - (tmp - buf), &proplist); */
	while (buf < end &&
	       (buf = parse_next_property(buf, end, &name, &length, &value))) {
		struct property *last = prop;

		prop = new_property(name, length, value, last);
		if (!prop) {
			rv = -ENOMEM;
			prop = last;
			goto out;
		}
	}
	if (!buf) {
		rv = -EINVAL;
		goto out;
	}

	rv = pSeries_reconfig_add_node(path, prop);

out:
	if (rv)
		release_prop_list(prop);
	return rv;
}

static int do_remove_node(char *buf)
{
	struct device_node *node;
	int rv = -ENODEV;

	if ((node = of_find_node_by_path(buf)))
		rv = pSeries_reconfig_remove_node(node);

	of_node_put(node);
	return rv;
}

static char *parse_node(char *buf, size_t bufsize, struct device_node **npp)
{
	char *handle_str;
	phandle handle;
	*npp = NULL;

	handle_str = buf;

	buf = strchr(buf, ' ');
	if (!buf)
		return NULL;
	*buf = '\0';
	buf++;

	handle = simple_strtoul(handle_str, NULL, 0);

	*npp = of_find_node_by_phandle(handle);
	return buf;
}

static int do_add_property(char *buf, size_t bufsize)
{
	struct property *prop = NULL;
	struct device_node *np;
	unsigned char *value;
	char *name, *end;
	int length;
	end = buf + bufsize;
	buf = parse_node(buf, bufsize, &np);

	if (!np)
		return -ENODEV;

	if (parse_next_property(buf, end, &name, &length, &value) == NULL)
		return -EINVAL;

	prop = new_property(name, length, value, NULL);
	if (!prop)
		return -ENOMEM;

	of_add_property(np, prop);

	return 0;
}

static int do_remove_property(char *buf, size_t bufsize)
{
	struct device_node *np;
	char *tmp;
	buf = parse_node(buf, bufsize, &np);

	if (!np)
		return -ENODEV;

	tmp = strchr(buf,' ');
	if (tmp)
		*tmp = '\0';

	if (strlen(buf) == 0)
		return -EINVAL;

	return of_remove_property(np, of_find_property(np, buf, NULL));
}

static int do_update_property(char *buf, size_t bufsize)
{
	struct device_node *np;
	unsigned char *value;
	char *name, *end, *next_prop;
	int length;
	struct property *newprop;
	buf = parse_node(buf, bufsize, &np);
	end = buf + bufsize;

	if (!np)
		return -ENODEV;

	next_prop = parse_next_property(buf, end, &name, &length, &value);
	if (!next_prop)
		return -EINVAL;

	if (!strlen(name))
		return -ENODEV;

	newprop = new_property(name, length, value, NULL);
	if (!newprop)
		return -ENOMEM;

	if (!strcmp(name, "slb-size") || !strcmp(name, "ibm,slb-size"))
		slb_set_size(*(int *)value);

	return of_update_property(np, newprop);
}

/**
 * ofdt_write - perform operations on the Open Firmware device tree
 *
 * @file: not used
 * @buf: command and arguments
 * @count: size of the command buffer
 * @off: not used
 *
 * Operations supported at this time are addition and removal of
 * whole nodes along with their properties.  Operations on individual
 * properties are not implemented (yet).
 */
static ssize_t ofdt_write(struct file *file, const char __user *buf, size_t count,
			  loff_t *off)
{
	int rv = 0;
	char *kbuf;
	char *tmp;

	if (!(kbuf = kmalloc(count + 1, GFP_KERNEL))) {
		rv = -ENOMEM;
		goto out;
	}
	if (copy_from_user(kbuf, buf, count)) {
		rv = -EFAULT;
		goto out;
	}

	kbuf[count] = '\0';

	tmp = strchr(kbuf, ' ');
	if (!tmp) {
		rv = -EINVAL;
		goto out;
	}
	*tmp = '\0';
	tmp++;

	if (!strcmp(kbuf, "add_node"))
		rv = do_add_node(tmp, count - (tmp - kbuf));
	else if (!strcmp(kbuf, "remove_node"))
		rv = do_remove_node(tmp);
	else if (!strcmp(kbuf, "add_property"))
		rv = do_add_property(tmp, count - (tmp - kbuf));
	else if (!strcmp(kbuf, "remove_property"))
		rv = do_remove_property(tmp, count - (tmp - kbuf));
	else if (!strcmp(kbuf, "update_property"))
		rv = do_update_property(tmp, count - (tmp - kbuf));
	else
		rv = -EINVAL;
out:
	kfree(kbuf);
	return rv ? rv : count;
}

static const struct file_operations ofdt_fops = {
	.write = ofdt_write,
	.llseek = noop_llseek,
};

/* create /proc/powerpc/ofdt write-only by root */
static int proc_ppc64_create_ofdt(void)
{
	struct proc_dir_entry *ent;

	ent = proc_create("powerpc/ofdt", S_IWUSR, NULL, &ofdt_fops);
	if (ent)
		proc_set_size(ent, 0);

	return 0;
}
machine_device_initcall(pseries, proc_ppc64_create_ofdt);
