/*
 * Support for dynamic device trees.
 *
 * On some platforms, the device tree can be manipulated at runtime.
 * The routines in this section support adding, removing and changing
 * device tree nodes.
 */

#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/proc_fs.h>

#include "of_private.h"

/**
 * of_node_get() - Increment refcount of a node
 * @node:	Node to inc refcount, NULL is supported to simplify writing of
 *		callers
 *
 * Returns node.
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

void __of_detach_node_sysfs(struct device_node *np)
{
	struct property *pp;

	if (!IS_ENABLED(CONFIG_SYSFS))
		return;

	BUG_ON(!of_node_is_initialized(np));
	if (!of_kset)
		return;

	/* only remove properties if on sysfs */
	if (of_node_is_attached(np)) {
		for_each_property_of_node(np, pp)
			sysfs_remove_bin_file(&np->kobj, &pp->attr);
		kobject_del(&np->kobj);
	}

	/* finally remove the kobj_init ref */
	of_node_put(np);
}

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

int of_reconfig_notify(unsigned long action, void *p)
{
	int rc;

	rc = blocking_notifier_call_chain(&of_reconfig_chain, action, p);
	return notifier_to_errno(rc);
}

int of_property_notify(int action, struct device_node *np,
		       struct property *prop, struct property *oldprop)
{
	struct of_prop_reconfig pr;

	/* only call notifiers if the node is attached */
	if (!of_node_is_attached(np))
		return 0;

	pr.dn = np;
	pr.prop = prop;
	pr.old_prop = oldprop;
	return of_reconfig_notify(action, &pr);
}

void __of_attach_node(struct device_node *np)
{
	const __be32 *phandle;
	int sz;

	np->name = __of_get_property(np, "name", NULL) ? : "<NULL>";
	np->type = __of_get_property(np, "device_type", NULL) ? : "<NULL>";

	phandle = __of_get_property(np, "phandle", &sz);
	if (!phandle)
		phandle = __of_get_property(np, "linux,phandle", &sz);
	if (IS_ENABLED(PPC_PSERIES) && !phandle)
		phandle = __of_get_property(np, "ibm,phandle", &sz);
	np->phandle = (phandle && (sz >= 4)) ? be32_to_cpup(phandle) : 0;

	np->child = NULL;
	np->sibling = np->parent->child;
	np->allnext = np->parent->allnext;
	np->parent->allnext = np;
	np->parent->child = np;
	of_node_clear_flag(np, OF_DETACHED);
}

/**
 * of_attach_node() - Plug a device node into the tree and global list.
 */
int of_attach_node(struct device_node *np)
{
	unsigned long flags;

	mutex_lock(&of_mutex);
	raw_spin_lock_irqsave(&devtree_lock, flags);
	__of_attach_node(np);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);

	__of_attach_node_sysfs(np);
	mutex_unlock(&of_mutex);

	of_reconfig_notify(OF_RECONFIG_ATTACH_NODE, np);

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

	if (of_allnodes == np)
		of_allnodes = np->allnext;
	else {
		struct device_node *prev;
		for (prev = of_allnodes;
		     prev->allnext != np;
		     prev = prev->allnext)
			;
		prev->allnext = np->allnext;
	}

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
}

/**
 * of_detach_node() - "Unplug" a node from the device tree.
 *
 * The caller must hold a reference to the node.  The memory associated with
 * the node is not freed until its refcount goes to zero.
 */
int of_detach_node(struct device_node *np)
{
	unsigned long flags;
	int rc = 0;

	mutex_lock(&of_mutex);
	raw_spin_lock_irqsave(&devtree_lock, flags);
	__of_detach_node(np);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);

	__of_detach_node_sysfs(np);
	mutex_unlock(&of_mutex);

	of_reconfig_notify(OF_RECONFIG_DETACH_NODE, np);

	return rc;
}

/**
 * of_node_release() - release a dynamically allocated node
 * @kref: kref element of the node to be released
 *
 * In of_node_put() this function is passed to kref_put() as the destructor.
 */
