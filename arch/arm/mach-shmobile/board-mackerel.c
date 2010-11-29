/*
 * mackerel board support
 *
 * Copyright (C) 2010 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * based on ap4evb
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2008  Yoshihiro Shimoda
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/smsc911x.h>

#include <mach/common.h>
#include <mach/sh7372.h>

#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>

/*
 * Address	Interface		BusWidth	note
 * ------------------------------------------------------------------
 * 0x0000_0000	NOR Flash ROM (MCP)	16bit		SW7 : bit1 = ON
 * 0x0800_0000	user area		-
 * 0x1000_0000	NOR Flash ROM (MCP)	16bit		SW7 : bit1 = OFF
 * 0x1400_0000	Ether (LAN9220)		16bit
 * 0x1600_0000	user area		-		cannot use with NAND
 * 0x1800_0000	user area		-
 * 0x1A00_0000	-
 * 0x4000_0000	LPDDR2-SDRAM (POP)	32bit
 */

/* MTD */
static struct mtd_partition nor_flash_partitions[] = {
	{
		.name		= "loader",
		.offset		= 0x00000000,
		.size		= 512 * 1024,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "bootenv",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 512 * 1024,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "kernel_ro",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 8 * 1024 * 1024,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 8 * 1024 * 1024,
	},
	{
		.name		= "data",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data nor_flash_data = {
	.width		= 2,
	.parts		= nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(nor_flash_partitions),
};

static struct resource nor_flash_resources[] = {
	[0]	= {
		.start	= 0x00000000,
		.end	= 0x08000000 - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device nor_flash_device = {
	.name		= "physmap-flash",
	.dev		= {
		.platform_data	= &nor_flash_data,
	},
	.num_resources	= ARRAY_SIZE(nor_flash_resources),
	.resource	= nor_flash_resources,
};

/* SMSC */
static struct resource smc911x_resources[] = {
	{
		.start	= 0x14000000,
		.end	= 0x16000000 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= evt2irq(0x02c0) /* IRQ6A */,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct smsc911x_platform_config smsc911x_info = {
	.flags		= SMSC911X_USE_16BIT | SMSC911X_SAVE_MAC_ADDRESS,
	.irq_polarity   = SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type       = SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device smc911x_device = {
	.name           = "smsc911x",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(smc911x_resources),
	.resource       = smc911x_resources,
	.dev            = {
		.platform_data = &smsc911x_info,
	},
};

static struct platform_device *mackerel_devices[] __initdata = {
	&nor_flash_device,
	&smc911x_device,
};

static struct map_desc mackerel_io_desc[] __initdata = {
	/* create a 1:1 entity map for 0xe6xxxxxx
	 * used by CPGA, INTC and PFC.
	 */
	{
		.virtual	= 0xe6000000,
		.pfn		= __phys_to_pfn(0xe6000000),
		.length		= 256 << 20,
		.type		= MT_DEVICE_NONSHARED
	},
};

static void __init mackerel_map_io(void)
{
	iotable_init(mackerel_io_desc, ARRAY_SIZE(mackerel_io_desc));

	/* setup early devices and console here as well */
	sh7372_add_early_devices();
	shmobile_setup_console();
}

static void __init mackerel_init(void)
{
	sh7372_pinmux_init();

	/* enable SCIFA0 */
	gpio_request(GPIO_FN_SCIFA0_TXD, NULL);
	gpio_request(GPIO_FN_SCIFA0_RXD, NULL);

	/* enable SMSC911X */
	gpio_request(GPIO_FN_CS5A,	NULL);
	gpio_request(GPIO_FN_IRQ6_39,	NULL);

	sh7372_add_standard_devices();

	platform_add_devices(mackerel_devices, ARRAY_SIZE(mackerel_devices));
}

static void __init mackerel_timer_init(void)
{
	sh7372_clock_init();
	shmobile_timer.init();
}

static struct sys_timer mackerel_timer = {
	.init		= mackerel_timer_init,
};

MACHINE_START(MACKEREL, "mackerel")
	.map_io		= mackerel_map_io,
	.init_irq	= sh7372_init_irq,
	.init_machine	= mackerel_init,
	.timer		= &mackerel_timer,
MACHINE_END
