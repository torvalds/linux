/*
 * Functions for working with device tree overlays
 *
 * Copyright (C) 2012 Pantelis Antoniou <panto@antoniou-consulting.com>
 * Copyright (C) 2012 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#define pr_fmt(fmt)	"OF: overlay: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/idr.h>

#include "of_private.h"

/**
 * struct fragment - info about fragment nodes in overlay expanded device tree
 * @target:	target of the overlay operation
 * @overlay:	pointer to the __overlay__ node
 */
struct fragment {
	struct device_node *target;
	struct device_node *overlay;
	bool is_symbols_node;
};

/**
 * struct overlay_changeset
 * @ovcs_list:	list on which we are located
 * @count:	count of @fragments structures
 * @fragments:	info about fragment nodes in overlay expanded device tree
 * @cset:	changeset to apply fragments to live device tree
 */
struct overlay_changeset {
	int id;
	struct list_head ovcs_list;
	int count;
	struct fragment *fragments;
	struct of_changeset cset;
};

static int build_changeset_next_level(struct overlay_changeset *ovcs,
		struct device_node *target_node,
		const struct device_node *overlay_node,
		bool is_symbols_node);

static BLOCKING_NOTIFIER_HEAD(overlay_notify_chain);

int of_overlay_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&overlay_notify_chain, nb);
}
EXPORT_SYMBOL_GPL(of_overlay_notifier_register);

int of_overlay_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&overlay_notify_chain, nb);
}
EXPORT_SYMBOL_GPL(of_overlay_notifier_unregister);

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
		if (ret)
			return notifier_to_errno(ret);
	}

	return 0;
}

/*
 * The properties in the "/__symbols__" node are "symbols".
 *
 * The value of properties in the "/__symbols__" node is the path of a
 * node in the subtree of a fragment node's "__overlay__" node, for
 * example "/fragment@0/__overlay__/symbol_path_tail".  Symbol_path_tail
 * can be a single node or it may be a multi-node path.
 *
 * The duplicated property value will be modified by replacing the
 * "/fragment_name/__overlay/" portion of the value  with the target
 * path from the fragment node.
 */
static struct property *dup_and_fixup_symbol_prop(
		struct overlay_changeset *ovcs, const struct property *prop)
{
	struct fragment *fragment;
	struct property *new;
	const char *overlay_name;
	char *symbol_path_tail;
	char *symbol_path;
	const char *target_path;
	int k;
	int symbol_path_tail_len;
	int overlay_name_len;
	int target_path_len;

	if (!prop->value)
		return NULL;
	symbol_path = prop->value;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	for (k = 0; k < ovcs->count; k++) {
		fragment = &ovcs->fragments[k];
		overlay_name = fragment->overlay->full_name;
		overlay_name_len = strlen(overlay_name);
		if (!strncasecmp(symbol_path, overlay_name, overlay_name_len))
			break;
	}

	if (k >= ovcs->count)
		goto err_free;

	target_path = fragment->target->full_name;
	target_path_len = strlen(target_path);

	symbol_path_tail = symbol_path + overlay_name_len;
	symbol_path_tail_len = strlen(symbol_path_tail);

	new->name = kstrdup(prop->name, GFP_KERNEL);
	new->length = target_path_len + symbol_path_tail_len + 1;
	new->value = kzalloc(new->length, GFP_KERNEL);

	if (!new->name || !new->value)
		goto err_free;

	strcpy(new->value, target_path);
	strcpy(new->value + target_path_len, symbol_path_tail);

	of_property_set_flag(new, OF_DYNAMIC);

	return new;

 err_free:
	kfree(new->name);
	kfree(new->value);
	kfree(new);
	return NULL;


}

/**
 * add_changeset_property() - add @overlay_prop to overlay changeset
 * @ovcs:		overlay changeset
 * @target_node:	where to place @overlay_prop in live tree
 * @overlay_prop:	property to add or update, from overlay tree
 * is_symbols_node:	1 if @target_node is "/__symbols__"
 *
 * If @overlay_prop does not already exist in @target_node, add changeset entry
 * to add @overlay_prop in @target_node, else add changeset entry to update
 * value of @overlay_prop.
 *
 * Some special properties are not updated (no error returned).
 *
 * Update of property in symbols node is not allowed.
 *
 * Returns 0 on success, -ENOMEM if memory allocation failure, or -EINVAL if
 * invalid @overlay.
 */
