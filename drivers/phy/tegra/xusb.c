// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2022, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/phy/tegra/xusb.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <soc/tegra/fuse.h>

#include "xusb.h"

static struct phy *tegra_xusb_pad_of_xlate(struct device *dev,
					   struct of_phandle_args *args)
{
	struct tegra_xusb_pad *pad = dev_get_drvdata(dev);
	struct phy *phy = NULL;
	unsigned int i;

	if (args->args_count != 0)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < pad->soc->num_lanes; i++) {
		if (!pad->lanes[i])
			continue;

		if (pad->lanes[i]->dev.of_node == args->np) {
			phy = pad->lanes[i];
			break;
		}
	}

	if (phy == NULL)
		phy = ERR_PTR(-ENODEV);

	return phy;
}

static const struct of_device_id tegra_xusb_padctl_of_match[] = {
#if defined(CONFIG_ARCH_TEGRA_124_SOC) || defined(CONFIG_ARCH_TEGRA_132_SOC)
	{
		.compatible = "nvidia,tegra124-xusb-padctl",
		.data = &tegra124_xusb_padctl_soc,
	},
#endif
#if defined(CONFIG_ARCH_TEGRA_210_SOC)
	{
		.compatible = "nvidia,tegra210-xusb-padctl",
		.data = &tegra210_xusb_padctl_soc,
	},
#endif
#if defined(CONFIG_ARCH_TEGRA_186_SOC)
	{
		.compatible = "nvidia,tegra186-xusb-padctl",
		.data = &tegra186_xusb_padctl_soc,
	},
#endif
#if defined(CONFIG_ARCH_TEGRA_194_SOC)
	{
		.compatible = "nvidia,tegra194-xusb-padctl",
		.data = &tegra194_xusb_padctl_soc,
	},
#endif
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_xusb_padctl_of_match);

static struct device_node *
tegra_xusb_find_pad_node(struct tegra_xusb_padctl *padctl, const char *name)
{
	struct device_node *pads, *np;

	pads = of_get_child_by_name(padctl->dev->of_node, "pads");
	if (!pads)
		return NULL;

	np = of_get_child_by_name(pads, name);
	of_node_put(pads);

	return np;
}

static struct device_node *
tegra_xusb_pad_find_phy_node(struct tegra_xusb_pad *pad, unsigned int index)
{
	struct device_node *np, *lanes;

	lanes = of_get_child_by_name(pad->dev.of_node, "lanes");
	if (!lanes)
		return NULL;

	np = of_get_child_by_name(lanes, pad->soc->lanes[index].name);
	of_node_put(lanes);

	return np;
}

int tegra_xusb_lane_parse_dt(struct tegra_xusb_lane *lane,
			     struct device_node *np)
{
	struct device *dev = &lane->pad->dev;
	const char *function;
	int err;

	err = of_property_read_string(np, "nvidia,function", &function);
	if (err < 0)
		return err;

	err = match_string(lane->soc->funcs, lane->soc->num_funcs, function);
	if (err < 0) {
		dev_err(dev, "invalid function \"%s\" for lane \"%pOFn\"\n",
			function, np);
		return err;
	}

	lane->function = err;

	return 0;
}

static void tegra_xusb_lane_destroy(struct phy *phy)
{
	if (phy) {
		struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

		lane->pad->ops->remove(lane);
		phy_destroy(phy);
	}
}

static void tegra_xusb_pad_release(struct device *dev)
{
	struct tegra_xusb_pad *pad = to_tegra_xusb_pad(dev);

	pad->soc->ops->remove(pad);
}

static const struct device_type tegra_xusb_pad_type = {
	.release = tegra_xusb_pad_release,
};

int tegra_xusb_pad_init(struct tegra_xusb_pad *pad,
			struct tegra_xusb_padctl *padctl,
			struct device_node *np)
{
	int err;

	device_initialize(&pad->dev);
	INIT_LIST_HEAD(&pad->list);
	pad->dev.parent = padctl->dev;
	pad->dev.type = &tegra_xusb_pad_type;
	pad->dev.of_node = np;
	pad->padctl = padctl;

	err = dev_set_name(&pad->dev, "%s", pad->soc->name);
	if (err < 0)
		goto unregister;

	err = device_add(&pad->dev);
	if (err < 0)
		goto unregister;

	return 0;

unregister:
	device_unregister(&pad->dev);
	return err;
}

int tegra_xusb_pad_register(struct tegra_xusb_pad *pad,
			    const struct phy_ops *ops)
{
	struct device_node *children;
	struct phy *lane;
	unsigned int i;
	int err;

	children = of_get_child_by_name(pad->dev.of_node, "lanes");
	if (!children)
		return -ENODEV;

	pad->lanes = devm_kcalloc(&pad->dev, pad->soc->num_lanes, sizeof(lane),
				  GFP_KERNEL);
	if (!pad->lanes) {
		of_node_put(children);
		return -ENOMEM;
	}

	for (i = 0; i < pad->soc->num_lanes; i++) {
		struct device_node *np = tegra_xusb_pad_find_phy_node(pad, i);
		struct tegra_xusb_lane *lane;

		/* skip disabled lanes */
		if (!np || !of_device_is_available(np)) {
			of_node_put(np);
			continue;
		}

		pad->lanes[i] = phy_create(&pad->dev, np, ops);
		if (IS_ERR(pad->lanes[i])) {
			err = PTR_ERR(pad->lanes[i]);
			of_node_put(np);
			goto remove;
		}

		lane = pad->ops->probe(pad, np, i);
		if (IS_ERR(lane)) {
			phy_destroy(pad->lanes[i]);
			err = PTR_ERR(lane);
			goto remove;
		}

		list_add_tail(&lane->list, &pad->padctl->lanes);
		phy_set_drvdata(pad->lanes[i], lane);
	}

	pad->provider = of_phy_provider_register_full(&pad->dev, children,
						      tegra_xusb_pad_of_xlate);
	if (IS_ERR(pad->provider)) {
		err = PTR_ERR(pad->provider);
		goto remove;
	}

	return 0;

remove:
	while (i--)
		tegra_xusb_lane_destroy(pad->lanes[i]);

	of_node_put(children);

	return err;
}

