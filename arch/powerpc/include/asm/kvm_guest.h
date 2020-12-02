/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 IBM Corporation
 */

#ifndef _ASM_POWERPC_KVM_GUEST_H_
#define _ASM_POWERPC_KVM_GUEST_H_

#if defined(CONFIG_PPC_PSERIES) || defined(CONFIG_KVM_GUEST)
bool check_kvm_guest(void);
#else
static inline bool check_kvm_guest(void) { return false; }
#endif

#endif /* _ASM_POWERPC_KVM_GUEST_H_ */
