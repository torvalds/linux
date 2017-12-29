/*
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Copyright (C) 2007 MIPS Technologies, Inc.
 *    Chris Dearman (chris@mips.com)
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/sched/task_stack.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/compiler.h>

#include <linux/atomic.h>
#include <asm/cacheflush.h>
#include <asm/cpu.h>
#include <asm/processor.h>
#include <asm/hardirq.h>
#include <asm/mmu_context.h>
#include <asm/smp.h>
#include <asm/time.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/mips_mt.h>
#include <asm/amon.h>

static void cmp_init_secondary(void)
{
	struct cpuinfo_mips *c __maybe_unused = &current_cpu_data;

	/* Assume GIC is present */
	change_c0_status(ST0_IM, STATUSF_IP2 | STATUSF_IP3 | STATUSF_IP4 |
				 STATUSF_IP5 | STATUSF_IP6 | STATUSF_IP7);

	/* Enable per-cpu interrupts: platform specific */

#ifdef CONFIG_MIPS_MT_SMP
	if (cpu_has_mipsmt)
		cpu_set_vpe_id(c, (read_c0_tcbind() >> TCBIND_CURVPE_SHIFT) &
				  TCBIND_CURVPE);
#endif
}

static void cmp_smp_finish(void)
{
	pr_debug("SMPCMP: CPU%d: %s\n", smp_processor_id(), __func__);

	/* CDFIXME: remove this? */
	write_c0_compare(read_c0_count() + (8 * mips_hpt_frequency / HZ));

#ifdef CONFIG_MIPS_MT_FPAFF
	/* If we have an FPU, enroll ourselves in the FPU-full mask */
	if (cpu_has_fpu)
		cpumask_set_cpu(smp_processor_id(), &mt_fpu_cpumask);
#endif /* CONFIG_MIPS_MT_FPAFF */

	local_irq_enable();
}

/*
 * Setup the PC, SP, and GP of a secondary processor and start it running
 * smp_bootstrap is the place to resume from
 * __KSTK_TOS(idle) is apparently the stack pointer
 * (unsigned long)idle->thread_info the gp
 */
static int cmp_boot_secondary(int cpu, struct task_struct *idle)
{
	struct thread_info *gp = task_thread_info(idle);
	unsigned long sp = __KSTK_TOS(idle);
	unsigned long pc = (unsigned long)&smp_bootstrap;
	unsigned long a0 = 0;

	pr_debug("SMPCMP: CPU%d: %s cpu %d\n", smp_processor_id(),
		__func__, cpu);

#if 0
	/* Needed? */
	flush_icache_range((unsigned long)gp,
			   (unsigned long)(gp + sizeof(struct thread_info)));
#endif

	amon_cpu_start(cpu, pc, sp, (unsigned long)gp, a0);
	return 0;
}

/*
 * Common setup before any secondaries are started
 */
void __init cmp_smp_setup(void)
{
	int i;
	int ncpu = 0;

	pr_debug("SMPCMP: CPU%d: %s\n", smp_processor_id(), __func__);

#ifdef CONFIG_MIPS_MT_FPAFF
	/* If we have an FPU, enroll ourselves in the FPU-full mask */
	if (cpu_has_fpu)
		cpumask_set_cpu(0, &mt_fpu_cpumask);
#endif /* CONFIG_MIPS_MT_FPAFF */

	for (i = 1; i < NR_CPUS; i++) {
		if (amon_cpu_avail(i)) {
			set_cpu_possible(i, true);
			__cpu_number_map[i]	= ++ncpu;
			__cpu_logical_map[ncpu] = i;
		}
	}

	if (cpu_has_mipsmt) {
		unsigned int nvpe = 1;
#ifdef CONFIG_MIPS_MT_SMP
		unsigned int mvpconf0 = read_c0_mvpconf0();

		nvpe = ((mvpconf0 & MVPCONF0_PVPE) >> MVPCONF0_PVPE_SHIFT) + 1;
#endif
		smp_num_siblings = nvpe;
	}
	pr_info("Detected %i available secondary CPU(s)\n", ncpu);
}

void __init cmp_prepare_cpus(unsigned int max_cpus)
{
	pr_debug("SMPCMP: CPU%d: %s max_cpus=%d\n",
		 smp_processor_id(), __func__, max_cpus);

#ifdef CONFIG_MIPS_MT
	/*
	 * FIXME: some of these options are per-system, some per-core and
	 * some per-cpu
	 */
	mips_mt_set_cpuoptions();
#endif

}

const struct plat_smp_ops cmp_smp_ops = {
	.send_ipi_single	= mips_smp_send_ipi_single,
	.send_ipi_mask		= mips_smp_send_ipi_mask,
	.init_secondary		= cmp_init_secondary,
	.smp_finish		= cmp_smp_finish,
	.boot_secondary		= cmp_boot_secondary,
	.smp_setup		= cmp_smp_setup,
	.prepare_cpus		= cmp_prepare_cpus,
};
