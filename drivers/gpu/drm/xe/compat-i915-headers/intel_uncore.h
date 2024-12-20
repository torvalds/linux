/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_UNCORE_H__
#define __INTEL_UNCORE_H__

#include "xe_device.h"
#include "xe_device_types.h"
#include "xe_mmio.h"

static inline struct xe_mmio *__compat_uncore_to_mmio(struct intel_uncore *uncore)
{
	struct xe_device *xe = container_of(uncore, struct xe_device, uncore);

	return xe_root_tile_mmio(xe);
}

static inline struct xe_tile *__compat_uncore_to_tile(struct intel_uncore *uncore)
{
	struct xe_device *xe = container_of(uncore, struct xe_device, uncore);

	return xe_device_get_root_tile(xe);
}

static inline u32 intel_uncore_read(struct intel_uncore *uncore,
				    i915_reg_t i915_reg)
{
	struct xe_reg reg = XE_REG(i915_mmio_reg_offset(i915_reg));

	return xe_mmio_read32(__compat_uncore_to_mmio(uncore), reg);
}

static inline u8 intel_uncore_read8(struct intel_uncore *uncore,
				    i915_reg_t i915_reg)
{
	struct xe_reg reg = XE_REG(i915_mmio_reg_offset(i915_reg));

	return xe_mmio_read8(__compat_uncore_to_mmio(uncore), reg);
}

static inline u16 intel_uncore_read16(struct intel_uncore *uncore,
				      i915_reg_t i915_reg)
{
	struct xe_reg reg = XE_REG(i915_mmio_reg_offset(i915_reg));

	return xe_mmio_read16(__compat_uncore_to_mmio(uncore), reg);
}

static inline u64
intel_uncore_read64_2x32(struct intel_uncore *uncore,
			 i915_reg_t i915_lower_reg, i915_reg_t i915_upper_reg)
{
	struct xe_reg lower_reg = XE_REG(i915_mmio_reg_offset(i915_lower_reg));
	struct xe_reg upper_reg = XE_REG(i915_mmio_reg_offset(i915_upper_reg));
	u32 upper, lower, old_upper;
	int loop = 0;

	upper = xe_mmio_read32(__compat_uncore_to_mmio(uncore), upper_reg);
	do {
		old_upper = upper;
		lower = xe_mmio_read32(__compat_uncore_to_mmio(uncore), lower_reg);
		upper = xe_mmio_read32(__compat_uncore_to_mmio(uncore), upper_reg);
	} while (upper != old_upper && loop++ < 2);

	return (u64)upper << 32 | lower;
}

static inline void intel_uncore_posting_read(struct intel_uncore *uncore,
					     i915_reg_t i915_reg)
{
	struct xe_reg reg = XE_REG(i915_mmio_reg_offset(i915_reg));

	xe_mmio_read32(__compat_uncore_to_mmio(uncore), reg);
}

static inline void intel_uncore_write(struct intel_uncore *uncore,
				      i915_reg_t i915_reg, u32 val)
{
	struct xe_reg reg = XE_REG(i915_mmio_reg_offset(i915_reg));

	xe_mmio_write32(__compat_uncore_to_mmio(uncore), reg, val);
}

static inline u32 intel_uncore_rmw(struct intel_uncore *uncore,
				   i915_reg_t i915_reg, u32 clear, u32 set)
{
	struct xe_reg reg = XE_REG(i915_mmio_reg_offset(i915_reg));

	return xe_mmio_rmw32(__compat_uncore_to_mmio(uncore), reg, clear, set);
}

static inline int intel_wait_for_register(struct intel_uncore *uncore,
					  i915_reg_t i915_reg, u32 mask,
					  u32 value, unsigned int timeout)
{
	struct xe_reg reg = XE_REG(i915_mmio_reg_offset(i915_reg));

	return xe_mmio_wait32(__compat_uncore_to_mmio(uncore), reg, mask, value,
			      timeout * USEC_PER_MSEC, NULL, false);
}

static inline int intel_wait_for_register_fw(struct intel_uncore *uncore,
					     i915_reg_t i915_reg, u32 mask,
					     u32 value, unsigned int timeout)
{
	struct xe_reg reg = XE_REG(i915_mmio_reg_offset(i915_reg));

	return xe_mmio_wait32(__compat_uncore_to_mmio(uncore), reg, mask, value,
			      timeout * USEC_PER_MSEC, NULL, false);
}

static inline int
__intel_wait_for_register(struct intel_uncore *uncore, i915_reg_t i915_reg,
			  u32 mask, u32 value, unsigned int fast_timeout_us,
			  unsigned int slow_timeout_ms, u32 *out_value)
{
	struct xe_reg reg = XE_REG(i915_mmio_reg_offset(i915_reg));

	return xe_mmio_wait32(__compat_uncore_to_mmio(uncore), reg, mask, value,
			      fast_timeout_us + 1000 * slow_timeout_ms,
			      out_value, false);
}

static inline u32 intel_uncore_read_fw(struct intel_uncore *uncore,
				       i915_reg_t i915_reg)
{
	struct xe_reg reg = XE_REG(i915_mmio_reg_offset(i915_reg));

	return xe_mmio_read32(__compat_uncore_to_mmio(uncore), reg);
}

static inline void intel_uncore_write_fw(struct intel_uncore *uncore,
					 i915_reg_t i915_reg, u32 val)
{
	struct xe_reg reg = XE_REG(i915_mmio_reg_offset(i915_reg));

	xe_mmio_write32(__compat_uncore_to_mmio(uncore), reg, val);
}

static inline u32 intel_uncore_read_notrace(struct intel_uncore *uncore,
					    i915_reg_t i915_reg)
{
	struct xe_reg reg = XE_REG(i915_mmio_reg_offset(i915_reg));

	return xe_mmio_read32(__compat_uncore_to_mmio(uncore), reg);
}

static inline void intel_uncore_write_notrace(struct intel_uncore *uncore,
					      i915_reg_t i915_reg, u32 val)
{
	struct xe_reg reg = XE_REG(i915_mmio_reg_offset(i915_reg));

	xe_mmio_write32(__compat_uncore_to_mmio(uncore), reg, val);
}

#define intel_uncore_forcewake_get(x, y) do { } while (0)
#define intel_uncore_forcewake_put(x, y) do { } while (0)

#define intel_uncore_arm_unclaimed_mmio_detection(x) do { } while (0)

#endif /* __INTEL_UNCORE_H__ */
