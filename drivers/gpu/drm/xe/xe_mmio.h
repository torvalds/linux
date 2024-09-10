/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021-2023 Intel Corporation
 */

#ifndef _XE_MMIO_H_
#define _XE_MMIO_H_

#include "xe_gt_types.h"

struct xe_device;
struct xe_reg;

int xe_mmio_init(struct xe_device *xe);
int xe_mmio_probe_tiles(struct xe_device *xe);

/*
 * Temporary transition helper for xe_gt -> xe_mmio conversion.  Allows
 * continued usage of xe_gt as a parameter to MMIO operations which now
 * take an xe_mmio structure instead.  Will be removed once the driver-wide
 * conversion is complete.
 */
#define __to_xe_mmio(ptr) \
	_Generic(ptr, \
		 const struct xe_gt *: (&((const struct xe_gt *)(ptr))->mmio), \
		 struct xe_gt *: (&((struct xe_gt *)(ptr))->mmio), \
		 const struct xe_mmio *: (ptr), \
		 struct xe_mmio *: (ptr))

u8 __xe_mmio_read8(struct xe_mmio *mmio, struct xe_reg reg);
#define xe_mmio_read8(p, reg) __xe_mmio_read8(__to_xe_mmio(p), reg)

u16 __xe_mmio_read16(struct xe_mmio *mmio, struct xe_reg reg);
#define xe_mmio_read16(p, reg) __xe_mmio_read16(__to_xe_mmio(p), reg)

void __xe_mmio_write32(struct xe_mmio *mmio, struct xe_reg reg, u32 val);
#define xe_mmio_write32(p, reg, val) __xe_mmio_write32(__to_xe_mmio(p), reg, val)

u32 __xe_mmio_read32(struct xe_mmio *mmio, struct xe_reg reg);
#define xe_mmio_read32(p, reg) __xe_mmio_read32(__to_xe_mmio(p), reg)

u32 __xe_mmio_rmw32(struct xe_mmio *mmio, struct xe_reg reg, u32 clr, u32 set);
#define xe_mmio_rmw32(p, reg, clr, set) __xe_mmio_rmw32(__to_xe_mmio(p), reg, clr, set)

int __xe_mmio_write32_and_verify(struct xe_mmio *mmio, struct xe_reg reg,
				 u32 val, u32 mask, u32 eval);
#define xe_mmio_write32_and_verify(p, reg, val, mask, eval) \
	__xe_mmio_write32_and_verify(__to_xe_mmio(p), reg, val, mask, eval)

bool __xe_mmio_in_range(const struct xe_mmio *mmio,
			const struct xe_mmio_range *range, struct xe_reg reg);
#define xe_mmio_in_range(p, range, reg) __xe_mmio_in_range(__to_xe_mmio(p), range, reg)

u64 __xe_mmio_read64_2x32(struct xe_mmio *mmio, struct xe_reg reg);
#define xe_mmio_read64_2x32(p, reg) __xe_mmio_read64_2x32(__to_xe_mmio(p), reg)

int __xe_mmio_wait32(struct xe_mmio *mmio, struct xe_reg reg, u32 mask, u32 val,
		     u32 timeout_us, u32 *out_val, bool atomic);
#define xe_mmio_wait32(p, reg, mask, val, timeout_us, out_val, atomic) \
	__xe_mmio_wait32(__to_xe_mmio(p), reg, mask, val, timeout_us, out_val, atomic)

int __xe_mmio_wait32_not(struct xe_mmio *mmio, struct xe_reg reg, u32 mask,
			 u32 val, u32 timeout_us, u32 *out_val, bool atomic);
#define xe_mmio_wait32_not(p, reg, mask, val, timeout_us, out_val, atomic) \
	__xe_mmio_wait32_not(__to_xe_mmio(p), reg, mask, val, timeout_us, out_val, atomic)

static inline u32 __xe_mmio_adjusted_addr(const struct xe_mmio *mmio, u32 addr)
{
	if (addr < mmio->adj_limit)
		addr += mmio->adj_offset;
	return addr;
}
#define xe_mmio_adjusted_addr(p, addr) __xe_mmio_adjusted_addr(__to_xe_mmio(p), addr)

static inline struct xe_mmio *xe_root_tile_mmio(struct xe_device *xe)
{
	return &xe->tiles[0].mmio;
}

#endif
