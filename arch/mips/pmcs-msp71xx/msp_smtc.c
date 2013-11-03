/*
 * MSP71xx Platform-specific hooks for SMP operation
 */
#include <linux/irq.h>
#include <linux/init.h>

#include <asm/mipsmtregs.h>
#include <asm/mipsregs.h>
#include <asm/smtc.h>
#include <asm/smtc_ipi.h>

/* VPE/SMP Prototype implements platform interfaces directly */

/*
 * Cause the specified action to be performed on a targeted "CPU"
 */

static void msp_smtc_send_ipi_single(int cpu, unsigned int action)
{
	/* "CPU" may be TC of same VPE, VPE of same CPU, or different CPU */
	smtc_send_ipi(cpu, LINUX_SMP_IPI, action);
}

static void msp_smtc_send_ipi_mask(const struct cpumask *mask,
						unsigned int action)
{
	unsigned int i;

	for_each_cpu(i, mask)
		msp_smtc_send_ipi_single(i, action);
}

/*
 * Post-config but pre-boot cleanup entry point
 */
static void msp_smtc_init_secondary(void)
{
	int myvpe;

	/* Don't enable Malta I/O interrupts (IP2) for secondary VPEs */
	myvpe = read_c0_tcbind() & TCBIND_CURVPE;
	if (myvpe > 0)
		change_c0_status(ST0_IM, STATUSF_IP0 | STATUSF_IP1 |
				STATUSF_IP6 | STATUSF_IP7);
	smtc_init_secondary();
}

/*
 * Platform "CPU" startup hook
 */
static void msp_smtc_boot_secondary(int cpu, struct task_struct *idle)
{
	smtc_boot_secondary(cpu, idle);
}

/*
 * SMP initialization finalization entry point
 */
static void msp_smtc_smp_finish(void)
{
	smtc_smp_finish();
}

/*
 * Hook for after all CPUs are online
 */

static void msp_smtc_cpus_done(void)
{
}

/*
 * Platform SMP pre-initialization
 *
 * As noted above, we can assume a single CPU for now
 * but it may be multithreaded.
 */

static void __init msp_smtc_smp_setup(void)
{
	/*
	 * we won't get the definitive value until
	 * we've run smtc_prepare_cpus later, but
	 */

	if (read_c0_config3() & (1 << 2))
		smp_num_siblings = smtc_build_cpu_map(0);
}

static void __init msp_smtc_prepare_cpus(unsigned int max_cpus)
{
	smtc_prepare_cpus(max_cpus);
}

struct plat_smp_ops msp_smtc_smp_ops = {
	.send_ipi_single	= msp_smtc_send_ipi_single,
	.send_ipi_mask		= msp_smtc_send_ipi_mask,
	.init_secondary		= msp_smtc_init_secondary,
	.smp_finish		= msp_smtc_smp_finish,
	.cpus_done		= msp_smtc_cpus_done,
	.boot_secondary		= msp_smtc_boot_secondary,
	.smp_setup		= msp_smtc_smp_setup,
	.prepare_cpus		= msp_smtc_prepare_cpus,
};
