/*
 * Malta Platform-specific hooks for SMP operation
 */
#include <linux/init.h>

#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/smtc.h>
#include <asm/smtc_ipi.h>

/* VPE/SMP Prototype implements platform interfaces directly */

/*
 * Cause the specified action to be performed on a targeted "CPU"
 */

void core_send_ipi(int cpu, unsigned int action)
{
	/* "CPU" may be TC of same VPE, VPE of same CPU, or different CPU */
	smtc_send_ipi(cpu, LINUX_SMP_IPI, action);
}

/*
 * Platform "CPU" startup hook
 */

void prom_boot_secondary(int cpu, struct task_struct *idle)
{
	smtc_boot_secondary(cpu, idle);
}

/*
 * Post-config but pre-boot cleanup entry point
 */

void prom_init_secondary(void)
{
        void smtc_init_secondary(void);
	int myvpe;

	/* Don't enable Malta I/O interrupts (IP2) for secondary VPEs */
	myvpe = read_c0_tcbind() & TCBIND_CURVPE;
	if (myvpe != 0) {
		/* Ideally, this should be done only once per VPE, but... */
		clear_c0_status(STATUSF_IP2);
		set_c0_status(STATUSF_IP0 | STATUSF_IP1 | STATUSF_IP3
				| STATUSF_IP4 | STATUSF_IP5 | STATUSF_IP6
				| STATUSF_IP7);
	}

        smtc_init_secondary();
}

/*
 * Platform SMP pre-initialization
 *
 * As noted above, we can assume a single CPU for now
 * but it may be multithreaded.
 */

void __cpuinit plat_smp_setup(void)
{
	if (read_c0_config3() & (1<<2))
		mipsmt_build_cpu_map(0);
}

void __init plat_prepare_cpus(unsigned int max_cpus)
{
	if (read_c0_config3() & (1<<2))
		mipsmt_prepare_cpus();
}

/*
 * SMP initialization finalization entry point
 */

void prom_smp_finish(void)
{
	smtc_smp_finish();
}

/*
 * Hook for after all CPUs are online
 */

void prom_cpus_done(void)
{
}
