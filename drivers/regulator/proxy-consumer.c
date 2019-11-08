// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/proxy-consumer.h>

struct proxy_consumer {
	struct list_head	list;
	struct regulator	*reg;
	struct device		*dev;
	bool			enable;
	int			min_uV;
	int			max_uV;
	u32			current_uA;
};

static DEFINE_MUTEX(proxy_consumer_list_lock);
static LIST_HEAD(proxy_consumer_list);
static bool proxy_consumers_removed;

/**
 * regulator_proxy_consumer_add() - conditionally add a proxy consumer for the
 *				    specified regulator and set its boot time
 *				    parameters
 * @dev:		Device pointer of the regulator
 * @node:		Device node pointer of the regulator
 *
 * This function calls regulator_get() after first checking if any proxy
 * consumer properties are present in the 'node' device node.  After that, the
 * voltage, minimum current, and/or the enable state will be set based upon the
 * device node property values.
 *
 * Returns a valid pointer on successfully proxy voting, NULL if no proxy voting
 * is needed, or an ERR_PTR(errno) if an error occurred.
 */
static struct proxy_consumer *regulator_proxy_consumer_add(struct device *dev,
						       struct device_node *node)
{
	struct proxy_consumer *consumer = NULL;
	const char *reg_name = "";
	const char *supply_name;
	u32 voltage[2] = {0};
	int ret;

	if (!dev || !node) {
		pr_err("dev or node is NULL\n");
		return ERR_PTR(-EINVAL);
	}

	/* Return immediately if no proxy consumer properties are specified. */
	if (!of_find_property(node, "qcom,proxy-consumer-enable", NULL)
	    && !of_find_property(node, "qcom,proxy-consumer-voltage", NULL)
	    && !of_find_property(node, "qcom,proxy-consumer-current", NULL))
		return NULL;

	mutex_lock(&proxy_consumer_list_lock);

	/* Do not register new consumers if they cannot be removed later. */
	if (proxy_consumers_removed) {
		ret = -EPERM;
		goto unlock_list;
	}

	if (node->name)
		reg_name = node->name;

	consumer = kzalloc(sizeof(*consumer), GFP_KERNEL);
	if (!consumer) {
		ret = -ENOMEM;
		goto unlock_list;
	}

	consumer->dev = dev;
	consumer->enable
		= of_property_read_bool(node, "qcom,proxy-consumer-enable");
	of_property_read_u32(node, "qcom,proxy-consumer-current",
				&consumer->current_uA);
	ret = of_property_read_u32_array(node, "qcom,proxy-consumer-voltage",
					voltage, 2);
	if (!ret) {
		consumer->min_uV = voltage[0];
		consumer->max_uV = voltage[1];
	}

	dev_dbg(dev, "proxy consumer request: enable=%d, voltage_range=[%d, %d] uV, min_current=%d uA\n",
		consumer->enable, consumer->min_uV, consumer->max_uV,
		consumer->current_uA);

	supply_name = "proxy";
	of_property_read_string(node, "qcom,proxy-consumer-name", &supply_name);

	consumer->reg = regulator_get(dev, supply_name);
	if (IS_ERR_OR_NULL(consumer->reg)) {
		ret = PTR_ERR(consumer->reg);
		pr_err("regulator_get(%s) failed for %s, ret=%d\n", supply_name,
			reg_name, ret);
		goto free_consumer;
	}

	if (consumer->max_uV > 0 && consumer->min_uV <= consumer->max_uV) {
		ret = regulator_set_voltage(consumer->reg, consumer->min_uV,
						consumer->max_uV);
		if (ret) {
			pr_err("regulator_set_voltage %s failed, ret=%d\n",
				reg_name, ret);
			goto free_regulator;
		}
	}

	if (consumer->current_uA > 0) {
		ret = regulator_set_load(consumer->reg, consumer->current_uA);
		if (ret < 0) {
			pr_err("regulator_set_load %s failed, ret=%d\n",
				reg_name, ret);
			goto remove_voltage;
		}
	}

	if (consumer->enable) {
		ret = regulator_enable(consumer->reg);
		if (ret) {
			pr_err("regulator_enable %s failed, ret=%d\n", reg_name,
				ret);
			goto remove_current;
		}
	}

