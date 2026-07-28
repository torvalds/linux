// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#include "dm_services_types.h"
#include "dc.h"
#include "dc/inc/core_types.h"
#include "dc/dc_dmub_srv.h"
#include "dmub/dmub_srv.h"
#include "dc/inc/hw/dmcu.h"
#include "dc/inc/hw/abm.h"
#include "dal_asic_id.h"

#include "amdgpu.h"
#include "amdgpu_display.h"
#include "amdgpu_ucode.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_dmub.h"
#include "amdgpu_dm_kunit_helpers.h"
#include <linux/component.h>
#include <linux/firmware.h>

static_assert(AMDGPU_DMUB_NOTIFICATION_MAX == DMUB_NOTIFICATION_MAX, "AMDGPU_DMUB_NOTIFICATION_MAX mismatch");

MODULE_FIRMWARE(FIRMWARE_RENOIR_DMUB);
MODULE_FIRMWARE(FIRMWARE_SIENNA_CICHLID_DMUB);
MODULE_FIRMWARE(FIRMWARE_NAVY_FLOUNDER_DMUB);
MODULE_FIRMWARE(FIRMWARE_GREEN_SARDINE_DMUB);
MODULE_FIRMWARE(FIRMWARE_VANGOGH_DMUB);
MODULE_FIRMWARE(FIRMWARE_DIMGREY_CAVEFISH_DMUB);
MODULE_FIRMWARE(FIRMWARE_BEIGE_GOBY_DMUB);
MODULE_FIRMWARE(FIRMWARE_YELLOW_CARP_DMUB);
MODULE_FIRMWARE(FIRMWARE_DCN_314_DMUB);
MODULE_FIRMWARE(FIRMWARE_DCN_315_DMUB);
MODULE_FIRMWARE(FIRMWARE_DCN316_DMUB);
MODULE_FIRMWARE(FIRMWARE_DCN_V3_2_0_DMCUB);
MODULE_FIRMWARE(FIRMWARE_DCN_V3_2_1_DMCUB);
MODULE_FIRMWARE(FIRMWARE_DCN_35_DMUB);
MODULE_FIRMWARE(FIRMWARE_DCN_351_DMUB);
MODULE_FIRMWARE(FIRMWARE_DCN_36_DMUB);
MODULE_FIRMWARE(FIRMWARE_DCN_401_DMUB);
MODULE_FIRMWARE(FIRMWARE_DCN_42_DMUB);
MODULE_FIRMWARE(FIRMWARE_DCN_42B_DMUB);

/**
 * dm_dmub_aux_setconfig_callback - Callback for AUX or SET_CONFIG command.
 * @adev: amdgpu_device pointer
 * @notify: dmub notification structure
 *
 * Dmub AUX or SET_CONFIG command completion processing callback
 * Copies dmub notification to DM which is to be read by AUX command.
 * issuing thread and also signals the event to wake up the thread.
 */
void dm_dmub_aux_setconfig_callback(struct amdgpu_device *adev,
				    struct dmub_notification *notify)
{
	if (adev->dm.dmub_notify)
		memcpy(adev->dm.dmub_notify, notify, sizeof(struct dmub_notification));
	if (notify->type == DMUB_NOTIFICATION_AUX_REPLY)
		complete(&adev->dm.dmub_aux_transfer_done);
}
EXPORT_IF_KUNIT(dm_dmub_aux_setconfig_callback);

void dm_dmub_aux_fused_io_callback(struct amdgpu_device *adev,
				   struct dmub_notification *notify)
{
	if (!adev || !notify) {
		ASSERT(false);
		return;
	}

	const struct dmub_cmd_fused_request *req = &notify->fused_request;
	const uint8_t ddc_line = req->u.aux.ddc_line;

	if (ddc_line >= ARRAY_SIZE(adev->dm.fused_io)) {
		ASSERT(false);
		return;
	}

	struct fused_io_sync *sync = &adev->dm.fused_io[ddc_line];

	static_assert(sizeof(*req) <= sizeof(sync->reply_data), "Size mismatch");
	memcpy(sync->reply_data, req, sizeof(*req));
	complete(&sync->replied);
}
EXPORT_IF_KUNIT(dm_dmub_aux_fused_io_callback);

/**
 * dm_register_dmub_notify_callback - Sets callback for DMUB notify
 * @adev: amdgpu_device pointer
 * @type: Type of dmub notification
 * @callback: Dmub interrupt callback function
 * @dmub_int_thread_offload: offload indicator
 *
 * API to register a dmub callback handler for a dmub notification
 * Also sets indicator whether callback processing to be offloaded.
 * to dmub interrupt handling thread
 * Return: true if successfully registered, false if there is existing registration
 */
