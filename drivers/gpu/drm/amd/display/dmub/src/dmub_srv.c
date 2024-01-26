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
 * Authors: AMD
 *
 */

#include "../dmub_srv.h"
#include "dmub_dcn20.h"
#include "dmub_dcn21.h"
#include "dmub_cmd.h"
#include "dmub_dcn30.h"
#include "dmub_dcn301.h"
#include "dmub_dcn302.h"
#include "dmub_dcn303.h"
#include "dmub_dcn31.h"
#include "dmub_dcn314.h"
#include "dmub_dcn315.h"
#include "dmub_dcn316.h"
#include "dmub_dcn32.h"
#include "dmub_dcn35.h"
#include "os_types.h"
/*
 * Note: the DMUB service is standalone. No additional headers should be
 * added below or above this line unless they reside within the DMUB
 * folder.
 */

/* Alignment for framebuffer memory. */
#define DMUB_FB_ALIGNMENT (1024 * 1024)

/* Stack size. */
#define DMUB_STACK_SIZE (128 * 1024)

/* Context size. */
#define DMUB_CONTEXT_SIZE (512 * 1024)

/* Mailbox size : Ring buffers are required for both inbox and outbox */
#define DMUB_MAILBOX_SIZE ((2 * DMUB_RB_SIZE))

/* Default state size if meta is absent. */
#define DMUB_FW_STATE_SIZE (64 * 1024)

/* Default tracebuffer size if meta is absent. */
#define DMUB_TRACE_BUFFER_SIZE (64 * 1024)


/* Default scratch mem size. */
#define DMUB_SCRATCH_MEM_SIZE (1024)

/* Number of windows in use. */
#define DMUB_NUM_WINDOWS (DMUB_WINDOW_TOTAL)
/* Base addresses. */

#define DMUB_CW0_BASE (0x60000000)
#define DMUB_CW1_BASE (0x61000000)
#define DMUB_CW3_BASE (0x63000000)
#define DMUB_CW4_BASE (0x64000000)
#define DMUB_CW5_BASE (0x65000000)
#define DMUB_CW6_BASE (0x66000000)

#define DMUB_REGION5_BASE (0xA0000000)

static struct dmub_srv_dcn32_regs dmub_srv_dcn32_regs;
static struct dmub_srv_dcn35_regs dmub_srv_dcn35_regs;

static inline uint32_t dmub_align(uint32_t val, uint32_t factor)
{
	return (val + factor - 1) / factor * factor;
}

void dmub_flush_buffer_mem(const struct dmub_fb *fb)
{
	const uint8_t *base = (const uint8_t *)fb->cpu_addr;
	uint8_t buf[64];
	uint32_t pos, end;

	/**
	 * Read 64-byte chunks since we don't want to store a
	 * large temporary buffer for this purpose.
	 */
	end = fb->size / sizeof(buf) * sizeof(buf);

	for (pos = 0; pos < end; pos += sizeof(buf))
		dmub_memcpy(buf, base + pos, sizeof(buf));

	/* Read anything leftover into the buffer. */
	if (end < fb->size)
		dmub_memcpy(buf, base + pos, fb->size - end);
}

static const struct dmub_fw_meta_info *
dmub_get_fw_meta_info_from_blob(const uint8_t *blob, uint32_t blob_size, uint32_t meta_offset)
{
	const union dmub_fw_meta *meta;

	if (!blob || !blob_size)
		return NULL;

	if (blob_size < sizeof(union dmub_fw_meta) + meta_offset)
		return NULL;

	meta = (const union dmub_fw_meta *)(blob + blob_size - meta_offset -
					    sizeof(union dmub_fw_meta));

	if (meta->info.magic_value != DMUB_FW_META_MAGIC)
		return NULL;

	return &meta->info;
}

static const struct dmub_fw_meta_info *
dmub_get_fw_meta_info(const struct dmub_srv_region_params *params)
{
	const struct dmub_fw_meta_info *info = NULL;

	if (params->fw_bss_data && params->bss_data_size) {
		/* Legacy metadata region. */
		info = dmub_get_fw_meta_info_from_blob(params->fw_bss_data,
						       params->bss_data_size,
						       DMUB_FW_META_OFFSET);
	} else if (params->fw_inst_const && params->inst_const_size) {
		/* Combined metadata region - can be aligned to 16-bytes. */
		uint32_t i;

		for (i = 0; i < 16; ++i) {
			info = dmub_get_fw_meta_info_from_blob(
				params->fw_inst_const, params->inst_const_size, i);

			if (info)
				break;
		}
	}

	return info;
}

