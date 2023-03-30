/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_REG_SR_TYPES_
#define _XE_REG_SR_TYPES_

#include <linux/xarray.h>
#include <linux/types.h>

#include "i915_reg_defs.h"

struct xe_reg_sr_entry {
	u32		clr_bits;
	u32		set_bits;
	/* Mask for bits to consider when reading value back */
	u32		read_mask;
	/*
	 * "Masked registers" are marked in spec as register with the upper 16
	 * bits as a mask for the bits that is being updated on the lower 16
	 * bits when writing to it.
	 */
	u8		masked_reg;
	u8		reg_type;
};

struct xe_reg_sr_kv {
	u32			k;
	struct xe_reg_sr_entry	v;
};

struct xe_reg_sr {
	struct {
		struct xe_reg_sr_entry *arr;
		unsigned int used;
		unsigned int allocated;
		unsigned int grow_step;
	} pool;
	struct xarray xa;
	const char *name;
};

#endif
