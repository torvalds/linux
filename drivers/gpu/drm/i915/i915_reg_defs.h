/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_REG_DEFS__
#define __I915_REG_DEFS__

#include <linux/bitfield.h>
#include <linux/bits.h>

/**
 * REG_BIT() - Prepare a u32 bit value
 * @__n: 0-based bit number
 *
 * Local wrapper for BIT() to force u32, with compile time checks.
 *
 * @return: Value with bit @__n set.
 */
#define REG_BIT(__n)							\
	((u32)(BIT(__n) +						\
	       BUILD_BUG_ON_ZERO(__is_constexpr(__n) &&		\
				 ((__n) < 0 || (__n) > 31))))

/**
 * REG_BIT8() - Prepare a u8 bit value
 * @__n: 0-based bit number
 *
 * Local wrapper for BIT() to force u8, with compile time checks.
 *
 * @return: Value with bit @__n set.
 */
#define REG_BIT8(__n)                                                   \
	((u8)(BIT(__n) +                                                \
	       BUILD_BUG_ON_ZERO(__is_constexpr(__n) &&         \
				 ((__n) < 0 || (__n) > 7))))

/**
 * REG_GENMASK() - Prepare a continuous u32 bitmask
 * @__high: 0-based high bit
 * @__low: 0-based low bit
 *
 * Local wrapper for GENMASK() to force u32, with compile time checks.
 *
 * @return: Continuous bitmask from @__high to @__low, inclusive.
 */
#define REG_GENMASK(__high, __low)					\
	((u32)(GENMASK(__high, __low) +					\
	       BUILD_BUG_ON_ZERO(__is_constexpr(__high) &&	\
				 __is_constexpr(__low) &&		\
				 ((__low) < 0 || (__high) > 31 || (__low) > (__high)))))

/**
 * REG_GENMASK64() - Prepare a continuous u64 bitmask
 * @__high: 0-based high bit
 * @__low: 0-based low bit
 *
 * Local wrapper for GENMASK_ULL() to force u64, with compile time checks.
 *
 * @return: Continuous bitmask from @__high to @__low, inclusive.
 */
#define REG_GENMASK64(__high, __low)					\
	((u64)(GENMASK_ULL(__high, __low) +				\
	       BUILD_BUG_ON_ZERO(__is_constexpr(__high) &&		\
				 __is_constexpr(__low) &&		\
				 ((__low) < 0 || (__high) > 63 || (__low) > (__high)))))

/**
 * REG_GENMASK8() - Prepare a continuous u8 bitmask
 * @__high: 0-based high bit
 * @__low: 0-based low bit
 *
 * Local wrapper for GENMASK() to force u8, with compile time checks.
 *
 * @return: Continuous bitmask from @__high to @__low, inclusive.
 */
#define REG_GENMASK8(__high, __low)                                     \
	((u8)(GENMASK(__high, __low) +                                  \
	       BUILD_BUG_ON_ZERO(__is_constexpr(__high) &&      \
				 __is_constexpr(__low) &&               \
				 ((__low) < 0 || (__high) > 7 || (__low) > (__high)))))

/*
 * Local integer constant expression version of is_power_of_2().
 */
#define IS_POWER_OF_2(__x)		((__x) && (((__x) & ((__x) - 1)) == 0))

/**
 * REG_FIELD_PREP() - Prepare a u32 bitfield value
 * @__mask: shifted mask defining the field's length and position
 * @__val: value to put in the field
 *
 * Local copy of FIELD_PREP() to generate an integer constant expression, force
 * u32 and for consistency with REG_FIELD_GET(), REG_BIT() and REG_GENMASK().
 *
 * @return: @__val masked and shifted into the field defined by @__mask.
 */
#define REG_FIELD_PREP(__mask, __val)						\
	((u32)((((typeof(__mask))(__val) << __bf_shf(__mask)) & (__mask)) +	\
	       BUILD_BUG_ON_ZERO(!__is_constexpr(__mask)) +		\
	       BUILD_BUG_ON_ZERO((__mask) == 0 || (__mask) > U32_MAX) +		\
	       BUILD_BUG_ON_ZERO(!IS_POWER_OF_2((__mask) + (1ULL << __bf_shf(__mask)))) + \
	       BUILD_BUG_ON_ZERO(__builtin_choose_expr(__is_constexpr(__val), (~((__mask) >> __bf_shf(__mask)) & (__val)), 0))))

/**
 * REG_FIELD_PREP8() - Prepare a u8 bitfield value
 * @__mask: shifted mask defining the field's length and position
 * @__val: value to put in the field
 *
 * Local copy of FIELD_PREP() to generate an integer constant expression, force
 * u8 and for consistency with REG_FIELD_GET8(), REG_BIT8() and REG_GENMASK8().
 *
 * @return: @__val masked and shifted into the field defined by @__mask.
 */
