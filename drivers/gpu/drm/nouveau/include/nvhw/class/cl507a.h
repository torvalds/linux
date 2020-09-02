/*
 * Copyright (c) 1993-2014, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */


#ifndef _cl507a_h_
#define _cl507a_h_

#define NV507A_FREE                                                             (0x00000008)
#define NV507A_FREE_COUNT                                                       5:0
#define NV507A_UPDATE                                                           (0x00000080)
#define NV507A_UPDATE_INTERLOCK_WITH_CORE                                       0:0
#define NV507A_UPDATE_INTERLOCK_WITH_CORE_DISABLE                               (0x00000000)
#define NV507A_UPDATE_INTERLOCK_WITH_CORE_ENABLE                                (0x00000001)
#define NV507A_SET_CURSOR_HOT_SPOT_POINT_OUT                                    (0x00000084)
#define NV507A_SET_CURSOR_HOT_SPOT_POINT_OUT_X                                  15:0
#define NV507A_SET_CURSOR_HOT_SPOT_POINT_OUT_Y                                  31:16
#endif // _cl507a_h
