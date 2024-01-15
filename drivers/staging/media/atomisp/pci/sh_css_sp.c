// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "hmm.h"

#include "sh_css_sp.h"

#include "input_formatter.h"

#include "dma.h"	/* N_DMA_CHANNEL_ID */

#include "ia_css_buffer.h"
#include "ia_css_binary.h"
#include "sh_css_hrt.h"
#include "sh_css_defs.h"
#include "sh_css_internal.h"
#include "ia_css_control.h"
#include "ia_css_debug.h"
#include "ia_css_debug_pipe.h"
#include "ia_css_event_public.h"
#include "ia_css_mmu.h"
#include "ia_css_stream.h"
#include "ia_css_isp_param.h"
#include "sh_css_params.h"
#include "sh_css_legacy.h"
#include "ia_css_frame_comm.h"
#include "ia_css_isys.h"

#include "gdc_device.h"				/* HRT_GDC_N */

/*#include "sp.h"*/	/* host2sp_enqueue_frame_data() */


#include "assert_support.h"

#include "sw_event_global.h"			/* Event IDs.*/
#include "ia_css_event.h"
#include "mmu_device.h"
#include "ia_css_spctrl.h"
#include "atomisp_internal.h"

#ifndef offsetof
#define offsetof(T, x) ((unsigned int)&(((T *)0)->x))
#endif

#define IA_CSS_INCLUDE_CONFIGURATIONS
#include "ia_css_isp_configs.h"
#define IA_CSS_INCLUDE_STATES
#include "ia_css_isp_states.h"

#include "isp/kernels/ipu2_io_ls/bayer_io_ls/ia_css_bayer_io.host.h"

struct sh_css_sp_group		sh_css_sp_group;
struct sh_css_sp_stage		sh_css_sp_stage;
struct sh_css_isp_stage		sh_css_isp_stage;
static struct sh_css_sp_output		sh_css_sp_output;
static struct sh_css_sp_per_frame_data per_frame_data;

/* true if SP supports frame loop and host2sp_commands */
/* For the moment there is only code that sets this bool to true */
/* TODO: add code that sets this bool to false */
static bool sp_running;

static int
set_output_frame_buffer(const struct ia_css_frame *frame,
			unsigned int idx);

static void
sh_css_copy_buffer_attr_to_spbuffer(struct ia_css_buffer_sp *dest_buf,
				    const enum sh_css_queue_id queue_id,
				    const ia_css_ptr xmem_addr,
				    const enum ia_css_buffer_type buf_type);

static void
initialize_frame_buffer_attribute(struct ia_css_buffer_sp *buf_attr);

static void
initialize_stage_frames(struct ia_css_frames_sp *frames);

/* This data is stored every frame */
void
store_sp_group_data(void)
{
	per_frame_data.sp_group_addr = sh_css_store_sp_group_to_ddr();
}

static void
copy_isp_stage_to_sp_stage(void)
{
	/* [WW07.5]type casting will cause potential issues */
	sh_css_sp_stage.num_stripes = (uint8_t)
				      sh_css_isp_stage.binary_info.iterator.num_stripes;
	sh_css_sp_stage.row_stripes_height = (uint16_t)
					     sh_css_isp_stage.binary_info.iterator.row_stripes_height;
	sh_css_sp_stage.row_stripes_overlap_lines = (uint16_t)
		sh_css_isp_stage.binary_info.iterator.row_stripes_overlap_lines;
	sh_css_sp_stage.top_cropping = (uint16_t)
				       sh_css_isp_stage.binary_info.pipeline.top_cropping;
	/* moved to sh_css_sp_init_stage
	   sh_css_sp_stage.enable.vf_output =
	   sh_css_isp_stage.binary_info.enable.vf_veceven ||
	   sh_css_isp_stage.binary_info.num_output_pins > 1;
	*/
	sh_css_sp_stage.enable.sdis = sh_css_isp_stage.binary_info.enable.dis;
	sh_css_sp_stage.enable.s3a = sh_css_isp_stage.binary_info.enable.s3a;
}

void
store_sp_stage_data(enum ia_css_pipe_id id, unsigned int pipe_num,
		    unsigned int stage)
{
	unsigned int thread_id;

	ia_css_pipeline_get_sp_thread_id(pipe_num, &thread_id);
	copy_isp_stage_to_sp_stage();
	if (id != IA_CSS_PIPE_ID_COPY)
		sh_css_sp_stage.isp_stage_addr =
		    sh_css_store_isp_stage_to_ddr(pipe_num, stage);
	sh_css_sp_group.pipe[thread_id].sp_stage_addr[stage] =
	    sh_css_store_sp_stage_to_ddr(pipe_num, stage);

	/* Clear for next frame */
	sh_css_sp_stage.program_input_circuit = false;
}

static void
store_sp_per_frame_data(const struct ia_css_fw_info *fw)
{
	unsigned int HIVE_ADDR_sp_per_frame_data = 0;

	assert(fw);

	switch (fw->type) {
	case ia_css_sp_firmware:
		HIVE_ADDR_sp_per_frame_data = fw->info.sp.per_frame_data;
		break;
	case ia_css_acc_firmware:
		HIVE_ADDR_sp_per_frame_data = fw->info.acc.per_frame_data;
		break;
	case ia_css_isp_firmware:
		return;
	}

	sp_dmem_store(SP0_ID,
		      (unsigned int)sp_address_of(sp_per_frame_data),
		      &per_frame_data,
		      sizeof(per_frame_data));
}

static void
sh_css_store_sp_per_frame_data(enum ia_css_pipe_id pipe_id,
			       unsigned int pipe_num,
			       const struct ia_css_fw_info *sp_fw)
{
	if (!sp_fw)
		sp_fw = &sh_css_sp_fw;

	store_sp_stage_data(pipe_id, pipe_num, 0);
	store_sp_group_data();
	store_sp_per_frame_data(sp_fw);
}

#if SP_DEBUG != SP_DEBUG_NONE

void
sh_css_sp_get_debug_state(struct sh_css_sp_debug_state *state)
{
	const struct ia_css_fw_info *fw = &sh_css_sp_fw;
	unsigned int HIVE_ADDR_sp_output = fw->info.sp.output;
	unsigned int i;
	unsigned int offset = (unsigned int)offsetof(struct sh_css_sp_output,
			      debug) / sizeof(int);

	assert(state);

	(void)HIVE_ADDR_sp_output; /* To get rid of warning in CRUN */
	for (i = 0; i < sizeof(*state) / sizeof(int); i++)
		((unsigned *)state)[i] = load_sp_array_uint(sp_output, i + offset);
}

#endif

void
sh_css_sp_start_binary_copy(unsigned int pipe_num,
			    struct ia_css_frame *out_frame,
			    unsigned int two_ppc)
{
	enum ia_css_pipe_id pipe_id;
	unsigned int thread_id;
	struct sh_css_sp_pipeline *pipe;
	u8 stage_num = 0;

	assert(out_frame);
	pipe_id = IA_CSS_PIPE_ID_CAPTURE;
	ia_css_pipeline_get_sp_thread_id(pipe_num, &thread_id);
	pipe = &sh_css_sp_group.pipe[thread_id];

	pipe->copy.bin.bytes_available = out_frame->data_bytes;
	pipe->num_stages = 1;
	pipe->pipe_id = pipe_id;
	pipe->pipe_num = pipe_num;
	pipe->thread_id = thread_id;
	pipe->pipe_config = 0x0; /* No parameters */
	pipe->pipe_qos_config = QOS_INVALID;

	if (pipe->inout_port_config == 0) {
		SH_CSS_PIPE_PORT_CONFIG_SET(pipe->inout_port_config,
					    (uint8_t)SH_CSS_PORT_INPUT,
					    (uint8_t)SH_CSS_HOST_TYPE, 1);
		SH_CSS_PIPE_PORT_CONFIG_SET(pipe->inout_port_config,
					    (uint8_t)SH_CSS_PORT_OUTPUT,
					    (uint8_t)SH_CSS_HOST_TYPE, 1);
	}
	IA_CSS_LOG("pipe_id %d port_config %08x",
		   pipe->pipe_id, pipe->inout_port_config);

	if (!IS_ISP2401)
		sh_css_sp_group.config.input_formatter.isp_2ppc = (uint8_t)two_ppc;

	sh_css_sp_stage.num = stage_num;
	sh_css_sp_stage.stage_type = SH_CSS_SP_STAGE_TYPE;
	sh_css_sp_stage.func =
	    (unsigned int)IA_CSS_PIPELINE_BIN_COPY;

	set_output_frame_buffer(out_frame, 0);

	/* sp_bin_copy_init on the SP does not deal with dynamica/static yet */
	/* For now always update the dynamic data from out frames. */
	sh_css_store_sp_per_frame_data(pipe_id, pipe_num, &sh_css_sp_fw);
}

