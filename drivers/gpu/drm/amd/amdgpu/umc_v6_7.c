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
#include "amdgpu_umc.h"
#include "amdgpu.h"

#include "umc/umc_6_7_0_offset.h"
#include "umc/umc_6_7_0_sh_mask.h"

const uint32_t
	umc_v6_7_channel_idx_tbl_second[UMC_V6_7_UMC_INSTANCE_NUM][UMC_V6_7_CHANNEL_INSTANCE_NUM] = {
		{28, 20, 24, 16, 12, 4, 8, 0},
		{6, 30, 2, 26, 22, 14, 18, 10},
		{19, 11, 15, 7, 3, 27, 31, 23},
		{9, 1, 5, 29, 25, 17, 21, 13}
};
const uint32_t
	umc_v6_7_channel_idx_tbl_first[UMC_V6_7_UMC_INSTANCE_NUM][UMC_V6_7_CHANNEL_INSTANCE_NUM] = {
		{19, 11, 15, 7,	3, 27, 31, 23},
		{9, 1, 5, 29, 25, 17, 21, 13},
		{28, 20, 24, 16, 12, 4, 8, 0},
		{6, 30, 2, 26, 22, 14, 18, 10},
};

static inline uint32_t get_umc_v6_7_reg_offset(struct amdgpu_device *adev,
					      uint32_t umc_inst,
					      uint32_t ch_inst)
{
	uint32_t index = umc_inst * adev->umc.channel_inst_num + ch_inst;

	/* adjust umc and channel index offset,
	 * the register address is not linear on each umc instace */
	umc_inst = index / 4;
	ch_inst = index % 4;

	return adev->umc.channel_offs * ch_inst + UMC_V6_7_INST_DIST * umc_inst;
}

static inline uint32_t get_umc_v6_7_channel_index(struct amdgpu_device *adev,
					      uint32_t umc_inst,
					      uint32_t ch_inst)
{
	return adev->umc.channel_idx_tbl[umc_inst * adev->umc.channel_inst_num + ch_inst];
}

static void umc_v6_7_query_error_status_helper(struct amdgpu_device *adev,
						  uint64_t mc_umc_status, uint32_t umc_reg_offset)
{
	uint32_t mc_umc_addr;
	uint64_t reg_value;

	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Deferred) == 1)
		dev_info(adev->dev, "Deferred error, no user action is needed.\n");

	if (mc_umc_status)
		dev_info(adev->dev, "MCA STATUS 0x%llx, umc_reg_offset 0x%x\n", mc_umc_status, umc_reg_offset);

	/* print IPID registers value */
	mc_umc_addr =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_IPIDT0);
	reg_value = RREG64_PCIE((mc_umc_addr + umc_reg_offset) * 4);
	if (reg_value)
		dev_info(adev->dev, "MCA IPID 0x%llx, umc_reg_offset 0x%x\n", reg_value, umc_reg_offset);

	/* print SYND registers value */
	mc_umc_addr =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_SYNDT0);
	reg_value = RREG64_PCIE((mc_umc_addr + umc_reg_offset) * 4);
	if (reg_value)
		dev_info(adev->dev, "MCA SYND 0x%llx, umc_reg_offset 0x%x\n", reg_value, umc_reg_offset);

	/* print MISC0 registers value */
	mc_umc_addr =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_MISC0T0);
	reg_value = RREG64_PCIE((mc_umc_addr + umc_reg_offset) * 4);
	if (reg_value)
		dev_info(adev->dev, "MCA MISC0 0x%llx, umc_reg_offset 0x%x\n", reg_value, umc_reg_offset);
}

static void umc_v6_7_ecc_info_query_correctable_error_count(struct amdgpu_device *adev,
						   uint32_t umc_inst, uint32_t ch_inst,
						   unsigned long *error_count)
{
	uint64_t mc_umc_status;
	uint32_t eccinfo_table_idx;
	uint32_t umc_reg_offset;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	umc_reg_offset = get_umc_v6_7_reg_offset(adev,
						umc_inst, ch_inst);

	eccinfo_table_idx = umc_inst * adev->umc.channel_inst_num + ch_inst;
	/* check for SRAM correctable error
	  MCUMC_STATUS is a 64 bit register */
	mc_umc_status = ras->umc_ecc.ecc[eccinfo_table_idx].mca_umc_status;
	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1) {
		*error_count += 1;

		umc_v6_7_query_error_status_helper(adev, mc_umc_status, umc_reg_offset);
	}
}

