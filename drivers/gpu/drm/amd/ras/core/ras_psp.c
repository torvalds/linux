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
#include "ta_if.h"
#include "ras_psp.h"
#include "ras_psp_v13_0.h"
#include "ras_psp_v15_0.h"
#include "ras_psp_v15_0_3.h"

/* position of instance value in sub_block_index of
 * ta_ras_trigger_error_input, the sub block uses lower 12 bits
 */
#define RAS_TA_INST_MASK 0xfffff000
#define RAS_TA_INST_SHIFT 0xc

#define RAS_PSP_RING_SIZE  0x100000
#define RAS_PSP_CMD_SIZE   0x100000
#define RAS_PSP_FENCE_SIZE 0x1000
#define RAS_FW_BIN_SIZE    0x100000
#define RAS_TA_CMD_SIZE    0x100000

static const struct ras_psp_ip_func *ras_psp_get_ip_funcs(
			struct ras_core_context *ras_core, uint32_t ip_version)
{
	switch (ip_version) {
	case IP_VERSION(13, 0, 6):
	case IP_VERSION(13, 0, 14):
	case IP_VERSION(13, 0, 12):
	case IP_VERSION(13, 0, 15):
		return &ras_psp_v13_0;
	case IP_VERSION(15, 0, 3):
		return &ras_psp_v15_0_3;
	case IP_VERSION(15, 0, 8):
		return &ras_psp_v15_0;
	default:
		RAS_DEV_ERR(ras_core->dev,
			"psp ip version(0x%x) is not supported!\n", ip_version);
		break;
	}

	return NULL;
}

static int ras_psp_get_system_ras_status(struct ras_core_context *ras_core,
		struct ras_psp_sys_status *status)
{
	struct ras_psp *psp = &ras_core->ras_psp;
	int ret;

	if (psp->sys_func && psp->sys_func->get_ras_psp_system_status) {
		ret = psp->sys_func->get_ras_psp_system_status(ras_core, status);
		if (ret)
			return ret;
	}

	return 0;
}

static int ras_psp_get_ras_param(struct ras_core_context *ras_core,
	struct ras_param *param)
{
	struct ras_psp *psp = &ras_core->ras_psp;
	int ret;

	if (!psp->sys_func || !psp->sys_func->get_ras_param) {
		RAS_DEV_ERR(ras_core->dev, "Not config get_ras_ta_init_param API!!\n");
		return -EINVAL;
	}

	ret = psp->sys_func->get_ras_param(ras_core, param);
	if (ret)
		return ret;

	param->ta_param.nps_mode = ras_core_get_curr_nps_mode(ras_core);
	param->ta_param.vram_type = ras_core_get_vram_type(ras_core);
	param->ta_param.poison_mode_en = ras_core_poison_supported(ras_core) ? 1 : 0;

	return 0;
}

static struct gpu_mem_block *ras_psp_alloc_mem(struct ras_core_context *ras_core,
			enum gpu_mem_type mem_type, u32 mem_size)
{
	struct gpu_mem_block *gpu_mem;
	int ret;

	if ((mem_type >= GPU_MEM_TYPE_MAX) || (mem_size < PAGE_SIZE))
		return NULL;

	gpu_mem = kzalloc(sizeof(*gpu_mem), GFP_KERNEL);
	if (!gpu_mem)
		return NULL;

	gpu_mem->mem_size = mem_size;
	ret = ras_core_get_gpu_mem(ras_core, mem_type, gpu_mem);
	if (ret)
		goto err;

	gpu_mem->mem_type = mem_type;
	gpu_mem->ref_count++;

	return gpu_mem;

err:
	kfree(gpu_mem);
	return NULL;
}

static int ras_psp_free_mem(struct ras_core_context *ras_core,
			struct gpu_mem_block *gpu_mem)
{
	if (!gpu_mem)
		return 0;

	if (!gpu_mem->ref_count) {
		return 0;
	} else if (gpu_mem->ref_count == 1) {
		ras_core_put_gpu_mem(ras_core, gpu_mem->mem_type, gpu_mem);
		memset(gpu_mem, 0, sizeof(*gpu_mem));
		kfree(gpu_mem);
	} else if (gpu_mem->ref_count > 1) {
		gpu_mem->ref_count--;
	}

	return 0;
}

