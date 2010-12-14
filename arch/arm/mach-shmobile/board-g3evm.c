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
#include <linux/mtd/sh_flctl.h>
#include <linux/usb/r8a66597.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/input/sh_keysc.h>
#include <mach/sh7367.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

/*
 * IrDA
 *
 * S67: 5bit : ON  power
 *    : 6bit : ON  remote control
 *             OFF IrDA
 */

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
static void usb_host_port_power(int port, int power)
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
		.start	= evt2irq(0xa20), /* USBHS_USHI0 */
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
		KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G,
		KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N,
		KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U,
		KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z, KEY_HOME, KEY_SLEEP,
		KEY_WAKEUP, KEY_COFFEE, KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
		KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_STOP, KEY_COMPUTER,
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
		.start  = evt2irq(0xbe0), /* KEYSC_KEY */
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device keysc_device = {
	.name           = "sh_keysc",
	.num_resources  = ARRAY_SIZE(keysc_resources),
	.resource       = keysc_resources,
	.dev	= {
		.platform_data	= &keysc_info,
	},
};

static struct mtd_partition nand_partition_info[] = {
	{
		.name	= "system",
		.offset	= 0,
		.size	= 64 * 1024 * 1024,
	},
	{
		.name	= "userdata",
		.offset	= MTDPART_OFS_APPEND,
		.size	= 128 * 1024 * 1024,
	},
	{
		.name	= "cache",
		.offset	= MTDPART_OFS_APPEND,
		.size	= 64 * 1024 * 1024,
	},
};

static struct resource nand_flash_resources[] = {
	[0] = {
		.start	= 0xe6a30000,
		.end	= 0xe6a3009b,
		.flags	= IORESOURCE_MEM,
	}
};

static struct sh_flctl_platform_data nand_flash_data = {
	.parts		= nand_partition_info,
	.nr_parts	= ARRAY_SIZE(nand_partition_info),
	.flcmncr_val	= QTSEL_E | FCKSEL_E | TYPESEL_SET | NANWF_E
			| SHBUSSEL | SEL_16BIT,
};

static struct platform_device nand_flash_device = {
	.name		= "sh_flctl",
	.resource	= nand_flash_resources,
	.num_resources	= ARRAY_SIZE(nand_flash_resources),
	.dev		= {
		.platform_data = &nand_flash_data,
	},
};

static struct resource irda_resources[] = {
	[0] = {
		.start	= 0xE6D00000,
		.end	= 0xE6D01FD4 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x480), /* IRDA */
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device irda_device = {
	.name		= "sh_irda",
	.id		= -1,
	.resource	= irda_resources,
	.num_resources	= ARRAY_SIZE(irda_resources),
};

static struct platform_device *g3evm_devices[] __initdata = {
	&nor_flash_device,
	&usb_host_device,
	&keysc_device,
	&nand_flash_device,
	&irda_device,
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

	/* setup early devices and console here as well */
	sh7367_add_early_devices();
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

	/* setup USB phy */
	__raw_writew(0x0300, 0xe605810a);	/* USBCR1 */
	__raw_writew(0x00e0, 0xe60581c0);	/* CPFCH */
	__raw_writew(0x6010, 0xe60581c6);	/* CGPOSR */
	__raw_writew(0x8a0a, 0xe605810c);	/* USBCR2 */

	/* KEYSC @ CN7 */
	gpio_request(GPIO_FN_PORT42_KEYOUT0, NULL);
	gpio_request(GPIO_FN_PORT43_KEYOUT1, NULL);
	gpio_request(GPIO_FN_PORT44_KEYOUT2, NULL);
	gpio_request(GPIO_FN_PORT45_KEYOUT3, NULL);
	gpio_request(GPIO_FN_PORT46_KEYOUT4, NULL);
	gpio_request(GPIO_FN_PORT47_KEYOUT5, NULL);
	gpio_request(GPIO_FN_PORT48_KEYIN0_PU, NULL);
	gpio_request(GPIO_FN_PORT49_KEYIN1_PU, NULL);
	gpio_request(GPIO_FN_PORT50_KEYIN2_PU, NULL);
	gpio_request(GPIO_FN_PORT55_KEYIN3_PU, NULL);
	gpio_request(GPIO_FN_PORT56_KEYIN4_PU, NULL);
	gpio_request(GPIO_FN_PORT57_KEYIN5_PU, NULL);
	gpio_request(GPIO_FN_PORT58_KEYIN6_PU, NULL);

	/* FLCTL */
	gpio_request(GPIO_FN_FCE0, NULL);
	gpio_request(GPIO_FN_D0_ED0_NAF0, NULL);
	gpio_request(GPIO_FN_D1_ED1_NAF1, NULL);
	gpio_request(GPIO_FN_D2_ED2_NAF2, NULL);
	gpio_request(GPIO_FN_D3_ED3_NAF3, NULL);
	gpio_request(GPIO_FN_D4_ED4_NAF4, NULL);
	gpio_request(GPIO_FN_D5_ED5_NAF5, NULL);
	gpio_request(GPIO_FN_D6_ED6_NAF6, NULL);
	gpio_request(GPIO_FN_D7_ED7_NAF7, NULL);
	gpio_request(GPIO_FN_D8_ED8_NAF8, NULL);
	gpio_request(GPIO_FN_D9_ED9_NAF9, NULL);
	gpio_request(GPIO_FN_D10_ED10_NAF10, NULL);
	gpio_request(GPIO_FN_D11_ED11_NAF11, NULL);
	gpio_request(GPIO_FN_D12_ED12_NAF12, NULL);
	gpio_request(GPIO_FN_D13_ED13_NAF13, NULL);
	gpio_request(GPIO_FN_D14_ED14_NAF14, NULL);
	gpio_request(GPIO_FN_D15_ED15_NAF15, NULL);
	gpio_request(GPIO_FN_WE0_XWR0_FWE, NULL);
	gpio_request(GPIO_FN_FRB, NULL);
	/* FOE, FCDE, FSC on dedicated pins */
	__raw_writel(__raw_readl(0xe6158048) & ~(1 << 15), 0xe6158048);

	/* IrDA */
	gpio_request(GPIO_FN_IRDA_OUT, NULL);
	gpio_request(GPIO_FN_IRDA_IN, NULL);
	gpio_request(GPIO_FN_IRDA_FIRSEL, NULL);
	set_irq_type(evt2irq(0x480), IRQ_TYPE_LEVEL_LOW);

	sh7367_add_standard_devices();

	platform_add_devices(g3evm_devices, ARRAY_SIZE(g3evm_devices));
}

static void __init g3evm_timer_init(void)
{
	sh7367_clock_init();
	shmobile_timer.init();
}

static struct sys_timer g3evm_timer = {
	.init		= g3evm_timer_init,
};

MACHINE_START(G3EVM, "g3evm")
	.map_io		= g3evm_map_io,
	.init_irq	= sh7367_init_irq,
	.init_machine	= g3evm_init,
	.timer		= &g3evm_timer,
MACHINE_END