static void umc_v6_7_ecc_info_querry_uncorrectable_error_count(struct amdgpu_device *adev,
							  uint32_t umc_inst, uint32_t ch_inst,
						      unsigned long *error_count)
{
	uint64_t mc_umc_status;
	uint32_t eccinfo_table_idx;
	uint32_t umc_reg_offset;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	umc_reg_offset = get_umc_v6_7_reg_offset(adev,
						umc_inst, ch_inst);

	eccinfo_table_idx = umc_inst * adev->umc.channel_inst_num + ch_inst;
	/* check the MCUMC_STATUS */
	mc_umc_status = ras->umc_ecc.ecc[eccinfo_table_idx].mca_umc_status;
	if ((REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1) &&
	    (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Deferred) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, PCC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, TCC) == 1)) {
		*error_count += 1;

		umc_v6_7_query_error_status_helper(adev, mc_umc_status, umc_reg_offset);
	}
}

static void umc_v6_7_ecc_info_query_ras_error_count(struct amdgpu_device *adev,
					   void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;

	uint32_t umc_inst        = 0;
	uint32_t ch_inst         = 0;

	/*TODO: driver needs to toggle DF Cstate to ensure
	 * safe access of UMC registers. Will add the protection */
	LOOP_UMC_INST_AND_CH(umc_inst, ch_inst) {
		umc_v6_7_ecc_info_query_correctable_error_count(adev,
						      umc_inst, ch_inst,
						      &(err_data->ce_count));
		umc_v6_7_ecc_info_querry_uncorrectable_error_count(adev,
						      umc_inst, ch_inst,
							  &(err_data->ue_count));
	}
}

static void umc_v6_7_ecc_info_query_error_address(struct amdgpu_device *adev,
					 struct ras_err_data *err_data,
					 uint32_t ch_inst,
					 uint32_t umc_inst)
{
	uint64_t mc_umc_status, err_addr, soc_pa, retired_page, column;
	uint32_t channel_index;
	uint32_t eccinfo_table_idx;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	eccinfo_table_idx = umc_inst * adev->umc.channel_inst_num + ch_inst;
	channel_index =
		adev->umc.channel_idx_tbl[umc_inst * adev->umc.channel_inst_num + ch_inst];

	mc_umc_status = ras->umc_ecc.ecc[eccinfo_table_idx].mca_umc_status;

	if (mc_umc_status == 0)
		return;

	if (!err_data->err_addr)
		return;

	/* calculate error address if ue/ce error is detected */
	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1)) {

		err_addr = ras->umc_ecc.ecc[eccinfo_table_idx].mca_umc_addr;
		err_addr = REG_GET_FIELD(err_addr, MCA_UMC_UMC0_MCUMC_ADDRT0, ErrorAddr);

		/* translate umc channel address to soc pa, 3 parts are included */
		soc_pa = ADDR_OF_8KB_BLOCK(err_addr) |
				ADDR_OF_256B_BLOCK(channel_index) |
				OFFSET_IN_256B_BLOCK(err_addr);

		/* The umc channel bits are not original values, they are hashed */
		SET_CHANNEL_HASH(channel_index, soc_pa);

		/* clear [C4 C3 C2] in soc physical address */
		soc_pa &= ~(0x7ULL << UMC_V6_7_PA_C2_BIT);

		/* we only save ue error information currently, ce is skipped */
		if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC)
				== 1) {
			/* loop for all possibilities of [C4 C3 C2] */
			for (column = 0; column < UMC_V6_7_NA_MAP_PA_NUM; column++) {
				retired_page = soc_pa | (column << UMC_V6_7_PA_C2_BIT);
				dev_info(adev->dev, "Error Address(PA): 0x%llx\n", retired_page);
				amdgpu_umc_fill_error_record(err_data, err_addr,
					retired_page, channel_index, umc_inst);

				/* shift R14 bit */
				retired_page ^= (0x1ULL << UMC_V6_7_PA_R14_BIT);
				dev_info(adev->dev, "Error Address(PA): 0x%llx\n", retired_page);
				amdgpu_umc_fill_error_record(err_data, err_addr,
					retired_page, channel_index, umc_inst);
			}
		}
	}
}

