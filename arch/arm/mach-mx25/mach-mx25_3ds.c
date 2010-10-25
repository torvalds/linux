/*
 * Copyright 2009 Sascha Hauer, <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

/*
 * This machine is known as:
 *  - i.MX25 3-Stack Development System
 *  - i.MX25 Platform Development Kit (i.MX25 PDK)
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/input/matrix_keypad.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <mach/mx25.h>
#include <mach/imxfb.h>
#include <mach/iomux-mx25.h>

#include "devices-imx25.h"
#include "devices.h"

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct pad_desc mx25pdk_pads[] = {
	MX25_PAD_FEC_MDC__FEC_MDC,
	MX25_PAD_FEC_MDIO__FEC_MDIO,
	MX25_PAD_FEC_TDATA0__FEC_TDATA0,
	MX25_PAD_FEC_TDATA1__FEC_TDATA1,
	MX25_PAD_FEC_TX_EN__FEC_TX_EN,
	MX25_PAD_FEC_RDATA0__FEC_RDATA0,
	MX25_PAD_FEC_RDATA1__FEC_RDATA1,
	MX25_PAD_FEC_RX_DV__FEC_RX_DV,
	MX25_PAD_FEC_TX_CLK__FEC_TX_CLK,
	MX25_PAD_A17__GPIO_2_3, /* FEC_EN, GPIO 35 */
	MX25_PAD_D12__GPIO_4_8, /* FEC_RESET_B, GPIO 104 */

	/* LCD */
	MX25_PAD_LD0__LD0,
	MX25_PAD_LD1__LD1,
	MX25_PAD_LD2__LD2,
	MX25_PAD_LD3__LD3,
	MX25_PAD_LD4__LD4,
	MX25_PAD_LD5__LD5,
	MX25_PAD_LD6__LD6,
	MX25_PAD_LD7__LD7,
	MX25_PAD_LD8__LD8,
	MX25_PAD_LD9__LD9,
	MX25_PAD_LD10__LD10,
	MX25_PAD_LD11__LD11,
	MX25_PAD_LD12__LD12,
	MX25_PAD_LD13__LD13,
	MX25_PAD_LD14__LD14,
	MX25_PAD_LD15__LD15,
	MX25_PAD_GPIO_E__LD16,
	MX25_PAD_GPIO_F__LD17,
	MX25_PAD_HSYNC__HSYNC,
	MX25_PAD_VSYNC__VSYNC,
	MX25_PAD_LSCLK__LSCLK,
	MX25_PAD_OE_ACD__OE_ACD,
	MX25_PAD_CONTRAST__CONTRAST,

	/* Keypad */
	MX25_PAD_KPP_ROW0__KPP_ROW0,
	MX25_PAD_KPP_ROW1__KPP_ROW1,
	MX25_PAD_KPP_ROW2__KPP_ROW2,
	MX25_PAD_KPP_ROW3__KPP_ROW3,
	MX25_PAD_KPP_COL0__KPP_COL0,
	MX25_PAD_KPP_COL1__KPP_COL1,
	MX25_PAD_KPP_COL2__KPP_COL2,
	MX25_PAD_KPP_COL3__KPP_COL3,
};

static const struct fec_platform_data mx25_fec_pdata __initconst = {
        .phy    = PHY_INTERFACE_MODE_RMII,
};

#define FEC_ENABLE_GPIO		35
#define FEC_RESET_B_GPIO	104

static void __init mx25pdk_fec_reset(void)
{
	gpio_request(FEC_ENABLE_GPIO, "FEC PHY enable");
	gpio_request(FEC_RESET_B_GPIO, "FEC PHY reset");

	gpio_direction_output(FEC_ENABLE_GPIO, 0);  /* drop PHY power */
	gpio_direction_output(FEC_RESET_B_GPIO, 0); /* assert reset */
	udelay(2);

	/* turn on PHY power and lift reset */
	gpio_set_value(FEC_ENABLE_GPIO, 1);
	gpio_set_value(FEC_RESET_B_GPIO, 1);
}

static const struct mxc_nand_platform_data
mx25pdk_nand_board_info __initconst = {
	.width		= 1,
	.hw_ecc		= 1,
	.flash_bbt	= 1,
};

static struct imx_fb_videomode mx25pdk_modes[] = {
	{
		.mode	= {
			.name		= "CRT-VGA",
			.refresh	= 60,
			.xres		= 640,
			.yres		= 480,
			.pixclock	= 39683,
			.left_margin	= 45,
			.right_margin	= 114,
			.upper_margin	= 33,
			.lower_margin	= 11,
			.hsync_len	= 1,
			.vsync_len	= 1,
		},
		.bpp	= 16,
		.pcr	= 0xFA208B80,
	},
};

static struct imx_fb_platform_data mx25pdk_fb_pdata = {
	.mode		= mx25pdk_modes,
	.num_modes	= ARRAY_SIZE(mx25pdk_modes),
	.pwmr		= 0x00A903FF,
	.lscr1		= 0x00120300,
	.dmacr		= 0x00020010,
};

static const uint32_t mx25pdk_keymap[] = {
	KEY(0, 0, KEY_UP),
	KEY(0, 1, KEY_DOWN),
	KEY(0, 2, KEY_VOLUMEDOWN),
	KEY(0, 3, KEY_HOME),
	KEY(1, 0, KEY_RIGHT),
	KEY(1, 1, KEY_LEFT),
	KEY(1, 2, KEY_ENTER),
	KEY(1, 3, KEY_VOLUMEUP),
	KEY(2, 0, KEY_F6),
	KEY(2, 1, KEY_F8),
	KEY(2, 2, KEY_F9),
	KEY(2, 3, KEY_F10),
	KEY(3, 0, KEY_F1),
	KEY(3, 1, KEY_F2),
	KEY(3, 2, KEY_F3),
	KEY(3, 3, KEY_POWER),
};

static struct matrix_keymap_data mx25pdk_keymap_data = {
	.keymap		= mx25pdk_keymap,
	.keymap_size	= ARRAY_SIZE(mx25pdk_keymap),
};

static void __init mx25pdk_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(mx25pdk_pads,
			ARRAY_SIZE(mx25pdk_pads));

	imx25_add_imx_uart0(&uart_pdata);
	mxc_register_device(&mxc_usbh2, NULL);
	imx25_add_mxc_nand(&mx25pdk_nand_board_info);
	mxc_register_device(&mx25_rtc_device, NULL);
	mxc_register_device(&mx25_fb_device, &mx25pdk_fb_pdata);
	mxc_register_device(&mxc_wdt, NULL);

	mx25pdk_fec_reset();
	imx25_add_fec(&mx25_fec_pdata);
	mxc_register_device(&mx25_kpp_device, &mx25pdk_keymap_data);
}

static void __init mx25pdk_timer_init(void)
{
	mx25_clocks_init();
}

static struct sys_timer mx25pdk_timer = {
	.init   = mx25pdk_timer_init,
};

MACHINE_START(MX25_3DS, "Freescale MX25PDK (3DS)")
	/* Maintainer: Freescale Semiconductor, Inc. */
	.boot_params    = MX25_PHYS_OFFSET + 0x100,
	.map_io         = mx25_map_io,
	.init_irq       = mx25_init_irq,
	.init_machine   = mx25pdk_init,
	.timer          = &mx25pdk_timer,
MACHINE_END

