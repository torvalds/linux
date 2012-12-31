/* linux/drivers/media/video/exynos/gsc/gsc-core.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * header file for Samsung EXYNOS5 SoC series G-scaler driver

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef GSC_CORE_H_
#define GSC_CORE_H_

#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <mach/videonode.h>
#include <media/videobuf2-core.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-mediabus.h>
#include <media/exynos_mc.h>
#include <media/exynos_gscaler.h>
#include "regs-gsc.h"

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
#include <media/videobuf2-cma-phys.h>
#elif defined(CONFIG_VIDEOBUF2_ION)
#include <media/videobuf2-ion.h>
#endif

extern int gsc_dbg;

#define gsc_info(fmt, args...)						\
	do {								\
		if (gsc_dbg >= 6)						\
			printk(KERN_INFO "[INFO]%s:%d: "fmt "\n",	\
				__func__, __LINE__, ##args);		\
	} while (0)

#define gsc_err(fmt, args...)						\
	do {								\
		if (gsc_dbg >= 3)						\
			printk(KERN_ERR "[ERROR]%s:%d: "fmt "\n",	\
				__func__, __LINE__, ##args);		\
	} while (0)

#define gsc_warn(fmt, args...)						\
	do {								\
		if (gsc_dbg >= 4)						\
			printk(KERN_WARNING "[WARN]%s:%d: "fmt "\n",	\
				__func__, __LINE__, ##args);		\
	} while (0)

#define gsc_dbg(fmt, args...)						\
	do {								\
		if (gsc_dbg >= 7)						\
			printk(KERN_DEBUG "[DEBUG]%s:%d: "fmt "\n",	\
				__func__, __LINE__, ##args);		\
	} while (0)

#define GSC_MAX_CLOCKS			3
#define GSC_SHUTDOWN_TIMEOUT		((100*HZ)/1000)
#define GSC_MAX_DEVS			4
#define WORKQUEUE_NAME_SIZE		32
#define FIMD_NAME_SIZE			32
#define GSC_M2M_BUF_NUM			0
#define GSC_OUT_BUF_MAX			2
#define GSC_MAX_CTRL_NUM		10
#define GSC_OUT_MAX_MASK_NUM		7
#define GSC_SC_ALIGN_4			4
#define GSC_SC_ALIGN_2			2
#define GSC_OUT_DEF_SRC			15
#define GSC_OUT_DEF_DST			7
#define DEFAULT_GSC_SINK_WIDTH		800
#define DEFAULT_GSC_SINK_HEIGHT		480
#define DEFAULT_GSC_SOURCE_WIDTH	800
#define DEFAULT_GSC_SOURCE_HEIGHT	480
#define DEFAULT_CSC_EQ			1
#define DEFAULT_CSC_RANGE		1

#define GSC_LAST_DEV_ID			3
#define GSC_PAD_SINK			0
#define GSC_PAD_SOURCE			1
#define GSC_PADS_NUM			2

#define	GSC_PARAMS			(1 << 0)
#define	GSC_SRC_FMT			(1 << 1)
#define	GSC_DST_FMT			(1 << 2)
#define	GSC_CTX_M2M			(1 << 3)
#define	GSC_CTX_OUTPUT			(1 << 4)
#define	GSC_CTX_START			(1 << 5)
#define	GSC_CTX_STOP_REQ		(1 << 6)
#define	GSC_CTX_CAP			(1 << 10)

enum gsc_dev_flags {
	/* for global */
	ST_PWR_ON,
	ST_STOP_REQ,
	/* for m2m node */
	ST_M2M_OPEN,
	ST_M2M_RUN,
	/* for output node */
	ST_OUTPUT_OPEN,
	ST_OUTPUT_STREAMON,
	/* for capture node */
	ST_CAPT_OPEN,
	ST_CAPT_PEND,
	ST_CAPT_RUN,
	ST_CAPT_STREAM,
	ST_CAPT_PIPE_STREAM,
	ST_CAPT_SUSPENDED,
	ST_CAPT_SHUT,
	ST_CAPT_APPLY_CFG,
	ST_CAPT_JPEG,
};

