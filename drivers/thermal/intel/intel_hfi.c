// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hardware Feedback Interface Driver
 *
 * Copyright (c) 2021, Intel Corporation.
 *
 * Authors: Aubrey Li <aubrey.li@linux.intel.com>
 *          Ricardo Neri <ricardo.neri-calderon@linux.intel.com>
 *
 *
 * The Hardware Feedback Interface provides a performance and energy efficiency
 * capability information for each CPU in the system. Depending on the processor
 * model, hardware may periodically update these capabilities as a result of
 * changes in the operating conditions (e.g., power limits or thermal
 * constraints). On other processor models, there is a single HFI update
 * at boot.
 *
 * This file provides functionality to process HFI updates and relay these
 * updates to userspace.
 */

#define pr_fmt(fmt)  "intel-hfi: " fmt

#include <linux/bitops.h>
#include <linux/cpufeature.h>
#include <linux/cpumask.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/math.h>
#include <linux/mutex.h>
#include <linux/percpu-defs.h>
#include <linux/printk.h>
#include <linux/processor.h>
#include <linux/slab.h>
#include <linux/topology.h>

#include <asm/msr.h>

#include "intel_hfi.h"

/* Hardware Feedback Interface MSR configuration bits */
#define HW_FEEDBACK_PTR_VALID_BIT		BIT(0)

/* CPUID detection and enumeration definitions for HFI */

#define CPUID_HFI_LEAF 6

union hfi_capabilities {
	struct {
		u8	performance:1;
		u8	energy_efficiency:1;
		u8	__reserved:6;
	} split;
	u8 bits;
};

union cpuid6_edx {
	struct {
		union hfi_capabilities	capabilities;
		u32			table_pages:4;
		u32			__reserved:4;
		s32			index:16;
	} split;
	u32 full;
};

/**
 * struct hfi_cpu_data - HFI capabilities per CPU
 * @perf_cap:		Performance capability
 * @ee_cap:		Energy efficiency capability
 *
 * Capabilities of a logical processor in the HFI table. These capabilities are
 * unitless.
 */
struct hfi_cpu_data {
	u8	perf_cap;
	u8	ee_cap;
} __packed;

/**
 * struct hfi_hdr - Header of the HFI table
 * @perf_updated:	Hardware updated performance capabilities
 * @ee_updated:		Hardware updated energy efficiency capabilities
 *
 * Properties of the data in an HFI table.
 */
struct hfi_hdr {
	u8	perf_updated;
	u8	ee_updated;
} __packed;

/**
 * struct hfi_instance - Representation of an HFI instance (i.e., a table)
 * @local_table:	Base of the local copy of the HFI table
 * @timestamp:		Timestamp of the last update of the local table.
 *			Located at the base of the local table.
 * @hdr:		Base address of the header of the local table
 * @data:		Base address of the data of the local table
 * @cpus:		CPUs represented in this HFI table instance
 * @hw_table:		Pointer to the HFI table of this instance
 *
 * A set of parameters to parse and navigate a specific HFI table.
 */
struct hfi_instance {
	union {
		void			*local_table;
		u64			*timestamp;
	};
	void			*hdr;
	void			*data;
	cpumask_var_t		cpus;
	void			*hw_table;
};

/**
 * struct hfi_features - Supported HFI features
 * @nr_table_pages:	Size of the HFI table in 4KB pages
 * @cpu_stride:		Stride size to locate the capability data of a logical
 *			processor within the table (i.e., row stride)
 * @hdr_size:		Size of the table header
 *
 * Parameters and supported features that are common to all HFI instances
 */
struct hfi_features {
	unsigned int	nr_table_pages;
	unsigned int	cpu_stride;
	unsigned int	hdr_size;
};

/**
 * struct hfi_cpu_info - Per-CPU attributes to consume HFI data
 * @index:		Row of this CPU in its HFI table
 * @hfi_instance:	Attributes of the HFI table to which this CPU belongs
 *
 * Parameters to link a logical processor to an HFI table and a row within it.
 */
struct hfi_cpu_info {
	s16			index;
	struct hfi_instance	*hfi_instance;
};

