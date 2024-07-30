/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef __INTEL_CURSOR_REGS_H__
#define __INTEL_CURSOR_REGS_H__

#include "intel_display_reg_defs.h"

#define _CURACNTR		0x70080
#define CURCNTR(dev_priv, pipe)	_MMIO_CURSOR2((dev_priv), (pipe), _CURACNTR)
/* Old style CUR*CNTR flags (desktop 8xx) */
#define   CURSOR_ENABLE			REG_BIT(31)
#define   CURSOR_PIPE_GAMMA_ENABLE	REG_BIT(30)
#define   CURSOR_STRIDE_MASK	REG_GENMASK(29, 28)
#define   CURSOR_STRIDE(stride)	REG_FIELD_PREP(CURSOR_STRIDE_MASK, ffs(stride) - 9) /* 256,512,1k,2k */
#define   CURSOR_FORMAT_MASK	REG_GENMASK(26, 24)
#define   CURSOR_FORMAT_2C	REG_FIELD_PREP(CURSOR_FORMAT_MASK, 0)
#define   CURSOR_FORMAT_3C	REG_FIELD_PREP(CURSOR_FORMAT_MASK, 1)
#define   CURSOR_FORMAT_4C	REG_FIELD_PREP(CURSOR_FORMAT_MASK, 2)
#define   CURSOR_FORMAT_ARGB	REG_FIELD_PREP(CURSOR_FORMAT_MASK, 4)
#define   CURSOR_FORMAT_XRGB	REG_FIELD_PREP(CURSOR_FORMAT_MASK, 5)
/* New style CUR*CNTR flags */
#define   MCURSOR_ARB_SLOTS_MASK	REG_GENMASK(30, 28) /* icl+ */
#define   MCURSOR_ARB_SLOTS(x)		REG_FIELD_PREP(MCURSOR_ARB_SLOTS_MASK, (x)) /* icl+ */
#define   MCURSOR_PIPE_SEL_MASK		REG_GENMASK(29, 28)
#define   MCURSOR_PIPE_SEL(pipe)	REG_FIELD_PREP(MCURSOR_PIPE_SEL_MASK, (pipe))
#define   MCURSOR_PIPE_GAMMA_ENABLE	REG_BIT(26)
#define   MCURSOR_PIPE_CSC_ENABLE	REG_BIT(24) /* ilk+ */
#define   MCURSOR_ROTATE_180		REG_BIT(15)
#define   MCURSOR_TRICKLE_FEED_DISABLE	REG_BIT(14)
#define   MCURSOR_MODE_MASK		0x27
#define   MCURSOR_MODE_DISABLE		0x00
#define   MCURSOR_MODE_128_32B_AX	0x02
#define   MCURSOR_MODE_256_32B_AX	0x03
#define   MCURSOR_MODE_64_2B		0x04
#define   MCURSOR_MODE_64_32B_AX	0x07
#define   MCURSOR_MODE_128_ARGB_AX	(0x20 | MCURSOR_MODE_128_32B_AX)
#define   MCURSOR_MODE_256_ARGB_AX	(0x20 | MCURSOR_MODE_256_32B_AX)
#define   MCURSOR_MODE_64_ARGB_AX	(0x20 | MCURSOR_MODE_64_32B_AX)

#define _CURABASE		0x70084
#define CURBASE(dev_priv, pipe)	_MMIO_CURSOR2((dev_priv), (pipe), _CURABASE)

#define _CURAPOS		0x70088
#define CURPOS(dev_priv, pipe)	_MMIO_CURSOR2((dev_priv), (pipe), _CURAPOS)
#define   CURSOR_POS_Y_SIGN		REG_BIT(31)
#define   CURSOR_POS_Y_MASK		REG_GENMASK(30, 16)
#define   CURSOR_POS_Y(y)		REG_FIELD_PREP(CURSOR_POS_Y_MASK, (y))
#define   CURSOR_POS_X_SIGN		REG_BIT(15)
#define   CURSOR_POS_X_MASK		REG_GENMASK(14, 0)
#define   CURSOR_POS_X(x)		REG_FIELD_PREP(CURSOR_POS_X_MASK, (x))

#define _CURAPOS_ERLY_TPT	0x7008c
#define CURPOS_ERLY_TPT(dev_priv, pipe)	_MMIO_CURSOR2((dev_priv), (pipe), _CURAPOS_ERLY_TPT)

