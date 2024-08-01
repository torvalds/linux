/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Imagination E5010 JPEG Encoder driver.
 *
 * Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Author: David Huang <d-huang@ti.com>
 * Author: Devarsh Thakkar <devarsht@ti.com>
 */

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>

#ifndef _E5010_JPEG_ENC_H
#define _E5010_JPEG_ENC_H

#define MAX_PLANES			2
#define HEADER_SIZE			0x025D
#define MIN_DIMENSION			64
#define MAX_DIMENSION			8192
#define DEFAULT_WIDTH			640
#define DEFAULT_HEIGHT			480
#define E5010_MODULE_NAME		"e5010"
#define JPEG_MAX_BYTES_PER_PIXEL	2

/* JPEG marker definitions */
#define START_OF_IMAGE			0xFFD8
#define SOF_BASELINE_DCT		0xFFC0
#define END_OF_IMAGE			0xFFD9
#define START_OF_SCAN			0xFFDA

/* Definitions for the huffman table specification in the Marker segment */
#define DHT_MARKER			0xFFC4
#define LH_DC				0x001F
#define LH_AC				0x00B5

/* Definitions for the quantization table specification in the Marker segment */
#define DQT_MARKER			0xFFDB
#define ACMAX				0x03FF
#define DCMAX				0x07FF

/* Length and precision of the quantization table parameters */
#define LQPQ				0x00430
#define QMAX				255

/* Misc JPEG header definitions */
#define UC_NUM_COMP			3
#define PRECISION			8
#define HORZ_SAMPLING_FACTOR		(2 << 4)
#define VERT_SAMPLING_FACTOR_422	1
#define VERT_SAMPLING_FACTOR_420	2
#define COMPONENTS_IN_SCAN		3
#define PELS_IN_BLOCK			64

/* Used for Qp table generation */
#define LUMINOSITY			10
#define CONTRAST			1
#define INCREASE			2
#define QP_TABLE_SIZE			(8 * 8)
#define QP_TABLE_FIELD_OFFSET		0x04

/*
 * vb2 queue structure
 * contains queue data information
 *
 * @fmt: format info
 * @width: frame width
 * @height: frame height
 * @bytesperline: bytes per line in memory
 * @size_image: image size in memory
 */
struct e5010_q_data {
	struct e5010_fmt	*fmt;
	u32			width;
	u32			height;
	u32			width_adjusted;
	u32			height_adjusted;
	u32			sizeimage[MAX_PLANES];
	u32			bytesperline[MAX_PLANES];
	u32			sequence;
	struct v4l2_rect	crop;
	bool			crop_set;
};

/*
 * Driver device structure
 * Holds all memory handles and global parameters
 * Shared by all instances
 */
struct e5010_dev {
	struct device *dev;
	struct v4l2_device	v4l2_dev;
	struct v4l2_m2m_dev	*m2m_dev;
	struct video_device	*vdev;
	void __iomem		*core_base;
	void __iomem		*mmu_base;
	struct clk		*clk;
	struct e5010_context	*last_context_run;
	/* Protect access to device data */
	struct mutex		mutex;
	/* Protect access to hardware*/
	spinlock_t		hw_lock;
};

/*
 * Driver context structure
 * One of these exists for every m2m context
 * Holds context specific data
 */
struct e5010_context {
	struct v4l2_fh			fh;
	struct e5010_dev		*e5010;
	struct e5010_q_data		out_queue;
	struct e5010_q_data		cap_queue;
	int				quality;
	bool				update_qp;
	struct v4l2_ctrl_handler	ctrl_handler;
	u8				luma_qp[QP_TABLE_SIZE];
	u8				chroma_qp[QP_TABLE_SIZE];
};

/*
 * Buffer structure
 * Contains info for all buffers
 */
struct e5010_buffer {
	struct v4l2_m2m_buffer buffer;
};

enum {
	CHROMA_ORDER_CB_CR = 0, //UV ordering
	CHROMA_ORDER_CR_CB = 1, //VU ordering
};

enum {
	SUBSAMPLING_420 = 1,
	SUBSAMPLING_422 = 2,
};

/*
 * e5010 format structure
 * contains format information
 */
struct e5010_fmt {
	u32					fourcc;
	unsigned int				num_planes;
	unsigned int				type;
	u32					subsampling;
	u32					chroma_order;
	const struct v4l2_frmsize_stepwise	frmsize;
};

/*
 * struct e5010_ctrl - contains info for each supported v4l2 control
 */
struct e5010_ctrl {
	unsigned int		cid;
	enum v4l2_ctrl_type	type;
	unsigned char		name[32];
	int			minimum;
	int			maximum;
	int			step;
	int			default_value;
	unsigned char		compound;
};

#endif
