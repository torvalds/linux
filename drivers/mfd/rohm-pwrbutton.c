// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Shared helper for ROHM PMIC power button registration
 *
 * Copyright 2018, 2019 ROHM Semiconductors
 * Copyright 2026 Google LLC
 */

#include <linux/device/devres.h>
#include <linux/gfp_types.h>
#include <linux/input.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/slab.h>

#include "rohm-pwrbutton.h"

#define GPIO_KEYS  0	/* Node corresponding to gpio-keys device itself */
#define PWRON_KEY  1	/* Node describing power button in gpio-keys */

static int rohm_pwrbutton_register_swnodes(const struct software_node *nodes)
{
	const struct software_node * const node_group[] = {
		&nodes[GPIO_KEYS], &nodes[PWRON_KEY], NULL
	};

	return software_node_register_node_group(node_group);
}

static void rohm_pwrbutton_unregister_swnodes(void *data)
{
	const struct software_node *nodes = data;
	const struct software_node * const node_group[] = {
		&nodes[GPIO_KEYS], &nodes[PWRON_KEY], NULL
	};

	software_node_unregister_node_group(node_group);
}

int rohm_register_pwrbutton(struct device *dev, int irq, const char *name,
			    bool wakeup, struct irq_domain *irq_domain)
{
	const struct resource res[] = {
		DEFINE_RES_IRQ_NAMED(irq, name),
	};
	struct mfd_cell gpio_keys_cell = {
		.name = "gpio-keys",
		.resources = res,
		.num_resources = ARRAY_SIZE(res),
	};
	struct property_entry *parent_props;
	struct property_entry *child_props;
	struct software_node *nodes;
	int n_props;
	int ret;

	if (irq <= 0)
		return -EINVAL;

	nodes = devm_kcalloc(dev, 2, sizeof(*nodes), GFP_KERNEL);
	if (!nodes)
		return -ENOMEM;

	nodes[GPIO_KEYS].name = devm_kasprintf(dev, GFP_KERNEL, "%s-power-key", dev_name(dev));
	if (!nodes[GPIO_KEYS].name)
		return -ENOMEM;

	parent_props = devm_kcalloc(dev, 2, sizeof(*parent_props), GFP_KERNEL);
	if (!parent_props)
		return -ENOMEM;

	parent_props[0] = PROPERTY_ENTRY_STRING("label", name);
	nodes[GPIO_KEYS].properties = parent_props;

	n_props = 2; /* linux,code and terminator */
	if (wakeup)
		n_props++;

	child_props = devm_kcalloc(dev, n_props, sizeof(*child_props), GFP_KERNEL);
	if (!child_props)
		return -ENOMEM;

	child_props[0] = PROPERTY_ENTRY_U32("linux,code", KEY_POWER);
	if (wakeup)
		child_props[1] = PROPERTY_ENTRY_BOOL("wakeup-source");

	nodes[PWRON_KEY].parent = &nodes[GPIO_KEYS];
	nodes[PWRON_KEY].properties = child_props;

	ret = rohm_pwrbutton_register_swnodes(nodes);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, rohm_pwrbutton_unregister_swnodes, nodes);
	if (ret)
		return ret;

	gpio_keys_cell.swnode = &nodes[GPIO_KEYS];

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, &gpio_keys_cell, 1,
				   NULL, 0, irq_domain);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register power-button");

	return 0;
}
EXPORT_SYMBOL_GPL(rohm_register_pwrbutton);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Torokhov <dmitry.torokhov@gmail.com>");
MODULE_DESCRIPTION("Shared helper for ROHM PMIC power button registration");