void tegra_xusb_pad_unregister(struct tegra_xusb_pad *pad)
{
	unsigned int i = pad->soc->num_lanes;

	of_phy_provider_unregister(pad->provider);

	while (i--)
		tegra_xusb_lane_destroy(pad->lanes[i]);

	device_unregister(&pad->dev);
}

static struct tegra_xusb_pad *
tegra_xusb_pad_create(struct tegra_xusb_padctl *padctl,
		      const struct tegra_xusb_pad_soc *soc)
{
	struct tegra_xusb_pad *pad;
	struct device_node *np;
	int err;

	np = tegra_xusb_find_pad_node(padctl, soc->name);
	if (!np || !of_device_is_available(np))
		return NULL;

	pad = soc->ops->probe(padctl, soc, np);
	if (IS_ERR(pad)) {
		err = PTR_ERR(pad);
		dev_err(padctl->dev, "failed to create pad %s: %d\n",
			soc->name, err);
		return ERR_PTR(err);
	}

	/* XXX move this into ->probe() to avoid string comparison */
	if (strcmp(soc->name, "pcie") == 0)
		padctl->pcie = pad;

	if (strcmp(soc->name, "sata") == 0)
		padctl->sata = pad;

	if (strcmp(soc->name, "usb2") == 0)
		padctl->usb2 = pad;

	if (strcmp(soc->name, "ulpi") == 0)
		padctl->ulpi = pad;

	if (strcmp(soc->name, "hsic") == 0)
		padctl->hsic = pad;

	return pad;
}

static void __tegra_xusb_remove_pads(struct tegra_xusb_padctl *padctl)
{
	struct tegra_xusb_pad *pad, *tmp;

	list_for_each_entry_safe_reverse(pad, tmp, &padctl->pads, list) {
		list_del(&pad->list);
		tegra_xusb_pad_unregister(pad);
	}
}

static void tegra_xusb_remove_pads(struct tegra_xusb_padctl *padctl)
{
	mutex_lock(&padctl->lock);
	__tegra_xusb_remove_pads(padctl);
	mutex_unlock(&padctl->lock);
}

static void tegra_xusb_lane_program(struct tegra_xusb_lane *lane)
{
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;
	const struct tegra_xusb_lane_soc *soc = lane->soc;
	u32 value;

	/* skip single function lanes */
	if (soc->num_funcs < 2)
		return;

	if (lane->pad->ops->iddq_enable)
		lane->pad->ops->iddq_enable(lane);

	/* choose function */
	value = padctl_readl(padctl, soc->offset);
	value &= ~(soc->mask << soc->shift);
	value |= lane->function << soc->shift;
	padctl_writel(padctl, value, soc->offset);

	if (lane->pad->ops->iddq_disable)
		lane->pad->ops->iddq_disable(lane);
}

static void tegra_xusb_pad_program(struct tegra_xusb_pad *pad)
{
	unsigned int i;

	for (i = 0; i < pad->soc->num_lanes; i++) {
		struct tegra_xusb_lane *lane;

		if (pad->lanes[i]) {
			lane = phy_get_drvdata(pad->lanes[i]);
			tegra_xusb_lane_program(lane);
		}
	}
}

static int tegra_xusb_setup_pads(struct tegra_xusb_padctl *padctl)
{
	struct tegra_xusb_pad *pad;
	unsigned int i;

	mutex_lock(&padctl->lock);

	for (i = 0; i < padctl->soc->num_pads; i++) {
		const struct tegra_xusb_pad_soc *soc = padctl->soc->pads[i];
		int err;

		pad = tegra_xusb_pad_create(padctl, soc);
		if (IS_ERR(pad)) {
			err = PTR_ERR(pad);
			dev_err(padctl->dev, "failed to create pad %s: %d\n",
				soc->name, err);
			__tegra_xusb_remove_pads(padctl);
			mutex_unlock(&padctl->lock);
			return err;
		}

		if (!pad)
			continue;

		list_add_tail(&pad->list, &padctl->pads);
	}

	list_for_each_entry(pad, &padctl->pads, list)
		tegra_xusb_pad_program(pad);

	mutex_unlock(&padctl->lock);
	return 0;
}

bool tegra_xusb_lane_check(struct tegra_xusb_lane *lane,
				  const char *function)
{
	const char *func = lane->soc->funcs[lane->function];

	return strcmp(function, func) == 0;
}

struct tegra_xusb_lane *tegra_xusb_find_lane(struct tegra_xusb_padctl *padctl,
					     const char *type,
					     unsigned int index)
{
	struct tegra_xusb_lane *lane, *hit = ERR_PTR(-ENODEV);
	char *name;

	name = kasprintf(GFP_KERNEL, "%s-%u", type, index);
	if (!name)
		return ERR_PTR(-ENOMEM);

	list_for_each_entry(lane, &padctl->lanes, list) {
		if (strcmp(lane->soc->name, name) == 0) {
			hit = lane;
			break;
		}
	}

	kfree(name);
	return hit;
}

