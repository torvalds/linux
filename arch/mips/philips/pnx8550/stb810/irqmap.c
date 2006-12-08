/*
 *  Philips STB810 board irqmap.
 *
 *  Author: MontaVista Software, Inc.
 *          source@mvista.com
 *
 *  Copyright 2005 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 */

#include <linux/init.h>
#include <int.h>

char pnx8550_irq_tab[][5] __initdata = {
	[8]	= { -1, PNX8550_INT_PCI_INTA, 0xff, 0xff, 0xff},
	[9]	= { -1, PNX8550_INT_PCI_INTA, 0xff, 0xff, 0xff},
	[10]	= { -1, PNX8550_INT_PCI_INTA, 0xff, 0xff, 0xff},
};