static bool dmub_srv_hw_setup(struct dmub_srv *dmub, enum dmub_asic asic)
{
	struct dmub_srv_hw_funcs *funcs = &dmub->hw_funcs;

	switch (asic) {
	case DMUB_ASIC_DCN20:
	case DMUB_ASIC_DCN21:
	case DMUB_ASIC_DCN30:
	case DMUB_ASIC_DCN301:
	case DMUB_ASIC_DCN302:
	case DMUB_ASIC_DCN303:
		dmub->regs = &dmub_srv_dcn20_regs;

		funcs->reset = dmub_dcn20_reset;
		funcs->reset_release = dmub_dcn20_reset_release;
		funcs->backdoor_load = dmub_dcn20_backdoor_load;
		funcs->setup_windows = dmub_dcn20_setup_windows;
		funcs->setup_mailbox = dmub_dcn20_setup_mailbox;
		funcs->get_inbox1_wptr = dmub_dcn20_get_inbox1_wptr;
		funcs->get_inbox1_rptr = dmub_dcn20_get_inbox1_rptr;
		funcs->set_inbox1_wptr = dmub_dcn20_set_inbox1_wptr;
		funcs->is_supported = dmub_dcn20_is_supported;
		funcs->is_hw_init = dmub_dcn20_is_hw_init;
		funcs->set_gpint = dmub_dcn20_set_gpint;
		funcs->is_gpint_acked = dmub_dcn20_is_gpint_acked;
		funcs->get_gpint_response = dmub_dcn20_get_gpint_response;
		funcs->get_fw_status = dmub_dcn20_get_fw_boot_status;
		funcs->enable_dmub_boot_options = dmub_dcn20_enable_dmub_boot_options;
		funcs->skip_dmub_panel_power_sequence = dmub_dcn20_skip_dmub_panel_power_sequence;
		funcs->get_current_time = dmub_dcn20_get_current_time;

		// Out mailbox register access functions for RN and above
		funcs->setup_out_mailbox = dmub_dcn20_setup_out_mailbox;
		funcs->get_outbox1_wptr = dmub_dcn20_get_outbox1_wptr;
		funcs->set_outbox1_rptr = dmub_dcn20_set_outbox1_rptr;

		//outbox0 call stacks
		funcs->setup_outbox0 = dmub_dcn20_setup_outbox0;
		funcs->get_outbox0_wptr = dmub_dcn20_get_outbox0_wptr;
		funcs->set_outbox0_rptr = dmub_dcn20_set_outbox0_rptr;

		funcs->get_diagnostic_data = dmub_dcn20_get_diagnostic_data;

		if (asic == DMUB_ASIC_DCN21)
			dmub->regs = &dmub_srv_dcn21_regs;

		if (asic == DMUB_ASIC_DCN30) {
			dmub->regs = &dmub_srv_dcn30_regs;

			funcs->backdoor_load = dmub_dcn30_backdoor_load;
			funcs->setup_windows = dmub_dcn30_setup_windows;
		}
		if (asic == DMUB_ASIC_DCN301) {
			dmub->regs = &dmub_srv_dcn301_regs;

			funcs->backdoor_load = dmub_dcn30_backdoor_load;
			funcs->setup_windows = dmub_dcn30_setup_windows;
		}
		if (asic == DMUB_ASIC_DCN302) {
			dmub->regs = &dmub_srv_dcn302_regs;

			funcs->backdoor_load = dmub_dcn30_backdoor_load;
			funcs->setup_windows = dmub_dcn30_setup_windows;
		}
		if (asic == DMUB_ASIC_DCN303) {
			dmub->regs = &dmub_srv_dcn303_regs;

			funcs->backdoor_load = dmub_dcn30_backdoor_load;
			funcs->setup_windows = dmub_dcn30_setup_windows;
		}
		break;

	case DMUB_ASIC_DCN31:
	case DMUB_ASIC_DCN31B:
	case DMUB_ASIC_DCN314:
	case DMUB_ASIC_DCN315:
	case DMUB_ASIC_DCN316:
		if (asic == DMUB_ASIC_DCN314) {
			dmub->regs_dcn31 = &dmub_srv_dcn314_regs;
			funcs->is_psrsu_supported = dmub_dcn314_is_psrsu_supported;
		} else if (asic == DMUB_ASIC_DCN315) {
			dmub->regs_dcn31 = &dmub_srv_dcn315_regs;
		} else if (asic == DMUB_ASIC_DCN316) {
			dmub->regs_dcn31 = &dmub_srv_dcn316_regs;
		} else {
			dmub->regs_dcn31 = &dmub_srv_dcn31_regs;
			funcs->is_psrsu_supported = dmub_dcn31_is_psrsu_supported;
		}
		funcs->reset = dmub_dcn31_reset;
		funcs->reset_release = dmub_dcn31_reset_release;
		funcs->backdoor_load = dmub_dcn31_backdoor_load;
		funcs->setup_windows = dmub_dcn31_setup_windows;
		funcs->setup_mailbox = dmub_dcn31_setup_mailbox;
		funcs->get_inbox1_wptr = dmub_dcn31_get_inbox1_wptr;
		funcs->get_inbox1_rptr = dmub_dcn31_get_inbox1_rptr;
		funcs->set_inbox1_wptr = dmub_dcn31_set_inbox1_wptr;
		funcs->setup_out_mailbox = dmub_dcn31_setup_out_mailbox;
		funcs->get_outbox1_wptr = dmub_dcn31_get_outbox1_wptr;
		funcs->set_outbox1_rptr = dmub_dcn31_set_outbox1_rptr;
		funcs->is_supported = dmub_dcn31_is_supported;
		funcs->is_hw_init = dmub_dcn31_is_hw_init;
		funcs->set_gpint = dmub_dcn31_set_gpint;
		funcs->is_gpint_acked = dmub_dcn31_is_gpint_acked;
		funcs->get_gpint_response = dmub_dcn31_get_gpint_response;
		funcs->get_gpint_dataout = dmub_dcn31_get_gpint_dataout;
		funcs->get_fw_status = dmub_dcn31_get_fw_boot_status;
		funcs->get_fw_boot_option = dmub_dcn31_get_fw_boot_option;
		funcs->enable_dmub_boot_options = dmub_dcn31_enable_dmub_boot_options;
		funcs->skip_dmub_panel_power_sequence = dmub_dcn31_skip_dmub_panel_power_sequence;
		//outbox0 call stacks
		funcs->setup_outbox0 = dmub_dcn31_setup_outbox0;
		funcs->get_outbox0_wptr = dmub_dcn31_get_outbox0_wptr;
		funcs->set_outbox0_rptr = dmub_dcn31_set_outbox0_rptr;

		funcs->get_diagnostic_data = dmub_dcn31_get_diagnostic_data;
		funcs->should_detect = dmub_dcn31_should_detect;
		funcs->get_current_time = dmub_dcn31_get_current_time;

		break;

	case DMUB_ASIC_DCN32:
	case DMUB_ASIC_DCN321:
		dmub->regs_dcn32 = &dmub_srv_dcn32_regs;
		funcs->configure_dmub_in_system_memory = dmub_dcn32_configure_dmub_in_system_memory;
		funcs->send_inbox0_cmd = dmub_dcn32_send_inbox0_cmd;
		funcs->clear_inbox0_ack_register = dmub_dcn32_clear_inbox0_ack_register;
		funcs->read_inbox0_ack_register = dmub_dcn32_read_inbox0_ack_register;
		funcs->subvp_save_surf_addr = dmub_dcn32_save_surf_addr;
		funcs->reset = dmub_dcn32_reset;
		funcs->reset_release = dmub_dcn32_reset_release;
		funcs->backdoor_load = dmub_dcn32_backdoor_load;
		funcs->backdoor_load_zfb_mode = dmub_dcn32_backdoor_load_zfb_mode;
		funcs->setup_windows = dmub_dcn32_setup_windows;
		funcs->setup_mailbox = dmub_dcn32_setup_mailbox;
		funcs->get_inbox1_wptr = dmub_dcn32_get_inbox1_wptr;
		funcs->get_inbox1_rptr = dmub_dcn32_get_inbox1_rptr;
		funcs->set_inbox1_wptr = dmub_dcn32_set_inbox1_wptr;
		funcs->setup_out_mailbox = dmub_dcn32_setup_out_mailbox;
		funcs->get_outbox1_wptr = dmub_dcn32_get_outbox1_wptr;
		funcs->set_outbox1_rptr = dmub_dcn32_set_outbox1_rptr;
		funcs->is_supported = dmub_dcn32_is_supported;
		funcs->is_hw_init = dmub_dcn32_is_hw_init;
		funcs->set_gpint = dmub_dcn32_set_gpint;
		funcs->is_gpint_acked = dmub_dcn32_is_gpint_acked;
		funcs->get_gpint_response = dmub_dcn32_get_gpint_response;
		funcs->get_gpint_dataout = dmub_dcn32_get_gpint_dataout;
		funcs->get_fw_status = dmub_dcn32_get_fw_boot_status;
		funcs->enable_dmub_boot_options = dmub_dcn32_enable_dmub_boot_options;
		funcs->skip_dmub_panel_power_sequence = dmub_dcn32_skip_dmub_panel_power_sequence;

		/* outbox0 call stacks */
		funcs->setup_outbox0 = dmub_dcn32_setup_outbox0;
		funcs->get_outbox0_wptr = dmub_dcn32_get_outbox0_wptr;
		funcs->set_outbox0_rptr = dmub_dcn32_set_outbox0_rptr;
		funcs->get_current_time = dmub_dcn32_get_current_time;
		funcs->get_diagnostic_data = dmub_dcn32_get_diagnostic_data;
		funcs->init_reg_offsets = dmub_srv_dcn32_regs_init;

		break;

	case DMUB_ASIC_DCN35:
			dmub->regs_dcn35 = &dmub_srv_dcn35_regs;
			funcs->configure_dmub_in_system_memory = dmub_dcn35_configure_dmub_in_system_memory;
			funcs->send_inbox0_cmd = dmub_dcn35_send_inbox0_cmd;
			funcs->clear_inbox0_ack_register = dmub_dcn35_clear_inbox0_ack_register;
			funcs->read_inbox0_ack_register = dmub_dcn35_read_inbox0_ack_register;
			funcs->reset = dmub_dcn35_reset;
			funcs->reset_release = dmub_dcn35_reset_release;
			funcs->backdoor_load = dmub_dcn35_backdoor_load;
			funcs->backdoor_load_zfb_mode = dmub_dcn35_backdoor_load_zfb_mode;
			funcs->setup_windows = dmub_dcn35_setup_windows;
			funcs->setup_mailbox = dmub_dcn35_setup_mailbox;
			funcs->get_inbox1_wptr = dmub_dcn35_get_inbox1_wptr;
			funcs->get_inbox1_rptr = dmub_dcn35_get_inbox1_rptr;
			funcs->set_inbox1_wptr = dmub_dcn35_set_inbox1_wptr;
			funcs->setup_out_mailbox = dmub_dcn35_setup_out_mailbox;
			funcs->get_outbox1_wptr = dmub_dcn35_get_outbox1_wptr;
			funcs->set_outbox1_rptr = dmub_dcn35_set_outbox1_rptr;
			funcs->is_supported = dmub_dcn35_is_supported;
			funcs->is_hw_init = dmub_dcn35_is_hw_init;
			funcs->set_gpint = dmub_dcn35_set_gpint;
			funcs->is_gpint_acked = dmub_dcn35_is_gpint_acked;
			funcs->get_gpint_response = dmub_dcn35_get_gpint_response;
			funcs->get_gpint_dataout = dmub_dcn35_get_gpint_dataout;
			funcs->get_fw_status = dmub_dcn35_get_fw_boot_status;
			funcs->get_fw_boot_option = dmub_dcn35_get_fw_boot_option;
			funcs->enable_dmub_boot_options = dmub_dcn35_enable_dmub_boot_options;
			funcs->skip_dmub_panel_power_sequence = dmub_dcn35_skip_dmub_panel_power_sequence;
			//outbox0 call stacks
			funcs->setup_outbox0 = dmub_dcn35_setup_outbox0;
			funcs->get_outbox0_wptr = dmub_dcn35_get_outbox0_wptr;
			funcs->set_outbox0_rptr = dmub_dcn35_set_outbox0_rptr;

			funcs->get_current_time = dmub_dcn35_get_current_time;
			funcs->get_diagnostic_data = dmub_dcn35_get_diagnostic_data;

			funcs->init_reg_offsets = dmub_srv_dcn35_regs_init;

			funcs->is_hw_powered_up = dmub_dcn35_is_hw_powered_up;
			funcs->should_detect = dmub_dcn35_should_detect;
			break;

	default:
		return false;
	}

	return true;
}

