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
#include "mp/mp_13_0_6_sh_mask.h"

#define MAX_ECC_NUM_PER_RETIREMENT  32
#define DELAYED_TIME_FOR_GPU_RESET  1000  //ms

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

bool umc_v12_0_is_deferred_error(struct amdgpu_device *adev, uint64_t mc_umc_status)
{
	dev_dbg(adev->dev,
		"MCA_UMC_STATUS(0x%llx): Val:%llu, Poison:%llu, Deferred:%llu, PCC:%llu, UC:%llu, TCC:%llu\n",
		mc_umc_status,
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val),
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Poison),
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Deferred),
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, PCC),
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UC),
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, TCC)
	);

	return (amdgpu_ras_is_poison_mode_supported(adev) &&
		(REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1) &&
		(REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Deferred) == 1));
}

bool umc_v12_0_is_uncorrectable_error(struct amdgpu_device *adev, uint64_t mc_umc_status)
{
	if (umc_v12_0_is_deferred_error(adev, mc_umc_status))
		return false;

	return ((REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1) &&
		(REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, PCC) == 1 ||
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UC) == 1 ||
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, TCC) == 1));
}

bool umc_v12_0_is_correctable_error(struct amdgpu_device *adev, uint64_t mc_umc_status)
{
	if (umc_v12_0_is_deferred_error(adev, mc_umc_status))
		return false;

	return (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
		(REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1 ||
		(REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1 &&
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UC) == 0) ||
		/* Identify data parity error in replay mode */
		((REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, ErrorCodeExt) == 0x5 ||
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, ErrorCodeExt) == 0xb) &&
		!(umc_v12_0_is_uncorrectable_error(adev, mc_umc_status)))));
}

static void umc_v12_0_query_error_count_per_type(struct amdgpu_device *adev,
						   uint64_t umc_reg_offset,
						   unsigned long *error_count,
						   check_error_type_func error_type_func)
{
	uint64_t mc_umc_status;
	uint64_t mc_umc_status_addr;

	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_STATUST0);

	/* Check MCUMC_STATUS */
	mc_umc_status =
		RREG64_PCIE_EXT((mc_umc_status_addr + umc_reg_offset) * 4);

	if (error_type_func(adev, mc_umc_status))
		*error_count += 1;
}

static int umc_v12_0_query_error_count(struct amdgpu_device *adev,
					uint32_t node_inst, uint32_t umc_inst,
					uint32_t ch_inst, void *data)
{
	struct ras_err_data *err_data = (struct ras_err_data *)data;
	unsigned long ue_count = 0, ce_count = 0, de_count = 0;

	/* NOTE: node_inst is converted by adev->umc.active_mask and the range is [0-3],
	 * which can be used as die ID directly */
	struct amdgpu_smuio_mcm_config_info mcm_info = {
		.socket_id = adev->smuio.funcs->get_socket_id(adev),
		.die_id = node_inst,
	};

	uint64_t umc_reg_offset =
		get_umc_v12_0_reg_offset(adev, node_inst, umc_inst, ch_inst);

	umc_v12_0_query_error_count_per_type(adev, umc_reg_offset,
					    &ce_count, umc_v12_0_is_correctable_error);
	umc_v12_0_query_error_count_per_type(adev, umc_reg_offset,
					    &ue_count, umc_v12_0_is_uncorrectable_error);
	umc_v12_0_query_error_count_per_type(adev, umc_reg_offset,
					    &de_count, umc_v12_0_is_deferred_error);

	amdgpu_ras_error_statistic_ue_count(err_data, &mcm_info, ue_count);
	amdgpu_ras_error_statistic_ce_count(err_data, &mcm_info, ce_count);
	amdgpu_ras_error_statistic_de_count(err_data, &mcm_info, de_count);

	return 0;
}

static void umc_v12_0_query_ras_error_count(struct amdgpu_device *adev,
					   void *ras_error_status)
{
	amdgpu_umc_loop_channels(adev,
		umc_v12_0_query_error_count, ras_error_status);

	umc_v12_0_reset_error_count(adev);
}

