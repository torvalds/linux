// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  reset.c: reset support for PNX833X.
 *
 *  Copyright 2008 NXP Semiconductors
 *	  Chris Steel <chris.steel@nxp.com>
 *    Daniel Laird <daniel.j.laird@nxp.com>
 *
 *  Based on software written by:
 *	Nikita Youshchenko <yoush@debian.org>, based on PNX8550 code.
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
