/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 - Arm Ltd */

#ifndef __ARM64_KVM_RAS_H__
#define __ARM64_KVM_RAS_H__

#include <linux/types.h>

int kvm_handle_guest_sea(phys_addr_t addr, unsigned int esr);

#endif /* __ARM64_KVM_RAS_H__ */
