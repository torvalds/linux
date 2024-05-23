/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef __I9XX_PLANE_REGS_H__
#define __I9XX_PLANE_REGS_H__

#include "intel_display_reg_defs.h"

#define _DSPAADDR_VLV				0x7017C /* vlv/chv */
#define DSPADDR_VLV(dev_priv, plane)		_MMIO_PIPE2(dev_priv, plane, _DSPAADDR_VLV)

#define _DSPACNTR				0x70180
#define DSPCNTR(dev_priv, plane)		_MMIO_PIPE2(dev_priv, plane, _DSPACNTR)
#define   DISP_ENABLE			REG_BIT(31)
#define   DISP_PIPE_GAMMA_ENABLE	REG_BIT(30)
#define   DISP_FORMAT_MASK		REG_GENMASK(29, 26)
#define   DISP_FORMAT_8BPP		REG_FIELD_PREP(DISP_FORMAT_MASK, 2)
#define   DISP_FORMAT_BGRA555		REG_FIELD_PREP(DISP_FORMAT_MASK, 3)
#define   DISP_FORMAT_BGRX555		REG_FIELD_PREP(DISP_FORMAT_MASK, 4)
#define   DISP_FORMAT_BGRX565		REG_FIELD_PREP(DISP_FORMAT_MASK, 5)
#define   DISP_FORMAT_BGRX888		REG_FIELD_PREP(DISP_FORMAT_MASK, 6)
#define   DISP_FORMAT_BGRA888		REG_FIELD_PREP(DISP_FORMAT_MASK, 7)
#define   DISP_FORMAT_RGBX101010	REG_FIELD_PREP(DISP_FORMAT_MASK, 8)
#define   DISP_FORMAT_RGBA101010	REG_FIELD_PREP(DISP_FORMAT_MASK, 9)
#define   DISP_FORMAT_BGRX101010	REG_FIELD_PREP(DISP_FORMAT_MASK, 10)
#define   DISP_FORMAT_BGRA101010	REG_FIELD_PREP(DISP_FORMAT_MASK, 11)
#define   DISP_FORMAT_RGBX161616	REG_FIELD_PREP(DISP_FORMAT_MASK, 12)
#define   DISP_FORMAT_RGBX888		REG_FIELD_PREP(DISP_FORMAT_MASK, 14)
#define   DISP_FORMAT_RGBA888		REG_FIELD_PREP(DISP_FORMAT_MASK, 15)
#define   DISP_STEREO_ENABLE		REG_BIT(25)
#define   DISP_PIPE_CSC_ENABLE		REG_BIT(24) /* ilk+ */
#define   DISP_PIPE_SEL_MASK		REG_GENMASK(25, 24)
#define   DISP_PIPE_SEL(pipe)		REG_FIELD_PREP(DISP_PIPE_SEL_MASK, (pipe))
#define   DISP_SRC_KEY_ENABLE		REG_BIT(22)
#define   DISP_LINE_DOUBLE		REG_BIT(20)
#define   DISP_STEREO_POLARITY_SECOND	REG_BIT(18)
#define   DISP_ALPHA_PREMULTIPLY	REG_BIT(16) /* CHV pipe B */
#define   DISP_ROTATE_180		REG_BIT(15) /* i965+ */
#define   DISP_TRICKLE_FEED_DISABLE	REG_BIT(14) /* g4x+ */
#define   DISP_TILED			REG_BIT(10) /* i965+ */
#define   DISP_ASYNC_FLIP		REG_BIT(9) /* g4x+ */
#define   DISP_MIRROR			REG_BIT(8) /* CHV pipe B */

#define _DSPAADDR				0x70184 /* pre-i965 */
#define DSPADDR(dev_priv, plane)		_MMIO_PIPE2(dev_priv, plane, _DSPAADDR)

#define _DSPALINOFF				0x70184 /* i965+ */
#define DSPLINOFF(dev_priv, plane)		_MMIO_PIPE2(dev_priv, plane, _DSPALINOFF)

#define _DSPASTRIDE				0x70188
#define DSPSTRIDE(dev_priv, plane)		_MMIO_PIPE2(dev_priv, plane, _DSPASTRIDE)