enum dmub_status dmub_srv_create(struct dmub_srv *dmub,
				 const struct dmub_srv_create_params *params)
{
	enum dmub_status status = DMUB_STATUS_OK;

	dmub_memset(dmub, 0, sizeof(*dmub));

	dmub->funcs = params->funcs;
	dmub->user_ctx = params->user_ctx;
	dmub->asic = params->asic;
	dmub->fw_version = params->fw_version;
	dmub->is_virtual = params->is_virtual;

	/* Setup asic dependent hardware funcs. */
	if (!dmub_srv_hw_setup(dmub, params->asic)) {
		status = DMUB_STATUS_INVALID;
		goto cleanup;
	}

	/* Override (some) hardware funcs based on user params. */
	if (params->hw_funcs) {
		if (params->hw_funcs->emul_get_inbox1_rptr)
			dmub->hw_funcs.emul_get_inbox1_rptr =
				params->hw_funcs->emul_get_inbox1_rptr;

		if (params->hw_funcs->emul_set_inbox1_wptr)
			dmub->hw_funcs.emul_set_inbox1_wptr =
				params->hw_funcs->emul_set_inbox1_wptr;

		if (params->hw_funcs->is_supported)
			dmub->hw_funcs.is_supported =
				params->hw_funcs->is_supported;
	}

	/* Sanity checks for required hw func pointers. */
	if (!dmub->hw_funcs.get_inbox1_rptr ||
	    !dmub->hw_funcs.set_inbox1_wptr) {
		status = DMUB_STATUS_INVALID;
		goto cleanup;
	}

cleanup:
	if (status == DMUB_STATUS_OK)
		dmub->sw_init = true;
	else
		dmub_srv_destroy(dmub);

	return status;
}

