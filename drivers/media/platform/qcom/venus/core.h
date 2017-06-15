/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __VENUS_CORE_H_
#define __VENUS_CORE_H_

#include <linux/list.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

#include "hfi.h"

#define VIDC_CLKS_NUM_MAX	4

struct freq_tbl {
	unsigned int load;
	unsigned long freq;
};

struct reg_val {
	u32 reg;
	u32 value;
};

struct venus_resources {
	u64 dma_mask;
	const struct freq_tbl *freq_tbl;
	unsigned int freq_tbl_size;
	const struct reg_val *reg_tbl;
	unsigned int reg_tbl_size;
	const char * const clks[VIDC_CLKS_NUM_MAX];
	unsigned int clks_num;
	enum hfi_version hfi_version;
	u32 max_load;
	unsigned int vmem_id;
	u32 vmem_size;
	u32 vmem_addr;
};

struct venus_format {
	u32 pixfmt;
	unsigned int num_planes;
	u32 type;
};

/**
 * struct venus_core - holds core parameters valid for all instances
 *
 * @base:	IO memory base address
 * @irq:		Venus irq
 * @clks:	an array of struct clk pointers
 * @core0_clk:	a struct clk pointer for core0
 * @core1_clk:	a struct clk pointer for core1
 * @vdev_dec:	a reference to video device structure for decoder instances
 * @vdev_enc:	a reference to video device structure for encoder instances
 * @v4l2_dev:	a holder for v4l2 device structure
 * @res:		a reference to venus resources structure
 * @dev:		convenience struct device pointer
 * @dev_dec:	convenience struct device pointer for decoder device
 * @dev_enc:	convenience struct device pointer for encoder device
 * @lock:	a lock for this strucure
 * @instances:	a list_head of all instances
 * @insts_count:	num of instances
 * @state:	the state of the venus core
 * @done:	a completion for sync HFI operations
 * @error:	an error returned during last HFI sync operations
 * @sys_error:	an error flag that signal system error event
 * @core_ops:	the core operations
 * @enc_codecs:	encoders supported by this core
 * @dec_codecs:	decoders supported by this core
 * @max_sessions_supported:	holds the maximum number of sessions
 * @core_caps:	core capabilities
 * @priv:	a private filed for HFI operations
 * @ops:		the core HFI operations
 * @work:	a delayed work for handling system fatal error
 */
struct venus_core {
	void __iomem *base;
	int irq;
	struct clk *clks[VIDC_CLKS_NUM_MAX];
	struct clk *core0_clk;
	struct clk *core1_clk;
	struct video_device *vdev_dec;
	struct video_device *vdev_enc;
	struct v4l2_device v4l2_dev;
	const struct venus_resources *res;
	struct device *dev;
	struct device *dev_dec;
	struct device *dev_enc;
	struct device dev_fw;
	struct mutex lock;
	struct list_head instances;
	atomic_t insts_count;
	unsigned int state;
	struct completion done;
	unsigned int error;
	bool sys_error;
	const struct hfi_core_ops *core_ops;
	u32 enc_codecs;
	u32 dec_codecs;
	unsigned int max_sessions_supported;
#define ENC_ROTATION_CAPABILITY		0x1
#define ENC_SCALING_CAPABILITY		0x2
#define ENC_DEINTERLACE_CAPABILITY	0x4
#define DEC_MULTI_STREAM_CAPABILITY	0x8
	unsigned int core_caps;
	void *priv;
	const struct hfi_ops *ops;
	struct delayed_work work;
};

struct vdec_controls {
	u32 post_loop_deb_mode;
	u32 profile;
	u32 level;
};

struct venc_controls {
	u16 gop_size;
	u32 num_p_frames;
	u32 num_b_frames;
	u32 bitrate_mode;
	u32 bitrate;
	u32 bitrate_peak;

	u32 h264_i_period;
	u32 h264_entropy_mode;
	u32 h264_i_qp;
	u32 h264_p_qp;
	u32 h264_b_qp;
	u32 h264_min_qp;
	u32 h264_max_qp;
	u32 h264_loop_filter_mode;
	u32 h264_loop_filter_alpha;
	u32 h264_loop_filter_beta;

	u32 vp8_min_qp;
	u32 vp8_max_qp;

	u32 multi_slice_mode;
	u32 multi_slice_max_bytes;
	u32 multi_slice_max_mb;

	u32 header_mode;

	struct {
		u32 mpeg4;
		u32 h264;
		u32 vpx;
	} profile;
	struct {
		u32 mpeg4;
		u32 h264;
	} level;
};

struct venus_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
	dma_addr_t dma_addr;
	u32 size;
	struct list_head reg_list;
	u32 flags;
	struct list_head ref_list;
};

#define to_venus_buffer(ptr)	container_of(ptr, struct venus_buffer, vb)

