/*
 * Copyright (c) 2010 Samsung Electronics
 *
 * Sylwester Nawrocki, <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_CORE_H_
#define FIMC_CORE_H_

/*#define DEBUG*/

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <media/videobuf-core.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-mediabus.h>
#include <media/s3c_fimc.h>

#include "regs-fimc.h"

#define err(fmt, args...) \
	printk(KERN_ERR "%s:%d: " fmt "\n", __func__, __LINE__, ##args)

#ifdef DEBUG
#define dbg(fmt, args...) \
	printk(KERN_DEBUG "%s:%d: " fmt "\n", __func__, __LINE__, ##args)
#else
#define dbg(fmt, args...)
#endif

/* Time to wait for next frame VSYNC interrupt while stopping operation. */
#define FIMC_SHUTDOWN_TIMEOUT	((100*HZ)/1000)
#define NUM_FIMC_CLOCKS		2
#define MODULE_NAME		"s5p-fimc"
#define FIMC_MAX_DEVS		4
#define FIMC_MAX_OUT_BUFS	4
#define SCALER_MAX_HRATIO	64
#define SCALER_MAX_VRATIO	64
#define DMA_MIN_SIZE		8

/* FIMC device state flags */
enum fimc_dev_flags {
	/* for m2m node */
	ST_IDLE,
	ST_OUTDMA_RUN,
	ST_M2M_PEND,
	/* for capture node */
	ST_CAPT_PEND,
	ST_CAPT_RUN,
	ST_CAPT_STREAM,
	ST_CAPT_SHUT,
};

#define fimc_m2m_active(dev) test_bit(ST_OUTDMA_RUN, &(dev)->state)
#define fimc_m2m_pending(dev) test_bit(ST_M2M_PEND, &(dev)->state)

#define fimc_capture_running(dev) test_bit(ST_CAPT_RUN, &(dev)->state)
#define fimc_capture_pending(dev) test_bit(ST_CAPT_PEND, &(dev)->state)

#define fimc_capture_active(dev) \
	(test_bit(ST_CAPT_RUN, &(dev)->state) || \
	 test_bit(ST_CAPT_PEND, &(dev)->state))

#define fimc_capture_streaming(dev) \
	test_bit(ST_CAPT_STREAM, &(dev)->state)

#define fimc_buf_finish(dev, vid_buf) do { \
	spin_lock(&(dev)->irqlock); \
	(vid_buf)->vb.state = VIDEOBUF_DONE; \
	spin_unlock(&(dev)->irqlock); \
	wake_up(&(vid_buf)->vb.done); \
} while (0)

enum fimc_datapath {
	FIMC_CAMERA,
	FIMC_DMA,
	FIMC_LCDFIFO,
	FIMC_WRITEBACK
};

enum fimc_color_fmt {
	S5P_FIMC_RGB565 = 0x10,
	S5P_FIMC_RGB666,
	S5P_FIMC_RGB888,
	S5P_FIMC_RGB30_LOCAL,
	S5P_FIMC_YCBCR420 = 0x20,
	S5P_FIMC_YCBCR422,
	S5P_FIMC_YCBYCR422,
	S5P_FIMC_YCRYCB422,
	S5P_FIMC_CBYCRY422,
	S5P_FIMC_CRYCBY422,
	S5P_FIMC_YCBCR444_LOCAL,
};

#define fimc_fmt_is_rgb(x) ((x) & 0x10)

/* Y/Cb/Cr components order at DMA output for 1 plane YCbCr 4:2:2 formats. */
#define	S5P_FIMC_OUT_CRYCBY	S5P_CIOCTRL_ORDER422_CRYCBY
#define	S5P_FIMC_OUT_CBYCRY	S5P_CIOCTRL_ORDER422_YCRYCB
#define	S5P_FIMC_OUT_YCRYCB	S5P_CIOCTRL_ORDER422_CBYCRY
#define	S5P_FIMC_OUT_YCBYCR	S5P_CIOCTRL_ORDER422_YCBYCR

/* Input Y/Cb/Cr components order for 1 plane YCbCr 4:2:2 color formats. */
#define	S5P_FIMC_IN_CRYCBY	S5P_MSCTRL_ORDER422_CRYCBY
#define	S5P_FIMC_IN_CBYCRY	S5P_MSCTRL_ORDER422_YCRYCB
#define	S5P_FIMC_IN_YCRYCB	S5P_MSCTRL_ORDER422_CBYCRY
#define	S5P_FIMC_IN_YCBYCR	S5P_MSCTRL_ORDER422_YCBYCR

/* Cb/Cr chrominance components order for 2 plane Y/CbCr 4:2:2 formats. */
#define	S5P_FIMC_LSB_CRCB	S5P_CIOCTRL_ORDER422_2P_LSB_CRCB

/* The embedded image effect selection */
#define	S5P_FIMC_EFFECT_ORIGINAL	S5P_CIIMGEFF_FIN_BYPASS
#define	S5P_FIMC_EFFECT_ARBITRARY	S5P_CIIMGEFF_FIN_ARBITRARY
#define	S5P_FIMC_EFFECT_NEGATIVE	S5P_CIIMGEFF_FIN_NEGATIVE
#define	S5P_FIMC_EFFECT_ARTFREEZE	S5P_CIIMGEFF_FIN_ARTFREEZE
#define	S5P_FIMC_EFFECT_EMBOSSING	S5P_CIIMGEFF_FIN_EMBOSSING
#define	S5P_FIMC_EFFECT_SIKHOUETTE	S5P_CIIMGEFF_FIN_SILHOUETTE

/* The hardware context state. */
#define	FIMC_PARAMS		(1 << 0)
#define	FIMC_SRC_ADDR		(1 << 1)
#define	FIMC_DST_ADDR		(1 << 2)
#define	FIMC_SRC_FMT		(1 << 3)
#define	FIMC_DST_FMT		(1 << 4)
#define	FIMC_CTX_M2M		(1 << 5)
#define	FIMC_CTX_CAP		(1 << 6)

/* Image conversion flags */
#define	FIMC_IN_DMA_ACCESS_TILED	(1 << 0)
#define	FIMC_IN_DMA_ACCESS_LINEAR	(0 << 0)
#define	FIMC_OUT_DMA_ACCESS_TILED	(1 << 1)
#define	FIMC_OUT_DMA_ACCESS_LINEAR	(0 << 1)
#define	FIMC_SCAN_MODE_PROGRESSIVE	(0 << 2)
#define	FIMC_SCAN_MODE_INTERLACED	(1 << 2)
/*
 * YCbCr data dynamic range for RGB-YUV color conversion.
 * Y/Cb/Cr: (0 ~ 255) */
#define	FIMC_COLOR_RANGE_WIDE		(0 << 3)
/* Y (16 ~ 235), Cb/Cr (16 ~ 240) */
#define	FIMC_COLOR_RANGE_NARROW		(1 << 3)

#define	FLIP_NONE			0
#define	FLIP_X_AXIS			1
#define	FLIP_Y_AXIS			2
#define	FLIP_XY_AXIS			(FLIP_X_AXIS | FLIP_Y_AXIS)

/**
 * struct fimc_fmt - the driver's internal color format data
 * @mbus_code: Media Bus pixel code, -1 if not applicable
 * @name: format description
 * @fourcc: the fourcc code for this format, 0 if not applicable
 * @color: the corresponding fimc_color_fmt
 * @depth: driver's private 'number of bits per pixel'
 * @buff_cnt: number of physically non-contiguous data planes
 * @planes_cnt: number of physically contiguous data planes
 */
struct fimc_fmt {
	enum v4l2_mbus_pixelcode mbus_code;
	char	*name;
	u32	fourcc;
	u32	color;
	u16	buff_cnt;
	u16	planes_cnt;
	u16	depth;
	u16	flags;
#define FMT_FLAGS_CAM	(1 << 0)
#define FMT_FLAGS_M2M	(1 << 1)
};

/**
 * struct fimc_dma_offset - pixel offset information for DMA
 * @y_h:	y value horizontal offset
 * @y_v:	y value vertical offset
 * @cb_h:	cb value horizontal offset
 * @cb_v:	cb value vertical offset
 * @cr_h:	cr value horizontal offset
 * @cr_v:	cr value vertical offset
 */
struct fimc_dma_offset {
	int	y_h;
	int	y_v;
	int	cb_h;
	int	cb_v;
	int	cr_h;
	int	cr_v;
};

/**
 * struct fimc_effect - the configuration data for the "Arbitrary" image effect
 * @type:	effect type
 * @pat_cb:	cr value when type is "arbitrary"
 * @pat_cr:	cr value when type is "arbitrary"
 */
struct fimc_effect {
	u32	type;
	u8	pat_cb;
	u8	pat_cr;
};

/**
 * struct fimc_scaler - the configuration data for FIMC inetrnal scaler
 *
 * @scaleup_h:		flag indicating scaling up horizontally
 * @scaleup_v:		flag indicating scaling up vertically
 * @copy_mode:		flag indicating transparent DMA transfer (no scaling
 *			and color format conversion)
 * @enabled:		flag indicating if the scaler is used
 * @hfactor:		horizontal shift factor
 * @vfactor:		vertical shift factor
 * @pre_hratio:		horizontal ratio of the prescaler
 * @pre_vratio:		vertical ratio of the prescaler
 * @pre_dst_width:	the prescaler's destination width
 * @pre_dst_height:	the prescaler's destination height
 * @main_hratio:	the main scaler's horizontal ratio
 * @main_vratio:	the main scaler's vertical ratio
 * @real_width:		source pixel (width - offset)
 * @real_height:	source pixel (height - offset)
 */
struct fimc_scaler {
	unsigned int scaleup_h:1;
	unsigned int scaleup_v:1;
	unsigned int copy_mode:1;
	unsigned int enabled:1;
	u32	hfactor;
	u32	vfactor;
	u32	pre_hratio;
	u32	pre_vratio;
	u32	pre_dst_width;
	u32	pre_dst_height;
	u32	main_hratio;
	u32	main_vratio;
	u32	real_width;
	u32	real_height;
};

/**
 * struct fimc_addr - the FIMC physical address set for DMA
 *
 * @y:	 luminance plane physical address
 * @cb:	 Cb plane physical address
 * @cr:	 Cr plane physical address
 */
struct fimc_addr {
	u32	y;
	u32	cb;
	u32	cr;
};

/**
 * struct fimc_vid_buffer - the driver's video buffer
 * @vb:    v4l videobuf buffer
 * @paddr: precalculated physical address set
 * @index: buffer index for the output DMA engine
 */
struct fimc_vid_buffer {
	struct videobuf_buffer	vb;
	struct fimc_addr	paddr;
	int			index;
};

/**
 * struct fimc_frame - source/target frame properties
 * @f_width:	image full width (virtual screen size)
 * @f_height:	image full height (virtual screen size)
 * @o_width:	original image width as set by S_FMT
 * @o_height:	original image height as set by S_FMT
 * @offs_h:	image horizontal pixel offset
 * @offs_v:	image vertical pixel offset
 * @width:	image pixel width
 * @height:	image pixel weight
 * @paddr:	image frame buffer physical addresses
 * @buf_cnt:	number of buffers depending on a color format
 * @size:	image size in bytes
 * @color:	color format
 * @dma_offset:	DMA offset in bytes
 */
struct fimc_frame {
	u32	f_width;
	u32	f_height;
	u32	o_width;
	u32	o_height;
	u32	offs_h;
	u32	offs_v;
	u32	width;
	u32	height;
	u32	size;
	struct fimc_addr	paddr;
	struct fimc_dma_offset	dma_offset;
	struct fimc_fmt		*fmt;
};

/**
 * struct fimc_m2m_device - v4l2 memory-to-memory device data
 * @vfd: the video device node for v4l2 m2m mode
 * @v4l2_dev: v4l2 device for m2m mode
 * @m2m_dev: v4l2 memory-to-memory device data
 * @ctx: hardware context data
 * @refcnt: the reference counter
 */
struct fimc_m2m_device {
	struct video_device	*vfd;
	struct v4l2_device	v4l2_dev;
	struct v4l2_m2m_dev	*m2m_dev;
	struct fimc_ctx		*ctx;
	int			refcnt;
};

/**
 * struct fimc_vid_cap - camera capture device information
 * @ctx: hardware context data
 * @vfd: video device node for camera capture mode
 * @v4l2_dev: v4l2_device struct to manage subdevs
 * @sd: pointer to camera sensor subdevice currently in use
 * @fmt: Media Bus format configured at selected image sensor
 * @pending_buf_q: the pending buffer queue head
 * @active_buf_q: the queue head of buffers scheduled in hardware
 * @vbq: the capture am video buffer queue
 * @active_buf_cnt: number of video buffers scheduled in hardware
 * @buf_index: index for managing the output DMA buffers
 * @frame_count: the frame counter for statistics
 * @reqbufs_count: the number of buffers requested in REQBUFS ioctl
 * @input_index: input (camera sensor) index
 * @refcnt: driver's private reference counter
 */
struct fimc_vid_cap {
	struct fimc_ctx			*ctx;
	struct video_device		*vfd;
	struct v4l2_device		v4l2_dev;
	struct v4l2_subdev		*sd;
	struct v4l2_mbus_framefmt	fmt;
	struct list_head		pending_buf_q;
	struct list_head		active_buf_q;
	struct videobuf_queue		vbq;
	int				active_buf_cnt;
	int				buf_index;
	unsigned int			frame_count;
	unsigned int			reqbufs_count;
	int				input_index;
	int				refcnt;
};

/**
 *  struct fimc_pix_limit - image pixel size limits in various IP configurations
 *
 *  @scaler_en_w: max input pixel width when the scaler is enabled
 *  @scaler_dis_w: max input pixel width when the scaler is disabled
 *  @in_rot_en_h: max input width with the input rotator is on
 *  @in_rot_dis_w: max input width with the input rotator is off
 *  @out_rot_en_w: max output width with the output rotator on
 *  @out_rot_dis_w: max output width with the output rotator off
 */
struct fimc_pix_limit {
	u16 scaler_en_w;
	u16 scaler_dis_w;
	u16 in_rot_en_h;
	u16 in_rot_dis_w;
	u16 out_rot_en_w;
	u16 out_rot_dis_w;
};

/**
 * struct samsung_fimc_variant - camera interface variant information
 *
 * @pix_hoff: indicate whether horizontal offset is in pixels or in bytes
 * @has_inp_rot: set if has input rotator
 * @has_out_rot: set if has output rotator
 * @has_cistatus2: 1 if CISTATUS2 register is present in this IP revision
 * @pix_limit: pixel size constraints for the scaler
 * @min_inp_pixsize: minimum input pixel size
 * @min_out_pixsize: minimum output pixel size
 * @hor_offs_align: horizontal pixel offset aligment
 * @out_buf_count: the number of buffers in output DMA sequence
 */
struct samsung_fimc_variant {
	unsigned int	pix_hoff:1;
	unsigned int	has_inp_rot:1;
	unsigned int	has_out_rot:1;
	unsigned int	has_cistatus2:1;
	struct fimc_pix_limit *pix_limit;
	u16		min_inp_pixsize;
	u16		min_out_pixsize;
	u16		hor_offs_align;
	u16		out_buf_count;
};

/**
 * struct samsung_fimc_driverdata - per device type driver data for init time.
 *
 * @variant: the variant information for this driver.
 * @dev_cnt: number of fimc sub-devices available in SoC
 * @lclk_frequency: fimc bus clock frequency
 */
struct samsung_fimc_driverdata {
	struct samsung_fimc_variant *variant[FIMC_MAX_DEVS];
	unsigned long	lclk_frequency;
	int		num_entities;
};

struct fimc_ctx;

/**
 * struct fimc_dev - abstraction for FIMC entity
 *
 * @slock:	the spinlock protecting this data structure
 * @lock:	the mutex protecting this data structure
 * @pdev:	pointer to the FIMC platform device
 * @pdata:	pointer to the device platform data
 * @id:		FIMC device index (0..2)
 * @clock[]:	the clocks required for FIMC operation
 * @regs:	the mapped hardware registers
 * @regs_res:	the resource claimed for IO registers
 * @irq:	interrupt number of the FIMC subdevice
 * @irqlock:	spinlock protecting videobuffer queue
 * @irq_queue:
 * @m2m:	memory-to-memory V4L2 device information
 * @vid_cap:	camera capture device information
 * @state:	flags used to synchronize m2m and capture mode operation
 */
struct fimc_dev {
	spinlock_t			slock;
	struct mutex			lock;
	struct platform_device		*pdev;
	struct s3c_platform_fimc	*pdata;
	struct samsung_fimc_variant	*variant;
	int				id;
	struct clk			*clock[NUM_FIMC_CLOCKS];
	void __iomem			*regs;
	struct resource			*regs_res;
	int				irq;
	spinlock_t			irqlock;
	wait_queue_head_t		irq_queue;
	struct fimc_m2m_device		m2m;
	struct fimc_vid_cap		vid_cap;
	unsigned long			state;
};

/**
 * fimc_ctx - the device context data
 *
 * @lock:		mutex protecting this data structure
 * @s_frame:		source frame properties
 * @d_frame:		destination frame properties
 * @out_order_1p:	output 1-plane YCBCR order
 * @out_order_2p:	output 2-plane YCBCR order
 * @in_order_1p		input 1-plane YCBCR order
 * @in_order_2p:	input 2-plane YCBCR order
 * @in_path:		input mode (DMA or camera)
 * @out_path:		output mode (DMA or FIFO)
 * @scaler:		image scaler properties
 * @effect:		image effect
 * @rotation:		image clockwise rotation in degrees
 * @flip:		image flip mode
 * @flags:		additional flags for image conversion
 * @state:		flags to keep track of user configuration
 * @fimc_dev:		the FIMC device this context applies to
 * @m2m_ctx:		memory-to-memory device context
 */
struct fimc_ctx {
	spinlock_t		slock;
	struct fimc_frame	s_frame;
	struct fimc_frame	d_frame;
	u32			out_order_1p;
	u32			out_order_2p;
	u32			in_order_1p;
	u32			in_order_2p;
	enum fimc_datapath	in_path;
	enum fimc_datapath	out_path;
	struct fimc_scaler	scaler;
	struct fimc_effect	effect;
	int			rotation;
	u32			flip;
	u32			flags;
	u32			state;
	struct fimc_dev		*fimc_dev;
	struct v4l2_m2m_ctx	*m2m_ctx;
};

extern struct videobuf_queue_ops fimc_qops;

static inline int tiled_fmt(struct fimc_fmt *fmt)
{
	return 0;
}

static inline void fimc_hw_clear_irq(struct fimc_dev *dev)
{
	u32 cfg = readl(dev->regs + S5P_CIGCTRL);
	cfg |= S5P_CIGCTRL_IRQ_CLR;
	writel(cfg, dev->regs + S5P_CIGCTRL);
}

static inline void fimc_hw_enable_scaler(struct fimc_dev *dev, bool on)
{
	u32 cfg = readl(dev->regs + S5P_CISCCTRL);
	if (on)
		cfg |= S5P_CISCCTRL_SCALERSTART;
	else
		cfg &= ~S5P_CISCCTRL_SCALERSTART;
	writel(cfg, dev->regs + S5P_CISCCTRL);
}

static inline void fimc_hw_activate_input_dma(struct fimc_dev *dev, bool on)
{
	u32 cfg = readl(dev->regs + S5P_MSCTRL);
	if (on)
		cfg |= S5P_MSCTRL_ENVID;
	else
		cfg &= ~S5P_MSCTRL_ENVID;
	writel(cfg, dev->regs + S5P_MSCTRL);
}

static inline void fimc_hw_dis_capture(struct fimc_dev *dev)
{
	u32 cfg = readl(dev->regs + S5P_CIIMGCPT);
	cfg &= ~(S5P_CIIMGCPT_IMGCPTEN | S5P_CIIMGCPT_IMGCPTEN_SC);
	writel(cfg, dev->regs + S5P_CIIMGCPT);
}

/**
 * fimc_hw_set_dma_seq - configure output DMA buffer sequence
 * @mask: each bit corresponds to one of 32 output buffer registers set
 *	  1 to include buffer in the sequence, 0 to disable
 *
 * This function mask output DMA ring buffers, i.e. it allows to configure
 * which of the output buffer address registers will be used by the DMA
 * engine.
 */
static inline void fimc_hw_set_dma_seq(struct fimc_dev *dev, u32 mask)
{
	writel(mask, dev->regs + S5P_CIFCNTSEQ);
}

static inline struct fimc_frame *ctx_get_frame(struct fimc_ctx *ctx,
					       enum v4l2_buf_type type)
{
	struct fimc_frame *frame;

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT == type) {
		if (ctx->state & FIMC_CTX_M2M)
			frame = &ctx->s_frame;
		else
			return ERR_PTR(-EINVAL);
	} else if (V4L2_BUF_TYPE_VIDEO_CAPTURE == type) {
		frame = &ctx->d_frame;
	} else {
		v4l2_err(&ctx->fimc_dev->m2m.v4l2_dev,
			"Wrong buffer/video queue type (%d)\n", type);
		return ERR_PTR(-EINVAL);
	}

	return frame;
}

