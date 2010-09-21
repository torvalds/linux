/*
 * linux/arch/arm/mach-omap2/board-cm-t3517.c
 *
 * Support for the CompuLab CM-T3517 modules
 *
 * Copyright (C) 2010 CompuLab, Ltd.
 * Author: Igor Grinberg <grinberg@compulab.co.il>
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/rtc-v3020.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/board.h>
#include <plat/common.h>
#include <plat/control.h>
#include <plat/usb.h>
#include <plat/nand.h>
#include <plat/gpmc.h>

#include "mux.h"

#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
static struct gpio_led cm_t3517_leds[] = {
	[0] = {
		.gpio			= 186,
		.name			= "cm-t3517:green",
		.default_trigger	= "heartbeat",
		.active_low		= 0,
	},
};

static struct gpio_led_platform_data cm_t3517_led_pdata = {
	.num_leds	= ARRAY_SIZE(cm_t3517_leds),
	.leds		= cm_t3517_leds,
};

static struct platform_device cm_t3517_led_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &cm_t3517_led_pdata,
	},
};

static void __init cm_t3517_init_leds(void)
{
	platform_device_register(&cm_t3517_led_device);
}
#else
static inline void cm_t3517_init_leds(void) {}
#endif

#if defined(CONFIG_RTC_DRV_V3020) || defined(CONFIG_RTC_DRV_V3020_MODULE)
#define RTC_IO_GPIO		(153)
#define RTC_WR_GPIO		(154)
#define RTC_RD_GPIO		(160)
#define RTC_CS_GPIO		(163)

struct v3020_platform_data cm_t3517_v3020_pdata = {
	.use_gpio	= 1,
	.gpio_cs	= RTC_CS_GPIO,
	.gpio_wr	= RTC_WR_GPIO,
	.gpio_rd	= RTC_RD_GPIO,
	.gpio_io	= RTC_IO_GPIO,
};

static struct platform_device cm_t3517_rtc_device = {
	.name		= "v3020",
	.id		= -1,
	.dev		= {
		.platform_data = &cm_t3517_v3020_pdata,
	}
};

static void __init cm_t3517_init_rtc(void)
{
	platform_device_register(&cm_t3517_rtc_device);
}
#else
static inline void cm_t3517_init_rtc(void) {}
#endif

#if defined(CONFIG_USB_EHCI_HCD) || defined(CONFIG_USB_EHCI_HCD_MODULE)
#define HSUSB1_RESET_GPIO	(146)
#define HSUSB2_RESET_GPIO	(147)
#define USB_HUB_RESET_GPIO	(152)

static struct ehci_hcd_omap_platform_data cm_t3517_ehci_pdata __initdata = {
	.port_mode[0] = EHCI_HCD_OMAP_MODE_PHY,
	.port_mode[1] = EHCI_HCD_OMAP_MODE_PHY,
	.port_mode[2] = EHCI_HCD_OMAP_MODE_UNKNOWN,

	.phy_reset  = true,
	.reset_gpio_port[0]  = HSUSB1_RESET_GPIO,
	.reset_gpio_port[1]  = HSUSB2_RESET_GPIO,
	.reset_gpio_port[2]  = -EINVAL,
};

static int cm_t3517_init_usbh(void)
{
	int err;

	err = gpio_request(USB_HUB_RESET_GPIO, "usb hub rst");
	if (err) {
		pr_err("CM-T3517: usb hub rst gpio request failed: %d\n", err);
	} else {
		gpio_direction_output(USB_HUB_RESET_GPIO, 0);
		udelay(10);
		gpio_set_value(USB_HUB_RESET_GPIO, 1);
		msleep(1);
	}

	usb_ehci_init(&cm_t3517_ehci_pdata);

	return 0;
}
#else
static inline int cm_t3517_init_usbh(void)
{
	return 0;
}
#endif

#if defined(CONFIG_MTD_NAND_OMAP2) || defined(CONFIG_MTD_NAND_OMAP2_MODULE)
#define NAND_BLOCK_SIZE		SZ_128K

static struct mtd_partition cm_t3517_nand_partitions[] = {
	{
		.name           = "xloader",
		.offset         = 0,			/* Offset = 0x00000 */
		.size           = 4 * NAND_BLOCK_SIZE,
		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name           = "uboot",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x80000 */
		.size           = 15 * NAND_BLOCK_SIZE,
	},
	{
		.name           = "uboot environment",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x260000 */
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

static struct omap_nand_platform_data cm_t3517_nand_data = {
	.parts			= cm_t3517_nand_partitions,
	.nr_parts		= ARRAY_SIZE(cm_t3517_nand_partitions),
	.dma_channel		= -1,	/* disable DMA in OMAP NAND driver */
	.cs			= 0,
};

static void __init cm_t3517_init_nand(void)
{
	if (gpmc_nand_init(&cm_t3517_nand_data) < 0)
		pr_err("CM-T3517: NAND initialization failed\n");
}
#else
static inline void cm_t3517_init_nand(void) {}
#endif

static struct omap_board_config_kernel cm_t3517_config[] __initdata = {
};

static void __init cm_t3517_init_irq(void)
{
	omap_board_config = cm_t3517_config;
	omap_board_config_size = ARRAY_SIZE(cm_t3517_config);

	omap2_init_common_hw(NULL, NULL);
	omap_init_irq();
	omap_gpio_init();
}

static struct omap_board_mux board_mux[] __initdata = {
	/* GPIO186 - Green LED */
	OMAP3_MUX(SYS_CLKOUT2, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	/* RTC GPIOs: IO, WR#, RD#, CS# */
	OMAP3_MUX(MCBSP4_DR, OMAP_MUX_MODE4 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP4_DX, OMAP_MUX_MODE4 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP_CLKS, OMAP_MUX_MODE4 | OMAP_PIN_INPUT),
	OMAP3_MUX(UART3_CTS_RCTX, OMAP_MUX_MODE4 | OMAP_PIN_INPUT),
	/* HSUSB1 RESET */
	OMAP3_MUX(UART2_TX, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	/* HSUSB2 RESET */
	OMAP3_MUX(UART2_RX, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	/* CM-T3517 USB HUB nRESET */
	OMAP3_MUX(MCBSP4_CLKX, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),

	{ .reg_offset = OMAP_MUX_TERMINATOR },
};

static void __init cm_t3517_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	omap_serial_init();
	cm_t3517_init_leds();
	cm_t3517_init_nand();
	cm_t3517_init_rtc();
	cm_t3517_init_usbh();
}

MACHINE_START(CM_T3517, "Compulab CM-T3517")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap3_map_io,
	.reserve        = omap_reserve,
	.init_irq	= cm_t3517_init_irq,
	.init_machine	= cm_t3517_init,
	.timer		= &omap_timer,
MACHINE_END