static int umc_v12_0_convert_error_address(struct amdgpu_device *adev,
					struct ras_err_data *err_data,
					struct ta_ras_query_address_input *addr_in,
					struct ta_ras_query_address_output *addr_out,
					bool dump_addr)
{
	uint32_t col, col_lower, row, row_lower, bank;
	uint32_t channel_index = 0, umc_inst = 0;
	uint32_t i, loop_bits[UMC_V12_0_RETIRE_LOOP_BITS];
	uint64_t soc_pa, column, err_addr;
	struct ta_ras_query_address_output addr_out_tmp;
	struct ta_ras_query_address_output *paddr_out;
	enum amdgpu_memory_partition nps = AMDGPU_NPS1_PARTITION_MODE;
	int ret = 0;

	if (!addr_out)
		paddr_out = &addr_out_tmp;
	else
		paddr_out = addr_out;

	err_addr = bank = 0;
	if (addr_in) {
		err_addr = addr_in->ma.err_addr;
		addr_in->addr_type = TA_RAS_MCA_TO_PA;
		ret = psp_ras_query_address(&adev->psp, addr_in, paddr_out);
		if (ret) {
			dev_warn(adev->dev, "Failed to query RAS physical address for 0x%llx",
				err_addr);

			goto out;
		}

		bank = paddr_out->pa.bank;
		/* no need to care about umc inst if addr_in is NULL */
		umc_inst = addr_in->ma.umc_inst;
	}

	loop_bits[0] = UMC_V12_0_PA_C2_BIT;
	loop_bits[1] = UMC_V12_0_PA_C3_BIT;
	loop_bits[2] = UMC_V12_0_PA_C4_BIT;
	loop_bits[3] = UMC_V12_0_PA_R13_BIT;

	if (adev->gmc.gmc_funcs->query_mem_partition_mode)
		nps = adev->gmc.gmc_funcs->query_mem_partition_mode(adev);

	/* other nps modes are taken as nps1 */
	if (nps == AMDGPU_NPS4_PARTITION_MODE) {
		loop_bits[0] = UMC_V12_0_PA_CH4_BIT;
		loop_bits[1] = UMC_V12_0_PA_CH5_BIT;
		loop_bits[2] = UMC_V12_0_PA_B0_BIT;
		loop_bits[3] = UMC_V12_0_PA_R11_BIT;
	}

	soc_pa = paddr_out->pa.pa;
	channel_index = paddr_out->pa.channel_idx;
	/* clear loop bits in soc physical address */
	for (i = 0; i < UMC_V12_0_RETIRE_LOOP_BITS; i++)
		soc_pa &= ~BIT_ULL(loop_bits[i]);

	paddr_out->pa.pa = soc_pa;
	/* get column bit 0 and 1 in mca address */
	col_lower = (err_addr >> 1) & 0x3ULL;
	/* MA_R13_BIT will be handled later */
	row_lower = (err_addr >> UMC_V12_0_MA_R0_BIT) & 0x1fffULL;

	if (!err_data && !dump_addr)
		goto out;

	/* loop for all possibilities of retired bits */
	for (column = 0; column < UMC_V12_0_BAD_PAGE_NUM_PER_CHANNEL; column++) {
		soc_pa = paddr_out->pa.pa;
		for (i = 0; i < UMC_V12_0_RETIRE_LOOP_BITS; i++)
			soc_pa |= (((column >> i) & 0x1ULL) << loop_bits[i]);

		col = ((column & 0x7) << 2) | col_lower;
		/* add row bit 13 */
		row = ((column >> 3) << 13) | row_lower;

		if (dump_addr)
			dev_info(adev->dev,
				"Error Address(PA):0x%-10llx Row:0x%-4x Col:0x%-2x Bank:0x%x Channel:0x%x\n",
				soc_pa, row, col, bank, channel_index);

		if (err_data)
			amdgpu_umc_fill_error_record(err_data, err_addr,
				soc_pa, channel_index, umc_inst);
	}

out:
	return ret;
}

