/*
 * Copyright (C) 2010-2011 Canonical Ltd <jeremy.kerr@canonical.com>
 * Copyright (C) 2011-2012 Linaro Ltd <mturquette@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Standard functionality for the common clock API.  See Documentation/clk.txt
 */

#include <linux/clk-private.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/slab.h>

static DEFINE_SPINLOCK(enable_lock);
static DEFINE_MUTEX(prepare_lock);

static HLIST_HEAD(clk_root_list);
static HLIST_HEAD(clk_orphan_list);
static LIST_HEAD(clk_notifier_list);

/***        debugfs support        ***/

#ifdef CONFIG_COMMON_CLK_DEBUG
#include <linux/debugfs.h>

static struct dentry *rootdir;
static struct dentry *orphandir;
static int inited = 0;

/* caller must hold prepare_lock */
static int clk_debug_create_one(struct clk *clk, struct dentry *pdentry)
{
	struct dentry *d;
	int ret = -ENOMEM;

	if (!clk || !pdentry) {
		ret = -EINVAL;
		goto out;
	}

	d = debugfs_create_dir(clk->name, pdentry);
	if (!d)
		goto out;

	clk->dentry = d;

	d = debugfs_create_u32("clk_rate", S_IRUGO, clk->dentry,
			(u32 *)&clk->rate);
	if (!d)
		goto err_out;

	d = debugfs_create_x32("clk_flags", S_IRUGO, clk->dentry,
			(u32 *)&clk->flags);
	if (!d)
		goto err_out;

	d = debugfs_create_u32("clk_prepare_count", S_IRUGO, clk->dentry,
			(u32 *)&clk->prepare_count);
	if (!d)
		goto err_out;

	d = debugfs_create_u32("clk_enable_count", S_IRUGO, clk->dentry,
			(u32 *)&clk->enable_count);
	if (!d)
		goto err_out;

	d = debugfs_create_u32("clk_notifier_count", S_IRUGO, clk->dentry,
			(u32 *)&clk->notifier_count);
	if (!d)
		goto err_out;

	ret = 0;
	goto out;

err_out:
	debugfs_remove(clk->dentry);
out:
	return ret;
}

/* caller must hold prepare_lock */
static int clk_debug_create_subtree(struct clk *clk, struct dentry *pdentry)
{
	struct clk *child;
	struct hlist_node *tmp;
	int ret = -EINVAL;;

	if (!clk || !pdentry)
		goto out;

	ret = clk_debug_create_one(clk, pdentry);

	if (ret)
		goto out;

	hlist_for_each_entry(child, tmp, &clk->children, child_node)
		clk_debug_create_subtree(child, clk->dentry);

	ret = 0;
out:
	return ret;
}

/**
 * clk_debug_register - add a clk node to the debugfs clk tree
 * @clk: the clk being added to the debugfs clk tree
 *
 * Dynamically adds a clk to the debugfs clk tree if debugfs has been
 * initialized.  Otherwise it bails out early since the debugfs clk tree
 * will be created lazily by clk_debug_init as part of a late_initcall.
 *
 * Caller must hold prepare_lock.  Only clk_init calls this function (so
 * far) so this is taken care.
 */
static int clk_debug_register(struct clk *clk)
{
	struct clk *parent;
	struct dentry *pdentry;
	int ret = 0;

	if (!inited)
		goto out;

	parent = clk->parent;

	/*
	 * Check to see if a clk is a root clk.  Also check that it is
	 * safe to add this clk to debugfs
	 */
	if (!parent)
		if (clk->flags & CLK_IS_ROOT)
			pdentry = rootdir;
		else
			pdentry = orphandir;
	else
		if (parent->dentry)
			pdentry = parent->dentry;
		else
			goto out;

	ret = clk_debug_create_subtree(clk, pdentry);

out:
	return ret;
}

/**
 * clk_debug_init - lazily create the debugfs clk tree visualization
 *
 * clks are often initialized very early during boot before memory can
 * be dynamically allocated and well before debugfs is setup.
 * clk_debug_init walks the clk tree hierarchy while holding
 * prepare_lock and creates the topology as part of a late_initcall,
 * thus insuring that clks initialized very early will still be
 * represented in the debugfs clk tree.  This function should only be
 * called once at boot-time, and all other clks added dynamically will
 * be done so with clk_debug_register.
 */
static int __init clk_debug_init(void)
{
	struct clk *clk;
	struct hlist_node *tmp;

	rootdir = debugfs_create_dir("clk", NULL);

	if (!rootdir)
		return -ENOMEM;

	orphandir = debugfs_create_dir("orphans", rootdir);

	if (!orphandir)
		return -ENOMEM;

	mutex_lock(&prepare_lock);

	hlist_for_each_entry(clk, tmp, &clk_root_list, child_node)
		clk_debug_create_subtree(clk, rootdir);

	hlist_for_each_entry(clk, tmp, &clk_orphan_list, child_node)
		clk_debug_create_subtree(clk, orphandir);

	inited = 1;

	mutex_unlock(&prepare_lock);

	return 0;
}
late_initcall(clk_debug_init);
#else
static inline int clk_debug_register(struct clk *clk) { return 0; }
#endif