void of_node_release(struct kobject *kobj)
{
	struct device_node *node = kobj_to_device_node(kobj);
	struct property *prop = node->properties;

	/* We should never be releasing nodes that haven't been detached. */
	if (!of_node_check_flag(node, OF_DETACHED)) {
		pr_err("ERROR: Bad of_node_put() on %s\n", node->full_name);
		dump_stack();
		return;
	}

	if (!of_node_check_flag(node, OF_DYNAMIC))
		return;

	while (prop) {
		struct property *next = prop->next;
		kfree(prop->name);
		kfree(prop->value);
		kfree(prop);
		prop = next;

		if (!prop) {
			prop = node->deadprops;
			node->deadprops = NULL;
		}
	}
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
 * Returns the newly allocated property or NULL on out of memory error.
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
 * __of_node_alloc() - Create an empty device node dynamically.
 * @full_name:	Full name of the new device node
 * @allocflags:	Allocation flags (typically pass GFP_KERNEL)
 *
 * Create an empty device tree node, suitable for further modification.
 * The node data are dynamically allocated and all the node flags
 * have the OF_DYNAMIC & OF_DETACHED bits set.
 * Returns the newly allocated node or NULL on out of memory error.
 */
struct device_node *__of_node_alloc(const char *full_name, gfp_t allocflags)
{
	struct device_node *node;

	node = kzalloc(sizeof(*node), allocflags);
	if (!node)
		return NULL;

	node->full_name = kstrdup(full_name, allocflags);
	of_node_set_flag(node, OF_DYNAMIC);
	of_node_set_flag(node, OF_DETACHED);
	if (!node->full_name)
		goto err_free;

	of_node_init(node);

	return node;

 err_free:
	kfree(node->full_name);
	kfree(node);
	return NULL;
}

static void __of_changeset_entry_destroy(struct of_changeset_entry *ce)
{
	of_node_put(ce->np);
	list_del(&ce->node);
	kfree(ce);
}

#ifdef DEBUG
static void __of_changeset_entry_dump(struct of_changeset_entry *ce)
{
	switch (ce->action) {
	case OF_RECONFIG_ADD_PROPERTY:
		pr_debug("%p: %s %s/%s\n",
			ce, "ADD_PROPERTY   ", ce->np->full_name,
			ce->prop->name);
		break;
	case OF_RECONFIG_REMOVE_PROPERTY:
		pr_debug("%p: %s %s/%s\n",
			ce, "REMOVE_PROPERTY", ce->np->full_name,
			ce->prop->name);
		break;
	case OF_RECONFIG_UPDATE_PROPERTY:
		pr_debug("%p: %s %s/%s\n",
			ce, "UPDATE_PROPERTY", ce->np->full_name,
			ce->prop->name);
		break;
	case OF_RECONFIG_ATTACH_NODE:
		pr_debug("%p: %s %s\n",
			ce, "ATTACH_NODE    ", ce->np->full_name);
		break;
	case OF_RECONFIG_DETACH_NODE:
		pr_debug("%p: %s %s\n",
			ce, "DETACH_NODE    ", ce->np->full_name);
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
		break;
	}
}

static void __of_changeset_entry_notify(struct of_changeset_entry *ce, bool revert)
{
	struct of_changeset_entry ce_inverted;
	int ret;

	if (revert) {
		__of_changeset_entry_invert(ce, &ce_inverted);
		ce = &ce_inverted;
	}

	switch (ce->action) {
	case OF_RECONFIG_ATTACH_NODE:
	case OF_RECONFIG_DETACH_NODE:
		ret = of_reconfig_notify(ce->action, ce->np);
		break;
	case OF_RECONFIG_ADD_PROPERTY:
	case OF_RECONFIG_REMOVE_PROPERTY:
	case OF_RECONFIG_UPDATE_PROPERTY:
		ret = of_property_notify(ce->action, ce->np, ce->prop, ce->old_prop);
		break;
	default:
		pr_err("%s: invalid devicetree changeset action: %i\n", __func__,
			(int)ce->action);
		return;
	}

	if (ret)
		pr_err("%s: notifier error @%s\n", __func__, ce->np->full_name);
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
		if (ret) {
			pr_err("%s: add_property failed @%s/%s\n",
				__func__, ce->np->full_name,
				ce->prop->name);
			break;
		}
		break;
	case OF_RECONFIG_REMOVE_PROPERTY:
		ret = __of_remove_property(ce->np, ce->prop);
		if (ret) {
			pr_err("%s: remove_property failed @%s/%s\n",
				__func__, ce->np->full_name,
				ce->prop->name);
			break;
		}
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
		if (ret) {
			pr_err("%s: update_property failed @%s/%s\n",
				__func__, ce->np->full_name,
				ce->prop->name);
			break;
		}
		break;
	default:
		ret = -EINVAL;
	}
	raw_spin_unlock_irqrestore(&devtree_lock, flags);

	if (ret)
		return ret;

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

/**
 * of_changeset_apply - Applies a changeset
 *
 * @ocs:	changeset pointer
 *
 * Applies a changeset to the live tree.
 * Any side-effects of live tree state changes are applied here on
 * sucess, like creation/destruction of devices and side-effects
 * like creation of sysfs properties and directories.
 * Returns 0 on success, a negative error value in case of an error.
 * On error the partially applied effects are reverted.
 */
int of_changeset_apply(struct of_changeset *ocs)
{
	struct of_changeset_entry *ce;
	int ret;

	/* perform the rest of the work */
	pr_debug("of_changeset: applying...\n");
	list_for_each_entry(ce, &ocs->entries, node) {
		ret = __of_changeset_entry_apply(ce);
		if (ret) {
			pr_err("%s: Error applying changeset (%d)\n", __func__, ret);
			list_for_each_entry_continue_reverse(ce, &ocs->entries, node)
				__of_changeset_entry_revert(ce);
			return ret;
		}
	}
	pr_debug("of_changeset: applied, emitting notifiers.\n");

	/* drop the global lock while emitting notifiers */
	mutex_unlock(&of_mutex);
	list_for_each_entry(ce, &ocs->entries, node)
		__of_changeset_entry_notify(ce, 0);
	mutex_lock(&of_mutex);
	pr_debug("of_changeset: notifiers sent.\n");

	return 0;
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
 * Returns 0 on success, a negative error value in case of an error.
 */
int of_changeset_revert(struct of_changeset *ocs)
{
	struct of_changeset_entry *ce;
	int ret;

	pr_debug("of_changeset: reverting...\n");
	list_for_each_entry_reverse(ce, &ocs->entries, node) {
		ret = __of_changeset_entry_revert(ce);
		if (ret) {
			pr_err("%s: Error reverting changeset (%d)\n", __func__, ret);
			list_for_each_entry_continue(ce, &ocs->entries, node)
				__of_changeset_entry_apply(ce);
			return ret;
		}
	}
	pr_debug("of_changeset: reverted, emitting notifiers.\n");

	/* drop the global lock while emitting notifiers */
	mutex_unlock(&of_mutex);
	list_for_each_entry_reverse(ce, &ocs->entries, node)
		__of_changeset_entry_notify(ce, 1);
	mutex_lock(&of_mutex);
	pr_debug("of_changeset: notifiers sent.\n");

	return 0;
}

/**
 * of_changeset_action - Perform a changeset action
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
 * Returns 0 on success, a negative error value in case of an error.
 */
int of_changeset_action(struct of_changeset *ocs, unsigned long action,
		struct device_node *np, struct property *prop)
{
	struct of_changeset_entry *ce;

	ce = kzalloc(sizeof(*ce), GFP_KERNEL);
	if (!ce) {
		pr_err("%s: Failed to allocate\n", __func__);
		return -ENOMEM;
	}
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