void dmub_srv_destroy(struct dmub_srv *dmub)
{
	dmub_memset(dmub, 0, sizeof(*dmub));
}

enum dmub_status
dmub_srv_calc_region_info(struct dmub_srv *dmub,
			  const struct dmub_srv_region_params *params,
			  struct dmub_srv_region_info *out)
{
	struct dmub_region *inst = &out->regions[DMUB_WINDOW_0_INST_CONST];
	struct dmub_region *stack = &out->regions[DMUB_WINDOW_1_STACK];
	struct dmub_region *data = &out->regions[DMUB_WINDOW_2_BSS_DATA];
	struct dmub_region *bios = &out->regions[DMUB_WINDOW_3_VBIOS];
	struct dmub_region *mail = &out->regions[DMUB_WINDOW_4_MAILBOX];
	struct dmub_region *trace_buff = &out->regions[DMUB_WINDOW_5_TRACEBUFF];
	struct dmub_region *fw_state = &out->regions[DMUB_WINDOW_6_FW_STATE];
	struct dmub_region *scratch_mem = &out->regions[DMUB_WINDOW_7_SCRATCH_MEM];
	const struct dmub_fw_meta_info *fw_info;
	uint32_t fw_state_size = DMUB_FW_STATE_SIZE;
	uint32_t trace_buffer_size = DMUB_TRACE_BUFFER_SIZE;
	uint32_t scratch_mem_size = DMUB_SCRATCH_MEM_SIZE;
	uint32_t previous_top = 0;
	if (!dmub->sw_init)
		return DMUB_STATUS_INVALID;

	memset(out, 0, sizeof(*out));

	out->num_regions = DMUB_NUM_WINDOWS;

	inst->base = 0x0;
	inst->top = inst->base + params->inst_const_size;

	data->base = dmub_align(inst->top, 256);
	data->top = data->base + params->bss_data_size;

	/*
	 * All cache windows below should be aligned to the size
	 * of the DMCUB cache line, 64 bytes.
	 */

	stack->base = dmub_align(data->top, 256);
	stack->top = stack->base + DMUB_STACK_SIZE + DMUB_CONTEXT_SIZE;

	bios->base = dmub_align(stack->top, 256);
	bios->top = bios->base + params->vbios_size;

	if (params->is_mailbox_in_inbox) {
		mail->base = 0;
		mail->top = mail->base + DMUB_MAILBOX_SIZE;
		previous_top = bios->top;
	} else {
		mail->base = dmub_align(bios->top, 256);
		mail->top = mail->base + DMUB_MAILBOX_SIZE;
		previous_top = mail->top;
	}

	fw_info = dmub_get_fw_meta_info(params);

	if (fw_info) {
		fw_state_size = fw_info->fw_region_size;
		trace_buffer_size = fw_info->trace_buffer_size;

		/**
		 * If DM didn't fill in a version, then fill it in based on
		 * the firmware meta now that we have it.
		 *
		 * TODO: Make it easier for driver to extract this out to
		 * pass during creation.
		 */
		if (dmub->fw_version == 0)
			dmub->fw_version = fw_info->fw_version;
	}

	trace_buff->base = dmub_align(previous_top, 256);
	trace_buff->top = trace_buff->base + dmub_align(trace_buffer_size, 64);

	fw_state->base = dmub_align(trace_buff->top, 256);
	fw_state->top = fw_state->base + dmub_align(fw_state_size, 64);

	scratch_mem->base = dmub_align(fw_state->top, 256);
	scratch_mem->top = scratch_mem->base + dmub_align(scratch_mem_size, 64);

	out->fb_size = dmub_align(scratch_mem->top, 4096);

	if (params->is_mailbox_in_inbox)
		out->inbox_size = dmub_align(mail->top, 4096);

	return DMUB_STATUS_OK;
}

