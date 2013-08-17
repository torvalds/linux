/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Header file for Exynos Scaler driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef SCALER__H_
#define SCALER__H_

#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_media.h>
#include <linux/io.h>
#include <media/videobuf2-core.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ctrls.h>

#include "scaler-regs.h"

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
#include <media/videobuf2-cma-phys.h>
#elif defined(CONFIG_VIDEOBUF2_ION)
#include <media/videobuf2-ion.h>
#endif

extern int sc_log_level;
#define sc_dbg(fmt, args...)						\
	do {								\
		if (sc_log_level)					\
			printk(KERN_DEBUG "[%s:%d] "			\
			fmt, __func__, __LINE__, ##args);		\
	} while (0)

#define MODULE_NAME		"exynos5-scaler"
#define SC_MAX_DEVS		1
#define SC_TIMEOUT		(2 * HZ)	/* 2 seconds */
#define SC_WDT_CNT		3
#define SC_MAX_CTRL_NUM		11

/* Address index */
#define SC_ADDR_RGB		0
#define SC_ADDR_Y		0
#define SC_ADDR_CB		1
#define SC_ADDR_CBCR		1
#define SC_ADDR_CR		2

/* Scaler flip direction */
#define SC_VFLIP	(1 << 1)
#define SC_HFLIP	(1 << 2)

/* Scaler hardware device state */
#define DEV_RUN		1
#define DEV_SUSPEND	2

/* Scaler m2m context state */
#define CTX_PARAMS	1
#define CTX_STREAMING	2
#define CTX_RUN		3
#define CTX_ABORT	4
#define CTX_SRC_FMT	5
#define CTX_DST_FMT	6

/* CSC equation */
#define SC_CSC_NARROW	0
#define SC_CSC_WIDE	1
#define SC_CSC_601	0
#define SC_CSC_709	1

#define fh_to_sc_ctx(__fh)	container_of(__fh, struct sc_ctx, fh)
#define sc_fmt_is_rgb(x)	(!!((x) & 0x10))
#define sc_fmt_is_yuv422(x)	((x == V4L2_PIX_FMT_YUYV) || \
		(x == V4L2_PIX_FMT_UYVY) || (x == V4L2_PIX_FMT_YVYU) || \
		(x == V4L2_PIX_FMT_YUV422P) || (x == V4L2_PIX_FMT_NV16) || \
		(x == V4L2_PIX_FMT_NV61))
#define sc_fmt_is_yuv420(x)	((x == V4L2_PIX_FMT_YUV420) || \
		(x == V4L2_PIX_FMT_YVU420) || (x == V4L2_PIX_FMT_NV12) || \
		(x == V4L2_PIX_FMT_NV21) || (x == V4L2_PIX_FMT_NV12M) || \
		(x == V4L2_PIX_FMT_NV21M) || (x == V4L2_PIX_FMT_YUV420M) || \
		(x == V4L2_PIX_FMT_YVU420M) || (x == V4L2_PIX_FMT_NV12MT_16X16))
#define sc_dith_val(a, b, c)	((a << SCALER_DITH_R_SHIFT) |	\
		(b << SCALER_DITH_G_SHIFT) | (c << SCALER_DITH_B_SHIFT))
#define sc_ver_is_5a(sc)	(sc->ver == 0x3)
#define sc_num_pbuf(sc)		(sc_ver_is_5a(sc) ? 2 : 3)

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
extern const struct sc_vb2 sc_vb2_cma;
#elif defined(CONFIG_VIDEOBUF2_ION)
extern const struct sc_vb2 sc_vb2_ion;
#endif

#ifdef CONFIG_VIDEOBUF2_ION
#define sc_buf_sync_prepare vb2_ion_buf_prepare
#define sc_buf_sync_finish vb2_ion_buf_finish
#else
int sc_buf_sync_finish(struct vb2_buffer *vb);
int sc_buf_sync_prepare(struct vb2_buffer *vb);
#endif

enum sc_csc_idx {
	NO_CSC,
	CSC_Y2R,
	CSC_R2Y,
};

