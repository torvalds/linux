/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#include "umc_v12_0.h"
#include "amdgpu_ras.h"
#include "amdgpu_umc.h"
#include "amdgpu.h"
#include "umc/umc_12_0_0_offset.h"
#include "umc/umc_12_0_0_sh_mask.h"

static inline uint64_t get_umc_v12_0_reg_offset(struct amdgpu_device *adev,
					    uint32_t node_inst,
					    uint32_t umc_inst,
					    uint32_t ch_inst)
{
	uint32_t index = umc_inst * adev->umc.channel_inst_num + ch_inst;
	uint64_t cross_node_offset = (node_inst == 0) ? 0 : UMC_V12_0_CROSS_NODE_OFFSET;

	umc_inst = index / 4;
	ch_inst = index % 4;

	return adev->umc.channel_offs * ch_inst + UMC_V12_0_INST_DIST * umc_inst +
		UMC_V12_0_NODE_DIST * node_inst + cross_node_offset;
}

static int umc_v12_0_reset_error_count_per_channel(struct amdgpu_device *adev,
					uint32_t node_inst, uint32_t umc_inst,
					uint32_t ch_inst, void *data)
{
	uint64_t odecc_err_cnt_addr;
	uint64_t umc_reg_offset =
		get_umc_v12_0_reg_offset(adev, node_inst, umc_inst, ch_inst);

	odecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_OdEccErrCnt);

	/* clear error count */
	WREG32_PCIE_EXT((odecc_err_cnt_addr + umc_reg_offset) * 4,
			UMC_V12_0_CE_CNT_INIT);

	return 0;
}

static void umc_v12_0_reset_error_count(struct amdgpu_device *adev)
{
	amdgpu_umc_loop_channels(adev,
		umc_v12_0_reset_error_count_per_channel, NULL);
}

static void umc_v12_0_query_correctable_error_count(struct amdgpu_device *adev,
						   uint64_t umc_reg_offset,
						   unsigned long *error_count)
{
	uint64_t mc_umc_status;
	uint64_t mc_umc_status_addr;

	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_STATUST0);

	/* Rely on MCUMC_STATUS for correctable error counter
	 * MCUMC_STATUS is a 64 bit register
	 */
	mc_umc_status =
		RREG64_PCIE_EXT((mc_umc_status_addr + umc_reg_offset) * 4);

	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1)
		*error_count += 1;
}

static void umc_v12_0_query_uncorrectable_error_count(struct amdgpu_device *adev,
						      uint64_t umc_reg_offset,
						      unsigned long *error_count)
{
	uint64_t mc_umc_status;
	uint64_t mc_umc_status_addr;

	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_STATUST0);

	/* Check the MCUMC_STATUS. */
	mc_umc_status =
		RREG64_PCIE_EXT((mc_umc_status_addr + umc_reg_offset) * 4);

	if ((REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1) &&
	    (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Deferred) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, PCC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UC) == 1 ||
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, TCC) == 1))
		*error_count += 1;
}

static int umc_v12_0_query_error_count(struct amdgpu_device *adev,
					uint32_t node_inst, uint32_t umc_inst,
					uint32_t ch_inst, void *data)
{
	struct ras_err_data *err_data = (struct ras_err_data *)data;
	uint64_t umc_reg_offset =
		get_umc_v12_0_reg_offset(adev, node_inst, umc_inst, ch_inst);

	umc_v12_0_query_correctable_error_count(adev,
					umc_reg_offset,
					&(err_data->ce_count));
	umc_v12_0_query_uncorrectable_error_count(adev,
					umc_reg_offset,
					&(err_data->ue_count));

	return 0;
}

static void umc_v12_0_query_ras_error_count(struct amdgpu_device *adev,
					   void *ras_error_status)
{
	amdgpu_umc_loop_channels(adev,
		umc_v12_0_query_error_count, ras_error_status);

	umc_v12_0_reset_error_count(adev);
}

static void umc_v12_0_convert_error_address(struct amdgpu_device *adev,
					    struct ras_err_data *err_data, uint64_t err_addr,
					    uint32_t ch_inst, uint32_t umc_inst,
					    uint32_t node_inst)
{

}

