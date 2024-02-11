/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Rockchip ISP1 Driver - Common definitions
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *
 * Based on Rockchip ISP1 driver by Rockchip Electronics Co., Ltd.
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#ifndef _RKISP1_COMMON_H
#define _RKISP1_COMMON_H

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/rkisp1-config.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

#include "rkisp1-regs.h"

struct dentry;

/*
 * flags on the 'direction' field in struct rkisp1_mbus_info' that indicate
 * on which pad the media bus format is supported
 */
#define RKISP1_ISP_SD_SRC			BIT(0)
#define RKISP1_ISP_SD_SINK			BIT(1)

/* min and max values for the widths and heights of the entities */
#define RKISP1_ISP_MAX_WIDTH			4032
#define RKISP1_ISP_MAX_HEIGHT			3024
#define RKISP1_ISP_MIN_WIDTH			32
#define RKISP1_ISP_MIN_HEIGHT			32

#define RKISP1_RSZ_MP_SRC_MAX_WIDTH		4416
#define RKISP1_RSZ_MP_SRC_MAX_HEIGHT		3312
#define RKISP1_RSZ_SP_SRC_MAX_WIDTH		1920
#define RKISP1_RSZ_SP_SRC_MAX_HEIGHT		1920
#define RKISP1_RSZ_SRC_MIN_WIDTH		32
#define RKISP1_RSZ_SRC_MIN_HEIGHT		16

/* the default width and height of all the entities */
#define RKISP1_DEFAULT_WIDTH			800
#define RKISP1_DEFAULT_HEIGHT			600

#define RKISP1_DRIVER_NAME			"rkisp1"
#define RKISP1_BUS_INFO				"platform:" RKISP1_DRIVER_NAME

/* maximum number of clocks */
#define RKISP1_MAX_BUS_CLK			8

/* a bitmask of the ready stats */
#define RKISP1_STATS_MEAS_MASK			(RKISP1_CIF_ISP_AWB_DONE |	\
						 RKISP1_CIF_ISP_AFM_FIN |	\
						 RKISP1_CIF_ISP_EXP_END |	\
						 RKISP1_CIF_ISP_HIST_MEASURE_RDY)

/* IRQ lines */
enum rkisp1_irq_line {
	RKISP1_IRQ_ISP = 0,
	RKISP1_IRQ_MI,
	RKISP1_IRQ_MIPI,
	RKISP1_NUM_IRQS,
};

/* enum for the resizer pads */
enum rkisp1_rsz_pad {
	RKISP1_RSZ_PAD_SINK,
	RKISP1_RSZ_PAD_SRC,
	RKISP1_RSZ_PAD_MAX
};

/* enum for the csi receiver pads */
enum rkisp1_csi_pad {
	RKISP1_CSI_PAD_SINK,
	RKISP1_CSI_PAD_SRC,
	RKISP1_CSI_PAD_NUM
};

/* enum for the capture id */
enum rkisp1_stream_id {
	RKISP1_MAINPATH,
	RKISP1_SELFPATH,
};

/* bayer patterns */
enum rkisp1_fmt_raw_pat_type {
	RKISP1_RAW_RGGB = 0,
	RKISP1_RAW_GRBG,
	RKISP1_RAW_GBRG,
	RKISP1_RAW_BGGR,
};

/* enum for the isp pads */
enum rkisp1_isp_pad {
	RKISP1_ISP_PAD_SINK_VIDEO,
	RKISP1_ISP_PAD_SINK_PARAMS,
	RKISP1_ISP_PAD_SOURCE_VIDEO,
	RKISP1_ISP_PAD_SOURCE_STATS,
	RKISP1_ISP_PAD_MAX
};

/*
 * enum rkisp1_feature - ISP features
 *
 * @RKISP1_FEATURE_MIPI_CSI2: The ISP has an internal MIPI CSI-2 receiver
 *
 * The ISP features are stored in a bitmask in &rkisp1_info.features and allow
 * the driver to implement support for features present in some ISP versions
 * only.
 */
