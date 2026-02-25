/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_REG_DEFS__
#define __I915_REG_DEFS__

#include <drm/intel/reg_bits.h>

/*
 * Given the first two numbers __a and __b of arbitrarily many evenly spaced
 * numbers, pick the 0-based __index'th value.
 *
 * Always prefer this over _PICK() if the numbers are evenly spaced.
 */
#define _PICK_EVEN(__index, __a, __b) ((__a) + (__index) * ((__b) - (__a)))

/*
 * Like _PICK_EVEN(), but supports 2 ranges of evenly spaced address offsets.
 * @__c_index corresponds to the index in which the second range starts to be
 * used. Using math interval notation, the first range is used for indexes [ 0,
 * @__c_index), while the second range is used for [ @__c_index, ... ). Example:
 *
 * #define _FOO_A			0xf000
 * #define _FOO_B			0xf004
 * #define _FOO_C			0xf008
 * #define _SUPER_FOO_A			0xa000
 * #define _SUPER_FOO_B			0xa100
 * #define FOO(x)			_MMIO(_PICK_EVEN_2RANGES(x, 3,		\
 *					      _FOO_A, _FOO_B,			\
 *					      _SUPER_FOO_A, _SUPER_FOO_B))
 *
 * This expands to:
 *	0: 0xf000,
 *	1: 0xf004,
 *	2: 0xf008,
 *	3: 0xa000,
 *	4: 0xa100,
 *	5: 0xa200,
 *	...
 */
#define _PICK_EVEN_2RANGES(__index, __c_index, __a, __b, __c, __d)		\
	(BUILD_BUG_ON_ZERO(!__is_constexpr(__c_index)) +			\
	 ((__index) < (__c_index) ? _PICK_EVEN(__index, __a, __b) :		\
				   _PICK_EVEN((__index) - (__c_index), __c, __d)))

/*
 * Given the arbitrary numbers in varargs, pick the 0-based __index'th number.
 *
 * Always prefer _PICK_EVEN() over this if the numbers are evenly spaced.
 */
#define _PICK(__index, ...) (((const u32 []){ __VA_ARGS__ })[__index])

typedef struct {
	u32 reg;
} i915_reg_t;

#define _MMIO(r) ((const i915_reg_t){ .reg = (r) })

typedef struct {
	u32 reg;
} i915_mcr_reg_t;

#define MCR_REG(offset)	((const i915_mcr_reg_t){ .reg = (offset) })

#define INVALID_MMIO_REG _MMIO(0)

/*
 * These macros can be used on either i915_reg_t or i915_mcr_reg_t since they're
 * simply operations on the register's offset and don't care about the MCR vs
 * non-MCR nature of the register.
 */
#define i915_mmio_reg_offset(r) \
	_Generic((r), i915_reg_t: (r).reg, i915_mcr_reg_t: (r).reg)
#define i915_mmio_reg_equal(a, b) (i915_mmio_reg_offset(a) == i915_mmio_reg_offset(b))
#define i915_mmio_reg_valid(r) (!i915_mmio_reg_equal(r, INVALID_MMIO_REG))

/* A triplet for IMR/IER/IIR registers. */
struct i915_irq_regs {
	i915_reg_t imr;
	i915_reg_t ier;
	i915_reg_t iir;
};

#define I915_IRQ_REGS(_imr, _ier, _iir) \
	((const struct i915_irq_regs){ .imr = (_imr), .ier = (_ier), .iir = (_iir) })

struct i915_error_regs {
	i915_reg_t emr;
	i915_reg_t eir;
};

#define I915_ERROR_REGS(_emr, _eir) \
	((const struct i915_error_regs){ .emr = (_emr), .eir = (_eir) })

#endif /* __I915_REG_DEFS__ */
