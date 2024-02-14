// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2011 Calxeda, Inc.
 */
#include <linux/io.h>
#include <asm/proc-fns.h>
#include <linux/reboot.h>

#include "core.h"
#include "sysregs.h"

void highbank_restart(enum reboot_mode mode, const char *cmd)
{
	if (mode == REBOOT_HARD)
		highbank_set_pwr_hard_reset();
	else
		highbank_set_pwr_soft_reset();

	while (1)
		cpu_do_idle();
}

