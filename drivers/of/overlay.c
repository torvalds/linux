// SPDX-License-Identifier: GPL-2.0
/*
 * Functions for working with device tree overlays
 *
 * Copyright (C) 2012 Pantelis Antoniou <panto@antoniou-consulting.com>
 * Copyright (C) 2012 Texas Instruments Inc.
 */

#define pr_fmt(fmt)	"OF: overlay: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_fdt.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/libfdt.h>
#include <linux/err.h>
#include <linux/idr.h>

#include "of_private.h"

/**
 * struct target - info about current target node as recursing through overlay
 * @np:			node where current level of overlay will be applied
 * @in_livetree:	@np is a node in the live devicetree
 *
 * Used in the algorithm to create the portion of a changeset that describes
 * an overlay fragment, which is a devicetree subtree.  Initially @np is a node
 * in the live devicetree where the overlay subtree is targeted to be grafted
 * into.  When recursing to the next level of the overlay subtree, the target
 * also recurses to the next level of the live devicetree, as long as overlay
 * subtree node also exists in the live devicetree.  When a node in the overlay
 * subtree does not exist at the same level in the live devicetree, target->np
 * points to a newly allocated node, and all subsequent targets in the subtree
 * will be newly allocated nodes.
 */
struct target {
	struct device_node *np;
	bool in_livetree;
};

/**
 * struct fragment - info about fragment nodes in overlay expanded device tree
 * @target:	target of the overlay operation
 * @overlay:	pointer to the __overlay__ node
 */
struct fragment {
	struct device_node *overlay;
	struct device_node *target;
};

/**
 * struct overlay_changeset
 * @id:			changeset identifier
 * @ovcs_list:		list on which we are located
 * @fdt:		base of memory allocated to hold aligned FDT that was unflattened to create @overlay_tree
 * @overlay_tree:	expanded device tree that contains the fragment nodes
 * @count:		count of fragment structures
 * @fragments:		fragment nodes in the overlay expanded device tree
 * @symbols_fragment:	last element of @fragments[] is the  __symbols__ node
 * @cset:		changeset to apply fragments to live device tree
 */
struct overlay_changeset {
	int id;
	struct list_head ovcs_list;
	const void *fdt;
	struct device_node *overlay_tree;
	int count;
	struct fragment *fragments;
	bool symbols_fragment;
	struct of_changeset cset;
};

/* flags are sticky - once set, do not reset */
static int devicetree_state_flags;
#define DTSF_APPLY_FAIL		0x01
#define DTSF_REVERT_FAIL	0x02

/*
 * If a changeset apply or revert encounters an error, an attempt will
 * be made to undo partial changes, but may fail.  If the undo fails
 * we do not know the state of the devicetree.
 */
static int devicetree_corrupt(void)
{
	return devicetree_state_flags &
		(DTSF_APPLY_FAIL | DTSF_REVERT_FAIL);
}

static int build_changeset_next_level(struct overlay_changeset *ovcs,
		struct target *target, const struct device_node *overlay_node);

/*
 * of_resolve_phandles() finds the largest phandle in the live tree.
 * of_overlay_apply() may add a larger phandle to the live tree.
 * Do not allow race between two overlays being applied simultaneously:
 *    mutex_lock(&of_overlay_phandle_mutex)
 *    of_resolve_phandles()
 *    of_overlay_apply()
 *    mutex_unlock(&of_overlay_phandle_mutex)
 */
static DEFINE_MUTEX(of_overlay_phandle_mutex);

void of_overlay_mutex_lock(void)
{
	mutex_lock(&of_overlay_phandle_mutex);
}

void of_overlay_mutex_unlock(void)
{
	mutex_unlock(&of_overlay_phandle_mutex);
}


static LIST_HEAD(ovcs_list);
static DEFINE_IDR(ovcs_idr);

static BLOCKING_NOTIFIER_HEAD(overlay_notify_chain);

/**
 * of_overlay_notifier_register() - Register notifier for overlay operations
 * @nb:		Notifier block to register
 *
 * Register for notification on overlay operations on device tree nodes. The
 * reported actions definied by @of_reconfig_change. The notifier callback
 * furthermore receives a pointer to the affected device tree node.
 *
 * Note that a notifier callback is not supposed to store pointers to a device
 * tree node or its content beyond @OF_OVERLAY_POST_REMOVE corresponding to the
 * respective node it received.
 */
int of_overlay_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&overlay_notify_chain, nb);
}
EXPORT_SYMBOL_GPL(of_overlay_notifier_register);

/**
 * of_overlay_notifier_register() - Unregister notifier for overlay operations
 * @nb:		Notifier block to unregister
 */
int of_overlay_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&overlay_notify_chain, nb);
}
EXPORT_SYMBOL_GPL(of_overlay_notifier_unregister);

static char *of_overlay_action_name[] = {
	"pre-apply",
	"post-apply",
	"pre-remove",
	"post-remove",
};

static int overlay_notify(struct overlay_changeset *ovcs,
		enum of_overlay_notify_action action)
{
	struct of_overlay_notify_data nd;
	int i, ret;

