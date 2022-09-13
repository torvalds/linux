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
#include <drm/drm_drv.h>

#include "amdgpu.h"
#include "amdgpu_psp.h"
#include "amdgpu_ucode.h"
#include "amdgpu_xgmi.h"
#include "soc15_common.h"
#include "psp_v3_1.h"
#include "psp_v10_0.h"
#include "psp_v11_0.h"
#include "psp_v11_0_8.h"
#include "psp_v12_0.h"
#include "psp_v13_0.h"
#include "psp_v13_0_4.h"

#include "amdgpu_ras.h"
#include "amdgpu_securedisplay.h"
#include "amdgpu_atomfirmware.h"

#define AMD_VBIOS_FILE_MAX_SIZE_B      (1024*1024*3)

static int psp_sysfs_init(struct amdgpu_device *adev);
static void psp_sysfs_fini(struct amdgpu_device *adev);

static int psp_load_smu_fw(struct psp_context *psp);
static int psp_rap_terminate(struct psp_context *psp);
static int psp_securedisplay_terminate(struct psp_context *psp);

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
 *   - Arcturus and onwards
 */
static void psp_check_pmfw_centralized_cstate_management(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		psp->pmfw_centralized_cstate_management = false;
		return;
	}

	switch (adev->ip_versions[MP0_HWIP][0]) {
	case IP_VERSION(11, 0, 0):
	case IP_VERSION(11, 0, 4):
	case IP_VERSION(11, 0, 5):
	case IP_VERSION(11, 0, 7):
	case IP_VERSION(11, 0, 9):
	case IP_VERSION(11, 0, 11):
	case IP_VERSION(11, 0, 12):
	case IP_VERSION(11, 0, 13):
	case IP_VERSION(13, 0, 0):
	case IP_VERSION(13, 0, 2):
	case IP_VERSION(13, 0, 7):
		psp->pmfw_centralized_cstate_management = true;
		break;
	default:
		psp->pmfw_centralized_cstate_management = false;
		break;
	}
}

static int psp_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct psp_context *psp = &adev->psp;

	switch (adev->ip_versions[MP0_HWIP][0]) {
	case IP_VERSION(9, 0, 0):
		psp_v3_1_set_psp_funcs(psp);
		psp->autoload_supported = false;
		break;
	case IP_VERSION(10, 0, 0):
	case IP_VERSION(10, 0, 1):
		psp_v10_0_set_psp_funcs(psp);
		psp->autoload_supported = false;
		break;
	case IP_VERSION(11, 0, 2):
	case IP_VERSION(11, 0, 4):
		psp_v11_0_set_psp_funcs(psp);
		psp->autoload_supported = false;
		break;
	case IP_VERSION(11, 0, 0):
	case IP_VERSION(11, 0, 5):
	case IP_VERSION(11, 0, 9):
	case IP_VERSION(11, 0, 7):
	case IP_VERSION(11, 0, 11):
	case IP_VERSION(11, 5, 0):
	case IP_VERSION(11, 0, 12):
	case IP_VERSION(11, 0, 13):
		psp_v11_0_set_psp_funcs(psp);
		psp->autoload_supported = true;
		break;
	case IP_VERSION(11, 0, 3):
	case IP_VERSION(12, 0, 1):
		psp_v12_0_set_psp_funcs(psp);
		break;
	case IP_VERSION(13, 0, 2):
		psp_v13_0_set_psp_funcs(psp);
		break;
	case IP_VERSION(13, 0, 1):
	case IP_VERSION(13, 0, 3):
	case IP_VERSION(13, 0, 5):
	case IP_VERSION(13, 0, 8):
		psp_v13_0_set_psp_funcs(psp);
		psp->autoload_supported = true;
		break;
	case IP_VERSION(11, 0, 8):
		if (adev->apu_flags & AMD_APU_IS_CYAN_SKILLFISH2) {
			psp_v11_0_8_set_psp_funcs(psp);
			psp->autoload_supported = false;
		}
		break;
	case IP_VERSION(13, 0, 0):
	case IP_VERSION(13, 0, 7):
		psp_v13_0_set_psp_funcs(psp);
		psp->autoload_supported = true;
		break;
	case IP_VERSION(13, 0, 4):
		psp_v13_0_4_set_psp_funcs(psp);
		psp->autoload_supported = true;
		break;
	default:
		return -EINVAL;
	}

	psp->adev = adev;

	psp_check_pmfw_centralized_cstate_management(psp);

	return 0;
}

void psp_ta_free_shared_buf(struct ta_mem_context *mem_ctx)
{
	amdgpu_bo_free_kernel(&mem_ctx->shared_bo, &mem_ctx->shared_mc_addr,
			      &mem_ctx->shared_buf);
}

static void psp_free_shared_bufs(struct psp_context *psp)
{
	void *tmr_buf;
	void **pptr;

	/* free TMR memory buffer */
	pptr = amdgpu_sriov_vf(psp->adev) ? &tmr_buf : NULL;
	amdgpu_bo_free_kernel(&psp->tmr_bo, &psp->tmr_mc_addr, pptr);

	/* free xgmi shared memory */
	psp_ta_free_shared_buf(&psp->xgmi_context.context.mem_context);

	/* free ras shared memory */
	psp_ta_free_shared_buf(&psp->ras_context.context.mem_context);

	/* free hdcp shared memory */
	psp_ta_free_shared_buf(&psp->hdcp_context.context.mem_context);

	/* free dtm shared memory */
	psp_ta_free_shared_buf(&psp->dtm_context.context.mem_context);

	/* free rap shared memory */
	psp_ta_free_shared_buf(&psp->rap_context.context.mem_context);

	/* free securedisplay shared memory */
	psp_ta_free_shared_buf(&psp->securedisplay_context.context.mem_context);


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

/*
 * Helper funciton to query psp runtime database entry
 *
 * @adev: amdgpu_device pointer
 * @entry_type: the type of psp runtime database entry
 * @db_entry: runtime database entry pointer
 *
 * Return false if runtime database doesn't exit or entry is invalid
 * or true if the specific database entry is found, and copy to @db_entry
 */
static bool psp_get_runtime_db_entry(struct amdgpu_device *adev,
				     enum psp_runtime_entry_type entry_type,
				     void *db_entry)
{
	uint64_t db_header_pos, db_dir_pos;
	struct psp_runtime_data_header db_header = {0};
	struct psp_runtime_data_directory db_dir = {0};
	bool ret = false;
	int i;

	db_header_pos = adev->gmc.mc_vram_size - PSP_RUNTIME_DB_OFFSET;
	db_dir_pos = db_header_pos + sizeof(struct psp_runtime_data_header);

	/* read runtime db header from vram */
	amdgpu_device_vram_access(adev, db_header_pos, (uint32_t *)&db_header,
			sizeof(struct psp_runtime_data_header), false);

	if (db_header.cookie != PSP_RUNTIME_DB_COOKIE_ID) {
		/* runtime db doesn't exist, exit */
		dev_warn(adev->dev, "PSP runtime database doesn't exist\n");
		return false;
	}

	/* read runtime database entry from vram */
	amdgpu_device_vram_access(adev, db_dir_pos, (uint32_t *)&db_dir,
			sizeof(struct psp_runtime_data_directory), false);

	if (db_dir.entry_count >= PSP_RUNTIME_DB_DIAG_ENTRY_MAX_COUNT) {
		/* invalid db entry count, exit */
		dev_warn(adev->dev, "Invalid PSP runtime database entry count\n");
		return false;
	}

	/* look up for requested entry type */
	for (i = 0; i < db_dir.entry_count && !ret; i++) {
		if (db_dir.entry_list[i].entry_type == entry_type) {
			switch (entry_type) {
			case PSP_RUNTIME_ENTRY_TYPE_BOOT_CONFIG:
				if (db_dir.entry_list[i].size < sizeof(struct psp_runtime_boot_cfg_entry)) {
					/* invalid db entry size */
					dev_warn(adev->dev, "Invalid PSP runtime database boot cfg entry size\n");
					return false;
				}
				/* read runtime database entry */
				amdgpu_device_vram_access(adev, db_header_pos + db_dir.entry_list[i].offset,
							  (uint32_t *)db_entry, sizeof(struct psp_runtime_boot_cfg_entry), false);
				ret = true;
				break;
			case PSP_RUNTIME_ENTRY_TYPE_PPTABLE_ERR_STATUS:
				if (db_dir.entry_list[i].size < sizeof(struct psp_runtime_scpm_entry)) {
					/* invalid db entry size */
					dev_warn(adev->dev, "Invalid PSP runtime database scpm entry size\n");
					return false;
				}
				/* read runtime database entry */
				amdgpu_device_vram_access(adev, db_header_pos + db_dir.entry_list[i].offset,
							  (uint32_t *)db_entry, sizeof(struct psp_runtime_scpm_entry), false);
				ret = true;
				break;
			default:
				ret = false;
				break;
			}
		}
	}

	return ret;
}

static int psp_init_sriov_microcode(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	int ret = 0;

	switch (adev->ip_versions[MP0_HWIP][0]) {
	case IP_VERSION(9, 0, 0):
		ret = psp_init_cap_microcode(psp, "vega10");
		break;
	case IP_VERSION(11, 0, 9):
		ret = psp_init_cap_microcode(psp, "navi12");
		break;
	case IP_VERSION(11, 0, 7):
		ret = psp_init_cap_microcode(psp, "sienna_cichlid");
		break;
	case IP_VERSION(13, 0, 2):
		ret = psp_init_cap_microcode(psp, "aldebaran");
		ret &= psp_init_ta_microcode(psp, "aldebaran");
		break;
	default:
		BUG();
		break;
	}

	return ret;
}

static int psp_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct psp_context *psp = &adev->psp;
	int ret;
	struct psp_runtime_boot_cfg_entry boot_cfg_entry;
	struct psp_memory_training_context *mem_training_ctx = &psp->mem_train_ctx;
	struct psp_runtime_scpm_entry scpm_entry;

	psp->cmd = kzalloc(sizeof(struct psp_gfx_cmd_resp), GFP_KERNEL);
	if (!psp->cmd) {
		DRM_ERROR("Failed to allocate memory to command buffer!\n");
		ret = -ENOMEM;
	}

	if (amdgpu_sriov_vf(adev))
		ret = psp_init_sriov_microcode(psp);
	else
		ret = psp_init_microcode(psp);
	if (ret) {
		DRM_ERROR("Failed to load psp firmware!\n");
		return ret;
	}

	adev->psp.xgmi_context.supports_extended_data =
		!adev->gmc.xgmi.connected_to_cpu &&
			adev->ip_versions[MP0_HWIP][0] == IP_VERSION(13, 0, 2);

	memset(&scpm_entry, 0, sizeof(scpm_entry));
	if ((psp_get_runtime_db_entry(adev,
				PSP_RUNTIME_ENTRY_TYPE_PPTABLE_ERR_STATUS,
				&scpm_entry)) &&
	    (SCPM_DISABLE != scpm_entry.scpm_status)) {
		adev->scpm_enabled = true;
		adev->scpm_status = scpm_entry.scpm_status;
	} else {
		adev->scpm_enabled = false;
		adev->scpm_status = SCPM_DISABLE;
	}

	/* TODO: stop gpu driver services and print alarm if scpm is enabled with error status */

	memset(&boot_cfg_entry, 0, sizeof(boot_cfg_entry));
	if (psp_get_runtime_db_entry(adev,
				PSP_RUNTIME_ENTRY_TYPE_BOOT_CONFIG,
				&boot_cfg_entry)) {
		psp->boot_cfg_bitmask = boot_cfg_entry.boot_cfg_bitmask;
		if ((psp->boot_cfg_bitmask) &
		    BOOT_CFG_FEATURE_TWO_STAGE_DRAM_TRAINING) {
			/* If psp runtime database exists, then
			 * only enable two stage memory training
			 * when TWO_STAGE_DRAM_TRAINING bit is set
			 * in runtime database */
			mem_training_ctx->enable_mem_training = true;
		}

	} else {
		/* If psp runtime database doesn't exist or
		 * is invalid, force enable two stage memory
		 * training */
		mem_training_ctx->enable_mem_training = true;
	}

	if (mem_training_ctx->enable_mem_training) {
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
	}

	if (adev->ip_versions[MP0_HWIP][0] == IP_VERSION(11, 0, 0) ||
	    adev->ip_versions[MP0_HWIP][0] == IP_VERSION(11, 0, 7)) {
		ret= psp_sysfs_init(adev);
		if (ret) {
			return ret;
		}
	}

	ret = amdgpu_bo_create_kernel(adev, PSP_1_MEG, PSP_1_MEG,
				      amdgpu_sriov_vf(adev) ?
				      AMDGPU_GEM_DOMAIN_VRAM : AMDGPU_GEM_DOMAIN_GTT,
				      &psp->fw_pri_bo,
				      &psp->fw_pri_mc_addr,
				      &psp->fw_pri_buf);
	if (ret)
		return ret;

	ret = amdgpu_bo_create_kernel(adev, PSP_FENCE_BUFFER_SIZE, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &psp->fence_buf_bo,
				      &psp->fence_buf_mc_addr,
				      &psp->fence_buf);
	if (ret)
		goto failed1;

	ret = amdgpu_bo_create_kernel(adev, PSP_CMD_BUFFER_SIZE, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &psp->cmd_buf_bo, &psp->cmd_buf_mc_addr,
				      (void **)&psp->cmd_buf_mem);
	if (ret)
		goto failed2;

	return 0;

failed2:
	amdgpu_bo_free_kernel(&psp->fw_pri_bo,
			      &psp->fw_pri_mc_addr, &psp->fw_pri_buf);
failed1:
	amdgpu_bo_free_kernel(&psp->fence_buf_bo,
			      &psp->fence_buf_mc_addr, &psp->fence_buf);
	return ret;
}

