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
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/usb/r8a66597.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/input/sh_keysc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/gpio.h>
#include <linux/dma-mapping.h>
#include <mach/irqs.h>
#include <mach/sh7377.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "sh-gpio.h"

/*
 * SDHI
 *
 * SDHI0 : card detection is possible
 * SDHI1 : card detection is impossible
 *
 * [G4-MAIN-BOARD]
 * JP74 : short		# DBG_2V8A    for SDHI0
 * JP75 : NC		# DBG_3V3A    for SDHI0
 * JP76 : NC		# DBG_3V3A_SD for SDHI0
 * JP77 : NC		# 3V3A_SDIO   for SDHI1
 * JP78 : short		# DBG_2V8A    for SDHI1
 * JP79 : NC		# DBG_3V3A    for SDHI1
 * JP80 : NC		# DBG_3V3A_SD for SDHI1
 *
 * [G4-CORE-BOARD]
 * S32 : all off	# to dissever from G3-CORE_DBG board
 * S33 : all off	# to dissever from G3-CORE_DBG board
 *
 * [G3-CORE_DBG-BOARD]
 * S1  : all off	# to dissever from G3-CORE_DBG board
 * S3  : all off	# to dissever from G3-CORE_DBG board
 * S4  : all off	# to dissever from G3-CORE_DBG board
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
	__raw_writew(__raw_readw(IOMEM(0xe6890008)) | 0x600, IOMEM(0xe6890008));
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
		.start	= evt2irq(0x0a20), /* USBHS_USHI0 */
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
		.start  = evt2irq(0x0be0), /* KEYSC_KEY */
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

/* Fixed 3.3V regulator to be used by SDHI0 and SDHI1 */
static struct regulator_consumer_supply fixed3v3_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.0"),
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.1"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.1"),
};

/* SDHI */
static struct sh_mobile_sdhi_info sdhi0_info = {
	.tmio_caps	= MMC_CAP_SDIO_IRQ,
};

static struct resource sdhi0_resources[] = {
	[0] = {
		.name	= "SDHI0",
		.start  = 0xe6d50000,
		.end    = 0xe6d500ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x0e00), /* SDHI0 */
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi0_device = {
	.name           = "sh_mobile_sdhi",
	.num_resources  = ARRAY_SIZE(sdhi0_resources),
	.resource       = sdhi0_resources,
	.id             = 0,
	.dev	= {
		.platform_data	= &sdhi0_info,
	},
};

static struct sh_mobile_sdhi_info sdhi1_info = {
	.tmio_caps	= MMC_CAP_NONREMOVABLE | MMC_CAP_SDIO_IRQ,
};

static struct resource sdhi1_resources[] = {
	[0] = {
		.name	= "SDHI1",
		.start  = 0xe6d60000,
		.end    = 0xe6d600ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x0e80), /* SDHI1 */
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi1_device = {
	.name           = "sh_mobile_sdhi",
	.num_resources  = ARRAY_SIZE(sdhi1_resources),
	.resource       = sdhi1_resources,
	.id             = 1,
	.dev	= {
		.platform_data	= &sdhi1_info,
	},
};

static struct platform_device *g4evm_devices[] __initdata = {
	&nor_flash_device,
	&usb_host_device,
	&keysc_device,
	&sdhi0_device,
	&sdhi1_device,
};

#define GPIO_SDHID0_D0	IOMEM(0xe60520fc)
#define GPIO_SDHID0_D1	IOMEM(0xe60520fd)
#define GPIO_SDHID0_D2	IOMEM(0xe60520fe)
#define GPIO_SDHID0_D3	IOMEM(0xe60520ff)
#define GPIO_SDHICMD0	IOMEM(0xe6052100)

#define GPIO_SDHID1_D0	IOMEM(0xe6052103)
#define GPIO_SDHID1_D1	IOMEM(0xe6052104)
#define GPIO_SDHID1_D2	IOMEM(0xe6052105)
#define GPIO_SDHID1_D3	IOMEM(0xe6052106)
#define GPIO_SDHICMD1	IOMEM(0xe6052107)

static void __init g4evm_init(void)
{
	regulator_register_always_on(0, "fixed-3.3V", fixed3v3_power_consumers,
				     ARRAY_SIZE(fixed3v3_power_consumers), 3300000);

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

	/* setup USB phy */
	__raw_writew(0x0200, IOMEM(0xe605810a));       /* USBCR1 */
	__raw_writew(0x00e0, IOMEM(0xe60581c0));       /* CPFCH */
	__raw_writew(0x6010, IOMEM(0xe60581c6));       /* CGPOSR */
	__raw_writew(0x8a0a, IOMEM(0xe605810c));       /* USBCR2 */

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

	/* SDHI0 */
	gpio_request(GPIO_FN_SDHICLK0, NULL);
	gpio_request(GPIO_FN_SDHICD0, NULL);
	gpio_request(GPIO_FN_SDHID0_0, NULL);
	gpio_request(GPIO_FN_SDHID0_1, NULL);
	gpio_request(GPIO_FN_SDHID0_2, NULL);
	gpio_request(GPIO_FN_SDHID0_3, NULL);
	gpio_request(GPIO_FN_SDHICMD0, NULL);
	gpio_request(GPIO_FN_SDHIWP0, NULL);
	gpio_request_pullup(GPIO_SDHID0_D0);
	gpio_request_pullup(GPIO_SDHID0_D1);
	gpio_request_pullup(GPIO_SDHID0_D2);
	gpio_request_pullup(GPIO_SDHID0_D3);
	gpio_request_pullup(GPIO_SDHICMD0);

	/* SDHI1 */
	gpio_request(GPIO_FN_SDHICLK1, NULL);
	gpio_request(GPIO_FN_SDHID1_0, NULL);
	gpio_request(GPIO_FN_SDHID1_1, NULL);
	gpio_request(GPIO_FN_SDHID1_2, NULL);
	gpio_request(GPIO_FN_SDHID1_3, NULL);
	gpio_request(GPIO_FN_SDHICMD1, NULL);
	gpio_request_pullup(GPIO_SDHID1_D0);
	gpio_request_pullup(GPIO_SDHID1_D1);
	gpio_request_pullup(GPIO_SDHID1_D2);
	gpio_request_pullup(GPIO_SDHID1_D3);
	gpio_request_pullup(GPIO_SDHICMD1);

	sh7377_add_standard_devices();

	platform_add_devices(g4evm_devices, ARRAY_SIZE(g4evm_devices));
}

MACHINE_START(G4EVM, "g4evm")
	.map_io		= sh7377_map_io,
	.init_early	= sh7377_add_early_devices,
	.init_irq	= sh7377_init_irq,
	.handle_irq	= shmobile_handle_irq_intc,
	.init_machine	= g4evm_init,
	.init_late	= shmobile_init_late,
	.timer		= &shmobile_timer,
MACHINE_END
