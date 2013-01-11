/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
 */
#ifndef __RADEON_UCODE_H__
#define __RADEON_UCODE_H__

/* CP */
#define R600_PFP_UCODE_SIZE          576
#define R600_PM4_UCODE_SIZE          1792
#define R700_PFP_UCODE_SIZE          848
#define R700_PM4_UCODE_SIZE          1360
#define EVERGREEN_PFP_UCODE_SIZE     1120
#define EVERGREEN_PM4_UCODE_SIZE     1376
#define CAYMAN_PFP_UCODE_SIZE        2176
#define CAYMAN_PM4_UCODE_SIZE        2176

/* RLC */
#define R600_RLC_UCODE_SIZE          768
#define R700_RLC_UCODE_SIZE          1024
#define EVERGREEN_RLC_UCODE_SIZE     768
#define CAYMAN_RLC_UCODE_SIZE        1024
#define ARUBA_RLC_UCODE_SIZE         1536

/* MC */
#define BTC_MC_UCODE_SIZE            6024
#define CAYMAN_MC_UCODE_SIZE         6037

#endif