enum gsc_cap_input_entity {
	GSC_IN_NONE,
	GSC_IN_FLITE_PREVIEW,
	GSC_IN_FLITE_CAMCORDING,
	GSC_IN_FIMD_WRITEBACK,
};

enum gsc_irq {
	GSC_OR_IRQ = 17,
	GSC_DONE_IRQ = 16,
};

/**
 * enum gsc_datapath - the path of data used for gscaler
 * @GSC_CAMERA: from camera
 * @GSC_DMA: from/to DMA
 * @GSC_LOCAL: to local path
 * @GSC_WRITEBACK: from FIMD
 */
enum gsc_datapath {
	GSC_CAMERA = 0x1,
	GSC_DMA,
	GSC_MIXER,
	GSC_FIMD,
	GSC_WRITEBACK,
};

enum gsc_yuv_fmt {
	GSC_LSB_Y = 0x10,
	GSC_LSB_C,
	GSC_CBCR = 0x20,
	GSC_CRCB,
};

#define fh_to_ctx(__fh) container_of(__fh, struct gsc_ctx, fh)

#define is_rgb(img) ((img == V4L2_PIX_FMT_RGB565X) | (img == V4L2_PIX_FMT_RGB32))
#define is_yuv422(img) ((img == V4L2_PIX_FMT_YUYV) | (img == V4L2_PIX_FMT_UYVY) | \
		     (img == V4L2_PIX_FMT_VYUY) | (img == V4L2_PIX_FMT_YVYU) | \
		     (img == V4L2_PIX_FMT_YUV422P) | (img == V4L2_PIX_FMT_NV16) | \
		     (img == V4L2_PIX_FMT_NV61))
#define is_yuv420(img) ((img == V4L2_PIX_FMT_YUV420) | (img == V4L2_PIX_FMT_YVU420) | \
		     (img == V4L2_PIX_FMT_NV12) | (img == V4L2_PIX_FMT_NV21) | \
		     (img == V4L2_PIX_FMT_NV12M) | (img == V4L2_PIX_FMT_NV21M) | \
		     (img == V4L2_PIX_FMT_YUV420M) | (img == V4L2_PIX_FMT_YVU420M) | \
		     (img == V4L2_PIX_FMT_NV12MT_16X16))

#define gsc_m2m_run(dev) test_bit(ST_M2M_RUN, &(dev)->state)
#define gsc_m2m_opened(dev) test_bit(ST_M2M_OPEN, &(dev)->state)
#define gsc_out_run(dev) test_bit(ST_OUTPUT_STREAMON, &(dev)->state)
#define gsc_out_opened(dev) test_bit(ST_OUTPUT_OPEN, &(dev)->state)
#define gsc_cap_opened(dev) test_bit(ST_CAPT_OPEN, &(dev)->state)
#define gsc_cap_active(dev) test_bit(ST_CAPT_RUN, &(dev)->state)

#define ctrl_to_ctx(__ctrl) \
	container_of((__ctrl)->handler, struct gsc_ctx, ctrl_handler)
#define entity_data_to_gsc(data) \
	container_of(data, struct gsc_dev, md_data)
#define gsc_capture_get_frame(ctx, pad)\
	((pad == GSC_PAD_SINK) ? &ctx->s_frame : &ctx->d_frame)
/**
 * struct gsc_fmt - the driver's internal color format data
 * @mbus_code: Media Bus pixel code, -1 if not applicable
 * @name: format description
 * @pixelformat: the fourcc code for this format, 0 if not applicable
 * @yorder: Y/C order
 * @corder: Chrominance order control
 * @num_planes: number of physically non-contiguous data planes
 * @nr_comp: number of physically contiguous data planes
 * @depth: per plane driver's private 'number of bits per pixel'
 * @flags: flags indicating which operation mode format applies to
 */
