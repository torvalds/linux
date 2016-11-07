/*
 * Reset Controller framework
 *
 * Copyright 2013 Philipp Zabel, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/reset.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>

static DEFINE_MUTEX(reset_list_mutex);
static LIST_HEAD(reset_controller_list);

/**
 * struct reset_control - a reset control
 * @rcdev: a pointer to the reset controller device
 *         this reset control belongs to
 * @list: list entry for the rcdev's reset controller list
 * @id: ID of the reset controller in the reset
 *      controller device
 * @refcnt: Number of gets of this reset_control
 * @shared: Is this a shared (1), or an exclusive (0) reset_control?
 * @deassert_cnt: Number of times this reset line has been deasserted
 */
struct reset_control {
	struct reset_controller_dev *rcdev;
	struct list_head list;
	unsigned int id;
	unsigned int refcnt;
	int shared;
	atomic_t deassert_count;
};

/**
 * of_reset_simple_xlate - translate reset_spec to the reset line number
 * @rcdev: a pointer to the reset controller device
 * @reset_spec: reset line specifier as found in the device tree
 * @flags: a flags pointer to fill in (optional)
 *
 * This simple translation function should be used for reset controllers
 * with 1:1 mapping, where reset lines can be indexed by number without gaps.
 */
static int of_reset_simple_xlate(struct reset_controller_dev *rcdev,
			  const struct of_phandle_args *reset_spec)
{
	if (reset_spec->args[0] >= rcdev->nr_resets)
		return -EINVAL;

	return reset_spec->args[0];
}

/**
 * reset_controller_register - register a reset controller device
 * @rcdev: a pointer to the initialized reset controller device
 */
