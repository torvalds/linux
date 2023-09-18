// SPDX-License-Identifier: GPL-2.0
/*
 * Support for dynamic device trees.
 *
 * On some platforms, the device tree can be manipulated at runtime.
 * The routines in this section support adding, removing and changing
 * device tree nodes.
 */

#define pr_fmt(fmt)	"OF: " fmt

#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/proc_fs.h>

#include "of_private.h"

static struct device_node *kobj_to_device_node(struct kobject *kobj)
{
	return container_of(kobj, struct device_node, kobj);
}

/**
 * of_node_get() - Increment refcount of a node
 * @node:	Node to inc refcount, NULL is supported to simplify writing of
 *		callers
 *
 * Return: The node with refcount incremented.
 */
struct device_node *of_node_get(struct device_node *node)
{
	if (node)
		kobject_get(&node->kobj);
	return node;
}
EXPORT_SYMBOL(of_node_get);

/**
 * of_node_put() - Decrement refcount of a node
 * @node:	Node to dec refcount, NULL is supported to simplify writing of
 *		callers
 */
void of_node_put(struct device_node *node)
{
	if (node)
		kobject_put(&node->kobj);
}
EXPORT_SYMBOL(of_node_put);

static BLOCKING_NOTIFIER_HEAD(of_reconfig_chain);

int of_reconfig_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&of_reconfig_chain, nb);
}
EXPORT_SYMBOL_GPL(of_reconfig_notifier_register);

int of_reconfig_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&of_reconfig_chain, nb);
}
EXPORT_SYMBOL_GPL(of_reconfig_notifier_unregister);

static const char *action_names[] = {
	[0] = "INVALID",
	[OF_RECONFIG_ATTACH_NODE] = "ATTACH_NODE",
	[OF_RECONFIG_DETACH_NODE] = "DETACH_NODE",
	[OF_RECONFIG_ADD_PROPERTY] = "ADD_PROPERTY",
	[OF_RECONFIG_REMOVE_PROPERTY] = "REMOVE_PROPERTY",
	[OF_RECONFIG_UPDATE_PROPERTY] = "UPDATE_PROPERTY",
};

int of_reconfig_notify(unsigned long action, struct of_reconfig_data *p)
{
	int rc;
#ifdef DEBUG
	struct of_reconfig_data *pr = p;

	switch (action) {
	case OF_RECONFIG_ATTACH_NODE:
	case OF_RECONFIG_DETACH_NODE:
		pr_debug("notify %-15s %pOF\n", action_names[action],
			pr->dn);
		break;
	case OF_RECONFIG_ADD_PROPERTY:
	case OF_RECONFIG_REMOVE_PROPERTY:
	case OF_RECONFIG_UPDATE_PROPERTY:
		pr_debug("notify %-15s %pOF:%s\n", action_names[action],
			pr->dn, pr->prop->name);
		break;

	}
#endif
	rc = blocking_notifier_call_chain(&of_reconfig_chain, action, p);
	return notifier_to_errno(rc);
}

/*
 * of_reconfig_get_state_change()	- Returns new state of device
 * @action	- action of the of notifier
 * @arg		- argument of the of notifier
 *
 * Returns the new state of a device based on the notifier used.
 *
 * Return: 0 on device going from enabled to disabled, 1 on device
 * going from disabled to enabled and -1 on no change.
 */
