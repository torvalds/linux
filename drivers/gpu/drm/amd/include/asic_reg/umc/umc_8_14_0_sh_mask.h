/*
 * Copyright (C) 2024  Advanced Micro Devices, Inc.
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
#ifndef _umc_8_14_0_SH_MASK_HEADER
#define _umc_8_14_0_SH_MASK_HEADER

//UMCCH0_GeccErrCntSel
#define UMCCH0_GeccErrCntSel__GeccErrInt__SHIFT              0xc
#define UMCCH0_GeccErrCntSel__GeccErrCntEn__SHIFT            0xf
#define UMCCH0_GeccErrCntSel__PoisonCntEn__SHIFT             0x10
#define UMCCH0_GeccErrCntSel__GeccErrInt_MASK                0x00003000L
#define UMCCH0_GeccErrCntSel__GeccErrCntEn_MASK              0x00008000L
#define UMCCH0_GeccErrCntSel__PoisonCntEn_MASK               0x00030000L
//UMCCH0_GeccErrCnt
#define UMCCH0_GeccErrCnt__GeccErrCnt__SHIFT                 0x0
#define UMCCH0_GeccErrCnt__GeccUnCorrErrCnt__SHIFT           0x10
#define UMCCH0_GeccErrCnt__GeccErrCnt_MASK                   0x0000FFFFL
#define UMCCH0_GeccErrCnt__GeccUnCorrErrCnt_MASK             0xFFFF0000L

#endif
