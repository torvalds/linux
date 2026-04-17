/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc.
 */

#ifndef __KVM_RISCV_ISA_H
#define __KVM_RISCV_ISA_H

#include <linux/types.h>

unsigned long kvm_riscv_base2isa_ext(unsigned long base_ext);

int __kvm_riscv_isa_check_host(unsigned long ext, unsigned long *base_ext);
#define kvm_riscv_isa_check_host(ext)	\
	__kvm_riscv_isa_check_host(KVM_RISCV_ISA_EXT_##ext, NULL)

bool kvm_riscv_isa_enable_allowed(unsigned long ext);
bool kvm_riscv_isa_disable_allowed(unsigned long ext);

#endif
