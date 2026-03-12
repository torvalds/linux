// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#include "ras.h"
#include "ras_ta_if.h"
#include "ras_psp.h"
#include "ras_psp_v13_0.h"

/* position of instance value in sub_block_index of
 * ta_ras_trigger_error_input, the sub block uses lower 12 bits
 */
#define RAS_TA_INST_MASK 0xfffff000
#define RAS_TA_INST_SHIFT 0xc

static const struct ras_psp_ip_func *ras_psp_get_ip_funcs(
			struct ras_core_context *ras_core, uint32_t ip_version)
{
	switch (ip_version) {
	case IP_VERSION(13, 0, 6):
	case IP_VERSION(13, 0, 14):
	case IP_VERSION(13, 0, 12):
		return &ras_psp_v13_0;
	default:
		RAS_DEV_ERR(ras_core->dev,
			"psp ip version(0x%x) is not supported!\n", ip_version);
		break;
	}

	return NULL;
}

static int ras_psp_sync_system_ras_psp_status(struct ras_core_context *ras_core)
{
	struct ras_psp *psp = &ras_core->ras_psp;
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;
	struct ras_psp_ctx *psp_ctx = &ras_core->ras_psp.psp_ctx;
	struct ras_psp_sys_status status = {0};
	int ret;

	if (psp->sys_func && psp->sys_func->get_ras_psp_system_status) {
		ret = psp->sys_func->get_ras_psp_system_status(ras_core, &status);
		if (ret)
			return ret;

		if (status.initialized) {
			ta_ctx->preload_ras_ta_enabled = true;
			ta_ctx->ras_ta_initialized = status.initialized;
			ta_ctx->session_id = status.session_id;
		}

		psp_ctx->external_mutex = status.psp_cmd_mutex;
	}

	return 0;
}

static int ras_psp_get_ras_ta_init_param(struct ras_core_context *ras_core,
	struct ras_ta_init_param *ras_ta_param)
{
	struct ras_psp *psp = &ras_core->ras_psp;

	if (psp->sys_func && psp->sys_func->get_ras_ta_init_param)
		return psp->sys_func->get_ras_ta_init_param(ras_core, ras_ta_param);

	RAS_DEV_ERR(ras_core->dev, "Not config get_ras_ta_init_param API!!\n");
	return -EACCES;
}

static struct gpu_mem_block *ras_psp_get_gpu_mem(struct ras_core_context *ras_core,
			enum gpu_mem_type mem_type)
{
	struct ras_psp *psp = &ras_core->ras_psp;
	struct gpu_mem_block *gpu_mem = NULL;
	int ret;

	switch (mem_type) {
	case GPU_MEM_TYPE_RAS_PSP_RING:
		gpu_mem = &psp->psp_ring.ras_ring_gpu_mem;
		break;
	case GPU_MEM_TYPE_RAS_PSP_CMD:
		gpu_mem = &psp->psp_ctx.psp_cmd_gpu_mem;
		break;
	case GPU_MEM_TYPE_RAS_PSP_FENCE:
		gpu_mem = &psp->psp_ctx.out_fence_gpu_mem;
		break;
	case GPU_MEM_TYPE_RAS_TA_FW:
		gpu_mem = &psp->ta_ctx.fw_gpu_mem;
		break;
	case GPU_MEM_TYPE_RAS_TA_CMD:
		gpu_mem = &psp->ta_ctx.cmd_gpu_mem;
		break;
	default:
		return NULL;
	}

	if (!gpu_mem->ref_count) {
		ret = ras_core_get_gpu_mem(ras_core, mem_type, gpu_mem);
		if (ret)
			return NULL;
		gpu_mem->mem_type = mem_type;
	}

	gpu_mem->ref_count++;

	return gpu_mem;
}

static int ras_psp_put_gpu_mem(struct ras_core_context *ras_core,
			struct gpu_mem_block *gpu_mem)
{
	if (!gpu_mem)
		return 0;

	gpu_mem->ref_count--;

