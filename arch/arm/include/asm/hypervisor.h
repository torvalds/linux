/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_HYPERVISOR_H
#define _ASM_ARM_HYPERVISOR_H

#include <asm/xen/hypervisor.h>

void kvm_init_hyp_services(void);
bool kvm_arm_hyp_service_available(u32 func_id);

#endif
