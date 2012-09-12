/* linux/arch/arm/mach-rk30/clock.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/delay.h>
#include <mach/clock.h>
#include <mach/dvfs.h>
#include <linux/delay.h>

#define CLOCK_PRINTK_DBG(fmt, args...) pr_debug(fmt, ## args);
#define CLOCK_PRINTK_ERR(fmt, args...) pr_err(fmt, ## args);
#define CLOCK_PRINTK_LOG(fmt, args...) pr_debug(fmt, ## args);

/* Clock flags */
/* bit 0 is free */
#define RATE_FIXED		(1 << 1)	/* Fixed clock rate */
#define CONFIG_PARTICIPANT	(1 << 10)	/* Fundamental clock */

#define MHZ			(1000*1000)
#define KHZ			(1000)

static void __clk_recalc(struct clk *clk);
static void __propagate_rate(struct clk *tclk);
static void __clk_reparent(struct clk *child, struct clk *parent);

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clockfw_lock);
static LIST_HEAD(root_clks);
static void clk_notify(struct clk *clk, unsigned long msg,
		       unsigned long old_rate, unsigned long new_rate);

#define LOCK() do { WARN_ON(in_irq()); if (!irqs_disabled()) spin_lock_bh(&clockfw_lock); } while (0)
#define UNLOCK() do { if (!irqs_disabled()) spin_unlock_bh(&clockfw_lock); } while (0)
/**********************************************for clock data****************************************************/
static struct clk *def_ops_clk=NULL;

void clk_register_default_ops_clk(struct clk *clk)
{
	def_ops_clk=clk;
}

static struct clk *clk_default_get_parent(struct clk *clk)
{
	if(def_ops_clk&&def_ops_clk->get_parent)
		return def_ops_clk->get_parent(clk);
	else return NULL;



}
static int clk_default_set_parent(struct clk *clk, struct clk *parent)
{
	if(def_ops_clk&&def_ops_clk->set_parent)
		return def_ops_clk->set_parent(clk,parent);
	else
		return -EINVAL;
}

int __init clk_disable_unused(void)
{
	struct clk *ck;
	list_for_each_entry(ck, &clocks, node) {
	if (ck->usecount > 0 || ck->mode == NULL || (ck->flags & IS_PD))
		continue;
		LOCK();
		clk_enable_nolock(ck);
		clk_disable_nolock(ck);
		UNLOCK();
	}
	return 0;
}
/**
 * recalculate_root_clocks - recalculate and propagate all root clocks
 *
 * Recalculates all root clocks (clocks with no parent), which if the
 * clock's .recalc is set correctly, should also propagate their rates.
 * Called at init.
 */
void clk_recalculate_root_clocks_nolock(void)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &root_clks, sibling) {
		__clk_recalc(clkp);
		__propagate_rate(clkp);
	}
}
/*
void clk_recalculate_root_clocks(void)
{
	LOCK();
	clk_recalculate_root_clocks_nolock();
	UNLOCK();
}*/
	
/**
 * clk_preinit - initialize any fields in the struct clk before clk init
 * @clk: struct clk * to initialize
 *
 * Initialize any struct clk fields needed before normal clk initialization
 * can run.  No return value.
 */
int clk_register(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;
	//INIT_LIST_HEAD(&clk->sibling);
	INIT_LIST_HEAD(&clk->children);

	/*
	 * trap out already registered clocks
	 */
	if (clk->node.next || clk->node.prev)
		return 0;

	mutex_lock(&clocks_mutex);
	if (clk->get_parent)
		clk->parent = clk->get_parent(clk);
	else if (clk->parents)
		clk->parent =clk_default_get_parent(clk);
	
	if (clk->parent)
		list_add(&clk->sibling, &clk->parent->children);
	else
		list_add(&clk->sibling, &root_clks);
	list_add(&clk->node, &clocks);
	mutex_unlock(&clocks_mutex);	
	return 0;
}

/************************************************************/
static void __clk_recalc(struct clk *clk)
{
	if (unlikely(clk->flags & RATE_FIXED))
		return;
	if (clk->recalc)
		clk->rate = clk->recalc(clk);
	else if (clk->parent)
		clk->rate = clk->parent->rate;
}
static void __clk_reparent(struct clk *child, struct clk *parent)
{
	if (child->parent == parent)
		return;
	//CLOCK_PRINTK_DBG("%s reparent to %s (was %s)\n", child->name, parent->name, ((child->parent) ? child->parent->name : "NULL"));

	list_del_init(&child->sibling);
	if (parent)
		list_add(&child->sibling, &parent->children);
	child->parent = parent;
}

