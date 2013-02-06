/* linux/arm/arch/mach-s5pv310/gsd4t.h
 *
 * GSD4T(GPS) platform driver data
 *
 * Copyright (c) 2011 Samsung Electronics
 * Minho Ban <mhban@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _GSD4T_H
#define _GSD4T_H

struct gsd4t_platform_data {
	unsigned int onoff;
	unsigned int nrst;
	unsigned int tsync;
	unsigned int uart_rxd;
	unsigned int uart_txd;
	unsigned int uart_cts;
	unsigned int uart_rts;
};

#endif /* _GSD4T_H */


