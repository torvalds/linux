/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Intel Corporation */

#ifndef __IPU3_CSS_H
#define __IPU3_CSS_H

#include <linux/videodev2.h>
#include <linux/types.h>

#include "ipu3-abi.h"
#include "ipu3-css-pool.h"

/* 2 stages for split isp pipeline, 1 for scaling */
#define IMGU_NUM_SP			2
#define IMGU_MAX_PIPELINE_NUM		20
#define IMGU_MAX_PIPE_NUM		2

/* For DVS etc., format FRAME_FMT_YUV420_16 */
#define IPU3_CSS_AUX_FRAME_REF		0
/* For temporal noise reduction DVS etc., format FRAME_FMT_YUV_LINE */
#define IPU3_CSS_AUX_FRAME_TNR		1
#define IPU3_CSS_AUX_FRAME_TYPES	2	/* REF and TNR */
#define IPU3_CSS_AUX_FRAMES		2	/* 2 for REF and 2 for TNR */

#define IPU3_CSS_QUEUE_IN		0
#define IPU3_CSS_QUEUE_PARAMS		1
#define IPU3_CSS_QUEUE_OUT		2
#define IPU3_CSS_QUEUE_VF		3
#define IPU3_CSS_QUEUE_STAT_3A		4
#define IPU3_CSS_QUEUES			5

#define IPU3_CSS_RECT_EFFECTIVE		0	/* Effective resolution */
#define IPU3_CSS_RECT_BDS		1	/* Resolution after BDS */
#define IPU3_CSS_RECT_ENVELOPE		2	/* DVS envelope size */
#define IPU3_CSS_RECT_GDC		3	/* gdc output res */
#define IPU3_CSS_RECTS			4	/* number of rects */

#define IA_CSS_BINARY_MODE_PRIMARY	2
#define IA_CSS_BINARY_MODE_VIDEO	3
#define IPU3_CSS_DEFAULT_BINARY		3	/* default binary index */

/*
 * The pipe id type, distinguishes the kind of pipes that
 * can be run in parallel.
 */
enum imgu_css_pipe_id {
	IPU3_CSS_PIPE_ID_PREVIEW,
	IPU3_CSS_PIPE_ID_COPY,
	IPU3_CSS_PIPE_ID_VIDEO,
	IPU3_CSS_PIPE_ID_CAPTURE,
	IPU3_CSS_PIPE_ID_YUVPP,
	IPU3_CSS_PIPE_ID_ACC,
	IPU3_CSS_PIPE_ID_NUM
};

struct imgu_css_resolution {
	u32 w;
	u32 h;
};

enum imgu_css_buffer_state {
	IPU3_CSS_BUFFER_NEW,	/* Not yet queued */
	IPU3_CSS_BUFFER_QUEUED,	/* Queued, waiting to be filled */
	IPU3_CSS_BUFFER_DONE,	/* Finished processing, removed from queue */
	IPU3_CSS_BUFFER_FAILED,	/* Was not processed, removed from queue */
};

struct imgu_css_buffer {
	/* Private fields: user doesn't touch */
	dma_addr_t daddr;
	unsigned int queue;
	enum imgu_css_buffer_state state;
	struct list_head list;
	u8 queue_pos;
	unsigned int pipe;
};

struct imgu_css_format {
	u32 pixelformat;
	enum v4l2_colorspace colorspace;
	enum imgu_abi_frame_format frame_format;
	enum imgu_abi_bayer_order bayer_order;
	enum imgu_abi_osys_format osys_format;
	enum imgu_abi_osys_tiling osys_tiling;
	u32 bytesperpixel_num;	/* Bytes per pixel in first plane * 50 */
	u8 bit_depth;		/* Effective bits per pixel */
	u8 chroma_decim;	/* Chroma plane decimation, 0=no chroma plane */
	u8 width_align;		/* Alignment requirement for width_pad */
	u8 flags;
};

struct imgu_css_queue {
	union {
		struct v4l2_pix_format_mplane mpix;
		struct v4l2_meta_format	meta;

	} fmt;
	const struct imgu_css_format *css_fmt;
	unsigned int width_pad;
	struct list_head bufs;
};

struct imgu_css_pipe {
	enum imgu_css_pipe_id pipe_id;
	unsigned int bindex;

	struct imgu_css_queue queue[IPU3_CSS_QUEUES];
	struct v4l2_rect rect[IPU3_CSS_RECTS];

	bool vf_output_en;
	/* Protect access to queue[IPU3_CSS_QUEUES] */
	spinlock_t qlock;