struct gsc_fmt {
	enum v4l2_mbus_pixelcode mbus_code;
	char	*name;
	u32	pixelformat;
	u32	yorder;
	u32	corder;
	u16	num_planes;
	u16	nr_comp;
	u8	depth[VIDEO_MAX_PLANES];
	u32	flags;
};

/**
 * struct gsc_input_buf - the driver's video buffer
 * @vb:	videobuf2 buffer
 * @list : linked list structure for buffer queue
 * @idx : index of G-Scaler input buffer
 */
struct gsc_input_buf {
	struct vb2_buffer	vb;
	struct list_head	list;
	int			idx;
};

/**
 * struct gsc_addr - the G-Scaler physical address set
 * @y:	 luminance plane address
 * @cb:	 Cb plane address
 * @cr:	 Cr plane address
 */
struct gsc_addr {
	dma_addr_t	y;
	dma_addr_t	cb;
	dma_addr_t	cr;
};

/* struct gsc_ctrls - the G-Scaler control set
 * @rotate: rotation degree
 * @hflip: horizontal flip
 * @vflip: vertical flip
 * @global_alpha: the alpha value of current frame
 * @cacheable: cacheability of current frame
 * @layer_blend_en: enable mixer layer alpha blending
 * @layer_alpha: set alpha value for mixer layer
 * @pixel_blend_en: enable mixer pixel alpha blending
 * @chroma_en: enable chromakey
 * @chroma_val:	set value for chromakey
 * @csc_eq_mode: mode to select csc equation of current frame
 * @csc_eq: csc equation of current frame
 * @csc_range: csc range of current frame
 */
struct gsc_ctrls {
	struct v4l2_ctrl	*rotate;
	struct v4l2_ctrl	*hflip;
	struct v4l2_ctrl	*vflip;
	struct v4l2_ctrl	*global_alpha;
	struct v4l2_ctrl	*cacheable;
	struct v4l2_ctrl	*layer_blend_en;
	struct v4l2_ctrl	*layer_alpha;
	struct v4l2_ctrl	*pixel_blend_en;
	struct v4l2_ctrl	*chroma_en;
	struct v4l2_ctrl	*chroma_val;
	struct v4l2_ctrl	*csc_eq_mode;
	struct v4l2_ctrl	*csc_eq;
	struct v4l2_ctrl	*csc_range;
};

/**
 * struct gsc_scaler - the configuration data for G-Scaler inetrnal scaler
 * @pre_shfactor:	pre sclaer shift factor
 * @pre_hratio:		horizontal ratio of the prescaler
 * @pre_vratio:		vertical ratio of the prescaler
 * @main_hratio:	the main scaler's horizontal ratio
 * @main_vratio:	the main scaler's vertical ratio
 */
struct gsc_scaler {
	u32	pre_shfactor;
	u32	pre_hratio;
	u32	pre_vratio;
	unsigned long main_hratio;
	unsigned long main_vratio;
};

struct gsc_dev;

struct gsc_ctx;

/**
 * struct gsc_frame - source/target frame properties
 * @f_width:	SRC : SRCIMG_WIDTH, DST : OUTPUTDMA_WHOLE_IMG_WIDTH
 * @f_height:	SRC : SRCIMG_HEIGHT, DST : OUTPUTDMA_WHOLE_IMG_HEIGHT
 * @crop:	cropped(source)/scaled(destination) size
 * @payload:	image size in bytes (w x h x bpp)
 * @addr:	image frame buffer physical addresses
 * @fmt:	G-scaler color format pointer
 * @cacheable:	frame's cacheability
 * @alph:	frame's alpha value
 */
struct gsc_frame {
	u32	f_width;
	u32	f_height;
	struct v4l2_rect	crop;
	unsigned long payload[VIDEO_MAX_PLANES];
	struct gsc_addr		addr;
	struct gsc_fmt		*fmt;
	bool			cacheable;
	u8	alpha;
};

struct gsc_sensor_info {
	struct exynos_isp_info *pdata;
	struct v4l2_subdev *sd;
	struct clk *camclk;
};

