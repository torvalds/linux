/*
 * Copyright 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2010 Jason Wang <jason77.wang@gmail.com>
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-mx51.h>
#include <mach/3ds_debugboard.h>

#include "devices-imx51.h"

#define MX51_3DS_ECSPI2_CS	(GPIO_PORTC + 28)

static iomux_v3_cfg_t mx51_3ds_pads[] = {
	/* UART1 */
	MX51_PAD_UART1_RXD__UART1_RXD,
	MX51_PAD_UART1_TXD__UART1_TXD,
	MX51_PAD_UART1_RTS__UART1_RTS,
	MX51_PAD_UART1_CTS__UART1_CTS,

	/* UART2 */
	MX51_PAD_UART2_RXD__UART2_RXD,
	MX51_PAD_UART2_TXD__UART2_TXD,
	MX51_PAD_EIM_D25__UART2_CTS,
	MX51_PAD_EIM_D26__UART2_RTS,

	/* UART3 */
	MX51_PAD_UART3_RXD__UART3_RXD,
	MX51_PAD_UART3_TXD__UART3_TXD,
	MX51_PAD_EIM_D24__UART3_CTS,
	MX51_PAD_EIM_D27__UART3_RTS,

	/* CPLD PARENT IRQ PIN */
	MX51_PAD_GPIO1_6__GPIO1_6,

	/* KPP */
	MX51_PAD_KEY_ROW0__KEY_ROW0,
	MX51_PAD_KEY_ROW1__KEY_ROW1,
	MX51_PAD_KEY_ROW2__KEY_ROW2,
	MX51_PAD_KEY_ROW3__KEY_ROW3,
	MX51_PAD_KEY_COL0__KEY_COL0,
	MX51_PAD_KEY_COL1__KEY_COL1,
	MX51_PAD_KEY_COL2__KEY_COL2,
	MX51_PAD_KEY_COL3__KEY_COL3,
	MX51_PAD_KEY_COL4__KEY_COL4,
	MX51_PAD_KEY_COL5__KEY_COL5,

	/* eCSPI2 */
	MX51_PAD_NANDF_RB2__ECSPI2_SCLK,
	MX51_PAD_NANDF_RB3__ECSPI2_MISO,
	MX51_PAD_NANDF_D15__ECSPI2_MOSI,
	MX51_PAD_NANDF_D12__GPIO3_28,
};

/* Serial ports */
static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static int mx51_3ds_board_keymap[] = {
	KEY(0, 0, KEY_1),
	KEY(0, 1, KEY_2),
	KEY(0, 2, KEY_3),
	KEY(0, 3, KEY_F1),
	KEY(0, 4, KEY_UP),
	KEY(0, 5, KEY_F2),

	KEY(1, 0, KEY_4),
	KEY(1, 1, KEY_5),
	KEY(1, 2, KEY_6),
	KEY(1, 3, KEY_LEFT),
	KEY(1, 4, KEY_SELECT),
	KEY(1, 5, KEY_RIGHT),

	KEY(2, 0, KEY_7),
	KEY(2, 1, KEY_8),
	KEY(2, 2, KEY_9),
	KEY(2, 3, KEY_F3),
	KEY(2, 4, KEY_DOWN),
	KEY(2, 5, KEY_F4),

	KEY(3, 0, KEY_0),
	KEY(3, 1, KEY_OK),
	KEY(3, 2, KEY_ESC),
	KEY(3, 3, KEY_ENTER),
	KEY(3, 4, KEY_MENU),
	KEY(3, 5, KEY_BACK)
};

static const struct matrix_keymap_data mx51_3ds_map_data __initconst = {
	.keymap		= mx51_3ds_board_keymap,
	.keymap_size	= ARRAY_SIZE(mx51_3ds_board_keymap),
};

static int mx51_3ds_spi2_cs[] = {
	MXC_SPI_CS(0),
	MX51_3DS_ECSPI2_CS,
};

static const struct spi_imx_master mx51_3ds_ecspi2_pdata __initconst = {
	.chipselect	= mx51_3ds_spi2_cs,
	.num_chipselect	= ARRAY_SIZE(mx51_3ds_spi2_cs),
};

static struct spi_board_info mx51_3ds_spi_nor_device[] = {
	{
	 .modalias = "m25p80",
	 .max_speed_hz = 25000000,	/* max spi clock (SCK) speed in HZ */
	 .bus_num = 1,
	 .chip_select = 1,
	 .mode = SPI_MODE_0,
	 .platform_data = NULL,},
};

/*
 * Board specific initialization.
 */
static void __init mx51_3ds_init(void)
{
	imx51_soc_init();

	mxc_iomux_v3_setup_multiple_pads(mx51_3ds_pads,
					ARRAY_SIZE(mx51_3ds_pads));

	imx51_add_imx_uart(0, &uart_pdata);
	imx51_add_imx_uart(1, &uart_pdata);
	imx51_add_imx_uart(2, &uart_pdata);

	imx51_add_ecspi(1, &mx51_3ds_ecspi2_pdata);
	spi_register_board_info(mx51_3ds_spi_nor_device,
				ARRAY_SIZE(mx51_3ds_spi_nor_device));

	if (mxc_expio_init(MX51_CS5_BASE_ADDR, IMX_GPIO_NR(1, 6)))
		printk(KERN_WARNING "Init of the debugboard failed, all "
				    "devices on the board are unusable.\n");

	imx51_add_sdhci_esdhc_imx(0, NULL);
	imx51_add_imx_keypad(&mx51_3ds_map_data);
	imx51_add_imx2_wdt(0);
}

static void __init mx51_3ds_timer_init(void)
{
	mx51_clocks_init(32768, 24000000, 22579200, 0);
}

static struct sys_timer mx51_3ds_timer = {
	.init = mx51_3ds_timer_init,
};

MACHINE_START(MX51_3DS, "Freescale MX51 3-Stack Board")
	/* Maintainer: Freescale Semiconductor, Inc. */
	.atag_offset = 0x100,
	.map_io = mx51_map_io,
	.init_early = imx51_init_early,
	.init_irq = mx51_init_irq,
	.handle_irq = imx51_handle_irq,
	.timer = &mx51_3ds_timer,
	.init_machine = mx51_3ds_init,
	.init_late	= imx51_init_late,
	.restart	= mxc_restart,
MACHINE_END
