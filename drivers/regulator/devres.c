/*
 * devres.c  --  Voltage/Current Regulator framework devres implementation.
 *
 * Copyright 2013 Linaro Ltd
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/module.h>

#include "internal.h"

enum {
	NORMAL_GET,
	EXCLUSIVE_GET,
	OPTIONAL_GET,
};

static void devm_regulator_release(struct device *dev, void *res)
{
	regulator_put(*(struct regulator **)res);
}

static struct regulator *_devm_regulator_get(struct device *dev, const char *id,
					     int get_type)
{
	struct regulator **ptr, *regulator;

	ptr = devres_alloc(devm_regulator_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	switch (get_type) {
	case NORMAL_GET:
		regulator = regulator_get(dev, id);
		break;
	case EXCLUSIVE_GET:
		regulator = regulator_get_exclusive(dev, id);
		break;
	case OPTIONAL_GET:
		regulator = regulator_get_optional(dev, id);
		break;
	default:
		regulator = ERR_PTR(-EINVAL);
	}

	if (!IS_ERR(regulator)) {
		*ptr = regulator;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return regulator;
}

/**
 * devm_regulator_get - Resource managed regulator_get()
 * @dev: device for regulator "consumer"
 * @id: Supply name or regulator ID.
 *
 * Managed regulator_get(). Regulators returned from this function are
 * automatically regulator_put() on driver detach. See regulator_get() for more
 * information.
 */
struct regulator *devm_regulator_get(struct device *dev, const char *id)
{
	return _devm_regulator_get(dev, id, NORMAL_GET);
}
EXPORT_SYMBOL_GPL(devm_regulator_get);

/**
 * devm_regulator_get_exclusive - Resource managed regulator_get_exclusive()
 * @dev: device for regulator "consumer"
 * @id: Supply name or regulator ID.
 *
 * Managed regulator_get_exclusive(). Regulators returned from this function
 * are automatically regulator_put() on driver detach. See regulator_get() for
 * more information.
 */
struct regulator *devm_regulator_get_exclusive(struct device *dev,
					       const char *id)
{
	return _devm_regulator_get(dev, id, EXCLUSIVE_GET);
}
EXPORT_SYMBOL_GPL(devm_regulator_get_exclusive);

/**
 * devm_regulator_get_optional - Resource managed regulator_get_optional()
 * @dev: device for regulator "consumer"
 * @id: Supply name or regulator ID.
 *
 * Managed regulator_get_optional(). Regulators returned from this
 * function are automatically regulator_put() on driver detach. See
 * regulator_get_optional() for more information.
 */
struct regulator *devm_regulator_get_optional(struct device *dev,
					      const char *id)
{
	return _devm_regulator_get(dev, id, OPTIONAL_GET);
}
EXPORT_SYMBOL_GPL(devm_regulator_get_optional);

static int devm_regulator_match(struct device *dev, void *res, void *data)
{
	struct regulator **r = res;
	if (!r || !*r) {
		WARN_ON(!r || !*r);
		return 0;
	}
	return *r == data;
}

/**
 * devm_regulator_put - Resource managed regulator_put()
 * @regulator: regulator to free
 *
 * Deallocate a regulator allocated with devm_regulator_get(). Normally
 * this function will not need to be called and the resource management
 * code will ensure that the resource is freed.
 */
void devm_regulator_put(struct regulator *regulator)
{
	int rc;

	rc = devres_release(regulator->dev, devm_regulator_release,
			    devm_regulator_match, regulator);
	if (rc != 0)
		WARN_ON(rc);
}
EXPORT_SYMBOL_GPL(devm_regulator_put);

/**
 * devm_regulator_bulk_get - managed get multiple regulator consumers
 *
 * @dev:           Device to supply
 * @num_consumers: Number of consumers to register
 * @consumers:     Configuration of consumers; clients are stored here.
 *
 * @return 0 on success, an errno on failure.
 *
 * This helper function allows drivers to get several regulator
 * consumers in one operation with management, the regulators will
 * automatically be freed when the device is unbound.  If any of the
 * regulators cannot be acquired then any regulators that were
 * allocated will be freed before returning to the caller.
 */
int devm_regulator_bulk_get(struct device *dev, int num_consumers,
			    struct regulator_bulk_data *consumers)
{
	int i;
	int ret;

	for (i = 0; i < num_consumers; i++)
		consumers[i].consumer = NULL;

	for (i = 0; i < num_consumers; i++) {
		consumers[i].consumer = devm_regulator_get(dev,
							   consumers[i].supply);
		if (IS_ERR(consumers[i].consumer)) {
			ret = PTR_ERR(consumers[i].consumer);
			dev_err(dev, "Failed to get supply '%s': %d\n",
				consumers[i].supply, ret);
			consumers[i].consumer = NULL;
			goto err;
		}
	}