int reset_controller_register(struct reset_controller_dev *rcdev)
{
	if (!rcdev->of_xlate) {
		rcdev->of_reset_n_cells = 1;
		rcdev->of_xlate = of_reset_simple_xlate;
	}

	INIT_LIST_HEAD(&rcdev->reset_control_head);

	mutex_lock(&reset_list_mutex);
	list_add(&rcdev->list, &reset_controller_list);
	mutex_unlock(&reset_list_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(reset_controller_register);

/**
 * reset_controller_unregister - unregister a reset controller device
 * @rcdev: a pointer to the reset controller device
 */
void reset_controller_unregister(struct reset_controller_dev *rcdev)
{
	mutex_lock(&reset_list_mutex);
	list_del(&rcdev->list);
	mutex_unlock(&reset_list_mutex);
}
EXPORT_SYMBOL_GPL(reset_controller_unregister);

static void devm_reset_controller_release(struct device *dev, void *res)
{
	reset_controller_unregister(*(struct reset_controller_dev **)res);
}

/**
 * devm_reset_controller_register - resource managed reset_controller_register()
 * @dev: device that is registering this reset controller
 * @rcdev: a pointer to the initialized reset controller device
 *
 * Managed reset_controller_register(). For reset controllers registered by
 * this function, reset_controller_unregister() is automatically called on
 * driver detach. See reset_controller_register() for more information.
 */
int devm_reset_controller_register(struct device *dev,
				   struct reset_controller_dev *rcdev)
{
	struct reset_controller_dev **rcdevp;
	int ret;

	rcdevp = devres_alloc(devm_reset_controller_release, sizeof(*rcdevp),
			      GFP_KERNEL);
	if (!rcdevp)
		return -ENOMEM;

	ret = reset_controller_register(rcdev);
	if (!ret) {
		*rcdevp = rcdev;
		devres_add(dev, rcdevp);
	} else {
		devres_free(rcdevp);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_reset_controller_register);

/**
 * reset_control_reset - reset the controlled device
 * @rstc: reset controller
 *
 * Calling this on a shared reset controller is an error.
 */
int reset_control_reset(struct reset_control *rstc)
{
	if (WARN_ON(IS_ERR_OR_NULL(rstc)) ||
	    WARN_ON(rstc->shared))
		return -EINVAL;

	if (rstc->rcdev->ops->reset)
		return rstc->rcdev->ops->reset(rstc->rcdev, rstc->id);

	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(reset_control_reset);

/**
 * reset_control_assert - asserts the reset line
 * @rstc: reset controller
 *
 * Calling this on an exclusive reset controller guarantees that the reset
 * will be asserted. When called on a shared reset controller the line may
 * still be deasserted, as long as other users keep it so.
 *
 * For shared reset controls a driver cannot expect the hw's registers and
 * internal state to be reset, but must be prepared for this to happen.
 */
int reset_control_assert(struct reset_control *rstc)
{
	if (WARN_ON(IS_ERR_OR_NULL(rstc)))
		return -EINVAL;

	if (!rstc->rcdev->ops->assert)
		return -ENOTSUPP;

	if (rstc->shared) {
		if (WARN_ON(atomic_read(&rstc->deassert_count) == 0))
			return -EINVAL;

		if (atomic_dec_return(&rstc->deassert_count) != 0)
			return 0;
	}

	return rstc->rcdev->ops->assert(rstc->rcdev, rstc->id);
}
EXPORT_SYMBOL_GPL(reset_control_assert);

/**
 * reset_control_deassert - deasserts the reset line
 * @rstc: reset controller
 *
 * After calling this function, the reset is guaranteed to be deasserted.
 */
int reset_control_deassert(struct reset_control *rstc)
{
	if (WARN_ON(IS_ERR_OR_NULL(rstc)))
		return -EINVAL;

	if (!rstc->rcdev->ops->deassert)
		return -ENOTSUPP;

	if (rstc->shared) {
		if (atomic_inc_return(&rstc->deassert_count) != 1)
			return 0;
	}

	return rstc->rcdev->ops->deassert(rstc->rcdev, rstc->id);
}
EXPORT_SYMBOL_GPL(reset_control_deassert);

/**
 * reset_control_status - returns a negative errno if not supported, a
 * positive value if the reset line is asserted, or zero if the reset
 * line is not asserted.
 * @rstc: reset controller
 */
int reset_control_status(struct reset_control *rstc)
{
	if (WARN_ON(IS_ERR_OR_NULL(rstc)))
		return -EINVAL;

	if (rstc->rcdev->ops->status)
		return rstc->rcdev->ops->status(rstc->rcdev, rstc->id);

	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(reset_control_status);

static struct reset_control *__reset_control_get(
				struct reset_controller_dev *rcdev,
				unsigned int index, int shared)
{
	struct reset_control *rstc;

	lockdep_assert_held(&reset_list_mutex);

	list_for_each_entry(rstc, &rcdev->reset_control_head, list) {
		if (rstc->id == index) {
			if (WARN_ON(!rstc->shared || !shared))
				return ERR_PTR(-EBUSY);

			rstc->refcnt++;
			return rstc;
		}
	}

	rstc = kzalloc(sizeof(*rstc), GFP_KERNEL);
	if (!rstc)
		return ERR_PTR(-ENOMEM);

	try_module_get(rcdev->owner);

	rstc->rcdev = rcdev;
	list_add(&rstc->list, &rcdev->reset_control_head);
	rstc->id = index;
	rstc->refcnt = 1;
	rstc->shared = shared;

	return rstc;
}

static void __reset_control_put(struct reset_control *rstc)
{
	lockdep_assert_held(&reset_list_mutex);

	if (--rstc->refcnt)
		return;

	module_put(rstc->rcdev->owner);

	list_del(&rstc->list);
	kfree(rstc);
}

struct reset_control *__of_reset_control_get(struct device_node *node,
				     const char *id, int index, int shared)
{
	struct reset_control *rstc;
	struct reset_controller_dev *r, *rcdev;
	struct of_phandle_args args;
	int rstc_id;
	int ret;

	if (!node)
		return ERR_PTR(-EINVAL);

	if (id) {
		index = of_property_match_string(node,
						 "reset-names", id);
		if (index < 0)
			return ERR_PTR(-ENOENT);
	}

	ret = of_parse_phandle_with_args(node, "resets", "#reset-cells",
					 index, &args);
	if (ret)
		return ERR_PTR(ret);

	mutex_lock(&reset_list_mutex);
	rcdev = NULL;
	list_for_each_entry(r, &reset_controller_list, list) {
		if (args.np == r->of_node) {
			rcdev = r;
			break;
		}
	}
	of_node_put(args.np);

	if (!rcdev) {
		mutex_unlock(&reset_list_mutex);
		return ERR_PTR(-EPROBE_DEFER);
	}

	if (WARN_ON(args.args_count != rcdev->of_reset_n_cells)) {
		mutex_unlock(&reset_list_mutex);
		return ERR_PTR(-EINVAL);
	}

	rstc_id = rcdev->of_xlate(rcdev, &args);
	if (rstc_id < 0) {
		mutex_unlock(&reset_list_mutex);
		return ERR_PTR(rstc_id);
	}

	/* reset_list_mutex also protects the rcdev's reset_control list */
	rstc = __reset_control_get(rcdev, rstc_id, shared);

	mutex_unlock(&reset_list_mutex);

	return rstc;
}
EXPORT_SYMBOL_GPL(__of_reset_control_get);

/**
 * reset_control_put - free the reset controller
 * @rstc: reset controller
 */

void reset_control_put(struct reset_control *rstc)
{
	if (IS_ERR(rstc))
		return;

	mutex_lock(&reset_list_mutex);
	__reset_control_put(rstc);
	mutex_unlock(&reset_list_mutex);
}
EXPORT_SYMBOL_GPL(reset_control_put);

static void devm_reset_control_release(struct device *dev, void *res)
{
	reset_control_put(*(struct reset_control **)res);
}

struct reset_control *__devm_reset_control_get(struct device *dev,
				     const char *id, int index, int shared)
{
	struct reset_control **ptr, *rstc;

	ptr = devres_alloc(devm_reset_control_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	rstc = __of_reset_control_get(dev ? dev->of_node : NULL,
				      id, index, shared);
	if (!IS_ERR(rstc)) {
		*ptr = rstc;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return rstc;
}
EXPORT_SYMBOL_GPL(__devm_reset_control_get);

/**
 * device_reset - find reset controller associated with the device
 *                and perform reset
 * @dev: device to be reset by the controller
 *
 * Convenience wrapper for reset_control_get() and reset_control_reset().
 * This is useful for the common case of devices with single, dedicated reset
 * lines.
 */
int device_reset(struct device *dev)
{
	struct reset_control *rstc;
	int ret;

	rstc = reset_control_get(dev, NULL);
	if (IS_ERR(rstc))
		return PTR_ERR(rstc);

	ret = reset_control_reset(rstc);

	reset_control_put(rstc);

	return ret;
}
EXPORT_SYMBOL_GPL(device_reset);
