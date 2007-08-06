/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 by Ralf Baechle (ralf@linux-mips.org)
 *
 * Symmetric Uniprocessor (TM) Support
 */
#include <linux/kernel.h>
#include <linux/sched.h>

/*
 * Send inter-processor interrupt
 */
void core_send_ipi(int cpu, unsigned int action)
{
	panic(KERN_ERR "%s called", __FUNCTION__);
}

/*
 *  After we've done initial boot, this function is called to allow the
 *  board code to clean up state, if needed
 */
void __cpuinit prom_init_secondary(void)
{
}

void __cpuinit prom_smp_finish(void)
{
}

/* Hook for after all CPUs are online */
void prom_cpus_done(void)
{
}

void __init prom_prepare_cpus(unsigned int max_cpus)
{
	cpus_clear(phys_cpu_present_map);
}

/*
 * Firmware CPU startup hook
 */
void __cpuinit prom_boot_secondary(int cpu, struct task_struct *idle)
{
}

void __init plat_smp_setup(void)
{
}
void __init plat_prepare_cpus(unsigned int max_cpus)
{
}