bool dm_register_dmub_notify_callback(struct amdgpu_device *adev,
				      enum dmub_notification_type type,
				      dmub_notify_interrupt_callback_t callback,
				      bool dmub_int_thread_offload)
{
	if (!callback || type >= ARRAY_SIZE(adev->dm.dmub_thread_offload))
		return false;

	adev->dm.dmub_callback[type] = callback;
	adev->dm.dmub_thread_offload[type] = dmub_int_thread_offload;

	return true;
}
EXPORT_IF_KUNIT(dm_register_dmub_notify_callback);

int dm_dmub_hw_init(struct amdgpu_device *adev)
{
	const struct dmcub_firmware_header_v1_0 *hdr;
	struct dmub_srv *dmub_srv = adev->dm.dmub_srv;
	struct dmub_srv_fb_info *fb_info = adev->dm.dmub_fb_info;
	const struct firmware *dmub_fw = adev->dm.dmub_fw;
	struct dc *dc = adev->dm.dc;
	struct dmcu *dmcu = adev->dm.dc->res_pool->dmcu;
	struct abm *abm = adev->dm.dc->res_pool->abm;
	struct dc_context *ctx = adev->dm.dc->ctx;
	struct dmub_srv_hw_params hw_params;
	enum dmub_status status;
	const unsigned char *fw_inst_const, *fw_bss_data;
	u32 fw_inst_const_size, fw_bss_data_size;
	bool has_hw_support;

	if (!dmub_srv)
		/* DMUB isn't supported on the ASIC. */
		return 0;

	if (!fb_info) {
		drm_err(adev_to_drm(adev), "No framebuffer info for DMUB service.\n");
		return -EINVAL;
	}

	if (!dmub_fw) {
		/* Firmware required for DMUB support. */
		drm_err(adev_to_drm(adev), "No firmware provided for DMUB.\n");
		return -EINVAL;
	}

	/* initialize register offsets for ASICs with runtime initialization available */
	if (dmub_srv->hw_funcs.init_reg_offsets)
		dmub_srv->hw_funcs.init_reg_offsets(dmub_srv, ctx);

	status = dmub_srv_has_hw_support(dmub_srv, &has_hw_support);
	if (status != DMUB_STATUS_OK) {
		drm_err(adev_to_drm(adev), "Error checking HW support for DMUB: %d\n", status);
		return -EINVAL;
	}

	if (!has_hw_support) {
		drm_info(adev_to_drm(adev), "DMUB unsupported on ASIC\n");
		return 0;
	}

	/* Reset DMCUB if it was previously running - before we overwrite its memory. */
	status = dmub_srv_hw_reset(dmub_srv);
	if (status != DMUB_STATUS_OK)
		drm_warn(adev_to_drm(adev), "Error resetting DMUB HW: %d\n", status);

	hdr = (const struct dmcub_firmware_header_v1_0 *)dmub_fw->data;

	fw_inst_const = dmub_fw->data +
			le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
			PSP_HEADER_BYTES_256;

	fw_bss_data = dmub_fw->data +
		      le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
		      le32_to_cpu(hdr->inst_const_bytes);

	/* Copy firmware and bios info into FB memory. */
	fw_inst_const_size = adev->dm.fw_inst_size;

	fw_bss_data_size = le32_to_cpu(hdr->bss_data_bytes);

	/* if adev->firmware.load_type == AMDGPU_FW_LOAD_PSP,
	 * amdgpu_ucode_init_single_fw will load dmub firmware
	 * fw_inst_const part to cw0; otherwise, the firmware back door load
	 * will be done by dm_dmub_hw_init
	 */
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		memcpy(fb_info->fb[DMUB_WINDOW_0_INST_CONST].cpu_addr, fw_inst_const,
				fw_inst_const_size);
	}

	if (fw_bss_data_size)
		memcpy(fb_info->fb[DMUB_WINDOW_2_BSS_DATA].cpu_addr,
		       fw_bss_data, fw_bss_data_size);

	/* Copy firmware bios info into FB memory. */
	memcpy(fb_info->fb[DMUB_WINDOW_3_VBIOS].cpu_addr, adev->bios,
	       adev->bios_size);

	/* Reset regions that need to be reset. */
	memset(fb_info->fb[DMUB_WINDOW_4_MAILBOX].cpu_addr, 0,
	fb_info->fb[DMUB_WINDOW_4_MAILBOX].size);

	memset(fb_info->fb[DMUB_WINDOW_5_TRACEBUFF].cpu_addr, 0,
	       fb_info->fb[DMUB_WINDOW_5_TRACEBUFF].size);

	memset(fb_info->fb[DMUB_WINDOW_6_FW_STATE].cpu_addr, 0,
	       fb_info->fb[DMUB_WINDOW_6_FW_STATE].size);

	memset(fb_info->fb[DMUB_WINDOW_SHARED_STATE].cpu_addr, 0,
	       fb_info->fb[DMUB_WINDOW_SHARED_STATE].size);

	/* Initialize hardware. */
	memset(&hw_params, 0, sizeof(hw_params));
	hw_params.soc_fb_info.fb_base = adev->gmc.fb_start;
	hw_params.soc_fb_info.fb_offset = adev->vm_manager.vram_base_offset;

	/* backdoor load firmware and trigger dmub running */
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP)
		hw_params.load_inst_const = true;

	if (dmcu)
		hw_params.psp_version = dmcu->psp_version;

	hw_params.fb_info = fb_info;

	/* Enable usb4 dpia in the FW APU */
	if (dc->caps.is_apu &&
		dc->res_pool->usb4_dpia_count != 0 &&
		!dc->debug.dpia_debug.bits.disable_dpia) {
		hw_params.dpia_supported = true;
		hw_params.disable_dpia = dc->debug.dpia_debug.bits.disable_dpia;
		hw_params.dpia_hpd_int_enable_supported = false;
		hw_params.enable_non_transparent_setconfig = dc->config.consolidated_dpia_dp_lt;
		hw_params.disable_dpia_bw_allocation = !dc->config.usb4_bw_alloc_support;
	}

	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(3, 5, 0):
	case IP_VERSION(3, 5, 1):
	case IP_VERSION(3, 6, 0):
	case IP_VERSION(4, 2, 0):
	case IP_VERSION(4, 2, 1):
		hw_params.ips_sequential_ono = adev->external_rev_id > 0x10;
		hw_params.lower_hbr3_phy_ssc = true;
		break;
	default:
		break;
	}

	status = dmub_srv_hw_init(dmub_srv, &hw_params);
	if (status != DMUB_STATUS_OK) {
		drm_err(adev_to_drm(adev), "Error initializing DMUB HW: %d\n", status);
		return -EINVAL;
	}

	/* Wait for firmware load to finish. */
	status = dmub_srv_wait_for_auto_load(dmub_srv, 100000);
	if (status != DMUB_STATUS_OK)
		drm_warn(adev_to_drm(adev), "Wait for DMUB auto-load failed: %d\n", status);

	/* Init DMCU and ABM if available. */
	if (dmcu && abm) {
		dmcu->funcs->dmcu_init(dmcu);
		abm->dmcu_is_running = dmcu->funcs->is_dmcu_initialized(dmcu);
	}

	if (!adev->dm.dc->ctx->dmub_srv)
		adev->dm.dc->ctx->dmub_srv = dc_dmub_srv_create(adev->dm.dc, dmub_srv);
	if (!adev->dm.dc->ctx->dmub_srv) {
		drm_err(adev_to_drm(adev), "Couldn't allocate DC DMUB server!\n");
		return -ENOMEM;
	}

	drm_info(adev_to_drm(adev), "DMUB hardware initialized: version=0x%08X\n",
		 adev->dm.dmcub_fw_version);

	/* Keeping sanity checks off if
	 * DCN31 >= 4.0.59.0
	 * DCN314 >= 8.0.16.0
	 * Otherwise, turn on sanity checks
	 */
	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(3, 1, 2):
	case IP_VERSION(3, 1, 3):
		if (adev->dm.dmcub_fw_version &&
		    adev->dm.dmcub_fw_version >= DMUB_FW_VERSION(4, 0, 0) &&
		    adev->dm.dmcub_fw_version < DMUB_FW_VERSION(4, 0, 59))
			adev->dm.dc->debug.sanity_checks = true;
		break;
	case IP_VERSION(3, 1, 4):
		if (adev->dm.dmcub_fw_version &&
		    adev->dm.dmcub_fw_version >= DMUB_FW_VERSION(4, 0, 0) &&
		    adev->dm.dmcub_fw_version < DMUB_FW_VERSION(8, 0, 16))
			adev->dm.dc->debug.sanity_checks = true;
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_IF_KUNIT(dm_dmub_hw_init);

