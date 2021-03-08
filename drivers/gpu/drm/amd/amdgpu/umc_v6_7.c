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
#include "umc_v6_7.h"
#include "amdgpu_ras.h"
#include "amdgpu.h"

#include "umc/umc_6_7_0_offset.h"
#include "umc/umc_6_7_0_sh_mask.h"

static void umc_v6_7_query_correctable_error_count(struct amdgpu_device *adev,
						   uint32_t umc_reg_offset,
						   unsigned long *error_count)
{
	uint32_t ecc_err_cnt_sel, ecc_err_cnt_sel_addr;
	uint32_t ecc_err_cnt, ecc_err_cnt_addr;
	uint64_t mc_umc_status;
	uint32_t mc_umc_status_addr;

	/* UMC 6_1_1 registers */
	ecc_err_cnt_sel_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_0_EccErrCntSel);
	ecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_0_EccErrCnt);
	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_STATUST0);

	/* select the lower chip and check the error count */
	ecc_err_cnt_sel = RREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4);
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_0_EccErrCntSel,
					EccErrCntCsSel, 0);
	WREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4, ecc_err_cnt_sel);

	ecc_err_cnt = RREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4);
	*error_count +=
		(REG_GET_FIELD(ecc_err_cnt, UMCCH0_0_EccErrCnt, EccErrCnt) -
		 UMC_V6_7_CE_CNT_INIT);

	/* select the higher chip and check the err counter */
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_0_EccErrCntSel,
					EccErrCntCsSel, 1);
	WREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4, ecc_err_cnt_sel);

	ecc_err_cnt = RREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4);
	*error_count +=
		(REG_GET_FIELD(ecc_err_cnt, UMCCH0_0_EccErrCnt, EccErrCnt) -
		 UMC_V6_7_CE_CNT_INIT);

	/* check for SRAM correctable error
	  MCUMC_STATUS is a 64 bit register */
	mc_umc_status = RREG64_PCIE((mc_umc_status_addr + umc_reg_offset) * 4);
	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1)
		*error_count += 1;
}

static void umc_v6_7_querry_uncorrectable_error_count(struct amdgpu_device *adev,
						      uint32_t umc_reg_offset,
						      unsigned long *error_count)
{
	uint64_t mc_umc_status;
	uint32_t mc_umc_status_addr;

	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_STATUST0);

	/* check the MCUMC_STATUS */
	mc_umc_status = RREG64_PCIE((mc_umc_status_addr + umc_reg_offset) * 4);
	if ((REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1) &&
	    (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Deferred) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, PCC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, TCC) == 1))
		*error_count += 1;
}

const struct amdgpu_umc_funcs umc_v6_7_funcs = {
	.ras_late_init = amdgpu_umc_ras_late_init,
};
