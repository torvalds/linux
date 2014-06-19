/* linux/drivers/media/platform/s5p-jpeg/jpeg-core.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef JPEG_CORE_H_
#define JPEG_CORE_H_

#include <linux/interrupt.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ctrls.h>

#define S5P_JPEG_M2M_NAME		"s5p-jpeg"

/* JPEG compression quality setting */
#define S5P_JPEG_COMPR_QUAL_BEST	0
#define S5P_JPEG_COMPR_QUAL_WORST	3

/* JPEG RGB to YCbCr conversion matrix coefficients */
#define S5P_JPEG_COEF11			0x4d
#define S5P_JPEG_COEF12			0x97
#define S5P_JPEG_COEF13			0x1e
#define S5P_JPEG_COEF21			0x2c
#define S5P_JPEG_COEF22			0x57
#define S5P_JPEG_COEF23			0x83
#define S5P_JPEG_COEF31			0x83
#define S5P_JPEG_COEF32			0x6e
#define S5P_JPEG_COEF33			0x13

/* a selection of JPEG markers */
#define TEM				0x01
#define SOF0				0xc0
#define RST				0xd0
#define SOI				0xd8
#define EOI				0xd9
#define DHP				0xde

/* Flags that indicate a format can be used for capture/output */
#define SJPEG_FMT_FLAG_ENC_CAPTURE	(1 << 0)
#define SJPEG_FMT_FLAG_ENC_OUTPUT	(1 << 1)
#define SJPEG_FMT_FLAG_DEC_CAPTURE	(1 << 2)
#define SJPEG_FMT_FLAG_DEC_OUTPUT	(1 << 3)
#define SJPEG_FMT_FLAG_S5P		(1 << 4)
#define SJPEG_FMT_FLAG_EXYNOS4		(1 << 5)
#define SJPEG_FMT_RGB			(1 << 6)
#define SJPEG_FMT_NON_RGB		(1 << 7)

#define S5P_JPEG_ENCODE		0
#define S5P_JPEG_DECODE		1

#define FMT_TYPE_OUTPUT		0
#define FMT_TYPE_CAPTURE	1

#define SJPEG_SUBSAMPLING_444	0x11
#define SJPEG_SUBSAMPLING_422	0x21
#define SJPEG_SUBSAMPLING_420	0x22

/* Version numbers */

#define SJPEG_S5P	1
#define SJPEG_EXYNOS4	2

enum exynos4_jpeg_result {
	OK_ENC_OR_DEC,
	ERR_PROT,
	ERR_DEC_INVALID_FORMAT,
	ERR_MULTI_SCAN,
	ERR_FRAME,
	ERR_UNKNOWN,
};

enum  exynos4_jpeg_img_quality_level {
	QUALITY_LEVEL_1 = 0,	/* high */
	QUALITY_LEVEL_2,
	QUALITY_LEVEL_3,
	QUALITY_LEVEL_4,	/* low */
};

/**
 * struct s5p_jpeg - JPEG IP abstraction
 * @lock:		the mutex protecting this structure
 * @slock:		spinlock protecting the device contexts
 * @v4l2_dev:		v4l2 device for mem2mem mode
 * @vfd_encoder:	video device node for encoder mem2mem mode
 * @vfd_decoder:	video device node for decoder mem2mem mode
 * @m2m_dev:		v4l2 mem2mem device data
 * @regs:		JPEG IP registers mapping
 * @irq:		JPEG IP irq
 * @clk:		JPEG IP clock
 * @dev:		JPEG IP struct device
 * @alloc_ctx:		videobuf2 memory allocator's context
 */
struct s5p_jpeg {
	struct mutex		lock;
	spinlock_t		slock;

	struct v4l2_device	v4l2_dev;
	struct video_device	*vfd_encoder;
	struct video_device	*vfd_decoder;
	struct v4l2_m2m_dev	*m2m_dev;

	void __iomem		*regs;
	unsigned int		irq;
	enum exynos4_jpeg_result irq_ret;
	struct clk		*clk;
	struct device		*dev;
	void			*alloc_ctx;
	struct s5p_jpeg_variant *variant;
};

struct s5p_jpeg_variant {
	unsigned int		version;
	unsigned int		fmt_ver_flag;
	struct v4l2_m2m_ops	*m2m_ops;
	irqreturn_t		(*jpeg_irq)(int irq, void *priv);
};

/**
 * struct jpeg_fmt - driver's internal color format data
 * @name:	format descritpion
 * @fourcc:	the fourcc code, 0 if not applicable
 * @depth:	number of bits per pixel
 * @colplanes:	number of color planes (1 for packed formats)
 * @h_align:	horizontal alignment order (align to 2^h_align)
 * @v_align:	vertical alignment order (align to 2^v_align)
 * @flags:	flags describing format applicability
 */
struct s5p_jpeg_fmt {
	char	*name;
	u32	fourcc;
	int	depth;
	int	colplanes;
	int	memplanes;
	int	h_align;
	int	v_align;
	int	subsampling;
	u32	flags;
};

/**
 * s5p_jpeg_q_data - parameters of one queue
 * @fmt:	driver-specific format of this queue
 * @w:		image width
 * @h:		image height
 * @size:	image buffer size in bytes
 */
struct s5p_jpeg_q_data {
	struct s5p_jpeg_fmt	*fmt;
	u32			w;
	u32			h;
	u32			size;
};

/**
 * s5p_jpeg_ctx - the device context data
 * @jpeg:		JPEG IP device for this context
 * @mode:		compression (encode) operation or decompression (decode)
 * @compr_quality:	destination image quality in compression (encode) mode
 * @out_q:		source (output) queue information
 * @cap_fmt:		destination (capture) queue queue information
 * @hdr_parsed:		set if header has been parsed during decompression
 * @ctrl_handler:	controls handler
 */
struct s5p_jpeg_ctx {
	struct s5p_jpeg		*jpeg;
	unsigned int		mode;
	unsigned short		compr_quality;
	unsigned short		restart_interval;
	unsigned short		subsampling;
	struct s5p_jpeg_q_data	out_q;
	struct s5p_jpeg_q_data	cap_q;
	struct v4l2_fh		fh;
	bool			hdr_parsed;
	struct v4l2_ctrl_handler ctrl_handler;
};

/**
 * s5p_jpeg_buffer - description of memory containing input JPEG data
 * @size:	buffer size
 * @curr:	current position in the buffer
 * @data:	pointer to the data
 */
struct s5p_jpeg_buffer {
	unsigned long size;
	unsigned long curr;
	unsigned long data;
};

/**
 * struct s5p_jpeg_addr - JPEG converter physical address set for DMA
 * @y:   luminance plane physical address
 * @cb:  Cb plane physical address
 * @cr:  Cr plane physical address
 */
struct s5p_jpeg_addr {
	u32     y;
	u32     cb;
	u32     cr;
};

#endif /* JPEG_CORE_H */