struct tegra_xusb_lane *
tegra_xusb_port_find_lane(struct tegra_xusb_port *port,
			  const struct tegra_xusb_lane_map *map,
			  const char *function)
{
	struct tegra_xusb_lane *lane, *match = ERR_PTR(-ENODEV);

	for (; map->type; map++) {
		if (port->index != map->port)
			continue;

		lane = tegra_xusb_find_lane(port->padctl, map->type,
					    map->index);
		if (IS_ERR(lane))
			continue;

		if (!tegra_xusb_lane_check(lane, function))
			continue;

		if (!IS_ERR(match))
			dev_err(&port->dev, "conflicting match: %s-%u / %s\n",
				map->type, map->index, match->soc->name);
		else
			match = lane;
	}

	return match;
}

static struct device_node *
tegra_xusb_find_port_node(struct tegra_xusb_padctl *padctl, const char *type,
			  unsigned int index)
{
	struct device_node *ports, *np;
	char *name;

	ports = of_get_child_by_name(padctl->dev->of_node, "ports");
	if (!ports)
		return NULL;

	name = kasprintf(GFP_KERNEL, "%s-%u", type, index);
	if (!name) {
		of_node_put(ports);
		return NULL;
	}
	np = of_get_child_by_name(ports, name);
	kfree(name);
	of_node_put(ports);

	return np;
}

struct tegra_xusb_port *
tegra_xusb_find_port(struct tegra_xusb_padctl *padctl, const char *type,
		     unsigned int index)
{
	struct tegra_xusb_port *port;
	struct device_node *np;

	np = tegra_xusb_find_port_node(padctl, type, index);
	if (!np)
		return NULL;

	list_for_each_entry(port, &padctl->ports, list) {
		if (np == port->dev.of_node) {
			of_node_put(np);
			return port;
		}
	}

	of_node_put(np);

	return NULL;
}

struct tegra_xusb_usb2_port *
tegra_xusb_find_usb2_port(struct tegra_xusb_padctl *padctl, unsigned int index)
{
	struct tegra_xusb_port *port;

	port = tegra_xusb_find_port(padctl, "usb2", index);
	if (port)
		return to_usb2_port(port);

	return NULL;
}

struct tegra_xusb_usb3_port *
tegra_xusb_find_usb3_port(struct tegra_xusb_padctl *padctl, unsigned int index)
{
	struct tegra_xusb_port *port;

	port = tegra_xusb_find_port(padctl, "usb3", index);
	if (port)
		return to_usb3_port(port);

	return NULL;
}

static void tegra_xusb_port_release(struct device *dev)
{
	struct tegra_xusb_port *port = to_tegra_xusb_port(dev);

	if (port->ops->release)
		port->ops->release(port);
}

static const struct device_type tegra_xusb_port_type = {
	.release = tegra_xusb_port_release,
};

static int tegra_xusb_port_init(struct tegra_xusb_port *port,
				struct tegra_xusb_padctl *padctl,
				struct device_node *np,
				const char *name,
				unsigned int index)
{
	int err;

	INIT_LIST_HEAD(&port->list);
	port->padctl = padctl;
	port->index = index;

	device_initialize(&port->dev);
	port->dev.type = &tegra_xusb_port_type;
	port->dev.of_node = of_node_get(np);
	port->dev.parent = padctl->dev;

	err = dev_set_name(&port->dev, "%s-%u", name, index);
	if (err < 0)
		goto unregister;

	err = device_add(&port->dev);
	if (err < 0)
		goto unregister;

	return 0;

unregister:
	device_unregister(&port->dev);
	return err;
}

static void tegra_xusb_port_unregister(struct tegra_xusb_port *port)
{
	if (!IS_ERR_OR_NULL(port->usb_role_sw)) {
		of_platform_depopulate(&port->dev);
		usb_role_switch_unregister(port->usb_role_sw);
		cancel_work_sync(&port->usb_phy_work);
		usb_remove_phy(&port->usb_phy);
		port->usb_phy.dev->driver = NULL;
	}

	if (port->ops->remove)
		port->ops->remove(port);

	device_unregister(&port->dev);
}

static const char *const modes[] = {
	[USB_DR_MODE_UNKNOWN] = "",
	[USB_DR_MODE_HOST] = "host",
	[USB_DR_MODE_PERIPHERAL] = "peripheral",
	[USB_DR_MODE_OTG] = "otg",
};

static const char * const usb_roles[] = {
	[USB_ROLE_NONE]		= "none",
	[USB_ROLE_HOST]		= "host",
	[USB_ROLE_DEVICE]	= "device",
};

static enum usb_phy_events to_usb_phy_event(enum usb_role role)
{
	switch (role) {
	case USB_ROLE_DEVICE:
		return USB_EVENT_VBUS;

	case USB_ROLE_HOST:
		return USB_EVENT_ID;

	default:
		return USB_EVENT_NONE;
	}
}

static void tegra_xusb_usb_phy_work(struct work_struct *work)
{
	struct tegra_xusb_port *port = container_of(work,
						    struct tegra_xusb_port,
						    usb_phy_work);
	enum usb_role role = usb_role_switch_get_role(port->usb_role_sw);

	usb_phy_set_event(&port->usb_phy, to_usb_phy_event(role));

	dev_dbg(&port->dev, "%s(): calling notifier for role %s\n", __func__,
		usb_roles[role]);

	atomic_notifier_call_chain(&port->usb_phy.notifier, 0, &port->usb_phy);
}

static int tegra_xusb_role_sw_set(struct usb_role_switch *sw,
				  enum usb_role role)
{
	struct tegra_xusb_port *port = usb_role_switch_get_drvdata(sw);