static void
sh_css_sp_start_raw_copy(struct ia_css_frame *out_frame,
			 unsigned int pipe_num,
			 unsigned int two_ppc,
			 unsigned int max_input_width,
			 enum sh_css_pipe_config_override pipe_conf_override,
			 unsigned int if_config_index)
{
	enum ia_css_pipe_id pipe_id;
	unsigned int thread_id;
	u8 stage_num = 0;
	struct sh_css_sp_pipeline *pipe;

	assert(out_frame);

	{
		/*
		 * Clear sh_css_sp_stage for easy debugging.
		 * program_input_circuit must be saved as it is set outside
		 * this function.
		 */
		u8 program_input_circuit;

		program_input_circuit = sh_css_sp_stage.program_input_circuit;
		memset(&sh_css_sp_stage, 0, sizeof(sh_css_sp_stage));
		sh_css_sp_stage.program_input_circuit = program_input_circuit;
	}

	pipe_id = IA_CSS_PIPE_ID_COPY;
	ia_css_pipeline_get_sp_thread_id(pipe_num, &thread_id);
	pipe = &sh_css_sp_group.pipe[thread_id];

	pipe->copy.raw.height	    = out_frame->frame_info.res.height;
	pipe->copy.raw.width	    = out_frame->frame_info.res.width;
	pipe->copy.raw.padded_width  = out_frame->frame_info.padded_width;
	pipe->copy.raw.raw_bit_depth = out_frame->frame_info.raw_bit_depth;
	pipe->copy.raw.max_input_width = max_input_width;
	pipe->num_stages = 1;
	pipe->pipe_id = pipe_id;
	/* TODO: next indicates from which queues parameters need to be
		 sampled, needs checking/improvement */
	if (pipe_conf_override == SH_CSS_PIPE_CONFIG_OVRD_NO_OVRD)
		pipe->pipe_config =
		    (SH_CSS_PIPE_CONFIG_SAMPLE_PARAMS << thread_id);
	else
		pipe->pipe_config = pipe_conf_override;

	pipe->pipe_qos_config = QOS_INVALID;

	if (pipe->inout_port_config == 0) {
		SH_CSS_PIPE_PORT_CONFIG_SET(pipe->inout_port_config,
					    (uint8_t)SH_CSS_PORT_INPUT,
					    (uint8_t)SH_CSS_HOST_TYPE, 1);
		SH_CSS_PIPE_PORT_CONFIG_SET(pipe->inout_port_config,
					    (uint8_t)SH_CSS_PORT_OUTPUT,
					    (uint8_t)SH_CSS_HOST_TYPE, 1);
	}
	IA_CSS_LOG("pipe_id %d port_config %08x",
		   pipe->pipe_id, pipe->inout_port_config);

	if (!IS_ISP2401)
		sh_css_sp_group.config.input_formatter.isp_2ppc = (uint8_t)two_ppc;

	sh_css_sp_stage.num = stage_num;
	sh_css_sp_stage.xmem_bin_addr = 0x0;
	sh_css_sp_stage.stage_type = SH_CSS_SP_STAGE_TYPE;
	sh_css_sp_stage.func = (unsigned int)IA_CSS_PIPELINE_RAW_COPY;
	sh_css_sp_stage.if_config_index = (uint8_t)if_config_index;
	set_output_frame_buffer(out_frame, 0);

	ia_css_debug_pipe_graph_dump_sp_raw_copy(out_frame);
}

static void
sh_css_sp_start_isys_copy(struct ia_css_frame *out_frame,
			  unsigned int pipe_num, unsigned int max_input_width,
			  unsigned int if_config_index)
{
	enum ia_css_pipe_id pipe_id;
	unsigned int thread_id;
	u8 stage_num = 0;
	struct sh_css_sp_pipeline *pipe;
	enum sh_css_queue_id queue_id;

	assert(out_frame);

	{
		/*
		 * Clear sh_css_sp_stage for easy debugging.
		 * program_input_circuit must be saved as it is set outside
		 * this function.
		 */
		u8 program_input_circuit;

		program_input_circuit = sh_css_sp_stage.program_input_circuit;
		memset(&sh_css_sp_stage, 0, sizeof(sh_css_sp_stage));
		sh_css_sp_stage.program_input_circuit = program_input_circuit;
	}

	pipe_id = IA_CSS_PIPE_ID_COPY;
	ia_css_pipeline_get_sp_thread_id(pipe_num, &thread_id);
	pipe = &sh_css_sp_group.pipe[thread_id];

	pipe->copy.raw.height		= out_frame->frame_info.res.height;
	pipe->copy.raw.width		= out_frame->frame_info.res.width;
	pipe->copy.raw.padded_width	= out_frame->frame_info.padded_width;
	pipe->copy.raw.raw_bit_depth	= out_frame->frame_info.raw_bit_depth;
	pipe->copy.raw.max_input_width	= max_input_width;
	pipe->num_stages		= 1;
	pipe->pipe_id			= pipe_id;
	pipe->pipe_config		= 0x0;	/* No parameters */
	pipe->pipe_qos_config		= QOS_INVALID;

	initialize_stage_frames(&sh_css_sp_stage.frames);
	sh_css_sp_stage.num = stage_num;
	sh_css_sp_stage.xmem_bin_addr = 0x0;
	sh_css_sp_stage.stage_type = SH_CSS_SP_STAGE_TYPE;
	sh_css_sp_stage.func = (unsigned int)IA_CSS_PIPELINE_ISYS_COPY;
	sh_css_sp_stage.if_config_index = (uint8_t)if_config_index;

	set_output_frame_buffer(out_frame, 0);

	if (pipe->metadata.height > 0) {
		ia_css_query_internal_queue_id(IA_CSS_BUFFER_TYPE_METADATA, thread_id,
					       &queue_id);
		sh_css_copy_buffer_attr_to_spbuffer(&sh_css_sp_stage.frames.metadata_buf,
						    queue_id, mmgr_EXCEPTION,
						    IA_CSS_BUFFER_TYPE_METADATA);
	}

	ia_css_debug_pipe_graph_dump_sp_raw_copy(out_frame);
}

unsigned int
sh_css_sp_get_binary_copy_size(void)
{
	const struct ia_css_fw_info *fw = &sh_css_sp_fw;
	unsigned int HIVE_ADDR_sp_output = fw->info.sp.output;
	unsigned int offset = (unsigned int)offsetof(struct sh_css_sp_output,
			      bin_copy_bytes_copied) / sizeof(int);
	(void)HIVE_ADDR_sp_output; /* To get rid of warning in CRUN */
	return load_sp_array_uint(sp_output, offset);
}

unsigned int
sh_css_sp_get_sw_interrupt_value(unsigned int irq)
{
	const struct ia_css_fw_info *fw = &sh_css_sp_fw;
	unsigned int HIVE_ADDR_sp_output = fw->info.sp.output;
	unsigned int offset = (unsigned int)offsetof(struct sh_css_sp_output,
			      sw_interrupt_value)
			      / sizeof(int);
	(void)HIVE_ADDR_sp_output; /* To get rid of warning in CRUN */
	return load_sp_array_uint(sp_output, offset + irq);
}

static void
sh_css_copy_buffer_attr_to_spbuffer(struct ia_css_buffer_sp *dest_buf,
				    const enum sh_css_queue_id queue_id,
				    const ia_css_ptr xmem_addr,
				    const enum ia_css_buffer_type buf_type)
{
	assert(buf_type < IA_CSS_NUM_BUFFER_TYPE);
	if (queue_id > SH_CSS_INVALID_QUEUE_ID) {
		/*
		 * value >=0 indicates that function init_frame_pointers()
		 * should use the dynamic data address
		 */
		assert(queue_id < SH_CSS_MAX_NUM_QUEUES);

		/* Klocwork assumes assert can be disabled;
		   Since we can get there with any type, and it does not
		   know that frame_in->dynamic_data_index can only be set
		   for one of the types in the assert) it has to assume we
		   can get here for any type. however this could lead to an
		   out of bounds reference when indexing buf_type about 10
		   lines below. In order to satisfy KW an additional if
		   has been added. This one will always yield true.
		 */
		if ((queue_id < SH_CSS_MAX_NUM_QUEUES)) {
			dest_buf->buf_src.queue_id = queue_id;
		}
	} else {
		assert(xmem_addr != mmgr_EXCEPTION);
		dest_buf->buf_src.xmem_addr = xmem_addr;
	}
	dest_buf->buf_type = buf_type;
}

