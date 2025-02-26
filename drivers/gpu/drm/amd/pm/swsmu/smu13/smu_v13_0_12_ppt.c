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

#define SWSMU_CODE_LAYER_L2

#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_v13_0_12_pmfw.h"
#include "smu_v13_0_6_ppt.h"
#include "smu_v13_0.h"
#include "amdgpu_xgmi.h"
#include <linux/pci.h>
#include "smu_cmn.h"

#undef MP1_Public
#undef smnMP1_FIRMWARE_FLAGS

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

#define SMU_13_0_12_FEA_MAP(smu_feature, smu_13_0_12_feature)                    \
	[smu_feature] = { 1, (smu_13_0_12_feature) }

#define FEATURE_MASK(feature) (1ULL << feature)
#define SMC_DPM_FEATURE                                                        \
	(FEATURE_MASK(FEATURE_DATA_CALCULATION) |                              \
	 FEATURE_MASK(FEATURE_DPM_GFXCLK) | FEATURE_MASK(FEATURE_DPM_FCLK))

const struct cmn2asic_mapping smu_v13_0_12_feature_mask_map[SMU_FEATURE_COUNT] = {
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DATA_CALCULATIONS_BIT, 		FEATURE_DATA_CALCULATION),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DPM_GFXCLK_BIT, 		FEATURE_DPM_GFXCLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DPM_FCLK_BIT, 			FEATURE_DPM_FCLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DS_GFXCLK_BIT, 			FEATURE_DS_GFXCLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DS_SOCCLK_BIT, 			FEATURE_DS_SOCCLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DS_LCLK_BIT, 			FEATURE_DS_LCLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DS_FCLK_BIT, 			FEATURE_DS_FCLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_PPT_BIT, 			FEATURE_PPT),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_TDC_BIT, 			FEATURE_TDC),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_APCC_DFLL_BIT, 			FEATURE_APCC_DFLL),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_MP1_CG_BIT, 			FEATURE_SMU_CG),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_FW_CTF_BIT, 			FEATURE_FW_CTF),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_THERMAL_BIT, 			FEATURE_THERMAL),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_SOC_PCC_BIT, 			FEATURE_SOC_PCC),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_XGMI_PER_LINK_PWR_DWN_BIT,	FEATURE_XGMI_PER_LINK_PWR_DOWN),
};

static int smu_v13_0_12_get_enabled_mask(struct smu_context *smu,
					 uint64_t *feature_mask)
{
	int ret;

	ret = smu_cmn_get_enabled_mask(smu, feature_mask);

	if (ret == -EIO) {
		*feature_mask = 0;
		ret = 0;
	}

	return ret;
}

bool smu_v13_0_12_is_dpm_running(struct smu_context *smu)
{
	int ret;
	uint64_t feature_enabled;

	ret = smu_v13_0_12_get_enabled_mask(smu, &feature_enabled);

	if (ret)
		return false;

	return !!(feature_enabled & SMC_DPM_FEATURE);
}