struct sc_csc_tab {
	int narrow_601[9];
	int wide_601[9];
	int narrow_709[9];
	int wide_709[9];
};

enum sc_clk_status {
	SC_CLK_ON,
	SC_CLK_OFF,
};

enum sc_color_fmt {
	SC_COLOR_RGB = 0x10,
	SC_COLOR_YUV = 0x20,
};

enum sc_dith {
	SC_DITH_NO,
	SC_DITH_8BIT,
	SC_DITH_6BIT,
	SC_DITH_5BIT,
	SC_DITH_4BIT,
};

/*
 * blending operation
 * The order is from Android PorterDuff.java
 */
enum sc_blend_op {
	/* [0, 0] */
	BL_OP_CLR = 1,
	/* [Sa, Sc] */
	BL_OP_SRC,
	/* [Da, Dc] */
	BL_OP_DST,
	/* [Sa + (1 - Sa)*Da, Rc = Sc + (1 - Sa)*Dc] */
	BL_OP_SRC_OVER,
	/* [Sa + (1 - Sa)*Da, Rc = Dc + (1 - Da)*Sc] */
	BL_OP_DST_OVER,
	/* [Sa * Da, Sc * Da] */
	BL_OP_SRC_IN,
	/* [Sa * Da, Sa * Dc] */
	BL_OP_DST_IN,
	/* [Sa * (1 - Da), Sc * (1 - Da)] */
	BL_OP_SRC_OUT,
	/* [Da * (1 - Sa), Dc * (1 - Sa)] */
	BL_OP_DST_OUT,
	/* [Da, Sc * Da + (1 - Sa) * Dc] */
	BL_OP_SRC_ATOP,
	/* [Sa, Sc * (1 - Da) + Sa * Dc ] */
	BL_OP_DST_ATOP,
	/* [-(Sa * Da), Sc * (1 - Da) + (1 - Sa) * Dc] */
	BL_OP_XOR,
	/* [Sa + Da - Sa*Da, Sc*(1 - Da) + Dc*(1 - Sa) + min(Sc, Dc)] */
	BL_OP_DARKEN,
	/* [Sa + Da - Sa*Da, Sc*(1 - Da) + Dc*(1 - Sa) + max(Sc, Dc)] */
	BL_OP_LIGHTEN,
	/** [Sa * Da, Sc * Dc] */
	BL_OP_MULTIPLY,
	/* [Sa + Da - Sa * Da, Sc + Dc - Sc * Dc] */
	BL_OP_SCREEN,
	/* Saturate(S + D) */
	BL_OP_ADD,
};

/*
 * Co = <src color op> * Cs + <dst color op> * Cd
 * Ao = <src_alpha_op> * As + <dst_color_op> * Ad
 */
#define BL_INV_BIT_OFFSET	0x10

enum sc_bl_comp {
	ONE = 0,
	SRC_A,
	SRC_C,
	DST_A,
	INV_SA = 0x11,
	INV_SC,
	INV_DA,
	ZERO = 0xff,
};

struct sc_bl_op_val {
	u32 src_color;
	u32 src_alpha;
	u32 dst_color;
	u32 dst_alpha;
};

/*
 * struct sc_size_limit - Scaler variant size information
 *
 * @min_w: minimum pixel width size
 * @min_h: minimum pixel height size
 * @max_w: maximum pixel width size
 * @max_h: maximum pixel height size
 * @align_w: pixel width align
 * @align_h: pixel height align
 */
struct sc_size_limit {
	u32 min_w;
	u32 min_h;
	u32 max_w;
	u32 max_h;
	u32 align_w;
	u32 align_h;
};

struct sc_variant {
	struct sc_size_limit limit_input;
	struct sc_size_limit limit_output;
	int sc_up_max;
	int sc_down_max;
};

/*
 * struct sc_fmt - the driver's internal color format data
 * @name: format description
 * @pixelformat: the fourcc code for this format, 0 if not applicable
 * @num_planes: number of physically non-contiguous data planes
 * @num_comp: number of color components(ex. RGB, Y, Cb, Cr)
 * @bitperpixel: bits per pixel
 * @color: the corresponding sc_color_fmt
 */