static DEFINE_PER_CPU(struct hfi_cpu_info, hfi_cpu_info) = { .index = -1 };

static int max_hfi_instances;
static struct hfi_instance *hfi_instances;

static struct hfi_features hfi_features;
static DEFINE_MUTEX(hfi_instance_lock);

static void init_hfi_cpu_index(struct hfi_cpu_info *info)
{
	union cpuid6_edx edx;

	/* Do not re-read @cpu's index if it has already been initialized. */
	if (info->index > -1)
		return;

	edx.full = cpuid_edx(CPUID_HFI_LEAF);
	info->index = edx.split.index;
}

/*
 * The format of the HFI table depends on the number of capabilities that the
 * hardware supports. Keep a data structure to navigate the table.
 */
static void init_hfi_instance(struct hfi_instance *hfi_instance)
{
	/* The HFI header is below the time-stamp. */
	hfi_instance->hdr = hfi_instance->local_table +
			    sizeof(*hfi_instance->timestamp);

	/* The HFI data starts below the header. */
	hfi_instance->data = hfi_instance->hdr + hfi_features.hdr_size;
}

/**
 * intel_hfi_online() - Enable HFI on @cpu
 * @cpu:	CPU in which the HFI will be enabled
 *
 * Enable the HFI to be used in @cpu. The HFI is enabled at the die/package
 * level. The first CPU in the die/package to come online does the full HFI
 * initialization. Subsequent CPUs will just link themselves to the HFI
 * instance of their die/package.
 *
 * This function is called before enabling the thermal vector in the local APIC
 * in order to ensure that @cpu has an associated HFI instance when it receives
 * an HFI event.
 */
void intel_hfi_online(unsigned int cpu)
{
	struct hfi_instance *hfi_instance;
	struct hfi_cpu_info *info;
	phys_addr_t hw_table_pa;
	u64 msr_val;
	u16 die_id;

	/* Nothing to do if hfi_instances are missing. */
	if (!hfi_instances)
		return;

	/*
	 * Link @cpu to the HFI instance of its package/die. It does not
	 * matter whether the instance has been initialized.
	 */
	info = &per_cpu(hfi_cpu_info, cpu);
	die_id = topology_logical_die_id(cpu);
	hfi_instance = info->hfi_instance;
	if (!hfi_instance) {
		if (die_id < 0 || die_id >= max_hfi_instances)
			return;

		hfi_instance = &hfi_instances[die_id];
		info->hfi_instance = hfi_instance;
	}

	init_hfi_cpu_index(info);

	/*
	 * Now check if the HFI instance of the package/die of @cpu has been
	 * initialized (by checking its header). In such case, all we have to
	 * do is to add @cpu to this instance's cpumask.
	 */
	mutex_lock(&hfi_instance_lock);
	if (hfi_instance->hdr) {
		cpumask_set_cpu(cpu, hfi_instance->cpus);
		goto unlock;
	}

	/*
	 * Hardware is programmed with the physical address of the first page
	 * frame of the table. Hence, the allocated memory must be page-aligned.
	 */
	hfi_instance->hw_table = alloc_pages_exact(hfi_features.nr_table_pages,
						   GFP_KERNEL | __GFP_ZERO);
	if (!hfi_instance->hw_table)
		goto unlock;

	hw_table_pa = virt_to_phys(hfi_instance->hw_table);

	/*
	 * Allocate memory to keep a local copy of the table that
	 * hardware generates.
	 */
	hfi_instance->local_table = kzalloc(hfi_features.nr_table_pages << PAGE_SHIFT,
					    GFP_KERNEL);
	if (!hfi_instance->local_table)
		goto free_hw_table;

	/*
	 * Program the address of the feedback table of this die/package. On
	 * some processors, hardware remembers the old address of the HFI table
	 * even after having been reprogrammed and re-enabled. Thus, do not free
	 * the pages allocated for the table or reprogram the hardware with a
	 * new base address. Namely, program the hardware only once.
	 */
	msr_val = hw_table_pa | HW_FEEDBACK_PTR_VALID_BIT;
	wrmsrl(MSR_IA32_HW_FEEDBACK_PTR, msr_val);

	init_hfi_instance(hfi_instance);

	cpumask_set_cpu(cpu, hfi_instance->cpus);

unlock:
	mutex_unlock(&hfi_instance_lock);
	return;

free_hw_table:
	free_pages_exact(hfi_instance->hw_table, hfi_features.nr_table_pages);
	goto unlock;
}

