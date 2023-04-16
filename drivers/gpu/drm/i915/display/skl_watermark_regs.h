/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __SKL_WATERMARK_REGS_H__
#define __SKL_WATERMARK_REGS_H__

#include "intel_display_reg_defs.h"

#define _PIPEA_MBUS_DBOX_CTL			0x7003C
#define _PIPEB_MBUS_DBOX_CTL			0x7103C
#define PIPE_MBUS_DBOX_CTL(pipe)		_MMIO_PIPE(pipe, _PIPEA_MBUS_DBOX_CTL, \
							   _PIPEB_MBUS_DBOX_CTL)
#define MBUS_DBOX_B2B_TRANSACTIONS_MAX_MASK	REG_GENMASK(24, 20) /* tgl+ */
#define MBUS_DBOX_B2B_TRANSACTIONS_MAX(x)	REG_FIELD_PREP(MBUS_DBOX_B2B_TRANSACTIONS_MAX_MASK, x)
#define MBUS_DBOX_B2B_TRANSACTIONS_DELAY_MASK	REG_GENMASK(19, 17) /* tgl+ */
#define MBUS_DBOX_B2B_TRANSACTIONS_DELAY(x)	REG_FIELD_PREP(MBUS_DBOX_B2B_TRANSACTIONS_DELAY_MASK, x)
#define MBUS_DBOX_REGULATE_B2B_TRANSACTIONS_EN	REG_BIT(16) /* tgl+ */
#define MBUS_DBOX_BW_CREDIT_MASK		REG_GENMASK(15, 14)
#define MBUS_DBOX_BW_CREDIT(x)			REG_FIELD_PREP(MBUS_DBOX_BW_CREDIT_MASK, x)
#define MBUS_DBOX_BW_4CREDITS_MTL		REG_FIELD_PREP(MBUS_DBOX_BW_CREDIT_MASK, 0x2)
#define MBUS_DBOX_BW_8CREDITS_MTL		REG_FIELD_PREP(MBUS_DBOX_BW_CREDIT_MASK, 0x3)
#define MBUS_DBOX_B_CREDIT_MASK			REG_GENMASK(12, 8)
#define MBUS_DBOX_B_CREDIT(x)			REG_FIELD_PREP(MBUS_DBOX_B_CREDIT_MASK, x)
#define MBUS_DBOX_I_CREDIT_MASK			REG_GENMASK(7, 5)
#define MBUS_DBOX_I_CREDIT(x)			REG_FIELD_PREP(MBUS_DBOX_I_CREDIT_MASK, x)
#define MBUS_DBOX_A_CREDIT_MASK			REG_GENMASK(3, 0)
#define MBUS_DBOX_A_CREDIT(x)			REG_FIELD_PREP(MBUS_DBOX_A_CREDIT_MASK, x)

#define MBUS_UBOX_CTL			_MMIO(0x4503C)
#define MBUS_BBOX_CTL_S1		_MMIO(0x45040)
#define MBUS_BBOX_CTL_S2		_MMIO(0x45044)

#define MBUS_CTL			_MMIO(0x4438C)
#define MBUS_JOIN			REG_BIT(31)
#define MBUS_HASHING_MODE_MASK		REG_BIT(30)
#define MBUS_HASHING_MODE_2x2		REG_FIELD_PREP(MBUS_HASHING_MODE_MASK, 0)
#define MBUS_HASHING_MODE_1x4		REG_FIELD_PREP(MBUS_HASHING_MODE_MASK, 1)
#define MBUS_JOIN_PIPE_SELECT_MASK	REG_GENMASK(28, 26)
#define MBUS_JOIN_PIPE_SELECT(pipe)	REG_FIELD_PREP(MBUS_JOIN_PIPE_SELECT_MASK, pipe)
#define MBUS_JOIN_PIPE_SELECT_NONE	MBUS_JOIN_PIPE_SELECT(7)