	/* Data structures shared with IMGU and driver, always allocated */
	struct imgu_css_map sp_ddr_ptrs;
	struct imgu_css_map xmem_sp_stage_ptrs[IPU3_CSS_PIPE_ID_NUM]
					    [IMGU_ABI_MAX_STAGES];
	struct imgu_css_map xmem_isp_stage_ptrs[IPU3_CSS_PIPE_ID_NUM]
					    [IMGU_ABI_MAX_STAGES];

	/*
	 * Data structures shared with IMGU and driver, binary specific.
	 * PARAM_CLASS_CONFIG and PARAM_CLASS_STATE parameters.
	 */
	struct imgu_css_map binary_params_cs[IMGU_ABI_PARAM_CLASS_NUM - 1]
					    [IMGU_ABI_NUM_MEMORIES];

	struct {
		struct imgu_css_map mem[IPU3_CSS_AUX_FRAMES];
		unsigned int width;
		unsigned int height;
		unsigned int bytesperline;
		unsigned int bytesperpixel;
	} aux_frames[IPU3_CSS_AUX_FRAME_TYPES];

	struct {
		struct imgu_css_pool parameter_set_info;
		struct imgu_css_pool acc;
		struct imgu_css_pool gdc;
		struct imgu_css_pool obgrid;
		/* PARAM_CLASS_PARAM parameters for binding while streaming */
		struct imgu_css_pool binary_params_p[IMGU_ABI_NUM_MEMORIES];
	} pool;

	struct imgu_css_map abi_buffers[IPU3_CSS_QUEUES]
				    [IMGU_ABI_HOST2SP_BUFQ_SIZE];
};

/* IPU3 Camera Sub System structure */
struct imgu_css {
	struct device *dev;
	void __iomem *base;
	const struct firmware *fw;
	struct imgu_fw_header *fwp;
	int iomem_length;
	int fw_bl, fw_sp[IMGU_NUM_SP];	/* Indices of bl and SP binaries */
	struct imgu_css_map *binary;	/* fw binaries mapped to device */
	bool streaming;		/* true when streaming is enabled */

	struct imgu_css_pipe pipes[IMGU_MAX_PIPE_NUM];
	struct imgu_css_map xmem_sp_group_ptrs;

	/* enabled pipe(s) */
	DECLARE_BITMAP(enabled_pipes, IMGU_MAX_PIPE_NUM);
};

/******************* css v4l *******************/
int imgu_css_init(struct device *dev, struct imgu_css *css,
		  void __iomem *base, int length);
void imgu_css_cleanup(struct imgu_css *css);
int imgu_css_fmt_try(struct imgu_css *css,
		     struct v4l2_pix_format_mplane *fmts[IPU3_CSS_QUEUES],
		     struct v4l2_rect *rects[IPU3_CSS_RECTS],
		     unsigned int pipe);
int imgu_css_fmt_set(struct imgu_css *css,
		     struct v4l2_pix_format_mplane *fmts[IPU3_CSS_QUEUES],
		     struct v4l2_rect *rects[IPU3_CSS_RECTS],
		     unsigned int pipe);
int imgu_css_meta_fmt_set(struct v4l2_meta_format *fmt);
int imgu_css_buf_queue(struct imgu_css *css, unsigned int pipe,
		       struct imgu_css_buffer *b);
struct imgu_css_buffer *imgu_css_buf_dequeue(struct imgu_css *css);
int imgu_css_start_streaming(struct imgu_css *css);
void imgu_css_stop_streaming(struct imgu_css *css);
bool imgu_css_queue_empty(struct imgu_css *css);
bool imgu_css_is_streaming(struct imgu_css *css);
bool imgu_css_pipe_queue_empty(struct imgu_css *css, unsigned int pipe);

/******************* css hw *******************/
int imgu_css_set_powerup(struct device *dev, void __iomem *base);
void imgu_css_set_powerdown(struct device *dev, void __iomem *base);
int imgu_css_irq_ack(struct imgu_css *css);

/******************* set parameters ************/
int imgu_css_set_parameters(struct imgu_css *css, unsigned int pipe,
			    struct ipu3_uapi_params *set_params);

/******************* auxiliary helpers *******************/
static inline enum imgu_css_buffer_state
imgu_css_buf_state(struct imgu_css_buffer *b)
{
	return b->state;
}

/* Initialize given buffer. May be called several times. */
static inline void imgu_css_buf_init(struct imgu_css_buffer *b,
				     unsigned int queue, dma_addr_t daddr)
{
	b->state = IPU3_CSS_BUFFER_NEW;
	b->queue = queue;
	b->daddr = daddr;
}
#endif