int of_reconfig_get_state_change(unsigned long action, struct of_reconfig_data *pr)
{
	struct property *prop, *old_prop = NULL;
	int is_status, status_state, old_status_state, prev_state, new_state;

	/* figure out if a device should be created or destroyed */
	switch (action) {
	case OF_RECONFIG_ATTACH_NODE:
	case OF_RECONFIG_DETACH_NODE:
		prop = of_find_property(pr->dn, "status", NULL);
		break;
	case OF_RECONFIG_ADD_PROPERTY:
	case OF_RECONFIG_REMOVE_PROPERTY:
		prop = pr->prop;
		break;
	case OF_RECONFIG_UPDATE_PROPERTY:
		prop = pr->prop;
		old_prop = pr->old_prop;
		break;
	default:
		return OF_RECONFIG_NO_CHANGE;
	}

	is_status = 0;
	status_state = -1;
	old_status_state = -1;
	prev_state = -1;
	new_state = -1;

	if (prop && !strcmp(prop->name, "status")) {
		is_status = 1;
		status_state = !strcmp(prop->value, "okay") ||
			       !strcmp(prop->value, "ok");
		if (old_prop)
			old_status_state = !strcmp(old_prop->value, "okay") ||
					   !strcmp(old_prop->value, "ok");
	}

	switch (action) {
	case OF_RECONFIG_ATTACH_NODE:
		prev_state = 0;
		/* -1 & 0 status either missing or okay */
		new_state = status_state != 0;
		break;
	case OF_RECONFIG_DETACH_NODE:
		/* -1 & 0 status either missing or okay */
		prev_state = status_state != 0;
		new_state = 0;
		break;
	case OF_RECONFIG_ADD_PROPERTY:
		if (is_status) {
			/* no status property -> enabled (legacy) */
			prev_state = 1;
			new_state = status_state;
		}
		break;
	case OF_RECONFIG_REMOVE_PROPERTY:
		if (is_status) {
			prev_state = status_state;
			/* no status property -> enabled (legacy) */
			new_state = 1;
		}
		break;
	case OF_RECONFIG_UPDATE_PROPERTY:
		if (is_status) {
			prev_state = old_status_state != 0;
			new_state = status_state != 0;
		}
		break;
	}

	if (prev_state == new_state)
		return OF_RECONFIG_NO_CHANGE;

	return new_state ? OF_RECONFIG_CHANGE_ADD : OF_RECONFIG_CHANGE_REMOVE;
}
EXPORT_SYMBOL_GPL(of_reconfig_get_state_change);

int of_property_notify(int action, struct device_node *np,
		       struct property *prop, struct property *oldprop)
{
	struct of_reconfig_data pr;

	/* only call notifiers if the node is attached */
	if (!of_node_is_attached(np))
		return 0;

	pr.dn = np;
	pr.prop = prop;
	pr.old_prop = oldprop;
	return of_reconfig_notify(action, &pr);
}

static void __of_attach_node(struct device_node *np)
{
	const __be32 *phandle;
	int sz;

	if (!of_node_check_flag(np, OF_OVERLAY)) {
		np->name = __of_get_property(np, "name", NULL);
		if (!np->name)
			np->name = "<NULL>";

		phandle = __of_get_property(np, "phandle", &sz);
		if (!phandle)
			phandle = __of_get_property(np, "linux,phandle", &sz);
		if (IS_ENABLED(CONFIG_PPC_PSERIES) && !phandle)
			phandle = __of_get_property(np, "ibm,phandle", &sz);
		if (phandle && (sz >= 4))
			np->phandle = be32_to_cpup(phandle);
		else
			np->phandle = 0;
	}

	np->child = NULL;
	np->sibling = np->parent->child;
	np->parent->child = np;
	of_node_clear_flag(np, OF_DETACHED);
	np->fwnode.flags |= FWNODE_FLAG_NOT_DEVICE;
}

/**
 * of_attach_node() - Plug a device node into the tree and global list.
 * @np:		Pointer to the caller's Device Node
 */
int of_attach_node(struct device_node *np)
{
	struct of_reconfig_data rd;
	unsigned long flags;

	memset(&rd, 0, sizeof(rd));
	rd.dn = np;

	mutex_lock(&of_mutex);
	raw_spin_lock_irqsave(&devtree_lock, flags);
	__of_attach_node(np);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);

	__of_attach_node_sysfs(np);
	mutex_unlock(&of_mutex);

	of_reconfig_notify(OF_RECONFIG_ATTACH_NODE, &rd);

	return 0;
}