#define _DSPAPOS				0x7018C /* pre-g4x */
#define DSPPOS(dev_priv, plane)			_MMIO_PIPE2(dev_priv, plane, _DSPAPOS)
#define   DISP_POS_Y_MASK		REG_GENMASK(31, 16)
#define   DISP_POS_Y(y)			REG_FIELD_PREP(DISP_POS_Y_MASK, (y))
#define   DISP_POS_X_MASK		REG_GENMASK(15, 0)
#define   DISP_POS_X(x)			REG_FIELD_PREP(DISP_POS_X_MASK, (x))

#define _DSPASIZE				0x70190 /* pre-g4x */
#define DSPSIZE(dev_priv, plane)		_MMIO_PIPE2(dev_priv, plane, _DSPASIZE)
#define   DISP_HEIGHT_MASK		REG_GENMASK(31, 16)
#define   DISP_HEIGHT(h)		REG_FIELD_PREP(DISP_HEIGHT_MASK, (h))
#define   DISP_WIDTH_MASK		REG_GENMASK(15, 0)
#define   DISP_WIDTH(w)			REG_FIELD_PREP(DISP_WIDTH_MASK, (w))

#define _DSPASURF				0x7019C /* i965+ */
#define DSPSURF(dev_priv, plane)		_MMIO_PIPE2(dev_priv, plane, _DSPASURF)
#define   DISP_ADDR_MASK		REG_GENMASK(31, 12)

#define _DSPATILEOFF				0x701A4 /* i965+ */
#define DSPTILEOFF(dev_priv, plane)		_MMIO_PIPE2(dev_priv, plane, _DSPATILEOFF)
#define   DISP_OFFSET_Y_MASK		REG_GENMASK(31, 16)
#define   DISP_OFFSET_Y(y)		REG_FIELD_PREP(DISP_OFFSET_Y_MASK, (y))
#define   DISP_OFFSET_X_MASK		REG_GENMASK(15, 0)
#define   DISP_OFFSET_X(x)		REG_FIELD_PREP(DISP_OFFSET_X_MASK, (x))

#define _DSPAOFFSET				0x701A4 /* hsw+ */
#define DSPOFFSET(dev_priv, plane)		_MMIO_PIPE2(dev_priv, plane, _DSPAOFFSET)

#define _DSPASURFLIVE				0x701AC /* g4x+ */
#define DSPSURFLIVE(dev_priv, plane)		_MMIO_PIPE2(dev_priv, plane, _DSPASURFLIVE)

#define _DSPAGAMC				0x701E0 /* pre-g4x */
#define DSPGAMC(dev_priv, plane, i)		_MMIO_PIPE2(dev_priv, plane, _DSPAGAMC + (5 - (i)) * 4) /* plane C only, 6 x u0.8 */

/* CHV pipe B primary plane */
#define _PRIMPOS_A			0x60a08
#define PRIMPOS(dev_priv, plane)	_MMIO_TRANS2(dev_priv, plane, _PRIMPOS_A)
#define   PRIM_POS_Y_MASK	REG_GENMASK(31, 16)
#define   PRIM_POS_Y(y)		REG_FIELD_PREP(PRIM_POS_Y_MASK, (y))
#define   PRIM_POS_X_MASK	REG_GENMASK(15, 0)
#define   PRIM_POS_X(x)		REG_FIELD_PREP(PRIM_POS_X_MASK, (x))

#define _PRIMSIZE_A			0x60a0c
#define PRIMSIZE(dev_priv, plane)	_MMIO_TRANS2(dev_priv, plane, _PRIMSIZE_A)
#define   PRIM_HEIGHT_MASK	REG_GENMASK(31, 16)
#define   PRIM_HEIGHT(h)	REG_FIELD_PREP(PRIM_HEIGHT_MASK, (h))
#define   PRIM_WIDTH_MASK	REG_GENMASK(15, 0)
#define   PRIM_WIDTH(w)		REG_FIELD_PREP(PRIM_WIDTH_MASK, (w))

#define _PRIMCNSTALPHA_A		0x60a10
#define PRIMCNSTALPHA(dev_priv, plane)	_MMIO_TRANS2(dev_priv, plane, _PRIMCNSTALPHA_A)
#define   PRIM_CONST_ALPHA_ENABLE	REG_BIT(31)
#define   PRIM_CONST_ALPHA_MASK		REG_GENMASK(7, 0)
#define   PRIM_CONST_ALPHA(alpha)	REG_FIELD_PREP(PRIM_CONST_ALPHA_MASK, (alpha))

#endif /* __I9XX_PLANE_REGS_H__ */
