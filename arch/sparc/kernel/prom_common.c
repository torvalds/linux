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
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <asm/prom.h>
#include <asm/oplib.h>

#include "prom.h"

struct device_node *of_find_node_by_phandle(phandle handle)
{
	struct device_node *np;

	for (np = allnodes; np; np = np->allnext)
		if (np->node == handle)
			break;

	return np;
}
EXPORT_SYMBOL(of_find_node_by_phandle);

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

	new_val = kmalloc(len, GFP_KERNEL);
	if (!new_val)
		return -ENOMEM;

	memcpy(new_val, val, len);

	err = -ENODEV;

	write_lock(&devtree_lock);
	prevp = &dp->properties;
	while (*prevp) {
		struct property *prop = *prevp;

		if (!strcasecmp(prop->name, name)) {
			void *old_val = prop->value;
			int ret;

			mutex_lock(&of_set_property_mutex);
			ret = prom_setprop(dp->node, name, val, len);
			mutex_unlock(&of_set_property_mutex);

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

unsigned int prom_unique_id;

static struct property * __init build_one_prop(phandle node, char *prev,
					       char *special_name,
					       void *special_val,
					       int special_len)
{
	static struct property *tmp = NULL;
	struct property *p;
	const char *name;

	if (tmp) {
		p = tmp;
		memset(p, 0, sizeof(*p) + 32);
		tmp = NULL;
	} else {
		p = prom_early_alloc(sizeof(struct property) + 32);
		p->unique_id = prom_unique_id++;
	}

	p->name = (char *) (p + 1);
	if (special_name) {
		strcpy(p->name, special_name);
		p->length = special_len;
		p->value = prom_early_alloc(special_len);
		memcpy(p->value, special_val, special_len);
	} else {
#ifdef CONFIG_SPARC32
		if (prev == NULL) {
			name = prom_firstprop(node, NULL);
		} else {
			name = prom_nextprop(node, prev, NULL);
		}
#else
		if (prev == NULL) {
			prom_firstprop(node, p->name);
		} else {
			prom_nextprop(node, prev, p->name);
		}
		name = p->name;
#endif
		if (strlen(name) == 0) {
			tmp = p;
			return NULL;
		}
#ifdef CONFIG_SPARC32
		strcpy(p->name, name);
#endif
		p->length = prom_getproplen(node, p->name);
		if (p->length <= 0) {
			p->length = 0;
		} else {
			int len;

			p->value = prom_early_alloc(p->length + 1);
			len = prom_getproperty(node, p->name, p->value,
					       p->length);
			if (len <= 0)
				p->length = 0;
			((unsigned char *)p->value)[p->length] = '\0';
		}
	}
	return p;
}

struct property * __init build_prop_list(phandle node)
{
	struct property *head, *tail;

	head = tail = build_one_prop(node, NULL,
				     ".node", &node, sizeof(node));

	tail->next = build_one_prop(node, NULL, NULL, NULL, 0);
	tail = tail->next;
	while(tail) {
		tail->next = build_one_prop(node, tail->name,
					    NULL, NULL, 0);
		tail = tail->next;
	}

	return head;
}
