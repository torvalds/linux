// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2012 ARM Limited
 */

#include <linux/gpio/driver.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/stat.h>

#define SYS_ID			0x000
#define SYS_SW			0x004
#define SYS_LED			0x008
#define SYS_100HZ		0x024
#define SYS_FLAGSSET		0x030
#define SYS_FLAGSCLR		0x034
#define SYS_NVFLAGS		0x038
#define SYS_NVFLAGSSET		0x038
#define SYS_NVFLAGSCLR		0x03c
#define SYS_MCI			0x048
#define SYS_FLASH		0x04c
#define SYS_CFGSW		0x058
#define SYS_24MHZ		0x05c
#define SYS_MISC		0x060
#define SYS_DMA			0x064
#define SYS_PROCID0		0x084
#define SYS_PROCID1		0x088
#define SYS_CFGDATA		0x0a0
#define SYS_CFGCTRL		0x0a4
#define SYS_CFGSTAT		0x0a8

/* The sysreg block is just a random collection of various functions... */

static struct bgpio_pdata vexpress_sysreg_sys_led_pdata = {
	.label = "sys_led",
	.base = -1,
	.ngpio = 8,
};

static struct bgpio_pdata vexpress_sysreg_sys_mci_pdata = {
	.label = "sys_mci",
	.base = -1,
	.ngpio = 2,
};

static struct bgpio_pdata vexpress_sysreg_sys_flash_pdata = {
	.label = "sys_flash",
	.base = -1,
	.ngpio = 1,
};

static struct mfd_cell vexpress_sysreg_cells[] = {
	{
		.name = "basic-mmio-gpio",
		.of_compatible = "arm,vexpress-sysreg,sys_led",
		.num_resources = 1,
		.resources = &DEFINE_RES_MEM_NAMED(SYS_LED, 0x4, "dat"),
		.platform_data = &vexpress_sysreg_sys_led_pdata,
		.pdata_size = sizeof(vexpress_sysreg_sys_led_pdata),
	}, {
		.name = "basic-mmio-gpio",
		.of_compatible = "arm,vexpress-sysreg,sys_mci",
		.num_resources = 1,
		.resources = &DEFINE_RES_MEM_NAMED(SYS_MCI, 0x4, "dat"),
		.platform_data = &vexpress_sysreg_sys_mci_pdata,
		.pdata_size = sizeof(vexpress_sysreg_sys_mci_pdata),
	}, {
		.name = "basic-mmio-gpio",
		.of_compatible = "arm,vexpress-sysreg,sys_flash",
		.num_resources = 1,
		.resources = &DEFINE_RES_MEM_NAMED(SYS_FLASH, 0x4, "dat"),
		.platform_data = &vexpress_sysreg_sys_flash_pdata,
		.pdata_size = sizeof(vexpress_sysreg_sys_flash_pdata),
	}, {
		.name = "vexpress-syscfg",
		.num_resources = 1,
		.resources = &DEFINE_RES_MEM(SYS_MISC, 0x4c),
	}
};

static int vexpress_sysreg_probe(struct platform_device *pdev)
{
	struct resource *mem;
	void __iomem *base;
	struct gpio_chip *mmc_gpio_chip;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -EINVAL;

	base = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (!base)
		return -ENOMEM;

	/*
	 * Duplicated SYS_MCI pseudo-GPIO controller for compatibility with
	 * older trees using sysreg node for MMC control lines.
	 */
	mmc_gpio_chip = devm_kzalloc(&pdev->dev, sizeof(*mmc_gpio_chip),
			GFP_KERNEL);
	if (!mmc_gpio_chip)
		return -ENOMEM;
	bgpio_init(mmc_gpio_chip, &pdev->dev, 0x4, base + SYS_MCI,
			NULL, NULL, NULL, NULL, 0);
	mmc_gpio_chip->ngpio = 2;
	devm_gpiochip_add_data(&pdev->dev, mmc_gpio_chip, NULL);

	return devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_AUTO,
			vexpress_sysreg_cells,
			ARRAY_SIZE(vexpress_sysreg_cells), mem, 0, NULL);
}

static const struct of_device_id vexpress_sysreg_match[] = {
	{ .compatible = "arm,vexpress-sysreg", },
	{},
};
MODULE_DEVICE_TABLE(of, vexpress_sysreg_match);

static struct platform_driver vexpress_sysreg_driver = {
	.driver = {
		.name = "vexpress-sysreg",
		.of_match_table = vexpress_sysreg_match,
	},
	.probe = vexpress_sysreg_probe,
};

module_platform_driver(vexpress_sysreg_driver);
MODULE_DESCRIPTION("Versatile Express system registers driver");
MODULE_LICENSE("GPL v2");
