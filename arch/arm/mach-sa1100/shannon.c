/*
 * linux/arch/arm/mach-sa1100/shannon.c
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <mach/mcp.h>
#include <mach/shannon.h>

#include "generic.h"

static struct mtd_partition shannon_partitions[] = {
	{
		.name		= "BLOB boot loader",
		.offset		= 0,
		.size		= 0x20000
	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0xe0000
	},
	{
		.name		= "initrd",
		.offset		= MTDPART_OFS_APPEND,	
		.size		= MTDPART_SIZ_FULL
	}
};

static struct flash_platform_data shannon_flash_data = {
	.map_name	= "cfi_probe",
	.parts		= shannon_partitions,
	.nr_parts	= ARRAY_SIZE(shannon_partitions),
};

static struct resource shannon_flash_resource = {
	.start		= SA1100_CS0_PHYS,
	.end		= SA1100_CS0_PHYS + SZ_4M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct mcp_plat_data shannon_mcp_data = {
	.mccr0		= MCCR0_ADM,
	.sclk_rate	= 11981000,
};

static void __init shannon_init(void)
{
	sa11x0_register_mtd(&shannon_flash_data, &shannon_flash_resource, 1);
	sa11x0_register_mcp(&shannon_mcp_data);
}

static void __init shannon_map_io(void)
{
	sa1100_map_io();

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);

	Ser1SDCR0 |= SDCR0_SUS;
	GAFR |= (GPIO_UART_TXD | GPIO_UART_RXD);
	GPDR |= GPIO_UART_TXD | SHANNON_GPIO_CODEC_RESET;
	GPDR &= ~GPIO_UART_RXD;
	PPAR |= PPAR_UPR;

	/* reset the codec */
	GPCR = SHANNON_GPIO_CODEC_RESET;
	GPSR = SHANNON_GPIO_CODEC_RESET;
}

MACHINE_START(SHANNON, "Shannon (AKA: Tuxscreen)")
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xf8000000) >> 18) & 0xfffc,
	.boot_params	= 0xc0000100,
	.map_io		= shannon_map_io,
	.init_irq	= sa1100_init_irq,
	.timer		= &sa1100_timer,
	.init_machine	= shannon_init,
MACHINE_END
