// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux I2C core OF component prober code
 *
 * Copyright (C) 2024 Google LLC
 */

#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/i2c-of-prober.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/stddef.h>

/*
 * Some devices, such as Google Hana Chromebooks, are produced by multiple
 * vendors each using their preferred components. Such components are all
 * in the device tree. Instead of having all of them enabled and having each
 * driver separately try and probe its device while fighting over shared
 * resources, they can be marked as "fail-needs-probe" and have a prober
 * figure out which one is actually used beforehand.
 *
 * This prober assumes such drop-in parts are on the same I2C bus, have
 * non-conflicting addresses, and can be directly probed by seeing which
 * address responds.
 *
 * TODO:
 * - Support I2C muxes
 */

static struct device_node *i2c_of_probe_get_i2c_node(struct device *dev, const char *type)
{
	struct device_node *node __free(device_node) = of_find_node_by_name(NULL, type);
	if (!node) {
		dev_err(dev, "Could not find %s device node\n", type);
		return NULL;
	}

	struct device_node *i2c_node __free(device_node) = of_get_parent(node);
	if (!of_node_name_eq(i2c_node, "i2c")) {
		dev_err(dev, "%s device isn't on I2C bus\n", type);
		return NULL;
	}

	if (!of_device_is_available(i2c_node)) {
		dev_err(dev, "I2C controller not available\n");
		return NULL;
	}

	return no_free_ptr(i2c_node);
}

static int i2c_of_probe_enable_node(struct device *dev, struct device_node *node)
{
	int ret;

	dev_dbg(dev, "Enabling %pOF\n", node);

	struct of_changeset *ocs __free(kfree) = kzalloc(sizeof(*ocs), GFP_KERNEL);
	if (!ocs)
		return -ENOMEM;

	of_changeset_init(ocs);
	ret = of_changeset_update_prop_string(ocs, node, "status", "okay");
	if (ret)
		return ret;

	ret = of_changeset_apply(ocs);
	if (ret) {
		/* ocs needs to be explicitly cleaned up before being freed. */
		of_changeset_destroy(ocs);
	} else {
		/*
		 * ocs is intentionally kept around as it needs to
		 * exist as long as the change is applied.
		 */
		void *ptr __always_unused = no_free_ptr(ocs);
	}

	return ret;
}

static const struct i2c_of_probe_ops i2c_of_probe_dummy_ops;

/**
 * i2c_of_probe_component() - probe for devices of "type" on the same i2c bus
 * @dev: Pointer to the &struct device of the caller, only used for dev_printk() messages.
 * @cfg: Pointer to the &struct i2c_of_probe_cfg containing callbacks and other options
 *       for the prober.
 * @ctx: Context data for callbacks.
 *
 * Probe for possible I2C components of the same "type" (&i2c_of_probe_cfg->type)
 * on the same I2C bus that have their status marked as "fail-needs-probe".
 *
 * Assumes that across the entire device tree the only instances of nodes
 * with "type" prefixed node names (not including the address portion) are
 * the ones that need handling for second source components. In other words,
 * if "type" is "touchscreen", then all device nodes named "touchscreen*"
 * are the ones that need probing. There cannot be another "touchscreen*"
 * node that is already enabled.
 *
 * Assumes that for each "type" of component, only one actually exists. In
 * other words, only one matching and existing device will be enabled.
 *
 * Context: Process context only. Does non-atomic I2C transfers.
 *          Should only be used from a driver probe function, as the function
 *          can return -EPROBE_DEFER if the I2C adapter or other resources
 *          are unavailable.
 * Return: 0 on success or no-op, error code otherwise.
 *         A no-op can happen when it seems like the device tree already
 *         has components of the type to be probed already enabled. This
 *         can happen when the device tree had not been updated to mark
 *         the status of the to-be-probed components as "fail-needs-probe".
 *         Or this function was already run with the same parameters and
 *         succeeded in enabling a component. The latter could happen if
 *         the user had multiple types of components to probe, and one of
 *         them down the list caused a deferred probe. This is expected
 *         behavior.
 */
