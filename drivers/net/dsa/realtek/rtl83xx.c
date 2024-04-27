// SPDX-License-Identifier: GPL-2.0+

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/of_mdio.h>

#include "realtek.h"
#include "rtl83xx.h"

/**
 * rtl83xx_lock() - Locks the mutex used by regmaps
 * @ctx: realtek_priv pointer
 *
 * This function is passed to regmap to be used as the lock function.
 * It is also used externally to block regmap before executing multiple
 * operations that must happen in sequence (which will use
 * realtek_priv.map_nolock instead).
 *
 * Context: Can sleep. Holds priv->map_lock lock.
 * Return: nothing
 */
void rtl83xx_lock(void *ctx)
{
	struct realtek_priv *priv = ctx;

	mutex_lock(&priv->map_lock);
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_lock, REALTEK_DSA);

/**
 * rtl83xx_unlock() - Unlocks the mutex used by regmaps
 * @ctx: realtek_priv pointer
 *
 * This function unlocks the lock acquired by rtl83xx_lock.
 *
 * Context: Releases priv->map_lock lock.
 * Return: nothing
 */
void rtl83xx_unlock(void *ctx)
{
	struct realtek_priv *priv = ctx;

	mutex_unlock(&priv->map_lock);
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_unlock, REALTEK_DSA);

static int rtl83xx_user_mdio_read(struct mii_bus *bus, int addr, int regnum)
{
	struct realtek_priv *priv = bus->priv;

	return priv->ops->phy_read(priv, addr, regnum);
}

static int rtl83xx_user_mdio_write(struct mii_bus *bus, int addr, int regnum,
				   u16 val)
{
	struct realtek_priv *priv = bus->priv;

	return priv->ops->phy_write(priv, addr, regnum, val);
}

/**
 * rtl83xx_setup_user_mdio() - register the user mii bus driver
 * @ds: DSA switch associated with this user_mii_bus
 *
 * Registers the MDIO bus for built-in Ethernet PHYs, and associates it with
 * the mandatory 'mdio' child OF node of the switch.
 *
 * Context: Can sleep.
 * Return: 0 on success, negative value for failure.
 */
int rtl83xx_setup_user_mdio(struct dsa_switch *ds)
{
	struct realtek_priv *priv = ds->priv;
	struct device_node *mdio_np;
	struct mii_bus *bus;
	int ret = 0;

	mdio_np = of_get_child_by_name(priv->dev->of_node, "mdio");
	if (!mdio_np) {
		dev_err(priv->dev, "no MDIO bus node\n");
		return -ENODEV;
	}

	bus = devm_mdiobus_alloc(priv->dev);
	if (!bus) {
		ret = -ENOMEM;
		goto err_put_node;
	}

	bus->priv = priv;
	bus->name = "Realtek user MII";
	bus->read = rtl83xx_user_mdio_read;
	bus->write = rtl83xx_user_mdio_write;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s:user_mii", dev_name(priv->dev));
	bus->parent = priv->dev;

	ret = devm_of_mdiobus_register(priv->dev, bus, mdio_np);
	if (ret) {
		dev_err(priv->dev, "unable to register MDIO bus %s\n",
			bus->id);
		goto err_put_node;
	}

	priv->user_mii_bus = bus;

err_put_node:
	of_node_put(mdio_np);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_setup_user_mdio, REALTEK_DSA);

/**
 * rtl83xx_probe() - probe a Realtek switch
 * @dev: the device being probed
 * @interface_info: specific management interface info.
 *
 * This function initializes realtek_priv and reads data from the device tree
 * node. The switch is hard resetted if a method is provided.
 *
 * Context: Can sleep.
 * Return: Pointer to the realtek_priv or ERR_PTR() in case of failure.
 *
 * The realtek_priv pointer does not need to be freed as it is controlled by
 * devres.
 */