void __of_detach_node(struct device_node *np)
{
	struct device_node *parent;

	if (WARN_ON(of_node_check_flag(np, OF_DETACHED)))
		return;

	parent = np->parent;
	if (WARN_ON(!parent))
		return;

	if (parent->child == np)
		parent->child = np->sibling;
	else {
		struct device_node *prevsib;
		for (prevsib = np->parent->child;
		     prevsib->sibling != np;
		     prevsib = prevsib->sibling)
			;
		prevsib->sibling = np->sibling;
	}

	of_node_set_flag(np, OF_DETACHED);

	/* race with of_find_node_by_phandle() prevented by devtree_lock */
	__of_phandle_cache_inv_entry(np->phandle);
}

/**
 * of_detach_node() - "Unplug" a node from the device tree.
 * @np:		Pointer to the caller's Device Node
 */
int of_detach_node(struct device_node *np)
{
	struct of_reconfig_data rd;
	unsigned long flags;

	memset(&rd, 0, sizeof(rd));
	rd.dn = np;

	mutex_lock(&of_mutex);
	raw_spin_lock_irqsave(&devtree_lock, flags);
	__of_detach_node(np);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);

	__of_detach_node_sysfs(np);
	mutex_unlock(&of_mutex);

	of_reconfig_notify(OF_RECONFIG_DETACH_NODE, &rd);

	return 0;
}
EXPORT_SYMBOL_GPL(of_detach_node);

static void property_list_free(struct property *prop_list)
{
	struct property *prop, *next;

	for (prop = prop_list; prop != NULL; prop = next) {
		next = prop->next;
		kfree(prop->name);
		kfree(prop->value);
		kfree(prop);
	}
}

/**
 * of_node_release() - release a dynamically allocated node
 * @kobj: kernel object of the node to be released
 *
 * In of_node_put() this function is passed to kref_put() as the destructor.
 */
void of_node_release(struct kobject *kobj)
{
	struct device_node *node = kobj_to_device_node(kobj);

	/* We should never be releasing nodes that haven't been detached. */
	if (!of_node_check_flag(node, OF_DETACHED)) {
		pr_err("ERROR: Bad of_node_put() on %pOF\n", node);
		dump_stack();
		return;
	}
	if (!of_node_check_flag(node, OF_DYNAMIC))
		return;

	if (of_node_check_flag(node, OF_OVERLAY)) {

		if (!of_node_check_flag(node, OF_OVERLAY_FREE_CSET)) {
			/* premature refcount of zero, do not free memory */
			pr_err("ERROR: memory leak before free overlay changeset,  %pOF\n",
			       node);
			return;
		}

		/*
		 * If node->properties non-empty then properties were added
		 * to this node either by different overlay that has not
		 * yet been removed, or by a non-overlay mechanism.
		 */
		if (node->properties)
			pr_err("ERROR: %s(), unexpected properties in %pOF\n",
			       __func__, node);
	}

	property_list_free(node->properties);
	property_list_free(node->deadprops);
	fwnode_links_purge(of_fwnode_handle(node));

	kfree(node->full_name);
	kfree(node->data);
	kfree(node);
}

/**
 * __of_prop_dup - Copy a property dynamically.
 * @prop:	Property to copy
 * @allocflags:	Allocation flags (typically pass GFP_KERNEL)
 *
 * Copy a property by dynamically allocating the memory of both the
 * property structure and the property name & contents. The property's
 * flags have the OF_DYNAMIC bit set so that we can differentiate between
 * dynamically allocated properties and not.
 *
 * Return: The newly allocated property or NULL on out of memory error.
 */
struct property *__of_prop_dup(const struct property *prop, gfp_t allocflags)
{
	struct property *new;

	new = kzalloc(sizeof(*new), allocflags);
	if (!new)
		return NULL;

	/*
	 * NOTE: There is no check for zero length value.
	 * In case of a boolean property, this will allocate a value
	 * of zero bytes. We do this to work around the use
	 * of of_get_property() calls on boolean values.
	 */
	new->name = kstrdup(prop->name, allocflags);
	new->value = kmemdup(prop->value, prop->length, allocflags);
	new->length = prop->length;
	if (!new->name || !new->value)
		goto err_free;

	/* mark the property as dynamic */
	of_property_set_flag(new, OF_DYNAMIC);

	return new;

