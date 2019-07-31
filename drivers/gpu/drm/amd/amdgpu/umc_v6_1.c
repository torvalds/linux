/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
#include "umc_v6_1.h"
#include "amdgpu_ras.h"
#include "amdgpu.h"

#include "rsmu/rsmu_0_0_2_offset.h"
#include "rsmu/rsmu_0_0_2_sh_mask.h"
#include "umc/umc_6_1_1_offset.h"
#include "umc/umc_6_1_1_sh_mask.h"

static void umc_v6_1_enable_umc_index_mode(struct amdgpu_device *adev,
					   uint32_t umc_instance)
{
	uint32_t rsmu_umc_index;

	rsmu_umc_index = RREG32_SOC15(RSMU, 0,
			mmRSMU_UMC_INDEX_REGISTER_NBIF_VG20_GPU);
	rsmu_umc_index = REG_SET_FIELD(rsmu_umc_index,
			RSMU_UMC_INDEX_REGISTER_NBIF_VG20_GPU,
			RSMU_UMC_INDEX_MODE_EN, 1);
	rsmu_umc_index = REG_SET_FIELD(rsmu_umc_index,
			RSMU_UMC_INDEX_REGISTER_NBIF_VG20_GPU,
			RSMU_UMC_INDEX_INSTANCE, umc_instance);
	rsmu_umc_index = REG_SET_FIELD(rsmu_umc_index,
			RSMU_UMC_INDEX_REGISTER_NBIF_VG20_GPU,
			RSMU_UMC_INDEX_WREN, 1 << umc_instance);
	WREG32_SOC15(RSMU, 0, mmRSMU_UMC_INDEX_REGISTER_NBIF_VG20_GPU,
				rsmu_umc_index);
}

static void umc_v6_1_disable_umc_index_mode(struct amdgpu_device *adev)
{
	WREG32_FIELD15(RSMU, 0, RSMU_UMC_INDEX_REGISTER_NBIF_VG20_GPU,
			RSMU_UMC_INDEX_MODE_EN, 0);
}

static void umc_v6_1_query_correctable_error_count(struct amdgpu_device *adev,
						   uint32_t umc_reg_offset,
						   unsigned long *error_count)
{
	uint32_t ecc_err_cnt_sel, ecc_err_cnt_sel_addr;
	uint32_t ecc_err_cnt, ecc_err_cnt_addr;
	uint64_t mc_umc_status;
	uint32_t mc_umc_status_addr;

	ecc_err_cnt_sel_addr =
		SOC15_REG_OFFSET(UMC, 0, mmUMCCH0_0_EccErrCntSel);
	ecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, mmUMCCH0_0_EccErrCnt);
	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, mmMCA_UMC_UMC0_MCUMC_STATUST0);

	/* select the lower chip and check the error count */
	ecc_err_cnt_sel = RREG32(ecc_err_cnt_sel_addr + umc_reg_offset);
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_0_EccErrCntSel,
					EccErrCntCsSel, 0);
	WREG32(ecc_err_cnt_sel_addr + umc_reg_offset, ecc_err_cnt_sel);
	ecc_err_cnt = RREG32(ecc_err_cnt_addr + umc_reg_offset);
	*error_count +=
		REG_GET_FIELD(ecc_err_cnt, UMCCH0_0_EccErrCnt, EccErrCnt);
	/* clear the lower chip err count */
	WREG32(ecc_err_cnt_addr + umc_reg_offset, 0);

	/* select the higher chip and check the err counter */
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_0_EccErrCntSel,
					EccErrCntCsSel, 1);
	WREG32(ecc_err_cnt_sel_addr + umc_reg_offset, ecc_err_cnt_sel);
	ecc_err_cnt = RREG32(ecc_err_cnt_addr + umc_reg_offset);
	*error_count +=
		REG_GET_FIELD(ecc_err_cnt, UMCCH0_0_EccErrCnt, EccErrCnt);
	/* clear the higher chip err count */
	WREG32(ecc_err_cnt_addr + umc_reg_offset, 0);

	/* check for SRAM correctable error
	  MCUMC_STATUS is a 64 bit register */
	mc_umc_status =
		RREG32(mc_umc_status_addr + umc_reg_offset);
	mc_umc_status |=
		(uint64_t)RREG32(mc_umc_status_addr + umc_reg_offset + 1) << 32;
	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, ErrorCodeExt) == 6 &&
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1)
		*error_count += 1;

	/* clear the MCUMC_STATUS */
	WREG32(mc_umc_status_addr + umc_reg_offset, 0);
	WREG32(mc_umc_status_addr + umc_reg_offset + 1, 0);
}

static void umc_v6_1_querry_uncorrectable_error_count(struct amdgpu_device *adev,
						      uint32_t umc_reg_offset,
						      unsigned long *error_count)
{
	uint64_t mc_umc_status;
	uint32_t mc_umc_status_addr;

	mc_umc_status_addr =
                SOC15_REG_OFFSET(UMC, 0, mmMCA_UMC_UMC0_MCUMC_STATUST0);

	/* check the MCUMC_STATUS */
	mc_umc_status = RREG32(mc_umc_status_addr + umc_reg_offset);
	mc_umc_status |=
		(uint64_t)RREG32(mc_umc_status_addr + umc_reg_offset + 1) << 32;

	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, ErrorCodeExt) == 6 &&
		(REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1 ||
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, PCC) == 1 ||
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UC) == 1 ||
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, TCC) == 1))
		*error_count += 1;

	/* clear the MCUMC_STATUS */
	WREG32(mc_umc_status_addr + umc_reg_offset, 0);
	WREG32(mc_umc_status_addr + umc_reg_offset + 1, 0);
}

static void umc_v6_1_query_ras_error_count(struct amdgpu_device *adev,
					   void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;
	uint32_t umc_inst, channel_inst, umc_reg_offset;

	for (umc_inst = 0; umc_inst < UMC_V6_1_UMC_INSTANCE_NUM; umc_inst++) {
		/* enable the index mode to query eror count per channel */
		umc_v6_1_enable_umc_index_mode(adev, umc_inst);
		for (channel_inst = 0; channel_inst < UMC_V6_1_CHANNEL_INSTANCE_NUM; channel_inst++) {
			/* calc the register offset according to channel instance */
			umc_reg_offset = UMC_V6_1_PER_CHANNEL_OFFSET * channel_inst;
			umc_v6_1_query_correctable_error_count(adev, umc_reg_offset,
							       &(err_data->ce_count));
			umc_v6_1_querry_uncorrectable_error_count(adev, umc_reg_offset,
								  &(err_data->ue_count));
		}
	}
	umc_v6_1_disable_umc_index_mode(adev);
}

const struct amdgpu_umc_funcs umc_v6_1_funcs = {
	.query_ras_error_count = umc_v6_1_query_ras_error_count,
};
