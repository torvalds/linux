/*
 *  LogicPD i.MX31 SOM-LV development board support
 *
 *    Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 *  based on code for other MX31 boards,
 *
 *    Copyright 2005-2007 Freescale Semiconductor
 *    Copyright (c) 2009 Alberto Panizzo <maramaopercheseimorto@gmail.com>
 *    Copyright (C) 2009 Valentin Longchamp, EPFL Mobots group
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>
#include <mach/board-mx31lite.h>

#include "devices.h"

/*
 * This file contains board-specific initialization routines for the
 * LogicPD i.MX31 SOM-LV development board, aka 'LiteKit'.
 * If you design an own baseboard for the module, use this file as base
 * for support code.
 */

static unsigned int litekit_db_board_pins[] __initdata = {
	/* UART1 */
	MX31_PIN_CTS1__CTS1,
	MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1,
};

/* UART */
static struct imxuart_platform_data uart_pdata __initdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

void __init mx31lite_db_init(void)
{
	mxc_iomux_setup_multiple_pins(litekit_db_board_pins,
					ARRAY_SIZE(litekit_db_board_pins),
					"development board pins");
	mxc_register_device(&mxc_uart_device0, &uart_pdata);
}