/**
 * intel_hfi_offline() - Disable HFI on @cpu
 * @cpu:	CPU in which the HFI will be disabled
 *
 * Remove @cpu from those covered by its HFI instance.
 *
 * On some processors, hardware remembers previous programming settings even
 * after being reprogrammed. Thus, keep HFI enabled even if all CPUs in the
 * die/package of @cpu are offline. See note in intel_hfi_online().
 */
void intel_hfi_offline(unsigned int cpu)
{
	struct hfi_cpu_info *info = &per_cpu(hfi_cpu_info, cpu);
	struct hfi_instance *hfi_instance;

	/*
	 * Check if @cpu as an associated, initialized (i.e., with a non-NULL
	 * header). Also, HFI instances are only initialized if X86_FEATURE_HFI
	 * is present.
	 */
	hfi_instance = info->hfi_instance;
	if (!hfi_instance)
		return;

	if (!hfi_instance->hdr)
		return;

	mutex_lock(&hfi_instance_lock);
	cpumask_clear_cpu(cpu, hfi_instance->cpus);
	mutex_unlock(&hfi_instance_lock);
}

static __init int hfi_parse_features(void)
{
	unsigned int nr_capabilities;
	union cpuid6_edx edx;

	if (!boot_cpu_has(X86_FEATURE_HFI))
		return -ENODEV;

	/*
	 * If we are here we know that CPUID_HFI_LEAF exists. Parse the
	 * supported capabilities and the size of the HFI table.
	 */
	edx.full = cpuid_edx(CPUID_HFI_LEAF);

	if (!edx.split.capabilities.split.performance) {
		pr_debug("Performance reporting not supported! Not using HFI\n");
		return -ENODEV;
	}

	/*
	 * The number of supported capabilities determines the number of
	 * columns in the HFI table. Exclude the reserved bits.
	 */
	edx.split.capabilities.split.__reserved = 0;
	nr_capabilities = hweight8(edx.split.capabilities.bits);

	/* The number of 4KB pages required by the table */
	hfi_features.nr_table_pages = edx.split.table_pages + 1;

	/*
	 * The header contains change indications for each supported feature.
	 * The size of the table header is rounded up to be a multiple of 8
	 * bytes.
	 */
	hfi_features.hdr_size = DIV_ROUND_UP(nr_capabilities, 8) * 8;

	/*
	 * Data of each logical processor is also rounded up to be a multiple
	 * of 8 bytes.
	 */
	hfi_features.cpu_stride = DIV_ROUND_UP(nr_capabilities, 8) * 8;

	return 0;
}

void __init intel_hfi_init(void)
{
	struct hfi_instance *hfi_instance;
	int i, j;

	if (hfi_parse_features())
		return;

	/* There is one HFI instance per die/package. */
	max_hfi_instances = topology_max_packages() *
			    topology_max_die_per_package();

	/*
	 * This allocation may fail. CPU hotplug callbacks must check
	 * for a null pointer.
	 */
	hfi_instances = kcalloc(max_hfi_instances, sizeof(*hfi_instances),
				GFP_KERNEL);
	if (!hfi_instances)
		return;

	for (i = 0; i < max_hfi_instances; i++) {
		hfi_instance = &hfi_instances[i];
		if (!zalloc_cpumask_var(&hfi_instance->cpus, GFP_KERNEL))
			goto err_nomem;
	}

	return;

err_nomem:
	for (j = 0; j < i; ++j) {
		hfi_instance = &hfi_instances[j];
		free_cpumask_var(hfi_instance->cpus);
	}

	kfree(hfi_instances);
	hfi_instances = NULL;
}
