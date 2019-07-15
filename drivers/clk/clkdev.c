// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/clk/clkdev.c
 *
 *  Copyright (C) 2008 Russell King.
 *
 * Helper for the clk API to assist looking up a struct clk.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>

#include "clk.h"

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);

/*
 * Find the correct struct clk for the device and connection ID.
 * We do slightly fuzzy matching here:
 *  An entry with a NULL ID is assumed to be a wildcard.
 *  If an entry has a device ID, it must match
 *  If an entry has a connection ID, it must match
 * Then we take the most specific entry - with the following
 * order of precedence: dev+con > dev only > con only.
 */
static struct clk_lookup *clk_find(const char *dev_id, const char *con_id)
{
	struct clk_lookup *p, *cl = NULL;
	int match, best_found = 0, best_possible = 0;

	if (dev_id)
		best_possible += 2;
	if (con_id)
		best_possible += 1;

	lockdep_assert_held(&clocks_mutex);

	list_for_each_entry(p, &clocks, node) {
		match = 0;
		if (p->dev_id) {
			if (!dev_id || strcmp(p->dev_id, dev_id))
				continue;
			match += 2;
		}
		if (p->con_id) {
			if (!con_id || strcmp(p->con_id, con_id))
				continue;
			match += 1;
		}

		if (match > best_found) {
			cl = p;
			if (match != best_possible)
				best_found = match;
			else
				break;
		}
	}
	return cl;
}

struct clk_hw *clk_find_hw(const char *dev_id, const char *con_id)
{
	struct clk_lookup *cl;
	struct clk_hw *hw = ERR_PTR(-ENOENT);

	mutex_lock(&clocks_mutex);
	cl = clk_find(dev_id, con_id);
	if (cl)
		hw = cl->clk_hw;
	mutex_unlock(&clocks_mutex);

	return hw;
}

static struct clk *__clk_get_sys(struct device *dev, const char *dev_id,
				 const char *con_id)
{
	struct clk_hw *hw = clk_find_hw(dev_id, con_id);

	return clk_hw_create_clk(dev, hw, dev_id, con_id);
}

struct clk *clk_get_sys(const char *dev_id, const char *con_id)
{
	return __clk_get_sys(NULL, dev_id, con_id);
}
EXPORT_SYMBOL(clk_get_sys);

struct clk *clk_get(struct device *dev, const char *con_id)
{
	const char *dev_id = dev ? dev_name(dev) : NULL;
	struct clk_hw *hw;

	if (dev && dev->of_node) {
		hw = of_clk_get_hw(dev->of_node, 0, con_id);
		if (!IS_ERR(hw) || PTR_ERR(hw) == -EPROBE_DEFER)
			return clk_hw_create_clk(dev, hw, dev_id, con_id);
	}

	return __clk_get_sys(dev, dev_id, con_id);
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
	__clk_put(clk);
}
EXPORT_SYMBOL(clk_put);

static void __clkdev_add(struct clk_lookup *cl)
{
	mutex_lock(&clocks_mutex);
	list_add_tail(&cl->node, &clocks);
	mutex_unlock(&clocks_mutex);
}

void clkdev_add(struct clk_lookup *cl)
{
	if (!cl->clk_hw)
		cl->clk_hw = __clk_get_hw(cl->clk);
	__clkdev_add(cl);
}
EXPORT_SYMBOL(clkdev_add);

void clkdev_add_table(struct clk_lookup *cl, size_t num)
{
	mutex_lock(&clocks_mutex);
	while (num--) {
		cl->clk_hw = __clk_get_hw(cl->clk);
		list_add_tail(&cl->node, &clocks);
		cl++;
	}
	mutex_unlock(&clocks_mutex);
}

#define MAX_DEV_ID	20
#define MAX_CON_ID	16

struct clk_lookup_alloc {
	struct clk_lookup cl;
	char	dev_id[MAX_DEV_ID];
	char	con_id[MAX_CON_ID];
};

static struct clk_lookup * __ref
vclkdev_alloc(struct clk_hw *hw, const char *con_id, const char *dev_fmt,
	va_list ap)
{
	struct clk_lookup_alloc *cla;

	cla = kzalloc(sizeof(*cla), GFP_KERNEL);
	if (!cla)
		return NULL;

	cla->cl.clk_hw = hw;
	if (con_id) {
		strlcpy(cla->con_id, con_id, sizeof(cla->con_id));
		cla->cl.con_id = cla->con_id;
	}

	if (dev_fmt) {
		vscnprintf(cla->dev_id, sizeof(cla->dev_id), dev_fmt, ap);
		cla->cl.dev_id = cla->dev_id;
	}

	return &cla->cl;
}

