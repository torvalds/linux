/* linux/drivers/media/video/exynos/rotator/rotator.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Header file for Exynos Rotator driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef ROTATOR__H_
#define ROTATOR__H_

#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <media/videobuf2-core.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-mediabus.h>

#include "rotator-regs.h"

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
#include <media/videobuf2-cma-phys.h>
#elif defined(CONFIG_VIDEOBUF2_ION)
#include <media/videobuf2-ion.h>
#endif

/* debug macro */
enum rot_log {
	ROT_LOG_DEBUG	= 1000,
	ROT_LOG_INFO	= 100,
	ROT_LOG_WARN	= 10,
	ROT_LOG_ERR	= 1,
};

#define ROT_LOG_DEFAULT	(ROT_LOG_INFO | ROT_LOG_WARN | ROT_LOG_ERR)
static enum rot_log log_level = ROT_LOG_DEFAULT;

#define ROT_DEBUG(fmt, ...)						\
	do {								\
		if (log_level & ROT_LOG_DEBUG)			\
			printk(KERN_DEBUG "[%s:%d] "			\
			fmt, __func__, __LINE__, ##__VA_ARGS__);	\
	} while (0)

#define ROT_INFO(fmt, ...)						\
	do {								\
		if (log_level & ROT_LOG_INFO)			\
			pr_info("[%s:%d] "				\
			fmt, __func__, __LINE__ , ##__VA_ARGS__);	\
	} while (0)

#define ROT_WARN(fmt, ...)						\
	do {								\
		if (log_level & ROT_LOG_WARN)			\
			pr_warn("[%s:%d] "				\
			fmt, __func__, __LINE__ , ##__VA_ARGS__);	\
	} while (0)

#define ROT_ERR(fmt, ...)						\
	do {								\
		if (log_level & ROT_LOG_ERR)				\
			pr_err("[%s:%d] "				\
			fmt, __func__, __LINE__ , ##__VA_ARGS__);	\
	} while (0)

#define rot_dbg(fmt, ...)		ROT_DEBUG(fmt, ##__VA_ARGS__)
#define rot_info(fmt, ...)		ROT_INFO(fmt, ##__VA_ARGS__)
#define rot_warn(fmt, ...)		ROT_WARN(fmt, ##__VA_ARGS__)
#define rot_err(fmt, ...)		ROT_ERR(fmt, ##__VA_ARGS__)

/* Time to wait for frame done interrupt */
#define ROT_TIMEOUT		(20*HZ)
#define ROT_WDT_CNT		5
#define MODULE_NAME		"rotator"
#define ROT_MAX_DEVS		1

/* Address index */
#define ROT_ADDR_RGB		0
#define ROT_ADDR_Y		0
#define ROT_ADDR_CB		1
#define ROT_ADDR_CBCR		1
#define ROT_ADDR_CR		2

/* Driver version */
#define MAJOR_VERSION		0
#define MINOR_VERSION		1
#define RELEASE_VERSION		1

/* Rotator flip direction */
#define ROT_NOFLIP	(1 << 0)
#define ROT_VFLIP	(1 << 1)
#define ROT_HFLIP	(1 << 2)

/* Rotator hardware device state */
#define DEV_RUN		(1 << 0)
#define DEV_SUSPEND	(1 << 1)

/* Rotator m2m context state */
#define CTX_PARAMS	(1 << 0)
#define CTX_STREAMING	(1 << 1)
#define CTX_RUN		(1 << 2)
#define CTX_ABORT	(1 << 3)
#define CTX_SUSPEND	(1 << 4)
#define CTX_SRC		(1 << 5)
#define CTX_DST		(1 << 6)

enum rot_irq_src {
	ISR_PEND_DONE = 8,
	ISR_PEND_ILLEGAL = 9,
};

enum rot_status {
	ROT_IDLE,
	ROT_RESERVED,
	ROT_RUNNING,
	ROT_RUNNING_REMAIN,
};

/*
 * struct exynos_rot_size_limit - Rotator variant size  information
 *
 * @min_x: minimum pixel x size
 * @min_y: minimum pixel y size
 * @max_x: maximum pixel x size
 * @max_y: maximum pixel y size
 */
struct exynos_rot_size_limit {
	u32 min_x;
	u32 min_y;
	u32 max_x;
	u32 max_y;
	u32 align;
};

struct exynos_rot_variant {
	struct exynos_rot_size_limit limit_rgb565;
	struct exynos_rot_size_limit limit_rgb888;
	struct exynos_rot_size_limit limit_yuv422;
	struct exynos_rot_size_limit limit_yuv420_2p;
	struct exynos_rot_size_limit limit_yuv420_3p;
};

/*
 * struct exynos_rot_driverdata - per device type driver data for init time.
 *
 * @variant: the variant information for this driver.
 * @nr_dev: number of devices available in SoC
 */
struct exynos_rot_driverdata {
	struct exynos_rot_variant	*variant[ROT_MAX_DEVS];
	int				nr_dev;
};

/**
 * struct rot_fmt - the driver's internal color format data
 * @name: format description
 * @pixelformat: the fourcc code for this format, 0 if not applicable
 * @num_planes: number of physically non-contiguous data planes
 * @nr_comp: number of color components(ex. RGB, Y, Cb, Cr)
 * @bitperpixel: bits per pixel
 */
struct rot_fmt {
	char	*name;
	u32	pixelformat;
	u16	num_planes;
	u16	nr_comp;
	u32	bitperpixel[VIDEO_MAX_PLANES];
};

struct rot_addr {
	dma_addr_t	y;
	dma_addr_t	cb;
	dma_addr_t	cr;
};

/*
 * struct rot_frame - source/target frame properties
 * @fmt:	buffer format(like virtual screen)
 * @crop:	image size / position
 * @addr:	buffer start address(access using ROT_ADDR_XXX)
 * @bytesused:	image size in bytes (w x h x bpp)
 * @cacheable:	cache property for image address
 */
struct rot_frame {
	struct rot_fmt			*rot_fmt;
	struct v4l2_pix_format_mplane	pix_mp;
	struct v4l2_rect		crop;
	struct rot_addr			addr;
	unsigned long			bytesused[VIDEO_MAX_PLANES];
	bool				cacheable;
};

/*
 * struct rot_m2m_device - v4l2 memory-to-memory device data
 * @v4l2_dev: v4l2 device
 * @vfd: the video device node
 * @m2m_dev: v4l2 memory-to-memory device data
 * @ctx: hardware context data
 * @in_use: the open count
 */
struct rot_m2m_device {
	struct v4l2_device	v4l2_dev;
	struct video_device	*vfd;
	struct v4l2_m2m_dev	*m2m_dev;
	struct rot_ctx		*ctx;
	atomic_t		in_use;
};

/*
 * struct rot_irq - Rotator irq information
 * @num:	Rotator interrupt number
 * @wait:	interrupt handler waitqueue
 */
struct rot_irq {
	int			num;
	wait_queue_head_t	wait;
};

struct rot_wdt {
	struct timer_list	timer;
	atomic_t		cnt;
};

struct rot_ctx;
struct rot_vb2;

/*
 * struct rot_dev - the abstraction for Rotator device
 * @pdev:	pointer to the Rotator platform device
 * @pdata:	pointer to the device platform data
 * @variant:	the IP variant information
 * @m2m:	memory-to-memory V4L2 device information
 * @id:		Rotator device index (0..ROT_MAX_DEVS)
 * @clock:	clock required for Rotator operation
 * @regs:	the mapped hardware registers
 * @regs_res:	the resource claimed for IO registers
 * @irq:	irq information
 * @ws:		work struct
 * @wq:		workqueue
 * @state:	device state flags
 * @alloc_ctx:	videobuf2 memory allocator context
 * @rot_vb2:	videobuf2 memory allocator callbacks
 * @slock:	the spinlock protecting this data structure
 * @lock:	the mutex protecting this data structure
 * @wdt:	watchdog timer information
 */
struct rot_dev {
	struct platform_device		*pdev;
	struct exynos_platform_rot	*pdata;
	struct exynos_rot_variant	*variant;
	struct rot_m2m_device		m2m;
	int				id;
	struct clk			*clock;
	void __iomem			*regs;
	struct resource			*regs_res;
	struct rot_irq			irq;
	struct work_struct		ws;
	struct workqueue_struct		*wq;
	unsigned long			state;
	struct vb2_alloc_ctx		*alloc_ctx;
	const struct rot_vb2		*vb2;
	spinlock_t			slock;
	struct mutex			lock;
	struct rot_wdt			wdt;
};

/*
 * rot_ctx - the abstration for Rotator open context
 * @rot_dev:		the Rotator device this context applies to
 * @m2m_ctx:		memory-to-memory device context
 * @frame:		source frame properties
 * @rotation:		image clockwise rotation in degrees
 * @flip:		image flip mode
 * @state:		context state flags
 * @slock:		spinlock protecting this data structure
 */
struct rot_ctx {
	struct rot_dev		*rot_dev;
	struct v4l2_m2m_ctx	*m2m_ctx;
	struct rot_frame	s_frame;
	struct rot_frame	d_frame;
	int			rotation;
	u32			flip;
	unsigned long		flags;
	spinlock_t		slock;
	bool			cacheable;
};

struct rot_vb2 {
	const struct vb2_mem_ops *ops;
	void *(*init)(struct rot_dev *rot);
	void (*cleanup)(void *alloc_ctx);

	unsigned long (*plane_addr)(struct vb2_buffer *vb, u32 plane_no);

	void (*resume)(void *alloc_ctx);
	void (*suspend)(void *alloc_ctx);

	int (*cache_flush)(struct vb2_buffer *vb, u32 num_planes);
	void (*set_cacheable)(void *alloc_ctx, bool cacheable);
	void (*set_sharable)(void *alloc_ctx, bool sharable);
};

static inline struct rot_frame *ctx_get_frame(struct rot_ctx *ctx,
						enum v4l2_buf_type type)
{
	struct rot_frame *frame;

	if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
		if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			frame = &ctx->s_frame;
		else
			frame = &ctx->d_frame;
	} else {
		rot_err("Wrong V4L2 buffer type %d\n", type);
		return ERR_PTR(-EINVAL);
	}

	return frame;
}

/* rotator-reg.c */
void rot_hwset_irq_frame_done(struct rot_dev *rot, u32 enable);
void rot_hwset_irq_illegal_config(struct rot_dev *rot, u32 enable);
int rot_hwset_image_format(struct rot_dev *rot, u32 pixelformat);
void rot_hwset_flip(struct rot_dev *rot, u32 direction);
void rot_hwset_rotation(struct rot_dev *rot, int degree);
void rot_hwset_start(struct rot_dev *rot);
void rot_hwset_src_addr(struct rot_dev *rot, dma_addr_t addr, u32 comp);
void rot_hwset_dst_addr(struct rot_dev *rot, dma_addr_t addr, u32 comp);
void rot_hwset_src_imgsize(struct rot_dev *rot, struct rot_frame *frame);
void rot_hwset_src_crop(struct rot_dev *rot, struct v4l2_rect *rect);
void rot_hwset_dst_imgsize(struct rot_dev *rot, struct rot_frame *frame);
void rot_hwset_dst_crop(struct rot_dev *rot, struct v4l2_rect *rect);
void rot_hwget_irq_src(struct rot_dev *rot, enum rot_irq_src *irq);
void rot_hwset_irq_clear(struct rot_dev *rot, enum rot_irq_src *irq);
void rot_hwget_status(struct rot_dev *rot, enum rot_status *state);
void rot_dump_register(struct rot_dev *rot);

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
extern const struct rot_vb2 rot_vb2_cma;
#elif defined(CONFIG_VIDEOBUF2_ION)
extern const struct rot_vb2 rot_vb2_ion;
#endif

#endif /* ROTATOR__H_ */
