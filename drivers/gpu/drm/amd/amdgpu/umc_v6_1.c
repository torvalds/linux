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

#define smnMCA_UMC0_MCUMC_ADDRT0	0x50f10

/*
 * (addr / 256) * 8192, the higher 26 bits in ErrorAddr
 * is the index of 8KB block
 */
#define ADDR_OF_8KB_BLOCK(addr)		(((addr) & ~0xffULL) << 5)
/* channel index is the index of 256B block */
#define ADDR_OF_256B_BLOCK(channel_index)	((channel_index) << 8)
/* offset in 256B block */
#define OFFSET_IN_256B_BLOCK(addr)		((addr) & 0xffULL)

const uint32_t
	umc_v6_1_channel_idx_tbl[UMC_V6_1_UMC_INSTANCE_NUM][UMC_V6_1_CHANNEL_INSTANCE_NUM] = {
		{2, 18, 11, 27},	{4, 20, 13, 29},
		{1, 17, 8, 24},		{7, 23, 14, 30},
		{10, 26, 3, 19},	{12, 28, 5, 21},
		{9, 25, 0, 16},		{15, 31, 6, 22}
};

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
		(REG_GET_FIELD(ecc_err_cnt, UMCCH0_0_EccErrCnt, EccErrCnt) -
		 UMC_V6_1_CE_CNT_INIT);
	/* clear the lower chip err count */
	WREG32(ecc_err_cnt_addr + umc_reg_offset, UMC_V6_1_CE_CNT_INIT);

	/* select the higher chip and check the err counter */
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_0_EccErrCntSel,
					EccErrCntCsSel, 1);
	WREG32(ecc_err_cnt_sel_addr + umc_reg_offset, ecc_err_cnt_sel);
	ecc_err_cnt = RREG32(ecc_err_cnt_addr + umc_reg_offset);
	*error_count +=
		(REG_GET_FIELD(ecc_err_cnt, UMCCH0_0_EccErrCnt, EccErrCnt) -
		 UMC_V6_1_CE_CNT_INIT);
	/* clear the higher chip err count */
	WREG32(ecc_err_cnt_addr + umc_reg_offset, UMC_V6_1_CE_CNT_INIT);

	/* check for SRAM correctable error
	  MCUMC_STATUS is a 64 bit register */
	mc_umc_status = RREG64_UMC(mc_umc_status_addr + umc_reg_offset);
	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, ErrorCodeExt) == 6 &&
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1)
		*error_count += 1;
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
	mc_umc_status = RREG64_UMC(mc_umc_status_addr + umc_reg_offset);
	if ((REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1) &&
	    (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Deferred) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, PCC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, TCC) == 1))
		*error_count += 1;
}

static void umc_v6_1_query_error_count(struct amdgpu_device *adev,
					   struct ras_err_data *err_data, uint32_t umc_reg_offset,
					   uint32_t channel_index)
{
	umc_v6_1_query_correctable_error_count(adev, umc_reg_offset,
						   &(err_data->ce_count));
	umc_v6_1_querry_uncorrectable_error_count(adev, umc_reg_offset,
						  &(err_data->ue_count));
}

static void umc_v6_1_query_ras_error_count(struct amdgpu_device *adev,
					   void *ras_error_status)
{
	amdgpu_umc_for_each_channel(umc_v6_1_query_error_count);
}

static void umc_v6_1_query_error_address(struct amdgpu_device *adev,
					 struct ras_err_data *err_data,
					 uint32_t umc_reg_offset, uint32_t channel_index)
{
	uint32_t lsb, mc_umc_status_addr;
	uint64_t mc_umc_status, err_addr;

	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, mmMCA_UMC_UMC0_MCUMC_STATUST0);

	/* skip error address process if -ENOMEM */
	if (!err_data->err_addr) {
		/* clear umc status */
		WREG64_UMC(mc_umc_status_addr + umc_reg_offset, 0x0ULL);
		return;
	}

	mc_umc_status = RREG64_UMC(mc_umc_status_addr + umc_reg_offset);

	/* calculate error address if ue/ce error is detected */
	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1)) {
		err_addr = RREG64_PCIE(smnMCA_UMC0_MCUMC_ADDRT0 + umc_reg_offset * 4);

		/* the lowest lsb bits should be ignored */
		lsb = REG_GET_FIELD(err_addr, MCA_UMC_UMC0_MCUMC_ADDRT0, LSB);
		err_addr = REG_GET_FIELD(err_addr, MCA_UMC_UMC0_MCUMC_ADDRT0, ErrorAddr);
		err_addr &= ~((0x1ULL << lsb) - 1);

		/* translate umc channel address to soc pa, 3 parts are included */
		err_data->err_addr[err_data->err_addr_cnt] =
						ADDR_OF_8KB_BLOCK(err_addr) |
						ADDR_OF_256B_BLOCK(channel_index) |
						OFFSET_IN_256B_BLOCK(err_addr);

		err_data->err_addr_cnt++;
	}

	/* clear umc status */
	WREG64_UMC(mc_umc_status_addr + umc_reg_offset, 0x0ULL);
}

static void umc_v6_1_query_ras_error_address(struct amdgpu_device *adev,
					     void *ras_error_status)
{
	amdgpu_umc_for_each_channel(umc_v6_1_query_error_address);
}

static void umc_v6_1_ras_init_per_channel(struct amdgpu_device *adev,
					 struct ras_err_data *err_data,
					 uint32_t umc_reg_offset, uint32_t channel_index)
{
	uint32_t ecc_err_cnt_sel, ecc_err_cnt_sel_addr;
	uint32_t ecc_err_cnt_addr;

	ecc_err_cnt_sel_addr =
		SOC15_REG_OFFSET(UMC, 0, mmUMCCH0_0_EccErrCntSel);
	ecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, mmUMCCH0_0_EccErrCnt);

	/* select the lower chip and check the error count */
	ecc_err_cnt_sel = RREG32(ecc_err_cnt_sel_addr + umc_reg_offset);
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_0_EccErrCntSel,
					EccErrCntCsSel, 0);
	/* set ce error interrupt type to APIC based interrupt */
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_0_EccErrCntSel,
					EccErrInt, 0x1);
	WREG32(ecc_err_cnt_sel_addr + umc_reg_offset, ecc_err_cnt_sel);
	/* set error count to initial value */
	WREG32(ecc_err_cnt_addr + umc_reg_offset, UMC_V6_1_CE_CNT_INIT);

	/* select the higher chip and check the err counter */
	ecc_err_cnt_sel = REG_SET_FIELD(ecc_err_cnt_sel, UMCCH0_0_EccErrCntSel,
					EccErrCntCsSel, 1);
	WREG32(ecc_err_cnt_sel_addr + umc_reg_offset, ecc_err_cnt_sel);
	WREG32(ecc_err_cnt_addr + umc_reg_offset, UMC_V6_1_CE_CNT_INIT);
}

static void umc_v6_1_ras_init(struct amdgpu_device *adev)
{
	void *ras_error_status = NULL;

	amdgpu_umc_for_each_channel(umc_v6_1_ras_init_per_channel);
}

const struct amdgpu_umc_funcs umc_v6_1_funcs = {
	.ras_init = umc_v6_1_ras_init,
	.query_ras_error_count = umc_v6_1_query_ras_error_count,
	.query_ras_error_address = umc_v6_1_query_ras_error_address,
	.enable_umc_index_mode = umc_v6_1_enable_umc_index_mode,
	.disable_umc_index_mode = umc_v6_1_disable_umc_index_mode,
};