static int __ras_psp_mem_init(struct ras_core_context *ras_core)
{
	struct ras_psp *psp = &ras_core->ras_psp;
	struct gpu_mem_block *psp_ring = NULL;
	struct gpu_mem_block *psp_cmd = NULL;
	struct gpu_mem_block *psp_fence = NULL;
	struct gpu_mem_block *fw_bin = NULL;
	struct gpu_mem_block *ta_cmd = NULL;

	/* Avoid repeatedly reinitializing memory */
	if (psp->psp_ring.ras_ring_gpu_mem)
		return 0;

	psp_ring = ras_psp_alloc_mem(ras_core,
				GPU_MEM_TYPE_RAS_PSP_RING, RAS_PSP_RING_SIZE);
	if (!psp_ring)
		return -EPIPE;

	psp_cmd = ras_psp_alloc_mem(ras_core,
				GPU_MEM_TYPE_RAS_PSP_CMD, RAS_PSP_CMD_SIZE);
	if (!psp_cmd)
		goto err;

	psp_fence = ras_psp_alloc_mem(ras_core,
				GPU_MEM_TYPE_RAS_PSP_FENCE, RAS_PSP_FENCE_SIZE);
	if (!psp_fence)
		goto err;

	if (psp->use_dedicated_memory) {
		fw_bin = ras_psp_alloc_mem(ras_core,
				GPU_MEM_TYPE_RAS_FW_BIN, RAS_FW_BIN_SIZE);
		if (!fw_bin)
			goto err;

		ta_cmd = ras_psp_alloc_mem(ras_core,
				GPU_MEM_TYPE_RAS_TA_CMD, RAS_TA_CMD_SIZE);
		if (!ta_cmd)
			goto err;
	} else {
		fw_bin = ras_psp_alloc_mem(ras_core,
				GPU_MEM_TYPE_ALLOC_MEM, RAS_FW_BIN_SIZE);
		if (!fw_bin)
			goto err;

		ta_cmd = ras_psp_alloc_mem(ras_core,
				GPU_MEM_TYPE_ALLOC_MEM, RAS_TA_CMD_SIZE);
		if (!ta_cmd)
			goto err;
	}

	psp->psp_ring.ras_ring_gpu_mem = psp_ring;
	psp->psp_ctx.psp_cmd_gpu_mem = psp_cmd;
	psp->psp_ctx.out_fence_gpu_mem = psp_fence;
	psp->ta_ctx.fw_gpu_mem = fw_bin;
	psp->ta_ctx.cmd_gpu_mem = ta_cmd;

	return 0;

err:
	ras_psp_free_mem(ras_core, psp_ring);
	ras_psp_free_mem(ras_core, psp_cmd);
	ras_psp_free_mem(ras_core, psp_fence);
	ras_psp_free_mem(ras_core, fw_bin);
	ras_psp_free_mem(ras_core, ta_cmd);
	return -EPIPE;
}

static int __ras_psp_mem_fini(struct ras_core_context *ras_core)
{
	struct ras_psp *psp = &ras_core->ras_psp;

	ras_psp_free_mem(ras_core, psp->psp_ring.ras_ring_gpu_mem);
	ras_psp_free_mem(ras_core, psp->psp_ctx.psp_cmd_gpu_mem);
	ras_psp_free_mem(ras_core, psp->psp_ctx.out_fence_gpu_mem);
	ras_psp_free_mem(ras_core, psp->ta_ctx.fw_gpu_mem);
	ras_psp_free_mem(ras_core, psp->ta_ctx.cmd_gpu_mem);

	psp->psp_ctx.psp_cmd_gpu_mem = NULL;
	psp->psp_ctx.out_fence_gpu_mem = NULL;
	psp->ta_ctx.fw_gpu_mem = NULL;
	psp->ta_ctx.cmd_gpu_mem = NULL;
	psp->psp_ring.ras_ring_gpu_mem = NULL;
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
	struct ras_psp *psp = &ras_core->ras_psp;
	struct gpu_mem_block *ring_mem;
	struct psp_gfx_rb_frame *rb_frame;
	uint32_t max_frame_slot;
	uint32_t slot_idx;
	uint32_t write_flush_read_back = 0;
	int ret = 0;

	ring_mem = psp->psp_ring.ras_ring_gpu_mem;
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
		return -EACCES;
	}

	slot_idx++;

	if (slot_idx >= max_frame_slot)
		slot_idx = 0;

	__set_ring_frame_slot(ras_core, slot_idx);

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

	psp_cmd_buf = psp_ctx->psp_cmd_gpu_mem;
	psp_fence_buf = psp_ctx->out_fence_gpu_mem;
	if (!psp_cmd_buf || !psp_fence_buf) {
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

		if (ras_core_gpu_device_lost(ras_core))
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
	__release_psp_cmd_lock(ras_core);

	return ret;
}