/* Return an index to the buffer actually being written. */
static inline u32 fimc_hw_get_frame_index(struct fimc_dev *dev)
{
	u32 reg;

	if (dev->variant->has_cistatus2) {
		reg = readl(dev->regs + S5P_CISTATUS2) & 0x3F;
		return reg > 0 ? --reg : reg;
	} else {
		reg = readl(dev->regs + S5P_CISTATUS);
		return (reg & S5P_CISTATUS_FRAMECNT_MASK) >>
			S5P_CISTATUS_FRAMECNT_SHIFT;
	}
}

/* -----------------------------------------------------*/
/* fimc-reg.c						*/
void fimc_hw_reset(struct fimc_dev *fimc);
void fimc_hw_set_rotation(struct fimc_ctx *ctx);
void fimc_hw_set_target_format(struct fimc_ctx *ctx);
void fimc_hw_set_out_dma(struct fimc_ctx *ctx);
void fimc_hw_en_lastirq(struct fimc_dev *fimc, int enable);
void fimc_hw_en_irq(struct fimc_dev *fimc, int enable);
void fimc_hw_set_scaler(struct fimc_ctx *ctx);
void fimc_hw_en_capture(struct fimc_ctx *ctx);
void fimc_hw_set_effect(struct fimc_ctx *ctx);
void fimc_hw_set_in_dma(struct fimc_ctx *ctx);
void fimc_hw_set_input_path(struct fimc_ctx *ctx);
void fimc_hw_set_output_path(struct fimc_ctx *ctx);
void fimc_hw_set_input_addr(struct fimc_dev *fimc, struct fimc_addr *paddr);
void fimc_hw_set_output_addr(struct fimc_dev *fimc, struct fimc_addr *paddr,
			      int index);
