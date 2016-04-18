/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
#include <linux/firmware.h>
#include "drmP.h"
#include "amdgpu.h"
#include "smu8.h"
#include "smu8_fusion.h"
#include "cz_ppsmc.h"
#include "cz_smumgr.h"
#include "smu_ucode_xfer_cz.h"
#include "amdgpu_ucode.h"

#include "smu/smu_8_0_d.h"
#include "smu/smu_8_0_sh_mask.h"
#include "gca/gfx_8_0_d.h"
#include "gca/gfx_8_0_sh_mask.h"

uint32_t cz_get_argument(struct amdgpu_device *adev)
{
	return RREG32(mmSMU_MP1_SRBM2P_ARG_0);
}

static struct cz_smu_private_data *cz_smu_get_priv(struct amdgpu_device *adev)
{
	struct cz_smu_private_data *priv =
			(struct cz_smu_private_data *)(adev->smu.priv);

	return priv;
}

int cz_send_msg_to_smc_async(struct amdgpu_device *adev, u16 msg)
{
	int i;
	u32 content = 0, tmp;

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = REG_GET_FIELD(RREG32(mmSMU_MP1_SRBM2P_RESP_0),
				SMU_MP1_SRBM2P_RESP_0, CONTENT);
		if (content != tmp)
			break;
		udelay(1);
	}

	/* timeout means wrong logic*/
	if (i == adev->usec_timeout)
		return -EINVAL;

	WREG32(mmSMU_MP1_SRBM2P_RESP_0, 0);
	WREG32(mmSMU_MP1_SRBM2P_MSG_0, msg);

	return 0;
}

int cz_send_msg_to_smc(struct amdgpu_device *adev, u16 msg)
{
	int i;
	u32 content = 0, tmp = 0;

	if (cz_send_msg_to_smc_async(adev, msg))
		return -EINVAL;

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = REG_GET_FIELD(RREG32(mmSMU_MP1_SRBM2P_RESP_0),
				SMU_MP1_SRBM2P_RESP_0, CONTENT);
		if (content != tmp)
			break;
		udelay(1);
	}

	/* timeout means wrong logic*/
	if (i == adev->usec_timeout)
		return -EINVAL;

	if (PPSMC_Result_OK != tmp) {
		dev_err(adev->dev, "SMC Failed to send Message.\n");
		return -EINVAL;
	}

	return 0;
}

int cz_send_msg_to_smc_with_parameter_async(struct amdgpu_device *adev,
						u16 msg, u32 parameter)
{
	WREG32(mmSMU_MP1_SRBM2P_ARG_0, parameter);
	return cz_send_msg_to_smc_async(adev, msg);
}

int cz_send_msg_to_smc_with_parameter(struct amdgpu_device *adev,
						u16 msg, u32 parameter)
{
	WREG32(mmSMU_MP1_SRBM2P_ARG_0, parameter);
	return cz_send_msg_to_smc(adev, msg);
}

static int cz_set_smc_sram_address(struct amdgpu_device *adev,
						u32 smc_address, u32 limit)
{
	if (smc_address & 3)
		return -EINVAL;
	if ((smc_address + 3) > limit)
		return -EINVAL;

	WREG32(mmMP0PUB_IND_INDEX_0, SMN_MP1_SRAM_START_ADDR + smc_address);

	return 0;
}

int cz_read_smc_sram_dword(struct amdgpu_device *adev, u32 smc_address,
						u32 *value, u32 limit)
{
	int ret;

	ret = cz_set_smc_sram_address(adev, smc_address, limit);
	if (ret)
		return ret;

	*value = RREG32(mmMP0PUB_IND_DATA_0);

	return 0;
}

int cz_write_smc_sram_dword(struct amdgpu_device *adev, u32 smc_address,
						u32 value, u32 limit)
{
	int ret;

	ret = cz_set_smc_sram_address(adev, smc_address, limit);
	if (ret)
		return ret;

	WREG32(mmMP0PUB_IND_DATA_0, value);

	return 0;
}

