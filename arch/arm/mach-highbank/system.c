/*
 * Copyright 2011 Calxeda, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/io.h>
#include <asm/proc-fns.h>

#include "core.h"
#include "sysregs.h"

void highbank_restart(char mode, const char *cmd)
{
	if (mode == 'h')
		highbank_set_pwr_hard_reset();
	else
		highbank_set_pwr_soft_reset();

	while (1)
		cpu_do_idle();
}