	if (gpu_mem->ref_count > 0) {
		return 0;
	} else if (gpu_mem->ref_count < 0) {
		RAS_DEV_WARN(ras_core->dev,
			"Duplicate free gpu memory %u\n", gpu_mem->mem_type);
	} else {
		ras_core_put_gpu_mem(ras_core, gpu_mem->mem_type, gpu_mem);
		memset(gpu_mem, 0, sizeof(*gpu_mem));
	}

	return 0;
}

static void __acquire_psp_cmd_lock(struct ras_core_context *ras_core)
{
	struct ras_psp_ctx *psp_ctx = &ras_core->ras_psp.psp_ctx;

	if (psp_ctx->external_mutex)
		mutex_lock(psp_ctx->external_mutex);
	else
		mutex_lock(&psp_ctx->internal_mutex);
}

static void __release_psp_cmd_lock(struct ras_core_context *ras_core)
{
	struct ras_psp_ctx *psp_ctx = &ras_core->ras_psp.psp_ctx;

	if (psp_ctx->external_mutex)
		mutex_unlock(psp_ctx->external_mutex);
	else
		mutex_unlock(&psp_ctx->internal_mutex);
}

static uint32_t __get_ring_frame_slot(struct ras_core_context *ras_core)
{
	struct ras_psp *psp = &ras_core->ras_psp;
	uint32_t ras_ring_wptr_dw;

	ras_ring_wptr_dw = psp->ip_func->psp_ras_ring_wptr_get(ras_core);

	return div64_u64((ras_ring_wptr_dw << 2), sizeof(struct psp_gfx_rb_frame));
}

static int __set_ring_frame_slot(struct ras_core_context *ras_core,
			uint32_t slot)
{
	struct ras_psp *psp = &ras_core->ras_psp;

	return psp->ip_func->psp_ras_ring_wptr_set(ras_core,
				(slot * sizeof(struct psp_gfx_rb_frame)) >> 2);
}

static int write_frame_to_ras_psp_ring(struct ras_core_context *ras_core,
		struct psp_gfx_rb_frame *frame)
{
	struct gpu_mem_block *ring_mem;
	struct psp_gfx_rb_frame *rb_frame;
	uint32_t max_frame_slot;
	uint32_t slot_idx;
	uint32_t write_flush_read_back = 0;
	int ret = 0;

	ring_mem = ras_psp_get_gpu_mem(ras_core, GPU_MEM_TYPE_RAS_PSP_RING);
	if (!ring_mem)
		return -ENOMEM;

	max_frame_slot =
		div64_u64(ring_mem->mem_size, sizeof(struct psp_gfx_rb_frame));

	rb_frame =
		(struct psp_gfx_rb_frame *)ring_mem->mem_cpu_addr;

	slot_idx = __get_ring_frame_slot(ras_core);
	if (slot_idx >= max_frame_slot)
		slot_idx = 0;

	memcpy(&rb_frame[slot_idx], frame, sizeof(*frame));

	/* Do a read to force the write of the frame before writing
	 * write pointer.
	 */
	write_flush_read_back = rb_frame[slot_idx].fence_value;
	if (write_flush_read_back != frame->fence_value) {
		RAS_DEV_ERR(ras_core->dev,
		"Failed to submit ring cmd! cmd:0x%x:0x%x, fence:0x%x:0x%x value:%u, expected:%u\n",
			rb_frame[slot_idx].cmd_buf_addr_hi,
			rb_frame[slot_idx].cmd_buf_addr_lo,
			rb_frame[slot_idx].fence_addr_hi,
			rb_frame[slot_idx].fence_addr_lo,
			write_flush_read_back, frame->fence_value);
		ret = -EACCES;
		goto err;
	}

	slot_idx++;

	if (slot_idx >= max_frame_slot)
		slot_idx = 0;

	__set_ring_frame_slot(ras_core, slot_idx);

err:
	ras_psp_put_gpu_mem(ras_core, ring_mem);
	return ret;
}

