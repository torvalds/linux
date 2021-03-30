/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 - Google LLC
 * Author: Quentin Perret <qperret@google.com>
 */

#ifndef __ARM64_KVM_CPUFEATURE_H__
#define __ARM64_KVM_CPUFEATURE_H__

#include <asm/cpufeature.h>

#include <linux/build_bug.h>

#if defined(__KVM_NVHE_HYPERVISOR__)
#define DECLARE_KVM_HYP_CPU_FTR_REG(name) extern struct arm64_ftr_reg name
#define DEFINE_KVM_HYP_CPU_FTR_REG(name) struct arm64_ftr_reg name
#else
#define DECLARE_KVM_HYP_CPU_FTR_REG(name) extern struct arm64_ftr_reg kvm_nvhe_sym(name)
#define DEFINE_KVM_HYP_CPU_FTR_REG(name) BUILD_BUG()
#endif

DECLARE_KVM_HYP_CPU_FTR_REG(arm64_ftr_reg_ctrel0);
DECLARE_KVM_HYP_CPU_FTR_REG(arm64_ftr_reg_id_aa64mmfr0_el1);
DECLARE_KVM_HYP_CPU_FTR_REG(arm64_ftr_reg_id_aa64mmfr1_el1);

#endif
