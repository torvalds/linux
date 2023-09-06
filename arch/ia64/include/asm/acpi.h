/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 1999 VA Linux Systems
 *  Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 *  Copyright (C) 2000,2001 J.I. Lee <jung-ik.lee@intel.com>
 *  Copyright (C) 2001,2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 */

#ifndef _ASM_ACPI_H
#define _ASM_ACPI_H

#ifdef __KERNEL__

#include <acpi/proc_cap_intel.h>

#include <linux/init.h>
#include <linux/numa.h>
#include <asm/numa.h>


extern int acpi_lapic;
#define acpi_disabled 0	/* ACPI always enabled on IA64 */
#define acpi_noirq 0	/* ACPI always enabled on IA64 */
#define acpi_pci_disabled 0 /* ACPI PCI always enabled on IA64 */
#define acpi_strict 1	/* no ACPI spec workarounds on IA64 */

static inline bool acpi_has_cpu_in_madt(void)
{
	return !!acpi_lapic;
}

#define acpi_processor_cstate_check(x) (x) /* no idle limits on IA64 :) */
static inline void disable_acpi(void) { }

int acpi_request_vector (u32 int_type);
int acpi_gsi_to_irq (u32 gsi, unsigned int *irq);

/* Low-level suspend routine. */
extern int acpi_suspend_lowlevel(void);

static inline unsigned long acpi_get_wakeup_address(void)
{
	return 0;
}

/*
 * Record the cpei override flag and current logical cpu. This is
 * useful for CPU removal.
 */
extern unsigned int can_cpei_retarget(void);
extern unsigned int is_cpu_cpei_target(unsigned int cpu);
extern void set_cpei_target_cpu(unsigned int cpu);
extern unsigned int get_cpei_target_cpu(void);
extern void prefill_possible_map(void);
#ifdef CONFIG_ACPI_HOTPLUG_CPU
extern int additional_cpus;
#else
#define additional_cpus 0
#endif

#ifdef CONFIG_ACPI_NUMA
#if MAX_NUMNODES > 256
#define MAX_PXM_DOMAINS MAX_NUMNODES
#else
#define MAX_PXM_DOMAINS (256)
#endif
extern int pxm_to_nid_map[MAX_PXM_DOMAINS];
extern int __initdata nid_to_pxm_map[MAX_NUMNODES];
#endif

static inline bool arch_has_acpi_pdc(void) { return true; }
static inline void arch_acpi_set_proc_cap_bits(u32 *cap)
{
	*cap |= ACPI_PROC_CAP_EST_CAPABILITY_SMP;
}

#ifdef CONFIG_ACPI_NUMA
extern cpumask_t early_cpu_possible_map;
#define for_each_possible_early_cpu(cpu)  \
	for_each_cpu((cpu), &early_cpu_possible_map)

static inline void per_cpu_scan_finalize(int min_cpus, int reserve_cpus)
{
	int low_cpu, high_cpu;
	int cpu;
	int next_nid = 0;

	low_cpu = cpumask_weight(&early_cpu_possible_map);

	high_cpu = max(low_cpu, min_cpus);
	high_cpu = min(high_cpu + reserve_cpus, NR_CPUS);

	for (cpu = low_cpu; cpu < high_cpu; cpu++) {
		cpumask_set_cpu(cpu, &early_cpu_possible_map);
		if (node_cpuid[cpu].nid == NUMA_NO_NODE) {
			node_cpuid[cpu].nid = next_nid;
			next_nid++;
			if (next_nid >= num_online_nodes())
				next_nid = 0;
		}
	}
}

extern void acpi_numa_fixup(void);

#endif /* CONFIG_ACPI_NUMA */

#endif /*__KERNEL__*/

#endif /*_ASM_ACPI_H*/
