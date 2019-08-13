// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2012 ARM Limited
 */

#include <linux/gpio/driver.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mfd/core.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_data/syscon.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/vexpress.h>

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

#define SYS_HBI_MASK		0xfff
#define SYS_PROCIDx_HBI_SHIFT	0

#define SYS_MISC_MASTERSITE	(1 << 14)

void vexpress_flags_set(u32 data)
{
	static void __iomem *base;

	if (!base) {
		struct device_node *node = of_find_compatible_node(NULL, NULL,
				"arm,vexpress-sysreg");

		base = of_iomap(node, 0);
	}

	if (WARN_ON(!base))
		return;

	writel(~0, base + SYS_FLAGSCLR);
	writel(data, base + SYS_FLAGSSET);
}

/* The sysreg block is just a random collection of various functions... */

static struct syscon_platform_data vexpress_sysreg_sys_id_pdata = {
	.label = "sys_id",
};

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

static struct syscon_platform_data vexpress_sysreg_sys_misc_pdata = {
	.label = "sys_misc",
};

static struct syscon_platform_data vexpress_sysreg_sys_procid_pdata = {
	.label = "sys_procid",
};

static struct mfd_cell vexpress_sysreg_cells[] = {
	{
		.name = "syscon",
		.num_resources = 1,
		.resources = (struct resource []) {
			DEFINE_RES_MEM(SYS_ID, 0x4),
		},
		.platform_data = &vexpress_sysreg_sys_id_pdata,
		.pdata_size = sizeof(vexpress_sysreg_sys_id_pdata),
	}, {
		.name = "basic-mmio-gpio",
		.of_compatible = "arm,vexpress-sysreg,sys_led",
		.num_resources = 1,
		.resources = (struct resource []) {
			DEFINE_RES_MEM_NAMED(SYS_LED, 0x4, "dat"),
		},
		.platform_data = &vexpress_sysreg_sys_led_pdata,
		.pdata_size = sizeof(vexpress_sysreg_sys_led_pdata),
	}, {
		.name = "basic-mmio-gpio",
		.of_compatible = "arm,vexpress-sysreg,sys_mci",
		.num_resources = 1,
		.resources = (struct resource []) {
			DEFINE_RES_MEM_NAMED(SYS_MCI, 0x4, "dat"),
		},
		.platform_data = &vexpress_sysreg_sys_mci_pdata,
		.pdata_size = sizeof(vexpress_sysreg_sys_mci_pdata),
	}, {
		.name = "basic-mmio-gpio",
		.of_compatible = "arm,vexpress-sysreg,sys_flash",
		.num_resources = 1,
		.resources = (struct resource []) {
			DEFINE_RES_MEM_NAMED(SYS_FLASH, 0x4, "dat"),
		},
		.platform_data = &vexpress_sysreg_sys_flash_pdata,
		.pdata_size = sizeof(vexpress_sysreg_sys_flash_pdata),
	}, {
		.name = "syscon",
		.num_resources = 1,
		.resources = (struct resource []) {
			DEFINE_RES_MEM(SYS_MISC, 0x4),
		},
		.platform_data = &vexpress_sysreg_sys_misc_pdata,
		.pdata_size = sizeof(vexpress_sysreg_sys_misc_pdata),
	}, {
		.name = "syscon",
		.num_resources = 1,
		.resources = (struct resource []) {
			DEFINE_RES_MEM(SYS_PROCID0, 0x8),
		},
		.platform_data = &vexpress_sysreg_sys_procid_pdata,
		.pdata_size = sizeof(vexpress_sysreg_sys_procid_pdata),
	}, {
		.name = "vexpress-syscfg",
		.num_resources = 1,
		.resources = (struct resource []) {
			DEFINE_RES_MEM(SYS_CFGDATA, 0xc),
		},
	}
};

static int vexpress_sysreg_probe(struct platform_device *pdev)
{
	struct resource *mem;
	void __iomem *base;
	struct gpio_chip *mmc_gpio_chip;
	int master;
	u32 dt_hbi;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -EINVAL;

	base = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (!base)
		return -ENOMEM;

	master = readl(base + SYS_MISC) & SYS_MISC_MASTERSITE ?
			VEXPRESS_SITE_DB2 : VEXPRESS_SITE_DB1;
	vexpress_config_set_master(master);

	/* Confirm board type against DT property, if available */
	if (of_property_read_u32(of_root, "arm,hbi", &dt_hbi) == 0) {
		u32 id = readl(base + (master == VEXPRESS_SITE_DB1 ?
				 SYS_PROCID0 : SYS_PROCID1));
		u32 hbi = (id >> SYS_PROCIDx_HBI_SHIFT) & SYS_HBI_MASK;

		if (WARN_ON(dt_hbi != hbi))
			dev_warn(&pdev->dev, "DT HBI (%x) is not matching hardware (%x)!\n",
					dt_hbi, hbi);
	}

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
	gpiochip_add_data(mmc_gpio_chip, NULL);

	return mfd_add_devices(&pdev->dev, PLATFORM_DEVID_AUTO,
			vexpress_sysreg_cells,
			ARRAY_SIZE(vexpress_sysreg_cells), mem, 0, NULL);
}

static const struct of_device_id vexpress_sysreg_match[] = {
	{ .compatible = "arm,vexpress-sysreg", },
	{},
};

static struct platform_driver vexpress_sysreg_driver = {
	.driver = {
		.name = "vexpress-sysreg",
		.of_match_table = vexpress_sysreg_match,
	},
	.probe = vexpress_sysreg_probe,
};

static int __init vexpress_sysreg_init(void)
{
	struct device_node *node;

	/* Need the sysreg early, before any other device... */
	for_each_matching_node(node, vexpress_sysreg_match)
		of_platform_device_create(node, NULL, NULL);

	return platform_driver_register(&vexpress_sysreg_driver);
}
core_initcall(vexpress_sysreg_init);