	dev_dbg(&port->dev, "%s(): role %s\n", __func__, usb_roles[role]);

	schedule_work(&port->usb_phy_work);

	return 0;
}

static int tegra_xusb_set_peripheral(struct usb_otg *otg,
				     struct usb_gadget *gadget)
{
	struct tegra_xusb_port *port = container_of(otg->usb_phy,
						    struct tegra_xusb_port,
						    usb_phy);

	if (gadget != NULL)
		schedule_work(&port->usb_phy_work);

	return 0;
}

static int tegra_xusb_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	struct tegra_xusb_port *port = container_of(otg->usb_phy,
						    struct tegra_xusb_port,
						    usb_phy);

	if (host != NULL)
		schedule_work(&port->usb_phy_work);

	return 0;
}


static int tegra_xusb_setup_usb_role_switch(struct tegra_xusb_port *port)
{
	struct tegra_xusb_lane *lane;
	struct usb_role_switch_desc role_sx_desc = {
		.fwnode = dev_fwnode(&port->dev),
		.set = tegra_xusb_role_sw_set,
		.allow_userspace_control = true,
	};
	int err = 0;

	/*
	 * USB role switch driver needs parent driver owner info. This is a
	 * suboptimal solution. TODO: Need to revisit this in a follow-up patch
	 * where an optimal solution is possible with changes to USB role
	 * switch driver.
	 */
	port->dev.driver = devm_kzalloc(&port->dev,
					sizeof(struct device_driver),
					GFP_KERNEL);
	if (!port->dev.driver)
		return -ENOMEM;

	port->dev.driver->owner	 = THIS_MODULE;

	port->usb_role_sw = usb_role_switch_register(&port->dev,
						     &role_sx_desc);
	if (IS_ERR(port->usb_role_sw)) {
		err = PTR_ERR(port->usb_role_sw);
		dev_err(&port->dev, "failed to register USB role switch: %d",
			err);
		return err;
	}

	INIT_WORK(&port->usb_phy_work, tegra_xusb_usb_phy_work);
	usb_role_switch_set_drvdata(port->usb_role_sw, port);

	port->usb_phy.otg = devm_kzalloc(&port->dev, sizeof(struct usb_otg),
					 GFP_KERNEL);
	if (!port->usb_phy.otg)
		return -ENOMEM;

	lane = tegra_xusb_find_lane(port->padctl, "usb2", port->index);

	/*
	 * Assign phy dev to usb-phy dev. Host/device drivers can use phy
	 * reference to retrieve usb-phy details.
	 */
	port->usb_phy.dev = &lane->pad->lanes[port->index]->dev;
	port->usb_phy.dev->driver = port->dev.driver;
	port->usb_phy.otg->usb_phy = &port->usb_phy;
	port->usb_phy.otg->set_peripheral = tegra_xusb_set_peripheral;
	port->usb_phy.otg->set_host = tegra_xusb_set_host;

	err = usb_add_phy_dev(&port->usb_phy);
	if (err < 0) {
		dev_err(&port->dev, "Failed to add USB PHY: %d\n", err);
		return err;
	}

	/* populate connector entry */
	of_platform_populate(port->dev.of_node, NULL, NULL, &port->dev);

	return err;
}

static int tegra_xusb_usb2_port_parse_dt(struct tegra_xusb_usb2_port *usb2)
{
	struct tegra_xusb_port *port = &usb2->base;
	struct device_node *np = port->dev.of_node;
	const char *mode;
	int err;

	usb2->internal = of_property_read_bool(np, "nvidia,internal");

	if (!of_property_read_string(np, "mode", &mode)) {
		int err = match_string(modes, ARRAY_SIZE(modes), mode);
		if (err < 0) {
			dev_err(&port->dev, "invalid value %s for \"mode\"\n",
				mode);
			usb2->mode = USB_DR_MODE_UNKNOWN;
		} else {
			usb2->mode = err;
		}
	} else {
		usb2->mode = USB_DR_MODE_HOST;
	}

	/* usb-role-switch property is mandatory for OTG/Peripheral modes */
	if (usb2->mode == USB_DR_MODE_PERIPHERAL ||
	    usb2->mode == USB_DR_MODE_OTG) {
		if (of_property_read_bool(np, "usb-role-switch")) {
			err = tegra_xusb_setup_usb_role_switch(port);
			if (err < 0)
				return err;
		} else {
			dev_err(&port->dev, "usb-role-switch not found for %s mode",
				modes[usb2->mode]);
			return -EINVAL;
		}
	}

	usb2->supply = regulator_get(&port->dev, "vbus");
	return PTR_ERR_OR_ZERO(usb2->supply);
}

static int tegra_xusb_add_usb2_port(struct tegra_xusb_padctl *padctl,
				    unsigned int index)
{
	struct tegra_xusb_usb2_port *usb2;
	struct device_node *np;
	int err = 0;

	/*
	 * USB2 ports don't require additional properties, but if the port is
	 * marked as disabled there is no reason to register it.
	 */
	np = tegra_xusb_find_port_node(padctl, "usb2", index);
	if (!np || !of_device_is_available(np))
		goto out;

	usb2 = kzalloc(sizeof(*usb2), GFP_KERNEL);
	if (!usb2) {
		err = -ENOMEM;
		goto out;
	}

	err = tegra_xusb_port_init(&usb2->base, padctl, np, "usb2", index);
	if (err < 0)
		goto out;

	usb2->base.ops = padctl->soc->ports.usb2.ops;

	usb2->base.lane = usb2->base.ops->map(&usb2->base);
	if (IS_ERR(usb2->base.lane)) {
		err = PTR_ERR(usb2->base.lane);
		tegra_xusb_port_unregister(&usb2->base);
		goto out;
	}

	err = tegra_xusb_usb2_port_parse_dt(usb2);
	if (err < 0) {
		tegra_xusb_port_unregister(&usb2->base);
		goto out;
	}

	list_add_tail(&usb2->base.list, &padctl->ports);

out:
	of_node_put(np);
	return err;
}