/* caller must hold prepare_lock */
static void clk_disable_unused_subtree(struct clk *clk)
{
	struct clk *child;
	struct hlist_node *tmp;
	unsigned long flags;

	if (!clk)
		goto out;

	hlist_for_each_entry(child, tmp, &clk->children, child_node)
		clk_disable_unused_subtree(child);

	spin_lock_irqsave(&enable_lock, flags);

	if (clk->enable_count)
		goto unlock_out;

	if (clk->flags & CLK_IGNORE_UNUSED)
		goto unlock_out;

	if (__clk_is_enabled(clk) && clk->ops->disable)
		clk->ops->disable(clk->hw);

unlock_out:
	spin_unlock_irqrestore(&enable_lock, flags);

out:
	return;
}

static int clk_disable_unused(void)
{
	struct clk *clk;
	struct hlist_node *tmp;

	mutex_lock(&prepare_lock);

	hlist_for_each_entry(clk, tmp, &clk_root_list, child_node)
		clk_disable_unused_subtree(clk);

	hlist_for_each_entry(clk, tmp, &clk_orphan_list, child_node)
		clk_disable_unused_subtree(clk);

	mutex_unlock(&prepare_lock);

	return 0;
}
late_initcall(clk_disable_unused);

/***    helper functions   ***/

inline const char *__clk_get_name(struct clk *clk)
{
	return !clk ? NULL : clk->name;
}

inline struct clk_hw *__clk_get_hw(struct clk *clk)
{
	return !clk ? NULL : clk->hw;
}

inline u8 __clk_get_num_parents(struct clk *clk)
{
	return !clk ? -EINVAL : clk->num_parents;
}

inline struct clk *__clk_get_parent(struct clk *clk)
{
	return !clk ? NULL : clk->parent;
}

inline int __clk_get_enable_count(struct clk *clk)
{
	return !clk ? -EINVAL : clk->enable_count;
}

inline int __clk_get_prepare_count(struct clk *clk)
{
	return !clk ? -EINVAL : clk->prepare_count;
}

unsigned long __clk_get_rate(struct clk *clk)
{
	unsigned long ret;

	if (!clk) {
		ret = 0;
		goto out;
	}

	ret = clk->rate;

	if (clk->flags & CLK_IS_ROOT)
		goto out;

	if (!clk->parent)
		ret = 0;

out:
	return ret;
}

inline unsigned long __clk_get_flags(struct clk *clk)
{
	return !clk ? -EINVAL : clk->flags;
}

int __clk_is_enabled(struct clk *clk)
{
	int ret;

	if (!clk)
		return -EINVAL;

	/*
	 * .is_enabled is only mandatory for clocks that gate
	 * fall back to software usage counter if .is_enabled is missing
	 */
	if (!clk->ops->is_enabled) {
		ret = clk->enable_count ? 1 : 0;
		goto out;
	}

	ret = clk->ops->is_enabled(clk->hw);
out:
	return ret;
}

static struct clk *__clk_lookup_subtree(const char *name, struct clk *clk)
{
	struct clk *child;
	struct clk *ret;
	struct hlist_node *tmp;

	if (!strcmp(clk->name, name))
		return clk;

	hlist_for_each_entry(child, tmp, &clk->children, child_node) {
		ret = __clk_lookup_subtree(name, child);
		if (ret)
			return ret;
	}

	return NULL;
}

struct clk *__clk_lookup(const char *name)
{
	struct clk *root_clk;
	struct clk *ret;
	struct hlist_node *tmp;

	if (!name)
		return NULL;

	/* search the 'proper' clk tree first */
	hlist_for_each_entry(root_clk, tmp, &clk_root_list, child_node) {
		ret = __clk_lookup_subtree(name, root_clk);
		if (ret)
			return ret;
	}

	/* if not found, then search the orphan tree */
	hlist_for_each_entry(root_clk, tmp, &clk_orphan_list, child_node) {
		ret = __clk_lookup_subtree(name, root_clk);
		if (ret)
			return ret;
	}

	return NULL;
}

/***        clk api        ***/

void __clk_unprepare(struct clk *clk)
{
	if (!clk)
		return;

	if (WARN_ON(clk->prepare_count == 0))
		return;

	if (--clk->prepare_count > 0)
		return;

	WARN_ON(clk->enable_count > 0);

	if (clk->ops->unprepare)
		clk->ops->unprepare(clk->hw);

	__clk_unprepare(clk->parent);
}

/**
 * clk_unprepare - undo preparation of a clock source
 * @clk: the clk being unprepare
 *
 * clk_unprepare may sleep, which differentiates it from clk_disable.  In a
 * simple case, clk_unprepare can be used instead of clk_disable to gate a clk
 * if the operation may sleep.  One example is a clk which is accessed over
 * I2c.  In the complex case a clk gate operation may require a fast and a slow
 * part.  It is this reason that clk_unprepare and clk_disable are not mutually
 * exclusive.  In fact clk_disable must be called before clk_unprepare.
 */
void clk_unprepare(struct clk *clk)
{
	mutex_lock(&prepare_lock);
	__clk_unprepare(clk);
	mutex_unlock(&prepare_lock);
}
EXPORT_SYMBOL_GPL(clk_unprepare);

int __clk_prepare(struct clk *clk)
{
	int ret = 0;

	if (!clk)
		return 0;

	if (clk->prepare_count == 0) {
		ret = __clk_prepare(clk->parent);
		if (ret)
			return ret;

		if (clk->ops->prepare) {
			ret = clk->ops->prepare(clk->hw);
			if (ret) {
				__clk_unprepare(clk->parent);
				return ret;
			}
		}
	}

	clk->prepare_count++;

	return 0;
}

