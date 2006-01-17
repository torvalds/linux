/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 by Ralf Baechle
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/reboot.h>
#include <asm/reboot.h>

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

/*
 * Urgs ...  Too many MIPS machines to handle this in a generic way.
 * So handle all using function pointers to machine specific
 * functions.
 */
void (*_machine_restart)(char *command);
void (*_machine_halt)(void);
void (*_machine_power_off)(void);

void machine_restart(char *command)
{
	_machine_restart(command);
}

void machine_halt(void)
{
	_machine_halt();
}

void machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();

	_machine_power_off();
}