/* Propagate rate to children */
static void __propagate_rate(struct clk *tclk)
{
	struct clk *clkp;
	
	//CLOCK_PRINTK_DBG("propagate_rate clk %s\n",clkp->name);
	
	list_for_each_entry(clkp, &tclk->children, sibling) {
		__clk_recalc(clkp);
		__propagate_rate(clkp);
	}
	//CLOCK_PRINTK_DBG("propagate_rate clk %s end\n",clkp->name);
}

int clk_enable_nolock(struct clk *clk)
{
	int ret = 0;

	if (clk->usecount == 0) {
		if (clk->parent) {
			ret = clk_enable_nolock(clk->parent);
			if (ret)
				return ret;
		}

		if (clk->notifier_count)
			clk_notify(clk, CLK_PRE_ENABLE, clk->rate, clk->rate);
		if (clk->mode)
			ret = clk->mode(clk, 1);
		if (clk->notifier_count)
			clk_notify(clk, ret ? CLK_ABORT_ENABLE : CLK_POST_ENABLE, clk->rate, clk->rate);
		if (ret) {
			if (clk->parent)
				clk_disable_nolock(clk->parent);
			return ret;
		}
		pr_debug("%s enabled\n", clk->name);
	}
	clk->usecount++;

	return ret;
}
 
void clk_disable_nolock(struct clk *clk)
{
	if (clk->usecount == 0) {
		CLOCK_PRINTK_ERR(KERN_ERR "Trying disable clock %s with 0 usecount\n", clk->name);
		WARN_ON(1);
		return;
	}
	if (--clk->usecount == 0) {
		int ret = 0;
		if (clk->notifier_count)
			clk_notify(clk, CLK_PRE_DISABLE, clk->rate, clk->rate);
		if (clk->mode)
			ret = clk->mode(clk, 0);
		if (clk->notifier_count)
			clk_notify(clk, ret ? CLK_ABORT_DISABLE : CLK_POST_DISABLE, clk->rate, clk->rate);
		pr_debug("%s disabled\n", clk->name);
		if (ret == 0 && clk->parent)
			clk_disable_nolock(clk->parent);
	}
}
/* Given a clock and a rate apply a clock specific rounding function */
long clk_round_rate_nolock(struct clk *clk, unsigned long rate)
{
	if (clk->round_rate)
		return clk->round_rate(clk, rate);

	if (clk->flags & RATE_FIXED)
		CLOCK_PRINTK_ERR("clock: clk_round_rate called on fixed-rate clock %s\n", clk->name);

	return clk->rate;
}
int is_suport_round_rate(struct clk *clk)
{
	return (clk->round_rate) ? 0:(-1);
}

int clk_set_rate_nolock(struct clk *clk, unsigned long rate)
{
	int ret;
	unsigned long old_rate;

	if (rate == clk->rate)
		return 0;
	if (clk->flags & CONFIG_PARTICIPANT)
		return -EINVAL;

	if (!clk->set_rate)
		return -EINVAL;
	
	//CLOCK_PRINTK_LOG("**will set %s rate %lu\n", clk->name, rate);

	old_rate = clk->rate;
	if (clk->notifier_count)
		clk_notify(clk, CLK_PRE_RATE_CHANGE, old_rate, rate);

	ret = clk->set_rate(clk, rate);

	if (ret == 0) {
		__clk_recalc(clk);
		CLOCK_PRINTK_LOG("**set %s rate recalc=%lu\n",clk->name,clk->rate);
		__propagate_rate(clk);
	}

	if (clk->notifier_count)
		clk_notify(clk, ret ? CLK_ABORT_RATE_CHANGE : CLK_POST_RATE_CHANGE, old_rate, clk->rate);

	return ret;
}
 
