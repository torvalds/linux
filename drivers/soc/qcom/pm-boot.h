/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (c) 2011-2014, 2018-2019, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _ARCH_ARM_MACH_MSM_PM_BOOT_H
#define _ARCH_ARM_MACH_MSM_PM_BOOT_H

void msm_pm_boot_config_before_pc(unsigned int cpu, unsigned long entry);
void msm_pm_boot_config_after_pc(unsigned int cpu);

#endif