static int add_changeset_property(struct overlay_changeset *ovcs,
		struct device_node *target_node,
		struct property *overlay_prop,
		bool is_symbols_node)
{
	struct property *new_prop = NULL, *prop;
	int ret = 0;

	prop = of_find_property(target_node, overlay_prop->name, NULL);

	if (!of_prop_cmp(overlay_prop->name, "name") ||
	    !of_prop_cmp(overlay_prop->name, "phandle") ||
	    !of_prop_cmp(overlay_prop->name, "linux,phandle"))
		return 0;

	if (is_symbols_node) {
		if (prop)
			return -EINVAL;
		new_prop = dup_and_fixup_symbol_prop(ovcs, overlay_prop);
	} else {
		new_prop = __of_prop_dup(overlay_prop, GFP_KERNEL);
	}

	if (!new_prop)
		return -ENOMEM;

	if (!prop)
		ret = of_changeset_add_property(&ovcs->cset, target_node,
						new_prop);
	else
		ret = of_changeset_update_property(&ovcs->cset, target_node,
						   new_prop);

	if (ret) {
		kfree(new_prop->name);
		kfree(new_prop->value);
		kfree(new_prop);
	}
	return ret;
}

/**
 * add_changeset_node() - add @node (and children) to overlay changeset
 * @ovcs:		overlay changeset
 * @target_node:	where to place @node in live tree
 * @node:		node from within overlay device tree fragment
 *
 * If @node does not already exist in @target_node, add changeset entry
 * to add @node in @target_node.
 *
 * If @node already exists in @target_node, and the existing node has
 * a phandle, the overlay node is not allowed to have a phandle.
 *
 * If @node has child nodes, add the children recursively via
 * build_changeset_next_level().
 *
 * NOTE: Multiple mods of created nodes not supported.
 *
 * Returns 0 on success, -ENOMEM if memory allocation failure, or -EINVAL if
 * invalid @overlay.
 */
static int add_changeset_node(struct overlay_changeset *ovcs,
		struct device_node *target_node, struct device_node *node)
{
	const char *node_kbasename;
	struct device_node *tchild;
	int ret = 0;

	node_kbasename = kbasename(node->full_name);
	if (!node_kbasename)
		return -ENOMEM;

	for_each_child_of_node(target_node, tchild)
		if (!of_node_cmp(node_kbasename, kbasename(tchild->full_name)))
			break;

	if (tchild) {
		if (node->phandle)
			return -EINVAL;

		ret = build_changeset_next_level(ovcs, tchild, node, 0);
		of_node_put(tchild);
	} else {
		tchild = __of_node_dup(node, "%pOF/%s",
				       target_node, node_kbasename);
		if (!tchild)
			return -ENOMEM;

		tchild->parent = target_node;

		ret = of_changeset_attach_node(&ovcs->cset, tchild);
		if (ret)
			return ret;

		ret = build_changeset_next_level(ovcs, tchild, node, 0);
		if (ret)
			return ret;
	}

	return ret;
}

/**
 * build_changeset_next_level() - add level of overlay changeset
 * @ovcs:		overlay changeset
 * @target_node:	where to place @overlay_node in live tree
 * @overlay_node:	node from within an overlay device tree fragment
 * @is_symbols_node:	@overlay_node is node "/__symbols__"
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
		struct device_node *target_node,
		const struct device_node *overlay_node,
		bool is_symbols_node)
{
	struct device_node *child;
	struct property *prop;
	int ret;

	for_each_property_of_node(overlay_node, prop) {
		ret = add_changeset_property(ovcs, target_node, prop,
					     is_symbols_node);
		if (ret) {
			pr_err("Failed to apply prop @%pOF/%s\n",
			       target_node, prop->name);
			return ret;
		}
	}

	if (is_symbols_node)
		return 0;

	for_each_child_of_node(overlay_node, child) {
		ret = add_changeset_node(ovcs, target_node, child);
		if (ret) {
			pr_err("Failed to apply node @%pOF/%s\n",
			       target_node, child->name);
			of_node_put(child);
			return ret;
		}
	}

	return 0;
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
	int i, ret;

	for (i = 0; i < ovcs->count; i++) {
		struct fragment *fragment = &ovcs->fragments[i];

		ret = build_changeset_next_level(ovcs, fragment->target,
					       fragment->overlay,
					       fragment->is_symbols_node);
		if (ret) {
			pr_err("apply failed '%pOF'\n", fragment->target);
			return ret;
		}
	}

	return 0;
}

/*
 * Find the target node using a number of different strategies
 * in order of preference:
 *
 * 1) "target" property containing the phandle of the target
 * 2) "target-path" property containing the path of the target
 */