void tegra_xusb_usb2_port_release(struct tegra_xusb_port *port)
{
	struct tegra_xusb_usb2_port *usb2 = to_usb2_port(port);

	kfree(usb2);
}

void tegra_xusb_usb2_port_remove(struct tegra_xusb_port *port)
{
	struct tegra_xusb_usb2_port *usb2 = to_usb2_port(port);

	regulator_put(usb2->supply);
}

static int tegra_xusb_ulpi_port_parse_dt(struct tegra_xusb_ulpi_port *ulpi)
{
	struct tegra_xusb_port *port = &ulpi->base;
	struct device_node *np = port->dev.of_node;

	ulpi->internal = of_property_read_bool(np, "nvidia,internal");

	return 0;
}

static int tegra_xusb_add_ulpi_port(struct tegra_xusb_padctl *padctl,
				    unsigned int index)
{
	struct tegra_xusb_ulpi_port *ulpi;
	struct device_node *np;
	int err = 0;

	np = tegra_xusb_find_port_node(padctl, "ulpi", index);
	if (!np || !of_device_is_available(np))
		goto out;

	ulpi = kzalloc(sizeof(*ulpi), GFP_KERNEL);
	if (!ulpi) {
		err = -ENOMEM;
		goto out;
	}

	err = tegra_xusb_port_init(&ulpi->base, padctl, np, "ulpi", index);
	if (err < 0)
		goto out;

	ulpi->base.ops = padctl->soc->ports.ulpi.ops;

	ulpi->base.lane = ulpi->base.ops->map(&ulpi->base);
	if (IS_ERR(ulpi->base.lane)) {
		err = PTR_ERR(ulpi->base.lane);
		tegra_xusb_port_unregister(&ulpi->base);
		goto out;
	}

	err = tegra_xusb_ulpi_port_parse_dt(ulpi);
	if (err < 0) {
		tegra_xusb_port_unregister(&ulpi->base);
		goto out;
	}

	list_add_tail(&ulpi->base.list, &padctl->ports);

out:
	of_node_put(np);
	return err;
}

void tegra_xusb_ulpi_port_release(struct tegra_xusb_port *port)
{
	struct tegra_xusb_ulpi_port *ulpi = to_ulpi_port(port);

	kfree(ulpi);
}

static int tegra_xusb_hsic_port_parse_dt(struct tegra_xusb_hsic_port *hsic)
{
	/* XXX */
	return 0;
}

static int tegra_xusb_add_hsic_port(struct tegra_xusb_padctl *padctl,
				    unsigned int index)
{
	struct tegra_xusb_hsic_port *hsic;
	struct device_node *np;
	int err = 0;

	np = tegra_xusb_find_port_node(padctl, "hsic", index);
	if (!np || !of_device_is_available(np))
		goto out;

	hsic = kzalloc(sizeof(*hsic), GFP_KERNEL);
	if (!hsic) {
		err = -ENOMEM;
		goto out;
	}

	err = tegra_xusb_port_init(&hsic->base, padctl, np, "hsic", index);
	if (err < 0)
		goto out;

	hsic->base.ops = padctl->soc->ports.hsic.ops;

	hsic->base.lane = hsic->base.ops->map(&hsic->base);
	if (IS_ERR(hsic->base.lane)) {
		err = PTR_ERR(hsic->base.lane);
		goto out;
	}

	err = tegra_xusb_hsic_port_parse_dt(hsic);
	if (err < 0) {
		tegra_xusb_port_unregister(&hsic->base);
		goto out;
	}

	list_add_tail(&hsic->base.list, &padctl->ports);

out:
	of_node_put(np);
	return err;
}

void tegra_xusb_hsic_port_release(struct tegra_xusb_port *port)
{
	struct tegra_xusb_hsic_port *hsic = to_hsic_port(port);

	kfree(hsic);
}

static int tegra_xusb_usb3_port_parse_dt(struct tegra_xusb_usb3_port *usb3)
{
	struct tegra_xusb_port *port = &usb3->base;
	struct device_node *np = port->dev.of_node;
	enum usb_device_speed maximum_speed;
	u32 value;
	int err;

	err = of_property_read_u32(np, "nvidia,usb2-companion", &value);
	if (err < 0) {
		dev_err(&port->dev, "failed to read port: %d\n", err);
		return err;
	}

	usb3->port = value;

	usb3->internal = of_property_read_bool(np, "nvidia,internal");

	if (device_property_present(&port->dev, "maximum-speed")) {
		maximum_speed =  usb_get_maximum_speed(&port->dev);
		if (maximum_speed == USB_SPEED_SUPER)
			usb3->disable_gen2 = true;
		else if (maximum_speed == USB_SPEED_SUPER_PLUS)
			usb3->disable_gen2 = false;
		else
			return -EINVAL;
	}

	usb3->supply = regulator_get(&port->dev, "vbus");
	return PTR_ERR_OR_ZERO(usb3->supply);
}

