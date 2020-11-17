/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * Author: Huang Rui
 *
 */

#include <linux/firmware.h>
#include <linux/dma-mapping.h>

#include "amdgpu.h"
#include "amdgpu_psp.h"
#include "amdgpu_ucode.h"
#include "soc15_common.h"
#include "psp_v3_1.h"
#include "psp_v10_0.h"
#include "psp_v11_0.h"
#include "psp_v12_0.h"

#include "amdgpu_ras.h"

static int psp_sysfs_init(struct amdgpu_device *adev);
static void psp_sysfs_fini(struct amdgpu_device *adev);

static int psp_load_smu_fw(struct psp_context *psp);

/*
 * Due to DF Cstate management centralized to PMFW, the firmware
 * loading sequence will be updated as below:
 *   - Load KDB
 *   - Load SYS_DRV
 *   - Load tOS
 *   - Load PMFW
 *   - Setup TMR
 *   - Load other non-psp fw
 *   - Load ASD
 *   - Load XGMI/RAS/HDCP/DTM TA if any
 *
 * This new sequence is required for
 *   - Arcturus
 *   - Navi12 and onwards
 */
static void psp_check_pmfw_centralized_cstate_management(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;

	psp->pmfw_centralized_cstate_management = false;

	if (amdgpu_sriov_vf(adev))
		return;

	if (adev->flags & AMD_IS_APU)
		return;

	if ((adev->asic_type == CHIP_ARCTURUS) ||
	    (adev->asic_type >= CHIP_NAVI12))
		psp->pmfw_centralized_cstate_management = true;
}

static int psp_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct psp_context *psp = &adev->psp;

	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_VEGA12:
		psp_v3_1_set_psp_funcs(psp);
		psp->autoload_supported = false;
		break;
	case CHIP_RAVEN:
		psp_v10_0_set_psp_funcs(psp);
		psp->autoload_supported = false;
		break;
	case CHIP_VEGA20:
	case CHIP_ARCTURUS:
		psp_v11_0_set_psp_funcs(psp);
		psp->autoload_supported = false;
		break;
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
	case CHIP_SIENNA_CICHLID:
	case CHIP_NAVY_FLOUNDER:
		psp_v11_0_set_psp_funcs(psp);
		psp->autoload_supported = true;
		break;
	case CHIP_RENOIR:
		psp_v12_0_set_psp_funcs(psp);
		break;
	default:
		return -EINVAL;
	}

	psp->adev = adev;

	psp_check_pmfw_centralized_cstate_management(psp);

	return 0;
}

static void psp_memory_training_fini(struct psp_context *psp)
{
	struct psp_memory_training_context *ctx = &psp->mem_train_ctx;

	ctx->init = PSP_MEM_TRAIN_NOT_SUPPORT;
	kfree(ctx->sys_cache);
	ctx->sys_cache = NULL;
}

static int psp_memory_training_init(struct psp_context *psp)
{
	int ret;
	struct psp_memory_training_context *ctx = &psp->mem_train_ctx;

	if (ctx->init != PSP_MEM_TRAIN_RESERVE_SUCCESS) {
		DRM_DEBUG("memory training is not supported!\n");
		return 0;
	}

	ctx->sys_cache = kzalloc(ctx->train_data_size, GFP_KERNEL);
	if (ctx->sys_cache == NULL) {
		DRM_ERROR("alloc mem_train_ctx.sys_cache failed!\n");
		ret = -ENOMEM;
		goto Err_out;
	}

	DRM_DEBUG("train_data_size:%llx,p2c_train_data_offset:%llx,c2p_train_data_offset:%llx.\n",
		  ctx->train_data_size,
		  ctx->p2c_train_data_offset,
		  ctx->c2p_train_data_offset);
	ctx->init = PSP_MEM_TRAIN_INIT_SUCCESS;
	return 0;

Err_out:
	psp_memory_training_fini(psp);
	return ret;
}

static int psp_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct psp_context *psp = &adev->psp;
	int ret;

	if (!amdgpu_sriov_vf(adev)) {
		ret = psp_init_microcode(psp);
		if (ret) {
			DRM_ERROR("Failed to load psp firmware!\n");
			return ret;
		}
	}

	ret = psp_memory_training_init(psp);
	if (ret) {
		DRM_ERROR("Failed to initialize memory training!\n");
		return ret;
	}
	ret = psp_mem_training(psp, PSP_MEM_TRAIN_COLD_BOOT);
	if (ret) {
		DRM_ERROR("Failed to process memory training!\n");
		return ret;
	}

	if (adev->asic_type == CHIP_NAVI10 || adev->asic_type == CHIP_SIENNA_CICHLID) {
		ret= psp_sysfs_init(adev);
		if (ret) {
			return ret;
		}
	}

	return 0;
}

static int psp_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	psp_memory_training_fini(&adev->psp);
	if (adev->psp.sos_fw) {
		release_firmware(adev->psp.sos_fw);
		adev->psp.sos_fw = NULL;
	}
	if (adev->psp.asd_fw) {
		release_firmware(adev->psp.asd_fw);
		adev->psp.asd_fw = NULL;
	}
	if (adev->psp.ta_fw) {
		release_firmware(adev->psp.ta_fw);
		adev->psp.ta_fw = NULL;
	}

	if (adev->asic_type == CHIP_NAVI10 ||
	    adev->asic_type == CHIP_SIENNA_CICHLID)
		psp_sysfs_fini(adev);

	return 0;
}

int psp_wait_for(struct psp_context *psp, uint32_t reg_index,
		 uint32_t reg_val, uint32_t mask, bool check_changed)
{
	uint32_t val;
	int i;
	struct amdgpu_device *adev = psp->adev;

	if (psp->adev->in_pci_err_recovery)
		return 0;

	for (i = 0; i < adev->usec_timeout; i++) {
		val = RREG32(reg_index);
		if (check_changed) {
			if (val != reg_val)
				return 0;
		} else {
			if ((val & mask) == reg_val)
				return 0;
		}
		udelay(1);
	}

	return -ETIME;
}

static int
psp_cmd_submit_buf(struct psp_context *psp,
		   struct amdgpu_firmware_info *ucode,
		   struct psp_gfx_cmd_resp *cmd, uint64_t fence_mc_addr)
{
	int ret;
	int index;
	int timeout = 2000;
	bool ras_intr = false;
	bool skip_unsupport = false;

	if (psp->adev->in_pci_err_recovery)
		return 0;

	mutex_lock(&psp->mutex);

	memset(psp->cmd_buf_mem, 0, PSP_CMD_BUFFER_SIZE);

	memcpy(psp->cmd_buf_mem, cmd, sizeof(struct psp_gfx_cmd_resp));

	index = atomic_inc_return(&psp->fence_value);
	ret = psp_ring_cmd_submit(psp, psp->cmd_buf_mc_addr, fence_mc_addr, index);
	if (ret) {
		atomic_dec(&psp->fence_value);
		mutex_unlock(&psp->mutex);
		return ret;
	}

	amdgpu_asic_invalidate_hdp(psp->adev, NULL);
	while (*((unsigned int *)psp->fence_buf) != index) {
		if (--timeout == 0)
			break;
		/*
		 * Shouldn't wait for timeout when err_event_athub occurs,
		 * because gpu reset thread triggered and lock resource should
		 * be released for psp resume sequence.
		 */
		ras_intr = amdgpu_ras_intr_triggered();
		if (ras_intr)
			break;
		msleep(1);
		amdgpu_asic_invalidate_hdp(psp->adev, NULL);
	}

	/* We allow TEE_ERROR_NOT_SUPPORTED for VMR command and PSP_ERR_UNKNOWN_COMMAND in SRIOV */
	skip_unsupport = (psp->cmd_buf_mem->resp.status == TEE_ERROR_NOT_SUPPORTED ||
		psp->cmd_buf_mem->resp.status == PSP_ERR_UNKNOWN_COMMAND) && amdgpu_sriov_vf(psp->adev);

	/* In some cases, psp response status is not 0 even there is no
	 * problem while the command is submitted. Some version of PSP FW
	 * doesn't write 0 to that field.
	 * So here we would like to only print a warning instead of an error
	 * during psp initialization to avoid breaking hw_init and it doesn't
	 * return -EINVAL.
	 */
	if (!skip_unsupport && (psp->cmd_buf_mem->resp.status || !timeout) && !ras_intr) {
		if (ucode)
			DRM_WARN("failed to load ucode id (%d) ",
				  ucode->ucode_id);
		DRM_WARN("psp command (0x%X) failed and response status is (0x%X)\n",
			 psp->cmd_buf_mem->cmd_id,
			 psp->cmd_buf_mem->resp.status);
		if (!timeout) {
			mutex_unlock(&psp->mutex);
			return -EINVAL;
		}
	}

	/* get xGMI session id from response buffer */
	cmd->resp.session_id = psp->cmd_buf_mem->resp.session_id;

	if (ucode) {
		ucode->tmr_mc_addr_lo = psp->cmd_buf_mem->resp.fw_addr_lo;
		ucode->tmr_mc_addr_hi = psp->cmd_buf_mem->resp.fw_addr_hi;
	}
	mutex_unlock(&psp->mutex);

	return ret;
}

static void psp_prep_tmr_cmd_buf(struct psp_context *psp,
				 struct psp_gfx_cmd_resp *cmd,
				 uint64_t tmr_mc, uint32_t size)
{
	if (amdgpu_sriov_vf(psp->adev))
		cmd->cmd_id = GFX_CMD_ID_SETUP_VMR;
	else
		cmd->cmd_id = GFX_CMD_ID_SETUP_TMR;
	cmd->cmd.cmd_setup_tmr.buf_phy_addr_lo = lower_32_bits(tmr_mc);
	cmd->cmd.cmd_setup_tmr.buf_phy_addr_hi = upper_32_bits(tmr_mc);
	cmd->cmd.cmd_setup_tmr.buf_size = size;
}

static void psp_prep_load_toc_cmd_buf(struct psp_gfx_cmd_resp *cmd,
				      uint64_t pri_buf_mc, uint32_t size)
{
	cmd->cmd_id = GFX_CMD_ID_LOAD_TOC;
	cmd->cmd.cmd_load_toc.toc_phy_addr_lo = lower_32_bits(pri_buf_mc);
	cmd->cmd.cmd_load_toc.toc_phy_addr_hi = upper_32_bits(pri_buf_mc);
	cmd->cmd.cmd_load_toc.toc_size = size;
}

/* Issue LOAD TOC cmd to PSP to part toc and calculate tmr size needed */
static int psp_load_toc(struct psp_context *psp,
			uint32_t *tmr_size)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	/* Copy toc to psp firmware private buffer */
	memset(psp->fw_pri_buf, 0, PSP_1_MEG);
	memcpy(psp->fw_pri_buf, psp->toc_start_addr, psp->toc_bin_size);

	psp_prep_load_toc_cmd_buf(cmd, psp->fw_pri_mc_addr, psp->toc_bin_size);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);
	if (!ret)
		*tmr_size = psp->cmd_buf_mem->resp.tmr_size;
	kfree(cmd);
	return ret;
}