static void
sh_css_copy_frame_to_spframe(struct ia_css_frame_sp *sp_frame_out,
			     const struct ia_css_frame *frame_in)
{
	assert(frame_in);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_copy_frame_to_spframe():\n");

	sh_css_copy_buffer_attr_to_spbuffer(&sp_frame_out->buf_attr,
					    frame_in->dynamic_queue_id,
					    frame_in->data,
					    frame_in->buf_type);

	ia_css_frame_info_to_frame_sp_info(&sp_frame_out->info, &frame_in->frame_info);

	switch (frame_in->frame_info.format) {
	case IA_CSS_FRAME_FORMAT_RAW_PACKED:
	case IA_CSS_FRAME_FORMAT_RAW:
		sp_frame_out->planes.raw.offset = frame_in->planes.raw.offset;
		break;
	case IA_CSS_FRAME_FORMAT_RGB565:
	case IA_CSS_FRAME_FORMAT_RGBA888:
		sp_frame_out->planes.rgb.offset = frame_in->planes.rgb.offset;
		break;
	case IA_CSS_FRAME_FORMAT_PLANAR_RGB888:
		sp_frame_out->planes.planar_rgb.r.offset =
		    frame_in->planes.planar_rgb.r.offset;
		sp_frame_out->planes.planar_rgb.g.offset =
		    frame_in->planes.planar_rgb.g.offset;
		sp_frame_out->planes.planar_rgb.b.offset =
		    frame_in->planes.planar_rgb.b.offset;
		break;
	case IA_CSS_FRAME_FORMAT_YUYV:
	case IA_CSS_FRAME_FORMAT_UYVY:
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_YUV420_8:
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_LEGACY_YUV420_8:
	case IA_CSS_FRAME_FORMAT_YUV_LINE:
		sp_frame_out->planes.yuyv.offset = frame_in->planes.yuyv.offset;
		break;
	case IA_CSS_FRAME_FORMAT_NV11:
	case IA_CSS_FRAME_FORMAT_NV12:
	case IA_CSS_FRAME_FORMAT_NV12_16:
	case IA_CSS_FRAME_FORMAT_NV12_TILEY:
	case IA_CSS_FRAME_FORMAT_NV21:
	case IA_CSS_FRAME_FORMAT_NV16:
	case IA_CSS_FRAME_FORMAT_NV61:
		sp_frame_out->planes.nv.y.offset =
		    frame_in->planes.nv.y.offset;
		sp_frame_out->planes.nv.uv.offset =
		    frame_in->planes.nv.uv.offset;
		break;
	case IA_CSS_FRAME_FORMAT_YUV420:
	case IA_CSS_FRAME_FORMAT_YUV422:
	case IA_CSS_FRAME_FORMAT_YUV444:
	case IA_CSS_FRAME_FORMAT_YUV420_16:
	case IA_CSS_FRAME_FORMAT_YUV422_16:
	case IA_CSS_FRAME_FORMAT_YV12:
	case IA_CSS_FRAME_FORMAT_YV16:
		sp_frame_out->planes.yuv.y.offset =
		    frame_in->planes.yuv.y.offset;
		sp_frame_out->planes.yuv.u.offset =
		    frame_in->planes.yuv.u.offset;
		sp_frame_out->planes.yuv.v.offset =
		    frame_in->planes.yuv.v.offset;
		break;
	case IA_CSS_FRAME_FORMAT_QPLANE6:
		sp_frame_out->planes.plane6.r.offset =
		    frame_in->planes.plane6.r.offset;
		sp_frame_out->planes.plane6.r_at_b.offset =
		    frame_in->planes.plane6.r_at_b.offset;
		sp_frame_out->planes.plane6.gr.offset =
		    frame_in->planes.plane6.gr.offset;
		sp_frame_out->planes.plane6.gb.offset =
		    frame_in->planes.plane6.gb.offset;
		sp_frame_out->planes.plane6.b.offset =
		    frame_in->planes.plane6.b.offset;
		sp_frame_out->planes.plane6.b_at_r.offset =
		    frame_in->planes.plane6.b_at_r.offset;
		break;
	case IA_CSS_FRAME_FORMAT_BINARY_8:
		sp_frame_out->planes.binary.data.offset =
		    frame_in->planes.binary.data.offset;
		break;
	default:
		/* This should not happen, but in case it does,
		 * nullify the planes
		 */
		memset(&sp_frame_out->planes, 0, sizeof(sp_frame_out->planes));
		break;
	}
}

static int
set_input_frame_buffer(const struct ia_css_frame *frame)
{
	if (!frame)
		return -EINVAL;

	switch (frame->frame_info.format) {
	case IA_CSS_FRAME_FORMAT_QPLANE6:
	case IA_CSS_FRAME_FORMAT_YUV420_16:
	case IA_CSS_FRAME_FORMAT_RAW_PACKED:
	case IA_CSS_FRAME_FORMAT_RAW:
	case IA_CSS_FRAME_FORMAT_YUV420:
	case IA_CSS_FRAME_FORMAT_YUYV:
	case IA_CSS_FRAME_FORMAT_YUV_LINE:
	case IA_CSS_FRAME_FORMAT_NV12:
	case IA_CSS_FRAME_FORMAT_NV12_16:
	case IA_CSS_FRAME_FORMAT_NV12_TILEY:
	case IA_CSS_FRAME_FORMAT_NV21:
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_YUV420_8:
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_LEGACY_YUV420_8:
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_YUV420_10:
		break;
	default:
		return -EINVAL;
	}
	sh_css_copy_frame_to_spframe(&sh_css_sp_stage.frames.in, frame);

	return 0;
}

static int
set_output_frame_buffer(const struct ia_css_frame *frame,
			unsigned int idx)
{
	if (!frame)
		return -EINVAL;

	switch (frame->frame_info.format) {
	case IA_CSS_FRAME_FORMAT_YUV420:
	case IA_CSS_FRAME_FORMAT_YUV422:
	case IA_CSS_FRAME_FORMAT_YUV444:
	case IA_CSS_FRAME_FORMAT_YV12:
	case IA_CSS_FRAME_FORMAT_YV16:
	case IA_CSS_FRAME_FORMAT_YUV420_16:
	case IA_CSS_FRAME_FORMAT_YUV422_16:
	case IA_CSS_FRAME_FORMAT_NV11:
	case IA_CSS_FRAME_FORMAT_NV12:
	case IA_CSS_FRAME_FORMAT_NV12_16:
	case IA_CSS_FRAME_FORMAT_NV12_TILEY:
	case IA_CSS_FRAME_FORMAT_NV16:
	case IA_CSS_FRAME_FORMAT_NV21:
	case IA_CSS_FRAME_FORMAT_NV61:
	case IA_CSS_FRAME_FORMAT_YUYV:
	case IA_CSS_FRAME_FORMAT_UYVY:
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_YUV420_8:
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_LEGACY_YUV420_8:
	case IA_CSS_FRAME_FORMAT_YUV_LINE:
	case IA_CSS_FRAME_FORMAT_RGB565:
	case IA_CSS_FRAME_FORMAT_RGBA888:
	case IA_CSS_FRAME_FORMAT_PLANAR_RGB888:
	case IA_CSS_FRAME_FORMAT_RAW:
	case IA_CSS_FRAME_FORMAT_RAW_PACKED:
	case IA_CSS_FRAME_FORMAT_QPLANE6:
	case IA_CSS_FRAME_FORMAT_BINARY_8:
		break;
	default:
		return -EINVAL;
	}
	sh_css_copy_frame_to_spframe(&sh_css_sp_stage.frames.out[idx], frame);
	return 0;
}

static int
set_view_finder_buffer(const struct ia_css_frame *frame)
{
	if (!frame)
		return -EINVAL;

	switch (frame->frame_info.format) {
	/* the dual output pin */
	case IA_CSS_FRAME_FORMAT_NV12:
	case IA_CSS_FRAME_FORMAT_NV12_16:
	case IA_CSS_FRAME_FORMAT_NV21:
	case IA_CSS_FRAME_FORMAT_YUYV:
	case IA_CSS_FRAME_FORMAT_UYVY:
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_YUV420_8:
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_LEGACY_YUV420_8:
	case IA_CSS_FRAME_FORMAT_YUV420:
	case IA_CSS_FRAME_FORMAT_YV12:
	case IA_CSS_FRAME_FORMAT_NV12_TILEY:

	/* for vf_veceven */
	case IA_CSS_FRAME_FORMAT_YUV_LINE:
		break;
	default:
		return -EINVAL;
	}

	sh_css_copy_frame_to_spframe(&sh_css_sp_stage.frames.out_vf, frame);
	return 0;
}