struct sc_fmt {
	char	*name;
	u32	pixelformat;
	u16	num_planes;
	u16	num_comp;
	u32	bitperpixel[VIDEO_MAX_PLANES];
	u32	color;
};

struct sc_addr {
	dma_addr_t	y;
	dma_addr_t	cb;
	dma_addr_t	cr;
};

/*
 * struct sc_frame - source/target frame properties
 * @fmt:	buffer format(like virtual screen)
 * @crop:	image size / position
 * @addr:	buffer start address(access using SC_ADDR_XXX)
 * @bytesused:	image size in bytes (w x h x bpp)
 */
struct sc_frame {
	struct sc_fmt			*sc_fmt;
	struct v4l2_pix_format_mplane	pix_mp;
	struct v4l2_rect		crop;
	struct sc_addr			addr;
	unsigned long			bytesused[VIDEO_MAX_PLANES];
};

/*
 * struct sc_m2m_device - v4l2 memory-to-memory device data
 * @v4l2_dev: v4l2 device
 * @vfd: the video device node
 * @m2m_dev: v4l2 memory-to-memory device data
 * @in_use: the open count
 */
struct sc_m2m_device {
	struct v4l2_device	v4l2_dev;
	struct video_device	*vfd;
	struct v4l2_m2m_dev	*m2m_dev;
	atomic_t		in_use;
};

struct sc_wdt {
	struct timer_list	timer;
	atomic_t		cnt;
};

struct sc_csc {
	bool			csc_eq;
	bool			csc_range;
};

struct sc_ctx;
struct sc_vb2;

/*
 * struct sc_dev - the abstraction for Rotator device
 * @dev:	pointer to the Rotator device
 * @pdata:	pointer to the device platform data
 * @variant:	the IP variant information
 * @m2m:	memory-to-memory V4L2 device information
 * @id:		Rotator device index (0..SC_MAX_DEVS)
 * @aclk:	aclk required for scaler operation
 * @pclk:	pclk required for scaler operation
 * @regs:	the mapped hardware registers
 * @regs_res:	the resource claimed for IO registers
 * @wait:	interrupt handler waitqueue
 * @ws:		work struct
 * @state:	device state flags
 * @alloc_ctx:	videobuf2 memory allocator context
 * @sc_vb2:	videobuf2 memory allocator callbacks
 * @slock:	the spinlock pscecting this data structure
 * @lock:	the mutex pscecting this data structure
 * @wdt:	watchdog timer information
 * @clk_cnt:	scator clock on/off count
 */
struct sc_dev {
	struct device			*dev;
	struct exynos_platform_sc	*pdata;
	struct sc_variant		*variant;
	struct sc_m2m_device		m2m;
	int				id;
	int				ver;
	struct clk			*aclk;
	struct clk			*pclk;
	void __iomem			*regs;
	struct resource			*regs_res;
	wait_queue_head_t		wait;
	unsigned long			state;
	struct vb2_alloc_ctx		*alloc_ctx;
	const struct sc_vb2		*vb2;
	spinlock_t			slock;
	struct mutex			lock;
	struct sc_wdt			wdt;
	atomic_t			clk_cnt;
};

/*
 * sc_ctx - the abstration for Rotator open context
 * @sc_dev:		the Rotator device this context applies to
 * @m2m_ctx:		memory-to-memory device context
 * @frame:		source frame properties
 * @ctrl_handler:	v4l2 controls handler
 * @fh:			v4l2 file handle
 * @fence_work:		work struct for sync fence work
 * @fence_wq:		workqueue for sync fence work
 * @fence_wait_list:	wait list for sync fence
 * @rotation:		image clockwise scation in degrees
 * @flip:		image flip mode
 * @bl_op:		image blend mode
 * @dith:		image dithering mode
 * @g_alpha:		global alpha value
 * @color_fill:		color fill value
 * @flags:		context state flags
 * @slock:		spinlock pscecting this data structure
 * @cacheable:		cacheability of current frame
 * @pre_multi:		pre-multiplied format
 * @csc:		csc equation value
 */
