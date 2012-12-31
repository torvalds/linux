/* linux/drivers/media/video/samsung/jpeg/jpeg_core.h
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

#include "jpeg_mem.h"

#define INT_TIMEOUT		1000

enum jpeg_result {
	OK_ENC_OR_DEC,
	ERR_ENC_OR_DEC,
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
	YUV_422,	/* decode output, encode input */
	YUV_420,	/* decode output, encode output */
	RGB_565,	/* encode input */
};

/* jpeg data format */
enum jpeg_stream_format {
	JPEG_422,	/* decode input, encode output */
	JPEG_420,	/* decode input, encode output */
	JPEG_444,	/* decode input*/
	JPEG_GRAY,	/* decode input*/
	JPEG_RESERVED,
};

struct jpeg_dec_param {
	unsigned int width;
	unsigned int height;
	unsigned int size;
	enum jpeg_stream_format in_fmt;
	enum jpeg_frame_format out_fmt;
};

struct jpeg_enc_param {
	unsigned int width;
	unsigned int height;
	unsigned int size;
	enum jpeg_frame_format in_fmt;
	enum jpeg_stream_format out_fmt;
	enum jpeg_img_quality_level quality;
};

struct jpeg_control {
	struct clk		*clk;
	atomic_t		in_use;
	struct mutex		lock;
	int			irq_no;
	enum jpeg_result	irq_ret;
	wait_queue_head_t	wq;
	void __iomem		*reg_base;	/* register i/o */
	struct jpeg_mem		mem;		/* for reserved memory */
	struct jpeg_dec_param	dec_param;
	struct jpeg_enc_param	enc_param;
};

enum jpeg_log {
	JPEG_LOG_DEBUG		= 0x1000,
	JPEG_LOG_INFO		= 0x0100,
	JPEG_LOG_WARN		= 0x0010,
	JPEG_LOG_ERR		= 0x0001,
};

/* debug macro */
#define JPEG_LOG_DEFAULT	(JPEG_LOG_WARN | JPEG_LOG_ERR)

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
int jpeg_int_pending(struct jpeg_control *ctrl);
int jpeg_set_dec_param(struct jpeg_control *ctrl);
int jpeg_set_enc_param(struct jpeg_control *ctrl);
int jpeg_exe_dec(struct jpeg_control *ctrl);
int jpeg_exe_enc(struct jpeg_control *ctrl);


#endif /*__JPEG_CORE_H__*/