/* Watermark register definitions for SKL */
#define _CUR_WM_A_0		0x70140
#define _CUR_WM_B_0		0x71140
#define _CUR_WM_SAGV_A		0x70158
#define _CUR_WM_SAGV_B		0x71158
#define _CUR_WM_SAGV_TRANS_A	0x7015C
#define _CUR_WM_SAGV_TRANS_B	0x7115C
#define _CUR_WM_TRANS_A		0x70168
#define _CUR_WM_TRANS_B		0x71168
#define _PLANE_WM_1_A_0		0x70240
#define _PLANE_WM_1_B_0		0x71240
#define _PLANE_WM_2_A_0		0x70340
#define _PLANE_WM_2_B_0		0x71340
#define _PLANE_WM_SAGV_1_A	0x70258
#define _PLANE_WM_SAGV_1_B	0x71258
#define _PLANE_WM_SAGV_2_A	0x70358
#define _PLANE_WM_SAGV_2_B	0x71358
#define _PLANE_WM_SAGV_TRANS_1_A	0x7025C
#define _PLANE_WM_SAGV_TRANS_1_B	0x7125C
#define _PLANE_WM_SAGV_TRANS_2_A	0x7035C
#define _PLANE_WM_SAGV_TRANS_2_B	0x7135C
#define _PLANE_WM_TRANS_1_A	0x70268
#define _PLANE_WM_TRANS_1_B	0x71268
#define _PLANE_WM_TRANS_2_A	0x70368
#define _PLANE_WM_TRANS_2_B	0x71368
#define   PLANE_WM_EN		(1 << 31)
#define   PLANE_WM_IGNORE_LINES	(1 << 30)
#define   PLANE_WM_LINES_MASK	REG_GENMASK(26, 14)
#define   PLANE_WM_BLOCKS_MASK	REG_GENMASK(11, 0)

#define _CUR_WM_0(pipe) _PIPE(pipe, _CUR_WM_A_0, _CUR_WM_B_0)
#define CUR_WM(pipe, level) _MMIO(_CUR_WM_0(pipe) + ((4) * (level)))
#define CUR_WM_SAGV(pipe) _MMIO_PIPE(pipe, _CUR_WM_SAGV_A, _CUR_WM_SAGV_B)
#define CUR_WM_SAGV_TRANS(pipe) _MMIO_PIPE(pipe, _CUR_WM_SAGV_TRANS_A, _CUR_WM_SAGV_TRANS_B)
#define CUR_WM_TRANS(pipe) _MMIO_PIPE(pipe, _CUR_WM_TRANS_A, _CUR_WM_TRANS_B)
#define _PLANE_WM_1(pipe) _PIPE(pipe, _PLANE_WM_1_A_0, _PLANE_WM_1_B_0)
#define _PLANE_WM_2(pipe) _PIPE(pipe, _PLANE_WM_2_A_0, _PLANE_WM_2_B_0)
#define _PLANE_WM_BASE(pipe, plane) \
	_PLANE(plane, _PLANE_WM_1(pipe), _PLANE_WM_2(pipe))
#define PLANE_WM(pipe, plane, level) \
	_MMIO(_PLANE_WM_BASE(pipe, plane) + ((4) * (level)))
#define _PLANE_WM_SAGV_1(pipe) \
	_PIPE(pipe, _PLANE_WM_SAGV_1_A, _PLANE_WM_SAGV_1_B)
#define _PLANE_WM_SAGV_2(pipe) \
	_PIPE(pipe, _PLANE_WM_SAGV_2_A, _PLANE_WM_SAGV_2_B)
#define PLANE_WM_SAGV(pipe, plane) \
	_MMIO(_PLANE(plane, _PLANE_WM_SAGV_1(pipe), _PLANE_WM_SAGV_2(pipe)))
#define _PLANE_WM_SAGV_TRANS_1(pipe) \
	_PIPE(pipe, _PLANE_WM_SAGV_TRANS_1_A, _PLANE_WM_SAGV_TRANS_1_B)
#define _PLANE_WM_SAGV_TRANS_2(pipe) \
	_PIPE(pipe, _PLANE_WM_SAGV_TRANS_2_A, _PLANE_WM_SAGV_TRANS_2_B)
#define PLANE_WM_SAGV_TRANS(pipe, plane) \
	_MMIO(_PLANE(plane, _PLANE_WM_SAGV_TRANS_1(pipe), _PLANE_WM_SAGV_TRANS_2(pipe)))
#define _PLANE_WM_TRANS_1(pipe) \
	_PIPE(pipe, _PLANE_WM_TRANS_1_A, _PLANE_WM_TRANS_1_B)
