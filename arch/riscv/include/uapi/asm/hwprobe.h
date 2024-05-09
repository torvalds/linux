/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2023 Rivos, Inc
 */

#ifndef _UAPI_ASM_HWPROBE_H
#define _UAPI_ASM_HWPROBE_H

#include <linux/types.h>

/*
 * Interface for probing hardware capabilities from userspace, see
 * Documentation/arch/riscv/hwprobe.rst for more information.
 */
struct riscv_hwprobe {
	__s64 key;
	__u64 value;
};

#define RISCV_HWPROBE_KEY_MVENDORID	0
#define RISCV_HWPROBE_KEY_MARCHID	1
#define RISCV_HWPROBE_KEY_MIMPID	2
#define RISCV_HWPROBE_KEY_BASE_BEHAVIOR	3
#define		RISCV_HWPROBE_BASE_BEHAVIOR_IMA	(1 << 0)
#define RISCV_HWPROBE_KEY_IMA_EXT_0	4
#define		RISCV_HWPROBE_IMA_FD		(1 << 0)
#define		RISCV_HWPROBE_IMA_C		(1 << 1)
#define		RISCV_HWPROBE_IMA_V		(1 << 2)
#define		RISCV_HWPROBE_EXT_ZBA		(1 << 3)
#define		RISCV_HWPROBE_EXT_ZBB		(1 << 4)
#define		RISCV_HWPROBE_EXT_ZBS		(1 << 5)
#define RISCV_HWPROBE_KEY_CPUPERF_0	5
#define		RISCV_HWPROBE_MISALIGNED_UNKNOWN	(0 << 0)
#define		RISCV_HWPROBE_MISALIGNED_EMULATED	(1 << 0)
#define		RISCV_HWPROBE_MISALIGNED_SLOW		(2 << 0)
#define		RISCV_HWPROBE_MISALIGNED_FAST		(3 << 0)
#define		RISCV_HWPROBE_MISALIGNED_UNSUPPORTED	(4 << 0)
#define		RISCV_HWPROBE_MISALIGNED_MASK		(7 << 0)
/* Increase RISCV_HWPROBE_MAX_KEY when adding items. */

#endif
