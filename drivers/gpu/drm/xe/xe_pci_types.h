/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_PCI_TYPES_H_
#define _XE_PCI_TYPES_H_

#include <linux/types.h>

struct xe_graphics_desc {
	u8 va_bits;
	u8 vm_max_level;
	u8 vram_flags;

	u64 hw_engine_mask;	/* hardware engines provided by graphics IP */

	u8 has_asid:1;
	u8 has_atomic_enable_pte_bit:1;
	u8 has_flat_ccs:1;
	u8 has_indirect_ring_state:1;
	u8 has_range_tlb_invalidation:1;
	u8 has_usm:1;
	u8 has_64bit_timestamp:1;
};

struct xe_media_desc {
	u64 hw_engine_mask;	/* hardware engines provided by media IP */

	u8 has_indirect_ring_state:1;
};

struct xe_ip {
	unsigned int verx100;
	const char *name;
	const void *desc;
};

#endif