static int send_psp_cmd(struct ras_core_context *ras_core,
		enum psp_gfx_cmd_id gfx_cmd_id, void *cmd_data,
		uint32_t cmd_size, struct psp_cmd_resp *resp)
{
	struct ras_psp_ctx *psp_ctx = &ras_core->ras_psp.psp_ctx;
	struct gpu_mem_block *psp_cmd_buf = NULL;
	struct gpu_mem_block *psp_fence_buf = NULL;
	struct psp_gfx_cmd_resp *gfx_cmd;
	struct psp_gfx_rb_frame rb_frame;
	int ret = 0;
	int timeout = 1000;

	if (!cmd_data || (cmd_size > sizeof(union psp_gfx_commands)) || !resp) {
		RAS_DEV_ERR(ras_core->dev, "Invalid RAS PSP command, id: %u\n", gfx_cmd_id);
		return -EINVAL;
	}

	__acquire_psp_cmd_lock(ras_core);

	psp_cmd_buf = ras_psp_get_gpu_mem(ras_core, GPU_MEM_TYPE_RAS_PSP_CMD);
	if (!psp_cmd_buf) {
		ret = -ENOMEM;
		goto exit;
	}

	psp_fence_buf = ras_psp_get_gpu_mem(ras_core, GPU_MEM_TYPE_RAS_PSP_FENCE);
	if (!psp_fence_buf) {
		ret = -ENOMEM;
		goto exit;
	}

	gfx_cmd = (struct psp_gfx_cmd_resp *)psp_cmd_buf->mem_cpu_addr;
	memset(gfx_cmd, 0, sizeof(*gfx_cmd));
	gfx_cmd->cmd_id = gfx_cmd_id;
	memcpy(&gfx_cmd->cmd, cmd_data, cmd_size);

	psp_ctx->in_fence_value++;

	memset(&rb_frame, 0, sizeof(rb_frame));
	rb_frame.cmd_buf_addr_hi = upper_32_bits(psp_cmd_buf->mem_mc_addr);
	rb_frame.cmd_buf_addr_lo = lower_32_bits(psp_cmd_buf->mem_mc_addr);
	rb_frame.fence_addr_hi = upper_32_bits(psp_fence_buf->mem_mc_addr);
	rb_frame.fence_addr_lo = lower_32_bits(psp_fence_buf->mem_mc_addr);
	rb_frame.fence_value = psp_ctx->in_fence_value;

	ret = write_frame_to_ras_psp_ring(ras_core, &rb_frame);
	if (ret) {
		psp_ctx->in_fence_value--;
		goto exit;
	}

	while (*((uint64_t *)psp_fence_buf->mem_cpu_addr) !=
		   psp_ctx->in_fence_value) {
		if (--timeout == 0)
			break;
		/*
		 * Shouldn't wait for timeout when err_event_athub occurs,
		 * because gpu reset thread triggered and lock resource should
		 * be released for psp resume sequence.
		 */
		if (ras_core_ras_interrupt_detected(ras_core))
			break;

		msleep(2);
	}

	resp->status = gfx_cmd->resp.status;
	resp->session_id = gfx_cmd->resp.session_id;

exit:
	ras_psp_put_gpu_mem(ras_core, psp_cmd_buf);
	ras_psp_put_gpu_mem(ras_core, psp_fence_buf);

	__release_psp_cmd_lock(ras_core);

	return ret;
}

static void __check_ras_ta_cmd_resp(struct ras_core_context *ras_core,
			struct ras_ta_cmd *ras_cmd)
{

	if (ras_cmd->ras_out_message.flags.err_inject_switch_disable_flag) {
		RAS_DEV_WARN(ras_core->dev, "ECC switch disabled\n");
		ras_cmd->ras_status = RAS_TA_STATUS__ERROR_RAS_NOT_AVAILABLE;
	} else if (ras_cmd->ras_out_message.flags.reg_access_failure_flag)
		RAS_DEV_WARN(ras_core->dev, "RAS internal register access blocked\n");