static struct device_node *find_target_node(struct device_node *info_node)
{
	const char *path;
	u32 val;
	int ret;

	ret = of_property_read_u32(info_node, "target", &val);
	if (!ret)
		return of_find_node_by_phandle(val);

	ret = of_property_read_string(info_node, "target-path", &path);
	if (!ret)
		return of_find_node_by_path(path);

	pr_err("Failed to find target for node %p (%s)\n",
		info_node, info_node->name);

	return NULL;
}

/**
 * of_fill_overlay_info() - Fill an overlay info structure
 * @ov		Overlay to fill
 * @info_node:	Device node containing the overlay
 * @ovinfo:	Pointer to the overlay info structure to fill
 *
 * Fills an overlay info structure with the overlay information
 * from a device node. This device node must have a target property
 * which contains a phandle of the overlay target node, and an
 * __overlay__ child node which has the overlay contents.
 * Both ovinfo->target & ovinfo->overlay have their references taken.
 *
 * Returns 0 on success, or a negative error value.
 */
static int of_fill_overlay_info(struct overlay_changeset *ovcset,
		struct device_node *info_node, struct fragment *fragment)
{
	fragment->overlay = of_get_child_by_name(info_node, "__overlay__");
	if (!fragment->overlay)
		goto err_fail;

	fragment->target = find_target_node(info_node);
	if (!fragment->target)
		goto err_fail;

	return 0;

err_fail:
	of_node_put(fragment->target);
	of_node_put(fragment->overlay);

	memset(fragment, 0, sizeof(*fragment));
	return -EINVAL;
}

/**
 * init_overlay_changeset() - initialize overlay changeset from overlay tree
 * @ovcs	Overlay changeset to build
 * @tree:	Contains all the overlay fragments and overlay fixup nodes
 *
 * Initialize @ovcs.  Populate @ovcs->fragments with node information from
 * the top level of @tree.  The relevant top level nodes are the fragment
 * nodes and the __symbols__ node.  Any other top level node will be ignored.
 *
 * Returns 0 on success, -ENOMEM if memory allocation failure, -EINVAL if error
 * detected in @tree, or -ENODEV if no valid nodes found.
 */
static int init_overlay_changeset(struct overlay_changeset *ovcs,
		struct device_node *tree)
{
	struct device_node *node;
	struct fragment *fragment;
	struct fragment *fragments;
	int cnt, ret;

	cnt = 0;
	for_each_child_of_node(tree, node)
		cnt++;

	if (of_get_child_by_name(tree, "__symbols__"))
		cnt++;

	fragments = kcalloc(cnt, sizeof(*fragments), GFP_KERNEL);
	if (!fragments)
		return -ENOMEM;

	cnt = 0;
	for_each_child_of_node(tree, node) {
		ret = of_fill_overlay_info(ovcs, node, &fragments[cnt]);
		if (!ret)
			cnt++;
	}

	node = of_get_child_by_name(tree, "__symbols__");
	if (node) {
		fragment = &fragments[cnt];
		fragment->overlay = node;
		fragment->target = of_find_node_by_path("/__symbols__");
		fragment->is_symbols_node = 1;

		if (!fragment->target) {
			pr_err("no symbols in root of device tree.\n");
			return -EINVAL;
		}

		cnt++;
	}

	if (!cnt) {
		kfree(fragments);
		return -ENODEV;
	}

	ovcs->count = cnt;
	ovcs->fragments = fragments;

	return 0;
}

/**
 * free_overlay_fragments() - Free a fragments array
 * @ovcs	Overlay to free the overlay info from
 *
 * Frees the memory of an ovcs->fragments[] array.
 */
static void free_overlay_fragments(struct overlay_changeset *ovcs)
{
	int i;

	/* do it in reverse */
	for (i = ovcs->count - 1; i >= 0; i--) {
		of_node_put(ovcs->fragments[i].target);
		of_node_put(ovcs->fragments[i].overlay);
	}

	kfree(ovcs->fragments);
}

static LIST_HEAD(ovcs_list);
static DEFINE_IDR(ovcs_idr);

/**
 * of_overlay_apply() - Create and apply an overlay changeset
 * @tree:	Expanded overlay device tree
 *
 * Creates and applies an overlay changeset.  If successful, the overlay
 * changeset is added to the overlay changeset list.
 *
 * Returns the id of the created overlay changeset, or a negative error number
 */