enum dmub_status dmub_srv_calc_mem_info(struct dmub_srv *dmub,
				       const struct dmub_srv_memory_params *params,
				       struct dmub_srv_fb_info *out)
{
	uint8_t *cpu_base;
	uint64_t gpu_base;
	uint32_t i;

	if (!dmub->sw_init)
		return DMUB_STATUS_INVALID;

	memset(out, 0, sizeof(*out));

	if (params->region_info->num_regions != DMUB_NUM_WINDOWS)
		return DMUB_STATUS_INVALID;

	cpu_base = (uint8_t *)params->cpu_fb_addr;
	gpu_base = params->gpu_fb_addr;

	for (i = 0; i < DMUB_NUM_WINDOWS; ++i) {
		const struct dmub_region *reg =
			&params->region_info->regions[i];

		out->fb[i].cpu_addr = cpu_base + reg->base;
		out->fb[i].gpu_addr = gpu_base + reg->base;

		if (i == DMUB_WINDOW_4_MAILBOX && params->cpu_inbox_addr != 0) {
			out->fb[i].cpu_addr = (uint8_t *)params->cpu_inbox_addr + reg->base;
			out->fb[i].gpu_addr = params->gpu_inbox_addr + reg->base;
		}

		out->fb[i].size = reg->top - reg->base;
	}

	out->num_fb = DMUB_NUM_WINDOWS;

	return DMUB_STATUS_OK;
}

enum dmub_status dmub_srv_has_hw_support(struct dmub_srv *dmub,
					 bool *is_supported)
{
	*is_supported = false;

	if (!dmub->sw_init)
		return DMUB_STATUS_INVALID;

	if (dmub->hw_funcs.is_supported)
		*is_supported = dmub->hw_funcs.is_supported(dmub);

	return DMUB_STATUS_OK;
}

enum dmub_status dmub_srv_is_hw_init(struct dmub_srv *dmub, bool *is_hw_init)
{
	*is_hw_init = false;

	if (!dmub->sw_init)
		return DMUB_STATUS_INVALID;

	if (!dmub->hw_init)
		return DMUB_STATUS_OK;

	if (dmub->hw_funcs.is_hw_init)
		*is_hw_init = dmub->hw_funcs.is_hw_init(dmub);

	return DMUB_STATUS_OK;
}

