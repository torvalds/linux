/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001, 06 by Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/types.h>
#include <linux/reboot.h>

#include <asm/reboot.h>

/*
 * Urgs ...  Too many MIPS machines to handle this in a generic way.
 * So handle all using function pointers to machine specific
 * functions.
 */
void (*_machine_restart)(char *command);
void (*_machine_halt)(void);
void (*pm_power_off)(void);

void machine_restart(char *command)
{
	if (_machine_restart)
		_machine_restart(command);
}

void machine_halt(void)
{
	if (_machine_halt)
		_machine_halt();
}

void machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
}