/**
 * clk_prepare - prepare a clock source
 * @clk: the clk being prepared
 *
 * clk_prepare may sleep, which differentiates it from clk_enable.  In a simple
 * case, clk_prepare can be used instead of clk_enable to ungate a clk if the
 * operation may sleep.  One example is a clk which is accessed over I2c.  In
 * the complex case a clk ungate operation may require a fast and a slow part.
 * It is this reason that clk_prepare and clk_enable are not mutually
 * exclusive.  In fact clk_prepare must be called before clk_enable.
 * Returns 0 on success, -EERROR otherwise.
 */
int clk_prepare(struct clk *clk)
{
	int ret;

	mutex_lock(&prepare_lock);
	ret = __clk_prepare(clk);
	mutex_unlock(&prepare_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_prepare);

static void __clk_disable(struct clk *clk)
{
	if (!clk)
		return;

	if (WARN_ON(clk->enable_count == 0))
		return;

	if (--clk->enable_count > 0)
		return;

	if (clk->ops->disable)
		clk->ops->disable(clk->hw);

	__clk_disable(clk->parent);
}

/**
 * clk_disable - gate a clock
 * @clk: the clk being gated
 *
 * clk_disable must not sleep, which differentiates it from clk_unprepare.  In
 * a simple case, clk_disable can be used instead of clk_unprepare to gate a
 * clk if the operation is fast and will never sleep.  One example is a
 * SoC-internal clk which is controlled via simple register writes.  In the
 * complex case a clk gate operation may require a fast and a slow part.  It is
 * this reason that clk_unprepare and clk_disable are not mutually exclusive.
 * In fact clk_disable must be called before clk_unprepare.
 */
void clk_disable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&enable_lock, flags);
	__clk_disable(clk);
	spin_unlock_irqrestore(&enable_lock, flags);
}
EXPORT_SYMBOL_GPL(clk_disable);

static int __clk_enable(struct clk *clk)
{
	int ret = 0;

	if (!clk)
		return 0;

	if (WARN_ON(clk->prepare_count == 0))
		return -ESHUTDOWN;

	if (clk->enable_count == 0) {
		ret = __clk_enable(clk->parent);

		if (ret)
			return ret;

		if (clk->ops->enable) {
			ret = clk->ops->enable(clk->hw);
			if (ret) {
				__clk_disable(clk->parent);
				return ret;
			}
		}
	}

	clk->enable_count++;
	return 0;
}

/**
 * clk_enable - ungate a clock
 * @clk: the clk being ungated
 *
 * clk_enable must not sleep, which differentiates it from clk_prepare.  In a
 * simple case, clk_enable can be used instead of clk_prepare to ungate a clk
 * if the operation will never sleep.  One example is a SoC-internal clk which
 * is controlled via simple register writes.  In the complex case a clk ungate
 * operation may require a fast and a slow part.  It is this reason that
 * clk_enable and clk_prepare are not mutually exclusive.  In fact clk_prepare
 * must be called before clk_enable.  Returns 0 on success, -EERROR
 * otherwise.
 */
int clk_enable(struct clk *clk)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&enable_lock, flags);
	ret = __clk_enable(clk);
	spin_unlock_irqrestore(&enable_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_enable);

/**
 * clk_get_rate - return the rate of clk
 * @clk: the clk whose rate is being returned
 *
 * Simply returns the cached rate of the clk.  Does not query the hardware.  If
 * clk is NULL then returns 0.
 */
unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long rate;

	mutex_lock(&prepare_lock);
	rate = __clk_get_rate(clk);
	mutex_unlock(&prepare_lock);

	return rate;
}
EXPORT_SYMBOL_GPL(clk_get_rate);

/**
 * __clk_round_rate - round the given rate for a clk
 * @clk: round the rate of this clock
 *
 * Caller must hold prepare_lock.  Useful for clk_ops such as .set_rate
 */
unsigned long __clk_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = 0;

	if (!clk)
		return -EINVAL;

	if (!clk->ops->round_rate) {
		if (clk->flags & CLK_SET_RATE_PARENT)
			return __clk_round_rate(clk->parent, rate);
		else
			return clk->rate;
	}

	if (clk->parent)
		parent_rate = clk->parent->rate;

	return clk->ops->round_rate(clk->hw, rate, &parent_rate);
}

/**
 * clk_round_rate - round the given rate for a clk
 * @clk: the clk for which we are rounding a rate
 * @rate: the rate which is to be rounded
 *
 * Takes in a rate as input and rounds it to a rate that the clk can actually
 * use which is then returned.  If clk doesn't support round_rate operation
 * then the parent rate is returned.
 */
long clk_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long ret;

	mutex_lock(&prepare_lock);
	ret = __clk_round_rate(clk, rate);
	mutex_unlock(&prepare_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_round_rate);

/**
 * __clk_notify - call clk notifier chain
 * @clk: struct clk * that is changing rate
 * @msg: clk notifier type (see include/linux/clk.h)
 * @old_rate: old clk rate
 * @new_rate: new clk rate
 *
 * Triggers a notifier call chain on the clk rate-change notification
 * for 'clk'.  Passes a pointer to the struct clk and the previous
 * and current rates to the notifier callback.  Intended to be called by
 * internal clock code only.  Returns NOTIFY_DONE from the last driver
 * called if all went well, or NOTIFY_STOP or NOTIFY_BAD immediately if
 * a driver returns that.
 */
static int __clk_notify(struct clk *clk, unsigned long msg,
		unsigned long old_rate, unsigned long new_rate)
{
	struct clk_notifier *cn;
	struct clk_notifier_data cnd;
	int ret = NOTIFY_DONE;

	cnd.clk = clk;
	cnd.old_rate = old_rate;
	cnd.new_rate = new_rate;

	list_for_each_entry(cn, &clk_notifier_list, node) {
		if (cn->clk == clk) {
			ret = srcu_notifier_call_chain(&cn->notifier_head, msg,
					&cnd);
			break;
		}
	}

	return ret;
}

