// SPDX-License-Identifier: GPL-2.0-only
/*
 * Mapping of TPMI power domains CPU mapping
 *
 * Copyright (c) 2024, Intel Corporation.
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/hashtable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/topology.h>
#include <linux/types.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/msr.h>

#include "tpmi_power_domains.h"

#define MSR_PM_LOGICAL_ID       0x54

/*
 * Struct of MSR 0x54
 * [15:11] PM_DOMAIN_ID
 * [10:3] MODULE_ID (aka IDI_AGENT_ID)
 * [2:0] LP_ID
 * For Atom:
 *   [2] Always 0
 *   [1:0] core ID within module
 * For Core
 *   [2:1] Always 0
 *   [0] thread ID
 */

#define LP_ID_MASK		GENMASK_ULL(2, 0)
#define MODULE_ID_MASK		GENMASK_ULL(10, 3)
#define PM_DOMAIN_ID_MASK	GENMASK_ULL(15, 11)

/**
 * struct tpmi_cpu_info - Mapping information for a CPU
 * @hnode: Used to add mapping information to hash list
 * @linux_cpu:	Linux CPU number
 * @pkg_id: Package ID of this CPU
 * @punit_thread_id: Punit thread id of this CPU
 * @punit_core_id: Punit core id
 * @punit_domain_id: Power domain id from Punit
 *
 * Structure to store mapping information for a Linux CPU
 * to a Punit core, thread and power domain.
 */
struct tpmi_cpu_info {
	struct hlist_node hnode;
	int linux_cpu;
	u8 pkg_id;
	u8 punit_thread_id;
	u8 punit_core_id;
	u8 punit_domain_id;
};

static DEFINE_PER_CPU(struct tpmi_cpu_info, tpmi_cpu_info);

/* The dynamically assigned cpu hotplug state to free later */
static enum cpuhp_state tpmi_hp_state __read_mostly;

#define MAX_POWER_DOMAINS	8

static cpumask_t *tpmi_power_domain_mask;

/* Lock to protect tpmi_power_domain_mask and tpmi_cpu_hash */
static DEFINE_MUTEX(tpmi_lock);

