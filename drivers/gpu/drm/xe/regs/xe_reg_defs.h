/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_REG_DEFS_H_
#define _XE_REG_DEFS_H_

#include "compat-i915-headers/i915_reg_defs.h"

/**
 * struct xe_reg - Register definition
 *
 * Register defintion to be used by the individual register. Although the same
 * definition is used for xe_reg and xe_reg_mcr, they use different internal
 * APIs for accesses.
 */
struct xe_reg {
	union {
		struct {
			/** @addr: address */
			u32 addr:28;
			/**
			 * @masked: register is "masked", with upper 16bits used
			 * to identify the bits that are updated on the lower
			 * bits
			 */
			u32 masked:1;
			/**
			 * @mcr: register is multicast/replicated in the
			 * hardware and needs special handling. Any register
			 * with this set should also use a type of xe_reg_mcr_t.
			 * It's only here so the few places that deal with MCR
			 * registers specially (xe_sr.c) and tests using the raw
			 * value can inspect it.
			 */
			u32 mcr:1;
			/**
			 * @ext: access MMIO extension space for current register.
			 */
			u32 ext:1;
		};
		/** @raw: Raw value with both address and options */
		u32 raw;
	};
};

/**
 * struct xe_reg_mcr - MCR register definition
 *
 * MCR register is the same as a regular register, but uses another type since
 * the internal API used for accessing them is different: it's never correct to
 * use regular MMIO access.
 */
struct xe_reg_mcr {
	/** @__reg: The register */
	struct xe_reg __reg;
};


/**
 * XE_REG_OPTION_MASKED - Register is "masked", with upper 16 bits marking the
 * written bits on the lower 16 bits.
 *
 * It only applies to registers explicitly marked in bspec with
 * "Access: Masked". Registers with this option can have write operations to
 * specific lower bits by setting the corresponding upper bits. Other bits will
 * not be affected. This allows register writes without needing a RMW cycle and
 * without caching in software the register value.
 *
 * Example: a write with value 0x00010001 will set bit 0 and all other bits
 * retain their previous values.
 *
 * To be used with XE_REG(). XE_REG_MCR() and XE_REG_INITIALIZER()
 */
#define XE_REG_OPTION_MASKED		.masked = 1

/**
 * XE_REG_INITIALIZER - Initializer for xe_reg_t.
 * @r_: Register offset
 * @...: Additional options like access mode. See struct xe_reg for available
 *       options.
 *
 * Register field is mandatory, and additional options may be passed as
 * arguments. Usually ``XE_REG()`` should be preferred since it creates an
 * object of the right type. However when initializing static const storage,
 * where a compound statement is not allowed, this can be used instead.
 */
#define XE_REG_INITIALIZER(r_, ...)    { .addr = r_, __VA_ARGS__ }


/**
 * XE_REG - Create a struct xe_reg from offset and additional flags
 * @r_: Register offset
 * @...: Additional options like access mode. See struct xe_reg for available
 *       options.
 */
#define XE_REG(r_, ...)		((const struct xe_reg)XE_REG_INITIALIZER(r_, ##__VA_ARGS__))

/**
 * XE_REG_EXT - Create a struct xe_reg from extension offset and additional
 * flags
 * @r_: Register extension offset
 * @...: Additional options like access mode. See struct xe_reg for available
 *       options.
 */
#define XE_REG_EXT(r_, ...)	\
	((const struct xe_reg)XE_REG_INITIALIZER(r_, ##__VA_ARGS__, .ext = 1))

/**
 * XE_REG_MCR - Create a struct xe_reg_mcr from offset and additional flags
 * @r_: Register offset
 * @...: Additional options like access mode. See struct xe_reg for available
 *       options.
 */
#define XE_REG_MCR(r_, ...)	((const struct xe_reg_mcr){					\
				 .__reg = XE_REG_INITIALIZER(r_,  ##__VA_ARGS__, .mcr = 1)	\
				 })

#endif
