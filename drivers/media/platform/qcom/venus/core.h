/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */

#ifndef __VENUS_CORE_H_
#define __VENUS_CORE_H_

#include <linux/list.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

#include "hfi.h"

#define VIDC_CLKS_NUM_MAX		4
#define VIDC_VCODEC_CLKS_NUM_MAX	2
#define VIDC_PMDOMAINS_NUM_MAX		3

struct freq_tbl {
	unsigned int load;
	unsigned long freq;
};

struct reg_val {
	u32 reg;
	u32 value;
};

struct codec_freq_data {
	u32 pixfmt;
	u32 session_type;
	unsigned long vpp_freq;
	unsigned long vsp_freq;
};

struct bw_tbl {
	u32 mbs_per_sec;
	u32 avg;
	u32 peak;
	u32 avg_10bit;
	u32 peak_10bit;
};

struct venus_resources {
	u64 dma_mask;
	const struct freq_tbl *freq_tbl;
	unsigned int freq_tbl_size;
	const struct bw_tbl *bw_tbl_enc;
	unsigned int bw_tbl_enc_size;
	const struct bw_tbl *bw_tbl_dec;
	unsigned int bw_tbl_dec_size;
	const struct reg_val *reg_tbl;
	unsigned int reg_tbl_size;
	const struct codec_freq_data *codec_freq_data;
	unsigned int codec_freq_data_size;
	const char * const clks[VIDC_CLKS_NUM_MAX];
	unsigned int clks_num;
	const char * const vcodec0_clks[VIDC_VCODEC_CLKS_NUM_MAX];
	const char * const vcodec1_clks[VIDC_VCODEC_CLKS_NUM_MAX];
	unsigned int vcodec_clks_num;
	const char * const vcodec_pmdomains[VIDC_PMDOMAINS_NUM_MAX];
	unsigned int vcodec_pmdomains_num;
	unsigned int vcodec_num;
	enum hfi_version hfi_version;
	u32 max_load;
	unsigned int vmem_id;
	u32 vmem_size;
	u32 vmem_addr;
	const char *fwname;
};

struct venus_format {
	u32 pixfmt;
	unsigned int num_planes;
	u32 type;
	u32 flags;
};

#define MAX_PLANES		4
#define MAX_FMT_ENTRIES		32
#define MAX_CAP_ENTRIES		32
#define MAX_ALLOC_MODE_ENTRIES	16
#define MAX_CODEC_NUM		32

struct raw_formats {
	u32 buftype;
	u32 fmt;
};

struct venus_caps {
	u32 codec;
	u32 domain;
	bool cap_bufs_mode_dynamic;
	unsigned int num_caps;
	struct hfi_capability caps[MAX_CAP_ENTRIES];
	unsigned int num_pl;
	struct hfi_profile_level pl[HFI_MAX_PROFILE_COUNT];
	unsigned int num_fmts;
	struct raw_formats fmts[MAX_FMT_ENTRIES];
	bool valid;	/* used only for Venus v1xx */
};