enum dmub_status dmub_srv_hw_init(struct dmub_srv *dmub,
				  const struct dmub_srv_hw_params *params)
{
	struct dmub_fb *inst_fb = params->fb[DMUB_WINDOW_0_INST_CONST];
	struct dmub_fb *stack_fb = params->fb[DMUB_WINDOW_1_STACK];
	struct dmub_fb *data_fb = params->fb[DMUB_WINDOW_2_BSS_DATA];
	struct dmub_fb *bios_fb = params->fb[DMUB_WINDOW_3_VBIOS];
	struct dmub_fb *mail_fb = params->fb[DMUB_WINDOW_4_MAILBOX];
	struct dmub_fb *tracebuff_fb = params->fb[DMUB_WINDOW_5_TRACEBUFF];
	struct dmub_fb *fw_state_fb = params->fb[DMUB_WINDOW_6_FW_STATE];
	struct dmub_fb *scratch_mem_fb = params->fb[DMUB_WINDOW_7_SCRATCH_MEM];

	struct dmub_rb_init_params rb_params, outbox0_rb_params;
	struct dmub_window cw0, cw1, cw2, cw3, cw4, cw5, cw6;
	struct dmub_region inbox1, outbox1, outbox0;

	if (!dmub->sw_init)
		return DMUB_STATUS_INVALID;

	if (!inst_fb || !stack_fb || !data_fb || !bios_fb || !mail_fb ||
		!tracebuff_fb || !fw_state_fb || !scratch_mem_fb) {
		ASSERT(0);
		return DMUB_STATUS_INVALID;
	}

	dmub->fb_base = params->fb_base;
	dmub->fb_offset = params->fb_offset;
	dmub->psp_version = params->psp_version;

	if (dmub->hw_funcs.reset)
		dmub->hw_funcs.reset(dmub);

	/* reset the cache of the last wptr as well now that hw is reset */
	dmub->inbox1_last_wptr = 0;

	cw0.offset.quad_part = inst_fb->gpu_addr;
	cw0.region.base = DMUB_CW0_BASE;
	cw0.region.top = cw0.region.base + inst_fb->size - 1;

	cw1.offset.quad_part = stack_fb->gpu_addr;
	cw1.region.base = DMUB_CW1_BASE;
	cw1.region.top = cw1.region.base + stack_fb->size - 1;

	if (params->fw_in_system_memory && dmub->hw_funcs.configure_dmub_in_system_memory)
		dmub->hw_funcs.configure_dmub_in_system_memory(dmub);

	if (params->load_inst_const && dmub->hw_funcs.backdoor_load) {
		/**
		 * Read back all the instruction memory so we don't hang the
		 * DMCUB when backdoor loading if the write from x86 hasn't been
		 * flushed yet. This only occurs in backdoor loading.
		 */
		if (params->mem_access_type == DMUB_MEMORY_ACCESS_CPU)
			dmub_flush_buffer_mem(inst_fb);

		if (params->fw_in_system_memory && dmub->hw_funcs.backdoor_load_zfb_mode)
			dmub->hw_funcs.backdoor_load_zfb_mode(dmub, &cw0, &cw1);
		else
			dmub->hw_funcs.backdoor_load(dmub, &cw0, &cw1);
	}

	cw2.offset.quad_part = data_fb->gpu_addr;
	cw2.region.base = DMUB_CW0_BASE + inst_fb->size;
	cw2.region.top = cw2.region.base + data_fb->size;

	cw3.offset.quad_part = bios_fb->gpu_addr;
	cw3.region.base = DMUB_CW3_BASE;
	cw3.region.top = cw3.region.base + bios_fb->size;

	cw4.offset.quad_part = mail_fb->gpu_addr;
	cw4.region.base = DMUB_CW4_BASE;
	cw4.region.top = cw4.region.base + mail_fb->size;

	/**
	 * Doubled the mailbox region to accomodate inbox and outbox.
	 * Note: Currently, currently total mailbox size is 16KB. It is split
	 * equally into 8KB between inbox and outbox. If this config is
	 * changed, then uncached base address configuration of outbox1
	 * has to be updated in funcs->setup_out_mailbox.
	 */
	inbox1.base = cw4.region.base;
	inbox1.top = cw4.region.base + DMUB_RB_SIZE;
	outbox1.base = inbox1.top;
	outbox1.top = cw4.region.top;

	cw5.offset.quad_part = tracebuff_fb->gpu_addr;
	cw5.region.base = DMUB_CW5_BASE;
	cw5.region.top = cw5.region.base + tracebuff_fb->size;

	outbox0.base = DMUB_REGION5_BASE + TRACE_BUFFER_ENTRY_OFFSET;
	outbox0.top = outbox0.base + tracebuff_fb->size - TRACE_BUFFER_ENTRY_OFFSET;

	cw6.offset.quad_part = fw_state_fb->gpu_addr;
	cw6.region.base = DMUB_CW6_BASE;
	cw6.region.top = cw6.region.base + fw_state_fb->size;

	dmub->fw_state = fw_state_fb->cpu_addr;

	dmub->scratch_mem_fb = *scratch_mem_fb;

	if (dmub->hw_funcs.setup_windows)
		dmub->hw_funcs.setup_windows(dmub, &cw2, &cw3, &cw4, &cw5, &cw6);

	if (dmub->hw_funcs.setup_outbox0)
		dmub->hw_funcs.setup_outbox0(dmub, &outbox0);

	if (dmub->hw_funcs.setup_mailbox)
		dmub->hw_funcs.setup_mailbox(dmub, &inbox1);
	if (dmub->hw_funcs.setup_out_mailbox)
		dmub->hw_funcs.setup_out_mailbox(dmub, &outbox1);

	dmub_memset(&rb_params, 0, sizeof(rb_params));
	rb_params.ctx = dmub;
	rb_params.base_address = mail_fb->cpu_addr;
	rb_params.capacity = DMUB_RB_SIZE;
	dmub_rb_init(&dmub->inbox1_rb, &rb_params);

	// Initialize outbox1 ring buffer
	rb_params.ctx = dmub;
	rb_params.base_address = (void *) ((uint8_t *) (mail_fb->cpu_addr) + DMUB_RB_SIZE);
	rb_params.capacity = DMUB_RB_SIZE;
	dmub_rb_init(&dmub->outbox1_rb, &rb_params);

	dmub_memset(&outbox0_rb_params, 0, sizeof(outbox0_rb_params));
	outbox0_rb_params.ctx = dmub;
	outbox0_rb_params.base_address = (void *)((uintptr_t)(tracebuff_fb->cpu_addr) + TRACE_BUFFER_ENTRY_OFFSET);
	outbox0_rb_params.capacity = tracebuff_fb->size - dmub_align(TRACE_BUFFER_ENTRY_OFFSET, 64);
	dmub_rb_init(&dmub->outbox0_rb, &outbox0_rb_params);

	/* Report to DMUB what features are supported by current driver */
	if (dmub->hw_funcs.enable_dmub_boot_options)
		dmub->hw_funcs.enable_dmub_boot_options(dmub, params);

	if (dmub->hw_funcs.skip_dmub_panel_power_sequence && !dmub->is_virtual)
		dmub->hw_funcs.skip_dmub_panel_power_sequence(dmub,
			params->skip_panel_power_sequence);

	if (dmub->hw_funcs.reset_release && !dmub->is_virtual)
		dmub->hw_funcs.reset_release(dmub);

	dmub->hw_init = true;
	dmub->power_state = DMUB_POWER_STATE_D0;

	return DMUB_STATUS_OK;
}

enum dmub_status dmub_srv_sync_inbox1(struct dmub_srv *dmub)
{
	if (!dmub->sw_init)
		return DMUB_STATUS_INVALID;

	if (dmub->hw_funcs.get_inbox1_rptr && dmub->hw_funcs.get_inbox1_wptr) {
		uint32_t rptr = dmub->hw_funcs.get_inbox1_rptr(dmub);
		uint32_t wptr = dmub->hw_funcs.get_inbox1_wptr(dmub);

		if (rptr > dmub->inbox1_rb.capacity || wptr > dmub->inbox1_rb.capacity) {
			return DMUB_STATUS_HW_FAILURE;
		} else {
			dmub->inbox1_rb.rptr = rptr;
			dmub->inbox1_rb.wrpt = wptr;
			dmub->inbox1_last_wptr = dmub->inbox1_rb.wrpt;
		}
	}

	return DMUB_STATUS_OK;
}

enum dmub_status dmub_srv_hw_reset(struct dmub_srv *dmub)
{
	if (!dmub->sw_init)
		return DMUB_STATUS_INVALID;

	if (dmub->hw_funcs.reset)
		dmub->hw_funcs.reset(dmub);

	/* mailboxes have been reset in hw, so reset the sw state as well */
	dmub->inbox1_last_wptr = 0;
	dmub->inbox1_rb.wrpt = 0;
	dmub->inbox1_rb.rptr = 0;
	dmub->outbox0_rb.wrpt = 0;
	dmub->outbox0_rb.rptr = 0;
	dmub->outbox1_rb.wrpt = 0;
	dmub->outbox1_rb.rptr = 0;

	dmub->hw_init = false;

	return DMUB_STATUS_OK;
}

