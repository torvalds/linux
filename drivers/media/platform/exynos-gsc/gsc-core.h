/*
 * Copyright (c) 2011 - 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * header file for Samsung EXYNOS5 SoC series G-Scaler driver

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
#include <media/videobuf2-core.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-mediabus.h>
#include <media/videobuf2-dma-contig.h>

#include "gsc-regs.h"

#define CONFIG_VB2_GSC_DMA_CONTIG	1
#define GSC_MODULE_NAME			"exynos-gsc"

#define GSC_SHUTDOWN_TIMEOUT		((100*HZ)/1000)
#define GSC_MAX_DEVS			4
#define GSC_M2M_BUF_NUM			0
#define GSC_MAX_CTRL_NUM		10
#define GSC_SC_ALIGN_4			4
#define GSC_SC_ALIGN_2			2
#define DEFAULT_CSC_EQ			1
#define DEFAULT_CSC_RANGE		1

#define GSC_PARAMS			(1 << 0)
#define GSC_SRC_FMT			(1 << 1)
#define GSC_DST_FMT			(1 << 2)
#define GSC_CTX_M2M			(1 << 3)
#define GSC_CTX_STOP_REQ		(1 << 6)
#define	GSC_CTX_ABORT			(1 << 7)

enum gsc_dev_flags {
	/* for global */
	ST_SUSPEND,

	/* for m2m node */
	ST_M2M_OPEN,
	ST_M2M_RUN,
	ST_M2M_PEND,
	ST_M2M_SUSPENDED,
	ST_M2M_SUSPENDING,
};

enum gsc_irq {
	GSC_IRQ_DONE,
	GSC_IRQ_OVERRUN
};

/**
 * enum gsc_datapath - the path of data used for G-Scaler
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

enum gsc_color_fmt {
	GSC_RGB = 0x1,
	GSC_YUV420 = 0x2,
	GSC_YUV422 = 0x4,
	GSC_YUV444 = 0x8,
};

enum gsc_yuv_fmt {
	GSC_LSB_Y = 0x10,
	GSC_LSB_C,
	GSC_CBCR = 0x20,
	GSC_CRCB,
};

#define fh_to_ctx(__fh) container_of(__fh, struct gsc_ctx, fh)
#define is_rgb(x) (!!((x) & 0x1))
#define is_yuv420(x) (!!((x) & 0x2))
#define is_yuv422(x) (!!((x) & 0x4))

#define gsc_m2m_active(dev)	test_bit(ST_M2M_RUN, &(dev)->state)
#define gsc_m2m_pending(dev)	test_bit(ST_M2M_PEND, &(dev)->state)
#define gsc_m2m_opened(dev)	test_bit(ST_M2M_OPEN, &(dev)->state)

#define ctrl_to_ctx(__ctrl) \
	container_of((__ctrl)->handler, struct gsc_ctx, ctrl_handler)
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
	u32	color;
	u32	yorder;
	u32	corder;
	u16	num_planes;
	u16	num_comp;
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
	dma_addr_t y;
	dma_addr_t cb;
	dma_addr_t cr;
};

/* struct gsc_ctrls - the G-Scaler control set
 * @rotate: rotation degree
 * @hflip: horizontal flip
 * @vflip: vertical flip
 * @global_alpha: the alpha value of current frame
 */