enum rkisp1_feature {
	RKISP1_FEATURE_MIPI_CSI2 = BIT(0),
};

/*
 * struct rkisp1_info - Model-specific ISP Information
 *
 * @clks: array of ISP clock names
 * @clk_size: number of entries in the @clks array
 * @isrs: array of ISP interrupt descriptors
 * @isr_size: number of entries in the @isrs array
 * @isp_ver: ISP version
 * @features: bitmask of rkisp1_feature features implemented by the ISP
 *
 * This structure contains information about the ISP specific to a particular
 * ISP model, version, or integration in a particular SoC.
 */
struct rkisp1_info {
	const char * const *clks;
	unsigned int clk_size;
	const struct rkisp1_isr_data *isrs;
	unsigned int isr_size;
	enum rkisp1_cif_isp_version isp_ver;
	unsigned int features;
};

/*
 * struct rkisp1_sensor_async - A container for the v4l2_async_subdev to add to the notifier
 *				of the v4l2-async API
 *
 * @asd:		async_subdev variable for the sensor
 * @index:		index of the sensor (counting sensor found in DT)
 * @source_ep:		fwnode for the sensor source endpoint
 * @lanes:		number of lanes
 * @mbus_type:		type of bus (currently only CSI2 is supported)
 * @mbus_flags:		media bus (V4L2_MBUS_*) flags
 * @sd:			a pointer to v4l2_subdev struct of the sensor
 * @pixel_rate_ctrl:	pixel rate of the sensor, used to initialize the phy
 * @port:		port number (0: MIPI, 1: Parallel)
 */
struct rkisp1_sensor_async {
	struct v4l2_async_connection asd;
	unsigned int index;
	struct fwnode_handle *source_ep;
	unsigned int lanes;
	enum v4l2_mbus_type mbus_type;
	unsigned int mbus_flags;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl *pixel_rate_ctrl;
	unsigned int port;
};

/*
 * struct rkisp1_csi - CSI receiver subdev
 *
 * @rkisp1: pointer to the rkisp1 device
 * @dphy: a pointer to the phy
 * @is_dphy_errctrl_disabled: if dphy errctrl is disabled (avoid endless interrupt)
 * @sd: v4l2_subdev variable
 * @pads: media pads
 * @source: source in-use, set when starting streaming
 */
struct rkisp1_csi {
	struct rkisp1_device *rkisp1;
	struct phy *dphy;
	bool is_dphy_errctrl_disabled;
	struct v4l2_subdev sd;
	struct media_pad pads[RKISP1_CSI_PAD_NUM];
	struct v4l2_subdev *source;
};

/*
 * struct rkisp1_isp - ISP subdev entity
 *
 * @sd:				v4l2_subdev variable
 * @rkisp1:			pointer to rkisp1_device
 * @pads:			media pads
 * @sink_fmt:			input format
 * @frame_sequence:		used to synchronize frame_id between video devices.
 */
struct rkisp1_isp {
	struct v4l2_subdev sd;
	struct rkisp1_device *rkisp1;
	struct media_pad pads[RKISP1_ISP_PAD_MAX];
	const struct rkisp1_mbus_info *sink_fmt;
	__u32 frame_sequence;
};

/*
 * struct rkisp1_vdev_node - Container for the video nodes: params, stats, mainpath, selfpath
 *
 * @buf_queue:	queue of buffers
 * @vlock:	lock of the video node
 * @vdev:	video node
 * @pad:	media pad
 */
struct rkisp1_vdev_node {
	struct vb2_queue buf_queue;
	struct mutex vlock; /* ioctl serialization mutex */
	struct video_device vdev;
	struct media_pad pad;
};

/*
 * struct rkisp1_buffer - A container for the vb2 buffers used by the video devices:
 *			  params, stats, mainpath, selfpath
 *
 * @vb:		vb2 buffer
 * @queue:	entry of the buffer in the queue
 * @buff_addr:	dma addresses of each plane, used only by the capture devices: selfpath, mainpath
 */