	switch (ras_cmd->ras_status) {
	case RAS_TA_STATUS__ERROR_UNSUPPORTED_IP:
		RAS_DEV_WARN(ras_core->dev,
			 "RAS WARNING: cmd failed due to unsupported ip\n");
		break;
	case RAS_TA_STATUS__ERROR_UNSUPPORTED_ERROR_INJ:
		RAS_DEV_WARN(ras_core->dev,
			 "RAS WARNING: cmd failed due to unsupported error injection\n");
		break;
	case RAS_TA_STATUS__SUCCESS:
		break;
	case RAS_TA_STATUS__TEE_ERROR_ACCESS_DENIED:
		if (ras_cmd->cmd_id == RAS_TA_CMD_ID__TRIGGER_ERROR)
			RAS_DEV_WARN(ras_core->dev,
				 "RAS WARNING: Inject error to critical region is not allowed\n");
		break;
	default:
		RAS_DEV_WARN(ras_core->dev,
			 "RAS WARNING: ras status = 0x%X\n", ras_cmd->ras_status);
		break;
	}
}

static int send_ras_ta_runtime_cmd(struct ras_core_context *ras_core,
			enum ras_ta_cmd_id cmd_id, void *in, uint32_t in_size,
			void *out, uint32_t out_size)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;
	struct gpu_mem_block *cmd_mem;
	struct ras_ta_cmd *ras_cmd;
	struct psp_gfx_cmd_invoke_cmd invoke_cmd = {0};
	struct psp_cmd_resp resp = {0};
	int ret = 0;

	if (!in || (in_size > sizeof(union ras_ta_cmd_input)) ||
		(cmd_id >= MAX_RAS_TA_CMD_ID)) {
		RAS_DEV_ERR(ras_core->dev, "Invalid RAS TA command, id: %u\n", cmd_id);
		return -EINVAL;
	}

	ras_psp_sync_system_ras_psp_status(ras_core);

	cmd_mem = ras_psp_get_gpu_mem(ras_core, GPU_MEM_TYPE_RAS_TA_CMD);
	if (!cmd_mem)
		return -ENOMEM;

	if (!ras_core_down_trylock_gpu_reset_lock(ras_core)) {
		ret = -EACCES;
		goto out;
	}

	ras_cmd = (struct ras_ta_cmd *)cmd_mem->mem_cpu_addr;

	mutex_lock(&ta_ctx->ta_mutex);

	memset(ras_cmd, 0, sizeof(*ras_cmd));
	ras_cmd->cmd_id = cmd_id;
	memcpy(&ras_cmd->ras_in_message, in, in_size);

	invoke_cmd.ta_cmd_id = cmd_id;
	invoke_cmd.session_id = ta_ctx->session_id;

	ret = send_psp_cmd(ras_core, GFX_CMD_ID_INVOKE_CMD,
			&invoke_cmd, sizeof(invoke_cmd), &resp);

	/* If err_event_athub occurs error inject was successful, however
	 *  return status from TA is no long reliable
	 */
	if (ras_core_ras_interrupt_detected(ras_core)) {
		ret = 0;
		goto unlock;
	}

	if (ret || resp.status) {
		RAS_DEV_ERR(ras_core->dev,
			"RAS: Failed to send psp cmd! ret:%d, status:%u\n",
			ret, resp.status);
		ret = -ESTRPIPE;
		goto unlock;
	}

	if (ras_cmd->if_version > RAS_TA_HOST_IF_VER) {
		RAS_DEV_WARN(ras_core->dev, "RAS: Unsupported Interface\n");
		ret = -EINVAL;
		goto unlock;
	}

	if (!ras_cmd->ras_status && out && out_size)
		memcpy(out, &ras_cmd->ras_out_message, out_size);

	__check_ras_ta_cmd_resp(ras_core, ras_cmd);

unlock:
	mutex_unlock(&ta_ctx->ta_mutex);
	ras_core_up_gpu_reset_lock(ras_core);
out:
	ras_psp_put_gpu_mem(ras_core, cmd_mem);
	return ret;
}

static int trigger_ras_ta_error(struct ras_core_context *ras_core,
	struct ras_ta_trigger_error_input *info, uint32_t instance_mask)
{
	uint32_t dev_mask = 0;

