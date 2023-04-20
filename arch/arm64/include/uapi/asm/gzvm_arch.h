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

#endif /* __GZVM_ARCH_H__ */