static int __check_ras_ta_cmd_resp(struct ras_core_context *ras_core,
			struct ras_ta_cmd *ras_cmd)
{
	if (ras_cmd->ras_out_message.flags.err_inject_switch_disable_flag) {
		RAS_DEV_WARN(ras_core->dev, "ECC switch disabled\n");
		ras_cmd->ras_status = RAS_TA_STATUS__ERROR_RAS_NOT_AVAILABLE;
	} else if (ras_cmd->ras_out_message.flags.reg_access_failure_flag) {
		RAS_DEV_WARN(ras_core->dev, "RAS internal register access blocked\n");
		ras_cmd->ras_status = RAS_TA_STATUS__TEE_ERROR_ACCESS_DENIED;
	}

	switch (ras_cmd->ras_status) {
	case RAS_TA_STATUS__SUCCESS:
		return 0;
	case RAS_TA_STATUS__ERROR_UNSUPPORTED_IP:
		RAS_DEV_WARN(ras_core->dev,
			 "RAS WARNING: cmd failed due to unsupported ip\n");
		return -EINVAL;
	case RAS_TA_STATUS__ERROR_UNSUPPORTED_ERROR_INJ:
		RAS_DEV_WARN(ras_core->dev,
			 "RAS WARNING: cmd failed due to unsupported error injection\n");
		return -EINVAL;
	case RAS_TA_STATUS__TEE_ERROR_ACCESS_DENIED:
		if (ras_cmd->cmd_id == RAS_TA_CMD_ID__TRIGGER_ERROR)
			RAS_DEV_WARN(ras_core->dev,
				 "RAS WARNING: Inject error to critical region is not allowed\n");
		return -EACCES;
	default:
		RAS_DEV_WARN(ras_core->dev,
			 "RAS WARNING: ras status = 0x%X\n", ras_cmd->ras_status);
		return -EINVAL;
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
	int got_reset_lock = 0;
	int ret = 0;

	if (!in || (in_size > sizeof(union ras_ta_cmd_input)) ||
		(cmd_id >= MAX_RAS_TA_CMD_ID)) {
		RAS_DEV_ERR(ras_core->dev, "Invalid RAS TA command, id: %u\n", cmd_id);
		return -EINVAL;
	}

	if (!ta_ctx->ras_ta_initialized) {
		RAS_DEV_ERR(ras_core->dev, "RAS TA is not initialized, cmd_id: %u\n", cmd_id);
		return -EACCES;
	}

	cmd_mem = ta_ctx->cmd_gpu_mem;
	if (!cmd_mem)
		return -ENOMEM;

	if (!ras_core_gpu_in_reset(ras_core)) {
		got_reset_lock = ras_core_down_trylock_gpu_reset_lock(ras_core);
		if (!got_reset_lock)
			return  -EACCES;
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

	ret = __check_ras_ta_cmd_resp(ras_core, ras_cmd);

unlock:
	mutex_unlock(&ta_ctx->ta_mutex);
	if (got_reset_lock)
		ras_core_up_gpu_reset_lock(ras_core);
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

static int load_ras_rl_fw(struct ras_core_context *ras_core,
		struct ras_fw_bin *rl_bin)
{
	struct ras_psp *psp = &ras_core->ras_psp;
	struct gpu_mem_block *fw_mem;
	struct psp_gfx_cmd_load_ip_fw psp_load_rl_fw;
	struct psp_cmd_resp resp = {0};
	int got_reset_lock = 0;
	int ret = 0;

	fw_mem = psp->ta_ctx.fw_gpu_mem;
	if (!fw_mem)
		return -ENOMEM;

	if (!ras_core_gpu_in_reset(ras_core)) {
		got_reset_lock = ras_core_down_trylock_gpu_reset_lock(ras_core);
		if (!got_reset_lock)
			return -EACCES;
	}

	mutex_lock(&psp->ta_ctx.ta_mutex);
	/* copy ras RL to shared gpu memory */
	memcpy(fw_mem->mem_cpu_addr, rl_bin->bin_addr, rl_bin->bin_size);
	fw_mem->mem_size = rl_bin->bin_size;

	/* Setup load RL command */
	memset(&psp_load_rl_fw, 0, sizeof(psp_load_rl_fw));
	psp_load_rl_fw.fw_phy_addr_lo = lower_32_bits(fw_mem->mem_mc_addr);
	psp_load_rl_fw.fw_phy_addr_hi = upper_32_bits(fw_mem->mem_mc_addr);
	psp_load_rl_fw.fw_size = fw_mem->mem_size;
	psp_load_rl_fw.fw_type = GFX_FW_TYPE_REG_LIST;

	ret = send_psp_cmd(ras_core, GFX_CMD_ID_LOAD_IP_FW,
			&psp_load_rl_fw, sizeof(psp_load_rl_fw), &resp);
	if (ret || resp.status) {
		RAS_DEV_ERR(ras_core->dev,
			"Failed to load RAS WL. ret:%d, status:%d\n", ret, resp.status);
		ret = -EPIPE;
	}

	mutex_unlock(&psp->ta_ctx.ta_mutex);
	if (got_reset_lock)
		ras_core_up_gpu_reset_lock(ras_core);

	return ret;
}

static int load_ras_ta_fw(struct ras_core_context *ras_core,
		struct ras_fw_bin *ta_bin, struct ras_ta_param *ta_param)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;
	struct gpu_mem_block *fw_mem = NULL;
	struct gpu_mem_block *cmd_mem = NULL;
	struct ras_ta_cmd *ta_cmd;
	struct ras_ta_init_flags *ta_init_flags;
	struct psp_gfx_cmd_load_ta  psp_load_ta_cmd;
	struct psp_cmd_resp resp = {0};
	struct ras_ta_image_header *fw_hdr = NULL;
	int got_reset_lock = 0;
	int ret;

	fw_mem = ta_ctx->fw_gpu_mem;
	cmd_mem = ta_ctx->cmd_gpu_mem;
	if (!fw_mem || !cmd_mem)
		return -ENOMEM;

	if (!ras_core_gpu_in_reset(ras_core)) {
		got_reset_lock = ras_core_down_trylock_gpu_reset_lock(ras_core);
		if (!got_reset_lock)
			return -EACCES;
	}

	mutex_lock(&ta_ctx->ta_mutex);

	/* copy ras ta binary to shared gpu memory */
	memcpy(fw_mem->mem_cpu_addr, ta_bin->bin_addr, ta_bin->bin_size);
	fw_mem->mem_size = ta_bin->bin_size;

	/* Initialize ras ta startup parameter */
	ta_cmd = (struct ras_ta_cmd *)cmd_mem->mem_cpu_addr;
	ta_init_flags = &ta_cmd->ras_in_message.init_flags;

	ta_init_flags->poison_mode_en = ta_param->poison_mode_en;
	ta_init_flags->dgpu_mode = ta_param->dgpu_mode;
	ta_init_flags->xcc_mask = ta_param->xcc_mask;
	ta_init_flags->channel_dis_num = ta_param->channel_dis_num;
	ta_init_flags->nps_mode = ta_param->nps_mode;
	ta_init_flags->active_umc_mask = ta_param->active_umc_mask;
	ta_init_flags->vram_type = ta_param->vram_type;
	ta_init_flags->ext_umc_mask = ta_param->ext_umc_mask;

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
		fw_hdr = (struct ras_ta_image_header *)ta_bin->bin_addr;
		RAS_DEV_INFO(ras_core->dev, "PSP: RAS TA(version:%X.%X.%X.%X) is loaded.\n",
			(fw_hdr->image_version >> 24) & 0xFF, (fw_hdr->image_version >> 16) & 0xFF,
			(fw_hdr->image_version >> 8) & 0xFF, fw_hdr->image_version & 0xFF);
		ta_ctx->ta_version = fw_hdr->image_version;
		ta_ctx->session_id = resp.session_id;
		ta_ctx->ras_ta_initialized = true;
	} else {
		RAS_DEV_ERR(ras_core->dev,
			"Failed to load RAS TA! ret:%d, status:%d\n", ret, resp.status);
		ret = -EPIPE;
	}

	mutex_unlock(&ta_ctx->ta_mutex);
	if (got_reset_lock)
		ras_core_up_gpu_reset_lock(ras_core);

	return ret;
}

static int unload_ras_ta_fw(struct ras_core_context *ras_core,
		struct ras_psp_ta_unload *ras_ta_unload)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;
	struct psp_gfx_cmd_unload_ta  cmd_unload_ta = {0};
	struct psp_cmd_resp resp = {0};
	int got_reset_lock = 0;
	int ret;