int fimc_hw_set_camera_source(struct fimc_dev *fimc,
			      struct s3c_fimc_isp_info *cam);
int fimc_hw_set_camera_offset(struct fimc_dev *fimc, struct fimc_frame *f);
int fimc_hw_set_camera_polarity(struct fimc_dev *fimc,
				struct s3c_fimc_isp_info *cam);
int fimc_hw_set_camera_type(struct fimc_dev *fimc,
			    struct s3c_fimc_isp_info *cam);

/* -----------------------------------------------------*/
/* fimc-core.c */
int fimc_vidioc_enum_fmt(struct file *file, void *priv,
		      struct v4l2_fmtdesc *f);
int fimc_vidioc_g_fmt(struct file *file, void *priv,
		      struct v4l2_format *f);
int fimc_vidioc_try_fmt(struct file *file, void *priv,
			struct v4l2_format *f);
int fimc_vidioc_queryctrl(struct file *file, void *priv,
			  struct v4l2_queryctrl *qc);
int fimc_vidioc_g_ctrl(struct file *file, void *priv,
		       struct v4l2_control *ctrl);

int fimc_try_crop(struct fimc_ctx *ctx, struct v4l2_crop *cr);
int check_ctrl_val(struct fimc_ctx *ctx,  struct v4l2_control *ctrl);
int fimc_s_ctrl(struct fimc_ctx *ctx, struct v4l2_control *ctrl);