struct gsc_capture_device {
	struct gsc_ctx			*ctx;
	struct video_device		*vfd;
	struct v4l2_subdev		*sd_cap;
	struct v4l2_subdev		*sd_disp;
	struct v4l2_subdev		*sd_flite[FLITE_MAX_ENTITIES];
	struct v4l2_subdev		*sd_csis[CSIS_MAX_ENTITIES];
	struct gsc_sensor_info		sensor[SENSOR_MAX_ENTITIES];
	struct media_pad		vd_pad;
	struct media_pad		sd_pads[GSC_PADS_NUM];
	struct v4l2_mbus_framefmt	mbus_fmt[GSC_PADS_NUM];
	struct vb2_queue		vbq;
	int				active_buf_cnt;
	int				buf_index;
	int				input_index;
	int				refcnt;
	u32				frame_cnt;
	u32				reqbufs_cnt;
	enum gsc_cap_input_entity	input;
	u32				cam_index;
	bool				user_subdev_api;
};

/**
 * struct gsc_output_device - v4l2 output device data
 * @vfd: the video device node for v4l2 output mode
 * @alloc_ctx: v4l2 memory-to-memory device data
 * @ctx: hardware context data
 * @sd: v4l2 subdev pointer of gscaler
 * @vbq: videobuf2 queue of gscaler output device
 * @vb_pad: the pad of gscaler video entity
 * @sd_pads: pads of gscaler subdev entity
 * @active_buf_q: linked list structure of input buffer
 * @req_cnt: the number of requested buffer
 */
struct gsc_output_device {
	struct video_device	*vfd;
	struct vb2_alloc_ctx	*alloc_ctx;
	struct gsc_ctx		*ctx;
	struct v4l2_subdev	*sd;
	struct vb2_queue	vbq;
	struct media_pad	vd_pad;
	struct media_pad	sd_pads[GSC_PADS_NUM];
	struct list_head	active_buf_q;
	int			req_cnt;
};

/**
 * struct gsc_m2m_device - v4l2 memory-to-memory device data
 * @vfd: the video device node for v4l2 m2m mode
 * @m2m_dev: v4l2 memory-to-memory device data
 * @ctx: hardware context data
 * @refcnt: the reference counter
 */
struct gsc_m2m_device {
	struct video_device	*vfd;
	struct v4l2_m2m_dev	*m2m_dev;
	struct gsc_ctx		*ctx;
	int			refcnt;
};

/**
 *  struct gsc_pix_max - image pixel size limits in various IP configurations
 *
 *  @org_scaler_bypass_w: max pixel width when the scaler is disabled
 *  @org_scaler_bypass_h: max pixel height when the scaler is disabled
 *  @org_scaler_input_w: max pixel width when the scaler is enabled
 *  @org_scaler_input_h: max pixel height when the scaler is enabled
 *  @real_rot_dis_w: max pixel src cropped height with the rotator is off
 *  @real_rot_dis_h: max pixel src croppped width with the rotator is off
 *  @real_rot_en_w: max pixel src cropped width with the rotator is on
 *  @real_rot_en_h: max pixel src cropped height with the rotator is on
 *  @target_rot_dis_w: max pixel dst scaled width with the rotator is off
 *  @target_rot_dis_h: max pixel dst scaled height with the rotator is off
 *  @target_rot_en_w: max pixel dst scaled width with the rotator is on
 *  @target_rot_en_h: max pixel dst scaled height with the rotator is on
 */
struct gsc_pix_max {
	u16 org_scaler_bypass_w;
	u16 org_scaler_bypass_h;
	u16 org_scaler_input_w;
	u16 org_scaler_input_h;
	u16 real_rot_dis_w;
	u16 real_rot_dis_h;
	u16 real_rot_en_w;
	u16 real_rot_en_h;
	u16 target_rot_dis_w;
	u16 target_rot_dis_h;
	u16 target_rot_en_w;
	u16 target_rot_en_h;
};

