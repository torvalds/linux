/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_PSP_TA_H__
#define __AMDGPU_PSP_TA_H__

/* Calling set_ta_context_funcs is required before using the following macros */
#define psp_fn_ta_initialize(psp) ((psp)->ta_funcs->fn_ta_initialize((psp)))
#define psp_fn_ta_invoke(psp, ta_cmd_id) ((psp)->ta_funcs->fn_ta_invoke((psp), (ta_cmd_id)))
#define psp_fn_ta_terminate(psp) ((psp)->ta_funcs->fn_ta_terminate((psp)))

void amdgpu_ta_if_debugfs_init(struct amdgpu_device *adev);

#endif