/**
 * __clk_recalc_rates
 * @clk: first clk in the subtree
 * @msg: notification type (see include/linux/clk.h)
 *
 * Walks the subtree of clks starting with clk and recalculates rates as it
 * goes.  Note that if a clk does not implement the .recalc_rate callback then
 * it is assumed that the clock will take on the rate of it's parent.
 *
 * clk_recalc_rates also propagates the POST_RATE_CHANGE notification,
 * if necessary.
 *
 * Caller must hold prepare_lock.
 */
static void __clk_recalc_rates(struct clk *clk, unsigned long msg)
{
	unsigned long old_rate;
	unsigned long parent_rate = 0;
	struct hlist_node *tmp;
	struct clk *child;

	old_rate = clk->rate;

	if (clk->parent)
		parent_rate = clk->parent->rate;

	if (clk->ops->recalc_rate)
		clk->rate = clk->ops->recalc_rate(clk->hw, parent_rate);
	else
		clk->rate = parent_rate;

	/*
	 * ignore NOTIFY_STOP and NOTIFY_BAD return values for POST_RATE_CHANGE
	 * & ABORT_RATE_CHANGE notifiers
	 */
	if (clk->notifier_count && msg)
		__clk_notify(clk, msg, old_rate, clk->rate);

	hlist_for_each_entry(child, tmp, &clk->children, child_node)
		__clk_recalc_rates(child, msg);
}

/**
 * __clk_speculate_rates
 * @clk: first clk in the subtree
 * @parent_rate: the "future" rate of clk's parent
 *
 * Walks the subtree of clks starting with clk, speculating rates as it
 * goes and firing off PRE_RATE_CHANGE notifications as necessary.
 *
 * Unlike clk_recalc_rates, clk_speculate_rates exists only for sending
 * pre-rate change notifications and returns early if no clks in the
 * subtree have subscribed to the notifications.  Note that if a clk does not
 * implement the .recalc_rate callback then it is assumed that the clock will
 * take on the rate of it's parent.
 *
 * Caller must hold prepare_lock.
 */
static int __clk_speculate_rates(struct clk *clk, unsigned long parent_rate)
{
	struct hlist_node *tmp;
	struct clk *child;
	unsigned long new_rate;
	int ret = NOTIFY_DONE;

	if (clk->ops->recalc_rate)
		new_rate = clk->ops->recalc_rate(clk->hw, parent_rate);
	else
		new_rate = parent_rate;

	/* abort the rate change if a driver returns NOTIFY_BAD */
	if (clk->notifier_count)
		ret = __clk_notify(clk, PRE_RATE_CHANGE, clk->rate, new_rate);

	if (ret == NOTIFY_BAD)
		goto out;

	hlist_for_each_entry(child, tmp, &clk->children, child_node) {
		ret = __clk_speculate_rates(child, new_rate);
		if (ret == NOTIFY_BAD)
			break;
	}

out:
	return ret;
}

static void clk_calc_subtree(struct clk *clk, unsigned long new_rate)
{
	struct clk *child;
	struct hlist_node *tmp;

	clk->new_rate = new_rate;

	hlist_for_each_entry(child, tmp, &clk->children, child_node) {
		if (child->ops->recalc_rate)
			child->new_rate = child->ops->recalc_rate(child->hw, new_rate);
		else
			child->new_rate = new_rate;
		clk_calc_subtree(child, child->new_rate);
	}
}

/*
 * calculate the new rates returning the topmost clock that has to be
 * changed.
 */
static struct clk *clk_calc_new_rates(struct clk *clk, unsigned long rate)
{
	struct clk *top = clk;
	unsigned long best_parent_rate = 0;
	unsigned long new_rate;

	/* sanity */
	if (IS_ERR_OR_NULL(clk))
		return NULL;

	/* save parent rate, if it exists */
	if (clk->parent)
		best_parent_rate = clk->parent->rate;

	/* never propagate up to the parent */
	if (!(clk->flags & CLK_SET_RATE_PARENT)) {
		if (!clk->ops->round_rate) {
			clk->new_rate = clk->rate;
			return NULL;
		}
		new_rate = clk->ops->round_rate(clk->hw, rate, &best_parent_rate);
		goto out;
	}

	/* need clk->parent from here on out */
	if (!clk->parent) {
		pr_debug("%s: %s has NULL parent\n", __func__, clk->name);
		return NULL;
	}

	if (!clk->ops->round_rate) {
		top = clk_calc_new_rates(clk->parent, rate);
		new_rate = clk->parent->new_rate;

		goto out;
	}

	new_rate = clk->ops->round_rate(clk->hw, rate, &best_parent_rate);

	if (best_parent_rate != clk->parent->rate) {
		top = clk_calc_new_rates(clk->parent, best_parent_rate);

		goto out;
	}

out:
	clk_calc_subtree(clk, new_rate);

	return top;
}

/*
 * Notify about rate changes in a subtree. Always walk down the whole tree
 * so that in case of an error we can walk down the whole tree again and
 * abort the change.
 */
static struct clk *clk_propagate_rate_change(struct clk *clk, unsigned long event)
{
	struct hlist_node *tmp;
	struct clk *child, *fail_clk = NULL;
	int ret = NOTIFY_DONE;

	if (clk->rate == clk->new_rate)
		return 0;