enum dmub_status dmub_srv_cmd_queue(struct dmub_srv *dmub,
				    const union dmub_rb_cmd *cmd)
{
	if (!dmub->hw_init)
		return DMUB_STATUS_INVALID;

	if (dmub->power_state != DMUB_POWER_STATE_D0)
		return DMUB_STATUS_POWER_STATE_D3;

	if (dmub->inbox1_rb.rptr > dmub->inbox1_rb.capacity ||
	    dmub->inbox1_rb.wrpt > dmub->inbox1_rb.capacity) {
		return DMUB_STATUS_HW_FAILURE;
	}

	if (dmub_rb_push_front(&dmub->inbox1_rb, cmd))
		return DMUB_STATUS_OK;

	return DMUB_STATUS_QUEUE_FULL;
}

enum dmub_status dmub_srv_cmd_execute(struct dmub_srv *dmub)
{
	struct dmub_rb flush_rb;

	if (!dmub->hw_init)
		return DMUB_STATUS_INVALID;

	if (dmub->power_state != DMUB_POWER_STATE_D0)
		return DMUB_STATUS_POWER_STATE_D3;

	/**
	 * Read back all the queued commands to ensure that they've
	 * been flushed to framebuffer memory. Otherwise DMCUB might
	 * read back stale, fully invalid or partially invalid data.
	 */
	flush_rb = dmub->inbox1_rb;
	flush_rb.rptr = dmub->inbox1_last_wptr;
	dmub_rb_flush_pending(&flush_rb);

	dmub->hw_funcs.set_inbox1_wptr(dmub, dmub->inbox1_rb.wrpt);

	dmub->inbox1_last_wptr = dmub->inbox1_rb.wrpt;

	return DMUB_STATUS_OK;
}

bool dmub_srv_is_hw_pwr_up(struct dmub_srv *dmub)
{
	if (!dmub->hw_funcs.is_hw_powered_up)
		return true;

	return dmub->hw_funcs.is_hw_powered_up(dmub) &&
		dmub->hw_funcs.is_hw_init(dmub);
}

enum dmub_status dmub_srv_wait_for_hw_pwr_up(struct dmub_srv *dmub,
					     uint32_t timeout_us)
{
	uint32_t i;

	if (!dmub->hw_init)
		return DMUB_STATUS_INVALID;

	for (i = 0; i <= timeout_us; i += 100) {
		if (dmub_srv_is_hw_pwr_up(dmub))
			return DMUB_STATUS_OK;

		udelay(100);
	}

	return DMUB_STATUS_TIMEOUT;
}

enum dmub_status dmub_srv_wait_for_auto_load(struct dmub_srv *dmub,
					     uint32_t timeout_us)
{
	uint32_t i;
	bool hw_on = true;

	if (!dmub->hw_init)
		return DMUB_STATUS_INVALID;

	for (i = 0; i <= timeout_us; i += 100) {
		union dmub_fw_boot_status status = dmub->hw_funcs.get_fw_status(dmub);

		if (dmub->hw_funcs.is_hw_powered_up)
			hw_on = dmub->hw_funcs.is_hw_powered_up(dmub);

		if (status.bits.dal_fw && status.bits.mailbox_rdy && hw_on)
			return DMUB_STATUS_OK;

		udelay(100);
	}

	return DMUB_STATUS_TIMEOUT;
}

enum dmub_status dmub_srv_wait_for_idle(struct dmub_srv *dmub,
					uint32_t timeout_us)
{
	uint32_t i, rptr;

	if (!dmub->hw_init)
		return DMUB_STATUS_INVALID;

	for (i = 0; i <= timeout_us; ++i) {
		rptr = dmub->hw_funcs.get_inbox1_rptr(dmub);

		if (rptr > dmub->inbox1_rb.capacity)
			return DMUB_STATUS_HW_FAILURE;

		dmub->inbox1_rb.rptr = rptr;

		if (dmub_rb_empty(&dmub->inbox1_rb))
			return DMUB_STATUS_OK;

		udelay(1);
	}

	return DMUB_STATUS_TIMEOUT;
}

enum dmub_status
dmub_srv_send_gpint_command(struct dmub_srv *dmub,
			    enum dmub_gpint_command command_code,
			    uint16_t param, uint32_t timeout_us)
{
	union dmub_gpint_data_register reg;
	uint32_t i;

	if (!dmub->sw_init)
		return DMUB_STATUS_INVALID;

	if (!dmub->hw_funcs.set_gpint)
		return DMUB_STATUS_INVALID;

	if (!dmub->hw_funcs.is_gpint_acked)
		return DMUB_STATUS_INVALID;

	reg.bits.status = 1;
	reg.bits.command_code = command_code;
	reg.bits.param = param;

	dmub->hw_funcs.set_gpint(dmub, reg);

	for (i = 0; i < timeout_us; ++i) {
		udelay(1);

		if (dmub->hw_funcs.is_gpint_acked(dmub, reg))
			return DMUB_STATUS_OK;
	}

	return DMUB_STATUS_TIMEOUT;
}

enum dmub_status dmub_srv_get_gpint_response(struct dmub_srv *dmub,
					     uint32_t *response)
{
	*response = 0;

	if (!dmub->sw_init)
		return DMUB_STATUS_INVALID;

	if (!dmub->hw_funcs.get_gpint_response)
		return DMUB_STATUS_INVALID;

	*response = dmub->hw_funcs.get_gpint_response(dmub);

	return DMUB_STATUS_OK;
}

enum dmub_status dmub_srv_get_gpint_dataout(struct dmub_srv *dmub,
					     uint32_t *dataout)
{
	*dataout = 0;

	if (!dmub->sw_init)
		return DMUB_STATUS_INVALID;

	if (!dmub->hw_funcs.get_gpint_dataout)
		return DMUB_STATUS_INVALID;

	*dataout = dmub->hw_funcs.get_gpint_dataout(dmub);

	return DMUB_STATUS_OK;
}

