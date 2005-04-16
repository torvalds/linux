/*
 * linux/arch/arm/mach-sa1100/shannon.c
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <asm/arch/shannon.h>

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

static void __init shannon_init(void)
{
	sa11x0_set_flash_data(&shannon_flash_data, &shannon_flash_resource, 1);
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
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100)
	MAPIO(shannon_map_io)
	INITIRQ(sa1100_init_irq)
	.timer		= &sa1100_timer,
	.init_machine	= shannon_init,
MACHINE_END