	if (clk->notifier_count) {
		ret = __clk_notify(clk, event, clk->rate, clk->new_rate);
		if (ret == NOTIFY_BAD)
			fail_clk = clk;
	}

	hlist_for_each_entry(child, tmp, &clk->children, child_node) {
		clk = clk_propagate_rate_change(child, event);
		if (clk)
			fail_clk = clk;
	}

	return fail_clk;
}

/*
 * walk down a subtree and set the new rates notifying the rate
 * change on the way
 */
static void clk_change_rate(struct clk *clk)
{
	struct clk *child;
	unsigned long old_rate;
	struct hlist_node *tmp;

	old_rate = clk->rate;

	if (clk->ops->set_rate)
		clk->ops->set_rate(clk->hw, clk->new_rate, clk->parent->rate);

	if (clk->ops->recalc_rate)
		clk->rate = clk->ops->recalc_rate(clk->hw,
				clk->parent->rate);
	else
		clk->rate = clk->parent->rate;

	if (clk->notifier_count && old_rate != clk->rate)
		__clk_notify(clk, POST_RATE_CHANGE, old_rate, clk->rate);

	hlist_for_each_entry(child, tmp, &clk->children, child_node)
		clk_change_rate(child);
}

/**
 * clk_set_rate - specify a new rate for clk
 * @clk: the clk whose rate is being changed
 * @rate: the new rate for clk
 *
 * In the simplest case clk_set_rate will only adjust the rate of clk.
 *
 * Setting the CLK_SET_RATE_PARENT flag allows the rate change operation to
 * propagate up to clk's parent; whether or not this happens depends on the
 * outcome of clk's .round_rate implementation.  If *parent_rate is unchanged
 * after calling .round_rate then upstream parent propagation is ignored.  If
 * *parent_rate comes back with a new rate for clk's parent then we propagate
 * up to clk's parent and set it's rate.  Upward propagation will continue
 * until either a clk does not support the CLK_SET_RATE_PARENT flag or
 * .round_rate stops requesting changes to clk's parent_rate.
 *
 * Rate changes are accomplished via tree traversal that also recalculates the
 * rates for the clocks and fires off POST_RATE_CHANGE notifiers.
 *
 * Returns 0 on success, -EERROR otherwise.
 */
