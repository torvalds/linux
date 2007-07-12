/*
 * File:         arch/blackfin/mach-bf548/cpu.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:  clock scaling for the bf54x
 *
 * Modified:
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <asm/dpmc.h>
#include <linux/fs.h>
#include <asm/bfin-global.h>

/* CONFIG_CLKIN_HZ=25000000 */
#define VCO5 (CONFIG_CLKIN_HZ*45)
#define VCO4 (CONFIG_CLKIN_HZ*36)
#define VCO3 (CONFIG_CLKIN_HZ*27)
#define VCO2 (CONFIG_CLKIN_HZ*18)
#define VCO1 (CONFIG_CLKIN_HZ*9)
#define VCO(x) VCO##x

#define MFREQ(x) {VCO(x),VCO(x)/4},{VCO(x),VCO(x)/2},{VCO(x),VCO(x)}
/* frequency */
static struct cpufreq_frequency_table bf548_freq_table[] = {
	MFREQ(1),
	MFREQ(3),
	{VCO4, VCO4 / 2}, {VCO4, VCO4},
	MFREQ(5),
	{0, CPUFREQ_TABLE_END},
};

/*
 * dpmc_fops->ioctl()
 * static int dpmc_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
 */
static int bf548_getfreq(unsigned int cpu)
{
	unsigned long cclk_mhz;

	/* The driver only support single cpu */
	if (cpu == 0)
		dpmc_fops.ioctl(NULL, NULL, IOCTL_GET_CORECLOCK, &cclk_mhz);
	else
		cclk_mhz = -1;

	return cclk_mhz;
}

static int bf548_target(struct cpufreq_policy *policy,
			    unsigned int target_freq, unsigned int relation)
{
	unsigned long cclk_mhz;
	unsigned long vco_mhz;
	unsigned long flags;
	unsigned int index;
	struct cpufreq_freqs freqs;

	if (cpufreq_frequency_table_target(policy, bf548_freq_table, target_freq, relation, &index))
		return -EINVAL;

	cclk_mhz = bf548_freq_table[index].frequency;
	vco_mhz = bf548_freq_table[index].index;

	dpmc_fops.ioctl(NULL, NULL, IOCTL_CHANGE_FREQUENCY, &vco_mhz);
	freqs.old = bf548_getfreq(0);
	freqs.new = cclk_mhz;
	freqs.cpu = 0;

	pr_debug("cclk begin change to cclk %d,vco=%d,index=%d,target=%d,oldfreq=%d\n",
	         cclk_mhz, vco_mhz, index, target_freq, freqs.old);

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	local_irq_save(flags);
	dpmc_fops.ioctl(NULL, NULL, IOCTL_SET_CCLK, &cclk_mhz);
	local_irq_restore(flags);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	vco_mhz = get_vco();
	cclk_mhz = get_cclk();
	return 0;
}

/* make sure that only the "userspace" governor is run -- anything else wouldn't make sense on
 * this platform, anyway.
 */
static int bf548_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, &bf548_freq_table);
}

static int __init __bf548_cpu_init(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return -EINVAL;

	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;

	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	/*Now ,only support one cpu */
	policy->cur = bf548_getfreq(0);
	cpufreq_frequency_table_get_attr(bf548_freq_table, policy->cpu);
	return cpufreq_frequency_table_cpuinfo(policy, bf548_freq_table);
}

static struct freq_attr *bf548_freq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver bf548_driver = {
	.verify = bf548_verify_speed,
	.target = bf548_target,
	.get = bf548_getfreq,
	.init = __bf548_cpu_init,
	.name = "bf548",
	.owner = THIS_MODULE,
	.attr = bf548_freq_attr,
};

static int __init bf548_cpu_init(void)
{
	return cpufreq_register_driver(&bf548_driver);
}

static void __exit bf548_cpu_exit(void)
{
	cpufreq_unregister_driver(&bf548_driver);
}

MODULE_AUTHOR("Mickael Kang");
MODULE_DESCRIPTION("cpufreq driver for BF548 CPU");
MODULE_LICENSE("GPL");

module_init(bf548_cpu_init);
module_exit(bf548_cpu_exit);