static int psp_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct psp_context *psp = &adev->psp;
	struct psp_gfx_cmd_resp *cmd = psp->cmd;

	psp_memory_training_fini(psp);
	if (psp->sos_fw) {
		release_firmware(psp->sos_fw);
		psp->sos_fw = NULL;
	}
	if (psp->asd_fw) {
		release_firmware(psp->asd_fw);
		psp->asd_fw = NULL;
	}
	if (psp->ta_fw) {
		release_firmware(psp->ta_fw);
		psp->ta_fw = NULL;
	}
	if (adev->psp.cap_fw) {
		release_firmware(psp->cap_fw);
		psp->cap_fw = NULL;
	}

	if (adev->ip_versions[MP0_HWIP][0] == IP_VERSION(11, 0, 0) ||
	    adev->ip_versions[MP0_HWIP][0] == IP_VERSION(11, 0, 7))
		psp_sysfs_fini(adev);

	kfree(cmd);
	cmd = NULL;

	amdgpu_bo_free_kernel(&psp->fw_pri_bo,
			      &psp->fw_pri_mc_addr, &psp->fw_pri_buf);
	amdgpu_bo_free_kernel(&psp->fence_buf_bo,
			      &psp->fence_buf_mc_addr, &psp->fence_buf);
	amdgpu_bo_free_kernel(&psp->cmd_buf_bo, &psp->cmd_buf_mc_addr,
			      (void **)&psp->cmd_buf_mem);

	return 0;
}

int psp_wait_for(struct psp_context *psp, uint32_t reg_index,
		 uint32_t reg_val, uint32_t mask, bool check_changed)
{
	uint32_t val;
	int i;
	struct amdgpu_device *adev = psp->adev;

	if (psp->adev->no_hw_access)
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

static const char *psp_gfx_cmd_name(enum psp_gfx_cmd_id cmd_id)
{
	switch (cmd_id) {
	case GFX_CMD_ID_LOAD_TA:
		return "LOAD_TA";
	case GFX_CMD_ID_UNLOAD_TA:
		return "UNLOAD_TA";
	case GFX_CMD_ID_INVOKE_CMD:
		return "INVOKE_CMD";
	case GFX_CMD_ID_LOAD_ASD:
		return "LOAD_ASD";
	case GFX_CMD_ID_SETUP_TMR:
		return "SETUP_TMR";
	case GFX_CMD_ID_LOAD_IP_FW:
		return "LOAD_IP_FW";
	case GFX_CMD_ID_DESTROY_TMR:
		return "DESTROY_TMR";
	case GFX_CMD_ID_SAVE_RESTORE:
		return "SAVE_RESTORE_IP_FW";
	case GFX_CMD_ID_SETUP_VMR:
		return "SETUP_VMR";
	case GFX_CMD_ID_DESTROY_VMR:
		return "DESTROY_VMR";
	case GFX_CMD_ID_PROG_REG:
		return "PROG_REG";
	case GFX_CMD_ID_GET_FW_ATTESTATION:
		return "GET_FW_ATTESTATION";
	case GFX_CMD_ID_LOAD_TOC:
		return "ID_LOAD_TOC";
	case GFX_CMD_ID_AUTOLOAD_RLC:
		return "AUTOLOAD_RLC";
	case GFX_CMD_ID_BOOT_CFG:
		return "BOOT_CFG";
	default:
		return "UNKNOWN CMD";
	}
}

static int
psp_cmd_submit_buf(struct psp_context *psp,
		   struct amdgpu_firmware_info *ucode,
		   struct psp_gfx_cmd_resp *cmd, uint64_t fence_mc_addr)
{
	int ret;
	int index, idx;
	int timeout = 20000;
	bool ras_intr = false;
	bool skip_unsupport = false;

	if (psp->adev->no_hw_access)
		return 0;

	if (!drm_dev_enter(adev_to_drm(psp->adev), &idx))
		return 0;

	memset(psp->cmd_buf_mem, 0, PSP_CMD_BUFFER_SIZE);

	memcpy(psp->cmd_buf_mem, cmd, sizeof(struct psp_gfx_cmd_resp));

	index = atomic_inc_return(&psp->fence_value);
	ret = psp_ring_cmd_submit(psp, psp->cmd_buf_mc_addr, fence_mc_addr, index);
	if (ret) {
		atomic_dec(&psp->fence_value);
		goto exit;
	}

	amdgpu_device_invalidate_hdp(psp->adev, NULL);
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
		usleep_range(10, 100);
		amdgpu_device_invalidate_hdp(psp->adev, NULL);
	}

	/* We allow TEE_ERROR_NOT_SUPPORTED for VMR command and PSP_ERR_UNKNOWN_COMMAND in SRIOV */
	skip_unsupport = (psp->cmd_buf_mem->resp.status == TEE_ERROR_NOT_SUPPORTED ||
		psp->cmd_buf_mem->resp.status == PSP_ERR_UNKNOWN_COMMAND) && amdgpu_sriov_vf(psp->adev);

	memcpy((void*)&cmd->resp, (void*)&psp->cmd_buf_mem->resp, sizeof(struct psp_gfx_resp));

	/* In some cases, psp response status is not 0 even there is no
	 * problem while the command is submitted. Some version of PSP FW
	 * doesn't write 0 to that field.
	 * So here we would like to only print a warning instead of an error
	 * during psp initialization to avoid breaking hw_init and it doesn't
	 * return -EINVAL.
	 */
	if (!skip_unsupport && (psp->cmd_buf_mem->resp.status || !timeout) && !ras_intr) {
		if (ucode)
			DRM_WARN("failed to load ucode %s(0x%X) ",
				  amdgpu_ucode_name(ucode->ucode_id), ucode->ucode_id);
		DRM_WARN("psp gfx command %s(0x%X) failed and response status is (0x%X)\n",
			 psp_gfx_cmd_name(psp->cmd_buf_mem->cmd_id), psp->cmd_buf_mem->cmd_id,
			 psp->cmd_buf_mem->resp.status);
		/* If any firmware (including CAP) load fails under SRIOV, it should
		 * return failure to stop the VF from initializing.
		 * Also return failure in case of timeout
		 */
		if ((ucode && amdgpu_sriov_vf(psp->adev)) || !timeout) {
			ret = -EINVAL;
			goto exit;
		}
	}

	if (ucode) {
		ucode->tmr_mc_addr_lo = psp->cmd_buf_mem->resp.fw_addr_lo;
		ucode->tmr_mc_addr_hi = psp->cmd_buf_mem->resp.fw_addr_hi;
	}

exit:
	drm_dev_exit(idx);
	return ret;
}

static struct psp_gfx_cmd_resp *acquire_psp_cmd_buf(struct psp_context *psp)
{
	struct psp_gfx_cmd_resp *cmd = psp->cmd;

	mutex_lock(&psp->mutex);

	memset(cmd, 0, sizeof(struct psp_gfx_cmd_resp));

	return cmd;
}

static void release_psp_cmd_buf(struct psp_context *psp)
{
	mutex_unlock(&psp->mutex);
}

static void psp_prep_tmr_cmd_buf(struct psp_context *psp,
				 struct psp_gfx_cmd_resp *cmd,
				 uint64_t tmr_mc, struct amdgpu_bo *tmr_bo)
{
	struct amdgpu_device *adev = psp->adev;
	uint32_t size = amdgpu_bo_size(tmr_bo);
	uint64_t tmr_pa = amdgpu_gmc_vram_pa(adev, tmr_bo);

	if (amdgpu_sriov_vf(psp->adev))
		cmd->cmd_id = GFX_CMD_ID_SETUP_VMR;
	else
		cmd->cmd_id = GFX_CMD_ID_SETUP_TMR;
	cmd->cmd.cmd_setup_tmr.buf_phy_addr_lo = lower_32_bits(tmr_mc);
	cmd->cmd.cmd_setup_tmr.buf_phy_addr_hi = upper_32_bits(tmr_mc);
	cmd->cmd.cmd_setup_tmr.buf_size = size;
	cmd->cmd.cmd_setup_tmr.bitfield.virt_phy_addr = 1;
	cmd->cmd.cmd_setup_tmr.system_phy_addr_lo = lower_32_bits(tmr_pa);
	cmd->cmd.cmd_setup_tmr.system_phy_addr_hi = upper_32_bits(tmr_pa);
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
	struct psp_gfx_cmd_resp *cmd = acquire_psp_cmd_buf(psp);

	/* Copy toc to psp firmware private buffer */
	psp_copy_fw(psp, psp->toc.start_addr, psp->toc.size_bytes);

	psp_prep_load_toc_cmd_buf(cmd, psp->fw_pri_mc_addr, psp->toc.size_bytes);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);
	if (!ret)
		*tmr_size = psp->cmd_buf_mem->resp.tmr_size;

	release_psp_cmd_buf(psp);

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
	tmr_size = PSP_TMR_SIZE(psp->adev);

	/* For ASICs support RLC autoload, psp will parse the toc
	 * and calculate the total size of TMR needed */
	if (!amdgpu_sriov_vf(psp->adev) &&
	    psp->toc.start_addr &&
	    psp->toc.size_bytes &&
	    psp->fw_pri_buf) {
		ret = psp_load_toc(psp, &tmr_size);
		if (ret) {
			DRM_ERROR("Failed to load toc\n");
			return ret;
		}
	}

	pptr = amdgpu_sriov_vf(psp->adev) ? &tmr_buf : NULL;
	ret = amdgpu_bo_create_kernel(psp->adev, tmr_size, PSP_TMR_SIZE(psp->adev),
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &psp->tmr_bo, &psp->tmr_mc_addr, pptr);

	return ret;
}

static bool psp_skip_tmr(struct psp_context *psp)
{
	switch (psp->adev->ip_versions[MP0_HWIP][0]) {
	case IP_VERSION(11, 0, 9):
	case IP_VERSION(11, 0, 7):
	case IP_VERSION(13, 0, 2):
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

	cmd = acquire_psp_cmd_buf(psp);

	psp_prep_tmr_cmd_buf(psp, cmd, psp->tmr_mc_addr, psp->tmr_bo);
	DRM_INFO("reserve 0x%lx from 0x%llx for PSP TMR\n",
		 amdgpu_bo_size(psp->tmr_bo), psp->tmr_mc_addr);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);

	release_psp_cmd_buf(psp);

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
	struct psp_gfx_cmd_resp *cmd = acquire_psp_cmd_buf(psp);

	psp_prep_tmr_unload_cmd_buf(psp, cmd);
	DRM_INFO("free PSP TMR buffer\n");

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);

	release_psp_cmd_buf(psp);

	return ret;
}

static int psp_tmr_terminate(struct psp_context *psp)
{
	return psp_tmr_unload(psp);
}

int psp_get_fw_attestation_records_addr(struct psp_context *psp,
					uint64_t *output_ptr)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	if (!output_ptr)
		return -EINVAL;

	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	cmd = acquire_psp_cmd_buf(psp);

	cmd->cmd_id = GFX_CMD_ID_GET_FW_ATTESTATION;

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);

	if (!ret) {
		*output_ptr = ((uint64_t)cmd->resp.uresp.fwar_db_info.fwar_db_addr_lo) +
			      ((uint64_t)cmd->resp.uresp.fwar_db_info.fwar_db_addr_hi << 32);
	}

	release_psp_cmd_buf(psp);

	return ret;
}