/**
 * struct venus_core - holds core parameters valid for all instances
 *
 * @base:	IO memory base address
 * @irq:		Venus irq
 * @clks:	an array of struct clk pointers
 * @vcodec0_clks: an array of vcodec0 struct clk pointers
 * @vcodec1_clks: an array of vcodec1 struct clk pointers
 * @pd_dl_venus: pmdomain device-link for venus domain
 * @pmdomains:	an array of pmdomains struct device pointers
 * @vdev_dec:	a reference to video device structure for decoder instances
 * @vdev_enc:	a reference to video device structure for encoder instances
 * @v4l2_dev:	a holder for v4l2 device structure
 * @res:		a reference to venus resources structure
 * @dev:		convenience struct device pointer
 * @dev_dec:	convenience struct device pointer for decoder device
 * @dev_enc:	convenience struct device pointer for encoder device
 * @use_tz:	a flag that suggests presence of trustzone
 * @lock:	a lock for this strucure
 * @instances:	a list_head of all instances
 * @insts_count:	num of instances
 * @state:	the state of the venus core
 * @done:	a completion for sync HFI operations
 * @error:	an error returned during last HFI sync operations
 * @sys_error:	an error flag that signal system error event
 * @core_ops:	the core operations
 * @pm_lock:	a lock for PM operations
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
	struct clk *vcodec0_clks[VIDC_VCODEC_CLKS_NUM_MAX];
	struct clk *vcodec1_clks[VIDC_VCODEC_CLKS_NUM_MAX];
	struct icc_path *video_path;
	struct icc_path *cpucfg_path;
	struct device_link *pd_dl_venus;
	struct device *pmdomains[VIDC_PMDOMAINS_NUM_MAX];
	struct video_device *vdev_dec;
	struct video_device *vdev_enc;
	struct v4l2_device v4l2_dev;
	const struct venus_resources *res;
	struct device *dev;
	struct device *dev_dec;
	struct device *dev_enc;
	unsigned int use_tz;
	struct video_firmware {
		struct device *dev;
		struct iommu_domain *iommu_domain;
		size_t mapped_mem_size;
	} fw;
	struct mutex lock;
	struct list_head instances;
	atomic_t insts_count;
	unsigned int state;
	struct completion done;
	unsigned int error;
	bool sys_error;
	const struct hfi_core_ops *core_ops;
	const struct venus_pm_ops *pm_ops;
	struct mutex pm_lock;
	unsigned long enc_codecs;
	unsigned long dec_codecs;
	unsigned int max_sessions_supported;
#define ENC_ROTATION_CAPABILITY		0x1
#define ENC_SCALING_CAPABILITY		0x2
#define ENC_DEINTERLACE_CAPABILITY	0x4
#define DEC_MULTI_STREAM_CAPABILITY	0x8
	unsigned int core_caps;
	void *priv;
	const struct hfi_ops *ops;
	struct delayed_work work;
	struct venus_caps caps[MAX_CODEC_NUM];
	unsigned int codecs_count;
	unsigned int core0_usage_count;
	unsigned int core1_usage_count;
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
	u32 rc_enable;

	u32 h264_i_period;
	u32 h264_entropy_mode;
	u32 h264_i_qp;
	u32 h264_p_qp;
	u32 h264_b_qp;
	u32 h264_min_qp;
	u32 h264_max_qp;
	u32 h264_loop_filter_mode;
	s32 h264_loop_filter_alpha;
	s32 h264_loop_filter_beta;

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
		u32 hevc;
	} profile;
	struct {
		u32 mpeg4;
		u32 h264;
		u32 hevc;
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

struct clock_data {
	u32 core_id;
	unsigned long freq;
	const struct codec_freq_data *codec_freq_data;
};

#define to_venus_buffer(ptr)	container_of(ptr, struct venus_buffer, vb)

enum venus_dec_state {
	VENUS_DEC_STATE_DEINIT		= 0,
	VENUS_DEC_STATE_INIT		= 1,
	VENUS_DEC_STATE_CAPTURE_SETUP	= 2,
	VENUS_DEC_STATE_STOPPED		= 3,
	VENUS_DEC_STATE_SEEK		= 4,
	VENUS_DEC_STATE_DRAIN		= 5,
	VENUS_DEC_STATE_DECODING	= 6,
	VENUS_DEC_STATE_DRC		= 7,
	VENUS_DEC_STATE_DRC_FLUSH_DONE	= 8,
};

struct venus_ts_metadata {
	bool used;
	u64 ts_ns;
	u64 ts_us;
	u32 flags;
	struct v4l2_timecode tc;
};

/**
 * struct venus_inst - holds per instance parameters
 *
 * @list:	used for attach an instance to the core
 * @lock:	instance lock
 * @core:	a reference to the core struct
 * @dpbbufs:	a list of decoded picture buffers
 * @internalbufs:	a list of internal bufferes
 * @registeredbufs:	a list of registered capture bufferes
 * @delayed_process	a list of delayed buffers
 * @delayed_process_work:	a work_struct for process delayed buffers
 * @ctrl_handler:	v4l control handler
 * @controls:	a union of decoder and encoder control parameters
 * @fh:	 a holder of v4l file handle structure
 * @streamon_cap: stream on flag for capture queue
 * @streamon_out: stream on flag for output queue
 * @width:	current capture width
 * @height:	current capture height
 * @out_width:	current output width
 * @out_height:	current output height
 * @colorspace:	current color space
 * @quantization:	current quantization
 * @xfer_func:	current xfer function
 * @codec_state:	current codec API state (see DEC/ENC_STATE_)
 * @reconf_wait:	wait queue for resolution change event
 * @subscriptions:	used to hold current events subscriptions
 * @buf_count:		used to count number of buffers (reqbuf(0))
 * @fps:		holds current FPS
 * @timeperframe:	holds current time per frame structure
 * @fmt_out:	a reference to output format structure
 * @fmt_cap:	a reference to capture format structure
 * @num_input_bufs:	holds number of input buffers
 * @num_output_bufs:	holds number of output buffers
 * @input_buf_size	holds input buffer size
 * @output_buf_size:	holds output buffer size
 * @output2_buf_size:	holds secondary decoder output buffer size
 * @dpb_buftype:	decoded picture buffer type
 * @dpb_fmt:		decoded picture buffer raw format
 * @opb_buftype:	output picture buffer type
 * @opb_fmt:		output picture buffer raw format
 * @reconfig:	a flag raised by decoder when the stream resolution changed
 * @hfi_codec:		current codec for this instance in HFI space
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
 * @last_buf:	last capture buffer for dynamic-resoluton-change
 */
struct venus_inst {
	struct list_head list;
	struct mutex lock;
	struct venus_core *core;
	struct clock_data clk_data;
	struct list_head dpbbufs;
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
	u32 width;
	u32 height;
	u32 out_width;
	u32 out_height;
	u32 colorspace;
	u8 ycbcr_enc;
	u8 quantization;
	u8 xfer_func;
	enum venus_dec_state codec_state;
	wait_queue_head_t reconf_wait;
	unsigned int subscriptions;
	int buf_count;
	struct venus_ts_metadata tss[VIDEO_MAX_FRAME];
	unsigned long payloads[VIDEO_MAX_FRAME];
	u64 fps;
	struct v4l2_fract timeperframe;
	const struct venus_format *fmt_out;
	const struct venus_format *fmt_cap;
	unsigned int num_input_bufs;
	unsigned int num_output_bufs;
	unsigned int input_buf_size;
	unsigned int output_buf_size;
	unsigned int output2_buf_size;
	u32 dpb_buftype;
	u32 dpb_fmt;
	u32 opb_buftype;
	u32 opb_fmt;
	bool reconfig;
	u32 hfi_codec;
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
	unsigned int core_acquired: 1;
	unsigned int bit_depth;
	struct vb2_buffer *last_buf;
};

#define IS_V1(core)	((core)->res->hfi_version == HFI_VERSION_1XX)
#define IS_V3(core)	((core)->res->hfi_version == HFI_VERSION_3XX)
#define IS_V4(core)	((core)->res->hfi_version == HFI_VERSION_4XX)

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

static inline struct venus_caps *
venus_caps_by_codec(struct venus_core *core, u32 codec, u32 domain)
{
	unsigned int c;

	for (c = 0; c < core->codecs_count; c++) {
		if (core->caps[c].codec == codec &&
		    core->caps[c].domain == domain)
			return &core->caps[c];
	}

	return NULL;
}

#endif