	switch (info->block_id) {
	case RAS_TA_BLOCK__GFX:
		if (ras_gfx_get_ta_subblock(ras_core, info->inject_error_type,
				info->sub_block_index, &info->sub_block_index))
			return -EINVAL;

		dev_mask = RAS_GET_MASK(ras_core->dev, GC, instance_mask);
		break;
	case RAS_TA_BLOCK__SDMA:
		dev_mask = RAS_GET_MASK(ras_core->dev, SDMA0, instance_mask);
		break;
	case RAS_TA_BLOCK__VCN:
	case RAS_TA_BLOCK__JPEG:
		dev_mask = RAS_GET_MASK(ras_core->dev, VCN, instance_mask);
		break;
	default:
		dev_mask = instance_mask;
		break;
	}

	/* reuse sub_block_index for backward compatibility */
	dev_mask <<= RAS_TA_INST_SHIFT;
	dev_mask &= RAS_TA_INST_MASK;
	info->sub_block_index |= dev_mask;

	return send_ras_ta_runtime_cmd(ras_core, RAS_TA_CMD_ID__TRIGGER_ERROR,
				info, sizeof(*info), NULL, 0);
}

static int send_load_ta_fw_cmd(struct ras_core_context *ras_core,
				struct ras_ta_ctx *ta_ctx)
{
	struct ras_ta_fw_bin  *fw_bin = &ta_ctx->fw_bin;
	struct gpu_mem_block *fw_mem;
	struct gpu_mem_block *cmd_mem;
	struct ras_ta_cmd *ta_cmd;
	struct ras_ta_init_flags *ta_init_flags;
	struct psp_gfx_cmd_load_ta  psp_load_ta_cmd;
	struct psp_cmd_resp resp = {0};
	struct ras_ta_image_header *fw_hdr = NULL;
	int ret;

	fw_mem = ras_psp_get_gpu_mem(ras_core, GPU_MEM_TYPE_RAS_TA_FW);
	if (!fw_mem)
		return -ENOMEM;

	cmd_mem = ras_psp_get_gpu_mem(ras_core, GPU_MEM_TYPE_RAS_TA_CMD);
	if (!cmd_mem) {
		ret = -ENOMEM;
		goto err;
	}

	ret = ras_psp_get_ras_ta_init_param(ras_core, &ta_ctx->init_param);
	if (ret)
		goto err;

	if (!ras_core_down_trylock_gpu_reset_lock(ras_core)) {
		ret = -EACCES;
		goto err;
	}

	/* copy ras ta binary to shared gpu memory */
	memcpy(fw_mem->mem_cpu_addr, fw_bin->bin_addr, fw_bin->bin_size);
	fw_mem->mem_size = fw_bin->bin_size;

	/* Initialize ras ta startup parameter */
	ta_cmd = (struct ras_ta_cmd *)cmd_mem->mem_cpu_addr;
	ta_init_flags = &ta_cmd->ras_in_message.init_flags;

	ta_init_flags->poison_mode_en = ta_ctx->init_param.poison_mode_en;
	ta_init_flags->dgpu_mode = ta_ctx->init_param.dgpu_mode;
	ta_init_flags->xcc_mask = ta_ctx->init_param.xcc_mask;
	ta_init_flags->channel_dis_num = ta_ctx->init_param.channel_dis_num;
	ta_init_flags->nps_mode = ta_ctx->init_param.nps_mode;
	ta_init_flags->active_umc_mask = ta_ctx->init_param.active_umc_mask;
	ta_init_flags->vram_type = ta_ctx->init_param.vram_type;

	/* Setup load ras ta command */
	memset(&psp_load_ta_cmd, 0, sizeof(psp_load_ta_cmd));
	psp_load_ta_cmd.app_phy_addr_lo	= lower_32_bits(fw_mem->mem_mc_addr);
	psp_load_ta_cmd.app_phy_addr_hi	= upper_32_bits(fw_mem->mem_mc_addr);
	psp_load_ta_cmd.app_len		= fw_mem->mem_size;
	psp_load_ta_cmd.cmd_buf_phy_addr_lo = lower_32_bits(cmd_mem->mem_mc_addr);
	psp_load_ta_cmd.cmd_buf_phy_addr_hi = upper_32_bits(cmd_mem->mem_mc_addr);
	psp_load_ta_cmd.cmd_buf_len = cmd_mem->mem_size;

