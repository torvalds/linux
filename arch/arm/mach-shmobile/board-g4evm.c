/*
 * G4EVM board support
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
#include <linux/input.h>
#include <linux/input/sh_keysc.h>
#include <linux/gpio.h>
#include <mach/sh7377.h>
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
		.end	= 65,
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

/* KEYSC */
static struct sh_keysc_info keysc_info = {
	.mode		= SH_KEYSC_MODE_5,
	.scan_timing	= 3,
	.delay		= 100,
	.keycodes = {
		KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F,
		KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L,
		KEY_M, KEY_N, KEY_U, KEY_P, KEY_Q, KEY_R,
		KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X,
		KEY_Y, KEY_Z, KEY_HOME, KEY_SLEEP, KEY_WAKEUP, KEY_COFFEE,
		KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5,
		KEY_6, KEY_7, KEY_8, KEY_9, KEY_STOP, KEY_COMPUTER,
	},
};

static struct resource keysc_resources[] = {
	[0] = {
		.name	= "KEYSC",
		.start  = 0xe61b0000,
		.end    = 0xe61b000f,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 79,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device keysc_device = {
	.name           = "sh_keysc",
	.id             = 0, /* keysc0 clock */
	.num_resources  = ARRAY_SIZE(keysc_resources),
	.resource       = keysc_resources,
	.dev	= {
		.platform_data	= &keysc_info,
	},
};

static struct platform_device *g4evm_devices[] __initdata = {
	&nor_flash_device,
	&usb_host_device,
	&keysc_device,
};

static struct map_desc g4evm_io_desc[] __initdata = {
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

static void __init g4evm_map_io(void)
{
	iotable_init(g4evm_io_desc, ARRAY_SIZE(g4evm_io_desc));

	/* setup early devices, clocks and console here as well */
	sh7377_add_early_devices();
	sh7367_clock_init(); /* use g3 clocks for now */
	shmobile_setup_console();
}

static void __init g4evm_init(void)
{
	sh7377_pinmux_init();

	/* Lit DS14 LED */
	gpio_request(GPIO_PORT109, NULL);
	gpio_direction_output(GPIO_PORT109, 1);
	gpio_export(GPIO_PORT109, 1);

	/* Lit DS15 LED */
	gpio_request(GPIO_PORT110, NULL);
	gpio_direction_output(GPIO_PORT110, 1);
	gpio_export(GPIO_PORT110, 1);

	/* Lit DS16 LED */
	gpio_request(GPIO_PORT112, NULL);
	gpio_direction_output(GPIO_PORT112, 1);
	gpio_export(GPIO_PORT112, 1);

	/* Lit DS17 LED */
	gpio_request(GPIO_PORT113, NULL);
	gpio_direction_output(GPIO_PORT113, 1);
	gpio_export(GPIO_PORT113, 1);

	/* USBHS */
	gpio_request(GPIO_FN_VBUS_0, NULL);
	gpio_request(GPIO_FN_PWEN, NULL);
	gpio_request(GPIO_FN_OVCN, NULL);
	gpio_request(GPIO_FN_OVCN2, NULL);
	gpio_request(GPIO_FN_EXTLP, NULL);
	gpio_request(GPIO_FN_IDIN, NULL);

	/* enable clock in SMSTPCR3 */
	__raw_writel(__raw_readl(0xe615013c) & ~(1 << 22), 0xe615013c);

	/* setup USB phy */
	__raw_writew(0x0200, 0xe605810a);       /* USBCR1 */
	__raw_writew(0x00e0, 0xe60581c0);       /* CPFCH */
	__raw_writew(0x6010, 0xe60581c6);       /* CGPOSR */
	__raw_writew(0x8a0a, 0xe605810c);       /* USBCR2 */

	/* KEYSC @ CN31 */
	gpio_request(GPIO_FN_PORT60_KEYOUT5, NULL);
	gpio_request(GPIO_FN_PORT61_KEYOUT4, NULL);
	gpio_request(GPIO_FN_PORT62_KEYOUT3, NULL);
	gpio_request(GPIO_FN_PORT63_KEYOUT2, NULL);
	gpio_request(GPIO_FN_PORT64_KEYOUT1, NULL);
	gpio_request(GPIO_FN_PORT65_KEYOUT0, NULL);
	gpio_request(GPIO_FN_PORT66_KEYIN0_PU, NULL);
	gpio_request(GPIO_FN_PORT67_KEYIN1_PU, NULL);
	gpio_request(GPIO_FN_PORT68_KEYIN2_PU, NULL);
	gpio_request(GPIO_FN_PORT69_KEYIN3_PU, NULL);
	gpio_request(GPIO_FN_PORT70_KEYIN4_PU, NULL);
	gpio_request(GPIO_FN_PORT71_KEYIN5_PU, NULL);
	gpio_request(GPIO_FN_PORT72_KEYIN6_PU, NULL);

	sh7377_add_standard_devices();

	platform_add_devices(g4evm_devices, ARRAY_SIZE(g4evm_devices));
}

MACHINE_START(G4EVM, "g4evm")
	.phys_io	= 0xe6000000,
	.io_pg_offst	= ((0xe6000000) >> 18) & 0xfffc,
	.map_io		= g4evm_map_io,
	.init_irq	= sh7377_init_irq,
	.init_machine	= g4evm_init,
	.timer		= &shmobile_timer,
MACHINE_END
