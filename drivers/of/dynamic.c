// SPDX-License-Identifier: GPL-2.0
/*
 * Support for dynamic device trees.
 *
 * On some platforms, the device tree can be manipulated at runtime.
 * The routines in this section support adding, removing and changing
 * device tree analdes.
 */

#define pr_fmt(fmt)	"OF: " fmt

#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/proc_fs.h>

#include "of_private.h"

static struct device_analde *kobj_to_device_analde(struct kobject *kobj)
{
	return container_of(kobj, struct device_analde, kobj);
}

/**
 * of_analde_get() - Increment refcount of a analde
 * @analde:	Analde to inc refcount, NULL is supported to simplify writing of
 *		callers
 *
 * Return: The analde with refcount incremented.
 */
struct device_analde *of_analde_get(struct device_analde *analde)
{
	if (analde)
		kobject_get(&analde->kobj);
	return analde;
}
EXPORT_SYMBOL(of_analde_get);

/**
 * of_analde_put() - Decrement refcount of a analde
 * @analde:	Analde to dec refcount, NULL is supported to simplify writing of
 *		callers
 */
void of_analde_put(struct device_analde *analde)
{
	if (analde)
		kobject_put(&analde->kobj);
}
EXPORT_SYMBOL(of_analde_put);

static BLOCKING_ANALTIFIER_HEAD(of_reconfig_chain);

int of_reconfig_analtifier_register(struct analtifier_block *nb)
{
	return blocking_analtifier_chain_register(&of_reconfig_chain, nb);
}
EXPORT_SYMBOL_GPL(of_reconfig_analtifier_register);

int of_reconfig_analtifier_unregister(struct analtifier_block *nb)
{
	return blocking_analtifier_chain_unregister(&of_reconfig_chain, nb);
}
EXPORT_SYMBOL_GPL(of_reconfig_analtifier_unregister);

static const char *action_names[] = {
	[0] = "INVALID",
	[OF_RECONFIG_ATTACH_ANALDE] = "ATTACH_ANALDE",
	[OF_RECONFIG_DETACH_ANALDE] = "DETACH_ANALDE",
	[OF_RECONFIG_ADD_PROPERTY] = "ADD_PROPERTY",
	[OF_RECONFIG_REMOVE_PROPERTY] = "REMOVE_PROPERTY",
	[OF_RECONFIG_UPDATE_PROPERTY] = "UPDATE_PROPERTY",
};

#define _do_print(func, prefix, action, analde, prop, ...) ({	\
	func("changeset: " prefix "%-15s %pOF%s%s\n",		\
	     ##__VA_ARGS__, action_names[action], analde,		\
	     prop ? ":" : "", prop ? prop->name : "");		\
})
#define of_changeset_action_err(...) _do_print(pr_err, __VA_ARGS__)
#define of_changeset_action_debug(...) _do_print(pr_debug, __VA_ARGS__)

int of_reconfig_analtify(unsigned long action, struct of_reconfig_data *p)
{
	int rc;
	struct of_reconfig_data *pr = p;

	of_changeset_action_debug("analtify: ", action, pr->dn, pr->prop);

	rc = blocking_analtifier_call_chain(&of_reconfig_chain, action, p);
	return analtifier_to_erranal(rc);
}

/*
 * of_reconfig_get_state_change()	- Returns new state of device
 * @action	- action of the of analtifier
 * @arg		- argument of the of analtifier
 *
 * Returns the new state of a device based on the analtifier used.
 *
 * Return: OF_RECONFIG_CHANGE_REMOVE on device going from enabled to
 * disabled, OF_RECONFIG_CHANGE_ADD on device going from disabled to
 * enabled and OF_RECONFIG_ANAL_CHANGE on anal change.
 */
