/*
 * Copyright 2024 Advanced Micro Devices, Inc.
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
#include "umc_v8_14.h"
#include "amdgpu_ras.h"
#include "amdgpu_umc.h"
#include "amdgpu.h"
#include "umc/umc_8_14_0_offset.h"
#include "umc/umc_8_14_0_sh_mask.h"

static inline uint32_t get_umc_v8_14_reg_offset(struct amdgpu_device *adev,
					    uint32_t umc_inst,
					    uint32_t ch_inst)
{
	return adev->umc.channel_offs * ch_inst + UMC_V8_14_INST_DIST * umc_inst;
}

static int umc_v8_14_clear_error_count_per_channel(struct amdgpu_device *adev,
					uint32_t node_inst, uint32_t umc_inst,
					uint32_t ch_inst, void *data)
{
	uint32_t ecc_err_cnt_addr;
	uint32_t umc_reg_offset =
		get_umc_v8_14_reg_offset(adev, umc_inst, ch_inst);

	ecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_GeccErrCnt);

	/* clear error count */
	WREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4,
			UMC_V8_14_CE_CNT_INIT);

	return 0;
}

static void umc_v8_14_clear_error_count(struct amdgpu_device *adev)
{
	amdgpu_umc_loop_channels(adev,
		umc_v8_14_clear_error_count_per_channel, NULL);
}

static void umc_v8_14_query_correctable_error_count(struct amdgpu_device *adev,
						   uint32_t umc_reg_offset,
						   unsigned long *error_count)
{
	uint32_t ecc_err_cnt, ecc_err_cnt_addr;

	/* UMC 8_14 registers */
	ecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_GeccErrCnt);

	ecc_err_cnt = RREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4);
	*error_count +=
		(REG_GET_FIELD(ecc_err_cnt, UMCCH0_GeccErrCnt, GeccErrCnt) -
		 UMC_V8_14_CE_CNT_INIT);
}

static void umc_v8_14_query_uncorrectable_error_count(struct amdgpu_device *adev,
						      uint32_t umc_reg_offset,
						      unsigned long *error_count)
{
	uint32_t ecc_err_cnt, ecc_err_cnt_addr;
	/* UMC 8_14 registers */
	ecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_GeccErrCnt);

	ecc_err_cnt = RREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4);
	*error_count +=
		(REG_GET_FIELD(ecc_err_cnt, UMCCH0_GeccErrCnt, GeccUnCorrErrCnt) -
		 UMC_V8_14_CE_CNT_INIT);
}

static int umc_v8_14_query_error_count_per_channel(struct amdgpu_device *adev,
					uint32_t node_inst, uint32_t umc_inst,
					uint32_t ch_inst, void *data)
{
	struct ras_err_data *err_data = (struct ras_err_data *)data;
	uint32_t umc_reg_offset =
		get_umc_v8_14_reg_offset(adev, umc_inst, ch_inst);

	umc_v8_14_query_correctable_error_count(adev,
					umc_reg_offset,
					&(err_data->ce_count));
	umc_v8_14_query_uncorrectable_error_count(adev,
					umc_reg_offset,
					&(err_data->ue_count));

	return 0;
}

static void umc_v8_14_query_ras_error_count(struct amdgpu_device *adev,
					   void *ras_error_status)
{
	amdgpu_umc_loop_channels(adev,
		umc_v8_14_query_error_count_per_channel, ras_error_status);

	umc_v8_14_clear_error_count(adev);
}

static int umc_v8_14_err_cnt_init_per_channel(struct amdgpu_device *adev,
					uint32_t node_inst, uint32_t umc_inst,
					uint32_t ch_inst, void *data)
{
	uint32_t ecc_err_cnt_sel, ecc_err_cnt_sel_addr;
	uint32_t ecc_err_cnt_addr;
	uint32_t umc_reg_offset =
		get_umc_v8_14_reg_offset(adev, umc_inst, ch_inst);

	ecc_err_cnt_sel_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_GeccErrCntSel);
	ecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_GeccErrCnt);

	ecc_err_cnt_sel = RREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4);

	/* set ce error interrupt type to APIC based interrupt */
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_GeccErrCntSel,
					GeccErrInt, 0x1);
	WREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4, ecc_err_cnt_sel);
	/* set error count to initial value */
	WREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4, UMC_V8_14_CE_CNT_INIT);

	return 0;
}

static void umc_v8_14_err_cnt_init(struct amdgpu_device *adev)
{
	amdgpu_umc_loop_channels(adev,
		umc_v8_14_err_cnt_init_per_channel, NULL);
}

const struct amdgpu_ras_block_hw_ops umc_v8_14_ras_hw_ops = {
	.query_ras_error_count = umc_v8_14_query_ras_error_count,
};

struct amdgpu_umc_ras umc_v8_14_ras = {
	.ras_block = {
		.hw_ops = &umc_v8_14_ras_hw_ops,
	},
	.err_cnt_init = umc_v8_14_err_cnt_init,
};