struct realtek_priv *
rtl83xx_probe(struct device *dev,
	      const struct realtek_interface_info *interface_info)
{
	const struct realtek_variant *var;
	struct realtek_priv *priv;
	struct regmap_config rc = {
		.reg_bits = 10, /* A4..A0 R4..R0 */
		.val_bits = 16,
		.reg_stride = 1,
		.max_register = 0xffff,
		.reg_format_endian = REGMAP_ENDIAN_BIG,
		.reg_read = interface_info->reg_read,
		.reg_write = interface_info->reg_write,
		.cache_type = REGCACHE_NONE,
		.lock = rtl83xx_lock,
		.unlock = rtl83xx_unlock,
	};
	int ret;

	var = of_device_get_match_data(dev);
	if (!var)
		return ERR_PTR(-EINVAL);

	priv = devm_kzalloc(dev, size_add(sizeof(*priv), var->chip_data_sz),
			    GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	mutex_init(&priv->map_lock);

	rc.lock_arg = priv;
	priv->map = devm_regmap_init(dev, NULL, priv, &rc);
	if (IS_ERR(priv->map)) {
		ret = PTR_ERR(priv->map);
		dev_err(dev, "regmap init failed: %d\n", ret);
		return ERR_PTR(ret);
	}

	rc.disable_locking = true;
	priv->map_nolock = devm_regmap_init(dev, NULL, priv, &rc);
	if (IS_ERR(priv->map_nolock)) {
		ret = PTR_ERR(priv->map_nolock);
		dev_err(dev, "regmap init failed: %d\n", ret);
		return ERR_PTR(ret);
	}

	/* Link forward and backward */
	priv->dev = dev;
	priv->variant = var;
	priv->ops = var->ops;
	priv->chip_data = (void *)priv + sizeof(*priv);

	spin_lock_init(&priv->lock);

	priv->leds_disabled = of_property_read_bool(dev->of_node,
						    "realtek,disable-leds");

	/* TODO: if power is software controlled, set up any regulators here */
	priv->reset_ctl = devm_reset_control_get_optional(dev, NULL);
	if (IS_ERR(priv->reset_ctl)) {
		ret = PTR_ERR(priv->reset_ctl);
		dev_err_probe(dev, ret, "failed to get reset control\n");
		return ERR_CAST(priv->reset_ctl);
	}

	priv->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset)) {
		dev_err(dev, "failed to get RESET GPIO\n");
		return ERR_CAST(priv->reset);
	}

	dev_set_drvdata(dev, priv);

	if (priv->reset_ctl || priv->reset) {
		rtl83xx_reset_assert(priv);
		dev_dbg(dev, "asserted RESET\n");
		msleep(REALTEK_HW_STOP_DELAY);
		rtl83xx_reset_deassert(priv);
		msleep(REALTEK_HW_START_DELAY);
		dev_dbg(dev, "deasserted RESET\n");
	}

	return priv;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_probe, REALTEK_DSA);

/**
 * rtl83xx_register_switch() - detects and register a switch
 * @priv: realtek_priv pointer
 *
 * This function first checks the switch chip ID and register a DSA
 * switch.
 *
 * Context: Can sleep. Takes and releases priv->map_lock.
 * Return: 0 on success, negative value for failure.
 */
int rtl83xx_register_switch(struct realtek_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;
	int ret;

	ret = priv->ops->detect(priv);
	if (ret) {
		dev_err_probe(priv->dev, ret, "unable to detect switch\n");
		return ret;
	}

	ds->priv = priv;
	ds->dev = priv->dev;
	ds->ops = priv->variant->ds_ops;
	ds->num_ports = priv->num_ports;

	ret = dsa_register_switch(ds);
	if (ret) {
		dev_err_probe(priv->dev, ret, "unable to register switch\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_register_switch, REALTEK_DSA);

/**
 * rtl83xx_unregister_switch() - unregister a switch
 * @priv: realtek_priv pointer
 *
 * This function unregister a DSA switch.
 *
 * Context: Can sleep.
 * Return: Nothing.
 */
void rtl83xx_unregister_switch(struct realtek_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;

	dsa_unregister_switch(ds);
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_unregister_switch, REALTEK_DSA);

/**
 * rtl83xx_shutdown() - shutdown a switch
 * @priv: realtek_priv pointer
 *
 * This function shuts down the DSA switch and cleans the platform driver data,
 * to prevent realtek_{smi,mdio}_remove() from running afterwards, which is
 * possible if the parent bus implements its own .shutdown() as .remove().
 *
 * Context: Can sleep.
 * Return: Nothing.
 */
void rtl83xx_shutdown(struct realtek_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;

	dsa_switch_shutdown(ds);

	dev_set_drvdata(priv->dev, NULL);
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_shutdown, REALTEK_DSA);

/**
 * rtl83xx_remove() - Cleanup a realtek switch driver
 * @priv: realtek_priv pointer
 *
 * Placehold for common cleanup procedures.
 *
 * Context: Any
 * Return: nothing
 */
void rtl83xx_remove(struct realtek_priv *priv)
{
}
EXPORT_SYMBOL_NS_GPL(rtl83xx_remove, REALTEK_DSA);

void rtl83xx_reset_assert(struct realtek_priv *priv)
{
	int ret;

	ret = reset_control_assert(priv->reset_ctl);
	if (ret)
		dev_warn(priv->dev,
			 "Failed to assert the switch reset control: %pe\n",
			 ERR_PTR(ret));

	gpiod_set_value(priv->reset, true);
}

void rtl83xx_reset_deassert(struct realtek_priv *priv)
{
	int ret;

	ret = reset_control_deassert(priv->reset_ctl);
	if (ret)
		dev_warn(priv->dev,
			 "Failed to deassert the switch reset control: %pe\n",
			 ERR_PTR(ret));

	gpiod_set_value(priv->reset, false);
}

MODULE_AUTHOR("Luiz Angelo Daros de Luca <luizluca@gmail.com>");
MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("Realtek DSA switches common module");
MODULE_LICENSE("GPL");