/**
 *  struct gsc_pix_min - image pixel size limits in various IP configurations
 *
 *  @org_w: minimum source pixel width
 *  @org_h: minimum source pixel height
 *  @real_w: minimum input crop pixel width
 *  @real_h: minimum input crop pixel height
 *  @target_rot_dis_w: minimum output scaled pixel height when rotator is off
 *  @target_rot_dis_h: minimum output scaled pixel height when rotator is off
 *  @target_rot_en_w: minimum output scaled pixel height when rotator is on
 *  @target_rot_en_h: minimum output scaled pixel height when rotator is on
 */
struct gsc_pix_min {
	u16 org_w;
	u16 org_h;
	u16 real_w;
	u16 real_h;
	u16 target_rot_dis_w;
	u16 target_rot_dis_h;
	u16 target_rot_en_w;
	u16 target_rot_en_h;
};

struct gsc_pix_align {
	u16 org_h;
	u16 org_w;
	u16 offset_h;
	u16 real_w;
	u16 real_h;
	u16 target_w;
	u16 target_h;
};

/**
 * struct gsc_variant - G-Scaler variant information
 */
struct gsc_variant {
	struct gsc_pix_max *pix_max;
	struct gsc_pix_min *pix_min;
	struct gsc_pix_align *pix_align;
	u16		in_buf_cnt;
	u16		out_buf_cnt;
	u16		sc_up_max;
	u16		sc_down_max;
	u16		poly_sc_down_max;
	u16		pre_sc_down_max;
	u16		local_sc_down;
};

/**
 * struct gsc_driverdata - per device type driver data for init time.
 *
 * @variant: the variant information for this driver.
 * @lclk_frequency: g-scaler clock frequency
 * @num_entities: the number of g-scalers
 */
struct gsc_driverdata {
	struct gsc_variant *variant[GSC_MAX_DEVS];
	unsigned long	lclk_frequency;
	int		num_entities;
};

struct gsc_vb2 {
	const struct vb2_mem_ops *ops;
	void *(*init)(struct gsc_dev *gsc);
	void (*cleanup)(void *alloc_ctx);

	unsigned long (*plane_addr)(struct vb2_buffer *vb, u32 plane_no);

	void (*resume)(void *alloc_ctx);
	void (*suspend)(void *alloc_ctx);

	int (*cache_flush)(struct vb2_buffer *vb, u32 num_planes);
	void (*set_cacheable)(void *alloc_ctx, bool cacheable);
	void (*set_sharable)(void *alloc_ctx, bool sharable);
};

struct gsc_pipeline {
	struct media_pipeline *pipe;
	struct v4l2_subdev *sd_gsc;
	struct v4l2_subdev *disp;
	struct v4l2_subdev *flite;
	struct v4l2_subdev *csis;
	struct v4l2_subdev *sensor;
};

/**
 * struct gsc_dev - abstraction for G-Scaler entity
 * @slock:	the spinlock protecting this data structure
 * @lock:	the mutex protecting this data structure
 * @pdev:	pointer to the G-Scaler platform device
 * @variant:	the IP variant information
 * @id:		g_scaler device index (0..GSC_MAX_DEVS)
 * @regs:	the mapped hardware registers
 * @regs_res:	the resource claimed for IO registers
 * @irq:	G-scaler interrupt number
 * @irq_queue:	interrupt handler waitqueue
 * @m2m:	memory-to-memory V4L2 device information
 * @out:	memory-to-local V4L2 output device information
 * @state:	flags used to synchronize m2m and capture mode operation
 * @alloc_ctx:	videobuf2 memory allocator context
 * @vb2:	videobuf2 memory allocator call-back functions
 * @mdev:	pointer to exynos media device
 * @pipeline:	pointer to subdevs that are connected with gscaler
 */
