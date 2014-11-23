/*
 * Functions for dealing with DT resolution
 *
 * Copyright (C) 2012 Pantelis Antoniou <panto@antoniou-consulting.com>
 * Copyright (C) 2012 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>

/* illegal phandle value (set when unresolved) */
#define OF_PHANDLE_ILLEGAL	0xdeadbeef

/**
 * Find a node with the give full name by recursively following any of
 * the child node links.
 */
static struct device_node *__of_find_node_by_full_name(struct device_node *node,
		const char *full_name)
{
	struct device_node *child, *found;

	if (node == NULL)
		return NULL;

	/* check */
	if (of_node_cmp(node->full_name, full_name) == 0)
		return node;

	for_each_child_of_node(node, child) {
		found = __of_find_node_by_full_name(child, full_name);
		if (found != NULL)
			return found;
	}

	return NULL;
}

/*
 * Find live tree's maximum phandle value.
 */
static phandle of_get_tree_max_phandle(void)
{
	struct device_node *node;
	phandle phandle;
	unsigned long flags;

	/* now search recursively */
	raw_spin_lock_irqsave(&devtree_lock, flags);
	phandle = 0;
	for_each_of_allnodes(node) {
		if (node->phandle != OF_PHANDLE_ILLEGAL &&
				node->phandle > phandle)
			phandle = node->phandle;
	}
	raw_spin_unlock_irqrestore(&devtree_lock, flags);

	return phandle;
}

/*
 * Adjust a subtree's phandle values by a given delta.
 * Makes sure not to just adjust the device node's phandle value,
 * but modify the phandle properties values as well.
 */
static void __of_adjust_tree_phandles(struct device_node *node,
		int phandle_delta)
{
	struct device_node *child;
	struct property *prop;
	phandle phandle;

	/* first adjust the node's phandle direct value */
	if (node->phandle != 0 && node->phandle != OF_PHANDLE_ILLEGAL)
		node->phandle += phandle_delta;

	/* now adjust phandle & linux,phandle values */
	for_each_property_of_node(node, prop) {

		/* only look for these two */
		if (of_prop_cmp(prop->name, "phandle") != 0 &&
		    of_prop_cmp(prop->name, "linux,phandle") != 0)
			continue;

		/* must be big enough */
		if (prop->length < 4)
			continue;

		/* read phandle value */
		phandle = be32_to_cpup(prop->value);
		if (phandle == OF_PHANDLE_ILLEGAL)	/* unresolved */
			continue;

		/* adjust */
		*(uint32_t *)prop->value = cpu_to_be32(node->phandle);
	}

	/* now do the children recursively */
	for_each_child_of_node(node, child)
		__of_adjust_tree_phandles(child, phandle_delta);
}

static int __of_adjust_phandle_ref(struct device_node *node, struct property *rprop, int value, bool is_delta)
{
	phandle phandle;
	struct device_node *refnode;
	struct property *sprop;
	char *propval, *propcur, *propend, *nodestr, *propstr, *s;
	int offset, propcurlen;
	int err = 0;

	/* make a copy */
	propval = kmalloc(rprop->length, GFP_KERNEL);
	if (!propval) {
		pr_err("%s: Could not copy value of '%s'\n",
				__func__, rprop->name);
		return -ENOMEM;
	}
	memcpy(propval, rprop->value, rprop->length);

	propend = propval + rprop->length;
	for (propcur = propval; propcur < propend; propcur += propcurlen + 1) {
		propcurlen = strlen(propcur);

		nodestr = propcur;
		s = strchr(propcur, ':');
		if (!s) {
			pr_err("%s: Illegal symbol entry '%s' (1)\n",
				__func__, propcur);
			err = -EINVAL;
			goto err_fail;
		}
		*s++ = '\0';

		propstr = s;
		s = strchr(s, ':');
		if (!s) {
			pr_err("%s: Illegal symbol entry '%s' (2)\n",
				__func__, (char *)rprop->value);
			err = -EINVAL;
			goto err_fail;
		}

		*s++ = '\0';
		err = kstrtoint(s, 10, &offset);
		if (err != 0) {
			pr_err("%s: Could get offset '%s'\n",
				__func__, (char *)rprop->value);
			goto err_fail;
		}

		/* look into the resolve node for the full path */
		refnode = __of_find_node_by_full_name(node, nodestr);
		if (!refnode) {
			pr_warn("%s: Could not find refnode '%s'\n",
				__func__, (char *)rprop->value);
			continue;
		}

		/* now find the property */
		for_each_property_of_node(refnode, sprop) {
			if (of_prop_cmp(sprop->name, propstr) == 0)
				break;
		}

		if (!sprop) {
			pr_err("%s: Could not find property '%s'\n",
				__func__, (char *)rprop->value);
			err = -ENOENT;
			goto err_fail;
		}

		phandle = is_delta ? be32_to_cpup(sprop->value + offset) + value : value;
		*(__be32 *)(sprop->value + offset) = cpu_to_be32(phandle);
	}

err_fail:
	kfree(propval);
	return err;
}