static int umc_v12_0_query_error_address(struct amdgpu_device *adev,
					uint32_t node_inst, uint32_t umc_inst,
					uint32_t ch_inst, void *data)
{
	struct ras_err_data *err_data = (struct ras_err_data *)data;
	struct ta_ras_query_address_input addr_in;
	uint64_t mc_umc_status_addr;
	uint64_t mc_umc_status, err_addr;
	uint64_t mc_umc_addrt0;
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
	if (umc_v12_0_is_uncorrectable_error(adev, mc_umc_status) ||
	    umc_v12_0_is_deferred_error(adev, mc_umc_status)) {
		mc_umc_addrt0 =
			SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_ADDRT0);

		err_addr = RREG64_PCIE_EXT((mc_umc_addrt0 + umc_reg_offset) * 4);

		err_addr = REG_GET_FIELD(err_addr, MCA_UMC_UMC0_MCUMC_ADDRT0, ErrorAddr);

		if (!adev->aid_mask &&
		    adev->smuio.funcs &&
		    adev->smuio.funcs->get_socket_id)
			addr_in.ma.socket_id = adev->smuio.funcs->get_socket_id(adev);
		else
			addr_in.ma.socket_id = 0;

		addr_in.ma.err_addr = err_addr;
		addr_in.ma.ch_inst = ch_inst;
		addr_in.ma.umc_inst = umc_inst;
		addr_in.ma.node_inst = node_inst;

		umc_v12_0_convert_error_address(adev, err_data, &addr_in, NULL, true);
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

static bool umc_v12_0_check_ecc_err_status(struct amdgpu_device *adev,
			enum amdgpu_mca_error_type type, void *ras_error_status)
{
	uint64_t mc_umc_status = *(uint64_t *)ras_error_status;

	switch (type) {
	case AMDGPU_MCA_ERROR_TYPE_UE:
		return umc_v12_0_is_uncorrectable_error(adev, mc_umc_status);
	case AMDGPU_MCA_ERROR_TYPE_CE:
		return umc_v12_0_is_correctable_error(adev, mc_umc_status);
	case AMDGPU_MCA_ERROR_TYPE_DE:
		return umc_v12_0_is_deferred_error(adev, mc_umc_status);
	default:
		return false;
	}

	return false;
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

static int umc_v12_0_aca_bank_parser(struct aca_handle *handle, struct aca_bank *bank,
				     enum aca_smu_type type, void *data)
{
	struct amdgpu_device *adev = handle->adev;
	struct aca_bank_info info;
	enum aca_error_type err_type;
	u64 status, count;
	u32 ext_error_code;
	int ret;

	status = bank->regs[ACA_REG_IDX_STATUS];
	if (umc_v12_0_is_deferred_error(adev, status))
		err_type = ACA_ERROR_TYPE_DEFERRED;
	else if (umc_v12_0_is_uncorrectable_error(adev, status))
		err_type = ACA_ERROR_TYPE_UE;
	else if (umc_v12_0_is_correctable_error(adev, status))
		err_type = ACA_ERROR_TYPE_CE;
	else
		return 0;

	ret = aca_bank_info_decode(bank, &info);
	if (ret)
		return ret;

	amdgpu_umc_update_ecc_status(adev,
		bank->regs[ACA_REG_IDX_STATUS],
		bank->regs[ACA_REG_IDX_IPID],
		bank->regs[ACA_REG_IDX_ADDR]);

	ext_error_code = ACA_REG__STATUS__ERRORCODEEXT(status);
	count = ext_error_code == 0 ?
		ACA_REG__MISC0__ERRCNT(bank->regs[ACA_REG_IDX_MISC0]) : 1ULL;

	return aca_error_cache_log_bank_error(handle, &info, err_type, count);
}

static const struct aca_bank_ops umc_v12_0_aca_bank_ops = {
	.aca_bank_parser = umc_v12_0_aca_bank_parser,
};

const struct aca_info umc_v12_0_aca_info = {
	.hwip = ACA_HWIP_TYPE_UMC,
	.mask = ACA_ERROR_UE_MASK | ACA_ERROR_CE_MASK | ACA_ERROR_DEFERRED_MASK,
	.bank_ops = &umc_v12_0_aca_bank_ops,
};

static int umc_v12_0_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block)
{
	int ret;

	ret = amdgpu_umc_ras_late_init(adev, ras_block);
	if (ret)
		return ret;

	ret = amdgpu_ras_bind_aca(adev, AMDGPU_RAS_BLOCK__UMC,
				  &umc_v12_0_aca_info, NULL);
	if (ret)
		return ret;

	return 0;
}

static int umc_v12_0_update_ecc_status(struct amdgpu_device *adev,
			uint64_t status, uint64_t ipid, uint64_t addr)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	uint16_t hwid, mcatype;
	uint64_t page_pfn[UMC_V12_0_BAD_PAGE_NUM_PER_CHANNEL];
	uint64_t err_addr, pa_addr = 0;
	struct ras_ecc_err *ecc_err;
	struct ta_ras_query_address_output addr_out;
	int count, ret, i;

	hwid = REG_GET_FIELD(ipid, MCMP1_IPIDT0, HardwareID);
	mcatype = REG_GET_FIELD(ipid, MCMP1_IPIDT0, McaType);

	if ((hwid != MCA_UMC_HWID_V12_0) || (mcatype != MCA_UMC_MCATYPE_V12_0))
		return 0;

	if (!status)
		return 0;

	if (!umc_v12_0_is_deferred_error(adev, status))
		return 0;

	err_addr = REG_GET_FIELD(addr,
				MCA_UMC_UMC0_MCUMC_ADDRT0, ErrorAddr);

	dev_dbg(adev->dev,
		"UMC:IPID:0x%llx, socket:%llu, aid:%llu, inst:%llu, ch:%llu, err_addr:0x%llx\n",
		ipid,
		MCA_IPID_2_SOCKET_ID(ipid),
		MCA_IPID_2_DIE_ID(ipid),
		MCA_IPID_2_UMC_INST(ipid),
		MCA_IPID_2_UMC_CH(ipid),
		err_addr);

	ret = amdgpu_umc_mca_to_addr(adev,
			err_addr, MCA_IPID_2_UMC_CH(ipid),
			MCA_IPID_2_UMC_INST(ipid), MCA_IPID_2_DIE_ID(ipid),
			MCA_IPID_2_SOCKET_ID(ipid), &addr_out, true);
	if (ret)
		return ret;

	ecc_err = kzalloc(sizeof(*ecc_err), GFP_KERNEL);
	if (!ecc_err)
		return -ENOMEM;

	pa_addr = addr_out.pa.pa;
	ecc_err->status = status;
	ecc_err->ipid = ipid;
	ecc_err->addr = addr;
	ecc_err->pa_pfn = pa_addr >> AMDGPU_GPU_PAGE_SHIFT;
	ecc_err->channel_idx = addr_out.pa.channel_idx;

	/* If converted pa_pfn is 0, use pa C4 pfn. */
	if (!ecc_err->pa_pfn)
		ecc_err->pa_pfn = BIT_ULL(UMC_V12_0_PA_C4_BIT) >> AMDGPU_GPU_PAGE_SHIFT;

	ret = amdgpu_umc_logs_ecc_err(adev, &con->umc_ecc_log.de_page_tree, ecc_err);
	if (ret) {
		if (ret == -EEXIST)
			con->umc_ecc_log.de_queried_count++;
		else
			dev_err(adev->dev, "Fail to log ecc error! ret:%d\n", ret);

		kfree(ecc_err);
		return ret;
	}

	con->umc_ecc_log.de_queried_count++;

	memset(page_pfn, 0, sizeof(page_pfn));
	count = amdgpu_umc_lookup_bad_pages_in_a_row(adev,
				pa_addr,
				page_pfn, ARRAY_SIZE(page_pfn));
	if (count <= 0) {
		dev_warn(adev->dev, "Fail to convert error address! count:%d\n", count);
		return 0;
	}

	/* Reserve memory */
	for (i = 0; i < count; i++)
		amdgpu_ras_reserve_page(adev, page_pfn[i]);

	/* The problem case is as follows:
	 * 1. GPU A triggers a gpu ras reset, and GPU A drives
	 *    GPU B to also perform a gpu ras reset.
	 * 2. After gpu B ras reset started, gpu B queried a DE
	 *    data. Since the DE data was queried in the ras reset
	 *    thread instead of the page retirement thread, bad
	 *    page retirement work would not be triggered. Then
	 *    even if all gpu resets are completed, the bad pages
	 *    will be cached in RAM until GPU B's bad page retirement
	 *    work is triggered again and then saved to eeprom.
	 * Trigger delayed work to save the bad pages to eeprom in time
	 * after gpu ras reset is completed.
	 */
	if (amdgpu_ras_in_recovery(adev))
		schedule_delayed_work(&con->page_retirement_dwork,
			msecs_to_jiffies(DELAYED_TIME_FOR_GPU_RESET));

	return 0;
}

