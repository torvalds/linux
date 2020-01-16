// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 IBM Corporation
 * Author: Nayna Jain
 */
#include <linux/types.h>
#include <linux/of.h>
#include <asm/secure_boot.h>

static struct device_yesde *get_ppc_fw_sb_yesde(void)
{
	static const struct of_device_id ids[] = {
		{ .compatible = "ibm,secureboot", },
		{ .compatible = "ibm,secureboot-v1", },
		{ .compatible = "ibm,secureboot-v2", },
		{},
	};

	return of_find_matching_yesde(NULL, ids);
}

bool is_ppc_secureboot_enabled(void)
{
	struct device_yesde *yesde;
	bool enabled = false;

	yesde = get_ppc_fw_sb_yesde();
	enabled = of_property_read_bool(yesde, "os-secureboot-enforcing");

	of_yesde_put(yesde);

	pr_info("Secure boot mode %s\n", enabled ? "enabled" : "disabled");

	return enabled;
}

bool is_ppc_trustedboot_enabled(void)
{
	struct device_yesde *yesde;
	bool enabled = false;

	yesde = get_ppc_fw_sb_yesde();
	enabled = of_property_read_bool(yesde, "trusted-enabled");

	of_yesde_put(yesde);

	pr_info("Trusted boot mode %s\n", enabled ? "enabled" : "disabled");

	return enabled;
}
