/* cik.h -- Private header for radeon driver -*- linux-c -*-
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
 */

#ifndef __CIK_H__
#define __CIK_H__

struct radeon_device;

void cik_enter_rlc_safe_mode(struct radeon_device *rdev);
void cik_exit_rlc_safe_mode(struct radeon_device *rdev);
int ci_mc_load_microcode(struct radeon_device *rdev);
void cik_update_cg(struct radeon_device *rdev, u32 block, bool enable);
u32 cik_gpu_check_soft_reset(struct radeon_device *rdev);
void cik_init_cp_pg_table(struct radeon_device *rdev);
u32 cik_get_csb_size(struct radeon_device *rdev);
void cik_get_csb_buffer(struct radeon_device *rdev, volatile u32 *buffer);

int cik_sdma_resume(struct radeon_device *rdev);
void cik_sdma_enable(struct radeon_device *rdev, bool enable);
void cik_sdma_fini(struct radeon_device *rdev);
#endif                         /* __CIK_H__ */
