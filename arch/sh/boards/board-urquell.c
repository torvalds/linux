/*
 * Renesas Technology Corp. SH7786 Urquell Support.
 *
 * Copyright (C) 2008  Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * Based on board-sh7785lcr.c
 * Copyright (C) 2008  Yoshihiro Shimoda
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/smc91x.h>
#include <linux/mtd/physmap.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <mach/urquell.h>
#include <cpu/sh7786.h>
#include <asm/heartbeat.h>
#include <asm/sizes.h>

/*
 * bit  1234 5678
 *----------------------------
 * SW1  0101 0010  -> Pck 33MHz version
 *     (1101 0010)    Pck 66MHz version
 * SW2  0x1x xxxx  -> little endian
 *                    29bit mode
 * SW47 0001 1000  -> CS0 : on-board flash
 *                    CS1 : SRAM, registers, LAN, PCMCIA
 *                    38400 bps for SCIF1
 *
 * Address
 * 0x00000000 - 0x04000000  (CS0)     Nor Flash
 * 0x04000000 - 0x04200000  (CS1)     SRAM
 * 0x05000000 - 0x05800000  (CS1)     on board register
 * 0x05800000 - 0x06000000  (CS1)     LAN91C111
 * 0x06000000 - 0x06400000  (CS1)     PCMCIA
 * 0x08000000 - 0x10000000  (CS2-CS3) DDR3
 * 0x10000000 - 0x14000000  (CS4)     PCIe
 * 0x14000000 - 0x14800000  (CS5)     Core0 LRAM/URAM
 * 0x14800000 - 0x15000000  (CS5)     Core1 LRAM/URAM
 * 0x18000000 - 0x1C000000  (CS6)     ATA/NAND-Flash
 * 0x1C000000 -             (CS7)     SH7786 Control register
 */

/* HeartBeat */
static struct resource heartbeat_resources[] = {
	[0] = {
		.start	= BOARDREG(SLEDR),
		.end	= BOARDREG(SLEDR),
		.flags	= IORESOURCE_MEM,
	},
};

static struct heartbeat_data heartbeat_data = {
	.regsize = 16,
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.dev	= {
		.platform_data	= &heartbeat_data,
	},
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
};

/* LAN91C111 */
static struct smc91x_platdata smc91x_info = {
	.flags = SMC91X_USE_16BIT | SMC91X_NOWAIT,
};

static struct resource smc91x_eth_resources[] = {
	[0] = {
		.name   = "SMC91C111" ,
		.start  = 0x05800300,
		.end    = 0x0580030f,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 11,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_eth_device = {
	.name           = "smc91x",
	.num_resources  = ARRAY_SIZE(smc91x_eth_resources),
	.resource       = smc91x_eth_resources,
	.dev	= {
		.platform_data	= &smc91x_info,
	},
};

/* Nor Flash */
static struct mtd_partition nor_flash_partitions[] = {
	{
		.name		= "loader",
		.offset		= 0x00000000,
		.size		= SZ_512K,
		.mask_flags	= MTD_WRITEABLE,	/* Read-only */
	},
	{
		.name		= "bootenv",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_512K,
		.mask_flags	= MTD_WRITEABLE,	/* Read-only */
	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_4M,
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
	[0] = {
		.start	= NOR_FLASH_ADDR,
		.end	= NOR_FLASH_ADDR + NOR_FLASH_SIZE - 1,
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

static struct platform_device *urquell_devices[] __initdata = {
	&heartbeat_device,
	&smc91x_eth_device,
	&nor_flash_device,
};

static int __init urquell_devices_setup(void)
{
	/* USB */
	gpio_request(GPIO_FN_USB_OVC0,  NULL);
	gpio_request(GPIO_FN_USB_PENC0, NULL);

	/* enable LAN */
	__raw_writew(__raw_readw(UBOARDREG(IRL2MSKR)) & ~0x00000001,
		  UBOARDREG(IRL2MSKR));

	return platform_add_devices(urquell_devices,
				    ARRAY_SIZE(urquell_devices));
}
device_initcall(urquell_devices_setup);

static void urquell_power_off(void)
{
	__raw_writew(0xa5a5, UBOARDREG(SRSTR));
}

static void __init urquell_init_irq(void)
{
	plat_irq_setup_pins(IRQ_MODE_IRL3210_MASK);
}

/* Initialize the board */
static void __init urquell_setup(char **cmdline_p)
{
	printk(KERN_INFO "Renesas Technology Corp. Urquell support.\n");

	pm_power_off = urquell_power_off;
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_urquell __initmv = {
	.mv_name	= "Urquell",
	.mv_setup	= urquell_setup,
	.mv_init_irq	= urquell_init_irq,
};