	got_reset_lock = ras_core_down_trylock_gpu_reset_lock(ras_core);
	if (!got_reset_lock)
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

	RAS_DEV_INFO(ras_core->dev,
		     "Successfully to unload RAS TA! ret:%d, status:%u\n",
			ret, resp.status);

	ta_ctx->ta_version = 0;
	ta_ctx->ras_ta_initialized = false;
	ta_ctx->session_id = 0;

unlock:
	if (got_reset_lock)
		ras_core_up_gpu_reset_lock(ras_core);

	return ret;
}

static int load_ras_all_fw(struct ras_core_context *ras_core)
{
	struct ras_param ras_param = {0};
	int ret;

	ret = ras_psp_get_ras_param(ras_core, &ras_param);
	if (ret)
		return ret;

	ret = load_ras_rl_fw(ras_core, &ras_param.fw_param.rl_bin);
	if (ret)
		return ret;

	return load_ras_ta_fw(ras_core,
			&ras_param.fw_param.ta_bin, &ras_param.ta_param);
}

static int unload_ras_all_fw(struct ras_core_context *ras_core)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;
	struct ras_psp_ta_unload ta_unload = {0};

	if (ta_ctx->ras_ta_initialized) {
		ta_unload.ras_session_id = ta_ctx->session_id;
		unload_ras_ta_fw(ras_core, &ta_unload);
		ta_ctx->ras_ta_initialized = false;
		ta_ctx->session_id = 0;
		ta_ctx->ta_version = 0;
	}

	return 0;
}

