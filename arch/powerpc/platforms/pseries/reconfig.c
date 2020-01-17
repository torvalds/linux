// SPDX-License-Identifier: GPL-2.0-only
/*
 * pSeries_reconfig.c - support for dynamic reconfiguration (including PCI
 * Hotplug and Dynamic Logical Partitioning on RPA platforms).
 *
 * Copyright (C) 2005 Nathan Lynch
 * Copyright (C) 2005 IBM Corporation
 */

#include <linux/kernel.h>
#include <linux/yestifier.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/of.h>

#include <asm/prom.h>
#include <asm/machdep.h>
#include <linux/uaccess.h>
#include <asm/mmu.h>

#include "of_helpers.h"

static int pSeries_reconfig_add_yesde(const char *path, struct property *proplist)
{
	struct device_yesde *np;
	int err = -ENOMEM;

	np = kzalloc(sizeof(*np), GFP_KERNEL);
	if (!np)
		goto out_err;

	np->full_name = kstrdup(kbasename(path), GFP_KERNEL);
	if (!np->full_name)
		goto out_err;

	np->properties = proplist;
	of_yesde_set_flag(np, OF_DYNAMIC);
	of_yesde_init(np);

	np->parent = pseries_of_derive_parent(path);
	if (IS_ERR(np->parent)) {
		err = PTR_ERR(np->parent);
		goto out_err;
	}

	err = of_attach_yesde(np);
	if (err) {
		printk(KERN_ERR "Failed to add device yesde %s\n", path);
		goto out_err;
	}

	of_yesde_put(np->parent);

	return 0;

out_err:
	if (np) {
		of_yesde_put(np->parent);
		kfree(np->full_name);
		kfree(np);
	}
	return err;
}

static int pSeries_reconfig_remove_yesde(struct device_yesde *np)
{
	struct device_yesde *parent, *child;

	parent = of_get_parent(np);
	if (!parent)
		return -EINVAL;

	if ((child = of_get_next_child(np, NULL))) {
		of_yesde_put(child);
		of_yesde_put(parent);
		return -EBUSY;
	}

	of_detach_yesde(np);
	of_yesde_put(parent);
	return 0;
}

/*
 * /proc/powerpc/ofdt - yucky binary interface for adding and removing
 * OF device yesdes.  Should be deprecated as soon as we get an
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
 * this function does yes allocation or copying of the data.  Return value
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

	/* yesw we're on the length */
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

	/* yesw we're on the value */
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

	/* and yesw we should be on the next name, or the end */
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

static int do_add_yesde(char *buf, size_t bufsize)
{
	char *path, *end, *name;
	struct device_yesde *np;
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

	if ((np = of_find_yesde_by_path(path))) {
		of_yesde_put(np);
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

	rv = pSeries_reconfig_add_yesde(path, prop);

out:
	if (rv)
		release_prop_list(prop);
	return rv;
}

static int do_remove_yesde(char *buf)
{
	struct device_yesde *yesde;
	int rv = -ENODEV;

	if ((yesde = of_find_yesde_by_path(buf)))
		rv = pSeries_reconfig_remove_yesde(yesde);

	of_yesde_put(yesde);
	return rv;
}

static char *parse_yesde(char *buf, size_t bufsize, struct device_yesde **npp)
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

	*npp = of_find_yesde_by_phandle(handle);
	return buf;
}

static int do_add_property(char *buf, size_t bufsize)
{
	struct property *prop = NULL;
	struct device_yesde *np;
	unsigned char *value;
	char *name, *end;
	int length;
	end = buf + bufsize;
	buf = parse_yesde(buf, bufsize, &np);

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
	struct device_yesde *np;
	char *tmp;
	buf = parse_yesde(buf, bufsize, &np);

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
	struct device_yesde *np;
	unsigned char *value;
	char *name, *end, *next_prop;
	int length;
	struct property *newprop;
	buf = parse_yesde(buf, bufsize, &np);
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
 * @file: yest used
 * @buf: command and arguments
 * @count: size of the command buffer
 * @off: yest used
 *
 * Operations supported at this time are addition and removal of
 * whole yesdes along with their properties.  Operations on individual
 * properties are yest implemented (yet).
 */
static ssize_t ofdt_write(struct file *file, const char __user *buf, size_t count,
			  loff_t *off)
{
	int rv;
	char *kbuf;
	char *tmp;

	kbuf = memdup_user_nul(buf, count);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	tmp = strchr(kbuf, ' ');
	if (!tmp) {
		rv = -EINVAL;
		goto out;
	}
	*tmp = '\0';
	tmp++;

	if (!strcmp(kbuf, "add_yesde"))
		rv = do_add_yesde(tmp, count - (tmp - kbuf));
	else if (!strcmp(kbuf, "remove_yesde"))
		rv = do_remove_yesde(tmp);
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
	.llseek = yesop_llseek,
};

/* create /proc/powerpc/ofdt write-only by root */
static int proc_ppc64_create_ofdt(void)
{
	struct proc_dir_entry *ent;

	ent = proc_create("powerpc/ofdt", 0200, NULL, &ofdt_fops);
	if (ent)
		proc_set_size(ent, 0);

	return 0;
}
machine_device_initcall(pseries, proc_ppc64_create_ofdt);