#define _PLANE_WM_TRANS_2(pipe) \
	_PIPE(pipe, _PLANE_WM_TRANS_2_A, _PLANE_WM_TRANS_2_B)
#define PLANE_WM_TRANS(pipe, plane) \
	_MMIO(_PLANE(plane, _PLANE_WM_TRANS_1(pipe), _PLANE_WM_TRANS_2(pipe)))

#define _PLANE_BUF_CFG_1_B			0x7127c
#define _PLANE_BUF_CFG_2_B			0x7137c
#define _PLANE_BUF_CFG_1(pipe)	\
	_PIPE(pipe, _PLANE_BUF_CFG_1_A, _PLANE_BUF_CFG_1_B)
#define _PLANE_BUF_CFG_2(pipe)	\
	_PIPE(pipe, _PLANE_BUF_CFG_2_A, _PLANE_BUF_CFG_2_B)
#define PLANE_BUF_CFG(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_BUF_CFG_1(pipe), _PLANE_BUF_CFG_2(pipe))

#define _PLANE_NV12_BUF_CFG_1_B		0x71278
#define _PLANE_NV12_BUF_CFG_2_B		0x71378
#define _PLANE_NV12_BUF_CFG_1(pipe)	\
	_PIPE(pipe, _PLANE_NV12_BUF_CFG_1_A, _PLANE_NV12_BUF_CFG_1_B)
#define _PLANE_NV12_BUF_CFG_2(pipe)	\
	_PIPE(pipe, _PLANE_NV12_BUF_CFG_2_A, _PLANE_NV12_BUF_CFG_2_B)
#define PLANE_NV12_BUF_CFG(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_NV12_BUF_CFG_1(pipe), _PLANE_NV12_BUF_CFG_2(pipe))

/* SKL new cursor registers */
#define _CUR_BUF_CFG_A				0x7017c
#define _CUR_BUF_CFG_B				0x7117c
#define CUR_BUF_CFG(pipe)	_MMIO_PIPE(pipe, _CUR_BUF_CFG_A, _CUR_BUF_CFG_B)

/*
 * The below are numbered starting from "S1" on gen11/gen12, but starting
 * with display 13, the bspec switches to a 0-based numbering scheme
 * (although the addresses stay the same so new S0 = old S1, new S1 = old S2).
 * We'll just use the 0-based numbering here for all platforms since it's the
 * way things will be named by the hardware team going forward, plus it's more
 * consistent with how most of the rest of our registers are named.
 */
#define _DBUF_CTL_S0				0x45008
#define _DBUF_CTL_S1				0x44FE8
#define _DBUF_CTL_S2				0x44300
#define _DBUF_CTL_S3				0x44304
#define DBUF_CTL_S(slice)			_MMIO(_PICK(slice, \
							    _DBUF_CTL_S0, \
							    _DBUF_CTL_S1, \
							    _DBUF_CTL_S2, \
							    _DBUF_CTL_S3))
#define  DBUF_POWER_REQUEST			REG_BIT(31)
#define  DBUF_POWER_STATE			REG_BIT(30)
#define  DBUF_TRACKER_STATE_SERVICE_MASK	REG_GENMASK(23, 19)
#define  DBUF_TRACKER_STATE_SERVICE(x)		REG_FIELD_PREP(DBUF_TRACKER_STATE_SERVICE_MASK, x)
#define  DBUF_MIN_TRACKER_STATE_SERVICE_MASK	REG_GENMASK(18, 16) /* ADL-P+ */
#define  DBUF_MIN_TRACKER_STATE_SERVICE(x)		REG_FIELD_PREP(DBUF_MIN_TRACKER_STATE_SERVICE_MASK, x) /* ADL-P+ */

#define MTL_LATENCY_LP0_LP1		_MMIO(0x45780)
#define MTL_LATENCY_LP2_LP3		_MMIO(0x45784)
#define MTL_LATENCY_LP4_LP5		_MMIO(0x45788)
#define  MTL_LATENCY_LEVEL_EVEN_MASK	REG_GENMASK(12, 0)
#define  MTL_LATENCY_LEVEL_ODD_MASK	REG_GENMASK(28, 16)

#define MTL_LATENCY_SAGV		_MMIO(0x4578c)
#define   MTL_LATENCY_QCLK_SAGV		REG_GENMASK(12, 0)

#endif /* __SKL_WATERMARK_REGS_H__ */
