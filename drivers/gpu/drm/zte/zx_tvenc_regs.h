/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2017 Linaro Ltd.
 * Copyright 2017 ZTE Corporation.
 */

#ifndef __ZX_TVENC_REGS_H__
#define __ZX_TVENC_REGS_H__

#define VENC_VIDEO_INFO			0x04
#define VENC_VIDEO_RES			0x08
#define VENC_FIELD1_PARAM		0x10
#define VENC_FIELD2_PARAM		0x14
#define VENC_LINE_O_1			0x18
#define VENC_LINE_E_1			0x1c
#define VENC_LINE_O_2			0x20
#define VENC_LINE_E_2			0x24
#define VENC_LINE_TIMING_PARAM		0x28
#define VENC_WEIGHT_VALUE		0x2c
#define VENC_BLANK_BLACK_LEVEL		0x30
#define VENC_BURST_LEVEL		0x34
#define VENC_CONTROL_PARAM		0x3c
#define VENC_SUB_CARRIER_PHASE1		0x40
#define VENC_PHASE_LINE_INCR_CVBS	0x48
#define VENC_ENABLE			0xa8

#endif /* __ZX_TVENC_REGS_H__ */
