/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES
 *
 */
#ifndef __GENERIC_PT_FMT_DEFS_RISCV_H
#define __GENERIC_PT_FMT_DEFS_RISCV_H

#include <linux/generic_pt/common.h>
#include <linux/types.h>

#ifdef PT_RISCV_32BIT
typedef u32 pt_riscv_entry_t;
#define riscvpt_write_attrs riscv32pt_write_attrs
#else
typedef u64 pt_riscv_entry_t;
#define riscvpt_write_attrs riscv64pt_write_attrs
#endif

typedef pt_riscv_entry_t pt_vaddr_t;
typedef u64 pt_oaddr_t;

struct riscvpt_write_attrs {
	pt_riscv_entry_t descriptor_bits;
	gfp_t gfp;
};
#define pt_write_attrs riscvpt_write_attrs

#endif
