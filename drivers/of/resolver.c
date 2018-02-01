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

#define pr_fmt(fmt)	"OF: resolver: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "of_private.h"

/* illegal phandle value (set when unresolved) */
#define OF_PHANDLE_ILLEGAL	0xdeadbeef

static phandle live_tree_max_phandle(void)
{
	struct device_node *node;
	phandle phandle;
	unsigned long flags;

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

static void adjust_overlay_phandles(struct device_node *overlay,
		int phandle_delta)
{
	struct device_node *child;
	struct property *prop;
	phandle phandle;

	/* adjust node's phandle in node */
	if (overlay->phandle != 0 && overlay->phandle != OF_PHANDLE_ILLEGAL)
		overlay->phandle += phandle_delta;

	/* copy adjusted phandle into *phandle properties */
	for_each_property_of_node(overlay, prop) {

		if (of_prop_cmp(prop->name, "phandle") &&
		    of_prop_cmp(prop->name, "linux,phandle"))
			continue;

		if (prop->length < 4)
			continue;

		phandle = be32_to_cpup(prop->value);
		if (phandle == OF_PHANDLE_ILLEGAL)
			continue;

		*(__be32 *)prop->value = cpu_to_be32(overlay->phandle);
	}

	for_each_child_of_node(overlay, child)
		adjust_overlay_phandles(child, phandle_delta);
}

static int update_usages_of_a_phandle_reference(struct device_node *overlay,
		struct property *prop_fixup, phandle phandle)
{
	struct device_node *refnode;
	struct property *prop;
	char *value, *cur, *end, *node_path, *prop_name, *s;
	int offset, len;
	int err = 0;

	value = kmemdup(prop_fixup->value, prop_fixup->length, GFP_KERNEL);
	if (!value)
		return -ENOMEM;

	/* prop_fixup contains a list of tuples of path:property_name:offset */
	end = value + prop_fixup->length;
	for (cur = value; cur < end; cur += len + 1) {
		len = strlen(cur);

		node_path = cur;
		s = strchr(cur, ':');
		if (!s) {
			err = -EINVAL;
			goto err_fail;
		}
		*s++ = '\0';

		prop_name = s;
		s = strchr(s, ':');
		if (!s) {
			err = -EINVAL;
			goto err_fail;
		}
		*s++ = '\0';

		err = kstrtoint(s, 10, &offset);
		if (err)
			goto err_fail;

		refnode = __of_find_node_by_full_path(of_node_get(overlay), node_path);
		if (!refnode)
			continue;

		for_each_property_of_node(refnode, prop) {
			if (!of_prop_cmp(prop->name, prop_name))
				break;
		}
		of_node_put(refnode);

		if (!prop) {
			err = -ENOENT;
			goto err_fail;
		}

		*(__be32 *)(prop->value + offset) = cpu_to_be32(phandle);
	}

err_fail:
	kfree(value);
	return err;
}

/* compare nodes taking into account that 'name' strips out the @ part */
static int node_name_cmp(const struct device_node *dn1,
		const struct device_node *dn2)
{
	const char *n1 = kbasename(dn1->full_name);
	const char *n2 = kbasename(dn2->full_name);

	return of_node_cmp(n1, n2);
}

/*
 * Adjust the local phandle references by the given phandle delta.
 *
 * Subtree @local_fixups, which is overlay node __local_fixups__,
 * mirrors the fragment node structure at the root of the overlay.
 *
 * For each property in the fragments that contains a phandle reference,
 * @local_fixups has a property of the same name that contains a list
 * of offsets of the phandle reference(s) within the respective property
 * value(s).  The values at these offsets will be fixed up.
 */
static int adjust_local_phandle_references(struct device_node *local_fixups,
		struct device_node *overlay, int phandle_delta)
{
	struct device_node *child, *overlay_child;
	struct property *prop_fix, *prop;
	int err, i, count;
	unsigned int off;

	if (!local_fixups)
		return 0;

	for_each_property_of_node(local_fixups, prop_fix) {

		/* skip properties added automatically */
		if (!of_prop_cmp(prop_fix->name, "name") ||
		    !of_prop_cmp(prop_fix->name, "phandle") ||
		    !of_prop_cmp(prop_fix->name, "linux,phandle"))
			continue;

		if ((prop_fix->length % 4) != 0 || prop_fix->length == 0)
			return -EINVAL;
		count = prop_fix->length / sizeof(__be32);

		for_each_property_of_node(overlay, prop) {
			if (!of_prop_cmp(prop->name, prop_fix->name))
				break;
		}

		if (!prop)
			return -EINVAL;

		for (i = 0; i < count; i++) {
			off = be32_to_cpu(((__be32 *)prop_fix->value)[i]);
			if ((off + 4) > prop->length)
				return -EINVAL;

			be32_add_cpu(prop->value + off, phandle_delta);
		}
	}

	/*
	 * These nested loops recurse down two subtrees in parallel, where the
	 * node names in the two subtrees match.
	 *
	 * The roots of the subtrees are the overlay's __local_fixups__ node
	 * and the overlay's root node.
	 */
	for_each_child_of_node(local_fixups, child) {

		for_each_child_of_node(overlay, overlay_child)
			if (!node_name_cmp(child, overlay_child))
				break;

		if (!overlay_child)
			return -EINVAL;

		err = adjust_local_phandle_references(child, overlay_child,
				phandle_delta);
		if (err)
			return err;
	}

	return 0;
}

/**
 * of_resolve_phandles - Relocate and resolve overlay against live tree
 *
 * @overlay:	Pointer to devicetree overlay to relocate and resolve
 *
 * Modify (relocate) values of local phandles in @overlay to a range that
 * does not conflict with the live expanded devicetree.  Update references
 * to the local phandles in @overlay.  Update (resolve) phandle references
 * in @overlay that refer to the live expanded devicetree.
 *
 * Phandle values in the live tree are in the range of
 * 1 .. live_tree_max_phandle().  The range of phandle values in the overlay
 * also begin with at 1.  Adjust the phandle values in the overlay to begin
 * at live_tree_max_phandle() + 1.  Update references to the phandles to
 * the adjusted phandle values.
 *
 * The name of each property in the "__fixups__" node in the overlay matches
 * the name of a symbol (a label) in the live tree.  The values of each
 * property in the "__fixups__" node is a list of the property values in the
 * overlay that need to be updated to contain the phandle reference
 * corresponding to that symbol in the live tree.  Update the references in
 * the overlay with the phandle values in the live tree.
 *
 * @overlay must be detached.
 *
 * Resolving and applying @overlay to the live expanded devicetree must be
 * protected by a mechanism to ensure that multiple overlays are processed
 * in a single threaded manner so that multiple overlays will not relocate
 * phandles to overlapping ranges.  The mechanism to enforce this is not
 * yet implemented.
 *
 * Return: %0 on success or a negative error value on error.
 */
int of_resolve_phandles(struct device_node *overlay)
{
	struct device_node *child, *local_fixups, *refnode;
	struct device_node *tree_symbols, *overlay_fixups;
	struct property *prop;
	const char *refpath;
	phandle phandle, phandle_delta;
	int err;

	tree_symbols = NULL;

	if (!overlay) {
		pr_err("null overlay\n");
		err = -EINVAL;
		goto out;
	}

#if 0
	Temporarily disable check so that old style overlay unittests
	do not fail when of_resolve_phandles() is moved into
	of_overlay_apply().

	if (!of_node_check_flag(overlay, OF_DETACHED)) {
		pr_err("overlay not detached\n");
		err = -EINVAL;
		goto out;
	}
#endif

	phandle_delta = live_tree_max_phandle() + 1;
	adjust_overlay_phandles(overlay, phandle_delta);

	for_each_child_of_node(overlay, local_fixups)
		if (!of_node_cmp(local_fixups->name, "__local_fixups__"))
			break;

	err = adjust_local_phandle_references(local_fixups, overlay, phandle_delta);
	if (err)
		goto out;

	overlay_fixups = NULL;

	for_each_child_of_node(overlay, child) {
		if (!of_node_cmp(child->name, "__fixups__"))
			overlay_fixups = child;
	}

	if (!overlay_fixups) {
		err = 0;
		goto out;
	}

	tree_symbols = of_find_node_by_path("/__symbols__");
	if (!tree_symbols) {
		pr_err("no symbols in root of device tree.\n");
		err = -EINVAL;
		goto out;
	}

	for_each_property_of_node(overlay_fixups, prop) {

		/* skip properties added automatically */
		if (!of_prop_cmp(prop->name, "name"))
			continue;

		err = of_property_read_string(tree_symbols,
				prop->name, &refpath);
		if (err)
			goto out;

		refnode = of_find_node_by_path(refpath);
		if (!refnode) {
			err = -ENOENT;
			goto out;
		}

		phandle = refnode->phandle;
		of_node_put(refnode);

		err = update_usages_of_a_phandle_reference(overlay, prop, phandle);
		if (err)
			break;
	}

out:
	if (err)
		pr_err("overlay phandle fixup failed: %d\n", err);
	of_node_put(tree_symbols);

	return err;
}
EXPORT_SYMBOL_GPL(of_resolve_phandles);
