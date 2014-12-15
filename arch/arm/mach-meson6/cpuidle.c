/*
 * arch/arm/plat-meson/include/mach/plat-cpuidle.h
 *
 * MESON6 cpuidle driver
 *
 * Copyright (C) 2012 Amlogic
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */


#include <linux/sched.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/export.h>
#include <linux/clockchips.h>

#include <asm/proc-fns.h>
#include <plat/plat-cpuidle.h>

/* Machine specific information */
struct meson6_idle_statedata {
	u32 cpu_state;
	u32 mpu_logic_state;
	u32 mpu_state;
};
/* Powerdomain basic power states */
#define PWRDM_POWER_OFF		0x0
#define PWRDM_POWER_RET		0x1
#define PWRDM_POWER_INACTIVE	0x2
#define PWRDM_POWER_ON		0x3


static struct meson6_idle_statedata meson6_idle_data[] = {
	{
		.cpu_state = PWRDM_POWER_ON,
		.mpu_state = PWRDM_POWER_ON,
		.mpu_logic_state = PWRDM_POWER_RET,
	},
/*	{
		.cpu_state = PWRDM_POWER_OFF,
		.mpu_state = PWRDM_POWER_RET,
		.mpu_logic_state = PWRDM_POWER_RET,
	},*/
};

/*
 * For each cpu, setup the broadcast timer because local timers
 * stops for the states above C1.
 */
static void meson6_setup_broadcast_timer(void *arg)
{
	int cpu = smp_processor_id();
	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ON, &cpu);
}

static DEFINE_PER_CPU(struct cpuidle_device, meson6_idle_dev);

static struct cpuidle_driver meson6_idle_driver = {
	.name				= "meson6_idle",
	.owner				= THIS_MODULE,
	.en_core_tk_irqen		= 1,
	.states = {
		{
			/* C1 - CPU WFI */
			.exit_latency = 10, //just for test
			.target_residency = 20,
			.flags = CPUIDLE_FLAG_TIME_VALID,
			.enter = meson_enter_idle_simple,
			.name = "C1",
			.desc = "CPU WFI"
		},
#if 0
		{
			/* C2 - CPU POWER OFF: MMU, VFP would be off*/
			.exit_latency = 200, //just for test
			.target_residency = 250,
			.flags = CPUIDLE_FLAG_TIME_VALID,
			.enter = meson_enter_idle_cpu_off,
			.name = "C2",
			.desc = "CPU OFF",
		},
#endif
	},
	.state_count = ARRAY_SIZE(meson6_idle_data),
	.safe_state_index = 0,
};

/* Public functions */

/**
 * meson6_idle_init - Init routine for MESON6 idle
 *
 * Registers the MESON6 specific cpuidle driver to the cpuidle
 * framework with the valid set of states.
 */
int __init meson6_idle_init(void)
{
	struct cpuidle_device *dev;
	unsigned int cpu_id = 0;

	/* Configure the broadcast timer on each cpu */
	on_each_cpu(meson6_setup_broadcast_timer, NULL, 1);

	for_each_cpu(cpu_id, cpu_online_mask) {
		dev = &per_cpu(meson6_idle_dev, cpu_id);
		dev->cpu = cpu_id;

		printk("Meson6 cpuidle driver register = %d\n",cpuidle_register_driver(&meson6_idle_driver));
		
		//cpuidle_register_driver(&meson6_idle_driver);

		if (cpuidle_register_device(dev)) {
			pr_err("%s: CPUidle register failed\n", __func__);
			return -EIO;
		}
	}

	return 0;
}

late_initcall(meson6_idle_init);