#define _CURASIZE		0x700a0 /* 845/865 */
#define CURSIZE(dev_priv, pipe)	_MMIO_CURSOR2((dev_priv), (pipe), _CURASIZE)
#define   CURSOR_HEIGHT_MASK		REG_GENMASK(21, 12)
#define   CURSOR_HEIGHT(h)		REG_FIELD_PREP(CURSOR_HEIGHT_MASK, (h))
#define   CURSOR_WIDTH_MASK		REG_GENMASK(9, 0)
#define   CURSOR_WIDTH(w)		REG_FIELD_PREP(CURSOR_WIDTH_MASK, (w))

#define _CUR_FBC_CTL_A		0x700a0 /* ivb+ */
#define CUR_FBC_CTL(dev_priv, pipe)	_MMIO_CURSOR2((dev_priv), (pipe), _CUR_FBC_CTL_A)
#define   CUR_FBC_EN			REG_BIT(31)
#define   CUR_FBC_HEIGHT_MASK		REG_GENMASK(7, 0)
#define   CUR_FBC_HEIGHT(h)		REG_FIELD_PREP(CUR_FBC_HEIGHT_MASK, (h))

#define _CUR_CHICKEN_A		0x700a4 /* mtl+ */
#define CUR_CHICKEN(dev_priv, pipe)	_MMIO_CURSOR2((dev_priv), (pipe), _CUR_CHICKEN_A)

#define _CURASURFLIVE		0x700ac /* g4x+ */
#define CURSURFLIVE(dev_priv, pipe)	_MMIO_CURSOR2((dev_priv), (pipe), _CURASURFLIVE)

/* skl+ */
#define _CUR_WM_A_0		0x70140
#define _CUR_WM_B_0		0x71140
#define CUR_WM(pipe, level)	_MMIO(_PIPE((pipe), _CUR_WM_A_0, _CUR_WM_B_0) + (level) * 4)
#define   CUR_WM_EN				REG_BIT(31)
#define   CUR_WM_IGNORE_LINES			REG_BIT(30)
#define   CUR_WM_LINES_MASK			REG_GENMASK(26, 14)
#define   CUR_WM_BLOCKS_MASK			REG_GENMASK(11, 0)

#define _CUR_WM_SAGV_A		0x70158
#define _CUR_WM_SAGV_B		0x71158
#define CUR_WM_SAGV(pipe)	_MMIO_PIPE((pipe), _CUR_WM_SAGV_A, _CUR_WM_SAGV_B)

#define _CUR_WM_SAGV_TRANS_A	0x7015C
#define _CUR_WM_SAGV_TRANS_B	0x7115C
#define CUR_WM_SAGV_TRANS(pipe)	_MMIO_PIPE((pipe), _CUR_WM_SAGV_TRANS_A, _CUR_WM_SAGV_TRANS_B)

#define _CUR_WM_TRANS_A		0x70168
#define _CUR_WM_TRANS_B		0x71168
#define CUR_WM_TRANS(pipe)	_MMIO_PIPE((pipe), _CUR_WM_TRANS_A, _CUR_WM_TRANS_B)

#define _CUR_BUF_CFG_A		0x7017c
#define _CUR_BUF_CFG_B		0x7117c
#define CUR_BUF_CFG(pipe)	_MMIO_PIPE((pipe), _CUR_BUF_CFG_A, _CUR_BUF_CFG_B)
/* skl+: 10 bits, icl+ 11 bits, adlp+ 12 bits */
#define   CUR_BUF_END_MASK		REG_GENMASK(27, 16)
#define   CUR_BUF_END(end)		REG_FIELD_PREP(CUR_BUF_END_MASK, (end))
#define   CUR_BUF_START_MASK		REG_GENMASK(11, 0)
#define   CUR_BUF_START(start)		REG_FIELD_PREP(CUR_BUF_START_MASK, (start))

/* tgl+ */
#define _SEL_FETCH_CUR_CTL_A	0x70880
#define _SEL_FETCH_CUR_CTL_B	0x71880
#define SEL_FETCH_CUR_CTL(pipe)	_MMIO_PIPE((pipe), _SEL_FETCH_CUR_CTL_A, _SEL_FETCH_CUR_CTL_B)

#endif /* __INTEL_CURSOR_REGS_H__ */
