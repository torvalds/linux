/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 * Copyright 2012 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/cpuidle.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>

static struct cpuidle_device __percpu * imx_cpuidle_devices;

static void __init imx_cpuidle_devices_uninit(void)
{
	int cpu_id;
	struct cpuidle_device *dev;

	for_each_possible_cpu(cpu_id) {
		dev = per_cpu_ptr(imx_cpuidle_devices, cpu_id);
		cpuidle_unregister_device(dev);
	}

	free_percpu(imx_cpuidle_devices);
}

int __init imx_cpuidle_init(struct cpuidle_driver *drv)
{
	struct cpuidle_device *dev;
	int cpu_id, ret;

	if (drv->state_count > CPUIDLE_STATE_MAX) {
		pr_err("%s: state_count exceeds maximum\n", __func__);
		return -EINVAL;
	}

	ret = cpuidle_register_driver(drv);
	if (ret) {
		pr_err("%s: Failed to register cpuidle driver with error: %d\n",
			 __func__, ret);
		return ret;
	}

	imx_cpuidle_devices = alloc_percpu(struct cpuidle_device);
	if (imx_cpuidle_devices == NULL) {
		ret = -ENOMEM;
		goto unregister_drv;
	}

	/* initialize state data for each cpuidle_device */
	for_each_possible_cpu(cpu_id) {
		dev = per_cpu_ptr(imx_cpuidle_devices, cpu_id);
		dev->cpu = cpu_id;
		dev->state_count = drv->state_count;

		ret = cpuidle_register_device(dev);
		if (ret) {
			pr_err("%s: Failed to register cpu %u, error: %d\n",
				__func__, cpu_id, ret);
			goto uninit;
		}
	}

	return 0;

uninit:
	imx_cpuidle_devices_uninit();

unregister_drv:
	cpuidle_unregister_driver(drv);
	return ret;
}