 err_free:
	kfree(new->name);
	kfree(new->value);
	kfree(new);
	return NULL;
}

/**
 * __of_node_dup() - Duplicate or create an empty device node dynamically.
 * @np:		if not NULL, contains properties to be duplicated in new node
 * @full_name:	string value to be duplicated into new node's full_name field
 *
 * Create a device tree node, optionally duplicating the properties of
 * another node.  The node data are dynamically allocated and all the node
 * flags have the OF_DYNAMIC & OF_DETACHED bits set.
 *
 * Return: The newly allocated node or NULL on out of memory error.
 */
struct device_node *__of_node_dup(const struct device_node *np,
				  const char *full_name)
{
	struct device_node *node;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return NULL;
	node->full_name = kstrdup(full_name, GFP_KERNEL);
	if (!node->full_name) {
		kfree(node);
		return NULL;
	}

	of_node_set_flag(node, OF_DYNAMIC);
	of_node_set_flag(node, OF_DETACHED);
	of_node_init(node);

	/* Iterate over and duplicate all properties */
	if (np) {
		struct property *pp, *new_pp;
		for_each_property_of_node(np, pp) {
			new_pp = __of_prop_dup(pp, GFP_KERNEL);
			if (!new_pp)
				goto err_prop;
			if (__of_add_property(node, new_pp)) {
				kfree(new_pp->name);
				kfree(new_pp->value);
				kfree(new_pp);
				goto err_prop;
			}
		}
	}
	return node;

 err_prop:
	of_node_put(node); /* Frees the node and properties */
	return NULL;
}

static void __of_changeset_entry_destroy(struct of_changeset_entry *ce)
{
	if (ce->action == OF_RECONFIG_ATTACH_NODE &&
	    of_node_check_flag(ce->np, OF_OVERLAY)) {
		if (kref_read(&ce->np->kobj.kref) > 1) {
			pr_err("ERROR: memory leak, expected refcount 1 instead of %d, of_node_get()/of_node_put() unbalanced - destroy cset entry: attach overlay node %pOF\n",
			       kref_read(&ce->np->kobj.kref), ce->np);
		} else {
			of_node_set_flag(ce->np, OF_OVERLAY_FREE_CSET);
		}
	}

	of_node_put(ce->np);
	list_del(&ce->node);
	kfree(ce);
}

#ifdef DEBUG
static void __of_changeset_entry_dump(struct of_changeset_entry *ce)
{
	switch (ce->action) {
	case OF_RECONFIG_ADD_PROPERTY:
	case OF_RECONFIG_REMOVE_PROPERTY:
	case OF_RECONFIG_UPDATE_PROPERTY:
		pr_debug("cset<%p> %-15s %pOF/%s\n", ce, action_names[ce->action],
			ce->np, ce->prop->name);
		break;
	case OF_RECONFIG_ATTACH_NODE:
	case OF_RECONFIG_DETACH_NODE:
		pr_debug("cset<%p> %-15s %pOF\n", ce, action_names[ce->action],
			ce->np);
		break;
	}
}
#else
static inline void __of_changeset_entry_dump(struct of_changeset_entry *ce)
{
	/* empty */
}
#endif

static void __of_changeset_entry_invert(struct of_changeset_entry *ce,
					  struct of_changeset_entry *rce)
{
	memcpy(rce, ce, sizeof(*rce));

	switch (ce->action) {
	case OF_RECONFIG_ATTACH_NODE:
		rce->action = OF_RECONFIG_DETACH_NODE;
		break;
	case OF_RECONFIG_DETACH_NODE:
		rce->action = OF_RECONFIG_ATTACH_NODE;
		break;
	case OF_RECONFIG_ADD_PROPERTY:
		rce->action = OF_RECONFIG_REMOVE_PROPERTY;
		break;
	case OF_RECONFIG_REMOVE_PROPERTY:
		rce->action = OF_RECONFIG_ADD_PROPERTY;
		break;
	case OF_RECONFIG_UPDATE_PROPERTY:
		rce->old_prop = ce->prop;
		rce->prop = ce->old_prop;
		/* update was used but original property did not exist */
		if (!rce->prop) {
			rce->action = OF_RECONFIG_REMOVE_PROPERTY;
			rce->prop = ce->prop;
		}
		break;
	}
}

