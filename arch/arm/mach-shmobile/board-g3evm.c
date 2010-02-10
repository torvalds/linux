/*
 * G3EVM board support
 *
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
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/usb/r8a66597.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <mach/sh7367.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

static struct mtd_partition nor_flash_partitions[] = {
	{
		.name		= "loader",
		.offset		= 0x00000000,
		.size		= 512 * 1024,
	},
	{
		.name		= "bootenv",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 512 * 1024,
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

/* USBHS */
void usb_host_port_power(int port, int power)
{
	if (!power) /* only power-on supported for now */
		return;

	/* set VBOUT/PWEN and EXTLP0 in DVSTCTR */
	__raw_writew(__raw_readw(0xe6890008) | 0x600, 0xe6890008);
}

static struct r8a66597_platdata usb_host_data = {
	.on_chip = 1,
	.port_power = usb_host_port_power,
};

static struct resource usb_host_resources[] = {
	[0] = {
		.name	= "USBHS",
		.start	= 0xe6890000,
		.end	= 0xe68900e5,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 65,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usb_host_device = {
	.name		= "r8a66597_hcd",
	.id		= 0,
	.dev = {
		.platform_data		= &usb_host_data,
		.dma_mask		= NULL,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(usb_host_resources),
	.resource	= usb_host_resources,
};

static struct platform_device *g3evm_devices[] __initdata = {
	&nor_flash_device,
	&usb_host_device,
};

static struct map_desc g3evm_io_desc[] __initdata = {
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

static void __init g3evm_map_io(void)
{
	iotable_init(g3evm_io_desc, ARRAY_SIZE(g3evm_io_desc));

	/* setup early devices, clocks and console here as well */
	sh7367_add_early_devices();
	sh7367_clock_init();
	shmobile_setup_console();
}

static void __init g3evm_init(void)
{
	sh7367_pinmux_init();

	/* Lit DS4 LED */
	gpio_request(GPIO_PORT22, NULL);
	gpio_direction_output(GPIO_PORT22, 1);
	gpio_export(GPIO_PORT22, 0);

	/* Lit DS8 LED */
	gpio_request(GPIO_PORT23, NULL);
	gpio_direction_output(GPIO_PORT23, 1);
	gpio_export(GPIO_PORT23, 0);

	/* Lit DS3 LED */
	gpio_request(GPIO_PORT24, NULL);
	gpio_direction_output(GPIO_PORT24, 1);
	gpio_export(GPIO_PORT24, 0);

	/* SCIFA1 */
	gpio_request(GPIO_FN_SCIFA1_TXD, NULL);
	gpio_request(GPIO_FN_SCIFA1_RXD, NULL);
	gpio_request(GPIO_FN_SCIFA1_CTS, NULL);
	gpio_request(GPIO_FN_SCIFA1_RTS, NULL);

	/* USBHS */
	gpio_request(GPIO_FN_VBUS0, NULL);
	gpio_request(GPIO_FN_PWEN, NULL);
	gpio_request(GPIO_FN_OVCN, NULL);
	gpio_request(GPIO_FN_OVCN2, NULL);
	gpio_request(GPIO_FN_EXTLP, NULL);
	gpio_request(GPIO_FN_IDIN, NULL);

	/* enable clock in SYMSTPCR2 */
	__raw_writel(__raw_readl(0xe6158048) & ~(1 << 22), 0xe6158048);

	/* setup USB phy */
	__raw_writew(0x0300, 0xe605810a);	/* USBCR1 */
	__raw_writew(0x00e0, 0xe60581c0);	/* CPFCH */
	__raw_writew(0x6010, 0xe60581c6);	/* CGPOSR */
	__raw_writew(0x8a0a, 0xe605810c);	/* USBCR2 */

	sh7367_add_standard_devices();

	platform_add_devices(g3evm_devices, ARRAY_SIZE(g3evm_devices));
}

MACHINE_START(G3EVM, "g3evm")
	.phys_io	= 0xe6000000,
	.io_pg_offst	= ((0xe6000000) >> 18) & 0xfffc,
	.map_io		= g3evm_map_io,
	.init_irq	= sh7367_init_irq,
	.init_machine	= g3evm_init,
	.timer		= &shmobile_timer,
MACHINE_END