static void umc_v6_7_ecc_info_query_ras_error_address(struct amdgpu_device *adev,
					     void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;

	uint32_t umc_inst        = 0;
	uint32_t ch_inst         = 0;

	/*TODO: driver needs to toggle DF Cstate to ensure
	 * safe access of UMC resgisters. Will add the protection
	 * when firmware interface is ready */
	LOOP_UMC_INST_AND_CH(umc_inst, ch_inst) {
		umc_v6_7_ecc_info_query_error_address(adev,
					     err_data,
					     ch_inst,
					     umc_inst);
	}
}

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
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1) {
		*error_count += 1;

		umc_v6_7_query_error_status_helper(adev, mc_umc_status, umc_reg_offset);
	}
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
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, TCC) == 1)) {
		*error_count += 1;

		umc_v6_7_query_error_status_helper(adev, mc_umc_status, umc_reg_offset);
	}
}

static void umc_v6_7_reset_error_count_per_channel(struct amdgpu_device *adev,
						   uint32_t umc_reg_offset)
{
	uint32_t ecc_err_cnt_addr;
	uint32_t ecc_err_cnt_sel, ecc_err_cnt_sel_addr;

	ecc_err_cnt_sel_addr =
		SOC15_REG_OFFSET(UMC, 0,
				regUMCCH0_0_EccErrCntSel);
	ecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0,
				regUMCCH0_0_EccErrCnt);

	/* select the lower chip */
	ecc_err_cnt_sel = RREG32_PCIE((ecc_err_cnt_sel_addr +
				       umc_reg_offset) * 4);
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel,
					UMCCH0_0_EccErrCntSel,
					EccErrCntCsSel, 0);
	WREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4,
			ecc_err_cnt_sel);

	/* clear lower chip error count */
	WREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4,
			UMC_V6_7_CE_CNT_INIT);

	/* select the higher chip */
	ecc_err_cnt_sel = RREG32_PCIE((ecc_err_cnt_sel_addr +
					umc_reg_offset) * 4);
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel,
					UMCCH0_0_EccErrCntSel,
					EccErrCntCsSel, 1);
	WREG32_PCIE((ecc_err_cnt_sel_addr + umc_reg_offset) * 4,
			ecc_err_cnt_sel);

	/* clear higher chip error count */
	WREG32_PCIE((ecc_err_cnt_addr + umc_reg_offset) * 4,
			UMC_V6_7_CE_CNT_INIT);
}

static void umc_v6_7_reset_error_count(struct amdgpu_device *adev)
{
	uint32_t umc_inst        = 0;
	uint32_t ch_inst         = 0;
	uint32_t umc_reg_offset  = 0;

	LOOP_UMC_INST_AND_CH(umc_inst, ch_inst) {
		umc_reg_offset = get_umc_v6_7_reg_offset(adev,
							 umc_inst,
							 ch_inst);

		umc_v6_7_reset_error_count_per_channel(adev,
						       umc_reg_offset);
	}
}

static void umc_v6_7_query_ras_error_count(struct amdgpu_device *adev,
					   void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;

	uint32_t umc_inst        = 0;
	uint32_t ch_inst         = 0;
	uint32_t umc_reg_offset  = 0;

	/*TODO: driver needs to toggle DF Cstate to ensure
	 * safe access of UMC registers. Will add the protection */
	LOOP_UMC_INST_AND_CH(umc_inst, ch_inst) {
		umc_reg_offset = get_umc_v6_7_reg_offset(adev,
							 umc_inst,
							 ch_inst);
		umc_v6_7_query_correctable_error_count(adev,
						       umc_reg_offset,
						       &(err_data->ce_count));
		umc_v6_7_querry_uncorrectable_error_count(adev,
							  umc_reg_offset,
							  &(err_data->ue_count));
	}

	umc_v6_7_reset_error_count(adev);
}