static int psp_boot_config_get(struct amdgpu_device *adev, uint32_t *boot_cfg)
{
	struct psp_context *psp = &adev->psp;
	struct psp_gfx_cmd_resp *cmd;
	int ret;

	if (amdgpu_sriov_vf(adev))
		return 0;

	cmd = acquire_psp_cmd_buf(psp);

	cmd->cmd_id = GFX_CMD_ID_BOOT_CFG;
	cmd->cmd.boot_cfg.sub_cmd = BOOTCFG_CMD_GET;

	ret = psp_cmd_submit_buf(psp, NULL, cmd, psp->fence_buf_mc_addr);
	if (!ret) {
		*boot_cfg =
			(cmd->resp.uresp.boot_cfg.boot_cfg & BOOT_CONFIG_GECC) ? 1 : 0;
	}

	release_psp_cmd_buf(psp);

	return ret;
}

static int psp_boot_config_set(struct amdgpu_device *adev, uint32_t boot_cfg)
{
	int ret;
	struct psp_context *psp = &adev->psp;
	struct psp_gfx_cmd_resp *cmd;

	if (amdgpu_sriov_vf(adev))
		return 0;

	cmd = acquire_psp_cmd_buf(psp);

	cmd->cmd_id = GFX_CMD_ID_BOOT_CFG;
	cmd->cmd.boot_cfg.sub_cmd = BOOTCFG_CMD_SET;
	cmd->cmd.boot_cfg.boot_config = boot_cfg;
	cmd->cmd.boot_cfg.boot_config_valid = boot_cfg;

	ret = psp_cmd_submit_buf(psp, NULL, cmd, psp->fence_buf_mc_addr);

	release_psp_cmd_buf(psp);

	return ret;
}

static int psp_rl_load(struct amdgpu_device *adev)
{
	int ret;
	struct psp_context *psp = &adev->psp;
	struct psp_gfx_cmd_resp *cmd;

	if (!is_psp_fw_valid(psp->rl))
		return 0;

	cmd = acquire_psp_cmd_buf(psp);

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);
	memcpy(psp->fw_pri_buf, psp->rl.start_addr, psp->rl.size_bytes);

	cmd->cmd_id = GFX_CMD_ID_LOAD_IP_FW;
	cmd->cmd.cmd_load_ip_fw.fw_phy_addr_lo = lower_32_bits(psp->fw_pri_mc_addr);
	cmd->cmd.cmd_load_ip_fw.fw_phy_addr_hi = upper_32_bits(psp->fw_pri_mc_addr);
	cmd->cmd.cmd_load_ip_fw.fw_size = psp->rl.size_bytes;
	cmd->cmd.cmd_load_ip_fw.fw_type = GFX_FW_TYPE_REG_LIST;

	ret = psp_cmd_submit_buf(psp, NULL, cmd, psp->fence_buf_mc_addr);

	release_psp_cmd_buf(psp);

	return ret;
}

static int psp_asd_initialize(struct psp_context *psp)
{
	int ret;

	/* If PSP version doesn't match ASD version, asd loading will be failed.
	 * add workaround to bypass it for sriov now.
	 * TODO: add version check to make it common
	 */
	if (amdgpu_sriov_vf(psp->adev) || !psp->asd_context.bin_desc.size_bytes)
		return 0;

	psp->asd_context.mem_context.shared_mc_addr  = 0;
	psp->asd_context.mem_context.shared_mem_size = PSP_ASD_SHARED_MEM_SIZE;
	psp->asd_context.ta_load_type                = GFX_CMD_ID_LOAD_ASD;

	ret = psp_ta_load(psp, &psp->asd_context);
	if (!ret)
		psp->asd_context.initialized = true;

	return ret;
}

static void psp_prep_ta_unload_cmd_buf(struct psp_gfx_cmd_resp *cmd,
				       uint32_t session_id)
{
	cmd->cmd_id = GFX_CMD_ID_UNLOAD_TA;
	cmd->cmd.cmd_unload_ta.session_id = session_id;
}

int psp_ta_unload(struct psp_context *psp, struct ta_context *context)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd = acquire_psp_cmd_buf(psp);

	psp_prep_ta_unload_cmd_buf(cmd, context->session_id);

	ret = psp_cmd_submit_buf(psp, NULL, cmd, psp->fence_buf_mc_addr);

	release_psp_cmd_buf(psp);

	return ret;
}

static int psp_asd_terminate(struct psp_context *psp)
{
	int ret;

	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->asd_context.initialized)
		return 0;

	ret = psp_ta_unload(psp, &psp->asd_context);
	if (!ret)
		psp->asd_context.initialized = false;

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
	struct psp_gfx_cmd_resp *cmd;
	int ret = 0;

	if (reg >= PSP_REG_LAST)
		return -EINVAL;

	cmd = acquire_psp_cmd_buf(psp);

	psp_prep_reg_prog_cmd_buf(cmd, reg, value);
	ret = psp_cmd_submit_buf(psp, NULL, cmd, psp->fence_buf_mc_addr);
	if (ret)
		DRM_ERROR("PSP failed to program reg id %d", reg);

	release_psp_cmd_buf(psp);

	return ret;
}

static void psp_prep_ta_load_cmd_buf(struct psp_gfx_cmd_resp *cmd,
				     uint64_t ta_bin_mc,
				     struct ta_context *context)
{
	cmd->cmd_id				= context->ta_load_type;
	cmd->cmd.cmd_load_ta.app_phy_addr_lo 	= lower_32_bits(ta_bin_mc);
	cmd->cmd.cmd_load_ta.app_phy_addr_hi	= upper_32_bits(ta_bin_mc);
	cmd->cmd.cmd_load_ta.app_len		= context->bin_desc.size_bytes;

	cmd->cmd.cmd_load_ta.cmd_buf_phy_addr_lo =
		lower_32_bits(context->mem_context.shared_mc_addr);
	cmd->cmd.cmd_load_ta.cmd_buf_phy_addr_hi =
		upper_32_bits(context->mem_context.shared_mc_addr);
	cmd->cmd.cmd_load_ta.cmd_buf_len = context->mem_context.shared_mem_size;
}

int psp_ta_init_shared_buf(struct psp_context *psp,
				  struct ta_mem_context *mem_ctx)
{
	/*
	* Allocate 16k memory aligned to 4k from Frame Buffer (local
	* physical) for ta to host memory
	*/
	return amdgpu_bo_create_kernel(psp->adev, mem_ctx->shared_mem_size,
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM,
				      &mem_ctx->shared_bo,
				      &mem_ctx->shared_mc_addr,
				      &mem_ctx->shared_buf);
}

static void psp_prep_ta_invoke_indirect_cmd_buf(struct psp_gfx_cmd_resp *cmd,
				       uint32_t ta_cmd_id,
				       struct ta_context *context)
{
	cmd->cmd_id                         = GFX_CMD_ID_INVOKE_CMD;
	cmd->cmd.cmd_invoke_cmd.session_id  = context->session_id;
	cmd->cmd.cmd_invoke_cmd.ta_cmd_id   = ta_cmd_id;

	cmd->cmd.cmd_invoke_cmd.buf.num_desc   = 1;
	cmd->cmd.cmd_invoke_cmd.buf.total_size = context->mem_context.shared_mem_size;
	cmd->cmd.cmd_invoke_cmd.buf.buf_desc[0].buf_size = context->mem_context.shared_mem_size;
	cmd->cmd.cmd_invoke_cmd.buf.buf_desc[0].buf_phy_addr_lo =
				     lower_32_bits(context->mem_context.shared_mc_addr);
	cmd->cmd.cmd_invoke_cmd.buf.buf_desc[0].buf_phy_addr_hi =
				     upper_32_bits(context->mem_context.shared_mc_addr);
}

int psp_ta_invoke_indirect(struct psp_context *psp,
		  uint32_t ta_cmd_id,
		  struct ta_context *context)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd = acquire_psp_cmd_buf(psp);

	psp_prep_ta_invoke_indirect_cmd_buf(cmd, ta_cmd_id, context);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);

	context->resp_status = cmd->resp.status;

	release_psp_cmd_buf(psp);

	return ret;
}

static void psp_prep_ta_invoke_cmd_buf(struct psp_gfx_cmd_resp *cmd,
				       uint32_t ta_cmd_id,
				       uint32_t session_id)
{
	cmd->cmd_id				= GFX_CMD_ID_INVOKE_CMD;
	cmd->cmd.cmd_invoke_cmd.session_id	= session_id;
	cmd->cmd.cmd_invoke_cmd.ta_cmd_id	= ta_cmd_id;
}

int psp_ta_invoke(struct psp_context *psp,
		  uint32_t ta_cmd_id,
		  struct ta_context *context)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd = acquire_psp_cmd_buf(psp);

	psp_prep_ta_invoke_cmd_buf(cmd, ta_cmd_id, context->session_id);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);

	context->resp_status = cmd->resp.status;

	release_psp_cmd_buf(psp);

	return ret;
}

int psp_ta_load(struct psp_context *psp, struct ta_context *context)
{
	int ret;
	struct psp_gfx_cmd_resp *cmd;

	cmd = acquire_psp_cmd_buf(psp);

	psp_copy_fw(psp, context->bin_desc.start_addr,
		    context->bin_desc.size_bytes);

	psp_prep_ta_load_cmd_buf(cmd, psp->fw_pri_mc_addr, context);

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);

	context->resp_status = cmd->resp.status;

	if (!ret) {
		context->session_id = cmd->resp.session_id;
	}

	release_psp_cmd_buf(psp);

	return ret;
}

int psp_xgmi_invoke(struct psp_context *psp, uint32_t ta_cmd_id)
{
	return psp_ta_invoke(psp, ta_cmd_id, &psp->xgmi_context.context);
}

int psp_xgmi_terminate(struct psp_context *psp)
{
	int ret;
	struct amdgpu_device *adev = psp->adev;

	/* XGMI TA unload currently is not supported on Arcturus/Aldebaran A+A */
	if (adev->ip_versions[MP0_HWIP][0] == IP_VERSION(11, 0, 4) ||
	    (adev->ip_versions[MP0_HWIP][0] == IP_VERSION(13, 0, 2) &&
	     adev->gmc.xgmi.connected_to_cpu))
		return 0;

	if (!psp->xgmi_context.context.initialized)
		return 0;

	ret = psp_ta_unload(psp, &psp->xgmi_context.context);

	psp->xgmi_context.context.initialized = false;

	return ret;
}

int psp_xgmi_initialize(struct psp_context *psp, bool set_extended_data, bool load_ta)
{
	struct ta_xgmi_shared_memory *xgmi_cmd;
	int ret;

	if (!psp->ta_fw ||
	    !psp->xgmi_context.context.bin_desc.size_bytes ||
	    !psp->xgmi_context.context.bin_desc.start_addr)
		return -ENOENT;

	if (!load_ta)
		goto invoke;

	psp->xgmi_context.context.mem_context.shared_mem_size = PSP_XGMI_SHARED_MEM_SIZE;
	psp->xgmi_context.context.ta_load_type = GFX_CMD_ID_LOAD_TA;

	if (!psp->xgmi_context.context.mem_context.shared_buf) {
		ret = psp_ta_init_shared_buf(psp, &psp->xgmi_context.context.mem_context);
		if (ret)
			return ret;
	}

	/* Load XGMI TA */
	ret = psp_ta_load(psp, &psp->xgmi_context.context);
	if (!ret)
		psp->xgmi_context.context.initialized = true;
	else
		return ret;

invoke:
	/* Initialize XGMI session */
	xgmi_cmd = (struct ta_xgmi_shared_memory *)(psp->xgmi_context.context.mem_context.shared_buf);
	memset(xgmi_cmd, 0, sizeof(struct ta_xgmi_shared_memory));
	xgmi_cmd->flag_extend_link_record = set_extended_data;
	xgmi_cmd->cmd_id = TA_COMMAND_XGMI__INITIALIZE;

	ret = psp_xgmi_invoke(psp, xgmi_cmd->cmd_id);

	return ret;
}

int psp_xgmi_get_hive_id(struct psp_context *psp, uint64_t *hive_id)
{
	struct ta_xgmi_shared_memory *xgmi_cmd;
	int ret;

	xgmi_cmd = (struct ta_xgmi_shared_memory *)psp->xgmi_context.context.mem_context.shared_buf;
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

	xgmi_cmd = (struct ta_xgmi_shared_memory *)psp->xgmi_context.context.mem_context.shared_buf;
	memset(xgmi_cmd, 0, sizeof(struct ta_xgmi_shared_memory));

	xgmi_cmd->cmd_id = TA_COMMAND_XGMI__GET_NODE_ID;

	/* Invoke xgmi ta to get the node id */
	ret = psp_xgmi_invoke(psp, xgmi_cmd->cmd_id);
	if (ret)
		return ret;

	*node_id = xgmi_cmd->xgmi_out_message.get_node_id.node_id;

	return 0;
}

