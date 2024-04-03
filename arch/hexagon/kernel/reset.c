// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <linux/reboot.h>
#include <linux/smp.h>
#include <asm/hexagon_vm.h>

void machine_power_off(void)
{
	smp_send_stop();
	__vmstop();
}

void machine_halt(void)
{
}

void machine_restart(char *cmd)
{
}

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);
