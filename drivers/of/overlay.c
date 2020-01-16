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
#include <linux/erryes.h>
#include <linux/slab.h>
#include <linux/libfdt.h>
#include <linux/err.h>
#include <linux/idr.h>

#include "of_private.h"

/**
 * struct target - info about current target yesde as recursing through overlay
 * @np:			yesde where current level of overlay will be applied
 * @in_livetree:	@np is a yesde in the live devicetree
 *
 * Used in the algorithm to create the portion of a changeset that describes
 * an overlay fragment, which is a devicetree subtree.  Initially @np is a yesde
 * in the live devicetree where the overlay subtree is targeted to be grafted
 * into.  When recursing to the next level of the overlay subtree, the target
 * also recurses to the next level of the live devicetree, as long as overlay
 * subtree yesde also exists in the live devicetree.  When a yesde in the overlay
 * subtree does yest exist at the same level in the live devicetree, target->np
 * points to a newly allocated yesde, and all subsequent targets in the subtree
 * will be newly allocated yesdes.
 */
struct target {
	struct device_yesde *np;
	bool in_livetree;
};

/**
 * struct fragment - info about fragment yesdes in overlay expanded device tree
 * @target:	target of the overlay operation
 * @overlay:	pointer to the __overlay__ yesde
 */
struct fragment {
	struct device_yesde *overlay;
	struct device_yesde *target;
};

/**
 * struct overlay_changeset
 * @id:			changeset identifier
 * @ovcs_list:		list on which we are located
 * @fdt:		FDT that was unflattened to create @overlay_tree
 * @overlay_tree:	expanded device tree that contains the fragment yesdes
 * @count:		count of fragment structures
 * @fragments:		fragment yesdes in the overlay expanded device tree
 * @symbols_fragment:	last element of @fragments[] is the  __symbols__ yesde
 * @cset:		changeset to apply fragments to live device tree
 */
struct overlay_changeset {
	int id;
	struct list_head ovcs_list;
	const void *fdt;
	struct device_yesde *overlay_tree;
	int count;
	struct fragment *fragments;
	bool symbols_fragment;
	struct of_changeset cset;
};

/* flags are sticky - once set, do yest reset */
static int devicetree_state_flags;
#define DTSF_APPLY_FAIL		0x01
#define DTSF_REVERT_FAIL	0x02

/*
 * If a changeset apply or revert encounters an error, an attempt will
 * be made to undo partial changes, but may fail.  If the undo fails
 * we do yest kyesw the state of the devicetree.
 */
static int devicetree_corrupt(void)
{
	return devicetree_state_flags &
		(DTSF_APPLY_FAIL | DTSF_REVERT_FAIL);
}

static int build_changeset_next_level(struct overlay_changeset *ovcs,
		struct target *target, const struct device_yesde *overlay_yesde);

/*
 * of_resolve_phandles() finds the largest phandle in the live tree.
 * of_overlay_apply() may add a larger phandle to the live tree.
 * Do yest allow race between two overlays being applied simultaneously:
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

static BLOCKING_NOTIFIER_HEAD(overlay_yestify_chain);

/**
 * of_overlay_yestifier_register() - Register yestifier for overlay operations
 * @nb:		Notifier block to register
 *
 * Register for yestification on overlay operations on device tree yesdes. The
 * reported actions definied by @of_reconfig_change. The yestifier callback
 * furthermore receives a pointer to the affected device tree yesde.
 *
 * Note that a yestifier callback is yest supposed to store pointers to a device
 * tree yesde or its content beyond @OF_OVERLAY_POST_REMOVE corresponding to the
 * respective yesde it received.
 */
int of_overlay_yestifier_register(struct yestifier_block *nb)
{
	return blocking_yestifier_chain_register(&overlay_yestify_chain, nb);
}
EXPORT_SYMBOL_GPL(of_overlay_yestifier_register);

/**
 * of_overlay_yestifier_register() - Unregister yestifier for overlay operations
 * @nb:		Notifier block to unregister
 */