static int cz_smu_request_load_fw(struct amdgpu_device *adev)
{
	struct cz_smu_private_data *priv = cz_smu_get_priv(adev);

	uint32_t smc_addr = SMU8_FIRMWARE_HEADER_LOCATION +
			offsetof(struct SMU8_Firmware_Header, UcodeLoadStatus);

	cz_write_smc_sram_dword(adev, smc_addr, 0, smc_addr + 4);

	/*prepare toc buffers*/
	cz_send_msg_to_smc_with_parameter(adev,
				PPSMC_MSG_DriverDramAddrHi,
				priv->toc_buffer.mc_addr_high);
	cz_send_msg_to_smc_with_parameter(adev,
				PPSMC_MSG_DriverDramAddrLo,
				priv->toc_buffer.mc_addr_low);
	cz_send_msg_to_smc(adev, PPSMC_MSG_InitJobs);

	/*execute jobs*/
	cz_send_msg_to_smc_with_parameter(adev,
				PPSMC_MSG_ExecuteJob,
				priv->toc_entry_aram);

	cz_send_msg_to_smc_with_parameter(adev,
				PPSMC_MSG_ExecuteJob,
				priv->toc_entry_power_profiling_index);

	cz_send_msg_to_smc_with_parameter(adev,
				PPSMC_MSG_ExecuteJob,
				priv->toc_entry_initialize_index);

	return 0;
}

/*
 *Check if the FW has been loaded, SMU will not return if loading
 *has not finished.
 */
static int cz_smu_check_fw_load_finish(struct amdgpu_device *adev,
						uint32_t fw_mask)
{
	int i;
	uint32_t index = SMN_MP1_SRAM_START_ADDR +
			SMU8_FIRMWARE_HEADER_LOCATION +
			offsetof(struct SMU8_Firmware_Header, UcodeLoadStatus);

	WREG32(mmMP0PUB_IND_INDEX, index);

	for (i = 0; i < adev->usec_timeout; i++) {
		if (fw_mask == (RREG32(mmMP0PUB_IND_DATA) & fw_mask))
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout) {
		dev_err(adev->dev,
		"SMU check loaded firmware failed, expecting 0x%x, getting 0x%x",
		fw_mask, RREG32(mmMP0PUB_IND_DATA));
		return -EINVAL;
	}

	return 0;
}

/*
 * interfaces for different ip blocks to check firmware loading status
 * 0 for success otherwise failed
 */
static int cz_smu_check_finished(struct amdgpu_device *adev,
							enum AMDGPU_UCODE_ID id)
{
	switch (id) {
	case AMDGPU_UCODE_ID_SDMA0:
		if (adev->smu.fw_flags & AMDGPU_SDMA0_UCODE_LOADED)
			return 0;
		break;
	case AMDGPU_UCODE_ID_SDMA1:
		if (adev->smu.fw_flags & AMDGPU_SDMA1_UCODE_LOADED)
			return 0;
		break;
	case AMDGPU_UCODE_ID_CP_CE:
		if (adev->smu.fw_flags & AMDGPU_CPCE_UCODE_LOADED)
			return 0;
		break;
	case AMDGPU_UCODE_ID_CP_PFP:
		if (adev->smu.fw_flags & AMDGPU_CPPFP_UCODE_LOADED)
			return 0;
	case AMDGPU_UCODE_ID_CP_ME:
		if (adev->smu.fw_flags & AMDGPU_CPME_UCODE_LOADED)
			return 0;
		break;
	case AMDGPU_UCODE_ID_CP_MEC1:
		if (adev->smu.fw_flags & AMDGPU_CPMEC1_UCODE_LOADED)
			return 0;
		break;
	case AMDGPU_UCODE_ID_CP_MEC2:
		if (adev->smu.fw_flags & AMDGPU_CPMEC2_UCODE_LOADED)
			return 0;
		break;
	case AMDGPU_UCODE_ID_RLC_G:
		if (adev->smu.fw_flags & AMDGPU_CPRLC_UCODE_LOADED)
			return 0;
		break;
	case AMDGPU_UCODE_ID_MAXIMUM:
	default:
		break;
	}

	return 1;
}

static int cz_load_mec_firmware(struct amdgpu_device *adev)
{
	struct amdgpu_firmware_info *ucode =
				&adev->firmware.ucode[AMDGPU_UCODE_ID_CP_MEC1];
	uint32_t reg_data;
	uint32_t tmp;

	if (ucode->fw == NULL)
		return -EINVAL;

	/* Disable MEC parsing/prefetching */
	tmp = RREG32(mmCP_MEC_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_MEC_CNTL, MEC_ME1_HALT, 1);
	tmp = REG_SET_FIELD(tmp, CP_MEC_CNTL, MEC_ME2_HALT, 1);
	WREG32(mmCP_MEC_CNTL, tmp);

	tmp = RREG32(mmCP_CPC_IC_BASE_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, VMID, 0);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, ATC, 0);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, CACHE_POLICY, 0);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, MTYPE, 1);
	WREG32(mmCP_CPC_IC_BASE_CNTL, tmp);

	reg_data = lower_32_bits(ucode->mc_addr) &
			REG_FIELD_MASK(CP_CPC_IC_BASE_LO, IC_BASE_LO);
	WREG32(mmCP_CPC_IC_BASE_LO, reg_data);

	reg_data = upper_32_bits(ucode->mc_addr) &
			REG_FIELD_MASK(CP_CPC_IC_BASE_HI, IC_BASE_HI);
	WREG32(mmCP_CPC_IC_BASE_HI, reg_data);

	return 0;
}