struct gsc_dev {
	spinlock_t			slock;
	struct mutex			lock;
	struct platform_device		*pdev;
	struct gsc_variant		*variant;
	u16				id;
	struct clk			*clock;
	void __iomem			*regs;
	struct resource			*regs_res;
	int				irq;
	wait_queue_head_t		irq_queue;
	struct work_struct		work_struct;
	struct workqueue_struct		*irq_workqueue;
	struct gsc_m2m_device		m2m;
	struct gsc_output_device	out;
	struct gsc_capture_device	cap;
	struct exynos_platform_gscaler	*pdata;
	unsigned long			state;
	struct vb2_alloc_ctx		*alloc_ctx;
	const struct gsc_vb2		*vb2;
	struct exynos_md		*mdev[2];
	struct gsc_pipeline		pipeline;
	struct exynos_entity_data	md_data;
};

/**
 * gsc_ctx - the device context data
 * @slock:		spinlock protecting this data structure
 * @s_frame:		source frame properties
 * @d_frame:		destination frame properties
 * @in_path:		input mode (DMA or camera)
 * @out_path:		output mode (DMA or FIFO)
 * @scaler:		image scaler properties
 * @flags:		additional flags for image conversion
 * @state:		flags to keep track of user configuration
 * @gsc_dev:		the g-scaler device this context applies to
 * @m2m_ctx:		memory-to-memory device context
 * @fh:                 v4l2 file handle
 * @ctrl_handler:       v4l2 controls handler
 * @ctrls_rdy:          true if the control handler is initialized
 * @gsc_ctrls		G-Scaler control set
 * @m2m_ctx:		memory-to-memory device context
 */
struct gsc_ctx {
	spinlock_t		slock;
	struct gsc_frame	s_frame;
	struct gsc_frame	d_frame;
	enum gsc_datapath	in_path;
	enum gsc_datapath	out_path;
	struct gsc_scaler	scaler;
	u32			flags;
	u32			state;
	struct gsc_dev		*gsc_dev;
	struct v4l2_m2m_ctx	*m2m_ctx;
	struct v4l2_fh		fh;
	struct v4l2_ctrl_handler ctrl_handler;
	struct gsc_ctrls	gsc_ctrls;
	bool			ctrls_rdy;
};

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
extern const struct gsc_vb2 gsc_vb2_cma;
#elif defined(CONFIG_VIDEOBUF2_ION)
extern const struct gsc_vb2 gsc_vb2_ion;
#endif

void gsc_set_prefbuf(struct gsc_dev *gsc, struct gsc_frame frm);
void gsc_clk_release(struct gsc_dev *gsc);
int gsc_register_m2m_device(struct gsc_dev *gsc);
void gsc_unregister_m2m_device(struct gsc_dev *gsc);
int gsc_register_output_device(struct gsc_dev *gsc);
void gsc_unregister_output_device(struct gsc_dev *gsc);
int gsc_register_capture_device(struct gsc_dev *gsc);
void gsc_unregister_capture_device(struct gsc_dev *gsc);

u32 get_plane_size(struct gsc_frame *fr, unsigned int plane);
char gsc_total_fmts(void);
struct gsc_fmt *get_format(int index);
struct gsc_fmt *find_format(u32 *pixelformat, u32 *mbus_code, int index);
int gsc_enum_fmt_mplane(struct v4l2_fmtdesc *f);
int gsc_try_fmt_mplane(struct gsc_ctx *ctx, struct v4l2_format *f);
void gsc_set_frame_size(struct gsc_frame *frame, int width, int height);
int gsc_g_fmt_mplane(struct gsc_ctx *ctx, struct v4l2_format *f);
void gsc_check_crop_change(u32 tmp_w, u32 tmp_h, u32 *w, u32 *h);
int gsc_g_crop(struct gsc_ctx *ctx, struct v4l2_crop *cr);
int gsc_try_crop(struct gsc_ctx *ctx, struct v4l2_crop *cr);
int gsc_cal_prescaler_ratio(struct gsc_variant *var, u32 src, u32 dst, u32 *ratio);
void gsc_get_prescaler_shfactor(u32 hratio, u32 vratio, u32 *sh);
void gsc_check_src_scale_info(struct gsc_variant *var, struct gsc_frame *s_frame,
			      u32 *wratio, u32 tx, u32 ty, u32 *hratio);