static int tegra_xusb_add_usb3_port(struct tegra_xusb_padctl *padctl,
				    unsigned int index)
{
	struct tegra_xusb_usb3_port *usb3;
	struct device_node *np;
	int err = 0;

	/*
	 * If there is no supplemental configuration in the device tree the
	 * port is unusable. But it is valid to configure only a single port,
	 * hence return 0 instead of an error to allow ports to be optional.
	 */
	np = tegra_xusb_find_port_node(padctl, "usb3", index);
	if (!np || !of_device_is_available(np))
		goto out;

	usb3 = kzalloc(sizeof(*usb3), GFP_KERNEL);
	if (!usb3) {
		err = -ENOMEM;
		goto out;
	}

	err = tegra_xusb_port_init(&usb3->base, padctl, np, "usb3", index);
	if (err < 0)
		goto out;

	usb3->base.ops = padctl->soc->ports.usb3.ops;

	usb3->base.lane = usb3->base.ops->map(&usb3->base);
	if (IS_ERR(usb3->base.lane)) {
		err = PTR_ERR(usb3->base.lane);
		goto out;
	}

	err = tegra_xusb_usb3_port_parse_dt(usb3);
	if (err < 0) {
		tegra_xusb_port_unregister(&usb3->base);
		goto out;
	}

	list_add_tail(&usb3->base.list, &padctl->ports);

out:
	of_node_put(np);
	return err;
}

void tegra_xusb_usb3_port_release(struct tegra_xusb_port *port)
{
	struct tegra_xusb_usb3_port *usb3 = to_usb3_port(port);

	kfree(usb3);
}

void tegra_xusb_usb3_port_remove(struct tegra_xusb_port *port)
{
	struct tegra_xusb_usb3_port *usb3 = to_usb3_port(port);

	regulator_put(usb3->supply);
}

static void __tegra_xusb_remove_ports(struct tegra_xusb_padctl *padctl)
{
	struct tegra_xusb_port *port, *tmp;

	list_for_each_entry_safe_reverse(port, tmp, &padctl->ports, list) {
		list_del(&port->list);
		tegra_xusb_port_unregister(port);
	}
}

static int tegra_xusb_find_unused_usb3_port(struct tegra_xusb_padctl *padctl)
{
	struct device_node *np;
	unsigned int i;

	for (i = 0; i < padctl->soc->ports.usb3.count; i++) {
		np = tegra_xusb_find_port_node(padctl, "usb3", i);
		if (!np || !of_device_is_available(np))
			return i;
	}

	return -ENODEV;
}

static bool tegra_xusb_port_is_companion(struct tegra_xusb_usb2_port *usb2)
{
	unsigned int i;
	struct tegra_xusb_usb3_port *usb3;
	struct tegra_xusb_padctl *padctl = usb2->base.padctl;

	for (i = 0; i < padctl->soc->ports.usb3.count; i++) {
		usb3 = tegra_xusb_find_usb3_port(padctl, i);
		if (usb3 && usb3->port == usb2->base.index)
			return true;
	}

	return false;
}

static int tegra_xusb_update_usb3_fake_port(struct tegra_xusb_usb2_port *usb2)
{
	int fake;

	/* Disable usb3_port_fake usage by default and assign if needed */
	usb2->usb3_port_fake = -1;

	if ((usb2->mode == USB_DR_MODE_OTG ||
	     usb2->mode == USB_DR_MODE_PERIPHERAL) &&
		!tegra_xusb_port_is_companion(usb2)) {
		fake = tegra_xusb_find_unused_usb3_port(usb2->base.padctl);
		if (fake < 0) {
			dev_err(&usb2->base.dev, "no unused USB3 ports available\n");
			return -ENODEV;
		}

		dev_dbg(&usb2->base.dev, "Found unused usb3 port: %d\n", fake);
		usb2->usb3_port_fake = fake;
	}

	return 0;
}

static int tegra_xusb_setup_ports(struct tegra_xusb_padctl *padctl)
{
	struct tegra_xusb_port *port;
	struct tegra_xusb_usb2_port *usb2;
	unsigned int i;
	int err = 0;

	mutex_lock(&padctl->lock);

	for (i = 0; i < padctl->soc->ports.usb2.count; i++) {
		err = tegra_xusb_add_usb2_port(padctl, i);
		if (err < 0)
			goto remove_ports;
	}

	for (i = 0; i < padctl->soc->ports.ulpi.count; i++) {
		err = tegra_xusb_add_ulpi_port(padctl, i);
		if (err < 0)
			goto remove_ports;
	}

	for (i = 0; i < padctl->soc->ports.hsic.count; i++) {
		err = tegra_xusb_add_hsic_port(padctl, i);
		if (err < 0)
			goto remove_ports;
	}

	for (i = 0; i < padctl->soc->ports.usb3.count; i++) {
		err = tegra_xusb_add_usb3_port(padctl, i);
		if (err < 0)
			goto remove_ports;
	}

	if (padctl->soc->need_fake_usb3_port) {
		for (i = 0; i < padctl->soc->ports.usb2.count; i++) {
			usb2 = tegra_xusb_find_usb2_port(padctl, i);
			if (!usb2)
				continue;

			err = tegra_xusb_update_usb3_fake_port(usb2);
			if (err < 0)
				goto remove_ports;
		}
	}

	list_for_each_entry(port, &padctl->ports, list) {
		err = port->ops->enable(port);
		if (err < 0)
			dev_err(padctl->dev, "failed to enable port %s: %d\n",
				dev_name(&port->dev), err);
	}

	goto unlock;

remove_ports:
	__tegra_xusb_remove_ports(padctl);
unlock:
	mutex_unlock(&padctl->lock);
	return err;
}

static void tegra_xusb_remove_ports(struct tegra_xusb_padctl *padctl)
{
	mutex_lock(&padctl->lock);
	__tegra_xusb_remove_ports(padctl);
	mutex_unlock(&padctl->lock);
}