	for (i = 0; i < ovcs->count; i++) {
		struct fragment *fragment = &ovcs->fragments[i];

		nd.target = fragment->target;
		nd.overlay = fragment->overlay;

		ret = blocking_notifier_call_chain(&overlay_notify_chain,
						   action, &nd);
		if (ret == NOTIFY_OK || ret == NOTIFY_STOP)
			return 0;
		if (ret) {
			ret = notifier_to_errno(ret);
			pr_err("overlay changeset %s notifier error %d, target: %pOF\n",
			       of_overlay_action_name[action], ret, nd.target);
			return ret;
		}
	}

	return 0;
}

/*
 * The values of properties in the "/__symbols__" node are paths in
 * the ovcs->overlay_tree.  When duplicating the properties, the paths
 * need to be adjusted to be the correct path for the live device tree.
 *
 * The paths refer to a node in the subtree of a fragment node's "__overlay__"
 * node, for example "/fragment@0/__overlay__/symbol_path_tail",
 * where symbol_path_tail can be a single node or it may be a multi-node path.
 *
 * The duplicated property value will be modified by replacing the
 * "/fragment_name/__overlay/" portion of the value  with the target
 * path from the fragment node.
 */
static struct property *dup_and_fixup_symbol_prop(
		struct overlay_changeset *ovcs, const struct property *prop)
{
	struct fragment *fragment;
	struct property *new_prop;
	struct device_node *fragment_node;
	struct device_node *overlay_node;
	const char *path;
	const char *path_tail;
	const char *target_path;
	int k;
	int overlay_name_len;
	int path_len;
	int path_tail_len;
	int target_path_len;

	if (!prop->value)
		return NULL;
	if (strnlen(prop->value, prop->length) >= prop->length)
		return NULL;
	path = prop->value;
	path_len = strlen(path);

	if (path_len < 1)
		return NULL;
	fragment_node = __of_find_node_by_path(ovcs->overlay_tree, path + 1);
	overlay_node = __of_find_node_by_path(fragment_node, "__overlay__/");
	of_node_put(fragment_node);
	of_node_put(overlay_node);

	for (k = 0; k < ovcs->count; k++) {
		fragment = &ovcs->fragments[k];
		if (fragment->overlay == overlay_node)
			break;
	}
	if (k >= ovcs->count)
		return NULL;

	overlay_name_len = snprintf(NULL, 0, "%pOF", fragment->overlay);

	if (overlay_name_len > path_len)
		return NULL;
	path_tail = path + overlay_name_len;
	path_tail_len = strlen(path_tail);

	target_path = kasprintf(GFP_KERNEL, "%pOF", fragment->target);
	if (!target_path)
		return NULL;
	target_path_len = strlen(target_path);

	new_prop = kzalloc(sizeof(*new_prop), GFP_KERNEL);
	if (!new_prop)
		goto err_free_target_path;

	new_prop->name = kstrdup(prop->name, GFP_KERNEL);
	new_prop->length = target_path_len + path_tail_len + 1;
	new_prop->value = kzalloc(new_prop->length, GFP_KERNEL);
	if (!new_prop->name || !new_prop->value)
		goto err_free_new_prop;

	strcpy(new_prop->value, target_path);
	strcpy(new_prop->value + target_path_len, path_tail);

	of_property_set_flag(new_prop, OF_DYNAMIC);

	kfree(target_path);

	return new_prop;

err_free_new_prop:
	kfree(new_prop->name);
	kfree(new_prop->value);
	kfree(new_prop);
err_free_target_path:
	kfree(target_path);

	return NULL;
}

/**
 * add_changeset_property() - add @overlay_prop to overlay changeset
 * @ovcs:		overlay changeset
 * @target:		where @overlay_prop will be placed
 * @overlay_prop:	property to add or update, from overlay tree
 * @is_symbols_prop:	1 if @overlay_prop is from node "/__symbols__"
 *
 * If @overlay_prop does not already exist in live devicetree, add changeset
 * entry to add @overlay_prop in @target, else add changeset entry to update
 * value of @overlay_prop.
 *
 * @target may be either in the live devicetree or in a new subtree that
 * is contained in the changeset.
 *
 * Some special properties are not added or updated (no error returned):
 * "name", "phandle", "linux,phandle".
 *
 * Properties "#address-cells" and "#size-cells" are not updated if they
 * are already in the live tree, but if present in the live tree, the values
 * in the overlay must match the values in the live tree.
 *
 * Update of property in symbols node is not allowed.
 *
 * Returns 0 on success, -ENOMEM if memory allocation failure, or -EINVAL if
 * invalid @overlay.
 */
