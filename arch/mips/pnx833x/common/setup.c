/*
 *  setup.c: Setup PNX833X Soc.
 *
 *  Copyright 2008 NXP Semiconductors
 *	  Chris Steel <chris.steel@nxp.com>
 *    Daniel Laird <daniel.j.laird@nxp.com>
 *
 *  Based on software written by:
 *	Nikita Youshchenko <yoush@debian.org>, based on PNX8550 code.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <asm/reboot.h>
#include <pnx833x.h>
#include <gpio.h>

extern void pnx833x_board_setup(void);
extern void pnx833x_machine_restart(char *);
extern void pnx833x_machine_halt(void);
extern void pnx833x_machine_power_off(void);

int __init plat_mem_setup(void)
{
	/* fake pci bus to avoid bounce buffers */
	PCI_DMA_BUS_IS_PHYS = 1;

	/* set mips clock to 320MHz */
#if defined(CONFIG_SOC_PNX8335)
	PNX8335_WRITEFIELD(0x17, CLOCK_PLL_CPU_CTL, FREQ);
#endif
	pnx833x_gpio_init();	/* so it will be ready in board_setup() */

	pnx833x_board_setup();

	_machine_restart = pnx833x_machine_restart;
	_machine_halt = pnx833x_machine_halt;
	pm_power_off = pnx833x_machine_power_off;

	/* IO/MEM resources. */
	set_io_port_base(KSEG1);
	ioport_resource.start = 0;
	ioport_resource.end = ~0;
	iomem_resource.start = 0;
	iomem_resource.end = ~0;

	return 0;
}
