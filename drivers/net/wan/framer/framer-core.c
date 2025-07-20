// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic Framer framework.
 *
 * Copyright 2023 CS GROUP France
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */

#include <linux/device.h>
#include <linux/framer/framer.h>
#include <linux/framer/framer-provider.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

static void framer_release(struct device *dev);
static const struct class framer_class = {
	.name = "framer",
	.dev_release = framer_release,
};

static DEFINE_MUTEX(framer_provider_mutex);
static LIST_HEAD(framer_provider_list);
static DEFINE_IDA(framer_ida);

#define dev_to_framer(a)	(container_of((a), struct framer, dev))

int framer_pm_runtime_get(struct framer *framer)
{
	int ret;

	if (!pm_runtime_enabled(&framer->dev))
		return -EOPNOTSUPP;

	ret = pm_runtime_get(&framer->dev);
	if (ret < 0 && ret != -EINPROGRESS)
		pm_runtime_put_noidle(&framer->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(framer_pm_runtime_get);

int framer_pm_runtime_get_sync(struct framer *framer)
{
	int ret;

	if (!pm_runtime_enabled(&framer->dev))
		return -EOPNOTSUPP;

	ret = pm_runtime_get_sync(&framer->dev);
	if (ret < 0)
		pm_runtime_put_sync(&framer->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(framer_pm_runtime_get_sync);

int framer_pm_runtime_put(struct framer *framer)
{
	if (!pm_runtime_enabled(&framer->dev))
		return -EOPNOTSUPP;

	return pm_runtime_put(&framer->dev);
}
EXPORT_SYMBOL_GPL(framer_pm_runtime_put);

int framer_pm_runtime_put_sync(struct framer *framer)
{
	if (!pm_runtime_enabled(&framer->dev))
		return -EOPNOTSUPP;

	return pm_runtime_put_sync(&framer->dev);
}
EXPORT_SYMBOL_GPL(framer_pm_runtime_put_sync);

/**
 * framer_init - framer internal initialization before framer operation
 * @framer: the framer returned by framer_get()
 *
 * Used to allow framer's driver to perform framer internal initialization,
 * such as PLL block powering, clock initialization or anything that's
 * is required by the framer to perform the start of operation.
 * Must be called before framer_power_on().
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int framer_init(struct framer *framer)
{
	bool start_polling = false;
	int ret;

	ret = framer_pm_runtime_get_sync(framer);
	if (ret < 0 && ret != -EOPNOTSUPP)
		return ret;
	ret = 0; /* Override possible ret == -EOPNOTSUPP */

	mutex_lock(&framer->mutex);
	if (framer->power_count > framer->init_count)
		dev_warn(&framer->dev, "framer_power_on was called before framer init\n");

	if (framer->init_count == 0) {
		if (framer->ops->init) {
			ret = framer->ops->init(framer);
			if (ret < 0) {
				dev_err(&framer->dev, "framer init failed --> %d\n", ret);
				goto out;
			}
		}
		if (framer->ops->flags & FRAMER_FLAG_POLL_STATUS)
			start_polling = true;
	}
	++framer->init_count;

out:
	mutex_unlock(&framer->mutex);

	if (!ret && start_polling) {
		ret = framer_get_status(framer, &framer->prev_status);
		if (ret < 0) {
			dev_warn(&framer->dev, "framer get status failed --> %d\n", ret);
			/* Will be retried on polling_work */
			ret = 0;
		}
		queue_delayed_work(system_power_efficient_wq, &framer->polling_work, 1 * HZ);
	}

	framer_pm_runtime_put(framer);
	return ret;
}
EXPORT_SYMBOL_GPL(framer_init);

/**
 * framer_exit - Framer internal un-initialization
 * @framer: the framer returned by framer_get()
 *
 * Must be called after framer_power_off().
 */
int framer_exit(struct framer *framer)
{
	int ret;

	ret = framer_pm_runtime_get_sync(framer);
	if (ret < 0 && ret != -EOPNOTSUPP)
		return ret;
	ret = 0; /* Override possible ret == -EOPNOTSUPP */

	mutex_lock(&framer->mutex);
	--framer->init_count;
	if (framer->init_count == 0) {
		if (framer->ops->flags & FRAMER_FLAG_POLL_STATUS) {
			mutex_unlock(&framer->mutex);
			cancel_delayed_work_sync(&framer->polling_work);
			mutex_lock(&framer->mutex);
		}

		if (framer->ops->exit)
			framer->ops->exit(framer);
	}

	mutex_unlock(&framer->mutex);
	framer_pm_runtime_put(framer);
	return ret;
}
EXPORT_SYMBOL_GPL(framer_exit);

/**
 * framer_power_on - Enable the framer and enter proper operation
 * @framer: the framer returned by framer_get()
 *
 * Must be called after framer_init().
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int framer_power_on(struct framer *framer)
{
	int ret;

	if (framer->pwr) {
		ret = regulator_enable(framer->pwr);
		if (ret)
			return ret;
	}

	ret = framer_pm_runtime_get_sync(framer);
	if (ret < 0 && ret != -EOPNOTSUPP)
		goto err_pm_sync;

	mutex_lock(&framer->mutex);
	if (framer->power_count == 0 && framer->ops->power_on) {
		ret = framer->ops->power_on(framer);
		if (ret < 0) {
			dev_err(&framer->dev, "framer poweron failed --> %d\n", ret);
			goto err_pwr_on;
		}
	}
	++framer->power_count;
	mutex_unlock(&framer->mutex);
	return 0;

err_pwr_on:
	mutex_unlock(&framer->mutex);
	framer_pm_runtime_put_sync(framer);
err_pm_sync:
	if (framer->pwr)
		regulator_disable(framer->pwr);
	return ret;
}
EXPORT_SYMBOL_GPL(framer_power_on);

/**
 * framer_power_off - Disable the framer.
 * @framer: the framer returned by framer_get()
 *
 * Must be called before framer_exit().
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int framer_power_off(struct framer *framer)
{
	int ret;

	mutex_lock(&framer->mutex);
	if (framer->power_count == 1 && framer->ops->power_off) {
		ret = framer->ops->power_off(framer);
		if (ret < 0) {
			dev_err(&framer->dev, "framer poweroff failed --> %d\n", ret);
			mutex_unlock(&framer->mutex);
			return ret;
		}
	}
	--framer->power_count;
	mutex_unlock(&framer->mutex);
	framer_pm_runtime_put(framer);

	if (framer->pwr)
		regulator_disable(framer->pwr);

	return 0;
}
EXPORT_SYMBOL_GPL(framer_power_off);

/**
 * framer_get_status() - Gets the framer status
 * @framer: the framer returned by framer_get()
 * @status: the status to retrieve
 *
 * Used to get the framer status. framer_init() must have been called
 * on the framer.
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int framer_get_status(struct framer *framer, struct framer_status *status)
{
	int ret;

	if (!framer->ops->get_status)
		return -EOPNOTSUPP;

	/* Be sure to have known values (struct padding and future extensions) */
	memset(status, 0, sizeof(*status));

	mutex_lock(&framer->mutex);
	ret = framer->ops->get_status(framer, status);
	mutex_unlock(&framer->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(framer_get_status);

/**
 * framer_set_config() - Sets the framer configuration
 * @framer: the framer returned by framer_get()
 * @config: the configuration to set
 *
 * Used to set the framer configuration. framer_init() must have been called
 * on the framer.
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int framer_set_config(struct framer *framer, const struct framer_config *config)
{
	int ret;

	if (!framer->ops->set_config)
		return -EOPNOTSUPP;

	mutex_lock(&framer->mutex);
	ret = framer->ops->set_config(framer, config);
	mutex_unlock(&framer->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(framer_set_config);

/**
 * framer_get_config() - Gets the framer configuration
 * @framer: the framer returned by framer_get()
 * @config: the configuration to retrieve
 *
 * Used to get the framer configuration. framer_init() must have been called
 * on the framer.
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int framer_get_config(struct framer *framer, struct framer_config *config)
{
	int ret;

	if (!framer->ops->get_config)
		return -EOPNOTSUPP;

	mutex_lock(&framer->mutex);
	ret = framer->ops->get_config(framer, config);
	mutex_unlock(&framer->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(framer_get_config);

static void framer_polling_work(struct work_struct *work)
{
	struct framer *framer = container_of(work, struct framer, polling_work.work);
	struct framer_status status;
	int ret;

	ret = framer_get_status(framer, &status);
	if (ret) {
		dev_err(&framer->dev, "polling, get status failed (%d)\n", ret);
		goto end;
	}
	if (memcmp(&framer->prev_status, &status, sizeof(status))) {
		blocking_notifier_call_chain(&framer->notifier_list,
					     FRAMER_EVENT_STATUS, NULL);
		memcpy(&framer->prev_status, &status, sizeof(status));
	}

end:
	/* Re-schedule task in 1 sec */
	queue_delayed_work(system_power_efficient_wq, &framer->polling_work, 1 * HZ);
}

/**
 * framer_notifier_register() - Registers a notifier
 * @framer: the framer returned by framer_get()
 * @nb: the notifier block to register
 *
 * Used to register a notifier block on framer events. framer_init() must have
 * been called on the framer.
 * The available framer events are present in enum framer_events.
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int framer_notifier_register(struct framer *framer, struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&framer->notifier_list, nb);
}
EXPORT_SYMBOL_GPL(framer_notifier_register);

/**
 * framer_notifier_unregister() - Unregisters a notifier
 * @framer: the framer returned by framer_get()
 * @nb: the notifier block to unregister
 *
 * Used to unregister a notifier block. framer_init() must have
 * been called on the framer.
 *
 * Return: %0 if successful, a negative error code otherwise
 */
int framer_notifier_unregister(struct framer *framer, struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&framer->notifier_list, nb);
}
EXPORT_SYMBOL_GPL(framer_notifier_unregister);

static struct framer_provider *framer_provider_of_lookup(const struct device_node *node)
{
	struct framer_provider *framer_provider;

	list_for_each_entry(framer_provider, &framer_provider_list, list) {
		if (device_match_of_node(framer_provider->dev, node))
			return framer_provider;
	}

	return ERR_PTR(-EPROBE_DEFER);
}

static struct framer *framer_of_get_from_provider(const struct of_phandle_args *args)
{
	struct framer_provider *framer_provider;
	struct framer *framer;

	mutex_lock(&framer_provider_mutex);
	framer_provider = framer_provider_of_lookup(args->np);
	if (IS_ERR(framer_provider) || !try_module_get(framer_provider->owner)) {
		framer = ERR_PTR(-EPROBE_DEFER);
		goto end;
	}

	framer = framer_provider->of_xlate(framer_provider->dev, args);

	module_put(framer_provider->owner);

end:
	mutex_unlock(&framer_provider_mutex);

	return framer;
}

static struct framer *framer_of_get_byphandle(struct device_node *np, const char *propname,
					      int index)
{
	struct of_phandle_args args;
	struct framer *framer;
	int ret;

	ret = of_parse_phandle_with_optional_args(np, propname, "#framer-cells", index, &args);
	if (ret)
		return ERR_PTR(-ENODEV);

	if (!of_device_is_available(args.np)) {
		framer = ERR_PTR(-ENODEV);
		goto out_node_put;
	}

	framer = framer_of_get_from_provider(&args);

out_node_put:
	of_node_put(args.np);

	return framer;
}

static struct framer *framer_of_get_byparent(struct device_node *np, int index)
{
	struct of_phandle_args args;
	struct framer *framer;

	args.np = of_get_parent(np);
	args.args_count = 1;
	args.args[0] = index;

	while (args.np) {
		framer = framer_of_get_from_provider(&args);
		if (IS_ERR(framer) && PTR_ERR(framer) != -EPROBE_DEFER) {
			args.np = of_get_next_parent(args.np);
			continue;
		}
		of_node_put(args.np);
		return framer;
	}

	return ERR_PTR(-ENODEV);
}

/**
 * framer_get() - lookup and obtain a reference to a framer.
 * @dev: device that requests the framer
 * @con_id: name of the framer from device's point of view
 *
 * Returns the framer driver, after getting a refcount to it; or
 * -ENODEV if there is no such framer. The caller is responsible for
 * calling framer_put() to release that count.
 */
struct framer *framer_get(struct device *dev, const char *con_id)
{
	struct framer *framer = ERR_PTR(-ENODEV);
	struct device_link *link;
	int ret;

	if (dev->of_node) {
		if (con_id)
			framer = framer_of_get_byphandle(dev->of_node, con_id, 0);
		else
			framer = framer_of_get_byparent(dev->of_node, 0);
	}

	if (IS_ERR(framer))
		return framer;

	get_device(&framer->dev);

	if (!try_module_get(framer->ops->owner)) {
		ret = -EPROBE_DEFER;
		goto err_put_device;
	}

	link = device_link_add(dev, &framer->dev, DL_FLAG_STATELESS);
	if (!link) {
		dev_err(dev, "failed to create device_link to %s\n", dev_name(&framer->dev));
		ret = -EPROBE_DEFER;
		goto err_module_put;
	}

	return framer;

err_module_put:
	module_put(framer->ops->owner);
err_put_device:
	put_device(&framer->dev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(framer_get);

/**
 * framer_put() - release the framer
 * @dev: device that wants to release this framer
 * @framer: the framer returned by framer_get()
 *
 * Releases a refcount the caller received from framer_get().
 */
void framer_put(struct device *dev, struct framer *framer)
{
	device_link_remove(dev, &framer->dev);

	module_put(framer->ops->owner);
	put_device(&framer->dev);
}
EXPORT_SYMBOL_GPL(framer_put);

static void devm_framer_put(struct device *dev, void *res)
{
	struct framer *framer = *(struct framer **)res;

	framer_put(dev, framer);
}

/**
 * devm_framer_get() - lookup and obtain a reference to a framer.
 * @dev: device that requests this framer
 * @con_id: name of the framer from device's point of view
 *
 * Gets the framer using framer_get(), and associates a device with it using
 * devres. On driver detach, framer_put() function is invoked on the devres
 * data, then, devres data is freed.
 */
struct framer *devm_framer_get(struct device *dev, const char *con_id)
{
	struct framer **ptr, *framer;

	ptr = devres_alloc(devm_framer_put, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	framer = framer_get(dev, con_id);
	if (!IS_ERR(framer)) {
		*ptr = framer;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
		return framer;
	}

	return framer;
}
EXPORT_SYMBOL_GPL(devm_framer_get);

/**
 * devm_framer_optional_get() - lookup and obtain a reference to an optional
 * framer.
 * @dev: device that requests this framer
 * @con_id: name of the framer from device's point of view
 *
 * Same as devm_framer_get() except that if the framer does not exist, it is not
 * considered an error and -ENODEV will not be returned. Instead the NULL framer
 * is returned.
 */
struct framer *devm_framer_optional_get(struct device *dev, const char *con_id)
{
	struct framer *framer = devm_framer_get(dev, con_id);

	if (PTR_ERR(framer) == -ENODEV)
		framer = NULL;

	return framer;
}
EXPORT_SYMBOL_GPL(devm_framer_optional_get);

static void framer_notify_status_work(struct work_struct *work)
{
	struct framer *framer = container_of(work, struct framer, notify_status_work);

	blocking_notifier_call_chain(&framer->notifier_list, FRAMER_EVENT_STATUS, NULL);
}

void framer_notify_status_change(struct framer *framer)
{
	/* Can be called from atomic context -> just schedule a task to call
	 * blocking notifiers
	 */
	queue_work(system_power_efficient_wq, &framer->notify_status_work);
}
EXPORT_SYMBOL_GPL(framer_notify_status_change);

/**
 * framer_create() - create a new framer
 * @dev: device that is creating the new framer
 * @node: device node of the framer. default to dev->of_node.
 * @ops: function pointers for performing framer operations
 *
 * Called to create a framer using framer framework.
 */
struct framer *framer_create(struct device *dev, struct device_node *node,
			     const struct framer_ops *ops)
{
	struct framer *framer;
	int ret;
	int id;

	/* get_status() is mandatory if the provider ask for polling status */
	if (WARN_ON((ops->flags & FRAMER_FLAG_POLL_STATUS) && !ops->get_status))
		return ERR_PTR(-EINVAL);

	framer = kzalloc(sizeof(*framer), GFP_KERNEL);
	if (!framer)
		return ERR_PTR(-ENOMEM);

	id = ida_alloc(&framer_ida, GFP_KERNEL);
	if (id < 0) {
		dev_err(dev, "unable to get id\n");
		ret = id;
		goto free_framer;
	}

	device_initialize(&framer->dev);
	mutex_init(&framer->mutex);
	INIT_WORK(&framer->notify_status_work, framer_notify_status_work);
	INIT_DELAYED_WORK(&framer->polling_work, framer_polling_work);
	BLOCKING_INIT_NOTIFIER_HEAD(&framer->notifier_list);

	framer->dev.class = &framer_class;
	framer->dev.parent = dev;
	framer->dev.of_node = node ? node : dev->of_node;
	framer->id = id;
	framer->ops = ops;

	ret = dev_set_name(&framer->dev, "framer-%s.%d", dev_name(dev), id);
	if (ret)
		goto put_dev;

	/* framer-supply */
	framer->pwr = regulator_get_optional(&framer->dev, "framer");
	if (IS_ERR(framer->pwr)) {
		ret = PTR_ERR(framer->pwr);
		if (ret == -EPROBE_DEFER)
			goto put_dev;

		framer->pwr = NULL;
	}

	ret = device_add(&framer->dev);
	if (ret)
		goto put_dev;

	if (pm_runtime_enabled(dev)) {
		pm_runtime_enable(&framer->dev);
		pm_runtime_no_callbacks(&framer->dev);
	}

	return framer;

put_dev:
	put_device(&framer->dev);  /* calls framer_release() which frees resources */
	return ERR_PTR(ret);

free_framer:
	kfree(framer);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(framer_create);

/**
 * framer_destroy() - destroy the framer
 * @framer: the framer to be destroyed
 *
 * Called to destroy the framer.
 */
void framer_destroy(struct framer *framer)
{
	/* polling_work should already be stopped but if framer_exit() was not
	 * called (bug), here it's the last time to do that ...
	 */
	cancel_delayed_work_sync(&framer->polling_work);
	cancel_work_sync(&framer->notify_status_work);
	pm_runtime_disable(&framer->dev);
	device_unregister(&framer->dev); /* calls framer_release() which frees resources */
}
EXPORT_SYMBOL_GPL(framer_destroy);

static void devm_framer_destroy(struct device *dev, void *res)
{
	struct framer *framer = *(struct framer **)res;

	framer_destroy(framer);
}

/**
 * devm_framer_create() - create a new framer
 * @dev: device that is creating the new framer
 * @node: device node of the framer
 * @ops: function pointers for performing framer operations
 *
 * Creates a new framer device adding it to the framer class.
 * While at that, it also associates the device with the framer using devres.
 * On driver detach, release function is invoked on the devres data,
 * then, devres data is freed.
 */
struct framer *devm_framer_create(struct device *dev, struct device_node *node,
				  const struct framer_ops *ops)
{
	struct framer **ptr, *framer;

	ptr = devres_alloc(devm_framer_destroy, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	framer = framer_create(dev, node, ops);
	if (!IS_ERR(framer)) {
		*ptr = framer;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return framer;
}
EXPORT_SYMBOL_GPL(devm_framer_create);

/**
 * framer_provider_simple_of_xlate() - returns the framer instance from framer provider
 * @dev: the framer provider device (not used here)
 * @args: of_phandle_args
 *
 * Intended to be used by framer provider for the common case where #framer-cells is
 * 0. For other cases where #framer-cells is greater than '0', the framer provider
 * should provide a custom of_xlate function that reads the *args* and returns
 * the appropriate framer.
 */
struct framer *framer_provider_simple_of_xlate(struct device *dev,
					       const struct of_phandle_args *args)
{
	struct device *target_dev;

	target_dev = class_find_device_by_of_node(&framer_class, args->np);
	if (!target_dev)
		return ERR_PTR(-ENODEV);

	put_device(target_dev);
	return dev_to_framer(target_dev);
}
EXPORT_SYMBOL_GPL(framer_provider_simple_of_xlate);

/**
 * __framer_provider_of_register() - create/register framer provider with the framework
 * @dev: struct device of the framer provider
 * @owner: the module owner containing of_xlate
 * @of_xlate: function pointer to obtain framer instance from framer provider
 *
 * Creates struct framer_provider from dev and of_xlate function pointer.
 * This is used in the case of dt boot for finding the framer instance from
 * framer provider.
 */
struct framer_provider *
__framer_provider_of_register(struct device *dev, struct module *owner,
			      struct framer *(*of_xlate)(struct device *dev,
							 const struct of_phandle_args *args))
{
	struct framer_provider *framer_provider;

	framer_provider = kzalloc(sizeof(*framer_provider), GFP_KERNEL);
	if (!framer_provider)
		return ERR_PTR(-ENOMEM);

	framer_provider->dev = dev;
	framer_provider->owner = owner;
	framer_provider->of_xlate = of_xlate;

	of_node_get(framer_provider->dev->of_node);

	mutex_lock(&framer_provider_mutex);
	list_add_tail(&framer_provider->list, &framer_provider_list);
	mutex_unlock(&framer_provider_mutex);

	return framer_provider;
}
EXPORT_SYMBOL_GPL(__framer_provider_of_register);

/**
 * framer_provider_of_unregister() - unregister framer provider from the framework
 * @framer_provider: framer provider returned by framer_provider_of_register()
 *
 * Removes the framer_provider created using framer_provider_of_register().
 */
void framer_provider_of_unregister(struct framer_provider *framer_provider)
{
	mutex_lock(&framer_provider_mutex);
	list_del(&framer_provider->list);
	mutex_unlock(&framer_provider_mutex);

	of_node_put(framer_provider->dev->of_node);
	kfree(framer_provider);
}
EXPORT_SYMBOL_GPL(framer_provider_of_unregister);

static void devm_framer_provider_of_unregister(struct device *dev, void *res)
{
	struct framer_provider *framer_provider = *(struct framer_provider **)res;

	framer_provider_of_unregister(framer_provider);
}

/**
 * __devm_framer_provider_of_register() - create/register framer provider with
 * the framework
 * @dev: struct device of the framer provider
 * @owner: the module owner containing of_xlate
 * @of_xlate: function pointer to obtain framer instance from framer provider
 *
 * Creates struct framer_provider from dev and of_xlate function pointer.
 * This is used in the case of dt boot for finding the framer instance from
 * framer provider. While at that, it also associates the device with the
 * framer provider using devres. On driver detach, release function is invoked
 * on the devres data, then, devres data is freed.
 */
struct framer_provider *
__devm_framer_provider_of_register(struct device *dev, struct module *owner,
				   struct framer *(*of_xlate)(struct device *dev,
							      const struct of_phandle_args *args))
{
	struct framer_provider **ptr, *framer_provider;

	ptr = devres_alloc(devm_framer_provider_of_unregister, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	framer_provider = __framer_provider_of_register(dev, owner, of_xlate);
	if (!IS_ERR(framer_provider)) {
		*ptr = framer_provider;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return framer_provider;
}
EXPORT_SYMBOL_GPL(__devm_framer_provider_of_register);

/**
 * framer_release() - release the framer
 * @dev: the dev member within framer
 *
 * When the last reference to the device is removed, it is called
 * from the embedded kobject as release method.
 */
static void framer_release(struct device *dev)
{
	struct framer *framer;

	framer = dev_to_framer(dev);
	regulator_put(framer->pwr);
	ida_free(&framer_ida, framer->id);
	kfree(framer);
}

static int __init framer_core_init(void)
{
	return class_register(&framer_class);
}
device_initcall(framer_core_init);
