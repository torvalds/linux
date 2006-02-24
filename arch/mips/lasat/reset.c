/*
 * Thomas Horsten <thh@lasat.com>
 * Copyright (C) 2000 LASAT Networks A/S.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Reset the LASAT board.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pm.h>

#include <asm/reboot.h>
#include <asm/system.h>
#include <asm/lasat/lasat.h>

#include "picvue.h"
#include "prom.h"

static void lasat_machine_restart(char *command);
static void lasat_machine_halt(void);

/* Used to set machine to boot in service mode via /proc interface */
int lasat_boot_to_service = 0;

static void lasat_machine_restart(char *command)
{
	local_irq_disable();

	if (lasat_boot_to_service) {
		printk("machine_restart: Rebooting to service mode\n");
		*(volatile unsigned int *)0xa0000024 = 0xdeadbeef;
		*(volatile unsigned int *)0xa00000fc = 0xfedeabba;
	}
	*lasat_misc->reset_reg = 0xbedead;
	for (;;) ;
}

#define MESSAGE "System halted"
static void lasat_machine_halt(void)
{
	local_irq_disable();

	/* Disable interrupts and loop forever */
	printk(KERN_NOTICE MESSAGE "\n");
#ifdef CONFIG_PICVUE
	pvc_clear();
	pvc_write_string(MESSAGE, 0, 0);
#endif
	prom_monitor();
	for (;;) ;
}

void lasat_reboot_setup(void)
{
	_machine_restart = lasat_machine_restart;
	_machine_halt = lasat_machine_halt;
	pm_power_off = lasat_machine_halt;
}