void sh_css_sp_set_if_configs(
    const input_formatter_cfg_t	*config_a,
    const input_formatter_cfg_t	*config_b,
    const uint8_t		if_config_index
)
{
	assert(if_config_index < SH_CSS_MAX_IF_CONFIGS);
	assert(config_a);

	sh_css_sp_group.config.input_formatter.set[if_config_index].config_a =
	    *config_a;
	sh_css_sp_group.config.input_formatter.a_changed = true;

	if (config_b) {
		sh_css_sp_group.config.input_formatter.set[if_config_index].config_b =
		    *config_b;
		sh_css_sp_group.config.input_formatter.b_changed = true;
	}

	return;
}

void
sh_css_sp_program_input_circuit(int fmt_type,
				int ch_id,
				enum ia_css_input_mode input_mode)
{
	sh_css_sp_group.config.input_circuit.no_side_band = false;
	sh_css_sp_group.config.input_circuit.fmt_type     = fmt_type;
	sh_css_sp_group.config.input_circuit.ch_id	      = ch_id;
	sh_css_sp_group.config.input_circuit.input_mode   = input_mode;
	/*
	 * The SP group is only loaded at SP boot time and is read once
	 * change flags as "input_circuit_cfg_changed" must be reset on the SP
	 */
	sh_css_sp_group.config.input_circuit_cfg_changed = true;
	sh_css_sp_stage.program_input_circuit = true;
}

void
sh_css_sp_configure_sync_gen(int width, int height,
			     int hblank_cycles,
			     int vblank_cycles)
{
	sh_css_sp_group.config.sync_gen.width	       = width;
	sh_css_sp_group.config.sync_gen.height	       = height;
	sh_css_sp_group.config.sync_gen.hblank_cycles = hblank_cycles;
	sh_css_sp_group.config.sync_gen.vblank_cycles = vblank_cycles;
}

void
sh_css_sp_configure_tpg(int x_mask,
			int y_mask,
			int x_delta,
			int y_delta,
			int xy_mask)
{
	sh_css_sp_group.config.tpg.x_mask  = x_mask;
	sh_css_sp_group.config.tpg.y_mask  = y_mask;
	sh_css_sp_group.config.tpg.x_delta = x_delta;
	sh_css_sp_group.config.tpg.y_delta = y_delta;
	sh_css_sp_group.config.tpg.xy_mask = xy_mask;
}

void
sh_css_sp_configure_prbs(int seed)
{
	sh_css_sp_group.config.prbs.seed = seed;
}

void
sh_css_sp_configure_enable_raw_pool_locking(bool lock_all)
{
	sh_css_sp_group.config.enable_raw_pool_locking = true;
	sh_css_sp_group.config.lock_all = lock_all;
}

void
sh_css_sp_enable_isys_event_queue(bool enable)
{
	sh_css_sp_group.config.enable_isys_event_queue = enable;
}

void
sh_css_sp_set_disable_continuous_viewfinder(bool flag)
{
	sh_css_sp_group.config.disable_cont_vf = flag;
}

static int
sh_css_sp_write_frame_pointers(const struct sh_css_binary_args *args)
{
	int err = 0;
	int i;

	assert(args);

	if (args->in_frame)
		err = set_input_frame_buffer(args->in_frame);
	if (!err && args->out_vf_frame)
		err = set_view_finder_buffer(args->out_vf_frame);
	for (i = 0; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++) {
		if (!err && args->out_frame[i])
			err = set_output_frame_buffer(args->out_frame[i], i);
	}

	/* we don't pass this error back to the upper layer, so we add a assert here
	   because we actually hit the error here but it still works by accident... */
	if (err) assert(false);
	return err;
}

static void
sh_css_sp_init_group(bool two_ppc,
		     enum atomisp_input_format input_format,
		     bool no_isp_sync,
		     uint8_t if_config_index)
{
	if (!IS_ISP2401)
		sh_css_sp_group.config.input_formatter.isp_2ppc = two_ppc;

	sh_css_sp_group.config.no_isp_sync = (uint8_t)no_isp_sync;
	/* decide whether the frame is processed online or offline */
	if (if_config_index == SH_CSS_IF_CONFIG_NOT_NEEDED) return;

	if (!IS_ISP2401) {
		assert(if_config_index < SH_CSS_MAX_IF_CONFIGS);
		sh_css_sp_group.config.input_formatter.set[if_config_index].stream_format =
		    input_format;
	}
}

void
sh_css_stage_write_binary_info(struct ia_css_binary_info *info)
{
	assert(info);
	sh_css_isp_stage.binary_info = *info;
}

static int
copy_isp_mem_if_to_ddr(struct ia_css_binary *binary)
{
	int err;

	err = ia_css_isp_param_copy_isp_mem_if_to_ddr(
	    &binary->css_params,
	    &binary->mem_params,
	    IA_CSS_PARAM_CLASS_CONFIG);
	if (err)
		return err;
	err = ia_css_isp_param_copy_isp_mem_if_to_ddr(
	    &binary->css_params,
	    &binary->mem_params,
	    IA_CSS_PARAM_CLASS_STATE);
	if (err)
		return err;
	return 0;
}

static bool
is_sp_stage(struct ia_css_pipeline_stage *stage)
{
	assert(stage);
	return stage->sp_func != IA_CSS_PIPELINE_NO_FUNC;
}

static int configure_isp_from_args(const struct sh_css_sp_pipeline *pipeline,
				   const struct ia_css_binary      *binary,
				   const struct sh_css_binary_args *args,
				   bool				   two_ppc,
				   bool				   deinterleaved)
{
	int ret;

	ret = ia_css_fpn_configure(binary,  &binary->in_frame_info);
	if (ret)
		return ret;
	ret = ia_css_crop_configure(binary, ia_css_frame_get_info(args->delay_frames[0]));
	if (ret)
		return ret;
	ret = ia_css_qplane_configure(pipeline, binary, &binary->in_frame_info);
	if (ret)
		return ret;
	ret = ia_css_output0_configure(binary, ia_css_frame_get_info(args->out_frame[0]));
	if (ret)
		return ret;
	ret = ia_css_output1_configure(binary, ia_css_frame_get_info(args->out_vf_frame));
	if (ret)
		return ret;
	ret = ia_css_copy_output_configure(binary, args->copy_output);
	if (ret)
		return ret;
	ret = ia_css_output0_configure(binary, ia_css_frame_get_info(args->out_frame[0]));
	if (ret)
		return ret;
	ret = ia_css_iterator_configure(binary, ia_css_frame_get_info(args->in_frame));
	if (ret)
		return ret;
	ret = ia_css_dvs_configure(binary, ia_css_frame_get_info(args->out_frame[0]));
	if (ret)
		return ret;
	ret = ia_css_output_configure(binary, ia_css_frame_get_info(args->out_frame[0]));
	if (ret)
		return ret;
	ret = ia_css_raw_configure(pipeline, binary, ia_css_frame_get_info(args->in_frame),
				   &binary->in_frame_info, two_ppc, deinterleaved);
	if (ret)
		return ret;

	/*
	 * FIXME: args->delay_frames can be NULL here
	 *
	 * Somehow, the driver at the Intel Atom Yocto tree doesn't seem to
	 * suffer from the same issue.
	 *
	 * Anyway, the function below should now handle a NULL delay_frames
	 * without crashing, but the pipeline should likely be built without
	 * adding it at the first place (or there are a hidden bug somewhere)
	 */
	ret = ia_css_ref_configure(binary, args->delay_frames, pipeline->dvs_frame_delay);
	if (ret)
		return ret;
	ret = ia_css_tnr_configure(binary, args->tnr_frames);
	if (ret)
		return ret;
	return ia_css_bayer_io_config(binary, args);
}

static void
initialize_isp_states(const struct ia_css_binary *binary)
{
	unsigned int i;

	if (!binary->info->mem_offsets.offsets.state)
		return;
	for (i = 0; i < IA_CSS_NUM_STATE_IDS; i++) {
		ia_css_kernel_init_state[i](binary);
	}
}

static void
initialize_frame_buffer_attribute(struct ia_css_buffer_sp *buf_attr)
{
	buf_attr->buf_src.queue_id = SH_CSS_INVALID_QUEUE_ID;
	buf_attr->buf_type = IA_CSS_BUFFER_TYPE_INVALID;
}

static void
initialize_stage_frames(struct ia_css_frames_sp *frames)
{
	unsigned int i;

	initialize_frame_buffer_attribute(&frames->in.buf_attr);
	for (i = 0; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++) {
		initialize_frame_buffer_attribute(&frames->out[i].buf_attr);
	}
	initialize_frame_buffer_attribute(&frames->out_vf.buf_attr);
	initialize_frame_buffer_attribute(&frames->s3a_buf);
	initialize_frame_buffer_attribute(&frames->dvs_buf);
	initialize_frame_buffer_attribute(&frames->metadata_buf);
}

