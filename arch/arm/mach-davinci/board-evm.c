/*
 * TI DaVinci EVM board support
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/io.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <mach/common.h>

/* other misc. init functions */
void __init davinci_psc_init(void);
void __init davinci_irq_init(void);
void __init davinci_map_common_io(void);
void __init davinci_init_common_hw(void);

/* NOR Flash base address set to CS0 by default */
#define NOR_FLASH_PHYS 0x02000000

static struct mtd_partition davinci_evm_partitions[] = {
	/* bootloader (U-Boot, etc) in first 4 sectors */
	{
		.name		= "bootloader",
		.offset		= 0,
		.size		= 4 * SZ_64K,
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	},
	/* bootloader params in the next 1 sectors */
	{
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_64K,
		.mask_flags	= 0,
	},
	/* kernel */
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_2M,
		.mask_flags	= 0
	},
	/* file system */
	{
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0
	}
};

static struct physmap_flash_data davinci_evm_flash_data = {
	.width		= 2,
	.parts		= davinci_evm_partitions,
	.nr_parts	= ARRAY_SIZE(davinci_evm_partitions),
};

/* NOTE: CFI probe will correctly detect flash part as 32M, but EMIF
 * limits addresses to 16M, so using addresses past 16M will wrap */
static struct resource davinci_evm_flash_resource = {
	.start		= NOR_FLASH_PHYS,
	.end		= NOR_FLASH_PHYS + SZ_16M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device davinci_evm_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &davinci_evm_flash_data,
	},
	.num_resources	= 1,
	.resource	= &davinci_evm_flash_resource,
};

static struct platform_device *davinci_evm_devices[] __initdata = {
	&davinci_evm_flash_device,
};

static void __init
davinci_evm_map_io(void)
{
	davinci_map_common_io();
}

static __init void davinci_evm_init(void)
{
	davinci_psc_init();

#if defined(CONFIG_BLK_DEV_DAVINCI) || defined(CONFIG_BLK_DEV_DAVINCI_MODULE)
	printk(KERN_WARNING "WARNING: both IDE and NOR flash are enabled, "
	       "but share pins.\n\t Disable IDE for NOR support.\n");
#endif

	platform_add_devices(davinci_evm_devices,
			     ARRAY_SIZE(davinci_evm_devices));
}

static __init void davinci_evm_irq_init(void)
{
	davinci_init_common_hw();
	davinci_irq_init();
}

MACHINE_START(DAVINCI_EVM, "DaVinci EVM")
	/* Maintainer: MontaVista Software <source@mvista.com> */
	.phys_io      = IO_PHYS,
	.io_pg_offst  = (io_p2v(IO_PHYS) >> 18) & 0xfffc,
	.boot_params  = (DAVINCI_DDR_BASE + 0x100),
	.map_io	      = davinci_evm_map_io,
	.init_irq     = davinci_evm_irq_init,
	.timer	      = &davinci_timer,
	.init_machine = davinci_evm_init,
MACHINE_END
