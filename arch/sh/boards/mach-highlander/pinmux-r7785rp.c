/*
 * Copyright (C) 2008 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/gpio.h>
#include <cpu/sh7785.h>

void __init highlander_plat_pinmux_setup(void)
{
	/* SCIF0 */
	gpio_request(GPIO_FN_SCIF0_CTS, NULL);
	gpio_request(GPIO_FN_SCIF0_RTS, NULL);
	gpio_request(GPIO_FN_SCIF0_SCK, NULL);
	gpio_request(GPIO_FN_SCIF0_RXD, NULL);
	gpio_request(GPIO_FN_SCIF0_TXD, NULL);
}