static int __of_changeset_entry_notify(struct of_changeset_entry *ce,
		bool revert)
{
	struct of_reconfig_data rd;
	struct of_changeset_entry ce_inverted;
	int ret = 0;

	if (revert) {
		__of_changeset_entry_invert(ce, &ce_inverted);
		ce = &ce_inverted;
	}

	switch (ce->action) {
	case OF_RECONFIG_ATTACH_NODE:
	case OF_RECONFIG_DETACH_NODE:
		memset(&rd, 0, sizeof(rd));
		rd.dn = ce->np;
		ret = of_reconfig_notify(ce->action, &rd);
		break;
	case OF_RECONFIG_ADD_PROPERTY:
	case OF_RECONFIG_REMOVE_PROPERTY:
	case OF_RECONFIG_UPDATE_PROPERTY:
		ret = of_property_notify(ce->action, ce->np, ce->prop, ce->old_prop);
		break;
	default:
		pr_err("invalid devicetree changeset action: %i\n",
			(int)ce->action);
		ret = -EINVAL;
	}

	if (ret)
		pr_err("changeset notifier error @%pOF\n", ce->np);
	return ret;
}

static int __of_changeset_entry_apply(struct of_changeset_entry *ce)
{
	struct property *old_prop, **propp;
	unsigned long flags;
	int ret = 0;

	__of_changeset_entry_dump(ce);

	raw_spin_lock_irqsave(&devtree_lock, flags);
	switch (ce->action) {
	case OF_RECONFIG_ATTACH_NODE:
		__of_attach_node(ce->np);
		break;
	case OF_RECONFIG_DETACH_NODE:
		__of_detach_node(ce->np);
		break;
	case OF_RECONFIG_ADD_PROPERTY:
		/* If the property is in deadprops then it must be removed */
		for (propp = &ce->np->deadprops; *propp; propp = &(*propp)->next) {
			if (*propp == ce->prop) {
				*propp = ce->prop->next;
				ce->prop->next = NULL;
				break;
			}
		}

		ret = __of_add_property(ce->np, ce->prop);
		break;
	case OF_RECONFIG_REMOVE_PROPERTY:
		ret = __of_remove_property(ce->np, ce->prop);
		break;

	case OF_RECONFIG_UPDATE_PROPERTY:
		/* If the property is in deadprops then it must be removed */
		for (propp = &ce->np->deadprops; *propp; propp = &(*propp)->next) {
			if (*propp == ce->prop) {
				*propp = ce->prop->next;
				ce->prop->next = NULL;
				break;
			}
		}

		ret = __of_update_property(ce->np, ce->prop, &old_prop);
		break;
	default:
		ret = -EINVAL;
	}
	raw_spin_unlock_irqrestore(&devtree_lock, flags);

	if (ret) {
		pr_err("changeset: apply failed: %-15s %pOF:%s\n",
		       action_names[ce->action], ce->np, ce->prop->name);
		return ret;
	}

	switch (ce->action) {
	case OF_RECONFIG_ATTACH_NODE:
		__of_attach_node_sysfs(ce->np);
		break;
	case OF_RECONFIG_DETACH_NODE:
		__of_detach_node_sysfs(ce->np);
		break;
	case OF_RECONFIG_ADD_PROPERTY:
		/* ignore duplicate names */
		__of_add_property_sysfs(ce->np, ce->prop);
		break;
	case OF_RECONFIG_REMOVE_PROPERTY:
		__of_remove_property_sysfs(ce->np, ce->prop);
		break;
	case OF_RECONFIG_UPDATE_PROPERTY:
		__of_update_property_sysfs(ce->np, ce->prop, ce->old_prop);
		break;
	}

	return 0;
}

static inline int __of_changeset_entry_revert(struct of_changeset_entry *ce)
{
	struct of_changeset_entry ce_inverted;

	__of_changeset_entry_invert(ce, &ce_inverted);
	return __of_changeset_entry_apply(&ce_inverted);
}

