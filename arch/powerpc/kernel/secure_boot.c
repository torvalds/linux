// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 IBM Corporation
 * Author: Nayna Jain
 */
#include <linux/types.h>
#include <linux/of.h>
#include <asm/secure_boot.h>

static struct device_node *get_ppc_fw_sb_node(void)
{
	static const struct of_device_id ids[] = {
		{ .compatible = "ibm,secureboot", },
		{ .compatible = "ibm,secureboot-v1", },
		{ .compatible = "ibm,secureboot-v2", },
		{},
	};

	return of_find_matching_node(NULL, ids);
}

bool is_ppc_secureboot_enabled(void)
{
	struct device_node *node;
	bool enabled = false;

	node = get_ppc_fw_sb_node();
	enabled = of_property_read_bool(node, "os-secureboot-enforcing");

	of_node_put(node);

	pr_info("Secure boot mode %s\n", enabled ? "enabled" : "disabled");

	return enabled;
}

bool is_ppc_trustedboot_enabled(void)
{
	struct device_node *node;
	bool enabled = false;

	node = get_ppc_fw_sb_node();
	enabled = of_property_read_bool(node, "trusted-enabled");

	of_node_put(node);

	pr_info("Trusted boot mode %s\n", enabled ? "enabled" : "disabled");

	return enabled;
}
