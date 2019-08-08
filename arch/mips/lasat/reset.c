// SPDX-License-Identifier: GPL-2.0-only
/*
 * Thomas Horsten <thh@lasat.com>
 * Copyright (C) 2000 LASAT Networks A/S.
 *
 * Reset the LASAT board.
 */
#include <linux/kernel.h>
#include <linux/pm.h>

#include <asm/reboot.h>
#include <asm/lasat/lasat.h>

#include "picvue.h"
#include "prom.h"

static void lasat_machine_restart(char *command);
static void lasat_machine_halt(void);

/* Used to set machine to boot in service mode via /proc interface */
int lasat_boot_to_service;

static void lasat_machine_restart(char *command)
{
	local_irq_disable();

	if (lasat_boot_to_service) {
		*(volatile unsigned int *)0xa0000024 = 0xdeadbeef;
		*(volatile unsigned int *)0xa00000fc = 0xfedeabba;
	}
	*lasat_misc->reset_reg = 0xbedead;
	for (;;) ;
}

static void lasat_machine_halt(void)
{
	local_irq_disable();

	prom_monitor();
	for (;;) ;
}

void lasat_reboot_setup(void)
{
	_machine_restart = lasat_machine_restart;
	_machine_halt = lasat_machine_halt;
	pm_power_off = lasat_machine_halt;
}
