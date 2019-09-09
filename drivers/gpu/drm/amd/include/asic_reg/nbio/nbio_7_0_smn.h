/*
 * Copyright (C) 2019  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _nbio_7_0_SMN_HEADER
#define _nbio_7_0_SMN_HEADER


#define smnCPM_CONTROL					0x11180460
#define smnPCIE_CNTL2					0x11180070

#define smnPCIE_PERF_COUNT_CNTL				0x11180200
#define smnPCIE_PERF_CNTL_TXCLK				0x11180204
#define smnPCIE_PERF_COUNT0_TXCLK			0x11180208
#define smnPCIE_PERF_COUNT1_TXCLK			0x1118020c
#define smnPCIE_PERF_CNTL_MST_R_CLK			0x11180210
#define smnPCIE_PERF_COUNT0_MST_R_CLK			0x11180214
#define smnPCIE_PERF_COUNT1_MST_R_CLK			0x11180218
#define smnPCIE_PERF_CNTL_MST_C_CLK			0x1118021c
#define smnPCIE_PERF_COUNT0_MST_C_CLK			0x11180220
#define smnPCIE_PERF_COUNT1_MST_C_CLK			0x11180224
#define smnPCIE_PERF_CNTL_SLV_R_CLK			0x11180228
#define smnPCIE_PERF_COUNT0_SLV_R_CLK			0x1118022c
#define smnPCIE_PERF_COUNT1_SLV_R_CLK			0x11180230
#define smnPCIE_PERF_CNTL_SLV_S_C_CLK			0x11180234
#define smnPCIE_PERF_COUNT0_SLV_S_C_CLK			0x11180238
#define smnPCIE_PERF_COUNT1_SLV_S_C_CLK			0x1118023c
#define smnPCIE_PERF_CNTL_SLV_NS_C_CLK			0x11180240
#define smnPCIE_PERF_COUNT0_SLV_NS_C_CLK		0x11180244
#define smnPCIE_PERF_COUNT1_SLV_NS_C_CLK		0x11180248
#define smnPCIE_PERF_CNTL_EVENT0_PORT_SEL		0x1118024c
#define smnPCIE_PERF_CNTL_EVENT1_PORT_SEL		0x11180250
#define smnPCIE_PERF_CNTL_TXCLK2			0x11180254
#define smnPCIE_PERF_COUNT0_TXCLK2			0x11180258
#define smnPCIE_PERF_COUNT1_TXCLK2			0x1118025c

#define smnPCIE_RX_NUM_NAK				0x11180038
#define smnPCIE_RX_NUM_NAK_GENERATED			0x1118003c

#endif	// _nbio_7_0_SMN_HEADER