void dm_dmub_hw_resume(struct amdgpu_device *adev)
{
	struct dmub_srv *dmub_srv = adev->dm.dmub_srv;
	enum dmub_status status;
	bool init;
	int r;

	if (!dmub_srv) {
		/* DMUB isn't supported on the ASIC. */
		return;
	}

	status = dmub_srv_is_hw_init(dmub_srv, &init);
	if (status != DMUB_STATUS_OK)
		drm_warn(adev_to_drm(adev), "DMUB hardware init check failed: %d\n", status);

	if (status == DMUB_STATUS_OK && init) {
		/* Wait for firmware load to finish. */
		status = dmub_srv_wait_for_auto_load(dmub_srv, 100000);
		if (status != DMUB_STATUS_OK)
			drm_warn(adev_to_drm(adev), "Wait for DMUB auto-load failed: %d\n", status);
	} else {
		/* Perform the full hardware initialization. */
		r = dm_dmub_hw_init(adev);
		if (r)
			drm_err(adev_to_drm(adev), "DMUB interface failed to initialize: status=%d\n", r);
	}
}
EXPORT_IF_KUNIT(dm_dmub_hw_resume);

static enum dmub_status
dm_dmub_send_vbios_gpint_command(struct amdgpu_device *adev,
				 enum dmub_gpint_command command_code,
				 uint16_t param,
				 uint32_t timeout_us)
{
	union dmub_gpint_data_register reg, test;
	uint32_t i;

	/* Assume that VBIOS DMUB is ready to take commands */

	reg.bits.status = 1;
	reg.bits.command_code = command_code;
	reg.bits.param = param;

	cgs_write_register(adev->dm.cgs_device, 0x34c0 + 0x01f8, reg.all);

	for (i = 0; i < timeout_us; ++i) {
		udelay(1);

		/* Check if our GPINT got acked */
		reg.bits.status = 0;
		test = (union dmub_gpint_data_register)
			cgs_read_register(adev->dm.cgs_device, 0x34c0 + 0x01f8);

		if (test.all == reg.all)
			return DMUB_STATUS_OK;
	}

	return DMUB_STATUS_TIMEOUT;
}

