/*
 * Copyright 2009 Advanced Micro Devices, Inc.
 * Copyright 2009 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#ifndef AVIVOD_H
#define AVIVOD_H


#define	D1CRTC_CONTROL					0x6080
#define		CRTC_EN						(1 << 0)
#define	D1CRTC_STATUS					0x609c
#define	D1CRTC_UPDATE_LOCK				0x60E8
#define	D1GRPH_PRIMARY_SURFACE_ADDRESS			0x6110
#define	D1GRPH_SECONDARY_SURFACE_ADDRESS		0x6118

#define	D2CRTC_CONTROL					0x6880
#define	D2CRTC_STATUS					0x689c
#define	D2CRTC_UPDATE_LOCK				0x68E8
#define	D2GRPH_PRIMARY_SURFACE_ADDRESS			0x6910
#define	D2GRPH_SECONDARY_SURFACE_ADDRESS		0x6918

#define	D1VGA_CONTROL					0x0330
#define		DVGA_CONTROL_MODE_ENABLE			(1 << 0)
#define		DVGA_CONTROL_TIMING_SELECT			(1 << 8)
#define		DVGA_CONTROL_SYNC_POLARITY_SELECT		(1 << 9)
#define		DVGA_CONTROL_OVERSCAN_TIMING_SELECT		(1 << 10)
#define		DVGA_CONTROL_OVERSCAN_COLOR_EN			(1 << 16)
#define		DVGA_CONTROL_ROTATE				(1 << 24)
#define D2VGA_CONTROL					0x0338

#define	VGA_HDP_CONTROL					0x328
#define		VGA_MEM_PAGE_SELECT_EN				(1 << 0)
#define		VGA_MEMORY_DISABLE				(1 << 4)
#define		VGA_RBBM_LOCK_DISABLE				(1 << 8)
#define		VGA_SOFT_RESET					(1 << 16)
#define	VGA_MEMORY_BASE_ADDRESS				0x0310
#define	VGA_RENDER_CONTROL				0x0300
#define		VGA_VSTATUS_CNTL_MASK				0x00030000

#endif