static int tegra_xusb_padctl_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct tegra_xusb_padctl_soc *soc;
	struct tegra_xusb_padctl *padctl;
	const struct of_device_id *match;
	int err;

	/* for backwards compatibility with old device trees */
	np = of_get_child_by_name(np, "pads");
	if (!np) {
		dev_warn(&pdev->dev, "deprecated DT, using legacy driver\n");
		return tegra_xusb_padctl_legacy_probe(pdev);
	}

	of_node_put(np);

	match = of_match_node(tegra_xusb_padctl_of_match, pdev->dev.of_node);
	soc = match->data;

	padctl = soc->ops->probe(&pdev->dev, soc);
	if (IS_ERR(padctl))
		return PTR_ERR(padctl);

	platform_set_drvdata(pdev, padctl);
	INIT_LIST_HEAD(&padctl->ports);
	INIT_LIST_HEAD(&padctl->lanes);
	INIT_LIST_HEAD(&padctl->pads);
	mutex_init(&padctl->lock);

	padctl->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(padctl->regs)) {
		err = PTR_ERR(padctl->regs);
		goto remove;
	}

	padctl->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(padctl->rst)) {
		err = PTR_ERR(padctl->rst);
		goto remove;
	}

	padctl->supplies = devm_kcalloc(&pdev->dev, padctl->soc->num_supplies,
					sizeof(*padctl->supplies), GFP_KERNEL);
	if (!padctl->supplies) {
		err = -ENOMEM;
		goto remove;
	}

	regulator_bulk_set_supply_names(padctl->supplies,
					padctl->soc->supply_names,
					padctl->soc->num_supplies);

	err = devm_regulator_bulk_get(&pdev->dev, padctl->soc->num_supplies,
				      padctl->supplies);
	if (err < 0) {
		dev_err_probe(&pdev->dev, err, "failed to get regulators\n");
		goto remove;
	}

	err = reset_control_deassert(padctl->rst);
	if (err < 0)
		goto remove;

	err = regulator_bulk_enable(padctl->soc->num_supplies,
				    padctl->supplies);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to enable supplies: %d\n", err);
		goto reset;
	}

	err = tegra_xusb_setup_pads(padctl);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to setup pads: %d\n", err);
		goto power_down;
	}

	err = tegra_xusb_setup_ports(padctl);
	if (err) {
		const char *level = KERN_ERR;

		if (err == -EPROBE_DEFER)
			level = KERN_DEBUG;

		dev_printk(level, &pdev->dev,
			   dev_fmt("failed to setup XUSB ports: %d\n"), err);
		goto remove_pads;
	}

	return 0;

remove_pads:
	tegra_xusb_remove_pads(padctl);
power_down:
	regulator_bulk_disable(padctl->soc->num_supplies, padctl->supplies);
reset:
	reset_control_assert(padctl->rst);
remove:
	platform_set_drvdata(pdev, NULL);
	soc->ops->remove(padctl);
	return err;
}

static int tegra_xusb_padctl_remove(struct platform_device *pdev)
{
	struct tegra_xusb_padctl *padctl = platform_get_drvdata(pdev);
	int err;

	tegra_xusb_remove_ports(padctl);
	tegra_xusb_remove_pads(padctl);

	err = regulator_bulk_disable(padctl->soc->num_supplies,
				     padctl->supplies);
	if (err < 0)
		dev_err(&pdev->dev, "failed to disable supplies: %d\n", err);

	err = reset_control_assert(padctl->rst);
	if (err < 0)
		dev_err(&pdev->dev, "failed to assert reset: %d\n", err);

	padctl->soc->ops->remove(padctl);

	return 0;
}

static __maybe_unused int tegra_xusb_padctl_suspend_noirq(struct device *dev)
{
	struct tegra_xusb_padctl *padctl = dev_get_drvdata(dev);

	if (padctl->soc && padctl->soc->ops && padctl->soc->ops->suspend_noirq)
		return padctl->soc->ops->suspend_noirq(padctl);

	return 0;
}

static __maybe_unused int tegra_xusb_padctl_resume_noirq(struct device *dev)
{
	struct tegra_xusb_padctl *padctl = dev_get_drvdata(dev);

	if (padctl->soc && padctl->soc->ops && padctl->soc->ops->resume_noirq)
		return padctl->soc->ops->resume_noirq(padctl);

	return 0;
}

static const struct dev_pm_ops tegra_xusb_padctl_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(tegra_xusb_padctl_suspend_noirq,
				      tegra_xusb_padctl_resume_noirq)
};

static struct platform_driver tegra_xusb_padctl_driver = {
	.driver = {
		.name = "tegra-xusb-padctl",
		.of_match_table = tegra_xusb_padctl_of_match,
		.pm = &tegra_xusb_padctl_pm_ops,
	},
	.probe = tegra_xusb_padctl_probe,
	.remove = tegra_xusb_padctl_remove,
};
module_platform_driver(tegra_xusb_padctl_driver);

struct tegra_xusb_padctl *tegra_xusb_padctl_get(struct device *dev)
{
	struct tegra_xusb_padctl *padctl;
	struct platform_device *pdev;
	struct device_node *np;

	np = of_parse_phandle(dev->of_node, "nvidia,xusb-padctl", 0);
	if (!np)
		return ERR_PTR(-EINVAL);

	/*
	 * This is slightly ugly. A better implementation would be to keep a
	 * registry of pad controllers, but since there will almost certainly
	 * only ever be one per SoC that would be a little overkill.
	 */
	pdev = of_find_device_by_node(np);
	if (!pdev) {
		of_node_put(np);
		return ERR_PTR(-ENODEV);
	}

	of_node_put(np);

