/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2023 Rivos, Inc
 */

#ifndef _UAPI_ASM_HWPROBE_H
#define _UAPI_ASM_HWPROBE_H

#include <linux/types.h>

/*
 * Interface for probing hardware capabilities from userspace, see
 * Documentation/riscv/hwprobe.rst for more information.
 */
struct riscv_hwprobe {
	__s64 key;
	__u64 value;
};

#define RISCV_HWPROBE_KEY_MVENDORID	0
#define RISCV_HWPROBE_KEY_MARCHID	1
#define RISCV_HWPROBE_KEY_MIMPID	2
/* Increase RISCV_HWPROBE_MAX_KEY when adding items. */

#endif
