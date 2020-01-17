// SPDX-License-Identifier: GPL-2.0
/*
 * linux/arch/arm/mach-sa1100/shanyesn.c
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/gpio/machine.h>
#include <linux/kernel.h>
#include <linux/platform_data/sa11x0-serial.h>
#include <linux/tty.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>

#include <video/sa1100fb.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>
#include <linux/platform_data/mfd-mcp-sa11x0.h>
#include <mach/shanyesn.h>
#include <mach/irqs.h>

#include "generic.h"

static struct mtd_partition shanyesn_partitions[] = {
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

static struct flash_platform_data shanyesn_flash_data = {
	.map_name	= "cfi_probe",
	.parts		= shanyesn_partitions,
	.nr_parts	= ARRAY_SIZE(shanyesn_partitions),
};

static struct resource shanyesn_flash_resource =
	DEFINE_RES_MEM(SA1100_CS0_PHYS, SZ_4M);

static struct mcp_plat_data shanyesn_mcp_data = {
	.mccr0		= MCCR0_ADM,
	.sclk_rate	= 11981000,
};

static struct sa1100fb_mach_info shanyesn_lcd_info = {
	.pixclock	= 152500,	.bpp		= 8,
	.xres		= 640,		.yres		= 480,

	.hsync_len	= 4,		.vsync_len	= 3,
	.left_margin	= 2,		.upper_margin	= 0,
	.right_margin	= 1,		.lower_margin	= 0,

	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,

	.lccr0		= LCCR0_Color | LCCR0_Dual | LCCR0_Pas,
	.lccr3		= LCCR3_ACBsDiv(512),
};

static struct gpiod_lookup_table shanyesn_pcmcia0_gpio_table = {
	.dev_id = "sa11x0-pcmcia.0",
	.table = {
		GPIO_LOOKUP("gpio", 24, "detect", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("gpio", 26, "ready", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static struct gpiod_lookup_table shanyesn_pcmcia1_gpio_table = {
	.dev_id = "sa11x0-pcmcia.1",
	.table = {
		GPIO_LOOKUP("gpio", 25, "detect", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("gpio", 27, "ready", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static struct regulator_consumer_supply shanyesn_cf_vcc_consumers[] = {
	REGULATOR_SUPPLY("vcc", "sa11x0-pcmcia.0"),
	REGULATOR_SUPPLY("vcc", "sa11x0-pcmcia.1"),
};

static struct fixed_voltage_config shanyesn_cf_vcc_pdata __initdata = {
	.supply_name = "cf-power",
	.microvolts = 3300000,
	.enabled_at_boot = 1,
};

static void __init shanyesn_init(void)
{
	sa11x0_register_fixed_regulator(0, &shanyesn_cf_vcc_pdata,
					shanyesn_cf_vcc_consumers,
					ARRAY_SIZE(shanyesn_cf_vcc_consumers),
					false);
	sa11x0_register_pcmcia(0, &shanyesn_pcmcia0_gpio_table);
	sa11x0_register_pcmcia(1, &shanyesn_pcmcia1_gpio_table);
	sa11x0_ppc_configure_mcp();
	sa11x0_register_lcd(&shanyesn_lcd_info);
	sa11x0_register_mtd(&shanyesn_flash_data, &shanyesn_flash_resource, 1);
	sa11x0_register_mcp(&shanyesn_mcp_data);
}

static void __init shanyesn_map_io(void)
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

MACHINE_START(SHANNON, "Shanyesn (AKA: Tuxscreen)")
	.atag_offset	= 0x100,
	.map_io		= shanyesn_map_io,
	.nr_irqs	= SA1100_NR_IRQS,
	.init_irq	= sa1100_init_irq,
	.init_time	= sa1100_timer_init,
	.init_machine	= shanyesn_init,
	.init_late	= sa11x0_init_late,
	.restart	= sa11x0_restart,
MACHINE_END