int cz_smu_start(struct amdgpu_device *adev)
{
	int ret = 0;

	uint32_t fw_to_check = UCODE_ID_RLC_G_MASK |
				UCODE_ID_SDMA0_MASK |
				UCODE_ID_SDMA1_MASK |
				UCODE_ID_CP_CE_MASK |
				UCODE_ID_CP_ME_MASK |
				UCODE_ID_CP_PFP_MASK |
				UCODE_ID_CP_MEC_JT1_MASK |
				UCODE_ID_CP_MEC_JT2_MASK;

	if (adev->asic_type == CHIP_STONEY)
		fw_to_check &= ~(UCODE_ID_SDMA1_MASK | UCODE_ID_CP_MEC_JT2_MASK);

	cz_smu_request_load_fw(adev);
	ret = cz_smu_check_fw_load_finish(adev, fw_to_check);
	if (ret)
		return ret;

	/* manually load MEC firmware for CZ */
	if (adev->asic_type == CHIP_CARRIZO || adev->asic_type == CHIP_STONEY) {
		ret = cz_load_mec_firmware(adev);
		if (ret) {
			dev_err(adev->dev, "(%d) Mec Firmware load failed\n", ret);
			return ret;
		}
	}

	/* setup fw load flag */
	adev->smu.fw_flags = AMDGPU_SDMA0_UCODE_LOADED |
				AMDGPU_SDMA1_UCODE_LOADED |
				AMDGPU_CPCE_UCODE_LOADED |
				AMDGPU_CPPFP_UCODE_LOADED |
				AMDGPU_CPME_UCODE_LOADED |
				AMDGPU_CPMEC1_UCODE_LOADED |
				AMDGPU_CPMEC2_UCODE_LOADED |
				AMDGPU_CPRLC_UCODE_LOADED;

	if (adev->asic_type == CHIP_STONEY)
		adev->smu.fw_flags &= ~(AMDGPU_SDMA1_UCODE_LOADED | AMDGPU_CPMEC2_UCODE_LOADED);

	return ret;
}

static uint32_t cz_convert_fw_type(uint32_t fw_type)
{
	enum AMDGPU_UCODE_ID result = AMDGPU_UCODE_ID_MAXIMUM;

	switch (fw_type) {
	case UCODE_ID_SDMA0:
		result = AMDGPU_UCODE_ID_SDMA0;
		break;
	case UCODE_ID_SDMA1:
		result = AMDGPU_UCODE_ID_SDMA1;
		break;
	case UCODE_ID_CP_CE:
		result = AMDGPU_UCODE_ID_CP_CE;
		break;
	case UCODE_ID_CP_PFP:
		result = AMDGPU_UCODE_ID_CP_PFP;
		break;
	case UCODE_ID_CP_ME:
		result = AMDGPU_UCODE_ID_CP_ME;
		break;
	case UCODE_ID_CP_MEC_JT1:
	case UCODE_ID_CP_MEC_JT2:
		result = AMDGPU_UCODE_ID_CP_MEC1;
		break;
	case UCODE_ID_RLC_G:
		result = AMDGPU_UCODE_ID_RLC_G;
		break;
	default:
		DRM_ERROR("UCode type is out of range!");
	}

	return result;
}

static uint8_t cz_smu_translate_firmware_enum_to_arg(
			enum cz_scratch_entry firmware_enum)
{
	uint8_t ret = 0;

