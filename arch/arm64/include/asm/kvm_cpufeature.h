/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 - Google LLC
 * Author: Quentin Perret <qperret@google.com>
 */

#include <asm/cpufeature.h>

#ifndef KVM_HYP_CPU_FTR_REG
#if defined(__KVM_NVHE_HYPERVISOR__)
#define KVM_HYP_CPU_FTR_REG(name) extern struct arm64_ftr_reg name
#else
#define KVM_HYP_CPU_FTR_REG(name) extern struct arm64_ftr_reg kvm_nvhe_sym(name)
#endif
#endif

KVM_HYP_CPU_FTR_REG(arm64_ftr_reg_ctrel0);
KVM_HYP_CPU_FTR_REG(arm64_ftr_reg_id_aa64mmfr0_el1);
KVM_HYP_CPU_FTR_REG(arm64_ftr_reg_id_aa64mmfr1_el1);
