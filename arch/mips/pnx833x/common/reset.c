/*
 *  reset.c: reset support for PNX833X.
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
#include <linux/reboot.h>
#include <pnx833x.h>

void pnx833x_machine_restart(char *command)
{
	PNX833X_RESET_CONTROL_2 = 0;
	PNX833X_RESET_CONTROL = 0;
}

void pnx833x_machine_halt(void)
{
	while (1)
		__asm__ __volatile__ ("wait");

}

void pnx833x_machine_power_off(void)
{
	pnx833x_machine_halt();
}
