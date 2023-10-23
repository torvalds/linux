/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#ifndef __GZVM_ARCH_H__
#define __GZVM_ARCH_H__

#include <linux/types.h>

#define GZVM_CAP_ARM_VM_IPA_SIZE	165
#define GZVM_CAP_ARM_PROTECTED_VM	0xffbadab1

/* sub-commands put in args[0] for GZVM_CAP_ARM_PROTECTED_VM */
#define GZVM_CAP_ARM_PVM_SET_PVMFW_IPA		0
#define GZVM_CAP_ARM_PVM_GET_PVMFW_SIZE		1
/* GZVM_CAP_ARM_PVM_SET_PROTECTED_VM only sets protected but not load pvmfw */
#define GZVM_CAP_ARM_PVM_SET_PROTECTED_VM	2

/*
 * Architecture specific registers are to be defined in arch headers and
 * ORed with the arch identifier.
 */
#define GZVM_REG_ARM		0x4000000000000000ULL
#define GZVM_REG_ARM64		0x6000000000000000ULL

#define GZVM_REG_SIZE_SHIFT	52
#define GZVM_REG_SIZE_MASK	0x00f0000000000000ULL
#define GZVM_REG_SIZE_U8	0x0000000000000000ULL
#define GZVM_REG_SIZE_U16	0x0010000000000000ULL
#define GZVM_REG_SIZE_U32	0x0020000000000000ULL
#define GZVM_REG_SIZE_U64	0x0030000000000000ULL
#define GZVM_REG_SIZE_U128	0x0040000000000000ULL
#define GZVM_REG_SIZE_U256	0x0050000000000000ULL
#define GZVM_REG_SIZE_U512	0x0060000000000000ULL
#define GZVM_REG_SIZE_U1024	0x0070000000000000ULL
#define GZVM_REG_SIZE_U2048	0x0080000000000000ULL

#define GZVM_REG_ARCH_MASK	0xff00000000000000ULL

/* If you need to interpret the index values, here is the key: */
#define GZVM_REG_ARM_COPROC_MASK	0x000000000FFF0000
#define GZVM_REG_ARM_COPROC_SHIFT	16

/* Normal registers are mapped as coprocessor 16. */
#define GZVM_REG_ARM_CORE		(0x0010 << GZVM_REG_ARM_COPROC_SHIFT)
#define GZVM_REG_ARM_CORE_REG(name)	\
	(offsetof(struct gzvm_regs, name) / sizeof(__u32))

#define GZVM_VGIC_NR_SGIS		16
#define GZVM_VGIC_NR_PPIS		16
#define GZVM_VGIC_NR_PRIVATE_IRQS	(GZVM_VGIC_NR_SGIS + GZVM_VGIC_NR_PPIS)

#endif /* __GZVM_ARCH_H__ */
