/*
 * drivers/video/tegra/host/dev.c
 *
 * Tegra Graphics Host Driver Entrypoint
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "dev.h"

#include <linux/nvhost.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/file.h>
#include <linux/nvhost.h>

#include <asm/io.h>

#define DRIVER_NAME "tegra_grhost"
#define IFACE_NAME "nvhost"

static int __devinit nvhost_probe(struct platform_device *pdev)
{
	struct nvhost_master *host;

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->pdev = pdev;

	platform_set_drvdata(pdev, host);

	nvhost_bus_register(host);

	dev_info(&pdev->dev, "initialized\n");
	return 0;
}

static int __exit nvhost_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver nvhost_driver = {
	.probe = nvhost_probe,
	.remove = __exit_p(nvhost_remove),
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME
	}
};

static int __init nvhost_mod_init(void)
{
	return platform_driver_register(&nvhost_driver);
}

static void __exit nvhost_mod_exit(void)
{
	platform_driver_unregister(&nvhost_driver);
}

module_init(nvhost_mod_init);
module_exit(nvhost_mod_exit);

MODULE_AUTHOR("NVIDIA");
MODULE_DESCRIPTION("Graphics host driver for Tegra products");
MODULE_VERSION("1.0");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform-nvhost");