static int umc_v12_0_query_error_address(struct amdgpu_device *adev,
					uint32_t node_inst, uint32_t umc_inst,
					uint32_t ch_inst, void *data)
{
	uint64_t mc_umc_status_addr;
	uint64_t mc_umc_status, err_addr;
	uint64_t mc_umc_addrt0;
	struct ras_err_data *err_data = (struct ras_err_data *)data;
	uint64_t umc_reg_offset =
		get_umc_v12_0_reg_offset(adev, node_inst, umc_inst, ch_inst);

	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_STATUST0);

	mc_umc_status = RREG64_PCIE_EXT((mc_umc_status_addr + umc_reg_offset) * 4);

	if (mc_umc_status == 0)
		return 0;

	if (!err_data->err_addr) {
		/* clear umc status */
		WREG64_PCIE_EXT((mc_umc_status_addr + umc_reg_offset) * 4, 0x0ULL);

		return 0;
	}

	/* calculate error address if ue error is detected */
	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, AddrV) == 1 &&
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1) {

		mc_umc_addrt0 =
			SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_ADDRT0);

		err_addr = RREG64_PCIE_EXT((mc_umc_addrt0 + umc_reg_offset) * 4);

		err_addr = REG_GET_FIELD(err_addr, MCA_UMC_UMC0_MCUMC_ADDRT0, ErrorAddr);

		umc_v12_0_convert_error_address(adev, err_data, err_addr,
					ch_inst, umc_inst, node_inst);
	}

	/* clear umc status */
	WREG64_PCIE_EXT((mc_umc_status_addr + umc_reg_offset) * 4, 0x0ULL);

	return 0;
}

static void umc_v12_0_query_ras_error_address(struct amdgpu_device *adev,
					     void *ras_error_status)
{
	amdgpu_umc_loop_channels(adev,
		umc_v12_0_query_error_address, ras_error_status);
}

static int umc_v12_0_err_cnt_init_per_channel(struct amdgpu_device *adev,
					uint32_t node_inst, uint32_t umc_inst,
					uint32_t ch_inst, void *data)
{
	uint32_t odecc_cnt_sel;
	uint64_t odecc_cnt_sel_addr, odecc_err_cnt_addr;
	uint64_t umc_reg_offset =
		get_umc_v12_0_reg_offset(adev, node_inst, umc_inst, ch_inst);

	odecc_cnt_sel_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_OdEccCntSel);
	odecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_OdEccErrCnt);

	odecc_cnt_sel = RREG32_PCIE_EXT((odecc_cnt_sel_addr + umc_reg_offset) * 4);

	/* set ce error interrupt type to APIC based interrupt */
	odecc_cnt_sel = REG_SET_FIELD(odecc_cnt_sel, UMCCH0_OdEccCntSel,
					OdEccErrInt, 0x1);
	WREG32_PCIE_EXT((odecc_cnt_sel_addr + umc_reg_offset) * 4, odecc_cnt_sel);

	/* set error count to initial value */
	WREG32_PCIE_EXT((odecc_err_cnt_addr + umc_reg_offset) * 4, UMC_V12_0_CE_CNT_INIT);

	return 0;
}

static void umc_v12_0_err_cnt_init(struct amdgpu_device *adev)
{
	amdgpu_umc_loop_channels(adev,
		umc_v12_0_err_cnt_init_per_channel, NULL);
}

static bool umc_v12_0_query_ras_poison_mode(struct amdgpu_device *adev)
{
	/*
	 * Force return true, because regUMCCH0_EccCtrl
	 * is not accessible from host side
	 */
	return true;
}

const struct amdgpu_ras_block_hw_ops umc_v12_0_ras_hw_ops = {
	.query_ras_error_count = umc_v12_0_query_ras_error_count,
	.query_ras_error_address = umc_v12_0_query_ras_error_address,
};

struct amdgpu_umc_ras umc_v12_0_ras = {
	.ras_block = {
		.hw_ops = &umc_v12_0_ras_hw_ops,
	},
	.err_cnt_init = umc_v12_0_err_cnt_init,
	.query_ras_poison_mode = umc_v12_0_query_ras_poison_mode,
};
