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
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/reset.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>

static DEFINE_MUTEX(reset_controller_list_mutex);
static LIST_HEAD(reset_controller_list);

/**
 * struct reset_control - a reset control
 * @rcdev: a pointer to the reset controller device
 *         this reset control belongs to
 * @id: ID of the reset controller in the reset
 *      controller device
 */
struct reset_control {
	struct reset_controller_dev *rcdev;
	unsigned int id;
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

	mutex_lock(&reset_controller_list_mutex);
	list_add(&rcdev->list, &reset_controller_list);
	mutex_unlock(&reset_controller_list_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(reset_controller_register);

/**
 * reset_controller_unregister - unregister a reset controller device
 * @rcdev: a pointer to the reset controller device
 */
void reset_controller_unregister(struct reset_controller_dev *rcdev)
{
	mutex_lock(&reset_controller_list_mutex);
	list_del(&rcdev->list);
	mutex_unlock(&reset_controller_list_mutex);
}
EXPORT_SYMBOL_GPL(reset_controller_unregister);

/**
 * reset_control_reset - reset the controlled device
 * @rstc: reset controller
 */
int reset_control_reset(struct reset_control *rstc)
{
	if (rstc->rcdev->ops->reset)
		return rstc->rcdev->ops->reset(rstc->rcdev, rstc->id);

	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(reset_control_reset);

/**
 * reset_control_assert - asserts the reset line
 * @rstc: reset controller
 */
int reset_control_assert(struct reset_control *rstc)
{
	if (rstc->rcdev->ops->assert)
		return rstc->rcdev->ops->assert(rstc->rcdev, rstc->id);

	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(reset_control_assert);

/**
 * reset_control_deassert - deasserts the reset line
 * @rstc: reset controller
 */
int reset_control_deassert(struct reset_control *rstc)
{
	if (rstc->rcdev->ops->deassert)
		return rstc->rcdev->ops->deassert(rstc->rcdev, rstc->id);

	return -ENOTSUPP;
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
	if (rstc->rcdev->ops->status)
		return rstc->rcdev->ops->status(rstc->rcdev, rstc->id);

	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(reset_control_status);

/**
 * of_reset_control_get_by_index - Lookup and obtain a reference to a reset
 * controller by index.
 * @node: device to be reset by the controller
 * @index: index of the reset controller
 *
 * This is to be used to perform a list of resets for a device or power domain
 * in whatever order. Returns a struct reset_control or IS_ERR() condition
 * containing errno.
 */
struct reset_control *of_reset_control_get_by_index(struct device_node *node,
					   int index)
{
	struct reset_control *rstc;
	struct reset_controller_dev *r, *rcdev;
	struct of_phandle_args args;
	int rstc_id;
	int ret;

	ret = of_parse_phandle_with_args(node, "resets", "#reset-cells",
					 index, &args);
	if (ret)
		return ERR_PTR(ret);

	mutex_lock(&reset_controller_list_mutex);
	rcdev = NULL;
	list_for_each_entry(r, &reset_controller_list, list) {
		if (args.np == r->of_node) {
			rcdev = r;
			break;
		}
	}
	of_node_put(args.np);

	if (!rcdev) {
		mutex_unlock(&reset_controller_list_mutex);
		return ERR_PTR(-EPROBE_DEFER);
	}

	if (WARN_ON(args.args_count != rcdev->of_reset_n_cells)) {
		mutex_unlock(&reset_controller_list_mutex);
		return ERR_PTR(-EINVAL);
	}

	rstc_id = rcdev->of_xlate(rcdev, &args);
	if (rstc_id < 0) {
		mutex_unlock(&reset_controller_list_mutex);
		return ERR_PTR(rstc_id);
	}

	try_module_get(rcdev->owner);
	mutex_unlock(&reset_controller_list_mutex);

	rstc = kzalloc(sizeof(*rstc), GFP_KERNEL);
	if (!rstc) {
		module_put(rcdev->owner);
		return ERR_PTR(-ENOMEM);
	}

	rstc->rcdev = rcdev;
	rstc->id = rstc_id;

	return rstc;
}
EXPORT_SYMBOL_GPL(of_reset_control_get_by_index);

/**
 * of_reset_control_get - Lookup and obtain a reference to a reset controller.
 * @node: device to be reset by the controller
 * @id: reset line name
 *
 * Returns a struct reset_control or IS_ERR() condition containing errno.
 *
 * Use of id names is optional.
 */
struct reset_control *of_reset_control_get(struct device_node *node,
					   const char *id)
{
	int index = 0;

	if (id) {
		index = of_property_match_string(node,
						 "reset-names", id);
		if (index < 0)
			return ERR_PTR(-ENOENT);
	}
	return of_reset_control_get_by_index(node, index);
}
EXPORT_SYMBOL_GPL(of_reset_control_get);

/**
 * reset_control_get - Lookup and obtain a reference to a reset controller.
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Returns a struct reset_control or IS_ERR() condition containing errno.
 *
 * Use of id names is optional.
 */
struct reset_control *reset_control_get(struct device *dev, const char *id)
{
	if (!dev)
		return ERR_PTR(-EINVAL);

	return of_reset_control_get(dev->of_node, id);
}
EXPORT_SYMBOL_GPL(reset_control_get);

/**
 * reset_control_put - free the reset controller
 * @rstc: reset controller
 */

void reset_control_put(struct reset_control *rstc)
{
	if (IS_ERR(rstc))
		return;

	module_put(rstc->rcdev->owner);
	kfree(rstc);
}
EXPORT_SYMBOL_GPL(reset_control_put);

static void devm_reset_control_release(struct device *dev, void *res)
{
	reset_control_put(*(struct reset_control **)res);
}

/**
 * devm_reset_control_get - resource managed reset_control_get()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Managed reset_control_get(). For reset controllers returned from this
 * function, reset_control_put() is called automatically on driver detach.
 * See reset_control_get() for more information.
 */
struct reset_control *devm_reset_control_get(struct device *dev, const char *id)
{
	struct reset_control **ptr, *rstc;

	ptr = devres_alloc(devm_reset_control_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	rstc = reset_control_get(dev, id);
	if (!IS_ERR(rstc)) {
		*ptr = rstc;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return rstc;
}
EXPORT_SYMBOL_GPL(devm_reset_control_get);

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
