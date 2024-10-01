/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef __DISPLAY_MODE_LIB_DEFINES_H__
#define __DISPLAY_MODE_LIB_DEFINES_H__

#define DCN_DML__DML_STANDALONE 1
#define DCN_DML__DML_STANDALONE__1 1
#define DCN_DML__PRESENT 1
#define DCN_DML__PRESENT__1 1
#define DCN_DML__NUM_PLANE 8
#define DCN_DML__NUM_PLANE__8 1
#define DCN_DML__NUM_CURSOR 1
#define DCN_DML__NUM_CURSOR__1 1
#define DCN_DML__NUM_PWR_STATE 30
#define DCN_DML__NUM_PWR_STATE__30 1
#define DCN_DML__VM_PRESENT 1
#define DCN_DML__VM_PRESENT__1 1
#define DCN_DML__HOST_VM_PRESENT 1
#define DCN_DML__HOST_VM_PRESENT__1 1
#define DCN_DML__DWB 1

#include "dml_depedencies.h"

#include "dml_logging.h"
#include "dml_assert.h"

// To enable a lot of debug msg
#define __DML_VBA_DEBUG__
#define __DML_VBA_ENABLE_INLINE_CHECK_                  0
#define __DML_VBA_MIN_VSTARTUP__                        9       //<brief At which vstartup the DML start to try if the mode can be supported
#define __DML_ARB_TO_RET_DELAY__                        7 + 95  //<brief Delay in DCFCLK from ARB to DET (1st num is ARB to SDPIF, 2nd number is SDPIF to DET)
#define __DML_MIN_DCFCLK_FACTOR__                       1.15    //<brief fudge factor for min dcfclk calclation
#define __DML_MAX_VRATIO_PRE__                          4.0     //<brief Prefetch schedule max vratio
#define __DML_MAX_VRATIO_PRE_OTO__                      4.0     //<brief Prefetch schedule max vratio for one to one scheduling calculation for prefetch
#define __DML_MAX_VRATIO_PRE_ENHANCE_PREFETCH_ACC__     6.0     //<brief Prefetch schedule max vratio when enhance prefetch schedule acceleration is enabled and vstartup is earliest possible already
#define __DML_NUM_PLANES__                              DCN_DML__NUM_PLANE
#define __DML_NUM_CURSORS__                             DCN_DML__NUM_CURSOR
#define __DML_DPP_INVALID__                             0
#define __DML_NUM_DMB__                                 DCN_DML__DWB
#define __DML_PIPE_NO_PLANE__                           99

#define __DML_MAX_STATE_ARRAY_SIZE__        DCN_DML__NUM_PWR_STATE

// Compilation define
#define __DML_DLL_EXPORT__

typedef          int   dml_int_t;   // int is 32-bit in C/C++, but Integer datatype is 16-bit in VBA. this should map to Long in VBA
typedef unsigned int   dml_uint_t;
typedef double         dml_float_t;

// Note: bool is 8-bit in C/C++, but Boolean is 16-bit in VBA, use "short" in C/C++ DLL so the struct work when vba uses DLL
// Or the VBA side don't use Boolean, just use "Byte", then C side can use bool
typedef bool          dml_bool_t;

#endif