int clk_set_parent_nolock(struct clk *clk, struct clk *parent)
{
	int ret;
	int enabled = clk->usecount > 0;
	struct clk *old_parent = clk->parent;

	if (clk->parent == parent)
		return 0;

	/* if clk is already enabled, enable new parent first and disable old parent later. */
	if (enabled)
		clk_enable_nolock(parent);

	if (clk->set_parent)
		ret = clk->set_parent(clk, parent);
	else
		ret = clk_default_set_parent(clk,parent);

	if (ret == 0) {
		/* OK */
		
		//CLOCK_PRINTK_DBG("set_parent %s reparent\n",clk->name,parent->name);
		__clk_reparent(clk, parent);
		__clk_recalc(clk);
		__propagate_rate(clk);
		if (enabled)
			clk_disable_nolock(old_parent);
	} else {
		//CLOCK_PRINTK_DBG("set_parent err\n",clk->name,parent->name);
		if (enabled)
			clk_disable_nolock(parent);
	}

	return ret;
}
/**********************************dvfs****************************************************/

struct clk_node *clk_get_dvfs_info(struct clk *clk)
{
    return clk->dvfs_info;
}

int clk_set_rate_locked(struct clk * clk,unsigned long rate)
{
	int ret;
	//CLOCK_PRINTK_DBG("%s dvfs clk_set_locked\n",clk->name);
	LOCK();
    ret=clk_set_rate_nolock(clk, rate);;
    UNLOCK();
	return ret;
	
}
void clk_register_dvfs(struct clk_node *dvfs_clk, struct clk *clk)
{
    clk->dvfs_info = dvfs_clk;
}


/*-------------------------------------------------------------------------
 * Optional clock functions defined in include/linux/clk.h
 *-------------------------------------------------------------------------*/
#ifdef RK30_CLK_OFFBOARD_TEST
long rk30_clk_round_rate(struct clk *clk, unsigned long rate)
#else
long clk_round_rate(struct clk *clk, unsigned long rate)
#endif
{
	long ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	LOCK();
	ret = clk_round_rate_nolock(clk, rate);
	UNLOCK();

	return ret;
}

#ifdef RK30_CLK_OFFBOARD_TEST
EXPORT_SYMBOL(rk30_clk_round_rate);
#else
EXPORT_SYMBOL(clk_round_rate);
#endif

#ifdef RK30_CLK_OFFBOARD_TEST
unsigned long rk30_clk_get_rate(struct clk *clk)
#else
unsigned long clk_get_rate(struct clk *clk)
#endif
{
	if (clk == NULL || IS_ERR(clk))
		return 0;

	return clk->rate;
}
#ifdef RK30_CLK_OFFBOARD_TEST
EXPORT_SYMBOL(rk30_clk_get_rate);
#else
EXPORT_SYMBOL(clk_get_rate);
#endif


/* Set the clock rate for a clock source */
#ifdef RK30_CLK_OFFBOARD_TEST
int rk30_clk_set_rate(struct clk *clk, unsigned long rate)
#else
int clk_set_rate(struct clk *clk, unsigned long rate)
#endif
{
	int ret = -EINVAL;
	if (clk == NULL || IS_ERR(clk)){
		return ret;
	}
	if (rate == clk->rate)
		return 0;
	if (clk->dvfs_info!=NULL&&is_support_dvfs(clk->dvfs_info))
		return dvfs_set_rate(clk, rate);

	LOCK();
	ret = clk_set_rate_nolock(clk, rate);
	UNLOCK();

	return ret;
}
#ifdef RK30_CLK_OFFBOARD_TEST
EXPORT_SYMBOL(rk30_clk_set_rate);
#else
EXPORT_SYMBOL(clk_set_rate);
#endif


#ifdef RK30_CLK_OFFBOARD_TEST
int rk30_clk_set_parent(struct clk *clk, struct clk *parent)
#else
int clk_set_parent(struct clk *clk, struct clk *parent)
#endif
{
	int ret = -EINVAL;

	if (clk == NULL || IS_ERR(clk) || parent == NULL || IS_ERR(parent))
		return ret;

	if (clk->parents == NULL)
		return ret;

	LOCK();
	if (clk->usecount == 0)
		ret = clk_set_parent_nolock(clk, parent);
	else
		ret = -EBUSY;
	UNLOCK();

	return ret;
}
int clk_set_parent_force(struct clk *clk, struct clk *parent)
{
	int ret = -EINVAL;

	if (clk == NULL || IS_ERR(clk) || parent == NULL || IS_ERR(parent))
		return ret;

	if (clk->parents == NULL)
		return ret;
	LOCK();
		ret = clk_set_parent_nolock(clk, parent);	
	UNLOCK();
	return ret;
}