/* Set up Trusted Memory Region */
static int psp_tmr_init(struct psp_context *psp)
{
	int ret;
	int tmr_size;
	void *tmr_buf;
	void **pptr;

	/*
	 * According to HW engineer, they prefer the TMR address be "naturally
	 * aligned" , e.g. the start address be an integer divide of TMR size.
	 *
	 * Note: this memory need be reserved till the driver
	 * uninitializes.
	 */
	tmr_size = PSP_TMR_SIZE;

	/* For ASICs support RLC autoload, psp will parse the toc
	 * and calculate the total size of TMR needed */
	if (!amdgpu_sriov_vf(psp->adev) &&
	    psp->toc_start_addr &&
	    psp->toc_bin_size &&
	    psp->fw_pri_buf) {
		ret = psp_load_toc(psp, &tmr_size);
		if (ret) {
			DRM_ERROR("Failed to load toc\n");
			return ret;
		}
	}

	pptr = amdgpu_sriov_vf(psp->adev) ? &tmr_buf : NULL;
	ret = amdgpu_bo_create_kernel(psp->adev, tmr_size, PSP_TMR_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &psp->tmr_bo, &psp->tmr_mc_addr, pptr);

	return ret;
}

static int psp_clear_vf_fw(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	if (!amdgpu_sriov_vf(psp->adev) || psp->adev->asic_type != CHIP_NAVI12)
		return 0;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->cmd_id = GFX_CMD_ID_CLEAR_VF_FW;

	ret = psp_cmd_submit_buf(psp, NULL, cmd, psp->fence_buf_mc_addr);
	kfree(cmd);

	return ret;
}

static bool psp_skip_tmr(struct psp_context *psp)
{
	switch (psp->adev->asic_type) {
	case CHIP_NAVI12:
	case CHIP_SIENNA_CICHLID:
		return true;
	default:
		return false;
	}
}

static int psp_tmr_load(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	/* For Navi12 and CHIP_SIENNA_CICHLID SRIOV, do not set up TMR.
	 * Already set up by host driver.
	 */
	if (amdgpu_sriov_vf(psp->adev) && psp_skip_tmr(psp))
		return 0;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	psp_prep_tmr_cmd_buf(psp, cmd, psp->tmr_mc_addr,
			     amdgpu_bo_size(psp->tmr_bo));
	DRM_INFO("reserve 0x%lx from 0x%llx for PSP TMR\n",
		 amdgpu_bo_size(psp->tmr_bo), psp->tmr_mc_addr);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);

	kfree(cmd);

	return ret;
}

static void psp_prep_tmr_unload_cmd_buf(struct psp_context *psp,
					struct psp_gfx_cmd_resp *cmd)
{
	if (amdgpu_sriov_vf(psp->adev))
		cmd->cmd_id = GFX_CMD_ID_DESTROY_VMR;
	else
		cmd->cmd_id = GFX_CMD_ID_DESTROY_TMR;
}

static int psp_tmr_unload(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	psp_prep_tmr_unload_cmd_buf(psp, cmd);
	DRM_INFO("free PSP TMR buffer\n");

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);

	kfree(cmd);

	return ret;
}

static int psp_tmr_terminate(struct psp_context *psp)
{
	int ret;
	void *tmr_buf;
	void **pptr;

	ret = psp_tmr_unload(psp);
	if (ret)
		return ret;

	/* free TMR memory buffer */
	pptr = amdgpu_sriov_vf(psp->adev) ? &tmr_buf : NULL;
	amdgpu_bo_free_kernel(&psp->tmr_bo, &psp->tmr_mc_addr, pptr);

	return 0;
}

static void psp_prep_asd_load_cmd_buf(struct psp_gfx_cmd_resp *cmd,
				uint64_t asd_mc, uint32_t size)
{
	cmd->cmd_id = GFX_CMD_ID_LOAD_ASD;
	cmd->cmd.cmd_load_ta.app_phy_addr_lo = lower_32_bits(asd_mc);
	cmd->cmd.cmd_load_ta.app_phy_addr_hi = upper_32_bits(asd_mc);
	cmd->cmd.cmd_load_ta.app_len = size;

	cmd->cmd.cmd_load_ta.cmd_buf_phy_addr_lo = 0;
	cmd->cmd.cmd_load_ta.cmd_buf_phy_addr_hi = 0;
	cmd->cmd.cmd_load_ta.cmd_buf_len = 0;
}

static int psp_asd_load(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	/* If PSP version doesn't match ASD version, asd loading will be failed.
	 * add workaround to bypass it for sriov now.
	 * TODO: add version check to make it common
	 */
	if (amdgpu_sriov_vf(psp->adev) || !psp->asd_fw)
		return 0;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);
	memcpy(psp->fw_pri_buf, psp->asd_start_addr, psp->asd_ucode_size);

	psp_prep_asd_load_cmd_buf(cmd, psp->fw_pri_mc_addr,
				  psp->asd_ucode_size);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);
	if (!ret) {
		psp->asd_context.asd_initialized = true;
		psp->asd_context.session_id = cmd->resp.session_id;
	}

	kfree(cmd);

	return ret;
}

static void psp_prep_ta_unload_cmd_buf(struct psp_gfx_cmd_resp *cmd,
				       uint32_t session_id)
{
	cmd->cmd_id = GFX_CMD_ID_UNLOAD_TA;
	cmd->cmd.cmd_unload_ta.session_id = session_id;
}

static int psp_asd_unload(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->asd_context.asd_initialized)
		return 0;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	psp_prep_ta_unload_cmd_buf(cmd, psp->asd_context.session_id);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);
	if (!ret)
		psp->asd_context.asd_initialized = false;

	kfree(cmd);

	return ret;
}

static void psp_prep_reg_prog_cmd_buf(struct psp_gfx_cmd_resp *cmd,
		uint32_t id, uint32_t value)
{
	cmd->cmd_id = GFX_CMD_ID_PROG_REG;
	cmd->cmd.cmd_setup_reg_prog.reg_value = value;
	cmd->cmd.cmd_setup_reg_prog.reg_id = id;
}

int psp_reg_program(struct psp_context *psp, enum psp_reg_prog_id reg,
		uint32_t value)
{
	struct psp_gfx_cmd_resp *cmd = NULL;
	int ret = 0;

	if (reg >= PSP_REG_LAST)
		return -EINVAL;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	psp_prep_reg_prog_cmd_buf(cmd, reg, value);
	ret = psp_cmd_submit_buf(psp, NULL, cmd, psp->fence_buf_mc_addr);

	kfree(cmd);
	return ret;
}

static void psp_prep_ta_load_cmd_buf(struct psp_gfx_cmd_resp *cmd,
				     uint64_t ta_bin_mc,
				     uint32_t ta_bin_size,
				     uint64_t ta_shared_mc,
				     uint32_t ta_shared_size)
{
	cmd->cmd_id 				= GFX_CMD_ID_LOAD_TA;
	cmd->cmd.cmd_load_ta.app_phy_addr_lo 	= lower_32_bits(ta_bin_mc);
	cmd->cmd.cmd_load_ta.app_phy_addr_hi 	= upper_32_bits(ta_bin_mc);
	cmd->cmd.cmd_load_ta.app_len 		= ta_bin_size;

	cmd->cmd.cmd_load_ta.cmd_buf_phy_addr_lo = lower_32_bits(ta_shared_mc);
	cmd->cmd.cmd_load_ta.cmd_buf_phy_addr_hi = upper_32_bits(ta_shared_mc);
	cmd->cmd.cmd_load_ta.cmd_buf_len 	 = ta_shared_size;
}

static int psp_xgmi_init_shared_buf(struct psp_context *psp)
{
	int ret;

	/*
	 * Allocate 16k memory aligned to 4k from Frame Buffer (local
	 * physical) for xgmi ta <-> Driver
	 */
	ret = amdgpu_bo_create_kernel(psp->adev, PSP_XGMI_SHARED_MEM_SIZE,
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM,
				      &psp->xgmi_context.xgmi_shared_bo,
				      &psp->xgmi_context.xgmi_shared_mc_addr,
				      &psp->xgmi_context.xgmi_shared_buf);

	return ret;
}

static void psp_prep_ta_invoke_cmd_buf(struct psp_gfx_cmd_resp *cmd,
				       uint32_t ta_cmd_id,
				       uint32_t session_id)
{
	cmd->cmd_id 				= GFX_CMD_ID_INVOKE_CMD;
	cmd->cmd.cmd_invoke_cmd.session_id 	= session_id;
	cmd->cmd.cmd_invoke_cmd.ta_cmd_id 	= ta_cmd_id;
}

static int psp_ta_invoke(struct psp_context *psp,
		  uint32_t ta_cmd_id,
		  uint32_t session_id)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	psp_prep_ta_invoke_cmd_buf(cmd, ta_cmd_id, session_id);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);

	kfree(cmd);

	return ret;
}

static int psp_xgmi_load(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	/*
	 * TODO: bypass the loading in sriov for now
	 */

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);
	memcpy(psp->fw_pri_buf, psp->ta_xgmi_start_addr, psp->ta_xgmi_ucode_size);

	psp_prep_ta_load_cmd_buf(cmd,
				 psp->fw_pri_mc_addr,
				 psp->ta_xgmi_ucode_size,
				 psp->xgmi_context.xgmi_shared_mc_addr,
				 PSP_XGMI_SHARED_MEM_SIZE);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);

	if (!ret) {
		psp->xgmi_context.initialized = 1;
		psp->xgmi_context.session_id = cmd->resp.session_id;
	}

	kfree(cmd);

	return ret;
}

static int psp_xgmi_unload(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;
	struct amdgpu_device *adev = psp->adev;

	/* XGMI TA unload currently is not supported on Arcturus */
	if (adev->asic_type == CHIP_ARCTURUS)
		return 0;

	/*
	 * TODO: bypass the unloading in sriov for now
	 */

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	psp_prep_ta_unload_cmd_buf(cmd, psp->xgmi_context.session_id);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);

	kfree(cmd);

	return ret;
}

int psp_xgmi_invoke(struct psp_context *psp, uint32_t ta_cmd_id)
{
	return psp_ta_invoke(psp, ta_cmd_id, psp->xgmi_context.session_id);
}

int psp_xgmi_terminate(struct psp_context *psp)
{
	int ret;

	if (!psp->xgmi_context.initialized)
		return 0;

	ret = psp_xgmi_unload(psp);
	if (ret)
		return ret;

	psp->xgmi_context.initialized = 0;

	/* free xgmi shared memory */
	amdgpu_bo_free_kernel(&psp->xgmi_context.xgmi_shared_bo,
			&psp->xgmi_context.xgmi_shared_mc_addr,
			&psp->xgmi_context.xgmi_shared_buf);

	return 0;
}