int ras_psp_sideload_ras_ta(struct ras_core_context *ras_core,
		struct ras_psp_ta_load *ta_load)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;
	struct ras_psp_ta_unload ras_ta_unload = {0};
	struct ras_param ras_param = {0};
	struct ras_fw_bin ta_bin = {0};
	int ret = 0;

	if (!ta_load || !ta_load->bin_addr || !ta_load->bin_size)
		return -EINVAL;

	if (ta_ctx->ras_ta_initialized) {
		ras_ta_unload.ras_session_id = ta_ctx->session_id;
		ret = unload_ras_ta_fw(ras_core, &ras_ta_unload);
		if (ret)
			return ret;
	}

	ret = ras_psp_get_ras_param(ras_core, &ras_param);
	if (ret)
		return ret;

	ta_bin.bin_addr = ta_load->bin_addr;
	ta_bin.bin_size = ta_load->bin_size;
	ta_bin.fw_version = ta_load->fw_version;
	ta_bin.feature_version = ta_load->feature_version;

	ret = load_ras_ta_fw(ras_core, &ta_bin, &ras_param.ta_param);
	if (ret)
		return ret;

	ta_load->out_session_id = ta_ctx->session_id;
	ta_load->out_loaded_ta_version = ta_ctx->ta_version;

	return 0;
}

int ras_psp_unsideload_ras_ta(struct ras_core_context *ras_core,
	struct ras_psp_ta_unload *ras_ta_unload)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;

	if ((!ras_ta_unload) ||
	    (ras_ta_unload->ras_session_id != ta_ctx->session_id))
		return -EINVAL;

	return unload_ras_ta_fw(ras_core, ras_ta_unload);
}

int ras_psp_reload_firmwares(struct ras_core_context *ras_core,
		uint32_t flags)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;

	ta_ctx->ras_ta_initialized = false;
	ta_ctx->session_id = 0;
	ta_ctx->ta_version = 0;

	return load_ras_all_fw(ras_core);
}

int ras_psp_enable_features(struct ras_core_context *ras_core,
	struct ras_ta_enable_features_input *info, bool enable)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;

	if (!info)
		return -EINVAL;

	if (!ta_ctx->ras_ta_initialized) {
		RAS_DEV_ERR(ras_core->dev, "RAS: ras firmware not initialized!");
		return -ENOEXEC;
	}

	return send_ras_ta_runtime_cmd(ras_core,
			enable ? RAS_TA_CMD_ID__ENABLE_FEATURES :
				 RAS_TA_CMD_ID__DISABLE_FEATURES,
			info, sizeof(*info), NULL, 0);
}