static int umc_v12_0_fill_error_record(struct amdgpu_device *adev,
				struct ras_ecc_err *ecc_err, void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;
	uint64_t page_pfn[UMC_V12_0_BAD_PAGE_NUM_PER_CHANNEL];
	int ret, i, count;

	if (!err_data || !ecc_err)
		return -EINVAL;

	memset(page_pfn, 0, sizeof(page_pfn));
	count = amdgpu_umc_lookup_bad_pages_in_a_row(adev,
				ecc_err->pa_pfn << AMDGPU_GPU_PAGE_SHIFT,
				page_pfn, ARRAY_SIZE(page_pfn));

	for (i = 0; i < count; i++) {
		ret = amdgpu_umc_fill_error_record(err_data,
				ecc_err->addr,
				page_pfn[i] << AMDGPU_GPU_PAGE_SHIFT,
				ecc_err->channel_idx,
				MCA_IPID_2_UMC_INST(ecc_err->ipid));
		if (ret)
			break;
	}

	err_data->de_count++;

	return ret;
}

static void umc_v12_0_query_ras_ecc_err_addr(struct amdgpu_device *adev,
					void *ras_error_status)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	struct ras_ecc_err *entries[MAX_ECC_NUM_PER_RETIREMENT];
	struct radix_tree_root *ecc_tree;
	int new_detected, ret, i;

	ecc_tree = &con->umc_ecc_log.de_page_tree;

	mutex_lock(&con->umc_ecc_log.lock);
	new_detected = radix_tree_gang_lookup_tag(ecc_tree, (void **)entries,
			0, ARRAY_SIZE(entries), UMC_ECC_NEW_DETECTED_TAG);
	for (i = 0; i < new_detected; i++) {
		if (!entries[i])
			continue;

		ret = umc_v12_0_fill_error_record(adev, entries[i], ras_error_status);
		if (ret) {
			dev_err(adev->dev, "Fail to fill umc error record, ret:%d\n", ret);
			break;
		}
		radix_tree_tag_clear(ecc_tree,
				entries[i]->pa_pfn, UMC_ECC_NEW_DETECTED_TAG);
	}
	mutex_unlock(&con->umc_ecc_log.lock);
}