	return 0;

err:
	for (i = 0; i < num_consumers && consumers[i].consumer; i++)
		devm_regulator_put(consumers[i].consumer);

	return ret;
}
EXPORT_SYMBOL_GPL(devm_regulator_bulk_get);

static void devm_rdev_release(struct device *dev, void *res)
{
	regulator_unregister(*(struct regulator_dev **)res);
}

/**
 * devm_regulator_register - Resource managed regulator_register()
 * @regulator_desc: regulator to register
 * @config: runtime configuration for regulator
 *
 * Called by regulator drivers to register a regulator.  Returns a
 * valid pointer to struct regulator_dev on success or an ERR_PTR() on
 * error.  The regulator will automatically be released when the device
 * is unbound.
 */
struct regulator_dev *devm_regulator_register(struct device *dev,
				  const struct regulator_desc *regulator_desc,
				  const struct regulator_config *config)
{
	struct regulator_dev **ptr, *rdev;

	ptr = devres_alloc(devm_rdev_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	rdev = regulator_register(regulator_desc, config);
	if (!IS_ERR(rdev)) {
		*ptr = rdev;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return rdev;
}
EXPORT_SYMBOL_GPL(devm_regulator_register);

static int devm_rdev_match(struct device *dev, void *res, void *data)
{
	struct regulator_dev **r = res;
	if (!r || !*r) {
		WARN_ON(!r || !*r);
		return 0;
	}
	return *r == data;
}

/**
 * devm_regulator_unregister - Resource managed regulator_unregister()
 * @regulator: regulator to free
 *
 * Unregister a regulator registered with devm_regulator_register().
 * Normally this function will not need to be called and the resource
 * management code will ensure that the resource is freed.
 */
void devm_regulator_unregister(struct device *dev, struct regulator_dev *rdev)
{
	int rc;

	rc = devres_release(dev, devm_rdev_release, devm_rdev_match, rdev);
	if (rc != 0)
		WARN_ON(rc);
}
EXPORT_SYMBOL_GPL(devm_regulator_unregister);

struct regulator_supply_alias_match {
	struct device *dev;
	const char *id;
};

static int devm_regulator_match_supply_alias(struct device *dev, void *res,
					     void *data)
{
	struct regulator_supply_alias_match *match = res;
	struct regulator_supply_alias_match *target = data;

	return match->dev == target->dev && strcmp(match->id, target->id) == 0;
}

static void devm_regulator_destroy_supply_alias(struct device *dev, void *res)
{
	struct regulator_supply_alias_match *match = res;

	regulator_unregister_supply_alias(match->dev, match->id);
}

/**
 * devm_regulator_register_supply_alias - Resource managed
 * regulator_register_supply_alias()
 *
 * @dev: device that will be given as the regulator "consumer"
 * @id: Supply name or regulator ID
 * @alias_dev: device that should be used to lookup the supply
 * @alias_id: Supply name or regulator ID that should be used to lookup the
 * supply
 *
 * The supply alias will automatically be unregistered when the source
 * device is unbound.
 */
int devm_regulator_register_supply_alias(struct device *dev, const char *id,
					 struct device *alias_dev,
					 const char *alias_id)
{
	struct regulator_supply_alias_match *match;
	int ret;

	match = devres_alloc(devm_regulator_destroy_supply_alias,
			   sizeof(struct regulator_supply_alias_match),
			   GFP_KERNEL);
	if (!match)
		return -ENOMEM;

	match->dev = dev;
	match->id = id;

	ret = regulator_register_supply_alias(dev, id, alias_dev, alias_id);
	if (ret < 0) {
		devres_free(match);
		return ret;
	}

	devres_add(dev, match);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_regulator_register_supply_alias);

/**
 * devm_regulator_unregister_supply_alias - Resource managed
 * regulator_unregister_supply_alias()
 *
 * @dev: device that will be given as the regulator "consumer"
 * @id: Supply name or regulator ID
 *
 * Unregister an alias registered with
 * devm_regulator_register_supply_alias(). Normally this function
 * will not need to be called and the resource management code
 * will ensure that the resource is freed.
 */
void devm_regulator_unregister_supply_alias(struct device *dev, const char *id)
{
	struct regulator_supply_alias_match match;
	int rc;

	match.dev = dev;
	match.id = id;

	rc = devres_release(dev, devm_regulator_destroy_supply_alias,
			    devm_regulator_match_supply_alias, &match);
	if (rc != 0)
		WARN_ON(rc);
}
EXPORT_SYMBOL_GPL(devm_regulator_unregister_supply_alias);

/**
 * devm_regulator_bulk_register_supply_alias - Managed register
 * multiple aliases
 *
 * @dev: device that will be given as the regulator "consumer"
 * @id: List of supply names or regulator IDs
 * @alias_dev: device that should be used to lookup the supply
 * @alias_id: List of supply names or regulator IDs that should be used to
 * lookup the supply
 * @num_id: Number of aliases to register
 *
 * @return 0 on success, an errno on failure.
 *
 * This helper function allows drivers to register several supply
 * aliases in one operation, the aliases will be automatically
 * unregisters when the source device is unbound.  If any of the
 * aliases cannot be registered any aliases that were registered
 * will be removed before returning to the caller.
 */
int devm_regulator_bulk_register_supply_alias(struct device *dev,
					      const char *const *id,
					      struct device *alias_dev,
					      const char *const *alias_id,
					      int num_id)
{
	int i;
	int ret;

	for (i = 0; i < num_id; ++i) {
		ret = devm_regulator_register_supply_alias(dev, id[i],
							   alias_dev,
							   alias_id[i]);
		if (ret < 0)
			goto err;
	}

	return 0;

err:
	dev_err(dev,
		"Failed to create supply alias %s,%s -> %s,%s\n",
		id[i], dev_name(dev), alias_id[i], dev_name(alias_dev));

	while (--i >= 0)
		devm_regulator_unregister_supply_alias(dev, id[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(devm_regulator_bulk_register_supply_alias);

/**
 * devm_regulator_bulk_unregister_supply_alias - Managed unregister
 * multiple aliases
 *
 * @dev: device that will be given as the regulator "consumer"
 * @id: List of supply names or regulator IDs
 * @num_id: Number of aliases to unregister
 *
 * Unregister aliases registered with
 * devm_regulator_bulk_register_supply_alias(). Normally this function
 * will not need to be called and the resource management code
 * will ensure that the resource is freed.
 */
void devm_regulator_bulk_unregister_supply_alias(struct device *dev,
						 const char *const *id,
						 int num_id)
{
	int i;

	for (i = 0; i < num_id; ++i)
		devm_regulator_unregister_supply_alias(dev, id[i]);
}
EXPORT_SYMBOL_GPL(devm_regulator_bulk_unregister_supply_alias);

struct regulator_notifier_match {
	struct regulator *regulator;
	struct notifier_block *nb;
};

static int devm_regulator_match_notifier(struct device *dev, void *res,
					 void *data)
{
	struct regulator_notifier_match *match = res;
	struct regulator_notifier_match *target = data;

	return match->regulator == target->regulator && match->nb == target->nb;
}

static void devm_regulator_destroy_notifier(struct device *dev, void *res)
{
	struct regulator_notifier_match *match = res;

	regulator_unregister_notifier(match->regulator, match->nb);
}

/**
 * devm_regulator_register_notifier - Resource managed
 * regulator_register_notifier
 *
 * @regulator: regulator source
 * @nb: notifier block
 *
 * The notifier will be registers under the consumer device and be
 * automatically be unregistered when the source device is unbound.
 */
int devm_regulator_register_notifier(struct regulator *regulator,
				     struct notifier_block *nb)
{
	struct regulator_notifier_match *match;
	int ret;

	match = devres_alloc(devm_regulator_destroy_notifier,
			     sizeof(struct regulator_notifier_match),
			     GFP_KERNEL);
	if (!match)
		return -ENOMEM;

	match->regulator = regulator;
	match->nb = nb;

	ret = regulator_register_notifier(regulator, nb);
	if (ret < 0) {
		devres_free(match);
		return ret;
	}

	devres_add(regulator->dev, match);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_regulator_register_notifier);

/**
 * devm_regulator_unregister_notifier - Resource managed
 * regulator_unregister_notifier()
 *
 * @regulator: regulator source
 * @nb: notifier block
 *
 * Unregister a notifier registered with devm_regulator_register_notifier().
 * Normally this function will not need to be called and the resource
 * management code will ensure that the resource is freed.
 */
void devm_regulator_unregister_notifier(struct regulator *regulator,
					struct notifier_block *nb)
{
	struct regulator_notifier_match match;
	int rc;

	match.regulator = regulator;
	match.nb = nb;

	rc = devres_release(regulator->dev, devm_regulator_destroy_notifier,
			    devm_regulator_match_notifier, &match);
	if (rc != 0)
		WARN_ON(rc);
}
EXPORT_SYMBOL_GPL(devm_regulator_unregister_notifier);
