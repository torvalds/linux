/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM64_HYPERVISOR_H
#define _ASM_ARM64_HYPERVISOR_H

#include <asm/xen/hypervisor.h>

void kvm_init_hyp_services(void);
bool kvm_arm_hyp_service_available(u32 func_id);
void kvm_arm_init_hyp_services(void);
void kvm_init_memshare_services(void);
void kvm_init_ioremap_services(void);

#ifdef CONFIG_MEMORY_RELINQUISH
void kvm_init_memrelinquish_services(void);
#else
static inline void kvm_init_memrelinquish_services(void) {}
#endif

#endif
