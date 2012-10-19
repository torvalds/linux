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

#include <linux/init.h>
#include <linux/io.h>
#include <linux/suspend.h>

#include <asm/proc-fns.h>
#include <asm/smp_scu.h>
#include <asm/suspend.h>

#include "core.h"
#include "sysregs.h"

static int highbank_suspend_finish(unsigned long val)
{
	cpu_do_idle();
	return 0;
}

static int highbank_pm_enter(suspend_state_t state)
{
	hignbank_set_pwr_suspend();
	highbank_set_cpu_jump(0, cpu_resume);

	scu_power_mode(scu_base_addr, SCU_PM_POWEROFF);
	cpu_suspend(0, highbank_suspend_finish);

	return 0;
}

static const struct platform_suspend_ops highbank_pm_ops = {
	.enter = highbank_pm_enter,
	.valid = suspend_valid_only_mem,
};

void __init highbank_pm_init(void)
{
	suspend_set_ops(&highbank_pm_ops);
}
