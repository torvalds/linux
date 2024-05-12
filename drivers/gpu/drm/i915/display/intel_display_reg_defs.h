/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_REG_DEFS_H__
#define __INTEL_DISPLAY_REG_DEFS_H__

#include "i915_reg_defs.h"

#define DISPLAY_MMIO_BASE(dev_priv)	(INTEL_INFO(dev_priv)->display.mmio_offset)

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

#define _MMIO_PIPE3(pipe, a, b, c)	_MMIO(_PICK_EVEN_2RANGES(pipe, 1, a, a, b, c))
#define _MMIO_PORT3(pipe, a, b, c)	_MMIO(_PICK_EVEN_2RANGES(pipe, 1, a, a, b, c))

/*
 * Device info offset array based helpers for groups of registers with unevenly
 * spaced base offsets.
 */
#define _MMIO_PIPE2(pipe, reg)		_MMIO(INTEL_INFO(dev_priv)->display.pipe_offsets[(pipe)] - \
					      INTEL_INFO(dev_priv)->display.pipe_offsets[PIPE_A] + \
					      DISPLAY_MMIO_BASE(dev_priv) + (reg))
#define _MMIO_TRANS2(tran, reg)		_MMIO(INTEL_INFO(dev_priv)->display.trans_offsets[(tran)] - \
					      INTEL_INFO(dev_priv)->display.trans_offsets[TRANSCODER_A] + \
					      DISPLAY_MMIO_BASE(dev_priv) + (reg))
#define _MMIO_CURSOR2(pipe, reg)	_MMIO(INTEL_INFO(dev_priv)->display.cursor_offsets[(pipe)] - \
					      INTEL_INFO(dev_priv)->display.cursor_offsets[PIPE_A] + \
					      DISPLAY_MMIO_BASE(dev_priv) + (reg))

#endif /* __INTEL_DISPLAY_REG_DEFS_H__ */
