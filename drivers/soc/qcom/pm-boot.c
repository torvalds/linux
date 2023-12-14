// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2014, 2016, 2018-2019, 2021 The Linux Foundation.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/cacheflush.h>
#include <asm/cputype.h>
#include <asm/smp_plat.h>
#include <linux/qcom_scm.h>
#include "pm-boot.h"
#include "idle.h"

#define CPU_INDEX(cluster, cpu) (cluster * MAX_CPUS_PER_CLUSTER + cpu)
#define SCM_FLAG_WARMBOOT_MC		0x04

static void (*msm_pm_boot_before_pc)(unsigned int cpu, unsigned long entry);
static void (*msm_pm_boot_after_pc)(unsigned int cpu);

static int msm_pm_tz_boot_init(void)
{
	void  (*warmboot_addr)(void) = msm_pm_boot_entry;

	return qcom_scm_set_warm_boot_addr_mc(warmboot_addr, ~0U, ~0U, ~0U,
								SCM_FLAG_WARMBOOT_MC);
}
static void msm_pm_write_boot_vector(unsigned int cpu, unsigned long address)
{
	uint32_t clust_id = MPIDR_AFFINITY_LEVEL(cpu_logical_map(cpu), 1);
	uint32_t cpu_id = MPIDR_AFFINITY_LEVEL(cpu_logical_map(cpu), 0);
	unsigned long *start_address;
	unsigned long *end_address;

	if (clust_id >= MAX_NUM_CLUSTER || cpu_id >= MAX_CPUS_PER_CLUSTER)
		WARN_ON(cpu);

	msm_pm_boot_vector[CPU_INDEX(clust_id, cpu_id)] = address;
	start_address = &msm_pm_boot_vector[CPU_INDEX(clust_id, cpu_id)];
	end_address = &msm_pm_boot_vector[CPU_INDEX(clust_id, cpu_id + 1)];
}

static void msm_pm_config_tz_before_pc(unsigned int cpu,
		unsigned long entry)
{
	msm_pm_write_boot_vector(cpu, entry);
}

void msm_pm_boot_config_before_pc(unsigned int cpu, unsigned long entry)
{
	if (msm_pm_boot_before_pc)
		msm_pm_boot_before_pc(cpu, entry);
}

void msm_pm_boot_config_after_pc(unsigned int cpu)
{
	if (msm_pm_boot_after_pc)
		msm_pm_boot_after_pc(cpu);
}

static int __init msm_pm_boot_init(void)
{
	int ret = 0;

	ret = msm_pm_tz_boot_init();
	msm_pm_boot_before_pc = msm_pm_config_tz_before_pc;
	msm_pm_boot_after_pc = NULL;

	return ret;
}
late_initcall(msm_pm_boot_init);
