// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * phy-core.c  --  Generic Phy framework.
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 *
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/idr.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

static struct class *phy_class;
static DEFINE_MUTEX(phy_provider_mutex);
static LIST_HEAD(phy_provider_list);
static LIST_HEAD(phys);
static DEFINE_IDA(phy_ida);

static void devm_phy_release(struct device *dev, void *res)
{
	struct phy *phy = *(struct phy **)res;

	phy_put(dev, phy);
}

static void devm_phy_provider_release(struct device *dev, void *res)
{
	struct phy_provider *phy_provider = *(struct phy_provider **)res;

	of_phy_provider_unregister(phy_provider);
}

static void devm_phy_consume(struct device *dev, void *res)
{
	struct phy *phy = *(struct phy **)res;

	phy_destroy(phy);
}

static int devm_phy_match(struct device *dev, void *res, void *match_data)
{
	struct phy **phy = res;

	return *phy == match_data;
}

/**
 * phy_create_lookup() - allocate and register PHY/device association
 * @phy: the phy of the association
 * @con_id: connection ID string on device
 * @dev_id: the device of the association
 *
 * Creates and registers phy_lookup entry.
 */
int phy_create_lookup(struct phy *phy, const char *con_id, const char *dev_id)
{
	struct phy_lookup *pl;

	if (!phy || !dev_id || !con_id)
		return -EINVAL;

	pl = kzalloc(sizeof(*pl), GFP_KERNEL);
	if (!pl)
		return -ENOMEM;

	pl->dev_id = dev_id;
	pl->con_id = con_id;
	pl->phy = phy;

	mutex_lock(&phy_provider_mutex);
	list_add_tail(&pl->node, &phys);
	mutex_unlock(&phy_provider_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(phy_create_lookup);

/**
 * phy_remove_lookup() - find and remove PHY/device association
 * @phy: the phy of the association
 * @con_id: connection ID string on device
 * @dev_id: the device of the association
 *
 * Finds and unregisters phy_lookup entry that was created with
 * phy_create_lookup().
 */
void phy_remove_lookup(struct phy *phy, const char *con_id, const char *dev_id)
{
	struct phy_lookup *pl;

	if (!phy || !dev_id || !con_id)
		return;

	mutex_lock(&phy_provider_mutex);
	list_for_each_entry(pl, &phys, node)
		if (pl->phy == phy && !strcmp(pl->dev_id, dev_id) &&
		    !strcmp(pl->con_id, con_id)) {
			list_del(&pl->node);
			kfree(pl);
			break;
		}
	mutex_unlock(&phy_provider_mutex);
}
EXPORT_SYMBOL_GPL(phy_remove_lookup);

static struct phy *phy_find(struct device *dev, const char *con_id)
{
	const char *dev_id = dev_name(dev);
	struct phy_lookup *p, *pl = NULL;

	mutex_lock(&phy_provider_mutex);
	list_for_each_entry(p, &phys, node)
		if (!strcmp(p->dev_id, dev_id) && !strcmp(p->con_id, con_id)) {
			pl = p;
			break;
		}
	mutex_unlock(&phy_provider_mutex);

	return pl ? pl->phy : ERR_PTR(-ENODEV);
}

static struct phy_provider *of_phy_provider_lookup(struct device_node *node)
{
	struct phy_provider *phy_provider;
	struct device_node *child;

	list_for_each_entry(phy_provider, &phy_provider_list, list) {
		if (phy_provider->dev->of_node == node)
			return phy_provider;

		for_each_child_of_node(phy_provider->children, child)
			if (child == node)
				return phy_provider;
	}

	return ERR_PTR(-EPROBE_DEFER);
}

int phy_pm_runtime_get(struct phy *phy)
{
	int ret;

	if (!phy)
		return 0;

	if (!pm_runtime_enabled(&phy->dev))
		return -ENOTSUPP;

	ret = pm_runtime_get(&phy->dev);
	if (ret < 0 && ret != -EINPROGRESS)
		pm_runtime_put_noidle(&phy->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_pm_runtime_get);

int phy_pm_runtime_get_sync(struct phy *phy)
{
	int ret;

	if (!phy)
		return 0;

	if (!pm_runtime_enabled(&phy->dev))
		return -ENOTSUPP;

	ret = pm_runtime_get_sync(&phy->dev);
	if (ret < 0)
		pm_runtime_put_sync(&phy->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_pm_runtime_get_sync);

int phy_pm_runtime_put(struct phy *phy)
{
	if (!phy)
		return 0;

	if (!pm_runtime_enabled(&phy->dev))
		return -ENOTSUPP;

	return pm_runtime_put(&phy->dev);
}
EXPORT_SYMBOL_GPL(phy_pm_runtime_put);

int phy_pm_runtime_put_sync(struct phy *phy)
{
	if (!phy)
		return 0;

	if (!pm_runtime_enabled(&phy->dev))
		return -ENOTSUPP;

	return pm_runtime_put_sync(&phy->dev);
}
EXPORT_SYMBOL_GPL(phy_pm_runtime_put_sync);

void phy_pm_runtime_allow(struct phy *phy)
{
	if (!phy)
		return;

	if (!pm_runtime_enabled(&phy->dev))
		return;

	pm_runtime_allow(&phy->dev);
}
EXPORT_SYMBOL_GPL(phy_pm_runtime_allow);

void phy_pm_runtime_forbid(struct phy *phy)
{
	if (!phy)
		return;

	if (!pm_runtime_enabled(&phy->dev))
		return;

	pm_runtime_forbid(&phy->dev);
}
EXPORT_SYMBOL_GPL(phy_pm_runtime_forbid);

/**
 * phy_init - phy internal initialization before phy operation
 * @phy: the phy returned by phy_get()
 *
 * Used to allow phy's driver to perform phy internal initialization,
 * such as PLL block powering, clock initialization or anything that's
 * is required by the phy to perform the start of operation.
 * Must be called before phy_power_on().
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int phy_init(struct phy *phy)
{
	int ret;

	if (!phy)
		return 0;

	ret = phy_pm_runtime_get_sync(phy);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;
	ret = 0; /* Override possible ret == -ENOTSUPP */

	mutex_lock(&phy->mutex);
	if (phy->power_count > phy->init_count)
		dev_warn(&phy->dev, "phy_power_on was called before phy_init\n");

	if (phy->init_count == 0 && phy->ops->init) {
		ret = phy->ops->init(phy);
		if (ret < 0) {
			dev_err(&phy->dev, "phy init failed --> %d\n", ret);
			goto out;
		}
	}
	++phy->init_count;

out:
	mutex_unlock(&phy->mutex);
	phy_pm_runtime_put(phy);
	return ret;
}
EXPORT_SYMBOL_GPL(phy_init);

/**
 * phy_exit - Phy internal un-initialization
 * @phy: the phy returned by phy_get()
 *
 * Must be called after phy_power_off().
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int phy_exit(struct phy *phy)
{
	int ret;

	if (!phy)
		return 0;

	ret = phy_pm_runtime_get_sync(phy);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;
	ret = 0; /* Override possible ret == -ENOTSUPP */

	mutex_lock(&phy->mutex);
	if (phy->init_count == 1 && phy->ops->exit) {
		ret = phy->ops->exit(phy);
		if (ret < 0) {
			dev_err(&phy->dev, "phy exit failed --> %d\n", ret);
			goto out;
		}
	}
	--phy->init_count;

out:
	mutex_unlock(&phy->mutex);
	phy_pm_runtime_put(phy);
	return ret;
}
EXPORT_SYMBOL_GPL(phy_exit);

/**
 * phy_power_on - Enable the phy and enter proper operation
 * @phy: the phy returned by phy_get()
 *
 * Must be called after phy_init().
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int phy_power_on(struct phy *phy)
{
	int ret = 0;

	if (!phy)
		goto out;

	if (phy->pwr) {
		ret = regulator_enable(phy->pwr);
		if (ret)
			goto out;
	}

	ret = phy_pm_runtime_get_sync(phy);
	if (ret < 0 && ret != -ENOTSUPP)
		goto err_pm_sync;

	ret = 0; /* Override possible ret == -ENOTSUPP */

	mutex_lock(&phy->mutex);
	if (phy->power_count == 0 && phy->ops->power_on) {
		ret = phy->ops->power_on(phy);
		if (ret < 0) {
			dev_err(&phy->dev, "phy poweron failed --> %d\n", ret);
			goto err_pwr_on;
		}
	}
	++phy->power_count;
	mutex_unlock(&phy->mutex);
	return 0;

err_pwr_on:
	mutex_unlock(&phy->mutex);
	phy_pm_runtime_put_sync(phy);
err_pm_sync:
	if (phy->pwr)
		regulator_disable(phy->pwr);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(phy_power_on);

/**
 * phy_power_off - Disable the phy.
 * @phy: the phy returned by phy_get()
 *
 * Must be called before phy_exit().
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int phy_power_off(struct phy *phy)
{
	int ret;

	if (!phy)
		return 0;

	mutex_lock(&phy->mutex);
	if (phy->power_count == 1 && phy->ops->power_off) {
		ret =  phy->ops->power_off(phy);
		if (ret < 0) {
			dev_err(&phy->dev, "phy poweroff failed --> %d\n", ret);
			mutex_unlock(&phy->mutex);
			return ret;
		}
	}
	--phy->power_count;
	mutex_unlock(&phy->mutex);
	phy_pm_runtime_put(phy);

	if (phy->pwr)
		regulator_disable(phy->pwr);

	return 0;
}
EXPORT_SYMBOL_GPL(phy_power_off);

int phy_set_mode_ext(struct phy *phy, enum phy_mode mode, int submode)
{
	int ret;

	if (!phy || !phy->ops->set_mode)
		return 0;

	mutex_lock(&phy->mutex);
	ret = phy->ops->set_mode(phy, mode, submode);
	if (!ret)
		phy->attrs.mode = mode;
	mutex_unlock(&phy->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_set_mode_ext);

int phy_set_media(struct phy *phy, enum phy_media media)
{
	int ret;

	if (!phy || !phy->ops->set_media)
		return 0;

	mutex_lock(&phy->mutex);
	ret = phy->ops->set_media(phy, media);
	mutex_unlock(&phy->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_set_media);

int phy_set_speed(struct phy *phy, int speed)
{
	int ret;

	if (!phy || !phy->ops->set_speed)
		return 0;

	mutex_lock(&phy->mutex);
	ret = phy->ops->set_speed(phy, speed);
	mutex_unlock(&phy->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_set_speed);

int phy_reset(struct phy *phy)
{
	int ret;

	if (!phy || !phy->ops->reset)
		return 0;

	ret = phy_pm_runtime_get_sync(phy);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;

	mutex_lock(&phy->mutex);
	ret = phy->ops->reset(phy);
	mutex_unlock(&phy->mutex);

	phy_pm_runtime_put(phy);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_reset);

/**
 * phy_calibrate() - Tunes the phy hw parameters for current configuration
 * @phy: the phy returned by phy_get()
 *
 * Used to calibrate phy hardware, typically by adjusting some parameters in
 * runtime, which are otherwise lost after host controller reset and cannot
 * be applied in phy_init() or phy_power_on().
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int phy_calibrate(struct phy *phy)
{
	int ret;

	if (!phy || !phy->ops->calibrate)
		return 0;

	mutex_lock(&phy->mutex);
	ret = phy->ops->calibrate(phy);
	mutex_unlock(&phy->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_calibrate);

/**
 * phy_configure() - Changes the phy parameters
 * @phy: the phy returned by phy_get()
 * @opts: New configuration to apply
 *
 * Used to change the PHY parameters. phy_init() must have been called
 * on the phy. The configuration will be applied on the current phy
 * mode, that can be changed using phy_set_mode().
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int phy_configure(struct phy *phy, union phy_configure_opts *opts)
{
	int ret;

	if (!phy)
		return -EINVAL;

	if (!phy->ops->configure)
		return -EOPNOTSUPP;

	mutex_lock(&phy->mutex);
	ret = phy->ops->configure(phy, opts);
	mutex_unlock(&phy->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_configure);

/**
 * phy_validate() - Checks the phy parameters
 * @phy: the phy returned by phy_get()
 * @mode: phy_mode the configuration is applicable to.
 * @submode: PHY submode the configuration is applicable to.
 * @opts: Configuration to check
 *
 * Used to check that the current set of parameters can be handled by
 * the phy. Implementations are free to tune the parameters passed as
 * arguments if needed by some implementation detail or
 * constraints. It will not change any actual configuration of the
 * PHY, so calling it as many times as deemed fit will have no side
 * effect.
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int phy_validate(struct phy *phy, enum phy_mode mode, int submode,
		 union phy_configure_opts *opts)
{
	int ret;

	if (!phy)
		return -EINVAL;

	if (!phy->ops->validate)
		return -EOPNOTSUPP;

	mutex_lock(&phy->mutex);
	ret = phy->ops->validate(phy, mode, submode, opts);
	mutex_unlock(&phy->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_validate);

/**
 * _of_phy_get() - lookup and obtain a reference to a phy by phandle
 * @np: device_node for which to get the phy
 * @index: the index of the phy
 *
 * Returns the phy associated with the given phandle value,
 * after getting a refcount to it or -ENODEV if there is no such phy or
 * -EPROBE_DEFER if there is a phandle to the phy, but the device is
 * not yet loaded. This function uses of_xlate call back function provided
 * while registering the phy_provider to find the phy instance.
 */
static struct phy *_of_phy_get(struct device_node *np, int index)
{
	int ret;
	struct phy_provider *phy_provider;
	struct phy *phy = NULL;
	struct of_phandle_args args;

	ret = of_parse_phandle_with_args(np, "phys", "#phy-cells",
		index, &args);
	if (ret)
		return ERR_PTR(-ENODEV);

	/* This phy type handled by the usb-phy subsystem for now */
	if (of_device_is_compatible(args.np, "usb-nop-xceiv"))
		return ERR_PTR(-ENODEV);

	mutex_lock(&phy_provider_mutex);
	phy_provider = of_phy_provider_lookup(args.np);
	if (IS_ERR(phy_provider) || !try_module_get(phy_provider->owner)) {
		phy = ERR_PTR(-EPROBE_DEFER);
		goto out_unlock;
	}

	if (!of_device_is_available(args.np)) {
		dev_warn(phy_provider->dev, "Requested PHY is disabled\n");
		phy = ERR_PTR(-ENODEV);
		goto out_put_module;
	}

	phy = phy_provider->of_xlate(phy_provider->dev, &args);

out_put_module:
	module_put(phy_provider->owner);

out_unlock:
	mutex_unlock(&phy_provider_mutex);
	of_node_put(args.np);

	return phy;
}

/**
 * of_phy_get() - lookup and obtain a reference to a phy using a device_node.
 * @np: device_node for which to get the phy
 * @con_id: name of the phy from device's point of view
 *
 * Returns the phy driver, after getting a refcount to it; or
 * -ENODEV if there is no such phy. The caller is responsible for
 * calling phy_put() to release that count.
 */
struct phy *of_phy_get(struct device_node *np, const char *con_id)
{
	struct phy *phy = NULL;
	int index = 0;

	if (con_id)
		index = of_property_match_string(np, "phy-names", con_id);

	phy = _of_phy_get(np, index);
	if (IS_ERR(phy))
		return phy;

	if (!try_module_get(phy->ops->owner))
		return ERR_PTR(-EPROBE_DEFER);

	get_device(&phy->dev);

	return phy;
}
EXPORT_SYMBOL_GPL(of_phy_get);

/**
 * of_phy_put() - release the PHY
 * @phy: the phy returned by of_phy_get()
 *
 * Releases a refcount the caller received from of_phy_get().
 */
void of_phy_put(struct phy *phy)
{
	if (!phy || IS_ERR(phy))
		return;

	mutex_lock(&phy->mutex);
	if (phy->ops->release)
		phy->ops->release(phy);
	mutex_unlock(&phy->mutex);

	module_put(phy->ops->owner);
	put_device(&phy->dev);
}
EXPORT_SYMBOL_GPL(of_phy_put);

/**
 * phy_put() - release the PHY
 * @dev: device that wants to release this phy
 * @phy: the phy returned by phy_get()
 *
 * Releases a refcount the caller received from phy_get().
 */
void phy_put(struct device *dev, struct phy *phy)
{
	device_link_remove(dev, &phy->dev);
	of_phy_put(phy);
}
EXPORT_SYMBOL_GPL(phy_put);

/**
 * devm_phy_put() - release the PHY
 * @dev: device that wants to release this phy
 * @phy: the phy returned by devm_phy_get()
 *
 * destroys the devres associated with this phy and invokes phy_put
 * to release the phy.
 */
void devm_phy_put(struct device *dev, struct phy *phy)
{
	int r;

	if (!phy)
		return;

	r = devres_destroy(dev, devm_phy_release, devm_phy_match, phy);
	dev_WARN_ONCE(dev, r, "couldn't find PHY resource\n");
}
EXPORT_SYMBOL_GPL(devm_phy_put);

/**
 * of_phy_simple_xlate() - returns the phy instance from phy provider
 * @dev: the PHY provider device
 * @args: of_phandle_args (not used here)
 *
 * Intended to be used by phy provider for the common case where #phy-cells is
 * 0. For other cases where #phy-cells is greater than '0', the phy provider
 * should provide a custom of_xlate function that reads the *args* and returns
 * the appropriate phy.
 */
struct phy *of_phy_simple_xlate(struct device *dev, struct of_phandle_args
	*args)
{
	struct phy *phy;
	struct class_dev_iter iter;

	class_dev_iter_init(&iter, phy_class, NULL, NULL);
	while ((dev = class_dev_iter_next(&iter))) {
		phy = to_phy(dev);
		if (args->np != phy->dev.of_node)
			continue;

		class_dev_iter_exit(&iter);
		return phy;
	}

	class_dev_iter_exit(&iter);
	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_GPL(of_phy_simple_xlate);

/**
 * phy_get() - lookup and obtain a reference to a phy.
 * @dev: device that requests this phy
 * @string: the phy name as given in the dt data or the name of the controller
 * port for non-dt case
 *
 * Returns the phy driver, after getting a refcount to it; or
 * -ENODEV if there is no such phy.  The caller is responsible for
 * calling phy_put() to release that count.
 */
struct phy *phy_get(struct device *dev, const char *string)
{
	int index = 0;
	struct phy *phy;
	struct device_link *link;

	if (dev->of_node) {
		if (string)
			index = of_property_match_string(dev->of_node, "phy-names",
				string);
		else
			index = 0;
		phy = _of_phy_get(dev->of_node, index);
	} else {
		if (string == NULL) {
			dev_WARN(dev, "missing string\n");
			return ERR_PTR(-EINVAL);
		}
		phy = phy_find(dev, string);
	}
	if (IS_ERR(phy))
		return phy;

	if (!try_module_get(phy->ops->owner))
		return ERR_PTR(-EPROBE_DEFER);

	get_device(&phy->dev);

	link = device_link_add(dev, &phy->dev, DL_FLAG_STATELESS);
	if (!link)
		dev_dbg(dev, "failed to create device link to %s\n",
			dev_name(phy->dev.parent));

	return phy;
}
EXPORT_SYMBOL_GPL(phy_get);

/**
 * devm_phy_get() - lookup and obtain a reference to a phy.
 * @dev: device that requests this phy
 * @string: the phy name as given in the dt data or phy device name
 * for non-dt case
 *
 * Gets the phy using phy_get(), and associates a device with it using
 * devres. On driver detach, release function is invoked on the devres data,
 * then, devres data is freed.
 */
struct phy *devm_phy_get(struct device *dev, const char *string)
{
	struct phy **ptr, *phy;

	ptr = devres_alloc(devm_phy_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	phy = phy_get(dev, string);
	if (!IS_ERR(phy)) {
		*ptr = phy;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return phy;
}
EXPORT_SYMBOL_GPL(devm_phy_get);

/**
 * devm_phy_optional_get() - lookup and obtain a reference to an optional phy.
 * @dev: device that requests this phy
 * @string: the phy name as given in the dt data or phy device name
 * for non-dt case
 *
 * Gets the phy using phy_get(), and associates a device with it using
 * devres. On driver detach, release function is invoked on the devres
 * data, then, devres data is freed. This differs to devm_phy_get() in
 * that if the phy does not exist, it is not considered an error and
 * -ENODEV will not be returned. Instead the NULL phy is returned,
 * which can be passed to all other phy consumer calls.
 */
struct phy *devm_phy_optional_get(struct device *dev, const char *string)
{
	struct phy *phy = devm_phy_get(dev, string);

	if (PTR_ERR(phy) == -ENODEV)
		phy = NULL;

	return phy;
}
EXPORT_SYMBOL_GPL(devm_phy_optional_get);

/**
 * devm_of_phy_get() - lookup and obtain a reference to a phy.
 * @dev: device that requests this phy
 * @np: node containing the phy
 * @con_id: name of the phy from device's point of view
 *
 * Gets the phy using of_phy_get(), and associates a device with it using
 * devres. On driver detach, release function is invoked on the devres data,
 * then, devres data is freed.
 */
struct phy *devm_of_phy_get(struct device *dev, struct device_node *np,
			    const char *con_id)
{
	struct phy **ptr, *phy;
	struct device_link *link;

	ptr = devres_alloc(devm_phy_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	phy = of_phy_get(np, con_id);
	if (!IS_ERR(phy)) {
		*ptr = phy;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
		return phy;
	}

	link = device_link_add(dev, &phy->dev, DL_FLAG_STATELESS);
	if (!link)
		dev_dbg(dev, "failed to create device link to %s\n",
			dev_name(phy->dev.parent));

	return phy;
}
EXPORT_SYMBOL_GPL(devm_of_phy_get);

/**
 * devm_of_phy_optional_get() - lookup and obtain a reference to an optional
 * phy.
 * @dev: device that requests this phy
 * @np: node containing the phy
 * @con_id: name of the phy from device's point of view
 *
 * Gets the phy using of_phy_get(), and associates a device with it using
 * devres. On driver detach, release function is invoked on the devres data,
 * then, devres data is freed.  This differs to devm_of_phy_get() in
 * that if the phy does not exist, it is not considered an error and
 * -ENODEV will not be returned. Instead the NULL phy is returned,
 * which can be passed to all other phy consumer calls.
 */
struct phy *devm_of_phy_optional_get(struct device *dev, struct device_node *np,
				     const char *con_id)
{
	struct phy *phy = devm_of_phy_get(dev, np, con_id);

	if (PTR_ERR(phy) == -ENODEV)
		phy = NULL;

	if (IS_ERR(phy))
		dev_err_probe(dev, PTR_ERR(phy), "failed to get PHY %pOF:%s",
			      np, con_id);

	return phy;
}
EXPORT_SYMBOL_GPL(devm_of_phy_optional_get);

/**
 * devm_of_phy_get_by_index() - lookup and obtain a reference to a phy by index.
 * @dev: device that requests this phy
 * @np: node containing the phy
 * @index: index of the phy
 *
 * Gets the phy using _of_phy_get(), then gets a refcount to it,
 * and associates a device with it using devres. On driver detach,
 * release function is invoked on the devres data,
 * then, devres data is freed.
 *
 */
struct phy *devm_of_phy_get_by_index(struct device *dev, struct device_node *np,
				     int index)
{
	struct phy **ptr, *phy;
	struct device_link *link;

	ptr = devres_alloc(devm_phy_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	phy = _of_phy_get(np, index);
	if (IS_ERR(phy)) {
		devres_free(ptr);
		return phy;
	}

	if (!try_module_get(phy->ops->owner)) {
		devres_free(ptr);
		return ERR_PTR(-EPROBE_DEFER);
	}

	get_device(&phy->dev);

	*ptr = phy;
	devres_add(dev, ptr);

	link = device_link_add(dev, &phy->dev, DL_FLAG_STATELESS);
	if (!link)
		dev_dbg(dev, "failed to create device link to %s\n",
			dev_name(phy->dev.parent));

	return phy;
}
EXPORT_SYMBOL_GPL(devm_of_phy_get_by_index);

/**
 * phy_create() - create a new phy
 * @dev: device that is creating the new phy
 * @node: device node of the phy
 * @ops: function pointers for performing phy operations
 *
 * Called to create a phy using phy framework.
 */
struct phy *phy_create(struct device *dev, struct device_node *node,
		       const struct phy_ops *ops)
{
	int ret;
	int id;
	struct phy *phy;

	if (WARN_ON(!dev))
		return ERR_PTR(-EINVAL);

	phy = kzalloc(sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return ERR_PTR(-ENOMEM);

	id = ida_simple_get(&phy_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		dev_err(dev, "unable to get id\n");
		ret = id;
		goto free_phy;
	}

	device_initialize(&phy->dev);
	mutex_init(&phy->mutex);

	phy->dev.class = phy_class;
	phy->dev.parent = dev;
	phy->dev.of_node = node ?: dev->of_node;
	phy->id = id;
	phy->ops = ops;

	ret = dev_set_name(&phy->dev, "phy-%s.%d", dev_name(dev), id);
	if (ret)
		goto put_dev;

	/* phy-supply */
	phy->pwr = regulator_get_optional(&phy->dev, "phy");
	if (IS_ERR(phy->pwr)) {
		ret = PTR_ERR(phy->pwr);
		if (ret == -EPROBE_DEFER)
			goto put_dev;

		phy->pwr = NULL;
	}

	ret = device_add(&phy->dev);
	if (ret)
		goto put_dev;

	if (pm_runtime_enabled(dev)) {
		pm_runtime_enable(&phy->dev);
		pm_runtime_no_callbacks(&phy->dev);
	}

	return phy;

put_dev:
	put_device(&phy->dev);  /* calls phy_release() which frees resources */
	return ERR_PTR(ret);

free_phy:
	kfree(phy);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(phy_create);

/**
 * devm_phy_create() - create a new phy
 * @dev: device that is creating the new phy
 * @node: device node of the phy
 * @ops: function pointers for performing phy operations
 *
 * Creates a new PHY device adding it to the PHY class.
 * While at that, it also associates the device with the phy using devres.
 * On driver detach, release function is invoked on the devres data,
 * then, devres data is freed.
 */
struct phy *devm_phy_create(struct device *dev, struct device_node *node,
			    const struct phy_ops *ops)
{
	struct phy **ptr, *phy;

	ptr = devres_alloc(devm_phy_consume, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	phy = phy_create(dev, node, ops);
	if (!IS_ERR(phy)) {
		*ptr = phy;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return phy;
}
EXPORT_SYMBOL_GPL(devm_phy_create);

/**
 * phy_destroy() - destroy the phy
 * @phy: the phy to be destroyed
 *
 * Called to destroy the phy.
 */
void phy_destroy(struct phy *phy)
{
	pm_runtime_disable(&phy->dev);
	device_unregister(&phy->dev);
}
EXPORT_SYMBOL_GPL(phy_destroy);

/**
 * devm_phy_destroy() - destroy the PHY
 * @dev: device that wants to release this phy
 * @phy: the phy returned by devm_phy_get()
 *
 * destroys the devres associated with this phy and invokes phy_destroy
 * to destroy the phy.
 */
void devm_phy_destroy(struct device *dev, struct phy *phy)
{
	int r;

	r = devres_destroy(dev, devm_phy_consume, devm_phy_match, phy);
	dev_WARN_ONCE(dev, r, "couldn't find PHY resource\n");
}
EXPORT_SYMBOL_GPL(devm_phy_destroy);

/**
 * __of_phy_provider_register() - create/register phy provider with the framework
 * @dev: struct device of the phy provider
 * @children: device node containing children (if different from dev->of_node)
 * @owner: the module owner containing of_xlate
 * @of_xlate: function pointer to obtain phy instance from phy provider
 *
 * Creates struct phy_provider from dev and of_xlate function pointer.
 * This is used in the case of dt boot for finding the phy instance from
 * phy provider.
 *
 * If the PHY provider doesn't nest children directly but uses a separate
 * child node to contain the individual children, the @children parameter
 * can be used to override the default. If NULL, the default (dev->of_node)
 * will be used. If non-NULL, the device node must be a child (or further
 * descendant) of dev->of_node. Otherwise an ERR_PTR()-encoded -EINVAL
 * error code is returned.
 */
struct phy_provider *__of_phy_provider_register(struct device *dev,
	struct device_node *children, struct module *owner,
	struct phy * (*of_xlate)(struct device *dev,
				 struct of_phandle_args *args))
{
	struct phy_provider *phy_provider;

	/*
	 * If specified, the device node containing the children must itself
	 * be the provider's device node or a child (or further descendant)
	 * thereof.
	 */
	if (children) {
		struct device_node *parent = of_node_get(children), *next;

		while (parent) {
			if (parent == dev->of_node)
				break;

			next = of_get_parent(parent);
			of_node_put(parent);
			parent = next;
		}

		if (!parent)
			return ERR_PTR(-EINVAL);

		of_node_put(parent);
	} else {
		children = dev->of_node;
	}

	phy_provider = kzalloc(sizeof(*phy_provider), GFP_KERNEL);
	if (!phy_provider)
		return ERR_PTR(-ENOMEM);

	phy_provider->dev = dev;
	phy_provider->children = of_node_get(children);
	phy_provider->owner = owner;
	phy_provider->of_xlate = of_xlate;

	mutex_lock(&phy_provider_mutex);
	list_add_tail(&phy_provider->list, &phy_provider_list);
	mutex_unlock(&phy_provider_mutex);

	return phy_provider;
}
EXPORT_SYMBOL_GPL(__of_phy_provider_register);

/**
 * __devm_of_phy_provider_register() - create/register phy provider with the
 * framework
 * @dev: struct device of the phy provider
 * @children: device node containing children (if different from dev->of_node)
 * @owner: the module owner containing of_xlate
 * @of_xlate: function pointer to obtain phy instance from phy provider
 *
 * Creates struct phy_provider from dev and of_xlate function pointer.
 * This is used in the case of dt boot for finding the phy instance from
 * phy provider. While at that, it also associates the device with the
 * phy provider using devres. On driver detach, release function is invoked
 * on the devres data, then, devres data is freed.
 */
struct phy_provider *__devm_of_phy_provider_register(struct device *dev,
	struct device_node *children, struct module *owner,
	struct phy * (*of_xlate)(struct device *dev,
				 struct of_phandle_args *args))
{
	struct phy_provider **ptr, *phy_provider;

	ptr = devres_alloc(devm_phy_provider_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	phy_provider = __of_phy_provider_register(dev, children, owner,
						  of_xlate);
	if (!IS_ERR(phy_provider)) {
		*ptr = phy_provider;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return phy_provider;
}
EXPORT_SYMBOL_GPL(__devm_of_phy_provider_register);

/**
 * of_phy_provider_unregister() - unregister phy provider from the framework
 * @phy_provider: phy provider returned by of_phy_provider_register()
 *
 * Removes the phy_provider created using of_phy_provider_register().
 */
void of_phy_provider_unregister(struct phy_provider *phy_provider)
{
	if (IS_ERR(phy_provider))
		return;

	mutex_lock(&phy_provider_mutex);
	list_del(&phy_provider->list);
	of_node_put(phy_provider->children);
	kfree(phy_provider);
	mutex_unlock(&phy_provider_mutex);
}
EXPORT_SYMBOL_GPL(of_phy_provider_unregister);

/**
 * devm_of_phy_provider_unregister() - remove phy provider from the framework
 * @dev: struct device of the phy provider
 * @phy_provider: phy provider returned by of_phy_provider_register()
 *
 * destroys the devres associated with this phy provider and invokes
 * of_phy_provider_unregister to unregister the phy provider.
 */
void devm_of_phy_provider_unregister(struct device *dev,
	struct phy_provider *phy_provider)
{
	int r;

	r = devres_destroy(dev, devm_phy_provider_release, devm_phy_match,
		phy_provider);
	dev_WARN_ONCE(dev, r, "couldn't find PHY provider device resource\n");
}
EXPORT_SYMBOL_GPL(devm_of_phy_provider_unregister);

/**
 * phy_release() - release the phy
 * @dev: the dev member within phy
 *
 * When the last reference to the device is removed, it is called
 * from the embedded kobject as release method.
 */
static void phy_release(struct device *dev)
{
	struct phy *phy;

	phy = to_phy(dev);
	dev_vdbg(dev, "releasing '%s'\n", dev_name(dev));
	regulator_put(phy->pwr);
	ida_simple_remove(&phy_ida, phy->id);
	kfree(phy);
}

static int __init phy_core_init(void)
{
	phy_class = class_create(THIS_MODULE, "phy");
	if (IS_ERR(phy_class)) {
		pr_err("failed to create phy class --> %ld\n",
			PTR_ERR(phy_class));
		return PTR_ERR(phy_class);
	}

	phy_class->dev_release = phy_release;

	return 0;
}
device_initcall(phy_core_init);
