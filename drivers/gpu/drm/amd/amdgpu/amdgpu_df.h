/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_DF_H__
#define __AMDGPU_DF_H__

struct amdgpu_df_hash_status {
	bool hash_64k;
	bool hash_2m;
	bool hash_1g;
};

struct amdgpu_df_funcs {
	void (*sw_init)(struct amdgpu_device *adev);
	void (*sw_fini)(struct amdgpu_device *adev);
	void (*enable_broadcast_mode)(struct amdgpu_device *adev,
				      bool enable);
	u32 (*get_fb_channel_number)(struct amdgpu_device *adev);
	u32 (*get_hbm_channel_number)(struct amdgpu_device *adev);
	void (*update_medium_grain_clock_gating)(struct amdgpu_device *adev,
						 bool enable);
	void (*get_clockgating_state)(struct amdgpu_device *adev,
				      u32 *flags);
	void (*enable_ecc_force_par_wr_rmw)(struct amdgpu_device *adev,
					    bool enable);
	int (*pmc_start)(struct amdgpu_device *adev, uint64_t config,
					 int counter_idx, int is_add);
	int (*pmc_stop)(struct amdgpu_device *adev, uint64_t config,
					 int counter_idx, int is_remove);
	void (*pmc_get_count)(struct amdgpu_device *adev, uint64_t config,
					 int counter_idx, uint64_t *count);
	uint64_t (*get_fica)(struct amdgpu_device *adev, uint32_t ficaa_val);
	void (*set_fica)(struct amdgpu_device *adev, uint32_t ficaa_val,
			 uint32_t ficadl_val, uint32_t ficadh_val);
	bool (*query_ras_poison_mode)(struct amdgpu_device *adev);
};

struct amdgpu_df {
	struct amdgpu_df_hash_status	hash_status;
	const struct amdgpu_df_funcs	*funcs;
};

#endif /* __AMDGPU_DF_H__ */
