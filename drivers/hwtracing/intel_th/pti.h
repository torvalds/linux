/*
 * Intel(R) Trace Hub PTI output data structures
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __INTEL_TH_STH_H__
#define __INTEL_TH_STH_H__

enum {
	REG_PTI_CTL	= 0x1c00,
};

#define PTI_EN		BIT(0)
#define PTI_FCEN	BIT(1)
#define PTI_MODE	0xf0
#define PTI_CLKDIV	0x000f0000
#define PTI_PATGENMODE	0x00f00000

#endif /* __INTEL_TH_STH_H__ */