static void umc_v6_7_query_error_address(struct amdgpu_device *adev,
					 struct ras_err_data *err_data,
					 uint32_t umc_reg_offset,
					 uint32_t ch_inst,
					 uint32_t umc_inst)
{
	uint32_t mc_umc_status_addr;
	uint32_t channel_index;
	uint64_t mc_umc_status, mc_umc_addrt0;
	uint64_t err_addr, soc_pa, retired_page, column;

	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_STATUST0);
	mc_umc_addrt0 =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_ADDRT0);

	mc_umc_status = RREG64_PCIE((mc_umc_status_addr + umc_reg_offset) * 4);

	if (mc_umc_status == 0)
		return;

	if (!err_data->err_addr) {
		/* clear umc status */
		WREG64_PCIE((mc_umc_status_addr + umc_reg_offset) * 4, 0x0ULL);
		return;
	}

	channel_index =
		adev->umc.channel_idx_tbl[umc_inst * adev->umc.channel_inst_num + ch_inst];

	/* calculate error address if ue/ce error is detected */
	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1)) {

		err_addr = RREG64_PCIE((mc_umc_addrt0 + umc_reg_offset) * 4);
		err_addr = REG_GET_FIELD(err_addr, MCA_UMC_UMC0_MCUMC_ADDRT0, ErrorAddr);

		/* translate umc channel address to soc pa, 3 parts are included */
		soc_pa = ADDR_OF_8KB_BLOCK(err_addr) |
				ADDR_OF_256B_BLOCK(channel_index) |
				OFFSET_IN_256B_BLOCK(err_addr);

		/* The umc channel bits are not original values, they are hashed */
		SET_CHANNEL_HASH(channel_index, soc_pa);

		/* clear [C4 C3 C2] in soc physical address */
		soc_pa &= ~(0x7ULL << UMC_V6_7_PA_C2_BIT);

		/* we only save ue error information currently, ce is skipped */
		if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC)
				== 1) {
			/* loop for all possibilities of [C4 C3 C2] */
			for (column = 0; column < UMC_V6_7_NA_MAP_PA_NUM; column++) {
				retired_page = soc_pa | (column << UMC_V6_7_PA_C2_BIT);
				dev_info(adev->dev, "Error Address(PA): 0x%llx\n", retired_page);
				amdgpu_umc_fill_error_record(err_data, err_addr,
					retired_page, channel_index, umc_inst);

				/* shift R14 bit */
				retired_page ^= (0x1ULL << UMC_V6_7_PA_R14_BIT);
				dev_info(adev->dev, "Error Address(PA): 0x%llx\n", retired_page);
				amdgpu_umc_fill_error_record(err_data, err_addr,
					retired_page, channel_index, umc_inst);
			}
		}
	}

	/* clear umc status */
	WREG64_PCIE((mc_umc_status_addr + umc_reg_offset) * 4, 0x0ULL);
}

static void umc_v6_7_query_ras_error_address(struct amdgpu_device *adev,
					     void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;

	uint32_t umc_inst        = 0;
	uint32_t ch_inst         = 0;
	uint32_t umc_reg_offset  = 0;

	/*TODO: driver needs to toggle DF Cstate to ensure
	 * safe access of UMC resgisters. Will add the protection
	 * when firmware interface is ready */
	LOOP_UMC_INST_AND_CH(umc_inst, ch_inst) {
		umc_reg_offset = get_umc_v6_7_reg_offset(adev,
							 umc_inst,
							 ch_inst);
		umc_v6_7_query_error_address(adev,
					     err_data,
					     umc_reg_offset,
					     ch_inst,
					     umc_inst);
	}
}

static uint32_t umc_v6_7_query_ras_poison_mode_per_channel(
						struct amdgpu_device *adev,
						uint32_t umc_reg_offset)
{
	uint32_t ecc_ctrl_addr, ecc_ctrl;

	ecc_ctrl_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_0_EccCtrl);
	ecc_ctrl = RREG32_PCIE((ecc_ctrl_addr +
					umc_reg_offset) * 4);

	return REG_GET_FIELD(ecc_ctrl, UMCCH0_0_EccCtrl, UCFatalEn);
}

static bool umc_v6_7_query_ras_poison_mode(struct amdgpu_device *adev)
{
	uint32_t umc_reg_offset  = 0;

	/* Enabling fatal error in umc instance0 channel0 will be
	 * considered as fatal error mode
	 */
	umc_reg_offset = get_umc_v6_7_reg_offset(adev, 0, 0);
	return !umc_v6_7_query_ras_poison_mode_per_channel(adev, umc_reg_offset);
}

const struct amdgpu_ras_block_hw_ops umc_v6_7_ras_hw_ops = {
	.query_ras_error_count = umc_v6_7_query_ras_error_count,
	.query_ras_error_address = umc_v6_7_query_ras_error_address,
};

struct amdgpu_umc_ras umc_v6_7_ras = {
	.ras_block = {
		.hw_ops = &umc_v6_7_ras_hw_ops,
	},
	.query_ras_poison_mode = umc_v6_7_query_ras_poison_mode,
	.ecc_info_query_ras_error_count = umc_v6_7_ecc_info_query_ras_error_count,
	.ecc_info_query_ras_error_address = umc_v6_7_ecc_info_query_ras_error_address,
};