	list_add(&consumer->list, &proxy_consumer_list);
	mutex_unlock(&proxy_consumer_list_lock);

	return consumer;

remove_current:
	regulator_set_load(consumer->reg, 0);
remove_voltage:
	regulator_set_voltage(consumer->reg, 0, INT_MAX);
free_regulator:
	regulator_put(consumer->reg);
free_consumer:
	kfree(consumer);
unlock_list:
	mutex_unlock(&proxy_consumer_list_lock);

	return ERR_PTR(ret);
}

/**
 * regulator_proxy_consumer_register() - conditionally register a proxy consumer
 *		 for the specified regulator and set its boot time parameters
 * @dev:		Device pointer of the regulator
 * @node:		Device node pointer of the regulator
 *
 * This function calls regulator_get() after first checking if any proxy
 * consumer properties are present in the 'node' device node.  After that, the
 * voltage, minimum current, and/or the enable state will be set based upon the
 * device node property values.
 *
 * Returns 0 on successfully proxy voting or if no proxy voting is needed, or an
 * errno if an error occurred.
 */
int regulator_proxy_consumer_register(struct device *dev,
				      struct device_node *node)
{
	struct proxy_consumer *consumer;

	consumer = regulator_proxy_consumer_add(dev, node);

	return PTR_ERR_OR_ZERO(consumer);
}
EXPORT_SYMBOL(regulator_proxy_consumer_register);

/* proxy_consumer_list_lock must be held by caller. */
static int regulator_proxy_consumer_remove(struct proxy_consumer *consumer)
{
	int ret = 0;

	if (consumer->enable) {
		ret = regulator_disable(consumer->reg);
		if (ret)
			pr_err("regulator_disable failed, ret=%d\n", ret);
	}

	if (consumer->current_uA > 0) {
		ret = regulator_set_load(consumer->reg, 0);
		if (ret < 0)
			pr_err("regulator_set_load failed, ret=%d\n",
				ret);
	}

	if (consumer->max_uV > 0 && consumer->min_uV <= consumer->max_uV) {
		ret = regulator_set_voltage(consumer->reg, 0, INT_MAX);
		if (ret)
			pr_err("regulator_set_voltage failed, ret=%d\n", ret);
	}

	regulator_put(consumer->reg);
	list_del(&consumer->list);
	kfree(consumer);

	return ret;
}

/**
 * regulator_proxy_consumer_unregister() - unregister the proxy consumers of a
 *					   device and remove their boot time
 *					   requests
 * @dev:		Device pointer of the regulator
 *
 * This function removes all requests made by the proxy consumers of regulators
 * in dev which where issued in regulator_proxy_consumer_register() and then
 * frees the consumers' resources.
 *
 * Returns 0 on success or an errno on failure.
 */
void regulator_proxy_consumer_unregister(struct device *dev)
{
	struct proxy_consumer *consumer, *temp;

	if (IS_ERR_OR_NULL(dev)) {
		pr_err("invalid device pointer\n");
		return;
	}

	mutex_lock(&proxy_consumer_list_lock);
	list_for_each_entry_safe(consumer, temp, &proxy_consumer_list, list) {
		if (consumer->dev == dev)
			regulator_proxy_consumer_remove(consumer);
	}
	mutex_unlock(&proxy_consumer_list_lock);
}
EXPORT_SYMBOL(regulator_proxy_consumer_unregister);

/* proxy_consumer_list_lock must be held by caller. */
static void
_devm_regulator_proxy_consumer_release(struct device *dev, void *res)
{
	struct proxy_consumer *consumer = *(struct proxy_consumer **)res;
	struct proxy_consumer *temp;
	bool found = false;

	/*
	 * The proxy consumer may have already been removed due to a
	 * sync_state() or devm_regulator_proxy_consumer_unregister() call.
	 * Therefore, verify that it is still in the list before attempting to
	 * remove it.
	 */
	list_for_each_entry(temp, &proxy_consumer_list, list) {
		if (temp == consumer) {
			found = true;
			break;
		}
	}

	if (found)
		regulator_proxy_consumer_remove(consumer);
}