static const struct x86_cpu_id tpmi_cpu_ids[] = {
	X86_MATCH_VFM(INTEL_GRANITERAPIDS_X,	NULL),
	X86_MATCH_VFM(INTEL_ATOM_CRESTMONT_X,	NULL),
	X86_MATCH_VFM(INTEL_ATOM_CRESTMONT,	NULL),
	X86_MATCH_VFM(INTEL_ATOM_DARKMONT_X,	NULL),
	X86_MATCH_VFM(INTEL_GRANITERAPIDS_D,	NULL),
	X86_MATCH_VFM(INTEL_PANTHERCOVE_X,	NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, tpmi_cpu_ids);

static DECLARE_HASHTABLE(tpmi_cpu_hash, 8);

static bool tpmi_domain_is_valid(struct tpmi_cpu_info *info)
{
	return info->pkg_id < topology_max_packages() &&
		info->punit_domain_id < MAX_POWER_DOMAINS;
}

int tpmi_get_linux_cpu_number(int package_id, int domain_id, int punit_core_id)
{
	struct tpmi_cpu_info *info;
	int ret = -EINVAL;

	guard(mutex)(&tpmi_lock);
	hash_for_each_possible(tpmi_cpu_hash, info, hnode, punit_core_id) {
		if (info->punit_domain_id == domain_id && info->pkg_id == package_id) {
			ret = info->linux_cpu;
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_NS_GPL(tpmi_get_linux_cpu_number, INTEL_TPMI_POWER_DOMAIN);

int tpmi_get_punit_core_number(int cpu_no)
{
	if (cpu_no >= num_possible_cpus())
		return -EINVAL;

	return per_cpu(tpmi_cpu_info, cpu_no).punit_core_id;
}
EXPORT_SYMBOL_NS_GPL(tpmi_get_punit_core_number, INTEL_TPMI_POWER_DOMAIN);

int tpmi_get_power_domain_id(int cpu_no)
{
	if (cpu_no >= num_possible_cpus())
		return -EINVAL;

	return per_cpu(tpmi_cpu_info, cpu_no).punit_domain_id;
}
EXPORT_SYMBOL_NS_GPL(tpmi_get_power_domain_id, INTEL_TPMI_POWER_DOMAIN);

cpumask_t *tpmi_get_power_domain_mask(int cpu_no)
{
	struct tpmi_cpu_info *info;
	cpumask_t *mask;
	int index;

	if (cpu_no >= num_possible_cpus())
		return NULL;

	info = &per_cpu(tpmi_cpu_info, cpu_no);
	if (!tpmi_domain_is_valid(info))
		return NULL;

	index = info->pkg_id * MAX_POWER_DOMAINS + info->punit_domain_id;
	guard(mutex)(&tpmi_lock);
	mask = &tpmi_power_domain_mask[index];

	return mask;
}
EXPORT_SYMBOL_NS_GPL(tpmi_get_power_domain_mask, INTEL_TPMI_POWER_DOMAIN);

static int tpmi_get_logical_id(unsigned int cpu, struct tpmi_cpu_info *info)
{
	u64 data;
	int ret;

	ret = rdmsrl_safe(MSR_PM_LOGICAL_ID, &data);
	if (ret)
		return ret;

	info->punit_domain_id = FIELD_GET(PM_DOMAIN_ID_MASK, data);
	if (info->punit_domain_id >= MAX_POWER_DOMAINS)
		return -EINVAL;

	info->punit_thread_id = FIELD_GET(LP_ID_MASK, data);
	info->punit_core_id = FIELD_GET(MODULE_ID_MASK, data);
	info->pkg_id = topology_physical_package_id(cpu);
	info->linux_cpu = cpu;

	return 0;
}

static int tpmi_cpu_online(unsigned int cpu)
{
	struct tpmi_cpu_info *info = &per_cpu(tpmi_cpu_info, cpu);
	int ret, index;

	/* Don't fail CPU online for some bad mapping of CPUs */
	ret = tpmi_get_logical_id(cpu, info);
	if (ret)
		return 0;

	index = info->pkg_id * MAX_POWER_DOMAINS + info->punit_domain_id;

	guard(mutex)(&tpmi_lock);
	cpumask_set_cpu(cpu, &tpmi_power_domain_mask[index]);
	hash_add(tpmi_cpu_hash, &info->hnode, info->punit_core_id);

	return 0;
}

static int __init tpmi_init(void)
{
	const struct x86_cpu_id *id;
	u64 data;
	int ret;

	id = x86_match_cpu(tpmi_cpu_ids);
	if (!id)
		return -ENODEV;

	/* Check for MSR 0x54 presence */
	ret = rdmsrl_safe(MSR_PM_LOGICAL_ID, &data);
	if (ret)
		return ret;

	tpmi_power_domain_mask = kcalloc(size_mul(topology_max_packages(), MAX_POWER_DOMAINS),
					 sizeof(*tpmi_power_domain_mask), GFP_KERNEL);
	if (!tpmi_power_domain_mask)
		return -ENOMEM;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"platform/x86/tpmi_power_domains:online",
				tpmi_cpu_online, NULL);
	if (ret < 0) {
		kfree(tpmi_power_domain_mask);
		return ret;
	}

	tpmi_hp_state = ret;

	return 0;
}
module_init(tpmi_init)

static void __exit tpmi_exit(void)
{
	cpuhp_remove_state(tpmi_hp_state);
	kfree(tpmi_power_domain_mask);
}
module_exit(tpmi_exit)

MODULE_DESCRIPTION("TPMI Power Domains Mapping");
MODULE_LICENSE("GPL");