static int add_changeset_property(struct overlay_changeset *ovcs,
		struct target *target, struct property *overlay_prop,
		bool is_symbols_prop)
{
	struct property *new_prop = NULL, *prop;
	int ret = 0;

	if (target->in_livetree)
		if (!of_prop_cmp(overlay_prop->name, "name") ||
		    !of_prop_cmp(overlay_prop->name, "phandle") ||
		    !of_prop_cmp(overlay_prop->name, "linux,phandle"))
			return 0;

	if (target->in_livetree)
		prop = of_find_property(target->np, overlay_prop->name, NULL);
	else
		prop = NULL;

	if (prop) {
		if (!of_prop_cmp(prop->name, "#address-cells")) {
			if (!of_prop_val_eq(prop, overlay_prop)) {
				pr_err("ERROR: changing value of #address-cells is not allowed in %pOF\n",
				       target->np);
				ret = -EINVAL;
			}
			return ret;

		} else if (!of_prop_cmp(prop->name, "#size-cells")) {
			if (!of_prop_val_eq(prop, overlay_prop)) {
				pr_err("ERROR: changing value of #size-cells is not allowed in %pOF\n",
				       target->np);
				ret = -EINVAL;
			}
			return ret;
		}
	}

	if (is_symbols_prop) {
		if (prop)
			return -EINVAL;
		new_prop = dup_and_fixup_symbol_prop(ovcs, overlay_prop);
	} else {
		new_prop = __of_prop_dup(overlay_prop, GFP_KERNEL);
	}

	if (!new_prop)
		return -ENOMEM;

	if (!prop) {
		if (!target->in_livetree) {
			new_prop->next = target->np->deadprops;
			target->np->deadprops = new_prop;
		}
		ret = of_changeset_add_property(&ovcs->cset, target->np,
						new_prop);
	} else {
		ret = of_changeset_update_property(&ovcs->cset, target->np,
						   new_prop);
	}

	if (!of_node_check_flag(target->np, OF_OVERLAY))
		pr_err("WARNING: memory leak will occur if overlay removed, property: %pOF/%s\n",
		       target->np, new_prop->name);

	if (ret) {
		kfree(new_prop->name);
		kfree(new_prop->value);
		kfree(new_prop);
	}
	return ret;
}

/**
 * add_changeset_node() - add @node (and children) to overlay changeset
 * @ovcs:	overlay changeset
 * @target:	where @node will be placed in live tree or changeset
 * @node:	node from within overlay device tree fragment
 *
 * If @node does not already exist in @target, add changeset entry
 * to add @node in @target.
 *
 * If @node already exists in @target, and the existing node has
 * a phandle, the overlay node is not allowed to have a phandle.
 *
 * If @node has child nodes, add the children recursively via
 * build_changeset_next_level().
 *
 * NOTE_1: A live devicetree created from a flattened device tree (FDT) will
 *       not contain the full path in node->full_name.  Thus an overlay
 *       created from an FDT also will not contain the full path in
 *       node->full_name.  However, a live devicetree created from Open
 *       Firmware may have the full path in node->full_name.
 *
 *       add_changeset_node() follows the FDT convention and does not include
 *       the full path in node->full_name.  Even though it expects the overlay
 *       to not contain the full path, it uses kbasename() to remove the
 *       full path should it exist.  It also uses kbasename() in comparisons
 *       to nodes in the live devicetree so that it can apply an overlay to
 *       a live devicetree created from Open Firmware.
 *
 * NOTE_2: Multiple mods of created nodes not supported.
 *
 * Returns 0 on success, -ENOMEM if memory allocation failure, or -EINVAL if
 * invalid @overlay.
 */
static int add_changeset_node(struct overlay_changeset *ovcs,
		struct target *target, struct device_node *node)
{
	const char *node_kbasename;
	const __be32 *phandle;
	struct device_node *tchild;
	struct target target_child;
	int ret = 0, size;

	node_kbasename = kbasename(node->full_name);

	for_each_child_of_node(target->np, tchild)
		if (!of_node_cmp(node_kbasename, kbasename(tchild->full_name)))
			break;

	if (!tchild) {
		tchild = __of_node_dup(NULL, node_kbasename);
		if (!tchild)
			return -ENOMEM;

		tchild->parent = target->np;
		tchild->name = __of_get_property(node, "name", NULL);

		if (!tchild->name)
			tchild->name = "<NULL>";

		/* ignore obsolete "linux,phandle" */
		phandle = __of_get_property(node, "phandle", &size);
		if (phandle && (size == 4))
			tchild->phandle = be32_to_cpup(phandle);

		of_node_set_flag(tchild, OF_OVERLAY);

		ret = of_changeset_attach_node(&ovcs->cset, tchild);
		if (ret)
			return ret;

		target_child.np = tchild;
		target_child.in_livetree = false;

		ret = build_changeset_next_level(ovcs, &target_child, node);
		of_node_put(tchild);
		return ret;
	}

	if (node->phandle && tchild->phandle) {
		ret = -EINVAL;
	} else {
		target_child.np = tchild;
		target_child.in_livetree = target->in_livetree;
		ret = build_changeset_next_level(ovcs, &target_child, node);
	}
	of_node_put(tchild);

	return ret;
}