static void *dm_dmub_get_vbios_bounding_box(struct amdgpu_device *adev)
{
	void *bb;
	long long addr;
	unsigned int bb_size;
	int i = 0;
	uint16_t chunk;
	enum dmub_gpint_command send_addrs[] = {
		DMUB_GPINT__SET_BB_ADDR_WORD0,
		DMUB_GPINT__SET_BB_ADDR_WORD1,
		DMUB_GPINT__SET_BB_ADDR_WORD2,
		DMUB_GPINT__SET_BB_ADDR_WORD3,
	};
	enum dmub_status ret;

	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(4, 0, 1):
		bb_size = sizeof(struct dml2_soc_bb);
		break;
	case IP_VERSION(4, 2, 0):
	case IP_VERSION(4, 2, 1):
		bb_size = sizeof(struct dml2_soc_bb);
		break;
	default:
		return NULL;
	}

	bb =  dm_allocate_gpu_mem(adev,
				  DC_MEM_ALLOC_TYPE_GART,
				  bb_size,
				  &addr);
	if (!bb)
		return NULL;

	for (i = 0; i < 4; i++) {
		/* Extract 16-bit chunk */
		chunk = ((uint64_t) addr >> (i * 16)) & 0xFFFF;
		/* Send the chunk */
		ret = dm_dmub_send_vbios_gpint_command(adev, send_addrs[i], chunk, 30000);
		if (ret != DMUB_STATUS_OK)
			goto free_bb;
	}

	/* Now ask DMUB to copy the bb */
	ret = dm_dmub_send_vbios_gpint_command(adev, DMUB_GPINT__BB_COPY, 1, 200000);
	if (ret != DMUB_STATUS_OK)
		goto free_bb;

	return bb;

free_bb:
	dm_free_gpu_mem(adev, DC_MEM_ALLOC_TYPE_GART, (void *) bb);
	return NULL;

}

enum dmub_ips_disable_type dm_get_default_ips_mode(
	struct amdgpu_device *adev)
{
	enum dmub_ips_disable_type ret = DMUB_IPS_ENABLE;

	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(3, 5, 0):
	case IP_VERSION(3, 6, 0):
	case IP_VERSION(3, 5, 1):
		ret =  DMUB_IPS_RCG_IN_ACTIVE_IPS2_IN_OFF;
		break;
	default:
		/* ASICs older than DCN35 do not have IPSs */
		if (amdgpu_ip_version(adev, DCE_HWIP, 0) < IP_VERSION(3, 5, 0))
			ret = DMUB_IPS_DISABLE_ALL;
		break;
	}

	return ret;
}
EXPORT_IF_KUNIT(dm_get_default_ips_mode);

static uint32_t amdgpu_dm_dmub_reg_read(void *ctx, uint32_t address)
{
	struct amdgpu_device *adev = ctx;

	return dm_read_reg(adev->dm.dc->ctx, address);
}

static void amdgpu_dm_dmub_reg_write(void *ctx, uint32_t address,
				     uint32_t value)
{
	struct amdgpu_device *adev = ctx;

	return dm_write_reg(adev->dm.dc->ctx, address, value);
}

