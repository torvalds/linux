/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 - Arm Ltd */

#ifndef __ARM_KVM_RAS_H__
#define __ARM_KVM_RAS_H__

#include <linux/types.h>

static inline int kvm_handle_guest_sea(phys_addr_t addr, unsigned int esr)
{
	return -1;
}

#endif /* __ARM_KVM_RAS_H__ */