/*
 * Adjust the local phandle references by the given phandle delta.
 * Assumes the existances of a __local_fixups__ node at the root
 * of the tree. Does not take any devtree locks so make sure you
 * call this on a tree which is at the detached state.
 */
static int __of_adjust_tree_phandle_references(struct device_node *node,
		int phandle_delta)
{
	struct device_node *child;
	struct property *rprop;
	int err;

	/* locate the symbols & fixups nodes on resolve */
	for_each_child_of_node(node, child)
		if (of_node_cmp(child->name, "__local_fixups__") == 0)
			break;

	/* no local fixups */
	if (!child)
		return 0;

	/* find the local fixups property */
	for_each_property_of_node(child, rprop) {
		/* skip properties added automatically */
		if (of_prop_cmp(rprop->name, "name") == 0)
			continue;

		err = __of_adjust_phandle_ref(node, rprop, phandle_delta, true);
		if (err)
			return err;
	}

	return 0;
}

/**
 * of_resolve	- Resolve the given node against the live tree.
 *
 * @resolve:	Node to resolve
 *
 * Perform dynamic Device Tree resolution against the live tree
 * to the given node to resolve. This depends on the live tree
 * having a __symbols__ node, and the resolve node the __fixups__ &
 * __local_fixups__ nodes (if needed).
 * The result of the operation is a resolve node that it's contents
 * are fit to be inserted or operate upon the live tree.
 * Returns 0 on success or a negative error value on error.
 */
int of_resolve_phandles(struct device_node *resolve)
{
	struct device_node *child, *refnode;
	struct device_node *root_sym, *resolve_sym, *resolve_fix;
	struct property *rprop;
	const char *refpath;
	phandle phandle, phandle_delta;
	int err;

	/* the resolve node must exist, and be detached */
	if (!resolve || !of_node_check_flag(resolve, OF_DETACHED))
		return -EINVAL;

	/* first we need to adjust the phandles */
	phandle_delta = of_get_tree_max_phandle() + 1;
	__of_adjust_tree_phandles(resolve, phandle_delta);
	err = __of_adjust_tree_phandle_references(resolve, phandle_delta);
	if (err != 0)
		return err;

	root_sym = NULL;
	resolve_sym = NULL;
	resolve_fix = NULL;

	/* this may fail (if no fixups are required) */
	root_sym = of_find_node_by_path("/__symbols__");

	/* locate the symbols & fixups nodes on resolve */
	for_each_child_of_node(resolve, child) {

		if (!resolve_sym &&
				of_node_cmp(child->name, "__symbols__") == 0)
			resolve_sym = child;

		if (!resolve_fix &&
				of_node_cmp(child->name, "__fixups__") == 0)
			resolve_fix = child;

		/* both found, don't bother anymore */
		if (resolve_sym && resolve_fix)
			break;
	}

	/* we do allow for the case where no fixups are needed */
	if (!resolve_fix) {
		err = 0;	/* no error */
		goto out;
	}

	/* we need to fixup, but no root symbols... */
	if (!root_sym) {
		err = -EINVAL;
		goto out;
	}

	for_each_property_of_node(resolve_fix, rprop) {

		/* skip properties added automatically */
		if (of_prop_cmp(rprop->name, "name") == 0)
			continue;

		err = of_property_read_string(root_sym,
				rprop->name, &refpath);
		if (err != 0) {
			pr_err("%s: Could not find symbol '%s'\n",
					__func__, rprop->name);
			goto out;
		}

		refnode = of_find_node_by_path(refpath);
		if (!refnode) {
			pr_err("%s: Could not find node by path '%s'\n",
					__func__, refpath);
			err = -ENOENT;
			goto out;
		}

		phandle = refnode->phandle;
		of_node_put(refnode);

		pr_debug("%s: %s phandle is 0x%08x\n",
				__func__, rprop->name, phandle);

		err = __of_adjust_phandle_ref(resolve, rprop, phandle, false);
		if (err)
			break;
	}

out:
	/* NULL is handled by of_node_put as NOP */
	of_node_put(root_sym);

	return err;
}
EXPORT_SYMBOL_GPL(of_resolve_phandles);
