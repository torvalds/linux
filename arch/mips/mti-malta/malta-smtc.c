/*
 * Malta Platform-specific hooks for SMP operation
 */
#include <linux/irq.h>
#include <linux/init.h>

#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/smtc.h>
#include <asm/smtc_ipi.h>

/* VPE/SMP Prototype implements platform interfaces directly */

/*
 * Cause the specified action to be performed on a targeted "CPU"
 */

static void msmtc_send_ipi_single(int cpu, unsigned int action)
{
	/* "CPU" may be TC of same VPE, VPE of same CPU, or different CPU */
	smtc_send_ipi(cpu, LINUX_SMP_IPI, action);
}

static void msmtc_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	unsigned int i;

	for_each_cpu(i, mask)
		msmtc_send_ipi_single(i, action);
}

/*
 * Post-config but pre-boot cleanup entry point
 */
static void __cpuinit msmtc_init_secondary(void)
{
	int myvpe;

	/* Don't enable Malta I/O interrupts (IP2) for secondary VPEs */
	myvpe = read_c0_tcbind() & TCBIND_CURVPE;
	if (myvpe != 0) {
		/* Ideally, this should be done only once per VPE, but... */
		clear_c0_status(ST0_IM);
		set_c0_status((0x100 << cp0_compare_irq)
				| (0x100 << MIPS_CPU_IPI_IRQ));
		if (cp0_perfcount_irq >= 0)
			set_c0_status(0x100 << cp0_perfcount_irq);
	}

	smtc_init_secondary();
}

/*
 * Platform "CPU" startup hook
 */
static void __cpuinit msmtc_boot_secondary(int cpu, struct task_struct *idle)
{
	smtc_boot_secondary(cpu, idle);
}

/*
 * SMP initialization finalization entry point
 */
static void __cpuinit msmtc_smp_finish(void)
{
	smtc_smp_finish();
}

/*
 * Hook for after all CPUs are online
 */

static void msmtc_cpus_done(void)
{
}

/*
 * Platform SMP pre-initialization
 *
 * As noted above, we can assume a single CPU for now
 * but it may be multithreaded.
 */

static void __init msmtc_smp_setup(void)
{
	/*
	 * we won't get the definitive value until
	 * we've run smtc_prepare_cpus later, but
	 * we would appear to need an upper bound now.
	 */
	smp_num_siblings = smtc_build_cpu_map(0);
}

static void __init msmtc_prepare_cpus(unsigned int max_cpus)
{
	smtc_prepare_cpus(max_cpus);
}

struct plat_smp_ops msmtc_smp_ops = {
	.send_ipi_single	= msmtc_send_ipi_single,
	.send_ipi_mask		= msmtc_send_ipi_mask,
	.init_secondary		= msmtc_init_secondary,
	.smp_finish		= msmtc_smp_finish,
	.cpus_done		= msmtc_cpus_done,
	.boot_secondary		= msmtc_boot_secondary,
	.smp_setup		= msmtc_smp_setup,
	.prepare_cpus		= msmtc_prepare_cpus,
};

#ifdef CONFIG_MIPS_MT_SMTC_IRQAFF
/*
 * IRQ affinity hook
 */


int plat_set_irq_affinity(struct irq_data *d, const struct cpumask *affinity,
			  bool force)
{
	cpumask_t tmask;
	int cpu = 0;
	void smtc_set_irq_affinity(unsigned int irq, cpumask_t aff);

	/*
	 * On the legacy Malta development board, all I/O interrupts
	 * are routed through the 8259 and combined in a single signal
	 * to the CPU daughterboard, and on the CoreFPGA2/3 34K models,
	 * that signal is brought to IP2 of both VPEs. To avoid racing
	 * concurrent interrupt service events, IP2 is enabled only on
	 * one VPE, by convention VPE0.	 So long as no bits are ever
	 * cleared in the affinity mask, there will never be any
	 * interrupt forwarding.  But as soon as a program or operator
	 * sets affinity for one of the related IRQs, we need to make
	 * sure that we don't ever try to forward across the VPE boundary,
	 * at least not until we engineer a system where the interrupt
	 * _ack() or _end() function can somehow know that it corresponds
	 * to an interrupt taken on another VPE, and perform the appropriate
	 * restoration of Status.IM state using MFTR/MTTR instead of the
	 * normal local behavior. We also ensure that no attempt will
	 * be made to forward to an offline "CPU".
	 */

	cpumask_copy(&tmask, affinity);
	for_each_cpu(cpu, affinity) {
		if ((cpu_data[cpu].vpe_id != 0) || !cpu_online(cpu))
			cpu_clear(cpu, tmask);
	}
	cpumask_copy(d->affinity, &tmask);

	if (cpus_empty(tmask))
		/*
		 * We could restore a default mask here, but the
		 * runtime code can anyway deal with the null set
		 */
		printk(KERN_WARNING
		       "IRQ affinity leaves no legal CPU for IRQ %d\n", d->irq);

	/* Do any generic SMTC IRQ affinity setup */
	smtc_set_irq_affinity(d->irq, tmask);

	return IRQ_SET_MASK_OK_NOCOPY;
}
#endif /* CONFIG_MIPS_MT_SMTC_IRQAFF */