static uint32_t umc_v12_0_get_die_id(struct amdgpu_device *adev,
		uint64_t mca_addr, uint64_t retired_page)
{
	uint32_t die = 0;

	/* we only calculate die id for nps1 mode right now */
	die += ((((retired_page >> 12) & 0x1ULL)^
	    ((retired_page >> 20) & 0x1ULL) ^
	    ((retired_page >> 27) & 0x1ULL) ^
	    ((retired_page >> 34) & 0x1ULL) ^
	    ((retired_page >> 41) & 0x1ULL)) << 0);

	/* the original PA_C4 and PA_R13 may be cleared in retired_page, so
	 * get them from mca_addr.
	 */
	die += ((((retired_page >> 13) & 0x1ULL) ^
	    ((mca_addr >> 5) & 0x1ULL) ^
	    ((retired_page >> 28) & 0x1ULL) ^
	    ((mca_addr >> 23) & 0x1ULL) ^
	    ((retired_page >> 42) & 0x1ULL)) << 1);
	die &= 3;

	return die;
}

struct amdgpu_umc_ras umc_v12_0_ras = {
	.ras_block = {
		.hw_ops = &umc_v12_0_ras_hw_ops,
		.ras_late_init = umc_v12_0_ras_late_init,
	},
	.err_cnt_init = umc_v12_0_err_cnt_init,
	.query_ras_poison_mode = umc_v12_0_query_ras_poison_mode,
	.ecc_info_query_ras_error_address = umc_v12_0_query_ras_ecc_err_addr,
	.check_ecc_err_status = umc_v12_0_check_ecc_err_status,
	.update_ecc_status = umc_v12_0_update_ecc_status,
	.convert_ras_err_addr = umc_v12_0_convert_error_address,
	.get_die_id_from_pa = umc_v12_0_get_die_id,
};

