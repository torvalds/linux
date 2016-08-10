/*
 * SMP support for pSeries machines.
 *
 * Dave Engebretsen, Peter Bergner, and
 * Mike Corrigan {engebret|bergner|mikec}@us.ibm.com
 *
 * Plus various changes from other IBM teams...
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */


#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/cpu.h>

#include <asm/ptrace.h>
#include <linux/atomic.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/paca.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/firmware.h>
#include <asm/rtas.h>
#include <asm/vdso_datapage.h>
#include <asm/cputhreads.h>
#include <asm/xics.h>
#include <asm/dbell.h>
#include <asm/plpar_wrappers.h>
#include <asm/code-patching.h>

#include "pseries.h"
#include "offline_states.h"


/*
 * The Primary thread of each non-boot processor was started from the OF client
 * interface by prom_hold_cpus and is spinning on secondary_hold_spinloop.
 */
static cpumask_var_t of_spin_mask;

/*
 * If we multiplex IPI mechanisms, store the appropriate XICS IPI mechanism here
 */
static void  (*xics_cause_ipi)(int cpu, unsigned long data);

/* Query where a cpu is now.  Return codes #defined in plpar_wrappers.h */
int smp_query_cpu_stopped(unsigned int pcpu)
{
	int cpu_status, status;
	int qcss_tok = rtas_token("query-cpu-stopped-state");

	if (qcss_tok == RTAS_UNKNOWN_SERVICE) {
		printk_once(KERN_INFO
			"Firmware doesn't support query-cpu-stopped-state\n");
		return QCSS_HARDWARE_ERROR;
	}

	status = rtas_call(qcss_tok, 1, 2, &cpu_status, pcpu);
	if (status != 0) {
		printk(KERN_ERR
		       "RTAS query-cpu-stopped-state failed: %i\n", status);
		return status;
	}

	return cpu_status;
}

/**
 * smp_startup_cpu() - start the given cpu
 *
 * At boot time, there is nothing to do for primary threads which were
 * started from Open Firmware.  For anything else, call RTAS with the
 * appropriate start location.
 *
 * Returns:
 *	0	- failure
 *	1	- success
 */
static inline int smp_startup_cpu(unsigned int lcpu)
{
	int status;
	unsigned long start_here =
			__pa(ppc_function_entry(generic_secondary_smp_init));
	unsigned int pcpu;
	int start_cpu;

	if (cpumask_test_cpu(lcpu, of_spin_mask))
		/* Already started by OF and sitting in spin loop */
		return 1;

	pcpu = get_hard_smp_processor_id(lcpu);

	/* Check to see if the CPU out of FW already for kexec */
	if (smp_query_cpu_stopped(pcpu) == QCSS_NOT_STOPPED){
		cpumask_set_cpu(lcpu, of_spin_mask);
		return 1;
	}

	/* Fixup atomic count: it exited inside IRQ handler. */
	task_thread_info(paca[lcpu].__current)->preempt_count	= 0;
#ifdef CONFIG_HOTPLUG_CPU
	if (get_cpu_current_state(lcpu) == CPU_STATE_INACTIVE)
		goto out;
#endif
	/* 
	 * If the RTAS start-cpu token does not exist then presume the
	 * cpu is already spinning.
	 */
	start_cpu = rtas_token("start-cpu");
	if (start_cpu == RTAS_UNKNOWN_SERVICE)
		return 1;

	status = rtas_call(start_cpu, 3, 1, NULL, pcpu, start_here, pcpu);
	if (status != 0) {
		printk(KERN_ERR "start-cpu failed: %i\n", status);
		return 0;
	}

#ifdef CONFIG_HOTPLUG_CPU
out:
#endif
	return 1;
}

static void smp_setup_cpu(int cpu)
{
	if (cpu != boot_cpuid)
		xics_setup_cpu();
	if (cpu_has_feature(CPU_FTR_DBELL))
		doorbell_setup_this_cpu();

	if (firmware_has_feature(FW_FEATURE_SPLPAR))
		vpa_init(cpu);

	cpumask_clear_cpu(cpu, of_spin_mask);
#ifdef CONFIG_HOTPLUG_CPU
	set_cpu_current_state(cpu, CPU_STATE_ONLINE);
	set_default_offline_state(cpu);
#endif
}

static int smp_pSeries_kick_cpu(int nr)
{
	BUG_ON(nr < 0 || nr >= NR_CPUS);

	if (!smp_startup_cpu(nr))
		return -ENOENT;

	/*
	 * The processor is currently spinning, waiting for the
	 * cpu_start field to become non-zero After we set cpu_start,
	 * the processor will continue on to secondary_start
	 */
	paca[nr].cpu_start = 1;
#ifdef CONFIG_HOTPLUG_CPU
	set_preferred_offline_state(nr, CPU_STATE_ONLINE);

	if (get_cpu_current_state(nr) == CPU_STATE_INACTIVE) {
		long rc;
		unsigned long hcpuid;

		hcpuid = get_hard_smp_processor_id(nr);
		rc = plpar_hcall_norets(H_PROD, hcpuid);
		if (rc != H_SUCCESS)
			printk(KERN_ERR "Error: Prod to wake up processor %d "
						"Ret= %ld\n", nr, rc);
	}
#endif

	return 0;
}

/* Only used on systems that support multiple IPI mechanisms */
static void pSeries_cause_ipi_mux(int cpu, unsigned long data)
{
	if (cpumask_test_cpu(cpu, cpu_sibling_mask(smp_processor_id())))
		doorbell_cause_ipi(cpu, data);
	else
		xics_cause_ipi(cpu, data);
}

static __init void pSeries_smp_probe(void)
{
	xics_smp_probe();

	if (cpu_has_feature(CPU_FTR_DBELL)) {
		xics_cause_ipi = smp_ops->cause_ipi;
		smp_ops->cause_ipi = pSeries_cause_ipi_mux;
	}
}

static struct smp_ops_t pseries_smp_ops = {
	.message_pass	= NULL,	/* Use smp_muxed_ipi_message_pass */
	.cause_ipi	= NULL,	/* Filled at runtime by pSeries_smp_probe() */
	.probe		= pSeries_smp_probe,
	.kick_cpu	= smp_pSeries_kick_cpu,
	.setup_cpu	= smp_setup_cpu,
	.cpu_bootable	= smp_generic_cpu_bootable,
};

/* This is called very early */
void __init smp_init_pseries(void)
{
	int i;

	pr_debug(" -> smp_init_pSeries()\n");
	smp_ops = &pseries_smp_ops;

	alloc_bootmem_cpumask_var(&of_spin_mask);

	/*
	 * Mark threads which are still spinning in hold loops
	 *
	 * We know prom_init will not have started them if RTAS supports
	 * query-cpu-stopped-state.
	 */
	if (rtas_token("query-cpu-stopped-state") == RTAS_UNKNOWN_SERVICE) {
		if (cpu_has_feature(CPU_FTR_SMT)) {
			for_each_present_cpu(i) {
				if (cpu_thread_in_core(i) == 0)
					cpumask_set_cpu(i, of_spin_mask);
			}
		} else
			cpumask_copy(of_spin_mask, cpu_present_mask);

		cpumask_clear_cpu(boot_cpuid, of_spin_mask);
	}

	/* Non-lpar has additional take/give timebase */
	if (rtas_token("freeze-time-base") != RTAS_UNKNOWN_SERVICE) {
		smp_ops->give_timebase = rtas_give_timebase;
		smp_ops->take_timebase = rtas_take_timebase;
	}

	pr_debug(" <- smp_init_pSeries()\n");
}