#ifdef RK30_CLK_OFFBOARD_TEST
EXPORT_SYMBOL(rk30_clk_set_parent);
#else
EXPORT_SYMBOL(clk_set_parent);
#endif

#ifdef RK30_CLK_OFFBOARD_TEST
struct clk *rk30_clk_get_parent(struct clk *clk)
#else
struct clk *clk_get_parent(struct clk *clk)
#endif
{
	if (clk == NULL || IS_ERR(clk)) {
		return ERR_PTR(-EINVAL);
	}
	return clk->parent;
}

#ifdef RK30_CLK_OFFBOARD_TEST
EXPORT_SYMBOL(rk30_clk_get_parent);
#else
EXPORT_SYMBOL(clk_get_parent);
#endif

#ifdef RK30_CLK_OFFBOARD_TEST
void rk30_clk_disable(struct clk *clk)
#else
void clk_disable(struct clk *clk)
#endif
{
	if (clk == NULL || IS_ERR(clk))
		return;

	LOCK();
	clk_disable_nolock(clk);
	UNLOCK();
}
#ifdef RK30_CLK_OFFBOARD_TEST
EXPORT_SYMBOL(rk30_clk_disable);
#else
EXPORT_SYMBOL(clk_disable);
#endif

#ifdef RK30_CLK_OFFBOARD_TEST
int rk30_clk_enable(struct clk *clk)
#else
int  clk_enable(struct clk *clk)
#endif
{
	int ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	LOCK();
	ret = clk_enable_nolock(clk);
	UNLOCK();

	return ret;
}
#ifdef RK30_CLK_OFFBOARD_TEST
EXPORT_SYMBOL(rk30_clk_enable);
#else
EXPORT_SYMBOL(clk_enable);
#endif

/* Clk notifier implementation */

/**
 * struct clk_notifier - associate a clk with a notifier
 * @clk: struct clk * to associate the notifier with
 * @notifier_head: a raw_notifier_head for this clk
 * @node: linked list pointers
 *
 * A list of struct clk_notifier is maintained by the notifier code.
 * An entry is created whenever code registers the first notifier on a
 * particular @clk.  Future notifiers on that @clk are added to the
 * @notifier_head.
 */
struct clk_notifier {
	struct clk			*clk;
	struct raw_notifier_head	notifier_head;
	struct list_head		node;
};
static LIST_HEAD(clk_notifier_list);
/**
 * _clk_free_notifier_chain - safely remove struct clk_notifier
 * @cn: struct clk_notifier *
 *
 * Removes the struct clk_notifier @cn from the clk_notifier_list and
 * frees it.
 */
static void _clk_free_notifier_chain(struct clk_notifier *cn)
{
	list_del(&cn->node);
	kfree(cn);
}

/**
 * clk_notify - call clk notifier chain
 * @clk: struct clk * that is changing rate
 * @msg: clk notifier type (i.e., CLK_POST_RATE_CHANGE; see mach/clock.h)
 * @old_rate: old rate
 * @new_rate: new rate
 *
 * Triggers a notifier call chain on the post-clk-rate-change notifier
 * for clock 'clk'.  Passes a pointer to the struct clk and the
 * previous and current rates to the notifier callback.  Intended to be
 * called by internal clock code only.  No return value.
 */
static void clk_notify(struct clk *clk, unsigned long msg,
		       unsigned long old_rate, unsigned long new_rate)
{
	struct clk_notifier *cn;
	struct clk_notifier_data cnd;

	cnd.clk = clk;
	cnd.old_rate = old_rate;
	cnd.new_rate = new_rate;

	UNLOCK();
	list_for_each_entry(cn, &clk_notifier_list, node) {
		if (cn->clk == clk) {
			pr_debug("%s msg %lu rate %lu -> %lu\n", clk->name, msg, old_rate, new_rate);
			raw_notifier_call_chain(&cn->notifier_head, msg, &cnd);
			break;
		}
	}
	LOCK();
}