int psp_xgmi_initialize(struct psp_context *psp)
{
	struct ta_xgmi_shared_memory *xgmi_cmd;
	int ret;

	if (!psp->adev->psp.ta_fw ||
	    !psp->adev->psp.ta_xgmi_ucode_size ||
	    !psp->adev->psp.ta_xgmi_start_addr)
		return -ENOENT;

	if (!psp->xgmi_context.initialized) {
		ret = psp_xgmi_init_shared_buf(psp);
		if (ret)
			return ret;
	}

	/* Load XGMI TA */
	ret = psp_xgmi_load(psp);
	if (ret)
		return ret;

	/* Initialize XGMI session */
	xgmi_cmd = (struct ta_xgmi_shared_memory *)(psp->xgmi_context.xgmi_shared_buf);
	memset(xgmi_cmd, 0, sizeof(struct ta_xgmi_shared_memory));
	xgmi_cmd->cmd_id = TA_COMMAND_XGMI__INITIALIZE;

	ret = psp_xgmi_invoke(psp, xgmi_cmd->cmd_id);

	return ret;
}

int psp_xgmi_get_hive_id(struct psp_context *psp, uint64_t *hive_id)
{
	struct ta_xgmi_shared_memory *xgmi_cmd;
	int ret;

	xgmi_cmd = (struct ta_xgmi_shared_memory*)psp->xgmi_context.xgmi_shared_buf;
	memset(xgmi_cmd, 0, sizeof(struct ta_xgmi_shared_memory));

	xgmi_cmd->cmd_id = TA_COMMAND_XGMI__GET_HIVE_ID;

	/* Invoke xgmi ta to get hive id */
	ret = psp_xgmi_invoke(psp, xgmi_cmd->cmd_id);
	if (ret)
		return ret;

	*hive_id = xgmi_cmd->xgmi_out_message.get_hive_id.hive_id;

	return 0;
}

int psp_xgmi_get_node_id(struct psp_context *psp, uint64_t *node_id)
{
	struct ta_xgmi_shared_memory *xgmi_cmd;
	int ret;

	xgmi_cmd = (struct ta_xgmi_shared_memory*)psp->xgmi_context.xgmi_shared_buf;
	memset(xgmi_cmd, 0, sizeof(struct ta_xgmi_shared_memory));

	xgmi_cmd->cmd_id = TA_COMMAND_XGMI__GET_NODE_ID;

	/* Invoke xgmi ta to get the node id */
	ret = psp_xgmi_invoke(psp, xgmi_cmd->cmd_id);
	if (ret)
		return ret;

	*node_id = xgmi_cmd->xgmi_out_message.get_node_id.node_id;

	return 0;
}

int psp_xgmi_get_topology_info(struct psp_context *psp,
			       int number_devices,
			       struct psp_xgmi_topology_info *topology)
{
	struct ta_xgmi_shared_memory *xgmi_cmd;
	struct ta_xgmi_cmd_get_topology_info_input *topology_info_input;
	struct ta_xgmi_cmd_get_topology_info_output *topology_info_output;
	int i;
	int ret;

	if (!topology || topology->num_nodes > TA_XGMI__MAX_CONNECTED_NODES)
		return -EINVAL;

	xgmi_cmd = (struct ta_xgmi_shared_memory*)psp->xgmi_context.xgmi_shared_buf;
	memset(xgmi_cmd, 0, sizeof(struct ta_xgmi_shared_memory));

	/* Fill in the shared memory with topology information as input */
	topology_info_input = &xgmi_cmd->xgmi_in_message.get_topology_info;
	xgmi_cmd->cmd_id = TA_COMMAND_XGMI__GET_GET_TOPOLOGY_INFO;
	topology_info_input->num_nodes = number_devices;

	for (i = 0; i < topology_info_input->num_nodes; i++) {
		topology_info_input->nodes[i].node_id = topology->nodes[i].node_id;
		topology_info_input->nodes[i].num_hops = topology->nodes[i].num_hops;
		topology_info_input->nodes[i].is_sharing_enabled = topology->nodes[i].is_sharing_enabled;
		topology_info_input->nodes[i].sdma_engine = topology->nodes[i].sdma_engine;
	}

	/* Invoke xgmi ta to get the topology information */
	ret = psp_xgmi_invoke(psp, TA_COMMAND_XGMI__GET_GET_TOPOLOGY_INFO);
	if (ret)
		return ret;

	/* Read the output topology information from the shared memory */
	topology_info_output = &xgmi_cmd->xgmi_out_message.get_topology_info;
	topology->num_nodes = xgmi_cmd->xgmi_out_message.get_topology_info.num_nodes;
	for (i = 0; i < topology->num_nodes; i++) {
		topology->nodes[i].node_id = topology_info_output->nodes[i].node_id;
		topology->nodes[i].num_hops = topology_info_output->nodes[i].num_hops;
		topology->nodes[i].is_sharing_enabled = topology_info_output->nodes[i].is_sharing_enabled;
		topology->nodes[i].sdma_engine = topology_info_output->nodes[i].sdma_engine;
	}

	return 0;
}

int psp_xgmi_set_topology_info(struct psp_context *psp,
			       int number_devices,
			       struct psp_xgmi_topology_info *topology)
{
	struct ta_xgmi_shared_memory *xgmi_cmd;
	struct ta_xgmi_cmd_get_topology_info_input *topology_info_input;
	int i;

	if (!topology || topology->num_nodes > TA_XGMI__MAX_CONNECTED_NODES)
		return -EINVAL;

	xgmi_cmd = (struct ta_xgmi_shared_memory*)psp->xgmi_context.xgmi_shared_buf;
	memset(xgmi_cmd, 0, sizeof(struct ta_xgmi_shared_memory));

	topology_info_input = &xgmi_cmd->xgmi_in_message.get_topology_info;
	xgmi_cmd->cmd_id = TA_COMMAND_XGMI__SET_TOPOLOGY_INFO;
	topology_info_input->num_nodes = number_devices;

	for (i = 0; i < topology_info_input->num_nodes; i++) {
		topology_info_input->nodes[i].node_id = topology->nodes[i].node_id;
		topology_info_input->nodes[i].num_hops = topology->nodes[i].num_hops;
		topology_info_input->nodes[i].is_sharing_enabled = 1;
		topology_info_input->nodes[i].sdma_engine = topology->nodes[i].sdma_engine;
	}

	/* Invoke xgmi ta to set topology information */
	return psp_xgmi_invoke(psp, TA_COMMAND_XGMI__SET_TOPOLOGY_INFO);
}

// ras begin
static int psp_ras_init_shared_buf(struct psp_context *psp)
{
	int ret;

	/*
	 * Allocate 16k memory aligned to 4k from Frame Buffer (local
	 * physical) for ras ta <-> Driver
	 */
	ret = amdgpu_bo_create_kernel(psp->adev, PSP_RAS_SHARED_MEM_SIZE,
			PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM,
			&psp->ras.ras_shared_bo,
			&psp->ras.ras_shared_mc_addr,
			&psp->ras.ras_shared_buf);

	return ret;
}

static int psp_ras_load(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;
	struct ta_ras_shared_memory *ras_cmd;

	/*
	 * TODO: bypass the loading in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);
	memcpy(psp->fw_pri_buf, psp->ta_ras_start_addr, psp->ta_ras_ucode_size);

	psp_prep_ta_load_cmd_buf(cmd,
				 psp->fw_pri_mc_addr,
				 psp->ta_ras_ucode_size,
				 psp->ras.ras_shared_mc_addr,
				 PSP_RAS_SHARED_MEM_SIZE);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
			psp->fence_buf_mc_addr);

	ras_cmd = (struct ta_ras_shared_memory*)psp->ras.ras_shared_buf;

	if (!ret) {
		psp->ras.session_id = cmd->resp.session_id;

		if (!ras_cmd->ras_status)
			psp->ras.ras_initialized = true;
		else
			dev_warn(psp->adev->dev, "RAS Init Status: 0x%X\n", ras_cmd->ras_status);
	}

	if (ret || ras_cmd->ras_status)
		amdgpu_ras_fini(psp->adev);

	kfree(cmd);

	return ret;
}

static int psp_ras_unload(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	/*
	 * TODO: bypass the unloading in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	psp_prep_ta_unload_cmd_buf(cmd, psp->ras.session_id);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
			psp->fence_buf_mc_addr);

	kfree(cmd);

	return ret;
}

int psp_ras_invoke(struct psp_context *psp, uint32_t ta_cmd_id)
{
	struct ta_ras_shared_memory *ras_cmd;
	int ret;

	ras_cmd = (struct ta_ras_shared_memory *)psp->ras.ras_shared_buf;

	/*
	 * TODO: bypass the loading in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	ret = psp_ta_invoke(psp, ta_cmd_id, psp->ras.session_id);

	if (amdgpu_ras_intr_triggered())
		return ret;

	if (ras_cmd->if_version > RAS_TA_HOST_IF_VER)
	{
		DRM_WARN("RAS: Unsupported Interface");
		return -EINVAL;
	}

	if (!ret) {
		if (ras_cmd->ras_out_message.flags.err_inject_switch_disable_flag) {
			dev_warn(psp->adev->dev, "ECC switch disabled\n");

			ras_cmd->ras_status = TA_RAS_STATUS__ERROR_RAS_NOT_AVAILABLE;
		}
		else if (ras_cmd->ras_out_message.flags.reg_access_failure_flag)
			dev_warn(psp->adev->dev,
				 "RAS internal register access blocked\n");
	}

	return ret;
}

int psp_ras_enable_features(struct psp_context *psp,
		union ta_ras_cmd_input *info, bool enable)
{
	struct ta_ras_shared_memory *ras_cmd;
	int ret;

	if (!psp->ras.ras_initialized)
		return -EINVAL;

	ras_cmd = (struct ta_ras_shared_memory *)psp->ras.ras_shared_buf;
	memset(ras_cmd, 0, sizeof(struct ta_ras_shared_memory));

	if (enable)
		ras_cmd->cmd_id = TA_RAS_COMMAND__ENABLE_FEATURES;
	else
		ras_cmd->cmd_id = TA_RAS_COMMAND__DISABLE_FEATURES;

	ras_cmd->ras_in_message = *info;

	ret = psp_ras_invoke(psp, ras_cmd->cmd_id);
	if (ret)
		return -EINVAL;

	return ras_cmd->ras_status;
}

static int psp_ras_terminate(struct psp_context *psp)
{
	int ret;

	/*
	 * TODO: bypass the terminate in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->ras.ras_initialized)
		return 0;

	ret = psp_ras_unload(psp);
	if (ret)
		return ret;

	psp->ras.ras_initialized = false;

	/* free ras shared memory */
	amdgpu_bo_free_kernel(&psp->ras.ras_shared_bo,
			&psp->ras.ras_shared_mc_addr,
			&psp->ras.ras_shared_buf);

	return 0;
}