	ret = send_psp_cmd(ras_core, GFX_CMD_ID_LOAD_TA,
			&psp_load_ta_cmd, sizeof(psp_load_ta_cmd), &resp);
	if (!ret && !resp.status) {
		/* Read TA version at FW offset 0x60 if TA version not found*/
		fw_hdr = (struct ras_ta_image_header *)fw_bin->bin_addr;
		RAS_DEV_INFO(ras_core->dev, "PSP: RAS TA(version:%X.%X.%X.%X) is loaded.\n",
			(fw_hdr->image_version >> 24) & 0xFF, (fw_hdr->image_version >> 16) & 0xFF,
			(fw_hdr->image_version >> 8) & 0xFF, fw_hdr->image_version & 0xFF);
		ta_ctx->ta_version = fw_hdr->image_version;
		ta_ctx->session_id = resp.session_id;
		ta_ctx->ras_ta_initialized = true;
	} else {
		RAS_DEV_ERR(ras_core->dev,
			"Failed to load RAS TA! ret:%d, status:%d\n", ret, resp.status);
	}

	ras_core_up_gpu_reset_lock(ras_core);

err:
	ras_psp_put_gpu_mem(ras_core, fw_mem);
	ras_psp_put_gpu_mem(ras_core, cmd_mem);
	return ret;
}

static int load_ras_ta_firmware(struct ras_core_context *ras_core,
		struct ras_psp_ta_load *ras_ta_load)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;
	struct ras_ta_fw_bin  *fw_bin = &ta_ctx->fw_bin;
	int ret;

	fw_bin->bin_addr = ras_ta_load->bin_addr;
	fw_bin->bin_size = ras_ta_load->bin_size;
	fw_bin->fw_version = ras_ta_load->fw_version;
	fw_bin->feature_version = ras_ta_load->feature_version;

	ret = send_load_ta_fw_cmd(ras_core, ta_ctx);
	if (!ret) {
		ras_ta_load->out_session_id = ta_ctx->session_id;
		ras_ta_load->out_loaded_ta_version = ta_ctx->ta_version;
	}

	return ret;
}

static int unload_ras_ta_firmware(struct ras_core_context *ras_core,
		struct ras_psp_ta_unload *ras_ta_unload)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;
	struct psp_gfx_cmd_unload_ta  cmd_unload_ta = {0};
	struct psp_cmd_resp resp = {0};
	int ret;

	if (!ras_core_down_trylock_gpu_reset_lock(ras_core))
		return -EACCES;

	cmd_unload_ta.session_id = ta_ctx->session_id;
	ret = send_psp_cmd(ras_core, GFX_CMD_ID_UNLOAD_TA,
		&cmd_unload_ta, sizeof(cmd_unload_ta), &resp);
	if (ret || resp.status) {
		RAS_DEV_ERR(ras_core->dev,
			"Failed to unload RAS TA! ret:%d, status:%u\n",
			ret, resp.status);
		goto unlock;
	}

	kfree(ta_ctx->fw_bin.bin_addr);
	memset(&ta_ctx->fw_bin, 0, sizeof(ta_ctx->fw_bin));
	ta_ctx->ta_version = 0;
	ta_ctx->ras_ta_initialized = false;
	ta_ctx->session_id = 0;

unlock:
	ras_core_up_gpu_reset_lock(ras_core);

	return ret;
}

int ras_psp_load_firmware(struct ras_core_context *ras_core,
	struct ras_psp_ta_load *ras_ta_load)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;
	struct ras_psp_ta_unload ras_ta_unload = {0};
	int ret;

	if (ta_ctx->preload_ras_ta_enabled)
		return 0;

	if (!ras_ta_load)
		return -EINVAL;

	if (ta_ctx->ras_ta_initialized) {
		ras_ta_unload.ras_session_id = ta_ctx->session_id;
		ret = unload_ras_ta_firmware(ras_core, &ras_ta_unload);
		if (ret)
			return ret;
	}

	return load_ras_ta_firmware(ras_core, ras_ta_load);
}