int i2c_of_probe_component(struct device *dev, const struct i2c_of_probe_cfg *cfg, void *ctx)
{
	const struct i2c_of_probe_ops *ops;
	const char *type;
	struct i2c_adapter *i2c;
	int ret;

	ops = cfg->ops ?: &i2c_of_probe_dummy_ops;
	type = cfg->type;

	struct device_node *i2c_node __free(device_node) = i2c_of_probe_get_i2c_node(dev, type);
	if (!i2c_node)
		return -ENODEV;

	/*
	 * If any devices of the given "type" are already enabled then this function is a no-op.
	 * Either the device tree hasn't been modified to work with this probe function, or the
	 * function had already run before and enabled some component.
	 */
	for_each_child_of_node_with_prefix(i2c_node, node, type)
		if (of_device_is_available(node))
			return 0;

	i2c = of_get_i2c_adapter_by_node(i2c_node);
	if (!i2c)
		return dev_err_probe(dev, -EPROBE_DEFER, "Couldn't get I2C adapter\n");

	/* Grab and enable resources */
	ret = 0;
	if (ops->enable)
		ret = ops->enable(dev, i2c_node, ctx);
	if (ret)
		goto out_put_i2c_adapter;

	for_each_child_of_node_with_prefix(i2c_node, node, type) {
		union i2c_smbus_data data;
		u32 addr;

		if (of_property_read_u32(node, "reg", &addr))
			continue;
		if (i2c_smbus_xfer(i2c, addr, 0, I2C_SMBUS_READ, 0, I2C_SMBUS_BYTE, &data) < 0)
			continue;

		/* Found a device that is responding */
		if (ops->cleanup_early)
			ops->cleanup_early(dev, ctx);
		ret = i2c_of_probe_enable_node(dev, node);
		break;
	}

	if (ops->cleanup)
		ops->cleanup(dev, ctx);
out_put_i2c_adapter:
	i2c_put_adapter(i2c);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(i2c_of_probe_component, "I2C_OF_PROBER");

static int i2c_of_probe_simple_get_supply(struct device *dev, struct device_node *node,
					  struct i2c_of_probe_simple_ctx *ctx)
{
	const char *supply_name;
	struct regulator *supply;

	/*
	 * It's entirely possible for the component's device node to not have the
	 * regulator supplies. While it does not make sense from a hardware perspective,
	 * the supplies could be always on or otherwise not modeled in the device tree,
	 * but the device would still work.
	 */
	supply_name = ctx->opts->supply_name;
	if (!supply_name)
		return 0;

	supply = of_regulator_get_optional(dev, node, supply_name);
	if (IS_ERR(supply)) {
		return dev_err_probe(dev, PTR_ERR(supply),
				     "Failed to get regulator supply \"%s\" from %pOF\n",
				     supply_name, node);
	}

	ctx->supply = supply;

	return 0;
}

static void i2c_of_probe_simple_put_supply(struct i2c_of_probe_simple_ctx *ctx)
{
	regulator_put(ctx->supply);
	ctx->supply = NULL;
}

static int i2c_of_probe_simple_enable_regulator(struct device *dev, struct i2c_of_probe_simple_ctx *ctx)
{
	int ret;

	if (!ctx->supply)
		return 0;

	dev_dbg(dev, "Enabling regulator supply \"%s\"\n", ctx->opts->supply_name);

	ret = regulator_enable(ctx->supply);
	if (ret)
		return ret;

	if (ctx->opts->post_power_on_delay_ms)
		msleep(ctx->opts->post_power_on_delay_ms);

	return 0;
}

static void i2c_of_probe_simple_disable_regulator(struct device *dev, struct i2c_of_probe_simple_ctx *ctx)
{
	if (!ctx->supply)
		return;

	dev_dbg(dev, "Disabling regulator supply \"%s\"\n", ctx->opts->supply_name);

	regulator_disable(ctx->supply);
}

static int i2c_of_probe_simple_get_gpiod(struct device *dev, struct device_node *node,
					 struct i2c_of_probe_simple_ctx *ctx)
{
	struct fwnode_handle *fwnode = of_fwnode_handle(node);
	struct gpio_desc *gpiod;
	const char *con_id;

	/* NULL signals no GPIO needed */
	if (!ctx->opts->gpio_name)
		return 0;

	/* An empty string signals an unnamed GPIO */
	if (!ctx->opts->gpio_name[0])
		con_id = NULL;
	else
		con_id = ctx->opts->gpio_name;

	gpiod = fwnode_gpiod_get_index(fwnode, con_id, 0, GPIOD_ASIS, "i2c-of-prober");
	if (IS_ERR(gpiod))
		return PTR_ERR(gpiod);

	ctx->gpiod = gpiod;

	return 0;
}

static void i2c_of_probe_simple_put_gpiod(struct i2c_of_probe_simple_ctx *ctx)
{
	gpiod_put(ctx->gpiod);
	ctx->gpiod = NULL;
}

static int i2c_of_probe_simple_set_gpio(struct device *dev, struct i2c_of_probe_simple_ctx *ctx)
{
	int ret;

	if (!ctx->gpiod)
		return 0;

	dev_dbg(dev, "Configuring GPIO\n");

	ret = gpiod_direction_output(ctx->gpiod, ctx->opts->gpio_assert_to_enable);
	if (ret)
		return ret;

	if (ctx->opts->post_gpio_config_delay_ms)
		msleep(ctx->opts->post_gpio_config_delay_ms);

	return 0;
}

static void i2c_of_probe_simple_disable_gpio(struct device *dev, struct i2c_of_probe_simple_ctx *ctx)
{
	gpiod_set_value(ctx->gpiod, !ctx->opts->gpio_assert_to_enable);
}

/**
 * i2c_of_probe_simple_enable - Simple helper for I2C OF prober to get and enable resources
 * @dev: Pointer to the &struct device of the caller, only used for dev_printk() messages
 * @bus_node: Pointer to the &struct device_node of the I2C adapter.
 * @data: Pointer to &struct i2c_of_probe_simple_ctx helper context.
 *
 * If &i2c_of_probe_simple_opts->supply_name is given, request the named regulator supply.
 * If &i2c_of_probe_simple_opts->gpio_name is given, request the named GPIO. Or if it is
 * the empty string, request the unnamed GPIO.
 * If a regulator supply was found, enable that regulator.
 * If a GPIO line was found, configure the GPIO line to output and set value
 * according to given options.
 *
 * Return: %0 on success or no-op, or a negative error number on failure.
 */
int i2c_of_probe_simple_enable(struct device *dev, struct device_node *bus_node, void *data)
{
	struct i2c_of_probe_simple_ctx *ctx = data;
	struct device_node *node;
	const char *compat;
	int ret;

	dev_dbg(dev, "Requesting resources for components under I2C bus %pOF\n", bus_node);

	if (!ctx || !ctx->opts)
		return -EINVAL;

	compat = ctx->opts->res_node_compatible;
	if (!compat)
		return -EINVAL;

	node = of_get_compatible_child(bus_node, compat);
	if (!node)
		return dev_err_probe(dev, -ENODEV, "No device compatible with \"%s\" found\n",
				     compat);

	ret = i2c_of_probe_simple_get_supply(dev, node, ctx);
	if (ret)
		goto out_put_node;

	ret = i2c_of_probe_simple_get_gpiod(dev, node, ctx);
	if (ret)
		goto out_put_supply;

	ret = i2c_of_probe_simple_enable_regulator(dev, ctx);
	if (ret)
		goto out_put_gpiod;

	ret = i2c_of_probe_simple_set_gpio(dev, ctx);
	if (ret)
		goto out_disable_regulator;

	return 0;

out_disable_regulator:
	i2c_of_probe_simple_disable_regulator(dev, ctx);
out_put_gpiod:
	i2c_of_probe_simple_put_gpiod(ctx);
out_put_supply:
	i2c_of_probe_simple_put_supply(ctx);
out_put_node:
	of_node_put(node);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(i2c_of_probe_simple_enable, "I2C_OF_PROBER");

/**
 * i2c_of_probe_simple_cleanup_early - \
 *	Simple helper for I2C OF prober to release GPIOs before component is enabled
 * @dev: Pointer to the &struct device of the caller; unused.
 * @data: Pointer to &struct i2c_of_probe_simple_ctx helper context.
 *
 * GPIO descriptors are exclusive and have to be released before the
 * actual driver probes so that the latter can acquire them.
 */
void i2c_of_probe_simple_cleanup_early(struct device *dev, void *data)
{
	struct i2c_of_probe_simple_ctx *ctx = data;

	i2c_of_probe_simple_put_gpiod(ctx);
}
EXPORT_SYMBOL_NS_GPL(i2c_of_probe_simple_cleanup_early, "I2C_OF_PROBER");

/**
 * i2c_of_probe_simple_cleanup - Clean up and release resources for I2C OF prober simple helpers
 * @dev: Pointer to the &struct device of the caller, only used for dev_printk() messages
 * @data: Pointer to &struct i2c_of_probe_simple_ctx helper context.
 *
 * * If a GPIO line was found and not yet released, set its value to the opposite of that
 *   set in i2c_of_probe_simple_enable() and release it.
 * * If a regulator supply was found, disable that regulator and release it.
 */
void i2c_of_probe_simple_cleanup(struct device *dev, void *data)
{
	struct i2c_of_probe_simple_ctx *ctx = data;

	/* GPIO operations here are no-ops if i2c_of_probe_simple_cleanup_early was called. */
	i2c_of_probe_simple_disable_gpio(dev, ctx);
	i2c_of_probe_simple_put_gpiod(ctx);

	i2c_of_probe_simple_disable_regulator(dev, ctx);
	i2c_of_probe_simple_put_supply(ctx);
}
EXPORT_SYMBOL_NS_GPL(i2c_of_probe_simple_cleanup, "I2C_OF_PROBER");

struct i2c_of_probe_ops i2c_of_probe_simple_ops = {
	.enable = i2c_of_probe_simple_enable,
	.cleanup_early = i2c_of_probe_simple_cleanup_early,
	.cleanup = i2c_of_probe_simple_cleanup,
};
EXPORT_SYMBOL_NS_GPL(i2c_of_probe_simple_ops, "I2C_OF_PROBER");
