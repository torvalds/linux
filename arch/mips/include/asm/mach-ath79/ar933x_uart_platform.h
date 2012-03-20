/*
 *  Platform data definition for Atheros AR933X UART
 *
 *  Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#ifndef _AR933X_UART_PLATFORM_H
#define _AR933X_UART_PLATFORM_H

struct ar933x_uart_platform_data {
	unsigned	uartclk;
};

#endif /* _AR933X_UART_PLATFORM_H */