struct gsc_ctrls {
	struct v4l2_ctrl *rotate;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *global_alpha;
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
	u32 pre_shfactor;
	u32 pre_hratio;
	u32 pre_vratio;
	u32 main_hratio;
	u32 main_vratio;
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
 * @fmt:	G-Scaler color format pointer
 * @colorspace: value indicating v4l2_colorspace
 * @alpha:	frame's alpha value
 */
struct gsc_frame {
	u32 f_width;
	u32 f_height;
	struct v4l2_rect crop;
	unsigned long payload[VIDEO_MAX_PLANES];
	struct gsc_addr	addr;
	const struct gsc_fmt *fmt;
	u32 colorspace;
	u8 alpha;
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
 * @lclk_frequency: G-Scaler clock frequency
 * @num_entities: the number of g-scalers
 */
struct gsc_driverdata {
	struct gsc_variant *variant[GSC_MAX_DEVS];
	unsigned long	lclk_frequency;
	int		num_entities;
};

/**
 * struct gsc_dev - abstraction for G-Scaler entity
 * @slock:	the spinlock protecting this data structure
 * @lock:	the mutex protecting this data structure
 * @pdev:	pointer to the G-Scaler platform device
 * @variant:	the IP variant information
 * @id:		G-Scaler device index (0..GSC_MAX_DEVS)
 * @clock:	clocks required for G-Scaler operation
 * @regs:	the mapped hardware registers
 * @irq_queue:	interrupt handler waitqueue
 * @m2m:	memory-to-memory V4L2 device information
 * @state:	flags used to synchronize m2m and capture mode operation
 * @alloc_ctx:	videobuf2 memory allocator context
 * @vdev:	video device for G-Scaler instance
 */
struct gsc_dev {
	spinlock_t			slock;
	struct mutex			lock;
	struct platform_device		*pdev;
	struct gsc_variant		*variant;
	u16				id;
	struct clk			*clock;
	void __iomem			*regs;
	wait_queue_head_t		irq_queue;
	struct gsc_m2m_device		m2m;
	struct exynos_platform_gscaler	*pdata;
	unsigned long			state;
	struct vb2_alloc_ctx		*alloc_ctx;
	struct video_device		vdev;
	struct v4l2_device		v4l2_dev;
};

/**
 * gsc_ctx - the device context data
 * @s_frame:		source frame properties
 * @d_frame:		destination frame properties
 * @in_path:		input mode (DMA or camera)
 * @out_path:		output mode (DMA or FIFO)
 * @scaler:		image scaler properties
 * @flags:		additional flags for image conversion
 * @state:		flags to keep track of user configuration
 * @gsc_dev:		the G-Scaler device this context applies to
 * @m2m_ctx:		memory-to-memory device context
 * @fh:                 v4l2 file handle
 * @ctrl_handler:       v4l2 controls handler
 * @gsc_ctrls		G-Scaler control set
 * @ctrls_rdy:          true if the control handler is initialized
 */
struct gsc_ctx {
	struct gsc_frame	s_frame;
	struct gsc_frame	d_frame;
	enum gsc_datapath	in_path;
	enum gsc_datapath	out_path;
	struct gsc_scaler	scaler;
	u32			flags;
	u32			state;
	int			rotation;
	unsigned int		hflip:1;
	unsigned int		vflip:1;
	struct gsc_dev		*gsc_dev;
	struct v4l2_m2m_ctx	*m2m_ctx;
	struct v4l2_fh		fh;
	struct v4l2_ctrl_handler ctrl_handler;
	struct gsc_ctrls	gsc_ctrls;
	bool			ctrls_rdy;
};

void gsc_set_prefbuf(struct gsc_dev *gsc, struct gsc_frame *frm);
int gsc_register_m2m_device(struct gsc_dev *gsc);
void gsc_unregister_m2m_device(struct gsc_dev *gsc);
void gsc_m2m_job_finish(struct gsc_ctx *ctx, int vb_state);

u32 get_plane_size(struct gsc_frame *fr, unsigned int plane);
const struct gsc_fmt *get_format(int index);
const struct gsc_fmt *find_fmt(u32 *pixelformat, u32 *mbus_code, u32 index);
int gsc_enum_fmt_mplane(struct v4l2_fmtdesc *f);
int gsc_try_fmt_mplane(struct gsc_ctx *ctx, struct v4l2_format *f);
void gsc_set_frame_size(struct gsc_frame *frame, int width, int height);
int gsc_g_fmt_mplane(struct gsc_ctx *ctx, struct v4l2_format *f);
void gsc_check_crop_change(u32 tmp_w, u32 tmp_h, u32 *w, u32 *h);
int gsc_g_crop(struct gsc_ctx *ctx, struct v4l2_crop *cr);
int gsc_try_crop(struct gsc_ctx *ctx, struct v4l2_crop *cr);
int gsc_cal_prescaler_ratio(struct gsc_variant *var, u32 src, u32 dst,
							u32 *ratio);
void gsc_get_prescaler_shfactor(u32 hratio, u32 vratio, u32 *sh);
void gsc_check_src_scale_info(struct gsc_variant *var,
				struct gsc_frame *s_frame,
				u32 *wratio, u32 tx, u32 ty, u32 *hratio);
int gsc_check_scaler_ratio(struct gsc_variant *var, int sw, int sh, int dw,
			   int dh, int rot, int out_path);
int gsc_set_scaler_info(struct gsc_ctx *ctx);
int gsc_ctrls_create(struct gsc_ctx *ctx);
void gsc_ctrls_delete(struct gsc_ctx *ctx);
int gsc_prepare_addr(struct gsc_ctx *ctx, struct vb2_buffer *vb,
		     struct gsc_frame *frame, struct gsc_addr *addr);

static inline void gsc_ctx_state_lock_set(u32 state, struct gsc_ctx *ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&ctx->gsc_dev->slock, flags);
	ctx->state |= state;
	spin_unlock_irqrestore(&ctx->gsc_dev->slock, flags);
}

static inline void gsc_ctx_state_lock_clear(u32 state, struct gsc_ctx *ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&ctx->gsc_dev->slock, flags);
	ctx->state &= ~state;
	spin_unlock_irqrestore(&ctx->gsc_dev->slock, flags);
}

static inline int is_tiled(const struct gsc_fmt *fmt)
{
	return fmt->pixelformat == V4L2_PIX_FMT_NV12MT_16X16;
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
	if (cfg & GSC_IRQ_STATUS_OR_IRQ)
		return GSC_IRQ_OVERRUN;
	else
		return GSC_IRQ_DONE;

}

static inline void gsc_hw_clear_irq(struct gsc_dev *dev, int irq)
{
	u32 cfg = readl(dev->regs + GSC_IRQ);
	if (irq == GSC_IRQ_OVERRUN)
		cfg |= GSC_IRQ_STATUS_OR_IRQ;
	else if (irq == GSC_IRQ_DONE)
		cfg |= GSC_IRQ_STATUS_FRM_DONE_IRQ;
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

	spin_lock_irqsave(&ctx->gsc_dev->slock, flags);
	ret = (ctx->state & mask) == mask;
	spin_unlock_irqrestore(&ctx->gsc_dev->slock, flags);
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
		pr_err("Wrong buffer/video queue type (%d)", type);
		return ERR_PTR(-EINVAL);
	}

	return frame;
}

void gsc_hw_set_sw_reset(struct gsc_dev *dev);
int gsc_wait_reset(struct gsc_dev *dev);

void gsc_hw_set_frm_done_irq_mask(struct gsc_dev *dev, bool mask);
void gsc_hw_set_gsc_irq_enable(struct gsc_dev *dev, bool mask);
void gsc_hw_set_input_buf_masking(struct gsc_dev *dev, u32 shift, bool enable);
void gsc_hw_set_output_buf_masking(struct gsc_dev *dev, u32 shift, bool enable);
void gsc_hw_set_input_addr(struct gsc_dev *dev, struct gsc_addr *addr,
							int index);
void gsc_hw_set_output_addr(struct gsc_dev *dev, struct gsc_addr *addr,
							int index);
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

#endif /* GSC_CORE_H_ */
