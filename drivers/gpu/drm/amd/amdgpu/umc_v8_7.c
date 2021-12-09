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
#include "umc_v8_7.h"
#include "amdgpu_ras.h"
#include "amdgpu_umc.h"
#include "amdgpu.h"

#include "rsmu/rsmu_0_0_2_offset.h"
#include "rsmu/rsmu_0_0_2_sh_mask.h"
#include "umc/umc_8_7_0_offset.h"
#include "umc/umc_8_7_0_sh_mask.h"

#define UMC_8_INST_DIST			0x40000

const uint32_t
	umc_v8_7_channel_idx_tbl[UMC_V8_7_UMC_INSTANCE_NUM][UMC_V8_7_CHANNEL_INSTANCE_NUM] = {
		{2, 11},  {4, 13},
		{1, 8},   {7, 14},
		{10, 3},  {12, 5},
		{9, 0},   {15, 6}
};

static inline uint32_t get_umc_8_reg_offset(struct amdgpu_device *adev,
					    uint32_t umc_inst,
					    uint32_t ch_inst)
{
	return adev->umc.channel_offs*ch_inst + UMC_8_INST_DIST*umc_inst;
}

static void umc_v8_7_clear_error_count_per_channel(struct amdgpu_device *adev,
					uint32_t umc_reg_offset)
{
	uint32_t ecc_err_cnt_addr;
	uint32_t ecc_err_cnt_sel, ecc_err_cnt_sel_addr;

	ecc_err_cnt_sel_addr =
		SOC15_REG_OFFSET(UMC, 0, mmUMCCH0_0_GeccErrCntSel);
	ecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, mmUMCCH0_0_GeccErrCnt);

	/* select the lower chip */
	ecc_err_cnt_sel = RREG32_PCIE((ecc_err_cnt_sel_addr +
					umc_reg_offset) * 4);
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel,
					UMCCH0_0_GeccErrCntSel,
					GeccErrCntCsSel, 0);
	WREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4,
			ecc_err_cnt_sel);

	/* clear lower chip error count */
	WREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4,
			UMC_V8_7_CE_CNT_INIT);

	/* select the higher chip */
	ecc_err_cnt_sel = RREG32_PCIE((ecc_err_cnt_sel_addr +
					umc_reg_offset) * 4);
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel,
					UMCCH0_0_GeccErrCntSel,
					GeccErrCntCsSel, 1);
	WREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4,
			ecc_err_cnt_sel);

	/* clear higher chip error count */
	WREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4,
			UMC_V8_7_CE_CNT_INIT);
}

static void umc_v8_7_clear_error_count(struct amdgpu_device *adev)
{
	uint32_t umc_inst        = 0;
	uint32_t ch_inst         = 0;
	uint32_t umc_reg_offset  = 0;

	LOOP_UMC_INST_AND_CH(umc_inst, ch_inst) {
		umc_reg_offset = get_umc_8_reg_offset(adev,
						umc_inst,
						ch_inst);

		umc_v8_7_clear_error_count_per_channel(adev,
						umc_reg_offset);
	}
}

