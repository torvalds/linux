/* SPDX-License-Identifier: GPL-2.0 */

#ifndef ASM_KVM_HOST_TYPES_H
#define ASM_KVM_HOST_TYPES_H

#ifdef KVM_S390_ARM64
#include <asm/kvm_host_arm64_types.h>
#else
#include <asm/kvm_host_s390_types.h>
#endif /* KVM_S390_ARM64 */

#endif /* ASM_KVM_HOST_TYPES_H */