	padctl = platform_get_drvdata(pdev);
	if (!padctl) {
		put_device(&pdev->dev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	return padctl;
}
EXPORT_SYMBOL_GPL(tegra_xusb_padctl_get);

void tegra_xusb_padctl_put(struct tegra_xusb_padctl *padctl)
{
	if (padctl)
		put_device(padctl->dev);
}
EXPORT_SYMBOL_GPL(tegra_xusb_padctl_put);

int tegra_xusb_padctl_usb3_save_context(struct tegra_xusb_padctl *padctl,
					unsigned int port)
{
	if (padctl->soc->ops->usb3_save_context)
		return padctl->soc->ops->usb3_save_context(padctl, port);

	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(tegra_xusb_padctl_usb3_save_context);

int tegra_xusb_padctl_hsic_set_idle(struct tegra_xusb_padctl *padctl,
				    unsigned int port, bool idle)
{
	if (padctl->soc->ops->hsic_set_idle)
		return padctl->soc->ops->hsic_set_idle(padctl, port, idle);

	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(tegra_xusb_padctl_hsic_set_idle);

int tegra_xusb_padctl_enable_phy_sleepwalk(struct tegra_xusb_padctl *padctl, struct phy *phy,
					   enum usb_device_speed speed)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	if (lane->pad->ops->enable_phy_sleepwalk)
		return lane->pad->ops->enable_phy_sleepwalk(lane, speed);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(tegra_xusb_padctl_enable_phy_sleepwalk);

int tegra_xusb_padctl_disable_phy_sleepwalk(struct tegra_xusb_padctl *padctl, struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	if (lane->pad->ops->disable_phy_sleepwalk)
		return lane->pad->ops->disable_phy_sleepwalk(lane);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(tegra_xusb_padctl_disable_phy_sleepwalk);

int tegra_xusb_padctl_enable_phy_wake(struct tegra_xusb_padctl *padctl, struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	if (lane->pad->ops->enable_phy_wake)
		return lane->pad->ops->enable_phy_wake(lane);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(tegra_xusb_padctl_enable_phy_wake);

int tegra_xusb_padctl_disable_phy_wake(struct tegra_xusb_padctl *padctl, struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	if (lane->pad->ops->disable_phy_wake)
		return lane->pad->ops->disable_phy_wake(lane);

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(tegra_xusb_padctl_disable_phy_wake);

bool tegra_xusb_padctl_remote_wake_detected(struct tegra_xusb_padctl *padctl, struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);

	if (lane->pad->ops->remote_wake_detected)
		return lane->pad->ops->remote_wake_detected(lane);

	return false;
}
EXPORT_SYMBOL_GPL(tegra_xusb_padctl_remote_wake_detected);

int tegra_xusb_padctl_usb3_set_lfps_detect(struct tegra_xusb_padctl *padctl,
					   unsigned int port, bool enable)
{
	if (padctl->soc->ops->usb3_set_lfps_detect)
		return padctl->soc->ops->usb3_set_lfps_detect(padctl, port,
							      enable);

	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(tegra_xusb_padctl_usb3_set_lfps_detect);

int tegra_xusb_padctl_set_vbus_override(struct tegra_xusb_padctl *padctl,
							bool val)
{
	if (padctl->soc->ops->vbus_override)
		return padctl->soc->ops->vbus_override(padctl, val);

	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tegra_xusb_padctl_set_vbus_override);

int tegra_phy_xusb_utmi_port_reset(struct phy *phy)
{
	struct tegra_xusb_lane *lane = phy_get_drvdata(phy);
	struct tegra_xusb_padctl *padctl = lane->pad->padctl;

	if (padctl->soc->ops->utmi_port_reset)
		return padctl->soc->ops->utmi_port_reset(phy);

	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(tegra_phy_xusb_utmi_port_reset);

void tegra_phy_xusb_utmi_pad_power_on(struct phy *phy)
{
	struct tegra_xusb_lane *lane;
	struct tegra_xusb_padctl *padctl;

	if (!phy)
		return;

	lane = phy_get_drvdata(phy);
	padctl = lane->pad->padctl;

	if (padctl->soc->ops->utmi_pad_power_on)
		padctl->soc->ops->utmi_pad_power_on(phy);
}
EXPORT_SYMBOL_GPL(tegra_phy_xusb_utmi_pad_power_on);

void tegra_phy_xusb_utmi_pad_power_down(struct phy *phy)
{
	struct tegra_xusb_lane *lane;
	struct tegra_xusb_padctl *padctl;

	if (!phy)
		return;

	lane = phy_get_drvdata(phy);
	padctl = lane->pad->padctl;

	if (padctl->soc->ops->utmi_pad_power_down)
		padctl->soc->ops->utmi_pad_power_down(phy);
}
EXPORT_SYMBOL_GPL(tegra_phy_xusb_utmi_pad_power_down);

int tegra_xusb_padctl_get_usb3_companion(struct tegra_xusb_padctl *padctl,
				    unsigned int port)
{
	struct tegra_xusb_usb2_port *usb2;
	struct tegra_xusb_usb3_port *usb3;
	int i;

	usb2 = tegra_xusb_find_usb2_port(padctl, port);
	if (!usb2)
		return -EINVAL;

	for (i = 0; i < padctl->soc->ports.usb3.count; i++) {
		usb3 = tegra_xusb_find_usb3_port(padctl, i);
		if (usb3 && usb3->port == usb2->base.index)
			return usb3->base.index;
	}

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(tegra_xusb_padctl_get_usb3_companion);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("Tegra XUSB Pad Controller driver");
MODULE_LICENSE("GPL v2");
