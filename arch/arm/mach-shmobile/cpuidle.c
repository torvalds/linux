/*
 * CPUIdle support code for SH-Mobile ARM
 *
 *  Copyright (C) 2011 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/pm.h>
#include <linux/cpuidle.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/err.h>
#include <asm/cpuidle.h>
#include <asm/io.h>

int shmobile_enter_wfi(struct cpuidle_device *dev, struct cpuidle_driver *drv,
		       int index)
{
	cpu_do_idle();
	return 0;
}

static struct cpuidle_device shmobile_cpuidle_dev;
static struct cpuidle_driver shmobile_cpuidle_default_driver = {
	.name			= "shmobile_cpuidle",
	.owner			= THIS_MODULE,
	.en_core_tk_irqen	= 1,
	.states[0]		= ARM_CPUIDLE_WFI_STATE,
	.states[0].enter	= shmobile_enter_wfi,
	.safe_state_index	= 0, /* C1 */
	.state_count		= 1,
};

static struct cpuidle_driver *cpuidle_drv = &shmobile_cpuidle_default_driver;

void shmobile_cpuidle_set_driver(struct cpuidle_driver *drv)
{
	cpuidle_drv = drv;
}

int shmobile_cpuidle_init(void)
{
	struct cpuidle_device *dev = &shmobile_cpuidle_dev;

	cpuidle_register_driver(cpuidle_drv);

	dev->state_count = cpuidle_drv->state_count;
	cpuidle_register_device(dev);

	return 0;
}
