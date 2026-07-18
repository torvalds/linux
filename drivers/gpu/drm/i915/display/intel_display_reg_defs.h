/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_REG_DEFS_H__
#define __INTEL_DISPLAY_REG_DEFS_H__

#include "i915_reg_defs.h"

typedef i915_reg_t intel_reg_t;

static inline u32 intel_reg_offset(intel_reg_t r)
{
	return r.reg;
}

static inline bool intel_reg_equal(intel_reg_t a, intel_reg_t b)
{
	return intel_reg_offset(a) == intel_reg_offset(b);
}

static inline bool intel_reg_valid(intel_reg_t r)
{
	return !intel_reg_equal(r, INVALID_MMIO_REG);
}

/* A triplet for IMR/IER/IIR registers. */
struct intel_irq_regs {
	intel_reg_t imr;
	intel_reg_t ier;
	intel_reg_t iir;
};

#define INTEL_IRQ_REGS(_imr, _ier, _iir) \
	((const struct intel_irq_regs){ .imr = (_imr), .ier = (_ier), .iir = (_iir) })

struct intel_error_regs {
	intel_reg_t emr;
	intel_reg_t eir;
};

#define INTEL_ERROR_REGS(_emr, _eir) \
	((const struct intel_error_regs){ .emr = (_emr), .eir = (_eir) })

#define VLV_DISPLAY_BASE		0x180000

/*
 * Named helper wrappers around _PICK_EVEN() and _PICK_EVEN_2RANGES().
 */
#define _PIPE(pipe, a, b)		_PICK_EVEN(pipe, a, b)
#define _PLANE(plane, a, b)		_PICK_EVEN(plane, a, b)
#define _TRANS(tran, a, b)		_PICK_EVEN(tran, a, b)
#define _PORT(port, a, b)		_PICK_EVEN(port, a, b)
#define _PLL(pll, a, b)			_PICK_EVEN(pll, a, b)
#define _PHY(phy, a, b)			_PICK_EVEN(phy, a, b)

#define _MMIO_PIPE(pipe, a, b)		_MMIO(_PIPE(pipe, a, b))
#define _MMIO_PLANE(plane, a, b)	_MMIO(_PLANE(plane, a, b))
#define _MMIO_TRANS(tran, a, b)		_MMIO(_TRANS(tran, a, b))
#define _MMIO_PORT(port, a, b)		_MMIO(_PORT(port, a, b))
#define _MMIO_PLL(pll, a, b)		_MMIO(_PLL(pll, a, b))
#define _MMIO_PHY(phy, a, b)		_MMIO(_PHY(phy, a, b))

#define _MMIO_BASE_PIPE3(base, pipe, a, b, c)	_MMIO((base) + _PICK_EVEN_2RANGES(pipe, 1, a, a, b, c))
#define _MMIO_BASE_PORT3(base, pipe, a, b, c)	_MMIO((base) + _PICK_EVEN_2RANGES(pipe, 1, a, a, b, c))

/*
 * Device info offset array based helpers for groups of registers with unevenly
 * spaced base offsets.
 */

#define _MMIO_PIPE2(display, pipe, reg)		_MMIO(INTEL_DISPLAY_DEVICE_PIPE_OFFSET((display), (pipe)) + (reg))
#define _MMIO_TRANS2(display, trans, reg)	_MMIO(INTEL_DISPLAY_DEVICE_TRANS_OFFSET((display), (trans)) + (reg))
#define _MMIO_CURSOR2(display, pipe, reg)	_MMIO(INTEL_DISPLAY_DEVICE_CURSOR_OFFSET((display), (pipe)) + (reg))

#endif /* __INTEL_DISPLAY_REG_DEFS_H__ */