struct rkisp1_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	u32 buff_addr[VIDEO_MAX_PLANES];
};

/*
 * struct rkisp1_dummy_buffer - A buffer to write the next frame to in case
 *				there are no vb2 buffers available.
 *
 * @vaddr:	return value of call to dma_alloc_attrs.
 * @dma_addr:	dma address of the buffer.
 * @size:	size of the buffer.
 */
struct rkisp1_dummy_buffer {
	void *vaddr;
	dma_addr_t dma_addr;
	u32 size;
};

struct rkisp1_device;

/*
 * struct rkisp1_capture - ISP capture video device
 *
 * @vnode:	  video node
 * @rkisp1:	  pointer to rkisp1_device
 * @id:		  id of the capture, one of RKISP1_SELFPATH, RKISP1_MAINPATH
 * @ops:	  list of callbacks to configure the capture device.
 * @config:	  a pointer to the list of registers to configure the capture format.
 * @is_streaming: device is streaming
 * @is_stopping:  stop_streaming callback was called and the device is in the process of
 *		  stopping the streaming.
 * @done:	  when stop_streaming callback is called, the device waits for the next irq
 *		  handler to stop the streaming by waiting on the 'done' wait queue.
 *		  If the irq handler is not called, the stream is stopped by the callback
 *		  after timeout.
 * @sp_y_stride:  the selfpath allows to configure a y stride that is longer than the image width.
 * @buf.lock:	  lock to protect buf.queue
 * @buf.queue:	  queued buffer list
 * @buf.dummy:	  dummy space to store dropped data
 *
 * rkisp1 uses shadow registers, so it needs two buffers at a time
 * @buf.curr:	  the buffer used for current frame
 * @buf.next:	  the buffer used for next frame
 * @pix.cfg:	  pixel configuration
 * @pix.info:	  a pointer to the v4l2_format_info of the pixel format
 * @pix.fmt:	  buffer format
 */
struct rkisp1_capture {
	struct rkisp1_vdev_node vnode;
	struct rkisp1_device *rkisp1;
	enum rkisp1_stream_id id;
	const struct rkisp1_capture_ops *ops;
	const struct rkisp1_capture_config *config;
	bool is_streaming;
	bool is_stopping;
	wait_queue_head_t done;
	unsigned int sp_y_stride;
	struct {
		/* protects queue, curr and next */
		spinlock_t lock;
		struct list_head queue;
		struct rkisp1_dummy_buffer dummy;
		struct rkisp1_buffer *curr;
		struct rkisp1_buffer *next;
	} buf;
	struct {
		const struct rkisp1_capture_fmt_cfg *cfg;
		const struct v4l2_format_info *info;
		struct v4l2_pix_format_mplane fmt;
	} pix;
};

struct rkisp1_stats;
struct rkisp1_stats_ops {
	void (*get_awb_meas)(struct rkisp1_stats *stats,
			     struct rkisp1_stat_buffer *pbuf);
	void (*get_aec_meas)(struct rkisp1_stats *stats,
			     struct rkisp1_stat_buffer *pbuf);
	void (*get_hst_meas)(struct rkisp1_stats *stats,
			     struct rkisp1_stat_buffer *pbuf);
};

/*
 * struct rkisp1_stats - ISP Statistics device
 *
 * @vnode:	  video node
 * @rkisp1:	  pointer to the rkisp1 device
 * @lock:	  locks the buffer list 'stat'
 * @stat:	  queue of rkisp1_buffer
 * @vdev_fmt:	  v4l2_format of the metadata format
 */
struct rkisp1_stats {
	struct rkisp1_vdev_node vnode;
	struct rkisp1_device *rkisp1;
	const struct rkisp1_stats_ops *ops;

	spinlock_t lock; /* locks the buffers list 'stats' */
	struct list_head stat;
	struct v4l2_format vdev_fmt;
};