static bool psp_xgmi_peer_link_info_supported(struct psp_context *psp)
{
	return psp->adev->ip_versions[MP0_HWIP][0] == IP_VERSION(13, 0, 2) &&
		psp->xgmi_context.context.bin_desc.fw_version >= 0x2000000b;
}

/*
 * Chips that support extended topology information require the driver to
 * reflect topology information in the opposite direction.  This is
 * because the TA has already exceeded its link record limit and if the
 * TA holds bi-directional information, the driver would have to do
 * multiple fetches instead of just two.
 */
static void psp_xgmi_reflect_topology_info(struct psp_context *psp,
					struct psp_xgmi_node_info node_info)
{
	struct amdgpu_device *mirror_adev;
	struct amdgpu_hive_info *hive;
	uint64_t src_node_id = psp->adev->gmc.xgmi.node_id;
	uint64_t dst_node_id = node_info.node_id;
	uint8_t dst_num_hops = node_info.num_hops;
	uint8_t dst_num_links = node_info.num_links;

	hive = amdgpu_get_xgmi_hive(psp->adev);
	list_for_each_entry(mirror_adev, &hive->device_list, gmc.xgmi.head) {
		struct psp_xgmi_topology_info *mirror_top_info;
		int j;

		if (mirror_adev->gmc.xgmi.node_id != dst_node_id)
			continue;

		mirror_top_info = &mirror_adev->psp.xgmi_context.top_info;
		for (j = 0; j < mirror_top_info->num_nodes; j++) {
			if (mirror_top_info->nodes[j].node_id != src_node_id)
				continue;

			mirror_top_info->nodes[j].num_hops = dst_num_hops;
			/*
			 * prevent 0 num_links value re-reflection since reflection
			 * criteria is based on num_hops (direct or indirect).
			 *
			 */
			if (dst_num_links)
				mirror_top_info->nodes[j].num_links = dst_num_links;

			break;
		}

		break;
	}

	amdgpu_put_xgmi_hive(hive);
}

int psp_xgmi_get_topology_info(struct psp_context *psp,
			       int number_devices,
			       struct psp_xgmi_topology_info *topology,
			       bool get_extended_data)
{
	struct ta_xgmi_shared_memory *xgmi_cmd;
	struct ta_xgmi_cmd_get_topology_info_input *topology_info_input;
	struct ta_xgmi_cmd_get_topology_info_output *topology_info_output;
	int i;
	int ret;

	if (!topology || topology->num_nodes > TA_XGMI__MAX_CONNECTED_NODES)
		return -EINVAL;

	xgmi_cmd = (struct ta_xgmi_shared_memory *)psp->xgmi_context.context.mem_context.shared_buf;
	memset(xgmi_cmd, 0, sizeof(struct ta_xgmi_shared_memory));
	xgmi_cmd->flag_extend_link_record = get_extended_data;

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
		/* extended data will either be 0 or equal to non-extended data */
		if (topology_info_output->nodes[i].num_hops)
			topology->nodes[i].num_hops = topology_info_output->nodes[i].num_hops;

		/* non-extended data gets everything here so no need to update */
		if (!get_extended_data) {
			topology->nodes[i].node_id = topology_info_output->nodes[i].node_id;
			topology->nodes[i].is_sharing_enabled =
					topology_info_output->nodes[i].is_sharing_enabled;
			topology->nodes[i].sdma_engine =
					topology_info_output->nodes[i].sdma_engine;
		}

	}

	/* Invoke xgmi ta again to get the link information */
	if (psp_xgmi_peer_link_info_supported(psp)) {
		struct ta_xgmi_cmd_get_peer_link_info_output *link_info_output;

		xgmi_cmd->cmd_id = TA_COMMAND_XGMI__GET_PEER_LINKS;

		ret = psp_xgmi_invoke(psp, TA_COMMAND_XGMI__GET_PEER_LINKS);

		if (ret)
			return ret;

		link_info_output = &xgmi_cmd->xgmi_out_message.get_link_info;
		for (i = 0; i < topology->num_nodes; i++) {
			/* accumulate num_links on extended data */
			topology->nodes[i].num_links = get_extended_data ?
					topology->nodes[i].num_links +
							link_info_output->nodes[i].num_links :
					link_info_output->nodes[i].num_links;

			/* reflect the topology information for bi-directionality */
			if (psp->xgmi_context.supports_extended_data &&
					get_extended_data && topology->nodes[i].num_hops)
				psp_xgmi_reflect_topology_info(psp, topology->nodes[i]);
		}
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

	xgmi_cmd = (struct ta_xgmi_shared_memory *)psp->xgmi_context.context.mem_context.shared_buf;
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
static void psp_ras_ta_check_status(struct psp_context *psp)
{
	struct ta_ras_shared_memory *ras_cmd =
		(struct ta_ras_shared_memory *)psp->ras_context.context.mem_context.shared_buf;

	switch (ras_cmd->ras_status) {
	case TA_RAS_STATUS__ERROR_UNSUPPORTED_IP:
		dev_warn(psp->adev->dev,
				"RAS WARNING: cmd failed due to unsupported ip\n");
		break;
	case TA_RAS_STATUS__ERROR_UNSUPPORTED_ERROR_INJ:
		dev_warn(psp->adev->dev,
				"RAS WARNING: cmd failed due to unsupported error injection\n");
		break;
	case TA_RAS_STATUS__SUCCESS:
		break;
	case TA_RAS_STATUS__TEE_ERROR_ACCESS_DENIED:
		if (ras_cmd->cmd_id == TA_RAS_COMMAND__TRIGGER_ERROR)
			dev_warn(psp->adev->dev,
					"RAS WARNING: Inject error to critical region is not allowed\n");
		break;
	default:
		dev_warn(psp->adev->dev,
				"RAS WARNING: ras status = 0x%X\n", ras_cmd->ras_status);
		break;
	}
}

int psp_ras_invoke(struct psp_context *psp, uint32_t ta_cmd_id)
{
	struct ta_ras_shared_memory *ras_cmd;
	int ret;

	ras_cmd = (struct ta_ras_shared_memory *)psp->ras_context.context.mem_context.shared_buf;

	/*
	 * TODO: bypass the loading in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	ret = psp_ta_invoke(psp, ta_cmd_id, &psp->ras_context.context);

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

		psp_ras_ta_check_status(psp);
	}

	return ret;
}

int psp_ras_enable_features(struct psp_context *psp,
		union ta_ras_cmd_input *info, bool enable)
{
	struct ta_ras_shared_memory *ras_cmd;
	int ret;

	if (!psp->ras_context.context.initialized)
		return -EINVAL;

	ras_cmd = (struct ta_ras_shared_memory *)psp->ras_context.context.mem_context.shared_buf;
	memset(ras_cmd, 0, sizeof(struct ta_ras_shared_memory));

	if (enable)
		ras_cmd->cmd_id = TA_RAS_COMMAND__ENABLE_FEATURES;
	else
		ras_cmd->cmd_id = TA_RAS_COMMAND__DISABLE_FEATURES;

	ras_cmd->ras_in_message = *info;

	ret = psp_ras_invoke(psp, ras_cmd->cmd_id);
	if (ret)
		return -EINVAL;

	return 0;
}

int psp_ras_terminate(struct psp_context *psp)
{
	int ret;

	/*
	 * TODO: bypass the terminate in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->ras_context.context.initialized)
		return 0;

	ret = psp_ta_unload(psp, &psp->ras_context.context);

	psp->ras_context.context.initialized = false;

	return ret;
}

static int psp_ras_initialize(struct psp_context *psp)
{
	int ret;
	uint32_t boot_cfg = 0xFF;
	struct amdgpu_device *adev = psp->adev;
	struct ta_ras_shared_memory *ras_cmd;

	/*
	 * TODO: bypass the initialize in sriov for now
	 */
	if (amdgpu_sriov_vf(adev))
		return 0;

	if (!adev->psp.ras_context.context.bin_desc.size_bytes ||
	    !adev->psp.ras_context.context.bin_desc.start_addr) {
		dev_info(adev->dev, "RAS: optional ras ta ucode is not available\n");
		return 0;
	}

	if (amdgpu_atomfirmware_dynamic_boot_config_supported(adev)) {
		/* query GECC enablement status from boot config
		 * boot_cfg: 1: GECC is enabled or 0: GECC is disabled
		 */
		ret = psp_boot_config_get(adev, &boot_cfg);
		if (ret)
			dev_warn(adev->dev, "PSP get boot config failed\n");

		if (!amdgpu_ras_is_supported(psp->adev, AMDGPU_RAS_BLOCK__UMC)) {
			if (!boot_cfg) {
				dev_info(adev->dev, "GECC is disabled\n");
			} else {
				/* disable GECC in next boot cycle if ras is
				 * disabled by module parameter amdgpu_ras_enable
				 * and/or amdgpu_ras_mask, or boot_config_get call
				 * is failed
				 */
				ret = psp_boot_config_set(adev, 0);
				if (ret)
					dev_warn(adev->dev, "PSP set boot config failed\n");
				else
					dev_warn(adev->dev, "GECC will be disabled in next boot cycle "
						 "if set amdgpu_ras_enable and/or amdgpu_ras_mask to 0x0\n");
			}
		} else {
			if (1 == boot_cfg) {
				dev_info(adev->dev, "GECC is enabled\n");
			} else {
				/* enable GECC in next boot cycle if it is disabled
				 * in boot config, or force enable GECC if failed to
				 * get boot configuration
				 */
				ret = psp_boot_config_set(adev, BOOT_CONFIG_GECC);
				if (ret)
					dev_warn(adev->dev, "PSP set boot config failed\n");
				else
					dev_warn(adev->dev, "GECC will be enabled in next boot cycle\n");
			}
		}
	}

	psp->ras_context.context.mem_context.shared_mem_size = PSP_RAS_SHARED_MEM_SIZE;
	psp->ras_context.context.ta_load_type = GFX_CMD_ID_LOAD_TA;

	if (!psp->ras_context.context.initialized) {
		ret = psp_ta_init_shared_buf(psp, &psp->ras_context.context.mem_context);
		if (ret)
			return ret;
	}

	ras_cmd = (struct ta_ras_shared_memory *)psp->ras_context.context.mem_context.shared_buf;
	memset(ras_cmd, 0, sizeof(struct ta_ras_shared_memory));

	if (amdgpu_ras_is_poison_mode_supported(adev))
		ras_cmd->ras_in_message.init_flags.poison_mode_en = 1;
	if (!adev->gmc.xgmi.connected_to_cpu)
		ras_cmd->ras_in_message.init_flags.dgpu_mode = 1;

	ret = psp_ta_load(psp, &psp->ras_context.context);

	if (!ret && !ras_cmd->ras_status)
		psp->ras_context.context.initialized = true;
	else {
		if (ras_cmd->ras_status)
			dev_warn(psp->adev->dev, "RAS Init Status: 0x%X\n", ras_cmd->ras_status);
		amdgpu_ras_fini(psp->adev);
	}

	return ret;
}

int psp_ras_trigger_error(struct psp_context *psp,
			  struct ta_ras_trigger_error_input *info)
{
	struct ta_ras_shared_memory *ras_cmd;
	int ret;

	if (!psp->ras_context.context.initialized)
		return -EINVAL;

	ras_cmd = (struct ta_ras_shared_memory *)psp->ras_context.context.mem_context.shared_buf;
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

	if (ras_cmd->ras_status == TA_RAS_STATUS__TEE_ERROR_ACCESS_DENIED)
		return -EACCES;
	else if (ras_cmd->ras_status)
		return -EINVAL;

	return 0;
}
// ras end

// HDCP start
static int psp_hdcp_initialize(struct psp_context *psp)
{
	int ret;

	/*
	 * TODO: bypass the initialize in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->hdcp_context.context.bin_desc.size_bytes ||
	    !psp->hdcp_context.context.bin_desc.start_addr) {
		dev_info(psp->adev->dev, "HDCP: optional hdcp ta ucode is not available\n");
		return 0;
	}

	psp->hdcp_context.context.mem_context.shared_mem_size = PSP_HDCP_SHARED_MEM_SIZE;
	psp->hdcp_context.context.ta_load_type = GFX_CMD_ID_LOAD_TA;

	if (!psp->hdcp_context.context.initialized) {
		ret = psp_ta_init_shared_buf(psp, &psp->hdcp_context.context.mem_context);
		if (ret)
			return ret;
	}

	ret = psp_ta_load(psp, &psp->hdcp_context.context);
	if (!ret) {
		psp->hdcp_context.context.initialized = true;
		mutex_init(&psp->hdcp_context.mutex);
	}

	return ret;
}

int psp_hdcp_invoke(struct psp_context *psp, uint32_t ta_cmd_id)
{
	/*
	 * TODO: bypass the loading in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	return psp_ta_invoke(psp, ta_cmd_id, &psp->hdcp_context.context);
}

static int psp_hdcp_terminate(struct psp_context *psp)
{
	int ret;

	/*
	 * TODO: bypass the terminate in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->hdcp_context.context.initialized)
		return 0;

	ret = psp_ta_unload(psp, &psp->hdcp_context.context);

	psp->hdcp_context.context.initialized = false;

	return ret;
}
// HDCP end

// DTM start
static int psp_dtm_initialize(struct psp_context *psp)
{
	int ret;

	/*
	 * TODO: bypass the initialize in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->dtm_context.context.bin_desc.size_bytes ||
	    !psp->dtm_context.context.bin_desc.start_addr) {
		dev_info(psp->adev->dev, "DTM: optional dtm ta ucode is not available\n");
		return 0;
	}

	psp->dtm_context.context.mem_context.shared_mem_size = PSP_DTM_SHARED_MEM_SIZE;
	psp->dtm_context.context.ta_load_type = GFX_CMD_ID_LOAD_TA;

	if (!psp->dtm_context.context.initialized) {
		ret = psp_ta_init_shared_buf(psp, &psp->dtm_context.context.mem_context);
		if (ret)
			return ret;
	}

	ret = psp_ta_load(psp, &psp->dtm_context.context);
	if (!ret) {
		psp->dtm_context.context.initialized = true;
		mutex_init(&psp->dtm_context.mutex);
	}

	return ret;
}

int psp_dtm_invoke(struct psp_context *psp, uint32_t ta_cmd_id)
{
	/*
	 * TODO: bypass the loading in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	return psp_ta_invoke(psp, ta_cmd_id, &psp->dtm_context.context);
}

static int psp_dtm_terminate(struct psp_context *psp)
{
	int ret;

	/*
	 * TODO: bypass the terminate in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->dtm_context.context.initialized)
		return 0;

	ret = psp_ta_unload(psp, &psp->dtm_context.context);

	psp->dtm_context.context.initialized = false;

	return ret;
}
// DTM end

// RAP start
static int psp_rap_initialize(struct psp_context *psp)
{
	int ret;
	enum ta_rap_status status = TA_RAP_STATUS__SUCCESS;

	/*
	 * TODO: bypass the initialize in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->rap_context.context.bin_desc.size_bytes ||
	    !psp->rap_context.context.bin_desc.start_addr) {
		dev_info(psp->adev->dev, "RAP: optional rap ta ucode is not available\n");
		return 0;
	}

	psp->rap_context.context.mem_context.shared_mem_size = PSP_RAP_SHARED_MEM_SIZE;
	psp->rap_context.context.ta_load_type = GFX_CMD_ID_LOAD_TA;

	if (!psp->rap_context.context.initialized) {
		ret = psp_ta_init_shared_buf(psp, &psp->rap_context.context.mem_context);
		if (ret)
			return ret;
	}

	ret = psp_ta_load(psp, &psp->rap_context.context);
	if (!ret) {
		psp->rap_context.context.initialized = true;
		mutex_init(&psp->rap_context.mutex);
	} else
		return ret;

	ret = psp_rap_invoke(psp, TA_CMD_RAP__INITIALIZE, &status);
	if (ret || status != TA_RAP_STATUS__SUCCESS) {
		psp_rap_terminate(psp);
		/* free rap shared memory */
		psp_ta_free_shared_buf(&psp->rap_context.context.mem_context);

		dev_warn(psp->adev->dev, "RAP TA initialize fail (%d) status %d.\n",
			 ret, status);

		return ret;
	}

	return 0;
}

