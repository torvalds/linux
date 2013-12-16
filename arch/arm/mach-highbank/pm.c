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

#include <linux/cpu_pm.h>
#include <linux/init.h>
#include <linux/suspend.h>

#include <asm/suspend.h>
#include <asm/psci.h>

static int highbank_suspend_finish(unsigned long val)
{
	const struct psci_power_state ps = {
		.type = PSCI_POWER_STATE_TYPE_POWER_DOWN,
		.affinity_level = 1,
	};

	return psci_ops.cpu_suspend(ps, __pa(cpu_resume));
}

static int highbank_pm_enter(suspend_state_t state)
{
	cpu_pm_enter();
	cpu_cluster_pm_enter();

	cpu_suspend(0, highbank_suspend_finish);

	cpu_cluster_pm_exit();
	cpu_pm_exit();

	return 0;
}

static const struct platform_suspend_ops highbank_pm_ops = {
	.enter = highbank_pm_enter,
	.valid = suspend_valid_only_mem,
};

void __init highbank_pm_init(void)
{
	if (!psci_ops.cpu_suspend)
		return;

	suspend_set_ops(&highbank_pm_ops);
}
