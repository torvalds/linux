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

#define smnMCMP0_STATUST0 	0x03830408
#define smnMCMP1_STATUST0 	0x03b30408
#define smnMCMPIO_STATUST0 	0x0c930408


static void mca_v3_0_mp0_query_ras_error_count(struct amdgpu_device *adev,
					       void *ras_error_status)
{
	amdgpu_mca_query_ras_error_count(adev,
				         smnMCMP0_STATUST0,
				         ras_error_status);
}

static int mca_v3_0_mp0_ras_late_init(struct amdgpu_device *adev)
{
	return amdgpu_mca_ras_late_init(adev, &adev->mca.mp0);
}

static void mca_v3_0_mp0_ras_fini(struct amdgpu_device *adev)
{
	amdgpu_mca_ras_fini(adev, &adev->mca.mp0);
}

const struct amdgpu_mca_ras_funcs mca_v3_0_mp0_ras_funcs = {
	.ras_late_init = mca_v3_0_mp0_ras_late_init,
	.ras_fini = mca_v3_0_mp0_ras_fini,
	.query_ras_error_count = mca_v3_0_mp0_query_ras_error_count,
	.query_ras_error_address = NULL,
	.ras_block = AMDGPU_RAS_BLOCK__MP0,
	.sysfs_name = "mp0_err_count",
};

static void mca_v3_0_mp1_query_ras_error_count(struct amdgpu_device *adev,
					       void *ras_error_status)
{
	amdgpu_mca_query_ras_error_count(adev,
				         smnMCMP1_STATUST0,
				         ras_error_status);
}

static int mca_v3_0_mp1_ras_late_init(struct amdgpu_device *adev)
{
	return amdgpu_mca_ras_late_init(adev, &adev->mca.mp1);
}

static void mca_v3_0_mp1_ras_fini(struct amdgpu_device *adev)
{
	amdgpu_mca_ras_fini(adev, &adev->mca.mp1);
}

const struct amdgpu_mca_ras_funcs mca_v3_0_mp1_ras_funcs = {
	.ras_late_init = mca_v3_0_mp1_ras_late_init,
	.ras_fini = mca_v3_0_mp1_ras_fini,
	.query_ras_error_count = mca_v3_0_mp1_query_ras_error_count,
	.query_ras_error_address = NULL,
	.ras_block = AMDGPU_RAS_BLOCK__MP1,
	.sysfs_name = "mp1_err_count",
};

static void mca_v3_0_mpio_query_ras_error_count(struct amdgpu_device *adev,
					       void *ras_error_status)
{
	amdgpu_mca_query_ras_error_count(adev,
				         smnMCMPIO_STATUST0,
				         ras_error_status);
}

static int mca_v3_0_mpio_ras_late_init(struct amdgpu_device *adev)
{
	return amdgpu_mca_ras_late_init(adev, &adev->mca.mpio);
}

static void mca_v3_0_mpio_ras_fini(struct amdgpu_device *adev)
{
	amdgpu_mca_ras_fini(adev, &adev->mca.mpio);
}

const struct amdgpu_mca_ras_funcs mca_v3_0_mpio_ras_funcs = {
	.ras_late_init = mca_v3_0_mpio_ras_late_init,
	.ras_fini = mca_v3_0_mpio_ras_fini,
	.query_ras_error_count = mca_v3_0_mpio_query_ras_error_count,
	.query_ras_error_address = NULL,
	.ras_block = AMDGPU_RAS_BLOCK__MPIO,
	.sysfs_name = "mpio_err_count",
};


static void mca_v3_0_init(struct amdgpu_device *adev)
{
	struct amdgpu_mca *mca = &adev->mca;

	mca->mp0.ras_funcs = &mca_v3_0_mp0_ras_funcs;
	mca->mp1.ras_funcs = &mca_v3_0_mp1_ras_funcs;
	mca->mpio.ras_funcs = &mca_v3_0_mpio_ras_funcs;
}

const struct amdgpu_mca_funcs mca_v3_0_funcs = {
	.init = mca_v3_0_init,
};