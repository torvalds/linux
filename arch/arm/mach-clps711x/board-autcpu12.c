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
#include <linux/mtd/physmap.h>
#include <linux/mtd/plat-ram.h>
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

#include "common.h"
#include "devices.h"

/* NOR flash */
#define AUTCPU12_FLASH_BASE	(CS0_PHYS_BASE)

/* Board specific hardware definitions */
#define AUTCPU12_CHAR_LCD_BASE	(CS1_PHYS_BASE + 0x00000000)
#define AUTCPU12_CSAUX1_BASE	(CS1_PHYS_BASE + 0x04000000)
#define AUTCPU12_CAN_BASE	(CS1_PHYS_BASE + 0x08000000)
#define AUTCPU12_TOUCH_BASE	(CS1_PHYS_BASE + 0x0a000000)
#define AUTCPU12_IO_BASE	(CS1_PHYS_BASE + 0x0c000000)
#define AUTCPU12_LPT_BASE	(CS1_PHYS_BASE + 0x0e000000)

/* NVRAM */
#define AUTCPU12_NVRAM_BASE	(CS1_PHYS_BASE + 0x02000000)

/* SmartMedia flash */
#define AUTCPU12_SMC_BASE	(CS1_PHYS_BASE + 0x06000000)
#define AUTCPU12_SMC_SEL_BASE	(AUTCPU12_SMC_BASE + 0x10)

/* Ethernet */
#define AUTCPU12_CS8900_BASE	(CS2_PHYS_BASE + 0x300)
#define AUTCPU12_CS8900_IRQ	(IRQ_EINT3)

/* NAND flash */
#define AUTCPU12_MMGPIO_BASE	(CLPS711X_NR_GPIO)
#define AUTCPU12_SMC_NCE	(AUTCPU12_MMGPIO_BASE + 0) /* Bit 0 */
#define AUTCPU12_SMC_RDY	CLPS711X_GPIO(1, 2)
#define AUTCPU12_SMC_ALE	CLPS711X_GPIO(1, 3)
#define AUTCPU12_SMC_CLE	CLPS711X_GPIO(1, 4)

/* LCD contrast digital potentiometer */
#define AUTCPU12_DPOT_CS	CLPS711X_GPIO(4, 0)
#define AUTCPU12_DPOT_CLK	CLPS711X_GPIO(4, 1)
#define AUTCPU12_DPOT_UD	CLPS711X_GPIO(4, 2)

static struct resource autcpu12_cs8900_resource[] __initdata = {
	DEFINE_RES_MEM(AUTCPU12_CS8900_BASE, SZ_1K),
	DEFINE_RES_IRQ(AUTCPU12_CS8900_IRQ),
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

static const struct gpio autcpu12_gpios[] __initconst = {
	{ AUTCPU12_DPOT_CS,	GPIOF_OUT_INIT_HIGH,	"DPOT CS" },
	{ AUTCPU12_DPOT_CLK,	GPIOF_OUT_INIT_LOW,	"DPOT CLK" },
	{ AUTCPU12_DPOT_UD,	GPIOF_OUT_INIT_LOW,	"DPOT UD" },
};

static struct mtd_partition autcpu12_flash_partitions[] = {
	{
		.name	= "NOR.0",
		.offset	= 0,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data autcpu12_flash_pdata = {
	.width		= 4,
	.parts		= autcpu12_flash_partitions,
	.nr_parts	= ARRAY_SIZE(autcpu12_flash_partitions),
};

static struct resource autcpu12_flash_resources[] __initdata = {
	DEFINE_RES_MEM(AUTCPU12_FLASH_BASE, SZ_8M),
};

static struct platform_device autcpu12_flash_pdev __initdata = {
	.name		= "physmap-flash",
	.id		= 0,
	.resource	= autcpu12_flash_resources,
	.num_resources	= ARRAY_SIZE(autcpu12_flash_resources),
	.dev		= {
		.platform_data	= &autcpu12_flash_pdata,
	},
};

static struct resource autcpu12_nvram_resource[] __initdata = {
	DEFINE_RES_MEM(AUTCPU12_NVRAM_BASE, 0),
};

static struct platdata_mtd_ram autcpu12_nvram_pdata = {
	.bankwidth	= 4,
};

static struct platform_device autcpu12_nvram_pdev __initdata = {
	.name		= "mtd-ram",
	.id		= 0,
	.resource	= autcpu12_nvram_resource,
	.num_resources	= ARRAY_SIZE(autcpu12_nvram_resource),
	.dev		= {
		.platform_data	= &autcpu12_nvram_pdata,
	},
};

static void __init autcpu12_nvram_init(void)
{
	void __iomem *nvram;
	unsigned int save[2];
	resource_size_t nvram_size = SZ_128K;

	/*
	 * Check for 32K/128K
	 * Read ofs 0K
	 * Read ofs 64K
	 * Write complement to ofs 64K
	 * Read and check result on ofs 0K
	 * Restore contents
	 */
	nvram = ioremap(autcpu12_nvram_resource[0].start, SZ_128K);
	if (nvram) {
		save[0] = readl(nvram + 0);
		save[1] = readl(nvram + SZ_64K);
		writel(~save[0], nvram + SZ_64K);
		if (readl(nvram + 0) != save[0]) {
			writel(save[0], nvram + 0);
			nvram_size = SZ_32K;
		} else
			writel(save[1], nvram + SZ_64K);
		iounmap(nvram);

		autcpu12_nvram_resource[0].end =
			autcpu12_nvram_resource[0].start + nvram_size - 1;
		platform_device_register(&autcpu12_nvram_pdev);
	} else
		pr_err("Failed to remap NVRAM resource\n");
}

static void __init autcpu12_init(void)
{
	clps711x_devices_init();
	platform_device_register(&autcpu12_flash_pdev);
	platform_device_register_simple("video-clps711x", 0, NULL, 0);
	platform_device_register_simple("cs89x0", 0, autcpu12_cs8900_resource,
					ARRAY_SIZE(autcpu12_cs8900_resource));
	platform_device_register(&autcpu12_mmgpio_pdev);
	autcpu12_nvram_init();
}

static void __init autcpu12_init_late(void)
{
	gpio_request_array(autcpu12_gpios, ARRAY_SIZE(autcpu12_gpios));
	platform_device_register(&autcpu12_nand_pdev);
}

MACHINE_START(AUTCPU12, "autronix autcpu12")
	/* Maintainer: Thomas Gleixner */
	.atag_offset	= 0x20000,
	.map_io		= clps711x_map_io,
	.init_irq	= clps711x_init_irq,
	.init_time	= clps711x_timer_init,
	.init_machine	= autcpu12_init,
	.init_late	= autcpu12_init_late,
	.restart	= clps711x_restart,
MACHINE_END