static void devm_regulator_proxy_consumer_release(struct device *dev, void *res)
{
	mutex_lock(&proxy_consumer_list_lock);
	_devm_regulator_proxy_consumer_release(dev, res);
	mutex_unlock(&proxy_consumer_list_lock);
}

/**
 * devm_regulator_proxy_consumer_register() - resource managed version of
 *					     regulator_proxy_consumer_register()
 * @dev:		Device pointer of the regulator
 * @node:		Device node pointer of the regulator
 *
 * This is a resource managed version of regulator_proxy_consumer_register().
 * Proxy consumer requests made via this call are automatically removed via
 * regulator_proxy_consumer_unregister() on driver detach. See
 * regulator_proxy_consumer_register() for more details.
 *
 * Returns 0 on success or an errno on failure.
 */
int devm_regulator_proxy_consumer_register(struct device *dev,
					   struct device_node *node)
{
	struct proxy_consumer *consumer;
	struct proxy_consumer **ptr;

	ptr = devres_alloc(devm_regulator_proxy_consumer_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	consumer = regulator_proxy_consumer_add(dev, node);
	if (IS_ERR_OR_NULL(consumer)) {
		devres_free(ptr);
		return PTR_ERR(consumer);
	}

	*ptr = consumer;
	devres_add(dev, ptr);

	return 0;
}
EXPORT_SYMBOL(devm_regulator_proxy_consumer_register);

static int devm_regulator_proxy_consumer_match(struct device *dev, void *res,
					       void *data)
{
	struct proxy_consumer **consumer = res;

	if (!consumer || !*consumer) {
		WARN_ON(!consumer || !*consumer);
		return 0;
	}

	return *consumer == data;
}

/**
 * devm_regulator_proxy_consumer_unregister() - resource managed version of
 *					regulator_proxy_consumer_unregister()
 * @dev:		Device pointer of the regulator
 *
 * Deallocate the proxy consumers allocated for 'dev' with
 * devm_regulator_proxy_consumer_register().  Normally this function will not
 * need to be called and the resource management code will ensure that the
 * resource is freed.
 *
 * Returns 0 on success or an errno on failure.
 */
void devm_regulator_proxy_consumer_unregister(struct device *dev)
{
	struct proxy_consumer *consumer, *temp;

	if (IS_ERR_OR_NULL(dev))
		return;

	mutex_lock(&proxy_consumer_list_lock);
	list_for_each_entry_safe(consumer, temp, &proxy_consumer_list, list) {
		if (consumer->dev == dev)
			devres_release(dev,
					_devm_regulator_proxy_consumer_release,
					devm_regulator_proxy_consumer_match,
					consumer);
	}
	mutex_unlock(&proxy_consumer_list_lock);
}
EXPORT_SYMBOL(devm_regulator_proxy_consumer_unregister);

#ifndef CONFIG_REGULATOR_PROXY_CONSUMER_LEGACY

void regulator_proxy_consumer_sync_state(struct device *dev)
{
	regulator_proxy_consumer_unregister(dev);
}
EXPORT_SYMBOL(regulator_proxy_consumer_sync_state);

#else /* CONFIG_REGULATOR_PROXY_CONSUMER_LEGACY=y */

void regulator_proxy_consumer_sync_state(struct device *dev) { }
EXPORT_SYMBOL(regulator_proxy_consumer_sync_state);

/*
 * Remove all proxy requests at late_initcall_sync.  The assumption is that all
 * devices have probed at this point and made their own regulator requests.
 */
static int __init regulator_proxy_consumer_remove_all(void)
{
	struct proxy_consumer *consumer;
	struct proxy_consumer *temp;

	mutex_lock(&proxy_consumer_list_lock);
	proxy_consumers_removed = true;

	if (!list_empty(&proxy_consumer_list))
		pr_info("removing regulator proxy consumer requests\n");

	list_for_each_entry_safe(consumer, temp, &proxy_consumer_list, list) {
		regulator_proxy_consumer_remove(consumer);
	}
	mutex_unlock(&proxy_consumer_list_lock);

	return 0;
}
late_initcall_sync(regulator_proxy_consumer_remove_all);

#endif

MODULE_DESCRIPTION("Regulator proxy consumer library");
MODULE_LICENSE("GPL v2");
