/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __GENERIC_PT_FMT_DEFS_BCM2712_H
#define __GENERIC_PT_FMT_DEFS_BCM2712_H

#include <linux/generic_pt/common.h>
#include <linux/types.h>

typedef u32 pt_bcm2712_entry_t;
typedef u64 pt_vaddr_t;
typedef u64 pt_oaddr_t;

struct bcm2712pt_write_attrs {
	pt_bcm2712_entry_t descriptor_bits;
	gfp_t gfp;
};
#define pt_write_attrs bcm2712pt_write_attrs

#endif
