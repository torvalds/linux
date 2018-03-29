// SPDX-License-Identifier: GPL-2.0
/*
 * Intel(R) Trace Hub PTI output data structures
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 */

#ifndef __INTEL_TH_STH_H__
#define __INTEL_TH_STH_H__

enum {
	REG_PTI_CTL	= 0x1c00,
};

#define PTI_EN		BIT(0)
#define PTI_FCEN	BIT(1)
#define PTI_MODE	0xf0
#define LPP_PTIPRESENT	BIT(8)
#define LPP_BSSBPRESENT	BIT(9)
#define PTI_CLKDIV	0x000f0000
#define PTI_PATGENMODE	0x00f00000
#define LPP_DEST	BIT(25)
#define LPP_BSSBACT	BIT(30)
#define LPP_LPPBUSY	BIT(31)

#define LPP_DEST_PTI	BIT(0)
#define LPP_DEST_EXI	BIT(1)

#endif /* __INTEL_TH_STH_H__ */
