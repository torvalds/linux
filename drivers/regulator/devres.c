// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * devres.c  --  Voltage/Current Regulator framework devres implementation.
 *
 * Copyright 2013 Linaro Ltd
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/module.h>

#include "internal.h"

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

	regulator = _regulator_get(dev, id, get_type);
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
 * @dev: device to supply
 * @id:  supply name or regulator ID.
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
 * @dev: device to supply
 * @id:  supply name or regulator ID.
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
 * @dev: device to supply
 * @id:  supply name or regulator ID.
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

struct regulator_bulk_devres {
	struct regulator_bulk_data *consumers;
	int num_consumers;
};

static void devm_regulator_bulk_release(struct device *dev, void *res)
{
	struct regulator_bulk_devres *devres = res;

	regulator_bulk_free(devres->num_consumers, devres->consumers);
}

/**
 * devm_regulator_bulk_get - managed get multiple regulator consumers
 *
 * @dev:           device to supply
 * @num_consumers: number of consumers to register
 * @consumers:     configuration of consumers; clients are stored here.
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
	struct regulator_bulk_devres *devres;
	int ret;

	devres = devres_alloc(devm_regulator_bulk_release,
			      sizeof(*devres), GFP_KERNEL);
	if (!devres)
		return -ENOMEM;

	ret = regulator_bulk_get(dev, num_consumers, consumers);
	if (!ret) {
		devres->consumers = consumers;
		devres->num_consumers = num_consumers;
		devres_add(dev, devres);
	} else {
		devres_free(devres);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_regulator_bulk_get);

/**
 * devm_regulator_bulk_get_const - devm_regulator_bulk_get() w/ const data
 *
 * @dev:           device to supply
 * @num_consumers: number of consumers to register
 * @in_consumers:  const configuration of consumers
 * @out_consumers: in_consumers is copied here and this is passed to
 *		   devm_regulator_bulk_get().
 *
 * This is a convenience function to allow bulk regulator configuration
 * to be stored "static const" in files.
 *
 * Return: 0 on success, an errno on failure.
 */
int devm_regulator_bulk_get_const(struct device *dev, int num_consumers,
				  const struct regulator_bulk_data *in_consumers,
				  struct regulator_bulk_data **out_consumers)
{
	*out_consumers = devm_kmemdup(dev, in_consumers,
				      num_consumers * sizeof(*in_consumers),
				      GFP_KERNEL);
	if (*out_consumers == NULL)
		return -ENOMEM;

	return devm_regulator_bulk_get(dev, num_consumers, *out_consumers);
}
EXPORT_SYMBOL_GPL(devm_regulator_bulk_get_const);

static void devm_rdev_release(struct device *dev, void *res)
{
	regulator_unregister(*(struct regulator_dev **)res);
}

/**
 * devm_regulator_register - Resource managed regulator_register()
 * @dev:            device to supply
 * @regulator_desc: regulator to register
 * @config:         runtime configuration for regulator
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
 * @dev:       device to supply
 * @id:        supply name or regulator ID
 * @alias_dev: device that should be used to lookup the supply
 * @alias_id:  supply name or regulator ID that should be used to lookup the
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

static void devm_regulator_unregister_supply_alias(struct device *dev,
						   const char *id)
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

/**
 * devm_regulator_bulk_register_supply_alias - Managed register
 * multiple aliases
 *
 * @dev:       device to supply
 * @id:        list of supply names or regulator IDs
 * @alias_dev: device that should be used to lookup the supply
 * @alias_id:  list of supply names or regulator IDs that should be used to
 *             lookup the supply
 * @num_id:    number of aliases to register
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
 * @nb:        notifier block
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
 * @nb:        notifier block
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

static void regulator_irq_helper_drop(void *res)
{
	regulator_irq_helper_cancel(&res);
}

/**
 * devm_regulator_irq_helper - resource managed registration of IRQ based
 * regulator event/error notifier
 *
 * @dev:		device to which lifetime the helper's lifetime is
 *			bound.
 * @d:			IRQ helper descriptor.
 * @irq:		IRQ used to inform events/errors to be notified.
 * @irq_flags:		Extra IRQ flags to be OR'ed with the default
 *			IRQF_ONESHOT when requesting the (threaded) irq.
 * @common_errs:	Errors which can be flagged by this IRQ for all rdevs.
 *			When IRQ is re-enabled these errors will be cleared
 *			from all associated regulators
 * @per_rdev_errs:	Optional error flag array describing errors specific
 *			for only some of the regulators. These errors will be
 *			or'ed with common errors. If this is given the array
 *			should contain rdev_amount flags. Can be set to NULL
 *			if there is no regulator specific error flags for this
 *			IRQ.
 * @rdev:		Array of pointers to regulators associated with this
 *			IRQ.
 * @rdev_amount:	Amount of regulators associated with this IRQ.
 *
 * Return: handle to irq_helper or an ERR_PTR() encoded error code.
 */
void *devm_regulator_irq_helper(struct device *dev,
				const struct regulator_irq_desc *d, int irq,
				int irq_flags, int common_errs,
				int *per_rdev_errs,
				struct regulator_dev **rdev, int rdev_amount)
{
	void *ptr;
	int ret;

	ptr = regulator_irq_helper(dev, d, irq, irq_flags, common_errs,
				    per_rdev_errs, rdev, rdev_amount);
	if (IS_ERR(ptr))
		return ptr;

	ret = devm_add_action_or_reset(dev, regulator_irq_helper_drop, ptr);
	if (ret)
		return ERR_PTR(ret);

	return ptr;
}
EXPORT_SYMBOL_GPL(devm_regulator_irq_helper);
