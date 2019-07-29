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
#ifndef __AMDGPU_UMC_H__
#define __AMDGPU_UMC_H__

struct amdgpu_umc_funcs {
	void (*ras_init)(struct amdgpu_device *adev);
	void (*query_ras_error_count)(struct amdgpu_device *adev,
					void *ras_error_status);
	void (*query_ras_error_address)(struct amdgpu_device *adev,
					void *ras_error_status);
	void (*enable_umc_index_mode)(struct amdgpu_device *adev,
					uint32_t umc_instance);
	void (*disable_umc_index_mode)(struct amdgpu_device *adev);
};

struct amdgpu_umc {
	/* max error count in one ras query call */
	uint32_t max_ras_err_cnt_per_query;
	/* number of umc channel instance with memory map register access */
	uint32_t channel_inst_num;
	/* number of umc instance with memory map register access */
	uint32_t umc_inst_num;
	/* UMC regiser per channel offset */
	uint32_t channel_offs;
	/* channel index table of interleaved memory */
	const uint32_t *channel_idx_tbl;

	const struct amdgpu_umc_funcs *funcs;
};

#endif