static int psp_rap_terminate(struct psp_context *psp)
{
	int ret;

	if (!psp->rap_context.context.initialized)
		return 0;

	ret = psp_ta_unload(psp, &psp->rap_context.context);

	psp->rap_context.context.initialized = false;

	return ret;
}

int psp_rap_invoke(struct psp_context *psp, uint32_t ta_cmd_id, enum ta_rap_status *status)
{
	struct ta_rap_shared_memory *rap_cmd;
	int ret = 0;

	if (!psp->rap_context.context.initialized)
		return 0;

	if (ta_cmd_id != TA_CMD_RAP__INITIALIZE &&
	    ta_cmd_id != TA_CMD_RAP__VALIDATE_L0)
		return -EINVAL;

	mutex_lock(&psp->rap_context.mutex);

	rap_cmd = (struct ta_rap_shared_memory *)
		  psp->rap_context.context.mem_context.shared_buf;
	memset(rap_cmd, 0, sizeof(struct ta_rap_shared_memory));

	rap_cmd->cmd_id = ta_cmd_id;
	rap_cmd->validation_method_id = METHOD_A;

	ret = psp_ta_invoke(psp, rap_cmd->cmd_id, &psp->rap_context.context);
	if (ret)
		goto out_unlock;

	if (status)
		*status = rap_cmd->rap_status;

out_unlock:
	mutex_unlock(&psp->rap_context.mutex);

	return ret;
}
// RAP end

/* securedisplay start */
static int psp_securedisplay_initialize(struct psp_context *psp)
{
	int ret;
	struct securedisplay_cmd *securedisplay_cmd;

	/*
	 * TODO: bypass the initialize in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->securedisplay_context.context.bin_desc.size_bytes ||
	    !psp->securedisplay_context.context.bin_desc.start_addr) {
		dev_info(psp->adev->dev, "SECUREDISPLAY: securedisplay ta ucode is not available\n");
		return 0;
	}

	psp->securedisplay_context.context.mem_context.shared_mem_size =
		PSP_SECUREDISPLAY_SHARED_MEM_SIZE;
	psp->securedisplay_context.context.ta_load_type = GFX_CMD_ID_LOAD_TA;

	if (!psp->securedisplay_context.context.initialized) {
		ret = psp_ta_init_shared_buf(psp,
					     &psp->securedisplay_context.context.mem_context);
		if (ret)
			return ret;
	}

	ret = psp_ta_load(psp, &psp->securedisplay_context.context);
	if (!ret) {
		psp->securedisplay_context.context.initialized = true;
		mutex_init(&psp->securedisplay_context.mutex);
	} else
		return ret;

	psp_prep_securedisplay_cmd_buf(psp, &securedisplay_cmd,
			TA_SECUREDISPLAY_COMMAND__QUERY_TA);

	ret = psp_securedisplay_invoke(psp, TA_SECUREDISPLAY_COMMAND__QUERY_TA);
	if (ret) {
		psp_securedisplay_terminate(psp);
		/* free securedisplay shared memory */
		psp_ta_free_shared_buf(&psp->securedisplay_context.context.mem_context);
		dev_err(psp->adev->dev, "SECUREDISPLAY TA initialize fail.\n");
		return -EINVAL;
	}

	if (securedisplay_cmd->status != TA_SECUREDISPLAY_STATUS__SUCCESS) {
		psp_securedisplay_parse_resp_status(psp, securedisplay_cmd->status);
		dev_err(psp->adev->dev, "SECUREDISPLAY: query securedisplay TA failed. ret 0x%x\n",
			securedisplay_cmd->securedisplay_out_message.query_ta.query_cmd_ret);
	}

	return 0;
}

static int psp_securedisplay_terminate(struct psp_context *psp)
{
	int ret;

	/*
	 * TODO:bypass the terminate in sriov for now
	 */
	if (amdgpu_sriov_vf(psp->adev))
		return 0;

	if (!psp->securedisplay_context.context.initialized)
		return 0;

	ret = psp_ta_unload(psp, &psp->securedisplay_context.context);

	psp->securedisplay_context.context.initialized = false;

	return ret;
}

int psp_securedisplay_invoke(struct psp_context *psp, uint32_t ta_cmd_id)
{
	int ret;

	if (!psp->securedisplay_context.context.initialized)
		return -EINVAL;

	if (ta_cmd_id != TA_SECUREDISPLAY_COMMAND__QUERY_TA &&
	    ta_cmd_id != TA_SECUREDISPLAY_COMMAND__SEND_ROI_CRC)
		return -EINVAL;

	mutex_lock(&psp->securedisplay_context.mutex);

	ret = psp_ta_invoke(psp, ta_cmd_id, &psp->securedisplay_context.context);

	mutex_unlock(&psp->securedisplay_context.mutex);

	return ret;
}
/* SECUREDISPLAY end */

static int psp_hw_start(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	int ret;

	if (!amdgpu_sriov_vf(adev)) {
		if ((is_psp_fw_valid(psp->kdb)) &&
		    (psp->funcs->bootloader_load_kdb != NULL)) {
			ret = psp_bootloader_load_kdb(psp);
			if (ret) {
				DRM_ERROR("PSP load kdb failed!\n");
				return ret;
			}
		}

		if ((is_psp_fw_valid(psp->spl)) &&
		    (psp->funcs->bootloader_load_spl != NULL)) {
			ret = psp_bootloader_load_spl(psp);
			if (ret) {
				DRM_ERROR("PSP load spl failed!\n");
				return ret;
			}
		}

		if ((is_psp_fw_valid(psp->sys)) &&
		    (psp->funcs->bootloader_load_sysdrv != NULL)) {
			ret = psp_bootloader_load_sysdrv(psp);
			if (ret) {
				DRM_ERROR("PSP load sys drv failed!\n");
				return ret;
			}
		}

		if ((is_psp_fw_valid(psp->soc_drv)) &&
		    (psp->funcs->bootloader_load_soc_drv != NULL)) {
			ret = psp_bootloader_load_soc_drv(psp);
			if (ret) {
				DRM_ERROR("PSP load soc drv failed!\n");
				return ret;
			}
		}

		if ((is_psp_fw_valid(psp->intf_drv)) &&
		    (psp->funcs->bootloader_load_intf_drv != NULL)) {
			ret = psp_bootloader_load_intf_drv(psp);
			if (ret) {
				DRM_ERROR("PSP load intf drv failed!\n");
				return ret;
			}
		}

		if ((is_psp_fw_valid(psp->dbg_drv)) &&
		    (psp->funcs->bootloader_load_dbg_drv != NULL)) {
			ret = psp_bootloader_load_dbg_drv(psp);
			if (ret) {
				DRM_ERROR("PSP load dbg drv failed!\n");
				return ret;
			}
		}

		if ((is_psp_fw_valid(psp->sos)) &&
		    (psp->funcs->bootloader_load_sos != NULL)) {
			ret = psp_bootloader_load_sos(psp);
			if (ret) {
				DRM_ERROR("PSP load sos failed!\n");
				return ret;
			}
		}
	}

	ret = psp_ring_create(psp, PSP_RING_TYPE__KM);
	if (ret) {
		DRM_ERROR("PSP create ring failed!\n");
		return ret;
	}

	if (amdgpu_sriov_vf(adev) && amdgpu_in_reset(adev))
		goto skip_pin_bo;

	ret = psp_tmr_init(psp);
	if (ret) {
		DRM_ERROR("PSP tmr init failed!\n");
		return ret;
	}

skip_pin_bo:
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
	case AMDGPU_UCODE_ID_CAP:
		*type = GFX_FW_TYPE_CAP;
		break;
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
	case AMDGPU_UCODE_ID_CP_MES1:
		*type = GFX_FW_TYPE_CP_MES_KIQ;
		break;
	case AMDGPU_UCODE_ID_CP_MES1_DATA:
		*type = GFX_FW_TYPE_MES_KIQ_STACK;
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
	case AMDGPU_UCODE_ID_RLC_P:
		*type = GFX_FW_TYPE_RLC_P;
		break;
	case AMDGPU_UCODE_ID_RLC_V:
		*type = GFX_FW_TYPE_RLC_V;
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
	case AMDGPU_UCODE_ID_GLOBAL_TAP_DELAYS:
		*type = GFX_FW_TYPE_GLOBAL_TAP_DELAYS;
		break;
	case AMDGPU_UCODE_ID_SE0_TAP_DELAYS:
		*type = GFX_FW_TYPE_SE0_TAP_DELAYS;
		break;
	case AMDGPU_UCODE_ID_SE1_TAP_DELAYS:
		*type = GFX_FW_TYPE_SE1_TAP_DELAYS;
		break;
	case AMDGPU_UCODE_ID_SE2_TAP_DELAYS:
		*type = GFX_FW_TYPE_SE2_TAP_DELAYS;
		break;
	case AMDGPU_UCODE_ID_SE3_TAP_DELAYS:
		*type = GFX_FW_TYPE_SE3_TAP_DELAYS;
		break;
	case AMDGPU_UCODE_ID_SMC:
		*type = GFX_FW_TYPE_SMU;
		break;
	case AMDGPU_UCODE_ID_PPTABLE:
		*type = GFX_FW_TYPE_PPTABLE;
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
	case AMDGPU_UCODE_ID_SDMA_UCODE_TH0:
		*type = GFX_FW_TYPE_SDMA_UCODE_TH0;
		break;
	case AMDGPU_UCODE_ID_SDMA_UCODE_TH1:
		*type = GFX_FW_TYPE_SDMA_UCODE_TH1;
		break;
	case AMDGPU_UCODE_ID_IMU_I:
		*type = GFX_FW_TYPE_IMU_I;
		break;
	case AMDGPU_UCODE_ID_IMU_D:
		*type = GFX_FW_TYPE_IMU_D;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_PFP:
		*type = GFX_FW_TYPE_RS64_PFP;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_ME:
		*type = GFX_FW_TYPE_RS64_ME;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC:
		*type = GFX_FW_TYPE_RS64_MEC;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_PFP_P0_STACK:
		*type = GFX_FW_TYPE_RS64_PFP_P0_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_PFP_P1_STACK:
		*type = GFX_FW_TYPE_RS64_PFP_P1_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_ME_P0_STACK:
		*type = GFX_FW_TYPE_RS64_ME_P0_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_ME_P1_STACK:
		*type = GFX_FW_TYPE_RS64_ME_P1_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P0_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P0_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P1_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P1_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P2_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P2_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P3_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P3_STACK;
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

	cmd->cmd_id = GFX_CMD_ID_LOAD_IP_FW;
	cmd->cmd.cmd_load_ip_fw.fw_phy_addr_lo = lower_32_bits(fw_mem_mc_addr);
	cmd->cmd.cmd_load_ip_fw.fw_phy_addr_hi = upper_32_bits(fw_mem_mc_addr);
	cmd->cmd.cmd_load_ip_fw.fw_size = ucode->ucode_size;

	ret = psp_get_fw_type(ucode, &cmd->cmd.cmd_load_ip_fw.fw_type);
	if (ret)
		DRM_ERROR("Unknown firmware type\n");

	return ret;
}

