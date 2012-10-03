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
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/smp.h>

#include <asm/smp_scu.h>
#include <asm/cacheflush.h>

#include "core.h"

extern void secondary_startup(void);

/*
 * platform-specific code to shutdown a CPU
 *
 */
void __ref highbank_cpu_die(unsigned int cpu)
{
	flush_cache_all();

	highbank_set_cpu_jump(cpu, secondary_startup);
	scu_power_mode(scu_base_addr, SCU_PM_POWEROFF);

	cpu_do_idle();

	/* We should never return from idle */
	panic("highbank: cpu %d unexpectedly exit from shutdown\n", cpu);
}