static int
sh_css_sp_init_stage(struct ia_css_binary *binary,
		     const char *binary_name,
		     const struct ia_css_blob_info *blob_info,
		     const struct sh_css_binary_args *args,
		     unsigned int pipe_num,
		     unsigned int stage,
		     bool xnr,
		     const struct ia_css_isp_param_css_segments *isp_mem_if,
		     unsigned int if_config_index,
		     bool two_ppc)
{
	const struct ia_css_binary_xinfo *xinfo;
	const struct ia_css_binary_info  *info;
	int err = 0;
	int i;
	struct ia_css_pipe *pipe = NULL;
	unsigned int thread_id;
	enum sh_css_queue_id queue_id;
	bool continuous = sh_css_continuous_is_enabled((uint8_t)pipe_num);

	assert(binary);
	assert(blob_info);
	assert(args);
	assert(isp_mem_if);

	xinfo = binary->info;
	info  = &xinfo->sp;
	{
		/*
		 * Clear sh_css_sp_stage for easy debugging.
		 * program_input_circuit must be saved as it is set outside
		 * this function.
		 */
		u8 program_input_circuit;

		program_input_circuit = sh_css_sp_stage.program_input_circuit;
		memset(&sh_css_sp_stage, 0, sizeof(sh_css_sp_stage));
		sh_css_sp_stage.program_input_circuit = (uint8_t)program_input_circuit;
	}

	ia_css_pipeline_get_sp_thread_id(pipe_num, &thread_id);

	if (!info) {
		sh_css_sp_group.pipe[thread_id].sp_stage_addr[stage] = mmgr_NULL;
		return 0;
	}

	if (IS_ISP2401)
		sh_css_sp_stage.deinterleaved = 0;
	else
		sh_css_sp_stage.deinterleaved = ((stage == 0) && continuous);

	initialize_stage_frames(&sh_css_sp_stage.frames);
	/*
	 * TODO: Make the Host dynamically determine
	 * the stage type.
	 */
	sh_css_sp_stage.stage_type = SH_CSS_ISP_STAGE_TYPE;
	sh_css_sp_stage.num		= (uint8_t)stage;
	sh_css_sp_stage.isp_online	= (uint8_t)binary->online;
	sh_css_sp_stage.isp_copy_vf     = (uint8_t)args->copy_vf;
	sh_css_sp_stage.isp_copy_output = (uint8_t)args->copy_output;
	sh_css_sp_stage.enable.vf_output = (args->out_vf_frame != NULL);

	/* Copy the frame infos first, to be overwritten by the frames,
	   if these are present.
	*/
	sh_css_sp_stage.frames.effective_in_res.width = binary->effective_in_frame_res.width;
	sh_css_sp_stage.frames.effective_in_res.height = binary->effective_in_frame_res.height;

	ia_css_frame_info_to_frame_sp_info(&sh_css_sp_stage.frames.in.info,
					   &binary->in_frame_info);
	for (i = 0; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++) {
		ia_css_frame_info_to_frame_sp_info(&sh_css_sp_stage.frames.out[i].info,
						   &binary->out_frame_info[i]);
	}
	ia_css_frame_info_to_frame_sp_info(&sh_css_sp_stage.frames.internal_frame_info,
					   &binary->internal_frame_info);
	sh_css_sp_stage.dvs_envelope.width    = binary->dvs_envelope.width;
	sh_css_sp_stage.dvs_envelope.height   = binary->dvs_envelope.height;
	sh_css_sp_stage.isp_pipe_version      = (uint8_t)info->pipeline.isp_pipe_version;
	sh_css_sp_stage.isp_deci_log_factor   = (uint8_t)binary->deci_factor_log2;
	sh_css_sp_stage.isp_vf_downscale_bits = (uint8_t)binary->vf_downscale_log2;

	sh_css_sp_stage.if_config_index = (uint8_t)if_config_index;

	sh_css_sp_stage.sp_enable_xnr = (uint8_t)xnr;
	sh_css_sp_stage.xmem_bin_addr = xinfo->xmem_addr;
	sh_css_sp_stage.xmem_map_addr = sh_css_params_ddr_address_map();
	sh_css_isp_stage.blob_info = *blob_info;
	sh_css_stage_write_binary_info((struct ia_css_binary_info *)info);

	/* Make sure binary name is smaller than allowed string size */
	assert(strlen(binary_name) < SH_CSS_MAX_BINARY_NAME - 1);
	strscpy(sh_css_isp_stage.binary_name, binary_name, SH_CSS_MAX_BINARY_NAME);
	sh_css_isp_stage.mem_initializers = *isp_mem_if;

	/*
	 * Even when a stage does not need uds and does not params,
	 * ia_css_uds_sp_scale_params() seems to be called (needs
	 * further investigation). This function can not deal with
	 * dx, dy = {0, 0}
	 */

	err = sh_css_sp_write_frame_pointers(args);
	/* TODO: move it to a better place */
	if (binary->info->sp.enable.s3a) {
		ia_css_query_internal_queue_id(IA_CSS_BUFFER_TYPE_3A_STATISTICS, thread_id,
					       &queue_id);
		sh_css_copy_buffer_attr_to_spbuffer(&sh_css_sp_stage.frames.s3a_buf, queue_id,
						    mmgr_EXCEPTION,
						    IA_CSS_BUFFER_TYPE_3A_STATISTICS);
	}
	if (binary->info->sp.enable.dis) {
		ia_css_query_internal_queue_id(IA_CSS_BUFFER_TYPE_DIS_STATISTICS, thread_id,
					       &queue_id);
		sh_css_copy_buffer_attr_to_spbuffer(&sh_css_sp_stage.frames.dvs_buf, queue_id,
						    mmgr_EXCEPTION,
						    IA_CSS_BUFFER_TYPE_DIS_STATISTICS);
	}
	ia_css_query_internal_queue_id(IA_CSS_BUFFER_TYPE_METADATA, thread_id, &queue_id);
	sh_css_copy_buffer_attr_to_spbuffer(&sh_css_sp_stage.frames.metadata_buf, queue_id, mmgr_EXCEPTION, IA_CSS_BUFFER_TYPE_METADATA);
	if (err)
		return err;

	if (IS_ISP2401) {
		pipe = find_pipe_by_num(sh_css_sp_group.pipe[thread_id].pipe_num);
		if (!pipe)
			return -EINVAL;

		if (args->in_frame)
			ia_css_get_crop_offsets(pipe, &args->in_frame->frame_info);
		else
			ia_css_get_crop_offsets(pipe, &binary->in_frame_info);
	}

	err = configure_isp_from_args(&sh_css_sp_group.pipe[thread_id],
				      binary, args, two_ppc, sh_css_sp_stage.deinterleaved);
	if (err)
		return err;

	initialize_isp_states(binary);

	/* we do this only for preview pipe because in fill_binary_info function
	 * we assign vf_out res to out res, but for ISP internal processing, we need
	 * the original out res. for video pipe, it has two output pins --- out and
	 * vf_out, so it can keep these two resolutions already. */
	if (binary->info->sp.pipeline.mode == IA_CSS_BINARY_MODE_PREVIEW &&
	    (binary->vf_downscale_log2 > 0)) {
		/* TODO: Remove this after preview output decimation is fixed
		 * by configuring out&vf info fiels properly */
		sh_css_sp_stage.frames.out[0].info.padded_width
		<<= binary->vf_downscale_log2;
		sh_css_sp_stage.frames.out[0].info.res.width
		<<= binary->vf_downscale_log2;
		sh_css_sp_stage.frames.out[0].info.res.height
		<<= binary->vf_downscale_log2;
	}
	err = copy_isp_mem_if_to_ddr(binary);
	if (err)
		return err;

	return 0;
}