static int psp_ras_initialize(struct psp_context *psp)
{
	int ret;

	/*
	 * TODO: bypass the initialize in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->adev->psp.ta_ras_ucode_size ||
	    !psp->adev->psp.ta_ras_start_addr) {
		dev_info(psp->adev->dev, "RAS: optional ras ta ucode is not available\n");
		return 0;
	}

	if (!psp->ras.ras_initialized) {
		ret = psp_ras_init_shared_buf(psp);
		if (ret)
			return ret;
	}

	ret = psp_ras_load(psp);
	if (ret)
		return ret;

	return 0;
}

int psp_ras_trigger_error(struct psp_context *psp,
			  struct ta_ras_trigger_error_input *info)
{
	struct ta_ras_shared_memory *ras_cmd;
	int ret;

	if (!psp->ras.ras_initialized)
		return -EINVAL;

	ras_cmd = (struct ta_ras_shared_memory *)psp->ras.ras_shared_buf;
	memset(ras_cmd, 0, sizeof(struct ta_ras_shared_memory));

	ras_cmd->cmd_id = TA_RAS_COMMAND__TRIGGER_ERROR;
	ras_cmd->ras_in_message.trigger_error = *info;

	ret = psp_ras_invoke(psp, ras_cmd->cmd_id);
	if (ret)
		return -EINVAL;

	/* If err_event_athub occurs error inject was successful, however
	   return status from TA is no long reliable */
	if (amdgpu_ras_intr_triggered())
		return 0;

	return ras_cmd->ras_status;
}
// ras end

// HDCP start
static int psp_hdcp_init_shared_buf(struct psp_context *psp)
{
	int ret;

	/*
	 * Allocate 16k memory aligned to 4k from Frame Buffer (local
	 * physical) for hdcp ta <-> Driver
	 */
	ret = amdgpu_bo_create_kernel(psp->adev, PSP_HDCP_SHARED_MEM_SIZE,
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM,
				      &psp->hdcp_context.hdcp_shared_bo,
				      &psp->hdcp_context.hdcp_shared_mc_addr,
				      &psp->hdcp_context.hdcp_shared_buf);

	return ret;
}

static int psp_hdcp_load(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	/*
	 * TODO: bypass the loading in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);
	memcpy(psp->fw_pri_buf, psp->ta_hdcp_start_addr,
	       psp->ta_hdcp_ucode_size);

	psp_prep_ta_load_cmd_buf(cmd,
				 psp->fw_pri_mc_addr,
				 psp->ta_hdcp_ucode_size,
				 psp->hdcp_context.hdcp_shared_mc_addr,
				 PSP_HDCP_SHARED_MEM_SIZE);

	ret = psp_cmd_submit_buf(psp, NULL, cmd, psp->fence_buf_mc_addr);

	if (!ret) {
		psp->hdcp_context.hdcp_initialized = true;
		psp->hdcp_context.session_id = cmd->resp.session_id;
		mutex_init(&psp->hdcp_context.mutex);
	}

	kfree(cmd);

	return ret;
}
static int psp_hdcp_initialize(struct psp_context *psp)
{
	int ret;

	/*
	 * TODO: bypass the initialize in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->adev->psp.ta_hdcp_ucode_size ||
	    !psp->adev->psp.ta_hdcp_start_addr) {
		dev_info(psp->adev->dev, "HDCP: optional hdcp ta ucode is not available\n");
		return 0;
	}

	if (!psp->hdcp_context.hdcp_initialized) {
		ret = psp_hdcp_init_shared_buf(psp);
		if (ret)
			return ret;
	}

	ret = psp_hdcp_load(psp);
	if (ret)
		return ret;

	return 0;
}

static int psp_hdcp_unload(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	/*
	 * TODO: bypass the unloading in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	psp_prep_ta_unload_cmd_buf(cmd, psp->hdcp_context.session_id);

	ret = psp_cmd_submit_buf(psp, NULL, cmd, psp->fence_buf_mc_addr);

	kfree(cmd);

	return ret;
}

int psp_hdcp_invoke(struct psp_context *psp, uint32_t ta_cmd_id)
{
	/*
	 * TODO: bypass the loading in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	return psp_ta_invoke(psp, ta_cmd_id, psp->hdcp_context.session_id);
}

static int psp_hdcp_terminate(struct psp_context *psp)
{
	int ret;

	/*
	 * TODO: bypass the terminate in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->hdcp_context.hdcp_initialized)
		return 0;

	ret = psp_hdcp_unload(psp);
	if (ret)
		return ret;

	psp->hdcp_context.hdcp_initialized = false;

	/* free hdcp shared memory */
	amdgpu_bo_free_kernel(&psp->hdcp_context.hdcp_shared_bo,
			      &psp->hdcp_context.hdcp_shared_mc_addr,
			      &psp->hdcp_context.hdcp_shared_buf);

	return 0;
}
// HDCP end

// DTM start
static int psp_dtm_init_shared_buf(struct psp_context *psp)
{
	int ret;

	/*
	 * Allocate 16k memory aligned to 4k from Frame Buffer (local
	 * physical) for dtm ta <-> Driver
	 */
	ret = amdgpu_bo_create_kernel(psp->adev, PSP_DTM_SHARED_MEM_SIZE,
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM,
				      &psp->dtm_context.dtm_shared_bo,
				      &psp->dtm_context.dtm_shared_mc_addr,
				      &psp->dtm_context.dtm_shared_buf);

	return ret;
}

static int psp_dtm_load(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	/*
	 * TODO: bypass the loading in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);
	memcpy(psp->fw_pri_buf, psp->ta_dtm_start_addr, psp->ta_dtm_ucode_size);

	psp_prep_ta_load_cmd_buf(cmd,
				 psp->fw_pri_mc_addr,
				 psp->ta_dtm_ucode_size,
				 psp->dtm_context.dtm_shared_mc_addr,
				 PSP_DTM_SHARED_MEM_SIZE);

	ret = psp_cmd_submit_buf(psp, NULL, cmd, psp->fence_buf_mc_addr);

	if (!ret) {
		psp->dtm_context.dtm_initialized = true;
		psp->dtm_context.session_id = cmd->resp.session_id;
		mutex_init(&psp->dtm_context.mutex);
	}

	kfree(cmd);

	return ret;
}

static int psp_dtm_initialize(struct psp_context *psp)
{
	int ret;

	/*
	 * TODO: bypass the initialize in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->adev->psp.ta_dtm_ucode_size ||
	    !psp->adev->psp.ta_dtm_start_addr) {
		dev_info(psp->adev->dev, "DTM: optional dtm ta ucode is not available\n");
		return 0;
	}

	if (!psp->dtm_context.dtm_initialized) {
		ret = psp_dtm_init_shared_buf(psp);
		if (ret)
			return ret;
	}

	ret = psp_dtm_load(psp);
	if (ret)
		return ret;

	return 0;
}

static int psp_dtm_unload(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	/*
	 * TODO: bypass the unloading in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	psp_prep_ta_unload_cmd_buf(cmd, psp->dtm_context.session_id);

	ret = psp_cmd_submit_buf(psp, NULL, cmd, psp->fence_buf_mc_addr);

	kfree(cmd);

	return ret;
}

int psp_dtm_invoke(struct psp_context *psp, uint32_t ta_cmd_id)
{
	/*
	 * TODO: bypass the loading in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	return psp_ta_invoke(psp, ta_cmd_id, psp->dtm_context.session_id);
}

static int psp_dtm_terminate(struct psp_context *psp)
{
	int ret;

	/*
	 * TODO: bypass the terminate in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->dtm_context.dtm_initialized)
		return 0;

	ret = psp_dtm_unload(psp);
	if (ret)
		return ret;

	psp->dtm_context.dtm_initialized = false;

	/* free hdcp shared memory */
	amdgpu_bo_free_kernel(&psp->dtm_context.dtm_shared_bo,
			      &psp->dtm_context.dtm_shared_mc_addr,
			      &psp->dtm_context.dtm_shared_buf);

	return 0;
}
// DTM end

// RAP start
static int psp_rap_init_shared_buf(struct psp_context *psp)
{
	int ret;

	/*
	 * Allocate 16k memory aligned to 4k from Frame Buffer (local
	 * physical) for rap ta <-> Driver
	 */
	ret = amdgpu_bo_create_kernel(psp->adev, PSP_RAP_SHARED_MEM_SIZE,
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM,
				      &psp->rap_context.rap_shared_bo,
				      &psp->rap_context.rap_shared_mc_addr,
				      &psp->rap_context.rap_shared_buf);

	return ret;
}

static int psp_rap_load(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);
	memcpy(psp->fw_pri_buf, psp->ta_rap_start_addr, psp->ta_rap_ucode_size);

	psp_prep_ta_load_cmd_buf(cmd,
				 psp->fw_pri_mc_addr,
				 psp->ta_rap_ucode_size,
				 psp->rap_context.rap_shared_mc_addr,
				 PSP_RAP_SHARED_MEM_SIZE);

	ret = psp_cmd_submit_buf(psp, NULL, cmd, psp->fence_buf_mc_addr);

	if (!ret) {
		psp->rap_context.rap_initialized = true;
		psp->rap_context.session_id = cmd->resp.session_id;
		mutex_init(&psp->rap_context.mutex);
	}

	kfree(cmd);

	return ret;
}

static int psp_rap_unload(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	psp_prep_ta_unload_cmd_buf(cmd, psp->rap_context.session_id);

	ret = psp_cmd_submit_buf(psp, NULL, cmd, psp->fence_buf_mc_addr);

	kfree(cmd);

	return ret;
}

static int psp_rap_initialize(struct psp_context *psp)
{
	int ret;

	/*
	 * TODO: bypass the initialize in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->adev->psp.ta_rap_ucode_size ||
	    !psp->adev->psp.ta_rap_start_addr) {
		dev_info(psp->adev->dev, "RAP: optional rap ta ucode is not available\n");
		return 0;
	}

	if (!psp->rap_context.rap_initialized) {
		ret = psp_rap_init_shared_buf(psp);
		if (ret)
			return ret;
	}

	ret = psp_rap_load(psp);
	if (ret)
		return ret;

	ret = psp_rap_invoke(psp, TA_CMD_RAP__INITIALIZE);
	if (ret != TA_RAP_STATUS__SUCCESS) {
		psp_rap_unload(psp);

		amdgpu_bo_free_kernel(&psp->rap_context.rap_shared_bo,
			      &psp->rap_context.rap_shared_mc_addr,
			      &psp->rap_context.rap_shared_buf);

		psp->rap_context.rap_initialized = false;

		dev_warn(psp->adev->dev, "RAP TA initialize fail.\n");
		return -EINVAL;
	}

	return 0;
}

static int psp_rap_terminate(struct psp_context *psp)
{
	int ret;

	if (!psp->rap_context.rap_initialized)
		return 0;

	ret = psp_rap_unload(psp);

	psp->rap_context.rap_initialized = false;

	/* free rap shared memory */
	amdgpu_bo_free_kernel(&psp->rap_context.rap_shared_bo,
			      &psp->rap_context.rap_shared_mc_addr,
			      &psp->rap_context.rap_shared_buf);

	return ret;
}