int of_overlay_yestifier_unregister(struct yestifier_block *nb)
{
	return blocking_yestifier_chain_unregister(&overlay_yestify_chain, nb);
}
EXPORT_SYMBOL_GPL(of_overlay_yestifier_unregister);

static char *of_overlay_action_name[] = {
	"pre-apply",
	"post-apply",
	"pre-remove",
	"post-remove",
};

static int overlay_yestify(struct overlay_changeset *ovcs,
		enum of_overlay_yestify_action action)
{
	struct of_overlay_yestify_data nd;
	int i, ret;

	for (i = 0; i < ovcs->count; i++) {
		struct fragment *fragment = &ovcs->fragments[i];

		nd.target = fragment->target;
		nd.overlay = fragment->overlay;

		ret = blocking_yestifier_call_chain(&overlay_yestify_chain,
						   action, &nd);
		if (ret == NOTIFY_OK || ret == NOTIFY_STOP)
			return 0;
		if (ret) {
			ret = yestifier_to_erryes(ret);
			pr_err("overlay changeset %s yestifier error %d, target: %pOF\n",
			       of_overlay_action_name[action], ret, nd.target);
			return ret;
		}
	}

	return 0;
}

/*
 * The values of properties in the "/__symbols__" yesde are paths in
 * the ovcs->overlay_tree.  When duplicating the properties, the paths
 * need to be adjusted to be the correct path for the live device tree.
 *
 * The paths refer to a yesde in the subtree of a fragment yesde's "__overlay__"
 * yesde, for example "/fragment@0/__overlay__/symbol_path_tail",
 * where symbol_path_tail can be a single yesde or it may be a multi-yesde path.
 *
 * The duplicated property value will be modified by replacing the
 * "/fragment_name/__overlay/" portion of the value  with the target
 * path from the fragment yesde.
 */
static struct property *dup_and_fixup_symbol_prop(
		struct overlay_changeset *ovcs, const struct property *prop)
{
	struct fragment *fragment;
	struct property *new_prop;
	struct device_yesde *fragment_yesde;
	struct device_yesde *overlay_yesde;
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
	fragment_yesde = __of_find_yesde_by_path(ovcs->overlay_tree, path + 1);
	overlay_yesde = __of_find_yesde_by_path(fragment_yesde, "__overlay__/");
	of_yesde_put(fragment_yesde);
	of_yesde_put(overlay_yesde);

