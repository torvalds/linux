/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006, 07 by Ralf Baechle (ralf@linux-mips.org)
 *
 * Symmetric Uniprocessor (TM) Support
 */
#include <linux/kernel.h>
#include <linux/sched.h>

/*
 * Send inter-processor interrupt
 */
static void up_send_ipi_single(int cpu, unsigned int action)
{
	panic(KERN_ERR "%s called", __func__);
}

static inline void up_send_ipi_mask(const struct cpumask *mask,
				    unsigned int action)
{
	panic(KERN_ERR "%s called", __func__);
}

/*
 *  After we've done initial boot, this function is called to allow the
 *  board code to clean up state, if needed
 */
static void up_init_secondary(void)
{
}

static void up_smp_finish(void)
{
}

/*
 * Firmware CPU startup hook
 */
static void up_boot_secondary(int cpu, struct task_struct *idle)
{
}

static void __init up_smp_setup(void)
{
}

static void __init up_prepare_cpus(unsigned int max_cpus)
{
}

#ifdef CONFIG_HOTPLUG_CPU
static int up_cpu_disable(void)
{
	return -ENOSYS;
}

static void up_cpu_die(unsigned int cpu)
{
	BUG();
}
#endif

struct plat_smp_ops up_smp_ops = {
	.send_ipi_single	= up_send_ipi_single,
	.send_ipi_mask		= up_send_ipi_mask,
	.init_secondary		= up_init_secondary,
	.smp_finish		= up_smp_finish,
	.boot_secondary		= up_boot_secondary,
	.smp_setup		= up_smp_setup,
	.prepare_cpus		= up_prepare_cpus,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= up_cpu_disable,
	.cpu_die		= up_cpu_die,
#endif
};
