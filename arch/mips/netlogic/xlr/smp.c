/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/irq.h>

#include <asm/mmu_context.h>

#include <asm/netlogic/interrupt.h>
#include <asm/netlogic/mips-extns.h>

#include <asm/netlogic/xlr/iomap.h>
#include <asm/netlogic/xlr/pic.h>
#include <asm/netlogic/xlr/xlr.h>

void core_send_ipi(int logical_cpu, unsigned int action)
{
	int cpu = cpu_logical_map(logical_cpu);
	u32 tid = cpu & 0x3;
	u32 pid = (cpu >> 2) & 0x07;
	u32 ipi = (tid << 16) | (pid << 20);

	if (action & SMP_CALL_FUNCTION)
		ipi |= IRQ_IPI_SMP_FUNCTION;
	else if (action & SMP_RESCHEDULE_YOURSELF)
		ipi |= IRQ_IPI_SMP_RESCHEDULE;
	else
		return;

	pic_send_ipi(ipi);
}

void nlm_send_ipi_single(int cpu, unsigned int action)
{
	core_send_ipi(cpu, action);
}

void nlm_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	int cpu;

	for_each_cpu(cpu, mask) {
		core_send_ipi(cpu, action);
	}
}

/* IRQ_IPI_SMP_FUNCTION Handler */
void nlm_smp_function_ipi_handler(unsigned int irq, struct irq_desc *desc)
{
	smp_call_function_interrupt();
}

/* IRQ_IPI_SMP_RESCHEDULE  handler */
void nlm_smp_resched_ipi_handler(unsigned int irq, struct irq_desc *desc)
{
	scheduler_ipi();
}

/*
 * Called before going into mips code, early cpu init
 */
void nlm_early_init_secondary(void)
{
	write_c0_ebase((uint32_t)nlm_common_ebase);
	/* TLB partition here later */
}

/*
 * Code to run on secondary just after probing the CPU
 */
static void __cpuinit nlm_init_secondary(void)
{
	nlm_smp_irq_init();
}

void nlm_smp_finish(void)
{
#ifdef notyet
	nlm_common_msgring_cpu_init();
#endif
	local_irq_enable();
}

void nlm_cpus_done(void)
{
}

/*
 * Boot all other cpus in the system, initialize them, and bring them into
 * the boot function
 */
int nlm_cpu_unblock[NR_CPUS];
int nlm_cpu_ready[NR_CPUS];
unsigned long nlm_next_gp;
unsigned long nlm_next_sp;
cpumask_t phys_cpu_present_map;

void nlm_boot_secondary(int logical_cpu, struct task_struct *idle)
{
	unsigned long gp = (unsigned long)task_thread_info(idle);
	unsigned long sp = (unsigned long)__KSTK_TOS(idle);
	int cpu = cpu_logical_map(logical_cpu);

	nlm_next_sp = sp;
	nlm_next_gp = gp;

	/* barrier */
	__sync();
	nlm_cpu_unblock[cpu] = 1;
}

void __init nlm_smp_setup(void)
{
	unsigned int boot_cpu;
	int num_cpus, i;

	boot_cpu = hard_smp_processor_id();
	cpus_clear(phys_cpu_present_map);

	cpu_set(boot_cpu, phys_cpu_present_map);
	__cpu_number_map[boot_cpu] = 0;
	__cpu_logical_map[0] = boot_cpu;
	cpu_set(0, cpu_possible_map);

	num_cpus = 1;
	for (i = 0; i < NR_CPUS; i++) {
		if (nlm_cpu_ready[i]) {
			cpu_set(i, phys_cpu_present_map);
			__cpu_number_map[i] = num_cpus;
			__cpu_logical_map[num_cpus] = i;
			cpu_set(num_cpus, cpu_possible_map);
			++num_cpus;
		}
	}

	pr_info("Phys CPU present map: %lx, possible map %lx\n",
		(unsigned long)phys_cpu_present_map.bits[0],
		(unsigned long)cpu_possible_map.bits[0]);

	pr_info("Detected %i Slave CPU(s)\n", num_cpus);
}

void nlm_prepare_cpus(unsigned int max_cpus)
{
}

struct plat_smp_ops nlm_smp_ops = {
	.send_ipi_single	= nlm_send_ipi_single,
	.send_ipi_mask		= nlm_send_ipi_mask,
	.init_secondary		= nlm_init_secondary,
	.smp_finish		= nlm_smp_finish,
	.cpus_done		= nlm_cpus_done,
	.boot_secondary		= nlm_boot_secondary,
	.smp_setup		= nlm_smp_setup,
	.prepare_cpus		= nlm_prepare_cpus,
};

unsigned long secondary_entry_point;

int nlm_wakeup_secondary_cpus(u32 wakeup_mask)
{
	unsigned int tid, pid, ipi, i, boot_cpu;
	void *reset_vec;

	secondary_entry_point = (unsigned long)prom_pre_boot_secondary_cpus;
	reset_vec = (void *)CKSEG1ADDR(0x1fc00000);
	memcpy(reset_vec, nlm_boot_smp_nmi, 0x80);
	boot_cpu = hard_smp_processor_id();

	for (i = 0; i < NR_CPUS; i++) {
		if (i == boot_cpu)
			continue;
		if (wakeup_mask & (1u << i)) {
			tid = i & 0x3;
			pid = (i >> 2) & 0x7;
			ipi = (tid << 16) | (pid << 20) | (1 << 8);
			pic_send_ipi(ipi);
		}
	}

	return 0;
}