/**
 * of_changeset_init - Initialize a changeset for use
 *
 * @ocs:	changeset pointer
 *
 * Initialize a changeset structure
 */
void of_changeset_init(struct of_changeset *ocs)
{
	memset(ocs, 0, sizeof(*ocs));
	INIT_LIST_HEAD(&ocs->entries);
}
EXPORT_SYMBOL_GPL(of_changeset_init);

/**
 * of_changeset_destroy - Destroy a changeset
 *
 * @ocs:	changeset pointer
 *
 * Destroys a changeset. Note that if a changeset is applied,
 * its changes to the tree cannot be reverted.
 */
void of_changeset_destroy(struct of_changeset *ocs)
{
	struct of_changeset_entry *ce, *cen;

	list_for_each_entry_safe_reverse(ce, cen, &ocs->entries, node)
		__of_changeset_entry_destroy(ce);
}
EXPORT_SYMBOL_GPL(of_changeset_destroy);

/*
 * Apply the changeset entries in @ocs.
 * If apply fails, an attempt is made to revert the entries that were
 * successfully applied.
 *
 * If multiple revert errors occur then only the final revert error is reported.
 *
 * Returns 0 on success, a negative error value in case of an error.
 * If a revert error occurs, it is returned in *ret_revert.
 */
int __of_changeset_apply_entries(struct of_changeset *ocs, int *ret_revert)
{
	struct of_changeset_entry *ce;
	int ret, ret_tmp;

	pr_debug("changeset: applying...\n");
	list_for_each_entry(ce, &ocs->entries, node) {
		ret = __of_changeset_entry_apply(ce);
		if (ret) {
			pr_err("Error applying changeset (%d)\n", ret);
			list_for_each_entry_continue_reverse(ce, &ocs->entries,
							     node) {
				ret_tmp = __of_changeset_entry_revert(ce);
				if (ret_tmp)
					*ret_revert = ret_tmp;
			}
			return ret;
		}
	}

	return 0;
}

/*
 * Returns 0 on success, a negative error value in case of an error.
 *
 * If multiple changeset entry notification errors occur then only the
 * final notification error is reported.
 */
int __of_changeset_apply_notify(struct of_changeset *ocs)
{
	struct of_changeset_entry *ce;
	int ret = 0, ret_tmp;

	pr_debug("changeset: emitting notifiers.\n");

	/* drop the global lock while emitting notifiers */
	mutex_unlock(&of_mutex);
	list_for_each_entry(ce, &ocs->entries, node) {
		ret_tmp = __of_changeset_entry_notify(ce, 0);
		if (ret_tmp)
			ret = ret_tmp;
	}
	mutex_lock(&of_mutex);
	pr_debug("changeset: notifiers sent.\n");

	return ret;
}

/*
 * Returns 0 on success, a negative error value in case of an error.
 *
 * If a changeset entry apply fails, an attempt is made to revert any
 * previous entries in the changeset.  If any of the reverts fails,
 * that failure is not reported.  Thus the state of the device tree
 * is unknown if an apply error occurs.
 */
static int __of_changeset_apply(struct of_changeset *ocs)
{
	int ret, ret_revert = 0;

	ret = __of_changeset_apply_entries(ocs, &ret_revert);
	if (!ret)
		ret = __of_changeset_apply_notify(ocs);

	return ret;
}

/**
 * of_changeset_apply - Applies a changeset
 *
 * @ocs:	changeset pointer
 *
 * Applies a changeset to the live tree.
 * Any side-effects of live tree state changes are applied here on
 * success, like creation/destruction of devices and side-effects
 * like creation of sysfs properties and directories.
 *
 * Return: 0 on success, a negative error value in case of an error.
 * On error the partially applied effects are reverted.
 */