int psp_rap_invoke(struct psp_context *psp, uint32_t ta_cmd_id)
{
	struct ta_rap_shared_memory *rap_cmd;
	int ret;

	if (!psp->rap_context.rap_initialized)
		return -EINVAL;

	if (ta_cmd_id != TA_CMD_RAP__INITIALIZE &&
	    ta_cmd_id != TA_CMD_RAP__VALIDATE_L0)
		return -EINVAL;

	mutex_lock(&psp->rap_context.mutex);

	rap_cmd = (struct ta_rap_shared_memory *)
		  psp->rap_context.rap_shared_buf;
	memset(rap_cmd, 0, sizeof(struct ta_rap_shared_memory));

	rap_cmd->cmd_id = ta_cmd_id;
	rap_cmd->validation_method_id = METHOD_A;

	ret = psp_ta_invoke(psp, rap_cmd->cmd_id, psp->rap_context.session_id);
	if (ret) {
		mutex_unlock(&psp->rap_context.mutex);
		return ret;
	}

	mutex_unlock(&psp->rap_context.mutex);

	return rap_cmd->rap_status;
}
// RAP end

static int psp_hw_start(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	int ret;

	if (!amdgpu_sriov_vf(adev)) {
		if (psp->kdb_bin_size &&
		    (psp->funcs->bootloader_load_kdb != NULL)) {
			ret = psp_bootloader_load_kdb(psp);
			if (ret) {
				DRM_ERROR("PSP load kdb failed!\n");
				return ret;
			}
		}

		if (psp->spl_bin_size) {
			ret = psp_bootloader_load_spl(psp);
			if (ret) {
				DRM_ERROR("PSP load spl failed!\n");
				return ret;
			}
		}

		ret = psp_bootloader_load_sysdrv(psp);
		if (ret) {
			DRM_ERROR("PSP load sysdrv failed!\n");
			return ret;
		}

		ret = psp_bootloader_load_sos(psp);
		if (ret) {
			DRM_ERROR("PSP load sos failed!\n");
			return ret;
		}
	}

	ret = psp_ring_create(psp, PSP_RING_TYPE__KM);
	if (ret) {
		DRM_ERROR("PSP create ring failed!\n");
		return ret;
	}

	ret = psp_clear_vf_fw(psp);
	if (ret) {
		DRM_ERROR("PSP clear vf fw!\n");
		return ret;
	}

	ret = psp_tmr_init(psp);
	if (ret) {
		DRM_ERROR("PSP tmr init failed!\n");
		return ret;
	}

	/*
	 * For ASICs with DF Cstate management centralized
	 * to PMFW, TMR setup should be performed after PMFW
	 * loaded and before other non-psp firmware loaded.
	 */
	if (psp->pmfw_centralized_cstate_management) {
		ret = psp_load_smu_fw(psp);
		if (ret)
			return ret;
	}

	ret = psp_tmr_load(psp);
	if (ret) {
		DRM_ERROR("PSP load tmr failed!\n");
		return ret;
	}

	return 0;
}

static int psp_get_fw_type(struct amdgpu_firmware_info *ucode,
			   enum psp_gfx_fw_type *type)
{
	switch (ucode->ucode_id) {
	case AMDGPU_UCODE_ID_SDMA0:
		*type = GFX_FW_TYPE_SDMA0;
		break;
	case AMDGPU_UCODE_ID_SDMA1:
		*type = GFX_FW_TYPE_SDMA1;
		break;
	case AMDGPU_UCODE_ID_SDMA2:
		*type = GFX_FW_TYPE_SDMA2;
		break;
	case AMDGPU_UCODE_ID_SDMA3:
		*type = GFX_FW_TYPE_SDMA3;
		break;
	case AMDGPU_UCODE_ID_SDMA4:
		*type = GFX_FW_TYPE_SDMA4;
		break;
	case AMDGPU_UCODE_ID_SDMA5:
		*type = GFX_FW_TYPE_SDMA5;
		break;
	case AMDGPU_UCODE_ID_SDMA6:
		*type = GFX_FW_TYPE_SDMA6;
		break;
	case AMDGPU_UCODE_ID_SDMA7:
		*type = GFX_FW_TYPE_SDMA7;
		break;
	case AMDGPU_UCODE_ID_CP_MES:
		*type = GFX_FW_TYPE_CP_MES;
		break;
	case AMDGPU_UCODE_ID_CP_MES_DATA:
		*type = GFX_FW_TYPE_MES_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_CE:
		*type = GFX_FW_TYPE_CP_CE;
		break;
	case AMDGPU_UCODE_ID_CP_PFP:
		*type = GFX_FW_TYPE_CP_PFP;
		break;
	case AMDGPU_UCODE_ID_CP_ME:
		*type = GFX_FW_TYPE_CP_ME;
		break;
	case AMDGPU_UCODE_ID_CP_MEC1:
		*type = GFX_FW_TYPE_CP_MEC;
		break;
	case AMDGPU_UCODE_ID_CP_MEC1_JT:
		*type = GFX_FW_TYPE_CP_MEC_ME1;
		break;
	case AMDGPU_UCODE_ID_CP_MEC2:
		*type = GFX_FW_TYPE_CP_MEC;
		break;
	case AMDGPU_UCODE_ID_CP_MEC2_JT:
		*type = GFX_FW_TYPE_CP_MEC_ME2;
		break;
	case AMDGPU_UCODE_ID_RLC_G:
		*type = GFX_FW_TYPE_RLC_G;
		break;
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL:
		*type = GFX_FW_TYPE_RLC_RESTORE_LIST_SRM_CNTL;
		break;
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM:
		*type = GFX_FW_TYPE_RLC_RESTORE_LIST_GPM_MEM;
		break;
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM:
		*type = GFX_FW_TYPE_RLC_RESTORE_LIST_SRM_MEM;
		break;
	case AMDGPU_UCODE_ID_RLC_IRAM:
		*type = GFX_FW_TYPE_RLC_IRAM;
		break;
	case AMDGPU_UCODE_ID_RLC_DRAM:
		*type = GFX_FW_TYPE_RLC_DRAM_BOOT;
		break;
	case AMDGPU_UCODE_ID_SMC:
		*type = GFX_FW_TYPE_SMU;
		break;
	case AMDGPU_UCODE_ID_UVD:
		*type = GFX_FW_TYPE_UVD;
		break;
	case AMDGPU_UCODE_ID_UVD1:
		*type = GFX_FW_TYPE_UVD1;
		break;
	case AMDGPU_UCODE_ID_VCE:
		*type = GFX_FW_TYPE_VCE;
		break;
	case AMDGPU_UCODE_ID_VCN:
		*type = GFX_FW_TYPE_VCN;
		break;
	case AMDGPU_UCODE_ID_VCN1:
		*type = GFX_FW_TYPE_VCN1;
		break;
	case AMDGPU_UCODE_ID_DMCU_ERAM:
		*type = GFX_FW_TYPE_DMCU_ERAM;
		break;
	case AMDGPU_UCODE_ID_DMCU_INTV:
		*type = GFX_FW_TYPE_DMCU_ISR;
		break;
	case AMDGPU_UCODE_ID_VCN0_RAM:
		*type = GFX_FW_TYPE_VCN0_RAM;
		break;
	case AMDGPU_UCODE_ID_VCN1_RAM:
		*type = GFX_FW_TYPE_VCN1_RAM;
		break;
	case AMDGPU_UCODE_ID_DMCUB:
		*type = GFX_FW_TYPE_DMUB;
		break;
	case AMDGPU_UCODE_ID_MAXIMUM:
	default:
		return -EINVAL;
	}

	return 0;
}

static void psp_print_fw_hdr(struct psp_context *psp,
			     struct amdgpu_firmware_info *ucode)
{
	struct amdgpu_device *adev = psp->adev;
	struct common_firmware_header *hdr;

	switch (ucode->ucode_id) {
	case AMDGPU_UCODE_ID_SDMA0:
	case AMDGPU_UCODE_ID_SDMA1:
	case AMDGPU_UCODE_ID_SDMA2:
	case AMDGPU_UCODE_ID_SDMA3:
	case AMDGPU_UCODE_ID_SDMA4:
	case AMDGPU_UCODE_ID_SDMA5:
	case AMDGPU_UCODE_ID_SDMA6:
	case AMDGPU_UCODE_ID_SDMA7:
		hdr = (struct common_firmware_header *)
			adev->sdma.instance[ucode->ucode_id - AMDGPU_UCODE_ID_SDMA0].fw->data;
		amdgpu_ucode_print_sdma_hdr(hdr);
		break;
	case AMDGPU_UCODE_ID_CP_CE:
		hdr = (struct common_firmware_header *)adev->gfx.ce_fw->data;
		amdgpu_ucode_print_gfx_hdr(hdr);
		break;
	case AMDGPU_UCODE_ID_CP_PFP:
		hdr = (struct common_firmware_header *)adev->gfx.pfp_fw->data;
		amdgpu_ucode_print_gfx_hdr(hdr);
		break;
	case AMDGPU_UCODE_ID_CP_ME:
		hdr = (struct common_firmware_header *)adev->gfx.me_fw->data;
		amdgpu_ucode_print_gfx_hdr(hdr);
		break;
	case AMDGPU_UCODE_ID_CP_MEC1:
		hdr = (struct common_firmware_header *)adev->gfx.mec_fw->data;
		amdgpu_ucode_print_gfx_hdr(hdr);
		break;
	case AMDGPU_UCODE_ID_RLC_G:
		hdr = (struct common_firmware_header *)adev->gfx.rlc_fw->data;
		amdgpu_ucode_print_rlc_hdr(hdr);
		break;
	case AMDGPU_UCODE_ID_SMC:
		hdr = (struct common_firmware_header *)adev->pm.fw->data;
		amdgpu_ucode_print_smc_hdr(hdr);
		break;
	default:
		break;
	}
}

static int psp_prep_load_ip_fw_cmd_buf(struct amdgpu_firmware_info *ucode,
				       struct psp_gfx_cmd_resp *cmd)
{
	int ret;
	uint64_t fw_mem_mc_addr = ucode->mc_addr;

	memset(cmd, 0, sizeof(struct psp_gfx_cmd_resp));