int dm_dmub_sw_init(struct amdgpu_device *adev)
{
	struct dmub_srv_create_params create_params;
	struct dmub_srv_fw_meta_info_params fw_meta_info_params;
	struct dmub_srv_region_params region_params;
	struct dmub_srv_region_info region_info;
	struct dmub_srv_memory_params memory_params;
	struct dmub_fw_meta_info fw_info;
	struct dmub_srv_fb_info *fb_info;
	struct dmub_srv *dmub_srv;
	const struct dmcub_firmware_header_v1_0 *hdr;
	enum dmub_asic dmub_asic;
	enum dmub_status status;
	static enum dmub_window_memory_type window_memory_type[DMUB_WINDOW_TOTAL] = {
		DMUB_WINDOW_MEMORY_TYPE_FB,		/* DMUB_WINDOW_0_INST_CONST */
		DMUB_WINDOW_MEMORY_TYPE_FB,		/* DMUB_WINDOW_1_STACK */
		DMUB_WINDOW_MEMORY_TYPE_FB,		/* DMUB_WINDOW_2_BSS_DATA */
		DMUB_WINDOW_MEMORY_TYPE_FB,		/* DMUB_WINDOW_3_VBIOS */
		DMUB_WINDOW_MEMORY_TYPE_FB,		/* DMUB_WINDOW_4_MAILBOX */
		DMUB_WINDOW_MEMORY_TYPE_FB,		/* DMUB_WINDOW_5_TRACEBUFF */
		DMUB_WINDOW_MEMORY_TYPE_FB,		/* DMUB_WINDOW_6_FW_STATE */
		DMUB_WINDOW_MEMORY_TYPE_FB,		/* DMUB_WINDOW_7_SCRATCH_MEM */
		DMUB_WINDOW_MEMORY_TYPE_FB,		/* DMUB_WINDOW_IB_MEM */
		DMUB_WINDOW_MEMORY_TYPE_FB,		/* DMUB_WINDOW_SHARED_STATE */
		DMUB_WINDOW_MEMORY_TYPE_FB,		/* DMUB_WINDOW_LSDMA_BUFFER */
		DMUB_WINDOW_MEMORY_TYPE_FB,		/* DMUB_WINDOW_CURSOR_OFFLOAD */
	};
	int r;
	int mem_domain = AMDGPU_GEM_DOMAIN_GTT;

	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(2, 1, 0):
		dmub_asic = DMUB_ASIC_DCN21;
		break;
	case IP_VERSION(3, 0, 0):
		dmub_asic = DMUB_ASIC_DCN30;
		break;
	case IP_VERSION(3, 0, 1):
		dmub_asic = DMUB_ASIC_DCN301;
		break;
	case IP_VERSION(3, 0, 2):
		dmub_asic = DMUB_ASIC_DCN302;
		break;
	case IP_VERSION(3, 0, 3):
		dmub_asic = DMUB_ASIC_DCN303;
		break;
	case IP_VERSION(3, 1, 2):
	case IP_VERSION(3, 1, 3):
		dmub_asic = (adev->external_rev_id == YELLOW_CARP_B0) ? DMUB_ASIC_DCN31B : DMUB_ASIC_DCN31;
		break;
	case IP_VERSION(3, 1, 4):
		dmub_asic = DMUB_ASIC_DCN314;
		break;
	case IP_VERSION(3, 1, 5):
		dmub_asic = DMUB_ASIC_DCN315;
		break;
	case IP_VERSION(3, 1, 6):
		dmub_asic = DMUB_ASIC_DCN316;
		break;
	case IP_VERSION(3, 2, 0):
		dmub_asic = DMUB_ASIC_DCN32;
		break;
	case IP_VERSION(3, 2, 1):
		dmub_asic = DMUB_ASIC_DCN321;
		break;
	case IP_VERSION(3, 5, 0):
	case IP_VERSION(3, 5, 1):
		dmub_asic = DMUB_ASIC_DCN35;
		break;
	case IP_VERSION(3, 6, 0):
		dmub_asic = DMUB_ASIC_DCN36;
		break;
	case IP_VERSION(4, 0, 1):
		dmub_asic = DMUB_ASIC_DCN401;
		break;
	case IP_VERSION(4, 2, 0):
		dmub_asic = DMUB_ASIC_DCN42;
		break;
	case IP_VERSION(4, 2, 1):
		dmub_asic = DMUB_ASIC_DCN42B;
		break;
	default:
		/* ASIC doesn't support DMUB. */
		return 0;
	}

	hdr = (const struct dmcub_firmware_header_v1_0 *)adev->dm.dmub_fw->data;
	adev->dm.dmcub_fw_version = le32_to_cpu(hdr->header.ucode_version);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		adev->firmware.ucode[AMDGPU_UCODE_ID_DMCUB].ucode_id =
			AMDGPU_UCODE_ID_DMCUB;
		adev->firmware.ucode[AMDGPU_UCODE_ID_DMCUB].fw =
			adev->dm.dmub_fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(hdr->inst_const_bytes), PAGE_SIZE);

		drm_info(adev_to_drm(adev), "Loading DMUB firmware via PSP: version=0x%08X\n",
			 adev->dm.dmcub_fw_version);
	}


	adev->dm.dmub_srv = kzalloc_obj(*adev->dm.dmub_srv);
	dmub_srv = adev->dm.dmub_srv;

	if (!dmub_srv) {
		drm_err(adev_to_drm(adev), "Failed to allocate DMUB service!\n");
		return -ENOMEM;
	}

	memset(&create_params, 0, sizeof(create_params));
	create_params.user_ctx = adev;
	create_params.funcs.reg_read = amdgpu_dm_dmub_reg_read;
	create_params.funcs.reg_write = amdgpu_dm_dmub_reg_write;
	create_params.asic = dmub_asic;

	/* Create the DMUB service. */
	status = dmub_srv_create(dmub_srv, &create_params);
	if (status != DMUB_STATUS_OK) {
		drm_err(adev_to_drm(adev), "Error creating DMUB service: %d\n", status);
		return -EINVAL;
	}

	/* Extract the FW meta info. */
	memset(&fw_meta_info_params, 0, sizeof(fw_meta_info_params));

	fw_meta_info_params.inst_const_size = le32_to_cpu(hdr->inst_const_bytes) -
					      PSP_HEADER_BYTES_256;
	fw_meta_info_params.bss_data_size = le32_to_cpu(hdr->bss_data_bytes);
	fw_meta_info_params.fw_inst_const = adev->dm.dmub_fw->data +
					    le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
					    PSP_HEADER_BYTES_256;
	fw_meta_info_params.fw_bss_data = fw_meta_info_params.bss_data_size ? adev->dm.dmub_fw->data +
					  le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
					  le32_to_cpu(hdr->inst_const_bytes) : NULL;
	fw_meta_info_params.custom_psp_footer_size = 0;

	status = dmub_srv_get_fw_meta_info_from_raw_fw(&fw_meta_info_params, &fw_info);
	if (status != DMUB_STATUS_OK) {
		/* Skip returning early, just log the error. */
		drm_err(adev_to_drm(adev), "Error getting DMUB FW meta info: %d\n", status);
	}

	/* Calculate the size of all the regions for the DMUB service. */
	memset(&region_params, 0, sizeof(region_params));

	region_params.inst_const_size = fw_meta_info_params.inst_const_size;
	region_params.bss_data_size = fw_meta_info_params.bss_data_size;
	region_params.vbios_size = adev->bios_size;
	region_params.fw_bss_data = fw_meta_info_params.fw_bss_data;
	region_params.fw_inst_const = fw_meta_info_params.fw_inst_const;
	region_params.window_memory_type = window_memory_type;
	region_params.fw_info = (status == DMUB_STATUS_OK) ? &fw_info : NULL;

	status = dmub_srv_calc_region_info(dmub_srv, &region_params,
					   &region_info);

	if (status != DMUB_STATUS_OK) {
		drm_err(adev_to_drm(adev), "Error calculating DMUB region info: %d\n", status);
		return -EINVAL;
	}

	/* Limit to allocate dmub to GTT on DCN32/1
	 * TODO: Other asics with GDDR7 may have worse latency
	 */
	if (dmub_asic != DMUB_ASIC_DCN32 &&
	    dmub_asic != DMUB_ASIC_DCN321)
		mem_domain |= AMDGPU_GEM_DOMAIN_VRAM;

	/*
	 * Allocate a framebuffer based on the total size of all the regions.
	 * TODO: Move this into GART.
	 */
	r = amdgpu_bo_create_kernel(adev, region_info.fb_size, PAGE_SIZE,
				    mem_domain,
				    &adev->dm.dmub_bo,
				    &adev->dm.dmub_bo_gpu_addr,
				    &adev->dm.dmub_bo_cpu_addr);
	if (r)
		return r;

	/* Rebase the regions on the framebuffer address. */
	memset(&memory_params, 0, sizeof(memory_params));
	memory_params.cpu_fb_addr = adev->dm.dmub_bo_cpu_addr;
	memory_params.gpu_fb_addr = adev->dm.dmub_bo_gpu_addr;
	memory_params.region_info = &region_info;
	memory_params.window_memory_type = window_memory_type;

	adev->dm.dmub_fb_info = kzalloc_obj(*adev->dm.dmub_fb_info);
	fb_info = adev->dm.dmub_fb_info;

	if (!fb_info) {
		drm_err(adev_to_drm(adev),
			"Failed to allocate framebuffer info for DMUB service!\n");
		return -ENOMEM;
	}

	status = dmub_srv_calc_mem_info(dmub_srv, &memory_params, fb_info);
	if (status != DMUB_STATUS_OK) {
		drm_err(adev_to_drm(adev), "Error calculating DMUB FB info: %d\n", status);
		return -EINVAL;
	}

	adev->dm.bb_from_dmub = dm_dmub_get_vbios_bounding_box(adev);
	adev->dm.fw_inst_size = fw_meta_info_params.inst_const_size;

	return 0;
}
EXPORT_IF_KUNIT(dm_dmub_sw_init);

