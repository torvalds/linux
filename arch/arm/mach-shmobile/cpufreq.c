/*
 * CPUFreq support code for SH-Mobile ARM
 *
 *  Copyright (C) 2014 Gaku Inami
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/platform_device.h>

int __init shmobile_cpufreq_init(void)
{
	platform_device_register_simple("cpufreq-dt", -1, NULL, 0);
	return 0;
}