	cmd->cmd_id = GFX_CMD_ID_LOAD_IP_FW;
	cmd->cmd.cmd_load_ip_fw.fw_phy_addr_lo = lower_32_bits(fw_mem_mc_addr);
	cmd->cmd.cmd_load_ip_fw.fw_phy_addr_hi = upper_32_bits(fw_mem_mc_addr);
	cmd->cmd.cmd_load_ip_fw.fw_size = ucode->ucode_size;

	ret = psp_get_fw_type(ucode, &cmd->cmd.cmd_load_ip_fw.fw_type);
	if (ret)
		DRM_ERROR("Unknown firmware type\n");

	return ret;
}

static int psp_execute_np_fw_load(struct psp_context *psp,
			          struct amdgpu_firmware_info *ucode)
{
	int ret = 0;

	ret = psp_prep_load_ip_fw_cmd_buf(ucode, psp->cmd);
	if (ret)
		return ret;

	ret = psp_cmd_submit_buf(psp, ucode, psp->cmd,
				 psp->fence_buf_mc_addr);

	return ret;
}

static int psp_load_smu_fw(struct psp_context *psp)
{
	int ret;
	struct amdgpu_device* adev = psp->adev;
	struct amdgpu_firmware_info *ucode =
			&adev->firmware.ucode[AMDGPU_UCODE_ID_SMC];
	struct amdgpu_ras *ras = psp->ras.ras;

	if (!ucode->fw || amdgpu_sriov_vf(psp->adev))
		return 0;


	if (amdgpu_in_reset(adev) && ras && ras->supported) {
		ret = amdgpu_dpm_set_mp1_state(adev, PP_MP1_STATE_UNLOAD);
		if (ret) {
			DRM_WARN("Failed to set MP1 state prepare for reload\n");
		}
	}

	ret = psp_execute_np_fw_load(psp, ucode);

	if (ret)
		DRM_ERROR("PSP load smu failed!\n");

	return ret;
}

static bool fw_load_skip_check(struct psp_context *psp,
			       struct amdgpu_firmware_info *ucode)
{
	if (!ucode->fw)
		return true;

	if (ucode->ucode_id == AMDGPU_UCODE_ID_SMC &&
	    (psp_smu_reload_quirk(psp) ||
	     psp->autoload_supported ||
	     psp->pmfw_centralized_cstate_management))
		return true;

	if (amdgpu_sriov_vf(psp->adev) &&
	   (ucode->ucode_id == AMDGPU_UCODE_ID_SDMA0
	    || ucode->ucode_id == AMDGPU_UCODE_ID_SDMA1
	    || ucode->ucode_id == AMDGPU_UCODE_ID_SDMA2
	    || ucode->ucode_id == AMDGPU_UCODE_ID_SDMA3
	    || ucode->ucode_id == AMDGPU_UCODE_ID_SDMA4
	    || ucode->ucode_id == AMDGPU_UCODE_ID_SDMA5
	    || ucode->ucode_id == AMDGPU_UCODE_ID_SDMA6
	    || ucode->ucode_id == AMDGPU_UCODE_ID_SDMA7
	    || ucode->ucode_id == AMDGPU_UCODE_ID_RLC_G
	    || ucode->ucode_id == AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL
	    || ucode->ucode_id == AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM
	    || ucode->ucode_id == AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM
	    || ucode->ucode_id == AMDGPU_UCODE_ID_SMC))
		/*skip ucode loading in SRIOV VF */
		return true;

	if (psp->autoload_supported &&
	    (ucode->ucode_id == AMDGPU_UCODE_ID_CP_MEC1_JT ||
	     ucode->ucode_id == AMDGPU_UCODE_ID_CP_MEC2_JT))
		/* skip mec JT when autoload is enabled */
		return true;

	return false;
}

static int psp_np_fw_load(struct psp_context *psp)
{
	int i, ret;
	struct amdgpu_firmware_info *ucode;
	struct amdgpu_device* adev = psp->adev;

	if (psp->autoload_supported &&
	    !psp->pmfw_centralized_cstate_management) {
		ret = psp_load_smu_fw(psp);
		if (ret)
			return ret;
	}

	for (i = 0; i < adev->firmware.max_ucodes; i++) {
		ucode = &adev->firmware.ucode[i];

		if (ucode->ucode_id == AMDGPU_UCODE_ID_SMC &&
		    !fw_load_skip_check(psp, ucode)) {
			ret = psp_load_smu_fw(psp);
			if (ret)
				return ret;
			continue;
		}

		if (fw_load_skip_check(psp, ucode))
			continue;

		if (psp->autoload_supported &&
		    (adev->asic_type == CHIP_SIENNA_CICHLID ||
		     adev->asic_type == CHIP_NAVY_FLOUNDER) &&
		    (ucode->ucode_id == AMDGPU_UCODE_ID_SDMA1 ||
		     ucode->ucode_id == AMDGPU_UCODE_ID_SDMA2 ||
		     ucode->ucode_id == AMDGPU_UCODE_ID_SDMA3))
			/* PSP only receive one SDMA fw for sienna_cichlid,
			 * as all four sdma fw are same */
			continue;

		psp_print_fw_hdr(psp, ucode);

		ret = psp_execute_np_fw_load(psp, ucode);
		if (ret)
			return ret;

		/* Start rlc autoload after psp recieved all the gfx firmware */
		if (psp->autoload_supported && ucode->ucode_id == (amdgpu_sriov_vf(adev) ?
		    AMDGPU_UCODE_ID_CP_MEC2 : AMDGPU_UCODE_ID_RLC_G)) {
			ret = psp_rlc_autoload_start(psp);
			if (ret) {
				DRM_ERROR("Failed to start rlc autoload\n");
				return ret;
			}
		}
	}

	return 0;
}

static int psp_load_fw(struct amdgpu_device *adev)
{
	int ret;
	struct psp_context *psp = &adev->psp;

	if (amdgpu_sriov_vf(adev) && amdgpu_in_reset(adev)) {
		psp_ring_stop(psp, PSP_RING_TYPE__KM); /* should not destroy ring, only stop */
		goto skip_memalloc;
	}

	psp->cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!psp->cmd)
		return -ENOMEM;

	ret = amdgpu_bo_create_kernel(adev, PSP_1_MEG, PSP_1_MEG,
					AMDGPU_GEM_DOMAIN_GTT,
					&psp->fw_pri_bo,
					&psp->fw_pri_mc_addr,
					&psp->fw_pri_buf);
	if (ret)
		goto failed;

	ret = amdgpu_bo_create_kernel(adev, PSP_FENCE_BUFFER_SIZE, PAGE_SIZE,
					AMDGPU_GEM_DOMAIN_VRAM,
					&psp->fence_buf_bo,
					&psp->fence_buf_mc_addr,
					&psp->fence_buf);
	if (ret)
		goto failed;

	ret = amdgpu_bo_create_kernel(adev, PSP_CMD_BUFFER_SIZE, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &psp->cmd_buf_bo, &psp->cmd_buf_mc_addr,
				      (void **)&psp->cmd_buf_mem);
	if (ret)
		goto failed;

	memset(psp->fence_buf, 0, PSP_FENCE_BUFFER_SIZE);

	ret = psp_ring_init(psp, PSP_RING_TYPE__KM);
	if (ret) {
		DRM_ERROR("PSP ring init failed!\n");
		goto failed;
	}

skip_memalloc:
	ret = psp_hw_start(psp);
	if (ret)
		goto failed;

	ret = psp_np_fw_load(psp);
	if (ret)
		goto failed;

	ret = psp_asd_load(psp);
	if (ret) {
		DRM_ERROR("PSP load asd failed!\n");
		return ret;
	}

	if (psp->adev->psp.ta_fw) {
		ret = psp_ras_initialize(psp);
		if (ret)
			dev_err(psp->adev->dev,
					"RAS: Failed to initialize RAS\n");

		ret = psp_hdcp_initialize(psp);
		if (ret)
			dev_err(psp->adev->dev,
				"HDCP: Failed to initialize HDCP\n");

		ret = psp_dtm_initialize(psp);
		if (ret)
			dev_err(psp->adev->dev,
				"DTM: Failed to initialize DTM\n");

		ret = psp_rap_initialize(psp);
		if (ret)
			dev_err(psp->adev->dev,
				"RAP: Failed to initialize RAP\n");
	}

	return 0;

failed:
	/*
	 * all cleanup jobs (xgmi terminate, ras terminate,
	 * ring destroy, cmd/fence/fw buffers destory,
	 * psp->cmd destory) are delayed to psp_hw_fini
	 */
	return ret;
}

static int psp_hw_init(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	mutex_lock(&adev->firmware.mutex);
	/*
	 * This sequence is just used on hw_init only once, no need on
	 * resume.
	 */
	ret = amdgpu_ucode_init_bo(adev);
	if (ret)
		goto failed;

	ret = psp_load_fw(adev);
	if (ret) {
		DRM_ERROR("PSP firmware loading failed\n");
		goto failed;
	}

	mutex_unlock(&adev->firmware.mutex);
	return 0;

failed:
	adev->firmware.load_type = AMDGPU_FW_LOAD_DIRECT;
	mutex_unlock(&adev->firmware.mutex);
	return -EINVAL;
}

static int psp_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct psp_context *psp = &adev->psp;
	int ret;

	if (psp->adev->psp.ta_fw) {
		psp_ras_terminate(psp);
		psp_rap_terminate(psp);
		psp_dtm_terminate(psp);
		psp_hdcp_terminate(psp);
	}

	psp_asd_unload(psp);
	ret = psp_clear_vf_fw(psp);
	if (ret) {
		DRM_ERROR("PSP clear vf fw!\n");
		return ret;
	}

	psp_tmr_terminate(psp);
	psp_ring_destroy(psp, PSP_RING_TYPE__KM);

	amdgpu_bo_free_kernel(&psp->fw_pri_bo,
			      &psp->fw_pri_mc_addr, &psp->fw_pri_buf);
	amdgpu_bo_free_kernel(&psp->fence_buf_bo,
			      &psp->fence_buf_mc_addr, &psp->fence_buf);
	amdgpu_bo_free_kernel(&psp->cmd_buf_bo, &psp->cmd_buf_mc_addr,
			      (void **)&psp->cmd_buf_mem);

	kfree(psp->cmd);
	psp->cmd = NULL;

	return 0;
}