static void umc_v8_7_query_correctable_error_count(struct amdgpu_device *adev,
						   uint32_t umc_reg_offset,
						   unsigned long *error_count)
{
	uint32_t ecc_err_cnt_sel, ecc_err_cnt_sel_addr;
	uint32_t ecc_err_cnt, ecc_err_cnt_addr;
	uint64_t mc_umc_status;
	uint32_t mc_umc_status_addr;

	/* UMC 8_7_2 registers */
	ecc_err_cnt_sel_addr =
		SOC15_REG_OFFSET(UMC, 0, mmUMCCH0_0_GeccErrCntSel);
	ecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, mmUMCCH0_0_GeccErrCnt);
	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, mmMCA_UMC_UMC0_MCUMC_STATUST0);

	/* select the lower chip and check the error count */
	ecc_err_cnt_sel = RREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4);
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_0_GeccErrCntSel,
					GeccErrCntCsSel, 0);
	WREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4, ecc_err_cnt_sel);

	ecc_err_cnt = RREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4);
	*error_count +=
		(REG_GET_FIELD(ecc_err_cnt, UMCCH0_0_GeccErrCnt, GeccErrCnt) -
		 UMC_V8_7_CE_CNT_INIT);

	/* select the higher chip and check the err counter */
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_0_GeccErrCntSel,
					GeccErrCntCsSel, 1);
	WREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4, ecc_err_cnt_sel);

	ecc_err_cnt = RREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4);
	*error_count +=
		(REG_GET_FIELD(ecc_err_cnt, UMCCH0_0_GeccErrCnt, GeccErrCnt) -
		 UMC_V8_7_CE_CNT_INIT);

	/* check for SRAM correctable error
	  MCUMC_STATUS is a 64 bit register */
	mc_umc_status = RREG64_PCIE((mc_umc_status_addr + umc_reg_offset) * 4);
	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, ErrorCodeExt) == 6 &&
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1)
		*error_count += 1;
}

static void umc_v8_7_querry_uncorrectable_error_count(struct amdgpu_device *adev,
						      uint32_t umc_reg_offset,
						      unsigned long *error_count)
{
	uint64_t mc_umc_status;
	uint32_t mc_umc_status_addr;

	mc_umc_status_addr = SOC15_REG_OFFSET(UMC, 0, mmMCA_UMC_UMC0_MCUMC_STATUST0);

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

static void umc_v8_7_query_ras_error_count(struct amdgpu_device *adev,
					   void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;

	uint32_t umc_inst        = 0;
	uint32_t ch_inst         = 0;
	uint32_t umc_reg_offset  = 0;

	LOOP_UMC_INST_AND_CH(umc_inst, ch_inst) {
		umc_reg_offset = get_umc_8_reg_offset(adev,
						      umc_inst,
						      ch_inst);

		umc_v8_7_query_correctable_error_count(adev,
						       umc_reg_offset,
						       &(err_data->ce_count));
		umc_v8_7_querry_uncorrectable_error_count(adev,
							  umc_reg_offset,
							  &(err_data->ue_count));
	}

	umc_v8_7_clear_error_count(adev);
}

static void umc_v8_7_query_error_address(struct amdgpu_device *adev,
					 struct ras_err_data *err_data,
					 uint32_t umc_reg_offset,
					 uint32_t ch_inst,
					 uint32_t umc_inst)
{
	uint32_t lsb, mc_umc_status_addr;
	uint64_t mc_umc_status, err_addr, retired_page, mc_umc_addrt0;
	struct eeprom_table_record *err_rec;
	uint32_t channel_index = adev->umc.channel_idx_tbl[umc_inst * adev->umc.channel_inst_num + ch_inst];

	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, mmMCA_UMC_UMC0_MCUMC_STATUST0);
	mc_umc_addrt0 =
		SOC15_REG_OFFSET(UMC, 0, mmMCA_UMC_UMC0_MCUMC_ADDRT0);

	mc_umc_status = RREG64_PCIE((mc_umc_status_addr + umc_reg_offset) * 4);

	if (mc_umc_status == 0)
		return;

	if (!err_data->err_addr) {
		/* clear umc status */
		WREG64_PCIE((mc_umc_status_addr + umc_reg_offset) * 4, 0x0ULL);
		return;
	}

	err_rec = &err_data->err_addr[err_data->err_addr_cnt];

	/* calculate error address if ue/ce error is detected */
	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1)) {

		err_addr = RREG64_PCIE((mc_umc_addrt0 + umc_reg_offset) * 4);
		/* the lowest lsb bits should be ignored */
		lsb = REG_GET_FIELD(err_addr, MCA_UMC_UMC0_MCUMC_ADDRT0, LSB);
		err_addr = REG_GET_FIELD(err_addr, MCA_UMC_UMC0_MCUMC_ADDRT0, ErrorAddr);
		err_addr &= ~((0x1ULL << lsb) - 1);

		/* translate umc channel address to soc pa, 3 parts are included */
		retired_page = ADDR_OF_4KB_BLOCK(err_addr) |
				ADDR_OF_256B_BLOCK(channel_index) |
				OFFSET_IN_256B_BLOCK(err_addr);

		/* we only save ue error information currently, ce is skipped */
		if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC)
				== 1) {
			err_rec->address = err_addr;
			/* page frame address is saved */
			err_rec->retired_page = retired_page >> AMDGPU_GPU_PAGE_SHIFT;
			err_rec->ts = (uint64_t)ktime_get_real_seconds();
			err_rec->err_type = AMDGPU_RAS_EEPROM_ERR_NON_RECOVERABLE;
			err_rec->cu = 0;
			err_rec->mem_channel = channel_index;
			err_rec->mcumc_id = umc_inst;

			err_data->err_addr_cnt++;
		}
	}

	/* clear umc status */
	WREG64_PCIE((mc_umc_status_addr + umc_reg_offset) * 4, 0x0ULL);
}

