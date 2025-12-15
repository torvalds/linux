/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 *
 */
#ifndef __GENERIC_PT_FMT_DEFS_X86_64_H
#define __GENERIC_PT_FMT_DEFS_X86_64_H

#include <linux/generic_pt/common.h>
#include <linux/types.h>

typedef u64 pt_vaddr_t;
typedef u64 pt_oaddr_t;

struct x86_64_pt_write_attrs {
	u64 descriptor_bits;
	gfp_t gfp;
};
#define pt_write_attrs x86_64_pt_write_attrs

#endif
