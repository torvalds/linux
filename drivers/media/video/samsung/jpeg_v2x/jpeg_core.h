/* linux/drivers/media/video/samsung/jpeg_v2x/jpeg_core.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * Definition for core file of the jpeg operation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __JPEG_CORE_H__
#define __JPEG_CORE_H__

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/sched.h>

#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-ioctl.h>

#include <media/videobuf2-core.h>
#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
#include <media/videobuf2-cma-phys.h>
#elif defined(CONFIG_VIDEOBUF2_ION)
#include <media/videobuf2-ion.h>
#endif
#include "jpeg_mem.h"

#define INT_TIMEOUT		1000

#define JPEG_NUM_INST		4
#define JPEG_MAX_PLANE		3

enum jpeg_state {
	JPEG_IDLE,
	JPEG_SRC_ADDR,
	JPEG_DST_ADDR,
	JPEG_ISR,
	JPEG_STREAM,
};

enum jpeg_mode {
	ENCODING,
	DECODING,
};

enum jpeg_result {
	OK_ENC_OR_DEC,
	ERR_PROT,
	ERR_DEC_INVALID_FORMAT,
	ERR_MULTI_SCAN,
	ERR_FRAME,
	ERR_UNKNOWN,
};

enum  jpeg_img_quality_level {
	QUALITY_LEVEL_1 = 0,	/* high */
	QUALITY_LEVEL_2,
	QUALITY_LEVEL_3,
	QUALITY_LEVEL_4,	/* low */
};

/* raw data image format */
enum jpeg_frame_format {
	YCRCB_444_2P,
	YCBCR_444_2P,
	YCBCR_444_3P,
	YCBYCR_422_1P,
	YCRYCB_422_1P,
	CBYCRY_422_1P,
	CRYCBY_422_1P,
	YCBCR_422_2P,
	YCRCB_422_2P,
	YCBYCR_422_3P,
	YCBCR_420_3P,
	YCBCR_420_2P,
	YCRCB_420_2P,
	YCBCR_420_2P_M,
	YCRCB_420_2P_M,
	RGB_565,
	RGB_888,
	GRAY,
};

/* jpeg data format */
enum jpeg_stream_format {
	JPEG_422,	/* decode input, encode output */
	JPEG_420,	/* decode input, encode output */
	JPEG_444,	/* decode input*/
	JPEG_GRAY,	/* decode input*/
	JPEG_RESERVED,
};

enum jpeg_scale_value {
	JPEG_SCALE_NORMAL,
	JPEG_SCALE_2,
	JPEG_SCALE_4,
};

enum jpeg_interface {
	M2M_OUTPUT,
	M2M_CAPTURE,
};

enum jpeg_node_type {
	JPEG_NODE_INVALID = -1,
	JPEG_NODE_DECODER = 11,
	JPEG_NODE_ENCODER = 12,
};

struct jpeg_fmt {
	char			*name;
	unsigned int			fourcc;
	int			depth[JPEG_MAX_PLANE];
	int			color;
	int			memplanes;
	int			colplanes;
	enum jpeg_interface	types;
};

struct jpeg_dec_param {
	unsigned int in_width;
	unsigned int in_height;
	unsigned int out_width;
	unsigned int out_height;
	unsigned int size;
	unsigned int mem_size;
	unsigned int in_plane;
	unsigned int out_plane;
	unsigned int in_depth;
	unsigned int out_depth[JPEG_MAX_PLANE];

	enum jpeg_stream_format in_fmt;
	enum jpeg_frame_format out_fmt;
};

struct jpeg_enc_param {
	unsigned int in_width;
	unsigned int in_height;
	unsigned int out_width;
	unsigned int out_height;
	unsigned int size;
	unsigned int in_plane;
	unsigned int out_plane;
	unsigned int in_depth[JPEG_MAX_PLANE];
	unsigned int out_depth;

	enum jpeg_frame_format in_fmt;
	enum jpeg_stream_format out_fmt;
	enum jpeg_img_quality_level quality;
};

struct jpeg_ctx {
	spinlock_t			slock;
	struct jpeg_dev		*dev;
	struct v4l2_m2m_ctx	*m2m_ctx;