	switch (firmware_enum) {
	case CZ_SCRATCH_ENTRY_UCODE_ID_SDMA0:
		ret = UCODE_ID_SDMA0;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_SDMA1:
		ret = UCODE_ID_SDMA1;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_CP_CE:
		ret = UCODE_ID_CP_CE;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_CP_PFP:
		ret = UCODE_ID_CP_PFP;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_CP_ME:
		ret = UCODE_ID_CP_ME;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1:
		ret = UCODE_ID_CP_MEC_JT1;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT2:
		ret = UCODE_ID_CP_MEC_JT2;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_GMCON_RENG:
		ret = UCODE_ID_GMCON_RENG;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_RLC_G:
		ret = UCODE_ID_RLC_G;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SCRATCH:
		ret = UCODE_ID_RLC_SCRATCH;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_ARAM:
		ret = UCODE_ID_RLC_SRM_ARAM;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_DRAM:
		ret = UCODE_ID_RLC_SRM_DRAM;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_DMCU_ERAM:
		ret = UCODE_ID_DMCU_ERAM;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_DMCU_IRAM:
		ret = UCODE_ID_DMCU_IRAM;
		break;
	case CZ_SCRATCH_ENTRY_UCODE_ID_POWER_PROFILING:
		ret = TASK_ARG_INIT_MM_PWR_LOG;
		break;
	case CZ_SCRATCH_ENTRY_DATA_ID_SDMA_HALT:
	case CZ_SCRATCH_ENTRY_DATA_ID_SYS_CLOCKGATING:
	case CZ_SCRATCH_ENTRY_DATA_ID_SDMA_RING_REGS:
	case CZ_SCRATCH_ENTRY_DATA_ID_NONGFX_REINIT:
	case CZ_SCRATCH_ENTRY_DATA_ID_SDMA_START:
	case CZ_SCRATCH_ENTRY_DATA_ID_IH_REGISTERS:
		ret = TASK_ARG_REG_MMIO;
		break;
	case CZ_SCRATCH_ENTRY_SMU8_FUSION_CLKTABLE:
		ret = TASK_ARG_INIT_CLK_TABLE;
		break;
	}

	return ret;
}

static int cz_smu_populate_single_firmware_entry(struct amdgpu_device *adev,
					enum cz_scratch_entry firmware_enum,
					struct cz_buffer_entry *entry)
{
	uint64_t gpu_addr;
	uint32_t data_size;
	uint8_t ucode_id = cz_smu_translate_firmware_enum_to_arg(firmware_enum);
	enum AMDGPU_UCODE_ID id = cz_convert_fw_type(ucode_id);
	struct amdgpu_firmware_info *ucode = &adev->firmware.ucode[id];
	const struct gfx_firmware_header_v1_0 *header;

	if (ucode->fw == NULL)
		return -EINVAL;

	gpu_addr  = ucode->mc_addr;
	header = (const struct gfx_firmware_header_v1_0 *)ucode->fw->data;
	data_size = le32_to_cpu(header->header.ucode_size_bytes);

	if ((firmware_enum == CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1) ||
	    (firmware_enum == CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT2)) {
		gpu_addr += le32_to_cpu(header->jt_offset) << 2;
		data_size = le32_to_cpu(header->jt_size) << 2;
	}

	entry->mc_addr_low = lower_32_bits(gpu_addr);
	entry->mc_addr_high = upper_32_bits(gpu_addr);
	entry->data_size = data_size;
	entry->firmware_ID = firmware_enum;

	return 0;
}

static int cz_smu_populate_single_scratch_entry(struct amdgpu_device *adev,
					enum cz_scratch_entry scratch_type,
					uint32_t size_in_byte,
					struct cz_buffer_entry *entry)
{
	struct cz_smu_private_data *priv = cz_smu_get_priv(adev);
	uint64_t mc_addr = (((uint64_t) priv->smu_buffer.mc_addr_high) << 32) |
						priv->smu_buffer.mc_addr_low;
	mc_addr += size_in_byte;

	priv->smu_buffer_used_bytes += size_in_byte;
	entry->data_size = size_in_byte;
	entry->kaddr = priv->smu_buffer.kaddr + priv->smu_buffer_used_bytes;
	entry->mc_addr_low = lower_32_bits(mc_addr);
	entry->mc_addr_high = upper_32_bits(mc_addr);
	entry->firmware_ID = scratch_type;

	return 0;
}

static int cz_smu_populate_single_ucode_load_task(struct amdgpu_device *adev,
						enum cz_scratch_entry firmware_enum,
						bool is_last)
{
	uint8_t i;
	struct cz_smu_private_data *priv = cz_smu_get_priv(adev);
	struct TOC *toc = (struct TOC *)priv->toc_buffer.kaddr;
	struct SMU_Task *task = &toc->tasks[priv->toc_entry_used_count++];

	task->type = TASK_TYPE_UCODE_LOAD;
	task->arg = cz_smu_translate_firmware_enum_to_arg(firmware_enum);
	task->next = is_last ? END_OF_TASK_LIST : priv->toc_entry_used_count;

	for (i = 0; i < priv->driver_buffer_length; i++)
		if (priv->driver_buffer[i].firmware_ID == firmware_enum)
			break;

	if (i >= priv->driver_buffer_length) {
		dev_err(adev->dev, "Invalid Firmware Type\n");
		return -EINVAL;
	}

	task->addr.low = priv->driver_buffer[i].mc_addr_low;
	task->addr.high = priv->driver_buffer[i].mc_addr_high;
	task->size_bytes = priv->driver_buffer[i].data_size;

	return 0;
}

