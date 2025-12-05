/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2023-2024 Rivos, Inc
 */

#ifndef _ASM_HWPROBE_H
#define _ASM_HWPROBE_H

#include <uapi/asm/hwprobe.h>

#define RISCV_HWPROBE_MAX_KEY 14

static inline bool riscv_hwprobe_key_is_valid(__s64 key)
{
	return key >= 0 && key <= RISCV_HWPROBE_MAX_KEY;
}

static inline bool hwprobe_key_is_bitmask(__s64 key)
{
	switch (key) {
	case RISCV_HWPROBE_KEY_BASE_BEHAVIOR:
	case RISCV_HWPROBE_KEY_IMA_EXT_0:
	case RISCV_HWPROBE_KEY_CPUPERF_0:
	case RISCV_HWPROBE_KEY_VENDOR_EXT_THEAD_0:
	case RISCV_HWPROBE_KEY_VENDOR_EXT_MIPS_0:
	case RISCV_HWPROBE_KEY_VENDOR_EXT_SIFIVE_0:
		return true;
	}

	return false;
}

static inline bool riscv_hwprobe_pair_cmp(struct riscv_hwprobe *pair,
					  struct riscv_hwprobe *other_pair)
{
	if (pair->key != other_pair->key)
		return false;

	if (hwprobe_key_is_bitmask(pair->key))
		return (pair->value & other_pair->value) == other_pair->value;

	return pair->value == other_pair->value;
}

#ifdef CONFIG_MMU
void riscv_hwprobe_register_async_probe(void);
void riscv_hwprobe_complete_async_probe(void);
#else
static inline void riscv_hwprobe_register_async_probe(void) {}
static inline void riscv_hwprobe_complete_async_probe(void) {}
#endif
#endif