static struct clk_lookup *
vclkdev_create(struct clk_hw *hw, const char *con_id, const char *dev_fmt,
	va_list ap)
{
	struct clk_lookup *cl;

	cl = vclkdev_alloc(hw, con_id, dev_fmt, ap);
	if (cl)
		__clkdev_add(cl);

	return cl;
}

struct clk_lookup * __ref
clkdev_alloc(struct clk *clk, const char *con_id, const char *dev_fmt, ...)
{
	struct clk_lookup *cl;
	va_list ap;

	va_start(ap, dev_fmt);
	cl = vclkdev_alloc(__clk_get_hw(clk), con_id, dev_fmt, ap);
	va_end(ap);

	return cl;
}
EXPORT_SYMBOL(clkdev_alloc);

struct clk_lookup *
clkdev_hw_alloc(struct clk_hw *hw, const char *con_id, const char *dev_fmt, ...)
{
	struct clk_lookup *cl;
	va_list ap;

	va_start(ap, dev_fmt);
	cl = vclkdev_alloc(hw, con_id, dev_fmt, ap);
	va_end(ap);

	return cl;
}
EXPORT_SYMBOL(clkdev_hw_alloc);

/**
 * clkdev_create - allocate and add a clkdev lookup structure
 * @clk: struct clk to associate with all clk_lookups
 * @con_id: connection ID string on device
 * @dev_fmt: format string describing device name
 *
 * Returns a clk_lookup structure, which can be later unregistered and
 * freed.
 */
struct clk_lookup *clkdev_create(struct clk *clk, const char *con_id,
	const char *dev_fmt, ...)
{
	struct clk_lookup *cl;
	va_list ap;

	va_start(ap, dev_fmt);
	cl = vclkdev_create(__clk_get_hw(clk), con_id, dev_fmt, ap);
	va_end(ap);

	return cl;
}
EXPORT_SYMBOL_GPL(clkdev_create);

/**
 * clkdev_hw_create - allocate and add a clkdev lookup structure
 * @hw: struct clk_hw to associate with all clk_lookups
 * @con_id: connection ID string on device
 * @dev_fmt: format string describing device name
 *
 * Returns a clk_lookup structure, which can be later unregistered and
 * freed.
 */
struct clk_lookup *clkdev_hw_create(struct clk_hw *hw, const char *con_id,
	const char *dev_fmt, ...)
{
	struct clk_lookup *cl;
	va_list ap;

	va_start(ap, dev_fmt);
	cl = vclkdev_create(hw, con_id, dev_fmt, ap);
	va_end(ap);

	return cl;
}
EXPORT_SYMBOL_GPL(clkdev_hw_create);

int clk_add_alias(const char *alias, const char *alias_dev_name,
	const char *con_id, struct device *dev)
{
	struct clk *r = clk_get(dev, con_id);
	struct clk_lookup *l;

	if (IS_ERR(r))
		return PTR_ERR(r);

	l = clkdev_create(r, alias, alias_dev_name ? "%s" : NULL,
			  alias_dev_name);
	clk_put(r);

	return l ? 0 : -ENODEV;
}
EXPORT_SYMBOL(clk_add_alias);

/*
 * clkdev_drop - remove a clock dynamically allocated
 */
void clkdev_drop(struct clk_lookup *cl)
{
	mutex_lock(&clocks_mutex);
	list_del(&cl->node);
	mutex_unlock(&clocks_mutex);
	kfree(cl);
}
EXPORT_SYMBOL(clkdev_drop);

static struct clk_lookup *__clk_register_clkdev(struct clk_hw *hw,
						const char *con_id,
						const char *dev_id, ...)
{
	struct clk_lookup *cl;
	va_list ap;

	va_start(ap, dev_id);
	cl = vclkdev_create(hw, con_id, dev_id, ap);
	va_end(ap);

	return cl;
}