struct fimc_fmt *find_format(struct v4l2_format *f, unsigned int mask);
struct fimc_fmt *find_mbus_format(struct v4l2_mbus_framefmt *f,
				  unsigned int mask);

int fimc_check_scaler_ratio(struct v4l2_rect *r, struct fimc_frame *f);
int fimc_set_scaler_info(struct fimc_ctx *ctx);
int fimc_prepare_config(struct fimc_ctx *ctx, u32 flags);
int fimc_prepare_addr(struct fimc_ctx *ctx, struct fimc_vid_buffer *buf,
		      struct fimc_frame *frame, struct fimc_addr *paddr);

/* -----------------------------------------------------*/
/* fimc-capture.c					*/
int fimc_register_capture_device(struct fimc_dev *fimc);
void fimc_unregister_capture_device(struct fimc_dev *fimc);
int fimc_sensor_sd_init(struct fimc_dev *fimc, int index);
int fimc_vid_cap_buf_queue(struct fimc_dev *fimc,
			     struct fimc_vid_buffer *fimc_vb);

/* Locking: the caller holds fimc->slock */
static inline void fimc_activate_capture(struct fimc_ctx *ctx)
{
	fimc_hw_enable_scaler(ctx->fimc_dev, ctx->scaler.enabled);
	fimc_hw_en_capture(ctx);
}