#define REG_FIELD_PREP8(__mask, __val)                                          \
	((u8)((((typeof(__mask))(__val) << __bf_shf(__mask)) & (__mask)) +      \
	       BUILD_BUG_ON_ZERO(!__is_constexpr(__mask)) +             \
	       BUILD_BUG_ON_ZERO((__mask) == 0 || (__mask) > U8_MAX) +          \
	       BUILD_BUG_ON_ZERO(!IS_POWER_OF_2((__mask) + (1ULL << __bf_shf(__mask)))) + \
	       BUILD_BUG_ON_ZERO(__builtin_choose_expr(__is_constexpr(__val), (~((__mask) >> __bf_shf(__mask)) & (__val)), 0))))

/**
 * REG_FIELD_GET() - Extract a u32 bitfield value
 * @__mask: shifted mask defining the field's length and position
 * @__val: value to extract the bitfield value from
 *
 * Local wrapper for FIELD_GET() to force u32 and for consistency with
 * REG_FIELD_PREP(), REG_BIT() and REG_GENMASK().
 *
 * @return: Masked and shifted value of the field defined by @__mask in @__val.
 */
#define REG_FIELD_GET(__mask, __val)	((u32)FIELD_GET(__mask, __val))

/**
 * REG_FIELD_GET64() - Extract a u64 bitfield value
 * @__mask: shifted mask defining the field's length and position
 * @__val: value to extract the bitfield value from
 *
 * Local wrapper for FIELD_GET() to force u64 and for consistency with
 * REG_GENMASK64().
 *
 * @return: Masked and shifted value of the field defined by @__mask in @__val.
 */
#define REG_FIELD_GET64(__mask, __val)	((u64)FIELD_GET(__mask, __val))

/**
 * REG_BIT16() - Prepare a u16 bit value
 * @__n: 0-based bit number
 *
 * Local wrapper for BIT() to force u16, with compile time
 * checks.
 *
 * @return: Value with bit @__n set.
 */
#define REG_BIT16(__n)                                                   \
	((u16)(BIT(__n) +                                                \
	       BUILD_BUG_ON_ZERO(__is_constexpr(__n) &&         \
				 ((__n) < 0 || (__n) > 15))))

/**
 * REG_GENMASK16() - Prepare a continuous u8 bitmask
 * @__high: 0-based high bit
 * @__low: 0-based low bit
 *
 * Local wrapper for GENMASK() to force u16, with compile time
 * checks.
 *
 * @return: Continuous bitmask from @__high to @__low, inclusive.
 */
#define REG_GENMASK16(__high, __low)                                     \
	((u16)(GENMASK(__high, __low) +                                  \
	       BUILD_BUG_ON_ZERO(__is_constexpr(__high) &&      \
				 __is_constexpr(__low) &&               \
				 ((__low) < 0 || (__high) > 15 || (__low) > (__high)))))

/**
 * REG_FIELD_PREP16() - Prepare a u16 bitfield value
 * @__mask: shifted mask defining the field's length and position
 * @__val: value to put in the field
 *
 * Local copy of FIELD_PREP16() to generate an integer constant
 * expression, force u8 and for consistency with
 * REG_FIELD_GET16(), REG_BIT16() and REG_GENMASK16().
 *
 * @return: @__val masked and shifted into the field defined by @__mask.
 */
#define REG_FIELD_PREP16(__mask, __val)                                          \
	((u16)((((typeof(__mask))(__val) << __bf_shf(__mask)) & (__mask)) +      \
	       BUILD_BUG_ON_ZERO(!__is_constexpr(__mask)) +             \
	       BUILD_BUG_ON_ZERO((__mask) == 0 || (__mask) > U16_MAX) +          \
	       BUILD_BUG_ON_ZERO(!IS_POWER_OF_2((__mask) + (1ULL << __bf_shf(__mask)))) + \
	       BUILD_BUG_ON_ZERO(__builtin_choose_expr(__is_constexpr(__val), (~((__mask) >> __bf_shf(__mask)) & (__val)), 0))))

#define __MASKED_FIELD(mask, value) ((mask) << 16 | (value))
#define _MASKED_FIELD(mask, value) ({					   \
	if (__builtin_constant_p(mask))					   \
		BUILD_BUG_ON_MSG(((mask) & 0xffff0000), "Incorrect mask"); \
	if (__builtin_constant_p(value))				   \
		BUILD_BUG_ON_MSG((value) & 0xffff0000, "Incorrect value"); \
	if (__builtin_constant_p(mask) && __builtin_constant_p(value))	   \
		BUILD_BUG_ON_MSG((value) & ~(mask),			   \
				 "Incorrect value for mask");		   \
	__MASKED_FIELD(mask, value); })
#define _MASKED_BIT_ENABLE(a)	({ typeof(a) _a = (a); _MASKED_FIELD(_a, _a); })
#define _MASKED_BIT_DISABLE(a)	(_MASKED_FIELD((a), 0))

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

/**
 * REG_FIELD_GET8() - Extract a u8 bitfield value
 * @__mask: shifted mask defining the field's length and position
 * @__val: value to extract the bitfield value from
 *
 * Local wrapper for FIELD_GET() to force u8 and for consistency with
 * REG_FIELD_PREP(), REG_BIT() and REG_GENMASK().
 *
 * @return: Masked and shifted value of the field defined by @__mask in @__val.
 */
#define REG_FIELD_GET8(__mask, __val)   ((u8)FIELD_GET(__mask, __val))

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

#endif /* __I915_REG_DEFS__ */