static int cz_smu_populate_single_scratch_task(struct amdgpu_device *adev,
						enum cz_scratch_entry firmware_enum,
						uint8_t type, bool is_last)
{
	uint8_t i;
	struct cz_smu_private_data *priv = cz_smu_get_priv(adev);
	struct TOC *toc = (struct TOC *)priv->toc_buffer.kaddr;
	struct SMU_Task *task = &toc->tasks[priv->toc_entry_used_count++];

	task->type = type;
	task->arg = cz_smu_translate_firmware_enum_to_arg(firmware_enum);
	task->next = is_last ? END_OF_TASK_LIST : priv->toc_entry_used_count;

	for (i = 0; i < priv->scratch_buffer_length; i++)
		if (priv->scratch_buffer[i].firmware_ID == firmware_enum)
			break;

	if (i >= priv->scratch_buffer_length) {
		dev_err(adev->dev, "Invalid Firmware Type\n");
		return -EINVAL;
	}

	task->addr.low = priv->scratch_buffer[i].mc_addr_low;
	task->addr.high = priv->scratch_buffer[i].mc_addr_high;
	task->size_bytes = priv->scratch_buffer[i].data_size;

	if (CZ_SCRATCH_ENTRY_DATA_ID_IH_REGISTERS == firmware_enum) {
		struct cz_ih_meta_data *pIHReg_restore =
			(struct cz_ih_meta_data *)priv->scratch_buffer[i].kaddr;
		pIHReg_restore->command =
			METADATA_CMD_MODE0 | METADATA_PERFORM_ON_LOAD;
	}

	return 0;
}

static int cz_smu_construct_toc_for_rlc_aram_save(struct amdgpu_device *adev)
{
	struct cz_smu_private_data *priv = cz_smu_get_priv(adev);
	priv->toc_entry_aram = priv->toc_entry_used_count;
	cz_smu_populate_single_scratch_task(adev,
			CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_ARAM,
			TASK_TYPE_UCODE_SAVE, true);

	return 0;
}

static int cz_smu_construct_toc_for_vddgfx_enter(struct amdgpu_device *adev)
{
	struct cz_smu_private_data *priv = cz_smu_get_priv(adev);
	struct TOC *toc = (struct TOC *)priv->toc_buffer.kaddr;

	toc->JobList[JOB_GFX_SAVE] = (uint8_t)priv->toc_entry_used_count;
	cz_smu_populate_single_scratch_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SCRATCH,
				TASK_TYPE_UCODE_SAVE, false);
	cz_smu_populate_single_scratch_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_DRAM,
				TASK_TYPE_UCODE_SAVE, true);

	return 0;
}

static int cz_smu_construct_toc_for_vddgfx_exit(struct amdgpu_device *adev)
{
	struct cz_smu_private_data *priv = cz_smu_get_priv(adev);
	struct TOC *toc = (struct TOC *)priv->toc_buffer.kaddr;

	toc->JobList[JOB_GFX_RESTORE] = (uint8_t)priv->toc_entry_used_count;

	/* populate ucode */
	if (adev->firmware.smu_load) {
		cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_CE, false);
		cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_PFP, false);
		cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_ME, false);
		cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1, false);
		if (adev->asic_type == CHIP_STONEY) {
			cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1, false);
		} else {
			cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT2, false);
		}
		cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_RLC_G, false);
	}

	/* populate scratch */
	cz_smu_populate_single_scratch_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SCRATCH,
				TASK_TYPE_UCODE_LOAD, false);
	cz_smu_populate_single_scratch_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_ARAM,
				TASK_TYPE_UCODE_LOAD, false);
	cz_smu_populate_single_scratch_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_DRAM,
				TASK_TYPE_UCODE_LOAD, true);

	return 0;
}

static int cz_smu_construct_toc_for_power_profiling(struct amdgpu_device *adev)
{
	struct cz_smu_private_data *priv = cz_smu_get_priv(adev);

	priv->toc_entry_power_profiling_index = priv->toc_entry_used_count;

	cz_smu_populate_single_scratch_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_POWER_PROFILING,
				TASK_TYPE_INITIALIZE, true);
	return 0;
}

