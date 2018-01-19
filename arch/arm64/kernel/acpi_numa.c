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

#include <acpi/processor.h>
#include <asm/numa.h>

static int cpus_in_srat;

struct __node_cpu_hwid {
	u32 node_id;    /* logical node containing this CPU */
	u64 cpu_hwid;   /* MPIDR for this CPU */
};

static struct __node_cpu_hwid early_node_cpu_hwid[NR_CPUS] = {
[0 ... NR_CPUS - 1] = {NUMA_NO_NODE, PHYS_CPUID_INVALID} };

int acpi_numa_get_nid(unsigned int cpu, u64 hwid)
{
	int i;

	for (i = 0; i < cpus_in_srat; i++) {
		if (hwid == early_node_cpu_hwid[i].cpu_hwid)
			return early_node_cpu_hwid[i].node_id;
	}

	return NUMA_NO_NODE;
}

/* Callback for Proximity Domain -> ACPI processor UID mapping */
void __init acpi_numa_gicc_affinity_init(struct acpi_srat_gicc_affinity *pa)
{
	int pxm, node;
	phys_cpuid_t mpidr;

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

	if (cpus_in_srat >= NR_CPUS) {
		pr_warn_once("SRAT: cpu_to_node_map[%d] is too small, may not be able to use all cpus\n",
			     NR_CPUS);
		return;
	}

	pxm = pa->proximity_domain;
	node = acpi_map_pxm_to_node(pxm);

	if (node == NUMA_NO_NODE || node >= MAX_NUMNODES) {
		pr_err("SRAT: Too many proximity domains %d\n", pxm);
		bad_srat();
		return;
	}

	mpidr = acpi_map_madt_entry(pa->acpi_processor_uid);
	if (mpidr == PHYS_CPUID_INVALID) {
		pr_err("SRAT: PXM %d with ACPI ID %d has no valid MPIDR in MADT\n",
			pxm, pa->acpi_processor_uid);
		bad_srat();
		return;
	}

	early_node_cpu_hwid[cpus_in_srat].node_id = node;
	early_node_cpu_hwid[cpus_in_srat].cpu_hwid =  mpidr;
	node_set(node, numa_nodes_parsed);
	cpus_in_srat++;
	pr_info("SRAT: PXM %d -> MPIDR 0x%Lx -> Node %d\n",
		pxm, mpidr, node);
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
