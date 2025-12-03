/* SPDX-License-Identifier: MIT */

/*
 * Copyright 2024 Advanced Micro Devices, Inc. All rights reserved.
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
 */

#ifndef __IRQSRCS_VCN_5_0_H__
#define __IRQSRCS_VCN_5_0_H__

#define VCN_5_0__SRCID__UVD_TRAP                        114	// 0x72 UVD_TRAP
#define VCN_5_0__SRCID__UVD_ENC_GENERAL_PURPOSE         119	// 0x77 Encoder General Purpose
#define VCN_5_0__SRCID__UVD_ENC_LOW_LATENCY             120	// 0x78 Encoder Low Latency
#define VCN_5_0__SRCID__UVD_SYSTEM_MESSAGE_INTERRUPT    124	// 0x7c UVD system message interrupt
#define VCN_5_0__SRCID__JPEG_ENCODE                     151	// 0x97 JRBC Encode interrupt
#define VCN_5_0__SRCID__JPEG_DECODE                     153	// 0x99 JRBC Decode interrupt
#define VCN_5_0__SRCID__JPEG1_DECODE                    149	// 0x95 JRBC1 Decode interrupt
#define VCN_5_0__SRCID__JPEG2_DECODE                    151	// 0x97 JRBC2 Decode interrupt
#define VCN_5_0__SRCID__JPEG3_DECODE                    171	// 0xab JRBC3 Decode interrupt
#define VCN_5_0__SRCID__JPEG4_DECODE                    172	// 0xac JRBC4 Decode interrupt
#define VCN_5_0__SRCID__JPEG5_DECODE                    173	// 0xad JRBC5 Decode interrupt
#define VCN_5_0__SRCID__JPEG6_DECODE                    174	// 0xae JRBC6 Decode interrupt
#define VCN_5_0__SRCID__JPEG7_DECODE                    175	// 0xaf JRBC7 Decode interrupt
#define VCN_5_0__SRCID__JPEG8_DECODE                    177	// 0xb1 JRBC8 Decode interrupt
#define VCN_5_0__SRCID__JPEG9_DECODE                    178	// 0xb2 JRBC9 Decode interrupt

#define VCN_5_0__SRCID_UVD_POISON                       160
#define VCN_5_0__SRCID_DJPEG0_POISON                    161
#define VCN_5_0__SRCID_EJPEG0_POISON                    162
#endif