int dm_init_microcode(struct amdgpu_device *adev)
{
	char *fw_name_dmub;
	int r;

	switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
	case IP_VERSION(2, 1, 0):
		fw_name_dmub = FIRMWARE_RENOIR_DMUB;
		if (ASICREV_IS_GREEN_SARDINE(adev->external_rev_id))
			fw_name_dmub = FIRMWARE_GREEN_SARDINE_DMUB;
		break;
	case IP_VERSION(3, 0, 0):
		if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(10, 3, 0))
			fw_name_dmub = FIRMWARE_SIENNA_CICHLID_DMUB;
		else
			fw_name_dmub = FIRMWARE_NAVY_FLOUNDER_DMUB;
		break;
	case IP_VERSION(3, 0, 1):
		fw_name_dmub = FIRMWARE_VANGOGH_DMUB;
		break;
	case IP_VERSION(3, 0, 2):
		fw_name_dmub = FIRMWARE_DIMGREY_CAVEFISH_DMUB;
		break;
	case IP_VERSION(3, 0, 3):
		fw_name_dmub = FIRMWARE_BEIGE_GOBY_DMUB;
		break;
	case IP_VERSION(3, 1, 2):
	case IP_VERSION(3, 1, 3):
		fw_name_dmub = FIRMWARE_YELLOW_CARP_DMUB;
		break;
	case IP_VERSION(3, 1, 4):
		fw_name_dmub = FIRMWARE_DCN_314_DMUB;
		break;
	case IP_VERSION(3, 1, 5):
		fw_name_dmub = FIRMWARE_DCN_315_DMUB;
		break;
	case IP_VERSION(3, 1, 6):
		fw_name_dmub = FIRMWARE_DCN316_DMUB;
		break;
	case IP_VERSION(3, 2, 0):
		fw_name_dmub = FIRMWARE_DCN_V3_2_0_DMCUB;
		break;
	case IP_VERSION(3, 2, 1):
		fw_name_dmub = FIRMWARE_DCN_V3_2_1_DMCUB;
		break;
	case IP_VERSION(3, 5, 0):
		fw_name_dmub = FIRMWARE_DCN_35_DMUB;
		break;
	case IP_VERSION(3, 5, 1):
		fw_name_dmub = FIRMWARE_DCN_351_DMUB;
		break;
	case IP_VERSION(3, 6, 0):
		fw_name_dmub = FIRMWARE_DCN_36_DMUB;
		break;
	case IP_VERSION(4, 0, 1):
		fw_name_dmub = FIRMWARE_DCN_401_DMUB;
		break;
	case IP_VERSION(4, 2, 0):
		fw_name_dmub = FIRMWARE_DCN_42_DMUB;
		break;
	case IP_VERSION(4, 2, 1):
		fw_name_dmub = FIRMWARE_DCN_42B_DMUB;
		break;
	default:
		/* ASIC doesn't support DMUB. */
		return 0;
	}
	r = amdgpu_ucode_request(adev, &adev->dm.dmub_fw, AMDGPU_UCODE_REQUIRED,
				 "%s", fw_name_dmub);
	return r;
}
EXPORT_IF_KUNIT(dm_init_microcode);

