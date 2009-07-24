/*
 *  linux/arch/arm/mach-nomadik/board-8815nhk.c
 *
 *  Copyright (C) STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 *  NHK15 board specifc driver definition
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/io.h>
#include <asm/sizes.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>
#include <mach/setup.h>
#include <mach/nand.h>
#include <mach/fsmc.h>
#include "clock.h"

/* These adresses span 16MB, so use three individual pages */
static struct resource nhk8815_nand_resources[] = {
	{
		.name = "nand_addr",
		.start = NAND_IO_ADDR,
		.end = NAND_IO_ADDR + 0xfff,
		.flags = IORESOURCE_MEM,
	}, {
		.name = "nand_cmd",
		.start = NAND_IO_CMD,
		.end = NAND_IO_CMD + 0xfff,
		.flags = IORESOURCE_MEM,
	}, {
		.name = "nand_data",
		.start = NAND_IO_DATA,
		.end = NAND_IO_DATA + 0xfff,
		.flags = IORESOURCE_MEM,
	}
};

static int nhk8815_nand_init(void)
{
	/* FSMC setup for nand chip select (8-bit nand in 8815NHK) */
	writel(0x0000000E, FSMC_PCR(0));
	writel(0x000D0A00, FSMC_PMEM(0));
	writel(0x00100A00, FSMC_PATT(0));

	/* enable access to the chip select area */
	writel(readl(FSMC_PCR(0)) | 0x04, FSMC_PCR(0));

	return 0;
}

/*
 * These partitions are the same as those used in the 2.6.20 release
 * shipped by the vendor; the first two partitions are mandated
 * by the boot ROM, and the bootloader area is somehow oversized...
 */
static struct mtd_partition nhk8815_partitions[] = {
	{
		.name	= "X-Loader(NAND)",
		.offset = 0,
		.size	= SZ_256K,
	}, {
		.name	= "MemInit(NAND)",
		.offset	= MTDPART_OFS_APPEND,
		.size	= SZ_256K,
	}, {
		.name	= "BootLoader(NAND)",
		.offset	= MTDPART_OFS_APPEND,
		.size	= SZ_2M,
	}, {
		.name	= "Kernel zImage(NAND)",
		.offset	= MTDPART_OFS_APPEND,
		.size	= 3 * SZ_1M,
	}, {
		.name	= "Root Filesystem(NAND)",
		.offset	= MTDPART_OFS_APPEND,
		.size	= 22 * SZ_1M,
	}, {
		.name	= "User Filesystem(NAND)",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL,
	}
};

static struct nomadik_nand_platform_data nhk8815_nand_data = {
	.parts		= nhk8815_partitions,
	.nparts		= ARRAY_SIZE(nhk8815_partitions),
	.options	= NAND_COPYBACK | NAND_CACHEPRG | NAND_NO_PADDING \
			| NAND_NO_READRDY | NAND_NO_AUTOINCR,
	.init		= nhk8815_nand_init,
};

static struct platform_device nhk8815_nand_device = {
	.name		= "nomadik_nand",
	.dev		= {
		.platform_data = &nhk8815_nand_data,
	},
	.resource	= nhk8815_nand_resources,
	.num_resources	= ARRAY_SIZE(nhk8815_nand_resources),
};

/* These are the partitions for the OneNand device, different from above */
static struct mtd_partition nhk8815_onenand_partitions[] = {
	{
		.name	= "X-Loader(OneNAND)",
		.offset = 0,
		.size	= SZ_256K,
	}, {
		.name	= "MemInit(OneNAND)",
		.offset	= MTDPART_OFS_APPEND,
		.size	= SZ_256K,
	}, {
		.name	= "BootLoader(OneNAND)",
		.offset	= MTDPART_OFS_APPEND,
		.size	= SZ_2M-SZ_256K,
	}, {
		.name	= "SysImage(OneNAND)",
		.offset	= MTDPART_OFS_APPEND,
		.size	= 4 * SZ_1M,
	}, {
		.name	= "Root Filesystem(OneNAND)",
		.offset	= MTDPART_OFS_APPEND,
		.size	= 22 * SZ_1M,
	}, {
		.name	= "User Filesystem(OneNAND)",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL,
	}
};