static int psp_suspend(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct psp_context *psp = &adev->psp;

	if (adev->gmc.xgmi.num_physical_nodes > 1 &&
	    psp->xgmi_context.initialized == 1) {
		ret = psp_xgmi_terminate(psp);
		if (ret) {
			DRM_ERROR("Failed to terminate xgmi ta\n");
			return ret;
		}
	}

	if (psp->adev->psp.ta_fw) {
		ret = psp_ras_terminate(psp);
		if (ret) {
			DRM_ERROR("Failed to terminate ras ta\n");
			return ret;
		}
		ret = psp_hdcp_terminate(psp);
		if (ret) {
			DRM_ERROR("Failed to terminate hdcp ta\n");
			return ret;
		}
		ret = psp_dtm_terminate(psp);
		if (ret) {
			DRM_ERROR("Failed to terminate dtm ta\n");
			return ret;
		}
		ret = psp_rap_terminate(psp);
		if (ret) {
			DRM_ERROR("Failed to terminate rap ta\n");
			return ret;
		}
	}

	ret = psp_asd_unload(psp);
	if (ret) {
		DRM_ERROR("Failed to unload asd\n");
		return ret;
	}

	ret = psp_tmr_terminate(psp);
	if (ret) {
		DRM_ERROR("Failed to terminate tmr\n");
		return ret;
	}

	ret = psp_ring_stop(psp, PSP_RING_TYPE__KM);
	if (ret) {
		DRM_ERROR("PSP ring stop failed\n");
		return ret;
	}

	return 0;
}

static int psp_resume(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct psp_context *psp = &adev->psp;

	DRM_INFO("PSP is resuming...\n");

	ret = psp_mem_training(psp, PSP_MEM_TRAIN_RESUME);
	if (ret) {
		DRM_ERROR("Failed to process memory training!\n");
		return ret;
	}

	mutex_lock(&adev->firmware.mutex);

	ret = psp_hw_start(psp);
	if (ret)
		goto failed;

	ret = psp_np_fw_load(psp);
	if (ret)
		goto failed;

	ret = psp_asd_load(psp);
	if (ret) {
		DRM_ERROR("PSP load asd failed!\n");
		goto failed;
	}

	if (adev->gmc.xgmi.num_physical_nodes > 1) {
		ret = psp_xgmi_initialize(psp);
		/* Warning the XGMI seesion initialize failure
		 * Instead of stop driver initialization
		 */
		if (ret)
			dev_err(psp->adev->dev,
				"XGMI: Failed to initialize XGMI session\n");
	}

	if (psp->adev->psp.ta_fw) {
		ret = psp_ras_initialize(psp);
		if (ret)
			dev_err(psp->adev->dev,
					"RAS: Failed to initialize RAS\n");

		ret = psp_hdcp_initialize(psp);
		if (ret)
			dev_err(psp->adev->dev,
				"HDCP: Failed to initialize HDCP\n");

		ret = psp_dtm_initialize(psp);
		if (ret)
			dev_err(psp->adev->dev,
				"DTM: Failed to initialize DTM\n");

		ret = psp_rap_initialize(psp);
		if (ret)
			dev_err(psp->adev->dev,
				"RAP: Failed to initialize RAP\n");
	}

	mutex_unlock(&adev->firmware.mutex);

	return 0;

failed:
	DRM_ERROR("PSP resume failed\n");
	mutex_unlock(&adev->firmware.mutex);
	return ret;
}

int psp_gpu_reset(struct amdgpu_device *adev)
{
	int ret;

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP)
		return 0;

	mutex_lock(&adev->psp.mutex);
	ret = psp_mode1_reset(&adev->psp);
	mutex_unlock(&adev->psp.mutex);

	return ret;
}

int psp_rlc_autoload_start(struct psp_context *psp)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->cmd_id = GFX_CMD_ID_AUTOLOAD_RLC;

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);
	kfree(cmd);
	return ret;
}

int psp_update_vcn_sram(struct amdgpu_device *adev, int inst_idx,
			uint64_t cmd_gpu_addr, int cmd_size)
{
	struct amdgpu_firmware_info ucode = {0};

	ucode.ucode_id = inst_idx ? AMDGPU_UCODE_ID_VCN1_RAM :
		AMDGPU_UCODE_ID_VCN0_RAM;
	ucode.mc_addr = cmd_gpu_addr;
	ucode.ucode_size = cmd_size;

	return psp_execute_np_fw_load(&adev->psp, &ucode);
}

int psp_ring_cmd_submit(struct psp_context *psp,
			uint64_t cmd_buf_mc_addr,
			uint64_t fence_mc_addr,
			int index)
{
	unsigned int psp_write_ptr_reg = 0;
	struct psp_gfx_rb_frame *write_frame;
	struct psp_ring *ring = &psp->km_ring;
	struct psp_gfx_rb_frame *ring_buffer_start = ring->ring_mem;
	struct psp_gfx_rb_frame *ring_buffer_end = ring_buffer_start +
		ring->ring_size / sizeof(struct psp_gfx_rb_frame) - 1;
	struct amdgpu_device *adev = psp->adev;
	uint32_t ring_size_dw = ring->ring_size / 4;
	uint32_t rb_frame_size_dw = sizeof(struct psp_gfx_rb_frame) / 4;

	/* KM (GPCOM) prepare write pointer */
	psp_write_ptr_reg = psp_ring_get_wptr(psp);

	/* Update KM RB frame pointer to new frame */
	/* write_frame ptr increments by size of rb_frame in bytes */
	/* psp_write_ptr_reg increments by size of rb_frame in DWORDs */
	if ((psp_write_ptr_reg % ring_size_dw) == 0)
		write_frame = ring_buffer_start;
	else
		write_frame = ring_buffer_start + (psp_write_ptr_reg / rb_frame_size_dw);
	/* Check invalid write_frame ptr address */
	if ((write_frame < ring_buffer_start) || (ring_buffer_end < write_frame)) {
		DRM_ERROR("ring_buffer_start = %p; ring_buffer_end = %p; write_frame = %p\n",
			  ring_buffer_start, ring_buffer_end, write_frame);
		DRM_ERROR("write_frame is pointing to address out of bounds\n");
		return -EINVAL;
	}

	/* Initialize KM RB frame */
	memset(write_frame, 0, sizeof(struct psp_gfx_rb_frame));

	/* Update KM RB frame */
	write_frame->cmd_buf_addr_hi = upper_32_bits(cmd_buf_mc_addr);
	write_frame->cmd_buf_addr_lo = lower_32_bits(cmd_buf_mc_addr);
	write_frame->fence_addr_hi = upper_32_bits(fence_mc_addr);
	write_frame->fence_addr_lo = lower_32_bits(fence_mc_addr);
	write_frame->fence_value = index;
	amdgpu_asic_flush_hdp(adev, NULL);

	/* Update the write Pointer in DWORDs */
	psp_write_ptr_reg = (psp_write_ptr_reg + rb_frame_size_dw) % ring_size_dw;
	psp_ring_set_wptr(psp, psp_write_ptr_reg);
	return 0;
}