/**
 * build_changeset_next_level() - add level of overlay changeset
 * @ovcs:		overlay changeset
 * @target:		where to place @overlay_node in live tree
 * @overlay_node:	node from within an overlay device tree fragment
 *
 * Add the properties (if any) and nodes (if any) from @overlay_node to the
 * @ovcs->cset changeset.  If an added node has child nodes, they will
 * be added recursively.
 *
 * Do not allow symbols node to have any children.
 *
 * Returns 0 on success, -ENOMEM if memory allocation failure, or -EINVAL if
 * invalid @overlay_node.
 */
static int build_changeset_next_level(struct overlay_changeset *ovcs,
		struct target *target, const struct device_node *overlay_node)
{
	struct device_node *child;
	struct property *prop;
	int ret;

	for_each_property_of_node(overlay_node, prop) {
		ret = add_changeset_property(ovcs, target, prop, 0);
		if (ret) {
			pr_debug("Failed to apply prop @%pOF/%s, err=%d\n",
				 target->np, prop->name, ret);
			return ret;
		}
	}

	for_each_child_of_node(overlay_node, child) {
		ret = add_changeset_node(ovcs, target, child);
		if (ret) {
			pr_debug("Failed to apply node @%pOF/%pOFn, err=%d\n",
				 target->np, child, ret);
			of_node_put(child);
			return ret;
		}
	}

	return 0;
}

/*
 * Add the properties from __overlay__ node to the @ovcs->cset changeset.
 */
static int build_changeset_symbols_node(struct overlay_changeset *ovcs,
		struct target *target,
		const struct device_node *overlay_symbols_node)
{
	struct property *prop;
	int ret;

	for_each_property_of_node(overlay_symbols_node, prop) {
		ret = add_changeset_property(ovcs, target, prop, 1);
		if (ret) {
			pr_debug("Failed to apply symbols prop @%pOF/%s, err=%d\n",
				 target->np, prop->name, ret);
			return ret;
		}
	}

	return 0;
}

static int find_dup_cset_node_entry(struct overlay_changeset *ovcs,
		struct of_changeset_entry *ce_1)
{
	struct of_changeset_entry *ce_2;
	char *fn_1, *fn_2;
	int node_path_match;

	if (ce_1->action != OF_RECONFIG_ATTACH_NODE &&
	    ce_1->action != OF_RECONFIG_DETACH_NODE)
		return 0;

	ce_2 = ce_1;
	list_for_each_entry_continue(ce_2, &ovcs->cset.entries, node) {
		if ((ce_2->action != OF_RECONFIG_ATTACH_NODE &&
		     ce_2->action != OF_RECONFIG_DETACH_NODE) ||
		    of_node_cmp(ce_1->np->full_name, ce_2->np->full_name))
			continue;

		fn_1 = kasprintf(GFP_KERNEL, "%pOF", ce_1->np);
		fn_2 = kasprintf(GFP_KERNEL, "%pOF", ce_2->np);
		node_path_match = !strcmp(fn_1, fn_2);
		kfree(fn_1);
		kfree(fn_2);
		if (node_path_match) {
			pr_err("ERROR: multiple fragments add and/or delete node %pOF\n",
			       ce_1->np);
			return -EINVAL;
		}
	}

	return 0;
}

static int find_dup_cset_prop(struct overlay_changeset *ovcs,
		struct of_changeset_entry *ce_1)
{
	struct of_changeset_entry *ce_2;
	char *fn_1, *fn_2;
	int node_path_match;

	if (ce_1->action != OF_RECONFIG_ADD_PROPERTY &&
	    ce_1->action != OF_RECONFIG_REMOVE_PROPERTY &&
	    ce_1->action != OF_RECONFIG_UPDATE_PROPERTY)
		return 0;