int of_changeset_apply(struct of_changeset *ocs)
{
	int ret;

	mutex_lock(&of_mutex);
	ret = __of_changeset_apply(ocs);
	mutex_unlock(&of_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(of_changeset_apply);

/*
 * Revert the changeset entries in @ocs.
 * If revert fails, an attempt is made to re-apply the entries that were
 * successfully removed.
 *
 * If multiple re-apply errors occur then only the final apply error is
 * reported.
 *
 * Returns 0 on success, a negative error value in case of an error.
 * If an apply error occurs, it is returned in *ret_apply.
 */
int __of_changeset_revert_entries(struct of_changeset *ocs, int *ret_apply)
{
	struct of_changeset_entry *ce;
	int ret, ret_tmp;

	pr_debug("changeset: reverting...\n");
	list_for_each_entry_reverse(ce, &ocs->entries, node) {
		ret = __of_changeset_entry_revert(ce);
		if (ret) {
			pr_err("Error reverting changeset (%d)\n", ret);
			list_for_each_entry_continue(ce, &ocs->entries, node) {
				ret_tmp = __of_changeset_entry_apply(ce);
				if (ret_tmp)
					*ret_apply = ret_tmp;
			}
			return ret;
		}
	}

	return 0;
}

/*
 * If multiple changeset entry notification errors occur then only the
 * final notification error is reported.
 */
int __of_changeset_revert_notify(struct of_changeset *ocs)
{
	struct of_changeset_entry *ce;
	int ret = 0, ret_tmp;

	pr_debug("changeset: emitting notifiers.\n");

	/* drop the global lock while emitting notifiers */
	mutex_unlock(&of_mutex);
	list_for_each_entry_reverse(ce, &ocs->entries, node) {
		ret_tmp = __of_changeset_entry_notify(ce, 1);
		if (ret_tmp)
			ret = ret_tmp;
	}
	mutex_lock(&of_mutex);
	pr_debug("changeset: notifiers sent.\n");

	return ret;
}

static int __of_changeset_revert(struct of_changeset *ocs)
{
	int ret, ret_reply;

	ret_reply = 0;
	ret = __of_changeset_revert_entries(ocs, &ret_reply);

	if (!ret)
		ret = __of_changeset_revert_notify(ocs);

	return ret;
}

/**
 * of_changeset_revert - Reverts an applied changeset
 *
 * @ocs:	changeset pointer
 *
 * Reverts a changeset returning the state of the tree to what it
 * was before the application.
 * Any side-effects like creation/destruction of devices and
 * removal of sysfs properties and directories are applied.
 *
 * Return: 0 on success, a negative error value in case of an error.
 */
int of_changeset_revert(struct of_changeset *ocs)
{
	int ret;

	mutex_lock(&of_mutex);
	ret = __of_changeset_revert(ocs);
	mutex_unlock(&of_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(of_changeset_revert);

/**
 * of_changeset_action - Add an action to the tail of the changeset list
 *
 * @ocs:	changeset pointer
 * @action:	action to perform
 * @np:		Pointer to device node
 * @prop:	Pointer to property
 *
 * On action being one of:
 * + OF_RECONFIG_ATTACH_NODE
 * + OF_RECONFIG_DETACH_NODE,
 * + OF_RECONFIG_ADD_PROPERTY
 * + OF_RECONFIG_REMOVE_PROPERTY,
 * + OF_RECONFIG_UPDATE_PROPERTY
 *
 * Return: 0 on success, a negative error value in case of an error.
 */
int of_changeset_action(struct of_changeset *ocs, unsigned long action,
		struct device_node *np, struct property *prop)
{
	struct of_changeset_entry *ce;

	ce = kzalloc(sizeof(*ce), GFP_KERNEL);
	if (!ce)
		return -ENOMEM;

	if (WARN_ON(action >= ARRAY_SIZE(action_names)))
		return -EINVAL;

	/* get a reference to the node */
	ce->action = action;
	ce->np = of_node_get(np);
	ce->prop = prop;

	if (action == OF_RECONFIG_UPDATE_PROPERTY && prop)
		ce->old_prop = of_find_property(np, prop->name, NULL);

	/* add it to the list */
	list_add_tail(&ce->node, &ocs->entries);
	return 0;
}
EXPORT_SYMBOL_GPL(of_changeset_action);