int gsc_check_scaler_ratio(struct gsc_variant *var, int sw, int sh, int dw,
			   int dh, int rot, int out_path);
int gsc_set_scaler_info(struct gsc_ctx *ctx);
int gsc_ctrls_create(struct gsc_ctx *ctx);
void gsc_ctrls_delete(struct gsc_ctx *ctx);
int gsc_out_hw_set(struct gsc_ctx *ctx);
int gsc_out_set_in_addr(struct gsc_dev *gsc, struct gsc_ctx *ctx,
		   struct gsc_input_buf *buf, int index);
int gsc_prepare_addr(struct gsc_ctx *ctx, struct vb2_buffer *vb,
		     struct gsc_frame *frame, struct gsc_addr *addr);
int gsc_out_link_validate(const struct media_pad *source,
			  const struct media_pad *sink);
int gsc_pipeline_s_stream(struct gsc_dev *gsc, bool on);

static inline void gsc_ctx_state_lock_set(u32 state, struct gsc_ctx *ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&ctx->slock, flags);
	ctx->state |= state;
	spin_unlock_irqrestore(&ctx->slock, flags);
}

static inline void gsc_ctx_state_lock_clear(u32 state, struct gsc_ctx *ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&ctx->slock, flags);
	ctx->state &= ~state;
	spin_unlock_irqrestore(&ctx->slock, flags);
}

static inline int get_win_num(struct gsc_dev *dev)
{
	return (dev->id == 3) ? 2 : dev->id;
}

static inline int is_tiled(struct gsc_fmt *fmt)
{
	return fmt->pixelformat == V4L2_PIX_FMT_NV12MT_16X16;
}

static inline int is_output(enum v4l2_buf_type type)
{
	return (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ||
		type == V4L2_BUF_TYPE_VIDEO_OUTPUT) ? 1 : 0;
}

static inline void gsc_hw_enable_control(struct gsc_dev *dev, bool on)
{
	u32 cfg = readl(dev->regs + GSC_ENABLE);

	if (on)
		cfg |= GSC_ENABLE_ON;
	else
		cfg &= ~GSC_ENABLE_ON;

	writel(cfg, dev->regs + GSC_ENABLE);
}

static inline int gsc_hw_get_irq_status(struct gsc_dev *dev)
{
	u32 cfg = readl(dev->regs + GSC_IRQ);
	if (cfg & (1 << GSC_OR_IRQ))
		return GSC_OR_IRQ;
	else
		return GSC_DONE_IRQ;

}

static inline void gsc_hw_clear_irq(struct gsc_dev *dev, int irq)
{
	u32 cfg = readl(dev->regs + GSC_IRQ);
	if (irq == GSC_OR_IRQ)
		cfg |= GSC_IRQ_STATUS_OR_IRQ;
	else if (irq == GSC_DONE_IRQ)
		cfg |= GSC_IRQ_STATUS_OR_FRM_DONE;
	writel(cfg, dev->regs + GSC_IRQ);
}

static inline void gsc_lock(struct vb2_queue *vq)
{
	struct gsc_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_lock(&ctx->gsc_dev->lock);
}

static inline void gsc_unlock(struct vb2_queue *vq)
{
	struct gsc_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_unlock(&ctx->gsc_dev->lock);
}

static inline bool gsc_ctx_state_is_set(u32 mask, struct gsc_ctx *ctx)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&ctx->slock, flags);
	ret = (ctx->state & mask) == mask;
	spin_unlock_irqrestore(&ctx->slock, flags);
	return ret;
}

static inline struct gsc_frame *ctx_get_frame(struct gsc_ctx *ctx,
					      enum v4l2_buf_type type)
{
	struct gsc_frame *frame;

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE == type) {
		frame = &ctx->s_frame;
	} else if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == type) {
		frame = &ctx->d_frame;
	} else {
		gsc_err("Wrong buffer/video queue type (%d)", type);
		return ERR_PTR(-EINVAL);
	}

	return frame;
}

