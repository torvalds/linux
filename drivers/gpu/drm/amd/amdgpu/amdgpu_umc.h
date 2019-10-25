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

/* implement 64 bits REG operations via 32 bits interface */
#define RREG64_UMC(reg)	(RREG32(reg) | \
				((uint64_t)RREG32((reg) + 1) << 32))
#define WREG64_UMC(reg, v)	\
	do {	\
		WREG32((reg), lower_32_bits(v));	\
		WREG32((reg) + 1, upper_32_bits(v));	\
	} while (0)

/*
 * void (*func)(struct amdgpu_device *adev, struct ras_err_data *err_data,
 *				uint32_t umc_reg_offset, uint32_t channel_index)
 */
#define amdgpu_umc_for_each_channel(func)	\
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;	\
	uint32_t umc_inst, channel_inst, umc_reg_offset, channel_index;	\
	for (umc_inst = 0; umc_inst < adev->umc.umc_inst_num; umc_inst++) {	\
		/* enable the index mode to query eror count per channel */	\
		adev->umc.funcs->enable_umc_index_mode(adev, umc_inst);	\
		for (channel_inst = 0;	\
			channel_inst < adev->umc.channel_inst_num;	\
			channel_inst++) {	\
			/* calc the register offset according to channel instance */	\
			umc_reg_offset = adev->umc.channel_offs * channel_inst;	\
			/* get channel index of interleaved memory */	\
			channel_index = adev->umc.channel_idx_tbl[	\
				umc_inst * adev->umc.channel_inst_num + channel_inst];	\
			(func)(adev, err_data, umc_reg_offset, channel_index);	\
		}	\
	}	\
	adev->umc.funcs->disable_umc_index_mode(adev);

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
