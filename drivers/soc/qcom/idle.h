/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (c) 2007-2009,2012-2014, 2018-2019, 2021 The Linux Foundation.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _ARCH_ARM_MACH_MSM_IDLE_H_
#define _ARCH_ARM_MACH_MSM_IDLE_H_

#define MAX_CPUS_PER_CLUSTER	4
#define MAX_NUM_CLUSTER	4

#ifndef __ASSEMBLY__
#if defined(CONFIG_CPU_V7) || defined(CONFIG_ARM64)
extern unsigned long msm_pm_boot_vector[MAX_NUM_CLUSTER * MAX_CPUS_PER_CLUSTER];
void msm_pm_boot_entry(void);
#else
static inline void msm_pm_boot_entry(void) {}
#endif
#endif
#endif
