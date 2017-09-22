/*
 * Copyright (C) 2017 Oracle Corporation
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

#ifndef __VBOX_ERR_H__
#define __VBOX_ERR_H__

/**
 * @name VirtualBox virtual-hardware error macros
 * @{
 */

#define VINF_SUCCESS                        0
#define VERR_INVALID_PARAMETER              (-2)
#define VERR_INVALID_POINTER                (-6)
#define VERR_NO_MEMORY                      (-8)
#define VERR_NOT_IMPLEMENTED                (-12)
#define VERR_INVALID_FUNCTION               (-36)
#define VERR_NOT_SUPPORTED                  (-37)
#define VERR_TOO_MUCH_DATA                  (-42)
#define VERR_INVALID_STATE                  (-79)
#define VERR_OUT_OF_RESOURCES               (-80)
#define VERR_ALREADY_EXISTS                 (-105)
#define VERR_INTERNAL_ERROR                 (-225)

#define RT_SUCCESS_NP(rc)   ((int)(rc) >= VINF_SUCCESS)
#define RT_SUCCESS(rc)      (likely(RT_SUCCESS_NP(rc)))
#define RT_FAILURE(rc)      (unlikely(!RT_SUCCESS_NP(rc)))

/** @}  */

#endif
