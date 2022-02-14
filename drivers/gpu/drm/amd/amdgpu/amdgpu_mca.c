/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "amdgpu_ras.h"
#include "amdgpu.h"
#include "amdgpu_mca.h"

#include "umc/umc_6_7_0_offset.h"
#include "umc/umc_6_7_0_sh_mask.h"

void amdgpu_mca_query_correctable_error_count(struct amdgpu_device *adev,
					      uint64_t mc_status_addr,
					      unsigned long *error_count)
{
	uint64_t mc_status = RREG64_PCIE(mc_status_addr);

	if (REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1)
		*error_count += 1;
}

void amdgpu_mca_query_uncorrectable_error_count(struct amdgpu_device *adev,
						uint64_t mc_status_addr,
						unsigned long *error_count)
{
	uint64_t mc_status = RREG64_PCIE(mc_status_addr);

	if ((REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1) &&
	    (REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Deferred) == 1 ||
	    REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1 ||
	    REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, PCC) == 1 ||
	    REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UC) == 1 ||
	    REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, TCC) == 1))
		*error_count += 1;
}

void amdgpu_mca_reset_error_count(struct amdgpu_device *adev,
				  uint64_t mc_status_addr)
{
	WREG64_PCIE(mc_status_addr, 0x0ULL);
}

void amdgpu_mca_query_ras_error_count(struct amdgpu_device *adev,
				      uint64_t mc_status_addr,
				      void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;

	amdgpu_mca_query_correctable_error_count(adev, mc_status_addr, &(err_data->ce_count));
	amdgpu_mca_query_uncorrectable_error_count(adev, mc_status_addr, &(err_data->ue_count));

	amdgpu_mca_reset_error_count(adev, mc_status_addr);
}

int amdgpu_mca_ras_late_init(struct amdgpu_device *adev,
			     struct amdgpu_mca_ras *mca_dev)
{
	char sysfs_name[32] = {0};
	int r;
	struct ras_ih_if ih_info = {
		.cb = NULL,
	};
	struct ras_fs_if fs_info= {
		.sysfs_name = sysfs_name,
	};

	snprintf(sysfs_name, sizeof(sysfs_name), "%s_err_count", mca_dev->ras->ras_block.name);

	if (!mca_dev->ras_if) {
		mca_dev->ras_if = kmalloc(sizeof(struct ras_common_if), GFP_KERNEL);
		if (!mca_dev->ras_if)
			return -ENOMEM;
		mca_dev->ras_if->block = mca_dev->ras->ras_block.block;
		mca_dev->ras_if->sub_block_index = mca_dev->ras->ras_block.sub_block_index;
		mca_dev->ras_if->type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
	}
	ih_info.head = fs_info.head = *mca_dev->ras_if;
	r = amdgpu_ras_late_init(adev, mca_dev->ras_if,
				 &fs_info, &ih_info);
	if (r || !amdgpu_ras_is_supported(adev, mca_dev->ras_if->block)) {
		kfree(mca_dev->ras_if);
		mca_dev->ras_if = NULL;
	}

	return r;
}

void amdgpu_mca_ras_fini(struct amdgpu_device *adev,
			 struct amdgpu_mca_ras *mca_dev)
{
	struct ras_ih_if ih_info = {
		.cb = NULL,
	};

	if (!mca_dev->ras_if)
		return;

	amdgpu_ras_late_fini(adev, mca_dev->ras_if, &ih_info);
	kfree(mca_dev->ras_if);
	mca_dev->ras_if = NULL;
}