static inline struct gsc_input_buf *
active_queue_pop(struct gsc_output_device *vid_out, struct gsc_dev *dev)
{
	struct gsc_input_buf *buf;

	buf = list_entry(vid_out->active_buf_q.next, struct gsc_input_buf, list);
	return buf;
}

static inline void active_queue_push(struct gsc_output_device *vid_out,
				     struct gsc_input_buf *buf, struct gsc_dev *dev)
{
	unsigned long flags;
	spin_lock_irqsave(&dev->slock, flags);
	list_add_tail(&buf->list, &vid_out->active_buf_q);
	spin_unlock_irqrestore(&dev->slock, flags);
}

static inline struct gsc_dev *entity_to_gsc(struct media_entity *me)
{
	struct v4l2_subdev *sd;

	sd = container_of(me, struct v4l2_subdev, entity);
	return entity_data_to_gsc(v4l2_get_subdevdata(sd));
}

static inline void user_to_drv(struct v4l2_ctrl *ctrl, s32 value)
{
	ctrl->cur.val = ctrl->val = value;
}

void gsc_hw_set_sw_reset(struct gsc_dev *dev);
void gsc_hw_set_one_frm_mode(struct gsc_dev *dev, bool mask);
void gsc_hw_set_frm_done_irq_mask(struct gsc_dev *dev, bool mask);
void gsc_hw_set_overflow_irq_mask(struct gsc_dev *dev, bool mask);
void gsc_hw_set_gsc_irq_enable(struct gsc_dev *dev, bool mask);
void gsc_hw_set_input_buf_mask_all(struct gsc_dev *dev);
void gsc_hw_set_output_buf_mask_all(struct gsc_dev *dev);
void gsc_hw_set_input_buf_masking(struct gsc_dev *dev, u32 shift, bool enable);
void gsc_hw_set_output_buf_masking(struct gsc_dev *dev, u32 shift, bool enable);
void gsc_hw_set_input_addr(struct gsc_dev *dev, struct gsc_addr *addr, int index);
void gsc_hw_set_output_addr(struct gsc_dev *dev, struct gsc_addr *addr, int index);
void gsc_hw_set_input_path(struct gsc_ctx *ctx);
void gsc_hw_set_in_size(struct gsc_ctx *ctx);
void gsc_hw_set_in_image_rgb(struct gsc_ctx *ctx);
void gsc_hw_set_in_image_format(struct gsc_ctx *ctx);
void gsc_hw_set_output_path(struct gsc_ctx *ctx);
void gsc_hw_set_out_size(struct gsc_ctx *ctx);
void gsc_hw_set_out_image_rgb(struct gsc_ctx *ctx);
void gsc_hw_set_out_image_format(struct gsc_ctx *ctx);
void gsc_hw_set_prescaler(struct gsc_ctx *ctx);
void gsc_hw_set_mainscaler(struct gsc_ctx *ctx);
void gsc_hw_set_rotation(struct gsc_ctx *ctx);
void gsc_hw_set_global_alpha(struct gsc_ctx *ctx);
void gsc_hw_set_sfr_update(struct gsc_ctx *ctx);
void gsc_hw_set_local_dst(int id, bool on);
void gsc_hw_set_sysreg_writeback(struct gsc_ctx *ctx);
void gsc_hw_set_sysreg_camif(bool on);

int gsc_hw_get_input_buf_mask_status(struct gsc_dev *dev);
int gsc_hw_get_done_input_buf_index(struct gsc_dev *dev);
int gsc_hw_get_done_output_buf_index(struct gsc_dev *dev);
int gsc_hw_get_nr_unmask_bits(struct gsc_dev *dev);
int gsc_wait_reset(struct gsc_dev *dev);
int gsc_wait_operating(struct gsc_dev *dev);
int gsc_wait_stop(struct gsc_dev *dev);

void gsc_disp_fifo_sw_reset(struct gsc_dev *dev);
void gsc_pixelasync_sw_reset(struct gsc_dev *dev);


#endif /* GSC_CORE_H_ */