int of_overlay_apply(struct device_node *tree)
{
	struct overlay_changeset *ovcs;
	int id, ret;

	ovcs = kzalloc(sizeof(*ovcs), GFP_KERNEL);
	if (!ovcs)
		return -ENOMEM;
	ovcs->id = -1;

	INIT_LIST_HEAD(&ovcs->ovcs_list);

	of_changeset_init(&ovcs->cset);

	mutex_lock(&of_mutex);

	id = idr_alloc(&ovcs_idr, ovcs, 0, 0, GFP_KERNEL);
	if (id < 0) {
		ret = id;
		goto err_destroy_trans;
	}
	ovcs->id = id;

	ret = init_overlay_changeset(ovcs, tree);
	if (ret) {
		pr_err("init_overlay_changeset() failed for tree@%pOF\n",
		       tree);
		goto err_free_idr;
	}

	ret = overlay_notify(ovcs, OF_OVERLAY_PRE_APPLY);
	if (ret < 0) {
		pr_err("%s: Pre-apply notifier failed (ret=%d)\n",
		       __func__, ret);
		goto err_free_overlay_fragments;
	}

	ret = build_changeset(ovcs);
	if (ret)
		goto err_free_overlay_fragments;

	ret = __of_changeset_apply(&ovcs->cset);
	if (ret)
		goto err_free_overlay_fragments;

	list_add_tail(&ovcs->ovcs_list, &ovcs_list);

	overlay_notify(ovcs, OF_OVERLAY_POST_APPLY);

	mutex_unlock(&of_mutex);

	return id;

err_free_overlay_fragments:
	free_overlay_fragments(ovcs);
err_free_idr:
	idr_remove(&ovcs_idr, ovcs->id);
err_destroy_trans:
	of_changeset_destroy(&ovcs->cset);
	kfree(ovcs);
	mutex_unlock(&of_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(of_overlay_apply);

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
 * Is @remove_ce_np a child of or the same as any
 * node in an overlay changeset more topmost than @remove_ovcs?
 *
 * Returns 1 if found, else 0
 */
static int node_in_later_cs(struct overlay_changeset *remove_ovcs,
		struct device_node *remove_ce_np)
{
	struct overlay_changeset *ovcs;
	struct of_changeset_entry *ce;

	list_for_each_entry_reverse(ovcs, &ovcs_list, ovcs_list) {
		if (ovcs == remove_ovcs)
			break;

		list_for_each_entry(ce, &ovcs->cset.entries, node) {
			if (find_node(ce->np, remove_ce_np)) {
				pr_err("%s: #%d clashes #%d @%pOF\n",
					__func__, remove_ovcs->id, ovcs->id,
					remove_ce_np);
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
		if (node_in_later_cs(remove_ovcs, remove_ce->np)) {
			pr_err("overlay #%d is not topmost\n", remove_ovcs->id);
			return 0;
		}
	}

	return 1;
}

/**
 * of_overlay_remove() - Revert and free an overlay changeset
 * @ovcs_id:	Overlay changeset id number
 *
 * Removes an overlay if it is permissible.  ovcs_id was previously returned
 * by of_overlay_apply().
 *
 * Returns 0 on success, or a negative error number
 */
int of_overlay_remove(int ovcs_id)
{
	struct overlay_changeset *ovcs;
	int ret = 0;

	mutex_lock(&of_mutex);

	ovcs = idr_find(&ovcs_idr, ovcs_id);
	if (!ovcs) {
		ret = -ENODEV;
		pr_err("remove: Could not find overlay #%d\n", ovcs_id);
		goto out;
	}

	if (!overlay_removal_is_ok(ovcs)) {
		ret = -EBUSY;
		goto out;
	}

	overlay_notify(ovcs, OF_OVERLAY_PRE_REMOVE);
	list_del(&ovcs->ovcs_list);
	__of_changeset_revert(&ovcs->cset);
	overlay_notify(ovcs, OF_OVERLAY_POST_REMOVE);
	free_overlay_fragments(ovcs);
	idr_remove(&ovcs_idr, ovcs_id);
	of_changeset_destroy(&ovcs->cset);
	kfree(ovcs);

out:
	mutex_unlock(&of_mutex);

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

	mutex_lock(&of_mutex);

	/* the tail of list is guaranteed to be safe to remove */
	list_for_each_entry_safe_reverse(ovcs, ovcs_n, &ovcs_list, ovcs_list) {
		list_del(&ovcs->ovcs_list);
		__of_changeset_revert(&ovcs->cset);
		free_overlay_fragments(ovcs);
		idr_remove(&ovcs_idr, ovcs->id);
		kfree(ovcs);
	}

	mutex_unlock(&of_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(of_overlay_remove_all);