struct rkisp1_params;
struct rkisp1_params_ops {
	void (*lsc_matrix_config)(struct rkisp1_params *params,
				  const struct rkisp1_cif_isp_lsc_config *pconfig);
	void (*goc_config)(struct rkisp1_params *params,
			   const struct rkisp1_cif_isp_goc_config *arg);
	void (*awb_meas_config)(struct rkisp1_params *params,
				const struct rkisp1_cif_isp_awb_meas_config *arg);
	void (*awb_meas_enable)(struct rkisp1_params *params,
				const struct rkisp1_cif_isp_awb_meas_config *arg,
				bool en);
	void (*awb_gain_config)(struct rkisp1_params *params,
				const struct rkisp1_cif_isp_awb_gain_config *arg);
	void (*aec_config)(struct rkisp1_params *params,
			   const struct rkisp1_cif_isp_aec_config *arg);
	void (*hst_config)(struct rkisp1_params *params,
			   const struct rkisp1_cif_isp_hst_config *arg);
	void (*hst_enable)(struct rkisp1_params *params,
			   const struct rkisp1_cif_isp_hst_config *arg, bool en);
	void (*afm_config)(struct rkisp1_params *params,
			   const struct rkisp1_cif_isp_afc_config *arg);
};

/*
 * struct rkisp1_params - ISP input parameters device
 *
 * @vnode:		video node
 * @rkisp1:		pointer to the rkisp1 device
 * @ops:		pointer to the variant-specific operations
 * @config_lock:	locks the buffer list 'params'
 * @params:		queue of rkisp1_buffer
 * @vdev_fmt:		v4l2_format of the metadata format
 * @quantization:	the quantization configured on the isp's src pad
 * @raw_type:		the bayer pattern on the isp video sink pad
 */
struct rkisp1_params {
	struct rkisp1_vdev_node vnode;
	struct rkisp1_device *rkisp1;
	const struct rkisp1_params_ops *ops;

	spinlock_t config_lock; /* locks the buffers list 'params' */
	struct list_head params;
	struct v4l2_format vdev_fmt;

	enum v4l2_quantization quantization;
	enum v4l2_ycbcr_encoding ycbcr_encoding;
	enum rkisp1_fmt_raw_pat_type raw_type;
};

/*
 * struct rkisp1_resizer - Resizer subdev
 *
 * @sd:	       v4l2_subdev variable
 * @regs_base: base register address offset
 * @id:	       id of the resizer, one of RKISP1_SELFPATH, RKISP1_MAINPATH
 * @rkisp1:    pointer to the rkisp1 device
 * @pads:      media pads
 * @config:    the set of registers to configure the resizer
 */
struct rkisp1_resizer {
	struct v4l2_subdev sd;
	u32 regs_base;
	enum rkisp1_stream_id id;
	struct rkisp1_device *rkisp1;
	struct media_pad pads[RKISP1_RSZ_PAD_MAX];
	const struct rkisp1_rsz_config *config;
};

/*
 * struct rkisp1_debug - Values to be exposed on debugfs.
 *			 The parameters are counters of the number of times the
 *			 event occurred since the driver was loaded.
 *
 * @data_loss:			  loss of data occurred within a line, processing failure
 * @outform_size_error:		  size error is generated in outmux submodule
 * @img_stabilization_size_error: size error is generated in image stabilization submodule
 * @inform_size_err:		  size error is generated in inform submodule
 * @mipi_error:			  mipi error occurred
 * @stats_error:		  writing to the 'Interrupt clear register' did not clear
 *				  it in the register 'Masked interrupt status'
 * @stop_timeout:		  upon stream stop, the capture waits 1 second for the isr to stop
 *				  the stream. This param is incremented in case of timeout.
 * @frame_drop:			  a frame was ready but the buffer queue was empty so the frame
 *				  was not sent to userspace
 */