int clk_set_rate(struct clk *clk, unsigned long rate)
{
	struct clk *top, *fail_clk;
	int ret = 0;

	/* prevent racing with updates to the clock topology */
	mutex_lock(&prepare_lock);

	/* bail early if nothing to do */
	if (rate == clk->rate)
		goto out;

	if ((clk->flags & CLK_SET_RATE_GATE) && clk->prepare_count) {
		ret = -EBUSY;
		goto out;
	}

	/* calculate new rates and get the topmost changed clock */
	top = clk_calc_new_rates(clk, rate);
	if (!top) {
		ret = -EINVAL;
		goto out;
	}

	/* notify that we are about to change rates */
	fail_clk = clk_propagate_rate_change(top, PRE_RATE_CHANGE);
	if (fail_clk) {
		pr_warn("%s: failed to set %s rate\n", __func__,
				fail_clk->name);
		clk_propagate_rate_change(top, ABORT_RATE_CHANGE);
		ret = -EBUSY;
		goto out;
	}

	/* change the rates */
	clk_change_rate(top);

	mutex_unlock(&prepare_lock);

	return 0;
out:
	mutex_unlock(&prepare_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_set_rate);

/**
 * clk_get_parent - return the parent of a clk
 * @clk: the clk whose parent gets returned
 *
 * Simply returns clk->parent.  Returns NULL if clk is NULL.
 */
struct clk *clk_get_parent(struct clk *clk)
{
	struct clk *parent;

	mutex_lock(&prepare_lock);
	parent = __clk_get_parent(clk);
	mutex_unlock(&prepare_lock);

	return parent;
}
EXPORT_SYMBOL_GPL(clk_get_parent);

/*
 * .get_parent is mandatory for clocks with multiple possible parents.  It is
 * optional for single-parent clocks.  Always call .get_parent if it is
 * available and WARN if it is missing for multi-parent clocks.
 *
 * For single-parent clocks without .get_parent, first check to see if the
 * .parents array exists, and if so use it to avoid an expensive tree
 * traversal.  If .parents does not exist then walk the tree with __clk_lookup.
 */
static struct clk *__clk_init_parent(struct clk *clk)
{
	struct clk *ret = NULL;
	u8 index;

	/* handle the trivial cases */

	if (!clk->num_parents)
		goto out;

	if (clk->num_parents == 1) {
		if (IS_ERR_OR_NULL(clk->parent))
			ret = clk->parent = __clk_lookup(clk->parent_names[0]);
		ret = clk->parent;
		goto out;
	}

	if (!clk->ops->get_parent) {
		WARN(!clk->ops->get_parent,
			"%s: multi-parent clocks must implement .get_parent\n",
			__func__);
		goto out;
	};

	/*
	 * Do our best to cache parent clocks in clk->parents.  This prevents
	 * unnecessary and expensive calls to __clk_lookup.  We don't set
	 * clk->parent here; that is done by the calling function
	 */

	index = clk->ops->get_parent(clk->hw);

	if (!clk->parents)
		clk->parents =
			kmalloc((sizeof(struct clk*) * clk->num_parents),
					GFP_KERNEL);

	if (!clk->parents)
		ret = __clk_lookup(clk->parent_names[index]);
	else if (!clk->parents[index])
		ret = clk->parents[index] =
			__clk_lookup(clk->parent_names[index]);
	else
		ret = clk->parents[index];

out:
	return ret;
}

void __clk_reparent(struct clk *clk, struct clk *new_parent)
{
#ifdef CONFIG_COMMON_CLK_DEBUG
	struct dentry *d;
	struct dentry *new_parent_d;
#endif

	if (!clk || !new_parent)
		return;

	hlist_del(&clk->child_node);

	if (new_parent)
		hlist_add_head(&clk->child_node, &new_parent->children);
	else
		hlist_add_head(&clk->child_node, &clk_orphan_list);

#ifdef CONFIG_COMMON_CLK_DEBUG
	if (!inited)
		goto out;

	if (new_parent)
		new_parent_d = new_parent->dentry;
	else
		new_parent_d = orphandir;

	d = debugfs_rename(clk->dentry->d_parent, clk->dentry,
			new_parent_d, clk->name);
	if (d)
		clk->dentry = d;
	else
		pr_debug("%s: failed to rename debugfs entry for %s\n",
				__func__, clk->name);
out:
#endif

	clk->parent = new_parent;

	__clk_recalc_rates(clk, POST_RATE_CHANGE);
}

static int __clk_set_parent(struct clk *clk, struct clk *parent)
{
	struct clk *old_parent;
	unsigned long flags;
	int ret = -EINVAL;
	u8 i;

	old_parent = clk->parent;

	/* find index of new parent clock using cached parent ptrs */
	for (i = 0; i < clk->num_parents; i++)
		if (clk->parents[i] == parent)
			break;

	/*
	 * find index of new parent clock using string name comparison
	 * also try to cache the parent to avoid future calls to __clk_lookup
	 */
	if (i == clk->num_parents)
		for (i = 0; i < clk->num_parents; i++)
			if (!strcmp(clk->parent_names[i], parent->name)) {
				clk->parents[i] = __clk_lookup(parent->name);
				break;
			}

	if (i == clk->num_parents) {
		pr_debug("%s: clock %s is not a possible parent of clock %s\n",
				__func__, parent->name, clk->name);
		goto out;
	}

	/* migrate prepare and enable */
	if (clk->prepare_count)
		__clk_prepare(parent);

	/* FIXME replace with clk_is_enabled(clk) someday */
	spin_lock_irqsave(&enable_lock, flags);
	if (clk->enable_count)
		__clk_enable(parent);
	spin_unlock_irqrestore(&enable_lock, flags);

	/* change clock input source */
	ret = clk->ops->set_parent(clk->hw, i);

	/* clean up old prepare and enable */
	spin_lock_irqsave(&enable_lock, flags);
	if (clk->enable_count)
		__clk_disable(old_parent);
	spin_unlock_irqrestore(&enable_lock, flags);

	if (clk->prepare_count)
		__clk_unprepare(old_parent);

out:
	return ret;
}

/**
 * clk_set_parent - switch the parent of a mux clk
 * @clk: the mux clk whose input we are switching
 * @parent: the new input to clk
 *
 * Re-parent clk to use parent as it's new input source.  If clk has the
 * CLK_SET_PARENT_GATE flag set then clk must be gated for this
 * operation to succeed.  After successfully changing clk's parent
 * clk_set_parent will update the clk topology, sysfs topology and
 * propagate rate recalculation via __clk_recalc_rates.  Returns 0 on
 * success, -EERROR otherwise.
 */
int clk_set_parent(struct clk *clk, struct clk *parent)
{
	int ret = 0;

	if (!clk || !clk->ops)
		return -EINVAL;

	if (!clk->ops->set_parent)
		return -ENOSYS;

	/* prevent racing with updates to the clock topology */
	mutex_lock(&prepare_lock);

	if (clk->parent == parent)
		goto out;

	/* propagate PRE_RATE_CHANGE notifications */
	if (clk->notifier_count)
		ret = __clk_speculate_rates(clk, parent->rate);

	/* abort if a driver objects */
	if (ret == NOTIFY_STOP)
		goto out;

	/* only re-parent if the clock is not in use */
	if ((clk->flags & CLK_SET_PARENT_GATE) && clk->prepare_count)
		ret = -EBUSY;
	else
		ret = __clk_set_parent(clk, parent);

	/* propagate ABORT_RATE_CHANGE if .set_parent failed */
	if (ret) {
		__clk_recalc_rates(clk, ABORT_RATE_CHANGE);
		goto out;
	}

	/* propagate rate recalculation downstream */
	__clk_reparent(clk, parent);

out:
	mutex_unlock(&prepare_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_set_parent);

/**
 * __clk_init - initialize the data structures in a struct clk
 * @dev:	device initializing this clk, placeholder for now
 * @clk:	clk being initialized
 *
 * Initializes the lists in struct clk, queries the hardware for the
 * parent and rate and sets them both.
 */
int __clk_init(struct device *dev, struct clk *clk)
{
	int i, ret = 0;
	struct clk *orphan;
	struct hlist_node *tmp, *tmp2;

	if (!clk)
		return -EINVAL;

	mutex_lock(&prepare_lock);

	/* check to see if a clock with this name is already registered */
	if (__clk_lookup(clk->name)) {
		pr_debug("%s: clk %s already initialized\n",
				__func__, clk->name);
		ret = -EEXIST;
		goto out;
	}

	/* check that clk_ops are sane.  See Documentation/clk.txt */
	if (clk->ops->set_rate &&
			!(clk->ops->round_rate && clk->ops->recalc_rate)) {
		pr_warning("%s: %s must implement .round_rate & .recalc_rate\n",
				__func__, clk->name);
		ret = -EINVAL;
		goto out;
	}

	if (clk->ops->set_parent && !clk->ops->get_parent) {
		pr_warning("%s: %s must implement .get_parent & .set_parent\n",
				__func__, clk->name);
		ret = -EINVAL;
		goto out;
	}

	/* throw a WARN if any entries in parent_names are NULL */
	for (i = 0; i < clk->num_parents; i++)
		WARN(!clk->parent_names[i],
				"%s: invalid NULL in %s's .parent_names\n",
				__func__, clk->name);

	/*
	 * Allocate an array of struct clk *'s to avoid unnecessary string
	 * look-ups of clk's possible parents.  This can fail for clocks passed
	 * in to clk_init during early boot; thus any access to clk->parents[]
	 * must always check for a NULL pointer and try to populate it if
	 * necessary.
	 *
	 * If clk->parents is not NULL we skip this entire block.  This allows
	 * for clock drivers to statically initialize clk->parents.
	 */
	if (clk->num_parents && !clk->parents) {
		clk->parents = kmalloc((sizeof(struct clk*) * clk->num_parents),
				GFP_KERNEL);
		/*
		 * __clk_lookup returns NULL for parents that have not been
		 * clk_init'd; thus any access to clk->parents[] must check
		 * for a NULL pointer.  We can always perform lazy lookups for
		 * missing parents later on.
		 */
		if (clk->parents)
			for (i = 0; i < clk->num_parents; i++)
				clk->parents[i] =
					__clk_lookup(clk->parent_names[i]);
	}

	clk->parent = __clk_init_parent(clk);

	/*
	 * Populate clk->parent if parent has already been __clk_init'd.  If
	 * parent has not yet been __clk_init'd then place clk in the orphan
	 * list.  If clk has set the CLK_IS_ROOT flag then place it in the root
	 * clk list.
	 *
	 * Every time a new clk is clk_init'd then we walk the list of orphan
	 * clocks and re-parent any that are children of the clock currently
	 * being clk_init'd.
	 */
	if (clk->parent)
		hlist_add_head(&clk->child_node,
				&clk->parent->children);
	else if (clk->flags & CLK_IS_ROOT)
		hlist_add_head(&clk->child_node, &clk_root_list);
	else
		hlist_add_head(&clk->child_node, &clk_orphan_list);

	/*
	 * Set clk's rate.  The preferred method is to use .recalc_rate.  For
	 * simple clocks and lazy developers the default fallback is to use the
	 * parent's rate.  If a clock doesn't have a parent (or is orphaned)
	 * then rate is set to zero.
	 */
	if (clk->ops->recalc_rate)
		clk->rate = clk->ops->recalc_rate(clk->hw,
				__clk_get_rate(clk->parent));
	else if (clk->parent)
		clk->rate = clk->parent->rate;
	else
		clk->rate = 0;

	/*
	 * walk the list of orphan clocks and reparent any that are children of
	 * this clock
	 */
	hlist_for_each_entry_safe(orphan, tmp, tmp2, &clk_orphan_list, child_node)
		for (i = 0; i < orphan->num_parents; i++)
			if (!strcmp(clk->name, orphan->parent_names[i])) {
				__clk_reparent(orphan, clk);
				break;
			}

	/*
	 * optional platform-specific magic
	 *
	 * The .init callback is not used by any of the basic clock types, but
	 * exists for weird hardware that must perform initialization magic.
	 * Please consider other ways of solving initialization problems before
	 * using this callback, as it's use is discouraged.
	 */
	if (clk->ops->init)
		clk->ops->init(clk->hw);

	clk_debug_register(clk);

out:
	mutex_unlock(&prepare_lock);

	return ret;
}

/**
 * __clk_register - register a clock and return a cookie.
 *
 * Same as clk_register, except that the .clk field inside hw shall point to a
 * preallocated (generally statically allocated) struct clk. None of the fields
 * of the struct clk need to be initialized.
 *
 * The data pointed to by .init and .clk field shall NOT be marked as init
 * data.
 *
 * __clk_register is only exposed via clk-private.h and is intended for use with
 * very large numbers of clocks that need to be statically initialized.  It is
 * a layering violation to include clk-private.h from any code which implements
 * a clock's .ops; as such any statically initialized clock data MUST be in a
 * separate C file from the logic that implements it's operations.  Returns 0
 * on success, otherwise an error code.
 */
struct clk *__clk_register(struct device *dev, struct clk_hw *hw)
{
	int ret;
	struct clk *clk;

	clk = hw->clk;
	clk->name = hw->init->name;
	clk->ops = hw->init->ops;
	clk->hw = hw;
	clk->flags = hw->init->flags;
	clk->parent_names = hw->init->parent_names;
	clk->num_parents = hw->init->num_parents;

	ret = __clk_init(dev, clk);
	if (ret)
		return ERR_PTR(ret);

	return clk;
}
EXPORT_SYMBOL_GPL(__clk_register);

/**
 * clk_register - allocate a new clock, register it and return an opaque cookie
 * @dev: device that is registering this clock
 * @hw: link to hardware-specific clock data
 *
 * clk_register is the primary interface for populating the clock tree with new
 * clock nodes.  It returns a pointer to the newly allocated struct clk which
 * cannot be dereferenced by driver code but may be used in conjuction with the
 * rest of the clock API.  In the event of an error clk_register will return an
 * error code; drivers must test for an error code after calling clk_register.
 */
struct clk *clk_register(struct device *dev, struct clk_hw *hw)
{
	int i, ret;
	struct clk *clk;

	clk = kzalloc(sizeof(*clk), GFP_KERNEL);
	if (!clk) {
		pr_err("%s: could not allocate clk\n", __func__);
		ret = -ENOMEM;
		goto fail_out;
	}

	clk->name = kstrdup(hw->init->name, GFP_KERNEL);
	if (!clk->name) {
		pr_err("%s: could not allocate clk->name\n", __func__);
		ret = -ENOMEM;
		goto fail_name;
	}
	clk->ops = hw->init->ops;
	clk->hw = hw;
	clk->flags = hw->init->flags;
	clk->num_parents = hw->init->num_parents;
	hw->clk = clk;

	/* allocate local copy in case parent_names is __initdata */
	clk->parent_names = kzalloc((sizeof(char*) * clk->num_parents),
			GFP_KERNEL);

	if (!clk->parent_names) {
		pr_err("%s: could not allocate clk->parent_names\n", __func__);
		ret = -ENOMEM;
		goto fail_parent_names;
	}


	/* copy each string name in case parent_names is __initdata */
	for (i = 0; i < clk->num_parents; i++) {
		clk->parent_names[i] = kstrdup(hw->init->parent_names[i],
						GFP_KERNEL);
		if (!clk->parent_names[i]) {
			pr_err("%s: could not copy parent_names\n", __func__);
			ret = -ENOMEM;
			goto fail_parent_names_copy;
		}
	}

	ret = __clk_init(dev, clk);
	if (!ret)
		return clk;

fail_parent_names_copy:
	while (--i >= 0)
		kfree(clk->parent_names[i]);
	kfree(clk->parent_names);
fail_parent_names:
	kfree(clk->name);
fail_name:
	kfree(clk);
fail_out:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(clk_register);

/**
 * clk_unregister - unregister a currently registered clock
 * @clk: clock to unregister
 *
 * Currently unimplemented.
 */
void clk_unregister(struct clk *clk) {}
EXPORT_SYMBOL_GPL(clk_unregister);

/***        clk rate change notifiers        ***/

/**
 * clk_notifier_register - add a clk rate change notifier
 * @clk: struct clk * to watch
 * @nb: struct notifier_block * with callback info
 *
 * Request notification when clk's rate changes.  This uses an SRCU
 * notifier because we want it to block and notifier unregistrations are
 * uncommon.  The callbacks associated with the notifier must not
 * re-enter into the clk framework by calling any top-level clk APIs;
 * this will cause a nested prepare_lock mutex.
 *
 * Pre-change notifier callbacks will be passed the current, pre-change
 * rate of the clk via struct clk_notifier_data.old_rate.  The new,
 * post-change rate of the clk is passed via struct
 * clk_notifier_data.new_rate.
 *
 * Post-change notifiers will pass the now-current, post-change rate of
 * the clk in both struct clk_notifier_data.old_rate and struct
 * clk_notifier_data.new_rate.
 *
 * Abort-change notifiers are effectively the opposite of pre-change
 * notifiers: the original pre-change clk rate is passed in via struct
 * clk_notifier_data.new_rate and the failed post-change rate is passed
 * in via struct clk_notifier_data.old_rate.
 *
 * clk_notifier_register() must be called from non-atomic context.
 * Returns -EINVAL if called with null arguments, -ENOMEM upon
 * allocation failure; otherwise, passes along the return value of
 * srcu_notifier_chain_register().
 */
int clk_notifier_register(struct clk *clk, struct notifier_block *nb)
{
	struct clk_notifier *cn;
	int ret = -ENOMEM;

	if (!clk || !nb)
		return -EINVAL;

	mutex_lock(&prepare_lock);

	/* search the list of notifiers for this clk */
	list_for_each_entry(cn, &clk_notifier_list, node)
		if (cn->clk == clk)
			break;

	/* if clk wasn't in the notifier list, allocate new clk_notifier */
	if (cn->clk != clk) {
		cn = kzalloc(sizeof(struct clk_notifier), GFP_KERNEL);
		if (!cn)
			goto out;

		cn->clk = clk;
		srcu_init_notifier_head(&cn->notifier_head);

		list_add(&cn->node, &clk_notifier_list);
	}

	ret = srcu_notifier_chain_register(&cn->notifier_head, nb);

	clk->notifier_count++;

out:
	mutex_unlock(&prepare_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_notifier_register);

/**
 * clk_notifier_unregister - remove a clk rate change notifier
 * @clk: struct clk *
 * @nb: struct notifier_block * with callback info
 *
 * Request no further notification for changes to 'clk' and frees memory
 * allocated in clk_notifier_register.
 *
 * Returns -EINVAL if called with null arguments; otherwise, passes
 * along the return value of srcu_notifier_chain_unregister().
 */
int clk_notifier_unregister(struct clk *clk, struct notifier_block *nb)
{
	struct clk_notifier *cn = NULL;
	int ret = -EINVAL;

	if (!clk || !nb)
		return -EINVAL;

	mutex_lock(&prepare_lock);

	list_for_each_entry(cn, &clk_notifier_list, node)
		if (cn->clk == clk)
			break;

	if (cn->clk == clk) {
		ret = srcu_notifier_chain_unregister(&cn->notifier_head, nb);

		clk->notifier_count--;

		/* XXX the notifier code should handle this better */
		if (!cn->notifier_head.head) {
			srcu_cleanup_notifier_head(&cn->notifier_head);
			kfree(cn);
		}

	} else {
		ret = -ENOENT;
	}

	mutex_unlock(&prepare_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(clk_notifier_unregister);
