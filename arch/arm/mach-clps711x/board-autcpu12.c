/*
 *  linux/arch/arm/mach-clps711x/autcpu12.c
 *
 * (c) 2001 Thomas Gleixner, autronix automation <gleixner@autronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand-gpio.h>
#include <linux/platform_device.h>
#include <linux/basic_mmio_gpio.h>

#include <mach/hardware.h>
#include <asm/sizes.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include <asm/mach/map.h>
#include <mach/autcpu12.h>

#include "common.h"

#define AUTCPU12_CS8900_BASE	(CS2_PHYS_BASE + 0x300)
#define AUTCPU12_CS8900_IRQ	(IRQ_EINT3)

#define AUTCPU12_SMC_BASE	(CS1_PHYS_BASE + 0x06000000)
#define AUTCPU12_SMC_SEL_BASE	(AUTCPU12_SMC_BASE + 0x10)

#define AUTCPU12_MMGPIO_BASE	(CLPS711X_NR_GPIO)
#define AUTCPU12_SMC_NCE	(AUTCPU12_MMGPIO_BASE + 0) /* Bit 0 */
#define AUTCPU12_SMC_RDY	CLPS711X_GPIO(1, 2)
#define AUTCPU12_SMC_ALE	CLPS711X_GPIO(1, 3)
#define AUTCPU12_SMC_CLE	CLPS711X_GPIO(1, 3)

static struct resource autcpu12_cs8900_resource[] __initdata = {
	DEFINE_RES_MEM(AUTCPU12_CS8900_BASE, SZ_1K),
	DEFINE_RES_IRQ(AUTCPU12_CS8900_IRQ),
};

static struct resource autcpu12_nvram_resource[] __initdata = {
	DEFINE_RES_MEM_NAMED(AUTCPU12_PHYS_NVRAM, SZ_128K, "SRAM"),
};

static struct platform_device autcpu12_nvram_pdev __initdata = {
	.name		= "autcpu12_nvram",
	.id		= -1,
	.resource	= autcpu12_nvram_resource,
	.num_resources	= ARRAY_SIZE(autcpu12_nvram_resource),
};

static struct resource autcpu12_nand_resource[] __initdata = {
	DEFINE_RES_MEM(AUTCPU12_SMC_BASE, SZ_16),
};

static struct mtd_partition autcpu12_nand_parts[] __initdata = {
	{
		.name	= "Flash partition 1",
		.offset	= 0,
		.size	= SZ_8M,
	},
	{
		.name	= "Flash partition 2",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL,
	},
};

static void __init autcpu12_adjust_parts(struct gpio_nand_platdata *pdata,
					 size_t sz)
{
	switch (sz) {
	case SZ_16M:
	case SZ_32M:
		break;
	case SZ_64M:
	case SZ_128M:
		pdata->parts[0].size = SZ_16M;
		break;
	default:
		pr_warn("Unsupported SmartMedia device size %u\n", sz);
		break;
	}
}

static struct gpio_nand_platdata autcpu12_nand_pdata __initdata = {
	.gpio_rdy	= AUTCPU12_SMC_RDY,
	.gpio_nce	= AUTCPU12_SMC_NCE,
	.gpio_ale	= AUTCPU12_SMC_ALE,
	.gpio_cle	= AUTCPU12_SMC_CLE,
	.gpio_nwp	= -1,
	.chip_delay	= 20,
	.parts		= autcpu12_nand_parts,
	.num_parts	= ARRAY_SIZE(autcpu12_nand_parts),
	.adjust_parts	= autcpu12_adjust_parts,
};

static struct platform_device autcpu12_nand_pdev __initdata = {
	.name		= "gpio-nand",
	.id		= -1,
	.resource	= autcpu12_nand_resource,
	.num_resources	= ARRAY_SIZE(autcpu12_nand_resource),
	.dev		= {
		.platform_data = &autcpu12_nand_pdata,
	},
};

static struct resource autcpu12_mmgpio_resource[] __initdata = {
	DEFINE_RES_MEM_NAMED(AUTCPU12_SMC_SEL_BASE, SZ_1, "dat"),
};

static struct bgpio_pdata autcpu12_mmgpio_pdata __initdata = {
	.base	= AUTCPU12_MMGPIO_BASE,
	.ngpio	= 8,
};

static struct platform_device autcpu12_mmgpio_pdev __initdata = {
	.name		= "basic-mmio-gpio",
	.id		= -1,
	.resource	= autcpu12_mmgpio_resource,
	.num_resources	= ARRAY_SIZE(autcpu12_mmgpio_resource),
	.dev		= {
		.platform_data = &autcpu12_mmgpio_pdata,
	},
};

static void __init autcpu12_init(void)
{
	platform_device_register_simple("video-clps711x", 0, NULL, 0);
	platform_device_register_simple("cs89x0", 0, autcpu12_cs8900_resource,
					ARRAY_SIZE(autcpu12_cs8900_resource));
	platform_device_register(&autcpu12_mmgpio_pdev);
	platform_device_register(&autcpu12_nvram_pdev);
}

static void __init autcpu12_init_late(void)
{
	if (IS_ENABLED(MTD_NAND_GPIO) && IS_ENABLED(GPIO_GENERIC_PLATFORM)) {
		/* We are need both drivers to handle NAND */
		platform_device_register(&autcpu12_nand_pdev);
	}
}

MACHINE_START(AUTCPU12, "autronix autcpu12")
	/* Maintainer: Thomas Gleixner */
	.atag_offset	= 0x20000,
	.nr_irqs	= CLPS711X_NR_IRQS,
	.map_io		= clps711x_map_io,
	.init_irq	= clps711x_init_irq,
	.timer		= &clps711x_timer,
	.init_machine	= autcpu12_init,
	.init_late	= autcpu12_init_late,
	.handle_irq	= clps711x_handle_irq,
	.restart	= clps711x_restart,
MACHINE_END