struct rkisp1_debug {
	struct dentry *debugfs_dir;
	unsigned long data_loss;
	unsigned long outform_size_error;
	unsigned long img_stabilization_size_error;
	unsigned long inform_size_error;
	unsigned long irq_delay;
	unsigned long mipi_error;
	unsigned long stats_error;
	unsigned long stop_timeout[2];
	unsigned long frame_drop[2];
	unsigned long complete_frames;
};

/*
 * struct rkisp1_device - ISP platform device
 *
 * @base_addr:	   base register address
 * @dev:	   a pointer to the struct device
 * @clk_size:	   number of clocks
 * @clks:	   array of clocks
 * @v4l2_dev:	   v4l2_device variable
 * @media_dev:	   media_device variable
 * @notifier:	   a notifier to register on the v4l2-async API to be notified on the sensor
 * @source:        source subdev in-use, set when starting streaming
 * @csi:	   internal CSI-2 receiver
 * @isp:	   ISP sub-device
 * @resizer_devs:  resizer sub-devices
 * @capture_devs:  capture devices
 * @stats:	   ISP statistics metadata capture device
 * @params:	   ISP parameters metadata output device
 * @pipe:	   media pipeline
 * @stream_lock:   serializes {start/stop}_streaming callbacks between the capture devices.
 * @debug:	   debug params to be exposed on debugfs
 * @info:	   version-specific ISP information
 * @irqs:          IRQ line numbers
 */
struct rkisp1_device {
	void __iomem *base_addr;
	struct device *dev;
	unsigned int clk_size;
	struct clk_bulk_data clks[RKISP1_MAX_BUS_CLK];
	struct v4l2_device v4l2_dev;
	struct media_device media_dev;
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *source;
	struct rkisp1_csi csi;
	struct rkisp1_isp isp;
	struct rkisp1_resizer resizer_devs[2];
	struct rkisp1_capture capture_devs[2];
	struct rkisp1_stats stats;
	struct rkisp1_params params;
	struct media_pipeline pipe;
	struct mutex stream_lock; /* serialize {start/stop}_streaming cb between capture devices */
	struct rkisp1_debug debug;
	const struct rkisp1_info *info;
	int irqs[RKISP1_NUM_IRQS];
};

/*
 * struct rkisp1_mbus_info - ISP media bus info, Translates media bus code to hardware
 *			     format values
 *
 * @mbus_code: media bus code
 * @pixel_enc: pixel encoding
 * @mipi_dt:   mipi data type
 * @yuv_seq:   the order of the Y, Cb, Cr values
 * @bus_width: bus width
 * @bayer_pat: bayer pattern
 * @direction: a bitmask of the flags indicating on which pad the format is supported on
 */
struct rkisp1_mbus_info {
	u32 mbus_code;
	enum v4l2_pixel_encoding pixel_enc;
	u32 mipi_dt;
	u32 yuv_seq;
	u8 bus_width;
	enum rkisp1_fmt_raw_pat_type bayer_pat;
	unsigned int direction;
};

static inline void
rkisp1_write(struct rkisp1_device *rkisp1, unsigned int addr, u32 val)
{
	writel(val, rkisp1->base_addr + addr);
}

static inline u32 rkisp1_read(struct rkisp1_device *rkisp1, unsigned int addr)
{
	return readl(rkisp1->base_addr + addr);
}

/*
 * rkisp1_cap_enum_mbus_codes - A helper function that return the i'th supported mbus code
 *				of the capture entity. This is used to enumerate the supported
 *				mbus codes on the source pad of the resizer.
 *
 * @cap:  the capture entity
 * @code: the mbus code, the function reads the code->index and fills the code->code
 */
int rkisp1_cap_enum_mbus_codes(struct rkisp1_capture *cap,
			       struct v4l2_subdev_mbus_code_enum *code);

/*
 * rkisp1_mbus_info_get_by_index - Retrieve the ith supported mbus info
 *
 * @index: index of the mbus info to fetch
 */
const struct rkisp1_mbus_info *rkisp1_mbus_info_get_by_index(unsigned int index);