	ce_2 = ce_1;
	list_for_each_entry_continue(ce_2, &ovcs->cset.entries, node) {
		if ((ce_2->action != OF_RECONFIG_ADD_PROPERTY &&
		     ce_2->action != OF_RECONFIG_REMOVE_PROPERTY &&
		     ce_2->action != OF_RECONFIG_UPDATE_PROPERTY) ||
		    of_node_cmp(ce_1->np->full_name, ce_2->np->full_name))
			continue;

		fn_1 = kasprintf(GFP_KERNEL, "%pOF", ce_1->np);
		fn_2 = kasprintf(GFP_KERNEL, "%pOF", ce_2->np);
		node_path_match = !strcmp(fn_1, fn_2);
		kfree(fn_1);
		kfree(fn_2);
		if (node_path_match &&
		    !of_prop_cmp(ce_1->prop->name, ce_2->prop->name)) {
			pr_err("ERROR: multiple fragments add, update, and/or delete property %pOF/%s\n",
			       ce_1->np, ce_1->prop->name);
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * changeset_dup_entry_check() - check for duplicate entries
 * @ovcs:	Overlay changeset
 *
 * Check changeset @ovcs->cset for multiple {add or delete} node entries for
 * the same node or duplicate {add, delete, or update} properties entries
 * for the same property.
 *
 * Returns 0 on success, or -EINVAL if duplicate changeset entry found.
 */
static int changeset_dup_entry_check(struct overlay_changeset *ovcs)
{
	struct of_changeset_entry *ce_1;
	int dup_entry = 0;

	list_for_each_entry(ce_1, &ovcs->cset.entries, node) {
		dup_entry |= find_dup_cset_node_entry(ovcs, ce_1);
		dup_entry |= find_dup_cset_prop(ovcs, ce_1);
	}

	return dup_entry ? -EINVAL : 0;
}

/**
 * build_changeset() - populate overlay changeset in @ovcs from @ovcs->fragments
 * @ovcs:	Overlay changeset
 *
 * Create changeset @ovcs->cset to contain the nodes and properties of the
 * overlay device tree fragments in @ovcs->fragments[].  If an error occurs,
 * any portions of the changeset that were successfully created will remain
 * in @ovcs->cset.
 *
 * Returns 0 on success, -ENOMEM if memory allocation failure, or -EINVAL if
 * invalid overlay in @ovcs->fragments[].
 */
static int build_changeset(struct overlay_changeset *ovcs)
{
	struct fragment *fragment;
	struct target target;
	int fragments_count, i, ret;

	/*
	 * if there is a symbols fragment in ovcs->fragments[i] it is
	 * the final element in the array
	 */
	if (ovcs->symbols_fragment)
		fragments_count = ovcs->count - 1;
	else
		fragments_count = ovcs->count;

	for (i = 0; i < fragments_count; i++) {
		fragment = &ovcs->fragments[i];

		target.np = fragment->target;
		target.in_livetree = true;
		ret = build_changeset_next_level(ovcs, &target,
						 fragment->overlay);
		if (ret) {
			pr_debug("fragment apply failed '%pOF'\n",
				 fragment->target);
			return ret;
		}
	}

	if (ovcs->symbols_fragment) {
		fragment = &ovcs->fragments[ovcs->count - 1];

		target.np = fragment->target;
		target.in_livetree = true;
		ret = build_changeset_symbols_node(ovcs, &target,
						   fragment->overlay);
		if (ret) {
			pr_debug("symbols fragment apply failed '%pOF'\n",
				 fragment->target);
			return ret;
		}
	}

	return changeset_dup_entry_check(ovcs);
}

/*
 * Find the target node using a number of different strategies
 * in order of preference:
 *
 * 1) "target" property containing the phandle of the target
 * 2) "target-path" property containing the path of the target
 */
static struct device_node *find_target(struct device_node *info_node)
{
	struct device_node *node;
	const char *path;
	u32 val;
	int ret;

	ret = of_property_read_u32(info_node, "target", &val);
	if (!ret) {
		node = of_find_node_by_phandle(val);
		if (!node)
			pr_err("find target, node: %pOF, phandle 0x%x not found\n",
			       info_node, val);
		return node;
	}

	ret = of_property_read_string(info_node, "target-path", &path);
	if (!ret) {
		node =  of_find_node_by_path(path);
		if (!node)
			pr_err("find target, node: %pOF, path '%s' not found\n",
			       info_node, path);
		return node;
	}

	pr_err("find target, node: %pOF, no target property\n", info_node);

	return NULL;
}

/**
 * init_overlay_changeset() - initialize overlay changeset from overlay tree
 * @ovcs:	Overlay changeset to build
 * @fdt:	base of memory allocated to hold aligned FDT that was unflattened to create @tree
 * @tree:	Contains the overlay fragments and overlay fixup nodes
 *
 * Initialize @ovcs.  Populate @ovcs->fragments with node information from
 * the top level of @tree.  The relevant top level nodes are the fragment
 * nodes and the __symbols__ node.  Any other top level node will be ignored.
 *
 * Returns 0 on success, -ENOMEM if memory allocation failure, -EINVAL if error
 * detected in @tree, or -ENOSPC if idr_alloc() error.
 */
static int init_overlay_changeset(struct overlay_changeset *ovcs,
		const void *fdt, struct device_node *tree)
{
	struct device_node *node, *overlay_node;
	struct fragment *fragment;
	struct fragment *fragments;
	int cnt, id, ret;

	/*
	 * Warn for some issues.  Can not return -EINVAL for these until
	 * of_unittest_apply_overlay() is fixed to pass these checks.
	 */
	if (!of_node_check_flag(tree, OF_DYNAMIC))
		pr_debug("%s() tree is not dynamic\n", __func__);

	if (!of_node_check_flag(tree, OF_DETACHED))
		pr_debug("%s() tree is not detached\n", __func__);

	if (!of_node_is_root(tree))
		pr_debug("%s() tree is not root\n", __func__);

	ovcs->overlay_tree = tree;
	ovcs->fdt = fdt;

	INIT_LIST_HEAD(&ovcs->ovcs_list);

	of_changeset_init(&ovcs->cset);

	id = idr_alloc(&ovcs_idr, ovcs, 1, 0, GFP_KERNEL);
	if (id <= 0)
		return id;

	cnt = 0;

	/* fragment nodes */
	for_each_child_of_node(tree, node) {
		overlay_node = of_get_child_by_name(node, "__overlay__");
		if (overlay_node) {
			cnt++;
			of_node_put(overlay_node);
		}
	}

	node = of_get_child_by_name(tree, "__symbols__");
	if (node) {
		cnt++;
		of_node_put(node);
	}

	fragments = kcalloc(cnt, sizeof(*fragments), GFP_KERNEL);
	if (!fragments) {
		ret = -ENOMEM;
		goto err_free_idr;
	}

	cnt = 0;
	for_each_child_of_node(tree, node) {
		overlay_node = of_get_child_by_name(node, "__overlay__");
		if (!overlay_node)
			continue;

		fragment = &fragments[cnt];
		fragment->overlay = overlay_node;
		fragment->target = find_target(node);
		if (!fragment->target) {
			of_node_put(fragment->overlay);
			ret = -EINVAL;
			goto err_free_fragments;
		}

		cnt++;
	}

	/*
	 * if there is a symbols fragment in ovcs->fragments[i] it is
	 * the final element in the array
	 */
	node = of_get_child_by_name(tree, "__symbols__");
	if (node) {
		ovcs->symbols_fragment = 1;
		fragment = &fragments[cnt];
		fragment->overlay = node;
		fragment->target = of_find_node_by_path("/__symbols__");

		if (!fragment->target) {
			pr_err("symbols in overlay, but not in live tree\n");
			ret = -EINVAL;
			goto err_free_fragments;
		}

		cnt++;
	}

	if (!cnt) {
		pr_err("no fragments or symbols in overlay\n");
		ret = -EINVAL;
		goto err_free_fragments;
	}

	ovcs->id = id;
	ovcs->count = cnt;
	ovcs->fragments = fragments;

	return 0;

err_free_fragments:
	kfree(fragments);
err_free_idr:
	idr_remove(&ovcs_idr, id);

	pr_err("%s() failed, ret = %d\n", __func__, ret);

	return ret;
}

static void free_overlay_changeset(struct overlay_changeset *ovcs)
{
	int i;

	if (ovcs->cset.entries.next)
		of_changeset_destroy(&ovcs->cset);

	if (ovcs->id)
		idr_remove(&ovcs_idr, ovcs->id);

	for (i = 0; i < ovcs->count; i++) {
		of_node_put(ovcs->fragments[i].target);
		of_node_put(ovcs->fragments[i].overlay);
	}
	kfree(ovcs->fragments);
	/*
	 * There should be no live pointers into ovcs->overlay_tree and
	 * ovcs->fdt due to the policy that overlay notifiers are not allowed
	 * to retain pointers into the overlay devicetree.
	 */
	kfree(ovcs->overlay_tree);
	kfree(ovcs->fdt);
	kfree(ovcs);
}

/*
 * internal documentation
 *
 * of_overlay_apply() - Create and apply an overlay changeset
 * @fdt:	base of memory allocated to hold the aligned FDT
 * @tree:	Expanded overlay device tree
 * @ovcs_id:	Pointer to overlay changeset id
 *
 * Creates and applies an overlay changeset.
 *
 * If an error occurs in a pre-apply notifier, then no changes are made
 * to the device tree.
 *

 * A non-zero return value will not have created the changeset if error is from:
 *   - parameter checks
 *   - building the changeset
 *   - overlay changeset pre-apply notifier
 *
 * If an error is returned by an overlay changeset pre-apply notifier
 * then no further overlay changeset pre-apply notifier will be called.
 *
 * A non-zero return value will have created the changeset if error is from:
 *   - overlay changeset entry notifier
 *   - overlay changeset post-apply notifier
 *
 * If an error is returned by an overlay changeset post-apply notifier
 * then no further overlay changeset post-apply notifier will be called.
 *
 * If more than one notifier returns an error, then the last notifier
 * error to occur is returned.
 *
 * If an error occurred while applying the overlay changeset, then an
 * attempt is made to revert any changes that were made to the
 * device tree.  If there were any errors during the revert attempt
 * then the state of the device tree can not be determined, and any
 * following attempt to apply or remove an overlay changeset will be
 * refused.
 *
 * Returns 0 on success, or a negative error number.  Overlay changeset
 * id is returned to *ovcs_id.
 */

static int of_overlay_apply(const void *fdt, struct device_node *tree,
		int *ovcs_id)
{
	struct overlay_changeset *ovcs;
	int ret = 0, ret_revert, ret_tmp;

	/*
	 * As of this point, fdt and tree belong to the overlay changeset.
	 * overlay changeset code is responsible for freeing them.
	 */

	if (devicetree_corrupt()) {
		pr_err("devicetree state suspect, refuse to apply overlay\n");
		kfree(fdt);
		kfree(tree);
		ret = -EBUSY;
		goto out;
	}

	ovcs = kzalloc(sizeof(*ovcs), GFP_KERNEL);
	if (!ovcs) {
		kfree(fdt);
		kfree(tree);
		ret = -ENOMEM;
		goto out;
	}

	of_overlay_mutex_lock();
	mutex_lock(&of_mutex);

	ret = of_resolve_phandles(tree);
	if (ret)
		goto err_free_tree;

	ret = init_overlay_changeset(ovcs, fdt, tree);
	if (ret)
		goto err_free_tree;

	/*
	 * after overlay_notify(), ovcs->overlay_tree related pointers may have
	 * leaked to drivers, so can not kfree() tree, aka ovcs->overlay_tree;
	 * and can not free memory containing aligned fdt.  The aligned fdt
	 * is contained within the memory at ovcs->fdt, possibly at an offset
	 * from ovcs->fdt.
	 */
	ret = overlay_notify(ovcs, OF_OVERLAY_PRE_APPLY);
	if (ret) {
		pr_err("overlay changeset pre-apply notify error %d\n", ret);
		goto err_free_overlay_changeset;
	}

	ret = build_changeset(ovcs);
	if (ret)
		goto err_free_overlay_changeset;

	ret_revert = 0;
	ret = __of_changeset_apply_entries(&ovcs->cset, &ret_revert);
	if (ret) {
		if (ret_revert) {
			pr_debug("overlay changeset revert error %d\n",
				 ret_revert);
			devicetree_state_flags |= DTSF_APPLY_FAIL;
		}
		goto err_free_overlay_changeset;
	}

	ret = __of_changeset_apply_notify(&ovcs->cset);
	if (ret)
		pr_err("overlay apply changeset entry notify error %d\n", ret);
	/* notify failure is not fatal, continue */

	list_add_tail(&ovcs->ovcs_list, &ovcs_list);
	*ovcs_id = ovcs->id;

	ret_tmp = overlay_notify(ovcs, OF_OVERLAY_POST_APPLY);
	if (ret_tmp) {
		pr_err("overlay changeset post-apply notify error %d\n",
		       ret_tmp);
		if (!ret)
			ret = ret_tmp;
	}

	goto out_unlock;

err_free_tree:
	kfree(fdt);
	kfree(tree);

err_free_overlay_changeset:
	free_overlay_changeset(ovcs);

out_unlock:
	mutex_unlock(&of_mutex);
	of_overlay_mutex_unlock();

out:
	pr_debug("%s() err=%d\n", __func__, ret);

	return ret;
}

int of_overlay_fdt_apply(const void *overlay_fdt, u32 overlay_fdt_size,
			 int *ovcs_id)
{
	void *new_fdt;
	void *new_fdt_align;
	int ret;
	u32 size;
	struct device_node *overlay_root = NULL;

	*ovcs_id = 0;
	ret = 0;

	if (overlay_fdt_size < sizeof(struct fdt_header) ||
	    fdt_check_header(overlay_fdt)) {
		pr_err("Invalid overlay_fdt header\n");
		return -EINVAL;
	}

	size = fdt_totalsize(overlay_fdt);
	if (overlay_fdt_size < size)
		return -EINVAL;

	/*
	 * Must create permanent copy of FDT because of_fdt_unflatten_tree()
	 * will create pointers to the passed in FDT in the unflattened tree.
	 */
	new_fdt = kmalloc(size + FDT_ALIGN_SIZE, GFP_KERNEL);
	if (!new_fdt)
		return -ENOMEM;

	new_fdt_align = PTR_ALIGN(new_fdt, FDT_ALIGN_SIZE);
	memcpy(new_fdt_align, overlay_fdt, size);

	of_fdt_unflatten_tree(new_fdt_align, NULL, &overlay_root);
	if (!overlay_root) {
		pr_err("unable to unflatten overlay_fdt\n");
		ret = -EINVAL;
		goto out_free_new_fdt;
	}

	ret = of_overlay_apply(new_fdt, overlay_root, ovcs_id);
	if (ret < 0) {
		/*
		 * new_fdt and overlay_root now belong to the overlay
		 * changeset.
		 * overlay changeset code is responsible for freeing them.
		 */
		goto out;
	}

	return 0;


out_free_new_fdt:
	kfree(new_fdt);

out:
	return ret;
}
EXPORT_SYMBOL_GPL(of_overlay_fdt_apply);

/*
 * Find @np in @tree.
 *
 * Returns 1 if @np is @tree or is contained in @tree, else 0
 */
static int find_node(struct device_node *tree, struct device_node *np)
{
	struct device_node *child;

	if (tree == np)
		return 1;

	for_each_child_of_node(tree, child) {
		if (find_node(child, np)) {
			of_node_put(child);
			return 1;
		}
	}

	return 0;
}

/*
 * Is @remove_ce_node a child of, a parent of, or the same as any
 * node in an overlay changeset more topmost than @remove_ovcs?
 *
 * Returns 1 if found, else 0
 */
static int node_overlaps_later_cs(struct overlay_changeset *remove_ovcs,
		struct device_node *remove_ce_node)
{
	struct overlay_changeset *ovcs;
	struct of_changeset_entry *ce;

	list_for_each_entry_reverse(ovcs, &ovcs_list, ovcs_list) {
		if (ovcs == remove_ovcs)
			break;

		list_for_each_entry(ce, &ovcs->cset.entries, node) {
			if (find_node(ce->np, remove_ce_node)) {
				pr_err("%s: #%d overlaps with #%d @%pOF\n",
					__func__, remove_ovcs->id, ovcs->id,
					remove_ce_node);
				return 1;
			}
			if (find_node(remove_ce_node, ce->np)) {
				pr_err("%s: #%d overlaps with #%d @%pOF\n",
					__func__, remove_ovcs->id, ovcs->id,
					remove_ce_node);
				return 1;
			}
		}
	}

	return 0;
}

/*
 * We can safely remove the overlay only if it's the top-most one.
 * Newly applied overlays are inserted at the tail of the overlay list,
 * so a top most overlay is the one that is closest to the tail.
 *
 * The topmost check is done by exploiting this property. For each
 * affected device node in the log list we check if this overlay is
 * the one closest to the tail. If another overlay has affected this
 * device node and is closest to the tail, then removal is not permited.
 */
static int overlay_removal_is_ok(struct overlay_changeset *remove_ovcs)
{
	struct of_changeset_entry *remove_ce;

	list_for_each_entry(remove_ce, &remove_ovcs->cset.entries, node) {
		if (node_overlaps_later_cs(remove_ovcs, remove_ce->np)) {
			pr_err("overlay #%d is not topmost\n", remove_ovcs->id);
			return 0;
		}
	}

	return 1;
}

/**
 * of_overlay_remove() - Revert and free an overlay changeset
 * @ovcs_id:	Pointer to overlay changeset id
 *
 * Removes an overlay if it is permissible.  @ovcs_id was previously returned
 * by of_overlay_fdt_apply().
 *
 * If an error occurred while attempting to revert the overlay changeset,
 * then an attempt is made to re-apply any changeset entry that was
 * reverted.  If an error occurs on re-apply then the state of the device
 * tree can not be determined, and any following attempt to apply or remove
 * an overlay changeset will be refused.
 *
 * A non-zero return value will not revert the changeset if error is from:
 *   - parameter checks
 *   - overlay changeset pre-remove notifier
 *   - overlay changeset entry revert
 *
 * If an error is returned by an overlay changeset pre-remove notifier
 * then no further overlay changeset pre-remove notifier will be called.
 *
 * If more than one notifier returns an error, then the last notifier
 * error to occur is returned.
 *
 * A non-zero return value will revert the changeset if error is from:
 *   - overlay changeset entry notifier
 *   - overlay changeset post-remove notifier
 *
 * If an error is returned by an overlay changeset post-remove notifier
 * then no further overlay changeset post-remove notifier will be called.
 *
 * Returns 0 on success, or a negative error number.  *ovcs_id is set to
 * zero after reverting the changeset, even if a subsequent error occurs.
 */
int of_overlay_remove(int *ovcs_id)
{
	struct overlay_changeset *ovcs;
	int ret, ret_apply, ret_tmp;

	ret = 0;

	if (devicetree_corrupt()) {
		pr_err("suspect devicetree state, refuse to remove overlay\n");
		ret = -EBUSY;
		goto out;
	}

	mutex_lock(&of_mutex);

	ovcs = idr_find(&ovcs_idr, *ovcs_id);
	if (!ovcs) {
		ret = -ENODEV;
		pr_err("remove: Could not find overlay #%d\n", *ovcs_id);
		goto out_unlock;
	}

	if (!overlay_removal_is_ok(ovcs)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = overlay_notify(ovcs, OF_OVERLAY_PRE_REMOVE);
	if (ret) {
		pr_err("overlay changeset pre-remove notify error %d\n", ret);
		goto out_unlock;
	}

	list_del(&ovcs->ovcs_list);

	ret_apply = 0;
	ret = __of_changeset_revert_entries(&ovcs->cset, &ret_apply);
	if (ret) {
		if (ret_apply)
			devicetree_state_flags |= DTSF_REVERT_FAIL;
		goto out_unlock;
	}

	ret = __of_changeset_revert_notify(&ovcs->cset);
	if (ret)
		pr_err("overlay remove changeset entry notify error %d\n", ret);
	/* notify failure is not fatal, continue */

	*ovcs_id = 0;

	ret_tmp = overlay_notify(ovcs, OF_OVERLAY_POST_REMOVE);
	if (ret_tmp) {
		pr_err("overlay changeset post-remove notify error %d\n",
		       ret_tmp);
		if (!ret)
			ret = ret_tmp;
	}

	free_overlay_changeset(ovcs);

out_unlock:
	mutex_unlock(&of_mutex);

out:
	pr_debug("%s() err=%d\n", __func__, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(of_overlay_remove);

/**
 * of_overlay_remove_all() - Reverts and frees all overlay changesets
 *
 * Removes all overlays from the system in the correct order.
 *
 * Returns 0 on success, or a negative error number
 */
int of_overlay_remove_all(void)
{
	struct overlay_changeset *ovcs, *ovcs_n;
	int ret;

	/* the tail of list is guaranteed to be safe to remove */
	list_for_each_entry_safe_reverse(ovcs, ovcs_n, &ovcs_list, ovcs_list) {
		ret = of_overlay_remove(&ovcs->id);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(of_overlay_remove_all);