struct sc_ctx {
	struct sc_dev			*sc_dev;
	struct v4l2_m2m_ctx		*m2m_ctx;
	struct sc_frame			s_frame;
	struct sc_frame			d_frame;
	struct v4l2_ctrl_handler	ctrl_handler;
	struct v4l2_fh			fh;
	struct work_struct		fence_work;
	struct workqueue_struct		*fence_wq;
	struct list_head		fence_wait_list;
	int				rotation;
	u32				flip;
	enum sc_blend_op		bl_op;
	u32				dith;
	u32				g_alpha;
	unsigned int			color_fill;
	unsigned long			flags;
	spinlock_t			slock;
	bool				cacheable;
	bool				pre_multi;
	struct sc_csc			csc;
};

struct sc_vb2 {
	const struct vb2_mem_ops *ops;
	void *(*init)(struct sc_dev *sc);
	void (*cleanup)(void *alloc_ctx);

	unsigned long (*plane_addr)(struct vb2_buffer *vb, u32 plane_no);

	int (*resume)(void *alloc_ctx);
	void (*suspend)(void *alloc_ctx);

	int (*cache_flush)(struct vb2_buffer *vb, u32 num_planes);
	void (*set_cacheable)(void *alloc_ctx, bool cacheable);
	void (*set_sharable)(void *alloc_ctx, bool sharable);
};

static inline struct sc_frame *ctx_get_frame(struct sc_ctx *ctx,
						enum v4l2_buf_type type)
{
	struct sc_frame *frame;

	if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
		if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			frame = &ctx->s_frame;
		else
			frame = &ctx->d_frame;
	} else {
		dev_err(ctx->sc_dev->dev,
				"Wrong V4L2 buffer type %d\n", type);
		return ERR_PTR(-EINVAL);
	}

	return frame;
}

int sc_hwset_src_image_format(struct sc_dev *sc, u32 pixelformat);
int sc_hwset_dst_image_format(struct sc_dev *sc, u32 pixelformat);
void sc_hwset_pre_multi_format(struct sc_dev *sc);
void sc_hwset_blend(struct sc_dev *sc, enum sc_blend_op bl_op, bool pre_multi);
void sc_hwset_color_fill(struct sc_dev *sc, unsigned int val);
void sc_hwset_dith(struct sc_dev *sc, unsigned int val);
void sc_hwset_csc_coef(struct sc_dev *sc, enum sc_csc_idx idx,
		struct sc_csc *csc);
void sc_hwset_flip_rotation(struct sc_dev *sc, u32 direction, int degree);
void sc_hwset_src_imgsize(struct sc_dev *sc, struct sc_frame *frame);
void sc_hwset_dst_imgsize(struct sc_dev *sc, struct sc_frame *frame);
void sc_hwset_src_crop(struct sc_dev *sc, struct v4l2_rect *rect);
void sc_hwset_dst_crop(struct sc_dev *sc, struct v4l2_rect *rect);
void sc_hwset_src_addr(struct sc_dev *sc, struct sc_addr *addr);
void sc_hwset_dst_addr(struct sc_dev *sc, struct sc_addr *addr);
void sc_hwset_hratio(struct sc_dev *sc, u32 ratio);
void sc_hwset_vratio(struct sc_dev *sc, u32 ratio);
void sc_hwset_hratio(struct sc_dev *sc, u32 ratio);
void sc_hwset_hcoef(struct sc_dev *sc, int coef);
void sc_hwset_vcoef(struct sc_dev *sc, int coef);
void sc_hwset_int_en(struct sc_dev *sc, u32 enable);
int sc_hwget_int_status(struct sc_dev *sc);
void sc_hwset_int_clear(struct sc_dev *sc);
int sc_hwget_version(struct sc_dev *sc);
void sc_hwset_soft_reset(struct sc_dev *sc);
void sc_hwset_start(struct sc_dev *sc);
#endif /* SCALER__H_ */