	union {
		struct jpeg_dec_param	dec_param;
		struct jpeg_enc_param	enc_param;
	} param;

	int			index;
	unsigned long		payload[VIDEO_MAX_PLANES];
	bool			input_cacheable;
	bool			output_cacheable;
};

struct jpeg_vb2 {
	const struct vb2_mem_ops *ops;
	void *(*init)(struct jpeg_dev *dev);
	void (*cleanup)(void *alloc_ctx);

	unsigned long (*plane_addr)(struct vb2_buffer *vb, u32 plane_no);

	void (*resume)(void *alloc_ctx);
	void (*suspend)(void *alloc_ctx);

	int (*cache_flush)(struct vb2_buffer *vb, u32 num_planes);
	void (*set_cacheable)(void *alloc_ctx, bool cacheable);
};

struct jpeg_dev {
	spinlock_t			slock;
	struct v4l2_device	v4l2_dev;
	struct video_device	*vfd_enc;
	struct video_device	*vfd_dec;
	struct v4l2_m2m_dev	*m2m_dev_enc;
	struct v4l2_m2m_dev	*m2m_dev_dec;
	struct jpeg_ctx		*ctx;
	struct vb2_alloc_ctx	*alloc_ctx;

	struct platform_device	*plat_dev;

	struct clk		*clk;

	struct mutex		lock;

	int			irq_no;
	enum jpeg_result	irq_ret;
	wait_queue_head_t	 wq;
	void __iomem		*reg_base;	/* register i/o */
	enum jpeg_mode		mode;
	const struct jpeg_vb2	*vb2;

	unsigned long		hw_run;
	atomic_t		watchdog_cnt;
	struct timer_list	watchdog_timer;
	struct workqueue_struct	*watchdog_workqueue;
	struct work_struct	watchdog_work;
	struct device			*bus_dev;
};

enum jpeg_log {
	JPEG_LOG_DEBUG		= 0x1000,
	JPEG_LOG_INFO		= 0x0100,
	JPEG_LOG_WARN		= 0x0010,
	JPEG_LOG_ERR		= 0x0001,
};

/* debug macro */
#define JPEG_LOG_DEFAULT       (JPEG_LOG_WARN | JPEG_LOG_ERR)

#define JPEG_DEBUG(fmt, ...)						\
	do {								\
		if (JPEG_LOG_DEFAULT & JPEG_LOG_DEBUG)			\
			printk(KERN_DEBUG "%s: "			\
				fmt, __func__, ##__VA_ARGS__);		\
	} while (0)

#define JPEG_INFO(fmt, ...)						\
	do {								\
		if (JPEG_LOG_DEFAULT & JPEG_LOG_INFO)			\
			printk(KERN_INFO "%s: "				\
				fmt, __func__, ##__VA_ARGS__);		\
	} while (0)

#define JPEG_WARN(fmt, ...)						\
	do {								\
		if (JPEG_LOG_DEFAULT & JPEG_LOG_WARN)			\
			printk(KERN_WARNING "%s: "			\
				fmt, __func__, ##__VA_ARGS__);		\
	} while (0)


#define JPEG_ERROR(fmt, ...)						\
	do {								\
		if (JPEG_LOG_DEFAULT & JPEG_LOG_ERR)			\
			printk(KERN_ERR "%s: "				\
				fmt, __func__, ##__VA_ARGS__);		\
	} while (0)


#define jpeg_dbg(fmt, ...)		JPEG_DEBUG(fmt, ##__VA_ARGS__)
#define jpeg_info(fmt, ...)		JPEG_INFO(fmt, ##__VA_ARGS__)
#define jpeg_warn(fmt, ...)		JPEG_WARN(fmt, ##__VA_ARGS__)
#define jpeg_err(fmt, ...)		JPEG_ERROR(fmt, ##__VA_ARGS__)

/*=====================================================================*/
const struct v4l2_ioctl_ops *get_jpeg_dec_v4l2_ioctl_ops(void);
const struct v4l2_ioctl_ops *get_jpeg_enc_v4l2_ioctl_ops(void);

int jpeg_int_pending(struct jpeg_dev *ctrl);

#endif /*__JPEG_CORE_H__*/

