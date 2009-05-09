/*
 * Copyright (C) 2009 Valentin Longchamp, EPFL Mobots group
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

#include <linux/types.h>
#include <linux/init.h>

#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>

#include "devices.h"

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static int mxc_uart1_pins[] = {
	MX31_PIN_CTS2__CTS2, MX31_PIN_RTS2__RTS2,
	MX31_PIN_TXD2__TXD2, MX31_PIN_RXD2__RXD2,
};

/*
 * system init for baseboard usage. Will be called by mx31moboard init.
 */
void __init mx31moboard_devboard_init(void)
{
	printk(KERN_INFO "Initializing mx31devboard peripherals\n");
	mxc_iomux_setup_multiple_pins(mxc_uart1_pins, ARRAY_SIZE(mxc_uart1_pins), "uart1");
	mxc_register_device(&mxc_uart_device1, &uart_pdata);
}
