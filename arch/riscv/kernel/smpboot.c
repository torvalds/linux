// SPDX-License-Identifier: GPL-2.0-only
/*
 * SMP initialisation and IPI support
 * Based on arch/arm64/kernel/smp.c
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2015 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#include <linux/acpi.h>
#include <linux/arch_topology.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/mm.h>

#include <asm/cacheflush.h>
#include <asm/cpu_ops.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/numa.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/smp.h>
#include <uapi/asm/hwcap.h>
#include <asm/vector.h>

#include "head.h"

static DECLARE_COMPLETION(cpu_running);

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	int cpuid;
	unsigned int curr_cpuid;

	init_cpu_topology();

	curr_cpuid = smp_processor_id();
	store_cpu_topology(curr_cpuid);
	numa_store_cpu_info(curr_cpuid);
	numa_add_cpu(curr_cpuid);

	/* This covers non-smp usecase mandated by "nosmp" option */
	if (max_cpus == 0)
		return;

	for_each_possible_cpu(cpuid) {
		if (cpuid == curr_cpuid)
			continue;
		set_cpu_present(cpuid, true);
		numa_store_cpu_info(cpuid);
	}
}

#ifdef CONFIG_ACPI
static unsigned int cpu_count = 1;

static int __init acpi_parse_rintc(union acpi_subtable_headers *header, const unsigned long end)
{
	unsigned long hart;
	static bool found_boot_cpu;
	struct acpi_madt_rintc *processor = (struct acpi_madt_rintc *)header;

	/*
	 * Each RINTC structure in MADT will have a flag. If ACPI_MADT_ENABLED
	 * bit in the flag is not enabled, it means OS should not try to enable
	 * the cpu to which RINTC belongs.
	 */
	if (!(processor->flags & ACPI_MADT_ENABLED))
		return 0;

	if (BAD_MADT_ENTRY(processor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(&header->common);

	hart = processor->hart_id;
	if (hart == INVALID_HARTID) {
		pr_warn("Invalid hartid\n");
		return 0;
	}

	if (hart == cpuid_to_hartid_map(0)) {
		BUG_ON(found_boot_cpu);
		found_boot_cpu = true;
		early_map_cpu_to_node(0, acpi_numa_get_nid(cpu_count));
		return 0;
	}

	if (cpu_count >= NR_CPUS) {
		pr_warn("NR_CPUS is too small for the number of ACPI tables.\n");
		return 0;
	}

	cpuid_to_hartid_map(cpu_count) = hart;
	early_map_cpu_to_node(cpu_count, acpi_numa_get_nid(cpu_count));
	cpu_count++;

	return 0;
}

static void __init acpi_parse_and_init_cpus(void)
{
	acpi_table_parse_madt(ACPI_MADT_TYPE_RINTC, acpi_parse_rintc, 0);
}
#else
#define acpi_parse_and_init_cpus(...)	do { } while (0)
#endif

static void __init of_parse_and_init_cpus(void)
{
	struct device_node *dn;
	unsigned long hart;
	bool found_boot_cpu = false;
	int cpuid = 1;
	int rc;

	for_each_of_cpu_node(dn) {
		rc = riscv_early_of_processor_hartid(dn, &hart);
		if (rc < 0)
			continue;

		if (hart == cpuid_to_hartid_map(0)) {
			BUG_ON(found_boot_cpu);
			found_boot_cpu = 1;
			early_map_cpu_to_node(0, of_node_to_nid(dn));
			continue;
		}
		if (cpuid >= NR_CPUS) {
			pr_warn("Invalid cpuid [%d] for hartid [%lu]\n",
				cpuid, hart);
			continue;
		}

		cpuid_to_hartid_map(cpuid) = hart;
		early_map_cpu_to_node(cpuid, of_node_to_nid(dn));
		cpuid++;
	}

	BUG_ON(!found_boot_cpu);

	if (cpuid > nr_cpu_ids)
		pr_warn("Total number of cpus [%d] is greater than nr_cpus option value [%d]\n",
			cpuid, nr_cpu_ids);
}

void __init setup_smp(void)
{
	int cpuid;

	cpu_set_ops();

	if (acpi_disabled)
		of_parse_and_init_cpus();
	else
		acpi_parse_and_init_cpus();

	for (cpuid = 1; cpuid < nr_cpu_ids; cpuid++)
		if (cpuid_to_hartid_map(cpuid) != INVALID_HARTID)
			set_cpu_possible(cpuid, true);
}

static int start_secondary_cpu(int cpu, struct task_struct *tidle)
{
	if (cpu_ops->cpu_start)
		return cpu_ops->cpu_start(cpu, tidle);

	return -EOPNOTSUPP;
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	int ret = 0;
	tidle->thread_info.cpu = cpu;

	ret = start_secondary_cpu(cpu, tidle);
	if (!ret) {
		wait_for_completion_timeout(&cpu_running,
					    msecs_to_jiffies(1000));

		if (!cpu_online(cpu)) {
			pr_crit("CPU%u: failed to come online\n", cpu);
			ret = -EIO;
		}
	} else {
		pr_crit("CPU%u: failed to start\n", cpu);
	}

	return ret;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
}

/*
 * C entry point for a secondary processor.
 */
asmlinkage __visible void smp_callin(void)
{
	struct mm_struct *mm = &init_mm;
	unsigned int curr_cpuid = smp_processor_id();

	/* All kernel threads share the same mm context.  */
	mmgrab(mm);
	current->active_mm = mm;

	store_cpu_topology(curr_cpuid);
	notify_cpu_starting(curr_cpuid);

	riscv_ipi_enable();

	numa_add_cpu(curr_cpuid);
	set_cpu_online(curr_cpuid, 1);

	if (has_vector()) {
		if (riscv_v_setup_vsize())
			elf_hwcap &= ~COMPAT_HWCAP_ISA_V;
	}

	riscv_user_isa_enable();

	/*
	 * Remote cache and TLB flushes are ignored while the CPU is offline,
	 * so flush them both right now just in case.
	 */
	local_flush_icache_all();
	local_flush_tlb_all();
	complete(&cpu_running);
	/*
	 * Disable preemption before enabling interrupts, so we don't try to
	 * schedule a CPU that hasn't actually started yet.
	 */
	local_irq_enable();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}