int amdgpu_dm_process_dmub_aux_transfer_sync(
		struct dc_context *ctx,
		unsigned int link_index,
		struct aux_payload *payload,
		enum aux_return_code_type *operation_result)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct dmub_notification *p_notify = adev->dm.dmub_notify;
	int ret = -1;

	mutex_lock(&adev->dm.dpia_aux_lock);
	if (!dc_process_dmub_aux_transfer_async(ctx->dc, link_index, payload)) {
		*operation_result = AUX_RET_ERROR_ENGINE_ACQUIRE;
		goto out;
	}

	if (!wait_for_completion_timeout(&adev->dm.dmub_aux_transfer_done, 10 * HZ)) {
		drm_err(adev_to_drm(adev), "wait_for_completion_timeout timeout!");
		*operation_result = AUX_RET_ERROR_TIMEOUT;
		goto out;
	}

	if (p_notify->result != AUX_RET_SUCCESS) {
		/*
		 * Transient states before tunneling is enabled could
		 * lead to this error. We can ignore this for now.
		 */
		if (p_notify->result == AUX_RET_ERROR_PROTOCOL_ERROR) {
			drm_warn(adev_to_drm(adev), "DPIA AUX failed on 0x%x(%d), error %d\n",
					payload->address, payload->length,
					p_notify->result);
		}
		*operation_result = p_notify->result;
		goto out;
	}

	payload->reply[0] = adev->dm.dmub_notify->aux_reply.command & 0xF;
	if (adev->dm.dmub_notify->aux_reply.command & 0xF0)
		/* The reply is stored in the top nibble of the command. */
		payload->reply[0] = (adev->dm.dmub_notify->aux_reply.command >> 4) & 0xF;

	/*write req may receive a byte indicating partially written number as well*/
	if (p_notify->aux_reply.length && payload->data) {
		/* Bound the reply to the scratch buffer it was read into. */
		ret = min_t(uint32_t, p_notify->aux_reply.length,
			    sizeof(p_notify->aux_reply.data));

		/*
		 * During a write-status-update retry the caller zeroes
		 * payload->length while still expecting the partial-write
		 * status byte in payload->data (see dce_aux_transfer_with_retries),
		 * so only clamp to payload->length for regular transfers.
		 */
		if (!payload->write_status_update)
			ret = min_t(int, ret, payload->length);

		memcpy(payload->data, p_notify->aux_reply.data, ret);
	} else {
		/* success */
		ret = p_notify->aux_reply.length;
	}

	*operation_result = p_notify->result;