int ras_psp_trigger_error(struct ras_core_context *ras_core,
	struct ras_ta_trigger_error_input *info, uint32_t instance_mask)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;

	if (!ta_ctx->ras_ta_initialized) {
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

	if (!ta_ctx->ras_ta_initialized) {
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
	int ret = 0;
	struct ras_psp *psp = &ras_core->ras_psp;
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;
	struct ras_psp_sys_status sys_status = {0};

	psp->psp_ip_version = ras_core->config->psp_ip_version;

	psp->ip_func = ras_psp_get_ip_funcs(ras_core, psp->psp_ip_version);
	if (!psp->ip_func)
		return -EINVAL;

	if (psp->ip_func->get_ras_block_maps &&
		psp->ip_func->get_ras_block_maps(ras_core,
			&psp->blk_maps, &psp->maps_size))
		return -EINVAL;

	if (psp->ip_func->get_ras_hw_caps &&
		psp->ip_func->get_ras_hw_caps(ras_core, &psp->ras_hw_caps))
		return -EINVAL;

	/* After GPU reset, the system RAS PSP status may change.
	 * therefore, it is necessary to synchronize the system status again.
	 */
	ret = ras_psp_get_system_ras_status(ras_core, &sys_status);
	if (ret)
		return ret;

	if (sys_status.uniras_load_fw) {
		ta_ctx->ras_ta_initialized = false;
		psp->load_ras_fw_internal = true;
	} else {
		ta_ctx->ras_ta_initialized = true;
		psp->load_ras_fw_internal = false;
	}

	psp->use_dedicated_memory = sys_status.use_dedicated_memory;
	psp->psp_ctx.external_mutex = sys_status.psp_cmd_mutex;

	ret = __ras_psp_mem_init(ras_core);
	if (ret)
		return ret;

	if (psp->load_ras_fw_internal)
		ret = load_ras_all_fw(ras_core);

	return ret;
}

int ras_psp_hw_fini(struct ras_core_context *ras_core)
{
	unload_ras_all_fw(ras_core);
	__ras_psp_mem_fini(ras_core);
	return 0;
}

bool ras_psp_check_supported_cmd(struct ras_core_context *ras_core,
		enum ras_ta_cmd_id cmd_id)
{
	struct ras_ta_ctx *ta_ctx = &ras_core->ras_psp.ta_ctx;
	bool ret = false;

	if (!ta_ctx->ras_ta_initialized)
		return false;

	switch (cmd_id) {
	case RAS_TA_CMD_ID__QUERY_ADDRESS:
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

int ras_psp_get_block_ta_id(struct ras_core_context *ras_core,
		uint32_t ras_id, uint32_t *ta_id)
{
	struct ras_psp *psp = &ras_core->ras_psp;
	int i;

	if (!ta_id || !psp->blk_maps || !psp->maps_size) {
		RAS_DEV_ERR(ras_core->dev, "Invalid ras block parameter\n");
		return -EINVAL;
	}

	for (i = 0; i < psp->maps_size; i++) {
		if (psp->blk_maps[i].ras_id == ras_id) {
			*ta_id = psp->blk_maps[i].ta_id;
			return 0;
		}
	}

	RAS_DEV_WARN(ras_core->dev, "Ras block %u is not supported\n", ras_id);

	return -RAS_CORE_NOT_SUPPORTED;
}

bool ras_psp_poison_supported(struct ras_core_context *ras_core)
{
	struct ras_psp *psp = &ras_core->ras_psp;

	return psp->ras_hw_caps.poison_supported;
}

bool ras_psp_flex_mca_enabled(struct ras_core_context *ras_core)
{
	struct ras_psp *psp = &ras_core->ras_psp;

	return psp->ras_hw_caps.flex_mca_enabled;
}

uint64_t ras_psp_get_hw_ras_caps(struct ras_core_context *ras_core)
{
	struct ras_psp *psp = &ras_core->ras_psp;

	return psp->ras_hw_caps.features.block_mask;
}

int ras_psp_translate_addr(struct ras_core_context *ras_core,
	struct ras_psp_addr_trans_in *in, struct ras_psp_addr_trans_out *out)
{
	struct ras_psp *psp = &ras_core->ras_psp;

	if (!in || !out)
		return -EINVAL;

	if (!psp->sys_func || !psp->sys_func->psp_translate_addr)
		return -EOPNOTSUPP;

	return psp->sys_func->psp_translate_addr(ras_core, in, out);
}