static int
sp_init_stage(struct ia_css_pipeline_stage *stage,
	      unsigned int pipe_num,
	      bool xnr,
	      unsigned int if_config_index,
	      bool two_ppc)
{
	struct ia_css_binary *binary;
	const struct ia_css_fw_info *firmware;
	const struct sh_css_binary_args *args;
	unsigned int stage_num;
	/*
	 * Initialiser required because of the "else" path below.
	 * Is this a valid path ?
	 */
	const char *binary_name = "";
	const struct ia_css_binary_xinfo *info = NULL;
	/* note: the var below is made static as it is quite large;
	   if it is not static it ends up on the stack which could
	   cause issues for drivers
	*/
	static struct ia_css_binary tmp_binary;
	const struct ia_css_blob_info *blob_info = NULL;
	struct ia_css_isp_param_css_segments isp_mem_if;
	/* LA: should be ia_css_data, should not contain host pointer.
	   However, CSS/DDR pointer is not available yet.
	   Hack is to store it in params->ddr_ptrs and then copy it late in the SP just before vmem init.
	   TODO: Call this after CSS/DDR allocation and store that pointer.
	   Best is to allocate it at stage creation time together with host pointer.
	   Remove vmem from params.
	*/
	struct ia_css_isp_param_css_segments *mem_if = &isp_mem_if;

	int err = 0;

	assert(stage);

	binary = stage->binary;
	firmware = stage->firmware;
	args = &stage->args;
	stage_num = stage->stage_num;

	if (binary) {
		info = binary->info;
		binary_name = (const char *)(info->blob->name);
		blob_info = &info->blob->header.blob;
		ia_css_init_memory_interface(mem_if, &binary->mem_params, &binary->css_params);
	} else if (firmware) {
		const struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS] = {NULL};

		if (args->out_frame[0])
			out_infos[0] = &args->out_frame[0]->frame_info;
		info = &firmware->info.isp;
		ia_css_binary_fill_info(info, false, false,
					ATOMISP_INPUT_FORMAT_RAW_10,
					ia_css_frame_get_info(args->in_frame),
					NULL,
					out_infos,
					ia_css_frame_get_info(args->out_vf_frame),
					&tmp_binary,
					NULL,
					-1, true);
		binary = &tmp_binary;
		binary->info = info;
		binary_name = IA_CSS_EXT_ISP_PROG_NAME(firmware);
		blob_info = &firmware->blob;
		mem_if = (struct ia_css_isp_param_css_segments *)&firmware->mem_initializers;
	} else {
		/* SP stage */
		assert(stage->sp_func != IA_CSS_PIPELINE_NO_FUNC);
		/* binary and blob_info are now NULL.
		   These will be passed to sh_css_sp_init_stage
		   and dereferenced there, so passing a NULL
		   pointer is no good. return an error */
		return -EINVAL;
	}

	err = sh_css_sp_init_stage(binary,
				   (const char *)binary_name,
				   blob_info,
				   args,
				   pipe_num,
				   stage_num,
				   xnr,
				   mem_if,
				   if_config_index,
				   two_ppc);
	return err;
}

static void
sp_init_sp_stage(struct ia_css_pipeline_stage *stage,
		 unsigned int pipe_num,
		 bool two_ppc,
		 enum sh_css_pipe_config_override copy_ovrd,
		 unsigned int if_config_index)
{
	const struct sh_css_binary_args *args = &stage->args;

	assert(stage);
	switch (stage->sp_func) {
	case IA_CSS_PIPELINE_RAW_COPY:
		sh_css_sp_start_raw_copy(args->out_frame[0],
					 pipe_num, two_ppc,
					 stage->max_input_width,
					 copy_ovrd, if_config_index);
		break;
	case IA_CSS_PIPELINE_BIN_COPY:
		assert(false); /* TBI */
		break;
	case IA_CSS_PIPELINE_ISYS_COPY:
		sh_css_sp_start_isys_copy(args->out_frame[0],
					  pipe_num, stage->max_input_width, if_config_index);
		break;
	case IA_CSS_PIPELINE_NO_FUNC:
		assert(false);
		break;
	}
}

void
sh_css_sp_init_pipeline(struct ia_css_pipeline *me,
			enum ia_css_pipe_id id,
			u8 pipe_num,
			bool xnr,
			bool two_ppc,
			bool continuous,
			bool offline,
			unsigned int required_bds_factor,
			enum sh_css_pipe_config_override copy_ovrd,
			enum ia_css_input_mode input_mode,
			const struct ia_css_metadata_config *md_config,
			const struct ia_css_metadata_info *md_info,
			const enum mipi_port_id port_id)
{
	/* Get first stage */
	struct ia_css_pipeline_stage *stage        = NULL;
	struct ia_css_binary	     *first_binary = NULL;
	struct ia_css_pipe *pipe = NULL;
	unsigned int num;
	enum ia_css_pipe_id pipe_id = id;
	unsigned int thread_id;
	u8 if_config_index, tmp_if_config_index;

	if (!me->stages) {
		dev_err(atomisp_dev, "%s called on a pipeline without stages\n",
			__func__);
		return; /* FIXME should be able to return an error */
	}

	first_binary = me->stages->binary;

	if (input_mode == IA_CSS_INPUT_MODE_SENSOR ||
	    input_mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR) {
		assert(port_id < N_MIPI_PORT_ID);
		if (port_id >= N_MIPI_PORT_ID) /* should not happen but KW does not know */
			return; /* we should be able to return an error */
		if_config_index  = (uint8_t)(port_id - MIPI_PORT0_ID);
	} else if (input_mode == IA_CSS_INPUT_MODE_MEMORY) {
		if_config_index = SH_CSS_IF_CONFIG_NOT_NEEDED;
	} else {
		if_config_index = 0x0;
	}

	ia_css_pipeline_get_sp_thread_id(pipe_num, &thread_id);
	memset(&sh_css_sp_group.pipe[thread_id], 0, sizeof(struct sh_css_sp_pipeline));

	/* Count stages */
	for (stage = me->stages, num = 0; stage; stage = stage->next, num++) {
		stage->stage_num = num;
		ia_css_debug_pipe_graph_dump_stage(stage, id);
	}
	me->num_stages = num;

	if (first_binary) {
		/* Init pipeline data */
		sh_css_sp_init_group(two_ppc, first_binary->input_format,
				     offline, if_config_index);
	} /* if (first_binary != NULL) */

	/* Signal the host immediately after start for SP_ISYS_COPY only */
	if (me->num_stages == 1 &&
	    me->stages->sp_func == IA_CSS_PIPELINE_ISYS_COPY)
		sh_css_sp_group.config.no_isp_sync = true;

	/* Init stage data */
	sh_css_init_host2sp_frame_data();

	sh_css_sp_group.pipe[thread_id].num_stages = 0;
	sh_css_sp_group.pipe[thread_id].pipe_id = pipe_id;
	sh_css_sp_group.pipe[thread_id].thread_id = thread_id;
	sh_css_sp_group.pipe[thread_id].pipe_num = pipe_num;
	sh_css_sp_group.pipe[thread_id].num_execs = me->num_execs;
	sh_css_sp_group.pipe[thread_id].pipe_qos_config = QOS_INVALID;
	sh_css_sp_group.pipe[thread_id].required_bds_factor = required_bds_factor;
	sh_css_sp_group.pipe[thread_id].input_system_mode
	= (uint32_t)input_mode;
	sh_css_sp_group.pipe[thread_id].port_id = port_id;
	sh_css_sp_group.pipe[thread_id].dvs_frame_delay = (uint32_t)me->dvs_frame_delay;

	/* TODO: next indicates from which queues parameters need to be
		 sampled, needs checking/improvement */
	if (ia_css_pipeline_uses_params(me)) {
		sh_css_sp_group.pipe[thread_id].pipe_config =
		SH_CSS_PIPE_CONFIG_SAMPLE_PARAMS << thread_id;
	}

	/* For continuous use-cases, SP copy is responsible for sampling the
	 * parameters */
	if (continuous)
		sh_css_sp_group.pipe[thread_id].pipe_config = 0;

	sh_css_sp_group.pipe[thread_id].inout_port_config = me->inout_port_config;

	pipe = find_pipe_by_num(pipe_num);
	assert(pipe);
	if (!pipe) {
		return;
	}
	sh_css_sp_group.pipe[thread_id].scaler_pp_lut = sh_css_pipe_get_pp_gdc_lut(pipe);

	if (md_info && md_info->size > 0) {
		sh_css_sp_group.pipe[thread_id].metadata.width  = md_info->resolution.width;
		sh_css_sp_group.pipe[thread_id].metadata.height = md_info->resolution.height;
		sh_css_sp_group.pipe[thread_id].metadata.stride = md_info->stride;
		sh_css_sp_group.pipe[thread_id].metadata.size   = md_info->size;
		ia_css_isys_convert_stream_format_to_mipi_format(
		    md_config->data_type, MIPI_PREDICTOR_NONE,
		    &sh_css_sp_group.pipe[thread_id].metadata.format);
	}

	sh_css_sp_group.pipe[thread_id].output_frame_queue_id = (uint32_t)SH_CSS_INVALID_QUEUE_ID;
	if (pipe_id != IA_CSS_PIPE_ID_COPY) {
		ia_css_query_internal_queue_id(IA_CSS_BUFFER_TYPE_OUTPUT_FRAME, thread_id,
					       (enum sh_css_queue_id *)(
						   &sh_css_sp_group.pipe[thread_id].output_frame_queue_id));
	}

	IA_CSS_LOG("pipe_id %d port_config %08x",
		   pipe_id, sh_css_sp_group.pipe[thread_id].inout_port_config);

	for (stage = me->stages, num = 0; stage; stage = stage->next, num++) {
		sh_css_sp_group.pipe[thread_id].num_stages++;
		if (is_sp_stage(stage)) {
			sp_init_sp_stage(stage, pipe_num, two_ppc,
					 copy_ovrd, if_config_index);
		} else {
			if ((stage->stage_num != 0) ||
			    SH_CSS_PIPE_PORT_CONFIG_IS_CONTINUOUS(me->inout_port_config))
				tmp_if_config_index = SH_CSS_IF_CONFIG_NOT_NEEDED;
			else
				tmp_if_config_index = if_config_index;
			sp_init_stage(stage, pipe_num,
				      xnr, tmp_if_config_index, two_ppc);
		}

		store_sp_stage_data(pipe_id, pipe_num, num);
	}
	sh_css_sp_group.pipe[thread_id].pipe_config |= (uint32_t)
		(me->acquire_isp_each_stage << IA_CSS_ACQUIRE_ISP_POS);
	store_sp_group_data();
}