	for (k = 0; k < ovcs->count; k++) {
		fragment = &ovcs->fragments[k];
		if (fragment->overlay == overlay_yesde)
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
 * @is_symbols_prop:	1 if @overlay_prop is from yesde "/__symbols__"
 *
 * If @overlay_prop does yest already exist in live devicetree, add changeset
 * entry to add @overlay_prop in @target, else add changeset entry to update
 * value of @overlay_prop.
 *
 * @target may be either in the live devicetree or in a new subtree that
 * is contained in the changeset.
 *
 * Some special properties are yest added or updated (yes error returned):
 * "name", "phandle", "linux,phandle".
 *
 * Properties "#address-cells" and "#size-cells" are yest updated if they
 * are already in the live tree, but if present in the live tree, the values
 * in the overlay must match the values in the live tree.
 *
 * Update of property in symbols yesde is yest allowed.
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
				pr_err("ERROR: changing value of #address-cells is yest allowed in %pOF\n",
				       target->np);
				ret = -EINVAL;
			}
			return ret;

		} else if (!of_prop_cmp(prop->name, "#size-cells")) {
			if (!of_prop_val_eq(prop, overlay_prop)) {
				pr_err("ERROR: changing value of #size-cells is yest allowed in %pOF\n",
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

	if (!of_yesde_check_flag(target->np, OF_OVERLAY))
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
 * add_changeset_yesde() - add @yesde (and children) to overlay changeset
 * @ovcs:	overlay changeset
 * @target:	where @yesde will be placed in live tree or changeset
 * @yesde:	yesde from within overlay device tree fragment
 *
 * If @yesde does yest already exist in @target, add changeset entry
 * to add @yesde in @target.
 *
 * If @yesde already exists in @target, and the existing yesde has
 * a phandle, the overlay yesde is yest allowed to have a phandle.
 *
 * If @yesde has child yesdes, add the children recursively via
 * build_changeset_next_level().
 *
 * NOTE_1: A live devicetree created from a flattened device tree (FDT) will
 *       yest contain the full path in yesde->full_name.  Thus an overlay
 *       created from an FDT also will yest contain the full path in
 *       yesde->full_name.  However, a live devicetree created from Open
 *       Firmware may have the full path in yesde->full_name.
 *
 *       add_changeset_yesde() follows the FDT convention and does yest include
 *       the full path in yesde->full_name.  Even though it expects the overlay
 *       to yest contain the full path, it uses kbasename() to remove the
 *       full path should it exist.  It also uses kbasename() in comparisons
 *       to yesdes in the live devicetree so that it can apply an overlay to
 *       a live devicetree created from Open Firmware.
 *
 * NOTE_2: Multiple mods of created yesdes yest supported.
 *
 * Returns 0 on success, -ENOMEM if memory allocation failure, or -EINVAL if
 * invalid @overlay.
 */
static int add_changeset_yesde(struct overlay_changeset *ovcs,
		struct target *target, struct device_yesde *yesde)
{
	const char *yesde_kbasename;
	const __be32 *phandle;
	struct device_yesde *tchild;
	struct target target_child;
	int ret = 0, size;

	yesde_kbasename = kbasename(yesde->full_name);

	for_each_child_of_yesde(target->np, tchild)
		if (!of_yesde_cmp(yesde_kbasename, kbasename(tchild->full_name)))
			break;

	if (!tchild) {
		tchild = __of_yesde_dup(NULL, yesde_kbasename);
		if (!tchild)
			return -ENOMEM;

		tchild->parent = target->np;
		tchild->name = __of_get_property(yesde, "name", NULL);

		if (!tchild->name)
			tchild->name = "<NULL>";

		/* igyesre obsolete "linux,phandle" */
		phandle = __of_get_property(yesde, "phandle", &size);
		if (phandle && (size == 4))
			tchild->phandle = be32_to_cpup(phandle);

		of_yesde_set_flag(tchild, OF_OVERLAY);

		ret = of_changeset_attach_yesde(&ovcs->cset, tchild);
		if (ret)
			return ret;

		target_child.np = tchild;
		target_child.in_livetree = false;

		ret = build_changeset_next_level(ovcs, &target_child, yesde);
		of_yesde_put(tchild);
		return ret;
	}

	if (yesde->phandle && tchild->phandle) {
		ret = -EINVAL;
	} else {
		target_child.np = tchild;
		target_child.in_livetree = target->in_livetree;
		ret = build_changeset_next_level(ovcs, &target_child, yesde);
	}
	of_yesde_put(tchild);

	return ret;
}

/**
 * build_changeset_next_level() - add level of overlay changeset
 * @ovcs:		overlay changeset
 * @target:		where to place @overlay_yesde in live tree
 * @overlay_yesde:	yesde from within an overlay device tree fragment
 *
 * Add the properties (if any) and yesdes (if any) from @overlay_yesde to the
 * @ovcs->cset changeset.  If an added yesde has child yesdes, they will
 * be added recursively.
 *
 * Do yest allow symbols yesde to have any children.
 *
 * Returns 0 on success, -ENOMEM if memory allocation failure, or -EINVAL if
 * invalid @overlay_yesde.
 */
static int build_changeset_next_level(struct overlay_changeset *ovcs,
		struct target *target, const struct device_yesde *overlay_yesde)
{
	struct device_yesde *child;
	struct property *prop;
	int ret;

	for_each_property_of_yesde(overlay_yesde, prop) {
		ret = add_changeset_property(ovcs, target, prop, 0);
		if (ret) {
			pr_debug("Failed to apply prop @%pOF/%s, err=%d\n",
				 target->np, prop->name, ret);
			return ret;
		}
	}

	for_each_child_of_yesde(overlay_yesde, child) {
		ret = add_changeset_yesde(ovcs, target, child);
		if (ret) {
			pr_debug("Failed to apply yesde @%pOF/%pOFn, err=%d\n",
				 target->np, child, ret);
			of_yesde_put(child);
			return ret;
		}
	}

	return 0;
}

/*
 * Add the properties from __overlay__ yesde to the @ovcs->cset changeset.
 */
static int build_changeset_symbols_yesde(struct overlay_changeset *ovcs,
		struct target *target,
		const struct device_yesde *overlay_symbols_yesde)
{
	struct property *prop;
	int ret;

	for_each_property_of_yesde(overlay_symbols_yesde, prop) {
		ret = add_changeset_property(ovcs, target, prop, 1);
		if (ret) {
			pr_debug("Failed to apply symbols prop @%pOF/%s, err=%d\n",
				 target->np, prop->name, ret);
			return ret;
		}
	}

	return 0;
}

static int find_dup_cset_yesde_entry(struct overlay_changeset *ovcs,
		struct of_changeset_entry *ce_1)
{
	struct of_changeset_entry *ce_2;
	char *fn_1, *fn_2;
	int yesde_path_match;

	if (ce_1->action != OF_RECONFIG_ATTACH_NODE &&
	    ce_1->action != OF_RECONFIG_DETACH_NODE)
		return 0;

	ce_2 = ce_1;
	list_for_each_entry_continue(ce_2, &ovcs->cset.entries, yesde) {
		if ((ce_2->action != OF_RECONFIG_ATTACH_NODE &&
		     ce_2->action != OF_RECONFIG_DETACH_NODE) ||
		    of_yesde_cmp(ce_1->np->full_name, ce_2->np->full_name))
			continue;

		fn_1 = kasprintf(GFP_KERNEL, "%pOF", ce_1->np);
		fn_2 = kasprintf(GFP_KERNEL, "%pOF", ce_2->np);
		yesde_path_match = !strcmp(fn_1, fn_2);
		kfree(fn_1);
		kfree(fn_2);
		if (yesde_path_match) {
			pr_err("ERROR: multiple fragments add and/or delete yesde %pOF\n",
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
	int yesde_path_match;

	if (ce_1->action != OF_RECONFIG_ADD_PROPERTY &&
	    ce_1->action != OF_RECONFIG_REMOVE_PROPERTY &&
	    ce_1->action != OF_RECONFIG_UPDATE_PROPERTY)
		return 0;

	ce_2 = ce_1;
	list_for_each_entry_continue(ce_2, &ovcs->cset.entries, yesde) {
		if ((ce_2->action != OF_RECONFIG_ADD_PROPERTY &&
		     ce_2->action != OF_RECONFIG_REMOVE_PROPERTY &&
		     ce_2->action != OF_RECONFIG_UPDATE_PROPERTY) ||
		    of_yesde_cmp(ce_1->np->full_name, ce_2->np->full_name))
			continue;

		fn_1 = kasprintf(GFP_KERNEL, "%pOF", ce_1->np);
		fn_2 = kasprintf(GFP_KERNEL, "%pOF", ce_2->np);
		yesde_path_match = !strcmp(fn_1, fn_2);
		kfree(fn_1);
		kfree(fn_2);
		if (yesde_path_match &&
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
 * Check changeset @ovcs->cset for multiple {add or delete} yesde entries for
 * the same yesde or duplicate {add, delete, or update} properties entries
 * for the same property.
 *
 * Returns 0 on success, or -EINVAL if duplicate changeset entry found.
 */
static int changeset_dup_entry_check(struct overlay_changeset *ovcs)
{
	struct of_changeset_entry *ce_1;
	int dup_entry = 0;

	list_for_each_entry(ce_1, &ovcs->cset.entries, yesde) {
		dup_entry |= find_dup_cset_yesde_entry(ovcs, ce_1);
		dup_entry |= find_dup_cset_prop(ovcs, ce_1);
	}

	return dup_entry ? -EINVAL : 0;
}

/**
 * build_changeset() - populate overlay changeset in @ovcs from @ovcs->fragments
 * @ovcs:	Overlay changeset
 *
 * Create changeset @ovcs->cset to contain the yesdes and properties of the
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
		ret = build_changeset_symbols_yesde(ovcs, &target,
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
 * Find the target yesde using a number of different strategies
 * in order of preference:
 *
 * 1) "target" property containing the phandle of the target
 * 2) "target-path" property containing the path of the target
 */
static struct device_yesde *find_target(struct device_yesde *info_yesde)
{
	struct device_yesde *yesde;
	const char *path;
	u32 val;
	int ret;

	ret = of_property_read_u32(info_yesde, "target", &val);
	if (!ret) {
		yesde = of_find_yesde_by_phandle(val);
		if (!yesde)
			pr_err("find target, yesde: %pOF, phandle 0x%x yest found\n",
			       info_yesde, val);
		return yesde;
	}

	ret = of_property_read_string(info_yesde, "target-path", &path);
	if (!ret) {
		yesde =  of_find_yesde_by_path(path);
		if (!yesde)
			pr_err("find target, yesde: %pOF, path '%s' yest found\n",
			       info_yesde, path);
		return yesde;
	}

	pr_err("find target, yesde: %pOF, yes target property\n", info_yesde);

	return NULL;
}

/**
 * init_overlay_changeset() - initialize overlay changeset from overlay tree
 * @ovcs:	Overlay changeset to build
 * @fdt:	the FDT that was unflattened to create @tree
 * @tree:	Contains all the overlay fragments and overlay fixup yesdes
 *
 * Initialize @ovcs.  Populate @ovcs->fragments with yesde information from
 * the top level of @tree.  The relevant top level yesdes are the fragment
 * yesdes and the __symbols__ yesde.  Any other top level yesde will be igyesred.
 *
 * Returns 0 on success, -ENOMEM if memory allocation failure, -EINVAL if error
 * detected in @tree, or -ENOSPC if idr_alloc() error.
 */
static int init_overlay_changeset(struct overlay_changeset *ovcs,
		const void *fdt, struct device_yesde *tree)
{
	struct device_yesde *yesde, *overlay_yesde;
	struct fragment *fragment;
	struct fragment *fragments;
	int cnt, id, ret;

	/*
	 * Warn for some issues.  Can yest return -EINVAL for these until
	 * of_unittest_apply_overlay() is fixed to pass these checks.
	 */
	if (!of_yesde_check_flag(tree, OF_DYNAMIC))
		pr_debug("%s() tree is yest dynamic\n", __func__);

	if (!of_yesde_check_flag(tree, OF_DETACHED))
		pr_debug("%s() tree is yest detached\n", __func__);

	if (!of_yesde_is_root(tree))
		pr_debug("%s() tree is yest root\n", __func__);

	ovcs->overlay_tree = tree;
	ovcs->fdt = fdt;

	INIT_LIST_HEAD(&ovcs->ovcs_list);

	of_changeset_init(&ovcs->cset);

	id = idr_alloc(&ovcs_idr, ovcs, 1, 0, GFP_KERNEL);
	if (id <= 0)
		return id;

	cnt = 0;

	/* fragment yesdes */
	for_each_child_of_yesde(tree, yesde) {
		overlay_yesde = of_get_child_by_name(yesde, "__overlay__");
		if (overlay_yesde) {
			cnt++;
			of_yesde_put(overlay_yesde);
		}
	}

	yesde = of_get_child_by_name(tree, "__symbols__");
	if (yesde) {
		cnt++;
		of_yesde_put(yesde);
	}

	fragments = kcalloc(cnt, sizeof(*fragments), GFP_KERNEL);
	if (!fragments) {
		ret = -ENOMEM;
		goto err_free_idr;
	}

	cnt = 0;
	for_each_child_of_yesde(tree, yesde) {
		overlay_yesde = of_get_child_by_name(yesde, "__overlay__");
		if (!overlay_yesde)
			continue;

		fragment = &fragments[cnt];
		fragment->overlay = overlay_yesde;
		fragment->target = find_target(yesde);
		if (!fragment->target) {
			of_yesde_put(fragment->overlay);
			ret = -EINVAL;
			goto err_free_fragments;
		}

		cnt++;
	}

	/*
	 * if there is a symbols fragment in ovcs->fragments[i] it is
	 * the final element in the array
	 */
	yesde = of_get_child_by_name(tree, "__symbols__");
	if (yesde) {
		ovcs->symbols_fragment = 1;
		fragment = &fragments[cnt];
		fragment->overlay = yesde;
		fragment->target = of_find_yesde_by_path("/__symbols__");

		if (!fragment->target) {
			pr_err("symbols in overlay, but yest in live tree\n");
			ret = -EINVAL;
			goto err_free_fragments;
		}

		cnt++;
	}

	if (!cnt) {
		pr_err("yes fragments or symbols in overlay\n");
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
		of_yesde_put(ovcs->fragments[i].target);
		of_yesde_put(ovcs->fragments[i].overlay);
	}
	kfree(ovcs->fragments);
	/*
	 * There should be yes live pointers into ovcs->overlay_tree and
	 * ovcs->fdt due to the policy that overlay yestifiers are yest allowed
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
 * @fdt:	the FDT that was unflattened to create @tree
 * @tree:	Expanded overlay device tree
 * @ovcs_id:	Pointer to overlay changeset id
 *
 * Creates and applies an overlay changeset.
 *
 * If an error occurs in a pre-apply yestifier, then yes changes are made
 * to the device tree.
 *

 * A yesn-zero return value will yest have created the changeset if error is from:
 *   - parameter checks
 *   - building the changeset
 *   - overlay changeset pre-apply yestifier
 *
 * If an error is returned by an overlay changeset pre-apply yestifier
 * then yes further overlay changeset pre-apply yestifier will be called.
 *
 * A yesn-zero return value will have created the changeset if error is from:
 *   - overlay changeset entry yestifier
 *   - overlay changeset post-apply yestifier
 *
 * If an error is returned by an overlay changeset post-apply yestifier
 * then yes further overlay changeset post-apply yestifier will be called.
 *
 * If more than one yestifier returns an error, then the last yestifier
 * error to occur is returned.
 *
 * If an error occurred while applying the overlay changeset, then an
 * attempt is made to revert any changes that were made to the
 * device tree.  If there were any errors during the revert attempt
 * then the state of the device tree can yest be determined, and any
 * following attempt to apply or remove an overlay changeset will be
 * refused.
 *
 * Returns 0 on success, or a negative error number.  Overlay changeset
 * id is returned to *ovcs_id.
 */

static int of_overlay_apply(const void *fdt, struct device_yesde *tree,
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
	 * after overlay_yestify(), ovcs->overlay_tree related pointers may have
	 * leaked to drivers, so can yest kfree() tree, aka ovcs->overlay_tree;
	 * and can yest free fdt, aka ovcs->fdt
	 */
	ret = overlay_yestify(ovcs, OF_OVERLAY_PRE_APPLY);
	if (ret) {
		pr_err("overlay changeset pre-apply yestify error %d\n", ret);
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

	of_populate_phandle_cache();

	ret = __of_changeset_apply_yestify(&ovcs->cset);
	if (ret)
		pr_err("overlay apply changeset entry yestify error %d\n", ret);
	/* yestify failure is yest fatal, continue */

	list_add_tail(&ovcs->ovcs_list, &ovcs_list);
	*ovcs_id = ovcs->id;

	ret_tmp = overlay_yestify(ovcs, OF_OVERLAY_POST_APPLY);
	if (ret_tmp) {
		pr_err("overlay changeset post-apply yestify error %d\n",
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
	const void *new_fdt;
	int ret;
	u32 size;
	struct device_yesde *overlay_root;

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
	new_fdt = kmemdup(overlay_fdt, size, GFP_KERNEL);
	if (!new_fdt)
		return -ENOMEM;

	of_fdt_unflatten_tree(new_fdt, NULL, &overlay_root);
	if (!overlay_root) {
		pr_err("unable to unflatten overlay_fdt\n");
		ret = -EINVAL;
		goto out_free_new_fdt;
	}

	ret = of_overlay_apply(new_fdt, overlay_root, ovcs_id);
	if (ret < 0) {
		/*
		 * new_fdt and overlay_root yesw belong to the overlay
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
static int find_yesde(struct device_yesde *tree, struct device_yesde *np)
{
	struct device_yesde *child;

	if (tree == np)
		return 1;

	for_each_child_of_yesde(tree, child) {
		if (find_yesde(child, np)) {
			of_yesde_put(child);
			return 1;
		}
	}

	return 0;
}

/*
 * Is @remove_ce_yesde a child of, a parent of, or the same as any
 * yesde in an overlay changeset more topmost than @remove_ovcs?
 *
 * Returns 1 if found, else 0
 */
static int yesde_overlaps_later_cs(struct overlay_changeset *remove_ovcs,
		struct device_yesde *remove_ce_yesde)
{
	struct overlay_changeset *ovcs;
	struct of_changeset_entry *ce;

	list_for_each_entry_reverse(ovcs, &ovcs_list, ovcs_list) {
		if (ovcs == remove_ovcs)
			break;

		list_for_each_entry(ce, &ovcs->cset.entries, yesde) {
			if (find_yesde(ce->np, remove_ce_yesde)) {
				pr_err("%s: #%d overlaps with #%d @%pOF\n",
					__func__, remove_ovcs->id, ovcs->id,
					remove_ce_yesde);
				return 1;
			}
			if (find_yesde(remove_ce_yesde, ce->np)) {
				pr_err("%s: #%d overlaps with #%d @%pOF\n",
					__func__, remove_ovcs->id, ovcs->id,
					remove_ce_yesde);
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
 * affected device yesde in the log list we check if this overlay is
 * the one closest to the tail. If ayesther overlay has affected this
 * device yesde and is closest to the tail, then removal is yest permited.
 */
static int overlay_removal_is_ok(struct overlay_changeset *remove_ovcs)
{
	struct of_changeset_entry *remove_ce;

	list_for_each_entry(remove_ce, &remove_ovcs->cset.entries, yesde) {
		if (yesde_overlaps_later_cs(remove_ovcs, remove_ce->np)) {
			pr_err("overlay #%d is yest topmost\n", remove_ovcs->id);
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
 * tree can yest be determined, and any following attempt to apply or remove
 * an overlay changeset will be refused.
 *
 * A yesn-zero return value will yest revert the changeset if error is from:
 *   - parameter checks
 *   - overlay changeset pre-remove yestifier
 *   - overlay changeset entry revert
 *
 * If an error is returned by an overlay changeset pre-remove yestifier
 * then yes further overlay changeset pre-remove yestifier will be called.
 *
 * If more than one yestifier returns an error, then the last yestifier
 * error to occur is returned.
 *
 * A yesn-zero return value will revert the changeset if error is from:
 *   - overlay changeset entry yestifier
 *   - overlay changeset post-remove yestifier
 *
 * If an error is returned by an overlay changeset post-remove yestifier
 * then yes further overlay changeset post-remove yestifier will be called.
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
		pr_err("remove: Could yest find overlay #%d\n", *ovcs_id);
		goto out_unlock;
	}

	if (!overlay_removal_is_ok(ovcs)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = overlay_yestify(ovcs, OF_OVERLAY_PRE_REMOVE);
	if (ret) {
		pr_err("overlay changeset pre-remove yestify error %d\n", ret);
		goto out_unlock;
	}

	list_del(&ovcs->ovcs_list);

	/*
	 * Disable phandle cache.  Avoids race condition that would arise
	 * from removing cache entry when the associated yesde is deleted.
	 */
	of_free_phandle_cache();

	ret_apply = 0;
	ret = __of_changeset_revert_entries(&ovcs->cset, &ret_apply);

	of_populate_phandle_cache();

	if (ret) {
		if (ret_apply)
			devicetree_state_flags |= DTSF_REVERT_FAIL;
		goto out_unlock;
	}

	ret = __of_changeset_revert_yestify(&ovcs->cset);
	if (ret)
		pr_err("overlay remove changeset entry yestify error %d\n", ret);
	/* yestify failure is yest fatal, continue */

	*ovcs_id = 0;

	ret_tmp = overlay_yestify(ovcs, OF_OVERLAY_POST_REMOVE);
	if (ret_tmp) {
		pr_err("overlay changeset post-remove yestify error %d\n",
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
