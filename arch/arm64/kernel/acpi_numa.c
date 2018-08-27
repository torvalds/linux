// SPDX-License-Identifier: GPL-2.0
/*
 * ACPI 5.1 based NUMA setup for ARM64
 * Lots of code was borrowed from arch/x86/mm/srat.c
 *
 * Copyright 2004 Andi Kleen, SuSE Labs.
 * Copyright (C) 2013-2016, Linaro Ltd.
 *		Author: Hanjun Guo <hanjun.guo@linaro.org>
 *
 * Reads the ACPI SRAT table to figure out what memory belongs to which CPUs.
 *
 * Called from acpi_numa_init while reading the SRAT and SLIT tables.
 * Assumes all memory regions belonging to a single proximity domain
 * are in one chunk. Holes between them will be included in the node.
 */

#define pr_fmt(fmt) "ACPI: NUMA: " fmt

#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <linux/bootmem.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/topology.h>

#include <asm/numa.h>

static int acpi_early_node_map[NR_CPUS] __initdata = { NUMA_NO_NODE };

int __init acpi_numa_get_nid(unsigned int cpu)
{
	return acpi_early_node_map[cpu];
}

static inline int get_cpu_for_acpi_id(u32 uid)
{
	int cpu;

	for (cpu = 0; cpu < nr_cpu_ids; cpu++)
		if (uid == get_acpi_id_for_cpu(cpu))
			return cpu;

	return -EINVAL;
}

static int __init acpi_parse_gicc_pxm(struct acpi_subtable_header *header,
				      const unsigned long end)
{
	struct acpi_srat_gicc_affinity *pa;
	int cpu, pxm, node;

	if (srat_disabled())
		return -EINVAL;

	pa = (struct acpi_srat_gicc_affinity *)header;
	if (!pa)
		return -EINVAL;

	if (!(pa->flags & ACPI_SRAT_GICC_ENABLED))
		return 0;

	pxm = pa->proximity_domain;
	node = pxm_to_node(pxm);

	/*
	 * If we can't map the UID to a logical cpu this
	 * means that the UID is not part of possible cpus
	 * so we do not need a NUMA mapping for it, skip
	 * the SRAT entry and keep parsing.
	 */
	cpu = get_cpu_for_acpi_id(pa->acpi_processor_uid);
	if (cpu < 0)
		return 0;

	acpi_early_node_map[cpu] = node;
	pr_info("SRAT: PXM %d -> MPIDR 0x%llx -> Node %d\n", pxm,
		cpu_logical_map(cpu), node);

	return 0;
}

void __init acpi_map_cpus_to_nodes(void)
{
	acpi_table_parse_entries(ACPI_SIG_SRAT, sizeof(struct acpi_table_srat),
					    ACPI_SRAT_TYPE_GICC_AFFINITY,
					    acpi_parse_gicc_pxm, 0);
}

/* Callback for Proximity Domain -> ACPI processor UID mapping */
void __init acpi_numa_gicc_affinity_init(struct acpi_srat_gicc_affinity *pa)
{
	int pxm, node;

	if (srat_disabled())
		return;

	if (pa->header.length < sizeof(struct acpi_srat_gicc_affinity)) {
		pr_err("SRAT: Invalid SRAT header length: %d\n",
			pa->header.length);
		bad_srat();
		return;
	}

	if (!(pa->flags & ACPI_SRAT_GICC_ENABLED))
		return;

	pxm = pa->proximity_domain;
	node = acpi_map_pxm_to_node(pxm);

	if (node == NUMA_NO_NODE || node >= MAX_NUMNODES) {
		pr_err("SRAT: Too many proximity domains %d\n", pxm);
		bad_srat();
		return;
	}

	node_set(node, numa_nodes_parsed);
}

int __init arm64_acpi_numa_init(void)
{
	int ret;

	ret = acpi_numa_init();
	if (ret) {
		pr_info("Failed to initialise from firmware\n");
		return ret;
	}

	return srat_disabled() ? -EINVAL : 0;
}