void
sh_css_sp_uninit_pipeline(unsigned int pipe_num)
{
	unsigned int thread_id;

	ia_css_pipeline_get_sp_thread_id(pipe_num, &thread_id);
	/*memset(&sh_css_sp_group.pipe[thread_id], 0, sizeof(struct sh_css_sp_pipeline));*/
	sh_css_sp_group.pipe[thread_id].num_stages = 0;
}

bool sh_css_write_host2sp_command(enum host2sp_commands host2sp_command)
{
	unsigned int HIVE_ADDR_host_sp_com = sh_css_sp_fw.info.sp.host_sp_com;
	unsigned int offset = (unsigned int)offsetof(struct host_sp_communication,
			      host2sp_command)
			      / sizeof(int);
	enum host2sp_commands last_cmd = host2sp_cmd_error;
	(void)HIVE_ADDR_host_sp_com; /* Suppres warnings in CRUN */

	/* Previous command must be handled by SP (by design) */
	last_cmd = load_sp_array_uint(host_sp_com, offset);
	if (last_cmd != host2sp_cmd_ready)
		IA_CSS_ERROR("last host command not handled by SP(%d)", last_cmd);

	store_sp_array_uint(host_sp_com, offset, host2sp_command);

	return (last_cmd == host2sp_cmd_ready);
}

enum host2sp_commands
sh_css_read_host2sp_command(void)
{
	unsigned int HIVE_ADDR_host_sp_com = sh_css_sp_fw.info.sp.host_sp_com;
	unsigned int offset = (unsigned int)offsetof(struct host_sp_communication, host2sp_command)
	/ sizeof(int);
	(void)HIVE_ADDR_host_sp_com; /* Suppres warnings in CRUN */
	return (enum host2sp_commands)load_sp_array_uint(host_sp_com, offset);
}

/*
 * Frame data is no longer part of the sp_stage structure but part of a
 * separate structure. The aim is to make the sp_data struct static
 * (it defines a pipeline) and that the dynamic (per frame) data is stored
 * separetly.
 *
 * This function must be called first every where were you start constructing
 * a new pipeline by defining one or more stages with use of variable
 * sh_css_sp_stage. Even the special cases like accelerator and copy_frame
 * These have a pipeline of just 1 stage.
 */
void
sh_css_init_host2sp_frame_data(void)
{
	/* Clean table */
	unsigned int HIVE_ADDR_host_sp_com = sh_css_sp_fw.info.sp.host_sp_com;

	(void)HIVE_ADDR_host_sp_com; /* Suppres warnings in CRUN */
	/*
	 * rvanimme: don't clean it to save static frame info line ref_in
	 * ref_out, and tnr_frames. Once this static data is in a
	 * separate data struct, this may be enable (but still, there is
	 * no need for it)
	 */
}

/*
 * @brief Update the offline frame information in host_sp_communication.
 * Refer to "sh_css_sp.h" for more details.
 */
void
sh_css_update_host2sp_offline_frame(
    unsigned int frame_num,
    struct ia_css_frame *frame,
    struct ia_css_metadata *metadata)
{
	unsigned int HIVE_ADDR_host_sp_com;
	unsigned int offset;

	assert(frame_num < NUM_CONTINUOUS_FRAMES);

	/* Write new frame data into SP DMEM */
	HIVE_ADDR_host_sp_com = sh_css_sp_fw.info.sp.host_sp_com;
	offset = (unsigned int)offsetof(struct host_sp_communication,
					host2sp_offline_frames)
		 / sizeof(int);
	offset += frame_num;
	store_sp_array_uint(host_sp_com, offset, frame ? frame->data : 0);

	/* Write metadata buffer into SP DMEM */
	offset = (unsigned int)offsetof(struct host_sp_communication,
					host2sp_offline_metadata)
		 / sizeof(int);
	offset += frame_num;
	store_sp_array_uint(host_sp_com, offset, metadata ? metadata->address : 0);
}

/*
 * @brief Update the mipi frame information in host_sp_communication.
 * Refer to "sh_css_sp.h" for more details.
 */
void
sh_css_update_host2sp_mipi_frame(
    unsigned int frame_num,
    struct ia_css_frame *frame)
{
	unsigned int HIVE_ADDR_host_sp_com;
	unsigned int offset;

	/* MIPI buffers are dedicated to port, so now there are more of them. */
	assert(frame_num < (N_CSI_PORTS * NUM_MIPI_FRAMES_PER_STREAM));

	/* Write new frame data into SP DMEM */
	HIVE_ADDR_host_sp_com = sh_css_sp_fw.info.sp.host_sp_com;
	offset = (unsigned int)offsetof(struct host_sp_communication,
					host2sp_mipi_frames)
		 / sizeof(int);
	offset += frame_num;

	store_sp_array_uint(host_sp_com, offset,
			    frame ? frame->data : 0);
}

/*
 * @brief Update the mipi metadata information in host_sp_communication.
 * Refer to "sh_css_sp.h" for more details.
 */
void
sh_css_update_host2sp_mipi_metadata(
    unsigned int frame_num,
    struct ia_css_metadata *metadata)
{
	unsigned int HIVE_ADDR_host_sp_com;
	unsigned int o;

	/* MIPI buffers are dedicated to port, so now there are more of them. */
	assert(frame_num < (N_CSI_PORTS * NUM_MIPI_FRAMES_PER_STREAM));

	/* Write new frame data into SP DMEM */
	HIVE_ADDR_host_sp_com = sh_css_sp_fw.info.sp.host_sp_com;
	o = offsetof(struct host_sp_communication, host2sp_mipi_metadata)
	    / sizeof(int);
	o += frame_num;
	store_sp_array_uint(host_sp_com, o,
			    metadata ? metadata->address : 0);
}

void
sh_css_update_host2sp_num_mipi_frames(unsigned int num_frames)
{
	unsigned int HIVE_ADDR_host_sp_com;
	unsigned int offset;

	/* Write new frame data into SP DMEM */
	HIVE_ADDR_host_sp_com = sh_css_sp_fw.info.sp.host_sp_com;
	offset = (unsigned int)offsetof(struct host_sp_communication,
					host2sp_num_mipi_frames)
		 / sizeof(int);

	store_sp_array_uint(host_sp_com, offset, num_frames);
}

void
sh_css_update_host2sp_cont_num_raw_frames(unsigned int num_frames,
	bool set_avail)
{
	const struct ia_css_fw_info *fw;
	unsigned int HIVE_ADDR_host_sp_com;
	unsigned int extra_num_frames, avail_num_frames;
	unsigned int offset, offset_extra;

	/* Write new frame data into SP DMEM */
	fw = &sh_css_sp_fw;
	HIVE_ADDR_host_sp_com = fw->info.sp.host_sp_com;
	if (set_avail) {
		offset = (unsigned int)offsetof(struct host_sp_communication,
						host2sp_cont_avail_num_raw_frames)
			 / sizeof(int);
		avail_num_frames = load_sp_array_uint(host_sp_com, offset);
		extra_num_frames = num_frames - avail_num_frames;
		offset_extra = (unsigned int)offsetof(struct host_sp_communication,
						      host2sp_cont_extra_num_raw_frames)
			       / sizeof(int);
		store_sp_array_uint(host_sp_com, offset_extra, extra_num_frames);
	} else
		offset = (unsigned int)offsetof(struct host_sp_communication,
						host2sp_cont_target_num_raw_frames)
			 / sizeof(int);

	store_sp_array_uint(host_sp_com, offset, num_frames);
}