int psp_init_asd_microcode(struct psp_context *psp,
			   const char *chip_name)
{
	struct amdgpu_device *adev = psp->adev;
	char fw_name[30];
	const struct psp_firmware_header_v1_0 *asd_hdr;
	int err = 0;

	if (!chip_name) {
		dev_err(adev->dev, "invalid chip name for asd microcode\n");
		return -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_asd.bin", chip_name);
	err = request_firmware(&adev->psp.asd_fw, fw_name, adev->dev);
	if (err)
		goto out;

	err = amdgpu_ucode_validate(adev->psp.asd_fw);
	if (err)
		goto out;

	asd_hdr = (const struct psp_firmware_header_v1_0 *)adev->psp.asd_fw->data;
	adev->psp.asd_fw_version = le32_to_cpu(asd_hdr->header.ucode_version);
	adev->psp.asd_feature_version = le32_to_cpu(asd_hdr->ucode_feature_version);
	adev->psp.asd_ucode_size = le32_to_cpu(asd_hdr->header.ucode_size_bytes);
	adev->psp.asd_start_addr = (uint8_t *)asd_hdr +
				le32_to_cpu(asd_hdr->header.ucode_array_offset_bytes);
	return 0;
out:
	dev_err(adev->dev, "fail to initialize asd microcode\n");
	release_firmware(adev->psp.asd_fw);
	adev->psp.asd_fw = NULL;
	return err;
}

int psp_init_sos_microcode(struct psp_context *psp,
			   const char *chip_name)
{
	struct amdgpu_device *adev = psp->adev;
	char fw_name[30];
	const struct psp_firmware_header_v1_0 *sos_hdr;
	const struct psp_firmware_header_v1_1 *sos_hdr_v1_1;
	const struct psp_firmware_header_v1_2 *sos_hdr_v1_2;
	const struct psp_firmware_header_v1_3 *sos_hdr_v1_3;
	int err = 0;

	if (!chip_name) {
		dev_err(adev->dev, "invalid chip name for sos microcode\n");
		return -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_sos.bin", chip_name);
	err = request_firmware(&adev->psp.sos_fw, fw_name, adev->dev);
	if (err)
		goto out;

	err = amdgpu_ucode_validate(adev->psp.sos_fw);
	if (err)
		goto out;

	sos_hdr = (const struct psp_firmware_header_v1_0 *)adev->psp.sos_fw->data;
	amdgpu_ucode_print_psp_hdr(&sos_hdr->header);

	switch (sos_hdr->header.header_version_major) {
	case 1:
		adev->psp.sos_fw_version = le32_to_cpu(sos_hdr->header.ucode_version);
		adev->psp.sos_feature_version = le32_to_cpu(sos_hdr->ucode_feature_version);
		adev->psp.sos_bin_size = le32_to_cpu(sos_hdr->sos_size_bytes);
		adev->psp.sys_bin_size = le32_to_cpu(sos_hdr->sos_offset_bytes);
		adev->psp.sys_start_addr = (uint8_t *)sos_hdr +
				le32_to_cpu(sos_hdr->header.ucode_array_offset_bytes);
		adev->psp.sos_start_addr = (uint8_t *)adev->psp.sys_start_addr +
				le32_to_cpu(sos_hdr->sos_offset_bytes);
		if (sos_hdr->header.header_version_minor == 1) {
			sos_hdr_v1_1 = (const struct psp_firmware_header_v1_1 *)adev->psp.sos_fw->data;
			adev->psp.toc_bin_size = le32_to_cpu(sos_hdr_v1_1->toc_size_bytes);
			adev->psp.toc_start_addr = (uint8_t *)adev->psp.sys_start_addr +
					le32_to_cpu(sos_hdr_v1_1->toc_offset_bytes);
			adev->psp.kdb_bin_size = le32_to_cpu(sos_hdr_v1_1->kdb_size_bytes);
			adev->psp.kdb_start_addr = (uint8_t *)adev->psp.sys_start_addr +
					le32_to_cpu(sos_hdr_v1_1->kdb_offset_bytes);
		}
		if (sos_hdr->header.header_version_minor == 2) {
			sos_hdr_v1_2 = (const struct psp_firmware_header_v1_2 *)adev->psp.sos_fw->data;
			adev->psp.kdb_bin_size = le32_to_cpu(sos_hdr_v1_2->kdb_size_bytes);
			adev->psp.kdb_start_addr = (uint8_t *)adev->psp.sys_start_addr +
						    le32_to_cpu(sos_hdr_v1_2->kdb_offset_bytes);
		}
		if (sos_hdr->header.header_version_minor == 3) {
			sos_hdr_v1_3 = (const struct psp_firmware_header_v1_3 *)adev->psp.sos_fw->data;
			adev->psp.toc_bin_size = le32_to_cpu(sos_hdr_v1_3->v1_1.toc_size_bytes);
			adev->psp.toc_start_addr = (uint8_t *)adev->psp.sys_start_addr +
				le32_to_cpu(sos_hdr_v1_3->v1_1.toc_offset_bytes);
			adev->psp.kdb_bin_size = le32_to_cpu(sos_hdr_v1_3->v1_1.kdb_size_bytes);
			adev->psp.kdb_start_addr = (uint8_t *)adev->psp.sys_start_addr +
				le32_to_cpu(sos_hdr_v1_3->v1_1.kdb_offset_bytes);
			adev->psp.spl_bin_size = le32_to_cpu(sos_hdr_v1_3->spl_size_bytes);
			adev->psp.spl_start_addr = (uint8_t *)adev->psp.sys_start_addr +
				le32_to_cpu(sos_hdr_v1_3->spl_offset_bytes);
		}
		break;
	default:
		dev_err(adev->dev,
			"unsupported psp sos firmware\n");
		err = -EINVAL;
		goto out;
	}

	return 0;
out:
	dev_err(adev->dev,
		"failed to init sos firmware\n");
	release_firmware(adev->psp.sos_fw);
	adev->psp.sos_fw = NULL;

	return err;
}

int parse_ta_bin_descriptor(struct psp_context *psp,
			    const struct ta_fw_bin_desc *desc,
			    const struct ta_firmware_header_v2_0 *ta_hdr)
{
	uint8_t *ucode_start_addr  = NULL;

	if (!psp || !desc || !ta_hdr)
		return -EINVAL;

	ucode_start_addr  = (uint8_t *)ta_hdr +
			    le32_to_cpu(desc->offset_bytes) +
			    le32_to_cpu(ta_hdr->header.ucode_array_offset_bytes);

	switch (desc->fw_type) {
	case TA_FW_TYPE_PSP_ASD:
		psp->asd_fw_version 	   = le32_to_cpu(desc->fw_version);
		psp->asd_feature_version   = le32_to_cpu(desc->fw_version);
		psp->asd_ucode_size 	   = le32_to_cpu(desc->size_bytes);
		psp->asd_start_addr 	   = ucode_start_addr;
		psp->asd_fw                = psp->ta_fw;
		break;
	case TA_FW_TYPE_PSP_XGMI:
		psp->ta_xgmi_ucode_version = le32_to_cpu(desc->fw_version);
		psp->ta_xgmi_ucode_size    = le32_to_cpu(desc->size_bytes);
		psp->ta_xgmi_start_addr    = ucode_start_addr;
		break;
	case TA_FW_TYPE_PSP_RAS:
		psp->ta_ras_ucode_version  = le32_to_cpu(desc->fw_version);
		psp->ta_ras_ucode_size     = le32_to_cpu(desc->size_bytes);
		psp->ta_ras_start_addr     = ucode_start_addr;
		break;
	case TA_FW_TYPE_PSP_HDCP:
		psp->ta_hdcp_ucode_version = le32_to_cpu(desc->fw_version);
		psp->ta_hdcp_ucode_size    = le32_to_cpu(desc->size_bytes);
		psp->ta_hdcp_start_addr    = ucode_start_addr;
		break;
	case TA_FW_TYPE_PSP_DTM:
		psp->ta_dtm_ucode_version  = le32_to_cpu(desc->fw_version);
		psp->ta_dtm_ucode_size     = le32_to_cpu(desc->size_bytes);
		psp->ta_dtm_start_addr     = ucode_start_addr;
		break;
	case TA_FW_TYPE_PSP_RAP:
		psp->ta_rap_ucode_version  = le32_to_cpu(desc->fw_version);
		psp->ta_rap_ucode_size     = le32_to_cpu(desc->size_bytes);
		psp->ta_rap_start_addr     = ucode_start_addr;
		break;
	default:
		dev_warn(psp->adev->dev, "Unsupported TA type: %d\n", desc->fw_type);
		break;
	}

	return 0;
}

int psp_init_ta_microcode(struct psp_context *psp,
			  const char *chip_name)
{
	struct amdgpu_device *adev = psp->adev;
	char fw_name[30];
	const struct ta_firmware_header_v2_0 *ta_hdr;
	int err = 0;
	int ta_index = 0;

	if (!chip_name) {
		dev_err(adev->dev, "invalid chip name for ta microcode\n");
		return -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_ta.bin", chip_name);
	err = request_firmware(&adev->psp.ta_fw, fw_name, adev->dev);
	if (err)
		goto out;

	err = amdgpu_ucode_validate(adev->psp.ta_fw);
	if (err)
		goto out;

	ta_hdr = (const struct ta_firmware_header_v2_0 *)adev->psp.ta_fw->data;

	if (le16_to_cpu(ta_hdr->header.header_version_major) != 2) {
		dev_err(adev->dev, "unsupported TA header version\n");
		err = -EINVAL;
		goto out;
	}

	if (le32_to_cpu(ta_hdr->ta_fw_bin_count) >= UCODE_MAX_TA_PACKAGING) {
		dev_err(adev->dev, "packed TA count exceeds maximum limit\n");
		err = -EINVAL;
		goto out;
	}

	for (ta_index = 0; ta_index < le32_to_cpu(ta_hdr->ta_fw_bin_count); ta_index++) {
		err = parse_ta_bin_descriptor(psp,
					      &ta_hdr->ta_fw_bin[ta_index],
					      ta_hdr);
		if (err)
			goto out;
	}

	return 0;
out:
	dev_err(adev->dev, "fail to initialize ta microcode\n");
	release_firmware(adev->psp.ta_fw);
	adev->psp.ta_fw = NULL;
	return err;
}

static int psp_set_clockgating_state(void *handle,
				     enum amd_clockgating_state state)
{
	return 0;
}

static int psp_set_powergating_state(void *handle,
				     enum amd_powergating_state state)
{
	return 0;
}

static ssize_t psp_usbc_pd_fw_sysfs_read(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	uint32_t fw_ver;
	int ret;

	if (!adev->ip_blocks[AMD_IP_BLOCK_TYPE_PSP].status.late_initialized) {
		DRM_INFO("PSP block is not ready yet.");
		return -EBUSY;
	}

	mutex_lock(&adev->psp.mutex);
	ret = psp_read_usbc_pd_fw(&adev->psp, &fw_ver);
	mutex_unlock(&adev->psp.mutex);

	if (ret) {
		DRM_ERROR("Failed to read USBC PD FW, err = %d", ret);
		return ret;
	}

	return snprintf(buf, PAGE_SIZE, "%x\n", fw_ver);
}

static ssize_t psp_usbc_pd_fw_sysfs_write(struct device *dev,
						       struct device_attribute *attr,
						       const char *buf,
						       size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	void *cpu_addr;
	dma_addr_t dma_addr;
	int ret;
	char fw_name[100];
	const struct firmware *usbc_pd_fw;

	if (!adev->ip_blocks[AMD_IP_BLOCK_TYPE_PSP].status.late_initialized) {
		DRM_INFO("PSP block is not ready yet.");
		return -EBUSY;
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s", buf);
	ret = request_firmware(&usbc_pd_fw, fw_name, adev->dev);
	if (ret)
		goto fail;

	/* We need contiguous physical mem to place the FW  for psp to access */
	cpu_addr = dma_alloc_coherent(adev->dev, usbc_pd_fw->size, &dma_addr, GFP_KERNEL);

	ret = dma_mapping_error(adev->dev, dma_addr);
	if (ret)
		goto rel_buf;

	memcpy_toio(cpu_addr, usbc_pd_fw->data, usbc_pd_fw->size);

	/*
	 * x86 specific workaround.
	 * Without it the buffer is invisible in PSP.
	 *
	 * TODO Remove once PSP starts snooping CPU cache
	 */
#ifdef CONFIG_X86
	clflush_cache_range(cpu_addr, (usbc_pd_fw->size & ~(L1_CACHE_BYTES - 1)));
#endif

	mutex_lock(&adev->psp.mutex);
	ret = psp_load_usbc_pd_fw(&adev->psp, dma_addr);
	mutex_unlock(&adev->psp.mutex);

rel_buf:
	dma_free_coherent(adev->dev, usbc_pd_fw->size, cpu_addr, dma_addr);
	release_firmware(usbc_pd_fw);

fail:
	if (ret) {
		DRM_ERROR("Failed to load USBC PD FW, err = %d", ret);
		return ret;
	}

	return count;
}

static DEVICE_ATTR(usbc_pd_fw, S_IRUGO | S_IWUSR,
		   psp_usbc_pd_fw_sysfs_read,
		   psp_usbc_pd_fw_sysfs_write);



const struct amd_ip_funcs psp_ip_funcs = {
	.name = "psp",
	.early_init = psp_early_init,
	.late_init = NULL,
	.sw_init = psp_sw_init,
	.sw_fini = psp_sw_fini,
	.hw_init = psp_hw_init,
	.hw_fini = psp_hw_fini,
	.suspend = psp_suspend,
	.resume = psp_resume,
	.is_idle = NULL,
	.check_soft_reset = NULL,
	.wait_for_idle = NULL,
	.soft_reset = NULL,
	.set_clockgating_state = psp_set_clockgating_state,
	.set_powergating_state = psp_set_powergating_state,
};

static int psp_sysfs_init(struct amdgpu_device *adev)
{
	int ret = device_create_file(adev->dev, &dev_attr_usbc_pd_fw);

	if (ret)
		DRM_ERROR("Failed to create USBC PD FW control file!");

	return ret;
}

static void psp_sysfs_fini(struct amdgpu_device *adev)
{
	device_remove_file(adev->dev, &dev_attr_usbc_pd_fw);
}

const struct amdgpu_ip_block_version psp_v3_1_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_PSP,
	.major = 3,
	.minor = 1,
	.rev = 0,
	.funcs = &psp_ip_funcs,
};

const struct amdgpu_ip_block_version psp_v10_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_PSP,
	.major = 10,
	.minor = 0,
	.rev = 0,
	.funcs = &psp_ip_funcs,
};

const struct amdgpu_ip_block_version psp_v11_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_PSP,
	.major = 11,
	.minor = 0,
	.rev = 0,
	.funcs = &psp_ip_funcs,
};

const struct amdgpu_ip_block_version psp_v12_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_PSP,
	.major = 12,
	.minor = 0,
	.rev = 0,
	.funcs = &psp_ip_funcs,
};
