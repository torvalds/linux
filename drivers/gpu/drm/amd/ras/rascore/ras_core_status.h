/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#ifndef __RAS_CORE_STATUS_H__
#define __RAS_CORE_STATUS_H__

#define RAS_CORE_OK                       0
#define RAS_CORE_NOT_SUPPORTED            248
#define RAS_CORE_FAIL_ERROR_QUERY         249
#define RAS_CORE_FAIL_ERROR_INJECTION     250
#define RAS_CORE_FAIL_FATAL_RECOVERY      251
#define RAS_CORE_FAIL_POISON_CONSUMPTION  252
#define RAS_CORE_FAIL_POISON_CREATION     253
#define RAS_CORE_FAIL_NO_VALID_BANKS      254
#define RAS_CORE_GPU_IN_MODE1_RESET       255
#endif
