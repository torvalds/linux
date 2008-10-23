/*
 * board-overo.c (Gumstix Overo)
 *
 * Initial code: Steve Sakoman <steve@sakoman.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>

#include <mach/board-overo.h>
#include <mach/board.h>
#include <mach/common.h>
#include <mach/gpio.h>
#include <mach/gpmc.h>
#include <mach/hardware.h>
#include <mach/nand.h>

#define NAND_BLOCK_SIZE SZ_128K
#define GPMC_CS0_BASE  0x60
#define GPMC_CS_SIZE   0x30

static struct mtd_partition overo_nand_partitions[] = {
	{
		.name           = "xloader",
		.offset         = 0,			/* Offset = 0x00000 */
		.size           = 4 * NAND_BLOCK_SIZE,
		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name           = "uboot",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x80000 */
		.size           = 14 * NAND_BLOCK_SIZE,
	},
	{
		.name           = "uboot environment",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x240000 */
		.size           = 2 * NAND_BLOCK_SIZE,
	},
	{
		.name           = "linux",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x280000 */
		.size           = 32 * NAND_BLOCK_SIZE,
	},
	{
		.name           = "rootfs",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x680000 */
		.size           = MTDPART_SIZ_FULL,
	},
};

static struct omap_nand_platform_data overo_nand_data = {
	.parts = overo_nand_partitions,
	.nr_parts = ARRAY_SIZE(overo_nand_partitions),
	.dma_channel = -1,	/* disable DMA in OMAP NAND driver */
};

static struct resource overo_nand_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device overo_nand_device = {
	.name		= "omap2-nand",
	.id		= -1,
	.dev		= {
		.platform_data	= &overo_nand_data,
	},
	.num_resources	= 1,
	.resource	= &overo_nand_resource,
};


static void __init overo_flash_init(void)
{
	u8 cs = 0;
	u8 nandcs = GPMC_CS_NUM + 1;

	u32 gpmc_base_add = OMAP34XX_GPMC_VIRT;

	/* find out the chip-select on which NAND exists */
	while (cs < GPMC_CS_NUM) {
		u32 ret = 0;
		ret = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG1);

		if ((ret & 0xC00) == 0x800) {
			printk(KERN_INFO "Found NAND on CS%d\n", cs);
			if (nandcs > GPMC_CS_NUM)
				nandcs = cs;
		}
		cs++;
	}

	if (nandcs > GPMC_CS_NUM) {
		printk(KERN_INFO "NAND: Unable to find configuration "
				 "in GPMC\n ");
		return;
	}

	if (nandcs < GPMC_CS_NUM) {
		overo_nand_data.cs = nandcs;
		overo_nand_data.gpmc_cs_baseaddr = (void *)
			(gpmc_base_add + GPMC_CS0_BASE + nandcs * GPMC_CS_SIZE);
		overo_nand_data.gpmc_baseaddr = (void *) (gpmc_base_add);

		printk(KERN_INFO "Registering NAND on CS%d\n", nandcs);
		if (platform_device_register(&overo_nand_device) < 0)
			printk(KERN_ERR "Unable to register NAND device\n");
	}
}
static struct omap_uart_config overo_uart_config __initdata = {
	.enabled_uarts	= ((1 << 0) | (1 << 1) | (1 << 2)),
};

static int __init overo_i2c_init(void)
{
	/* i2c2 pins are used for gpio */
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

static void __init overo_init_irq(void)
{
	omap2_init_common_hw();
	omap_init_irq();
	omap_gpio_init();
}

static struct platform_device overo_lcd_device = {
	.name		= "overo_lcd",
	.id		= -1,
};

static struct omap_lcd_config overo_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel overo_config[] __initdata = {
	{ OMAP_TAG_UART,	&overo_uart_config },
	{ OMAP_TAG_LCD,		&overo_lcd_config },
};

static struct platform_device *overo_devices[] __initdata = {
	&overo_lcd_device,
};

static void __init overo_init(void)
{
	overo_i2c_init();
	platform_add_devices(overo_devices, ARRAY_SIZE(overo_devices));
	omap_board_config = overo_config;
	omap_board_config_size = ARRAY_SIZE(overo_config);
	omap_serial_init();
	overo_flash_init();

	if ((gpio_request(OVERO_GPIO_W2W_NRESET,
			  "OVERO_GPIO_W2W_NRESET") == 0) &&
	    (gpio_direction_output(OVERO_GPIO_W2W_NRESET, 1) == 0)) {
		gpio_export(OVERO_GPIO_W2W_NRESET, 0);
		gpio_set_value(OVERO_GPIO_W2W_NRESET, 0);
		udelay(10);
		gpio_set_value(OVERO_GPIO_W2W_NRESET, 1);
	} else {
		printk(KERN_ERR "could not obtain gpio for "
					"OVERO_GPIO_W2W_NRESET\n");
	}

	if ((gpio_request(OVERO_GPIO_BT_XGATE, "OVERO_GPIO_BT_XGATE") == 0) &&
	    (gpio_direction_output(OVERO_GPIO_BT_XGATE, 0) == 0))
		gpio_export(OVERO_GPIO_BT_XGATE, 0);
	else
		printk(KERN_ERR "could not obtain gpio for OVERO_GPIO_BT_XGATE\n");

	if ((gpio_request(OVERO_GPIO_BT_NRESET, "OVERO_GPIO_BT_NRESET") == 0) &&
	    (gpio_direction_output(OVERO_GPIO_BT_NRESET, 1) == 0)) {
		gpio_export(OVERO_GPIO_BT_NRESET, 0);
		gpio_set_value(OVERO_GPIO_BT_NRESET, 0);
		mdelay(6);
		gpio_set_value(OVERO_GPIO_BT_NRESET, 1);
	} else {
		printk(KERN_ERR "could not obtain gpio for "
					"OVERO_GPIO_BT_NRESET\n");
	}

	if ((gpio_request(OVERO_GPIO_USBH_CPEN, "OVERO_GPIO_USBH_CPEN") == 0) &&
	    (gpio_direction_output(OVERO_GPIO_USBH_CPEN, 1) == 0))
		gpio_export(OVERO_GPIO_USBH_CPEN, 0);
	else
		printk(KERN_ERR "could not obtain gpio for "
					"OVERO_GPIO_USBH_CPEN\n");

	if ((gpio_request(OVERO_GPIO_USBH_NRESET,
			  "OVERO_GPIO_USBH_NRESET") == 0) &&
	    (gpio_direction_output(OVERO_GPIO_USBH_NRESET, 1) == 0))
		gpio_export(OVERO_GPIO_USBH_NRESET, 0);
	else
		printk(KERN_ERR "could not obtain gpio for "
					"OVERO_GPIO_USBH_NRESET\n");
}

static void __init overo_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

MACHINE_START(OVERO, "Gumstix Overo")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= overo_map_io,
	.init_irq	= overo_init_irq,
	.init_machine	= overo_init,
	.timer		= &omap_timer,
MACHINE_END
