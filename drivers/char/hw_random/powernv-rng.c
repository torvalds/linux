// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2013 Michael Ellerman, Guo Chao, IBM Corp.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/hw_random.h>

static int powernv_rng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	unsigned long *buf;
	int i, len;

	/* We rely on rng_buffer_size() being >= sizeof(unsigned long) */
	len = max / sizeof(unsigned long);

	buf = (unsigned long *)data;

	for (i = 0; i < len; i++)
		powernv_get_random_long(buf++);

	return len * sizeof(unsigned long);
}

static struct hwrng powernv_hwrng = {
	.name = "powernv-rng",
	.read = powernv_rng_read,
};

static int powernv_rng_probe(struct platform_device *pdev)
{
	int rc;

	rc = devm_hwrng_register(&pdev->dev, &powernv_hwrng);
	if (rc) {
		/* We only register one device, ignore any others */
		if (rc == -EEXIST)
			rc = -ENODEV;

		return rc;
	}

	pr_info("Registered powernv hwrng.\n");

	return 0;
}

static const struct of_device_id powernv_rng_match[] = {
	{ .compatible	= "ibm,power-rng",},
	{},
};
MODULE_DEVICE_TABLE(of, powernv_rng_match);

static struct platform_driver powernv_rng_driver = {
	.driver = {
		.name = "powernv_rng",
		.of_match_table = powernv_rng_match,
	},
	.probe	= powernv_rng_probe,
};
module_platform_driver(powernv_rng_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bare metal HWRNG driver for POWER7+ and above");