enum dmub_status dmub_srv_get_fw_boot_status(struct dmub_srv *dmub,
					     union dmub_fw_boot_status *status)
{
	status->all = 0;

	if (!dmub->sw_init)
		return DMUB_STATUS_INVALID;

	if (dmub->hw_funcs.get_fw_status)
		*status = dmub->hw_funcs.get_fw_status(dmub);

	return DMUB_STATUS_OK;
}

enum dmub_status dmub_srv_get_fw_boot_option(struct dmub_srv *dmub,
					     union dmub_fw_boot_options *option)
{
	option->all = 0;

	if (!dmub->sw_init)
		return DMUB_STATUS_INVALID;

	if (dmub->hw_funcs.get_fw_boot_option)
		*option = dmub->hw_funcs.get_fw_boot_option(dmub);

	return DMUB_STATUS_OK;
}

enum dmub_status dmub_srv_set_skip_panel_power_sequence(struct dmub_srv *dmub,
					     bool skip)
{
	if (!dmub->sw_init)
		return DMUB_STATUS_INVALID;

	if (dmub->hw_funcs.skip_dmub_panel_power_sequence && !dmub->is_virtual)
		dmub->hw_funcs.skip_dmub_panel_power_sequence(dmub, skip);

	return DMUB_STATUS_OK;
}

enum dmub_status dmub_srv_cmd_with_reply_data(struct dmub_srv *dmub,
					      union dmub_rb_cmd *cmd)
{
	enum dmub_status status = DMUB_STATUS_OK;

	// Queue command
	status = dmub_srv_cmd_queue(dmub, cmd);

	if (status != DMUB_STATUS_OK)
		return status;

	// Execute command
	status = dmub_srv_cmd_execute(dmub);

	if (status != DMUB_STATUS_OK)
		return status;

	// Wait for DMUB to process command
	status = dmub_srv_wait_for_idle(dmub, 100000);

	if (status != DMUB_STATUS_OK)
		return status;

	// Copy data back from ring buffer into command
	dmub_rb_get_return_data(&dmub->inbox1_rb, cmd);

	return status;
}

static inline bool dmub_rb_out_trace_buffer_front(struct dmub_rb *rb,
				 void *entry)
{
	const uint64_t *src = (const uint64_t *)(rb->base_address) + rb->rptr / sizeof(uint64_t);
	uint64_t *dst = (uint64_t *)entry;
	uint8_t i;
	uint8_t loop_count;

	if (rb->rptr == rb->wrpt)
		return false;

	loop_count = sizeof(struct dmcub_trace_buf_entry) / sizeof(uint64_t);
	// copying data
	for (i = 0; i < loop_count; i++)
		*dst++ = *src++;

	rb->rptr += sizeof(struct dmcub_trace_buf_entry);

	rb->rptr %= rb->capacity;

	return true;
}

bool dmub_srv_get_outbox0_msg(struct dmub_srv *dmub, struct dmcub_trace_buf_entry *entry)
{
	dmub->outbox0_rb.wrpt = dmub->hw_funcs.get_outbox0_wptr(dmub);

	return dmub_rb_out_trace_buffer_front(&dmub->outbox0_rb, (void *)entry);
}

bool dmub_srv_get_diagnostic_data(struct dmub_srv *dmub, struct dmub_diagnostic_data *diag_data)
{
	if (!dmub || !dmub->hw_funcs.get_diagnostic_data || !diag_data)
		return false;
	dmub->hw_funcs.get_diagnostic_data(dmub, diag_data);
	return true;
}

bool dmub_srv_should_detect(struct dmub_srv *dmub)
{
	if (!dmub->hw_init || !dmub->hw_funcs.should_detect)
		return false;

	return dmub->hw_funcs.should_detect(dmub);
}

enum dmub_status dmub_srv_clear_inbox0_ack(struct dmub_srv *dmub)
{
	if (!dmub->hw_init || !dmub->hw_funcs.clear_inbox0_ack_register)
		return DMUB_STATUS_INVALID;

	dmub->hw_funcs.clear_inbox0_ack_register(dmub);
	return DMUB_STATUS_OK;
}

enum dmub_status dmub_srv_wait_for_inbox0_ack(struct dmub_srv *dmub, uint32_t timeout_us)
{
	uint32_t i = 0;
	uint32_t ack = 0;

	if (!dmub->hw_init || !dmub->hw_funcs.read_inbox0_ack_register)
		return DMUB_STATUS_INVALID;

	for (i = 0; i <= timeout_us; i++) {
		ack = dmub->hw_funcs.read_inbox0_ack_register(dmub);
		if (ack)
			return DMUB_STATUS_OK;
		udelay(1);
	}
	return DMUB_STATUS_TIMEOUT;
}

enum dmub_status dmub_srv_send_inbox0_cmd(struct dmub_srv *dmub,
		union dmub_inbox0_data_register data)
{
	if (!dmub->hw_init || !dmub->hw_funcs.send_inbox0_cmd)
		return DMUB_STATUS_INVALID;

	dmub->hw_funcs.send_inbox0_cmd(dmub, data);
	return DMUB_STATUS_OK;
}

void dmub_srv_subvp_save_surf_addr(struct dmub_srv *dmub, const struct dc_plane_address *addr, uint8_t subvp_index)
{
	if (dmub->hw_funcs.subvp_save_surf_addr) {
		dmub->hw_funcs.subvp_save_surf_addr(dmub,
				addr,
				subvp_index);
	}
}

void dmub_srv_set_power_state(struct dmub_srv *dmub, enum dmub_srv_power_state_type dmub_srv_power_state)
{
	if (!dmub || !dmub->hw_init)
		return;

	dmub->power_state = dmub_srv_power_state;
}