static inline void fimc_deactivate_capture(struct fimc_dev *fimc)
{
	fimc_hw_en_lastirq(fimc, true);
	fimc_hw_dis_capture(fimc);
	fimc_hw_enable_scaler(fimc, false);
	fimc_hw_en_lastirq(fimc, false);
}

/*
 * Add video buffer to the active buffers queue.
 * The caller holds irqlock spinlock.
 */
static inline void active_queue_add(struct fimc_vid_cap *vid_cap,
					 struct fimc_vid_buffer *buf)
{
	buf->vb.state = VIDEOBUF_ACTIVE;
	list_add_tail(&buf->vb.queue, &vid_cap->active_buf_q);
	vid_cap->active_buf_cnt++;
}

/*
 * Pop a video buffer from the capture active buffers queue
 * Locking: Need to be called with dev->slock held.
 */
static inline struct fimc_vid_buffer *
active_queue_pop(struct fimc_vid_cap *vid_cap)
{
	struct fimc_vid_buffer *buf;
	buf = list_entry(vid_cap->active_buf_q.next,
			 struct fimc_vid_buffer, vb.queue);
	list_del(&buf->vb.queue);
	vid_cap->active_buf_cnt--;
	return buf;
}

/* Add video buffer to the capture pending buffers queue */
static inline void fimc_pending_queue_add(struct fimc_vid_cap *vid_cap,
					  struct fimc_vid_buffer *buf)
{
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vid_cap->pending_buf_q);
}

/* Add video buffer to the capture pending buffers queue */
static inline struct fimc_vid_buffer *
pending_queue_pop(struct fimc_vid_cap *vid_cap)
{
	struct fimc_vid_buffer *buf;
	buf = list_entry(vid_cap->pending_buf_q.next,
			struct fimc_vid_buffer, vb.queue);
	list_del(&buf->vb.queue);
	return buf;
}


#endif /* FIMC_CORE_H_ */
