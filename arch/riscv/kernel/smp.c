/*
 * SMP initialisation and IPI support
 * Based on arch/arm64/kernel/smp.c
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2015 Regents of the University of California
 * Copyright (C) 2017 SiFive
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/delay.h>

#include <asm/sbi.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

enum ipi_message_type {
	IPI_RESCHEDULE,
	IPI_CALL_FUNC,
	IPI_CPU_STOP,
	IPI_MAX
};

unsigned long __cpuid_to_hartid_map[NR_CPUS] = {
	[0 ... NR_CPUS-1] = INVALID_HARTID
};

void __init smp_setup_processor_id(void)
{
	cpuid_to_hartid_map(0) = boot_cpu_hartid;
}

/* A collection of single bit ipi messages.  */
static struct {
	unsigned long stats[IPI_MAX] ____cacheline_aligned;
	unsigned long bits ____cacheline_aligned;
} ipi_data[NR_CPUS] __cacheline_aligned;

int riscv_hartid_to_cpuid(int hartid)
{
	int i;

	for (i = 0; i < NR_CPUS; i++)
		if (cpuid_to_hartid_map(i) == hartid)
			return i;

	pr_err("Couldn't find cpu id for hartid [%d]\n", hartid);
	return i;
}

void riscv_cpuid_to_hartid_mask(const struct cpumask *in, struct cpumask *out)
{
	int cpu;

	for_each_cpu(cpu, in)
		cpumask_set_cpu(cpuid_to_hartid_map(cpu), out);
}

bool arch_match_cpu_phys_id(int cpu, u64 phys_id)
{
	return phys_id == cpuid_to_hartid_map(cpu);
}

/* Unsupported */
int setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}

static void ipi_stop(void)
{
	set_cpu_online(smp_processor_id(), false);
	while (1)
		wait_for_interrupt();
}

void riscv_software_interrupt(void)
{
	unsigned long *pending_ipis = &ipi_data[smp_processor_id()].bits;
	unsigned long *stats = ipi_data[smp_processor_id()].stats;

	/* Clear pending IPI */
	csr_clear(CSR_SIP, SIE_SSIE);

	while (true) {
		unsigned long ops;

		/* Order bit clearing and data access. */
		mb();

		ops = xchg(pending_ipis, 0);
		if (ops == 0)
			return;

		if (ops & (1 << IPI_RESCHEDULE)) {
			stats[IPI_RESCHEDULE]++;
			scheduler_ipi();
		}

		if (ops & (1 << IPI_CALL_FUNC)) {
			stats[IPI_CALL_FUNC]++;
			generic_smp_call_function_interrupt();
		}

		if (ops & (1 << IPI_CPU_STOP)) {
			stats[IPI_CPU_STOP]++;
			ipi_stop();
		}

		BUG_ON((ops >> IPI_MAX) != 0);

		/* Order data access and bit testing. */
		mb();
	}
}

static void
send_ipi_message(const struct cpumask *to_whom, enum ipi_message_type operation)
{
	int cpuid, hartid;
	struct cpumask hartid_mask;

	cpumask_clear(&hartid_mask);
	mb();
	for_each_cpu(cpuid, to_whom) {
		set_bit(operation, &ipi_data[cpuid].bits);
		hartid = cpuid_to_hartid_map(cpuid);
		cpumask_set_cpu(hartid, &hartid_mask);
	}
	mb();
	sbi_send_ipi(cpumask_bits(&hartid_mask));
}

static const char * const ipi_names[] = {
	[IPI_RESCHEDULE]	= "Rescheduling interrupts",
	[IPI_CALL_FUNC]		= "Function call interrupts",
	[IPI_CPU_STOP]		= "CPU stop interrupts",
};

void show_ipi_stats(struct seq_file *p, int prec)
{
	unsigned int cpu, i;

	for (i = 0; i < IPI_MAX; i++) {
		seq_printf(p, "%*s%u:%s", prec - 1, "IPI", i,
			   prec >= 4 ? " " : "");
		for_each_online_cpu(cpu)
			seq_printf(p, "%10lu ", ipi_data[cpu].stats[i]);
		seq_printf(p, " %s\n", ipi_names[i]);
	}
}

void arch_send_call_function_ipi_mask(struct cpumask *mask)
{
	send_ipi_message(mask, IPI_CALL_FUNC);
}

void arch_send_call_function_single_ipi(int cpu)
{
	send_ipi_message(cpumask_of(cpu), IPI_CALL_FUNC);
}

void smp_send_stop(void)
{
	unsigned long timeout;

	if (num_online_cpus() > 1) {
		cpumask_t mask;

		cpumask_copy(&mask, cpu_online_mask);
		cpumask_clear_cpu(smp_processor_id(), &mask);

		if (system_state <= SYSTEM_RUNNING)
			pr_crit("SMP: stopping secondary CPUs\n");
		send_ipi_message(&mask, IPI_CPU_STOP);
	}

	/* Wait up to one second for other CPUs to stop */
	timeout = USEC_PER_SEC;
	while (num_online_cpus() > 1 && timeout--)
		udelay(1);

	if (num_online_cpus() > 1)
		pr_warn("SMP: failed to stop secondary CPUs %*pbl\n",
			   cpumask_pr_args(cpu_online_mask));
}

void smp_send_reschedule(int cpu)
{
	send_ipi_message(cpumask_of(cpu), IPI_RESCHEDULE);
}

