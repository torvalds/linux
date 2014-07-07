/*
 * CPUFreq support code for SH-Mobile ARM
 *
 *  Copyright (C) 2014 Gaku Inami
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

int __init shmobile_cpufreq_init(void)
{
	struct device_node *np;

	np = of_cpu_device_node_get(0);
	if (np == NULL) {
		pr_err("failed to find cpu0 node\n");
		return 0;
	}

	if (of_get_property(np, "operating-points", NULL))
		platform_device_register_simple("cpufreq-cpu0", -1, NULL, 0);

	of_node_put(np);

	return 0;
}