static int cz_smu_construct_toc_for_bootup(struct amdgpu_device *adev)
{
	struct cz_smu_private_data *priv = cz_smu_get_priv(adev);

	priv->toc_entry_initialize_index = priv->toc_entry_used_count;

	if (adev->firmware.smu_load) {
		cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_SDMA0, false);
		if (adev->asic_type == CHIP_STONEY) {
			cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_SDMA0, false);
		} else {
			cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_SDMA1, false);
		}
		cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_CE, false);
		cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_PFP, false);
		cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_ME, false);
		cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1, false);
		if (adev->asic_type == CHIP_STONEY) {
			cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1, false);
		} else {
			cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT2, false);
		}
		cz_smu_populate_single_ucode_load_task(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_RLC_G, true);
	}

	return 0;
}

static int cz_smu_construct_toc_for_clock_table(struct amdgpu_device *adev)
{
	struct cz_smu_private_data *priv = cz_smu_get_priv(adev);

	priv->toc_entry_clock_table = priv->toc_entry_used_count;

	cz_smu_populate_single_scratch_task(adev,
				CZ_SCRATCH_ENTRY_SMU8_FUSION_CLKTABLE,
				TASK_TYPE_INITIALIZE, true);

	return 0;
}

static int cz_smu_initialize_toc_empty_job_list(struct amdgpu_device *adev)
{
	int i;
	struct cz_smu_private_data *priv = cz_smu_get_priv(adev);
	struct TOC *toc = (struct TOC *)priv->toc_buffer.kaddr;

	for (i = 0; i < NUM_JOBLIST_ENTRIES; i++)
		toc->JobList[i] = (uint8_t)IGNORE_JOB;

	return 0;
}

/*
 * cz smu uninitialization
 */
int cz_smu_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_unref(&adev->smu.toc_buf);
	amdgpu_bo_unref(&adev->smu.smu_buf);
	kfree(adev->smu.priv);
	adev->smu.priv = NULL;
	if (adev->firmware.smu_load)
		amdgpu_ucode_fini_bo(adev);

	return 0;
}

int cz_smu_download_pptable(struct amdgpu_device *adev, void **table)
{
	uint8_t i;
	struct cz_smu_private_data *priv = cz_smu_get_priv(adev);

	for (i = 0; i < priv->scratch_buffer_length; i++)
		if (priv->scratch_buffer[i].firmware_ID ==
				CZ_SCRATCH_ENTRY_SMU8_FUSION_CLKTABLE)
			break;

	if (i >= priv->scratch_buffer_length) {
		dev_err(adev->dev, "Invalid Scratch Type\n");
		return -EINVAL;
	}

	*table = (struct SMU8_Fusion_ClkTable *)priv->scratch_buffer[i].kaddr;

	/* prepare buffer for pptable */
	cz_send_msg_to_smc_with_parameter(adev,
				PPSMC_MSG_SetClkTableAddrHi,
				priv->scratch_buffer[i].mc_addr_high);
	cz_send_msg_to_smc_with_parameter(adev,
				PPSMC_MSG_SetClkTableAddrLo,
				priv->scratch_buffer[i].mc_addr_low);
	cz_send_msg_to_smc_with_parameter(adev,
				PPSMC_MSG_ExecuteJob,
				priv->toc_entry_clock_table);

	/* actual downloading */
	cz_send_msg_to_smc(adev, PPSMC_MSG_ClkTableXferToDram);

	return 0;
}

int cz_smu_upload_pptable(struct amdgpu_device *adev)
{
	uint8_t i;
	struct cz_smu_private_data *priv = cz_smu_get_priv(adev);

	for (i = 0; i < priv->scratch_buffer_length; i++)
		if (priv->scratch_buffer[i].firmware_ID ==
				CZ_SCRATCH_ENTRY_SMU8_FUSION_CLKTABLE)
			break;

	if (i >= priv->scratch_buffer_length) {
		dev_err(adev->dev, "Invalid Scratch Type\n");
		return -EINVAL;
	}

	/* prepare SMU */
	cz_send_msg_to_smc_with_parameter(adev,
				PPSMC_MSG_SetClkTableAddrHi,
				priv->scratch_buffer[i].mc_addr_high);
	cz_send_msg_to_smc_with_parameter(adev,
				PPSMC_MSG_SetClkTableAddrLo,
				priv->scratch_buffer[i].mc_addr_low);
	cz_send_msg_to_smc_with_parameter(adev,
				PPSMC_MSG_ExecuteJob,
				priv->toc_entry_clock_table);

	/* actual uploading */
	cz_send_msg_to_smc(adev, PPSMC_MSG_ClkTableXferToSmu);

	return 0;
}

