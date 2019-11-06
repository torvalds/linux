// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2014, 2016, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/proxy-consumer.h>

struct proxy_consumer {
	struct list_head	list;
	struct regulator	*reg;
	bool			enable;
	int			min_uV;
	int			max_uV;
	u32			current_uA;
};

static DEFINE_MUTEX(proxy_consumer_list_mutex);
static LIST_HEAD(proxy_consumer_list);
static bool proxy_consumers_removed;

/**
 * regulator_proxy_consumer_register() - conditionally register a proxy consumer
 *		 for the specified regulator and set its boot time parameters
 * @reg_dev:		Device pointer of the regulator
 * @reg_node:		Device node pointer of the regulator
 *
 * Returns a struct proxy_consumer pointer corresponding to the regulator on
 * success, ERR_PTR() if an error occurred, or NULL if no proxy consumer is
 * needed for the regulator.  This function calls
 * regulator_get(reg_dev, "proxy") after first checking if any proxy consumer
 * properties are present in the reg_node device node.  After that, the voltage,
 * minimum current, and/or the enable state will be set based upon the device
 * node property values.
 */
struct proxy_consumer *regulator_proxy_consumer_register(struct device *reg_dev,
			struct device_node *reg_node)
{
	struct proxy_consumer *consumer = NULL;
	const char *reg_name = "";
	u32 voltage[2] = {0};
	int rc;
	bool no_sync_state = !reg_dev->driver->sync_state;

	/* Return immediately if no proxy consumer properties are specified. */
	if (!of_find_property(reg_node, "qcom,proxy-consumer-enable", NULL)
	    && !of_find_property(reg_node, "qcom,proxy-consumer-voltage", NULL)
	    && !of_find_property(reg_node, "qcom,proxy-consumer-current", NULL))
		return NULL;

	mutex_lock(&proxy_consumer_list_mutex);

	/* Do not register new consumers if they cannot be removed later. */
	if (proxy_consumers_removed && no_sync_state) {
		rc = -EPERM;
		goto unlock;
	}

	if (dev_name(reg_dev))
		reg_name = dev_name(reg_dev);

	consumer = kzalloc(sizeof(*consumer), GFP_KERNEL);
	if (!consumer) {
		rc = -ENOMEM;
		goto unlock;
	}

	INIT_LIST_HEAD(&consumer->list);
	consumer->enable
		= of_property_read_bool(reg_node, "qcom,proxy-consumer-enable");
	of_property_read_u32(reg_node, "qcom,proxy-consumer-current",
				&consumer->current_uA);
	rc = of_property_read_u32_array(reg_node, "qcom,proxy-consumer-voltage",
					voltage, 2);
	if (!rc) {
		consumer->min_uV = voltage[0];
		consumer->max_uV = voltage[1];
	}

	dev_dbg(reg_dev, "proxy consumer request: enable=%d, voltage_range=[%d, %d] uV, min_current=%d uA\n",
		consumer->enable, consumer->min_uV, consumer->max_uV,
		consumer->current_uA);

	consumer->reg = regulator_get(reg_dev, "proxy");
	if (IS_ERR_OR_NULL(consumer->reg)) {
		rc = PTR_ERR(consumer->reg);
		pr_err("regulator_get() failed for %s, rc=%d\n", reg_name, rc);
		goto unlock;
	}

	if (consumer->max_uV > 0 && consumer->min_uV <= consumer->max_uV) {
		rc = regulator_set_voltage(consumer->reg, consumer->min_uV,
						consumer->max_uV);
		if (rc) {
			pr_err("regulator_set_voltage %s failed, rc=%d\n",
				reg_name, rc);
			goto free_regulator;
		}
	}

	if (consumer->current_uA > 0) {
		rc = regulator_set_load(consumer->reg,
						consumer->current_uA);
		if (rc < 0) {
			pr_err("regulator_set_load %s failed, rc=%d\n",
				reg_name, rc);
			goto remove_voltage;
		}
	}

	if (consumer->enable) {
		rc = regulator_enable(consumer->reg);
		if (rc) {
			pr_err("regulator_enable %s failed, rc=%d\n", reg_name,
				rc);
			goto remove_current;
		}
	}

	if (no_sync_state)
		list_add(&consumer->list, &proxy_consumer_list);
	mutex_unlock(&proxy_consumer_list_mutex);

	return consumer;

remove_current:
	regulator_set_load(consumer->reg, 0);
remove_voltage:
	regulator_set_voltage(consumer->reg, 0, INT_MAX);
free_regulator:
	regulator_put(consumer->reg);
unlock:
	kfree(consumer);
	mutex_unlock(&proxy_consumer_list_mutex);
	return ERR_PTR(rc);
}

/* proxy_consumer_list_mutex must be held by caller. */
static int regulator_proxy_consumer_remove(struct proxy_consumer *consumer)
{
	int rc = 0;

	if (consumer->enable) {
		rc = regulator_disable(consumer->reg);
		if (rc)
			pr_err("regulator_disable failed, rc=%d\n", rc);
	}

	if (consumer->current_uA > 0) {
		rc = regulator_set_load(consumer->reg, 0);
		if (rc < 0)
			pr_err("regulator_set_load failed, rc=%d\n",
				rc);
	}

	if (consumer->max_uV > 0 && consumer->min_uV <= consumer->max_uV) {
		rc = regulator_set_voltage(consumer->reg, 0, INT_MAX);
		if (rc)
			pr_err("regulator_set_voltage failed, rc=%d\n", rc);
	}

	regulator_put(consumer->reg);
	list_del(&consumer->list);
	kfree(consumer);

	return rc;
}

/**
 * regulator_proxy_consumer_unregister() - unregister a proxy consumer and
 *					   remove its boot time requests
 * @consumer:		Pointer to proxy_consumer to be removed
 *
 * Returns 0 on success or errno on failure.  This function removes all requests
 * made by the proxy consumer in regulator_proxy_consumer_register() and then
 * frees the consumer's resources.
 */
int regulator_proxy_consumer_unregister(struct proxy_consumer *consumer)
{
	int rc = 0;

	if (IS_ERR_OR_NULL(consumer))
		return 0;

	mutex_lock(&proxy_consumer_list_mutex);
	rc = regulator_proxy_consumer_remove(consumer);
	mutex_unlock(&proxy_consumer_list_mutex);

	return rc;
}

/*
 * Remove all proxy requests at late_initcall_sync.  The assumption is that all
 * devices have probed at this point and made their own regulator requests.
 */
static int __init regulator_proxy_consumer_remove_all(void)
{
	struct proxy_consumer *consumer;
	struct proxy_consumer *temp;

	mutex_lock(&proxy_consumer_list_mutex);
	proxy_consumers_removed = true;

	if (!list_empty(&proxy_consumer_list))
		pr_info("removing legacy regulator proxy consumer requests\n");

	list_for_each_entry_safe(consumer, temp, &proxy_consumer_list, list) {
		regulator_proxy_consumer_remove(consumer);
	}
	mutex_unlock(&proxy_consumer_list_mutex);

	return 0;
}
late_initcall_sync(regulator_proxy_consumer_remove_all);
