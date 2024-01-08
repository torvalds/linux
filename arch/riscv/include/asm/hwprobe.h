/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2023 Rivos, Inc
 */

#ifndef _ASM_HWPROBE_H
#define _ASM_HWPROBE_H

#include <uapi/asm/hwprobe.h>

#define RISCV_HWPROBE_MAX_KEY 6

static inline bool riscv_hwprobe_key_is_valid(__s64 key)
{
	return key >= 0 && key <= RISCV_HWPROBE_MAX_KEY;
}

#endif
