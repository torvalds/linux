/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */
#ifndef __POWERPC_KVM_PARA_H__
#define __POWERPC_KVM_PARA_H__

#include <asm/firmware.h>

#include <uapi/asm/kvm_para.h>

static inline int kvm_para_available(void)
{
	return IS_ENABLED(CONFIG_KVM_GUEST) && is_kvm_guest();
}

static inline unsigned int kvm_arch_para_features(void)
{
	unsigned long r;

	if (!kvm_para_available())
		return 0;

	if(epapr_hypercall0_1(KVM_HCALL_TOKEN(KVM_HC_FEATURES), &r))
		return 0;

	return r;
}

static inline unsigned int kvm_arch_para_hints(void)
{
	return 0;
}

static inline bool kvm_check_and_clear_guest_paused(void)
{
	return false;
}

#endif /* __POWERPC_KVM_PARA_H__ */