static struct flash_platform_data nhk8815_onenand_data = {
	.parts		= nhk8815_onenand_partitions,
	.nr_parts	= ARRAY_SIZE(nhk8815_onenand_partitions),
};

static struct resource nhk8815_onenand_resource[] = {
	{
		.start		= 0x30000000,
		.end		= 0x30000000 + SZ_128K - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device nhk8815_onenand_device = {
	.name		= "onenand",
	.id		= -1,
	.dev		= {
		.platform_data	= &nhk8815_onenand_data,
	},
	.resource	= nhk8815_onenand_resource,
	.num_resources	= ARRAY_SIZE(nhk8815_onenand_resource),
};

static void __init nhk8815_onenand_init(void)
{
#ifdef CONFIG_ONENAND
       /* Set up SMCS0 for OneNand */
       writel(0x000030db, FSMC_BCR0);
       writel(0x02100551, FSMC_BTR0);
#endif
}

#define __MEM_4K_RESOURCE(x) \
	.res = {.start = (x), .end = (x) + SZ_4K - 1, .flags = IORESOURCE_MEM}

static struct amba_device uart0_device = {
	.dev = { .init_name = "uart0" },
	__MEM_4K_RESOURCE(NOMADIK_UART0_BASE),
	.irq = {IRQ_UART0, NO_IRQ},
};

static struct amba_device uart1_device = {
	.dev = { .init_name = "uart1" },
	__MEM_4K_RESOURCE(NOMADIK_UART1_BASE),
	.irq = {IRQ_UART1, NO_IRQ},
};

static struct amba_device *amba_devs[] __initdata = {
	&uart0_device,
	&uart1_device,
};

/* We have a fixed clock alone, by now */
static struct clk nhk8815_clk_48 = {
	.rate = 48*1000*1000,
};

static struct resource nhk8815_eth_resources[] = {
	{
		.name = "smc91x-regs",
		.start = 0x34000000 + 0x300,
		.end = 0x34000000 + SZ_64K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = NOMADIK_GPIO_TO_IRQ(115),
		.end = NOMADIK_GPIO_TO_IRQ(115),
		.flags = IORESOURCE_IRQ | IRQF_TRIGGER_RISING,
	}
};

static struct platform_device nhk8815_eth_device = {
	.name = "smc91x",
	.resource = nhk8815_eth_resources,
	.num_resources = ARRAY_SIZE(nhk8815_eth_resources),
};

static int __init nhk8815_eth_init(void)
{
	int gpio_nr = 115; /* hardwired in the board */
	int err;

	err = gpio_request(gpio_nr, "eth_irq");
	if (!err) err = nmk_gpio_set_mode(gpio_nr, NMK_GPIO_ALT_GPIO);
	if (!err) err = gpio_direction_input(gpio_nr);
	if (err)
		pr_err("Error %i in %s\n", err, __func__);
	return err;
}
device_initcall(nhk8815_eth_init);

static struct platform_device *nhk8815_platform_devices[] __initdata = {
	&nhk8815_nand_device,
	&nhk8815_onenand_device,
	&nhk8815_eth_device,
	/* will add more devices */
};

static void __init nhk8815_platform_init(void)
{
	int i;

	cpu8815_platform_init();
	nhk8815_onenand_init();
	platform_add_devices(nhk8815_platform_devices,
			     ARRAY_SIZE(nhk8815_platform_devices));

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		nmdk_clk_create(&nhk8815_clk_48, amba_devs[i]->dev.init_name);
		amba_device_register(amba_devs[i], &iomem_resource);
	}
}

MACHINE_START(NOMADIK, "NHK8815")
	/* Maintainer: ST MicroElectronics */
	.phys_io	= NOMADIK_UART0_BASE,
	.io_pg_offst	= (IO_ADDRESS(NOMADIK_UART0_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x100,
	.map_io		= cpu8815_map_io,
	.init_irq	= cpu8815_init_irq,
	.timer		= &nomadik_timer,
	.init_machine	= nhk8815_platform_init,
MACHINE_END