/*
 * cz smumgr functions initialization
 */
static const struct amdgpu_smumgr_funcs cz_smumgr_funcs = {
	.check_fw_load_finish = cz_smu_check_finished,
	.request_smu_load_fw = NULL,
	.request_smu_specific_fw = NULL,
};

/*
 * cz smu initialization
 */
int cz_smu_init(struct amdgpu_device *adev)
{
	int ret = -EINVAL;
	uint64_t mc_addr = 0;
	struct amdgpu_bo **toc_buf = &adev->smu.toc_buf;
	struct amdgpu_bo **smu_buf = &adev->smu.smu_buf;
	void *toc_buf_ptr = NULL;
	void *smu_buf_ptr = NULL;

	struct cz_smu_private_data *priv =
		kzalloc(sizeof(struct cz_smu_private_data), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	/* allocate firmware buffers */
	if (adev->firmware.smu_load)
		amdgpu_ucode_init_bo(adev);

	adev->smu.priv = priv;
	adev->smu.fw_flags = 0;
	priv->toc_buffer.data_size = 4096;

	priv->smu_buffer.data_size =
				ALIGN(UCODE_ID_RLC_SCRATCH_SIZE_BYTE, 32) +
				ALIGN(UCODE_ID_RLC_SRM_ARAM_SIZE_BYTE, 32) +
				ALIGN(UCODE_ID_RLC_SRM_DRAM_SIZE_BYTE, 32) +
				ALIGN(sizeof(struct SMU8_MultimediaPowerLogData), 32) +
				ALIGN(sizeof(struct SMU8_Fusion_ClkTable), 32);

	/* prepare toc buffer and smu buffer:
	* 1. create amdgpu_bo for toc buffer and smu buffer
	* 2. pin mc address
	* 3. map kernel virtual address
	*/
	ret = amdgpu_bo_create(adev, priv->toc_buffer.data_size, PAGE_SIZE,
			       true, AMDGPU_GEM_DOMAIN_GTT, 0, NULL, NULL,
			       toc_buf);

	if (ret) {
		dev_err(adev->dev, "(%d) SMC TOC buffer allocation failed\n", ret);
		return ret;
	}

	ret = amdgpu_bo_create(adev, priv->smu_buffer.data_size, PAGE_SIZE,
			       true, AMDGPU_GEM_DOMAIN_GTT, 0, NULL, NULL,
			       smu_buf);

	if (ret) {
		dev_err(adev->dev, "(%d) SMC Internal buffer allocation failed\n", ret);
		return ret;
	}

	/* toc buffer reserve/pin/map */
	ret = amdgpu_bo_reserve(adev->smu.toc_buf, false);
	if (ret) {
		amdgpu_bo_unref(&adev->smu.toc_buf);
		dev_err(adev->dev, "(%d) SMC TOC buffer reserve failed\n", ret);
		return ret;
	}

	ret = amdgpu_bo_pin(adev->smu.toc_buf, AMDGPU_GEM_DOMAIN_GTT, &mc_addr);
	if (ret) {
		amdgpu_bo_unreserve(adev->smu.toc_buf);
		amdgpu_bo_unref(&adev->smu.toc_buf);
		dev_err(adev->dev, "(%d) SMC TOC buffer pin failed\n", ret);
		return ret;
	}

	ret = amdgpu_bo_kmap(*toc_buf, &toc_buf_ptr);
	if (ret)
		goto smu_init_failed;

	amdgpu_bo_unreserve(adev->smu.toc_buf);

	priv->toc_buffer.mc_addr_low = lower_32_bits(mc_addr);
	priv->toc_buffer.mc_addr_high = upper_32_bits(mc_addr);
	priv->toc_buffer.kaddr = toc_buf_ptr;

	/* smu buffer reserve/pin/map */
	ret = amdgpu_bo_reserve(adev->smu.smu_buf, false);
	if (ret) {
		amdgpu_bo_unref(&adev->smu.smu_buf);
		dev_err(adev->dev, "(%d) SMC Internal buffer reserve failed\n", ret);
		return ret;
	}

	ret = amdgpu_bo_pin(adev->smu.smu_buf, AMDGPU_GEM_DOMAIN_GTT, &mc_addr);
	if (ret) {
		amdgpu_bo_unreserve(adev->smu.smu_buf);
		amdgpu_bo_unref(&adev->smu.smu_buf);
		dev_err(adev->dev, "(%d) SMC Internal buffer pin failed\n", ret);
		return ret;
	}

	ret = amdgpu_bo_kmap(*smu_buf, &smu_buf_ptr);
	if (ret)
		goto smu_init_failed;

	amdgpu_bo_unreserve(adev->smu.smu_buf);

	priv->smu_buffer.mc_addr_low = lower_32_bits(mc_addr);
	priv->smu_buffer.mc_addr_high = upper_32_bits(mc_addr);
	priv->smu_buffer.kaddr = smu_buf_ptr;

	if (adev->firmware.smu_load) {
		if (cz_smu_populate_single_firmware_entry(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_SDMA0,
				&priv->driver_buffer[priv->driver_buffer_length++]))
			goto smu_init_failed;

		if (adev->asic_type == CHIP_STONEY) {
			if (cz_smu_populate_single_firmware_entry(adev,
					CZ_SCRATCH_ENTRY_UCODE_ID_SDMA0,
					&priv->driver_buffer[priv->driver_buffer_length++]))
				goto smu_init_failed;
		} else {
			if (cz_smu_populate_single_firmware_entry(adev,
					CZ_SCRATCH_ENTRY_UCODE_ID_SDMA1,
					&priv->driver_buffer[priv->driver_buffer_length++]))
				goto smu_init_failed;
		}
		if (cz_smu_populate_single_firmware_entry(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_CE,
				&priv->driver_buffer[priv->driver_buffer_length++]))
			goto smu_init_failed;
		if (cz_smu_populate_single_firmware_entry(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_PFP,
				&priv->driver_buffer[priv->driver_buffer_length++]))
			goto smu_init_failed;
		if (cz_smu_populate_single_firmware_entry(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_ME,
				&priv->driver_buffer[priv->driver_buffer_length++]))
			goto smu_init_failed;
		if (cz_smu_populate_single_firmware_entry(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1,
				&priv->driver_buffer[priv->driver_buffer_length++]))
			goto smu_init_failed;
		if (adev->asic_type == CHIP_STONEY) {
			if (cz_smu_populate_single_firmware_entry(adev,
					CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1,
					&priv->driver_buffer[priv->driver_buffer_length++]))
				goto smu_init_failed;
		} else {
			if (cz_smu_populate_single_firmware_entry(adev,
					CZ_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT2,
					&priv->driver_buffer[priv->driver_buffer_length++]))
				goto smu_init_failed;
		}
		if (cz_smu_populate_single_firmware_entry(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_RLC_G,
				&priv->driver_buffer[priv->driver_buffer_length++]))
			goto smu_init_failed;
	}

	if (cz_smu_populate_single_scratch_entry(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SCRATCH,
				UCODE_ID_RLC_SCRATCH_SIZE_BYTE,
				&priv->scratch_buffer[priv->scratch_buffer_length++]))
		goto smu_init_failed;
	if (cz_smu_populate_single_scratch_entry(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_ARAM,
				UCODE_ID_RLC_SRM_ARAM_SIZE_BYTE,
				&priv->scratch_buffer[priv->scratch_buffer_length++]))
		goto smu_init_failed;
	if (cz_smu_populate_single_scratch_entry(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_DRAM,
				UCODE_ID_RLC_SRM_DRAM_SIZE_BYTE,
				&priv->scratch_buffer[priv->scratch_buffer_length++]))
		goto smu_init_failed;
	if (cz_smu_populate_single_scratch_entry(adev,
				CZ_SCRATCH_ENTRY_UCODE_ID_POWER_PROFILING,
				sizeof(struct SMU8_MultimediaPowerLogData),
				&priv->scratch_buffer[priv->scratch_buffer_length++]))
		goto smu_init_failed;
	if (cz_smu_populate_single_scratch_entry(adev,
				CZ_SCRATCH_ENTRY_SMU8_FUSION_CLKTABLE,
				sizeof(struct SMU8_Fusion_ClkTable),
				&priv->scratch_buffer[priv->scratch_buffer_length++]))
		goto smu_init_failed;

	cz_smu_initialize_toc_empty_job_list(adev);
	cz_smu_construct_toc_for_rlc_aram_save(adev);
	cz_smu_construct_toc_for_vddgfx_enter(adev);
	cz_smu_construct_toc_for_vddgfx_exit(adev);
	cz_smu_construct_toc_for_power_profiling(adev);
	cz_smu_construct_toc_for_bootup(adev);
	cz_smu_construct_toc_for_clock_table(adev);
	/* init the smumgr functions */
	adev->smu.smumgr_funcs = &cz_smumgr_funcs;

	return 0;

smu_init_failed:
	amdgpu_bo_unref(toc_buf);
	amdgpu_bo_unref(smu_buf);

	return ret;
}