static int do_clk_register_clkdev(struct clk_hw *hw,
	struct clk_lookup **cl, const char *con_id, const char *dev_id)
{
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	/*
	 * Since dev_id can be NULL, and NULL is handled specially, we must
	 * pass it as either a NULL format string, or with "%s".
	 */
	if (dev_id)
		*cl = __clk_register_clkdev(hw, con_id, "%s", dev_id);
	else
		*cl = __clk_register_clkdev(hw, con_id, NULL);

	return *cl ? 0 : -ENOMEM;
}

/**
 * clk_register_clkdev - register one clock lookup for a struct clk
 * @clk: struct clk to associate with all clk_lookups
 * @con_id: connection ID string on device
 * @dev_id: string describing device name
 *
 * con_id or dev_id may be NULL as a wildcard, just as in the rest of
 * clkdev.
 *
 * To make things easier for mass registration, we detect error clks
 * from a previous clk_register() call, and return the error code for
 * those.  This is to permit this function to be called immediately
 * after clk_register().
 */
int clk_register_clkdev(struct clk *clk, const char *con_id,
	const char *dev_id)
{
	struct clk_lookup *cl;

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	return do_clk_register_clkdev(__clk_get_hw(clk), &cl, con_id,
					      dev_id);
}
EXPORT_SYMBOL(clk_register_clkdev);

/**
 * clk_hw_register_clkdev - register one clock lookup for a struct clk_hw
 * @hw: struct clk_hw to associate with all clk_lookups
 * @con_id: connection ID string on device
 * @dev_id: format string describing device name
 *
 * con_id or dev_id may be NULL as a wildcard, just as in the rest of
 * clkdev.
 *
 * To make things easier for mass registration, we detect error clk_hws
 * from a previous clk_hw_register_*() call, and return the error code for
 * those.  This is to permit this function to be called immediately
 * after clk_hw_register_*().
 */
int clk_hw_register_clkdev(struct clk_hw *hw, const char *con_id,
	const char *dev_id)
{
	struct clk_lookup *cl;

	return do_clk_register_clkdev(hw, &cl, con_id, dev_id);
}
EXPORT_SYMBOL(clk_hw_register_clkdev);

static void devm_clkdev_release(struct device *dev, void *res)
{
	clkdev_drop(*(struct clk_lookup **)res);
}

static int devm_clk_match_clkdev(struct device *dev, void *res, void *data)
{
	struct clk_lookup **l = res;

	return *l == data;
}

/**
 * devm_clk_release_clkdev - Resource managed clkdev lookup release
 * @dev: device this lookup is bound
 * @con_id: connection ID string on device
 * @dev_id: format string describing device name
 *
 * Drop the clkdev lookup created with devm_clk_hw_register_clkdev.
 * Normally this function will not need to be called and the resource
 * management code will ensure that the resource is freed.
 */
void devm_clk_release_clkdev(struct device *dev, const char *con_id,
			     const char *dev_id)
{
	struct clk_lookup *cl;
	int rval;

	mutex_lock(&clocks_mutex);
	cl = clk_find(dev_id, con_id);
	mutex_unlock(&clocks_mutex);

	WARN_ON(!cl);
	rval = devres_release(dev, devm_clkdev_release,
			      devm_clk_match_clkdev, cl);
	WARN_ON(rval);
}
EXPORT_SYMBOL(devm_clk_release_clkdev);

/**
 * devm_clk_hw_register_clkdev - managed clk lookup registration for clk_hw
 * @dev: device this lookup is bound
 * @hw: struct clk_hw to associate with all clk_lookups
 * @con_id: connection ID string on device
 * @dev_id: format string describing device name
 *
 * con_id or dev_id may be NULL as a wildcard, just as in the rest of
 * clkdev.
 *
 * To make things easier for mass registration, we detect error clk_hws
 * from a previous clk_hw_register_*() call, and return the error code for
 * those.  This is to permit this function to be called immediately
 * after clk_hw_register_*().
 */
int devm_clk_hw_register_clkdev(struct device *dev, struct clk_hw *hw,
				const char *con_id, const char *dev_id)
{
	int rval = -ENOMEM;
	struct clk_lookup **cl;

	cl = devres_alloc(devm_clkdev_release, sizeof(*cl), GFP_KERNEL);
	if (cl) {
		rval = do_clk_register_clkdev(hw, cl, con_id, dev_id);
		if (!rval)
			devres_add(dev, cl);
		else
			devres_free(cl);
	}
	return rval;
}
EXPORT_SYMBOL(devm_clk_hw_register_clkdev);
