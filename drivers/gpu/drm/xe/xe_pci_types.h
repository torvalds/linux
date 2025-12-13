/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_PCI_TYPES_H_
#define _XE_PCI_TYPES_H_

#include <linux/types.h>

#include "xe_platform_types.h"

struct xe_subplatform_desc {
	enum xe_subplatform subplatform;
	const char *name;
	const u16 *pciidlist;
};

struct xe_device_desc {
	/* Should only ever be set for platforms without GMD_ID */
	const struct xe_ip *pre_gmdid_graphics_ip;
	/* Should only ever be set for platforms without GMD_ID */
	const struct xe_ip *pre_gmdid_media_ip;

	const char *platform_name;
	const struct xe_subplatform_desc *subplatforms;

	enum xe_platform platform;

	u8 dma_mask_size;
	u8 max_remote_tiles:2;
	u8 max_gt_per_tile:2;

	u8 require_force_probe:1;
	u8 is_dgfx:1;

	u8 has_display:1;
	u8 has_fan_control:1;
	u8 has_gsc_nvm:1;
	u8 has_heci_gscfi:1;
	u8 has_heci_cscfi:1;
	u8 has_late_bind:1;
	u8 has_llc:1;
	u8 has_mbx_power_limits:1;
	u8 has_pxp:1;
	u8 has_sriov:1;
	u8 needs_scratch:1;
	u8 skip_guc_pc:1;
	u8 skip_mtcfg:1;
	u8 skip_pcode:1;
};

struct xe_graphics_desc {
	u8 va_bits;
	u8 vm_max_level;
	u8 vram_flags;

	u64 hw_engine_mask;	/* hardware engines provided by graphics IP */

	u8 has_asid:1;
	u8 has_atomic_enable_pte_bit:1;
	u8 has_flat_ccs:1;
	u8 has_indirect_ring_state:1;
	u8 has_range_tlb_inval:1;
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