/*
 * rkisp1_sd_adjust_crop_rect - adjust a rectangle to fit into another rectangle.
 *
 * @crop:   rectangle to adjust.
 * @bounds: rectangle used as bounds.
 */
void rkisp1_sd_adjust_crop_rect(struct v4l2_rect *crop,
				const struct v4l2_rect *bounds);

/*
 * rkisp1_sd_adjust_crop - adjust a rectangle to fit into media bus format
 *
 * @crop:   rectangle to adjust.
 * @bounds: media bus format used as bounds.
 */
void rkisp1_sd_adjust_crop(struct v4l2_rect *crop,
			   const struct v4l2_mbus_framefmt *bounds);

/*
 * rkisp1_mbus_info_get_by_code - get the isp info of the media bus code
 *
 * @mbus_code: the media bus code
 */
const struct rkisp1_mbus_info *rkisp1_mbus_info_get_by_code(u32 mbus_code);

/*
 * rkisp1_params_pre_configure - Configure the params before stream start
 *
 * @params:	  pointer to rkisp1_params
 * @bayer_pat:	  the bayer pattern on the isp video sink pad
 * @quantization: the quantization configured on the isp's src pad
 * @ycbcr_encoding: the ycbcr_encoding configured on the isp's src pad
 *
 * This function is called by the ISP entity just before the ISP gets started.
 * It applies the initial ISP parameters from the first params buffer, but
 * skips LSC as it needs to be configured after the ISP is started.
 */
void rkisp1_params_pre_configure(struct rkisp1_params *params,
				 enum rkisp1_fmt_raw_pat_type bayer_pat,
				 enum v4l2_quantization quantization,
				 enum v4l2_ycbcr_encoding ycbcr_encoding);

/*
 * rkisp1_params_post_configure - Configure the params after stream start
 *
 * @params:	  pointer to rkisp1_params
 *
 * This function is called by the ISP entity just after the ISP gets started.
 * It applies the initial ISP LSC parameters from the first params buffer.
 */
void rkisp1_params_post_configure(struct rkisp1_params *params);

/* rkisp1_params_disable - disable all parameters.
 *			   This function is called by the isp entity upon stream start
 *			   when capturing bayer format.
 *
 * @params: pointer to rkisp1_params.
 */
void rkisp1_params_disable(struct rkisp1_params *params);

/* irq handlers */
irqreturn_t rkisp1_isp_isr(int irq, void *ctx);
irqreturn_t rkisp1_csi_isr(int irq, void *ctx);
irqreturn_t rkisp1_capture_isr(int irq, void *ctx);
void rkisp1_stats_isr(struct rkisp1_stats *stats, u32 isp_ris);
void rkisp1_params_isr(struct rkisp1_device *rkisp1);

/* register/unregisters functions of the entities */
int rkisp1_capture_devs_register(struct rkisp1_device *rkisp1);
void rkisp1_capture_devs_unregister(struct rkisp1_device *rkisp1);

int rkisp1_isp_register(struct rkisp1_device *rkisp1);
void rkisp1_isp_unregister(struct rkisp1_device *rkisp1);

int rkisp1_resizer_devs_register(struct rkisp1_device *rkisp1);
void rkisp1_resizer_devs_unregister(struct rkisp1_device *rkisp1);

int rkisp1_stats_register(struct rkisp1_device *rkisp1);
void rkisp1_stats_unregister(struct rkisp1_device *rkisp1);

int rkisp1_params_register(struct rkisp1_device *rkisp1);
void rkisp1_params_unregister(struct rkisp1_device *rkisp1);

#if IS_ENABLED(CONFIG_DEBUG_FS)
void rkisp1_debug_init(struct rkisp1_device *rkisp1);
void rkisp1_debug_cleanup(struct rkisp1_device *rkisp1);
#else
static inline void rkisp1_debug_init(struct rkisp1_device *rkisp1)
{
}
static inline void rkisp1_debug_cleanup(struct rkisp1_device *rkisp1)
{
}
#endif

#endif /* _RKISP1_COMMON_H */