/**
 * struct venus_inst - holds per instance paramerters
 *
 * @list:	used for attach an instance to the core
 * @lock:	instance lock
 * @core:	a reference to the core struct
 * @internalbufs:	a list of internal bufferes
 * @registeredbufs:	a list of registered capture bufferes
 * @delayed_process	a list of delayed buffers
 * @delayed_process_work:	a work_struct for process delayed buffers
 * @ctrl_handler:	v4l control handler
 * @controls:	a union of decoder and encoder control parameters
 * @fh:	 a holder of v4l file handle structure
 * @streamon_cap: stream on flag for capture queue
 * @streamon_out: stream on flag for output queue
 * @cmd_stop:	a flag to signal encoder/decoder commands
 * @width:	current capture width
 * @height:	current capture height
 * @out_width:	current output width
 * @out_height:	current output height
 * @colorspace:	current color space
 * @quantization:	current quantization
 * @xfer_func:	current xfer function
 * @fps:		holds current FPS
 * @timeperframe:	holds current time per frame structure
 * @fmt_out:	a reference to output format structure
 * @fmt_cap:	a reference to capture format structure
 * @num_input_bufs:	holds number of input buffers
 * @num_output_bufs:	holds number of output buffers
 * @input_buf_size	holds input buffer size
 * @output_buf_size:	holds output buffer size
 * @reconfig:	a flag raised by decoder when the stream resolution changed
 * @reconfig_width:	holds the new width
 * @reconfig_height:	holds the new height
 * @sequence_cap:	a sequence counter for capture queue
 * @sequence_out:	a sequence counter for output queue
 * @m2m_dev:	a reference to m2m device structure
 * @m2m_ctx:	a reference to m2m context structure
 * @state:	current state of the instance
 * @done:	a completion for sync HFI operation
 * @error:	an error returned during last HFI sync operation
 * @session_error:	a flag rised by HFI interface in case of session error
 * @ops:		HFI operations
 * @priv:	a private for HFI operations callbacks
 * @session_type:	the type of the session (decoder or encoder)
 * @hprop:	a union used as a holder by get property
 * @cap_width:	width capability
 * @cap_height:	height capability
 * @cap_mbs_per_frame:	macroblocks per frame capability
 * @cap_mbs_per_sec:	macroblocks per second capability
 * @cap_framerate:	framerate capability
 * @cap_scale_x:		horizontal scaling capability
 * @cap_scale_y:		vertical scaling capability
 * @cap_bitrate:		bitrate capability
 * @cap_hier_p:		hier capability
 * @cap_ltr_count:	LTR count capability
 * @cap_secure_output2_threshold: secure OUTPUT2 threshold capability
 * @cap_bufs_mode_static:	buffers allocation mode capability
 * @cap_bufs_mode_dynamic:	buffers allocation mode capability
 * @pl_count:	count of supported profiles/levels
 * @pl:		supported profiles/levels
 * @bufreq:	holds buffer requirements
 */
struct venus_inst {
	struct list_head list;
	struct mutex lock;
	struct venus_core *core;
	struct list_head internalbufs;
	struct list_head registeredbufs;
	struct list_head delayed_process;
	struct work_struct delayed_process_work;

	struct v4l2_ctrl_handler ctrl_handler;
	union {
		struct vdec_controls dec;
		struct venc_controls enc;
	} controls;
	struct v4l2_fh fh;
	unsigned int streamon_cap, streamon_out;
	bool cmd_stop;
	u32 width;
	u32 height;
	u32 out_width;
	u32 out_height;
	u32 colorspace;
	u8 ycbcr_enc;
	u8 quantization;
	u8 xfer_func;
	u64 fps;
	struct v4l2_fract timeperframe;
	const struct venus_format *fmt_out;
	const struct venus_format *fmt_cap;
	unsigned int num_input_bufs;
	unsigned int num_output_bufs;
	unsigned int input_buf_size;
	unsigned int output_buf_size;
	bool reconfig;
	u32 reconfig_width;
	u32 reconfig_height;
	u32 sequence_cap;
	u32 sequence_out;
	struct v4l2_m2m_dev *m2m_dev;
	struct v4l2_m2m_ctx *m2m_ctx;
	unsigned int state;
	struct completion done;
	unsigned int error;
	bool session_error;
	const struct hfi_inst_ops *ops;
	u32 session_type;
	union hfi_get_property hprop;
	struct hfi_capability cap_width;
	struct hfi_capability cap_height;
	struct hfi_capability cap_mbs_per_frame;
	struct hfi_capability cap_mbs_per_sec;
	struct hfi_capability cap_framerate;
	struct hfi_capability cap_scale_x;
	struct hfi_capability cap_scale_y;
	struct hfi_capability cap_bitrate;
	struct hfi_capability cap_hier_p;
	struct hfi_capability cap_ltr_count;
	struct hfi_capability cap_secure_output2_threshold;
	bool cap_bufs_mode_static;
	bool cap_bufs_mode_dynamic;
	unsigned int pl_count;
	struct hfi_profile_level pl[HFI_MAX_PROFILE_COUNT];
	struct hfi_buffer_requirements bufreq[HFI_BUFFER_TYPE_MAX];
};

#define ctrl_to_inst(ctrl)	\
	container_of((ctrl)->handler, struct venus_inst, ctrl_handler)

static inline struct venus_inst *to_inst(struct file *filp)
{
	return container_of(filp->private_data, struct venus_inst, fh);
}

static inline void *to_hfi_priv(struct venus_core *core)
{
	return core->priv;
}

#endif
