/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_REG_DEFS__
#define __I915_REG_DEFS__

#include <drm/intel/pick.h>
#include <drm/intel/reg_bits.h>

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