void
sh_css_event_init_irq_mask(void)
{
	int i;
	unsigned int HIVE_ADDR_host_sp_com = sh_css_sp_fw.info.sp.host_sp_com;
	unsigned int offset;
	struct sh_css_event_irq_mask event_irq_mask_init;

	event_irq_mask_init.or_mask  = IA_CSS_EVENT_TYPE_ALL;
	event_irq_mask_init.and_mask = IA_CSS_EVENT_TYPE_NONE;
	(void)HIVE_ADDR_host_sp_com; /* Suppress warnings in CRUN */

	assert(sizeof(event_irq_mask_init) % HRT_BUS_BYTES == 0);
	for (i = 0; i < IA_CSS_PIPE_ID_NUM; i++) {
		offset = (unsigned int)offsetof(struct host_sp_communication,
						host2sp_event_irq_mask[i]);
		assert(offset % HRT_BUS_BYTES == 0);
		sp_dmem_store(SP0_ID,
			      (unsigned int)sp_address_of(host_sp_com) + offset,
			      &event_irq_mask_init, sizeof(event_irq_mask_init));
	}
}

int
ia_css_pipe_set_irq_mask(struct ia_css_pipe *pipe,
			 unsigned int or_mask,
			 unsigned int and_mask)
{
	unsigned int HIVE_ADDR_host_sp_com = sh_css_sp_fw.info.sp.host_sp_com;
	unsigned int offset;
	struct sh_css_event_irq_mask event_irq_mask;
	unsigned int pipe_num;

	assert(pipe);

	assert(IA_CSS_PIPE_ID_NUM == NR_OF_PIPELINES);
	/* Linux kernel does not have UINT16_MAX
	 * Therefore decided to comment out these 2 asserts for Linux
	 * Alternatives that were not chosen:
	 * - add a conditional #define for UINT16_MAX
	 * - compare with (uint16_t)~0 or 0xffff
	 * - different assert for Linux and Windows
	 */

	(void)HIVE_ADDR_host_sp_com; /* Suppres warnings in CRUN */

	IA_CSS_LOG("or_mask=%x, and_mask=%x", or_mask, and_mask);
	event_irq_mask.or_mask  = (uint16_t)or_mask;
	event_irq_mask.and_mask = (uint16_t)and_mask;

	pipe_num = ia_css_pipe_get_pipe_num(pipe);
	if (pipe_num >= IA_CSS_PIPE_ID_NUM)
		return -EINVAL;
	offset = (unsigned int)offsetof(struct host_sp_communication,
					host2sp_event_irq_mask[pipe_num]);
	assert(offset % HRT_BUS_BYTES == 0);
	sp_dmem_store(SP0_ID,
		      (unsigned int)sp_address_of(host_sp_com) + offset,
		      &event_irq_mask, sizeof(event_irq_mask));

	return 0;
}

int
ia_css_event_get_irq_mask(const struct ia_css_pipe *pipe,
			  unsigned int *or_mask,
			  unsigned int *and_mask)
{
	unsigned int HIVE_ADDR_host_sp_com = sh_css_sp_fw.info.sp.host_sp_com;
	unsigned int offset;
	struct sh_css_event_irq_mask event_irq_mask;
	unsigned int pipe_num;

	(void)HIVE_ADDR_host_sp_com; /* Suppres warnings in CRUN */

	IA_CSS_ENTER_LEAVE("");

	assert(pipe);
	assert(IA_CSS_PIPE_ID_NUM == NR_OF_PIPELINES);

	pipe_num = ia_css_pipe_get_pipe_num(pipe);
	if (pipe_num >= IA_CSS_PIPE_ID_NUM)
		return -EINVAL;
	offset = (unsigned int)offsetof(struct host_sp_communication,
					host2sp_event_irq_mask[pipe_num]);
	assert(offset % HRT_BUS_BYTES == 0);
	sp_dmem_load(SP0_ID,
		     (unsigned int)sp_address_of(host_sp_com) + offset,
		     &event_irq_mask, sizeof(event_irq_mask));

	if (or_mask)
		*or_mask = event_irq_mask.or_mask;

	if (and_mask)
		*and_mask = event_irq_mask.and_mask;

	return 0;
}

void
sh_css_sp_set_sp_running(bool flag)
{
	sp_running = flag;
}

bool
sh_css_sp_is_running(void)
{
	return sp_running;
}

void
sh_css_sp_start_isp(void)
{
	const struct ia_css_fw_info *fw;
	unsigned int HIVE_ADDR_sp_sw_state;

	fw = &sh_css_sp_fw;
	HIVE_ADDR_sp_sw_state = fw->info.sp.sw_state;

	if (sp_running)
		return;

	(void)HIVE_ADDR_sp_sw_state; /* Suppres warnings in CRUN */

	/* no longer here, sp started immediately */
	/*ia_css_debug_pipe_graph_dump_epilogue();*/

	store_sp_group_data();
	store_sp_per_frame_data(fw);

	sp_dmem_store_uint32(SP0_ID,
			     (unsigned int)sp_address_of(sp_sw_state),
			     (uint32_t)(IA_CSS_SP_SW_TERMINATED));

	/* Note 1: The sp_start_isp function contains a wait till
	 * the input network is configured by the SP.
	 * Note 2: Not all SP binaries supports host2sp_commands.
	 * In case a binary does support it, the host2sp_command
	 * will have status cmd_ready after return of the function
	 * sh_css_hrt_sp_start_isp. There is no race-condition here
	 * because only after the process_frame command has been
	 * received, the SP starts configuring the input network.
	 */

	/* we need to set sp_running before we call ia_css_mmu_invalidate_cache
	 * as ia_css_mmu_invalidate_cache checks on sp_running to
	 * avoid that it accesses dmem while the SP is not powered
	 */
	sp_running = true;
	ia_css_mmu_invalidate_cache();
	/* Invalidate all MMU caches */
	mmu_invalidate_cache_all();

	ia_css_spctrl_start(SP0_ID);
}

bool
ia_css_isp_has_started(void)
{
	const struct ia_css_fw_info *fw = &sh_css_sp_fw;
	unsigned int HIVE_ADDR_ia_css_ispctrl_sp_isp_started = fw->info.sp.isp_started;
	(void)HIVE_ADDR_ia_css_ispctrl_sp_isp_started; /* Suppres warnings in CRUN */

	return (bool)load_sp_uint(ia_css_ispctrl_sp_isp_started);
}

/*
 * @brief Initialize the DMA software-mask in the debug mode.
 * Refer to "sh_css_sp.h" for more details.
 */
bool
sh_css_sp_init_dma_sw_reg(int dma_id)
{
	int i;

	/* enable all the DMA channels */
	for (i = 0; i < N_DMA_CHANNEL_ID; i++) {
		/* enable the writing request */
		sh_css_sp_set_dma_sw_reg(dma_id,
					 i,
					 0,
					 true);
		/* enable the reading request */
		sh_css_sp_set_dma_sw_reg(dma_id,
					 i,
					 1,
					 true);
	}

	return true;
}

/*
 * @brief Set the DMA software-mask in the debug mode.
 * Refer to "sh_css_sp.h" for more details.
 */
bool
sh_css_sp_set_dma_sw_reg(int dma_id,
			 int channel_id,
			 int request_type,
			 bool enable)
{
	u32 sw_reg;
	u32 bit_val;
	u32 bit_offset;
	u32 bit_mask;

	(void)dma_id;

	assert(channel_id >= 0 && channel_id < N_DMA_CHANNEL_ID);
	assert(request_type >= 0);

	/* get the software-mask */
	sw_reg =
	    sh_css_sp_group.debug.dma_sw_reg;

	/* get the offest of the target bit */
	bit_offset = (8 * request_type) + channel_id;

	/* clear the value of the target bit */
	bit_mask = ~(1 << bit_offset);
	sw_reg &= bit_mask;

	/* set the value of the bit for the DMA channel */
	bit_val = enable ? 1 : 0;
	bit_val <<= bit_offset;
	sw_reg |= bit_val;

	/* update the software status of DMA channels */
	sh_css_sp_group.debug.dma_sw_reg = sw_reg;

	return true;
}

void
sh_css_sp_reset_global_vars(void)
{
	memset(&sh_css_sp_group, 0, sizeof(struct sh_css_sp_group));
	memset(&sh_css_sp_stage, 0, sizeof(struct sh_css_sp_stage));
	memset(&sh_css_isp_stage, 0, sizeof(struct sh_css_isp_stage));
	memset(&sh_css_sp_output, 0, sizeof(struct sh_css_sp_output));
	memset(&per_frame_data, 0, sizeof(struct sh_css_sp_per_frame_data));
}
