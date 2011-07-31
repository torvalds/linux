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

#include <linux/types.h>
#include <media/videobuf-core.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <linux/videodev2.h>
#include "regs-fimc.h"

#define err(fmt, args...) \
	printk(KERN_ERR "%s:%d: " fmt "\n", __func__, __LINE__, ##args)

#ifdef DEBUG
#define dbg(fmt, args...) \
	printk(KERN_DEBUG "%s:%d: " fmt "\n", __func__, __LINE__, ##args)
#else
#define dbg(fmt, args...)
#endif

#define NUM_FIMC_CLOCKS		2
#define MODULE_NAME		"s5p-fimc"
#define FIMC_MAX_DEVS		3
#define FIMC_MAX_OUT_BUFS	4
#define SCALER_MAX_HRATIO	64
#define SCALER_MAX_VRATIO	64

enum {
	ST_IDLE,
	ST_OUTDMA_RUN,
	ST_M2M_PEND,
};

#define fimc_m2m_active(dev) test_bit(ST_OUTDMA_RUN, &(dev)->state)
#define fimc_m2m_pending(dev) test_bit(ST_M2M_PEND, &(dev)->state)

enum fimc_datapath {
	FIMC_ITU_CAM_A,
	FIMC_ITU_CAM_B,
	FIMC_MIPI_CAM,
	FIMC_DMA,
	FIMC_LCDFIFO,
	FIMC_WRITEBACK
};

enum fimc_color_fmt {
	S5P_FIMC_RGB565,
	S5P_FIMC_RGB666,
	S5P_FIMC_RGB888,
	S5P_FIMC_YCBCR420,
	S5P_FIMC_YCBCR422,
	S5P_FIMC_YCBYCR422,
	S5P_FIMC_YCRYCB422,
	S5P_FIMC_CBYCRY422,
	S5P_FIMC_CRYCBY422,
	S5P_FIMC_RGB30_LOCAL,
	S5P_FIMC_YCBCR444_LOCAL,
	S5P_FIMC_MAX_COLOR = S5P_FIMC_YCBCR444_LOCAL,
	S5P_FIMC_COLOR_MASK = 0x0F,
};

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
#define	FIMC_PARAMS			(1 << 0)
#define	FIMC_SRC_ADDR			(1 << 1)
#define	FIMC_DST_ADDR			(1 << 2)
#define	FIMC_SRC_FMT			(1 << 3)
#define	FIMC_DST_FMT			(1 << 4)

/* Image conversion flags */
#define	FIMC_IN_DMA_ACCESS_TILED	(1 << 0)
#define	FIMC_IN_DMA_ACCESS_LINEAR	(0 << 0)
#define	FIMC_OUT_DMA_ACCESS_TILED	(1 << 1)
#define	FIMC_OUT_DMA_ACCESS_LINEAR	(0 << 1)
#define	FIMC_SCAN_MODE_PROGRESSIVE	(0 << 2)
#define	FIMC_SCAN_MODE_INTERLACED	(1 << 2)
/* YCbCr data dynamic range for RGB-YUV color conversion. Y/Cb/Cr: (0 ~ 255) */
#define	FIMC_COLOR_RANGE_WIDE		(0 << 3)
/* Y (16 ~ 235), Cb/Cr (16 ~ 240) */
#define	FIMC_COLOR_RANGE_NARROW		(1 << 3)

#define	FLIP_NONE			0
#define	FLIP_X_AXIS			1
#define	FLIP_Y_AXIS			2
#define	FLIP_XY_AXIS			(FLIP_X_AXIS | FLIP_Y_AXIS)

/**
 * struct fimc_fmt - the driver's internal color format data
 * @name: format description
 * @fourcc: the fourcc code for this format
 * @color: the corresponding fimc_color_fmt
 * @depth: number of bits per pixel
 * @buff_cnt: number of physically non-contiguous data planes
 * @planes_cnt: number of physically contiguous data planes
 */
struct fimc_fmt {
	char	*name;
	u32	fourcc;
	u32	color;
	u32	depth;
	u16	buff_cnt;
	u16	planes_cnt;
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
 * @enabled:		the flag set when the scaler is used
 * @hfactor:		horizontal shift factor
 * @vfactor:		vertical shift factor
 * @pre_hratio:		horizontal ratio of the prescaler
 * @pre_vratio:		vertical ratio of the prescaler
 * @pre_dst_width:	the prescaler's destination width
 * @pre_dst_height:	the prescaler's destination height
 * @scaleup_h:		flag indicating scaling up horizontally
 * @scaleup_v:		flag indicating scaling up vertically
 * @main_hratio:	the main scaler's horizontal ratio
 * @main_vratio:	the main scaler's vertical ratio
 * @real_width:		source width - offset
 * @real_height:	source height - offset
 * @copy_mode:		flag set if one-to-one mode is used, i.e. no scaling
 *			and color format conversion
 */
struct fimc_scaler {
	u32	enabled;
	u32	hfactor;
	u32	vfactor;
	u32	pre_hratio;
	u32	pre_vratio;
	u32	pre_dst_width;
	u32	pre_dst_height;
	u32	scaleup_h;
	u32	scaleup_v;
	u32	main_hratio;
	u32	main_vratio;
	u32	real_width;
	u32	real_height;
	u32	copy_mode;
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
 * @vb:	v4l videobuf buffer
 */
struct fimc_vid_buffer {
	struct videobuf_buffer	vb;
};

/**
 * struct fimc_frame - input/output frame format properties
 *
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
 * struct samsung_fimc_variant - camera interface variant information
 *
 * @pix_hoff: indicate whether horizontal offset is in pixels or in bytes
 * @has_inp_rot: set if has input rotator
 * @has_out_rot: set if has output rotator
 * @min_inp_pixsize: minimum input pixel size
 * @min_out_pixsize: minimum output pixel size
 * @scaler_en_w: maximum input pixel width when the scaler is enabled
 * @scaler_dis_w: maximum input pixel width when the scaler is disabled
 * @in_rot_en_h: maximum input width when the input rotator is used
 * @in_rot_dis_w: maximum input width when the input rotator is used
 * @out_rot_en_w: maximum output width for the output rotator enabled
 * @out_rot_dis_w: maximum output width for the output rotator enabled
 */
struct samsung_fimc_variant {
	unsigned int	pix_hoff:1;
	unsigned int	has_inp_rot:1;
	unsigned int	has_out_rot:1;

	u16		min_inp_pixsize;
	u16		min_out_pixsize;
	u16		scaler_en_w;
	u16		scaler_dis_w;
	u16		in_rot_en_h;
	u16		in_rot_dis_w;
	u16		out_rot_en_w;
	u16		out_rot_dis_w;
};

/**
 * struct samsung_fimc_driverdata - per-device type driver data for init time.
 *
 * @variant: the variant information for this driver.
 * @dev_cnt: number of fimc sub-devices available in SoC
 */
struct samsung_fimc_driverdata {
	struct samsung_fimc_variant *variant[FIMC_MAX_DEVS];
	int	devs_cnt;
};

struct fimc_ctx;

/**
 * struct fimc_subdev - abstraction for a FIMC entity
 *
 * @slock:	the spinlock protecting this data structure
 * @lock:	the mutex protecting this data structure
 * @pdev:	pointer to the FIMC platform device
 * @id:		FIMC device index (0..2)
 * @clock[]:	the clocks required for FIMC operation
 * @regs:	the mapped hardware registers
 * @regs_res:	the resource claimed for IO registers
 * @irq:	interrupt number of the FIMC subdevice
 * @irqlock:	spinlock protecting videbuffer queue
 * @m2m:	memory-to-memory V4L2 device information
 * @state:	the FIMC device state flags
 */
struct fimc_dev {
	spinlock_t			slock;
	struct mutex			lock;
	struct platform_device		*pdev;
	struct samsung_fimc_variant	*variant;
	int				id;
	struct clk			*clock[NUM_FIMC_CLOCKS];
	void __iomem			*regs;
	struct resource			*regs_res;
	int				irq;
	spinlock_t			irqlock;
	struct workqueue_struct		*work_queue;
	struct fimc_m2m_device		m2m;
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
 * @flags:		an additional flags for image conversion
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

static inline void fimc_hw_start_scaler(struct fimc_dev *dev)
{
	u32 cfg = readl(dev->regs + S5P_CISCCTRL);
	cfg |= S5P_CISCCTRL_SCALERSTART;
	writel(cfg, dev->regs + S5P_CISCCTRL);
}

static inline void fimc_hw_stop_scaler(struct fimc_dev *dev)
{
	u32 cfg = readl(dev->regs + S5P_CISCCTRL);
	cfg &= ~S5P_CISCCTRL_SCALERSTART;
	writel(cfg, dev->regs + S5P_CISCCTRL);
}

static inline void fimc_hw_dis_capture(struct fimc_dev *dev)
{
	u32 cfg = readl(dev->regs + S5P_CIIMGCPT);
	cfg &= ~(S5P_CIIMGCPT_IMGCPTEN | S5P_CIIMGCPT_IMGCPTEN_SC);
	writel(cfg, dev->regs + S5P_CIIMGCPT);
}

static inline void fimc_hw_start_in_dma(struct fimc_dev *dev)
{
	u32 cfg = readl(dev->regs + S5P_MSCTRL);
	cfg |= S5P_MSCTRL_ENVID;
	writel(cfg, dev->regs + S5P_MSCTRL);
}

static inline void fimc_hw_stop_in_dma(struct fimc_dev *dev)
{
	u32 cfg = readl(dev->regs + S5P_MSCTRL);
	cfg &= ~S5P_MSCTRL_ENVID;
	writel(cfg, dev->regs + S5P_MSCTRL);
}

static inline struct fimc_frame *ctx_m2m_get_frame(struct fimc_ctx *ctx,
						   enum v4l2_buf_type type)
{
	struct fimc_frame *frame;

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT == type) {
		frame = &ctx->s_frame;
	} else if (V4L2_BUF_TYPE_VIDEO_CAPTURE == type) {
		frame = &ctx->d_frame;
	} else {
		v4l2_err(&ctx->fimc_dev->m2m.v4l2_dev,
			"Wrong buffer/video queue type (%d)\n", type);
		return ERR_PTR(-EINVAL);
	}

	return frame;
}

/* -----------------------------------------------------*/
/* fimc-reg.c						*/
void fimc_hw_reset(struct fimc_dev *dev);
void fimc_hw_set_rotation(struct fimc_ctx *ctx);
void fimc_hw_set_target_format(struct fimc_ctx *ctx);
void fimc_hw_set_out_dma(struct fimc_ctx *ctx);
void fimc_hw_en_lastirq(struct fimc_dev *dev, int enable);
void fimc_hw_en_irq(struct fimc_dev *dev, int enable);
void fimc_hw_set_prescaler(struct fimc_ctx *ctx);
void fimc_hw_set_scaler(struct fimc_ctx *ctx);
void fimc_hw_en_capture(struct fimc_ctx *ctx);
void fimc_hw_set_effect(struct fimc_ctx *ctx);
void fimc_hw_set_in_dma(struct fimc_ctx *ctx);
void fimc_hw_set_input_path(struct fimc_ctx *ctx);
void fimc_hw_set_output_path(struct fimc_ctx *ctx);
void fimc_hw_set_input_addr(struct fimc_dev *dev, struct fimc_addr *paddr);
void fimc_hw_set_output_addr(struct fimc_dev *dev, struct fimc_addr *paddr);

#endif /* FIMC_CORE_H_ */
