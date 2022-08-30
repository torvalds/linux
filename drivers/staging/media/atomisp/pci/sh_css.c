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

/*! \file */
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "hmm.h"

#include "atomisp_internal.h"

#include "ia_css.h"
#include "sh_css_hrt.h"		/* only for file 2 MIPI */
#include "ia_css_buffer.h"
#include "ia_css_binary.h"
#include "sh_css_internal.h"
#include "sh_css_mipi.h"
#include "sh_css_sp.h"		/* sh_css_sp_group */
#include "ia_css_isys.h"
#include "ia_css_frame.h"
#include "sh_css_defs.h"
#include "sh_css_firmware.h"
#include "sh_css_params.h"
#include "sh_css_params_internal.h"
#include "sh_css_param_shading.h"
#include "ia_css_refcount.h"
#include "ia_css_rmgr.h"
#include "ia_css_debug.h"
#include "ia_css_debug_pipe.h"
#include "ia_css_device_access.h"
#include "device_access.h"
#include "sh_css_legacy.h"
#include "ia_css_pipeline.h"
#include "ia_css_stream.h"
#include "sh_css_stream_format.h"
#include "ia_css_pipe.h"
#include "ia_css_util.h"
#include "ia_css_pipe_util.h"
#include "ia_css_pipe_binarydesc.h"
#include "ia_css_pipe_stagedesc.h"

#include "tag.h"
#include "assert_support.h"
#include "math_support.h"
#include "sw_event_global.h"			/* Event IDs.*/
#if !defined(ISP2401)
#include "ia_css_ifmtr.h"
#endif
#include "input_system.h"
#include "mmu_device.h"		/* mmu_set_page_table_base_index(), ... */
#include "ia_css_mmu_private.h" /* sh_css_mmu_set_page_table_base_index() */
#include "gdc_device.h"		/* HRT_GDC_N */
#include "dma.h"		/* dma_set_max_burst_size() */
#include "irq.h"		/* virq */
#include "sp.h"			/* cnd_sp_irq_enable() */
#include "isp.h"		/* cnd_isp_irq_enable, ISP_VEC_NELEMS */
#include "gp_device.h"		/* gp_device_reg_store() */
#define __INLINE_GPIO__
#include "gpio.h"
#include "timed_ctrl.h"
#include "ia_css_inputfifo.h"
#define WITH_PC_MONITORING  0

#define SH_CSS_VIDEO_BUFFER_ALIGNMENT 0


#include "ia_css_spctrl.h"
#include "ia_css_version_data.h"
#include "sh_css_struct.h"
#include "ia_css_bufq.h"
#include "ia_css_timer.h" /* clock_value_t */

#include "isp/modes/interface/input_buf.isp.h"

/* Name of the sp program: should not be built-in */
#define SP_PROG_NAME "sp"
/* Size of Refcount List */
#define REFCOUNT_SIZE 1000

/*
 * for JPEG, we don't know the length of the image upfront,
 * but since we support sensor up to 16MP, we take this as
 * upper limit.
 */
#define JPEG_BYTES (16 * 1024 * 1024)

#define STATS_ENABLED(stage) (stage && stage->binary && stage->binary->info && \
	(stage->binary->info->sp.enable.s3a || stage->binary->info->sp.enable.dis))

struct sh_css my_css;

int  __printf(1, 0) (*sh_css_printf)(const char *fmt, va_list args) = NULL;

/*
 * modes of work: stream_create and stream_destroy will update the save/restore
 * data only when in working mode, not suspend/resume
 */
enum ia_sh_css_modes {
	sh_css_mode_none = 0,
	sh_css_mode_working,
	sh_css_mode_suspend,
	sh_css_mode_resume
};

/**
 * struct sh_css_stream_seed - a stream seed, to save and restore the
 * stream data.
 *
 * @orig_stream:	pointer to restore the original handle
 * @stream:		handle, used as ID too.
 * @stream_config:	stream config struct
 * @num_pipes:		number of pipes
 * @pipes:		pipe handles
 * @orig_pipes:		pointer to restore original handle
 * @pipe_config:	pipe config structs
 *
 * the stream seed contains all the data required to "grow" the seed again
 * after it was closed.
*/
struct sh_css_stream_seed {
	struct ia_css_stream		**orig_stream;
	struct ia_css_stream		*stream;
	struct ia_css_stream_config	stream_config;
	int				num_pipes;
	struct ia_css_pipe		*pipes[IA_CSS_PIPE_ID_NUM];
	struct ia_css_pipe		**orig_pipes[IA_CSS_PIPE_ID_NUM];
	struct ia_css_pipe_config	pipe_config[IA_CSS_PIPE_ID_NUM];
};

#define MAX_ACTIVE_STREAMS	5
/*
 * A global struct for save/restore to hold all the data that should
 * sustain power-down: MMU base, IRQ type, env for routines, binary loaded FW
 * and the stream seeds.
 */
struct sh_css_save {
	enum ia_sh_css_modes		mode;
	u32		       mmu_base;		/* the last mmu_base */
	enum ia_css_irq_type           irq_type;
	struct sh_css_stream_seed      stream_seeds[MAX_ACTIVE_STREAMS];
	struct ia_css_fw	       *loaded_fw;	/* fw struct previously loaded */
	struct ia_css_env	       driver_env;	/* driver-supplied env copy */
};

static bool my_css_save_initialized;	/* if my_css_save was initialized */
static struct sh_css_save my_css_save;

/*
 * pqiao NOTICE: this is for css internal buffer recycling when stopping
 * pipeline,
 * this array is temporary and will be replaced by resource manager
 */

/* Taking the biggest Size for number of Elements */
#define MAX_HMM_BUFFER_NUM	\
	(SH_CSS_MAX_NUM_QUEUES * (IA_CSS_NUM_ELEMS_SP2HOST_BUFFER_QUEUE + 2))

struct sh_css_hmm_buffer_record {
	bool in_use;
	enum ia_css_buffer_type type;
	struct ia_css_rmgr_vbuf_handle *h_vbuf;
	hrt_address kernel_ptr;
};

static struct sh_css_hmm_buffer_record hmm_buffer_record[MAX_HMM_BUFFER_NUM];

#define GPIO_FLASH_PIN_MASK BIT(HIVE_GPIO_STROBE_TRIGGER_PIN)

static bool fw_explicitly_loaded;

/*
 * Local prototypes
 */

static int
allocate_delay_frames(struct ia_css_pipe *pipe);

static int
sh_css_pipe_start(struct ia_css_stream *stream);

/*
 * @brief Check if all "ia_css_pipe" instances in the target
 * "ia_css_stream" instance have stopped.
 *
 * @param[in] stream	Point to the target "ia_css_stream" instance.
 *
 * @return
 * - true, if all "ia_css_pipe" instances in the target "ia_css_stream"
 *   instance have ben stopped.
 * - false, otherwise.
 */

/* ISP 2401 */
static int
ia_css_pipe_check_format(struct ia_css_pipe *pipe,
			 enum ia_css_frame_format format);

/* ISP 2401 */
static int
ia_css_pipe_load_extension(struct ia_css_pipe *pipe,
			   struct ia_css_fw_info *firmware);

static void
ia_css_pipe_unload_extension(struct ia_css_pipe *pipe,
			     struct ia_css_fw_info *firmware);
static void
ia_css_reset_defaults(struct sh_css *css);

static void
sh_css_init_host_sp_control_vars(void);

static int
set_num_primary_stages(unsigned int *num, enum ia_css_pipe_version version);

static bool
need_capture_pp(const struct ia_css_pipe *pipe);

static bool
need_yuv_scaler_stage(const struct ia_css_pipe *pipe);

static int ia_css_pipe_create_cas_scaler_desc_single_output(
    struct ia_css_frame_info *cas_scaler_in_info,
    struct ia_css_frame_info *cas_scaler_out_info,
    struct ia_css_frame_info *cas_scaler_vf_info,
    struct ia_css_cas_binary_descr *descr);

static void ia_css_pipe_destroy_cas_scaler_desc(struct ia_css_cas_binary_descr
	*descr);

static bool
need_downscaling(const struct ia_css_resolution in_res,
		 const struct ia_css_resolution out_res);

static bool need_capt_ldc(const struct ia_css_pipe *pipe);

static int
sh_css_pipe_load_binaries(struct ia_css_pipe *pipe);

static
int sh_css_pipe_get_viewfinder_frame_info(
    struct ia_css_pipe *pipe,
    struct ia_css_frame_info *info,
    unsigned int idx);

static int
sh_css_pipe_get_output_frame_info(struct ia_css_pipe *pipe,
				  struct ia_css_frame_info *info,
				  unsigned int idx);

static int
capture_start(struct ia_css_pipe *pipe);

static int
video_start(struct ia_css_pipe *pipe);

static int
preview_start(struct ia_css_pipe *pipe);

static int
yuvpp_start(struct ia_css_pipe *pipe);

static bool copy_on_sp(struct ia_css_pipe *pipe);

static int
init_vf_frameinfo_defaults(struct ia_css_pipe *pipe,
			   struct ia_css_frame *vf_frame, unsigned int idx);

static int
init_in_frameinfo_memory_defaults(struct ia_css_pipe *pipe,
				  struct ia_css_frame *frame, enum ia_css_frame_format format);

static int
init_out_frameinfo_defaults(struct ia_css_pipe *pipe,
			    struct ia_css_frame *out_frame, unsigned int idx);

static int
sh_css_pipeline_add_acc_stage(struct ia_css_pipeline *pipeline,
			      const void *acc_fw);

static int
alloc_continuous_frames(struct ia_css_pipe *pipe, bool init_time);

static void
pipe_global_init(void);

static int
pipe_generate_pipe_num(const struct ia_css_pipe *pipe,
		       unsigned int *pipe_number);

static void
pipe_release_pipe_num(unsigned int pipe_num);

static int
create_host_pipeline_structure(struct ia_css_stream *stream);

static int
create_host_pipeline(struct ia_css_stream *stream);

static int
create_host_preview_pipeline(struct ia_css_pipe *pipe);

static int
create_host_video_pipeline(struct ia_css_pipe *pipe);

static int
create_host_copy_pipeline(struct ia_css_pipe *pipe,
			  unsigned int max_input_width,
			  struct ia_css_frame *out_frame);

static int
create_host_isyscopy_capture_pipeline(struct ia_css_pipe *pipe);

static int
create_host_capture_pipeline(struct ia_css_pipe *pipe);

static int
create_host_yuvpp_pipeline(struct ia_css_pipe *pipe);

static int
create_host_acc_pipeline(struct ia_css_pipe *pipe);

static unsigned int
sh_css_get_sw_interrupt_value(unsigned int irq);

static struct ia_css_binary *ia_css_pipe_get_shading_correction_binary(
    const struct ia_css_pipe *pipe);

static struct ia_css_binary *
ia_css_pipe_get_s3a_binary(const struct ia_css_pipe *pipe);

static struct ia_css_binary *
ia_css_pipe_get_sdis_binary(const struct ia_css_pipe *pipe);

static void
sh_css_hmm_buffer_record_init(void);

static void
sh_css_hmm_buffer_record_uninit(void);

static void
sh_css_hmm_buffer_record_reset(struct sh_css_hmm_buffer_record *buffer_record);

static struct sh_css_hmm_buffer_record
*sh_css_hmm_buffer_record_acquire(struct ia_css_rmgr_vbuf_handle *h_vbuf,
				  enum ia_css_buffer_type type,
				  hrt_address kernel_ptr);

static struct sh_css_hmm_buffer_record
*sh_css_hmm_buffer_record_validate(ia_css_ptr ddr_buffer_addr,
				   enum ia_css_buffer_type type);

void
ia_css_get_acc_configs(
    struct ia_css_pipe *pipe,
    struct ia_css_isp_config *config);


#ifdef ISP2401
static unsigned int get_crop_lines_for_bayer_order(const struct
	ia_css_stream_config *config);
static unsigned int get_crop_columns_for_bayer_order(const struct
	ia_css_stream_config *config);
static void get_pipe_extra_pixel(struct ia_css_pipe *pipe,
				 unsigned int *extra_row, unsigned int *extra_column);

#endif

static void
sh_css_pipe_free_shading_table(struct ia_css_pipe *pipe)
{
	if (!pipe) {
		IA_CSS_ERROR("NULL input parameter");
		return;
	}

	if (pipe->shading_table)
		ia_css_shading_table_free(pipe->shading_table);
	pipe->shading_table = NULL;
}

static enum ia_css_frame_format yuv420_copy_formats[] = {
	IA_CSS_FRAME_FORMAT_NV12,
	IA_CSS_FRAME_FORMAT_NV21,
	IA_CSS_FRAME_FORMAT_YV12,
	IA_CSS_FRAME_FORMAT_YUV420,
	IA_CSS_FRAME_FORMAT_YUV420_16,
	IA_CSS_FRAME_FORMAT_CSI_MIPI_YUV420_8,
	IA_CSS_FRAME_FORMAT_CSI_MIPI_LEGACY_YUV420_8
};

static enum ia_css_frame_format yuv422_copy_formats[] = {
	IA_CSS_FRAME_FORMAT_NV12,
	IA_CSS_FRAME_FORMAT_NV16,
	IA_CSS_FRAME_FORMAT_NV21,
	IA_CSS_FRAME_FORMAT_NV61,
	IA_CSS_FRAME_FORMAT_YV12,
	IA_CSS_FRAME_FORMAT_YV16,
	IA_CSS_FRAME_FORMAT_YUV420,
	IA_CSS_FRAME_FORMAT_YUV420_16,
	IA_CSS_FRAME_FORMAT_YUV422,
	IA_CSS_FRAME_FORMAT_YUV422_16,
	IA_CSS_FRAME_FORMAT_UYVY,
	IA_CSS_FRAME_FORMAT_YUYV
};

/*
 * Verify whether the selected output format is can be produced
 * by the copy binary given the stream format.
 */
static int
verify_copy_out_frame_format(struct ia_css_pipe *pipe)
{
	enum ia_css_frame_format out_fmt = pipe->output_info[0].format;
	unsigned int i, found = 0;

	assert(pipe);
	assert(pipe->stream);

	switch (pipe->stream->config.input_config.format) {
	case ATOMISP_INPUT_FORMAT_YUV420_8_LEGACY:
	case ATOMISP_INPUT_FORMAT_YUV420_8:
		for (i = 0; i < ARRAY_SIZE(yuv420_copy_formats) && !found; i++)
			found = (out_fmt == yuv420_copy_formats[i]);
		break;
	case ATOMISP_INPUT_FORMAT_YUV420_10:
	case ATOMISP_INPUT_FORMAT_YUV420_16:
		found = (out_fmt == IA_CSS_FRAME_FORMAT_YUV420_16);
		break;
	case ATOMISP_INPUT_FORMAT_YUV422_8:
		for (i = 0; i < ARRAY_SIZE(yuv422_copy_formats) && !found; i++)
			found = (out_fmt == yuv422_copy_formats[i]);
		break;
	case ATOMISP_INPUT_FORMAT_YUV422_10:
	case ATOMISP_INPUT_FORMAT_YUV422_16:
		found = (out_fmt == IA_CSS_FRAME_FORMAT_YUV422_16 ||
			 out_fmt == IA_CSS_FRAME_FORMAT_YUV420_16);
		break;
	case ATOMISP_INPUT_FORMAT_RGB_444:
	case ATOMISP_INPUT_FORMAT_RGB_555:
	case ATOMISP_INPUT_FORMAT_RGB_565:
		found = (out_fmt == IA_CSS_FRAME_FORMAT_RGBA888 ||
			 out_fmt == IA_CSS_FRAME_FORMAT_RGB565);
		break;
	case ATOMISP_INPUT_FORMAT_RGB_666:
	case ATOMISP_INPUT_FORMAT_RGB_888:
		found = (out_fmt == IA_CSS_FRAME_FORMAT_RGBA888 ||
			 out_fmt == IA_CSS_FRAME_FORMAT_YUV420);
		break;
	case ATOMISP_INPUT_FORMAT_RAW_6:
	case ATOMISP_INPUT_FORMAT_RAW_7:
	case ATOMISP_INPUT_FORMAT_RAW_8:
	case ATOMISP_INPUT_FORMAT_RAW_10:
	case ATOMISP_INPUT_FORMAT_RAW_12:
	case ATOMISP_INPUT_FORMAT_RAW_14:
	case ATOMISP_INPUT_FORMAT_RAW_16:
		found = (out_fmt == IA_CSS_FRAME_FORMAT_RAW) ||
		(out_fmt == IA_CSS_FRAME_FORMAT_RAW_PACKED);
		break;
	case ATOMISP_INPUT_FORMAT_BINARY_8:
		found = (out_fmt == IA_CSS_FRAME_FORMAT_BINARY_8);
		break;
	default:
		break;
	}
	if (!found)
		return -EINVAL;
	return 0;
}

unsigned int
ia_css_stream_input_format_bits_per_pixel(struct ia_css_stream *stream)
{
	int bpp = 0;

	if (stream)
		bpp = ia_css_util_input_format_bpp(stream->config.input_config.format,
						   stream->config.pixels_per_clock == 2);

	return bpp;
}

/* TODO: move define to proper file in tools */
#define GP_ISEL_TPG_MODE 0x90058

#if !defined(ISP2401)
static int
sh_css_config_input_network(struct ia_css_stream *stream)
{
	unsigned int fmt_type;
	struct ia_css_pipe *pipe = stream->last_pipe;
	struct ia_css_binary *binary = NULL;
	int err = 0;

	assert(stream);
	assert(pipe);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_config_input_network() enter:\n");

	if (pipe->pipeline.stages)
		binary = pipe->pipeline.stages->binary;

	err = ia_css_isys_convert_stream_format_to_mipi_format(
	    stream->config.input_config.format,
	    stream->csi_rx_config.comp,
	    &fmt_type);
	if (err)
		return err;
	sh_css_sp_program_input_circuit(fmt_type,
					stream->config.channel_id,
					stream->config.mode);

	if ((binary && (binary->online || stream->config.continuous)) ||
	    pipe->config.mode == IA_CSS_PIPE_MODE_COPY) {
		err = ia_css_ifmtr_configure(&stream->config,
					     binary);
		if (err)
			return err;
	}

	if (stream->config.mode == IA_CSS_INPUT_MODE_TPG ||
	    stream->config.mode == IA_CSS_INPUT_MODE_PRBS) {
		unsigned int hblank_cycles = 100,
		vblank_lines = 6,
		width,
		height,
		vblank_cycles;
		width  = (stream->config.input_config.input_res.width) / (1 +
			(stream->config.pixels_per_clock == 2));
		height = stream->config.input_config.input_res.height;
		vblank_cycles = vblank_lines * (width + hblank_cycles);
		sh_css_sp_configure_sync_gen(width, height, hblank_cycles,
					     vblank_cycles);
		if (pipe->stream->config.mode == IA_CSS_INPUT_MODE_TPG)
			ia_css_device_store_uint32(GP_ISEL_TPG_MODE, 0);
	}
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_config_input_network() leave:\n");
	return 0;
}
#elif defined(ISP2401)
static unsigned int csi2_protocol_calculate_max_subpixels_per_line(
    enum atomisp_input_format	format,
    unsigned int			pixels_per_line)
{
	unsigned int rval;

	switch (format) {
	case ATOMISP_INPUT_FORMAT_YUV420_8_LEGACY:
		/*
		 * The frame format layout is shown below.
		 *
		 *		Line	0:	UYY0 UYY0 ... UYY0
		 *		Line	1:	VYY0 VYY0 ... VYY0
		 *		Line	2:	UYY0 UYY0 ... UYY0
		 *		Line	3:	VYY0 VYY0 ... VYY0
		 *		...
		 *		Line (n-2):	UYY0 UYY0 ... UYY0
		 *		Line (n-1):	VYY0 VYY0 ... VYY0
		 *
		 *	In this frame format, the even-line is
		 *	as wide as the odd-line.
		 *	The 0 is introduced by the input system
		 *	(mipi backend).
		 */
		rval = pixels_per_line * 2;
		break;
	case ATOMISP_INPUT_FORMAT_YUV420_8:
	case ATOMISP_INPUT_FORMAT_YUV420_10:
	case ATOMISP_INPUT_FORMAT_YUV420_16:
		/*
		 * The frame format layout is shown below.
		 *
		 *		Line	0:	YYYY YYYY ... YYYY
		 *		Line	1:	UYVY UYVY ... UYVY UYVY
		 *		Line	2:	YYYY YYYY ... YYYY
		 *		Line	3:	UYVY UYVY ... UYVY UYVY
		 *		...
		 *		Line (n-2):	YYYY YYYY ... YYYY
		 *		Line (n-1):	UYVY UYVY ... UYVY UYVY
		 *
		 * In this frame format, the odd-line is twice
		 * wider than the even-line.
		 */
		rval = pixels_per_line * 2;
		break;
	case ATOMISP_INPUT_FORMAT_YUV422_8:
	case ATOMISP_INPUT_FORMAT_YUV422_10:
	case ATOMISP_INPUT_FORMAT_YUV422_16:
		/*
		 * The frame format layout is shown below.
		 *
		 *		Line	0:	UYVY UYVY ... UYVY
		 *		Line	1:	UYVY UYVY ... UYVY
		 *		Line	2:	UYVY UYVY ... UYVY
		 *		Line	3:	UYVY UYVY ... UYVY
		 *		...
		 *		Line (n-2):	UYVY UYVY ... UYVY
		 *		Line (n-1):	UYVY UYVY ... UYVY
		 *
		 * In this frame format, the even-line is
		 * as wide as the odd-line.
		 */
		rval = pixels_per_line * 2;
		break;
	case ATOMISP_INPUT_FORMAT_RGB_444:
	case ATOMISP_INPUT_FORMAT_RGB_555:
	case ATOMISP_INPUT_FORMAT_RGB_565:
	case ATOMISP_INPUT_FORMAT_RGB_666:
	case ATOMISP_INPUT_FORMAT_RGB_888:
		/*
		 * The frame format layout is shown below.
		 *
		 *		Line	0:	ABGR ABGR ... ABGR
		 *		Line	1:	ABGR ABGR ... ABGR
		 *		Line	2:	ABGR ABGR ... ABGR
		 *		Line	3:	ABGR ABGR ... ABGR
		 *		...
		 *		Line (n-2):	ABGR ABGR ... ABGR
		 *		Line (n-1):	ABGR ABGR ... ABGR
		 *
		 * In this frame format, the even-line is
		 * as wide as the odd-line.
		 */
		rval = pixels_per_line * 4;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_6:
	case ATOMISP_INPUT_FORMAT_RAW_7:
	case ATOMISP_INPUT_FORMAT_RAW_8:
	case ATOMISP_INPUT_FORMAT_RAW_10:
	case ATOMISP_INPUT_FORMAT_RAW_12:
	case ATOMISP_INPUT_FORMAT_RAW_14:
	case ATOMISP_INPUT_FORMAT_RAW_16:
	case ATOMISP_INPUT_FORMAT_BINARY_8:
	case ATOMISP_INPUT_FORMAT_USER_DEF1:
	case ATOMISP_INPUT_FORMAT_USER_DEF2:
	case ATOMISP_INPUT_FORMAT_USER_DEF3:
	case ATOMISP_INPUT_FORMAT_USER_DEF4:
	case ATOMISP_INPUT_FORMAT_USER_DEF5:
	case ATOMISP_INPUT_FORMAT_USER_DEF6:
	case ATOMISP_INPUT_FORMAT_USER_DEF7:
	case ATOMISP_INPUT_FORMAT_USER_DEF8:
		/*
		 * The frame format layout is shown below.
		 *
		 *		Line	0:	Pixel ... Pixel
		 *		Line	1:	Pixel ... Pixel
		 *		Line	2:	Pixel ... Pixel
		 *		Line	3:	Pixel ... Pixel
		 *		...
		 *		Line (n-2):	Pixel ... Pixel
		 *		Line (n-1):	Pixel ... Pixel
		 *
		 * In this frame format, the even-line is
		 * as wide as the odd-line.
		 */
		rval = pixels_per_line;
		break;
	default:
		rval = 0;
		break;
	}

	return rval;
}

static bool sh_css_translate_stream_cfg_to_input_system_input_port_id(
    struct ia_css_stream_config *stream_cfg,
    ia_css_isys_descr_t	*isys_stream_descr)
{
	bool rc;

	rc = true;
	switch (stream_cfg->mode) {
	case IA_CSS_INPUT_MODE_TPG:

		if (stream_cfg->source.tpg.id == IA_CSS_TPG_ID0)
			isys_stream_descr->input_port_id = INPUT_SYSTEM_PIXELGEN_PORT0_ID;
		else if (stream_cfg->source.tpg.id == IA_CSS_TPG_ID1)
			isys_stream_descr->input_port_id = INPUT_SYSTEM_PIXELGEN_PORT1_ID;
		else if (stream_cfg->source.tpg.id == IA_CSS_TPG_ID2)
			isys_stream_descr->input_port_id = INPUT_SYSTEM_PIXELGEN_PORT2_ID;

		break;
	case IA_CSS_INPUT_MODE_PRBS:

		if (stream_cfg->source.prbs.id == IA_CSS_PRBS_ID0)
			isys_stream_descr->input_port_id = INPUT_SYSTEM_PIXELGEN_PORT0_ID;
		else if (stream_cfg->source.prbs.id == IA_CSS_PRBS_ID1)
			isys_stream_descr->input_port_id = INPUT_SYSTEM_PIXELGEN_PORT1_ID;
		else if (stream_cfg->source.prbs.id == IA_CSS_PRBS_ID2)
			isys_stream_descr->input_port_id = INPUT_SYSTEM_PIXELGEN_PORT2_ID;

		break;
	case IA_CSS_INPUT_MODE_BUFFERED_SENSOR:

		if (stream_cfg->source.port.port == MIPI_PORT0_ID)
			isys_stream_descr->input_port_id = INPUT_SYSTEM_CSI_PORT0_ID;
		else if (stream_cfg->source.port.port == MIPI_PORT1_ID)
			isys_stream_descr->input_port_id = INPUT_SYSTEM_CSI_PORT1_ID;
		else if (stream_cfg->source.port.port == MIPI_PORT2_ID)
			isys_stream_descr->input_port_id = INPUT_SYSTEM_CSI_PORT2_ID;

		break;
	default:
		rc = false;
		break;
	}

	return rc;
}

static bool sh_css_translate_stream_cfg_to_input_system_input_port_type(
    struct ia_css_stream_config *stream_cfg,
    ia_css_isys_descr_t	*isys_stream_descr)
{
	bool rc;

	rc = true;
	switch (stream_cfg->mode) {
	case IA_CSS_INPUT_MODE_TPG:

		isys_stream_descr->mode = INPUT_SYSTEM_SOURCE_TYPE_TPG;

		break;
	case IA_CSS_INPUT_MODE_PRBS:

		isys_stream_descr->mode = INPUT_SYSTEM_SOURCE_TYPE_PRBS;

		break;
	case IA_CSS_INPUT_MODE_SENSOR:
	case IA_CSS_INPUT_MODE_BUFFERED_SENSOR:

		isys_stream_descr->mode = INPUT_SYSTEM_SOURCE_TYPE_SENSOR;
		break;

	default:
		rc = false;
		break;
	}

	return rc;
}

static bool sh_css_translate_stream_cfg_to_input_system_input_port_attr(
    struct ia_css_stream_config *stream_cfg,
    ia_css_isys_descr_t	*isys_stream_descr,
    int isys_stream_idx)
{
	bool rc;

	rc = true;
	switch (stream_cfg->mode) {
	case IA_CSS_INPUT_MODE_TPG:
		if (stream_cfg->source.tpg.mode == IA_CSS_TPG_MODE_RAMP)
			isys_stream_descr->tpg_port_attr.mode = PIXELGEN_TPG_MODE_RAMP;
		else if (stream_cfg->source.tpg.mode == IA_CSS_TPG_MODE_CHECKERBOARD)
			isys_stream_descr->tpg_port_attr.mode = PIXELGEN_TPG_MODE_CHBO;
		else if (stream_cfg->source.tpg.mode == IA_CSS_TPG_MODE_MONO)
			isys_stream_descr->tpg_port_attr.mode = PIXELGEN_TPG_MODE_MONO;
		else
			rc = false;

		/*
		 * TODO
		 * - Make "color_cfg" as part of "ia_css_tpg_config".
		 */
		isys_stream_descr->tpg_port_attr.color_cfg.R1 = 51;
		isys_stream_descr->tpg_port_attr.color_cfg.G1 = 102;
		isys_stream_descr->tpg_port_attr.color_cfg.B1 = 255;
		isys_stream_descr->tpg_port_attr.color_cfg.R2 = 0;
		isys_stream_descr->tpg_port_attr.color_cfg.G2 = 100;
		isys_stream_descr->tpg_port_attr.color_cfg.B2 = 160;

		isys_stream_descr->tpg_port_attr.mask_cfg.h_mask =
		    stream_cfg->source.tpg.x_mask;
		isys_stream_descr->tpg_port_attr.mask_cfg.v_mask =
		    stream_cfg->source.tpg.y_mask;
		isys_stream_descr->tpg_port_attr.mask_cfg.hv_mask =
		    stream_cfg->source.tpg.xy_mask;

		isys_stream_descr->tpg_port_attr.delta_cfg.h_delta =
		    stream_cfg->source.tpg.x_delta;
		isys_stream_descr->tpg_port_attr.delta_cfg.v_delta =
		    stream_cfg->source.tpg.y_delta;

		/*
		 * TODO
		 * - Make "sync_gen_cfg" as part of "ia_css_tpg_config".
		 */
		isys_stream_descr->tpg_port_attr.sync_gen_cfg.hblank_cycles = 100;
		isys_stream_descr->tpg_port_attr.sync_gen_cfg.vblank_cycles = 100;
		isys_stream_descr->tpg_port_attr.sync_gen_cfg.pixels_per_clock =
		    stream_cfg->pixels_per_clock;
		isys_stream_descr->tpg_port_attr.sync_gen_cfg.nr_of_frames = (uint32_t)~(0x0);
		isys_stream_descr->tpg_port_attr.sync_gen_cfg.pixels_per_line =
		    stream_cfg->isys_config[IA_CSS_STREAM_DEFAULT_ISYS_STREAM_IDX].input_res.width;
		isys_stream_descr->tpg_port_attr.sync_gen_cfg.lines_per_frame =
		    stream_cfg->isys_config[IA_CSS_STREAM_DEFAULT_ISYS_STREAM_IDX].input_res.height;

		break;
	case IA_CSS_INPUT_MODE_PRBS:

		isys_stream_descr->prbs_port_attr.seed0 = stream_cfg->source.prbs.seed;
		isys_stream_descr->prbs_port_attr.seed1 = stream_cfg->source.prbs.seed1;

		/*
		 * TODO
		 * - Make "sync_gen_cfg" as part of "ia_css_prbs_config".
		 */
		isys_stream_descr->prbs_port_attr.sync_gen_cfg.hblank_cycles = 100;
		isys_stream_descr->prbs_port_attr.sync_gen_cfg.vblank_cycles = 100;
		isys_stream_descr->prbs_port_attr.sync_gen_cfg.pixels_per_clock =
		    stream_cfg->pixels_per_clock;
		isys_stream_descr->prbs_port_attr.sync_gen_cfg.nr_of_frames = (uint32_t)~(0x0);
		isys_stream_descr->prbs_port_attr.sync_gen_cfg.pixels_per_line =
		    stream_cfg->isys_config[IA_CSS_STREAM_DEFAULT_ISYS_STREAM_IDX].input_res.width;
		isys_stream_descr->prbs_port_attr.sync_gen_cfg.lines_per_frame =
		    stream_cfg->isys_config[IA_CSS_STREAM_DEFAULT_ISYS_STREAM_IDX].input_res.height;

		break;
	case IA_CSS_INPUT_MODE_BUFFERED_SENSOR: {
		int err;
		unsigned int fmt_type;

		err = ia_css_isys_convert_stream_format_to_mipi_format(
			  stream_cfg->isys_config[isys_stream_idx].format,
			  MIPI_PREDICTOR_NONE,
			  &fmt_type);
		if (err)
			rc = false;

		isys_stream_descr->csi_port_attr.active_lanes =
		    stream_cfg->source.port.num_lanes;
		isys_stream_descr->csi_port_attr.fmt_type = fmt_type;
		isys_stream_descr->csi_port_attr.ch_id = stream_cfg->channel_id;
#ifdef ISP2401
		isys_stream_descr->online = stream_cfg->online;
#endif
		err |= ia_css_isys_convert_compressed_format(
			   &stream_cfg->source.port.compression,
			   isys_stream_descr);
		if (err)
			rc = false;

		/* metadata */
		isys_stream_descr->metadata.enable = false;
		if (stream_cfg->metadata_config.resolution.height > 0) {
			err = ia_css_isys_convert_stream_format_to_mipi_format(
				  stream_cfg->metadata_config.data_type,
				  MIPI_PREDICTOR_NONE,
				  &fmt_type);
			if (err)
				rc = false;
			isys_stream_descr->metadata.fmt_type = fmt_type;
			isys_stream_descr->metadata.bits_per_pixel =
			    ia_css_util_input_format_bpp(stream_cfg->metadata_config.data_type, true);
			isys_stream_descr->metadata.pixels_per_line =
			    stream_cfg->metadata_config.resolution.width;
			isys_stream_descr->metadata.lines_per_frame =
			    stream_cfg->metadata_config.resolution.height;
#ifdef ISP2401
			/*
			 * For new input system, number of str2mmio requests must be even.
			 * So we round up number of metadata lines to be even.
			 */
			if (isys_stream_descr->metadata.lines_per_frame > 0)
				isys_stream_descr->metadata.lines_per_frame +=
				    (isys_stream_descr->metadata.lines_per_frame & 1);
#endif
			isys_stream_descr->metadata.align_req_in_bytes =
			    ia_css_csi2_calculate_input_system_alignment(
				stream_cfg->metadata_config.data_type);
			isys_stream_descr->metadata.enable = true;
		}

		break;
	}
	default:
		rc = false;
		break;
	}

	return rc;
}

static bool sh_css_translate_stream_cfg_to_input_system_input_port_resolution(
    struct ia_css_stream_config *stream_cfg,
    ia_css_isys_descr_t	*isys_stream_descr,
    int isys_stream_idx)
{
	unsigned int bits_per_subpixel;
	unsigned int max_subpixels_per_line;
	unsigned int lines_per_frame;
	unsigned int align_req_in_bytes;
	enum atomisp_input_format fmt_type;

	fmt_type = stream_cfg->isys_config[isys_stream_idx].format;
	if ((stream_cfg->mode == IA_CSS_INPUT_MODE_SENSOR ||
	     stream_cfg->mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR) &&
	    stream_cfg->source.port.compression.type != IA_CSS_CSI2_COMPRESSION_TYPE_NONE) {
		if (stream_cfg->source.port.compression.uncompressed_bits_per_pixel ==
		    UNCOMPRESSED_BITS_PER_PIXEL_10)
			fmt_type = ATOMISP_INPUT_FORMAT_RAW_10;
		else if (stream_cfg->source.port.compression.uncompressed_bits_per_pixel ==
			   UNCOMPRESSED_BITS_PER_PIXEL_12)
			fmt_type = ATOMISP_INPUT_FORMAT_RAW_12;
		else
			return false;
	}

	bits_per_subpixel =
	    sh_css_stream_format_2_bits_per_subpixel(fmt_type);
	if (bits_per_subpixel == 0)
		return false;

	max_subpixels_per_line =
	    csi2_protocol_calculate_max_subpixels_per_line(fmt_type,
		    stream_cfg->isys_config[isys_stream_idx].input_res.width);
	if (max_subpixels_per_line == 0)
		return false;

	lines_per_frame = stream_cfg->isys_config[isys_stream_idx].input_res.height;
	if (lines_per_frame == 0)
		return false;

	align_req_in_bytes = ia_css_csi2_calculate_input_system_alignment(fmt_type);

	/* HW needs subpixel info for their settings */
	isys_stream_descr->input_port_resolution.bits_per_pixel = bits_per_subpixel;
	isys_stream_descr->input_port_resolution.pixels_per_line =
	    max_subpixels_per_line;
	isys_stream_descr->input_port_resolution.lines_per_frame = lines_per_frame;
	isys_stream_descr->input_port_resolution.align_req_in_bytes =
	    align_req_in_bytes;

	return true;
}

static bool sh_css_translate_stream_cfg_to_isys_stream_descr(
    struct ia_css_stream_config *stream_cfg,
    bool early_polling,
    ia_css_isys_descr_t	*isys_stream_descr,
    int isys_stream_idx)
{
	bool rc;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_translate_stream_cfg_to_isys_stream_descr() enter:\n");
	rc  = sh_css_translate_stream_cfg_to_input_system_input_port_id(stream_cfg,
		isys_stream_descr);
	rc &= sh_css_translate_stream_cfg_to_input_system_input_port_type(stream_cfg,
		isys_stream_descr);
	rc &= sh_css_translate_stream_cfg_to_input_system_input_port_attr(stream_cfg,
		isys_stream_descr, isys_stream_idx);
	rc &= sh_css_translate_stream_cfg_to_input_system_input_port_resolution(
		  stream_cfg, isys_stream_descr, isys_stream_idx);

	isys_stream_descr->raw_packed = stream_cfg->pack_raw_pixels;
	isys_stream_descr->linked_isys_stream_id = (int8_t)
		stream_cfg->isys_config[isys_stream_idx].linked_isys_stream_id;

	if (IS_ISP2401)
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
				    "sh_css_translate_stream_cfg_to_isys_stream_descr() leave:\n");

	return rc;
}

static bool sh_css_translate_binary_info_to_input_system_output_port_attr(
    struct ia_css_binary *binary,
    ia_css_isys_descr_t     *isys_stream_descr)
{
	if (!binary)
		return false;

	isys_stream_descr->output_port_attr.left_padding = binary->left_padding;
	isys_stream_descr->output_port_attr.max_isp_input_width =
	    binary->info->sp.input.max_width;

	return true;
}

static int
sh_css_config_input_network(struct ia_css_stream *stream)
{
	bool					rc;
	ia_css_isys_descr_t			isys_stream_descr;
	unsigned int				sp_thread_id;
	struct sh_css_sp_pipeline_terminal	*sp_pipeline_input_terminal;
	struct ia_css_pipe *pipe = NULL;
	struct ia_css_binary *binary = NULL;
	int i;
	u32 isys_stream_id;
	bool early_polling = false;

	assert(stream);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_config_input_network() enter 0x%p:\n", stream);

	if (stream->config.continuous) {
		if (stream->last_pipe->config.mode == IA_CSS_PIPE_MODE_CAPTURE)
			pipe = stream->last_pipe;
		else if (stream->last_pipe->config.mode == IA_CSS_PIPE_MODE_YUVPP)
			pipe = stream->last_pipe;
		else if (stream->last_pipe->config.mode == IA_CSS_PIPE_MODE_PREVIEW)
			pipe = stream->last_pipe->pipe_settings.preview.copy_pipe;
		else if (stream->last_pipe->config.mode == IA_CSS_PIPE_MODE_VIDEO)
			pipe = stream->last_pipe->pipe_settings.video.copy_pipe;
	} else {
		pipe = stream->last_pipe;
	}

	if (!pipe)
		return -EINVAL;

	if (pipe->pipeline.stages)
		if (pipe->pipeline.stages->binary)
			binary = pipe->pipeline.stages->binary;

	if (binary) {
		/*
		 * this was being done in ifmtr in 2400.
		 * online and cont bypass the init_in_frameinfo_memory_defaults
		 * so need to do it here
		 */
		ia_css_get_crop_offsets(pipe, &binary->in_frame_info);
	}

	/* get the SP thread id */
	rc = ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &sp_thread_id);
	if (!rc)
		return -EINVAL;
	/* get the target input terminal */
	sp_pipeline_input_terminal = &sh_css_sp_group.pipe_io[sp_thread_id].input;

	for (i = 0; i < IA_CSS_STREAM_MAX_ISYS_STREAM_PER_CH; i++) {
		/* initialization */
		memset((void *)(&isys_stream_descr), 0, sizeof(ia_css_isys_descr_t));
		sp_pipeline_input_terminal->context.virtual_input_system_stream[i].valid = 0;
		sp_pipeline_input_terminal->ctrl.virtual_input_system_stream_cfg[i].valid = 0;

		if (!stream->config.isys_config[i].valid)
			continue;

		/* translate the stream configuration to the Input System (2401) configuration */
		rc = sh_css_translate_stream_cfg_to_isys_stream_descr(
			 &stream->config,
			 early_polling,
			 &(isys_stream_descr), i);

		if (stream->config.online) {
			rc &= sh_css_translate_binary_info_to_input_system_output_port_attr(
				  binary,
				  &(isys_stream_descr));
		}

		if (!rc)
			return -EINVAL;

		isys_stream_id = ia_css_isys_generate_stream_id(sp_thread_id, i);

		/* create the virtual Input System (2401) */
		rc =  ia_css_isys_stream_create(
			  &(isys_stream_descr),
			  &sp_pipeline_input_terminal->context.virtual_input_system_stream[i],
			  isys_stream_id);
		if (!rc)
			return -EINVAL;

		/* calculate the configuration of the virtual Input System (2401) */
		rc = ia_css_isys_stream_calculate_cfg(
			 &sp_pipeline_input_terminal->context.virtual_input_system_stream[i],
			 &(isys_stream_descr),
			 &sp_pipeline_input_terminal->ctrl.virtual_input_system_stream_cfg[i]);
		if (!rc) {
			ia_css_isys_stream_destroy(
			    &sp_pipeline_input_terminal->context.virtual_input_system_stream[i]);
			return -EINVAL;
		}
	}

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_config_input_network() leave:\n");

	return 0;
}

static inline struct ia_css_pipe *stream_get_last_pipe(
    struct ia_css_stream *stream)
{
	struct ia_css_pipe *last_pipe = NULL;

	if (stream)
		last_pipe = stream->last_pipe;

	return last_pipe;
}

static inline struct ia_css_pipe *stream_get_copy_pipe(
    struct ia_css_stream *stream)
{
	struct ia_css_pipe *copy_pipe = NULL;
	struct ia_css_pipe *last_pipe = NULL;
	enum ia_css_pipe_id pipe_id;

	last_pipe = stream_get_last_pipe(stream);

	if ((stream) &&
	    (last_pipe) &&
	    (stream->config.continuous)) {
		pipe_id = last_pipe->mode;
		switch (pipe_id) {
		case IA_CSS_PIPE_ID_PREVIEW:
			copy_pipe = last_pipe->pipe_settings.preview.copy_pipe;
			break;
		case IA_CSS_PIPE_ID_VIDEO:
			copy_pipe = last_pipe->pipe_settings.video.copy_pipe;
			break;
		default:
			copy_pipe = NULL;
			break;
		}
	}

	return copy_pipe;
}

static inline struct ia_css_pipe *stream_get_target_pipe(
    struct ia_css_stream *stream)
{
	struct ia_css_pipe *target_pipe;

	/* get the pipe that consumes the stream */
	if (stream->config.continuous)
		target_pipe = stream_get_copy_pipe(stream);
	else
		target_pipe = stream_get_last_pipe(stream);

	return target_pipe;
}

static int stream_csi_rx_helper(
    struct ia_css_stream *stream,
    int (*func)(enum mipi_port_id, uint32_t))
{
	int retval = -EINVAL;
	u32 sp_thread_id, stream_id;
	bool rc;
	struct ia_css_pipe *target_pipe = NULL;

	if ((!stream) || (stream->config.mode != IA_CSS_INPUT_MODE_BUFFERED_SENSOR))
		goto exit;

	target_pipe = stream_get_target_pipe(stream);

	if (!target_pipe)
		goto exit;

	rc = ia_css_pipeline_get_sp_thread_id(
		 ia_css_pipe_get_pipe_num(target_pipe),
		 &sp_thread_id);

	if (!rc)
		goto exit;

	/* (un)register all valid "virtual isys streams" within the ia_css_stream */
	stream_id = 0;
	do {
		if (stream->config.isys_config[stream_id].valid) {
			u32 isys_stream_id = ia_css_isys_generate_stream_id(sp_thread_id, stream_id);

			retval = func(stream->config.source.port.port, isys_stream_id);
		}
		stream_id++;
	} while ((retval == 0) &&
		 (stream_id < IA_CSS_STREAM_MAX_ISYS_STREAM_PER_CH));

exit:
	return retval;
}

static inline int stream_register_with_csi_rx(
    struct ia_css_stream *stream)
{
	return stream_csi_rx_helper(stream, ia_css_isys_csi_rx_register_stream);
}

static inline int stream_unregister_with_csi_rx(
    struct ia_css_stream *stream)
{
	return stream_csi_rx_helper(stream, ia_css_isys_csi_rx_unregister_stream);
}
#endif


static void
start_binary(struct ia_css_pipe *pipe,
	     struct ia_css_binary *binary)
{
	assert(pipe);
	/* Acceleration uses firmware, the binary thus can be NULL */

	if (binary)
		sh_css_metrics_start_binary(&binary->metrics);


#if !defined(ISP2401)
	if (pipe->stream->reconfigure_css_rx) {
		ia_css_isys_rx_configure(&pipe->stream->csi_rx_config,
					 pipe->stream->config.mode);
		pipe->stream->reconfigure_css_rx = false;
	}
#endif
}

/* start the copy function on the SP */
static int
start_copy_on_sp(struct ia_css_pipe *pipe,
		 struct ia_css_frame *out_frame)
{
	(void)out_frame;

	if ((!pipe) || (!pipe->stream))
		return -EINVAL;

#if !defined(ISP2401)
	if (pipe->stream->reconfigure_css_rx)
		ia_css_isys_rx_disable();
#endif

	if (pipe->stream->config.input_config.format != ATOMISP_INPUT_FORMAT_BINARY_8)
		return -EINVAL;
	sh_css_sp_start_binary_copy(ia_css_pipe_get_pipe_num(pipe), out_frame, pipe->stream->config.pixels_per_clock == 2);

#if !defined(ISP2401)
	if (pipe->stream->reconfigure_css_rx) {
		ia_css_isys_rx_configure(&pipe->stream->csi_rx_config,
					 pipe->stream->config.mode);
		pipe->stream->reconfigure_css_rx = false;
	}
#endif

	return 0;
}

void sh_css_binary_args_reset(struct sh_css_binary_args *args)
{
	unsigned int i;

	for (i = 0; i < NUM_VIDEO_TNR_FRAMES; i++)
		args->tnr_frames[i] = NULL;
	for (i = 0; i < MAX_NUM_VIDEO_DELAY_FRAMES; i++)
		args->delay_frames[i] = NULL;
	args->in_frame      = NULL;
	for (i = 0; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		args->out_frame[i] = NULL;
	args->out_vf_frame  = NULL;
	args->copy_vf       = false;
	args->copy_output   = true;
	args->vf_downscale_log2 = 0;
}

static void start_pipe(
    struct ia_css_pipe *me,
    enum sh_css_pipe_config_override copy_ovrd,
    enum ia_css_input_mode input_mode)
{
	IA_CSS_ENTER_PRIVATE("me = %p, copy_ovrd = %d, input_mode = %d",
			     me, copy_ovrd, input_mode);

	assert(me); /* all callers are in this file and call with non null argument */

	sh_css_sp_init_pipeline(&me->pipeline,
				me->mode,
				(uint8_t)ia_css_pipe_get_pipe_num(me),
				me->config.default_capture_config.enable_xnr != 0,
				me->stream->config.pixels_per_clock == 2,
				me->stream->config.continuous,
				false,
				me->required_bds_factor,
				copy_ovrd,
				input_mode,
				&me->stream->config.metadata_config,
				&me->stream->info.metadata_info
				, (input_mode == IA_CSS_INPUT_MODE_MEMORY) ?
				(enum mipi_port_id)0 :
				me->stream->config.source.port.port);

	if (me->config.mode != IA_CSS_PIPE_MODE_COPY) {
		struct ia_css_pipeline_stage *stage;

		stage = me->pipeline.stages;
		if (stage) {
			me->pipeline.current_stage = stage;
			start_binary(me, stage->binary);
		}
	}
	IA_CSS_LEAVE_PRIVATE("void");
}

void
sh_css_invalidate_shading_tables(struct ia_css_stream *stream)
{
	int i;

	assert(stream);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "sh_css_invalidate_shading_tables() enter:\n");

	for (i = 0; i < stream->num_pipes; i++) {
		assert(stream->pipes[i]);
		sh_css_pipe_free_shading_table(stream->pipes[i]);
	}

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "sh_css_invalidate_shading_tables() leave: return_void\n");
}

static void
enable_interrupts(enum ia_css_irq_type irq_type)
{
#ifndef ISP2401
	enum mipi_port_id port;
#endif
	bool enable_pulse = irq_type != IA_CSS_IRQ_TYPE_EDGE;

	IA_CSS_ENTER_PRIVATE("");
	/* Enable IRQ on the SP which signals that SP goes to idle
	 * (aka ready state) */
	cnd_sp_irq_enable(SP0_ID, true);
	/* Set the IRQ device 0 to either level or pulse */
	irq_enable_pulse(IRQ0_ID, enable_pulse);

	cnd_virq_enable_channel(virq_sp, true);

	/* Enable SW interrupt 0, this is used to signal ISYS events */
	cnd_virq_enable_channel(
	    (enum virq_id)(IRQ_SW_CHANNEL0_ID + IRQ_SW_CHANNEL_OFFSET),
	    true);
	/* Enable SW interrupt 1, this is used to signal PSYS events */
	cnd_virq_enable_channel(
	    (enum virq_id)(IRQ_SW_CHANNEL1_ID + IRQ_SW_CHANNEL_OFFSET),
	    true);

#ifndef ISP2401
	for (port = 0; port < N_MIPI_PORT_ID; port++)
		ia_css_isys_rx_enable_all_interrupts(port);
#endif

	IA_CSS_LEAVE_PRIVATE("");
}

static bool sh_css_setup_spctrl_config(const struct ia_css_fw_info *fw,
				       const char *program,
				       ia_css_spctrl_cfg  *spctrl_cfg)
{
	if ((!fw) || (!spctrl_cfg))
		return false;
	spctrl_cfg->sp_entry = 0;
	spctrl_cfg->program_name = (char *)(program);

	spctrl_cfg->ddr_data_offset =  fw->blob.data_source;
	spctrl_cfg->dmem_data_addr = fw->blob.data_target;
	spctrl_cfg->dmem_bss_addr = fw->blob.bss_target;
	spctrl_cfg->data_size = fw->blob.data_size;
	spctrl_cfg->bss_size = fw->blob.bss_size;

	spctrl_cfg->spctrl_config_dmem_addr = fw->info.sp.init_dmem_data;
	spctrl_cfg->spctrl_state_dmem_addr = fw->info.sp.sw_state;

	spctrl_cfg->code_size = fw->blob.size;
	spctrl_cfg->code      = fw->blob.code;
	spctrl_cfg->sp_entry  = fw->info.sp.sp_entry; /* entry function ptr on SP */

	return true;
}

void
ia_css_unload_firmware(void)
{
	if (sh_css_num_binaries) {
		/* we have already loaded before so get rid of the old stuff */
		ia_css_binary_uninit();
		sh_css_unload_firmware();
	}
	fw_explicitly_loaded = false;
}

static void
ia_css_reset_defaults(struct sh_css *css)
{
	struct sh_css default_css;

	/* Reset everything to zero */
	memset(&default_css, 0, sizeof(default_css));

	/* Initialize the non zero values */
	default_css.check_system_idle = true;
	default_css.num_cont_raw_frames = NUM_CONTINUOUS_FRAMES;

	/*
	 * All should be 0: but memset does it already.
	 * default_css.num_mipi_frames[N_CSI_PORTS] = 0;
	 */

	default_css.irq_type = IA_CSS_IRQ_TYPE_EDGE;

	/* Set the defaults to the output */
	*css = default_css;
}

int
ia_css_load_firmware(struct device *dev, const struct ia_css_env *env,
		     const struct ia_css_fw  *fw)
{
	int err;

	if (!env)
		return -EINVAL;
	if (!fw)
		return -EINVAL;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_load_firmware() enter\n");

	/* make sure we initialize my_css */
	if (my_css.flush != env->cpu_mem_env.flush) {
		ia_css_reset_defaults(&my_css);
		my_css.flush = env->cpu_mem_env.flush;
	}

	ia_css_unload_firmware(); /* in case we are called twice */
	err = sh_css_load_firmware(dev, fw->data, fw->bytes);
	if (!err) {
		err = ia_css_binary_init_infos();
		if (!err)
			fw_explicitly_loaded = true;
	}

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_load_firmware() leave\n");
	return err;
}

int
ia_css_init(struct device *dev, const struct ia_css_env *env,
	    const struct ia_css_fw  *fw,
	    u32                 mmu_l1_base,
	    enum ia_css_irq_type     irq_type)
{
	int err;
	ia_css_spctrl_cfg spctrl_cfg;

	void (*flush_func)(struct ia_css_acc_fw *fw);
	hrt_data select, enable;

	/*
	 * The C99 standard does not specify the exact object representation of structs;
	 * the representation is compiler dependent.
	 *
	 * The structs that are communicated between host and SP/ISP should have the
	 * exact same object representation. The compiler that is used to compile the
	 * firmware is hivecc.
	 *
	 * To check if a different compiler, used to compile a host application, uses
	 * another object representation, macros are defined specifying the size of
	 * the structs as expected by the firmware.
	 *
	 * A host application shall verify that a sizeof( ) of the struct is equal to
	 * the SIZE_OF_XXX macro of the corresponding struct. If they are not
	 * equal, functionality will break.
	 */

	/* Check struct sh_css_ddr_address_map */
	COMPILATION_ERROR_IF(sizeof(struct sh_css_ddr_address_map)		!= SIZE_OF_SH_CSS_DDR_ADDRESS_MAP_STRUCT);
	/* Check struct host_sp_queues */
	COMPILATION_ERROR_IF(sizeof(struct host_sp_queues)			!= SIZE_OF_HOST_SP_QUEUES_STRUCT);
	COMPILATION_ERROR_IF(sizeof(struct ia_css_circbuf_desc_s)		!= SIZE_OF_IA_CSS_CIRCBUF_DESC_S_STRUCT);
	COMPILATION_ERROR_IF(sizeof(struct ia_css_circbuf_elem_s)		!= SIZE_OF_IA_CSS_CIRCBUF_ELEM_S_STRUCT);

	/* Check struct host_sp_communication */
	COMPILATION_ERROR_IF(sizeof(struct host_sp_communication)		!= SIZE_OF_HOST_SP_COMMUNICATION_STRUCT);
	COMPILATION_ERROR_IF(sizeof(struct sh_css_event_irq_mask)		!= SIZE_OF_SH_CSS_EVENT_IRQ_MASK_STRUCT);

	/* Check struct sh_css_hmm_buffer */
	COMPILATION_ERROR_IF(sizeof(struct sh_css_hmm_buffer)			!= SIZE_OF_SH_CSS_HMM_BUFFER_STRUCT);
	COMPILATION_ERROR_IF(sizeof(struct ia_css_isp_3a_statistics)		!= SIZE_OF_IA_CSS_ISP_3A_STATISTICS_STRUCT);
	COMPILATION_ERROR_IF(sizeof(struct ia_css_isp_dvs_statistics)		!= SIZE_OF_IA_CSS_ISP_DVS_STATISTICS_STRUCT);
	COMPILATION_ERROR_IF(sizeof(struct ia_css_metadata)			!= SIZE_OF_IA_CSS_METADATA_STRUCT);

	/* Check struct ia_css_init_dmem_cfg */
	COMPILATION_ERROR_IF(sizeof(struct ia_css_sp_init_dmem_cfg)		!= SIZE_OF_IA_CSS_SP_INIT_DMEM_CFG_STRUCT);

	if (!fw && !fw_explicitly_loaded)
		return -EINVAL;
	if (!env)
		return -EINVAL;

	sh_css_printf = env->print_env.debug_print;

	IA_CSS_ENTER("void");

	flush_func     = env->cpu_mem_env.flush;

	pipe_global_init();
	ia_css_pipeline_init();
	ia_css_queue_map_init();

	ia_css_device_access_init(&env->hw_access_env);

	select = gpio_reg_load(GPIO0_ID, _gpio_block_reg_do_select)
	& (~GPIO_FLASH_PIN_MASK);
	enable = gpio_reg_load(GPIO0_ID, _gpio_block_reg_do_e)
	| GPIO_FLASH_PIN_MASK;
	sh_css_mmu_set_page_table_base_index(mmu_l1_base);

	my_css_save.mmu_base = mmu_l1_base;

	ia_css_reset_defaults(&my_css);

	my_css_save.driver_env = *env;
	my_css.flush     = flush_func;

	err = ia_css_rmgr_init();
	if (err) {
		IA_CSS_LEAVE_ERR(err);
		return err;
	}

	IA_CSS_LOG("init: %d", my_css_save_initialized);

	if (!my_css_save_initialized) {
		my_css_save_initialized = true;
		my_css_save.mode = sh_css_mode_working;
		memset(my_css_save.stream_seeds, 0,
		       sizeof(struct sh_css_stream_seed) * MAX_ACTIVE_STREAMS);
		IA_CSS_LOG("init: %d mode=%d", my_css_save_initialized, my_css_save.mode);
	}

	mipi_init();

#ifndef ISP2401
	/*
	 * In case this has been programmed already, update internal
	 * data structure ...
	 * DEPRECATED
	 */
	my_css.page_table_base_index = mmu_get_page_table_base_index(MMU0_ID);

#endif
	my_css.irq_type = irq_type;

	my_css_save.irq_type = irq_type;

	enable_interrupts(my_css.irq_type);

	/* configure GPIO to output mode */
	gpio_reg_store(GPIO0_ID, _gpio_block_reg_do_select, select);
	gpio_reg_store(GPIO0_ID, _gpio_block_reg_do_e, enable);
	gpio_reg_store(GPIO0_ID, _gpio_block_reg_do_0, 0);

	err = ia_css_refcount_init(REFCOUNT_SIZE);
	if (err) {
		IA_CSS_LEAVE_ERR(err);
		return err;
	}
	err = sh_css_params_init();
	if (err) {
		IA_CSS_LEAVE_ERR(err);
		return err;
	}
	if (fw) {
		ia_css_unload_firmware(); /* in case we already had firmware loaded */
		err = sh_css_load_firmware(dev, fw->data, fw->bytes);
		if (err) {
			IA_CSS_LEAVE_ERR(err);
			return err;
		}
		err = ia_css_binary_init_infos();
		if (err) {
			IA_CSS_LEAVE_ERR(err);
			return err;
		}
		fw_explicitly_loaded = false;

		my_css_save.loaded_fw = (struct ia_css_fw *)fw;
	}
	if (!sh_css_setup_spctrl_config(&sh_css_sp_fw, SP_PROG_NAME, &spctrl_cfg))
		return -EINVAL;

	err = ia_css_spctrl_load_fw(SP0_ID, &spctrl_cfg);
	if (err) {
		IA_CSS_LEAVE_ERR(err);
		return err;
	}

	if (!sh_css_hrt_system_is_idle()) {
		IA_CSS_LEAVE_ERR(-EBUSY);
		return -EBUSY;
	}
	/*
	 * can be called here, queuing works, but:
	 * - when sp is started later, it will wipe queued items
	 * so for now we leave it for later and make sure
	 * updates are not called to frequently.
	 * sh_css_init_buffer_queues();
	 */

#if defined(ISP2401)
	gp_device_reg_store(GP_DEVICE0_ID, _REG_GP_SWITCH_ISYS2401_ADDR, 1);
#endif


	if (!IS_ISP2401)
		dma_set_max_burst_size(DMA0_ID, HIVE_DMA_BUS_DDR_CONN,
				       ISP2400_DMA_MAX_BURST_LENGTH);
	else
		dma_set_max_burst_size(DMA0_ID, HIVE_DMA_BUS_DDR_CONN,
				       ISP2401_DMA_MAX_BURST_LENGTH);

	if (ia_css_isys_init() != INPUT_SYSTEM_ERR_NO_ERROR)
		err = -EINVAL;

	sh_css_params_map_and_store_default_gdc_lut();

	IA_CSS_LEAVE_ERR(err);
	return err;
}

int
ia_css_enable_isys_event_queue(bool enable)
{
	if (sh_css_sp_is_running())
		return -EBUSY;
	sh_css_sp_enable_isys_event_queue(enable);
	return 0;
}

/* For Acceleration API: Flush FW (shared buffer pointer) arguments */
void
sh_css_flush(struct ia_css_acc_fw *fw)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "sh_css_flush() enter:\n");
	if ((fw) && (my_css.flush))
		my_css.flush(fw);
}

/*
 * Mapping sp threads. Currently, this is done when a stream is created and
 * pipelines are ready to be converted to sp pipelines. Be careful if you are
 * doing it from stream_create since we could run out of sp threads due to
 * allocation on inactive pipelines.
 */
static int
map_sp_threads(struct ia_css_stream *stream, bool map)
{
	struct ia_css_pipe *main_pipe = NULL;
	struct ia_css_pipe *copy_pipe = NULL;
	struct ia_css_pipe *capture_pipe = NULL;
	struct ia_css_pipe *acc_pipe = NULL;
	int err = 0;
	enum ia_css_pipe_id pipe_id;

	IA_CSS_ENTER_PRIVATE("stream = %p, map = %s",
			     stream, map ? "true" : "false");

	if (!stream) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	main_pipe = stream->last_pipe;
	pipe_id	= main_pipe->mode;

	ia_css_pipeline_map(main_pipe->pipe_num, map);

	switch (pipe_id) {
	case IA_CSS_PIPE_ID_PREVIEW:
		copy_pipe    = main_pipe->pipe_settings.preview.copy_pipe;
		capture_pipe = main_pipe->pipe_settings.preview.capture_pipe;
		acc_pipe     = main_pipe->pipe_settings.preview.acc_pipe;
		break;

	case IA_CSS_PIPE_ID_VIDEO:
		copy_pipe    = main_pipe->pipe_settings.video.copy_pipe;
		capture_pipe = main_pipe->pipe_settings.video.capture_pipe;
		break;

	case IA_CSS_PIPE_ID_CAPTURE:
	case IA_CSS_PIPE_ID_ACC:
	default:
		break;
	}

	if (acc_pipe)
		ia_css_pipeline_map(acc_pipe->pipe_num, map);

	if (capture_pipe)
		ia_css_pipeline_map(capture_pipe->pipe_num, map);

	/* Firmware expects copy pipe to be the last pipe mapped. (if needed) */
	if (copy_pipe)
		ia_css_pipeline_map(copy_pipe->pipe_num, map);

	/* DH regular multi pipe - not continuous mode: map the next pipes too */
	if (!stream->config.continuous) {
		int i;

		for (i = 1; i < stream->num_pipes; i++)
			ia_css_pipeline_map(stream->pipes[i]->pipe_num, map);
	}

	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

/*
 * creates a host pipeline skeleton for all pipes in a stream. Called during
 * stream_create.
 */
static int
create_host_pipeline_structure(struct ia_css_stream *stream)
{
	struct ia_css_pipe *copy_pipe = NULL, *capture_pipe = NULL;
	struct ia_css_pipe *acc_pipe = NULL;
	enum ia_css_pipe_id pipe_id;
	struct ia_css_pipe *main_pipe = NULL;
	int err = 0;
	unsigned int copy_pipe_delay = 0,
	capture_pipe_delay = 0;

	IA_CSS_ENTER_PRIVATE("stream = %p", stream);

	if (!stream) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	main_pipe	= stream->last_pipe;
	if (!main_pipe) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	pipe_id	= main_pipe->mode;

	switch (pipe_id) {
	case IA_CSS_PIPE_ID_PREVIEW:
		copy_pipe    = main_pipe->pipe_settings.preview.copy_pipe;
		copy_pipe_delay = main_pipe->dvs_frame_delay;
		capture_pipe = main_pipe->pipe_settings.preview.capture_pipe;
		capture_pipe_delay = IA_CSS_FRAME_DELAY_0;
		acc_pipe     = main_pipe->pipe_settings.preview.acc_pipe;
		err = ia_css_pipeline_create(&main_pipe->pipeline, main_pipe->mode,
					     main_pipe->pipe_num, main_pipe->dvs_frame_delay);
		break;

	case IA_CSS_PIPE_ID_VIDEO:
		copy_pipe    = main_pipe->pipe_settings.video.copy_pipe;
		copy_pipe_delay = main_pipe->dvs_frame_delay;
		capture_pipe = main_pipe->pipe_settings.video.capture_pipe;
		capture_pipe_delay = IA_CSS_FRAME_DELAY_0;
		err = ia_css_pipeline_create(&main_pipe->pipeline, main_pipe->mode,
					     main_pipe->pipe_num, main_pipe->dvs_frame_delay);
		break;

	case IA_CSS_PIPE_ID_CAPTURE:
		capture_pipe = main_pipe;
		capture_pipe_delay = main_pipe->dvs_frame_delay;
		break;

	case IA_CSS_PIPE_ID_YUVPP:
		err = ia_css_pipeline_create(&main_pipe->pipeline, main_pipe->mode,
					     main_pipe->pipe_num, main_pipe->dvs_frame_delay);
		break;

	case IA_CSS_PIPE_ID_ACC:
		err = ia_css_pipeline_create(&main_pipe->pipeline, main_pipe->mode,
					     main_pipe->pipe_num, main_pipe->dvs_frame_delay);
		break;

	default:
		err = -EINVAL;
	}

	if (!(err) && copy_pipe)
		err = ia_css_pipeline_create(&copy_pipe->pipeline,
					     copy_pipe->mode,
					     copy_pipe->pipe_num,
					     copy_pipe_delay);

	if (!(err) && capture_pipe)
		err = ia_css_pipeline_create(&capture_pipe->pipeline,
					     capture_pipe->mode,
					     capture_pipe->pipe_num,
					     capture_pipe_delay);

	if (!(err) && acc_pipe)
		err = ia_css_pipeline_create(&acc_pipe->pipeline, acc_pipe->mode,
					     acc_pipe->pipe_num, main_pipe->dvs_frame_delay);

	/* DH regular multi pipe - not continuous mode: create the next pipelines too */
	if (!stream->config.continuous) {
		int i;

		for (i = 1; i < stream->num_pipes && 0 == err; i++) {
			main_pipe = stream->pipes[i];
			err = ia_css_pipeline_create(&main_pipe->pipeline,
						     main_pipe->mode,
						     main_pipe->pipe_num,
						     main_pipe->dvs_frame_delay);
		}
	}

	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

/*
 * creates a host pipeline for all pipes in a stream. Called during
 * stream_start.
 */
static int
create_host_pipeline(struct ia_css_stream *stream)
{
	struct ia_css_pipe *copy_pipe = NULL, *capture_pipe = NULL;
	struct ia_css_pipe *acc_pipe = NULL;
	enum ia_css_pipe_id pipe_id;
	struct ia_css_pipe *main_pipe = NULL;
	int err = 0;
	unsigned int max_input_width = 0;

	IA_CSS_ENTER_PRIVATE("stream = %p", stream);
	if (!stream) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	main_pipe	= stream->last_pipe;
	pipe_id	= main_pipe->mode;

	/*
	 * No continuous frame allocation for capture pipe. It uses the
	 * "main" pipe's frames.
	 */
	if ((pipe_id == IA_CSS_PIPE_ID_PREVIEW) ||
	    (pipe_id == IA_CSS_PIPE_ID_VIDEO)) {
		/*
		 * About
		 *    pipe_id == IA_CSS_PIPE_ID_PREVIEW &&
		 *    stream->config.mode != IA_CSS_INPUT_MODE_MEMORY:
		 *
		 * The original condition pipe_id == IA_CSS_PIPE_ID_PREVIEW is
		 * too strong. E.g. in SkyCam (with memory based input frames)
		 * there is no continuous mode and thus no need for allocated
		 * continuous frames.
		 * This is not only for SkyCam but for all preview cases that
		 * use DDR based input frames. For this reason the
		 * stream->config.mode != IA_CSS_INPUT_MODE_MEMORY has beed
		 * added.
		 */
		if (stream->config.continuous ||
		    (pipe_id == IA_CSS_PIPE_ID_PREVIEW &&
		     stream->config.mode != IA_CSS_INPUT_MODE_MEMORY)) {
			err = alloc_continuous_frames(main_pipe, true);
			if (err)
				goto ERR;
		}
	}

#if !defined(ISP2401)
	/* old isys: need to allocate_mipi_frames() even in IA_CSS_PIPE_MODE_COPY */
	if (pipe_id != IA_CSS_PIPE_ID_ACC) {
		err = allocate_mipi_frames(main_pipe, &stream->info);
		if (err)
			goto ERR;
	}
#elif defined(ISP2401)
	if ((pipe_id != IA_CSS_PIPE_ID_ACC) &&
	    (main_pipe->config.mode != IA_CSS_PIPE_MODE_COPY)) {
		err = allocate_mipi_frames(main_pipe, &stream->info);
		if (err)
			goto ERR;
	}
#endif

	switch (pipe_id) {
	case IA_CSS_PIPE_ID_PREVIEW:
		copy_pipe    = main_pipe->pipe_settings.preview.copy_pipe;
		capture_pipe = main_pipe->pipe_settings.preview.capture_pipe;
		acc_pipe     = main_pipe->pipe_settings.preview.acc_pipe;
		max_input_width =
		    main_pipe->pipe_settings.preview.preview_binary.info->sp.input.max_width;

		err = create_host_preview_pipeline(main_pipe);
		if (err)
			goto ERR;

		break;

	case IA_CSS_PIPE_ID_VIDEO:
		copy_pipe    = main_pipe->pipe_settings.video.copy_pipe;
		capture_pipe = main_pipe->pipe_settings.video.capture_pipe;
		max_input_width =
		    main_pipe->pipe_settings.video.video_binary.info->sp.input.max_width;

		err = create_host_video_pipeline(main_pipe);
		if (err)
			goto ERR;

		break;

	case IA_CSS_PIPE_ID_CAPTURE:
		capture_pipe = main_pipe;

		break;

	case IA_CSS_PIPE_ID_YUVPP:
		err = create_host_yuvpp_pipeline(main_pipe);
		if (err)
			goto ERR;

		break;

	case IA_CSS_PIPE_ID_ACC:
		err = create_host_acc_pipeline(main_pipe);
		if (err)
			goto ERR;

		break;
	default:
		err = -EINVAL;
	}
	if (err)
		goto ERR;

	if (copy_pipe) {
		err = create_host_copy_pipeline(copy_pipe, max_input_width,
						main_pipe->continuous_frames[0]);
		if (err)
			goto ERR;
	}

	if (capture_pipe) {
		err = create_host_capture_pipeline(capture_pipe);
		if (err)
			goto ERR;
	}

	if (acc_pipe) {
		err = create_host_acc_pipeline(acc_pipe);
		if (err)
			goto ERR;
	}

	/* DH regular multi pipe - not continuous mode: create the next pipelines too */
	if (!stream->config.continuous) {
		int i;

		for (i = 1; i < stream->num_pipes && 0 == err; i++) {
			switch (stream->pipes[i]->mode) {
			case IA_CSS_PIPE_ID_PREVIEW:
				err = create_host_preview_pipeline(stream->pipes[i]);
				break;
			case IA_CSS_PIPE_ID_VIDEO:
				err = create_host_video_pipeline(stream->pipes[i]);
				break;
			case IA_CSS_PIPE_ID_CAPTURE:
				err = create_host_capture_pipeline(stream->pipes[i]);
				break;
			case IA_CSS_PIPE_ID_YUVPP:
				err = create_host_yuvpp_pipeline(stream->pipes[i]);
				break;
			case IA_CSS_PIPE_ID_ACC:
				err = create_host_acc_pipeline(stream->pipes[i]);
				break;
			default:
				err = -EINVAL;
			}
			if (err)
				goto ERR;
		}
	}

ERR:
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static const struct ia_css_pipe default_pipe = IA_CSS_DEFAULT_PIPE;
static const struct ia_css_preview_settings preview = IA_CSS_DEFAULT_PREVIEW_SETTINGS;
static const struct ia_css_capture_settings capture = IA_CSS_DEFAULT_CAPTURE_SETTINGS;
static const struct ia_css_video_settings video = IA_CSS_DEFAULT_VIDEO_SETTINGS;
static const struct ia_css_yuvpp_settings yuvpp = IA_CSS_DEFAULT_YUVPP_SETTINGS;

static int
init_pipe_defaults(enum ia_css_pipe_mode mode,
		   struct ia_css_pipe *pipe,
		   bool copy_pipe)
{
	if (!pipe) {
		IA_CSS_ERROR("NULL pipe parameter");
		return -EINVAL;
	}

	/* Initialize pipe to pre-defined defaults */
	memcpy(pipe, &default_pipe, sizeof(default_pipe));

	/* TODO: JB should not be needed, but temporary backward reference */
	switch (mode) {
	case IA_CSS_PIPE_MODE_PREVIEW:
		pipe->mode = IA_CSS_PIPE_ID_PREVIEW;
		memcpy(&pipe->pipe_settings.preview, &preview, sizeof(preview));
		break;
	case IA_CSS_PIPE_MODE_CAPTURE:
		if (copy_pipe)
			pipe->mode = IA_CSS_PIPE_ID_COPY;
		else
			pipe->mode = IA_CSS_PIPE_ID_CAPTURE;

		memcpy(&pipe->pipe_settings.capture, &capture, sizeof(capture));
		break;
	case IA_CSS_PIPE_MODE_VIDEO:
		pipe->mode = IA_CSS_PIPE_ID_VIDEO;
		memcpy(&pipe->pipe_settings.video, &video, sizeof(video));
		break;
	case IA_CSS_PIPE_MODE_ACC:
		pipe->mode = IA_CSS_PIPE_ID_ACC;
		break;
	case IA_CSS_PIPE_MODE_COPY:
		pipe->mode = IA_CSS_PIPE_ID_CAPTURE;
		break;
	case IA_CSS_PIPE_MODE_YUVPP:
		pipe->mode = IA_CSS_PIPE_ID_YUVPP;
		memcpy(&pipe->pipe_settings.yuvpp, &yuvpp, sizeof(yuvpp));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void
pipe_global_init(void)
{
	u8 i;

	my_css.pipe_counter = 0;
	for (i = 0; i < IA_CSS_PIPELINE_NUM_MAX; i++)
		my_css.all_pipes[i] = NULL;
}

static int
pipe_generate_pipe_num(const struct ia_css_pipe *pipe,
		       unsigned int *pipe_number)
{
	const u8 INVALID_PIPE_NUM = (uint8_t)~(0);
	u8 pipe_num = INVALID_PIPE_NUM;
	u8 i;

	if (!pipe) {
		IA_CSS_ERROR("NULL pipe parameter");
		return -EINVAL;
	}

	/* Assign a new pipe_num .... search for empty place */
	for (i = 0; i < IA_CSS_PIPELINE_NUM_MAX; i++) {
		if (!my_css.all_pipes[i]) {
			/* position is reserved */
			my_css.all_pipes[i] = (struct ia_css_pipe *)pipe;
			pipe_num = i;
			break;
		}
	}
	if (pipe_num == INVALID_PIPE_NUM) {
		/* Max number of pipes already allocated */
		IA_CSS_ERROR("Max number of pipes already created");
		return -ENOSPC;
	}

	my_css.pipe_counter++;

	IA_CSS_LOG("pipe_num (%d)", pipe_num);

	*pipe_number = pipe_num;
	return 0;
}

static void
pipe_release_pipe_num(unsigned int pipe_num)
{
	my_css.all_pipes[pipe_num] = NULL;
	my_css.pipe_counter--;
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "pipe_release_pipe_num (%d)\n", pipe_num);
}

static int
create_pipe(enum ia_css_pipe_mode mode,
	    struct ia_css_pipe **pipe,
	    bool copy_pipe)
{
	int err = 0;
	struct ia_css_pipe *me;

	if (!pipe) {
		IA_CSS_ERROR("NULL pipe parameter");
		return -EINVAL;
	}

	me = kmalloc(sizeof(*me), GFP_KERNEL);
	if (!me)
		return -ENOMEM;

	err = init_pipe_defaults(mode, me, copy_pipe);
	if (err) {
		kfree(me);
		return err;
	}

	err = pipe_generate_pipe_num(me, &me->pipe_num);
	if (err) {
		kfree(me);
		return err;
	}

	*pipe = me;
	return 0;
}

struct ia_css_pipe *
find_pipe_by_num(uint32_t pipe_num)
{
	unsigned int i;

	for (i = 0; i < IA_CSS_PIPELINE_NUM_MAX; i++) {
		if (my_css.all_pipes[i] &&
		    ia_css_pipe_get_pipe_num(my_css.all_pipes[i]) == pipe_num) {
			return my_css.all_pipes[i];
		}
	}
	return NULL;
}

static void sh_css_pipe_free_acc_binaries(
    struct ia_css_pipe *pipe)
{
	struct ia_css_pipeline *pipeline;
	struct ia_css_pipeline_stage *stage;

	if (!pipe) {
		IA_CSS_ERROR("NULL input pointer");
		return;
	}
	pipeline = &pipe->pipeline;

	/* loop through the stages and unload them */
	for (stage = pipeline->stages; stage; stage = stage->next) {
		struct ia_css_fw_info *firmware = (struct ia_css_fw_info *)
						  stage->firmware;
		if (firmware)
			ia_css_pipe_unload_extension(pipe, firmware);
	}
}

int
ia_css_pipe_destroy(struct ia_css_pipe *pipe)
{
	int err = 0;

	IA_CSS_ENTER("pipe = %p", pipe);

	if (!pipe) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	if (pipe->stream) {
		IA_CSS_LOG("ia_css_stream_destroy not called!");
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	switch (pipe->config.mode) {
	case IA_CSS_PIPE_MODE_PREVIEW:
		/*
		 * need to take into account that this function is also called
		 * on the internal copy pipe
		 */
		if (pipe->mode == IA_CSS_PIPE_ID_PREVIEW) {
			ia_css_frame_free_multiple(NUM_CONTINUOUS_FRAMES,
						   pipe->continuous_frames);
			ia_css_metadata_free_multiple(NUM_CONTINUOUS_FRAMES,
						      pipe->cont_md_buffers);
			if (pipe->pipe_settings.preview.copy_pipe) {
				err = ia_css_pipe_destroy(pipe->pipe_settings.preview.copy_pipe);
				ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
						    "ia_css_pipe_destroy(): destroyed internal copy pipe err=%d\n",
						    err);
			}
		}
		break;
	case IA_CSS_PIPE_MODE_VIDEO:
		if (pipe->mode == IA_CSS_PIPE_ID_VIDEO) {
			ia_css_frame_free_multiple(NUM_CONTINUOUS_FRAMES,
						   pipe->continuous_frames);
			ia_css_metadata_free_multiple(NUM_CONTINUOUS_FRAMES,
						      pipe->cont_md_buffers);
			if (pipe->pipe_settings.video.copy_pipe) {
				err = ia_css_pipe_destroy(pipe->pipe_settings.video.copy_pipe);
				ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
						    "ia_css_pipe_destroy(): destroyed internal copy pipe err=%d\n",
						    err);
			}
		}
#ifndef ISP2401
		ia_css_frame_free_multiple(NUM_VIDEO_TNR_FRAMES,
					   pipe->pipe_settings.video.tnr_frames);
#else
		ia_css_frame_free_multiple(NUM_VIDEO_TNR_FRAMES,
					   pipe->pipe_settings.video.tnr_frames);
#endif
		ia_css_frame_free_multiple(MAX_NUM_VIDEO_DELAY_FRAMES,
					   pipe->pipe_settings.video.delay_frames);
		break;
	case IA_CSS_PIPE_MODE_CAPTURE:
		ia_css_frame_free_multiple(MAX_NUM_VIDEO_DELAY_FRAMES,
					   pipe->pipe_settings.capture.delay_frames);
		break;
	case IA_CSS_PIPE_MODE_ACC:
		sh_css_pipe_free_acc_binaries(pipe);
		break;
	case IA_CSS_PIPE_MODE_COPY:
		break;
	case IA_CSS_PIPE_MODE_YUVPP:
		break;
	}

	if (pipe->scaler_pp_lut != mmgr_NULL) {
		hmm_free(pipe->scaler_pp_lut);
		pipe->scaler_pp_lut = mmgr_NULL;
	}

	my_css.active_pipes[ia_css_pipe_get_pipe_num(pipe)] = NULL;
	sh_css_pipe_free_shading_table(pipe);

	ia_css_pipeline_destroy(&pipe->pipeline);
	pipe_release_pipe_num(ia_css_pipe_get_pipe_num(pipe));

	/* Temporarily, not every sh_css_pipe has an acc_extension. */
	if (pipe->config.acc_extension)
		ia_css_pipe_unload_extension(pipe, pipe->config.acc_extension);

	kfree(pipe);
	IA_CSS_LEAVE("err = %d", err);
	return err;
}

void
ia_css_uninit(void)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_uninit() enter: void\n");

	sh_css_params_free_default_gdc_lut();

	/* TODO: JB: implement decent check and handling of freeing mipi frames */
	if (!mipi_is_free())
		dev_warn(atomisp_dev, "mipi frames are not freed.\n");

	/* cleanup generic data */
	sh_css_params_uninit();
	ia_css_refcount_uninit();

	ia_css_rmgr_uninit();

#if !defined(ISP2401)
	/* needed for reprogramming the inputformatter after power cycle of css */
	ifmtr_set_if_blocking_mode_reset = true;
#endif

	if (!fw_explicitly_loaded)
		ia_css_unload_firmware();

	ia_css_spctrl_unload_fw(SP0_ID);
	sh_css_sp_set_sp_running(false);
	/* check and free any remaining mipi frames */
	free_mipi_frames(NULL);

	sh_css_sp_reset_global_vars();

	ia_css_isys_uninit();

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_uninit() leave: return_void\n");
}

int ia_css_irq_translate(
    unsigned int *irq_infos)
{
	enum virq_id	irq;
	enum hrt_isp_css_irq_status status = hrt_isp_css_irq_status_more_irqs;
	unsigned int infos = 0;

	/* irq_infos can be NULL, but that would make the function useless */
	/* assert(irq_infos != NULL); */
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_irq_translate() enter: irq_infos=%p\n", irq_infos);

	while (status == hrt_isp_css_irq_status_more_irqs) {
		status = virq_get_channel_id(&irq);
		if (status == hrt_isp_css_irq_status_error)
			return -EINVAL;


		switch (irq) {
		case virq_sp:
			/*
			 * When SP goes to idle, info is available in the
			 * event queue.
			 */
			infos |= IA_CSS_IRQ_INFO_EVENTS_READY;
			break;
		case virq_isp:
			break;
		case virq_isys_sof:
			infos |= IA_CSS_IRQ_INFO_CSS_RECEIVER_SOF;
			break;
		case virq_isys_eof:
			infos |= IA_CSS_IRQ_INFO_CSS_RECEIVER_EOF;
			break;
		case virq_isys_csi:
			infos |= IA_CSS_IRQ_INFO_INPUT_SYSTEM_ERROR;
			break;
#if !defined(ISP2401)
		case virq_ifmt0_id:
			infos |= IA_CSS_IRQ_INFO_IF_ERROR;
			break;
#endif
		case virq_dma:
			infos |= IA_CSS_IRQ_INFO_DMA_ERROR;
			break;
		case virq_sw_pin_0:
			infos |= sh_css_get_sw_interrupt_value(0);
			break;
		case virq_sw_pin_1:
			infos |= sh_css_get_sw_interrupt_value(1);
			/* pqiao TODO: also assumption here */
			break;
		default:
			break;
		}
	}

	if (irq_infos)
		*irq_infos = infos;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_irq_translate() leave: irq_infos=%u\n",
			    infos);

	return 0;
}

int ia_css_irq_enable(
    enum ia_css_irq_info info,
    bool enable)
{
	enum virq_id	irq = N_virq_id;

	IA_CSS_ENTER("info=%d, enable=%d", info, enable);

	switch (info) {
#if !defined(ISP2401)
	case IA_CSS_IRQ_INFO_CSS_RECEIVER_SOF:
		irq = virq_isys_sof;
		break;
	case IA_CSS_IRQ_INFO_CSS_RECEIVER_EOF:
		irq = virq_isys_eof;
		break;
	case IA_CSS_IRQ_INFO_INPUT_SYSTEM_ERROR:
		irq = virq_isys_csi;
		break;
	case IA_CSS_IRQ_INFO_IF_ERROR:
		irq = virq_ifmt0_id;
		break;
#else
	case IA_CSS_IRQ_INFO_CSS_RECEIVER_SOF:
	case IA_CSS_IRQ_INFO_CSS_RECEIVER_EOF:
	case IA_CSS_IRQ_INFO_INPUT_SYSTEM_ERROR:
	case IA_CSS_IRQ_INFO_IF_ERROR:
		/* Just ignore those unused IRQs without printing errors */
		return 0;
#endif
	case IA_CSS_IRQ_INFO_DMA_ERROR:
		irq = virq_dma;
		break;
	case IA_CSS_IRQ_INFO_SW_0:
		irq = virq_sw_pin_0;
		break;
	case IA_CSS_IRQ_INFO_SW_1:
		irq = virq_sw_pin_1;
		break;
	default:
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	cnd_virq_enable_channel(irq, enable);

	IA_CSS_LEAVE_ERR(0);
	return 0;
}


static unsigned int
sh_css_get_sw_interrupt_value(unsigned int irq)
{
	unsigned int irq_value;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "sh_css_get_sw_interrupt_value() enter: irq=%d\n", irq);
	irq_value = sh_css_sp_get_sw_interrupt_value(irq);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "sh_css_get_sw_interrupt_value() leave: irq_value=%d\n", irq_value);
	return irq_value;
}

/*
 * configure and load the copy binary, the next binary is used to
 * determine whether the copy binary needs to do left padding.
 */
static int load_copy_binary(
    struct ia_css_pipe *pipe,
    struct ia_css_binary *copy_binary,
    struct ia_css_binary *next_binary)
{
	struct ia_css_frame_info copy_out_info, copy_in_info, copy_vf_info;
	unsigned int left_padding;
	int err;
	struct ia_css_binary_descr copy_descr;

	/* next_binary can be NULL */
	assert(pipe);
	assert(copy_binary);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "load_copy_binary() enter:\n");

	if (next_binary) {
		copy_out_info = next_binary->in_frame_info;
		left_padding = next_binary->left_padding;
	} else {
		copy_out_info = pipe->output_info[0];
		copy_vf_info = pipe->vf_output_info[0];
		ia_css_frame_info_set_format(&copy_vf_info, IA_CSS_FRAME_FORMAT_YUV_LINE);
		left_padding = 0;
	}

	ia_css_pipe_get_copy_binarydesc(pipe, &copy_descr,
					&copy_in_info, &copy_out_info,
					(next_binary) ? NULL : NULL/*TODO: &copy_vf_info*/);
	err = ia_css_binary_find(&copy_descr, copy_binary);
	if (err)
		return err;
	copy_binary->left_padding = left_padding;
	return 0;
}

static int
alloc_continuous_frames(struct ia_css_pipe *pipe, bool init_time)
{
	int err = 0;
	struct ia_css_frame_info ref_info;
	enum ia_css_pipe_id pipe_id;
	bool continuous;
	unsigned int i, idx;
	unsigned int num_frames;

	IA_CSS_ENTER_PRIVATE("pipe = %p, init_time = %d", pipe, init_time);

	if ((!pipe) || (!pipe->stream)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	pipe_id = pipe->mode;
	continuous = pipe->stream->config.continuous;

	if (continuous) {
		if (init_time) {
			num_frames = pipe->stream->config.init_num_cont_raw_buf;
			pipe->stream->continuous_pipe = pipe;
		} else {
			num_frames = pipe->stream->config.target_num_cont_raw_buf;
		}
	} else {
		num_frames = NUM_ONLINE_INIT_CONTINUOUS_FRAMES;
	}

	if (pipe_id == IA_CSS_PIPE_ID_PREVIEW) {
		ref_info = pipe->pipe_settings.preview.preview_binary.in_frame_info;
	} else if (pipe_id == IA_CSS_PIPE_ID_VIDEO) {
		ref_info = pipe->pipe_settings.video.video_binary.in_frame_info;
	} else {
		/* should not happen */
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

#if defined(ISP2401)
	/* For CSI2+, the continuous frame will hold the full input frame */
	ref_info.res.width = pipe->stream->config.input_config.input_res.width;
	ref_info.res.height = pipe->stream->config.input_config.input_res.height;

	/* Ensure padded width is aligned for 2401 */
	ref_info.padded_width = CEIL_MUL(ref_info.res.width, 2 * ISP_VEC_NELEMS);
#endif

	if (pipe->stream->config.pack_raw_pixels) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
				    "alloc_continuous_frames() IA_CSS_FRAME_FORMAT_RAW_PACKED\n");
		ref_info.format = IA_CSS_FRAME_FORMAT_RAW_PACKED;
	} else
	{
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
				    "alloc_continuous_frames() IA_CSS_FRAME_FORMAT_RAW\n");
		ref_info.format = IA_CSS_FRAME_FORMAT_RAW;
	}

	/* Write format back to binary */
	if (pipe_id == IA_CSS_PIPE_ID_PREVIEW) {
		pipe->pipe_settings.preview.preview_binary.in_frame_info.format =
		    ref_info.format;
	} else if (pipe_id == IA_CSS_PIPE_ID_VIDEO) {
		pipe->pipe_settings.video.video_binary.in_frame_info.format = ref_info.format;
	} else {
		/* should not happen */
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	if (init_time)
		idx = 0;
	else
		idx = pipe->stream->config.init_num_cont_raw_buf;

	for (i = idx; i < NUM_CONTINUOUS_FRAMES; i++) {
		/* free previous frame */
		if (pipe->continuous_frames[i]) {
			ia_css_frame_free(pipe->continuous_frames[i]);
			pipe->continuous_frames[i] = NULL;
		}
		/* free previous metadata buffer */
		ia_css_metadata_free(pipe->cont_md_buffers[i]);
		pipe->cont_md_buffers[i] = NULL;

		/* check if new frame needed */
		if (i < num_frames) {
			/* allocate new frame */
			err = ia_css_frame_allocate_from_info(
				  &pipe->continuous_frames[i],
				  &ref_info);
			if (err) {
				IA_CSS_LEAVE_ERR_PRIVATE(err);
				return err;
			}
			/* allocate metadata buffer */
			pipe->cont_md_buffers[i] = ia_css_metadata_allocate(
						       &pipe->stream->info.metadata_info);
		}
	}
	IA_CSS_LEAVE_ERR_PRIVATE(0);
	return 0;
}

int
ia_css_alloc_continuous_frame_remain(struct ia_css_stream *stream)
{
	if (!stream)
		return -EINVAL;
	return alloc_continuous_frames(stream->continuous_pipe, false);
}

static int
load_preview_binaries(struct ia_css_pipe *pipe)
{
	struct ia_css_frame_info prev_in_info,
		prev_bds_out_info,
		prev_out_info,
		prev_vf_info;
	struct ia_css_binary_descr preview_descr;
	bool online;
	int err = 0;
	bool need_vf_pp = false;
	bool need_isp_copy_binary = false;
#ifdef ISP2401
	bool sensor = false;
#else
	bool continuous;
#endif
	/* preview only have 1 output pin now */
	struct ia_css_frame_info *pipe_out_info = &pipe->output_info[0];
	struct ia_css_preview_settings *mycs  = &pipe->pipe_settings.preview;

	IA_CSS_ENTER_PRIVATE("");
	assert(pipe);
	assert(pipe->stream);
	assert(pipe->mode == IA_CSS_PIPE_ID_PREVIEW);

	online = pipe->stream->config.online;
#ifdef ISP2401
	sensor = pipe->stream->config.mode == IA_CSS_INPUT_MODE_SENSOR;
#else
	continuous = pipe->stream->config.continuous;
#endif

	if (mycs->preview_binary.info)
		return 0;

	err = ia_css_util_check_input(&pipe->stream->config, false, false);
	if (err)
		return err;
	err = ia_css_frame_check_info(pipe_out_info);
	if (err)
		return err;

	/*
	 * Note: the current selection of vf_pp binary and
	 * parameterization of the preview binary contains a few pieces
	 * of hardcoded knowledge. This needs to be cleaned up such that
	 * the binary selection becomes more generic.
	 * The vf_pp binary is needed if one or more of the following features
	 * are required:
	 * 1. YUV downscaling.
	 * 2. Digital zoom.
	 * 3. An output format that is not supported by the preview binary.
	 *    In practice this means something other than yuv_line or nv12.
	 * The decision if the vf_pp binary is needed for YUV downscaling is
	 * made after the preview binary selection, since some preview binaries
	 * can perform the requested YUV downscaling.
	 */
	need_vf_pp = pipe->config.enable_dz;
	need_vf_pp |= pipe_out_info->format != IA_CSS_FRAME_FORMAT_YUV_LINE &&
	!(pipe_out_info->format == IA_CSS_FRAME_FORMAT_NV12 ||
	  pipe_out_info->format == IA_CSS_FRAME_FORMAT_NV12_16 ||
	  pipe_out_info->format == IA_CSS_FRAME_FORMAT_NV12_TILEY);

	/* Preview step 1 */
	if (pipe->vf_yuv_ds_input_info.res.width)
		prev_vf_info = pipe->vf_yuv_ds_input_info;
	else
		prev_vf_info = *pipe_out_info;
	/*
	 * If vf_pp is needed, then preview must output yuv_line.
	 * The exception is when vf_pp is manually disabled, that is only
	 * used in combination with a pipeline extension that requires
	 * yuv_line as input.
	 */
	if (need_vf_pp)
		ia_css_frame_info_set_format(&prev_vf_info,
					     IA_CSS_FRAME_FORMAT_YUV_LINE);

	err = ia_css_pipe_get_preview_binarydesc(
	    pipe,
	    &preview_descr,
	    &prev_in_info,
	    &prev_bds_out_info,
	    &prev_out_info,
	    &prev_vf_info);
	if (err)
		return err;
	err = ia_css_binary_find(&preview_descr, &mycs->preview_binary);
	if (err)
		return err;

	/* The vf_pp binary is needed when (further) YUV downscaling is required */
	need_vf_pp |= mycs->preview_binary.out_frame_info[0].res.width != pipe_out_info->res.width;
	need_vf_pp |= mycs->preview_binary.out_frame_info[0].res.height != pipe_out_info->res.height;

	/*
	 * When vf_pp is needed, then the output format of the selected
	 * preview binary must be yuv_line. If this is not the case,
	 * then the preview binary selection is done again.
	 */
	if (need_vf_pp &&
	    (mycs->preview_binary.out_frame_info[0].format != IA_CSS_FRAME_FORMAT_YUV_LINE)) {
		/* Preview step 2 */
		if (pipe->vf_yuv_ds_input_info.res.width)
			prev_vf_info = pipe->vf_yuv_ds_input_info;
		else
			prev_vf_info = *pipe_out_info;

		ia_css_frame_info_set_format(&prev_vf_info,
					     IA_CSS_FRAME_FORMAT_YUV_LINE);

		err = ia_css_pipe_get_preview_binarydesc(
		    pipe,
		    &preview_descr,
		    &prev_in_info,
		    &prev_bds_out_info,
		    &prev_out_info,
		    &prev_vf_info);
		if (err)
			return err;
		err = ia_css_binary_find(&preview_descr,
					 &mycs->preview_binary);
		if (err)
			return err;
	}

	if (need_vf_pp) {
		struct ia_css_binary_descr vf_pp_descr;

		/* Viewfinder post-processing */
		ia_css_pipe_get_vfpp_binarydesc(pipe, &vf_pp_descr,
						&mycs->preview_binary.out_frame_info[0],
						pipe_out_info);
		err = ia_css_binary_find(&vf_pp_descr,
					 &mycs->vf_pp_binary);
		if (err)
			return err;
	}

#ifdef ISP2401
	/*
	 * When the input system is 2401, only the Direct Sensor Mode
	 * Offline Preview uses the ISP copy binary.
	 */
	need_isp_copy_binary = !online && sensor;
#else
	/*
	 * About pipe->stream->config.mode == IA_CSS_INPUT_MODE_MEMORY:
	 * This is typical the case with SkyCam (which has no input system) but it also applies to all cases
	 * where the driver chooses for memory based input frames. In these cases, a copy binary (which typical
	 * copies sensor data to DDR) does not have much use.
	 */
	if (!IS_ISP2401)
		need_isp_copy_binary = !online && !continuous;
	else
		need_isp_copy_binary = !online && !continuous && !(pipe->stream->config.mode == IA_CSS_INPUT_MODE_MEMORY);
#endif

	/* Copy */
	if (need_isp_copy_binary) {
		err = load_copy_binary(pipe,
				       &mycs->copy_binary,
				       &mycs->preview_binary);
		if (err)
			return err;
	}

	if (pipe->shading_table) {
		ia_css_shading_table_free(pipe->shading_table);
		pipe->shading_table = NULL;
	}

	return 0;
}

static void
ia_css_binary_unload(struct ia_css_binary *binary)
{
	ia_css_binary_destroy_isp_parameters(binary);
}

static int
unload_preview_binaries(struct ia_css_pipe *pipe)
{
	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);

	if ((!pipe) || (pipe->mode != IA_CSS_PIPE_ID_PREVIEW)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}
	ia_css_binary_unload(&pipe->pipe_settings.preview.copy_binary);
	ia_css_binary_unload(&pipe->pipe_settings.preview.preview_binary);
	ia_css_binary_unload(&pipe->pipe_settings.preview.vf_pp_binary);

	IA_CSS_LEAVE_ERR_PRIVATE(0);
	return 0;
}

static const struct ia_css_fw_info *last_output_firmware(
    const struct ia_css_fw_info *fw)
{
	const struct ia_css_fw_info *last_fw = NULL;
	/* fw can be NULL */
	IA_CSS_ENTER_LEAVE_PRIVATE("");

	for (; fw; fw = fw->next) {
		const struct ia_css_fw_info *info = fw;

		if (info->info.isp.sp.enable.output)
			last_fw = fw;
	}
	return last_fw;
}

static int add_firmwares(
    struct ia_css_pipeline *me,
    struct ia_css_binary *binary,
    const struct ia_css_fw_info *fw,
    const struct ia_css_fw_info *last_fw,
    unsigned int binary_mode,
    struct ia_css_frame *in_frame,
    struct ia_css_frame *out_frame,
    struct ia_css_frame *vf_frame,
    struct ia_css_pipeline_stage **my_stage,
    struct ia_css_pipeline_stage **vf_stage)
{
	int err = 0;
	struct ia_css_pipeline_stage *extra_stage = NULL;
	struct ia_css_pipeline_stage_desc stage_desc;

	/* all args can be NULL ??? */
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "add_firmwares() enter:\n");

	for (; fw; fw = fw->next) {
		struct ia_css_frame *out[IA_CSS_BINARY_MAX_OUTPUT_PORTS] = {NULL};
		struct ia_css_frame *in = NULL;
		struct ia_css_frame *vf = NULL;

		if ((fw == last_fw) && (fw->info.isp.sp.enable.out_frame  != 0))
			out[0] = out_frame;

		if (fw->info.isp.sp.enable.in_frame != 0)
			in = in_frame;

		if (fw->info.isp.sp.enable.out_frame != 0)
			vf = vf_frame;

		ia_css_pipe_get_firmwares_stage_desc(&stage_desc, binary,
						     out, in, vf, fw, binary_mode);
		err = ia_css_pipeline_create_and_add_stage(me, &stage_desc,
							   &extra_stage);
		if (err)
			return err;
		if (fw->info.isp.sp.enable.output != 0)
			in_frame = extra_stage->args.out_frame[0];
		if (my_stage && !*my_stage && extra_stage)
			*my_stage = extra_stage;
		if (vf_stage && !*vf_stage && extra_stage &&
		    fw->info.isp.sp.enable.vf_veceven)
			*vf_stage = extra_stage;
	}
	return err;
}

static int add_vf_pp_stage(
    struct ia_css_pipe *pipe,
    struct ia_css_frame *in_frame,
    struct ia_css_frame *out_frame,
    struct ia_css_binary *vf_pp_binary,
    struct ia_css_pipeline_stage **vf_pp_stage)
{
	struct ia_css_pipeline *me = NULL;
	const struct ia_css_fw_info *last_fw = NULL;
	int err = 0;
	struct ia_css_frame *out_frames[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	struct ia_css_pipeline_stage_desc stage_desc;

	/* out_frame can be NULL ??? */

	if (!pipe)
		return -EINVAL;
	if (!in_frame)
		return -EINVAL;
	if (!vf_pp_binary)
		return -EINVAL;
	if (!vf_pp_stage)
		return -EINVAL;

	ia_css_pipe_util_create_output_frames(out_frames);
	me = &pipe->pipeline;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "add_vf_pp_stage() enter:\n");

	*vf_pp_stage = NULL;

	last_fw = last_output_firmware(pipe->vf_stage);
	if (!pipe->extra_config.disable_vf_pp) {
		if (last_fw) {
			ia_css_pipe_util_set_output_frames(out_frames, 0, NULL);
			ia_css_pipe_get_generic_stage_desc(&stage_desc, vf_pp_binary,
							   out_frames, in_frame, NULL);
		} else {
			ia_css_pipe_util_set_output_frames(out_frames, 0, out_frame);
			ia_css_pipe_get_generic_stage_desc(&stage_desc, vf_pp_binary,
							   out_frames, in_frame, NULL);
		}
		err = ia_css_pipeline_create_and_add_stage(me, &stage_desc, vf_pp_stage);
		if (err)
			return err;
		in_frame = (*vf_pp_stage)->args.out_frame[0];
	}
	err = add_firmwares(me, vf_pp_binary, pipe->vf_stage, last_fw,
			    IA_CSS_BINARY_MODE_VF_PP,
			    in_frame, out_frame, NULL,
			    vf_pp_stage, NULL);
	return err;
}

static int add_yuv_scaler_stage(
    struct ia_css_pipe *pipe,
    struct ia_css_pipeline *me,
    struct ia_css_frame *in_frame,
    struct ia_css_frame *out_frame,
    struct ia_css_frame *internal_out_frame,
    struct ia_css_binary *yuv_scaler_binary,
    struct ia_css_pipeline_stage **pre_vf_pp_stage)
{
	const struct ia_css_fw_info *last_fw;
	int err = 0;
	struct ia_css_frame *vf_frame = NULL;
	struct ia_css_frame *out_frames[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	struct ia_css_pipeline_stage_desc stage_desc;

	/* out_frame can be NULL ??? */
	assert(in_frame);
	assert(pipe);
	assert(me);
	assert(yuv_scaler_binary);
	assert(pre_vf_pp_stage);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "add_yuv_scaler_stage() enter:\n");

	*pre_vf_pp_stage = NULL;
	ia_css_pipe_util_create_output_frames(out_frames);

	last_fw = last_output_firmware(pipe->output_stage);

	if (last_fw) {
		ia_css_pipe_util_set_output_frames(out_frames, 0, NULL);
		ia_css_pipe_get_generic_stage_desc(&stage_desc,
						   yuv_scaler_binary, out_frames, in_frame, vf_frame);
	} else {
		ia_css_pipe_util_set_output_frames(out_frames, 0, out_frame);
		ia_css_pipe_util_set_output_frames(out_frames, 1, internal_out_frame);
		ia_css_pipe_get_generic_stage_desc(&stage_desc,
						   yuv_scaler_binary, out_frames, in_frame, vf_frame);
	}
	err = ia_css_pipeline_create_and_add_stage(me, &stage_desc,
						   pre_vf_pp_stage);
	if (err)
		return err;
	in_frame = (*pre_vf_pp_stage)->args.out_frame[0];

	err = add_firmwares(me, yuv_scaler_binary, pipe->output_stage, last_fw,
			    IA_CSS_BINARY_MODE_CAPTURE_PP,
			    in_frame, out_frame, vf_frame,
			    NULL, pre_vf_pp_stage);
	/* If a firmware produce vf_pp output, we set that as vf_pp input */
	(*pre_vf_pp_stage)->args.vf_downscale_log2 =
	    yuv_scaler_binary->vf_downscale_log2;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "add_yuv_scaler_stage() leave:\n");
	return err;
}

static int add_capture_pp_stage(
    struct ia_css_pipe *pipe,
    struct ia_css_pipeline *me,
    struct ia_css_frame *in_frame,
    struct ia_css_frame *out_frame,
    struct ia_css_binary *capture_pp_binary,
    struct ia_css_pipeline_stage **capture_pp_stage)
{
	const struct ia_css_fw_info *last_fw = NULL;
	int err = 0;
	struct ia_css_frame *vf_frame = NULL;
	struct ia_css_frame *out_frames[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	struct ia_css_pipeline_stage_desc stage_desc;

	/* out_frame can be NULL ??? */
	assert(in_frame);
	assert(pipe);
	assert(me);
	assert(capture_pp_binary);
	assert(capture_pp_stage);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "add_capture_pp_stage() enter:\n");

	*capture_pp_stage = NULL;
	ia_css_pipe_util_create_output_frames(out_frames);

	last_fw = last_output_firmware(pipe->output_stage);
	err = ia_css_frame_allocate_from_info(&vf_frame,
					      &capture_pp_binary->vf_frame_info);
	if (err)
		return err;
	if (last_fw)	{
		ia_css_pipe_util_set_output_frames(out_frames, 0, NULL);
		ia_css_pipe_get_generic_stage_desc(&stage_desc,
						   capture_pp_binary, out_frames, NULL, vf_frame);
	} else {
		ia_css_pipe_util_set_output_frames(out_frames, 0, out_frame);
		ia_css_pipe_get_generic_stage_desc(&stage_desc,
						   capture_pp_binary, out_frames, NULL, vf_frame);
	}
	err = ia_css_pipeline_create_and_add_stage(me, &stage_desc,
						   capture_pp_stage);
	if (err)
		return err;
	err = add_firmwares(me, capture_pp_binary, pipe->output_stage, last_fw,
			    IA_CSS_BINARY_MODE_CAPTURE_PP,
			    in_frame, out_frame, vf_frame,
			    NULL, capture_pp_stage);
	/* If a firmware produce vf_pp output, we set that as vf_pp input */
	if (*capture_pp_stage) {
		(*capture_pp_stage)->args.vf_downscale_log2 =
		    capture_pp_binary->vf_downscale_log2;
	}
	return err;
}

static void sh_css_setup_queues(void)
{
	const struct ia_css_fw_info *fw;
	unsigned int HIVE_ADDR_host_sp_queues_initialized;

	sh_css_hmm_buffer_record_init();

	sh_css_event_init_irq_mask();

	fw = &sh_css_sp_fw;
	HIVE_ADDR_host_sp_queues_initialized =
	    fw->info.sp.host_sp_queues_initialized;

	ia_css_bufq_init();

	/* set "host_sp_queues_initialized" to "true" */
	sp_dmem_store_uint32(SP0_ID,
			     (unsigned int)sp_address_of(host_sp_queues_initialized),
			     (uint32_t)(1));
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "sh_css_setup_queues() leave:\n");
}

static int
init_vf_frameinfo_defaults(struct ia_css_pipe *pipe,
			   struct ia_css_frame *vf_frame, unsigned int idx)
{
	int err = 0;
	unsigned int thread_id;
	enum sh_css_queue_id queue_id;

	assert(vf_frame);

	sh_css_pipe_get_viewfinder_frame_info(pipe, &vf_frame->info, idx);
	vf_frame->flash_state = IA_CSS_FRAME_FLASH_STATE_NONE;
	ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &thread_id);
	ia_css_query_internal_queue_id(IA_CSS_BUFFER_TYPE_VF_OUTPUT_FRAME + idx, thread_id, &queue_id);
	vf_frame->dynamic_queue_id = queue_id;
	vf_frame->buf_type = IA_CSS_BUFFER_TYPE_VF_OUTPUT_FRAME + idx;

	err = ia_css_frame_init_planes(vf_frame);
	return err;
}

#ifdef ISP2401
static unsigned int
get_crop_lines_for_bayer_order(const struct ia_css_stream_config *config)
{
	assert(config);
	if ((config->input_config.bayer_order == IA_CSS_BAYER_ORDER_BGGR) ||
	    (config->input_config.bayer_order == IA_CSS_BAYER_ORDER_GBRG))
		return 1;

	return 0;
}

static unsigned int
get_crop_columns_for_bayer_order(const struct ia_css_stream_config *config)
{
	assert(config);
	if ((config->input_config.bayer_order == IA_CSS_BAYER_ORDER_RGGB) ||
	    (config->input_config.bayer_order == IA_CSS_BAYER_ORDER_GBRG))
		return 1;

	return 0;
}

/*
 * This function is to get the sum of all extra pixels in addition to the effective
 * input, it includes dvs envelop and filter run-in
 */
static void get_pipe_extra_pixel(struct ia_css_pipe *pipe,
				 unsigned int *extra_row, unsigned int *extra_column)
{
	enum ia_css_pipe_id pipe_id = pipe->mode;
	unsigned int left_cropping = 0, top_cropping = 0;
	unsigned int i;
	struct ia_css_resolution dvs_env = pipe->config.dvs_envelope;

	/*
	 * The dvs envelope info may not be correctly sent down via pipe config
	 * The check is made and the correct value is populated in the binary info
	 * Use this value when computing crop, else excess lines may get trimmed
	 */
	switch (pipe_id) {
	case IA_CSS_PIPE_ID_PREVIEW:
		if (pipe->pipe_settings.preview.preview_binary.info) {
			left_cropping =
			    pipe->pipe_settings.preview.preview_binary.info->sp.pipeline.left_cropping;
			top_cropping =
			    pipe->pipe_settings.preview.preview_binary.info->sp.pipeline.top_cropping;
		}
		dvs_env = pipe->pipe_settings.preview.preview_binary.dvs_envelope;
		break;
	case IA_CSS_PIPE_ID_VIDEO:
		if (pipe->pipe_settings.video.video_binary.info) {
			left_cropping =
			    pipe->pipe_settings.video.video_binary.info->sp.pipeline.left_cropping;
			top_cropping =
			    pipe->pipe_settings.video.video_binary.info->sp.pipeline.top_cropping;
		}
		dvs_env = pipe->pipe_settings.video.video_binary.dvs_envelope;
		break;
	case IA_CSS_PIPE_ID_CAPTURE:
		for (i = 0; i < pipe->pipe_settings.capture.num_primary_stage; i++) {
			if (pipe->pipe_settings.capture.primary_binary[i].info) {
				left_cropping +=
				    pipe->pipe_settings.capture.primary_binary[i].info->sp.pipeline.left_cropping;
				top_cropping +=
				    pipe->pipe_settings.capture.primary_binary[i].info->sp.pipeline.top_cropping;
			}
			dvs_env.width +=
			    pipe->pipe_settings.capture.primary_binary[i].dvs_envelope.width;
			dvs_env.height +=
			    pipe->pipe_settings.capture.primary_binary[i].dvs_envelope.height;
		}
		break;
	default:
		break;
	}

	*extra_row = top_cropping + dvs_env.height;
	*extra_column = left_cropping + dvs_env.width;
}

void
ia_css_get_crop_offsets(
    struct ia_css_pipe *pipe,
    struct ia_css_frame_info *in_frame)
{
	unsigned int row = 0;
	unsigned int column = 0;
	struct ia_css_resolution *input_res;
	struct ia_css_resolution *effective_res;
	unsigned int extra_row = 0, extra_col = 0;
	unsigned int min_reqd_height, min_reqd_width;

	assert(pipe);
	assert(pipe->stream);
	assert(in_frame);

	IA_CSS_ENTER_PRIVATE("pipe = %p effective_wd = %u effective_ht = %u",
			     pipe, pipe->config.input_effective_res.width,
			     pipe->config.input_effective_res.height);

	input_res = &pipe->stream->config.input_config.input_res;
#ifndef ISP2401
	effective_res = &pipe->stream->config.input_config.effective_res;
#else
	effective_res = &pipe->config.input_effective_res;
#endif

	get_pipe_extra_pixel(pipe, &extra_row, &extra_col);

	in_frame->raw_bayer_order = pipe->stream->config.input_config.bayer_order;

	min_reqd_height = effective_res->height + extra_row;
	min_reqd_width = effective_res->width + extra_col;

	if (input_res->height > min_reqd_height) {
		row = (input_res->height - min_reqd_height) / 2;
		row &= ~0x1;
	}
	if (input_res->width > min_reqd_width) {
		column = (input_res->width - min_reqd_width) / 2;
		column &= ~0x1;
	}

	/*
	 * TODO:
	 * 1. Require the special support for RAW10 packed mode.
	 * 2. Require the special support for the online use cases.
	 */

	/*
	 * ISP expects GRBG bayer order, we skip one line and/or one row
	 * to correct in case the input bayer order is different.
	 */
	column += get_crop_columns_for_bayer_order(&pipe->stream->config);
	row += get_crop_lines_for_bayer_order(&pipe->stream->config);

	in_frame->crop_info.start_column = column;
	in_frame->crop_info.start_line = row;

	IA_CSS_LEAVE_PRIVATE("void start_col: %u start_row: %u", column, row);

	return;
}
#endif

static int
init_in_frameinfo_memory_defaults(struct ia_css_pipe *pipe,
				  struct ia_css_frame *frame, enum ia_css_frame_format format)
{
	struct ia_css_frame *in_frame;
	int err = 0;
	unsigned int thread_id;
	enum sh_css_queue_id queue_id;

	assert(frame);
	in_frame = frame;

	in_frame->info.format = format;

#ifdef ISP2401
	if (format == IA_CSS_FRAME_FORMAT_RAW)
		in_frame->info.format = (pipe->stream->config.pack_raw_pixels) ?
		IA_CSS_FRAME_FORMAT_RAW_PACKED : IA_CSS_FRAME_FORMAT_RAW;
#endif

	in_frame->info.res.width = pipe->stream->config.input_config.input_res.width;
	in_frame->info.res.height = pipe->stream->config.input_config.input_res.height;
	in_frame->info.raw_bit_depth =
	ia_css_pipe_util_pipe_input_format_bpp(pipe);
	ia_css_frame_info_set_width(&in_frame->info, pipe->stream->config.input_config.input_res.width, 0);
	in_frame->flash_state = IA_CSS_FRAME_FLASH_STATE_NONE;
	ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &thread_id);
	ia_css_query_internal_queue_id(IA_CSS_BUFFER_TYPE_INPUT_FRAME, thread_id, &queue_id);
	in_frame->dynamic_queue_id = queue_id;
	in_frame->buf_type = IA_CSS_BUFFER_TYPE_INPUT_FRAME;
#ifdef ISP2401
	ia_css_get_crop_offsets(pipe, &in_frame->info);
#endif
	err = ia_css_frame_init_planes(in_frame);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "init_in_frameinfo_memory_defaults() bayer_order = %d:\n", in_frame->info.raw_bayer_order);

	return err;
}

static int
init_out_frameinfo_defaults(struct ia_css_pipe *pipe,
			    struct ia_css_frame *out_frame, unsigned int idx)
{
	int err = 0;
	unsigned int thread_id;
	enum sh_css_queue_id queue_id;

	assert(out_frame);

	sh_css_pipe_get_output_frame_info(pipe, &out_frame->info, idx);
	out_frame->flash_state = IA_CSS_FRAME_FLASH_STATE_NONE;
	ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &thread_id);
	ia_css_query_internal_queue_id(IA_CSS_BUFFER_TYPE_OUTPUT_FRAME + idx, thread_id, &queue_id);
	out_frame->dynamic_queue_id = queue_id;
	out_frame->buf_type = IA_CSS_BUFFER_TYPE_OUTPUT_FRAME + idx;
	err = ia_css_frame_init_planes(out_frame);

	return err;
}

/* Create stages for video pipe */
static int create_host_video_pipeline(struct ia_css_pipe *pipe)
{
	struct ia_css_pipeline_stage_desc stage_desc;
	struct ia_css_binary *copy_binary, *video_binary,
		       *yuv_scaler_binary, *vf_pp_binary;
	struct ia_css_pipeline_stage *copy_stage  = NULL;
	struct ia_css_pipeline_stage *video_stage = NULL;
	struct ia_css_pipeline_stage *yuv_scaler_stage  = NULL;
	struct ia_css_pipeline_stage *vf_pp_stage = NULL;
	struct ia_css_pipeline *me;
	struct ia_css_frame *in_frame = NULL;
	struct ia_css_frame *out_frame;
	struct ia_css_frame *out_frames[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	struct ia_css_frame *vf_frame = NULL;
	int err = 0;
	bool need_copy   = false;
	bool need_vf_pp  = false;
	bool need_yuv_pp = false;
	bool need_in_frameinfo_memory = false;

	unsigned int i, num_yuv_scaler;
	bool *is_output_stage = NULL;

	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);
	if ((!pipe) || (!pipe->stream) || (pipe->mode != IA_CSS_PIPE_ID_VIDEO)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}
	ia_css_pipe_util_create_output_frames(out_frames);
	out_frame = &pipe->out_frame_struct;

	/* pipeline already created as part of create_host_pipeline_structure */
	me = &pipe->pipeline;
	ia_css_pipeline_clean(me);

	me->dvs_frame_delay = pipe->dvs_frame_delay;

#ifdef ISP2401
	/*
	 * When the input system is 2401, always enable 'in_frameinfo_memory'
	 * except for the following: online or continuous
	 */
	need_in_frameinfo_memory = !(pipe->stream->config.online ||
				     pipe->stream->config.continuous);
#else
	/* Construct in_frame info (only in case we have dynamic input */
	need_in_frameinfo_memory = pipe->stream->config.mode ==
				   IA_CSS_INPUT_MODE_MEMORY;
#endif

	/* Construct in_frame info (only in case we have dynamic input */
	if (need_in_frameinfo_memory) {
		in_frame = &pipe->in_frame_struct;
		err = init_in_frameinfo_memory_defaults(pipe, in_frame,
							IA_CSS_FRAME_FORMAT_RAW);
		if (err)
			goto ERR;
	}

	out_frame->data = 0;
	err = init_out_frameinfo_defaults(pipe, out_frame, 0);
	if (err)
		goto ERR;

	if (pipe->enable_viewfinder[IA_CSS_PIPE_OUTPUT_STAGE_0]) {
		vf_frame = &pipe->vf_frame_struct;
		vf_frame->data = 0;
		err = init_vf_frameinfo_defaults(pipe, vf_frame, 0);
		if (err)
			goto ERR;
	}

	copy_binary  = &pipe->pipe_settings.video.copy_binary;
	video_binary = &pipe->pipe_settings.video.video_binary;
	vf_pp_binary = &pipe->pipe_settings.video.vf_pp_binary;

	yuv_scaler_binary = pipe->pipe_settings.video.yuv_scaler_binary;
	num_yuv_scaler  = pipe->pipe_settings.video.num_yuv_scaler;
	is_output_stage = pipe->pipe_settings.video.is_output_stage;

	need_copy   = (copy_binary && copy_binary->info);
	need_vf_pp  = (vf_pp_binary && vf_pp_binary->info);
	need_yuv_pp = (yuv_scaler_binary && yuv_scaler_binary->info);

	if (need_copy) {
		ia_css_pipe_util_set_output_frames(out_frames, 0, NULL);
		ia_css_pipe_get_generic_stage_desc(&stage_desc, copy_binary,
						   out_frames, NULL, NULL);
		err = ia_css_pipeline_create_and_add_stage(me, &stage_desc,
							   &copy_stage);
		if (err)
			goto ERR;
		in_frame = me->stages->args.out_frame[0];
	} else if (pipe->stream->config.continuous) {
#ifdef ISP2401
		/*
		 * When continuous is enabled, configure in_frame with the
		 * last pipe, which is the copy pipe.
		 */
		in_frame = pipe->stream->last_pipe->continuous_frames[0];
#else
		in_frame = pipe->continuous_frames[0];
#endif
	}

	ia_css_pipe_util_set_output_frames(out_frames, 0,
					   need_yuv_pp ? NULL : out_frame);

	/*
	 * when the video binary supports a second output pin,
	 * it can directly produce the vf_frame.
	 */
	if (need_vf_pp) {
		ia_css_pipe_get_generic_stage_desc(&stage_desc, video_binary,
						   out_frames, in_frame, NULL);
	} else {
		ia_css_pipe_get_generic_stage_desc(&stage_desc, video_binary,
						   out_frames, in_frame, vf_frame);
	}
	err = ia_css_pipeline_create_and_add_stage(me, &stage_desc,
						   &video_stage);
	if (err)
		goto ERR;

	/* If we use copy iso video, the input must be yuv iso raw */
	if (video_stage) {
		video_stage->args.copy_vf =
		    video_binary->info->sp.pipeline.mode == IA_CSS_BINARY_MODE_COPY;
		video_stage->args.copy_output = video_stage->args.copy_vf;
	}

	/* when the video binary supports only 1 output pin, vf_pp is needed to
	produce the vf_frame.*/
	if (need_vf_pp && video_stage) {
		in_frame = video_stage->args.out_vf_frame;
		err = add_vf_pp_stage(pipe, in_frame, vf_frame, vf_pp_binary,
				      &vf_pp_stage);
		if (err)
			goto ERR;
	}
	if (video_stage) {
		int frm;

		for (frm = 0; frm < NUM_VIDEO_TNR_FRAMES; frm++) {
			video_stage->args.tnr_frames[frm] =
			    pipe->pipe_settings.video.tnr_frames[frm];
		}
		for (frm = 0; frm < MAX_NUM_VIDEO_DELAY_FRAMES; frm++) {
			video_stage->args.delay_frames[frm] =
			    pipe->pipe_settings.video.delay_frames[frm];
		}
	}

	/* Append Extension on Video out, if enabled */
	if (!need_vf_pp && video_stage && pipe->config.acc_extension &&
	    (pipe->config.acc_extension->info.isp.type == IA_CSS_ACC_OUTPUT)) {
		struct ia_css_frame *out = NULL;
		struct ia_css_frame *in = NULL;

		if ((pipe->config.acc_extension->info.isp.sp.enable.output) &&
		    (pipe->config.acc_extension->info.isp.sp.enable.in_frame) &&
		    (pipe->config.acc_extension->info.isp.sp.enable.out_frame)) {
			/* In/Out Frame mapping to support output frame extension.*/
			out = video_stage->args.out_frame[0];
			err = ia_css_frame_allocate_from_info(&in, &pipe->output_info[0]);
			if (err)
				goto ERR;
			video_stage->args.out_frame[0] = in;
		}

		err = add_firmwares(me, video_binary, pipe->output_stage,
				    last_output_firmware(pipe->output_stage),
				    IA_CSS_BINARY_MODE_VIDEO,
				    in, out, NULL, &video_stage, NULL);
		if (err)
			goto ERR;
	}

	if (need_yuv_pp && video_stage) {
		struct ia_css_frame *tmp_in_frame = video_stage->args.out_frame[0];
		struct ia_css_frame *tmp_out_frame = NULL;

		for (i = 0; i < num_yuv_scaler; i++) {
			tmp_out_frame = is_output_stage[i] ? out_frame : NULL;

			err = add_yuv_scaler_stage(pipe, me, tmp_in_frame,
						   tmp_out_frame, NULL,
						   &yuv_scaler_binary[i],
						   &yuv_scaler_stage);

			if (err) {
				IA_CSS_LEAVE_ERR_PRIVATE(err);
				return err;
			}
			/* we use output port 1 as internal output port */
			if (yuv_scaler_stage)
				tmp_in_frame = yuv_scaler_stage->args.out_frame[1];
		}
	}

	pipe->pipeline.acquire_isp_each_stage = false;
	ia_css_pipeline_finalize_stages(&pipe->pipeline,
					pipe->stream->config.continuous);

ERR:
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static int
create_host_acc_pipeline(struct ia_css_pipe *pipe)
{
	int err = 0;
	const struct ia_css_fw_info *fw;
	unsigned int i;

	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);
	if ((!pipe) || (!pipe->stream)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	pipe->pipeline.num_execs = pipe->config.acc_num_execs;
	/* Reset pipe_qos_config to default disable all QOS extension stages */
	if (pipe->config.acc_extension)
		pipe->pipeline.pipe_qos_config = 0;

	for (fw = pipe->vf_stage; fw; fw = fw->next) {
		err = sh_css_pipeline_add_acc_stage(&pipe->pipeline, fw);
		if (err)
			goto ERR;
	}

	for (i = 0; i < pipe->config.num_acc_stages; i++) {
		struct ia_css_fw_info *fw = pipe->config.acc_stages[i];

		err = sh_css_pipeline_add_acc_stage(&pipe->pipeline, fw);
		if (err)
			goto ERR;
	}

	ia_css_pipeline_finalize_stages(&pipe->pipeline, pipe->stream->config.continuous);

ERR:
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

/* Create stages for preview */
static int
create_host_preview_pipeline(struct ia_css_pipe *pipe)
{
	struct ia_css_pipeline_stage *copy_stage = NULL;
	struct ia_css_pipeline_stage *preview_stage = NULL;
	struct ia_css_pipeline_stage *vf_pp_stage = NULL;
	struct ia_css_pipeline_stage_desc stage_desc;
	struct ia_css_pipeline *me = NULL;
	struct ia_css_binary *copy_binary, *preview_binary, *vf_pp_binary = NULL;
	struct ia_css_frame *in_frame = NULL;
	int err = 0;
	struct ia_css_frame *out_frame;
	struct ia_css_frame *out_frames[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	bool need_in_frameinfo_memory = false;
#ifdef ISP2401
	bool sensor = false;
	bool buffered_sensor = false;
	bool online = false;
	bool continuous = false;
#endif

	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);
	if ((!pipe) || (!pipe->stream) || (pipe->mode != IA_CSS_PIPE_ID_PREVIEW)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	ia_css_pipe_util_create_output_frames(out_frames);
	/* pipeline already created as part of create_host_pipeline_structure */
	me = &pipe->pipeline;
	ia_css_pipeline_clean(me);

#ifdef ISP2401
	/*
	 * When the input system is 2401, always enable 'in_frameinfo_memory'
	 * except for the following:
	 * - Direct Sensor Mode Online Preview
	 * - Buffered Sensor Mode Online Preview
	 * - Direct Sensor Mode Continuous Preview
	 * - Buffered Sensor Mode Continuous Preview
	 */
	sensor = (pipe->stream->config.mode == IA_CSS_INPUT_MODE_SENSOR);
	buffered_sensor = (pipe->stream->config.mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR);
	online = pipe->stream->config.online;
	continuous = pipe->stream->config.continuous;
	need_in_frameinfo_memory =
	!((sensor && (online || continuous)) || (buffered_sensor && (online || continuous)));
#else
	/* Construct in_frame info (only in case we have dynamic input */
	need_in_frameinfo_memory = pipe->stream->config.mode == IA_CSS_INPUT_MODE_MEMORY;
#endif
	if (need_in_frameinfo_memory) {
		err = init_in_frameinfo_memory_defaults(pipe, &me->in_frame,
							IA_CSS_FRAME_FORMAT_RAW);
		if (err)
			goto ERR;

		in_frame = &me->in_frame;
	} else {
		in_frame = NULL;
	}

	err = init_out_frameinfo_defaults(pipe, &me->out_frame[0], 0);
	if (err)
		goto ERR;
	out_frame = &me->out_frame[0];

	copy_binary    = &pipe->pipe_settings.preview.copy_binary;
	preview_binary = &pipe->pipe_settings.preview.preview_binary;
	if (pipe->pipe_settings.preview.vf_pp_binary.info)
		vf_pp_binary = &pipe->pipe_settings.preview.vf_pp_binary;

	if (pipe->pipe_settings.preview.copy_binary.info) {
		ia_css_pipe_util_set_output_frames(out_frames, 0, NULL);
		ia_css_pipe_get_generic_stage_desc(&stage_desc, copy_binary,
						   out_frames, NULL, NULL);
		err = ia_css_pipeline_create_and_add_stage(me, &stage_desc,
							   &copy_stage);
		if (err)
			goto ERR;
		in_frame = me->stages->args.out_frame[0];
	} else if (pipe->stream->config.continuous) {
#ifdef ISP2401
		/*
		 * When continuous is enabled, configure in_frame with the
		 * last pipe, which is the copy pipe.
		 */
		if (continuous || !online)
			in_frame = pipe->stream->last_pipe->continuous_frames[0];

#else
		in_frame = pipe->continuous_frames[0];
#endif
	}

	if (vf_pp_binary) {
		ia_css_pipe_util_set_output_frames(out_frames, 0, NULL);
		ia_css_pipe_get_generic_stage_desc(&stage_desc, preview_binary,
						   out_frames, in_frame, NULL);
	} else {
		ia_css_pipe_util_set_output_frames(out_frames, 0, out_frame);
		ia_css_pipe_get_generic_stage_desc(&stage_desc, preview_binary,
						   out_frames, in_frame, NULL);
	}
	err = ia_css_pipeline_create_and_add_stage(me, &stage_desc,
						   &preview_stage);
	if (err)
		goto ERR;
	/* If we use copy iso preview, the input must be yuv iso raw */
	preview_stage->args.copy_vf =
	    preview_binary->info->sp.pipeline.mode == IA_CSS_BINARY_MODE_COPY;
	preview_stage->args.copy_output = !preview_stage->args.copy_vf;
	if (preview_stage->args.copy_vf && !preview_stage->args.out_vf_frame) {
		/* in case of copy, use the vf frame as output frame */
		preview_stage->args.out_vf_frame =
		    preview_stage->args.out_frame[0];
	}
	if (vf_pp_binary) {
		if (preview_binary->info->sp.pipeline.mode == IA_CSS_BINARY_MODE_COPY)
			in_frame = preview_stage->args.out_vf_frame;
		else
			in_frame = preview_stage->args.out_frame[0];
		err = add_vf_pp_stage(pipe, in_frame, out_frame, vf_pp_binary,
				      &vf_pp_stage);
		if (err)
			goto ERR;
	}

	pipe->pipeline.acquire_isp_each_stage = false;
	ia_css_pipeline_finalize_stages(&pipe->pipeline, pipe->stream->config.continuous);

ERR:
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static void send_raw_frames(struct ia_css_pipe *pipe)
{
	if (pipe->stream->config.continuous) {
		unsigned int i;

		sh_css_update_host2sp_cont_num_raw_frames
		(pipe->stream->config.init_num_cont_raw_buf, true);
		sh_css_update_host2sp_cont_num_raw_frames
		(pipe->stream->config.target_num_cont_raw_buf, false);

		/* Hand-over all the SP-internal buffers */
		for (i = 0; i < pipe->stream->config.init_num_cont_raw_buf; i++) {
			sh_css_update_host2sp_offline_frame(i,
							    pipe->continuous_frames[i], pipe->cont_md_buffers[i]);
		}
	}

	return;
}

static int
preview_start(struct ia_css_pipe *pipe)
{
	int err = 0;
	struct ia_css_pipe *copy_pipe, *capture_pipe;
	struct ia_css_pipe *acc_pipe;
	enum sh_css_pipe_config_override copy_ovrd;
	enum ia_css_input_mode preview_pipe_input_mode;
	unsigned int thread_id;

	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);
	if ((!pipe) || (!pipe->stream) || (pipe->mode != IA_CSS_PIPE_ID_PREVIEW)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	preview_pipe_input_mode = pipe->stream->config.mode;

	copy_pipe    = pipe->pipe_settings.preview.copy_pipe;
	capture_pipe = pipe->pipe_settings.preview.capture_pipe;
	acc_pipe     = pipe->pipe_settings.preview.acc_pipe;

	sh_css_metrics_start_frame();

	/* multi stream video needs mipi buffers */
	err = send_mipi_frames(pipe);
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}
	send_raw_frames(pipe);

	ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &thread_id);
	copy_ovrd = 1 << thread_id;

	if (pipe->stream->cont_capt) {
		ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(capture_pipe),
						 &thread_id);
		copy_ovrd |= 1 << thread_id;
	}

	/* Construct and load the copy pipe */
	if (pipe->stream->config.continuous) {
		sh_css_sp_init_pipeline(&copy_pipe->pipeline,
					IA_CSS_PIPE_ID_COPY,
					(uint8_t)ia_css_pipe_get_pipe_num(copy_pipe),
					false,
					pipe->stream->config.pixels_per_clock == 2, false,
					false, pipe->required_bds_factor,
					copy_ovrd,
					pipe->stream->config.mode,
					&pipe->stream->config.metadata_config,
					&pipe->stream->info.metadata_info,
					pipe->stream->config.source.port.port);

		/*
		 * make the preview pipe start with mem mode input, copy handles
		 * the actual mode
		 */
		preview_pipe_input_mode = IA_CSS_INPUT_MODE_MEMORY;
	}

	/* Construct and load the capture pipe */
	if (pipe->stream->cont_capt) {
		sh_css_sp_init_pipeline(&capture_pipe->pipeline,
					IA_CSS_PIPE_ID_CAPTURE,
					(uint8_t)ia_css_pipe_get_pipe_num(capture_pipe),
					capture_pipe->config.default_capture_config.enable_xnr != 0,
					capture_pipe->stream->config.pixels_per_clock == 2,
					true, /* continuous */
					false, /* offline */
					capture_pipe->required_bds_factor,
					0,
					IA_CSS_INPUT_MODE_MEMORY,
					&pipe->stream->config.metadata_config,
					&pipe->stream->info.metadata_info,
					(enum mipi_port_id)0);
	}

	if (acc_pipe) {
		sh_css_sp_init_pipeline(&acc_pipe->pipeline,
					IA_CSS_PIPE_ID_ACC,
					(uint8_t)ia_css_pipe_get_pipe_num(acc_pipe),
					false,
					pipe->stream->config.pixels_per_clock == 2,
					false, /* continuous */
					false, /* offline */
					pipe->required_bds_factor,
					0,
					IA_CSS_INPUT_MODE_MEMORY,
					NULL,
					NULL,
					(enum mipi_port_id)0);
	}

	start_pipe(pipe, copy_ovrd, preview_pipe_input_mode);

	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

int
ia_css_pipe_enqueue_buffer(struct ia_css_pipe *pipe,
			   const struct ia_css_buffer *buffer)
{
	int return_err = 0;
	unsigned int thread_id;
	enum sh_css_queue_id queue_id;
	struct ia_css_pipeline *pipeline;
	struct ia_css_pipeline_stage *stage;
	struct ia_css_rmgr_vbuf_handle p_vbuf;
	struct ia_css_rmgr_vbuf_handle *h_vbuf;
	struct sh_css_hmm_buffer ddr_buffer;
	enum ia_css_buffer_type buf_type;
	enum ia_css_pipe_id pipe_id;
	bool ret_err;

	IA_CSS_ENTER("pipe=%p, buffer=%p", pipe, buffer);

	if ((!pipe) || (!buffer)) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	buf_type = buffer->type;

	pipe_id = pipe->mode;

	IA_CSS_LOG("pipe_id=%d, buf_type=%d", pipe_id, buf_type);

	assert(pipe_id < IA_CSS_PIPE_ID_NUM);
	assert(buf_type < IA_CSS_NUM_DYNAMIC_BUFFER_TYPE);
	if (buf_type == IA_CSS_BUFFER_TYPE_INVALID ||
	    buf_type >= IA_CSS_NUM_DYNAMIC_BUFFER_TYPE ||
	    pipe_id >= IA_CSS_PIPE_ID_NUM) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	ret_err = ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &thread_id);
	if (!ret_err) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	ret_err = ia_css_query_internal_queue_id(buf_type, thread_id, &queue_id);
	if (!ret_err) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	if ((queue_id <= SH_CSS_INVALID_QUEUE_ID) || (queue_id >= SH_CSS_MAX_NUM_QUEUES)) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	if (!sh_css_sp_is_running()) {
		IA_CSS_LOG("SP is not running!");
		IA_CSS_LEAVE_ERR(-EBUSY);
		/* SP is not running. The queues are not valid */
		return -EBUSY;
	}

	pipeline = &pipe->pipeline;

	assert(pipeline ||
	       pipe_id == IA_CSS_PIPE_ID_COPY ||
	       pipe_id == IA_CSS_PIPE_ID_ACC);

	assert(sizeof(NULL) <= sizeof(ddr_buffer.kernel_ptr));
	ddr_buffer.kernel_ptr = HOST_ADDRESS(NULL);
	ddr_buffer.cookie_ptr = buffer->driver_cookie;
	ddr_buffer.timing_data = buffer->timing_data;

	if (buf_type == IA_CSS_BUFFER_TYPE_3A_STATISTICS) {
		if (!buffer->data.stats_3a) {
			IA_CSS_LEAVE_ERR(-EINVAL);
			return -EINVAL;
		}
		ddr_buffer.kernel_ptr = HOST_ADDRESS(buffer->data.stats_3a);
		ddr_buffer.payload.s3a = *buffer->data.stats_3a;
	} else if (buf_type == IA_CSS_BUFFER_TYPE_DIS_STATISTICS) {
		if (!buffer->data.stats_dvs) {
			IA_CSS_LEAVE_ERR(-EINVAL);
			return -EINVAL;
		}
		ddr_buffer.kernel_ptr = HOST_ADDRESS(buffer->data.stats_dvs);
		ddr_buffer.payload.dis = *buffer->data.stats_dvs;
	} else if (buf_type == IA_CSS_BUFFER_TYPE_METADATA) {
		if (!buffer->data.metadata) {
			IA_CSS_LEAVE_ERR(-EINVAL);
			return -EINVAL;
		}
		ddr_buffer.kernel_ptr = HOST_ADDRESS(buffer->data.metadata);
		ddr_buffer.payload.metadata = *buffer->data.metadata;
	} else if (buf_type == IA_CSS_BUFFER_TYPE_INPUT_FRAME ||
		   buf_type == IA_CSS_BUFFER_TYPE_OUTPUT_FRAME ||
		   buf_type == IA_CSS_BUFFER_TYPE_VF_OUTPUT_FRAME ||
		   buf_type == IA_CSS_BUFFER_TYPE_SEC_OUTPUT_FRAME ||
		   buf_type == IA_CSS_BUFFER_TYPE_SEC_VF_OUTPUT_FRAME) {
		if (!buffer->data.frame) {
			IA_CSS_LEAVE_ERR(-EINVAL);
			return -EINVAL;
		}
		ddr_buffer.kernel_ptr = HOST_ADDRESS(buffer->data.frame);
		ddr_buffer.payload.frame.frame_data = buffer->data.frame->data;
		ddr_buffer.payload.frame.flashed = 0;

		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				    "ia_css_pipe_enqueue_buffer() buf_type=%d, data(DDR address)=0x%x\n",
				    buf_type, buffer->data.frame->data);

	}

	/* start of test for using rmgr for acq/rel memory */
	p_vbuf.vptr = 0;
	p_vbuf.count = 0;
	p_vbuf.size = sizeof(struct sh_css_hmm_buffer);
	h_vbuf = &p_vbuf;
	/* TODO: change next to correct pool for optimization */
	ia_css_rmgr_acq_vbuf(hmm_buffer_pool, &h_vbuf);

	if ((!h_vbuf) || (h_vbuf->vptr == 0x0)) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	hmm_store(h_vbuf->vptr,
		  (void *)(&ddr_buffer),
		  sizeof(struct sh_css_hmm_buffer));
	if (buf_type == IA_CSS_BUFFER_TYPE_3A_STATISTICS ||
	    buf_type == IA_CSS_BUFFER_TYPE_DIS_STATISTICS ||
	    buf_type == IA_CSS_BUFFER_TYPE_LACE_STATISTICS) {
		if (!pipeline) {
			ia_css_rmgr_rel_vbuf(hmm_buffer_pool, &h_vbuf);
			IA_CSS_LOG("pipeline is empty!");
			IA_CSS_LEAVE_ERR(-EINVAL);
			return -EINVAL;
		}

		for (stage = pipeline->stages; stage; stage = stage->next) {
			/*
			 * The SP will read the params after it got
			 * empty 3a and dis
			 */
			if (STATS_ENABLED(stage)) {
				/* there is a stage that needs it */
				return_err = ia_css_bufq_enqueue_buffer(thread_id,
									queue_id,
									(uint32_t)h_vbuf->vptr);
			}
		}
	} else if (buf_type == IA_CSS_BUFFER_TYPE_INPUT_FRAME ||
		   buf_type == IA_CSS_BUFFER_TYPE_OUTPUT_FRAME ||
		   buf_type == IA_CSS_BUFFER_TYPE_VF_OUTPUT_FRAME ||
		   buf_type == IA_CSS_BUFFER_TYPE_SEC_OUTPUT_FRAME ||
		   buf_type == IA_CSS_BUFFER_TYPE_SEC_VF_OUTPUT_FRAME ||
		   buf_type == IA_CSS_BUFFER_TYPE_METADATA) {
		return_err = ia_css_bufq_enqueue_buffer(thread_id,
							queue_id,
							(uint32_t)h_vbuf->vptr);
		if (!return_err &&
		    buf_type == IA_CSS_BUFFER_TYPE_OUTPUT_FRAME) {
			IA_CSS_LOG("pfp: enqueued OF %d to q %d thread %d",
				   ddr_buffer.payload.frame.frame_data,
				   queue_id, thread_id);
		}
	}

	if (!return_err) {
		if (sh_css_hmm_buffer_record_acquire(
			h_vbuf, buf_type,
			HOST_ADDRESS(ddr_buffer.kernel_ptr))) {
			IA_CSS_LOG("send vbuf=%p", h_vbuf);
		} else {
			return_err = -EINVAL;
			IA_CSS_ERROR("hmm_buffer_record[]: no available slots\n");
		}
	}

	/*
	 * Tell the SP which queues are not empty,
	 * by sending the software event.
	 */
	if (!return_err) {
		if (!sh_css_sp_is_running()) {
			/* SP is not running. The queues are not valid */
			IA_CSS_LOG("SP is not running!");
			IA_CSS_LEAVE_ERR(-EBUSY);
			return -EBUSY;
		}
		return_err = ia_css_bufq_enqueue_psys_event(
				 IA_CSS_PSYS_SW_EVENT_BUFFER_ENQUEUED,
				 (uint8_t)thread_id,
				 queue_id,
				 0);
	} else {
		ia_css_rmgr_rel_vbuf(hmm_buffer_pool, &h_vbuf);
		IA_CSS_ERROR("buffer not enqueued");
	}

	IA_CSS_LEAVE("return value = %d", return_err);

	return return_err;
}

/*
 * TODO: Free up the hmm memory space.
 */
int
ia_css_pipe_dequeue_buffer(struct ia_css_pipe *pipe,
			   struct ia_css_buffer *buffer)
{
	int return_err;
	enum sh_css_queue_id queue_id;
	ia_css_ptr ddr_buffer_addr = (ia_css_ptr)0;
	struct sh_css_hmm_buffer ddr_buffer;
	enum ia_css_buffer_type buf_type;
	enum ia_css_pipe_id pipe_id;
	unsigned int thread_id;
	hrt_address kernel_ptr = 0;
	bool ret_err;

	IA_CSS_ENTER("pipe=%p, buffer=%p", pipe, buffer);

	if ((!pipe) || (!buffer)) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	pipe_id = pipe->mode;

	buf_type = buffer->type;

	IA_CSS_LOG("pipe_id=%d, buf_type=%d", pipe_id, buf_type);

	ddr_buffer.kernel_ptr = 0;

	ret_err = ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &thread_id);
	if (!ret_err) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	ret_err = ia_css_query_internal_queue_id(buf_type, thread_id, &queue_id);
	if (!ret_err) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	if ((queue_id <= SH_CSS_INVALID_QUEUE_ID) || (queue_id >= SH_CSS_MAX_NUM_QUEUES)) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	if (!sh_css_sp_is_running()) {
		IA_CSS_LOG("SP is not running!");
		IA_CSS_LEAVE_ERR(-EBUSY);
		/* SP is not running. The queues are not valid */
		return -EBUSY;
	}

	return_err = ia_css_bufq_dequeue_buffer(queue_id,
						(uint32_t *)&ddr_buffer_addr);

	if (!return_err) {
		struct ia_css_frame *frame;
		struct sh_css_hmm_buffer_record *hmm_buffer_record = NULL;

		IA_CSS_LOG("receive vbuf=%x", (int)ddr_buffer_addr);

		/* Validate the ddr_buffer_addr and buf_type */
		hmm_buffer_record = sh_css_hmm_buffer_record_validate(
		    ddr_buffer_addr, buf_type);
		if (hmm_buffer_record) {
			/*
			 * valid hmm_buffer_record found. Save the kernel_ptr
			 * for validation after performing hmm_load.  The
			 * vbuf handle and buffer_record can be released.
			 */
			kernel_ptr = hmm_buffer_record->kernel_ptr;
			ia_css_rmgr_rel_vbuf(hmm_buffer_pool, &hmm_buffer_record->h_vbuf);
			sh_css_hmm_buffer_record_reset(hmm_buffer_record);
		} else {
			IA_CSS_ERROR("hmm_buffer_record not found (0x%x) buf_type(%d)",
				     ddr_buffer_addr, buf_type);
			IA_CSS_LEAVE_ERR(-EINVAL);
			return -EINVAL;
		}

		hmm_load(ddr_buffer_addr,
			 &ddr_buffer,
			 sizeof(struct sh_css_hmm_buffer));

		/*
		 * if the kernel_ptr is 0 or an invalid, return an error.
		 * do not access the buffer via the kernal_ptr.
		 */
		if ((ddr_buffer.kernel_ptr == 0) ||
		    (kernel_ptr != HOST_ADDRESS(ddr_buffer.kernel_ptr))) {
			IA_CSS_ERROR("kernel_ptr invalid");
			IA_CSS_ERROR("expected: (0x%llx)", (u64)kernel_ptr);
			IA_CSS_ERROR("actual: (0x%llx)", (u64)HOST_ADDRESS(ddr_buffer.kernel_ptr));
			IA_CSS_ERROR("buf_type: %d\n", buf_type);
			IA_CSS_LEAVE_ERR(-EINVAL);
			return -EINVAL;
		}

		if (ddr_buffer.kernel_ptr != 0) {
			/*
			 * buffer->exp_id : all instances to be removed later
			 * once the driver change is completed. See patch #5758
			 * for reference
			 */
			buffer->exp_id = 0;
			buffer->driver_cookie = ddr_buffer.cookie_ptr;
			buffer->timing_data = ddr_buffer.timing_data;

			if (buf_type == IA_CSS_BUFFER_TYPE_OUTPUT_FRAME ||
			    buf_type == IA_CSS_BUFFER_TYPE_VF_OUTPUT_FRAME) {
				buffer->isys_eof_clock_tick.ticks = ddr_buffer.isys_eof_clock_tick;
			}

			switch (buf_type) {
			case IA_CSS_BUFFER_TYPE_INPUT_FRAME:
			case IA_CSS_BUFFER_TYPE_OUTPUT_FRAME:
			case IA_CSS_BUFFER_TYPE_SEC_OUTPUT_FRAME:
				if (pipe && pipe->stop_requested) {
#if !defined(ISP2401)
					/*
					 * free mipi frames only for old input
					 * system for 2401 it is done in
					 * ia_css_stream_destroy call
					 */
					return_err = free_mipi_frames(pipe);
					if (return_err) {
						IA_CSS_LOG("free_mipi_frames() failed");
						IA_CSS_LEAVE_ERR(return_err);
						return return_err;
					}
#endif
					pipe->stop_requested = false;
				}
				fallthrough;
			case IA_CSS_BUFFER_TYPE_VF_OUTPUT_FRAME:
			case IA_CSS_BUFFER_TYPE_SEC_VF_OUTPUT_FRAME:
				frame = (struct ia_css_frame *)HOST_ADDRESS(ddr_buffer.kernel_ptr);
				buffer->data.frame = frame;
				buffer->exp_id = ddr_buffer.payload.frame.exp_id;
				frame->exp_id = ddr_buffer.payload.frame.exp_id;
				frame->isp_config_id = ddr_buffer.payload.frame.isp_parameters_id;
				if (ddr_buffer.payload.frame.flashed == 1)
					frame->flash_state =
					    IA_CSS_FRAME_FLASH_STATE_PARTIAL;
				if (ddr_buffer.payload.frame.flashed == 2)
					frame->flash_state =
					    IA_CSS_FRAME_FLASH_STATE_FULL;
				frame->valid = pipe->num_invalid_frames == 0;
				if (!frame->valid)
					pipe->num_invalid_frames--;

				if (frame->info.format == IA_CSS_FRAME_FORMAT_BINARY_8) {
#ifdef ISP2401
					frame->planes.binary.size = frame->data_bytes;
#else
					frame->planes.binary.size =
					    sh_css_sp_get_binary_copy_size();
#endif
				}
				if (buf_type == IA_CSS_BUFFER_TYPE_OUTPUT_FRAME) {
					IA_CSS_LOG("pfp: dequeued OF %d with config id %d thread %d",
						   frame->data, frame->isp_config_id, thread_id);
				}

				ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
						    "ia_css_pipe_dequeue_buffer() buf_type=%d, data(DDR address)=0x%x\n",
						    buf_type, buffer->data.frame->data);

				break;
			case IA_CSS_BUFFER_TYPE_3A_STATISTICS:
				buffer->data.stats_3a =
				    (struct ia_css_isp_3a_statistics *)HOST_ADDRESS(ddr_buffer.kernel_ptr);
				buffer->exp_id = ddr_buffer.payload.s3a.exp_id;
				buffer->data.stats_3a->exp_id = ddr_buffer.payload.s3a.exp_id;
				buffer->data.stats_3a->isp_config_id = ddr_buffer.payload.s3a.isp_config_id;
				break;
			case IA_CSS_BUFFER_TYPE_DIS_STATISTICS:
				buffer->data.stats_dvs =
				    (struct ia_css_isp_dvs_statistics *)
				    HOST_ADDRESS(ddr_buffer.kernel_ptr);
				buffer->exp_id = ddr_buffer.payload.dis.exp_id;
				buffer->data.stats_dvs->exp_id = ddr_buffer.payload.dis.exp_id;
				break;
			case IA_CSS_BUFFER_TYPE_LACE_STATISTICS:
				break;
			case IA_CSS_BUFFER_TYPE_METADATA:
				buffer->data.metadata =
				    (struct ia_css_metadata *)HOST_ADDRESS(ddr_buffer.kernel_ptr);
				buffer->exp_id = ddr_buffer.payload.metadata.exp_id;
				buffer->data.metadata->exp_id = ddr_buffer.payload.metadata.exp_id;
				break;
			default:
				return_err = -EINVAL;
				break;
			}
		}
	}

	/*
	 * Tell the SP which queues are not full,
	 * by sending the software event.
	 */
	if (!return_err) {
		if (!sh_css_sp_is_running()) {
			IA_CSS_LOG("SP is not running!");
			IA_CSS_LEAVE_ERR(-EBUSY);
			/* SP is not running. The queues are not valid */
			return -EBUSY;
		}
		ia_css_bufq_enqueue_psys_event(
		    IA_CSS_PSYS_SW_EVENT_BUFFER_DEQUEUED,
		    0,
		    queue_id,
		    0);
	}
	IA_CSS_LEAVE("buffer=%p", buffer);

	return return_err;
}

/*
 * Cannot Move this to event module as it is of ia_css_event_type which is declared in ia_css.h
 * TODO: modify and move it if possible.
 *
 * !!!IMPORTANT!!! KEEP THE FOLLOWING IN SYNC:
 * 1) "enum ia_css_event_type"					(ia_css_event_public.h)
 * 2) "enum sh_css_sp_event_type"				(sh_css_internal.h)
 * 3) "enum ia_css_event_type event_id_2_event_mask"		(event_handler.sp.c)
 * 4) "enum ia_css_event_type convert_event_sp_to_host_domain"	(sh_css.c)
 */
static enum ia_css_event_type convert_event_sp_to_host_domain[] = {
	IA_CSS_EVENT_TYPE_OUTPUT_FRAME_DONE,	/* Output frame ready. */
	IA_CSS_EVENT_TYPE_SECOND_OUTPUT_FRAME_DONE,	/* Second output frame ready. */
	IA_CSS_EVENT_TYPE_VF_OUTPUT_FRAME_DONE,	/* Viewfinder Output frame ready. */
	IA_CSS_EVENT_TYPE_SECOND_VF_OUTPUT_FRAME_DONE,	/* Second viewfinder Output frame ready. */
	IA_CSS_EVENT_TYPE_3A_STATISTICS_DONE,	/* Indication that 3A statistics are available. */
	IA_CSS_EVENT_TYPE_DIS_STATISTICS_DONE,	/* Indication that DIS statistics are available. */
	IA_CSS_EVENT_TYPE_PIPELINE_DONE,	/* Pipeline Done event, sent after last pipeline stage. */
	IA_CSS_EVENT_TYPE_FRAME_TAGGED,		/* Frame tagged. */
	IA_CSS_EVENT_TYPE_INPUT_FRAME_DONE,	/* Input frame ready. */
	IA_CSS_EVENT_TYPE_METADATA_DONE,	/* Metadata ready. */
	IA_CSS_EVENT_TYPE_LACE_STATISTICS_DONE,	/* Indication that LACE statistics are available. */
	IA_CSS_EVENT_TYPE_ACC_STAGE_COMPLETE,	/* Extension stage executed. */
	IA_CSS_EVENT_TYPE_TIMER,		/* Timing measurement data. */
	IA_CSS_EVENT_TYPE_PORT_EOF,		/* End Of Frame event, sent when in buffered sensor mode. */
	IA_CSS_EVENT_TYPE_FW_WARNING,		/* Performance warning encountered by FW */
	IA_CSS_EVENT_TYPE_FW_ASSERT,		/* Assertion hit by FW */
	0,					/* error if sp passes  SH_CSS_SP_EVENT_NR_OF_TYPES as a valid event. */
};

int
ia_css_dequeue_psys_event(struct ia_css_event *event)
{
	enum ia_css_pipe_id pipe_id = 0;
	u8 payload[4] = {0, 0, 0, 0};
	int ret_err;

	/*
	 * TODO:
	 * a) use generic decoding function , same as the one used by sp.
	 * b) group decode and dequeue into eventQueue module
	 *
	 * We skip the IA_CSS_ENTER logging call
	 * to avoid flooding the logs when the host application
	 * uses polling.
	 */
	if (!event)
		return -EINVAL;

	/* SP is not running. The queues are not valid */
	if (!sh_css_sp_is_running())
		return -EBUSY;

	/* dequeue the event (if any) from the psys event queue */
	ret_err = ia_css_bufq_dequeue_psys_event(payload);
	if (ret_err)
		return ret_err;

	IA_CSS_LOG("event dequeued from psys event queue");

	/* Tell the SP that we dequeued an event from the event queue. */
	ia_css_bufq_enqueue_psys_event(
	    IA_CSS_PSYS_SW_EVENT_EVENT_DEQUEUED, 0, 0, 0);

	/*
	 * Events are decoded into 4 bytes of payload, the first byte
	 * contains the sp event type. This is converted to a host enum.
	 * TODO: can this enum conversion be eliminated
	 */
	event->type = convert_event_sp_to_host_domain[payload[0]];
	/* Some sane default values since not all events use all fields. */
	event->pipe = NULL;
	event->port = MIPI_PORT0_ID;
	event->exp_id = 0;
	event->fw_warning = IA_CSS_FW_WARNING_NONE;
	event->fw_handle = 0;
	event->timer_data = 0;
	event->timer_code = 0;
	event->timer_subcode = 0;

	if (event->type == IA_CSS_EVENT_TYPE_TIMER) {
		/*
		 * timer event ??? get the 2nd event and decode the data
		 * into the event struct
		 */
		u32 tmp_data;
		/* 1st event: LSB 16-bit timer data and code */
		event->timer_data = ((payload[1] & 0xFF) | ((payload[3] & 0xFF) << 8));
		event->timer_code = payload[2];
		payload[0] = payload[1] = payload[2] = payload[3] = 0;
		ret_err = ia_css_bufq_dequeue_psys_event(payload);
		if (ret_err) {
			/* no 2nd event ??? an error */
			/*
			 * Putting IA_CSS_ERROR is resulting in failures in
			 * Merrifield smoke testing
			 */
			IA_CSS_WARNING("Timer: Error de-queuing the 2nd TIMER event!!!\n");
			return ret_err;
		}
		ia_css_bufq_enqueue_psys_event(
		    IA_CSS_PSYS_SW_EVENT_EVENT_DEQUEUED, 0, 0, 0);
		event->type = convert_event_sp_to_host_domain[payload[0]];
		/* It's a timer */
		if (event->type == IA_CSS_EVENT_TYPE_TIMER) {
			/* 2nd event data: MSB 16-bit timer and subcode */
			tmp_data = ((payload[1] & 0xFF) | ((payload[3] & 0xFF) << 8));
			event->timer_data |= (tmp_data << 16);
			event->timer_subcode = payload[2];
		} else {
			/*
			 * It's a non timer event. So clear first half of the
			 * timer event data.
			 * If the second part of the TIMER event is not
			 * received, we discard the first half of the timer
			 * data and process the non timer event without
			 * affecting the flow. So the non timer event falls
			 * through the code.
			 */
			event->timer_data = 0;
			event->timer_code = 0;
			event->timer_subcode = 0;
			IA_CSS_ERROR("Missing 2nd timer event. Timer event discarded");
		}
	}
	if (event->type == IA_CSS_EVENT_TYPE_PORT_EOF) {
		event->port = (enum mipi_port_id)payload[1];
		event->exp_id = payload[3];
	} else if (event->type == IA_CSS_EVENT_TYPE_FW_WARNING) {
		event->fw_warning = (enum ia_css_fw_warning)payload[1];
		/* exp_id is only available in these warning types */
		if (event->fw_warning == IA_CSS_FW_WARNING_EXP_ID_LOCKED ||
		    event->fw_warning == IA_CSS_FW_WARNING_TAG_EXP_ID_FAILED)
			event->exp_id = payload[3];
	} else if (event->type == IA_CSS_EVENT_TYPE_FW_ASSERT) {
		event->fw_assert_module_id = payload[1]; /* module */
		event->fw_assert_line_no = (payload[2] << 8) + payload[3];
		/* payload[2] is line_no>>8, payload[3] is line_no&0xff */
	} else if (event->type != IA_CSS_EVENT_TYPE_TIMER) {
		/*
		 * pipe related events.
		 * payload[1] contains the pipe_num,
		 * payload[2] contains the pipe_id. These are different.
		 */
		event->pipe = find_pipe_by_num(payload[1]);
		pipe_id = (enum ia_css_pipe_id)payload[2];
		/* Check to see if pipe still exists */
		if (!event->pipe)
			return -EBUSY;

		if (event->type == IA_CSS_EVENT_TYPE_FRAME_TAGGED) {
			/* find the capture pipe that goes with this */
			int i, n;

			n = event->pipe->stream->num_pipes;
			for (i = 0; i < n; i++) {
				struct ia_css_pipe *p =
					    event->pipe->stream->pipes[i];
				if (p->config.mode == IA_CSS_PIPE_MODE_CAPTURE) {
					event->pipe = p;
					break;
				}
			}
			event->exp_id = payload[3];
		}
		if (event->type == IA_CSS_EVENT_TYPE_ACC_STAGE_COMPLETE) {
			/* payload[3] contains the acc fw handle. */
			u32 stage_num = (uint32_t)payload[3];

			ret_err = ia_css_pipeline_get_fw_from_stage(
				      &event->pipe->pipeline,
				      stage_num,
				      &event->fw_handle);
			if (ret_err) {
				IA_CSS_ERROR("Invalid stage num received for ACC event. stage_num:%u",
					     stage_num);
				return ret_err;
			}
		}
	}

	if (event->pipe)
		IA_CSS_LEAVE("event_id=%d, pipe_id=%d", event->type, pipe_id);
	else
		IA_CSS_LEAVE("event_id=%d", event->type);

	return 0;
}

int
ia_css_dequeue_isys_event(struct ia_css_event *event)
{
	u8 payload[4] = {0, 0, 0, 0};
	int err = 0;

	/*
	 * We skip the IA_CSS_ENTER logging call
	 * to avoid flooding the logs when the host application
	 * uses polling.
	 */
	if (!event)
		return -EINVAL;

	/* SP is not running. The queues are not valid */
	if (!sh_css_sp_is_running())
		return -EBUSY;

	err = ia_css_bufq_dequeue_isys_event(payload);
	if (err)
		return err;

	IA_CSS_LOG("event dequeued from isys event queue");

	/* Update SP state to indicate that element was dequeued. */
	ia_css_bufq_enqueue_isys_event(IA_CSS_ISYS_SW_EVENT_EVENT_DEQUEUED);

	/* Fill return struct with appropriate info */
	event->type = IA_CSS_EVENT_TYPE_PORT_EOF;
	/* EOF events are associated with a CSI port, not with a pipe */
	event->pipe = NULL;
	event->port = payload[1];
	event->exp_id = payload[3];

	IA_CSS_LEAVE_ERR(err);
	return err;
}

static void
acc_start(struct ia_css_pipe *pipe)
{
	assert(pipe);
	assert(pipe->stream);

	start_pipe(pipe, SH_CSS_PIPE_CONFIG_OVRD_NO_OVRD,
		   pipe->stream->config.mode);
}

static int
sh_css_pipe_start(struct ia_css_stream *stream)
{
	int err = 0;

	struct ia_css_pipe *pipe;
	enum ia_css_pipe_id pipe_id;
	unsigned int thread_id;

	IA_CSS_ENTER_PRIVATE("stream = %p", stream);

	if (!stream) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}
	pipe = stream->last_pipe;
	if (!pipe) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	pipe_id = pipe->mode;

	if (stream->started) {
		IA_CSS_WARNING("Cannot start stream that is already started");
		IA_CSS_LEAVE_ERR(err);
		return err;
	}

	pipe->stop_requested = false;

	switch (pipe_id) {
	case IA_CSS_PIPE_ID_PREVIEW:
		err = preview_start(pipe);
		break;
	case IA_CSS_PIPE_ID_VIDEO:
		err = video_start(pipe);
		break;
	case IA_CSS_PIPE_ID_CAPTURE:
		err = capture_start(pipe);
		break;
	case IA_CSS_PIPE_ID_YUVPP:
		err = yuvpp_start(pipe);
		break;
	case IA_CSS_PIPE_ID_ACC:
		acc_start(pipe);
		break;
	default:
		err = -EINVAL;
	}
	/* DH regular multi pipe - not continuous mode: start the next pipes too */
	if (!stream->config.continuous) {
		int i;

		for (i = 1; i < stream->num_pipes && 0 == err ; i++) {
			switch (stream->pipes[i]->mode) {
			case IA_CSS_PIPE_ID_PREVIEW:
				stream->pipes[i]->stop_requested = false;
				err = preview_start(stream->pipes[i]);
				break;
			case IA_CSS_PIPE_ID_VIDEO:
				stream->pipes[i]->stop_requested = false;
				err = video_start(stream->pipes[i]);
				break;
			case IA_CSS_PIPE_ID_CAPTURE:
				stream->pipes[i]->stop_requested = false;
				err = capture_start(stream->pipes[i]);
				break;
			case IA_CSS_PIPE_ID_YUVPP:
				stream->pipes[i]->stop_requested = false;
				err = yuvpp_start(stream->pipes[i]);
				break;
			case IA_CSS_PIPE_ID_ACC:
				stream->pipes[i]->stop_requested = false;
				acc_start(stream->pipes[i]);
				break;
			default:
				err = -EINVAL;
			}
		}
	}
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}

	/*
	 * Force ISP parameter calculation after a mode change
	 * Acceleration API examples pass NULL for stream but they
	 * don't use ISP parameters anyway. So this should be okay.
	 * The SP binary (jpeg) copy does not use any parameters.
	 */
	if (!copy_on_sp(pipe)) {
		sh_css_invalidate_params(stream);
		err = sh_css_param_update_isp_params(pipe,
						     stream->isp_params_configs, true, NULL);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	}

	ia_css_debug_pipe_graph_dump_epilogue();

	ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &thread_id);

	if (!sh_css_sp_is_running()) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EBUSY);
		/* SP is not running. The queues are not valid */
		return -EBUSY;
	}
	ia_css_bufq_enqueue_psys_event(IA_CSS_PSYS_SW_EVENT_START_STREAM,
				       (uint8_t)thread_id, 0, 0);

	/* DH regular multi pipe - not continuous mode: enqueue event to the next pipes too */
	if (!stream->config.continuous) {
		int i;

		for (i = 1; i < stream->num_pipes; i++) {
			ia_css_pipeline_get_sp_thread_id(
			    ia_css_pipe_get_pipe_num(stream->pipes[i]),
			    &thread_id);
			ia_css_bufq_enqueue_psys_event(
			    IA_CSS_PSYS_SW_EVENT_START_STREAM,
			    (uint8_t)thread_id, 0, 0);
		}
	}

	/* in case of continuous capture mode, we also start capture thread and copy thread*/
	if (pipe->stream->config.continuous) {
		struct ia_css_pipe *copy_pipe = NULL;

		if (pipe_id == IA_CSS_PIPE_ID_PREVIEW)
			copy_pipe = pipe->pipe_settings.preview.copy_pipe;
		else if (pipe_id == IA_CSS_PIPE_ID_VIDEO)
			copy_pipe = pipe->pipe_settings.video.copy_pipe;

		if (!copy_pipe) {
			IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
			return -EINVAL;
		}
		ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(copy_pipe),
						 &thread_id);
		/* by the time we reach here q is initialized and handle is available.*/
		ia_css_bufq_enqueue_psys_event(
		    IA_CSS_PSYS_SW_EVENT_START_STREAM,
		    (uint8_t)thread_id, 0,  0);
	}
	if (pipe->stream->cont_capt) {
		struct ia_css_pipe *capture_pipe = NULL;

		if (pipe_id == IA_CSS_PIPE_ID_PREVIEW)
			capture_pipe = pipe->pipe_settings.preview.capture_pipe;
		else if (pipe_id == IA_CSS_PIPE_ID_VIDEO)
			capture_pipe = pipe->pipe_settings.video.capture_pipe;

		if (!capture_pipe) {
			IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
			return -EINVAL;
		}
		ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(capture_pipe),
						 &thread_id);
		/* by the time we reach here q is initialized and handle is available.*/
		ia_css_bufq_enqueue_psys_event(
		    IA_CSS_PSYS_SW_EVENT_START_STREAM,
		    (uint8_t)thread_id, 0,  0);
	}

	/* in case of PREVIEW mode, check whether QOS acc_pipe is available, then start the qos pipe */
	if (pipe_id == IA_CSS_PIPE_ID_PREVIEW) {
		struct ia_css_pipe *acc_pipe = NULL;

		acc_pipe = pipe->pipe_settings.preview.acc_pipe;

		if (acc_pipe) {
			ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(acc_pipe),
							 &thread_id);
			/* by the time we reach here q is initialized and handle is available.*/
			ia_css_bufq_enqueue_psys_event(
			    IA_CSS_PSYS_SW_EVENT_START_STREAM,
			    (uint8_t)thread_id, 0, 0);
		}
	}

	stream->started = true;

	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

/* ISP2400 */
void
sh_css_enable_cont_capt(bool enable, bool stop_copy_preview)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "sh_css_enable_cont_capt() enter: enable=%d\n", enable);
//my_css.cont_capt = enable;
	my_css.stop_copy_preview = stop_copy_preview;
}

bool
sh_css_continuous_is_enabled(uint8_t pipe_num)
{
	struct ia_css_pipe *pipe;
	bool continuous;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "sh_css_continuous_is_enabled() enter: pipe_num=%d\n", pipe_num);

	pipe = find_pipe_by_num(pipe_num);
	continuous = pipe && pipe->stream->config.continuous;
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "sh_css_continuous_is_enabled() leave: enable=%d\n",
			    continuous);
	return continuous;
}

/* ISP2400 */
int
ia_css_stream_get_max_buffer_depth(struct ia_css_stream *stream,
				   int *buffer_depth)
{
	if (!buffer_depth)
		return -EINVAL;
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_stream_get_max_buffer_depth() enter: void\n");
	(void)stream;
	*buffer_depth = NUM_CONTINUOUS_FRAMES;
	return 0;
}

int
ia_css_stream_set_buffer_depth(struct ia_css_stream *stream, int buffer_depth)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_stream_set_buffer_depth() enter: num_frames=%d\n", buffer_depth);
	(void)stream;
	if (buffer_depth > NUM_CONTINUOUS_FRAMES || buffer_depth < 1)
		return -EINVAL;
	/* ok, value allowed */
	stream->config.target_num_cont_raw_buf = buffer_depth;
	/* TODO: check what to regarding initialization */
	return 0;
}

/* ISP2401 */
int
ia_css_stream_get_buffer_depth(struct ia_css_stream *stream,
			       int *buffer_depth)
{
	if (!buffer_depth)
		return -EINVAL;
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_stream_get_buffer_depth() enter: void\n");
	(void)stream;
	*buffer_depth = stream->config.target_num_cont_raw_buf;
	return 0;
}

#if !defined(ISP2401)
unsigned int
sh_css_get_mipi_sizes_for_check(const unsigned int port, const unsigned int idx)
{
	OP___assert(port < N_CSI_PORTS);
	OP___assert(idx  < IA_CSS_MIPI_SIZE_CHECK_MAX_NOF_ENTRIES_PER_PORT);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_get_mipi_sizes_for_check(port %d, idx %d): %d\n",
			    port, idx, my_css.mipi_sizes_for_check[port][idx]);
	return my_css.mipi_sizes_for_check[port][idx];
}
#endif

static int sh_css_pipe_configure_output(
    struct ia_css_pipe *pipe,
    unsigned int width,
    unsigned int height,
    unsigned int padded_width,
    enum ia_css_frame_format format,
    unsigned int idx)
{
	int err = 0;

	IA_CSS_ENTER_PRIVATE("pipe = %p, width = %d, height = %d, padded width = %d, format = %d, idx = %d",
			     pipe, width, height, padded_width, format, idx);
	if (!pipe) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	err = ia_css_util_check_res(width, height);
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}
	if (pipe->output_info[idx].res.width != width ||
	    pipe->output_info[idx].res.height != height ||
	    pipe->output_info[idx].format != format) {
		ia_css_frame_info_init(
		    &pipe->output_info[idx],
		    width,
		    height,
		    format,
		    padded_width);
	}
	IA_CSS_LEAVE_ERR_PRIVATE(0);
	return 0;
}

static int
sh_css_pipe_get_shading_info(struct ia_css_pipe *pipe,
			     struct ia_css_shading_info *shading_info,
			     struct ia_css_pipe_config *pipe_config)
{
	int err = 0;
	struct ia_css_binary *binary = NULL;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_pipe_get_shading_info() enter:\n");

	binary = ia_css_pipe_get_shading_correction_binary(pipe);

	if (binary) {
		err = ia_css_binary_get_shading_info(binary,
						     IA_CSS_SHADING_CORRECTION_TYPE_1,
						     pipe->required_bds_factor,
						     (const struct ia_css_stream_config *)&pipe->stream->config,
						     shading_info, pipe_config);

		/*
		 * Other function calls can be added here when other shading
		 * correction types will be added in the future.
		 */
	} else {
		/*
		 * When the pipe does not have a binary which has the shading
		 * correction, this function does not need to fill the shading
		 * information. It is not a error case, and then
		 * this function should return 0.
		 */
		memset(shading_info, 0, sizeof(*shading_info));
	}
	return err;
}

static int
sh_css_pipe_get_grid_info(struct ia_css_pipe *pipe,
			  struct ia_css_grid_info *info)
{
	int err = 0;
	struct ia_css_binary *binary = NULL;

	assert(pipe);
	assert(info);

	IA_CSS_ENTER_PRIVATE("");

	binary = ia_css_pipe_get_s3a_binary(pipe);

	if (binary) {
		err = ia_css_binary_3a_grid_info(binary, info, pipe);
		if (err)
			goto err;
	} else {
		memset(&info->s3a_grid, 0, sizeof(info->s3a_grid));
	}

	binary = ia_css_pipe_get_sdis_binary(pipe);

	if (binary) {
		ia_css_binary_dvs_grid_info(binary, info, pipe);
		ia_css_binary_dvs_stat_grid_info(binary, info, pipe);
	} else {
		memset(&info->dvs_grid, 0, sizeof(info->dvs_grid));
		memset(&info->dvs_grid.dvs_stat_grid_info, 0,
			   sizeof(info->dvs_grid.dvs_stat_grid_info));
	}

	if (binary) {
		/* copy pipe does not have ISP binary*/
		info->isp_in_width = binary->internal_frame_info.res.width;
		info->isp_in_height = binary->internal_frame_info.res.height;
	}

	info->vamem_type = IA_CSS_VAMEM_TYPE_2;

err:
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

/* ISP2401 */
/*
 * @brief Check if a format is supported by the pipe.
 *
 */
static int
ia_css_pipe_check_format(struct ia_css_pipe *pipe,
			 enum ia_css_frame_format format)
{
	const enum ia_css_frame_format *supported_formats;
	int number_of_formats;
	int found = 0;
	int i;

	IA_CSS_ENTER_PRIVATE("");

	if (NULL == pipe || NULL == pipe->pipe_settings.video.video_binary.info) {
		IA_CSS_ERROR("Pipe or binary info is not set");
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	supported_formats = pipe->pipe_settings.video.video_binary.info->output_formats;
	number_of_formats = sizeof(pipe->pipe_settings.video.video_binary.info->output_formats) / sizeof(enum ia_css_frame_format);

	for (i = 0; i < number_of_formats && !found; i++) {
		if (supported_formats[i] == format) {
			found = 1;
			break;
		}
	}
	if (!found) {
		IA_CSS_ERROR("Requested format is not supported by binary");
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}
	IA_CSS_LEAVE_ERR_PRIVATE(0);
	return 0;
}

static int load_video_binaries(struct ia_css_pipe *pipe)
{
	struct ia_css_frame_info video_in_info, tnr_info,
		       *video_vf_info, video_bds_out_info, *pipe_out_info, *pipe_vf_out_info;
	bool online;
	int err = 0;
	bool continuous = pipe->stream->config.continuous;
	unsigned int i;
	unsigned int num_output_pins;
	struct ia_css_frame_info video_bin_out_info;
	bool need_scaler = false;
	bool vf_res_different_than_output = false;
	bool need_vf_pp = false;
	int vf_ds_log2;
	struct ia_css_video_settings *mycs  = &pipe->pipe_settings.video;

	IA_CSS_ENTER_PRIVATE("");
	assert(pipe);
	assert(pipe->mode == IA_CSS_PIPE_ID_VIDEO);
	/*
	 * we only test the video_binary because offline video doesn't need a
	 * vf_pp binary and online does not (always use) the copy_binary.
	 * All are always reset at the same time anyway.
	 */
	if (mycs->video_binary.info)
		return 0;

	online = pipe->stream->config.online;
	pipe_out_info = &pipe->output_info[0];
	pipe_vf_out_info = &pipe->vf_output_info[0];

	assert(pipe_out_info);

	/*
	 * There is no explicit input format requirement for raw or yuv
	 * What matters is that there is a binary that supports the stream format.
	 * This is checked in the binary_find(), so no need to check it here
	 */
	err = ia_css_util_check_input(&pipe->stream->config, false, false);
	if (err)
		return err;
	/* cannot have online video and input_mode memory */
	if (online && pipe->stream->config.mode == IA_CSS_INPUT_MODE_MEMORY)
		return -EINVAL;
	if (pipe->enable_viewfinder[IA_CSS_PIPE_OUTPUT_STAGE_0]) {
		err = ia_css_util_check_vf_out_info(pipe_out_info,
						    pipe_vf_out_info);
		if (err)
			return err;
	} else {
		err = ia_css_frame_check_info(pipe_out_info);
		if (err)
			return err;
	}

	if (pipe->out_yuv_ds_input_info.res.width)
		video_bin_out_info = pipe->out_yuv_ds_input_info;
	else
		video_bin_out_info = *pipe_out_info;

	/* Video */
	if (pipe->enable_viewfinder[IA_CSS_PIPE_OUTPUT_STAGE_0]) {
		video_vf_info = pipe_vf_out_info;
		vf_res_different_than_output = (video_vf_info->res.width !=
						video_bin_out_info.res.width) ||
					       (video_vf_info->res.height != video_bin_out_info.res.height);
	} else {
		video_vf_info = NULL;
	}

	need_scaler = need_downscaling(video_bin_out_info.res, pipe_out_info->res);

	/* we build up the pipeline starting at the end */
	/* YUV post-processing if needed */
	if (need_scaler) {
		struct ia_css_cas_binary_descr cas_scaler_descr = { };

		/* NV12 is the common format that is supported by both */
		/* yuv_scaler and the video_xx_isp2_min binaries. */
		video_bin_out_info.format = IA_CSS_FRAME_FORMAT_NV12;

		err = ia_css_pipe_create_cas_scaler_desc_single_output(
			  &video_bin_out_info,
			  pipe_out_info,
			  NULL,
			  &cas_scaler_descr);
		if (err)
			return err;
		mycs->num_yuv_scaler = cas_scaler_descr.num_stage;
		mycs->yuv_scaler_binary = kcalloc(cas_scaler_descr.num_stage,
						  sizeof(struct ia_css_binary),
						  GFP_KERNEL);
		if (!mycs->yuv_scaler_binary) {
			err = -ENOMEM;
			return err;
		}
		mycs->is_output_stage = kcalloc(cas_scaler_descr.num_stage,
						sizeof(bool), GFP_KERNEL);
		if (!mycs->is_output_stage) {
			err = -ENOMEM;
			return err;
		}
		for (i = 0; i < cas_scaler_descr.num_stage; i++) {
			struct ia_css_binary_descr yuv_scaler_descr;

			mycs->is_output_stage[i] = cas_scaler_descr.is_output_stage[i];
			ia_css_pipe_get_yuvscaler_binarydesc(pipe,
							     &yuv_scaler_descr, &cas_scaler_descr.in_info[i],
							     &cas_scaler_descr.out_info[i],
							     &cas_scaler_descr.internal_out_info[i],
							     &cas_scaler_descr.vf_info[i]);
			err = ia_css_binary_find(&yuv_scaler_descr,
						 &mycs->yuv_scaler_binary[i]);
			if (err) {
				kfree(mycs->is_output_stage);
				mycs->is_output_stage = NULL;
				return err;
			}
		}
		ia_css_pipe_destroy_cas_scaler_desc(&cas_scaler_descr);
	}

	{
		struct ia_css_binary_descr video_descr;
		enum ia_css_frame_format vf_info_format;

		err = ia_css_pipe_get_video_binarydesc(pipe,
						       &video_descr, &video_in_info, &video_bds_out_info, &video_bin_out_info,
						       video_vf_info,
						       pipe->stream->config.left_padding);
		if (err)
			return err;

		/*
		 * In the case where video_vf_info is not NULL, this allows
		 * us to find a potential video library with desired vf format.
		 * If success, no vf_pp binary is needed.
		 * If failed, we will look up video binary with YUV_LINE vf format
		 */
		err = ia_css_binary_find(&video_descr,
					 &mycs->video_binary);

		if (err) {
			/* This will do another video binary lookup later for YUV_LINE format*/
			if (video_vf_info)
				need_vf_pp = true;
			else
				return err;
		} else if (video_vf_info) {
			/*
			 * The first video binary lookup is successful, but we
			 * may still need vf_pp binary based on additional check
			 */
			num_output_pins = mycs->video_binary.info->num_output_pins;
			vf_ds_log2 = mycs->video_binary.vf_downscale_log2;

			/*
			 * If the binary has dual output pins, we need vf_pp
			 * if the resolution is different.
			 */
			need_vf_pp |= ((num_output_pins == 2) && vf_res_different_than_output);

			/*
			 * If the binary has single output pin, we need vf_pp
			 * if additional scaling is needed for vf
			 */
			need_vf_pp |= ((num_output_pins == 1) &&
				       ((video_vf_info->res.width << vf_ds_log2 != pipe_out_info->res.width) ||
					(video_vf_info->res.height << vf_ds_log2 != pipe_out_info->res.height)));
		}

		if (need_vf_pp) {
			/* save the current vf_info format for restoration later */
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
					    "load_video_binaries() need_vf_pp; find video binary with YUV_LINE again\n");

			vf_info_format = video_vf_info->format;

			if (!pipe->config.enable_vfpp_bci)
				ia_css_frame_info_set_format(video_vf_info,
							     IA_CSS_FRAME_FORMAT_YUV_LINE);

			ia_css_binary_destroy_isp_parameters(&mycs->video_binary);

			err = ia_css_binary_find(&video_descr,
						 &mycs->video_binary);

			/* restore original vf_info format */
			ia_css_frame_info_set_format(video_vf_info,
						     vf_info_format);
			if (err)
				return err;
		}
	}

	/*
	 * If a video binary does not use a ref_frame, we set the frame delay
	 * to 0. This is the case for the 1-stage low-power video binary.
	 */
	if (!mycs->video_binary.info->sp.enable.ref_frame)
		pipe->dvs_frame_delay = 0;

	/*
	 * The delay latency determines the number of invalid frames after
	 * a stream is started.
	 */
	pipe->num_invalid_frames = pipe->dvs_frame_delay;
	pipe->info.num_invalid_frames = pipe->num_invalid_frames;

	/*
	 * Viewfinder frames also decrement num_invalid_frames. If the pipe
	 * outputs a viewfinder output, then we need double the number of
	 * invalid frames
	 */
	if (video_vf_info)
		pipe->num_invalid_frames *= 2;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "load_video_binaries() num_invalid_frames=%d dvs_frame_delay=%d\n",
			    pipe->num_invalid_frames, pipe->dvs_frame_delay);

	/* pqiao TODO: temp hack for PO, should be removed after offline YUVPP is enabled */
#if !defined(ISP2401)
	/* Copy */
	if (!online && !continuous) {
		/*
		 * TODO: what exactly needs doing, prepend the copy binary to
		 *	 video base this only on !online?
		 */
		err = load_copy_binary(pipe,
				       &mycs->copy_binary,
				       &mycs->video_binary);
		if (err)
			return err;
	}
#else
	(void)continuous;
#endif

	if (pipe->enable_viewfinder[IA_CSS_PIPE_OUTPUT_STAGE_0] && need_vf_pp) {
		struct ia_css_binary_descr vf_pp_descr;

		if (mycs->video_binary.vf_frame_info.format
		    == IA_CSS_FRAME_FORMAT_YUV_LINE) {
			ia_css_pipe_get_vfpp_binarydesc(pipe, &vf_pp_descr,
							&mycs->video_binary.vf_frame_info,
							pipe_vf_out_info);
		} else {
			/*
			 * output from main binary is not yuv line. currently
			 * this is possible only when bci is enabled on vfpp
			 * output
			 */
			assert(pipe->config.enable_vfpp_bci);
			ia_css_pipe_get_yuvscaler_binarydesc(pipe, &vf_pp_descr,
							     &mycs->video_binary.vf_frame_info,
							     pipe_vf_out_info, NULL, NULL);
		}

		err = ia_css_binary_find(&vf_pp_descr,
					 &mycs->vf_pp_binary);
		if (err)
			return err;
	}

	err = allocate_delay_frames(pipe);

	if (err)
		return err;

	if (mycs->video_binary.info->sp.enable.block_output) {
		tnr_info = mycs->video_binary.out_frame_info[0];

		/* Make tnr reference buffers output block height align */
		tnr_info.res.height = CEIL_MUL(tnr_info.res.height,
					       mycs->video_binary.info->sp.block.output_block_height);
	} else {
		tnr_info = mycs->video_binary.internal_frame_info;
	}
	tnr_info.format = IA_CSS_FRAME_FORMAT_YUV_LINE;
	tnr_info.raw_bit_depth = SH_CSS_TNR_BIT_DEPTH;

	for (i = 0; i < NUM_VIDEO_TNR_FRAMES; i++) {
		if (mycs->tnr_frames[i]) {
			ia_css_frame_free(mycs->tnr_frames[i]);
			mycs->tnr_frames[i] = NULL;
		}
		err = ia_css_frame_allocate_from_info(
			  &mycs->tnr_frames[i],
			  &tnr_info);
		if (err)
			return err;
	}
	IA_CSS_LEAVE_PRIVATE("");
	return 0;
}

static int
unload_video_binaries(struct ia_css_pipe *pipe)
{
	unsigned int i;

	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);

	if ((!pipe) || (pipe->mode != IA_CSS_PIPE_ID_VIDEO)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}
	ia_css_binary_unload(&pipe->pipe_settings.video.copy_binary);
	ia_css_binary_unload(&pipe->pipe_settings.video.video_binary);
	ia_css_binary_unload(&pipe->pipe_settings.video.vf_pp_binary);

	for (i = 0; i < pipe->pipe_settings.video.num_yuv_scaler; i++)
		ia_css_binary_unload(&pipe->pipe_settings.video.yuv_scaler_binary[i]);

	kfree(pipe->pipe_settings.video.is_output_stage);
	pipe->pipe_settings.video.is_output_stage = NULL;
	kfree(pipe->pipe_settings.video.yuv_scaler_binary);
	pipe->pipe_settings.video.yuv_scaler_binary = NULL;

	IA_CSS_LEAVE_ERR_PRIVATE(0);
	return 0;
}

static int video_start(struct ia_css_pipe *pipe)
{
	int err = 0;
	struct ia_css_pipe *copy_pipe, *capture_pipe;
	enum sh_css_pipe_config_override copy_ovrd;
	enum ia_css_input_mode video_pipe_input_mode;
	unsigned int thread_id;

	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);
	if ((!pipe) || (pipe->mode != IA_CSS_PIPE_ID_VIDEO)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	video_pipe_input_mode = pipe->stream->config.mode;

	copy_pipe    = pipe->pipe_settings.video.copy_pipe;
	capture_pipe = pipe->pipe_settings.video.capture_pipe;

	sh_css_metrics_start_frame();

	/* multi stream video needs mipi buffers */

	err = send_mipi_frames(pipe);
	if (err)
		return err;

	send_raw_frames(pipe);

	ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &thread_id);
	copy_ovrd = 1 << thread_id;

	if (pipe->stream->cont_capt) {
		ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(capture_pipe),
						    &thread_id);
		copy_ovrd |= 1 << thread_id;
	}

	/* Construct and load the copy pipe */
	if (pipe->stream->config.continuous) {
		sh_css_sp_init_pipeline(&copy_pipe->pipeline,
					IA_CSS_PIPE_ID_COPY,
					(uint8_t)ia_css_pipe_get_pipe_num(copy_pipe),
					false,
					pipe->stream->config.pixels_per_clock == 2, false,
					false, pipe->required_bds_factor,
					copy_ovrd,
					pipe->stream->config.mode,
					&pipe->stream->config.metadata_config,
					&pipe->stream->info.metadata_info,
					pipe->stream->config.source.port.port);

		/*
		 * make the video pipe start with mem mode input, copy handles
		 * the actual mode
		 */
		video_pipe_input_mode = IA_CSS_INPUT_MODE_MEMORY;
	}

	/* Construct and load the capture pipe */
	if (pipe->stream->cont_capt) {
		sh_css_sp_init_pipeline(&capture_pipe->pipeline,
					IA_CSS_PIPE_ID_CAPTURE,
					(uint8_t)ia_css_pipe_get_pipe_num(capture_pipe),
					capture_pipe->config.default_capture_config.enable_xnr != 0,
					capture_pipe->stream->config.pixels_per_clock == 2,
					true, /* continuous */
					false, /* offline */
					capture_pipe->required_bds_factor,
					0,
					IA_CSS_INPUT_MODE_MEMORY,
					&pipe->stream->config.metadata_config,
					&pipe->stream->info.metadata_info,
					(enum mipi_port_id)0);
	}

	start_pipe(pipe, copy_ovrd, video_pipe_input_mode);

	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static
int sh_css_pipe_get_viewfinder_frame_info(
    struct ia_css_pipe *pipe,
    struct ia_css_frame_info *info,
    unsigned int idx)
{
	assert(pipe);
	assert(info);

	/* We could print the pointer as input arg, and the values as output */
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_pipe_get_viewfinder_frame_info() enter: void\n");

	if (pipe->mode == IA_CSS_PIPE_ID_CAPTURE &&
	    (pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_RAW ||
	     pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_BAYER))
		return -EINVAL;
	/* offline video does not generate viewfinder output */
	*info = pipe->vf_output_info[idx];

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_pipe_get_viewfinder_frame_info() leave: \
		info.res.width=%d, info.res.height=%d, \
		info.padded_width=%d, info.format=%d, \
		info.raw_bit_depth=%d, info.raw_bayer_order=%d\n",
			    info->res.width, info->res.height,
			    info->padded_width, info->format,
			    info->raw_bit_depth, info->raw_bayer_order);

	return 0;
}

static int
sh_css_pipe_configure_viewfinder(struct ia_css_pipe *pipe, unsigned int width,
				 unsigned int height, unsigned int min_width,
				 enum ia_css_frame_format format,
				 unsigned int idx)
{
	int err = 0;

	IA_CSS_ENTER_PRIVATE("pipe = %p, width = %d, height = %d, min_width = %d, format = %d, idx = %d\n",
			     pipe, width, height, min_width, format, idx);

	if (!pipe) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	err = ia_css_util_check_res(width, height);
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}
	if (pipe->vf_output_info[idx].res.width != width ||
	    pipe->vf_output_info[idx].res.height != height ||
	    pipe->vf_output_info[idx].format != format)
		ia_css_frame_info_init(&pipe->vf_output_info[idx], width, height,
				       format, min_width);

	IA_CSS_LEAVE_ERR_PRIVATE(0);
	return 0;
}

static int load_copy_binaries(struct ia_css_pipe *pipe)
{
	int err = 0;

	assert(pipe);
	IA_CSS_ENTER_PRIVATE("");

	assert(pipe->mode == IA_CSS_PIPE_ID_CAPTURE ||
	       pipe->mode == IA_CSS_PIPE_ID_COPY);
	if (pipe->pipe_settings.capture.copy_binary.info)
		return 0;

	err = ia_css_frame_check_info(&pipe->output_info[0]);
	if (err)
		goto ERR;

	err = verify_copy_out_frame_format(pipe);
	if (err)
		goto ERR;

	err = load_copy_binary(pipe,
			       &pipe->pipe_settings.capture.copy_binary,
			       NULL);

ERR:
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static bool need_capture_pp(
    const struct ia_css_pipe *pipe)
{
	const struct ia_css_frame_info *out_info = &pipe->output_info[0];

	IA_CSS_ENTER_LEAVE_PRIVATE("");
	assert(pipe);
	assert(pipe->mode == IA_CSS_PIPE_ID_CAPTURE);

	/* determine whether we need to use the capture_pp binary.
	 * This is needed for:
	 *   1. XNR or
	 *   2. Digital Zoom or
	 *   3. YUV downscaling
	 */
	if (pipe->out_yuv_ds_input_info.res.width &&
	    ((pipe->out_yuv_ds_input_info.res.width != out_info->res.width) ||
	     (pipe->out_yuv_ds_input_info.res.height != out_info->res.height)))
		return true;

	if (pipe->config.default_capture_config.enable_xnr != 0)
		return true;

	if ((pipe->stream->isp_params_configs->dz_config.dx < HRT_GDC_N) ||
	    (pipe->stream->isp_params_configs->dz_config.dy < HRT_GDC_N) ||
	    pipe->config.enable_dz)
		return true;

	return false;
}

static bool need_capt_ldc(
    const struct ia_css_pipe *pipe)
{
	IA_CSS_ENTER_LEAVE_PRIVATE("");
	assert(pipe);
	assert(pipe->mode == IA_CSS_PIPE_ID_CAPTURE);
	return (pipe->extra_config.enable_dvs_6axis) ? true : false;
}

static int set_num_primary_stages(unsigned int *num,
				  enum ia_css_pipe_version version)
{
	int err = 0;

	if (!num)
		return -EINVAL;

	switch (version) {
	case IA_CSS_PIPE_VERSION_2_6_1:
		*num = NUM_PRIMARY_HQ_STAGES;
		break;
	case IA_CSS_PIPE_VERSION_2_2:
	case IA_CSS_PIPE_VERSION_1:
		*num = NUM_PRIMARY_STAGES;
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int load_primary_binaries(
    struct ia_css_pipe *pipe)
{
	bool online = false;
	bool need_pp = false;
	bool need_isp_copy_binary = false;
	bool need_ldc = false;
#ifdef ISP2401
	bool sensor = false;
#else
	bool memory, continuous;
#endif
	struct ia_css_frame_info prim_in_info,
		       prim_out_info,
		       capt_pp_out_info, vf_info,
		       *vf_pp_in_info, *pipe_out_info,
		       *pipe_vf_out_info, *capt_pp_in_info,
		       capt_ldc_out_info;
	int err = 0;
	struct ia_css_capture_settings *mycs;
	unsigned int i;
	bool need_extra_yuv_scaler = false;
	struct ia_css_binary_descr prim_descr[MAX_NUM_PRIMARY_STAGES];

	IA_CSS_ENTER_PRIVATE("");
	assert(pipe);
	assert(pipe->stream);
	assert(pipe->mode == IA_CSS_PIPE_ID_CAPTURE ||
	       pipe->mode == IA_CSS_PIPE_ID_COPY);

	online = pipe->stream->config.online;
#ifdef ISP2401
	sensor = (pipe->stream->config.mode == IA_CSS_INPUT_MODE_SENSOR);
#else
	memory = pipe->stream->config.mode == IA_CSS_INPUT_MODE_MEMORY;
	continuous = pipe->stream->config.continuous;
#endif

	mycs = &pipe->pipe_settings.capture;
	pipe_out_info = &pipe->output_info[0];
	pipe_vf_out_info = &pipe->vf_output_info[0];

	if (mycs->primary_binary[0].info)
		return 0;

	err = set_num_primary_stages(&mycs->num_primary_stage,
				     pipe->config.isp_pipe_version);
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}

	if (pipe->enable_viewfinder[IA_CSS_PIPE_OUTPUT_STAGE_0]) {
		err = ia_css_util_check_vf_out_info(pipe_out_info, pipe_vf_out_info);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	} else {
		err = ia_css_frame_check_info(pipe_out_info);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	}
	need_pp = need_capture_pp(pipe);

	/*
	 * we use the vf output info to get the primary/capture_pp binary
	 * configured for vf_veceven. It will select the closest downscaling
	 * factor.
	 */
	vf_info = *pipe_vf_out_info;

	/*
	 * WARNING: The #if def flag has been added below as a
	 * temporary solution to solve the problem of enabling the
	 * view finder in a single binary in a capture flow. The
	 * vf-pp stage has been removed for Skycam in the solution
	 * provided. The vf-pp stage should be re-introduced when
	 * required. This should not be considered as a clean solution.
	 * Proper investigation should be done to come up with the clean
	 * solution.
	 */
	ia_css_frame_info_set_format(&vf_info, IA_CSS_FRAME_FORMAT_YUV_LINE);

	/*
	 * TODO: All this yuv_scaler and capturepp calculation logic
	 * can be shared later. Capture_pp is also a yuv_scale binary
	 * with extra XNR funcionality. Therefore, it can be made as the
	 * first step of the cascade.
	 */
	capt_pp_out_info = pipe->out_yuv_ds_input_info;
	capt_pp_out_info.format = IA_CSS_FRAME_FORMAT_YUV420;
	capt_pp_out_info.res.width  /= MAX_PREFERRED_YUV_DS_PER_STEP;
	capt_pp_out_info.res.height /= MAX_PREFERRED_YUV_DS_PER_STEP;
	ia_css_frame_info_set_width(&capt_pp_out_info, capt_pp_out_info.res.width, 0);

	need_extra_yuv_scaler = need_downscaling(capt_pp_out_info.res,
						 pipe_out_info->res);

	if (need_extra_yuv_scaler) {
		struct ia_css_cas_binary_descr cas_scaler_descr = { };

		err = ia_css_pipe_create_cas_scaler_desc_single_output(
			  &capt_pp_out_info,
			  pipe_out_info,
			  NULL,
			  &cas_scaler_descr);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
		mycs->num_yuv_scaler = cas_scaler_descr.num_stage;
		mycs->yuv_scaler_binary = kcalloc(cas_scaler_descr.num_stage,
						  sizeof(struct ia_css_binary),
						  GFP_KERNEL);
		if (!mycs->yuv_scaler_binary) {
			err = -ENOMEM;
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
		mycs->is_output_stage = kcalloc(cas_scaler_descr.num_stage,
						sizeof(bool), GFP_KERNEL);
		if (!mycs->is_output_stage) {
			err = -ENOMEM;
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
		for (i = 0; i < cas_scaler_descr.num_stage; i++) {
			struct ia_css_binary_descr yuv_scaler_descr;

			mycs->is_output_stage[i] = cas_scaler_descr.is_output_stage[i];
			ia_css_pipe_get_yuvscaler_binarydesc(pipe,
							     &yuv_scaler_descr, &cas_scaler_descr.in_info[i],
							     &cas_scaler_descr.out_info[i],
							     &cas_scaler_descr.internal_out_info[i],
							     &cas_scaler_descr.vf_info[i]);
			err = ia_css_binary_find(&yuv_scaler_descr,
						 &mycs->yuv_scaler_binary[i]);
			if (err) {
				IA_CSS_LEAVE_ERR_PRIVATE(err);
				return err;
			}
		}
		ia_css_pipe_destroy_cas_scaler_desc(&cas_scaler_descr);

	} else {
		capt_pp_out_info = pipe->output_info[0];
	}

	/* TODO Do we disable ldc for skycam */
	need_ldc = need_capt_ldc(pipe);

	/* we build up the pipeline starting at the end */
	/* Capture post-processing */
	if (need_pp) {
		struct ia_css_binary_descr capture_pp_descr;

		capt_pp_in_info = need_ldc ? &capt_ldc_out_info : &prim_out_info;

		ia_css_pipe_get_capturepp_binarydesc(pipe,
						     &capture_pp_descr,
						     capt_pp_in_info,
						     &capt_pp_out_info,
						     &vf_info);

		err = ia_css_binary_find(&capture_pp_descr,
					 &mycs->capture_pp_binary);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}

		if (need_ldc) {
			struct ia_css_binary_descr capt_ldc_descr;

			ia_css_pipe_get_ldc_binarydesc(pipe,
						       &capt_ldc_descr,
						       &prim_out_info,
						       &capt_ldc_out_info);

			err = ia_css_binary_find(&capt_ldc_descr,
						 &mycs->capture_ldc_binary);
			if (err) {
				IA_CSS_LEAVE_ERR_PRIVATE(err);
				return err;
			}
		}
	} else {
		prim_out_info = *pipe_out_info;
	}

	/* Primary */
	for (i = 0; i < mycs->num_primary_stage; i++) {
		struct ia_css_frame_info *local_vf_info = NULL;

		if (pipe->enable_viewfinder[IA_CSS_PIPE_OUTPUT_STAGE_0] &&
		    (i == mycs->num_primary_stage - 1))
			local_vf_info = &vf_info;
		ia_css_pipe_get_primary_binarydesc(pipe, &prim_descr[i],
						   &prim_in_info, &prim_out_info,
						   local_vf_info, i);
		err = ia_css_binary_find(&prim_descr[i], &mycs->primary_binary[i]);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	}

	/* Viewfinder post-processing */
	if (need_pp)
		vf_pp_in_info = &mycs->capture_pp_binary.vf_frame_info;
	else
		vf_pp_in_info = &mycs->primary_binary[mycs->num_primary_stage - 1].vf_frame_info;

	/*
	 * WARNING: The #if def flag has been added below as a
	 * temporary solution to solve the problem of enabling the
	 * view finder in a single binary in a capture flow. The
	 * vf-pp stage has been removed for Skycam in the solution
	 * provided. The vf-pp stage should be re-introduced when
	 * required. Thisshould not be considered as a clean solution.
	 * Proper  * investigation should be done to come up with the clean
	 * solution.
	 */
	if (pipe->enable_viewfinder[IA_CSS_PIPE_OUTPUT_STAGE_0]) {
		struct ia_css_binary_descr vf_pp_descr;

		ia_css_pipe_get_vfpp_binarydesc(pipe,
						&vf_pp_descr, vf_pp_in_info, pipe_vf_out_info);
		err = ia_css_binary_find(&vf_pp_descr, &mycs->vf_pp_binary);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	}
	err = allocate_delay_frames(pipe);

	if (err)
		return err;

#ifdef ISP2401
	/*
	 * When the input system is 2401, only the Direct Sensor Mode
	 * Offline Capture uses the ISP copy binary.
	 */
	need_isp_copy_binary = !online && sensor;
#else
	need_isp_copy_binary = !online && !continuous && !memory;
#endif

	/* ISP Copy */
	if (need_isp_copy_binary) {
		err = load_copy_binary(pipe,
				       &mycs->copy_binary,
				       &mycs->primary_binary[0]);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	}

	return 0;
}

static int
allocate_delay_frames(struct ia_css_pipe *pipe)
{
	unsigned int num_delay_frames = 0, i = 0;
	unsigned int dvs_frame_delay = 0;
	struct ia_css_frame_info ref_info;
	int err = 0;
	enum ia_css_pipe_id mode = IA_CSS_PIPE_ID_VIDEO;
	struct ia_css_frame **delay_frames = NULL;

	IA_CSS_ENTER_PRIVATE("");

	if (!pipe) {
		IA_CSS_ERROR("Invalid args - pipe %p", pipe);
		return -EINVAL;
	}

	mode = pipe->mode;
	dvs_frame_delay = pipe->dvs_frame_delay;

	if (dvs_frame_delay > 0)
		num_delay_frames = dvs_frame_delay + 1;

	switch (mode) {
	case IA_CSS_PIPE_ID_CAPTURE: {
		struct ia_css_capture_settings *mycs_capture = &pipe->pipe_settings.capture;
		(void)mycs_capture;
		return err;
	}
	break;
	case IA_CSS_PIPE_ID_VIDEO: {
		struct ia_css_video_settings *mycs_video = &pipe->pipe_settings.video;

		ref_info = mycs_video->video_binary.internal_frame_info;

		/*
		 * The ref frame expects
		 * 1. Y plane
		 * 2. UV plane with line interleaving, like below
		 *	UUUUUU(width/2 times) VVVVVVVV..(width/2 times)
		 *
		 * This format is not YUV420(which has Y, U and V planes).
		 * Its closer to NV12, except that the UV plane has UV
		 * interleaving, like UVUVUVUVUVUVUVUVU...
		 *
		 * TODO: make this ref_frame format as a separate frame format
		 */
		ref_info.format        = IA_CSS_FRAME_FORMAT_NV12;
		delay_frames = mycs_video->delay_frames;
	}
	break;
	case IA_CSS_PIPE_ID_PREVIEW: {
		struct ia_css_preview_settings *mycs_preview = &pipe->pipe_settings.preview;

		ref_info = mycs_preview->preview_binary.internal_frame_info;

		/*
		 * The ref frame expects
		 * 1. Y plane
		 * 2. UV plane with line interleaving, like below
		 *	UUUUUU(width/2 times) VVVVVVVV..(width/2 times)
		 *
		 * This format is not YUV420(which has Y, U and V planes).
		 * Its closer to NV12, except that the UV plane has UV
		 * interleaving, like UVUVUVUVUVUVUVUVU...
		 *
		 * TODO: make this ref_frame format as a separate frame format
		 */
		ref_info.format        = IA_CSS_FRAME_FORMAT_NV12;
		delay_frames = mycs_preview->delay_frames;
	}
	break;
	default:
		return -EINVAL;
	}

	ref_info.raw_bit_depth = SH_CSS_REF_BIT_DEPTH;

	assert(num_delay_frames <= MAX_NUM_VIDEO_DELAY_FRAMES);
	for (i = 0; i < num_delay_frames; i++) {
		err = ia_css_frame_allocate_from_info(&delay_frames[i],	&ref_info);
		if (err)
			return err;
	}
	IA_CSS_LEAVE_PRIVATE("");
	return 0;
}

static int load_advanced_binaries(struct ia_css_pipe *pipe)
{
	struct ia_css_frame_info pre_in_info, gdc_in_info,
			post_in_info, post_out_info,
			vf_info, *vf_pp_in_info, *pipe_out_info,
			*pipe_vf_out_info;
	bool need_pp;
	bool need_isp_copy = true;
	int err = 0;

	IA_CSS_ENTER_PRIVATE("");

	assert(pipe);
	assert(pipe->mode == IA_CSS_PIPE_ID_CAPTURE ||
	       pipe->mode == IA_CSS_PIPE_ID_COPY);
	if (pipe->pipe_settings.capture.pre_isp_binary.info)
		return 0;
	pipe_out_info = &pipe->output_info[0];
	pipe_vf_out_info = &pipe->vf_output_info[0];

	vf_info = *pipe_vf_out_info;
	err = ia_css_util_check_vf_out_info(pipe_out_info, &vf_info);
	if (err)
		return err;
	need_pp = need_capture_pp(pipe);

	ia_css_frame_info_set_format(&vf_info,
				     IA_CSS_FRAME_FORMAT_YUV_LINE);

	/* we build up the pipeline starting at the end */
	/* Capture post-processing */
	if (need_pp) {
		struct ia_css_binary_descr capture_pp_descr;

		ia_css_pipe_get_capturepp_binarydesc(pipe, &capture_pp_descr,
						     &post_out_info,
						     pipe_out_info, &vf_info);
		err = ia_css_binary_find(&capture_pp_descr,
					 &pipe->pipe_settings.capture.capture_pp_binary);
		if (err)
			return err;
	} else {
		post_out_info = *pipe_out_info;
	}

	/* Post-gdc */
	{
		struct ia_css_binary_descr post_gdc_descr;

		ia_css_pipe_get_post_gdc_binarydesc(pipe, &post_gdc_descr,
						    &post_in_info,
						    &post_out_info, &vf_info);
		err = ia_css_binary_find(&post_gdc_descr,
					 &pipe->pipe_settings.capture.post_isp_binary);
		if (err)
			return err;
	}

	/* Gdc */
	{
		struct ia_css_binary_descr gdc_descr;

		ia_css_pipe_get_gdc_binarydesc(pipe, &gdc_descr, &gdc_in_info,
					       &pipe->pipe_settings.capture.post_isp_binary.in_frame_info);
		err = ia_css_binary_find(&gdc_descr,
					 &pipe->pipe_settings.capture.anr_gdc_binary);
		if (err)
			return err;
	}
	pipe->pipe_settings.capture.anr_gdc_binary.left_padding =
	    pipe->pipe_settings.capture.post_isp_binary.left_padding;

	/* Pre-gdc */
	{
		struct ia_css_binary_descr pre_gdc_descr;

		ia_css_pipe_get_pre_gdc_binarydesc(pipe, &pre_gdc_descr, &pre_in_info,
						   &pipe->pipe_settings.capture.anr_gdc_binary.in_frame_info);
		err = ia_css_binary_find(&pre_gdc_descr,
					 &pipe->pipe_settings.capture.pre_isp_binary);
		if (err)
			return err;
	}
	pipe->pipe_settings.capture.pre_isp_binary.left_padding =
	    pipe->pipe_settings.capture.anr_gdc_binary.left_padding;

	/* Viewfinder post-processing */
	if (need_pp) {
		vf_pp_in_info =
		    &pipe->pipe_settings.capture.capture_pp_binary.vf_frame_info;
	} else {
		vf_pp_in_info =
		    &pipe->pipe_settings.capture.post_isp_binary.vf_frame_info;
	}

	{
		struct ia_css_binary_descr vf_pp_descr;

		ia_css_pipe_get_vfpp_binarydesc(pipe,
						&vf_pp_descr, vf_pp_in_info, pipe_vf_out_info);
		err = ia_css_binary_find(&vf_pp_descr,
					 &pipe->pipe_settings.capture.vf_pp_binary);
		if (err)
			return err;
	}

	/* Copy */
#ifdef ISP2401
	/* For CSI2+, only the direct sensor mode/online requires ISP copy */
	need_isp_copy = pipe->stream->config.mode == IA_CSS_INPUT_MODE_SENSOR;
#endif
	if (need_isp_copy)
		load_copy_binary(pipe,
				 &pipe->pipe_settings.capture.copy_binary,
				 &pipe->pipe_settings.capture.pre_isp_binary);

	return err;
}

static int load_bayer_isp_binaries(struct ia_css_pipe *pipe)
{
	struct ia_css_frame_info pre_isp_in_info, *pipe_out_info;
	int err = 0;
	struct ia_css_binary_descr pre_de_descr;

	IA_CSS_ENTER_PRIVATE("");
	assert(pipe);
	assert(pipe->mode == IA_CSS_PIPE_ID_CAPTURE ||
	       pipe->mode == IA_CSS_PIPE_ID_COPY);
	pipe_out_info = &pipe->output_info[0];

	if (pipe->pipe_settings.capture.pre_isp_binary.info)
		return 0;

	err = ia_css_frame_check_info(pipe_out_info);
	if (err)
		return err;

	ia_css_pipe_get_pre_de_binarydesc(pipe, &pre_de_descr,
					  &pre_isp_in_info,
					  pipe_out_info);

	err = ia_css_binary_find(&pre_de_descr,
				 &pipe->pipe_settings.capture.pre_isp_binary);

	return err;
}

static int load_low_light_binaries(struct ia_css_pipe *pipe)
{
	struct ia_css_frame_info pre_in_info, anr_in_info,
			post_in_info, post_out_info,
			vf_info, *pipe_vf_out_info, *pipe_out_info,
			*vf_pp_in_info;
	bool need_pp;
	bool need_isp_copy = true;
	int err = 0;

	IA_CSS_ENTER_PRIVATE("");
	assert(pipe);
	assert(pipe->mode == IA_CSS_PIPE_ID_CAPTURE ||
	       pipe->mode == IA_CSS_PIPE_ID_COPY);

	if (pipe->pipe_settings.capture.pre_isp_binary.info)
		return 0;
	pipe_vf_out_info = &pipe->vf_output_info[0];
	pipe_out_info = &pipe->output_info[0];

	vf_info = *pipe_vf_out_info;
	err = ia_css_util_check_vf_out_info(pipe_out_info,
					    &vf_info);
	if (err)
		return err;
	need_pp = need_capture_pp(pipe);

	ia_css_frame_info_set_format(&vf_info,
				     IA_CSS_FRAME_FORMAT_YUV_LINE);

	/* we build up the pipeline starting at the end */
	/* Capture post-processing */
	if (need_pp) {
		struct ia_css_binary_descr capture_pp_descr;

		ia_css_pipe_get_capturepp_binarydesc(pipe, &capture_pp_descr,
						     &post_out_info,
						     pipe_out_info, &vf_info);
		err = ia_css_binary_find(&capture_pp_descr,
					 &pipe->pipe_settings.capture.capture_pp_binary);
		if (err)
			return err;
	} else {
		post_out_info = *pipe_out_info;
	}

	/* Post-anr */
	{
		struct ia_css_binary_descr post_anr_descr;

		ia_css_pipe_get_post_anr_binarydesc(pipe,
						    &post_anr_descr, &post_in_info, &post_out_info, &vf_info);
		err = ia_css_binary_find(&post_anr_descr,
					 &pipe->pipe_settings.capture.post_isp_binary);
		if (err)
			return err;
	}

	/* Anr */
	{
		struct ia_css_binary_descr anr_descr;

		ia_css_pipe_get_anr_binarydesc(pipe, &anr_descr, &anr_in_info,
					       &pipe->pipe_settings.capture.post_isp_binary.in_frame_info);
		err = ia_css_binary_find(&anr_descr,
					 &pipe->pipe_settings.capture.anr_gdc_binary);
		if (err)
			return err;
	}
	pipe->pipe_settings.capture.anr_gdc_binary.left_padding =
	    pipe->pipe_settings.capture.post_isp_binary.left_padding;

	/* Pre-anr */
	{
		struct ia_css_binary_descr pre_anr_descr;

		ia_css_pipe_get_pre_anr_binarydesc(pipe, &pre_anr_descr, &pre_in_info,
						   &pipe->pipe_settings.capture.anr_gdc_binary.in_frame_info);
		err = ia_css_binary_find(&pre_anr_descr,
					 &pipe->pipe_settings.capture.pre_isp_binary);
		if (err)
			return err;
	}
	pipe->pipe_settings.capture.pre_isp_binary.left_padding =
	    pipe->pipe_settings.capture.anr_gdc_binary.left_padding;

	/* Viewfinder post-processing */
	if (need_pp) {
		vf_pp_in_info =
		    &pipe->pipe_settings.capture.capture_pp_binary.vf_frame_info;
	} else {
		vf_pp_in_info =
		    &pipe->pipe_settings.capture.post_isp_binary.vf_frame_info;
	}

	{
		struct ia_css_binary_descr vf_pp_descr;

		ia_css_pipe_get_vfpp_binarydesc(pipe, &vf_pp_descr,
						vf_pp_in_info, pipe_vf_out_info);
		err = ia_css_binary_find(&vf_pp_descr,
					 &pipe->pipe_settings.capture.vf_pp_binary);
		if (err)
			return err;
	}

	/* Copy */
#ifdef ISP2401
	/* For CSI2+, only the direct sensor mode/online requires ISP copy */
	need_isp_copy = pipe->stream->config.mode == IA_CSS_INPUT_MODE_SENSOR;
#endif
	if (need_isp_copy)
		err = load_copy_binary(pipe,
				       &pipe->pipe_settings.capture.copy_binary,
				       &pipe->pipe_settings.capture.pre_isp_binary);

	return err;
}

static bool copy_on_sp(struct ia_css_pipe *pipe)
{
	bool rval;

	assert(pipe);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE, "copy_on_sp() enter:\n");

	rval = true;

	rval &=	(pipe->mode == IA_CSS_PIPE_ID_CAPTURE);

	rval &= (pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_RAW);

	rval &= ((pipe->stream->config.input_config.format ==
		    ATOMISP_INPUT_FORMAT_BINARY_8) ||
		    (pipe->config.mode == IA_CSS_PIPE_MODE_COPY));

	return rval;
}

static int load_capture_binaries(struct ia_css_pipe *pipe)
{
	int err = 0;
	bool must_be_raw;

	IA_CSS_ENTER_PRIVATE("");
	assert(pipe);
	assert(pipe->mode == IA_CSS_PIPE_ID_CAPTURE ||
	       pipe->mode == IA_CSS_PIPE_ID_COPY);

	if (pipe->pipe_settings.capture.primary_binary[0].info) {
		IA_CSS_LEAVE_ERR_PRIVATE(0);
		return 0;
	}

	/* in primary, advanced,low light or bayer,
						the input format must be raw */
	must_be_raw =
	    pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_ADVANCED ||
	    pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_BAYER ||
	    pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_LOW_LIGHT;
	err = ia_css_util_check_input(&pipe->stream->config, must_be_raw, false);
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}
	if (copy_on_sp(pipe) &&
	    pipe->stream->config.input_config.format == ATOMISP_INPUT_FORMAT_BINARY_8) {
		ia_css_frame_info_init(
		    &pipe->output_info[0],
		    JPEG_BYTES,
		    1,
		    IA_CSS_FRAME_FORMAT_BINARY_8,
		    0);
		IA_CSS_LEAVE_ERR_PRIVATE(0);
		return 0;
	}

	switch (pipe->config.default_capture_config.mode) {
	case IA_CSS_CAPTURE_MODE_RAW:
		err = load_copy_binaries(pipe);
#if defined(ISP2401)
		if (!err)
			pipe->pipe_settings.capture.copy_binary.online = pipe->stream->config.online;
#endif
		break;
	case IA_CSS_CAPTURE_MODE_BAYER:
		err = load_bayer_isp_binaries(pipe);
		break;
	case IA_CSS_CAPTURE_MODE_PRIMARY:
		err = load_primary_binaries(pipe);
		break;
	case IA_CSS_CAPTURE_MODE_ADVANCED:
		err = load_advanced_binaries(pipe);
		break;
	case IA_CSS_CAPTURE_MODE_LOW_LIGHT:
		err = load_low_light_binaries(pipe);
		break;
	}
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}

	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static int
unload_capture_binaries(struct ia_css_pipe *pipe)
{
	unsigned int i;

	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);

	if (!pipe || (pipe->mode != IA_CSS_PIPE_ID_CAPTURE &&
		      pipe->mode != IA_CSS_PIPE_ID_COPY)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}
	ia_css_binary_unload(&pipe->pipe_settings.capture.copy_binary);
	for (i = 0; i < MAX_NUM_PRIMARY_STAGES; i++)
		ia_css_binary_unload(&pipe->pipe_settings.capture.primary_binary[i]);
	ia_css_binary_unload(&pipe->pipe_settings.capture.pre_isp_binary);
	ia_css_binary_unload(&pipe->pipe_settings.capture.anr_gdc_binary);
	ia_css_binary_unload(&pipe->pipe_settings.capture.post_isp_binary);
	ia_css_binary_unload(&pipe->pipe_settings.capture.capture_pp_binary);
	ia_css_binary_unload(&pipe->pipe_settings.capture.capture_ldc_binary);
	ia_css_binary_unload(&pipe->pipe_settings.capture.vf_pp_binary);

	for (i = 0; i < pipe->pipe_settings.capture.num_yuv_scaler; i++)
		ia_css_binary_unload(&pipe->pipe_settings.capture.yuv_scaler_binary[i]);

	kfree(pipe->pipe_settings.capture.is_output_stage);
	pipe->pipe_settings.capture.is_output_stage = NULL;
	kfree(pipe->pipe_settings.capture.yuv_scaler_binary);
	pipe->pipe_settings.capture.yuv_scaler_binary = NULL;

	IA_CSS_LEAVE_ERR_PRIVATE(0);
	return 0;
}

static bool
need_downscaling(const struct ia_css_resolution in_res,
		 const struct ia_css_resolution out_res)
{
	if (in_res.width > out_res.width || in_res.height > out_res.height)
		return true;

	return false;
}

static bool
need_yuv_scaler_stage(const struct ia_css_pipe *pipe)
{
	unsigned int i;
	struct ia_css_resolution in_res, out_res;

	bool need_format_conversion = false;

	IA_CSS_ENTER_PRIVATE("");
	assert(pipe);
	assert(pipe->mode == IA_CSS_PIPE_ID_YUVPP);

	/* TODO: make generic function */
	need_format_conversion =
	    ((pipe->stream->config.input_config.format ==
		ATOMISP_INPUT_FORMAT_YUV420_8_LEGACY) &&
		(pipe->output_info[0].format != IA_CSS_FRAME_FORMAT_CSI_MIPI_LEGACY_YUV420_8));

	in_res = pipe->config.input_effective_res;

	if (pipe->config.enable_dz)
		return true;

	if ((pipe->output_info[0].res.width != 0) && need_format_conversion)
		return true;

	for (i = 0; i < IA_CSS_PIPE_MAX_OUTPUT_STAGE; i++) {
		out_res = pipe->output_info[i].res;

		/* A non-zero width means it is a valid output port */
		if ((out_res.width != 0) && need_downscaling(in_res, out_res))
			return true;
	}

	return false;
}

/*
 * TODO: it is temporarily created from ia_css_pipe_create_cas_scaler_desc
 * which has some hard-coded knowledge which prevents reuse of the function.
 * Later, merge this with ia_css_pipe_create_cas_scaler_desc
 */
static int ia_css_pipe_create_cas_scaler_desc_single_output(
	    struct ia_css_frame_info *cas_scaler_in_info,
	    struct ia_css_frame_info *cas_scaler_out_info,
	    struct ia_css_frame_info *cas_scaler_vf_info,
	    struct ia_css_cas_binary_descr *descr)
{
	unsigned int i;
	unsigned int hor_ds_factor = 0, ver_ds_factor = 0;
	int err = 0;
	struct ia_css_frame_info tmp_in_info;

	unsigned int max_scale_factor_per_stage = MAX_PREFERRED_YUV_DS_PER_STEP;

	assert(cas_scaler_in_info);
	assert(cas_scaler_out_info);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "ia_css_pipe_create_cas_scaler_desc() enter:\n");

	/* We assume that this function is used only for single output port case. */
	descr->num_output_stage = 1;

	hor_ds_factor = CEIL_DIV(cas_scaler_in_info->res.width,
				 cas_scaler_out_info->res.width);
	ver_ds_factor = CEIL_DIV(cas_scaler_in_info->res.height,
				 cas_scaler_out_info->res.height);
	/* use the same horizontal and vertical downscaling factor for simplicity */
	assert(hor_ds_factor == ver_ds_factor);

	i = 1;
	while (i < hor_ds_factor) {
		descr->num_stage++;
		i *= max_scale_factor_per_stage;
	}

	descr->in_info = kmalloc(descr->num_stage *
				 sizeof(struct ia_css_frame_info),
				 GFP_KERNEL);
	if (!descr->in_info) {
		err = -ENOMEM;
		goto ERR;
	}
	descr->internal_out_info = kmalloc(descr->num_stage *
					   sizeof(struct ia_css_frame_info),
					   GFP_KERNEL);
	if (!descr->internal_out_info) {
		err = -ENOMEM;
		goto ERR;
	}
	descr->out_info = kmalloc(descr->num_stage *
				  sizeof(struct ia_css_frame_info),
				  GFP_KERNEL);
	if (!descr->out_info) {
		err = -ENOMEM;
		goto ERR;
	}
	descr->vf_info = kmalloc(descr->num_stage *
				 sizeof(struct ia_css_frame_info),
				 GFP_KERNEL);
	if (!descr->vf_info) {
		err = -ENOMEM;
		goto ERR;
	}
	descr->is_output_stage = kmalloc(descr->num_stage * sizeof(bool),
					 GFP_KERNEL);
	if (!descr->is_output_stage) {
		err = -ENOMEM;
		goto ERR;
	}

	tmp_in_info = *cas_scaler_in_info;
	for (i = 0; i < descr->num_stage; i++) {
		descr->in_info[i] = tmp_in_info;
		if ((tmp_in_info.res.width / max_scale_factor_per_stage) <=
		    cas_scaler_out_info->res.width) {
			descr->is_output_stage[i] = true;
			if ((descr->num_output_stage > 1) && (i != (descr->num_stage - 1))) {
				descr->internal_out_info[i].res.width = cas_scaler_out_info->res.width;
				descr->internal_out_info[i].res.height = cas_scaler_out_info->res.height;
				descr->internal_out_info[i].padded_width = cas_scaler_out_info->padded_width;
				descr->internal_out_info[i].format = IA_CSS_FRAME_FORMAT_YUV420;
			} else {
				assert(i == (descr->num_stage - 1));
				descr->internal_out_info[i].res.width = 0;
				descr->internal_out_info[i].res.height = 0;
			}
			descr->out_info[i].res.width = cas_scaler_out_info->res.width;
			descr->out_info[i].res.height = cas_scaler_out_info->res.height;
			descr->out_info[i].padded_width = cas_scaler_out_info->padded_width;
			descr->out_info[i].format = cas_scaler_out_info->format;
			if (cas_scaler_vf_info) {
				descr->vf_info[i].res.width = cas_scaler_vf_info->res.width;
				descr->vf_info[i].res.height = cas_scaler_vf_info->res.height;
				descr->vf_info[i].padded_width = cas_scaler_vf_info->padded_width;
				ia_css_frame_info_set_format(&descr->vf_info[i], IA_CSS_FRAME_FORMAT_YUV_LINE);
			} else {
				descr->vf_info[i].res.width = 0;
				descr->vf_info[i].res.height = 0;
				descr->vf_info[i].padded_width = 0;
			}
		} else {
			descr->is_output_stage[i] = false;
			descr->internal_out_info[i].res.width = tmp_in_info.res.width /
								max_scale_factor_per_stage;
			descr->internal_out_info[i].res.height = tmp_in_info.res.height /
				max_scale_factor_per_stage;
			descr->internal_out_info[i].format = IA_CSS_FRAME_FORMAT_YUV420;
			ia_css_frame_info_init(&descr->internal_out_info[i],
					       tmp_in_info.res.width / max_scale_factor_per_stage,
					       tmp_in_info.res.height / max_scale_factor_per_stage,
					       IA_CSS_FRAME_FORMAT_YUV420, 0);
			descr->out_info[i].res.width = 0;
			descr->out_info[i].res.height = 0;
			descr->vf_info[i].res.width = 0;
			descr->vf_info[i].res.height = 0;
		}
		tmp_in_info = descr->internal_out_info[i];
	}
ERR:
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "ia_css_pipe_create_cas_scaler_desc() leave, err=%d\n",
			    err);
	return err;
}

/* FIXME: merge most of this and single output version */
static int
ia_css_pipe_create_cas_scaler_desc(struct ia_css_pipe *pipe,
				   struct ia_css_cas_binary_descr *descr)
{
	struct ia_css_frame_info in_info = IA_CSS_BINARY_DEFAULT_FRAME_INFO;
	struct ia_css_frame_info *out_info[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	struct ia_css_frame_info *vf_out_info[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	struct ia_css_frame_info tmp_in_info = IA_CSS_BINARY_DEFAULT_FRAME_INFO;
	unsigned int i, j;
	unsigned int hor_scale_factor[IA_CSS_PIPE_MAX_OUTPUT_STAGE],
		    ver_scale_factor[IA_CSS_PIPE_MAX_OUTPUT_STAGE],
		    scale_factor = 0;
	unsigned int num_stages = 0;
	int err = 0;

	unsigned int max_scale_factor_per_stage = MAX_PREFERRED_YUV_DS_PER_STEP;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "ia_css_pipe_create_cas_scaler_desc() enter:\n");

	for (i = 0; i < IA_CSS_PIPE_MAX_OUTPUT_STAGE; i++) {
		out_info[i] = NULL;
		vf_out_info[i] = NULL;
		hor_scale_factor[i] = 0;
		ver_scale_factor[i] = 0;
	}

	in_info.res = pipe->config.input_effective_res;
	in_info.padded_width = in_info.res.width;
	descr->num_output_stage = 0;
	/* Find out how much scaling we need for each output */
	for (i = 0; i < IA_CSS_PIPE_MAX_OUTPUT_STAGE; i++) {
		if (pipe->output_info[i].res.width != 0) {
			out_info[i] = &pipe->output_info[i];
			if (pipe->vf_output_info[i].res.width != 0)
				vf_out_info[i] = &pipe->vf_output_info[i];
			descr->num_output_stage += 1;
		}

		if (out_info[i]) {
			hor_scale_factor[i] = CEIL_DIV(in_info.res.width, out_info[i]->res.width);
			ver_scale_factor[i] = CEIL_DIV(in_info.res.height, out_info[i]->res.height);
			/* use the same horizontal and vertical scaling factor for simplicity */
			assert(hor_scale_factor[i] == ver_scale_factor[i]);
			scale_factor = 1;
			do {
				num_stages++;
				scale_factor *= max_scale_factor_per_stage;
			} while (scale_factor < hor_scale_factor[i]);

			in_info.res = out_info[i]->res;
		}
	}

	if (need_yuv_scaler_stage(pipe) && (num_stages == 0))
		num_stages = 1;

	descr->num_stage = num_stages;

	descr->in_info = kmalloc_array(descr->num_stage,
				       sizeof(struct ia_css_frame_info),
				       GFP_KERNEL);
	if (!descr->in_info) {
		err = -ENOMEM;
		goto ERR;
	}
	descr->internal_out_info = kmalloc(descr->num_stage *
					   sizeof(struct ia_css_frame_info),
					   GFP_KERNEL);
	if (!descr->internal_out_info) {
		err = -ENOMEM;
		goto ERR;
	}
	descr->out_info = kmalloc(descr->num_stage *
				  sizeof(struct ia_css_frame_info),
				  GFP_KERNEL);
	if (!descr->out_info) {
		err = -ENOMEM;
		goto ERR;
	}
	descr->vf_info = kmalloc(descr->num_stage *
				 sizeof(struct ia_css_frame_info),
				 GFP_KERNEL);
	if (!descr->vf_info) {
		err = -ENOMEM;
		goto ERR;
	}
	descr->is_output_stage = kmalloc(descr->num_stage * sizeof(bool),
					 GFP_KERNEL);
	if (!descr->is_output_stage) {
		err = -ENOMEM;
		goto ERR;
	}

	for (i = 0; i < IA_CSS_PIPE_MAX_OUTPUT_STAGE; i++) {
		if (out_info[i]) {
			if (i > 0) {
				assert((out_info[i - 1]->res.width >= out_info[i]->res.width) &&
				       (out_info[i - 1]->res.height >= out_info[i]->res.height));
			}
		}
	}

	tmp_in_info.res = pipe->config.input_effective_res;
	tmp_in_info.format = IA_CSS_FRAME_FORMAT_YUV420;
	for (i = 0, j = 0; i < descr->num_stage; i++) {
		assert(j < 2);
		assert(out_info[j]);

		descr->in_info[i] = tmp_in_info;
		if ((tmp_in_info.res.width / max_scale_factor_per_stage) <=
		    out_info[j]->res.width) {
			descr->is_output_stage[i] = true;
			if ((descr->num_output_stage > 1) && (i != (descr->num_stage - 1))) {
				descr->internal_out_info[i].res.width = out_info[j]->res.width;
				descr->internal_out_info[i].res.height = out_info[j]->res.height;
				descr->internal_out_info[i].padded_width = out_info[j]->padded_width;
				descr->internal_out_info[i].format = IA_CSS_FRAME_FORMAT_YUV420;
			} else {
				assert(i == (descr->num_stage - 1));
				descr->internal_out_info[i].res.width = 0;
				descr->internal_out_info[i].res.height = 0;
			}
			descr->out_info[i].res.width = out_info[j]->res.width;
			descr->out_info[i].res.height = out_info[j]->res.height;
			descr->out_info[i].padded_width = out_info[j]->padded_width;
			descr->out_info[i].format = out_info[j]->format;
			if (vf_out_info[j]) {
				descr->vf_info[i].res.width = vf_out_info[j]->res.width;
				descr->vf_info[i].res.height = vf_out_info[j]->res.height;
				descr->vf_info[i].padded_width = vf_out_info[j]->padded_width;
				ia_css_frame_info_set_format(&descr->vf_info[i], IA_CSS_FRAME_FORMAT_YUV_LINE);
			} else {
				descr->vf_info[i].res.width = 0;
				descr->vf_info[i].res.height = 0;
				descr->vf_info[i].padded_width = 0;
			}
			j++;
		} else {
			descr->is_output_stage[i] = false;
			descr->internal_out_info[i].res.width = tmp_in_info.res.width /
								max_scale_factor_per_stage;
			descr->internal_out_info[i].res.height = tmp_in_info.res.height /
				max_scale_factor_per_stage;
			descr->internal_out_info[i].format = IA_CSS_FRAME_FORMAT_YUV420;
			ia_css_frame_info_init(&descr->internal_out_info[i],
					       tmp_in_info.res.width / max_scale_factor_per_stage,
					       tmp_in_info.res.height / max_scale_factor_per_stage,
					       IA_CSS_FRAME_FORMAT_YUV420, 0);
			descr->out_info[i].res.width = 0;
			descr->out_info[i].res.height = 0;
			descr->vf_info[i].res.width = 0;
			descr->vf_info[i].res.height = 0;
		}
		tmp_in_info = descr->internal_out_info[i];
	}
ERR:
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "ia_css_pipe_create_cas_scaler_desc() leave, err=%d\n",
			    err);
	return err;
}

static void ia_css_pipe_destroy_cas_scaler_desc(struct ia_css_cas_binary_descr
	*descr)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "ia_css_pipe_destroy_cas_scaler_desc() enter:\n");
	kfree(descr->in_info);
	descr->in_info = NULL;
	kfree(descr->internal_out_info);
	descr->internal_out_info = NULL;
	kfree(descr->out_info);
	descr->out_info = NULL;
	kfree(descr->vf_info);
	descr->vf_info = NULL;
	kfree(descr->is_output_stage);
	descr->is_output_stage = NULL;
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "ia_css_pipe_destroy_cas_scaler_desc() leave\n");
}

static int
load_yuvpp_binaries(struct ia_css_pipe *pipe)
{
	int err = 0;
	bool need_scaler = false;
	struct ia_css_frame_info *vf_pp_in_info[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	struct ia_css_yuvpp_settings *mycs;
	struct ia_css_binary *next_binary;
	struct ia_css_cas_binary_descr cas_scaler_descr = { };
	unsigned int i, j;
	bool need_isp_copy_binary = false;

	IA_CSS_ENTER_PRIVATE("");
	assert(pipe);
	assert(pipe->stream);
	assert(pipe->mode == IA_CSS_PIPE_ID_YUVPP);

	if (pipe->pipe_settings.yuvpp.copy_binary.info)
		goto ERR;

	/* Set both must_be_raw and must_be_yuv to false then yuvpp can take rgb inputs */
	err = ia_css_util_check_input(&pipe->stream->config, false, false);
	if (err)
		goto ERR;

	mycs = &pipe->pipe_settings.yuvpp;

	for (i = 0; i < IA_CSS_PIPE_MAX_OUTPUT_STAGE; i++) {
		if (pipe->vf_output_info[i].res.width != 0) {
			err = ia_css_util_check_vf_out_info(&pipe->output_info[i],
							    &pipe->vf_output_info[i]);
			if (err)
				goto ERR;
		}
		vf_pp_in_info[i] = NULL;
	}

	need_scaler = need_yuv_scaler_stage(pipe);

	/* we build up the pipeline starting at the end */
	/* Capture post-processing */
	if (need_scaler) {
		struct ia_css_binary_descr yuv_scaler_descr;

		err = ia_css_pipe_create_cas_scaler_desc(pipe,
							 &cas_scaler_descr);
		if (err)
			goto ERR;
		mycs->num_output = cas_scaler_descr.num_output_stage;
		mycs->num_yuv_scaler = cas_scaler_descr.num_stage;
		mycs->yuv_scaler_binary = kcalloc(cas_scaler_descr.num_stage,
						  sizeof(struct ia_css_binary),
						  GFP_KERNEL);
		if (!mycs->yuv_scaler_binary) {
			err = -ENOMEM;
			goto ERR;
		}
		mycs->is_output_stage = kcalloc(cas_scaler_descr.num_stage,
						sizeof(bool), GFP_KERNEL);
		if (!mycs->is_output_stage) {
			err = -ENOMEM;
			goto ERR;
		}
		for (i = 0; i < cas_scaler_descr.num_stage; i++) {
			mycs->is_output_stage[i] = cas_scaler_descr.is_output_stage[i];
			ia_css_pipe_get_yuvscaler_binarydesc(pipe,
							     &yuv_scaler_descr,
							     &cas_scaler_descr.in_info[i],
							     &cas_scaler_descr.out_info[i],
							     &cas_scaler_descr.internal_out_info[i],
							     &cas_scaler_descr.vf_info[i]);
			err = ia_css_binary_find(&yuv_scaler_descr,
						 &mycs->yuv_scaler_binary[i]);
			if (err)
				goto ERR;
		}
		ia_css_pipe_destroy_cas_scaler_desc(&cas_scaler_descr);
	} else {
		mycs->num_output = 1;
	}

	if (need_scaler)
		next_binary = &mycs->yuv_scaler_binary[0];
	else
		next_binary = NULL;

#if defined(ISP2401)
	/*
	 * NOTES
	 * - Why does the "yuvpp" pipe needs "isp_copy_binary" (i.e. ISP Copy) when
	 *   its input is "ATOMISP_INPUT_FORMAT_YUV422_8"?
	 *
	 *   In most use cases, the first stage in the "yuvpp" pipe is the "yuv_scale_
	 *   binary". However, the "yuv_scale_binary" does NOT support the input-frame
	 *   format as "IA_CSS_STREAM _FORMAT_YUV422_8".
	 *
	 *   Hence, the "isp_copy_binary" is required to be present in front of the "yuv
	 *   _scale_binary". It would translate the input-frame to the frame formats that
	 *   are supported by the "yuv_scale_binary".
	 *
	 *   Please refer to "FrameWork/css/isp/pipes/capture_pp/capture_pp_1.0/capture_
	 *   pp_defs.h" for the list of input-frame formats that are supported by the
	 *   "yuv_scale_binary".
	 */
	need_isp_copy_binary =
	    (pipe->stream->config.input_config.format == ATOMISP_INPUT_FORMAT_YUV422_8);
#else  /* !ISP2401 */
	need_isp_copy_binary = true;
#endif /*  ISP2401 */

	if (need_isp_copy_binary) {
		err = load_copy_binary(pipe,
				       &mycs->copy_binary,
				       next_binary);

		if (err)
			goto ERR;

		/*
		 * NOTES
		 * - Why is "pipe->pipe_settings.capture.copy_binary.online" specified?
		 *
		 *   In some use cases, the first stage in the "yuvpp" pipe is the
		 *   "isp_copy_binary". The "isp_copy_binary" is designed to process
		 *   the input from either the system DDR or from the IPU internal VMEM.
		 *   So it provides the flag "online" to specify where its input is from,
		 *   i.e.:
		 *
		 *      (1) "online <= true", the input is from the IPU internal VMEM.
		 *      (2) "online <= false", the input is from the system DDR.
		 *
		 *   In other use cases, the first stage in the "yuvpp" pipe is the
		 *   "yuv_scale_binary". "The "yuv_scale_binary" is designed to process the
		 *   input ONLY from the system DDR. So it does not provide the flag "online"
		 *   to specify where its input is from.
		 */
		pipe->pipe_settings.capture.copy_binary.online = pipe->stream->config.online;
	}

	/* Viewfinder post-processing */
	if (need_scaler) {
		for (i = 0, j = 0; i < mycs->num_yuv_scaler; i++) {
			if (mycs->is_output_stage[i]) {
				assert(j < 2);
				vf_pp_in_info[j] =
				    &mycs->yuv_scaler_binary[i].vf_frame_info;
				j++;
			}
		}
		mycs->num_vf_pp = j;
	} else {
		vf_pp_in_info[0] =
		    &mycs->copy_binary.vf_frame_info;
		for (i = 1; i < IA_CSS_PIPE_MAX_OUTPUT_STAGE; i++)
			vf_pp_in_info[i] = NULL;

		mycs->num_vf_pp = 1;
	}
	mycs->vf_pp_binary = kcalloc(mycs->num_vf_pp,
				     sizeof(struct ia_css_binary),
				     GFP_KERNEL);
	if (!mycs->vf_pp_binary) {
		err = -ENOMEM;
		goto ERR;
	}

	{
		struct ia_css_binary_descr vf_pp_descr;

		for (i = 0; i < mycs->num_vf_pp; i++) {
			if (pipe->vf_output_info[i].res.width != 0) {
				ia_css_pipe_get_vfpp_binarydesc(pipe,
								&vf_pp_descr, vf_pp_in_info[i], &pipe->vf_output_info[i]);
				err = ia_css_binary_find(&vf_pp_descr, &mycs->vf_pp_binary[i]);
				if (err)
					goto ERR;
			}
		}
	}

	if (err)
		goto ERR;

ERR:
	if (need_scaler)
		ia_css_pipe_destroy_cas_scaler_desc(&cas_scaler_descr);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE, "load_yuvpp_binaries() leave, err=%d\n",
			    err);
	return err;
}

static int
unload_yuvpp_binaries(struct ia_css_pipe *pipe)
{
	unsigned int i;

	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);

	if ((!pipe) || (pipe->mode != IA_CSS_PIPE_ID_YUVPP)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}
	ia_css_binary_unload(&pipe->pipe_settings.yuvpp.copy_binary);
	for (i = 0; i < pipe->pipe_settings.yuvpp.num_yuv_scaler; i++)
		ia_css_binary_unload(&pipe->pipe_settings.yuvpp.yuv_scaler_binary[i]);

	for (i = 0; i < pipe->pipe_settings.yuvpp.num_vf_pp; i++)
		ia_css_binary_unload(&pipe->pipe_settings.yuvpp.vf_pp_binary[i]);

	kfree(pipe->pipe_settings.yuvpp.is_output_stage);
	pipe->pipe_settings.yuvpp.is_output_stage = NULL;
	kfree(pipe->pipe_settings.yuvpp.yuv_scaler_binary);
	pipe->pipe_settings.yuvpp.yuv_scaler_binary = NULL;
	kfree(pipe->pipe_settings.yuvpp.vf_pp_binary);
	pipe->pipe_settings.yuvpp.vf_pp_binary = NULL;

	IA_CSS_LEAVE_ERR_PRIVATE(0);
	return 0;
}

static int yuvpp_start(struct ia_css_pipe *pipe)
{
	int err = 0;
	enum sh_css_pipe_config_override copy_ovrd;
	enum ia_css_input_mode yuvpp_pipe_input_mode;
	unsigned int thread_id;

	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);
	if ((!pipe) || (pipe->mode != IA_CSS_PIPE_ID_YUVPP)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	yuvpp_pipe_input_mode = pipe->stream->config.mode;

	sh_css_metrics_start_frame();

	/* multi stream video needs mipi buffers */

	err = send_mipi_frames(pipe);
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}

	ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &thread_id);
	copy_ovrd = 1 << thread_id;

	start_pipe(pipe, copy_ovrd, yuvpp_pipe_input_mode);

	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static int
sh_css_pipe_unload_binaries(struct ia_css_pipe *pipe)
{
	int err = 0;

	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);

	if (!pipe) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}
	/* PIPE_MODE_COPY has no binaries, but has output frames to outside*/
	if (pipe->config.mode == IA_CSS_PIPE_MODE_COPY) {
		IA_CSS_LEAVE_ERR_PRIVATE(0);
		return 0;
	}

	switch (pipe->mode) {
	case IA_CSS_PIPE_ID_PREVIEW:
		err = unload_preview_binaries(pipe);
		break;
	case IA_CSS_PIPE_ID_VIDEO:
		err = unload_video_binaries(pipe);
		break;
	case IA_CSS_PIPE_ID_CAPTURE:
		err = unload_capture_binaries(pipe);
		break;
	case IA_CSS_PIPE_ID_YUVPP:
		err = unload_yuvpp_binaries(pipe);
		break;
	default:
		break;
	}
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static int
sh_css_pipe_load_binaries(struct ia_css_pipe *pipe)
{
	int err = 0;

	assert(pipe);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE, "sh_css_pipe_load_binaries() enter:\n");

	/* PIPE_MODE_COPY has no binaries, but has output frames to outside*/
	if (pipe->config.mode == IA_CSS_PIPE_MODE_COPY)
		return err;

	switch (pipe->mode) {
	case IA_CSS_PIPE_ID_PREVIEW:
		err = load_preview_binaries(pipe);
		break;
	case IA_CSS_PIPE_ID_VIDEO:
		err = load_video_binaries(pipe);
		break;
	case IA_CSS_PIPE_ID_CAPTURE:
		err = load_capture_binaries(pipe);
		break;
	case IA_CSS_PIPE_ID_YUVPP:
		err = load_yuvpp_binaries(pipe);
		break;
	case IA_CSS_PIPE_ID_ACC:
		break;
	default:
		err = -EINVAL;
		break;
	}
	if (err) {
		if (sh_css_pipe_unload_binaries(pipe)) {
			/*
			 * currently css does not support multiple error
			 * returns in a single function, using -EINVAL in
			 * this case
			 */
			err = -EINVAL;
		}
	}
	return err;
}

static int
create_host_yuvpp_pipeline(struct ia_css_pipe *pipe)
{
	struct ia_css_pipeline *me;
	int err = 0;
	struct ia_css_pipeline_stage *vf_pp_stage = NULL,
		*copy_stage = NULL,
		*yuv_scaler_stage = NULL;
	struct ia_css_binary *copy_binary,
		*vf_pp_binary,
		*yuv_scaler_binary;
	bool need_scaler = false;
	unsigned int num_stage, num_output_stage;
	unsigned int i, j;

	struct ia_css_frame *in_frame = NULL;
	struct ia_css_frame *out_frame[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	struct ia_css_frame *bin_out_frame[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	struct ia_css_frame *vf_frame[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	struct ia_css_pipeline_stage_desc stage_desc;
	bool need_in_frameinfo_memory = false;
#ifdef ISP2401
	bool sensor = false;
	bool buffered_sensor = false;
	bool online = false;
	bool continuous = false;
#endif

	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);
	if ((!pipe) || (!pipe->stream) || (pipe->mode != IA_CSS_PIPE_ID_YUVPP)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}
	me = &pipe->pipeline;
	ia_css_pipeline_clean(me);
	for (i = 0; i < IA_CSS_PIPE_MAX_OUTPUT_STAGE; i++) {
		out_frame[i] = NULL;
		vf_frame[i] = NULL;
	}
	ia_css_pipe_util_create_output_frames(bin_out_frame);
	num_stage  = pipe->pipe_settings.yuvpp.num_yuv_scaler;
	num_output_stage   = pipe->pipe_settings.yuvpp.num_output;

#ifdef ISP2401
	/*
	 * When the input system is 2401, always enable 'in_frameinfo_memory'
	 * except for the following:
	 * - Direct Sensor Mode Online Capture
	 * - Direct Sensor Mode Continuous Capture
	 * - Buffered Sensor Mode Continuous Capture
	 */
	sensor = pipe->stream->config.mode == IA_CSS_INPUT_MODE_SENSOR;
	buffered_sensor = pipe->stream->config.mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR;
	online = pipe->stream->config.online;
	continuous = pipe->stream->config.continuous;
	need_in_frameinfo_memory =
	!((sensor && (online || continuous)) || (buffered_sensor && continuous));
#else
	/* Construct in_frame info (only in case we have dynamic input */
	need_in_frameinfo_memory = pipe->stream->config.mode == IA_CSS_INPUT_MODE_MEMORY;
#endif
	/*
	 * the input frame can come from:
	 *
	 *  a) memory: connect yuvscaler to me->in_frame
	 *  b) sensor, via copy binary: connect yuvscaler to copy binary later
	 *     on
	 */
	if (need_in_frameinfo_memory) {
		/* TODO: improve for different input formats. */

		/*
		 * "pipe->stream->config.input_config.format" represents the sensor output
		 * frame format, e.g. YUV422 8-bit.
		 *
		 * "in_frame_format" represents the imaging pipe's input frame format, e.g.
		 * Bayer-Quad RAW.
		 */
		int in_frame_format;

		if (pipe->stream->config.input_config.format ==
		    ATOMISP_INPUT_FORMAT_YUV420_8_LEGACY) {
			in_frame_format = IA_CSS_FRAME_FORMAT_CSI_MIPI_LEGACY_YUV420_8;
		} else if (pipe->stream->config.input_config.format ==
			    ATOMISP_INPUT_FORMAT_YUV422_8) {
			/*
			 * When the sensor output frame format is "ATOMISP_INPUT_FORMAT_YUV422_8",
			 * the "isp_copy_var" binary is selected as the first stage in the yuvpp
			 * pipe.
			 *
			 * For the "isp_copy_var" binary, it reads the YUV422-8 pixels from
			 * the frame buffer (at DDR) to the frame-line buffer (at VMEM).
			 *
			 * By now, the "isp_copy_var" binary does NOT provide a separated
			 * frame-line buffer to store the YUV422-8 pixels. Instead, it stores
			 * the YUV422-8 pixels in the frame-line buffer which is designed to
			 * store the Bayer-Quad RAW pixels.
			 *
			 * To direct the "isp_copy_var" binary reading from the RAW frame-line
			 * buffer, its input frame format must be specified as "IA_CSS_FRAME_
			 * FORMAT_RAW".
			 */
			in_frame_format = IA_CSS_FRAME_FORMAT_RAW;
		} else {
			in_frame_format = IA_CSS_FRAME_FORMAT_NV12;
		}

		err = init_in_frameinfo_memory_defaults(pipe,
							&me->in_frame,
							in_frame_format);

		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}

		in_frame = &me->in_frame;
	} else {
		in_frame = NULL;
	}

	for (i = 0; i < num_output_stage; i++) {
		assert(i < IA_CSS_PIPE_MAX_OUTPUT_STAGE);
		if (pipe->output_info[i].res.width != 0) {
			err = init_out_frameinfo_defaults(pipe, &me->out_frame[i], i);
			if (err) {
				IA_CSS_LEAVE_ERR_PRIVATE(err);
				return err;
			}
			out_frame[i] = &me->out_frame[i];
		}

		/* Construct vf_frame info (only in case we have VF) */
		if (pipe->vf_output_info[i].res.width != 0) {
			err = init_vf_frameinfo_defaults(pipe, &me->vf_frame[i], i);
			if (err) {
				IA_CSS_LEAVE_ERR_PRIVATE(err);
				return err;
			}
			vf_frame[i] = &me->vf_frame[i];
		}
	}

	copy_binary       = &pipe->pipe_settings.yuvpp.copy_binary;
	vf_pp_binary      = pipe->pipe_settings.yuvpp.vf_pp_binary;
	yuv_scaler_binary = pipe->pipe_settings.yuvpp.yuv_scaler_binary;
	need_scaler = need_yuv_scaler_stage(pipe);

	if (pipe->pipe_settings.yuvpp.copy_binary.info) {
		struct ia_css_frame *in_frame_local = NULL;

#ifdef ISP2401
		/* After isp copy is enabled in_frame needs to be passed. */
		if (!online)
			in_frame_local = in_frame;
#endif

		if (need_scaler) {
			ia_css_pipe_util_set_output_frames(bin_out_frame,
							   0, NULL);
			ia_css_pipe_get_generic_stage_desc(&stage_desc,
							   copy_binary,
							   bin_out_frame,
							   in_frame_local,
							   NULL);
		} else {
			ia_css_pipe_util_set_output_frames(bin_out_frame,
							   0, out_frame[0]);
			ia_css_pipe_get_generic_stage_desc(&stage_desc,
							   copy_binary,
							   bin_out_frame,
							   in_frame_local,
							   NULL);
		}

		err = ia_css_pipeline_create_and_add_stage(me,
							   &stage_desc,
							   &copy_stage);

		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}

		if (copy_stage) {
			/* if we use yuv scaler binary, vf output should be from there */
			copy_stage->args.copy_vf = !need_scaler;
			/* for yuvpp pipe, it should always be enabled */
			copy_stage->args.copy_output = true;
			/* connect output of copy binary to input of yuv scaler */
			in_frame = copy_stage->args.out_frame[0];
		}
	}

	if (need_scaler) {
		struct ia_css_frame *tmp_out_frame = NULL;
		struct ia_css_frame *tmp_vf_frame = NULL;
		struct ia_css_frame *tmp_in_frame = in_frame;

		for (i = 0, j = 0; i < num_stage; i++) {
			assert(j < num_output_stage);
			if (pipe->pipe_settings.yuvpp.is_output_stage[i]) {
				tmp_out_frame = out_frame[j];
				tmp_vf_frame = vf_frame[j];
			} else {
				tmp_out_frame = NULL;
				tmp_vf_frame = NULL;
			}

			err = add_yuv_scaler_stage(pipe, me, tmp_in_frame,
						   tmp_out_frame,
						   NULL,
						   &yuv_scaler_binary[i],
						   &yuv_scaler_stage);

			if (err) {
				IA_CSS_LEAVE_ERR_PRIVATE(err);
				return err;
			}
			/* we use output port 1 as internal output port */
			tmp_in_frame = yuv_scaler_stage->args.out_frame[1];
			if (pipe->pipe_settings.yuvpp.is_output_stage[i]) {
				if (tmp_vf_frame && (tmp_vf_frame->info.res.width != 0)) {
					in_frame = yuv_scaler_stage->args.out_vf_frame;
					err = add_vf_pp_stage(pipe, in_frame,
							      tmp_vf_frame,
							      &vf_pp_binary[j],
							      &vf_pp_stage);

					if (err) {
						IA_CSS_LEAVE_ERR_PRIVATE(err);
						return err;
					}
				}
				j++;
			}
		}
	} else if (copy_stage) {
		if (vf_frame[0] && vf_frame[0]->info.res.width != 0) {
			in_frame = copy_stage->args.out_vf_frame;
			err = add_vf_pp_stage(pipe, in_frame, vf_frame[0],
					      &vf_pp_binary[0], &vf_pp_stage);
		}
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	}

	ia_css_pipeline_finalize_stages(&pipe->pipeline,
					pipe->stream->config.continuous);

	IA_CSS_LEAVE_ERR_PRIVATE(0);

	return 0;
}

static int
create_host_copy_pipeline(struct ia_css_pipe *pipe,
			  unsigned int max_input_width,
			  struct ia_css_frame *out_frame)
{
	struct ia_css_pipeline *me;
	int err = 0;
	struct ia_css_pipeline_stage_desc stage_desc;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "create_host_copy_pipeline() enter:\n");

	/* pipeline already created as part of create_host_pipeline_structure */
	me = &pipe->pipeline;
	ia_css_pipeline_clean(me);

	/* Construct out_frame info */
	out_frame->flash_state = IA_CSS_FRAME_FLASH_STATE_NONE;

	if (copy_on_sp(pipe) &&
	    pipe->stream->config.input_config.format == ATOMISP_INPUT_FORMAT_BINARY_8) {
		ia_css_frame_info_init(&out_frame->info, JPEG_BYTES, 1,
				       IA_CSS_FRAME_FORMAT_BINARY_8, 0);
	} else if (out_frame->info.format == IA_CSS_FRAME_FORMAT_RAW) {
		out_frame->info.raw_bit_depth =
		ia_css_pipe_util_pipe_input_format_bpp(pipe);
	}

	me->num_stages = 1;
	me->pipe_id = IA_CSS_PIPE_ID_COPY;
	pipe->mode  = IA_CSS_PIPE_ID_COPY;

	ia_css_pipe_get_sp_func_stage_desc(&stage_desc, out_frame,
					   IA_CSS_PIPELINE_RAW_COPY,
					   max_input_width);
	err = ia_css_pipeline_create_and_add_stage(me, &stage_desc, NULL);

	ia_css_pipeline_finalize_stages(&pipe->pipeline,
					pipe->stream->config.continuous);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "create_host_copy_pipeline() leave:\n");

	return err;
}

static int
create_host_isyscopy_capture_pipeline(struct ia_css_pipe *pipe)
{
	struct ia_css_pipeline *me = &pipe->pipeline;
	int err = 0;
	struct ia_css_pipeline_stage_desc stage_desc;
	struct ia_css_frame *out_frame = &me->out_frame[0];
	struct ia_css_pipeline_stage *out_stage = NULL;
	unsigned int thread_id;
	enum sh_css_queue_id queue_id;
	unsigned int max_input_width = MAX_VECTORS_PER_INPUT_LINE_CONT * ISP_VEC_NELEMS;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "create_host_isyscopy_capture_pipeline() enter:\n");
	ia_css_pipeline_clean(me);

	/* Construct out_frame info */
	err = sh_css_pipe_get_output_frame_info(pipe, &out_frame->info, 0);
	if (err)
		return err;
	out_frame->flash_state = IA_CSS_FRAME_FLASH_STATE_NONE;
	ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &thread_id);
	ia_css_query_internal_queue_id(IA_CSS_BUFFER_TYPE_OUTPUT_FRAME, thread_id, &queue_id);
	out_frame->dynamic_queue_id = queue_id;
	out_frame->buf_type = IA_CSS_BUFFER_TYPE_OUTPUT_FRAME;

	me->num_stages = 1;
	me->pipe_id = IA_CSS_PIPE_ID_CAPTURE;
	pipe->mode  = IA_CSS_PIPE_ID_CAPTURE;
	ia_css_pipe_get_sp_func_stage_desc(&stage_desc, out_frame,
					   IA_CSS_PIPELINE_ISYS_COPY,
					   max_input_width);
	err = ia_css_pipeline_create_and_add_stage(me,
						   &stage_desc, &out_stage);
	if (err)
		return err;

	ia_css_pipeline_finalize_stages(me, pipe->stream->config.continuous);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "create_host_isyscopy_capture_pipeline() leave:\n");

	return err;
}

static int
create_host_regular_capture_pipeline(struct ia_css_pipe *pipe)
{
	struct ia_css_pipeline *me;
	int err = 0;
	enum ia_css_capture_mode mode;
	struct ia_css_pipeline_stage *current_stage = NULL;
	struct ia_css_pipeline_stage *yuv_scaler_stage = NULL;
	struct ia_css_binary *copy_binary,
		*primary_binary[MAX_NUM_PRIMARY_STAGES],
		*vf_pp_binary,
		*pre_isp_binary,
		*anr_gdc_binary,
		*post_isp_binary,
		*yuv_scaler_binary,
		*capture_pp_binary,
		*capture_ldc_binary;
	bool need_pp = false;
	bool raw;

	struct ia_css_frame *in_frame;
	struct ia_css_frame *out_frame;
	struct ia_css_frame *out_frames[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	struct ia_css_frame *vf_frame;
	struct ia_css_pipeline_stage_desc stage_desc;
	bool need_in_frameinfo_memory = false;
#ifdef ISP2401
	bool sensor = false;
	bool buffered_sensor = false;
	bool online = false;
	bool continuous = false;
#endif
	unsigned int i, num_yuv_scaler, num_primary_stage;
	bool need_yuv_pp = false;
	bool *is_output_stage = NULL;
	bool need_ldc = false;

	IA_CSS_ENTER_PRIVATE("");
	assert(pipe);
	assert(pipe->stream);
	assert(pipe->mode == IA_CSS_PIPE_ID_CAPTURE ||
	       pipe->mode == IA_CSS_PIPE_ID_COPY);

	me = &pipe->pipeline;
	mode = pipe->config.default_capture_config.mode;
	raw = (mode == IA_CSS_CAPTURE_MODE_RAW);
	ia_css_pipeline_clean(me);
	ia_css_pipe_util_create_output_frames(out_frames);

#ifdef ISP2401
	/*
	 * When the input system is 2401, always enable 'in_frameinfo_memory'
	 * except for the following:
	 * - Direct Sensor Mode Online Capture
	 * - Direct Sensor Mode Online Capture
	 * - Direct Sensor Mode Continuous Capture
	 * - Buffered Sensor Mode Continuous Capture
	 */
	sensor = (pipe->stream->config.mode == IA_CSS_INPUT_MODE_SENSOR);
	buffered_sensor = (pipe->stream->config.mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR);
	online = pipe->stream->config.online;
	continuous = pipe->stream->config.continuous;
	need_in_frameinfo_memory =
	!((sensor && (online || continuous)) || (buffered_sensor && (online || continuous)));
#else
	/* Construct in_frame info (only in case we have dynamic input */
	need_in_frameinfo_memory = pipe->stream->config.mode == IA_CSS_INPUT_MODE_MEMORY;
#endif
	if (need_in_frameinfo_memory) {
		err = init_in_frameinfo_memory_defaults(pipe, &me->in_frame,
							IA_CSS_FRAME_FORMAT_RAW);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}

		in_frame = &me->in_frame;
	} else {
		in_frame = NULL;
	}

	err = init_out_frameinfo_defaults(pipe, &me->out_frame[0], 0);
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}
	out_frame = &me->out_frame[0];

	/* Construct vf_frame info (only in case we have VF) */
	if (pipe->enable_viewfinder[IA_CSS_PIPE_OUTPUT_STAGE_0]) {
		if (mode == IA_CSS_CAPTURE_MODE_RAW || mode == IA_CSS_CAPTURE_MODE_BAYER) {
			/* These modes don't support viewfinder output */
			vf_frame = NULL;
		} else {
			init_vf_frameinfo_defaults(pipe, &me->vf_frame[0], 0);
			vf_frame = &me->vf_frame[0];
		}
	} else {
		vf_frame = NULL;
	}

	copy_binary       = &pipe->pipe_settings.capture.copy_binary;
	num_primary_stage = pipe->pipe_settings.capture.num_primary_stage;
	if ((num_primary_stage == 0) && (mode == IA_CSS_CAPTURE_MODE_PRIMARY)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	for (i = 0; i < num_primary_stage; i++)
		primary_binary[i] = &pipe->pipe_settings.capture.primary_binary[i];

	vf_pp_binary      = &pipe->pipe_settings.capture.vf_pp_binary;
	pre_isp_binary    = &pipe->pipe_settings.capture.pre_isp_binary;
	anr_gdc_binary    = &pipe->pipe_settings.capture.anr_gdc_binary;
	post_isp_binary   = &pipe->pipe_settings.capture.post_isp_binary;
	capture_pp_binary = &pipe->pipe_settings.capture.capture_pp_binary;
	yuv_scaler_binary = pipe->pipe_settings.capture.yuv_scaler_binary;
	num_yuv_scaler	  = pipe->pipe_settings.capture.num_yuv_scaler;
	is_output_stage   = pipe->pipe_settings.capture.is_output_stage;
	capture_ldc_binary = &pipe->pipe_settings.capture.capture_ldc_binary;

	need_pp = (need_capture_pp(pipe) || pipe->output_stage) &&
		    mode != IA_CSS_CAPTURE_MODE_RAW &&
		    mode != IA_CSS_CAPTURE_MODE_BAYER;
	need_yuv_pp = (yuv_scaler_binary && yuv_scaler_binary->info);
	need_ldc = (capture_ldc_binary && capture_ldc_binary->info);

	if (pipe->pipe_settings.capture.copy_binary.info) {
		if (raw) {
			ia_css_pipe_util_set_output_frames(out_frames, 0, out_frame);
#if defined(ISP2401)
			if (!continuous) {
				ia_css_pipe_get_generic_stage_desc(&stage_desc,
								   copy_binary,
								   out_frames,
								   in_frame,
								   NULL);
			} else {
				in_frame = pipe->stream->last_pipe->continuous_frames[0];
				ia_css_pipe_get_generic_stage_desc(&stage_desc,
								   copy_binary,
								   out_frames,
								   in_frame,
								   NULL);
			}
#else
			ia_css_pipe_get_generic_stage_desc(&stage_desc,
							   copy_binary,
							   out_frames,
							   NULL, NULL);
#endif
		} else {
			ia_css_pipe_util_set_output_frames(out_frames, 0,
							   in_frame);
			ia_css_pipe_get_generic_stage_desc(&stage_desc,
							   copy_binary,
							   out_frames,
							   NULL, NULL);
		}

		err = ia_css_pipeline_create_and_add_stage(me,
							   &stage_desc,
							   &current_stage);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	} else if (pipe->stream->config.continuous) {
		in_frame = pipe->stream->last_pipe->continuous_frames[0];
	}

	if (mode == IA_CSS_CAPTURE_MODE_PRIMARY) {
		struct ia_css_frame *local_in_frame = NULL;
		struct ia_css_frame *local_out_frame = NULL;

		for (i = 0; i < num_primary_stage; i++) {
			if (i == 0)
				local_in_frame = in_frame;
			else
				local_in_frame = NULL;
#ifndef ISP2401
			if (!need_pp && (i == num_primary_stage - 1))
#else
			if (!need_pp && (i == num_primary_stage - 1) && !need_ldc)
#endif
				local_out_frame = out_frame;
			else
				local_out_frame = NULL;
			ia_css_pipe_util_set_output_frames(out_frames, 0, local_out_frame);
			/*
			 * WARNING: The #if def flag has been added below as a
			 * temporary solution to solve the problem of enabling the
			 * view finder in a single binary in a capture flow. The
			 * vf-pp stage has been removed from Skycam in the solution
			 * provided. The vf-pp stage should be re-introduced when
			 * required. This  * should not be considered as a clean solution.
			 * Proper investigation should be done to come up with the clean
			 * solution.
			 */
			ia_css_pipe_get_generic_stage_desc(&stage_desc,
							   primary_binary[i],
							   out_frames,
							   local_in_frame,
							   NULL);
			err = ia_css_pipeline_create_and_add_stage(me,
								   &stage_desc,
								   &current_stage);
			if (err) {
				IA_CSS_LEAVE_ERR_PRIVATE(err);
				return err;
			}
		}
		/* If we use copy iso primary, the input must be yuv iso raw */
		current_stage->args.copy_vf =
		    primary_binary[0]->info->sp.pipeline.mode ==
		    IA_CSS_BINARY_MODE_COPY;
		current_stage->args.copy_output = current_stage->args.copy_vf;
	} else if (mode == IA_CSS_CAPTURE_MODE_ADVANCED ||
		    mode == IA_CSS_CAPTURE_MODE_LOW_LIGHT) {
		ia_css_pipe_util_set_output_frames(out_frames, 0, NULL);
		ia_css_pipe_get_generic_stage_desc(&stage_desc, pre_isp_binary,
						   out_frames, in_frame, NULL);
		err = ia_css_pipeline_create_and_add_stage(me, &stage_desc,
							   NULL);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
		ia_css_pipe_util_set_output_frames(out_frames, 0, NULL);
		ia_css_pipe_get_generic_stage_desc(&stage_desc, anr_gdc_binary,
						   out_frames, NULL, NULL);
		err = ia_css_pipeline_create_and_add_stage(me, &stage_desc,
							   NULL);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}

		if (need_pp) {
			ia_css_pipe_util_set_output_frames(out_frames, 0, NULL);
			ia_css_pipe_get_generic_stage_desc(&stage_desc,
							   post_isp_binary,
							   out_frames,
							   NULL, NULL);
		} else {
			ia_css_pipe_util_set_output_frames(out_frames, 0,
							   out_frame);
			ia_css_pipe_get_generic_stage_desc(&stage_desc,
							   post_isp_binary,
							   out_frames,
							   NULL, NULL);
		}

		err = ia_css_pipeline_create_and_add_stage(me, &stage_desc,
							   &current_stage);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	} else if (mode == IA_CSS_CAPTURE_MODE_BAYER) {
		ia_css_pipe_util_set_output_frames(out_frames, 0, out_frame);
		ia_css_pipe_get_generic_stage_desc(&stage_desc, pre_isp_binary,
						   out_frames, in_frame, NULL);
		err = ia_css_pipeline_create_and_add_stage(me, &stage_desc,
							   NULL);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	}

	if (need_pp && current_stage) {
		struct ia_css_frame *local_in_frame = NULL;

		local_in_frame = current_stage->args.out_frame[0];

		if (need_ldc) {
			ia_css_pipe_util_set_output_frames(out_frames, 0, NULL);
			ia_css_pipe_get_generic_stage_desc(&stage_desc,
							   capture_ldc_binary,
							   out_frames,
							   local_in_frame,
							   NULL);
			err = ia_css_pipeline_create_and_add_stage(me,
								   &stage_desc,
								   &current_stage);
			local_in_frame = current_stage->args.out_frame[0];
		}
		err = add_capture_pp_stage(pipe, me, local_in_frame,
					   need_yuv_pp ? NULL : out_frame,
					   capture_pp_binary,
					   &current_stage);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	}

	if (need_yuv_pp && current_stage) {
		struct ia_css_frame *tmp_in_frame = current_stage->args.out_frame[0];
		struct ia_css_frame *tmp_out_frame = NULL;

		for (i = 0; i < num_yuv_scaler; i++) {
			if (is_output_stage[i])
				tmp_out_frame = out_frame;
			else
				tmp_out_frame = NULL;

			err = add_yuv_scaler_stage(pipe, me, tmp_in_frame,
						   tmp_out_frame, NULL,
						   &yuv_scaler_binary[i],
						   &yuv_scaler_stage);
			if (err) {
				IA_CSS_LEAVE_ERR_PRIVATE(err);
				return err;
			}
			/* we use output port 1 as internal output port */
			tmp_in_frame = yuv_scaler_stage->args.out_frame[1];
		}
	}

	/*
	 * WARNING: The #if def flag has been added below as a
	 * temporary solution to solve the problem of enabling the
	 * view finder in a single binary in a capture flow. The vf-pp
	 * stage has been removed from Skycam in the solution provided.
	 * The vf-pp stage should be re-introduced when required. This
	 * should not be considered as a clean solution. Proper
	 * investigation should be done to come up with the clean solution.
	 */
	if (mode != IA_CSS_CAPTURE_MODE_RAW &&
	    mode != IA_CSS_CAPTURE_MODE_BAYER &&
	    current_stage && vf_frame) {
		in_frame = current_stage->args.out_vf_frame;
		err = add_vf_pp_stage(pipe, in_frame, vf_frame, vf_pp_binary,
				      &current_stage);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	}
	ia_css_pipeline_finalize_stages(&pipe->pipeline, pipe->stream->config.continuous);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "create_host_regular_capture_pipeline() leave:\n");

	return 0;
}

static int
create_host_capture_pipeline(struct ia_css_pipe *pipe)
{
	int err = 0;

	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);

	if (pipe->config.mode == IA_CSS_PIPE_MODE_COPY)
		err = create_host_isyscopy_capture_pipeline(pipe);
	else
		err = create_host_regular_capture_pipeline(pipe);
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}

	IA_CSS_LEAVE_ERR_PRIVATE(err);

	return err;
}

static int capture_start(struct ia_css_pipe *pipe)
{
	struct ia_css_pipeline *me;
	unsigned int thread_id;

	int err = 0;
	enum sh_css_pipe_config_override copy_ovrd;

	IA_CSS_ENTER_PRIVATE("pipe = %p", pipe);
	if (!pipe) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	me = &pipe->pipeline;

	if ((pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_RAW   ||
	     pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_BAYER) &&
	    (pipe->config.mode != IA_CSS_PIPE_MODE_COPY)) {
		if (copy_on_sp(pipe)) {
			err = start_copy_on_sp(pipe, &me->out_frame[0]);
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	}

#if !defined(ISP2401)
	/* old isys: need to send_mipi_frames() in all pipe modes */
	err = send_mipi_frames(pipe);
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}
#else
	if (pipe->config.mode != IA_CSS_PIPE_MODE_COPY) {
		err = send_mipi_frames(pipe);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	}
#endif

	ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &thread_id);
	copy_ovrd = 1 << thread_id;

	start_pipe(pipe, copy_ovrd, pipe->stream->config.mode);

#if !defined(ISP2401)
	/*
	 * old isys: for IA_CSS_PIPE_MODE_COPY pipe, isys rx has to be configured,
	 * which is currently done in start_binary(); but COPY pipe contains no binary,
	 * and does not call start_binary(); so we need to configure the rx here.
	 */
	if (pipe->config.mode == IA_CSS_PIPE_MODE_COPY &&
	    pipe->stream->reconfigure_css_rx) {
		ia_css_isys_rx_configure(&pipe->stream->csi_rx_config,
					 pipe->stream->config.mode);
		pipe->stream->reconfigure_css_rx = false;
	}
#endif

	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static int
sh_css_pipe_get_output_frame_info(struct ia_css_pipe *pipe,
				  struct ia_css_frame_info *info,
				  unsigned int idx)
{
	assert(pipe);
	assert(info);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_pipe_get_output_frame_info() enter:\n");

	*info = pipe->output_info[idx];
	if (copy_on_sp(pipe) &&
	    pipe->stream->config.input_config.format == ATOMISP_INPUT_FORMAT_BINARY_8) {
		ia_css_frame_info_init(
		    info,
		    JPEG_BYTES,
		    1,
		    IA_CSS_FRAME_FORMAT_BINARY_8,
		    0);
	} else if (info->format == IA_CSS_FRAME_FORMAT_RAW ||
		   info->format == IA_CSS_FRAME_FORMAT_RAW_PACKED) {
		info->raw_bit_depth =
		ia_css_pipe_util_pipe_input_format_bpp(pipe);
	}

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_pipe_get_output_frame_info() leave:\n");
	return 0;
}

void
ia_css_stream_send_input_frame(const struct ia_css_stream *stream,
			       const unsigned short *data,
			       unsigned int width,
			       unsigned int height)
{
	assert(stream);

	ia_css_inputfifo_send_input_frame(
	    data, width, height,
	    stream->config.channel_id,
	    stream->config.input_config.format,
	    stream->config.pixels_per_clock == 2);
}

void
ia_css_stream_start_input_frame(const struct ia_css_stream *stream)
{
	assert(stream);

	ia_css_inputfifo_start_frame(
	    stream->config.channel_id,
	    stream->config.input_config.format,
	    stream->config.pixels_per_clock == 2);
}

void
ia_css_stream_send_input_line(const struct ia_css_stream *stream,
			      const unsigned short *data,
			      unsigned int width,
			      const unsigned short *data2,
			      unsigned int width2)
{
	assert(stream);

	ia_css_inputfifo_send_line(stream->config.channel_id,
				   data, width, data2, width2);
}

void
ia_css_stream_send_input_embedded_line(const struct ia_css_stream *stream,
				       enum atomisp_input_format format,
				       const unsigned short *data,
				       unsigned int width)
{
	assert(stream);
	if (!data || width == 0)
		return;
	ia_css_inputfifo_send_embedded_line(stream->config.channel_id,
					    format, data, width);
}

void
ia_css_stream_end_input_frame(const struct ia_css_stream *stream)
{
	assert(stream);

	ia_css_inputfifo_end_frame(stream->config.channel_id);
}

static void
append_firmware(struct ia_css_fw_info **l, struct ia_css_fw_info *firmware)
{
	IA_CSS_ENTER_PRIVATE("l = %p, firmware = %p", l, firmware);
	if (!l) {
		IA_CSS_ERROR("NULL fw_info");
		IA_CSS_LEAVE_PRIVATE("");
		return;
	}
	while (*l)
		l = &(*l)->next;
	*l = firmware;
	/* when multiple acc extensions are loaded, 'next' can be not NULL */
	/*firmware->next = NULL;*/
	IA_CSS_LEAVE_PRIVATE("");
}

static void
remove_firmware(struct ia_css_fw_info **l, struct ia_css_fw_info *firmware)
{
	assert(*l);
	assert(firmware);
	(void)l;
	(void)firmware;
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE, "remove_firmware() enter:\n");

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE, "remove_firmware() leave:\n");
	return; /* removing single and multiple firmware is handled in acc_unload_extension() */
}

static int upload_isp_code(struct ia_css_fw_info *firmware)
{
	ia_css_ptr binary;

	if (!firmware) {
		IA_CSS_ERROR("NULL input parameter");
		return -EINVAL;
	}
	binary = firmware->info.isp.xmem_addr;

	if (!binary) {
		unsigned int size = firmware->blob.size;
		const unsigned char *blob;
		const unsigned char *binary_name;

		binary_name =
		    (const unsigned char *)(IA_CSS_EXT_ISP_PROG_NAME(
						firmware));
		blob = binary_name +
			strlen((const char *)binary_name) +
			1;
		binary = sh_css_load_blob(blob, size);
		firmware->info.isp.xmem_addr = binary;
	}

	if (!binary)
		return -ENOMEM;
	return 0;
}

static int
acc_load_extension(struct ia_css_fw_info *firmware)
{
	int err;
	struct ia_css_fw_info *hd = firmware;

	while (hd) {
		err = upload_isp_code(hd);
		if (err)
			return err;
		hd = hd->next;
	}

	if (!firmware)
		return -EINVAL;
	firmware->loaded = true;
	return 0;
}

static void
acc_unload_extension(struct ia_css_fw_info *firmware)
{
	struct ia_css_fw_info *hd = firmware;
	struct ia_css_fw_info *hdn = NULL;

	if (!firmware) /* should not happen */
		return;
	/* unload and remove multiple firmwares */
	while (hd) {
		hdn = (hd->next) ? &(*hd->next) : NULL;
		if (hd->info.isp.xmem_addr) {
			hmm_free(hd->info.isp.xmem_addr);
			hd->info.isp.xmem_addr = mmgr_NULL;
		}
		hd->isp_code = NULL;
		hd->next = NULL;
		hd = hdn;
	}

	firmware->loaded = false;
}

/* Load firmware for extension */
static int
ia_css_pipe_load_extension(struct ia_css_pipe *pipe,
			   struct ia_css_fw_info *firmware)
{
	int err = 0;

	IA_CSS_ENTER_PRIVATE("fw = %p pipe = %p", firmware, pipe);

	if ((!firmware) || (!pipe)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	if (firmware->info.isp.type == IA_CSS_ACC_OUTPUT)
		append_firmware(&pipe->output_stage, firmware);
	else if (firmware->info.isp.type == IA_CSS_ACC_VIEWFINDER)
		append_firmware(&pipe->vf_stage, firmware);
	err = acc_load_extension(firmware);

	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

/* Unload firmware for extension */
static void
ia_css_pipe_unload_extension(struct ia_css_pipe *pipe,
			     struct ia_css_fw_info *firmware)
{
	IA_CSS_ENTER_PRIVATE("fw = %p pipe = %p", firmware, pipe);

	if ((!firmware) || (!pipe)) {
		IA_CSS_ERROR("NULL input parameters");
		IA_CSS_LEAVE_PRIVATE("");
		return;
	}

	if (firmware->info.isp.type == IA_CSS_ACC_OUTPUT)
		remove_firmware(&pipe->output_stage, firmware);
	else if (firmware->info.isp.type == IA_CSS_ACC_VIEWFINDER)
		remove_firmware(&pipe->vf_stage, firmware);
	acc_unload_extension(firmware);

	IA_CSS_LEAVE_PRIVATE("");
}

bool
ia_css_pipeline_uses_params(struct ia_css_pipeline *me)
{
	struct ia_css_pipeline_stage *stage;

	assert(me);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_pipeline_uses_params() enter: me=%p\n", me);

	for (stage = me->stages; stage; stage = stage->next)
		if (stage->binary_info && stage->binary_info->enable.params) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
					    "ia_css_pipeline_uses_params() leave: return_bool=true\n");
			return true;
		}
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_pipeline_uses_params() leave: return_bool=false\n");
	return false;
}

static int
sh_css_pipeline_add_acc_stage(struct ia_css_pipeline *pipeline,
			      const void *acc_fw)
{
	struct ia_css_fw_info *fw = (struct ia_css_fw_info *)acc_fw;
	/* In QoS case, load_extension already called, so skipping */
	int	err = 0;

	if (!fw->loaded)
		err = acc_load_extension(fw);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "sh_css_pipeline_add_acc_stage() enter: pipeline=%p, acc_fw=%p\n",
			    pipeline, acc_fw);

	if (!err) {
		struct ia_css_pipeline_stage_desc stage_desc;

		ia_css_pipe_get_acc_stage_desc(&stage_desc, NULL, fw);
		err = ia_css_pipeline_create_and_add_stage(pipeline,
							   &stage_desc,
							   NULL);
	}

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "sh_css_pipeline_add_acc_stage() leave: return_err=%d\n", err);
	return err;
}

/*
 * @brief Tag a specific frame in continuous capture.
 * Refer to "sh_css_internal.h" for details.
 */
int ia_css_stream_capture_frame(struct ia_css_stream *stream,
				unsigned int exp_id)
{
	struct sh_css_tag_descr tag_descr;
	u32 encoded_tag_descr;
	int err;

	assert(stream);
	IA_CSS_ENTER("exp_id=%d", exp_id);

	/* Only continuous streams have a tagger */
	if (exp_id == 0 || !stream->config.continuous) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}

	if (!sh_css_sp_is_running()) {
		/* SP is not running. The queues are not valid */
		IA_CSS_LEAVE_ERR(-EBUSY);
		return -EBUSY;
	}

	/* Create the tag descriptor from the parameters */
	sh_css_create_tag_descr(0, 0, 0, exp_id, &tag_descr);
	/* Encode the tag descriptor into a 32-bit value */
	encoded_tag_descr = sh_css_encode_tag_descr(&tag_descr);
	/*
	 * Enqueue the encoded tag to the host2sp queue.
	 * Note: The pipe and stage IDs for tag_cmd queue are hard-coded to 0
	 * on both host and the SP side.
	 * It is mainly because it is enough to have only one tag_cmd queue
	 */
	err = ia_css_bufq_enqueue_tag_cmd(encoded_tag_descr);

	IA_CSS_LEAVE_ERR(err);
	return err;
}

/*
 * @brief Configure the continuous capture.
 * Refer to "sh_css_internal.h" for details.
 */
int ia_css_stream_capture(struct ia_css_stream *stream, int num_captures,
			  unsigned int skip, int offset)
{
	struct sh_css_tag_descr tag_descr;
	unsigned int encoded_tag_descr;
	int return_err;

	if (!stream)
		return -EINVAL;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_stream_capture() enter: num_captures=%d, skip=%d, offset=%d\n",
			    num_captures, skip, offset);

	/* Check if the tag descriptor is valid */
	if (num_captures < SH_CSS_MINIMUM_TAG_ID) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				    "ia_css_stream_capture() leave: return_err=%d\n",
				    -EINVAL);
		return -EINVAL;
	}

	/* Create the tag descriptor from the parameters */
	sh_css_create_tag_descr(num_captures, skip, offset, 0, &tag_descr);

	/* Encode the tag descriptor into a 32-bit value */
	encoded_tag_descr = sh_css_encode_tag_descr(&tag_descr);

	if (!sh_css_sp_is_running()) {
		/* SP is not running. The queues are not valid */
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				    "ia_css_stream_capture() leaving:queues unavailable\n");
		return -EBUSY;
	}

	/*
	 * Enqueue the encoded tag to the host2sp queue.
	 * Note: The pipe and stage IDs for tag_cmd queue are hard-coded to 0
	 * on both host and the SP side.
	 * It is mainly because it is enough to have only one tag_cmd queue
	 */
	return_err = ia_css_bufq_enqueue_tag_cmd((uint32_t)encoded_tag_descr);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_stream_capture() leave: return_err=%d\n",
			    return_err);

	return return_err;
}

void ia_css_stream_request_flash(struct ia_css_stream *stream)
{
	(void)stream;

	assert(stream);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_stream_request_flash() enter: void\n");

#ifndef ISP2401
	sh_css_write_host2sp_command(host2sp_cmd_start_flash);
#else
	if (sh_css_sp_is_running()) {
		if (!sh_css_write_host2sp_command(host2sp_cmd_start_flash)) {
			IA_CSS_ERROR("Call to 'sh-css_write_host2sp_command()' failed");
			ia_css_debug_dump_sp_sw_debug_info();
			ia_css_debug_dump_debug_info(NULL);
		}
	} else {
		IA_CSS_LOG("SP is not running!");
	}

#endif
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_stream_request_flash() leave: return_void\n");
}

static void
sh_css_init_host_sp_control_vars(void)
{
	const struct ia_css_fw_info *fw;
	unsigned int HIVE_ADDR_ia_css_ispctrl_sp_isp_started;

	unsigned int HIVE_ADDR_host_sp_queues_initialized;
	unsigned int HIVE_ADDR_sp_sleep_mode;
	unsigned int HIVE_ADDR_ia_css_dmaproxy_sp_invalidate_tlb;
	unsigned int HIVE_ADDR_sp_stop_copy_preview;
	unsigned int HIVE_ADDR_host_sp_com;
	unsigned int o = offsetof(struct host_sp_communication, host2sp_command)
			    / sizeof(int);

	unsigned int i;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_init_host_sp_control_vars() enter: void\n");

	fw = &sh_css_sp_fw;
	HIVE_ADDR_ia_css_ispctrl_sp_isp_started = fw->info.sp.isp_started;

	HIVE_ADDR_host_sp_queues_initialized =
	    fw->info.sp.host_sp_queues_initialized;
	HIVE_ADDR_sp_sleep_mode = fw->info.sp.sleep_mode;
	HIVE_ADDR_ia_css_dmaproxy_sp_invalidate_tlb = fw->info.sp.invalidate_tlb;
	HIVE_ADDR_sp_stop_copy_preview = fw->info.sp.stop_copy_preview;
	HIVE_ADDR_host_sp_com = fw->info.sp.host_sp_com;

	sp_dmem_store_uint32(SP0_ID,
			     (unsigned int)sp_address_of(ia_css_ispctrl_sp_isp_started),
			     (uint32_t)(0));

	sp_dmem_store_uint32(SP0_ID,
			     (unsigned int)sp_address_of(host_sp_queues_initialized),
			     (uint32_t)(0));
	sp_dmem_store_uint32(SP0_ID,
			     (unsigned int)sp_address_of(sp_sleep_mode),
			     (uint32_t)(0));
	sp_dmem_store_uint32(SP0_ID,
			     (unsigned int)sp_address_of(ia_css_dmaproxy_sp_invalidate_tlb),
			     (uint32_t)(false));
	sp_dmem_store_uint32(SP0_ID,
			     (unsigned int)sp_address_of(sp_stop_copy_preview),
			     my_css.stop_copy_preview ? (uint32_t)(1) : (uint32_t)(0));
	store_sp_array_uint(host_sp_com, o, host2sp_cmd_ready);

	for (i = 0; i < N_CSI_PORTS; i++) {
		sh_css_update_host2sp_num_mipi_frames
		(my_css.num_mipi_frames[i]);
	}

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "sh_css_init_host_sp_control_vars() leave: return_void\n");
}

/*
 * create the internal structures and fill in the configuration data
 */

static const struct
ia_css_pipe_config ia_css_pipe_default_config = DEFAULT_PIPE_CONFIG;

void ia_css_pipe_config_defaults(struct ia_css_pipe_config *pipe_config)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_pipe_config_defaults()\n");
	memcpy(pipe_config, &ia_css_pipe_default_config, sizeof(*pipe_config));
}

void
ia_css_pipe_extra_config_defaults(struct ia_css_pipe_extra_config *extra_config)
{
	if (!extra_config) {
		IA_CSS_ERROR("NULL input parameter");
		return;
	}

	extra_config->enable_raw_binning = false;
	extra_config->enable_yuv_ds = false;
	extra_config->enable_high_speed = false;
	extra_config->enable_dvs_6axis = false;
	extra_config->enable_reduced_pipe = false;
	extra_config->disable_vf_pp = false;
	extra_config->enable_fractional_ds = false;
}

void ia_css_stream_config_defaults(struct ia_css_stream_config *stream_config)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_stream_config_defaults()\n");
	assert(stream_config);
	memset(stream_config, 0, sizeof(*stream_config));
	stream_config->online = true;
	stream_config->left_padding = -1;
	stream_config->pixels_per_clock = 1;
	/*
	 * temporary default value for backwards compatibility.
	 * This field used to be hardcoded within CSS but this has now
	 * been moved to the stream_config struct.
	 */
	stream_config->source.port.rxcount = 0x04040404;
}

static int
ia_css_acc_pipe_create(struct ia_css_pipe *pipe)
{
	int err = 0;

	if (!pipe) {
		IA_CSS_ERROR("NULL input parameter");
		return -EINVAL;
	}

	/* There is not meaning for num_execs = 0 semantically. Run at least once. */
	if (pipe->config.acc_num_execs == 0)
		pipe->config.acc_num_execs = 1;

	if (pipe->config.acc_extension)
		err = ia_css_pipe_load_extension(pipe, pipe->config.acc_extension);

	return err;
}

int ia_css_pipe_create(const struct ia_css_pipe_config *config,
		       struct ia_css_pipe **pipe)
{
	int err = 0;

	IA_CSS_ENTER_PRIVATE("config = %p, pipe = %p", config, pipe);

	if (!config || !pipe) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	err = ia_css_pipe_create_extra(config, NULL, pipe);

	if (err == 0)
		IA_CSS_LOG("pipe created successfully = %p", *pipe);

	IA_CSS_LEAVE_ERR_PRIVATE(err);

	return err;
}

int
ia_css_pipe_create_extra(const struct ia_css_pipe_config *config,
			 const struct ia_css_pipe_extra_config *extra_config,
			 struct ia_css_pipe **pipe)
{
	int err = -EINVAL;
	struct ia_css_pipe *internal_pipe = NULL;
	unsigned int i;

	IA_CSS_ENTER_PRIVATE("config = %p, extra_config = %p and pipe = %p", config, extra_config, pipe);

	/* do not allow to create more than the maximum limit */
	if (my_css.pipe_counter >= IA_CSS_PIPELINE_NUM_MAX) {
		IA_CSS_LEAVE_ERR_PRIVATE(-ENOSPC);
		return -EINVAL;
	}

	if ((!pipe) || (!config)) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	ia_css_debug_dump_pipe_config(config);
	ia_css_debug_dump_pipe_extra_config(extra_config);

	err = create_pipe(config->mode, &internal_pipe, false);
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}

	/* now we have a pipe structure to fill */
	internal_pipe->config = *config;
	if (extra_config)
		internal_pipe->extra_config = *extra_config;
	else
		ia_css_pipe_extra_config_defaults(&internal_pipe->extra_config);

	if (config->mode == IA_CSS_PIPE_MODE_ACC) {
		/*
		 * Temporary hack to migrate acceleration to CSS 2.0.
		 * In the future the code for all pipe types should be
		 * unified.
		 */
		*pipe = internal_pipe;
		if (!internal_pipe->config.acc_extension &&
		    internal_pipe->config.num_acc_stages ==
		    0) { /* if no acc binary and no standalone stage */
			*pipe = NULL;
			IA_CSS_LEAVE_ERR_PRIVATE(0);
			return 0;
		}
		return ia_css_acc_pipe_create(internal_pipe);
	}

	/*
	 * Use config value when dvs_frame_delay setting equal to 2,
	 * otherwise always 1 by default
	 */
	if (internal_pipe->config.dvs_frame_delay == IA_CSS_FRAME_DELAY_2)
		internal_pipe->dvs_frame_delay = 2;
	else
		internal_pipe->dvs_frame_delay = 1;

	/*
	 * we still keep enable_raw_binning for backward compatibility,
	 * for any new fractional bayer downscaling, we should use
	 * bayer_ds_out_res. if both are specified, bayer_ds_out_res will
	 * take precedence.if none is specified, we set bayer_ds_out_res
	 * equal to IF output resolution(IF may do cropping on sensor output)
	 * or use default decimation factor 1.
	 */

	/* YUV downscaling */
	if ((internal_pipe->config.vf_pp_in_res.width ||
	     internal_pipe->config.capt_pp_in_res.width)) {
		enum ia_css_frame_format format;

		if (internal_pipe->config.vf_pp_in_res.width) {
			format = IA_CSS_FRAME_FORMAT_YUV_LINE;
			ia_css_frame_info_init(
			    &internal_pipe->vf_yuv_ds_input_info,
			    internal_pipe->config.vf_pp_in_res.width,
			    internal_pipe->config.vf_pp_in_res.height,
			    format, 0);
		}
		if (internal_pipe->config.capt_pp_in_res.width) {
			format = IA_CSS_FRAME_FORMAT_YUV420;
			ia_css_frame_info_init(
			    &internal_pipe->out_yuv_ds_input_info,
			    internal_pipe->config.capt_pp_in_res.width,
			    internal_pipe->config.capt_pp_in_res.height,
			    format, 0);
		}
	}
	if (internal_pipe->config.vf_pp_in_res.width &&
	    internal_pipe->config.mode == IA_CSS_PIPE_MODE_PREVIEW) {
		ia_css_frame_info_init(
		    &internal_pipe->vf_yuv_ds_input_info,
		    internal_pipe->config.vf_pp_in_res.width,
		    internal_pipe->config.vf_pp_in_res.height,
		    IA_CSS_FRAME_FORMAT_YUV_LINE, 0);
	}
	/* handle bayer downscaling output info */
	if (internal_pipe->config.bayer_ds_out_res.width) {
		ia_css_frame_info_init(
		    &internal_pipe->bds_output_info,
		    internal_pipe->config.bayer_ds_out_res.width,
		    internal_pipe->config.bayer_ds_out_res.height,
		    IA_CSS_FRAME_FORMAT_RAW, 0);
	}

	/* handle output info, assume always needed */
	for (i = 0; i < IA_CSS_PIPE_MAX_OUTPUT_STAGE; i++) {
		if (internal_pipe->config.output_info[i].res.width) {
			err = sh_css_pipe_configure_output(
				    internal_pipe,
				    internal_pipe->config.output_info[i].res.width,
				    internal_pipe->config.output_info[i].res.height,
				    internal_pipe->config.output_info[i].padded_width,
				    internal_pipe->config.output_info[i].format,
				    i);
			if (err) {
				IA_CSS_LEAVE_ERR_PRIVATE(err);
				kvfree(internal_pipe);
				internal_pipe = NULL;
				return err;
			}
		}

		/* handle vf output info, when configured */
		internal_pipe->enable_viewfinder[i] =
		    (internal_pipe->config.vf_output_info[i].res.width != 0);
		if (internal_pipe->config.vf_output_info[i].res.width) {
			err = sh_css_pipe_configure_viewfinder(
				    internal_pipe,
				    internal_pipe->config.vf_output_info[i].res.width,
				    internal_pipe->config.vf_output_info[i].res.height,
				    internal_pipe->config.vf_output_info[i].padded_width,
				    internal_pipe->config.vf_output_info[i].format,
				    i);
			if (err) {
				IA_CSS_LEAVE_ERR_PRIVATE(err);
				kvfree(internal_pipe);
				internal_pipe = NULL;
				return err;
			}
		}
	}
	if (internal_pipe->config.acc_extension) {
		err = ia_css_pipe_load_extension(internal_pipe,
						 internal_pipe->config.acc_extension);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			kvfree(internal_pipe);
			return err;
		}
	}
	/* set all info to zeroes first */
	memset(&internal_pipe->info, 0, sizeof(internal_pipe->info));

	/* all went well, return the pipe */
	*pipe = internal_pipe;
	IA_CSS_LEAVE_ERR_PRIVATE(0);
	return 0;
}

int
ia_css_pipe_get_info(const struct ia_css_pipe *pipe,
		     struct ia_css_pipe_info *pipe_info)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_pipe_get_info()\n");
	if (!pipe_info) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_ERROR,
				    "ia_css_pipe_get_info: pipe_info cannot be NULL\n");
		return -EINVAL;
	}
	if (!pipe || !pipe->stream) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_ERROR,
				    "ia_css_pipe_get_info: ia_css_stream_create needs to be called before ia_css_[stream/pipe]_get_info\n");
		return -EINVAL;
	}
	/* we succeeded return the info */
	*pipe_info = pipe->info;
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_pipe_get_info() leave\n");
	return 0;
}

bool ia_css_pipe_has_dvs_stats(struct ia_css_pipe_info *pipe_info)
{
	unsigned int i;

	if (pipe_info) {
		for (i = 0; i < IA_CSS_DVS_STAT_NUM_OF_LEVELS; i++) {
			if (pipe_info->grid_info.dvs_grid.dvs_stat_grid_info.grd_cfg[i].grd_start.enable)
				return true;
		}
	}

	return false;
}

int
ia_css_pipe_override_frame_format(struct ia_css_pipe *pipe,
				  int pin_index,
				  enum ia_css_frame_format new_format)
{
	int err = 0;

	IA_CSS_ENTER_PRIVATE("pipe = %p, pin_index = %d, new_formats = %d", pipe, pin_index, new_format);

	if (!pipe) {
		IA_CSS_ERROR("pipe is not set");
		err = -EINVAL;
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}
	if (0 != pin_index && 1 != pin_index) {
		IA_CSS_ERROR("pin index is not valid");
		err = -EINVAL;
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}
	if (new_format != IA_CSS_FRAME_FORMAT_NV12_TILEY) {
		IA_CSS_ERROR("new format is not valid");
		err = -EINVAL;
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	} else {
		err = ia_css_pipe_check_format(pipe, new_format);
		if (!err) {
			if (pin_index == 0)
				pipe->output_info[0].format = new_format;
			else
				pipe->vf_output_info[0].format = new_format;
		}
	}
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

#if !defined(ISP2401)
/* Configuration of INPUT_SYSTEM_VERSION_2401 is done on SP */
static int
ia_css_stream_configure_rx(struct ia_css_stream *stream)
{
	struct ia_css_input_port *config;

	assert(stream);

	config = &stream->config.source.port;
	/* AM: this code is not reliable, especially for 2400 */
	if (config->num_lanes == 1)
		stream->csi_rx_config.mode = MONO_1L_1L_0L;
	else if (config->num_lanes == 2)
		stream->csi_rx_config.mode = MONO_2L_1L_0L;
	else if (config->num_lanes == 3)
		stream->csi_rx_config.mode = MONO_3L_1L_0L;
	else if (config->num_lanes == 4)
		stream->csi_rx_config.mode = MONO_4L_1L_0L;
	else if (config->num_lanes != 0)
		return -EINVAL;

	if (config->port > MIPI_PORT2_ID)
		return -EINVAL;
	stream->csi_rx_config.port =
	ia_css_isys_port_to_mipi_port(config->port);
	stream->csi_rx_config.timeout    = config->timeout;
	stream->csi_rx_config.initcount  = 0;
	stream->csi_rx_config.synccount  = 0x28282828;
	stream->csi_rx_config.rxcount    = config->rxcount;
	if (config->compression.type == IA_CSS_CSI2_COMPRESSION_TYPE_NONE)
		stream->csi_rx_config.comp = MIPI_PREDICTOR_NONE;
	else
		/*
		 * not implemented yet, requires extension of the rx_cfg_t
		 * struct
		 */
		return -EINVAL;

	stream->csi_rx_config.is_two_ppc = (stream->config.pixels_per_clock == 2);
	stream->reconfigure_css_rx = true;
	return 0;
}
#endif

static struct ia_css_pipe *
find_pipe(struct ia_css_pipe *pipes[], unsigned int num_pipes,
	  enum ia_css_pipe_mode mode, bool copy_pipe)
{
	unsigned int i;

	assert(pipes);
	for (i = 0; i < num_pipes; i++) {
		assert(pipes[i]);
		if (pipes[i]->config.mode != mode)
			continue;
		if (copy_pipe && pipes[i]->mode != IA_CSS_PIPE_ID_COPY)
			continue;
		return pipes[i];
	}
	return NULL;
}

static int
ia_css_acc_stream_create(struct ia_css_stream *stream)
{
	int i;
	int err = 0;

	IA_CSS_ENTER_PRIVATE("stream = %p", stream);

	if (!stream) {
		IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
		return -EINVAL;
	}

	for (i = 0;  i < stream->num_pipes; i++) {
		struct ia_css_pipe *pipe = stream->pipes[i];

		if (!pipe) {
			IA_CSS_LEAVE_ERR_PRIVATE(-EINVAL);
			return -EINVAL;
		}

		pipe->stream = stream;
	}

	/* Map SP threads before doing anything. */
	err = map_sp_threads(stream, true);
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}

	for (i = 0;  i < stream->num_pipes; i++) {
		struct ia_css_pipe *pipe = stream->pipes[i];

		assert(pipe);
		ia_css_pipe_map_queue(pipe, true);
	}

	err = create_host_pipeline_structure(stream);
	if (err) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}

	stream->started = false;

	IA_CSS_LEAVE_ERR_PRIVATE(0);

	return 0;
}

static int
metadata_info_init(const struct ia_css_metadata_config *mdc,
		   struct ia_css_metadata_info *md)
{
	/* Either both width and height should be set or neither */
	if ((mdc->resolution.height > 0) ^ (mdc->resolution.width > 0))
		return -EINVAL;

	md->resolution = mdc->resolution;
	/*
	 * We round up the stride to a multiple of the width
	 * of the port going to DDR, this is a HW requirements (DMA).
	 */
	md->stride = CEIL_MUL(mdc->resolution.width, HIVE_ISP_DDR_WORD_BYTES);
	md->size = mdc->resolution.height * md->stride;
	return 0;
}

int
ia_css_stream_create(const struct ia_css_stream_config *stream_config,
		     int num_pipes,
		     struct ia_css_pipe *pipes[],
		     struct ia_css_stream **stream)
{
	struct ia_css_pipe *curr_pipe;
	struct ia_css_stream *curr_stream = NULL;
	bool spcopyonly;
	bool sensor_binning_changed;
	int i, j;
	int err = -EINVAL;
	struct ia_css_metadata_info md_info;
	struct ia_css_resolution effective_res;

	IA_CSS_ENTER("num_pipes=%d", num_pipes);
	ia_css_debug_dump_stream_config(stream_config, num_pipes);

	/* some checks */
	if (num_pipes == 0 ||
	    !stream ||
	    !pipes) {
		err = -EINVAL;
		IA_CSS_LEAVE_ERR(err);
		return err;
	}

#if !defined(ISP2401)
	/* We don't support metadata for JPEG stream, since they both use str2mem */
	if (stream_config->input_config.format == ATOMISP_INPUT_FORMAT_BINARY_8 &&
	    stream_config->metadata_config.resolution.height > 0) {
		err = -EINVAL;
		IA_CSS_LEAVE_ERR(err);
		return err;
	}
#endif

#ifdef ISP2401
	if (stream_config->online && stream_config->pack_raw_pixels) {
		IA_CSS_LOG("online and pack raw is invalid on input system 2401");
		err = -EINVAL;
		IA_CSS_LEAVE_ERR(err);
		return err;
	}
#endif

	ia_css_debug_pipe_graph_dump_stream_config(stream_config);

	/* check if mipi size specified */
	if (stream_config->mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR)
#ifdef ISP2401
		if (!stream_config->online)
#endif
		{
			unsigned int port = (unsigned int)stream_config->source.port.port;

			if (port >= N_MIPI_PORT_ID) {
				err = -EINVAL;
				IA_CSS_LEAVE_ERR(err);
				return err;
			}

			if (my_css.size_mem_words != 0) {
				my_css.mipi_frame_size[port] = my_css.size_mem_words;
			} else if (stream_config->mipi_buffer_config.size_mem_words != 0) {
				my_css.mipi_frame_size[port] = stream_config->mipi_buffer_config.size_mem_words;
			} else {
				ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
						    "ia_css_stream_create() exit: error, need to set mipi frame size.\n");
				assert(stream_config->mipi_buffer_config.size_mem_words != 0);
				err = -EINVAL;
				IA_CSS_LEAVE_ERR(err);
				return err;
			}

			if (my_css.size_mem_words != 0) {
				my_css.num_mipi_frames[port] =
				    2; /* Temp change: Default for backwards compatibility. */
			} else if (stream_config->mipi_buffer_config.nof_mipi_buffers != 0) {
				my_css.num_mipi_frames[port] =
				    stream_config->mipi_buffer_config.nof_mipi_buffers;
			} else {
				ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
						    "ia_css_stream_create() exit: error, need to set number of mipi frames.\n");
				assert(stream_config->mipi_buffer_config.nof_mipi_buffers != 0);
				err = -EINVAL;
				IA_CSS_LEAVE_ERR(err);
				return err;
			}
		}

	/* Currently we only supported metadata up to a certain size. */
	err = metadata_info_init(&stream_config->metadata_config, &md_info);
	if (err) {
		IA_CSS_LEAVE_ERR(err);
		return err;
	}

	/* allocate the stream instance */
	curr_stream = kzalloc(sizeof(struct ia_css_stream), GFP_KERNEL);
	if (!curr_stream) {
		err = -ENOMEM;
		IA_CSS_LEAVE_ERR(err);
		return err;
	}
	/* default all to 0 */
	curr_stream->info.metadata_info = md_info;

	/* allocate pipes */
	curr_stream->num_pipes = num_pipes;
	curr_stream->pipes = kcalloc(num_pipes, sizeof(struct ia_css_pipe *), GFP_KERNEL);
	if (!curr_stream->pipes) {
		curr_stream->num_pipes = 0;
		kfree(curr_stream);
		curr_stream = NULL;
		err = -ENOMEM;
		IA_CSS_LEAVE_ERR(err);
		return err;
	}
	/* store pipes */
	spcopyonly = (num_pipes == 1) && (pipes[0]->config.mode == IA_CSS_PIPE_MODE_COPY);
	for (i = 0; i < num_pipes; i++)
		curr_stream->pipes[i] = pipes[i];
	curr_stream->last_pipe = curr_stream->pipes[0];
	/* take over stream config */
	curr_stream->config = *stream_config;

#if defined(ISP2401)
	if (stream_config->mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR &&
	    stream_config->online)
		curr_stream->config.online = false;
#endif

#ifdef ISP2401
	if (curr_stream->config.online) {
		curr_stream->config.source.port.num_lanes =
		    stream_config->source.port.num_lanes;
		curr_stream->config.mode =  IA_CSS_INPUT_MODE_BUFFERED_SENSOR;
	}
#endif
	/* in case driver doesn't configure init number of raw buffers, configure it here */
	if (curr_stream->config.target_num_cont_raw_buf == 0)
		curr_stream->config.target_num_cont_raw_buf = NUM_CONTINUOUS_FRAMES;
	if (curr_stream->config.init_num_cont_raw_buf == 0)
		curr_stream->config.init_num_cont_raw_buf = curr_stream->config.target_num_cont_raw_buf;

	/* Enable locking & unlocking of buffers in RAW buffer pool */
	if (curr_stream->config.ia_css_enable_raw_buffer_locking)
		sh_css_sp_configure_enable_raw_pool_locking(
		    curr_stream->config.lock_all);

	/* copy mode specific stuff */
	switch (curr_stream->config.mode) {
	case IA_CSS_INPUT_MODE_SENSOR:
	case IA_CSS_INPUT_MODE_BUFFERED_SENSOR:
#if !defined(ISP2401)
		ia_css_stream_configure_rx(curr_stream);
#endif
		break;
	case IA_CSS_INPUT_MODE_TPG:
#if !defined(ISP2401)
		IA_CSS_LOG("tpg_configuration: x_mask=%d, y_mask=%d, x_delta=%d, y_delta=%d, xy_mask=%d",
			   curr_stream->config.source.tpg.x_mask,
			   curr_stream->config.source.tpg.y_mask,
			   curr_stream->config.source.tpg.x_delta,
			   curr_stream->config.source.tpg.y_delta,
			   curr_stream->config.source.tpg.xy_mask);

		sh_css_sp_configure_tpg(
		    curr_stream->config.source.tpg.x_mask,
		    curr_stream->config.source.tpg.y_mask,
		    curr_stream->config.source.tpg.x_delta,
		    curr_stream->config.source.tpg.y_delta,
		    curr_stream->config.source.tpg.xy_mask);
#endif
		break;
	case IA_CSS_INPUT_MODE_PRBS:
#if !defined(ISP2401)
		IA_CSS_LOG("mode prbs");
		sh_css_sp_configure_prbs(curr_stream->config.source.prbs.seed);
#endif
		break;
	case IA_CSS_INPUT_MODE_MEMORY:
		IA_CSS_LOG("mode memory");
		curr_stream->reconfigure_css_rx = false;
		break;
	default:
		IA_CSS_LOG("mode sensor/default");
	}

	for (i = 0; i < num_pipes; i++) {
		struct ia_css_resolution effective_res;

		curr_pipe = pipes[i];
		/* set current stream */
		curr_pipe->stream = curr_stream;
		/* take over effective info */

		effective_res = curr_pipe->config.input_effective_res;
		if (effective_res.height == 0 || effective_res.width == 0) {
			effective_res = curr_pipe->stream->config.input_config.effective_res;

			curr_pipe->config.input_effective_res = effective_res;
		}
		IA_CSS_LOG("effective_res=%dx%d",
			   effective_res.width,
			   effective_res.height);
	}

	err = ia_css_stream_isp_parameters_init(curr_stream);
	if (err)
		goto ERR;
	IA_CSS_LOG("isp_params_configs: %p", curr_stream->isp_params_configs);

	if (num_pipes == 1 && pipes[0]->config.mode == IA_CSS_PIPE_MODE_ACC) {
		*stream = curr_stream;
		err = ia_css_acc_stream_create(curr_stream);
		goto ERR;
	}
	/* sensor binning */
	if (!spcopyonly) {
		sensor_binning_changed =
		    sh_css_params_set_binning_factor(curr_stream,
						     curr_stream->config.sensor_binning_factor);
	} else {
		sensor_binning_changed = false;
	}

	IA_CSS_LOG("sensor_binning=%d, changed=%d",
		   curr_stream->config.sensor_binning_factor, sensor_binning_changed);
	/* loop over pipes */
	IA_CSS_LOG("num_pipes=%d", num_pipes);
	curr_stream->cont_capt = false;
	/* Temporary hack: we give the preview pipe a reference to the capture
	    * pipe in continuous capture mode. */
	if (curr_stream->config.continuous) {
		/* Search for the preview pipe and create the copy pipe */
		struct ia_css_pipe *preview_pipe;
		struct ia_css_pipe *video_pipe;
		struct ia_css_pipe *acc_pipe;
		struct ia_css_pipe *capture_pipe = NULL;
		struct ia_css_pipe *copy_pipe = NULL;

		if (num_pipes >= 2) {
			curr_stream->cont_capt = true;
			curr_stream->disable_cont_vf = curr_stream->config.disable_cont_viewfinder;
			curr_stream->stop_copy_preview = my_css.stop_copy_preview;
		}

		/* Create copy pipe here, since it may not be exposed to the driver */
		preview_pipe = find_pipe(pipes, num_pipes,
					 IA_CSS_PIPE_MODE_PREVIEW, false);
		video_pipe = find_pipe(pipes, num_pipes,
				       IA_CSS_PIPE_MODE_VIDEO, false);
		acc_pipe = find_pipe(pipes, num_pipes, IA_CSS_PIPE_MODE_ACC,
				     false);
		if (acc_pipe && num_pipes == 2 && curr_stream->cont_capt)
			curr_stream->cont_capt =
			    false; /* preview + QoS case will not need cont_capt switch */
		if (curr_stream->cont_capt) {
			capture_pipe = find_pipe(pipes, num_pipes,
						 IA_CSS_PIPE_MODE_CAPTURE,
						 false);
			if (!capture_pipe) {
				err = -EINVAL;
				goto ERR;
			}
		}
		/* We do not support preview and video pipe at the same time */
		if (preview_pipe && video_pipe) {
			err = -EINVAL;
			goto ERR;
		}

		if (preview_pipe && !preview_pipe->pipe_settings.preview.copy_pipe) {
			err = create_pipe(IA_CSS_PIPE_MODE_CAPTURE, &copy_pipe, true);
			if (err)
				goto ERR;
			ia_css_pipe_config_defaults(&copy_pipe->config);
			preview_pipe->pipe_settings.preview.copy_pipe = copy_pipe;
			copy_pipe->stream = curr_stream;
		}
		if (preview_pipe && curr_stream->cont_capt)
			preview_pipe->pipe_settings.preview.capture_pipe = capture_pipe;

		if (video_pipe && !video_pipe->pipe_settings.video.copy_pipe) {
			err = create_pipe(IA_CSS_PIPE_MODE_CAPTURE, &copy_pipe, true);
			if (err)
				goto ERR;
			ia_css_pipe_config_defaults(&copy_pipe->config);
			video_pipe->pipe_settings.video.copy_pipe = copy_pipe;
			copy_pipe->stream = curr_stream;
		}
		if (video_pipe && curr_stream->cont_capt)
			video_pipe->pipe_settings.video.capture_pipe = capture_pipe;

		if (preview_pipe && acc_pipe)
			preview_pipe->pipe_settings.preview.acc_pipe = acc_pipe;
	}
	for (i = 0; i < num_pipes; i++) {
		curr_pipe = pipes[i];
		/* set current stream */
		curr_pipe->stream = curr_stream;

		/* take over effective info */

		effective_res = curr_pipe->config.input_effective_res;
		err = ia_css_util_check_res(
			effective_res.width,
			effective_res.height);
		if (err)
			goto ERR;

		/* sensor binning per pipe */
		if (sensor_binning_changed)
			sh_css_pipe_free_shading_table(curr_pipe);
	}

	/* now pipes have been configured, info should be available */
	for (i = 0; i < num_pipes; i++) {
		struct ia_css_pipe_info *pipe_info = NULL;

		curr_pipe = pipes[i];

		err = sh_css_pipe_load_binaries(curr_pipe);
		if (err)
			goto ERR;

		/* handle each pipe */
		pipe_info = &curr_pipe->info;
		for (j = 0; j < IA_CSS_PIPE_MAX_OUTPUT_STAGE; j++) {
			err = sh_css_pipe_get_output_frame_info(curr_pipe,
								&pipe_info->output_info[j], j);
			if (err)
				goto ERR;
		}

		if (!spcopyonly) {
			if (!IS_ISP2401)
				err = sh_css_pipe_get_shading_info(curr_pipe,
								   &pipe_info->shading_info,
								   NULL);
			else
				err = sh_css_pipe_get_shading_info(curr_pipe,
								   &pipe_info->shading_info,
								   &curr_pipe->config);

			if (err)
				goto ERR;
			err = sh_css_pipe_get_grid_info(curr_pipe,
							&pipe_info->grid_info);
			if (err)
				goto ERR;
			for (j = 0; j < IA_CSS_PIPE_MAX_OUTPUT_STAGE; j++) {
				sh_css_pipe_get_viewfinder_frame_info(curr_pipe,
								      &pipe_info->vf_output_info[j],
								      j);
				if (err)
					goto ERR;
			}
		}

		my_css.active_pipes[ia_css_pipe_get_pipe_num(curr_pipe)] = curr_pipe;
	}

	curr_stream->started = false;

	/* Map SP threads before doing anything. */
	err = map_sp_threads(curr_stream, true);
	if (err) {
		IA_CSS_LOG("map_sp_threads: return_err=%d", err);
		goto ERR;
	}

	for (i = 0; i < num_pipes; i++) {
		curr_pipe = pipes[i];
		ia_css_pipe_map_queue(curr_pipe, true);
	}

	/* Create host side pipeline objects without stages */
	err = create_host_pipeline_structure(curr_stream);
	if (err) {
		IA_CSS_LOG("create_host_pipeline_structure: return_err=%d", err);
		goto ERR;
	}

	/* assign curr_stream */
	*stream = curr_stream;

ERR:
	if (!err) {
		/* working mode: enter into the seed list */
		if (my_css_save.mode == sh_css_mode_working) {
			for (i = 0; i < MAX_ACTIVE_STREAMS; i++) {
				if (!my_css_save.stream_seeds[i].stream) {
					IA_CSS_LOG("entered stream into loc=%d", i);
					my_css_save.stream_seeds[i].orig_stream = stream;
					my_css_save.stream_seeds[i].stream = curr_stream;
					my_css_save.stream_seeds[i].num_pipes = num_pipes;
					my_css_save.stream_seeds[i].stream_config = *stream_config;
					for (j = 0; j < num_pipes; j++) {
						my_css_save.stream_seeds[i].pipe_config[j] = pipes[j]->config;
						my_css_save.stream_seeds[i].pipes[j] = pipes[j];
						my_css_save.stream_seeds[i].orig_pipes[j] = &pipes[j];
					}
					break;
				}
			}
		} else {
			ia_css_stream_destroy(curr_stream);
		}
	} else {
		ia_css_stream_destroy(curr_stream);
	}
	IA_CSS_LEAVE("return_err=%d mode=%d", err, my_css_save.mode);
	return err;
}

int
ia_css_stream_destroy(struct ia_css_stream *stream)
{
	int i;
	int err = 0;

	IA_CSS_ENTER_PRIVATE("stream = %p", stream);
	if (!stream) {
		err = -EINVAL;
		IA_CSS_LEAVE_ERR_PRIVATE(err);
		return err;
	}

	ia_css_stream_isp_parameters_uninit(stream);

	if ((stream->last_pipe) &&
	    ia_css_pipeline_is_mapped(stream->last_pipe->pipe_num)) {
#if defined(ISP2401)
		for (i = 0; i < stream->num_pipes; i++) {
			struct ia_css_pipe *entry = stream->pipes[i];
			unsigned int sp_thread_id;
			struct sh_css_sp_pipeline_terminal *sp_pipeline_input_terminal;

			assert(entry);
			if (entry) {
				/* get the SP thread id */
				if (!ia_css_pipeline_get_sp_thread_id(
					ia_css_pipe_get_pipe_num(entry), &sp_thread_id))
					return -EINVAL;
				/* get the target input terminal */
				sp_pipeline_input_terminal =
				&sh_css_sp_group.pipe_io[sp_thread_id].input;

				for (i = 0; i < IA_CSS_STREAM_MAX_ISYS_STREAM_PER_CH; i++) {
					ia_css_isys_stream_h isys_stream =
					&sp_pipeline_input_terminal->context.virtual_input_system_stream[i];
					if (stream->config.isys_config[i].valid && isys_stream->valid)
						ia_css_isys_stream_destroy(isys_stream);
				}
			}
		}
		if (stream->config.mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR) {
			for (i = 0; i < stream->num_pipes; i++) {
				struct ia_css_pipe *entry = stream->pipes[i];
				/*
				 * free any mipi frames that are remaining:
				 * some test stream create-destroy cycles do
				 * not generate output frames
				 * and the mipi buffer is not freed in the
				 * deque function
				 */
				if (entry)
					free_mipi_frames(entry);
			}
		}
		stream_unregister_with_csi_rx(stream);
#endif

		for (i = 0; i < stream->num_pipes; i++) {
			struct ia_css_pipe *curr_pipe = stream->pipes[i];

			assert(curr_pipe);
			ia_css_pipe_map_queue(curr_pipe, false);
		}

		err = map_sp_threads(stream, false);
		if (err) {
			IA_CSS_LEAVE_ERR_PRIVATE(err);
			return err;
		}
	}

	/* remove references from pipes to stream */
	for (i = 0; i < stream->num_pipes; i++) {
		struct ia_css_pipe *entry = stream->pipes[i];

		assert(entry);
		if (entry) {
			/* clear reference to stream */
			entry->stream = NULL;
			/* check internal copy pipe */
			if (entry->mode == IA_CSS_PIPE_ID_PREVIEW &&
			    entry->pipe_settings.preview.copy_pipe) {
				IA_CSS_LOG("clearing stream on internal preview copy pipe");
				entry->pipe_settings.preview.copy_pipe->stream = NULL;
			}
			if (entry->mode == IA_CSS_PIPE_ID_VIDEO &&
			    entry->pipe_settings.video.copy_pipe) {
				IA_CSS_LOG("clearing stream on internal video copy pipe");
				entry->pipe_settings.video.copy_pipe->stream = NULL;
			}
			err = sh_css_pipe_unload_binaries(entry);
		}
	}
	/* free associated memory of stream struct */
	kfree(stream->pipes);
	stream->pipes = NULL;
	stream->num_pipes = 0;

	/* working mode: take out of the seed list */
	if (my_css_save.mode == sh_css_mode_working) {
		for (i = 0; i < MAX_ACTIVE_STREAMS; i++) {
			if (my_css_save.stream_seeds[i].stream == stream) {
				IA_CSS_LOG("took out stream %d", i);
				my_css_save.stream_seeds[i].stream = NULL;
				break;
			}
		}
	}

	kfree(stream);
	IA_CSS_LEAVE_ERR(err);

	return err;
}

int
ia_css_stream_get_info(const struct ia_css_stream *stream,
		       struct ia_css_stream_info *stream_info)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_stream_get_info: enter/exit\n");
	assert(stream);
	assert(stream_info);

	*stream_info = stream->info;
	return 0;
}

int
ia_css_stream_start(struct ia_css_stream *stream)
{
	int err = 0;

	IA_CSS_ENTER("stream = %p", stream);
	if ((!stream) || (!stream->last_pipe)) {
		IA_CSS_LEAVE_ERR(-EINVAL);
		return -EINVAL;
	}
	IA_CSS_LOG("starting %d", stream->last_pipe->mode);

	sh_css_sp_set_disable_continuous_viewfinder(stream->disable_cont_vf);

	/* Create host side pipeline. */
	err = create_host_pipeline(stream);
	if (err) {
		IA_CSS_LEAVE_ERR(err);
		return err;
	}

#if defined(ISP2401)
	if ((stream->config.mode == IA_CSS_INPUT_MODE_SENSOR) ||
	    (stream->config.mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR))
		stream_register_with_csi_rx(stream);
#endif

#if !defined(ISP2401)
	/* Initialize mipi size checks */
	if (stream->config.mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR) {
		unsigned int idx;
		unsigned int port = (unsigned int)(stream->config.source.port.port);

		for (idx = 0; idx < IA_CSS_MIPI_SIZE_CHECK_MAX_NOF_ENTRIES_PER_PORT; idx++) {
			sh_css_sp_group.config.mipi_sizes_for_check[port][idx] =
			sh_css_get_mipi_sizes_for_check(port, idx);
		}
	}
#endif

	if (stream->config.mode != IA_CSS_INPUT_MODE_MEMORY) {
		err = sh_css_config_input_network(stream);
		if (err)
			return err;
	}

	err = sh_css_pipe_start(stream);
	IA_CSS_LEAVE_ERR(err);
	return err;
}

int
ia_css_stream_stop(struct ia_css_stream *stream)
{
	int err = 0;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_stream_stop() enter/exit\n");
	assert(stream);
	assert(stream->last_pipe);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_stream_stop: stopping %d\n",
			    stream->last_pipe->mode);

#if !defined(ISP2401)
	/* De-initialize mipi size checks */
	if (stream->config.mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR) {
		unsigned int idx;
		unsigned int port = (unsigned int)(stream->config.source.port.port);

		for (idx = 0; idx < IA_CSS_MIPI_SIZE_CHECK_MAX_NOF_ENTRIES_PER_PORT; idx++)
			sh_css_sp_group.config.mipi_sizes_for_check[port][idx] = 0;
	}
#endif

	err = ia_css_pipeline_request_stop(&stream->last_pipe->pipeline);
	if (err)
		return err;

	/*
	 * Ideally, unmapping should happen after pipeline_stop, but current
	 * semantics do not allow that.
	 */
	/* err = map_sp_threads(stream, false); */

	return err;
}

bool
ia_css_stream_has_stopped(struct ia_css_stream *stream)
{
	bool stopped;

	assert(stream);

	stopped = ia_css_pipeline_has_stopped(&stream->last_pipe->pipeline);

	return stopped;
}

/* ISP2400 */
/*
 * Destroy the stream and all the pipes related to it.
 * The stream handle is used to identify the correct entry in the css_save struct
 */
int
ia_css_stream_unload(struct ia_css_stream *stream)
{
	int i;

	assert(stream);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,	"ia_css_stream_unload() enter,\n");
	/* some checks */
	assert(stream);
	for (i = 0; i < MAX_ACTIVE_STREAMS; i++)
		if (my_css_save.stream_seeds[i].stream == stream) {
			int j;

			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
					    "ia_css_stream_unload(): unloading %d (%p)\n", i,
					    my_css_save.stream_seeds[i].stream);
			ia_css_stream_destroy(stream);
			for (j = 0; j < my_css_save.stream_seeds[i].num_pipes; j++)
				ia_css_pipe_destroy(my_css_save.stream_seeds[i].pipes[j]);
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
					    "ia_css_stream_unload(): after unloading %d (%p)\n", i,
					    my_css_save.stream_seeds[i].stream);
			break;
		}
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,	"ia_css_stream_unload() exit,\n");
	return 0;
}

int
ia_css_temp_pipe_to_pipe_id(const struct ia_css_pipe *pipe,
			    enum ia_css_pipe_id *pipe_id)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_temp_pipe_to_pipe_id() enter/exit\n");
	if (pipe)
		*pipe_id = pipe->mode;
	else
		*pipe_id = IA_CSS_PIPE_ID_COPY;

	return 0;
}

enum atomisp_input_format
ia_css_stream_get_format(const struct ia_css_stream *stream)
{
	return stream->config.input_config.format;
}

bool
ia_css_stream_get_two_pixels_per_clock(const struct ia_css_stream *stream)
{
	return (stream->config.pixels_per_clock == 2);
}

struct ia_css_binary *
ia_css_stream_get_shading_correction_binary(const struct ia_css_stream
	*stream)
{
	struct ia_css_pipe *pipe;

	assert(stream);

	pipe = stream->pipes[0];

	if (stream->num_pipes == 2) {
		assert(stream->pipes[1]);
		if (stream->pipes[1]->config.mode == IA_CSS_PIPE_MODE_VIDEO ||
		    stream->pipes[1]->config.mode == IA_CSS_PIPE_MODE_PREVIEW)
			pipe = stream->pipes[1];
	}

	return ia_css_pipe_get_shading_correction_binary(pipe);
}

struct ia_css_binary *
ia_css_stream_get_dvs_binary(const struct ia_css_stream *stream)
{
	int i;
	struct ia_css_pipe *video_pipe = NULL;

	/* First we find the video pipe */
	for (i = 0; i < stream->num_pipes; i++) {
		struct ia_css_pipe *pipe = stream->pipes[i];

		if (pipe->config.mode == IA_CSS_PIPE_MODE_VIDEO) {
			video_pipe = pipe;
			break;
		}
	}
	if (video_pipe)
		return &video_pipe->pipe_settings.video.video_binary;
	return NULL;
}

struct ia_css_binary *
ia_css_stream_get_3a_binary(const struct ia_css_stream *stream)
{
	struct ia_css_pipe *pipe;
	struct ia_css_binary *s3a_binary = NULL;

	assert(stream);

	pipe = stream->pipes[0];

	if (stream->num_pipes == 2) {
		assert(stream->pipes[1]);
		if (stream->pipes[1]->config.mode == IA_CSS_PIPE_MODE_VIDEO ||
		    stream->pipes[1]->config.mode == IA_CSS_PIPE_MODE_PREVIEW)
			pipe = stream->pipes[1];
	}

	s3a_binary = ia_css_pipe_get_s3a_binary(pipe);

	return s3a_binary;
}

int
ia_css_stream_set_output_padded_width(struct ia_css_stream *stream,
				      unsigned int output_padded_width)
{
	struct ia_css_pipe *pipe;

	assert(stream);

	pipe = stream->last_pipe;

	assert(pipe);

	/* set the config also just in case (redundant info? why do we save config in pipe?) */
	pipe->config.output_info[IA_CSS_PIPE_OUTPUT_STAGE_0].padded_width = output_padded_width;
	pipe->output_info[IA_CSS_PIPE_OUTPUT_STAGE_0].padded_width = output_padded_width;

	return 0;
}

static struct ia_css_binary *
ia_css_pipe_get_shading_correction_binary(const struct ia_css_pipe *pipe)
{
	struct ia_css_binary *binary = NULL;

	assert(pipe);

	switch (pipe->config.mode) {
	case IA_CSS_PIPE_MODE_PREVIEW:
		binary = (struct ia_css_binary *)&pipe->pipe_settings.preview.preview_binary;
		break;
	case IA_CSS_PIPE_MODE_VIDEO:
		binary = (struct ia_css_binary *)&pipe->pipe_settings.video.video_binary;
		break;
	case IA_CSS_PIPE_MODE_CAPTURE:
		if (pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_PRIMARY) {
			unsigned int i;

			for (i = 0; i < pipe->pipe_settings.capture.num_primary_stage; i++) {
				if (pipe->pipe_settings.capture.primary_binary[i].info->sp.enable.sc) {
					binary = (struct ia_css_binary *)&pipe->pipe_settings.capture.primary_binary[i];
					break;
				}
			}
		} else if (pipe->config.default_capture_config.mode ==
			    IA_CSS_CAPTURE_MODE_BAYER)
			binary = (struct ia_css_binary *)&pipe->pipe_settings.capture.pre_isp_binary;
		else if (pipe->config.default_capture_config.mode ==
			    IA_CSS_CAPTURE_MODE_ADVANCED ||
			    pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_LOW_LIGHT) {
			if (pipe->config.isp_pipe_version == IA_CSS_PIPE_VERSION_1)
				binary = (struct ia_css_binary *)&pipe->pipe_settings.capture.pre_isp_binary;
			else if (pipe->config.isp_pipe_version == IA_CSS_PIPE_VERSION_2_2)
				binary = (struct ia_css_binary *)&pipe->pipe_settings.capture.post_isp_binary;
		}
		break;
	default:
		break;
	}

	if (binary && binary->info->sp.enable.sc)
		return binary;

	return NULL;
}

static struct ia_css_binary *
ia_css_pipe_get_s3a_binary(const struct ia_css_pipe *pipe)
{
	struct ia_css_binary *binary = NULL;

	assert(pipe);

	switch (pipe->config.mode) {
	case IA_CSS_PIPE_MODE_PREVIEW:
		binary = (struct ia_css_binary *)&pipe->pipe_settings.preview.preview_binary;
		break;
	case IA_CSS_PIPE_MODE_VIDEO:
		binary = (struct ia_css_binary *)&pipe->pipe_settings.video.video_binary;
		break;
	case IA_CSS_PIPE_MODE_CAPTURE:
		if (pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_PRIMARY) {
			unsigned int i;

			for (i = 0; i < pipe->pipe_settings.capture.num_primary_stage; i++) {
				if (pipe->pipe_settings.capture.primary_binary[i].info->sp.enable.s3a) {
					binary = (struct ia_css_binary *)&pipe->pipe_settings.capture.primary_binary[i];
					break;
				}
			}
		} else if (pipe->config.default_capture_config.mode ==
			    IA_CSS_CAPTURE_MODE_BAYER) {
			binary = (struct ia_css_binary *)&pipe->pipe_settings.capture.pre_isp_binary;
		} else if (pipe->config.default_capture_config.mode ==
			    IA_CSS_CAPTURE_MODE_ADVANCED ||
			    pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_LOW_LIGHT) {
			if (pipe->config.isp_pipe_version == IA_CSS_PIPE_VERSION_1)
				binary = (struct ia_css_binary *)&pipe->pipe_settings.capture.pre_isp_binary;
			else if (pipe->config.isp_pipe_version == IA_CSS_PIPE_VERSION_2_2)
				binary = (struct ia_css_binary *)&pipe->pipe_settings.capture.post_isp_binary;
			else
				assert(0);
		}
		break;
	default:
		break;
	}

	if (binary && !binary->info->sp.enable.s3a)
		binary = NULL;

	return binary;
}

static struct ia_css_binary *
ia_css_pipe_get_sdis_binary(const struct ia_css_pipe *pipe)
{
	struct ia_css_binary *binary = NULL;

	assert(pipe);

	switch (pipe->config.mode) {
	case IA_CSS_PIPE_MODE_VIDEO:
		binary = (struct ia_css_binary *)&pipe->pipe_settings.video.video_binary;
		break;
	default:
		break;
	}

	if (binary && !binary->info->sp.enable.dis)
		binary = NULL;

	return binary;
}

struct ia_css_pipeline *
ia_css_pipe_get_pipeline(const struct ia_css_pipe *pipe)
{
	assert(pipe);

	return (struct ia_css_pipeline *)&pipe->pipeline;
}

unsigned int
ia_css_pipe_get_pipe_num(const struct ia_css_pipe *pipe)
{
	assert(pipe);

	/*
	 * KW was not sure this function was not returning a value
	 * that was out of range; so added an assert, and, for the
	 * case when asserts are not enabled, clip to the largest
	 * value; pipe_num is unsigned so the value cannot be too small
	 */
	assert(pipe->pipe_num < IA_CSS_PIPELINE_NUM_MAX);

	if (pipe->pipe_num >= IA_CSS_PIPELINE_NUM_MAX)
		return (IA_CSS_PIPELINE_NUM_MAX - 1);

	return pipe->pipe_num;
}

unsigned int
ia_css_pipe_get_isp_pipe_version(const struct ia_css_pipe *pipe)
{
	assert(pipe);

	return (unsigned int)pipe->config.isp_pipe_version;
}

#define SP_START_TIMEOUT_US 30000000

int
ia_css_start_sp(void)
{
	unsigned long timeout;
	int err = 0;

	IA_CSS_ENTER("");
	sh_css_sp_start_isp();

	/* waiting for the SP is completely started */
	timeout = SP_START_TIMEOUT_US;
	while ((ia_css_spctrl_get_state(SP0_ID) != IA_CSS_SP_SW_INITIALIZED) && timeout) {
		timeout--;
		udelay(1);
	}
	if (timeout == 0) {
		IA_CSS_ERROR("timeout during SP initialization");
		return -EINVAL;
	}

	/* Workaround, in order to run two streams in parallel. See TASK 4271*/
	/* TODO: Fix this. */

	sh_css_init_host_sp_control_vars();

	/* buffers should be initialized only when sp is started */
	/* AM: At the moment it will be done only when there is no stream active. */

	sh_css_setup_queues();
	ia_css_bufq_dump_queue_info();

	IA_CSS_LEAVE_ERR(err);
	return err;
}

/*
 * Time to wait SP for termincate. Only condition when this can happen
 * is a fatal hw failure, but we must be able to detect this and emit
 * a proper error trace.
 */
#define SP_SHUTDOWN_TIMEOUT_US 200000

int
ia_css_stop_sp(void)
{
	unsigned long timeout;
	int err = 0;

	IA_CSS_ENTER("void");

	if (!sh_css_sp_is_running()) {
		err = -EINVAL;
		IA_CSS_LEAVE("SP already stopped : return_err=%d", err);

		/* Return an error - stop SP should not have been called by driver */
		return err;
	}

	/* For now, stop whole SP */
	if (!sh_css_write_host2sp_command(host2sp_cmd_terminate)) {
		IA_CSS_ERROR("Call to 'sh-css_write_host2sp_command()' failed");
		ia_css_debug_dump_sp_sw_debug_info();
		ia_css_debug_dump_debug_info(NULL);
	}

	sh_css_sp_set_sp_running(false);

	timeout = SP_SHUTDOWN_TIMEOUT_US;
	while (!ia_css_spctrl_is_idle(SP0_ID) && timeout) {
		timeout--;
		udelay(1);
	}
	if (ia_css_spctrl_get_state(SP0_ID) != IA_CSS_SP_SW_TERMINATED)
		IA_CSS_WARNING("SP has not terminated (SW)");

	if (timeout == 0) {
		IA_CSS_WARNING("SP is not idle");
		ia_css_debug_dump_sp_sw_debug_info();
	}
	timeout = SP_SHUTDOWN_TIMEOUT_US;
	while (!isp_ctrl_getbit(ISP0_ID, ISP_SC_REG, ISP_IDLE_BIT) && timeout) {
		timeout--;
		udelay(1);
	}
	if (timeout == 0) {
		IA_CSS_WARNING("ISP is not idle");
		ia_css_debug_dump_sp_sw_debug_info();
	}

	sh_css_hmm_buffer_record_uninit();

	/* clear pending param sets from refcount */
	sh_css_param_clear_param_sets();

	IA_CSS_LEAVE_ERR(err);
	return err;
}

int
ia_css_update_continuous_frames(struct ia_css_stream *stream)
{
	struct ia_css_pipe *pipe;
	unsigned int i;

	ia_css_debug_dtrace(
	    IA_CSS_DEBUG_TRACE,
	    "sh_css_update_continuous_frames() enter:\n");

	if (!stream) {
		ia_css_debug_dtrace(
		    IA_CSS_DEBUG_TRACE,
		    "sh_css_update_continuous_frames() leave: invalid stream, return_void\n");
		return -EINVAL;
	}

	pipe = stream->continuous_pipe;

	for (i = stream->config.init_num_cont_raw_buf;
		i < stream->config.target_num_cont_raw_buf; i++)
		sh_css_update_host2sp_offline_frame(i,
						    pipe->continuous_frames[i], pipe->cont_md_buffers[i]);

	sh_css_update_host2sp_cont_num_raw_frames
	(stream->config.target_num_cont_raw_buf, true);
	ia_css_debug_dtrace(
	    IA_CSS_DEBUG_TRACE,
	    "sh_css_update_continuous_frames() leave: return_void\n");

	return 0;
}

void ia_css_pipe_map_queue(struct ia_css_pipe *pipe, bool map)
{
	unsigned int thread_id;
	unsigned int pipe_num;
	bool need_input_queue;

	IA_CSS_ENTER("");
	assert(pipe);

	pipe_num = pipe->pipe_num;

	ia_css_pipeline_get_sp_thread_id(pipe_num, &thread_id);

#if defined(ISP2401)
	need_input_queue = true;
#else
	need_input_queue = pipe->stream->config.mode == IA_CSS_INPUT_MODE_MEMORY;
#endif

	/* map required buffer queues to resources */
	/* TODO: to be improved */
	if (pipe->mode == IA_CSS_PIPE_ID_PREVIEW) {
		if (need_input_queue)
			ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_INPUT_FRAME, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_OUTPUT_FRAME, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_PARAMETER_SET, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_PER_FRAME_PARAMETER_SET, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_METADATA, map);
		if (pipe->pipe_settings.preview.preview_binary.info &&
		    pipe->pipe_settings.preview.preview_binary.info->sp.enable.s3a)
			ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_3A_STATISTICS, map);
	} else if (pipe->mode == IA_CSS_PIPE_ID_CAPTURE) {
		unsigned int i;

		if (need_input_queue)
			ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_INPUT_FRAME, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_OUTPUT_FRAME, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_VF_OUTPUT_FRAME, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_PARAMETER_SET, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_PER_FRAME_PARAMETER_SET, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_METADATA, map);
		if (pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_PRIMARY) {
			for (i = 0; i < pipe->pipe_settings.capture.num_primary_stage; i++) {
				if (pipe->pipe_settings.capture.primary_binary[i].info &&
				    pipe->pipe_settings.capture.primary_binary[i].info->sp.enable.s3a) {
					ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_3A_STATISTICS, map);
					break;
				}
			}
		} else if (pipe->config.default_capture_config.mode ==
			    IA_CSS_CAPTURE_MODE_ADVANCED ||
			    pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_LOW_LIGHT ||
			    pipe->config.default_capture_config.mode == IA_CSS_CAPTURE_MODE_BAYER) {
			if (pipe->pipe_settings.capture.pre_isp_binary.info &&
			    pipe->pipe_settings.capture.pre_isp_binary.info->sp.enable.s3a)
				ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_3A_STATISTICS, map);
		}
	} else if (pipe->mode == IA_CSS_PIPE_ID_VIDEO) {
		if (need_input_queue)
			ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_INPUT_FRAME, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_OUTPUT_FRAME, map);
		if (pipe->enable_viewfinder[IA_CSS_PIPE_OUTPUT_STAGE_0])
			ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_VF_OUTPUT_FRAME, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_PARAMETER_SET, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_PER_FRAME_PARAMETER_SET, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_METADATA, map);
		if (pipe->pipe_settings.video.video_binary.info &&
		    pipe->pipe_settings.video.video_binary.info->sp.enable.s3a)
			ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_3A_STATISTICS, map);
		if (pipe->pipe_settings.video.video_binary.info &&
		    (pipe->pipe_settings.video.video_binary.info->sp.enable.dis
		    ))
			ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_DIS_STATISTICS, map);
	} else if (pipe->mode == IA_CSS_PIPE_ID_COPY) {
		if (need_input_queue)
			ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_INPUT_FRAME, map);
		if (!pipe->stream->config.continuous)
			ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_OUTPUT_FRAME, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_METADATA, map);
	} else if (pipe->mode == IA_CSS_PIPE_ID_ACC) {
		if (need_input_queue)
			ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_INPUT_FRAME, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_OUTPUT_FRAME, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_PARAMETER_SET, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_PER_FRAME_PARAMETER_SET, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_METADATA, map);
	} else if (pipe->mode == IA_CSS_PIPE_ID_YUVPP) {
		unsigned int idx;

		for (idx = 0; idx < IA_CSS_PIPE_MAX_OUTPUT_STAGE; idx++) {
			ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_OUTPUT_FRAME + idx, map);
			if (pipe->enable_viewfinder[idx])
				ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_VF_OUTPUT_FRAME + idx, map);
		}
		if (need_input_queue)
			ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_INPUT_FRAME, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_PARAMETER_SET, map);
		ia_css_queue_map(thread_id, IA_CSS_BUFFER_TYPE_METADATA, map);
	}
	IA_CSS_LEAVE("");
}


int
ia_css_unlock_raw_frame(struct ia_css_stream *stream, uint32_t exp_id)
{
	int ret;

	IA_CSS_ENTER("");

	/*
	 * Only continuous streams have a tagger to which we can send the
	 * unlock message.
	 */
	if (!stream || !stream->config.continuous) {
		IA_CSS_ERROR("invalid stream pointer");
		return -EINVAL;
	}

	if (exp_id > IA_CSS_ISYS_MAX_EXPOSURE_ID ||
	    exp_id < IA_CSS_ISYS_MIN_EXPOSURE_ID) {
		IA_CSS_ERROR("invalid exposure ID: %d\n", exp_id);
		return -EINVAL;
	}

	/*
	 * Send the event. Since we verified that the exp_id is valid,
	 * we can safely assign it to an 8-bit argument here.
	 */
	ret = ia_css_bufq_enqueue_psys_event(
	    IA_CSS_PSYS_SW_EVENT_UNLOCK_RAW_BUFFER, exp_id, 0, 0);

	IA_CSS_LEAVE_ERR(ret);
	return ret;
}

/*
 * @brief	Set the state (Enable or Disable) of the Extension stage in the
 *		given pipe.
 */
int
ia_css_pipe_set_qos_ext_state(struct ia_css_pipe *pipe, uint32_t fw_handle,
			      bool enable)
{
	unsigned int thread_id;
	struct ia_css_pipeline_stage *stage;
	int err = 0;

	IA_CSS_ENTER("");

	/* Parameter Check */
	if (!pipe || !pipe->stream) {
		IA_CSS_ERROR("Invalid Pipe.");
		err = -EINVAL;
	} else if (!(pipe->config.acc_extension)) {
		IA_CSS_ERROR("Invalid Pipe(No Extension Firmware)");
		err = -EINVAL;
	} else if (!sh_css_sp_is_running()) {
		IA_CSS_ERROR("Leaving: queue unavailable.");
		err = -EBUSY;
	} else {
		/* Query the threadid and stage_num for the Extension firmware*/
		ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &thread_id);
		err = ia_css_pipeline_get_stage_from_fw(&pipe->pipeline, fw_handle, &stage);
		if (!err) {
			/* Set the Extension State;. TODO: Add check for stage firmware.type (QOS)*/
			err = ia_css_bufq_enqueue_psys_event(
			    (uint8_t)IA_CSS_PSYS_SW_EVENT_STAGE_ENABLE_DISABLE,
			    (uint8_t)thread_id,
			    (uint8_t)stage->stage_num,
			    enable ? 1 : 0);
			if (!err) {
				if (enable)
					SH_CSS_QOS_STAGE_ENABLE(&sh_css_sp_group.pipe[thread_id], stage->stage_num);
				else
					SH_CSS_QOS_STAGE_DISABLE(&sh_css_sp_group.pipe[thread_id], stage->stage_num);
			}
		}
	}
	IA_CSS_LEAVE("err:%d handle:%u enable:%d", err, fw_handle, enable);
	return err;
}

/*
 * @brief	Get the state (Enable or Disable) of the Extension stage in the
 * given pipe.
 */
int
ia_css_pipe_get_qos_ext_state(struct ia_css_pipe *pipe, uint32_t fw_handle,
			      bool *enable)
{
	struct ia_css_pipeline_stage *stage;
	unsigned int thread_id;
	int err = 0;

	IA_CSS_ENTER("");

	/* Parameter Check */
	if (!pipe || !pipe->stream) {
		IA_CSS_ERROR("Invalid Pipe.");
		err = -EINVAL;
	} else if (!(pipe->config.acc_extension)) {
		IA_CSS_ERROR("Invalid Pipe (No Extension Firmware).");
		err = -EINVAL;
	} else if (!sh_css_sp_is_running()) {
		IA_CSS_ERROR("Leaving: queue unavailable.");
		err = -EBUSY;
	} else {
		/* Query the threadid and stage_num corresponding to the Extension firmware*/
		ia_css_pipeline_get_sp_thread_id(ia_css_pipe_get_pipe_num(pipe), &thread_id);
		err = ia_css_pipeline_get_stage_from_fw(&pipe->pipeline, fw_handle, &stage);

		if (!err) {
			/* Get the Extension State */
			*enable = (SH_CSS_QOS_STAGE_IS_ENABLED(&sh_css_sp_group.pipe[thread_id],
								stage->stage_num)) ? true : false;
		}
	}
	IA_CSS_LEAVE("err:%d handle:%u enable:%d", err, fw_handle, *enable);
	return err;
}

static void
sh_css_hmm_buffer_record_init(void)
{
	int i;

	for (i = 0; i < MAX_HMM_BUFFER_NUM; i++)
		sh_css_hmm_buffer_record_reset(&hmm_buffer_record[i]);
}

static void
sh_css_hmm_buffer_record_uninit(void)
{
	int i;
	struct sh_css_hmm_buffer_record *buffer_record = NULL;

	buffer_record = &hmm_buffer_record[0];
	for (i = 0; i < MAX_HMM_BUFFER_NUM; i++) {
		if (buffer_record->in_use) {
			if (buffer_record->h_vbuf)
				ia_css_rmgr_rel_vbuf(hmm_buffer_pool, &buffer_record->h_vbuf);
			sh_css_hmm_buffer_record_reset(buffer_record);
		}
		buffer_record++;
	}
}

static void
sh_css_hmm_buffer_record_reset(struct sh_css_hmm_buffer_record *buffer_record)
{
	assert(buffer_record);
	buffer_record->in_use = false;
	buffer_record->type = IA_CSS_BUFFER_TYPE_INVALID;
	buffer_record->h_vbuf = NULL;
	buffer_record->kernel_ptr = 0;
}

static struct sh_css_hmm_buffer_record
*sh_css_hmm_buffer_record_acquire(struct ia_css_rmgr_vbuf_handle *h_vbuf,
				    enum ia_css_buffer_type type,
				    hrt_address kernel_ptr)
{
	int i;
	struct sh_css_hmm_buffer_record *buffer_record = NULL;
	struct sh_css_hmm_buffer_record *out_buffer_record = NULL;

	assert(h_vbuf);
	assert((type > IA_CSS_BUFFER_TYPE_INVALID) &&
	       (type < IA_CSS_NUM_DYNAMIC_BUFFER_TYPE));
	assert(kernel_ptr != 0);

	buffer_record = &hmm_buffer_record[0];
	for (i = 0; i < MAX_HMM_BUFFER_NUM; i++) {
		if (!buffer_record->in_use) {
			buffer_record->in_use = true;
			buffer_record->type = type;
			buffer_record->h_vbuf = h_vbuf;
			buffer_record->kernel_ptr = kernel_ptr;
			out_buffer_record = buffer_record;
			break;
		}
		buffer_record++;
	}

	return out_buffer_record;
}

static struct sh_css_hmm_buffer_record
*sh_css_hmm_buffer_record_validate(ia_css_ptr ddr_buffer_addr,
				    enum ia_css_buffer_type type)
{
	int i;
	struct sh_css_hmm_buffer_record *buffer_record = NULL;
	bool found_record = false;

	buffer_record = &hmm_buffer_record[0];
	for (i = 0; i < MAX_HMM_BUFFER_NUM; i++) {
		if ((buffer_record->in_use) &&
		    (buffer_record->type == type) &&
		    (buffer_record->h_vbuf) &&
		    (buffer_record->h_vbuf->vptr == ddr_buffer_addr)) {
			found_record = true;
			break;
		}
		buffer_record++;
	}

	if (found_record)
		return buffer_record;
	else
		return NULL;
}