/**
 * clk_notifier_register - add a clock parameter change notifier
 * @clk: struct clk * to watch
 * @nb: struct notifier_block * with callback info
 *
 * Request notification for changes to the clock 'clk'.  This uses a
 * blocking notifier.  Callback code must not call into the clock
 * framework, as clocks_mutex is held.  Pre-notifier callbacks will be
 * passed the previous and new rate of the clock.
 *
 * clk_notifier_register() must be called from process
 * context.  Returns -EINVAL if called with null arguments, -ENOMEM
 * upon allocation failure; otherwise, passes along the return value
 * of blocking_notifier_chain_register().
 */
int rk30_clk_notifier_register(struct clk *clk, struct notifier_block *nb)
{
	struct clk_notifier *cn = NULL, *cn_new = NULL;
	int r;
	struct clk *clkp;

	if (!clk || IS_ERR(clk) || !nb)
		return -EINVAL;

	mutex_lock(&clocks_mutex);

	list_for_each_entry(cn, &clk_notifier_list, node)
		if (cn->clk == clk)
			break;

	if (cn->clk != clk) {
		cn_new = kzalloc(sizeof(struct clk_notifier), GFP_KERNEL);
		if (!cn_new) {
			r = -ENOMEM;
			goto cnr_out;
		};

		cn_new->clk = clk;
		RAW_INIT_NOTIFIER_HEAD(&cn_new->notifier_head);

		list_add(&cn_new->node, &clk_notifier_list);
		cn = cn_new;
	}

	r = raw_notifier_chain_register(&cn->notifier_head, nb);
	if (!IS_ERR_VALUE(r)) {
		clkp = clk;
		do {
			clkp->notifier_count++;
		} while ((clkp = clkp->parent));
	} else {
		if (cn_new)
			_clk_free_notifier_chain(cn);
	}

cnr_out:
	mutex_unlock(&clocks_mutex);

	return r;
}
EXPORT_SYMBOL(rk30_clk_notifier_register);

/**
 * clk_notifier_unregister - remove a clock change notifier
 * @clk: struct clk *
 * @nb: struct notifier_block * with callback info
 *
 * Request no further notification for changes to clock 'clk'.
 * Returns -EINVAL if called with null arguments; otherwise, passes
 * along the return value of blocking_notifier_chain_unregister().
 */
int rk30_clk_notifier_unregister(struct clk *clk, struct notifier_block *nb)
{
	struct clk_notifier *cn = NULL;
	struct clk *clkp;
	int r = -EINVAL;

	if (!clk || IS_ERR(clk) || !nb)
		return -EINVAL;

	mutex_lock(&clocks_mutex);

	list_for_each_entry(cn, &clk_notifier_list, node)
		if (cn->clk == clk)
			break;

	if (cn->clk != clk) {
		r = -ENOENT;
		goto cnu_out;
	};

	r = raw_notifier_chain_unregister(&cn->notifier_head, nb);
	if (!IS_ERR_VALUE(r)) {
		clkp = clk;
		do {
			clkp->notifier_count--;
		} while ((clkp = clkp->parent));
	}

	/*
	 * XXX ugh, layering violation.  There should be some
	 * support in the notifier code for this.
	 */
	if (!cn->notifier_head.head)
		_clk_free_notifier_chain(cn);

cnu_out:
	mutex_unlock(&clocks_mutex);

	return r;
}
EXPORT_SYMBOL(rk30_clk_notifier_unregister);

static struct clk_dump_ops *dump_def_ops;

void clk_register_dump_ops(struct clk_dump_ops *ops)
{
	dump_def_ops=ops;
}

#ifdef CONFIG_RK_CLOCK_PROC
static int proc_clk_show(struct seq_file *s, void *v)
{
	struct clk* clk;
	
	if(!dump_def_ops)
		return 0;

	if(dump_def_ops->dump_clk)
	{
		mutex_lock(&clocks_mutex);
		list_for_each_entry(clk, &clocks, node) {
			if (!clk->parent)
			{
				dump_def_ops->dump_clk(s, clk, 0,&clocks);
			}
		}
		mutex_unlock(&clocks_mutex);
	}
	if(dump_def_ops->dump_regs)
		dump_def_ops->dump_regs(s);
	return 0;
}


static int proc_clk_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_clk_show, NULL);
}

static const struct file_operations proc_clk_fops = {
	.open		= proc_clk_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init clk_proc_init(void)
{
	proc_create("clocks", 0, NULL, &proc_clk_fops);
	return 0;

}
late_initcall(clk_proc_init);
#endif /* CONFIG_RK_CLOCK_PROC */

