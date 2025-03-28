// SPDX-License-Identifier: GPL-2.0+
/* pdt.c: OF PROM device tree support code.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com
 *
 *  Adapted for sparc by David S. Miller davem@davemloft.net
 *  Adapted for multiple architectures by Andres Salomon <dilinger@queued.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_pdt.h>

#include "of_private.h"

static struct of_pdt_ops *of_pdt_prom_ops __initdata;

#if defined(CONFIG_SPARC)
unsigned int of_pdt_unique_id __initdata;

#define of_pdt_incr_unique_id(p) do { \
	(p)->unique_id = of_pdt_unique_id++; \
} while (0)

static char * __init of_pdt_build_full_name(struct device_node *dp)
{
	return build_path_component(dp);
}

#else /* CONFIG_SPARC */

static inline void of_pdt_incr_unique_id(void *p) { }
static inline void irq_trans_init(struct device_node *dp) { }

static char * __init of_pdt_build_full_name(struct device_node *dp)
{
	static int failsafe_id = 0; /* for generating unique names on failure */
	const char *name;
	char path[256];
	char *buf;
	int len;

	if (!of_pdt_prom_ops->pkg2path(dp->phandle, path, sizeof(path), &len)) {
		name = kbasename(path);
		buf = prom_early_alloc(strlen(name) + 1);
		strcpy(buf, name);
		return buf;
	}

	name = of_get_property(dp, "name", &len);
	buf = prom_early_alloc(len + 16);
	sprintf(buf, "%s@unknown%i", name, failsafe_id++);
	pr_err("%s: pkg2path failed; assigning %s\n", __func__, buf);
	return buf;
}

#endif /* !CONFIG_SPARC */

static struct property * __init of_pdt_build_one_prop(phandle node, char *prev,
					       char *special_name,
					       void *special_val,
					       int special_len)
{
	static struct property *tmp = NULL;
	struct property *p;
	int err;

	if (tmp) {
		p = tmp;
		memset(p, 0, sizeof(*p) + 32);
		tmp = NULL;
	} else {
		p = prom_early_alloc(sizeof(struct property) + 32);
		of_pdt_incr_unique_id(p);
	}

	p->name = (char *) (p + 1);
	if (special_name) {
		strcpy(p->name, special_name);
		p->length = special_len;
		p->value = prom_early_alloc(special_len);
		memcpy(p->value, special_val, special_len);
	} else {
		err = of_pdt_prom_ops->nextprop(node, prev, p->name);
		if (err) {
			tmp = p;
			return NULL;
		}
		p->length = of_pdt_prom_ops->getproplen(node, p->name);
		if (p->length <= 0) {
			p->length = 0;
		} else {
			int len;

			p->value = prom_early_alloc(p->length + 1);
			len = of_pdt_prom_ops->getproperty(node, p->name,
					p->value, p->length);
			if (len <= 0)
				p->length = 0;
			((unsigned char *)p->value)[p->length] = '\0';
		}
	}
	return p;
}

static struct property * __init of_pdt_build_prop_list(phandle node)
{
	struct property *head, *tail;

	head = tail = of_pdt_build_one_prop(node, NULL,
				     ".node", &node, sizeof(node));

	tail->next = of_pdt_build_one_prop(node, NULL, NULL, NULL, 0);
	tail = tail->next;
	while(tail) {
		tail->next = of_pdt_build_one_prop(node, tail->name,
					    NULL, NULL, 0);
		tail = tail->next;
	}

	return head;
}

static char * __init of_pdt_get_one_property(phandle node, const char *name)
{
	char *buf = "<NULL>";
	int len;

	len = of_pdt_prom_ops->getproplen(node, name);
	if (len > 0) {
		buf = prom_early_alloc(len);
		len = of_pdt_prom_ops->getproperty(node, name, buf, len);
	}

	return buf;
}

static struct device_node * __init of_pdt_create_node(phandle node,
						    struct device_node *parent)
{
	struct device_node *dp;

	if (!node)
		return NULL;

	dp = prom_early_alloc(sizeof(*dp));
	of_node_init(dp);
	of_pdt_incr_unique_id(dp);
	dp->parent = parent;

	dp->name = of_pdt_get_one_property(node, "name");
	dp->phandle = node;

	dp->properties = of_pdt_build_prop_list(node);

	dp->full_name = of_pdt_build_full_name(dp);

	irq_trans_init(dp);

	return dp;
}

static struct device_node * __init of_pdt_build_tree(struct device_node *parent,
						   phandle node)
{
	struct device_node *ret = NULL, *prev_sibling = NULL;
	struct device_node *dp;

	while (1) {
		dp = of_pdt_create_node(node, parent);
		if (!dp)
			break;

		if (prev_sibling)
			prev_sibling->sibling = dp;

		if (!ret)
			ret = dp;
		prev_sibling = dp;

		dp->child = of_pdt_build_tree(dp, of_pdt_prom_ops->getchild(node));

		node = of_pdt_prom_ops->getsibling(node);
	}

	return ret;
}

static void * __init kernel_tree_alloc(u64 size, u64 align)
{
	return prom_early_alloc(size);
}

void __init of_pdt_build_devicetree(phandle root_node, struct of_pdt_ops *ops)
{
	BUG_ON(!ops);
	of_pdt_prom_ops = ops;

	of_root = of_pdt_create_node(root_node, NULL);
	of_root->full_name = "/";

	of_root->child = of_pdt_build_tree(of_root,
				of_pdt_prom_ops->getchild(of_root->phandle));

	/* Get pointer to "/chosen" and "/aliases" nodes for use everywhere */
	of_alias_scan(kernel_tree_alloc);
}
