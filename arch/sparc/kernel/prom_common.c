/* prom_common.c: OF device tree support common code.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com
 *
 *  Adapted for sparc by David S. Miller davem@davemloft.net
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_pdt.h>
#include <asm/prom.h>
#include <asm/oplib.h>

#include "prom.h"

struct device_node *of_console_device;
EXPORT_SYMBOL(of_console_device);

char *of_console_path;
EXPORT_SYMBOL(of_console_path);

char *of_console_options;
EXPORT_SYMBOL(of_console_options);

int of_getintprop_default(struct device_node *np, const char *name, int def)
{
	struct property *prop;
	int len;

	prop = of_find_property(np, name, &len);
	if (!prop || len != 4)
		return def;

	return *(int *) prop->value;
}
EXPORT_SYMBOL(of_getintprop_default);

DEFINE_MUTEX(of_set_property_mutex);
EXPORT_SYMBOL(of_set_property_mutex);

int of_set_property(struct device_node *dp, const char *name, void *val, int len)
{
	struct property **prevp;
	void *new_val;
	int err;

	new_val = kmemdup(val, len, GFP_KERNEL);
	if (!new_val)
		return -ENOMEM;

	err = -ENODEV;

	mutex_lock(&of_set_property_mutex);
	write_lock(&devtree_lock);
	prevp = &dp->properties;
	while (*prevp) {
		struct property *prop = *prevp;

		if (!strcasecmp(prop->name, name)) {
			void *old_val = prop->value;
			int ret;

			ret = prom_setprop(dp->phandle, name, val, len);

			err = -EINVAL;
			if (ret >= 0) {
				prop->value = new_val;
				prop->length = len;

				if (OF_IS_DYNAMIC(prop))
					kfree(old_val);

				OF_MARK_DYNAMIC(prop);

				err = 0;
			}
			break;
		}
		prevp = &(*prevp)->next;
	}
	write_unlock(&devtree_lock);
	mutex_unlock(&of_set_property_mutex);

	/* XXX Upate procfs if necessary... */

	return err;
}
EXPORT_SYMBOL(of_set_property);

int of_find_in_proplist(const char *list, const char *match, int len)
{
	while (len > 0) {
		int l;

		if (!strcmp(list, match))
			return 1;
		l = strlen(list) + 1;
		list += l;
		len -= l;
	}
	return 0;
}
EXPORT_SYMBOL(of_find_in_proplist);

/*
 * SPARC32 and SPARC64's prom_nextprop() do things differently
 * here, despite sharing the same interface.  SPARC32 doesn't fill in 'buf',
 * returning NULL on an error.  SPARC64 fills in 'buf', but sets it to an
 * empty string upon error.
 */
static int __init handle_nextprop_quirks(char *buf, const char *name)
{
	if (!name || strlen(name) == 0)
		return -1;

#ifdef CONFIG_SPARC32
	strcpy(buf, name);
#endif
	return 0;
}

static int __init prom_common_nextprop(phandle node, char *prev, char *buf)
{
	const char *name;

	buf[0] = '\0';
	name = prom_nextprop(node, prev, buf);
	return handle_nextprop_quirks(buf, name);
}

unsigned int prom_early_allocated __initdata;

static struct of_pdt_ops prom_sparc_ops __initdata = {
	.nextprop = prom_common_nextprop,
	.getproplen = prom_getproplen,
	.getproperty = prom_getproperty,
	.getchild = prom_getchild,
	.getsibling = prom_getsibling,
};

void __init prom_build_devicetree(void)
{
	of_pdt_build_devicetree(prom_root_node, &prom_sparc_ops);
	of_console_init();

	pr_info("PROM: Built device tree with %u bytes of memory.\n",
			prom_early_allocated);
}