static int psp_execute_non_psp_fw_load(struct psp_context *psp,
			          struct amdgpu_firmware_info *ucode)
{
	int ret = 0;
	struct psp_gfx_cmd_resp *cmd = acquire_psp_cmd_buf(psp);

	ret = psp_prep_load_ip_fw_cmd_buf(ucode, cmd);
	if (!ret) {
		ret = psp_cmd_submit_buf(psp, ucode, cmd,
					 psp->fence_buf_mc_addr);
	}

	release_psp_cmd_buf(psp);

	return ret;
}

static int psp_load_smu_fw(struct psp_context *psp)
{
	int ret;
	struct amdgpu_device *adev = psp->adev;
	struct amdgpu_firmware_info *ucode =
			&adev->firmware.ucode[AMDGPU_UCODE_ID_SMC];
	struct amdgpu_ras *ras = psp->ras_context.ras;

	/*
	 * Skip SMU FW reloading in case of using BACO for runpm only,
	 * as SMU is always alive.
	 */
	if (adev->in_runpm && (adev->pm.rpm_mode == AMDGPU_RUNPM_BACO))
		return 0;

	if (!ucode->fw || amdgpu_sriov_vf(psp->adev))
		return 0;

	if ((amdgpu_in_reset(adev) &&
	     ras && adev->ras_enabled &&
	     (adev->ip_versions[MP0_HWIP][0] == IP_VERSION(11, 0, 4) ||
	      adev->ip_versions[MP0_HWIP][0] == IP_VERSION(11, 0, 2)))) {
		ret = amdgpu_dpm_set_mp1_state(adev, PP_MP1_STATE_UNLOAD);
		if (ret) {
			DRM_WARN("Failed to set MP1 state prepare for reload\n");
		}
	}

	ret = psp_execute_non_psp_fw_load(psp, ucode);

	if (ret)
		DRM_ERROR("PSP load smu failed!\n");

	return ret;
}

static bool fw_load_skip_check(struct psp_context *psp,
			       struct amdgpu_firmware_info *ucode)
{
	if (!ucode->fw || !ucode->ucode_size)
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

int psp_load_fw_list(struct psp_context *psp,
		     struct amdgpu_firmware_info **ucode_list, int ucode_count)
{
	int ret = 0, i;
	struct amdgpu_firmware_info *ucode;

	for (i = 0; i < ucode_count; ++i) {
		ucode = ucode_list[i];
		psp_print_fw_hdr(psp, ucode);
		ret = psp_execute_non_psp_fw_load(psp, ucode);
		if (ret)
			return ret;
	}
	return ret;
}

static int psp_load_non_psp_fw(struct psp_context *psp)
{
	int i, ret;
	struct amdgpu_firmware_info *ucode;
	struct amdgpu_device *adev = psp->adev;

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
		    (adev->ip_versions[MP0_HWIP][0] == IP_VERSION(11, 0, 7) ||
		     adev->ip_versions[MP0_HWIP][0] == IP_VERSION(11, 0, 11) ||
		     adev->ip_versions[MP0_HWIP][0] == IP_VERSION(11, 0, 12)) &&
		    (ucode->ucode_id == AMDGPU_UCODE_ID_SDMA1 ||
		     ucode->ucode_id == AMDGPU_UCODE_ID_SDMA2 ||
		     ucode->ucode_id == AMDGPU_UCODE_ID_SDMA3))
			/* PSP only receive one SDMA fw for sienna_cichlid,
			 * as all four sdma fw are same */
			continue;

		psp_print_fw_hdr(psp, ucode);

		ret = psp_execute_non_psp_fw_load(psp, ucode);
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
		/* should not destroy ring, only stop */
		psp_ring_stop(psp, PSP_RING_TYPE__KM);
	} else {
		memset(psp->fence_buf, 0, PSP_FENCE_BUFFER_SIZE);

		ret = psp_ring_init(psp, PSP_RING_TYPE__KM);
		if (ret) {
			DRM_ERROR("PSP ring init failed!\n");
			goto failed;
		}
	}

	ret = psp_hw_start(psp);
	if (ret)
		goto failed;

	ret = psp_load_non_psp_fw(psp);
	if (ret)
		goto failed1;

	ret = psp_asd_initialize(psp);
	if (ret) {
		DRM_ERROR("PSP load asd failed!\n");
		goto failed1;
	}

	ret = psp_rl_load(adev);
	if (ret) {
		DRM_ERROR("PSP load RL failed!\n");
		goto failed1;
	}

	if (amdgpu_sriov_vf(adev) && amdgpu_in_reset(adev)) {
		if (adev->gmc.xgmi.num_physical_nodes > 1) {
			ret = psp_xgmi_initialize(psp, false, true);
			/* Warning the XGMI seesion initialize failure
			* Instead of stop driver initialization
			*/
			if (ret)
				dev_err(psp->adev->dev,
					"XGMI: Failed to initialize XGMI session\n");
		}
	}

	if (psp->ta_fw) {
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

		ret = psp_securedisplay_initialize(psp);
		if (ret)
			dev_err(psp->adev->dev,
				"SECUREDISPLAY: Failed to initialize SECUREDISPLAY\n");
	}

	return 0;

failed1:
	psp_free_shared_bufs(psp);
failed:
	/*
	 * all cleanup jobs (xgmi terminate, ras terminate,
	 * ring destroy, cmd/fence/fw buffers destory,
	 * psp->cmd destory) are delayed to psp_hw_fini
	 */
	psp_ring_destroy(psp, PSP_RING_TYPE__KM);
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

	if (psp->ta_fw) {
		psp_ras_terminate(psp);
		psp_securedisplay_terminate(psp);
		psp_rap_terminate(psp);
		psp_dtm_terminate(psp);
		psp_hdcp_terminate(psp);

		if (adev->gmc.xgmi.num_physical_nodes > 1)
			psp_xgmi_terminate(psp);
	}

	psp_asd_terminate(psp);
	psp_tmr_terminate(psp);

	psp_ring_destroy(psp, PSP_RING_TYPE__KM);

	psp_free_shared_bufs(psp);

	return 0;
}

static int psp_suspend(void *handle)
{
	int ret = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct psp_context *psp = &adev->psp;

	if (adev->gmc.xgmi.num_physical_nodes > 1 &&
	    psp->xgmi_context.context.initialized) {
		ret = psp_xgmi_terminate(psp);
		if (ret) {
			DRM_ERROR("Failed to terminate xgmi ta\n");
			goto out;
		}
	}

	if (psp->ta_fw) {
		ret = psp_ras_terminate(psp);
		if (ret) {
			DRM_ERROR("Failed to terminate ras ta\n");
			goto out;
		}
		ret = psp_hdcp_terminate(psp);
		if (ret) {
			DRM_ERROR("Failed to terminate hdcp ta\n");
			goto out;
		}
		ret = psp_dtm_terminate(psp);
		if (ret) {
			DRM_ERROR("Failed to terminate dtm ta\n");
			goto out;
		}
		ret = psp_rap_terminate(psp);
		if (ret) {
			DRM_ERROR("Failed to terminate rap ta\n");
			goto out;
		}
		ret = psp_securedisplay_terminate(psp);
		if (ret) {
			DRM_ERROR("Failed to terminate securedisplay ta\n");
			goto out;
		}
	}

	ret = psp_asd_terminate(psp);
	if (ret) {
		DRM_ERROR("Failed to terminate asd\n");
		goto out;
	}

	ret = psp_tmr_terminate(psp);
	if (ret) {
		DRM_ERROR("Failed to terminate tmr\n");
		goto out;
	}

	ret = psp_ring_stop(psp, PSP_RING_TYPE__KM);
	if (ret) {
		DRM_ERROR("PSP ring stop failed\n");
	}

out:
	psp_free_shared_bufs(psp);

	return ret;
}

