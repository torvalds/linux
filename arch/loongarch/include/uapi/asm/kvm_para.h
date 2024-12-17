/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_KVM_PARA_H
#define _UAPI_ASM_KVM_PARA_H

#include <linux/types.h>

/*
 * CPUCFG index area: 0x40000000 -- 0x400000ff
 * SW emulation for KVM hypervirsor
 */
#define CPUCFG_KVM_BASE			0x40000000
#define CPUCFG_KVM_SIZE			0x100
#define CPUCFG_KVM_SIG			(CPUCFG_KVM_BASE + 0)
#define  KVM_SIGNATURE			"KVM\0"
#define CPUCFG_KVM_FEATURE		(CPUCFG_KVM_BASE + 4)
#define  KVM_FEATURE_IPI		1
#define  KVM_FEATURE_STEAL_TIME		2
/* BIT 24 - 31 are features configurable by user space vmm */
#define  KVM_FEATURE_VIRT_EXTIOI	24

#endif /* _UAPI_ASM_KVM_PARA_H */
