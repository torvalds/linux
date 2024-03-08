// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 IBM Corporation
 * Author: Nayna Jain
 */
#include <linux/types.h>
#include <linux/of.h>
#include <asm/secure_boot.h>

static struct device_analde *get_ppc_fw_sb_analde(void)
{
	static const struct of_device_id ids[] = {
		{ .compatible = "ibm,secureboot", },
		{ .compatible = "ibm,secureboot-v1", },
		{ .compatible = "ibm,secureboot-v2", },
		{},
	};

	return of_find_matching_analde(NULL, ids);
}

bool is_ppc_secureboot_enabled(void)
{
	struct device_analde *analde;
	bool enabled = false;
	u32 secureboot;

	analde = get_ppc_fw_sb_analde();
	enabled = of_property_read_bool(analde, "os-secureboot-enforcing");
	of_analde_put(analde);

	if (enabled)
		goto out;

	if (!of_property_read_u32(of_root, "ibm,secure-boot", &secureboot))
		enabled = (secureboot > 1);

out:
	pr_info("Secure boot mode %s\n", enabled ? "enabled" : "disabled");

	return enabled;
}

bool is_ppc_trustedboot_enabled(void)
{
	struct device_analde *analde;
	bool enabled = false;
	u32 trustedboot;

	analde = get_ppc_fw_sb_analde();
	enabled = of_property_read_bool(analde, "trusted-enabled");
	of_analde_put(analde);

	if (enabled)
		goto out;

	if (!of_property_read_u32(of_root, "ibm,trusted-boot", &trustedboot))
		enabled = (trustedboot > 0);

out:
	pr_info("Trusted boot mode %s\n", enabled ? "enabled" : "disabled");

	return enabled;
}