static int psp_resume(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct psp_context *psp = &adev->psp;

	DRM_INFO("PSP is resuming...\n");

	if (psp->mem_train_ctx.enable_mem_training) {
		ret = psp_mem_training(psp, PSP_MEM_TRAIN_RESUME);
		if (ret) {
			DRM_ERROR("Failed to process memory training!\n");
			return ret;
		}
	}

	mutex_lock(&adev->firmware.mutex);

	ret = psp_hw_start(psp);
	if (ret)
		goto failed;

	ret = psp_load_non_psp_fw(psp);
	if (ret)
		goto failed;

	ret = psp_asd_initialize(psp);
	if (ret) {
		DRM_ERROR("PSP load asd failed!\n");
		goto failed;
	}

	ret = psp_rl_load(adev);
	if (ret) {
		dev_err(adev->dev, "PSP load RL failed!\n");
		goto failed;
	}

	if (adev->gmc.xgmi.num_physical_nodes > 1) {
		ret = psp_xgmi_initialize(psp, false, true);
		/* Warning the XGMI seesion initialize failure
		 * Instead of stop driver initialization
		 */
		if (ret)
			dev_err(psp->adev->dev,
				"XGMI: Failed to initialize XGMI session\n");
	}

	if (psp->ta_fw) {
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

		ret = psp_securedisplay_initialize(psp);
		if (ret)
			dev_err(psp->adev->dev,
				"SECUREDISPLAY: Failed to initialize SECUREDISPLAY\n");
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
	struct psp_gfx_cmd_resp *cmd = acquire_psp_cmd_buf(psp);

	cmd->cmd_id = GFX_CMD_ID_AUTOLOAD_RLC;

	ret = psp_cmd_submit_buf(psp, NULL, cmd,
				 psp->fence_buf_mc_addr);

	release_psp_cmd_buf(psp);

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

	return psp_execute_non_psp_fw_load(&adev->psp, &ucode);
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
	amdgpu_device_flush_hdp(adev, NULL);

	/* Update the write Pointer in DWORDs */
	psp_write_ptr_reg = (psp_write_ptr_reg + rb_frame_size_dw) % ring_size_dw;
	psp_ring_set_wptr(psp, psp_write_ptr_reg);
	return 0;
}

int psp_init_asd_microcode(struct psp_context *psp,
			   const char *chip_name)
{
	struct amdgpu_device *adev = psp->adev;
	char fw_name[PSP_FW_NAME_LEN];
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
	adev->psp.asd_context.bin_desc.fw_version = le32_to_cpu(asd_hdr->header.ucode_version);
	adev->psp.asd_context.bin_desc.feature_version = le32_to_cpu(asd_hdr->sos.fw_version);
	adev->psp.asd_context.bin_desc.size_bytes = le32_to_cpu(asd_hdr->header.ucode_size_bytes);
	adev->psp.asd_context.bin_desc.start_addr = (uint8_t *)asd_hdr +
				le32_to_cpu(asd_hdr->header.ucode_array_offset_bytes);
	return 0;
out:
	dev_err(adev->dev, "fail to initialize asd microcode\n");
	release_firmware(adev->psp.asd_fw);
	adev->psp.asd_fw = NULL;
	return err;
}

int psp_init_toc_microcode(struct psp_context *psp,
			   const char *chip_name)
{
	struct amdgpu_device *adev = psp->adev;
	char fw_name[PSP_FW_NAME_LEN];
	const struct psp_firmware_header_v1_0 *toc_hdr;
	int err = 0;

	if (!chip_name) {
		dev_err(adev->dev, "invalid chip name for toc microcode\n");
		return -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_toc.bin", chip_name);
	err = request_firmware(&adev->psp.toc_fw, fw_name, adev->dev);
	if (err)
		goto out;

	err = amdgpu_ucode_validate(adev->psp.toc_fw);
	if (err)
		goto out;

	toc_hdr = (const struct psp_firmware_header_v1_0 *)adev->psp.toc_fw->data;
	adev->psp.toc.fw_version = le32_to_cpu(toc_hdr->header.ucode_version);
	adev->psp.toc.feature_version = le32_to_cpu(toc_hdr->sos.fw_version);
	adev->psp.toc.size_bytes = le32_to_cpu(toc_hdr->header.ucode_size_bytes);
	adev->psp.toc.start_addr = (uint8_t *)toc_hdr +
				le32_to_cpu(toc_hdr->header.ucode_array_offset_bytes);
	return 0;
out:
	dev_err(adev->dev, "fail to request/validate toc microcode\n");
	release_firmware(adev->psp.toc_fw);
	adev->psp.toc_fw = NULL;
	return err;
}

static int parse_sos_bin_descriptor(struct psp_context *psp,
				   const struct psp_fw_bin_desc *desc,
				   const struct psp_firmware_header_v2_0 *sos_hdr)
{
	uint8_t *ucode_start_addr  = NULL;

	if (!psp || !desc || !sos_hdr)
		return -EINVAL;

	ucode_start_addr  = (uint8_t *)sos_hdr +
			    le32_to_cpu(desc->offset_bytes) +
			    le32_to_cpu(sos_hdr->header.ucode_array_offset_bytes);

	switch (desc->fw_type) {
	case PSP_FW_TYPE_PSP_SOS:
		psp->sos.fw_version        = le32_to_cpu(desc->fw_version);
		psp->sos.feature_version   = le32_to_cpu(desc->fw_version);
		psp->sos.size_bytes        = le32_to_cpu(desc->size_bytes);
		psp->sos.start_addr 	   = ucode_start_addr;
		break;
	case PSP_FW_TYPE_PSP_SYS_DRV:
		psp->sys.fw_version        = le32_to_cpu(desc->fw_version);
		psp->sys.feature_version   = le32_to_cpu(desc->fw_version);
		psp->sys.size_bytes        = le32_to_cpu(desc->size_bytes);
		psp->sys.start_addr        = ucode_start_addr;
		break;
	case PSP_FW_TYPE_PSP_KDB:
		psp->kdb.fw_version        = le32_to_cpu(desc->fw_version);
		psp->kdb.feature_version   = le32_to_cpu(desc->fw_version);
		psp->kdb.size_bytes        = le32_to_cpu(desc->size_bytes);
		psp->kdb.start_addr        = ucode_start_addr;
		break;
	case PSP_FW_TYPE_PSP_TOC:
		psp->toc.fw_version        = le32_to_cpu(desc->fw_version);
		psp->toc.feature_version   = le32_to_cpu(desc->fw_version);
		psp->toc.size_bytes        = le32_to_cpu(desc->size_bytes);
		psp->toc.start_addr        = ucode_start_addr;
		break;
	case PSP_FW_TYPE_PSP_SPL:
		psp->spl.fw_version        = le32_to_cpu(desc->fw_version);
		psp->spl.feature_version   = le32_to_cpu(desc->fw_version);
		psp->spl.size_bytes        = le32_to_cpu(desc->size_bytes);
		psp->spl.start_addr        = ucode_start_addr;
		break;
	case PSP_FW_TYPE_PSP_RL:
		psp->rl.fw_version         = le32_to_cpu(desc->fw_version);
		psp->rl.feature_version    = le32_to_cpu(desc->fw_version);
		psp->rl.size_bytes         = le32_to_cpu(desc->size_bytes);
		psp->rl.start_addr         = ucode_start_addr;
		break;
	case PSP_FW_TYPE_PSP_SOC_DRV:
		psp->soc_drv.fw_version         = le32_to_cpu(desc->fw_version);
		psp->soc_drv.feature_version    = le32_to_cpu(desc->fw_version);
		psp->soc_drv.size_bytes         = le32_to_cpu(desc->size_bytes);
		psp->soc_drv.start_addr         = ucode_start_addr;
		break;
	case PSP_FW_TYPE_PSP_INTF_DRV:
		psp->intf_drv.fw_version        = le32_to_cpu(desc->fw_version);
		psp->intf_drv.feature_version   = le32_to_cpu(desc->fw_version);
		psp->intf_drv.size_bytes        = le32_to_cpu(desc->size_bytes);
		psp->intf_drv.start_addr        = ucode_start_addr;
		break;
	case PSP_FW_TYPE_PSP_DBG_DRV:
		psp->dbg_drv.fw_version         = le32_to_cpu(desc->fw_version);
		psp->dbg_drv.feature_version    = le32_to_cpu(desc->fw_version);
		psp->dbg_drv.size_bytes         = le32_to_cpu(desc->size_bytes);
		psp->dbg_drv.start_addr         = ucode_start_addr;
		break;
	default:
		dev_warn(psp->adev->dev, "Unsupported PSP FW type: %d\n", desc->fw_type);
		break;
	}

	return 0;
}

static int psp_init_sos_base_fw(struct amdgpu_device *adev)
{
	const struct psp_firmware_header_v1_0 *sos_hdr;
	const struct psp_firmware_header_v1_3 *sos_hdr_v1_3;
	uint8_t *ucode_array_start_addr;

	sos_hdr = (const struct psp_firmware_header_v1_0 *)adev->psp.sos_fw->data;
	ucode_array_start_addr = (uint8_t *)sos_hdr +
		le32_to_cpu(sos_hdr->header.ucode_array_offset_bytes);

	if (adev->gmc.xgmi.connected_to_cpu ||
	    (adev->ip_versions[MP0_HWIP][0] != IP_VERSION(13, 0, 2))) {
		adev->psp.sos.fw_version = le32_to_cpu(sos_hdr->header.ucode_version);
		adev->psp.sos.feature_version = le32_to_cpu(sos_hdr->sos.fw_version);

		adev->psp.sys.size_bytes = le32_to_cpu(sos_hdr->sos.offset_bytes);
		adev->psp.sys.start_addr = ucode_array_start_addr;

		adev->psp.sos.size_bytes = le32_to_cpu(sos_hdr->sos.size_bytes);
		adev->psp.sos.start_addr = ucode_array_start_addr +
				le32_to_cpu(sos_hdr->sos.offset_bytes);
	} else {
		/* Load alternate PSP SOS FW */
		sos_hdr_v1_3 = (const struct psp_firmware_header_v1_3 *)adev->psp.sos_fw->data;

		adev->psp.sos.fw_version = le32_to_cpu(sos_hdr_v1_3->sos_aux.fw_version);
		adev->psp.sos.feature_version = le32_to_cpu(sos_hdr_v1_3->sos_aux.fw_version);

		adev->psp.sys.size_bytes = le32_to_cpu(sos_hdr_v1_3->sys_drv_aux.size_bytes);
		adev->psp.sys.start_addr = ucode_array_start_addr +
			le32_to_cpu(sos_hdr_v1_3->sys_drv_aux.offset_bytes);

		adev->psp.sos.size_bytes = le32_to_cpu(sos_hdr_v1_3->sos_aux.size_bytes);
		adev->psp.sos.start_addr = ucode_array_start_addr +
			le32_to_cpu(sos_hdr_v1_3->sos_aux.offset_bytes);
	}

	if ((adev->psp.sys.size_bytes == 0) || (adev->psp.sos.size_bytes == 0)) {
		dev_warn(adev->dev, "PSP SOS FW not available");
		return -EINVAL;
	}

	return 0;
}

int psp_init_sos_microcode(struct psp_context *psp,
			   const char *chip_name)
{
	struct amdgpu_device *adev = psp->adev;
	char fw_name[PSP_FW_NAME_LEN];
	const struct psp_firmware_header_v1_0 *sos_hdr;
	const struct psp_firmware_header_v1_1 *sos_hdr_v1_1;
	const struct psp_firmware_header_v1_2 *sos_hdr_v1_2;
	const struct psp_firmware_header_v1_3 *sos_hdr_v1_3;
	const struct psp_firmware_header_v2_0 *sos_hdr_v2_0;
	int err = 0;
	uint8_t *ucode_array_start_addr;
	int fw_index = 0;

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
	ucode_array_start_addr = (uint8_t *)sos_hdr +
		le32_to_cpu(sos_hdr->header.ucode_array_offset_bytes);
	amdgpu_ucode_print_psp_hdr(&sos_hdr->header);

	switch (sos_hdr->header.header_version_major) {
	case 1:
		err = psp_init_sos_base_fw(adev);
		if (err)
			goto out;

		if (sos_hdr->header.header_version_minor == 1) {
			sos_hdr_v1_1 = (const struct psp_firmware_header_v1_1 *)adev->psp.sos_fw->data;
			adev->psp.toc.size_bytes = le32_to_cpu(sos_hdr_v1_1->toc.size_bytes);
			adev->psp.toc.start_addr = (uint8_t *)adev->psp.sys.start_addr +
					le32_to_cpu(sos_hdr_v1_1->toc.offset_bytes);
			adev->psp.kdb.size_bytes = le32_to_cpu(sos_hdr_v1_1->kdb.size_bytes);
			adev->psp.kdb.start_addr = (uint8_t *)adev->psp.sys.start_addr +
					le32_to_cpu(sos_hdr_v1_1->kdb.offset_bytes);
		}
		if (sos_hdr->header.header_version_minor == 2) {
			sos_hdr_v1_2 = (const struct psp_firmware_header_v1_2 *)adev->psp.sos_fw->data;
			adev->psp.kdb.size_bytes = le32_to_cpu(sos_hdr_v1_2->kdb.size_bytes);
			adev->psp.kdb.start_addr = (uint8_t *)adev->psp.sys.start_addr +
						    le32_to_cpu(sos_hdr_v1_2->kdb.offset_bytes);
		}
		if (sos_hdr->header.header_version_minor == 3) {
			sos_hdr_v1_3 = (const struct psp_firmware_header_v1_3 *)adev->psp.sos_fw->data;
			adev->psp.toc.size_bytes = le32_to_cpu(sos_hdr_v1_3->v1_1.toc.size_bytes);
			adev->psp.toc.start_addr = ucode_array_start_addr +
				le32_to_cpu(sos_hdr_v1_3->v1_1.toc.offset_bytes);
			adev->psp.kdb.size_bytes = le32_to_cpu(sos_hdr_v1_3->v1_1.kdb.size_bytes);
			adev->psp.kdb.start_addr = ucode_array_start_addr +
				le32_to_cpu(sos_hdr_v1_3->v1_1.kdb.offset_bytes);
			adev->psp.spl.size_bytes = le32_to_cpu(sos_hdr_v1_3->spl.size_bytes);
			adev->psp.spl.start_addr = ucode_array_start_addr +
				le32_to_cpu(sos_hdr_v1_3->spl.offset_bytes);
			adev->psp.rl.size_bytes = le32_to_cpu(sos_hdr_v1_3->rl.size_bytes);
			adev->psp.rl.start_addr = ucode_array_start_addr +
				le32_to_cpu(sos_hdr_v1_3->rl.offset_bytes);
		}
		break;
	case 2:
		sos_hdr_v2_0 = (const struct psp_firmware_header_v2_0 *)adev->psp.sos_fw->data;

		if (le32_to_cpu(sos_hdr_v2_0->psp_fw_bin_count) >= UCODE_MAX_PSP_PACKAGING) {
			dev_err(adev->dev, "packed SOS count exceeds maximum limit\n");
			err = -EINVAL;
			goto out;
		}

		for (fw_index = 0; fw_index < le32_to_cpu(sos_hdr_v2_0->psp_fw_bin_count); fw_index++) {
			err = parse_sos_bin_descriptor(psp,
						       &sos_hdr_v2_0->psp_fw_bin[fw_index],
						       sos_hdr_v2_0);
			if (err)
				goto out;
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

static int parse_ta_bin_descriptor(struct psp_context *psp,
				   const struct psp_fw_bin_desc *desc,
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
		psp->asd_context.bin_desc.fw_version        = le32_to_cpu(desc->fw_version);
		psp->asd_context.bin_desc.feature_version   = le32_to_cpu(desc->fw_version);
		psp->asd_context.bin_desc.size_bytes        = le32_to_cpu(desc->size_bytes);
		psp->asd_context.bin_desc.start_addr        = ucode_start_addr;
		break;
	case TA_FW_TYPE_PSP_XGMI:
		psp->xgmi_context.context.bin_desc.fw_version       = le32_to_cpu(desc->fw_version);
		psp->xgmi_context.context.bin_desc.size_bytes       = le32_to_cpu(desc->size_bytes);
		psp->xgmi_context.context.bin_desc.start_addr       = ucode_start_addr;
		break;
	case TA_FW_TYPE_PSP_RAS:
		psp->ras_context.context.bin_desc.fw_version        = le32_to_cpu(desc->fw_version);
		psp->ras_context.context.bin_desc.size_bytes        = le32_to_cpu(desc->size_bytes);
		psp->ras_context.context.bin_desc.start_addr        = ucode_start_addr;
		break;
	case TA_FW_TYPE_PSP_HDCP:
		psp->hdcp_context.context.bin_desc.fw_version       = le32_to_cpu(desc->fw_version);
		psp->hdcp_context.context.bin_desc.size_bytes       = le32_to_cpu(desc->size_bytes);
		psp->hdcp_context.context.bin_desc.start_addr       = ucode_start_addr;
		break;
	case TA_FW_TYPE_PSP_DTM:
		psp->dtm_context.context.bin_desc.fw_version       = le32_to_cpu(desc->fw_version);
		psp->dtm_context.context.bin_desc.size_bytes       = le32_to_cpu(desc->size_bytes);
		psp->dtm_context.context.bin_desc.start_addr       = ucode_start_addr;
		break;
	case TA_FW_TYPE_PSP_RAP:
		psp->rap_context.context.bin_desc.fw_version       = le32_to_cpu(desc->fw_version);
		psp->rap_context.context.bin_desc.size_bytes       = le32_to_cpu(desc->size_bytes);
		psp->rap_context.context.bin_desc.start_addr       = ucode_start_addr;
		break;
	case TA_FW_TYPE_PSP_SECUREDISPLAY:
		psp->securedisplay_context.context.bin_desc.fw_version =
			le32_to_cpu(desc->fw_version);
		psp->securedisplay_context.context.bin_desc.size_bytes =
			le32_to_cpu(desc->size_bytes);
		psp->securedisplay_context.context.bin_desc.start_addr =
			ucode_start_addr;
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
	char fw_name[PSP_FW_NAME_LEN];
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

	if (le32_to_cpu(ta_hdr->ta_fw_bin_count) >= UCODE_MAX_PSP_PACKAGING) {
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

int psp_init_cap_microcode(struct psp_context *psp,
			  const char *chip_name)
{
	struct amdgpu_device *adev = psp->adev;
	char fw_name[PSP_FW_NAME_LEN];
	const struct psp_firmware_header_v1_0 *cap_hdr_v1_0;
	struct amdgpu_firmware_info *info = NULL;
	int err = 0;

	if (!chip_name) {
		dev_err(adev->dev, "invalid chip name for cap microcode\n");
		return -EINVAL;
	}

	if (!amdgpu_sriov_vf(adev)) {
		dev_err(adev->dev, "cap microcode should only be loaded under SRIOV\n");
		return -EINVAL;
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_cap.bin", chip_name);
	err = request_firmware(&adev->psp.cap_fw, fw_name, adev->dev);
	if (err) {
		dev_warn(adev->dev, "cap microcode does not exist, skip\n");
		err = 0;
		goto out;
	}

	err = amdgpu_ucode_validate(adev->psp.cap_fw);
	if (err) {
		dev_err(adev->dev, "fail to initialize cap microcode\n");
		goto out;
	}

	info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CAP];
	info->ucode_id = AMDGPU_UCODE_ID_CAP;
	info->fw = adev->psp.cap_fw;
	cap_hdr_v1_0 = (const struct psp_firmware_header_v1_0 *)
		adev->psp.cap_fw->data;
	adev->firmware.fw_size += ALIGN(
			le32_to_cpu(cap_hdr_v1_0->header.ucode_size_bytes), PAGE_SIZE);
	adev->psp.cap_fw_version = le32_to_cpu(cap_hdr_v1_0->header.ucode_version);
	adev->psp.cap_feature_version = le32_to_cpu(cap_hdr_v1_0->sos.fw_version);
	adev->psp.cap_ucode_size = le32_to_cpu(cap_hdr_v1_0->header.ucode_size_bytes);

	return 0;

out:
	release_firmware(adev->psp.cap_fw);
	adev->psp.cap_fw = NULL;
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

	return sysfs_emit(buf, "%x\n", fw_ver);
}

static ssize_t psp_usbc_pd_fw_sysfs_write(struct device *dev,
						       struct device_attribute *attr,
						       const char *buf,
						       size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int ret, idx;
	char fw_name[100];
	const struct firmware *usbc_pd_fw;
	struct amdgpu_bo *fw_buf_bo = NULL;
	uint64_t fw_pri_mc_addr;
	void *fw_pri_cpu_addr;

	if (!adev->ip_blocks[AMD_IP_BLOCK_TYPE_PSP].status.late_initialized) {
		DRM_INFO("PSP block is not ready yet.");
		return -EBUSY;
	}

	if (!drm_dev_enter(ddev, &idx))
		return -ENODEV;

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s", buf);
	ret = request_firmware(&usbc_pd_fw, fw_name, adev->dev);
	if (ret)
		goto fail;

	/* LFB address which is aligned to 1MB boundary per PSP request */
	ret = amdgpu_bo_create_kernel(adev, usbc_pd_fw->size, 0x100000,
						AMDGPU_GEM_DOMAIN_VRAM,
						&fw_buf_bo,
						&fw_pri_mc_addr,
						&fw_pri_cpu_addr);
	if (ret)
		goto rel_buf;

	memcpy_toio(fw_pri_cpu_addr, usbc_pd_fw->data, usbc_pd_fw->size);

	mutex_lock(&adev->psp.mutex);
	ret = psp_load_usbc_pd_fw(&adev->psp, fw_pri_mc_addr);
	mutex_unlock(&adev->psp.mutex);

	amdgpu_bo_free_kernel(&fw_buf_bo, &fw_pri_mc_addr, &fw_pri_cpu_addr);

rel_buf:
	release_firmware(usbc_pd_fw);
fail:
	if (ret) {
		DRM_ERROR("Failed to load USBC PD FW, err = %d", ret);
		count = ret;
	}

	drm_dev_exit(idx);
	return count;
}

void psp_copy_fw(struct psp_context *psp, uint8_t *start_addr, uint32_t bin_size)
{
	int idx;

	if (!drm_dev_enter(adev_to_drm(psp->adev), &idx))
		return;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);
	memcpy(psp->fw_pri_buf, start_addr, bin_size);

	drm_dev_exit(idx);
}

static DEVICE_ATTR(usbc_pd_fw, S_IRUGO | S_IWUSR,
		   psp_usbc_pd_fw_sysfs_read,
		   psp_usbc_pd_fw_sysfs_write);

int is_psp_fw_valid(struct psp_bin_desc bin)
{
	return bin.size_bytes;
}

static ssize_t amdgpu_psp_vbflash_write(struct file *filp, struct kobject *kobj,
					struct bin_attribute *bin_attr,
					char *buffer, loff_t pos, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	adev->psp.vbflash_done = false;

	/* Safeguard against memory drain */
	if (adev->psp.vbflash_image_size > AMD_VBIOS_FILE_MAX_SIZE_B) {
		dev_err(adev->dev, "File size cannot exceed %u", AMD_VBIOS_FILE_MAX_SIZE_B);
		kvfree(adev->psp.vbflash_tmp_buf);
		adev->psp.vbflash_tmp_buf = NULL;
		adev->psp.vbflash_image_size = 0;
		return -ENOMEM;
	}

	/* TODO Just allocate max for now and optimize to realloc later if needed */
	if (!adev->psp.vbflash_tmp_buf) {
		adev->psp.vbflash_tmp_buf = kvmalloc(AMD_VBIOS_FILE_MAX_SIZE_B, GFP_KERNEL);
		if (!adev->psp.vbflash_tmp_buf)
			return -ENOMEM;
	}

	mutex_lock(&adev->psp.mutex);
	memcpy(adev->psp.vbflash_tmp_buf + pos, buffer, count);
	adev->psp.vbflash_image_size += count;
	mutex_unlock(&adev->psp.mutex);

	dev_info(adev->dev, "VBIOS flash write PSP done");

	return count;
}

static ssize_t amdgpu_psp_vbflash_read(struct file *filp, struct kobject *kobj,
				       struct bin_attribute *bin_attr, char *buffer,
				       loff_t pos, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct amdgpu_bo *fw_buf_bo = NULL;
	uint64_t fw_pri_mc_addr;
	void *fw_pri_cpu_addr;
	int ret;

	dev_info(adev->dev, "VBIOS flash to PSP started");

	ret = amdgpu_bo_create_kernel(adev, adev->psp.vbflash_image_size,
					AMDGPU_GPU_PAGE_SIZE,
					AMDGPU_GEM_DOMAIN_VRAM,
					&fw_buf_bo,
					&fw_pri_mc_addr,
					&fw_pri_cpu_addr);
	if (ret)
		goto rel_buf;

	memcpy_toio(fw_pri_cpu_addr, adev->psp.vbflash_tmp_buf, adev->psp.vbflash_image_size);

	mutex_lock(&adev->psp.mutex);
	ret = psp_update_spirom(&adev->psp, fw_pri_mc_addr);
	mutex_unlock(&adev->psp.mutex);

	amdgpu_bo_free_kernel(&fw_buf_bo, &fw_pri_mc_addr, &fw_pri_cpu_addr);

rel_buf:
	kvfree(adev->psp.vbflash_tmp_buf);
	adev->psp.vbflash_tmp_buf = NULL;
	adev->psp.vbflash_image_size = 0;

	if (ret) {
		dev_err(adev->dev, "Failed to load VBIOS FW, err = %d", ret);
		return ret;
	}

	dev_info(adev->dev, "VBIOS flash to PSP done");
	return 0;
}

static ssize_t amdgpu_psp_vbflash_status(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	uint32_t vbflash_status;

	vbflash_status = psp_vbflash_status(&adev->psp);
	if (!adev->psp.vbflash_done)
		vbflash_status = 0;
	else if (adev->psp.vbflash_done && !(vbflash_status & 0x80000000))
		vbflash_status = 1;

	return sysfs_emit(buf, "0x%x\n", vbflash_status);
}

static const struct bin_attribute psp_vbflash_bin_attr = {
	.attr = {.name = "psp_vbflash", .mode = 0664},
	.size = 0,
	.write = amdgpu_psp_vbflash_write,
	.read = amdgpu_psp_vbflash_read,
};

static DEVICE_ATTR(psp_vbflash_status, 0444, amdgpu_psp_vbflash_status, NULL);

int amdgpu_psp_sysfs_init(struct amdgpu_device *adev)
{
	int ret = 0;
	struct psp_context *psp = &adev->psp;

	if (amdgpu_sriov_vf(adev))
		return -EINVAL;

	switch (adev->ip_versions[MP0_HWIP][0]) {
	case IP_VERSION(13, 0, 0):
	case IP_VERSION(13, 0, 7):
		if (!psp->adev) {
			psp->adev = adev;
			psp_v13_0_set_psp_funcs(psp);
		}
		ret = sysfs_create_bin_file(&adev->dev->kobj, &psp_vbflash_bin_attr);
		if (ret)
			dev_err(adev->dev, "Failed to create device file psp_vbflash");
		ret = device_create_file(adev->dev, &dev_attr_psp_vbflash_status);
		if (ret)
			dev_err(adev->dev, "Failed to create device file psp_vbflash_status");
		return ret;
	default:
		return 0;
	}
}

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

void amdgpu_psp_sysfs_fini(struct amdgpu_device *adev)
{
	sysfs_remove_bin_file(&adev->dev->kobj, &psp_vbflash_bin_attr);
	device_remove_file(adev->dev, &dev_attr_psp_vbflash_status);
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

const struct amdgpu_ip_block_version psp_v11_0_8_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_PSP,
	.major = 11,
	.minor = 0,
	.rev = 8,
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

const struct amdgpu_ip_block_version psp_v13_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_PSP,
	.major = 13,
	.minor = 0,
	.rev = 0,
	.funcs = &psp_ip_funcs,
};

const struct amdgpu_ip_block_version psp_v13_0_4_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_PSP,
	.major = 13,
	.minor = 0,
	.rev = 4,
	.funcs = &psp_ip_funcs,
};
