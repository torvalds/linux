/*
 * Copyright 2013 Michael Ellerman, Guo Chao, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/module.h>
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

static int powernv_rng_remove(struct platform_device *pdev)
{
	hwrng_unregister(&powernv_hwrng);

	return 0;
}

static int powernv_rng_probe(struct platform_device *pdev)
{
	int rc;

	rc = hwrng_register(&powernv_hwrng);
	if (rc) {
		/* We only register one device, ignore any others */
		if (rc == -EEXIST)
			rc = -ENODEV;

		return rc;
	}

	pr_info("Registered powernv hwrng.\n");

	return 0;
}

static struct of_device_id powernv_rng_match[] = {
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
	.remove = powernv_rng_remove,
};
module_platform_driver(powernv_rng_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bare metal HWRNG driver for POWER7+ and above");