static void umc_v8_7_query_ras_error_address(struct amdgpu_device *adev,
					     void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;

	uint32_t umc_inst        = 0;
	uint32_t ch_inst         = 0;
	uint32_t umc_reg_offset  = 0;

	LOOP_UMC_INST_AND_CH(umc_inst, ch_inst) {
		umc_reg_offset = get_umc_8_reg_offset(adev,
						      umc_inst,
						      ch_inst);

		umc_v8_7_query_error_address(adev,
					     err_data,
					     umc_reg_offset,
					     ch_inst,
					     umc_inst);
	}
}

static void umc_v8_7_err_cnt_init_per_channel(struct amdgpu_device *adev,
					      uint32_t umc_reg_offset)
{
	uint32_t ecc_err_cnt_sel, ecc_err_cnt_sel_addr;
	uint32_t ecc_err_cnt_addr;

	ecc_err_cnt_sel_addr =
		SOC15_REG_OFFSET(UMC, 0, mmUMCCH0_0_GeccErrCntSel);
	ecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, mmUMCCH0_0_GeccErrCnt);

	/* select the lower chip and check the error count */
	ecc_err_cnt_sel = RREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4);
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_0_GeccErrCntSel,
					GeccErrCntCsSel, 0);
	/* set ce error interrupt type to APIC based interrupt */
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_0_GeccErrCntSel,
					GeccErrInt, 0x1);
	WREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4, ecc_err_cnt_sel);
	/* set error count to initial value */
	WREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4, UMC_V8_7_CE_CNT_INIT);

	/* select the higher chip and check the err counter */
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_0_GeccErrCntSel,
					GeccErrCntCsSel, 1);
	WREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4, ecc_err_cnt_sel);
	WREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4, UMC_V8_7_CE_CNT_INIT);
}

static void umc_v8_7_err_cnt_init(struct amdgpu_device *adev)
{
	uint32_t umc_inst        = 0;
	uint32_t ch_inst         = 0;
	uint32_t umc_reg_offset  = 0;

	LOOP_UMC_INST_AND_CH(umc_inst, ch_inst) {
		umc_reg_offset = get_umc_8_reg_offset(adev,
						      umc_inst,
						      ch_inst);

		umc_v8_7_err_cnt_init_per_channel(adev, umc_reg_offset);
	}
}

const struct amdgpu_umc_ras_funcs umc_v8_7_ras_funcs = {
	.err_cnt_init = umc_v8_7_err_cnt_init,
	.ras_late_init = amdgpu_umc_ras_late_init,
	.ras_fini = amdgpu_umc_ras_fini,
	.query_ras_error_count = umc_v8_7_query_ras_error_count,
	.query_ras_error_address = umc_v8_7_query_ras_error_address,
};