out:
	reinit_completion(&adev->dm.dmub_aux_transfer_done);
	mutex_unlock(&adev->dm.dpia_aux_lock);
	return ret;
}

static void abort_fused_io(
		struct dc_context *ctx,
		const struct dmub_cmd_fused_request *request
)
{
	union dmub_rb_cmd command = { 0 };
	struct dmub_rb_cmd_fused_io *io = &command.fused_io;

	io->header.type = DMUB_CMD__FUSED_IO;
	io->header.sub_type = DMUB_CMD__FUSED_IO_ABORT;
	io->header.payload_bytes = sizeof(*io) - sizeof(io->header);
	io->request = *request;
	dm_execute_dmub_cmd(ctx, &command, DM_DMUB_WAIT_TYPE_NO_WAIT);
}

static bool execute_fused_io(
		struct amdgpu_device *dev,
		struct dc_context *ctx,
		union dmub_rb_cmd *commands,
		uint8_t count,
		uint32_t timeout_us
)
{
	const uint8_t ddc_line = commands[0].fused_io.request.u.aux.ddc_line;

	if (ddc_line >= ARRAY_SIZE(dev->dm.fused_io))
		return false;

	struct fused_io_sync *sync = &dev->dm.fused_io[ddc_line];
	struct dmub_rb_cmd_fused_io *first = &commands[0].fused_io;
	const bool result = dm_execute_dmub_cmd_list(ctx, count, commands, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY)
			&& first->header.ret_status
			&& first->request.status == FUSED_REQUEST_STATUS_SUCCESS;

	if (!result)
		return false;

	while (wait_for_completion_timeout(&sync->replied, usecs_to_jiffies(timeout_us))) {
		reinit_completion(&sync->replied);

		struct dmub_cmd_fused_request *reply = (struct dmub_cmd_fused_request *) sync->reply_data;

		static_assert(sizeof(*reply) <= sizeof(sync->reply_data), "Size mismatch");

		if (reply->identifier == first->request.identifier) {
			first->request = *reply;
			return true;
		}
	}

	reinit_completion(&sync->replied);
	first->request.status = FUSED_REQUEST_STATUS_TIMEOUT;
	abort_fused_io(ctx, &first->request);
	return false;
}

bool amdgpu_dm_execute_fused_io(
		struct amdgpu_device *dev,
		struct dc_link *link,
		union dmub_rb_cmd *commands,
		uint8_t count,
		uint32_t timeout_us)
{
	struct amdgpu_display_manager *dm = &dev->dm;

	mutex_lock(&dm->dpia_aux_lock);

	const bool result = execute_fused_io(dev, link->ctx, commands, count, timeout_us);

	mutex_unlock(&dm->dpia_aux_lock);
	return result;
}

int amdgpu_dm_process_dmub_set_config_sync(
		struct dc_context *ctx,
		unsigned int link_index,
		struct set_config_cmd_payload *payload,
		enum set_config_status *operation_result)
{
	struct amdgpu_device *adev = ctx->driver_context;
	bool is_cmd_complete;
	int ret;

	mutex_lock(&adev->dm.dpia_aux_lock);
	is_cmd_complete = dc_process_dmub_set_config_async(ctx->dc,
			link_index, payload, adev->dm.dmub_notify);

	if (is_cmd_complete || wait_for_completion_timeout(&adev->dm.dmub_aux_transfer_done, 10 * HZ)) {
		ret = 0;
		*operation_result = adev->dm.dmub_notify->sc_status;
	} else {
		drm_err(adev_to_drm(adev), "wait_for_completion_timeout timeout!");
		ret = -1;
		*operation_result = SET_CONFIG_UNKNOWN_ERROR;
	}

	if (!is_cmd_complete)
		reinit_completion(&adev->dm.dmub_aux_transfer_done);
	mutex_unlock(&adev->dm.dpia_aux_lock);
	return ret;
}

bool dm_execute_dmub_cmd(const struct dc_context *ctx, union dmub_rb_cmd *cmd, enum dm_dmub_wait_type wait_type)
{
	struct amdgpu_device *adev = ctx->driver_context;

	guard(spinlock_irqsave)(&adev->dm.dmub_lock);
	return dc_dmub_srv_cmd_run(ctx->dmub_srv, cmd, wait_type);
}

bool dm_execute_dmub_cmd_list(const struct dc_context *ctx, unsigned int count, union dmub_rb_cmd *cmd, enum dm_dmub_wait_type wait_type)
{
	struct amdgpu_device *adev = ctx->driver_context;

	guard(spinlock_irqsave)(&adev->dm.dmub_lock);
	return dc_dmub_srv_cmd_run_list(ctx->dmub_srv, count, cmd, wait_type);
}