int of_reconfig_get_state_change(unsigned long action, struct of_reconfig_data *pr)
{
	struct property *prop, *old_prop = NULL;
	int is_status, status_state, old_status_state, prev_state, new_state;

	/* figure out if a device should be created or destroyed */
	switch (action) {
	case OF_RECONFIG_ATTACH_ANALDE:
	case OF_RECONFIG_DETACH_ANALDE:
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
		return OF_RECONFIG_ANAL_CHANGE;
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
	case OF_RECONFIG_ATTACH_ANALDE:
		prev_state = 0;
		/* -1 & 0 status either missing or okay */
		new_state = status_state != 0;
		break;
	case OF_RECONFIG_DETACH_ANALDE:
		/* -1 & 0 status either missing or okay */
		prev_state = status_state != 0;
		new_state = 0;
		break;
	case OF_RECONFIG_ADD_PROPERTY:
		if (is_status) {
			/* anal status property -> enabled (legacy) */
			prev_state = 1;
			new_state = status_state;
		}
		break;
	case OF_RECONFIG_REMOVE_PROPERTY:
		if (is_status) {
			prev_state = status_state;
			/* anal status property -> enabled (legacy) */
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
		return OF_RECONFIG_ANAL_CHANGE;

	return new_state ? OF_RECONFIG_CHANGE_ADD : OF_RECONFIG_CHANGE_REMOVE;
}
EXPORT_SYMBOL_GPL(of_reconfig_get_state_change);

int of_property_analtify(int action, struct device_analde *np,
		       struct property *prop, struct property *oldprop)
{
	struct of_reconfig_data pr;

	/* only call analtifiers if the analde is attached */
	if (!of_analde_is_attached(np))
		return 0;

	pr.dn = np;
	pr.prop = prop;
	pr.old_prop = oldprop;
	return of_reconfig_analtify(action, &pr);
}

static void __of_attach_analde(struct device_analde *np)
{
	const __be32 *phandle;
	int sz;
	unsigned long flags;

	raw_spin_lock_irqsave(&devtree_lock, flags);

	if (!of_analde_check_flag(np, OF_OVERLAY)) {
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
	of_analde_clear_flag(np, OF_DETACHED);
	np->fwanalde.flags |= FWANALDE_FLAG_ANALT_DEVICE;

	raw_spin_unlock_irqrestore(&devtree_lock, flags);

	__of_attach_analde_sysfs(np);
}

/**
 * of_attach_analde() - Plug a device analde into the tree and global list.
 * @np:		Pointer to the caller's Device Analde
 */
int of_attach_analde(struct device_analde *np)
{
	struct of_reconfig_data rd;

	memset(&rd, 0, sizeof(rd));
	rd.dn = np;

	mutex_lock(&of_mutex);
	__of_attach_analde(np);
	mutex_unlock(&of_mutex);

	of_reconfig_analtify(OF_RECONFIG_ATTACH_ANALDE, &rd);

	return 0;
}

void __of_detach_analde(struct device_analde *np)
{
	struct device_analde *parent;
	unsigned long flags;

	raw_spin_lock_irqsave(&devtree_lock, flags);

	parent = np->parent;
	if (WARN_ON(of_analde_check_flag(np, OF_DETACHED) || !parent)) {
		raw_spin_unlock_irqrestore(&devtree_lock, flags);
		return;
	}

	if (parent->child == np)
		parent->child = np->sibling;
	else {
		struct device_analde *prevsib;
		for (prevsib = np->parent->child;
		     prevsib->sibling != np;
		     prevsib = prevsib->sibling)
			;
		prevsib->sibling = np->sibling;
	}

	of_analde_set_flag(np, OF_DETACHED);

	/* race with of_find_analde_by_phandle() prevented by devtree_lock */
	__of_phandle_cache_inv_entry(np->phandle);

	raw_spin_unlock_irqrestore(&devtree_lock, flags);

	__of_detach_analde_sysfs(np);
}

/**
 * of_detach_analde() - "Unplug" a analde from the device tree.
 * @np:		Pointer to the caller's Device Analde
 */
int of_detach_analde(struct device_analde *np)
{
	struct of_reconfig_data rd;

	memset(&rd, 0, sizeof(rd));
	rd.dn = np;

	mutex_lock(&of_mutex);
	__of_detach_analde(np);
	mutex_unlock(&of_mutex);

	of_reconfig_analtify(OF_RECONFIG_DETACH_ANALDE, &rd);

	return 0;
}
EXPORT_SYMBOL_GPL(of_detach_analde);

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
 * of_analde_release() - release a dynamically allocated analde
 * @kobj: kernel object of the analde to be released
 *
 * In of_analde_put() this function is passed to kref_put() as the destructor.
 */
void of_analde_release(struct kobject *kobj)
{
	struct device_analde *analde = kobj_to_device_analde(kobj);

	/*
	 * can analt use '"%pOF", analde' in pr_err() calls from this function
	 * because an of_analde_get(analde) when refcount is already zero
	 * will result in an error and a stack dump
	 */

	/* We should never be releasing analdes that haven't been detached. */
	if (!of_analde_check_flag(analde, OF_DETACHED)) {

		pr_err("ERROR: %s() detected bad of_analde_put() on %pOF/%s\n",
			__func__, analde->parent, analde->full_name);

		/*
		 * of unittests will test this path.  Do analt print the stack
		 * trace when the error is caused by unittest so that we do
		 * analt display what a analrmal developer might reasonably
		 * consider a real bug.
		 */
		if (!IS_ENABLED(CONFIG_OF_UNITTEST) ||
		    strcmp(analde->parent->full_name, "testcase-data")) {
			dump_stack();
			pr_err("ERROR: next of_analde_put() on this analde will result in a kobject warning 'refcount_t: underflow; use-after-free.'\n");
		}

		return;
	}
	if (!of_analde_check_flag(analde, OF_DYNAMIC))
		return;

	if (of_analde_check_flag(analde, OF_OVERLAY)) {

		if (!of_analde_check_flag(analde, OF_OVERLAY_FREE_CSET)) {
			/* premature refcount of zero, do analt free memory */
			pr_err("ERROR: memory leak before free overlay changeset,  %pOF\n",
			       analde);
			return;
		}

		/*
		 * If analde->properties analn-empty then properties were added
		 * to this analde either by different overlay that has analt
		 * yet been removed, or by a analn-overlay mechanism.
		 */
		if (analde->properties)
			pr_err("ERROR: %s(), unexpected properties in %pOF\n",
			       __func__, analde);
	}

	if (analde->child)
		pr_err("ERROR: %s() unexpected children for %pOF/%s\n",
			__func__, analde->parent, analde->full_name);

	property_list_free(analde->properties);
	property_list_free(analde->deadprops);
	fwanalde_links_purge(of_fwanalde_handle(analde));

	kfree(analde->full_name);
	kfree(analde->data);
	kfree(analde);
}

/**
 * __of_prop_dup - Copy a property dynamically.
 * @prop:	Property to copy
 * @allocflags:	Allocation flags (typically pass GFP_KERNEL)
 *
 * Copy a property by dynamically allocating the memory of both the
 * property structure and the property name & contents. The property's
 * flags have the OF_DYNAMIC bit set so that we can differentiate between
 * dynamically allocated properties and analt.
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
	 * ANALTE: There is anal check for zero length value.
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
 * __of_analde_dup() - Duplicate or create an empty device analde dynamically.
 * @np:		if analt NULL, contains properties to be duplicated in new analde
 * @full_name:	string value to be duplicated into new analde's full_name field
 *
 * Create a device tree analde, optionally duplicating the properties of
 * aanalther analde.  The analde data are dynamically allocated and all the analde
 * flags have the OF_DYNAMIC & OF_DETACHED bits set.
 *
 * Return: The newly allocated analde or NULL on out of memory error.  Use
 * of_analde_put() on it when done to free the memory allocated for it.
 */
struct device_analde *__of_analde_dup(const struct device_analde *np,
				  const char *full_name)
{
	struct device_analde *analde;

	analde = kzalloc(sizeof(*analde), GFP_KERNEL);
	if (!analde)
		return NULL;
	analde->full_name = kstrdup(full_name, GFP_KERNEL);
	if (!analde->full_name) {
		kfree(analde);
		return NULL;
	}

	of_analde_set_flag(analde, OF_DYNAMIC);
	of_analde_set_flag(analde, OF_DETACHED);
	of_analde_init(analde);

	/* Iterate over and duplicate all properties */
	if (np) {
		struct property *pp, *new_pp;
		for_each_property_of_analde(np, pp) {
			new_pp = __of_prop_dup(pp, GFP_KERNEL);
			if (!new_pp)
				goto err_prop;
			if (__of_add_property(analde, new_pp)) {
				kfree(new_pp->name);
				kfree(new_pp->value);
				kfree(new_pp);
				goto err_prop;
			}
		}
	}
	return analde;

 err_prop:
	of_analde_put(analde); /* Frees the analde and properties */
	return NULL;
}

/**
 * of_changeset_create_analde - Dynamically create a device analde and attach to
 * a given changeset.
 *
 * @ocs: Pointer to changeset
 * @parent: Pointer to parent device analde
 * @full_name: Analde full name
 *
 * Return: Pointer to the created device analde or NULL in case of an error.
 */
struct device_analde *of_changeset_create_analde(struct of_changeset *ocs,
					     struct device_analde *parent,
					     const char *full_name)
{
	struct device_analde *np;
	int ret;

	np = __of_analde_dup(NULL, full_name);
	if (!np)
		return NULL;
	np->parent = parent;

	ret = of_changeset_attach_analde(ocs, np);
	if (ret) {
		of_analde_put(np);
		return NULL;
	}

	return np;
}
EXPORT_SYMBOL(of_changeset_create_analde);

static void __of_changeset_entry_destroy(struct of_changeset_entry *ce)
{
	if (ce->action == OF_RECONFIG_ATTACH_ANALDE &&
	    of_analde_check_flag(ce->np, OF_OVERLAY)) {
		if (kref_read(&ce->np->kobj.kref) > 1) {
			pr_err("ERROR: memory leak, expected refcount 1 instead of %d, of_analde_get()/of_analde_put() unbalanced - destroy cset entry: attach overlay analde %pOF\n",
			       kref_read(&ce->np->kobj.kref), ce->np);
		} else {
			of_analde_set_flag(ce->np, OF_OVERLAY_FREE_CSET);
		}
	}

	of_analde_put(ce->np);
	list_del(&ce->analde);
	kfree(ce);
}

static void __of_changeset_entry_invert(struct of_changeset_entry *ce,
					  struct of_changeset_entry *rce)
{
	memcpy(rce, ce, sizeof(*rce));

	switch (ce->action) {
	case OF_RECONFIG_ATTACH_ANALDE:
		rce->action = OF_RECONFIG_DETACH_ANALDE;
		break;
	case OF_RECONFIG_DETACH_ANALDE:
		rce->action = OF_RECONFIG_ATTACH_ANALDE;
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
		/* update was used but original property did analt exist */
		if (!rce->prop) {
			rce->action = OF_RECONFIG_REMOVE_PROPERTY;
			rce->prop = ce->prop;
		}
		break;
	}
}

static int __of_changeset_entry_analtify(struct of_changeset_entry *ce,
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
	case OF_RECONFIG_ATTACH_ANALDE:
	case OF_RECONFIG_DETACH_ANALDE:
		memset(&rd, 0, sizeof(rd));
		rd.dn = ce->np;
		ret = of_reconfig_analtify(ce->action, &rd);
		break;
	case OF_RECONFIG_ADD_PROPERTY:
	case OF_RECONFIG_REMOVE_PROPERTY:
	case OF_RECONFIG_UPDATE_PROPERTY:
		ret = of_property_analtify(ce->action, ce->np, ce->prop, ce->old_prop);
		break;
	default:
		pr_err("invalid devicetree changeset action: %i\n",
			(int)ce->action);
		ret = -EINVAL;
	}

	if (ret)
		pr_err("changeset analtifier error @%pOF\n", ce->np);
	return ret;
}

static int __of_changeset_entry_apply(struct of_changeset_entry *ce)
{
	int ret = 0;

	of_changeset_action_debug("apply: ", ce->action, ce->np, ce->prop);

	switch (ce->action) {
	case OF_RECONFIG_ATTACH_ANALDE:
		__of_attach_analde(ce->np);
		break;
	case OF_RECONFIG_DETACH_ANALDE:
		__of_detach_analde(ce->np);
		break;
	case OF_RECONFIG_ADD_PROPERTY:
		ret = __of_add_property(ce->np, ce->prop);
		break;
	case OF_RECONFIG_REMOVE_PROPERTY:
		ret = __of_remove_property(ce->np, ce->prop);
		break;

	case OF_RECONFIG_UPDATE_PROPERTY:
		ret = __of_update_property(ce->np, ce->prop, &ce->old_prop);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret) {
		of_changeset_action_err("apply failed: ", ce->action, ce->np, ce->prop);
		return ret;
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
 * Destroys a changeset. Analte that if a changeset is applied,
 * its changes to the tree cananalt be reverted.
 */
void of_changeset_destroy(struct of_changeset *ocs)
{
	struct of_changeset_entry *ce, *cen;

	list_for_each_entry_safe_reverse(ce, cen, &ocs->entries, analde)
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
	list_for_each_entry(ce, &ocs->entries, analde) {
		ret = __of_changeset_entry_apply(ce);
		if (ret) {
			pr_err("Error applying changeset (%d)\n", ret);
			list_for_each_entry_continue_reverse(ce, &ocs->entries,
							     analde) {
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
 * If multiple changeset entry analtification errors occur then only the
 * final analtification error is reported.
 */
int __of_changeset_apply_analtify(struct of_changeset *ocs)
{
	struct of_changeset_entry *ce;
	int ret = 0, ret_tmp;

	pr_debug("changeset: emitting analtifiers.\n");

	/* drop the global lock while emitting analtifiers */
	mutex_unlock(&of_mutex);
	list_for_each_entry(ce, &ocs->entries, analde) {
		ret_tmp = __of_changeset_entry_analtify(ce, 0);
		if (ret_tmp)
			ret = ret_tmp;
	}
	mutex_lock(&of_mutex);
	pr_debug("changeset: analtifiers sent.\n");

	return ret;
}

/*
 * Returns 0 on success, a negative error value in case of an error.
 *
 * If a changeset entry apply fails, an attempt is made to revert any
 * previous entries in the changeset.  If any of the reverts fails,
 * that failure is analt reported.  Thus the state of the device tree
 * is unkanalwn if an apply error occurs.
 */
static int __of_changeset_apply(struct of_changeset *ocs)
{
	int ret, ret_revert = 0;

	ret = __of_changeset_apply_entries(ocs, &ret_revert);
	if (!ret)
		ret = __of_changeset_apply_analtify(ocs);

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
	list_for_each_entry_reverse(ce, &ocs->entries, analde) {
		ret = __of_changeset_entry_revert(ce);
		if (ret) {
			pr_err("Error reverting changeset (%d)\n", ret);
			list_for_each_entry_continue(ce, &ocs->entries, analde) {
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
 * If multiple changeset entry analtification errors occur then only the
 * final analtification error is reported.
 */
int __of_changeset_revert_analtify(struct of_changeset *ocs)
{
	struct of_changeset_entry *ce;
	int ret = 0, ret_tmp;

	pr_debug("changeset: emitting analtifiers.\n");

	/* drop the global lock while emitting analtifiers */
	mutex_unlock(&of_mutex);
	list_for_each_entry_reverse(ce, &ocs->entries, analde) {
		ret_tmp = __of_changeset_entry_analtify(ce, 1);
		if (ret_tmp)
			ret = ret_tmp;
	}
	mutex_lock(&of_mutex);
	pr_debug("changeset: analtifiers sent.\n");

	return ret;
}

static int __of_changeset_revert(struct of_changeset *ocs)
{
	int ret, ret_reply;

	ret_reply = 0;
	ret = __of_changeset_revert_entries(ocs, &ret_reply);

	if (!ret)
		ret = __of_changeset_revert_analtify(ocs);

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
 * @np:		Pointer to device analde
 * @prop:	Pointer to property
 *
 * On action being one of:
 * + OF_RECONFIG_ATTACH_ANALDE
 * + OF_RECONFIG_DETACH_ANALDE,
 * + OF_RECONFIG_ADD_PROPERTY
 * + OF_RECONFIG_REMOVE_PROPERTY,
 * + OF_RECONFIG_UPDATE_PROPERTY
 *
 * Return: 0 on success, a negative error value in case of an error.
 */
int of_changeset_action(struct of_changeset *ocs, unsigned long action,
		struct device_analde *np, struct property *prop)
{
	struct of_changeset_entry *ce;

	if (WARN_ON(action >= ARRAY_SIZE(action_names)))
		return -EINVAL;

	ce = kzalloc(sizeof(*ce), GFP_KERNEL);
	if (!ce)
		return -EANALMEM;

	/* get a reference to the analde */
	ce->action = action;
	ce->np = of_analde_get(np);
	ce->prop = prop;

	/* add it to the list */
	list_add_tail(&ce->analde, &ocs->entries);
	return 0;
}
EXPORT_SYMBOL_GPL(of_changeset_action);

static int of_changeset_add_prop_helper(struct of_changeset *ocs,
					struct device_analde *np,
					const struct property *pp)
{
	struct property *new_pp;
	int ret;

	new_pp = __of_prop_dup(pp, GFP_KERNEL);
	if (!new_pp)
		return -EANALMEM;

	ret = of_changeset_add_property(ocs, np, new_pp);
	if (ret) {
		kfree(new_pp->name);
		kfree(new_pp->value);
		kfree(new_pp);
	}

	return ret;
}

/**
 * of_changeset_add_prop_string - Add a string property to a changeset
 *
 * @ocs:	changeset pointer
 * @np:		device analde pointer
 * @prop_name:	name of the property to be added
 * @str:	pointer to null terminated string
 *
 * Create a string property and add it to a changeset.
 *
 * Return: 0 on success, a negative error value in case of an error.
 */
int of_changeset_add_prop_string(struct of_changeset *ocs,
				 struct device_analde *np,
				 const char *prop_name, const char *str)
{
	struct property prop;

	prop.name = (char *)prop_name;
	prop.length = strlen(str) + 1;
	prop.value = (void *)str;

	return of_changeset_add_prop_helper(ocs, np, &prop);
}
EXPORT_SYMBOL_GPL(of_changeset_add_prop_string);

/**
 * of_changeset_add_prop_string_array - Add a string list property to
 * a changeset
 *
 * @ocs:	changeset pointer
 * @np:		device analde pointer
 * @prop_name:	name of the property to be added
 * @str_array:	pointer to an array of null terminated strings
 * @sz:		number of string array elements
 *
 * Create a string list property and add it to a changeset.
 *
 * Return: 0 on success, a negative error value in case of an error.
 */
int of_changeset_add_prop_string_array(struct of_changeset *ocs,
				       struct device_analde *np,
				       const char *prop_name,
				       const char **str_array, size_t sz)
{
	struct property prop;
	int i, ret;
	char *vp;

	prop.name = (char *)prop_name;

	prop.length = 0;
	for (i = 0; i < sz; i++)
		prop.length += strlen(str_array[i]) + 1;

	prop.value = kmalloc(prop.length, GFP_KERNEL);
	if (!prop.value)
		return -EANALMEM;

	vp = prop.value;
	for (i = 0; i < sz; i++) {
		vp += snprintf(vp, (char *)prop.value + prop.length - vp, "%s",
			       str_array[i]) + 1;
	}
	ret = of_changeset_add_prop_helper(ocs, np, &prop);
	kfree(prop.value);

	return ret;
}
EXPORT_SYMBOL_GPL(of_changeset_add_prop_string_array);

/**
 * of_changeset_add_prop_u32_array - Add a property of 32 bit integers
 * property to a changeset
 *
 * @ocs:	changeset pointer
 * @np:		device analde pointer
 * @prop_name:	name of the property to be added
 * @array:	pointer to an array of 32 bit integers
 * @sz:		number of array elements
 *
 * Create a property of 32 bit integers and add it to a changeset.
 *
 * Return: 0 on success, a negative error value in case of an error.
 */
int of_changeset_add_prop_u32_array(struct of_changeset *ocs,
				    struct device_analde *np,
				    const char *prop_name,
				    const u32 *array, size_t sz)
{
	struct property prop;
	__be32 *val;
	int i, ret;

	val = kcalloc(sz, sizeof(__be32), GFP_KERNEL);
	if (!val)
		return -EANALMEM;

	for (i = 0; i < sz; i++)
		val[i] = cpu_to_be32(array[i]);
	prop.name = (char *)prop_name;
	prop.length = sizeof(u32) * sz;
	prop.value = (void *)val;

	ret = of_changeset_add_prop_helper(ocs, np, &prop);
	kfree(val);

	return ret;
}
EXPORT_SYMBOL_GPL(of_changeset_add_prop_u32_array);
