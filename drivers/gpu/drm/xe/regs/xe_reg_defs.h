/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_REG_DEFS_H_
#define _XE_REG_DEFS_H_

#include "../../i915/i915_reg_defs.h"

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
			/** @reg: address */
			u32 reg:22;
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
 * read/written bits on the lower 16 bits.
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
#define XE_REG_INITIALIZER(r_, ...)    { .reg = r_, __VA_ARGS__ }


/**
 * XE_REG - Create a struct xe_reg from offset and additional flags
 * @r_: Register offset
 * @...: Additional options like access mode. See struct xe_reg for available
 *       options.
 */
#define XE_REG(r_, ...)		((const struct xe_reg)XE_REG_INITIALIZER(r_, ##__VA_ARGS__))

/**
 * XE_REG_MCR - Create a struct xe_reg_mcr from offset and additional flags
 * @r_: Register offset
 * @...: Additional options like access mode. See struct xe_reg for available
 *       options.
 */
#define XE_REG_MCR(r_, ...)	((const struct xe_reg_mcr){					\
				 .__reg = XE_REG_INITIALIZER(r_,  ##__VA_ARGS__, .mcr = 1)	\
				 })

/*
 * TODO: remove these once the register declarations are not using them anymore
 */
#undef _MMIO
#undef MCR_REG
#define _MMIO(r_)	((const struct xe_reg){ .reg = r_ })
#define MCR_REG(r_)	((const struct xe_reg_mcr){ .__reg.reg = r_,		\
						    .__reg.mcr = 1 })

#endif