int ras_psp_unload_firmware(struct ras_core_context *ras_core,
	struct ras_psp_ta_unload *ras_ta_unload)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;

	if (ta_ctx->preload_ras_ta_enabled)
		return 0;

	if ((!ras_ta_unload) ||
	    (ras_ta_unload->ras_session_id != ta_ctx->session_id))
		return -EINVAL;

	return unload_ras_ta_firmware(ras_core, ras_ta_unload);
}

int ras_psp_trigger_error(struct ras_core_context *ras_core,
	struct ras_ta_trigger_error_input *info, uint32_t instance_mask)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;

	if (!ta_ctx->preload_ras_ta_enabled && !ta_ctx->ras_ta_initialized) {
		RAS_DEV_ERR(ras_core->dev, "RAS: ras firmware not initialized!");
		return -ENOEXEC;
	}

	if (!info)
		return -EINVAL;

	return trigger_ras_ta_error(ras_core, info, instance_mask);
}

int ras_psp_query_address(struct ras_core_context *ras_core,
		struct ras_ta_query_address_input *addr_in,
		struct ras_ta_query_address_output *addr_out)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;

	if (!ta_ctx->preload_ras_ta_enabled &&
	    !ta_ctx->ras_ta_initialized) {
		RAS_DEV_ERR(ras_core->dev, "RAS: ras firmware not initialized!");
		return -ENOEXEC;
	}

	if (!addr_in || !addr_out)
		return -EINVAL;

	return send_ras_ta_runtime_cmd(ras_core, RAS_TA_CMD_ID__QUERY_ADDRESS,
		addr_in, sizeof(*addr_in), addr_out, sizeof(*addr_out));
}

int ras_psp_sw_init(struct ras_core_context *ras_core)
{
	struct ras_psp *psp = &ras_core->ras_psp;

	memset(psp, 0, sizeof(*psp));

	psp->sys_func = ras_core->config->psp_cfg.psp_sys_fn;
	if (!psp->sys_func) {
		RAS_DEV_ERR(ras_core->dev, "RAS psp sys function not configured!\n");
		return -EINVAL;
	}

	mutex_init(&psp->psp_ctx.internal_mutex);
	mutex_init(&psp->ta_ctx.ta_mutex);

	return 0;
}

int ras_psp_sw_fini(struct ras_core_context *ras_core)
{
	struct ras_psp *psp = &ras_core->ras_psp;

	mutex_destroy(&psp->psp_ctx.internal_mutex);
	mutex_destroy(&psp->ta_ctx.ta_mutex);

	memset(psp, 0, sizeof(*psp));

	return 0;
}

int ras_psp_hw_init(struct ras_core_context *ras_core)
{
	struct ras_psp *psp = &ras_core->ras_psp;

	psp->psp_ip_version = ras_core->config->psp_ip_version;

	psp->ip_func = ras_psp_get_ip_funcs(ras_core, psp->psp_ip_version);
	if (!psp->ip_func)
		return -EINVAL;

	/* After GPU reset, the system RAS PSP status may change.
	 * therefore, it is necessary to synchronize the system status again.
	 */
	ras_psp_sync_system_ras_psp_status(ras_core);

	return 0;
}

int ras_psp_hw_fini(struct ras_core_context *ras_core)
{
	return 0;
}

bool ras_psp_check_supported_cmd(struct ras_core_context *ras_core,
		enum ras_ta_cmd_id cmd_id)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;
	bool ret = false;

	if (!ta_ctx->preload_ras_ta_enabled && !ta_ctx->ras_ta_initialized)
		return false;

	switch (cmd_id) {
	case RAS_TA_CMD_ID__QUERY_ADDRESS:
		/* Currently, querying the address from RAS TA is only supported
		 * when the RAS TA firmware is loaded during driver installation.
		 */
		if (ta_ctx->preload_ras_ta_enabled)
			ret = true;
		break;
	case RAS_TA_CMD_ID__TRIGGER_ERROR:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}
