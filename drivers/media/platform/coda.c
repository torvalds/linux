/*
 * Coda multi-standard codec IP
 *
 * Copyright (C) 2012 Vista Silicon S.L.
 *    Javier Martin, <javier.martin@vista-silicon.com>
 *    Xavier Duret
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/of.h>
#include <linux/platform_data/coda.h>
#include <linux/reset.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "coda.h"

#define CODA_NAME		"coda"

#define CODADX6_MAX_INSTANCES	4

#define CODA_PARA_BUF_SIZE	(10 * 1024)
#define CODA_ISRAM_SIZE	(2048 * 2)

#define CODA7_PS_BUF_SIZE	0x28000
#define CODA9_PS_SAVE_SIZE	(512 * 1024)

#define CODA_MAX_FRAMEBUFFERS	8

#define CODA_MAX_FRAME_SIZE	0x100000
#define FMO_SLICE_SAVE_BUF_SIZE         (32)
#define CODA_DEFAULT_GAMMA		4096
#define CODA9_DEFAULT_GAMMA		24576	/* 0.75 * 32768 */

#define MIN_W 176
#define MIN_H 144

#define S_ALIGN		1 /* multiple of 2 */
#define W_ALIGN		1 /* multiple of 2 */
#define H_ALIGN		1 /* multiple of 2 */

#define fh_to_ctx(__fh)	container_of(__fh, struct coda_ctx, fh)

static int coda_debug;
module_param(coda_debug, int, 0644);
MODULE_PARM_DESC(coda_debug, "Debug level (0-1)");

enum {
	V4L2_M2M_SRC = 0,
	V4L2_M2M_DST = 1,
};

enum coda_inst_type {
	CODA_INST_ENCODER,
	CODA_INST_DECODER,
};

enum coda_product {
	CODA_DX6 = 0xf001,
	CODA_7541 = 0xf012,
	CODA_960 = 0xf020,
};

struct coda_fmt {
	char *name;
	u32 fourcc;
};

struct coda_codec {
	u32 mode;
	u32 src_fourcc;
	u32 dst_fourcc;
	u32 max_w;
	u32 max_h;
};

struct coda_devtype {
	char			*firmware;
	enum coda_product	product;
	struct coda_codec	*codecs;
	unsigned int		num_codecs;
	size_t			workbuf_size;
	size_t			tempbuf_size;
	size_t			iram_size;
};

/* Per-queue, driver-specific private data */
struct coda_q_data {
	unsigned int		width;
	unsigned int		height;
	unsigned int		bytesperline;
	unsigned int		sizeimage;
	unsigned int		fourcc;
	struct v4l2_rect	rect;
};

struct coda_aux_buf {
	void			*vaddr;
	dma_addr_t		paddr;
	u32			size;
	struct debugfs_blob_wrapper blob;
	struct dentry		*dentry;
};

struct coda_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	vfd;
	struct platform_device	*plat_dev;
	const struct coda_devtype *devtype;

	void __iomem		*regs_base;
	struct clk		*clk_per;
	struct clk		*clk_ahb;
	struct reset_control	*rstc;

	struct coda_aux_buf	codebuf;
	struct coda_aux_buf	tempbuf;
	struct coda_aux_buf	workbuf;
	struct gen_pool		*iram_pool;
	struct coda_aux_buf	iram;

	spinlock_t		irqlock;
	struct mutex		dev_mutex;
	struct mutex		coda_mutex;
	struct workqueue_struct	*workqueue;
	struct v4l2_m2m_dev	*m2m_dev;
	struct vb2_alloc_ctx	*alloc_ctx;
	struct list_head	instances;
	unsigned long		instance_mask;
	struct dentry		*debugfs_root;
};

struct coda_params {
	u8			rot_mode;
	u8			h264_intra_qp;
	u8			h264_inter_qp;
	u8			h264_min_qp;
	u8			h264_max_qp;
	u8			h264_deblk_enabled;
	u8			h264_deblk_alpha;
	u8			h264_deblk_beta;
	u8			mpeg4_intra_qp;
	u8			mpeg4_inter_qp;
	u8			gop_size;
	int			intra_refresh;
	int			codec_mode;
	int			codec_mode_aux;
	enum v4l2_mpeg_video_multi_slice_mode slice_mode;
	u32			framerate;
	u16			bitrate;
	u32			slice_max_bits;
	u32			slice_max_mb;
};

struct coda_iram_info {
	u32		axi_sram_use;
	phys_addr_t	buf_bit_use;
	phys_addr_t	buf_ip_ac_dc_use;
	phys_addr_t	buf_dbk_y_use;
	phys_addr_t	buf_dbk_c_use;
	phys_addr_t	buf_ovl_use;
	phys_addr_t	buf_btp_use;
	phys_addr_t	search_ram_paddr;
	int		search_ram_size;
	int		remaining;
	phys_addr_t	next_paddr;
};

struct gdi_tiled_map {
	int xy2ca_map[16];
	int xy2ba_map[16];
	int xy2ra_map[16];
	int rbc2axi_map[32];
	int xy2rbc_config;
	int map_type;
#define GDI_LINEAR_FRAME_MAP 0
};

struct coda_timestamp {
	struct list_head	list;
	u32			sequence;
	struct v4l2_timecode	timecode;
	struct timeval		timestamp;
};

struct coda_ctx {
	struct coda_dev			*dev;
	struct mutex			buffer_mutex;
	struct list_head		list;
	struct work_struct		pic_run_work;
	struct work_struct		seq_end_work;
	struct completion		completion;
	int				aborting;
	int				initialized;
	int				streamon_out;
	int				streamon_cap;
	u32				isequence;
	u32				qsequence;
	u32				osequence;
	u32				sequence_offset;
	struct coda_q_data		q_data[2];
	enum coda_inst_type		inst_type;
	struct coda_codec		*codec;
	enum v4l2_colorspace		colorspace;
	struct coda_params		params;
	struct v4l2_ctrl_handler	ctrls;
	struct v4l2_fh			fh;
	int				gopcounter;
	int				runcounter;
	char				vpu_header[3][64];
	int				vpu_header_size[3];
	struct kfifo			bitstream_fifo;
	struct mutex			bitstream_mutex;
	struct coda_aux_buf		bitstream;
	bool				hold;
	struct coda_aux_buf		parabuf;
	struct coda_aux_buf		psbuf;
	struct coda_aux_buf		slicebuf;
	struct coda_aux_buf		internal_frames[CODA_MAX_FRAMEBUFFERS];
	u32				frame_types[CODA_MAX_FRAMEBUFFERS];
	struct coda_timestamp		frame_timestamps[CODA_MAX_FRAMEBUFFERS];
	u32				frame_errors[CODA_MAX_FRAMEBUFFERS];
	struct list_head		timestamp_list;
	struct coda_aux_buf		workbuf;
	int				num_internal_frames;
	int				idx;
	int				reg_idx;
	struct coda_iram_info		iram_info;
	struct gdi_tiled_map		tiled_map;
	u32				bit_stream_param;
	u32				frm_dis_flg;
	u32				frame_mem_ctrl;
	int				display_idx;
	struct dentry			*debugfs_entry;
};

static const u8 coda_filler_nal[14] = { 0x00, 0x00, 0x00, 0x01, 0x0c, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80 };
static const u8 coda_filler_size[8] = { 0, 7, 14, 13, 12, 11, 10, 9 };

static inline void coda_write(struct coda_dev *dev, u32 data, u32 reg)
{
	v4l2_dbg(1, coda_debug, &dev->v4l2_dev,
		 "%s: data=0x%x, reg=0x%x\n", __func__, data, reg);
	writel(data, dev->regs_base + reg);
}

static inline unsigned int coda_read(struct coda_dev *dev, u32 reg)
{
	u32 data;
	data = readl(dev->regs_base + reg);
	v4l2_dbg(1, coda_debug, &dev->v4l2_dev,
		 "%s: data=0x%x, reg=0x%x\n", __func__, data, reg);
	return data;
}

static inline unsigned long coda_isbusy(struct coda_dev *dev)
{
	return coda_read(dev, CODA_REG_BIT_BUSY);
}

static inline int coda_is_initialized(struct coda_dev *dev)
{
	return (coda_read(dev, CODA_REG_BIT_CUR_PC) != 0);
}

static int coda_wait_timeout(struct coda_dev *dev)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	while (coda_isbusy(dev)) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
	}
	return 0;
}

static void coda_command_async(struct coda_ctx *ctx, int cmd)
{
	struct coda_dev *dev = ctx->dev;

	if (dev->devtype->product == CODA_960 ||
	    dev->devtype->product == CODA_7541) {
		/* Restore context related registers to CODA */
		coda_write(dev, ctx->bit_stream_param,
				CODA_REG_BIT_BIT_STREAM_PARAM);
		coda_write(dev, ctx->frm_dis_flg,
				CODA_REG_BIT_FRM_DIS_FLG(ctx->reg_idx));
		coda_write(dev, ctx->frame_mem_ctrl,
				CODA_REG_BIT_FRAME_MEM_CTRL);
		coda_write(dev, ctx->workbuf.paddr, CODA_REG_BIT_WORK_BUF_ADDR);
	}

	if (dev->devtype->product == CODA_960) {
		coda_write(dev, 1, CODA9_GDI_WPROT_ERR_CLR);
		coda_write(dev, 0, CODA9_GDI_WPROT_RGN_EN);
	}

	coda_write(dev, CODA_REG_BIT_BUSY_FLAG, CODA_REG_BIT_BUSY);

	coda_write(dev, ctx->idx, CODA_REG_BIT_RUN_INDEX);
	coda_write(dev, ctx->params.codec_mode, CODA_REG_BIT_RUN_COD_STD);
	coda_write(dev, ctx->params.codec_mode_aux, CODA7_REG_BIT_RUN_AUX_STD);

	coda_write(dev, cmd, CODA_REG_BIT_RUN_COMMAND);
}

static int coda_command_sync(struct coda_ctx *ctx, int cmd)
{
	struct coda_dev *dev = ctx->dev;

	coda_command_async(ctx, cmd);
	return coda_wait_timeout(dev);
}

static int coda_hw_reset(struct coda_ctx *ctx)
{
	struct coda_dev *dev = ctx->dev;
	unsigned long timeout;
	unsigned int idx;
	int ret;

	if (!dev->rstc)
		return -ENOENT;

	idx = coda_read(dev, CODA_REG_BIT_RUN_INDEX);

	timeout = jiffies + msecs_to_jiffies(100);
	coda_write(dev, 0x11, CODA9_GDI_BUS_CTRL);
	while (coda_read(dev, CODA9_GDI_BUS_STATUS) != 0x77) {
		if (time_after(jiffies, timeout))
			return -ETIME;
		cpu_relax();
	}

	ret = reset_control_reset(dev->rstc);
	if (ret < 0)
		return ret;

	coda_write(dev, 0x00, CODA9_GDI_BUS_CTRL);
	coda_write(dev, CODA_REG_BIT_BUSY_FLAG, CODA_REG_BIT_BUSY);
	coda_write(dev, CODA_REG_RUN_ENABLE, CODA_REG_BIT_CODE_RUN);
	ret = coda_wait_timeout(dev);
	coda_write(dev, idx, CODA_REG_BIT_RUN_INDEX);

	return ret;
}

static struct coda_q_data *get_q_data(struct coda_ctx *ctx,
					 enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &(ctx->q_data[V4L2_M2M_SRC]);
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &(ctx->q_data[V4L2_M2M_DST]);
	default:
		return NULL;
	}
}

/*
 * Array of all formats supported by any version of Coda:
 */
static struct coda_fmt coda_formats[] = {
	{
		.name = "YUV 4:2:0 Planar, YCbCr",
		.fourcc = V4L2_PIX_FMT_YUV420,
	},
	{
		.name = "YUV 4:2:0 Planar, YCrCb",
		.fourcc = V4L2_PIX_FMT_YVU420,
	},
	{
		.name = "H264 Encoded Stream",
		.fourcc = V4L2_PIX_FMT_H264,
	},
	{
		.name = "MPEG4 Encoded Stream",
		.fourcc = V4L2_PIX_FMT_MPEG4,
	},
};

#define CODA_CODEC(mode, src_fourcc, dst_fourcc, max_w, max_h) \
	{ mode, src_fourcc, dst_fourcc, max_w, max_h }

/*
 * Arrays of codecs supported by each given version of Coda:
 *  i.MX27 -> codadx6
 *  i.MX5x -> coda7
 *  i.MX6  -> coda960
 * Use V4L2_PIX_FMT_YUV420 as placeholder for all supported YUV 4:2:0 variants
 */
static struct coda_codec codadx6_codecs[] = {
	CODA_CODEC(CODADX6_MODE_ENCODE_H264, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_H264,  720, 576),
	CODA_CODEC(CODADX6_MODE_ENCODE_MP4,  V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_MPEG4, 720, 576),
};

static struct coda_codec coda7_codecs[] = {
	CODA_CODEC(CODA7_MODE_ENCODE_H264, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_H264,   1280, 720),
	CODA_CODEC(CODA7_MODE_ENCODE_MP4,  V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_MPEG4,  1280, 720),
	CODA_CODEC(CODA7_MODE_DECODE_H264, V4L2_PIX_FMT_H264,   V4L2_PIX_FMT_YUV420, 1920, 1080),
	CODA_CODEC(CODA7_MODE_DECODE_MP4,  V4L2_PIX_FMT_MPEG4,  V4L2_PIX_FMT_YUV420, 1920, 1080),
};

static struct coda_codec coda9_codecs[] = {
	CODA_CODEC(CODA9_MODE_ENCODE_H264, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_H264,   1920, 1080),
	CODA_CODEC(CODA9_MODE_ENCODE_MP4,  V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_MPEG4,  1920, 1080),
	CODA_CODEC(CODA9_MODE_DECODE_H264, V4L2_PIX_FMT_H264,   V4L2_PIX_FMT_YUV420, 1920, 1080),
	CODA_CODEC(CODA9_MODE_DECODE_MP4,  V4L2_PIX_FMT_MPEG4,  V4L2_PIX_FMT_YUV420, 1920, 1080),
};

static bool coda_format_is_yuv(u32 fourcc)
{
	switch (fourcc) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		return true;
	default:
		return false;
	}
}

/*
 * Normalize all supported YUV 4:2:0 formats to the value used in the codec
 * tables.
 */
static u32 coda_format_normalize_yuv(u32 fourcc)
{
	return coda_format_is_yuv(fourcc) ? V4L2_PIX_FMT_YUV420 : fourcc;
}

static struct coda_codec *coda_find_codec(struct coda_dev *dev, int src_fourcc,
					  int dst_fourcc)
{
	struct coda_codec *codecs = dev->devtype->codecs;
	int num_codecs = dev->devtype->num_codecs;
	int k;

	src_fourcc = coda_format_normalize_yuv(src_fourcc);
	dst_fourcc = coda_format_normalize_yuv(dst_fourcc);
	if (src_fourcc == dst_fourcc)
		return NULL;

	for (k = 0; k < num_codecs; k++) {
		if (codecs[k].src_fourcc == src_fourcc &&
		    codecs[k].dst_fourcc == dst_fourcc)
			break;
	}

	if (k == num_codecs)
		return NULL;

	return &codecs[k];
}

static void coda_get_max_dimensions(struct coda_dev *dev,
				    struct coda_codec *codec,
				    int *max_w, int *max_h)
{
	struct coda_codec *codecs = dev->devtype->codecs;
	int num_codecs = dev->devtype->num_codecs;
	unsigned int w, h;
	int k;

	if (codec) {
		w = codec->max_w;
		h = codec->max_h;
	} else {
		for (k = 0, w = 0, h = 0; k < num_codecs; k++) {
			w = max(w, codecs[k].max_w);
			h = max(h, codecs[k].max_h);
		}
	}

	if (max_w)
		*max_w = w;
	if (max_h)
		*max_h = h;
}

static char *coda_product_name(int product)
{
	static char buf[9];

	switch (product) {
	case CODA_DX6:
		return "CodaDx6";
	case CODA_7541:
		return "CODA7541";
	case CODA_960:
		return "CODA960";
	default:
		snprintf(buf, sizeof(buf), "(0x%04x)", product);
		return buf;
	}
}

/*
 * V4L2 ioctl() operations.
 */
static int coda_querycap(struct file *file, void *priv,
			 struct v4l2_capability *cap)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);

	strlcpy(cap->driver, CODA_NAME, sizeof(cap->driver));
	strlcpy(cap->card, coda_product_name(ctx->dev->devtype->product),
		sizeof(cap->card));
	strlcpy(cap->bus_info, "platform:" CODA_NAME, sizeof(cap->bus_info));
	/*
	 * This is only a mem-to-mem video device. The capture and output
	 * device capability flags are left only for backward compatibility
	 * and are scheduled for removal.
	 */
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT |
			   V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int enum_fmt(void *priv, struct v4l2_fmtdesc *f,
			enum v4l2_buf_type type, int src_fourcc)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	struct coda_codec *codecs = ctx->dev->devtype->codecs;
	struct coda_fmt *formats = coda_formats;
	struct coda_fmt *fmt;
	int num_codecs = ctx->dev->devtype->num_codecs;
	int num_formats = ARRAY_SIZE(coda_formats);
	int i, k, num = 0;

	for (i = 0; i < num_formats; i++) {
		/* Both uncompressed formats are always supported */
		if (coda_format_is_yuv(formats[i].fourcc) &&
		    !coda_format_is_yuv(src_fourcc)) {
			if (num == f->index)
				break;
			++num;
			continue;
		}
		/* Compressed formats may be supported, check the codec list */
		for (k = 0; k < num_codecs; k++) {
			/* if src_fourcc is set, only consider matching codecs */
			if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
			    formats[i].fourcc == codecs[k].dst_fourcc &&
			    (!src_fourcc || src_fourcc == codecs[k].src_fourcc))
				break;
			if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT &&
			    formats[i].fourcc == codecs[k].src_fourcc)
				break;
		}
		if (k < num_codecs) {
			if (num == f->index)
				break;
			++num;
		}
	}

	if (i < num_formats) {
		fmt = &formats[i];
		strlcpy(f->description, fmt->name, sizeof(f->description));
		f->pixelformat = fmt->fourcc;
		if (!coda_format_is_yuv(fmt->fourcc))
			f->flags |= V4L2_FMT_FLAG_COMPRESSED;
		return 0;
	}

	/* Format not found */
	return -EINVAL;
}

static int coda_enum_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_fmtdesc *f)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *src_vq;
	struct coda_q_data *q_data_src;

	/* If the source format is already fixed, only list matching formats */
	src_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	if (vb2_is_streaming(src_vq)) {
		q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);

		return enum_fmt(priv, f, V4L2_BUF_TYPE_VIDEO_CAPTURE,
				q_data_src->fourcc);
	}

	return enum_fmt(priv, f, V4L2_BUF_TYPE_VIDEO_CAPTURE, 0);
}

static int coda_enum_fmt_vid_out(struct file *file, void *priv,
				 struct v4l2_fmtdesc *f)
{
	return enum_fmt(priv, f, V4L2_BUF_TYPE_VIDEO_OUTPUT, 0);
}

static int coda_g_fmt(struct file *file, void *priv,
		      struct v4l2_format *f)
{
	struct coda_q_data *q_data;
	struct coda_ctx *ctx = fh_to_ctx(priv);

	q_data = get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	f->fmt.pix.field	= V4L2_FIELD_NONE;
	f->fmt.pix.pixelformat	= q_data->fourcc;
	f->fmt.pix.width	= q_data->width;
	f->fmt.pix.height	= q_data->height;
	f->fmt.pix.bytesperline = q_data->bytesperline;

	f->fmt.pix.sizeimage	= q_data->sizeimage;
	f->fmt.pix.colorspace	= ctx->colorspace;

	return 0;
}

static int coda_try_fmt(struct coda_ctx *ctx, struct coda_codec *codec,
			struct v4l2_format *f)
{
	struct coda_dev *dev = ctx->dev;
	struct coda_q_data *q_data;
	unsigned int max_w, max_h;
	enum v4l2_field field;

	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY)
		field = V4L2_FIELD_NONE;
	else if (V4L2_FIELD_NONE != field)
		return -EINVAL;

	/* V4L2 specification suggests the driver corrects the format struct
	 * if any of the dimensions is unsupported */
	f->fmt.pix.field = field;

	coda_get_max_dimensions(dev, codec, &max_w, &max_h);
	v4l_bound_align_image(&f->fmt.pix.width, MIN_W, max_w, W_ALIGN,
			      &f->fmt.pix.height, MIN_H, max_h, H_ALIGN,
			      S_ALIGN);

	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_H264:
	case V4L2_PIX_FMT_MPEG4:
	case V4L2_PIX_FMT_JPEG:
		break;
	default:
		q_data = get_q_data(ctx, f->type);
		if (!q_data)
			return -EINVAL;
		f->fmt.pix.pixelformat = q_data->fourcc;
	}

	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		/* Frame stride must be multiple of 8, but 16 for h.264 */
		f->fmt.pix.bytesperline = round_up(f->fmt.pix.width, 16);
		f->fmt.pix.sizeimage = f->fmt.pix.bytesperline *
					f->fmt.pix.height * 3 / 2;
		break;
	case V4L2_PIX_FMT_H264:
	case V4L2_PIX_FMT_MPEG4:
	case V4L2_PIX_FMT_JPEG:
		f->fmt.pix.bytesperline = 0;
		f->fmt.pix.sizeimage = CODA_MAX_FRAME_SIZE;
		break;
	default:
		BUG();
	}

	return 0;
}

static int coda_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	struct coda_codec *codec;
	struct vb2_queue *src_vq;
	int ret;

	/*
	 * If the source format is already fixed, try to find a codec that
	 * converts to the given destination format
	 */
	src_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	if (vb2_is_streaming(src_vq)) {
		struct coda_q_data *q_data_src;

		q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
		codec = coda_find_codec(ctx->dev, q_data_src->fourcc,
					f->fmt.pix.pixelformat);
		if (!codec)
			return -EINVAL;
	} else {
		/* Otherwise determine codec by encoded format, if possible */
		codec = coda_find_codec(ctx->dev, V4L2_PIX_FMT_YUV420,
					f->fmt.pix.pixelformat);
	}

	f->fmt.pix.colorspace = ctx->colorspace;

	ret = coda_try_fmt(ctx, codec, f);
	if (ret < 0)
		return ret;

	/* The h.264 decoder only returns complete 16x16 macroblocks */
	if (codec && codec->src_fourcc == V4L2_PIX_FMT_H264) {
		f->fmt.pix.width = f->fmt.pix.width;
		f->fmt.pix.height = round_up(f->fmt.pix.height, 16);
		f->fmt.pix.bytesperline = round_up(f->fmt.pix.width, 16);
		f->fmt.pix.sizeimage = f->fmt.pix.bytesperline *
				       f->fmt.pix.height * 3 / 2;
	}

	return 0;
}

static int coda_try_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	struct coda_codec *codec;

	/* Determine codec by encoded format, returns NULL if raw or invalid */
	codec = coda_find_codec(ctx->dev, f->fmt.pix.pixelformat,
				V4L2_PIX_FMT_YUV420);

	if (!f->fmt.pix.colorspace)
		f->fmt.pix.colorspace = V4L2_COLORSPACE_REC709;

	return coda_try_fmt(ctx, codec, f);
}

static int coda_s_fmt(struct coda_ctx *ctx, struct v4l2_format *f)
{
	struct coda_q_data *q_data;
	struct vb2_queue *vq;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		v4l2_err(&ctx->dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	q_data->fourcc = f->fmt.pix.pixelformat;
	q_data->width = f->fmt.pix.width;
	q_data->height = f->fmt.pix.height;
	q_data->bytesperline = f->fmt.pix.bytesperline;
	q_data->sizeimage = f->fmt.pix.sizeimage;
	q_data->rect.left = 0;
	q_data->rect.top = 0;
	q_data->rect.width = f->fmt.pix.width;
	q_data->rect.height = f->fmt.pix.height;

	v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
		"Setting format for type %d, wxh: %dx%d, fmt: %d\n",
		f->type, q_data->width, q_data->height, q_data->fourcc);

	return 0;
}

static int coda_s_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	int ret;

	ret = coda_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	return coda_s_fmt(ctx, f);
}

static int coda_s_fmt_vid_out(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	int ret;

	ret = coda_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	ret = coda_s_fmt(ctx, f);
	if (ret)
		ctx->colorspace = f->fmt.pix.colorspace;

	return ret;
}

static int coda_qbuf(struct file *file, void *priv,
		     struct v4l2_buffer *buf)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);

	return v4l2_m2m_qbuf(file, ctx->fh.m2m_ctx, buf);
}

static bool coda_buf_is_end_of_stream(struct coda_ctx *ctx,
				      struct v4l2_buffer *buf)
{
	struct vb2_queue *src_vq;

	src_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);

	return ((ctx->bit_stream_param & CODA_BIT_STREAM_END_FLAG) &&
		(buf->sequence == (ctx->qsequence - 1)));
}

static int coda_dqbuf(struct file *file, void *priv,
		      struct v4l2_buffer *buf)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	int ret;

	ret = v4l2_m2m_dqbuf(file, ctx->fh.m2m_ctx, buf);

	/* If this is the last capture buffer, emit an end-of-stream event */
	if (buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    coda_buf_is_end_of_stream(ctx, buf)) {
		const struct v4l2_event eos_event = {
			.type = V4L2_EVENT_EOS
		};

		v4l2_event_queue_fh(&ctx->fh, &eos_event);
	}

	return ret;
}

static int coda_g_selection(struct file *file, void *fh,
			    struct v4l2_selection *s)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);
	struct coda_q_data *q_data;
	struct v4l2_rect r, *rsel;

	q_data = get_q_data(ctx, s->type);
	if (!q_data)
		return -EINVAL;

	r.left = 0;
	r.top = 0;
	r.width = q_data->width;
	r.height = q_data->height;
	rsel = &q_data->rect;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		rsel = &r;
		/* fallthrough */
	case V4L2_SEL_TGT_CROP:
		if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		rsel = &r;
		/* fallthrough */
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	s->r = *rsel;

	return 0;
}

static int coda_try_decoder_cmd(struct file *file, void *fh,
				struct v4l2_decoder_cmd *dc)
{
	if (dc->cmd != V4L2_DEC_CMD_STOP)
		return -EINVAL;

	if (dc->flags & V4L2_DEC_CMD_STOP_TO_BLACK)
		return -EINVAL;

	if (!(dc->flags & V4L2_DEC_CMD_STOP_IMMEDIATELY) && (dc->stop.pts != 0))
		return -EINVAL;

	return 0;
}

static int coda_decoder_cmd(struct file *file, void *fh,
			    struct v4l2_decoder_cmd *dc)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);
	struct coda_dev *dev = ctx->dev;
	int ret;

	ret = coda_try_decoder_cmd(file, fh, dc);
	if (ret < 0)
		return ret;

	/* Ignore decoder stop command silently in encoder context */
	if (ctx->inst_type != CODA_INST_DECODER)
		return 0;

	/* Set the strem-end flag on this context */
	ctx->bit_stream_param |= CODA_BIT_STREAM_END_FLAG;

	if ((dev->devtype->product == CODA_960) &&
	    coda_isbusy(dev) &&
	    (ctx->idx == coda_read(dev, CODA_REG_BIT_RUN_INDEX))) {
		/* If this context is currently running, update the hardware flag */
		coda_write(dev, ctx->bit_stream_param, CODA_REG_BIT_BIT_STREAM_PARAM);
	}
	ctx->hold = false;
	v4l2_m2m_try_schedule(ctx->fh.m2m_ctx);

	return 0;
}

static int coda_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

static const struct v4l2_ioctl_ops coda_ioctl_ops = {
	.vidioc_querycap	= coda_querycap,

	.vidioc_enum_fmt_vid_cap = coda_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	= coda_g_fmt,
	.vidioc_try_fmt_vid_cap	= coda_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= coda_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out = coda_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out	= coda_g_fmt,
	.vidioc_try_fmt_vid_out	= coda_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out	= coda_s_fmt_vid_out,

	.vidioc_reqbufs		= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf	= v4l2_m2m_ioctl_querybuf,

	.vidioc_qbuf		= coda_qbuf,
	.vidioc_expbuf		= v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf		= coda_dqbuf,
	.vidioc_create_bufs	= v4l2_m2m_ioctl_create_bufs,

	.vidioc_streamon	= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff	= v4l2_m2m_ioctl_streamoff,

	.vidioc_g_selection	= coda_g_selection,

	.vidioc_try_decoder_cmd	= coda_try_decoder_cmd,
	.vidioc_decoder_cmd	= coda_decoder_cmd,

	.vidioc_subscribe_event = coda_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int coda_start_decoding(struct coda_ctx *ctx);

static inline int coda_get_bitstream_payload(struct coda_ctx *ctx)
{
	return kfifo_len(&ctx->bitstream_fifo);
}

static void coda_kfifo_sync_from_device(struct coda_ctx *ctx)
{
	struct __kfifo *kfifo = &ctx->bitstream_fifo.kfifo;
	struct coda_dev *dev = ctx->dev;
	u32 rd_ptr;

	rd_ptr = coda_read(dev, CODA_REG_BIT_RD_PTR(ctx->reg_idx));
	kfifo->out = (kfifo->in & ~kfifo->mask) |
		      (rd_ptr - ctx->bitstream.paddr);
	if (kfifo->out > kfifo->in)
		kfifo->out -= kfifo->mask + 1;
}

static void coda_kfifo_sync_to_device_full(struct coda_ctx *ctx)
{
	struct __kfifo *kfifo = &ctx->bitstream_fifo.kfifo;
	struct coda_dev *dev = ctx->dev;
	u32 rd_ptr, wr_ptr;

	rd_ptr = ctx->bitstream.paddr + (kfifo->out & kfifo->mask);
	coda_write(dev, rd_ptr, CODA_REG_BIT_RD_PTR(ctx->reg_idx));
	wr_ptr = ctx->bitstream.paddr + (kfifo->in & kfifo->mask);
	coda_write(dev, wr_ptr, CODA_REG_BIT_WR_PTR(ctx->reg_idx));
}

static void coda_kfifo_sync_to_device_write(struct coda_ctx *ctx)
{
	struct __kfifo *kfifo = &ctx->bitstream_fifo.kfifo;
	struct coda_dev *dev = ctx->dev;
	u32 wr_ptr;

	wr_ptr = ctx->bitstream.paddr + (kfifo->in & kfifo->mask);
	coda_write(dev, wr_ptr, CODA_REG_BIT_WR_PTR(ctx->reg_idx));
}

static int coda_bitstream_queue(struct coda_ctx *ctx, struct vb2_buffer *src_buf)
{
	u32 src_size = vb2_get_plane_payload(src_buf, 0);
	u32 n;

	n = kfifo_in(&ctx->bitstream_fifo, vb2_plane_vaddr(src_buf, 0), src_size);
	if (n < src_size)
		return -ENOSPC;

	dma_sync_single_for_device(&ctx->dev->plat_dev->dev, ctx->bitstream.paddr,
				   ctx->bitstream.size, DMA_TO_DEVICE);

	src_buf->v4l2_buf.sequence = ctx->qsequence++;

	return 0;
}

static bool coda_bitstream_try_queue(struct coda_ctx *ctx,
				     struct vb2_buffer *src_buf)
{
	int ret;

	if (coda_get_bitstream_payload(ctx) +
	    vb2_get_plane_payload(src_buf, 0) + 512 >= ctx->bitstream.size)
		return false;

	if (vb2_plane_vaddr(src_buf, 0) == NULL) {
		v4l2_err(&ctx->dev->v4l2_dev, "trying to queue empty buffer\n");
		return true;
	}

	ret = coda_bitstream_queue(ctx, src_buf);
	if (ret < 0) {
		v4l2_err(&ctx->dev->v4l2_dev, "bitstream buffer overflow\n");
		return false;
	}
	/* Sync read pointer to device */
	if (ctx == v4l2_m2m_get_curr_priv(ctx->dev->m2m_dev))
		coda_kfifo_sync_to_device_write(ctx);

	ctx->hold = false;

	return true;
}

static void coda_fill_bitstream(struct coda_ctx *ctx)
{
	struct vb2_buffer *src_buf;
	struct coda_timestamp *ts;

	while (v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx) > 0) {
		src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);

		if (coda_bitstream_try_queue(ctx, src_buf)) {
			/*
			 * Source buffer is queued in the bitstream ringbuffer;
			 * queue the timestamp and mark source buffer as done
			 */
			src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);

			ts = kmalloc(sizeof(*ts), GFP_KERNEL);
			if (ts) {
				ts->sequence = src_buf->v4l2_buf.sequence;
				ts->timecode = src_buf->v4l2_buf.timecode;
				ts->timestamp = src_buf->v4l2_buf.timestamp;
				list_add_tail(&ts->list, &ctx->timestamp_list);
			}

			v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
		} else {
			break;
		}
	}
}

static void coda_set_gdi_regs(struct coda_ctx *ctx)
{
	struct gdi_tiled_map *tiled_map = &ctx->tiled_map;
	struct coda_dev *dev = ctx->dev;
	int i;

	for (i = 0; i < 16; i++)
		coda_write(dev, tiled_map->xy2ca_map[i],
				CODA9_GDI_XY2_CAS_0 + 4 * i);
	for (i = 0; i < 4; i++)
		coda_write(dev, tiled_map->xy2ba_map[i],
				CODA9_GDI_XY2_BA_0 + 4 * i);
	for (i = 0; i < 16; i++)
		coda_write(dev, tiled_map->xy2ra_map[i],
				CODA9_GDI_XY2_RAS_0 + 4 * i);
	coda_write(dev, tiled_map->xy2rbc_config, CODA9_GDI_XY2_RBC_CONFIG);
	for (i = 0; i < 32; i++)
		coda_write(dev, tiled_map->rbc2axi_map[i],
				CODA9_GDI_RBC2_AXI_0 + 4 * i);
}

/*
 * Mem-to-mem operations.
 */
static int coda_prepare_decode(struct coda_ctx *ctx)
{
	struct vb2_buffer *dst_buf;
	struct coda_dev *dev = ctx->dev;
	struct coda_q_data *q_data_dst;
	u32 stridey, height;
	u32 picture_y, picture_cb, picture_cr;

	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);

	if (ctx->params.rot_mode & CODA_ROT_90) {
		stridey = q_data_dst->height;
		height = q_data_dst->width;
	} else {
		stridey = q_data_dst->width;
		height = q_data_dst->height;
	}

	/* Try to copy source buffer contents into the bitstream ringbuffer */
	mutex_lock(&ctx->bitstream_mutex);
	coda_fill_bitstream(ctx);
	mutex_unlock(&ctx->bitstream_mutex);

	if (coda_get_bitstream_payload(ctx) < 512 &&
	    (!(ctx->bit_stream_param & CODA_BIT_STREAM_END_FLAG))) {
		v4l2_dbg(1, coda_debug, &dev->v4l2_dev,
			 "bitstream payload: %d, skipping\n",
			 coda_get_bitstream_payload(ctx));
		v4l2_m2m_job_finish(ctx->dev->m2m_dev, ctx->fh.m2m_ctx);
		return -EAGAIN;
	}

	/* Run coda_start_decoding (again) if not yet initialized */
	if (!ctx->initialized) {
		int ret = coda_start_decoding(ctx);
		if (ret < 0) {
			v4l2_err(&dev->v4l2_dev, "failed to start decoding\n");
			v4l2_m2m_job_finish(ctx->dev->m2m_dev, ctx->fh.m2m_ctx);
			return -EAGAIN;
		} else {
			ctx->initialized = 1;
		}
	}

	if (dev->devtype->product == CODA_960)
		coda_set_gdi_regs(ctx);

	/* Set rotator output */
	picture_y = vb2_dma_contig_plane_dma_addr(dst_buf, 0);
	if (q_data_dst->fourcc == V4L2_PIX_FMT_YVU420) {
		/* Switch Cr and Cb for YVU420 format */
		picture_cr = picture_y + stridey * height;
		picture_cb = picture_cr + stridey / 2 * height / 2;
	} else {
		picture_cb = picture_y + stridey * height;
		picture_cr = picture_cb + stridey / 2 * height / 2;
	}

	if (dev->devtype->product == CODA_960) {
		/*
		 * The CODA960 seems to have an internal list of buffers with
		 * 64 entries that includes the registered frame buffers as
		 * well as the rotator buffer output.
		 * ROT_INDEX needs to be < 0x40, but > ctx->num_internal_frames.
		 */
		coda_write(dev, CODA_MAX_FRAMEBUFFERS + dst_buf->v4l2_buf.index,
				CODA9_CMD_DEC_PIC_ROT_INDEX);
		coda_write(dev, picture_y, CODA9_CMD_DEC_PIC_ROT_ADDR_Y);
		coda_write(dev, picture_cb, CODA9_CMD_DEC_PIC_ROT_ADDR_CB);
		coda_write(dev, picture_cr, CODA9_CMD_DEC_PIC_ROT_ADDR_CR);
		coda_write(dev, stridey, CODA9_CMD_DEC_PIC_ROT_STRIDE);
	} else {
		coda_write(dev, picture_y, CODA_CMD_DEC_PIC_ROT_ADDR_Y);
		coda_write(dev, picture_cb, CODA_CMD_DEC_PIC_ROT_ADDR_CB);
		coda_write(dev, picture_cr, CODA_CMD_DEC_PIC_ROT_ADDR_CR);
		coda_write(dev, stridey, CODA_CMD_DEC_PIC_ROT_STRIDE);
	}
	coda_write(dev, CODA_ROT_MIR_ENABLE | ctx->params.rot_mode,
			CODA_CMD_DEC_PIC_ROT_MODE);

	switch (dev->devtype->product) {
	case CODA_DX6:
		/* TBD */
	case CODA_7541:
		coda_write(dev, CODA_PRE_SCAN_EN, CODA_CMD_DEC_PIC_OPTION);
		break;
	case CODA_960:
		coda_write(dev, (1 << 10), CODA_CMD_DEC_PIC_OPTION); /* 'hardcode to use interrupt disable mode'? */
		break;
	}

	coda_write(dev, 0, CODA_CMD_DEC_PIC_SKIP_NUM);

	coda_write(dev, 0, CODA_CMD_DEC_PIC_BB_START);
	coda_write(dev, 0, CODA_CMD_DEC_PIC_START_BYTE);

	return 0;
}

static void coda_prepare_encode(struct coda_ctx *ctx)
{
	struct coda_q_data *q_data_src, *q_data_dst;
	struct vb2_buffer *src_buf, *dst_buf;
	struct coda_dev *dev = ctx->dev;
	int force_ipicture;
	int quant_param = 0;
	u32 picture_y, picture_cb, picture_cr;
	u32 pic_stream_buffer_addr, pic_stream_buffer_size;
	u32 dst_fourcc;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	dst_fourcc = q_data_dst->fourcc;

	src_buf->v4l2_buf.sequence = ctx->osequence;
	dst_buf->v4l2_buf.sequence = ctx->osequence;
	ctx->osequence++;

	/*
	 * Workaround coda firmware BUG that only marks the first
	 * frame as IDR. This is a problem for some decoders that can't
	 * recover when a frame is lost.
	 */
	if (src_buf->v4l2_buf.sequence % ctx->params.gop_size) {
		src_buf->v4l2_buf.flags |= V4L2_BUF_FLAG_PFRAME;
		src_buf->v4l2_buf.flags &= ~V4L2_BUF_FLAG_KEYFRAME;
	} else {
		src_buf->v4l2_buf.flags |= V4L2_BUF_FLAG_KEYFRAME;
		src_buf->v4l2_buf.flags &= ~V4L2_BUF_FLAG_PFRAME;
	}

	if (dev->devtype->product == CODA_960)
		coda_set_gdi_regs(ctx);

	/*
	 * Copy headers at the beginning of the first frame for H.264 only.
	 * In MPEG4 they are already copied by the coda.
	 */
	if (src_buf->v4l2_buf.sequence == 0) {
		pic_stream_buffer_addr =
			vb2_dma_contig_plane_dma_addr(dst_buf, 0) +
			ctx->vpu_header_size[0] +
			ctx->vpu_header_size[1] +
			ctx->vpu_header_size[2];
		pic_stream_buffer_size = CODA_MAX_FRAME_SIZE -
			ctx->vpu_header_size[0] -
			ctx->vpu_header_size[1] -
			ctx->vpu_header_size[2];
		memcpy(vb2_plane_vaddr(dst_buf, 0),
		       &ctx->vpu_header[0][0], ctx->vpu_header_size[0]);
		memcpy(vb2_plane_vaddr(dst_buf, 0) + ctx->vpu_header_size[0],
		       &ctx->vpu_header[1][0], ctx->vpu_header_size[1]);
		memcpy(vb2_plane_vaddr(dst_buf, 0) + ctx->vpu_header_size[0] +
			ctx->vpu_header_size[1], &ctx->vpu_header[2][0],
			ctx->vpu_header_size[2]);
	} else {
		pic_stream_buffer_addr =
			vb2_dma_contig_plane_dma_addr(dst_buf, 0);
		pic_stream_buffer_size = CODA_MAX_FRAME_SIZE;
	}

	if (src_buf->v4l2_buf.flags & V4L2_BUF_FLAG_KEYFRAME) {
		force_ipicture = 1;
		switch (dst_fourcc) {
		case V4L2_PIX_FMT_H264:
			quant_param = ctx->params.h264_intra_qp;
			break;
		case V4L2_PIX_FMT_MPEG4:
			quant_param = ctx->params.mpeg4_intra_qp;
			break;
		default:
			v4l2_warn(&ctx->dev->v4l2_dev,
				"cannot set intra qp, fmt not supported\n");
			break;
		}
	} else {
		force_ipicture = 0;
		switch (dst_fourcc) {
		case V4L2_PIX_FMT_H264:
			quant_param = ctx->params.h264_inter_qp;
			break;
		case V4L2_PIX_FMT_MPEG4:
			quant_param = ctx->params.mpeg4_inter_qp;
			break;
		default:
			v4l2_warn(&ctx->dev->v4l2_dev,
				"cannot set inter qp, fmt not supported\n");
			break;
		}
	}

	/* submit */
	coda_write(dev, CODA_ROT_MIR_ENABLE | ctx->params.rot_mode, CODA_CMD_ENC_PIC_ROT_MODE);
	coda_write(dev, quant_param, CODA_CMD_ENC_PIC_QS);


	picture_y = vb2_dma_contig_plane_dma_addr(src_buf, 0);
	switch (q_data_src->fourcc) {
	case V4L2_PIX_FMT_YVU420:
		/* Switch Cb and Cr for YVU420 format */
		picture_cr = picture_y + q_data_src->bytesperline *
				q_data_src->height;
		picture_cb = picture_cr + q_data_src->bytesperline / 2 *
				q_data_src->height / 2;
		break;
	case V4L2_PIX_FMT_YUV420:
	default:
		picture_cb = picture_y + q_data_src->bytesperline *
				q_data_src->height;
		picture_cr = picture_cb + q_data_src->bytesperline / 2 *
				q_data_src->height / 2;
		break;
	}

	if (dev->devtype->product == CODA_960) {
		coda_write(dev, 4/*FIXME: 0*/, CODA9_CMD_ENC_PIC_SRC_INDEX);
		coda_write(dev, q_data_src->width, CODA9_CMD_ENC_PIC_SRC_STRIDE);
		coda_write(dev, 0, CODA9_CMD_ENC_PIC_SUB_FRAME_SYNC);

		coda_write(dev, picture_y, CODA9_CMD_ENC_PIC_SRC_ADDR_Y);
		coda_write(dev, picture_cb, CODA9_CMD_ENC_PIC_SRC_ADDR_CB);
		coda_write(dev, picture_cr, CODA9_CMD_ENC_PIC_SRC_ADDR_CR);
	} else {
		coda_write(dev, picture_y, CODA_CMD_ENC_PIC_SRC_ADDR_Y);
		coda_write(dev, picture_cb, CODA_CMD_ENC_PIC_SRC_ADDR_CB);
		coda_write(dev, picture_cr, CODA_CMD_ENC_PIC_SRC_ADDR_CR);
	}
	coda_write(dev, force_ipicture << 1 & 0x2,
		   CODA_CMD_ENC_PIC_OPTION);

	coda_write(dev, pic_stream_buffer_addr, CODA_CMD_ENC_PIC_BB_START);
	coda_write(dev, pic_stream_buffer_size / 1024,
		   CODA_CMD_ENC_PIC_BB_SIZE);

	if (!ctx->streamon_out) {
		/* After streamoff on the output side, set the stream end flag */
		ctx->bit_stream_param |= CODA_BIT_STREAM_END_FLAG;
		coda_write(dev, ctx->bit_stream_param, CODA_REG_BIT_BIT_STREAM_PARAM);
	}
}

static void coda_device_run(void *m2m_priv)
{
	struct coda_ctx *ctx = m2m_priv;
	struct coda_dev *dev = ctx->dev;

	queue_work(dev->workqueue, &ctx->pic_run_work);
}

static void coda_free_framebuffers(struct coda_ctx *ctx);
static void coda_free_context_buffers(struct coda_ctx *ctx);

static void coda_seq_end_work(struct work_struct *work)
{
	struct coda_ctx *ctx = container_of(work, struct coda_ctx, seq_end_work);
	struct coda_dev *dev = ctx->dev;

	mutex_lock(&ctx->buffer_mutex);
	mutex_lock(&dev->coda_mutex);

	v4l2_dbg(1, coda_debug, &dev->v4l2_dev,
		 "%d: %s: sent command 'SEQ_END' to coda\n", ctx->idx, __func__);
	if (coda_command_sync(ctx, CODA_COMMAND_SEQ_END)) {
		v4l2_err(&dev->v4l2_dev,
			 "CODA_COMMAND_SEQ_END failed\n");
	}

	kfifo_init(&ctx->bitstream_fifo,
		ctx->bitstream.vaddr, ctx->bitstream.size);

	coda_free_framebuffers(ctx);
	coda_free_context_buffers(ctx);

	mutex_unlock(&dev->coda_mutex);
	mutex_unlock(&ctx->buffer_mutex);
}

static void coda_finish_decode(struct coda_ctx *ctx);
static void coda_finish_encode(struct coda_ctx *ctx);

static void coda_pic_run_work(struct work_struct *work)
{
	struct coda_ctx *ctx = container_of(work, struct coda_ctx, pic_run_work);
	struct coda_dev *dev = ctx->dev;
	int ret;

	mutex_lock(&ctx->buffer_mutex);
	mutex_lock(&dev->coda_mutex);

	if (ctx->inst_type == CODA_INST_DECODER) {
		ret = coda_prepare_decode(ctx);
		if (ret < 0) {
			mutex_unlock(&dev->coda_mutex);
			mutex_unlock(&ctx->buffer_mutex);
			/* job_finish scheduled by prepare_decode */
			return;
		}
	} else {
		coda_prepare_encode(ctx);
	}

	if (dev->devtype->product != CODA_DX6)
		coda_write(dev, ctx->iram_info.axi_sram_use,
				CODA7_REG_BIT_AXI_SRAM_USE);

	if (ctx->inst_type == CODA_INST_DECODER)
		coda_kfifo_sync_to_device_full(ctx);
	coda_command_async(ctx, CODA_COMMAND_PIC_RUN);

	if (!wait_for_completion_timeout(&ctx->completion, msecs_to_jiffies(1000))) {
		dev_err(&dev->plat_dev->dev, "CODA PIC_RUN timeout\n");

		ctx->hold = true;

		coda_hw_reset(ctx);
	} else if (!ctx->aborting) {
		if (ctx->inst_type == CODA_INST_DECODER)
			coda_finish_decode(ctx);
		else
			coda_finish_encode(ctx);
	}

	if (ctx->aborting || (!ctx->streamon_cap && !ctx->streamon_out))
		queue_work(dev->workqueue, &ctx->seq_end_work);

	mutex_unlock(&dev->coda_mutex);
	mutex_unlock(&ctx->buffer_mutex);

	v4l2_m2m_job_finish(ctx->dev->m2m_dev, ctx->fh.m2m_ctx);
}

static int coda_job_ready(void *m2m_priv)
{
	struct coda_ctx *ctx = m2m_priv;

	/*
	 * For both 'P' and 'key' frame cases 1 picture
	 * and 1 frame are needed. In the decoder case,
	 * the compressed frame can be in the bitstream.
	 */
	if (!v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx) &&
	    ctx->inst_type != CODA_INST_DECODER) {
		v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			 "not ready: not enough video buffers.\n");
		return 0;
	}

	if (!v4l2_m2m_num_dst_bufs_ready(ctx->fh.m2m_ctx)) {
		v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			 "not ready: not enough video capture buffers.\n");
		return 0;
	}

	if (ctx->hold ||
	    ((ctx->inst_type == CODA_INST_DECODER) &&
	     (coda_get_bitstream_payload(ctx) < 512) &&
	     !(ctx->bit_stream_param & CODA_BIT_STREAM_END_FLAG))) {
		v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			 "%d: not ready: not enough bitstream data.\n",
			 ctx->idx);
		return 0;
	}

	if (ctx->aborting) {
		v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			 "not ready: aborting\n");
		return 0;
	}

	v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			"job ready\n");
	return 1;
}

static void coda_job_abort(void *priv)
{
	struct coda_ctx *ctx = priv;

	ctx->aborting = 1;

	v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
		 "Aborting task\n");
}

static void coda_lock(void *m2m_priv)
{
	struct coda_ctx *ctx = m2m_priv;
	struct coda_dev *pcdev = ctx->dev;
	mutex_lock(&pcdev->dev_mutex);
}

static void coda_unlock(void *m2m_priv)
{
	struct coda_ctx *ctx = m2m_priv;
	struct coda_dev *pcdev = ctx->dev;
	mutex_unlock(&pcdev->dev_mutex);
}

static struct v4l2_m2m_ops coda_m2m_ops = {
	.device_run	= coda_device_run,
	.job_ready	= coda_job_ready,
	.job_abort	= coda_job_abort,
	.lock		= coda_lock,
	.unlock		= coda_unlock,
};

static void coda_set_tiled_map_type(struct coda_ctx *ctx, int tiled_map_type)
{
	struct gdi_tiled_map *tiled_map = &ctx->tiled_map;
	int luma_map, chro_map, i;

	memset(tiled_map, 0, sizeof(*tiled_map));

	luma_map = 64;
	chro_map = 64;
	tiled_map->map_type = tiled_map_type;
	for (i = 0; i < 16; i++)
		tiled_map->xy2ca_map[i] = luma_map << 8 | chro_map;
	for (i = 0; i < 4; i++)
		tiled_map->xy2ba_map[i] = luma_map << 8 | chro_map;
	for (i = 0; i < 16; i++)
		tiled_map->xy2ra_map[i] = luma_map << 8 | chro_map;

	if (tiled_map_type == GDI_LINEAR_FRAME_MAP) {
		tiled_map->xy2rbc_config = 0;
	} else {
		dev_err(&ctx->dev->plat_dev->dev, "invalid map type: %d\n",
			tiled_map_type);
		return;
	}
}

static void set_default_params(struct coda_ctx *ctx)
{
	int max_w;
	int max_h;

	ctx->codec = &ctx->dev->devtype->codecs[0];
	max_w = ctx->codec->max_w;
	max_h = ctx->codec->max_h;

	ctx->params.codec_mode = CODA_MODE_INVALID;
	ctx->colorspace = V4L2_COLORSPACE_REC709;
	ctx->params.framerate = 30;
	ctx->aborting = 0;

	/* Default formats for output and input queues */
	ctx->q_data[V4L2_M2M_SRC].fourcc = ctx->codec->src_fourcc;
	ctx->q_data[V4L2_M2M_DST].fourcc = ctx->codec->dst_fourcc;
	ctx->q_data[V4L2_M2M_SRC].width = max_w;
	ctx->q_data[V4L2_M2M_SRC].height = max_h;
	ctx->q_data[V4L2_M2M_SRC].bytesperline = max_w;
	ctx->q_data[V4L2_M2M_SRC].sizeimage = (max_w * max_h * 3) / 2;
	ctx->q_data[V4L2_M2M_DST].width = max_w;
	ctx->q_data[V4L2_M2M_DST].height = max_h;
	ctx->q_data[V4L2_M2M_DST].bytesperline = 0;
	ctx->q_data[V4L2_M2M_DST].sizeimage = CODA_MAX_FRAME_SIZE;
	ctx->q_data[V4L2_M2M_SRC].rect.width = max_w;
	ctx->q_data[V4L2_M2M_SRC].rect.height = max_h;
	ctx->q_data[V4L2_M2M_DST].rect.width = max_w;
	ctx->q_data[V4L2_M2M_DST].rect.height = max_h;

	if (ctx->dev->devtype->product == CODA_960)
		coda_set_tiled_map_type(ctx, GDI_LINEAR_FRAME_MAP);
}

/*
 * Queue operations
 */
static int coda_queue_setup(struct vb2_queue *vq,
				const struct v4l2_format *fmt,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], void *alloc_ctxs[])
{
	struct coda_ctx *ctx = vb2_get_drv_priv(vq);
	struct coda_q_data *q_data;
	unsigned int size;

	q_data = get_q_data(ctx, vq->type);
	size = q_data->sizeimage;

	*nplanes = 1;
	sizes[0] = size;

	alloc_ctxs[0] = ctx->dev->alloc_ctx;

	v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
		 "get %d buffer(s) of size %d each.\n", *nbuffers, size);

	return 0;
}

static int coda_buf_prepare(struct vb2_buffer *vb)
{
	struct coda_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct coda_q_data *q_data;

	q_data = get_q_data(ctx, vb->vb2_queue->type);

	if (vb2_plane_size(vb, 0) < q_data->sizeimage) {
		v4l2_warn(&ctx->dev->v4l2_dev,
			  "%s data will not fit into plane (%lu < %lu)\n",
			  __func__, vb2_plane_size(vb, 0),
			  (long)q_data->sizeimage);
		return -EINVAL;
	}

	return 0;
}

static void coda_buf_queue(struct vb2_buffer *vb)
{
	struct coda_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct coda_dev *dev = ctx->dev;
	struct coda_q_data *q_data;

	q_data = get_q_data(ctx, vb->vb2_queue->type);

	/*
	 * In the decoder case, immediately try to copy the buffer into the
	 * bitstream ringbuffer and mark it as ready to be dequeued.
	 */
	if (q_data->fourcc == V4L2_PIX_FMT_H264 &&
	    vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		/*
		 * For backwards compatibility, queuing an empty buffer marks
		 * the stream end
		 */
		if (vb2_get_plane_payload(vb, 0) == 0) {
			ctx->bit_stream_param |= CODA_BIT_STREAM_END_FLAG;
			if ((dev->devtype->product == CODA_960) &&
			    coda_isbusy(dev) &&
			    (ctx->idx == coda_read(dev, CODA_REG_BIT_RUN_INDEX))) {
				/* if this decoder instance is running, set the stream end flag */
				coda_write(dev, ctx->bit_stream_param, CODA_REG_BIT_BIT_STREAM_PARAM);
			}
		}
		mutex_lock(&ctx->bitstream_mutex);
		v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vb);
		coda_fill_bitstream(ctx);
		mutex_unlock(&ctx->bitstream_mutex);
	} else {
		v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vb);
	}
}

static void coda_parabuf_write(struct coda_ctx *ctx, int index, u32 value)
{
	struct coda_dev *dev = ctx->dev;
	u32 *p = ctx->parabuf.vaddr;

	if (dev->devtype->product == CODA_DX6)
		p[index] = value;
	else
		p[index ^ 1] = value;
}

static int coda_alloc_aux_buf(struct coda_dev *dev,
			      struct coda_aux_buf *buf, size_t size,
			      const char *name, struct dentry *parent)
{
	buf->vaddr = dma_alloc_coherent(&dev->plat_dev->dev, size, &buf->paddr,
					GFP_KERNEL);
	if (!buf->vaddr)
		return -ENOMEM;

	buf->size = size;

	if (name && parent) {
		buf->blob.data = buf->vaddr;
		buf->blob.size = size;
		buf->dentry = debugfs_create_blob(name, 0644, parent, &buf->blob);
		if (!buf->dentry)
			dev_warn(&dev->plat_dev->dev,
				 "failed to create debugfs entry %s\n", name);
	}

	return 0;
}

static inline int coda_alloc_context_buf(struct coda_ctx *ctx,
					 struct coda_aux_buf *buf, size_t size,
					 const char *name)
{
	return coda_alloc_aux_buf(ctx->dev, buf, size, name, ctx->debugfs_entry);
}

static void coda_free_aux_buf(struct coda_dev *dev,
			      struct coda_aux_buf *buf)
{
	if (buf->vaddr) {
		dma_free_coherent(&dev->plat_dev->dev, buf->size,
				  buf->vaddr, buf->paddr);
		buf->vaddr = NULL;
		buf->size = 0;
	}
	debugfs_remove(buf->dentry);
}

static void coda_free_framebuffers(struct coda_ctx *ctx)
{
	int i;

	for (i = 0; i < CODA_MAX_FRAMEBUFFERS; i++)
		coda_free_aux_buf(ctx->dev, &ctx->internal_frames[i]);
}

static int coda_alloc_framebuffers(struct coda_ctx *ctx, struct coda_q_data *q_data, u32 fourcc)
{
	struct coda_dev *dev = ctx->dev;
	int width, height;
	dma_addr_t paddr;
	int ysize;
	int ret;
	int i;

	if (ctx->codec && (ctx->codec->src_fourcc == V4L2_PIX_FMT_H264 ||
	     ctx->codec->dst_fourcc == V4L2_PIX_FMT_H264)) {
		width = round_up(q_data->width, 16);
		height = round_up(q_data->height, 16);
	} else {
		width = round_up(q_data->width, 8);
		height = q_data->height;
	}
	ysize = width * height;

	/* Allocate frame buffers */
	for (i = 0; i < ctx->num_internal_frames; i++) {
		size_t size;
		char *name;

		size = ysize + ysize / 2;
		if (ctx->codec->src_fourcc == V4L2_PIX_FMT_H264 &&
		    dev->devtype->product != CODA_DX6)
			size += ysize / 4;
		name = kasprintf(GFP_KERNEL, "fb%d", i);
		ret = coda_alloc_context_buf(ctx, &ctx->internal_frames[i],
					     size, name);
		kfree(name);
		if (ret < 0) {
			coda_free_framebuffers(ctx);
			return ret;
		}
	}

	/* Register frame buffers in the parameter buffer */
	for (i = 0; i < ctx->num_internal_frames; i++) {
		paddr = ctx->internal_frames[i].paddr;
		coda_parabuf_write(ctx, i * 3 + 0, paddr); /* Y */
		coda_parabuf_write(ctx, i * 3 + 1, paddr + ysize); /* Cb */
		coda_parabuf_write(ctx, i * 3 + 2, paddr + ysize + ysize/4); /* Cr */

		/* mvcol buffer for h.264 */
		if (ctx->codec->src_fourcc == V4L2_PIX_FMT_H264 &&
		    dev->devtype->product != CODA_DX6)
			coda_parabuf_write(ctx, 96 + i,
					   ctx->internal_frames[i].paddr +
					   ysize + ysize/4 + ysize/4);
	}

	/* mvcol buffer for mpeg4 */
	if ((dev->devtype->product != CODA_DX6) &&
	    (ctx->codec->src_fourcc == V4L2_PIX_FMT_MPEG4))
		coda_parabuf_write(ctx, 97, ctx->internal_frames[i].paddr +
					    ysize + ysize/4 + ysize/4);

	return 0;
}

static int coda_h264_padding(int size, char *p)
{
	int nal_size;
	int diff;

	diff = size - (size & ~0x7);
	if (diff == 0)
		return 0;

	nal_size = coda_filler_size[diff];
	memcpy(p, coda_filler_nal, nal_size);

	/* Add rbsp stop bit and trailing at the end */
	*(p + nal_size - 1) = 0x80;

	return nal_size;
}

static phys_addr_t coda_iram_alloc(struct coda_iram_info *iram, size_t size)
{
	phys_addr_t ret;

	size = round_up(size, 1024);
	if (size > iram->remaining)
		return 0;
	iram->remaining -= size;

	ret = iram->next_paddr;
	iram->next_paddr += size;

	return ret;
}

static void coda_setup_iram(struct coda_ctx *ctx)
{
	struct coda_iram_info *iram_info = &ctx->iram_info;
	struct coda_dev *dev = ctx->dev;
	int mb_width;
	int dbk_bits;
	int bit_bits;
	int ip_bits;

	memset(iram_info, 0, sizeof(*iram_info));
	iram_info->next_paddr = dev->iram.paddr;
	iram_info->remaining = dev->iram.size;

	switch (dev->devtype->product) {
	case CODA_7541:
		dbk_bits = CODA7_USE_HOST_DBK_ENABLE | CODA7_USE_DBK_ENABLE;
		bit_bits = CODA7_USE_HOST_BIT_ENABLE | CODA7_USE_BIT_ENABLE;
		ip_bits = CODA7_USE_HOST_IP_ENABLE | CODA7_USE_IP_ENABLE;
		break;
	case CODA_960:
		dbk_bits = CODA9_USE_HOST_DBK_ENABLE | CODA9_USE_DBK_ENABLE;
		bit_bits = CODA9_USE_HOST_BIT_ENABLE | CODA7_USE_BIT_ENABLE;
		ip_bits = CODA9_USE_HOST_IP_ENABLE | CODA7_USE_IP_ENABLE;
		break;
	default: /* CODA_DX6 */
		return;
	}

	if (ctx->inst_type == CODA_INST_ENCODER) {
		struct coda_q_data *q_data_src;

		q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
		mb_width = DIV_ROUND_UP(q_data_src->width, 16);

		/* Prioritize in case IRAM is too small for everything */
		if (dev->devtype->product == CODA_7541) {
			iram_info->search_ram_size = round_up(mb_width * 16 *
							      36 + 2048, 1024);
			iram_info->search_ram_paddr = coda_iram_alloc(iram_info,
							iram_info->search_ram_size);
			if (!iram_info->search_ram_paddr) {
				pr_err("IRAM is smaller than the search ram size\n");
				goto out;
			}
			iram_info->axi_sram_use |= CODA7_USE_HOST_ME_ENABLE |
						   CODA7_USE_ME_ENABLE;
		}

		/* Only H.264BP and H.263P3 are considered */
		iram_info->buf_dbk_y_use = coda_iram_alloc(iram_info, 64 * mb_width);
		iram_info->buf_dbk_c_use = coda_iram_alloc(iram_info, 64 * mb_width);
		if (!iram_info->buf_dbk_c_use)
			goto out;
		iram_info->axi_sram_use |= dbk_bits;

		iram_info->buf_bit_use = coda_iram_alloc(iram_info, 128 * mb_width);
		if (!iram_info->buf_bit_use)
			goto out;
		iram_info->axi_sram_use |= bit_bits;

		iram_info->buf_ip_ac_dc_use = coda_iram_alloc(iram_info, 128 * mb_width);
		if (!iram_info->buf_ip_ac_dc_use)
			goto out;
		iram_info->axi_sram_use |= ip_bits;

		/* OVL and BTP disabled for encoder */
	} else if (ctx->inst_type == CODA_INST_DECODER) {
		struct coda_q_data *q_data_dst;

		q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
		mb_width = DIV_ROUND_UP(q_data_dst->width, 16);

		iram_info->buf_dbk_y_use = coda_iram_alloc(iram_info, 128 * mb_width);
		iram_info->buf_dbk_c_use = coda_iram_alloc(iram_info, 128 * mb_width);
		if (!iram_info->buf_dbk_c_use)
			goto out;
		iram_info->axi_sram_use |= dbk_bits;

		iram_info->buf_bit_use = coda_iram_alloc(iram_info, 128 * mb_width);
		if (!iram_info->buf_bit_use)
			goto out;
		iram_info->axi_sram_use |= bit_bits;

		iram_info->buf_ip_ac_dc_use = coda_iram_alloc(iram_info, 128 * mb_width);
		if (!iram_info->buf_ip_ac_dc_use)
			goto out;
		iram_info->axi_sram_use |= ip_bits;

		/* OVL and BTP unused as there is no VC1 support yet */
	}

out:
	if (!(iram_info->axi_sram_use & CODA7_USE_HOST_IP_ENABLE))
		v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			 "IRAM smaller than needed\n");

	if (dev->devtype->product == CODA_7541) {
		/* TODO - Enabling these causes picture errors on CODA7541 */
		if (ctx->inst_type == CODA_INST_DECODER) {
			/* fw 1.4.50 */
			iram_info->axi_sram_use &= ~(CODA7_USE_HOST_IP_ENABLE |
						     CODA7_USE_IP_ENABLE);
		} else {
			/* fw 13.4.29 */
			iram_info->axi_sram_use &= ~(CODA7_USE_HOST_IP_ENABLE |
						     CODA7_USE_HOST_DBK_ENABLE |
						     CODA7_USE_IP_ENABLE |
						     CODA7_USE_DBK_ENABLE);
		}
	}
}

static void coda_free_context_buffers(struct coda_ctx *ctx)
{
	struct coda_dev *dev = ctx->dev;

	coda_free_aux_buf(dev, &ctx->slicebuf);
	coda_free_aux_buf(dev, &ctx->psbuf);
	if (dev->devtype->product != CODA_DX6)
		coda_free_aux_buf(dev, &ctx->workbuf);
}

static int coda_alloc_context_buffers(struct coda_ctx *ctx,
				      struct coda_q_data *q_data)
{
	struct coda_dev *dev = ctx->dev;
	size_t size;
	int ret;

	if (dev->devtype->product == CODA_DX6)
		return 0;

	if (ctx->psbuf.vaddr) {
		v4l2_err(&dev->v4l2_dev, "psmembuf still allocated\n");
		return -EBUSY;
	}
	if (ctx->slicebuf.vaddr) {
		v4l2_err(&dev->v4l2_dev, "slicebuf still allocated\n");
		return -EBUSY;
	}
	if (ctx->workbuf.vaddr) {
		v4l2_err(&dev->v4l2_dev, "context buffer still allocated\n");
		ret = -EBUSY;
		return -ENOMEM;
	}

	if (q_data->fourcc == V4L2_PIX_FMT_H264) {
		/* worst case slice size */
		size = (DIV_ROUND_UP(q_data->width, 16) *
			DIV_ROUND_UP(q_data->height, 16)) * 3200 / 8 + 512;
		ret = coda_alloc_context_buf(ctx, &ctx->slicebuf, size, "slicebuf");
		if (ret < 0) {
			v4l2_err(&dev->v4l2_dev, "failed to allocate %d byte slice buffer",
				 ctx->slicebuf.size);
			return ret;
		}
	}

	if (dev->devtype->product == CODA_7541) {
		ret = coda_alloc_context_buf(ctx, &ctx->psbuf, CODA7_PS_BUF_SIZE, "psbuf");
		if (ret < 0) {
			v4l2_err(&dev->v4l2_dev, "failed to allocate psmem buffer");
			goto err;
		}
	}

	size = dev->devtype->workbuf_size;
	if (dev->devtype->product == CODA_960 &&
	    q_data->fourcc == V4L2_PIX_FMT_H264)
		size += CODA9_PS_SAVE_SIZE;
	ret = coda_alloc_context_buf(ctx, &ctx->workbuf, size, "workbuf");
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev, "failed to allocate %d byte context buffer",
			 ctx->workbuf.size);
		goto err;
	}

	return 0;

err:
	coda_free_context_buffers(ctx);
	return ret;
}

static int coda_start_decoding(struct coda_ctx *ctx)
{
	struct coda_q_data *q_data_src, *q_data_dst;
	u32 bitstream_buf, bitstream_size;
	struct coda_dev *dev = ctx->dev;
	int width, height;
	u32 src_fourcc;
	u32 val;
	int ret;

	/* Start decoding */
	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	bitstream_buf = ctx->bitstream.paddr;
	bitstream_size = ctx->bitstream.size;
	src_fourcc = q_data_src->fourcc;

	coda_write(dev, ctx->parabuf.paddr, CODA_REG_BIT_PARA_BUF_ADDR);

	/* Update coda bitstream read and write pointers from kfifo */
	coda_kfifo_sync_to_device_full(ctx);

	ctx->display_idx = -1;
	ctx->frm_dis_flg = 0;
	coda_write(dev, 0, CODA_REG_BIT_FRM_DIS_FLG(ctx->reg_idx));

	coda_write(dev, CODA_BIT_DEC_SEQ_INIT_ESCAPE,
			CODA_REG_BIT_BIT_STREAM_PARAM);

	coda_write(dev, bitstream_buf, CODA_CMD_DEC_SEQ_BB_START);
	coda_write(dev, bitstream_size / 1024, CODA_CMD_DEC_SEQ_BB_SIZE);
	val = 0;
	if ((dev->devtype->product == CODA_7541) ||
	    (dev->devtype->product == CODA_960))
		val |= CODA_REORDER_ENABLE;
	coda_write(dev, val, CODA_CMD_DEC_SEQ_OPTION);

	ctx->params.codec_mode = ctx->codec->mode;
	if (dev->devtype->product == CODA_960 &&
	    src_fourcc == V4L2_PIX_FMT_MPEG4)
		ctx->params.codec_mode_aux = CODA_MP4_AUX_MPEG4;
	else
		ctx->params.codec_mode_aux = 0;
	if (src_fourcc == V4L2_PIX_FMT_H264) {
		if (dev->devtype->product == CODA_7541) {
			coda_write(dev, ctx->psbuf.paddr,
					CODA_CMD_DEC_SEQ_PS_BB_START);
			coda_write(dev, (CODA7_PS_BUF_SIZE / 1024),
					CODA_CMD_DEC_SEQ_PS_BB_SIZE);
		}
		if (dev->devtype->product == CODA_960) {
			coda_write(dev, 0, CODA_CMD_DEC_SEQ_X264_MV_EN);
			coda_write(dev, 512, CODA_CMD_DEC_SEQ_SPP_CHUNK_SIZE);
		}
	}
	if (dev->devtype->product != CODA_960) {
		coda_write(dev, 0, CODA_CMD_DEC_SEQ_SRC_SIZE);
	}

	if (coda_command_sync(ctx, CODA_COMMAND_SEQ_INIT)) {
		v4l2_err(&dev->v4l2_dev, "CODA_COMMAND_SEQ_INIT timeout\n");
		coda_write(dev, 0, CODA_REG_BIT_BIT_STREAM_PARAM);
		return -ETIMEDOUT;
	}

	/* Update kfifo out pointer from coda bitstream read pointer */
	coda_kfifo_sync_from_device(ctx);

	coda_write(dev, 0, CODA_REG_BIT_BIT_STREAM_PARAM);

	if (coda_read(dev, CODA_RET_DEC_SEQ_SUCCESS) == 0) {
		v4l2_err(&dev->v4l2_dev,
			"CODA_COMMAND_SEQ_INIT failed, error code = %d\n",
			coda_read(dev, CODA_RET_DEC_SEQ_ERR_REASON));
		return -EAGAIN;
	}

	val = coda_read(dev, CODA_RET_DEC_SEQ_SRC_SIZE);
	if (dev->devtype->product == CODA_DX6) {
		width = (val >> CODADX6_PICWIDTH_OFFSET) & CODADX6_PICWIDTH_MASK;
		height = val & CODADX6_PICHEIGHT_MASK;
	} else {
		width = (val >> CODA7_PICWIDTH_OFFSET) & CODA7_PICWIDTH_MASK;
		height = val & CODA7_PICHEIGHT_MASK;
	}

	if (width > q_data_dst->width || height > q_data_dst->height) {
		v4l2_err(&dev->v4l2_dev, "stream is %dx%d, not %dx%d\n",
			 width, height, q_data_dst->width, q_data_dst->height);
		return -EINVAL;
	}

	width = round_up(width, 16);
	height = round_up(height, 16);

	v4l2_dbg(1, coda_debug, &dev->v4l2_dev, "%s instance %d now: %dx%d\n",
		 __func__, ctx->idx, width, height);

	ctx->num_internal_frames = coda_read(dev, CODA_RET_DEC_SEQ_FRAME_NEED);
	if (ctx->num_internal_frames > CODA_MAX_FRAMEBUFFERS) {
		v4l2_err(&dev->v4l2_dev,
			 "not enough framebuffers to decode (%d < %d)\n",
			 CODA_MAX_FRAMEBUFFERS, ctx->num_internal_frames);
		return -EINVAL;
	}

	if (src_fourcc == V4L2_PIX_FMT_H264) {
		u32 left_right;
		u32 top_bottom;

		left_right = coda_read(dev, CODA_RET_DEC_SEQ_CROP_LEFT_RIGHT);
		top_bottom = coda_read(dev, CODA_RET_DEC_SEQ_CROP_TOP_BOTTOM);

		q_data_dst->rect.left = (left_right >> 10) & 0x3ff;
		q_data_dst->rect.top = (top_bottom >> 10) & 0x3ff;
		q_data_dst->rect.width = width - q_data_dst->rect.left -
					 (left_right & 0x3ff);
		q_data_dst->rect.height = height - q_data_dst->rect.top -
					  (top_bottom & 0x3ff);
	}

	ret = coda_alloc_framebuffers(ctx, q_data_dst, src_fourcc);
	if (ret < 0)
		return ret;

	/* Tell the decoder how many frame buffers we allocated. */
	coda_write(dev, ctx->num_internal_frames, CODA_CMD_SET_FRAME_BUF_NUM);
	coda_write(dev, width, CODA_CMD_SET_FRAME_BUF_STRIDE);

	if (dev->devtype->product != CODA_DX6) {
		/* Set secondary AXI IRAM */
		coda_setup_iram(ctx);

		coda_write(dev, ctx->iram_info.buf_bit_use,
				CODA7_CMD_SET_FRAME_AXI_BIT_ADDR);
		coda_write(dev, ctx->iram_info.buf_ip_ac_dc_use,
				CODA7_CMD_SET_FRAME_AXI_IPACDC_ADDR);
		coda_write(dev, ctx->iram_info.buf_dbk_y_use,
				CODA7_CMD_SET_FRAME_AXI_DBKY_ADDR);
		coda_write(dev, ctx->iram_info.buf_dbk_c_use,
				CODA7_CMD_SET_FRAME_AXI_DBKC_ADDR);
		coda_write(dev, ctx->iram_info.buf_ovl_use,
				CODA7_CMD_SET_FRAME_AXI_OVL_ADDR);
		if (dev->devtype->product == CODA_960)
			coda_write(dev, ctx->iram_info.buf_btp_use,
					CODA9_CMD_SET_FRAME_AXI_BTP_ADDR);
	}

	if (dev->devtype->product == CODA_960) {
		coda_write(dev, -1, CODA9_CMD_SET_FRAME_DELAY);

		coda_write(dev, 0x20262024, CODA9_CMD_SET_FRAME_CACHE_SIZE);
		coda_write(dev, 2 << CODA9_CACHE_PAGEMERGE_OFFSET |
				32 << CODA9_CACHE_LUMA_BUFFER_SIZE_OFFSET |
				8 << CODA9_CACHE_CB_BUFFER_SIZE_OFFSET |
				8 << CODA9_CACHE_CR_BUFFER_SIZE_OFFSET,
				CODA9_CMD_SET_FRAME_CACHE_CONFIG);
	}

	if (src_fourcc == V4L2_PIX_FMT_H264) {
		coda_write(dev, ctx->slicebuf.paddr,
				CODA_CMD_SET_FRAME_SLICE_BB_START);
		coda_write(dev, ctx->slicebuf.size / 1024,
				CODA_CMD_SET_FRAME_SLICE_BB_SIZE);
	}

	if (dev->devtype->product == CODA_7541) {
		int max_mb_x = 1920 / 16;
		int max_mb_y = 1088 / 16;
		int max_mb_num = max_mb_x * max_mb_y;

		coda_write(dev, max_mb_num << 16 | max_mb_x << 8 | max_mb_y,
				CODA7_CMD_SET_FRAME_MAX_DEC_SIZE);
	} else if (dev->devtype->product == CODA_960) {
		int max_mb_x = 1920 / 16;
		int max_mb_y = 1088 / 16;
		int max_mb_num = max_mb_x * max_mb_y;

		coda_write(dev, max_mb_num << 16 | max_mb_x << 8 | max_mb_y,
				CODA9_CMD_SET_FRAME_MAX_DEC_SIZE);
	}

	if (coda_command_sync(ctx, CODA_COMMAND_SET_FRAME_BUF)) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "CODA_COMMAND_SET_FRAME_BUF timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int coda_encode_header(struct coda_ctx *ctx, struct vb2_buffer *buf,
			      int header_code, u8 *header, int *size)
{
	struct coda_dev *dev = ctx->dev;
	size_t bufsize;
	int ret;
	int i;

	if (dev->devtype->product == CODA_960)
		memset(vb2_plane_vaddr(buf, 0), 0, 64);

	coda_write(dev, vb2_dma_contig_plane_dma_addr(buf, 0),
		   CODA_CMD_ENC_HEADER_BB_START);
	bufsize = vb2_plane_size(buf, 0);
	if (dev->devtype->product == CODA_960)
		bufsize /= 1024;
	coda_write(dev, bufsize, CODA_CMD_ENC_HEADER_BB_SIZE);
	coda_write(dev, header_code, CODA_CMD_ENC_HEADER_CODE);
	ret = coda_command_sync(ctx, CODA_COMMAND_ENCODE_HEADER);
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev, "CODA_COMMAND_ENCODE_HEADER timeout\n");
		return ret;
	}

	if (dev->devtype->product == CODA_960) {
		for (i = 63; i > 0; i--)
			if (((char *)vb2_plane_vaddr(buf, 0))[i] != 0)
				break;
		*size = i + 1;
	} else {
		*size = coda_read(dev, CODA_REG_BIT_WR_PTR(ctx->reg_idx)) -
			coda_read(dev, CODA_CMD_ENC_HEADER_BB_START);
	}
	memcpy(header, vb2_plane_vaddr(buf, 0), *size);

	return 0;
}

static int coda_start_encoding(struct coda_ctx *ctx);

static int coda_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct coda_ctx *ctx = vb2_get_drv_priv(q);
	struct v4l2_device *v4l2_dev = &ctx->dev->v4l2_dev;
	struct coda_dev *dev = ctx->dev;
	struct coda_q_data *q_data_src, *q_data_dst;
	u32 dst_fourcc;
	int ret = 0;

	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		if (q_data_src->fourcc == V4L2_PIX_FMT_H264) {
			if (coda_get_bitstream_payload(ctx) < 512)
				return -EINVAL;
		} else {
			if (count < 1)
				return -EINVAL;
		}

		ctx->streamon_out = 1;

		if (coda_format_is_yuv(q_data_src->fourcc))
			ctx->inst_type = CODA_INST_ENCODER;
		else
			ctx->inst_type = CODA_INST_DECODER;
	} else {
		if (count < 1)
			return -EINVAL;

		ctx->streamon_cap = 1;
	}

	/* Don't start the coda unless both queues are on */
	if (!(ctx->streamon_out & ctx->streamon_cap))
		return 0;

	/* Allow decoder device_run with no new buffers queued */
	if (ctx->inst_type == CODA_INST_DECODER)
		v4l2_m2m_set_src_buffered(ctx->fh.m2m_ctx, true);

	ctx->gopcounter = ctx->params.gop_size - 1;
	q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	dst_fourcc = q_data_dst->fourcc;

	ctx->codec = coda_find_codec(ctx->dev, q_data_src->fourcc,
				     q_data_dst->fourcc);
	if (!ctx->codec) {
		v4l2_err(v4l2_dev, "couldn't tell instance type.\n");
		return -EINVAL;
	}

	/* Allocate per-instance buffers */
	ret = coda_alloc_context_buffers(ctx, q_data_src);
	if (ret < 0)
		return ret;

	if (ctx->inst_type == CODA_INST_DECODER) {
		mutex_lock(&dev->coda_mutex);
		ret = coda_start_decoding(ctx);
		mutex_unlock(&dev->coda_mutex);
		if (ret == -EAGAIN)
			return 0;
		else if (ret < 0)
			return ret;
	} else {
		ret = coda_start_encoding(ctx);
	}

	ctx->initialized = 1;
	return ret;
}

static int coda_start_encoding(struct coda_ctx *ctx)
{
	struct coda_dev *dev = ctx->dev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct coda_q_data *q_data_src, *q_data_dst;
	u32 bitstream_buf, bitstream_size;
	struct vb2_buffer *buf;
	int gamma, ret, value;
	u32 dst_fourcc;

	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	dst_fourcc = q_data_dst->fourcc;

	buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	bitstream_buf = vb2_dma_contig_plane_dma_addr(buf, 0);
	bitstream_size = q_data_dst->sizeimage;

	if (!coda_is_initialized(dev)) {
		v4l2_err(v4l2_dev, "coda is not initialized.\n");
		return -EFAULT;
	}

	mutex_lock(&dev->coda_mutex);

	coda_write(dev, ctx->parabuf.paddr, CODA_REG_BIT_PARA_BUF_ADDR);
	coda_write(dev, bitstream_buf, CODA_REG_BIT_RD_PTR(ctx->reg_idx));
	coda_write(dev, bitstream_buf, CODA_REG_BIT_WR_PTR(ctx->reg_idx));
	switch (dev->devtype->product) {
	case CODA_DX6:
		coda_write(dev, CODADX6_STREAM_BUF_DYNALLOC_EN |
			CODADX6_STREAM_BUF_PIC_RESET, CODA_REG_BIT_STREAM_CTRL);
		break;
	case CODA_960:
		coda_write(dev, 0, CODA9_GDI_WPROT_RGN_EN);
		/* fallthrough */
	case CODA_7541:
		coda_write(dev, CODA7_STREAM_BUF_DYNALLOC_EN |
			CODA7_STREAM_BUF_PIC_RESET, CODA_REG_BIT_STREAM_CTRL);
		break;
	}

	value = coda_read(dev, CODA_REG_BIT_FRAME_MEM_CTRL);
	value &= ~(1 << 2 | 0x7 << 9);
	ctx->frame_mem_ctrl = value;
	coda_write(dev, value, CODA_REG_BIT_FRAME_MEM_CTRL);

	if (dev->devtype->product == CODA_DX6) {
		/* Configure the coda */
		coda_write(dev, dev->iram.paddr, CODADX6_REG_BIT_SEARCH_RAM_BASE_ADDR);
	}

	/* Could set rotation here if needed */
	switch (dev->devtype->product) {
	case CODA_DX6:
		value = (q_data_src->width & CODADX6_PICWIDTH_MASK) << CODADX6_PICWIDTH_OFFSET;
		value |= (q_data_src->height & CODADX6_PICHEIGHT_MASK) << CODA_PICHEIGHT_OFFSET;
		break;
	case CODA_7541:
		if (dst_fourcc == V4L2_PIX_FMT_H264) {
			value = (round_up(q_data_src->width, 16) &
				 CODA7_PICWIDTH_MASK) << CODA7_PICWIDTH_OFFSET;
			value |= (round_up(q_data_src->height, 16) &
				  CODA7_PICHEIGHT_MASK) << CODA_PICHEIGHT_OFFSET;
			break;
		}
		/* fallthrough */
	case CODA_960:
		value = (q_data_src->width & CODA7_PICWIDTH_MASK) << CODA7_PICWIDTH_OFFSET;
		value |= (q_data_src->height & CODA7_PICHEIGHT_MASK) << CODA_PICHEIGHT_OFFSET;
	}
	coda_write(dev, value, CODA_CMD_ENC_SEQ_SRC_SIZE);
	coda_write(dev, ctx->params.framerate,
		   CODA_CMD_ENC_SEQ_SRC_F_RATE);

	ctx->params.codec_mode = ctx->codec->mode;
	switch (dst_fourcc) {
	case V4L2_PIX_FMT_MPEG4:
		if (dev->devtype->product == CODA_960)
			coda_write(dev, CODA9_STD_MPEG4, CODA_CMD_ENC_SEQ_COD_STD);
		else
			coda_write(dev, CODA_STD_MPEG4, CODA_CMD_ENC_SEQ_COD_STD);
		coda_write(dev, 0, CODA_CMD_ENC_SEQ_MP4_PARA);
		break;
	case V4L2_PIX_FMT_H264:
		if (dev->devtype->product == CODA_960)
			coda_write(dev, CODA9_STD_H264, CODA_CMD_ENC_SEQ_COD_STD);
		else
			coda_write(dev, CODA_STD_H264, CODA_CMD_ENC_SEQ_COD_STD);
		if (ctx->params.h264_deblk_enabled) {
			value = ((ctx->params.h264_deblk_alpha &
				  CODA_264PARAM_DEBLKFILTEROFFSETALPHA_MASK) <<
				 CODA_264PARAM_DEBLKFILTEROFFSETALPHA_OFFSET) |
				((ctx->params.h264_deblk_beta &
				  CODA_264PARAM_DEBLKFILTEROFFSETBETA_MASK) <<
				 CODA_264PARAM_DEBLKFILTEROFFSETBETA_OFFSET);
		} else {
			value = 1 << CODA_264PARAM_DISABLEDEBLK_OFFSET;
		}
		coda_write(dev, value, CODA_CMD_ENC_SEQ_264_PARA);
		break;
	default:
		v4l2_err(v4l2_dev,
			 "dst format (0x%08x) invalid.\n", dst_fourcc);
		ret = -EINVAL;
		goto out;
	}

	switch (ctx->params.slice_mode) {
	case V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE:
		value = 0;
		break;
	case V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_MB:
		value  = (ctx->params.slice_max_mb & CODA_SLICING_SIZE_MASK) << CODA_SLICING_SIZE_OFFSET;
		value |= (1 & CODA_SLICING_UNIT_MASK) << CODA_SLICING_UNIT_OFFSET;
		value |=  1 & CODA_SLICING_MODE_MASK;
		break;
	case V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_BYTES:
		value  = (ctx->params.slice_max_bits & CODA_SLICING_SIZE_MASK) << CODA_SLICING_SIZE_OFFSET;
		value |= (0 & CODA_SLICING_UNIT_MASK) << CODA_SLICING_UNIT_OFFSET;
		value |=  1 & CODA_SLICING_MODE_MASK;
		break;
	}
	coda_write(dev, value, CODA_CMD_ENC_SEQ_SLICE_MODE);
	value = ctx->params.gop_size & CODA_GOP_SIZE_MASK;
	coda_write(dev, value, CODA_CMD_ENC_SEQ_GOP_SIZE);

	if (ctx->params.bitrate) {
		/* Rate control enabled */
		value = (ctx->params.bitrate & CODA_RATECONTROL_BITRATE_MASK) << CODA_RATECONTROL_BITRATE_OFFSET;
		value |=  1 & CODA_RATECONTROL_ENABLE_MASK;
		if (dev->devtype->product == CODA_960)
			value |= BIT(31); /* disable autoskip */
	} else {
		value = 0;
	}
	coda_write(dev, value, CODA_CMD_ENC_SEQ_RC_PARA);

	coda_write(dev, 0, CODA_CMD_ENC_SEQ_RC_BUF_SIZE);
	coda_write(dev, ctx->params.intra_refresh,
		   CODA_CMD_ENC_SEQ_INTRA_REFRESH);

	coda_write(dev, bitstream_buf, CODA_CMD_ENC_SEQ_BB_START);
	coda_write(dev, bitstream_size / 1024, CODA_CMD_ENC_SEQ_BB_SIZE);


	value = 0;
	if (dev->devtype->product == CODA_960)
		gamma = CODA9_DEFAULT_GAMMA;
	else
		gamma = CODA_DEFAULT_GAMMA;
	if (gamma > 0) {
		coda_write(dev, (gamma & CODA_GAMMA_MASK) << CODA_GAMMA_OFFSET,
			   CODA_CMD_ENC_SEQ_RC_GAMMA);
	}

	if (ctx->params.h264_min_qp || ctx->params.h264_max_qp) {
		coda_write(dev,
			   ctx->params.h264_min_qp << CODA_QPMIN_OFFSET |
			   ctx->params.h264_max_qp << CODA_QPMAX_OFFSET,
			   CODA_CMD_ENC_SEQ_RC_QP_MIN_MAX);
	}
	if (dev->devtype->product == CODA_960) {
		if (ctx->params.h264_max_qp)
			value |= 1 << CODA9_OPTION_RCQPMAX_OFFSET;
		if (CODA_DEFAULT_GAMMA > 0)
			value |= 1 << CODA9_OPTION_GAMMA_OFFSET;
	} else {
		if (CODA_DEFAULT_GAMMA > 0) {
			if (dev->devtype->product == CODA_DX6)
				value |= 1 << CODADX6_OPTION_GAMMA_OFFSET;
			else
				value |= 1 << CODA7_OPTION_GAMMA_OFFSET;
		}
		if (ctx->params.h264_min_qp)
			value |= 1 << CODA7_OPTION_RCQPMIN_OFFSET;
		if (ctx->params.h264_max_qp)
			value |= 1 << CODA7_OPTION_RCQPMAX_OFFSET;
	}
	coda_write(dev, value, CODA_CMD_ENC_SEQ_OPTION);

	coda_write(dev, 0, CODA_CMD_ENC_SEQ_RC_INTERVAL_MODE);

	coda_setup_iram(ctx);

	if (dst_fourcc == V4L2_PIX_FMT_H264) {
		switch (dev->devtype->product) {
		case CODA_DX6:
			value = FMO_SLICE_SAVE_BUF_SIZE << 7;
			coda_write(dev, value, CODADX6_CMD_ENC_SEQ_FMO);
			break;
		case CODA_7541:
			coda_write(dev, ctx->iram_info.search_ram_paddr,
					CODA7_CMD_ENC_SEQ_SEARCH_BASE);
			coda_write(dev, ctx->iram_info.search_ram_size,
					CODA7_CMD_ENC_SEQ_SEARCH_SIZE);
			break;
		case CODA_960:
			coda_write(dev, 0, CODA9_CMD_ENC_SEQ_ME_OPTION);
			coda_write(dev, 0, CODA9_CMD_ENC_SEQ_INTRA_WEIGHT);
		}
	}

	ret = coda_command_sync(ctx, CODA_COMMAND_SEQ_INIT);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "CODA_COMMAND_SEQ_INIT timeout\n");
		goto out;
	}

	if (coda_read(dev, CODA_RET_ENC_SEQ_SUCCESS) == 0) {
		v4l2_err(v4l2_dev, "CODA_COMMAND_SEQ_INIT failed\n");
		ret = -EFAULT;
		goto out;
	}

	if (dev->devtype->product == CODA_960)
		ctx->num_internal_frames = 4;
	else
		ctx->num_internal_frames = 2;
	ret = coda_alloc_framebuffers(ctx, q_data_src, dst_fourcc);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "failed to allocate framebuffers\n");
		goto out;
	}

	coda_write(dev, ctx->num_internal_frames, CODA_CMD_SET_FRAME_BUF_NUM);
	coda_write(dev, q_data_src->bytesperline,
			CODA_CMD_SET_FRAME_BUF_STRIDE);
	if (dev->devtype->product == CODA_7541) {
		coda_write(dev, q_data_src->bytesperline,
				CODA7_CMD_SET_FRAME_SOURCE_BUF_STRIDE);
	}
	if (dev->devtype->product != CODA_DX6) {
		coda_write(dev, ctx->iram_info.buf_bit_use,
				CODA7_CMD_SET_FRAME_AXI_BIT_ADDR);
		coda_write(dev, ctx->iram_info.buf_ip_ac_dc_use,
				CODA7_CMD_SET_FRAME_AXI_IPACDC_ADDR);
		coda_write(dev, ctx->iram_info.buf_dbk_y_use,
				CODA7_CMD_SET_FRAME_AXI_DBKY_ADDR);
		coda_write(dev, ctx->iram_info.buf_dbk_c_use,
				CODA7_CMD_SET_FRAME_AXI_DBKC_ADDR);
		coda_write(dev, ctx->iram_info.buf_ovl_use,
				CODA7_CMD_SET_FRAME_AXI_OVL_ADDR);
		if (dev->devtype->product == CODA_960) {
			coda_write(dev, ctx->iram_info.buf_btp_use,
					CODA9_CMD_SET_FRAME_AXI_BTP_ADDR);

			/* FIXME */
			coda_write(dev, ctx->internal_frames[2].paddr, CODA9_CMD_SET_FRAME_SUBSAMP_A);
			coda_write(dev, ctx->internal_frames[3].paddr, CODA9_CMD_SET_FRAME_SUBSAMP_B);
		}
	}

	ret = coda_command_sync(ctx, CODA_COMMAND_SET_FRAME_BUF);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "CODA_COMMAND_SET_FRAME_BUF timeout\n");
		goto out;
	}

	/* Save stream headers */
	buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	switch (dst_fourcc) {
	case V4L2_PIX_FMT_H264:
		/*
		 * Get SPS in the first frame and copy it to an
		 * intermediate buffer.
		 */
		ret = coda_encode_header(ctx, buf, CODA_HEADER_H264_SPS,
					 &ctx->vpu_header[0][0],
					 &ctx->vpu_header_size[0]);
		if (ret < 0)
			goto out;

		/*
		 * Get PPS in the first frame and copy it to an
		 * intermediate buffer.
		 */
		ret = coda_encode_header(ctx, buf, CODA_HEADER_H264_PPS,
					 &ctx->vpu_header[1][0],
					 &ctx->vpu_header_size[1]);
		if (ret < 0)
			goto out;

		/*
		 * Length of H.264 headers is variable and thus it might not be
		 * aligned for the coda to append the encoded frame. In that is
		 * the case a filler NAL must be added to header 2.
		 */
		ctx->vpu_header_size[2] = coda_h264_padding(
					(ctx->vpu_header_size[0] +
					 ctx->vpu_header_size[1]),
					 ctx->vpu_header[2]);
		break;
	case V4L2_PIX_FMT_MPEG4:
		/*
		 * Get VOS in the first frame and copy it to an
		 * intermediate buffer
		 */
		ret = coda_encode_header(ctx, buf, CODA_HEADER_MP4V_VOS,
					 &ctx->vpu_header[0][0],
					 &ctx->vpu_header_size[0]);
		if (ret < 0)
			goto out;

		ret = coda_encode_header(ctx, buf, CODA_HEADER_MP4V_VIS,
					 &ctx->vpu_header[1][0],
					 &ctx->vpu_header_size[1]);
		if (ret < 0)
			goto out;

		ret = coda_encode_header(ctx, buf, CODA_HEADER_MP4V_VOL,
					 &ctx->vpu_header[2][0],
					 &ctx->vpu_header_size[2]);
		if (ret < 0)
			goto out;
		break;
	default:
		/* No more formats need to save headers at the moment */
		break;
	}

out:
	mutex_unlock(&dev->coda_mutex);
	return ret;
}

static void coda_stop_streaming(struct vb2_queue *q)
{
	struct coda_ctx *ctx = vb2_get_drv_priv(q);
	struct coda_dev *dev = ctx->dev;

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		v4l2_dbg(1, coda_debug, &dev->v4l2_dev,
			 "%s: output\n", __func__);
		ctx->streamon_out = 0;

		if (ctx->inst_type == CODA_INST_DECODER &&
		    coda_isbusy(dev) && ctx->idx == coda_read(dev, CODA_REG_BIT_RUN_INDEX)) {
			/* if this decoder instance is running, set the stream end flag */
			if (dev->devtype->product == CODA_960) {
				u32 val = coda_read(dev, CODA_REG_BIT_BIT_STREAM_PARAM);

				val |= CODA_BIT_STREAM_END_FLAG;
				coda_write(dev, val, CODA_REG_BIT_BIT_STREAM_PARAM);
				ctx->bit_stream_param = val;
			}
		}
		ctx->bit_stream_param |= CODA_BIT_STREAM_END_FLAG;

		ctx->isequence = 0;
	} else {
		v4l2_dbg(1, coda_debug, &dev->v4l2_dev,
			 "%s: capture\n", __func__);
		ctx->streamon_cap = 0;

		ctx->osequence = 0;
		ctx->sequence_offset = 0;
	}

	if (!ctx->streamon_out && !ctx->streamon_cap) {
		struct coda_timestamp *ts;

		while (!list_empty(&ctx->timestamp_list)) {
			ts = list_first_entry(&ctx->timestamp_list,
					      struct coda_timestamp, list);
			list_del(&ts->list);
			kfree(ts);
		}
		kfifo_init(&ctx->bitstream_fifo,
			ctx->bitstream.vaddr, ctx->bitstream.size);
		ctx->runcounter = 0;
	}
}

static struct vb2_ops coda_qops = {
	.queue_setup		= coda_queue_setup,
	.buf_prepare		= coda_buf_prepare,
	.buf_queue		= coda_buf_queue,
	.start_streaming	= coda_start_streaming,
	.stop_streaming		= coda_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static int coda_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct coda_ctx *ctx =
			container_of(ctrl->handler, struct coda_ctx, ctrls);

	v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
		 "s_ctrl: id = %d, val = %d\n", ctrl->id, ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			ctx->params.rot_mode |= CODA_MIR_HOR;
		else
			ctx->params.rot_mode &= ~CODA_MIR_HOR;
		break;
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			ctx->params.rot_mode |= CODA_MIR_VER;
		else
			ctx->params.rot_mode &= ~CODA_MIR_VER;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		ctx->params.bitrate = ctrl->val / 1000;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		ctx->params.gop_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:
		ctx->params.h264_intra_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP:
		ctx->params.h264_inter_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
		ctx->params.h264_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
		ctx->params.h264_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA:
		ctx->params.h264_deblk_alpha = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA:
		ctx->params.h264_deblk_beta = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:
		ctx->params.h264_deblk_enabled = (ctrl->val ==
				V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_ENABLED);
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP:
		ctx->params.mpeg4_intra_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP:
		ctx->params.mpeg4_inter_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:
		ctx->params.slice_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB:
		ctx->params.slice_max_mb = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES:
		ctx->params.slice_max_bits = ctrl->val * 8;
		break;
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
		break;
	case V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB:
		ctx->params.intra_refresh = ctrl->val;
		break;
	default:
		v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			"Invalid control, id=%d, val=%d\n",
			ctrl->id, ctrl->val);
		return -EINVAL;
	}

	return 0;
}

static struct v4l2_ctrl_ops coda_ctrl_ops = {
	.s_ctrl = coda_s_ctrl,
};

static int coda_ctrls_setup(struct coda_ctx *ctx)
{
	v4l2_ctrl_handler_init(&ctx->ctrls, 9);

	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_BITRATE, 0, 32767000, 1, 0);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_GOP_SIZE, 1, 60, 1, 16);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, 0, 51, 1, 25);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, 0, 51, 1, 25);
	if (ctx->dev->devtype->product != CODA_960) {
		v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_H264_MIN_QP, 0, 51, 1, 12);
	}
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_MAX_QP, 0, 51, 1, 51);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA, 0, 15, 1, 0);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA, 0, 15, 1, 0);
	v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE,
		V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED, 0x0,
		V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_ENABLED);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP, 1, 31, 1, 2);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP, 1, 31, 1, 2);
	v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE,
		V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_BYTES, 0x0,
		V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB, 1, 0x3fffffff, 1, 1);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES, 1, 0x3fffffff, 1, 500);
	v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_HEADER_MODE,
		V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		(1 << V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE),
		V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB, 0, 1920 * 1088 / 256, 1, 0);

	if (ctx->ctrls.error) {
		v4l2_err(&ctx->dev->v4l2_dev, "control initialization error (%d)",
			ctx->ctrls.error);
		return -EINVAL;
	}

	return v4l2_ctrl_handler_setup(&ctx->ctrls);
}

static int coda_queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct coda_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_DMABUF | VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &coda_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->dev->dev_mutex;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_DMABUF | VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &coda_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->dev->dev_mutex;

	return vb2_queue_init(dst_vq);
}

static int coda_next_free_instance(struct coda_dev *dev)
{
	int idx = ffz(dev->instance_mask);

	if ((idx < 0) ||
	    (dev->devtype->product == CODA_DX6 && idx > CODADX6_MAX_INSTANCES))
		return -EBUSY;

	return idx;
}

static int coda_open(struct file *file)
{
	struct coda_dev *dev = video_drvdata(file);
	struct coda_ctx *ctx = NULL;
	char *name;
	int ret;
	int idx;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	idx = coda_next_free_instance(dev);
	if (idx < 0) {
		ret = idx;
		goto err_coda_max;
	}
	set_bit(idx, &dev->instance_mask);

	name = kasprintf(GFP_KERNEL, "context%d", idx);
	ctx->debugfs_entry = debugfs_create_dir(name, dev->debugfs_root);
	kfree(name);

	init_completion(&ctx->completion);
	INIT_WORK(&ctx->pic_run_work, coda_pic_run_work);
	INIT_WORK(&ctx->seq_end_work, coda_seq_end_work);
	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);
	ctx->dev = dev;
	ctx->idx = idx;
	switch (dev->devtype->product) {
	case CODA_7541:
	case CODA_960:
		ctx->reg_idx = 0;
		break;
	default:
		ctx->reg_idx = idx;
	}

	/* Power up and upload firmware if necessary */
	ret = pm_runtime_get_sync(&dev->plat_dev->dev);
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev, "failed to power up: %d\n", ret);
		goto err_pm_get;
	}

	ret = clk_prepare_enable(dev->clk_per);
	if (ret)
		goto err_clk_per;

	ret = clk_prepare_enable(dev->clk_ahb);
	if (ret)
		goto err_clk_ahb;

	set_default_params(ctx);
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx,
					 &coda_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);

		v4l2_err(&dev->v4l2_dev, "%s return error (%d)\n",
			 __func__, ret);
		goto err_ctx_init;
	}

	ret = coda_ctrls_setup(ctx);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "failed to setup coda controls\n");
		goto err_ctrls_setup;
	}

	ctx->fh.ctrl_handler = &ctx->ctrls;

	ret = coda_alloc_context_buf(ctx, &ctx->parabuf, CODA_PARA_BUF_SIZE,
				     "parabuf");
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev, "failed to allocate parabuf");
		goto err_dma_alloc;
	}

	ctx->bitstream.size = CODA_MAX_FRAME_SIZE;
	ctx->bitstream.vaddr = dma_alloc_writecombine(&dev->plat_dev->dev,
			ctx->bitstream.size, &ctx->bitstream.paddr, GFP_KERNEL);
	if (!ctx->bitstream.vaddr) {
		v4l2_err(&dev->v4l2_dev, "failed to allocate bitstream ringbuffer");
		ret = -ENOMEM;
		goto err_dma_writecombine;
	}
	kfifo_init(&ctx->bitstream_fifo,
		ctx->bitstream.vaddr, ctx->bitstream.size);
	mutex_init(&ctx->bitstream_mutex);
	mutex_init(&ctx->buffer_mutex);
	INIT_LIST_HEAD(&ctx->timestamp_list);

	coda_lock(ctx);
	list_add(&ctx->list, &dev->instances);
	coda_unlock(ctx);

	v4l2_dbg(1, coda_debug, &dev->v4l2_dev, "Created instance %d (%p)\n",
		 ctx->idx, ctx);

	return 0;

err_dma_writecombine:
	coda_free_context_buffers(ctx);
	if (ctx->dev->devtype->product == CODA_DX6)
		coda_free_aux_buf(dev, &ctx->workbuf);
	coda_free_aux_buf(dev, &ctx->parabuf);
err_dma_alloc:
	v4l2_ctrl_handler_free(&ctx->ctrls);
err_ctrls_setup:
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
err_ctx_init:
	clk_disable_unprepare(dev->clk_ahb);
err_clk_ahb:
	clk_disable_unprepare(dev->clk_per);
err_clk_per:
	pm_runtime_put_sync(&dev->plat_dev->dev);
err_pm_get:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	clear_bit(ctx->idx, &dev->instance_mask);
err_coda_max:
	kfree(ctx);
	return ret;
}

static int coda_release(struct file *file)
{
	struct coda_dev *dev = video_drvdata(file);
	struct coda_ctx *ctx = fh_to_ctx(file->private_data);

	v4l2_dbg(1, coda_debug, &dev->v4l2_dev, "Releasing instance %p\n",
		 ctx);

	debugfs_remove_recursive(ctx->debugfs_entry);

	/* If this instance is running, call .job_abort and wait for it to end */
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);

	/* In case the instance was not running, we still need to call SEQ_END */
	if (ctx->initialized) {
		queue_work(dev->workqueue, &ctx->seq_end_work);
		flush_work(&ctx->seq_end_work);
	}

	coda_free_framebuffers(ctx);

	coda_lock(ctx);
	list_del(&ctx->list);
	coda_unlock(ctx);

	dma_free_writecombine(&dev->plat_dev->dev, ctx->bitstream.size,
		ctx->bitstream.vaddr, ctx->bitstream.paddr);
	coda_free_context_buffers(ctx);
	if (ctx->dev->devtype->product == CODA_DX6)
		coda_free_aux_buf(dev, &ctx->workbuf);

	coda_free_aux_buf(dev, &ctx->parabuf);
	v4l2_ctrl_handler_free(&ctx->ctrls);
	clk_disable_unprepare(dev->clk_ahb);
	clk_disable_unprepare(dev->clk_per);
	pm_runtime_put_sync(&dev->plat_dev->dev);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	clear_bit(ctx->idx, &dev->instance_mask);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations coda_fops = {
	.owner		= THIS_MODULE,
	.open		= coda_open,
	.release	= coda_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static void coda_finish_decode(struct coda_ctx *ctx)
{
	struct coda_dev *dev = ctx->dev;
	struct coda_q_data *q_data_src;
	struct coda_q_data *q_data_dst;
	struct vb2_buffer *dst_buf;
	struct coda_timestamp *ts;
	int width, height;
	int decoded_idx;
	int display_idx;
	u32 src_fourcc;
	int success;
	u32 err_mb;
	u32 val;

	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	/* Update kfifo out pointer from coda bitstream read pointer */
	coda_kfifo_sync_from_device(ctx);

	/*
	 * in stream-end mode, the read pointer can overshoot the write pointer
	 * by up to 512 bytes
	 */
	if (ctx->bit_stream_param & CODA_BIT_STREAM_END_FLAG) {
		if (coda_get_bitstream_payload(ctx) >= 0x100000 - 512)
			kfifo_init(&ctx->bitstream_fifo,
				ctx->bitstream.vaddr, ctx->bitstream.size);
	}

	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	src_fourcc = q_data_src->fourcc;

	val = coda_read(dev, CODA_RET_DEC_PIC_SUCCESS);
	if (val != 1)
		pr_err("DEC_PIC_SUCCESS = %d\n", val);

	success = val & 0x1;
	if (!success)
		v4l2_err(&dev->v4l2_dev, "decode failed\n");

	if (src_fourcc == V4L2_PIX_FMT_H264) {
		if (val & (1 << 3))
			v4l2_err(&dev->v4l2_dev,
				 "insufficient PS buffer space (%d bytes)\n",
				 ctx->psbuf.size);
		if (val & (1 << 2))
			v4l2_err(&dev->v4l2_dev,
				 "insufficient slice buffer space (%d bytes)\n",
				 ctx->slicebuf.size);
	}

	val = coda_read(dev, CODA_RET_DEC_PIC_SIZE);
	width = (val >> 16) & 0xffff;
	height = val & 0xffff;

	q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);

	/* frame crop information */
	if (src_fourcc == V4L2_PIX_FMT_H264) {
		u32 left_right;
		u32 top_bottom;

		left_right = coda_read(dev, CODA_RET_DEC_PIC_CROP_LEFT_RIGHT);
		top_bottom = coda_read(dev, CODA_RET_DEC_PIC_CROP_TOP_BOTTOM);

		if (left_right == 0xffffffff && top_bottom == 0xffffffff) {
			/* Keep current crop information */
		} else {
			struct v4l2_rect *rect = &q_data_dst->rect;

			rect->left = left_right >> 16 & 0xffff;
			rect->top = top_bottom >> 16 & 0xffff;
			rect->width = width - rect->left -
				      (left_right & 0xffff);
			rect->height = height - rect->top -
				       (top_bottom & 0xffff);
		}
	} else {
		/* no cropping */
	}

	err_mb = coda_read(dev, CODA_RET_DEC_PIC_ERR_MB);
	if (err_mb > 0)
		v4l2_err(&dev->v4l2_dev,
			 "errors in %d macroblocks\n", err_mb);

	if (dev->devtype->product == CODA_7541) {
		val = coda_read(dev, CODA_RET_DEC_PIC_OPTION);
		if (val == 0) {
			/* not enough bitstream data */
			v4l2_dbg(1, coda_debug, &dev->v4l2_dev,
				 "prescan failed: %d\n", val);
			ctx->hold = true;
			return;
		}
	}

	ctx->frm_dis_flg = coda_read(dev, CODA_REG_BIT_FRM_DIS_FLG(ctx->reg_idx));

	/*
	 * The previous display frame was copied out by the rotator,
	 * now it can be overwritten again
	 */
	if (ctx->display_idx >= 0 &&
	    ctx->display_idx < ctx->num_internal_frames) {
		ctx->frm_dis_flg &= ~(1 << ctx->display_idx);
		coda_write(dev, ctx->frm_dis_flg,
				CODA_REG_BIT_FRM_DIS_FLG(ctx->reg_idx));
	}

	/*
	 * The index of the last decoded frame, not necessarily in
	 * display order, and the index of the next display frame.
	 * The latter could have been decoded in a previous run.
	 */
	decoded_idx = coda_read(dev, CODA_RET_DEC_PIC_CUR_IDX);
	display_idx = coda_read(dev, CODA_RET_DEC_PIC_FRAME_IDX);

	if (decoded_idx == -1) {
		/* no frame was decoded, but we might have a display frame */
		if (display_idx >= 0 && display_idx < ctx->num_internal_frames)
			ctx->sequence_offset++;
		else if (ctx->display_idx < 0)
			ctx->hold = true;
	} else if (decoded_idx == -2) {
		/* no frame was decoded, we still return the remaining buffers */
	} else if (decoded_idx < 0 || decoded_idx >= ctx->num_internal_frames) {
		v4l2_err(&dev->v4l2_dev,
			 "decoded frame index out of range: %d\n", decoded_idx);
	} else {
		ts = list_first_entry(&ctx->timestamp_list,
				      struct coda_timestamp, list);
		list_del(&ts->list);
		val = coda_read(dev, CODA_RET_DEC_PIC_FRAME_NUM) - 1;
		val -= ctx->sequence_offset;
		if (val != (ts->sequence & 0xffff)) {
			v4l2_err(&dev->v4l2_dev,
				 "sequence number mismatch (%d(%d) != %d)\n",
				 val, ctx->sequence_offset, ts->sequence);
		}
		ctx->frame_timestamps[decoded_idx] = *ts;
		kfree(ts);

		val = coda_read(dev, CODA_RET_DEC_PIC_TYPE) & 0x7;
		if (val == 0)
			ctx->frame_types[decoded_idx] = V4L2_BUF_FLAG_KEYFRAME;
		else if (val == 1)
			ctx->frame_types[decoded_idx] = V4L2_BUF_FLAG_PFRAME;
		else
			ctx->frame_types[decoded_idx] = V4L2_BUF_FLAG_BFRAME;

		ctx->frame_errors[decoded_idx] = err_mb;
	}

	if (display_idx == -1) {
		/*
		 * no more frames to be decoded, but there could still
		 * be rotator output to dequeue
		 */
		ctx->hold = true;
	} else if (display_idx == -3) {
		/* possibly prescan failure */
	} else if (display_idx < 0 || display_idx >= ctx->num_internal_frames) {
		v4l2_err(&dev->v4l2_dev,
			 "presentation frame index out of range: %d\n",
			 display_idx);
	}

	/* If a frame was copied out, return it */
	if (ctx->display_idx >= 0 &&
	    ctx->display_idx < ctx->num_internal_frames) {
		dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		dst_buf->v4l2_buf.sequence = ctx->osequence++;

		dst_buf->v4l2_buf.flags &= ~(V4L2_BUF_FLAG_KEYFRAME |
					     V4L2_BUF_FLAG_PFRAME |
					     V4L2_BUF_FLAG_BFRAME);
		dst_buf->v4l2_buf.flags |= ctx->frame_types[ctx->display_idx];
		ts = &ctx->frame_timestamps[ctx->display_idx];
		dst_buf->v4l2_buf.timecode = ts->timecode;
		dst_buf->v4l2_buf.timestamp = ts->timestamp;

		vb2_set_plane_payload(dst_buf, 0, width * height * 3 / 2);

		v4l2_m2m_buf_done(dst_buf, ctx->frame_errors[display_idx] ?
				  VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);

		v4l2_dbg(1, coda_debug, &dev->v4l2_dev,
			"job finished: decoding frame (%d) (%s)\n",
			dst_buf->v4l2_buf.sequence,
			(dst_buf->v4l2_buf.flags & V4L2_BUF_FLAG_KEYFRAME) ?
			"KEYFRAME" : "PFRAME");
	} else {
		v4l2_dbg(1, coda_debug, &dev->v4l2_dev,
			"job finished: no frame decoded\n");
	}

	/* The rotator will copy the current display frame next time */
	ctx->display_idx = display_idx;
}

static void coda_finish_encode(struct coda_ctx *ctx)
{
	struct vb2_buffer *src_buf, *dst_buf;
	struct coda_dev *dev = ctx->dev;
	u32 wr_ptr, start_ptr;

	src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	/* Get results from the coda */
	start_ptr = coda_read(dev, CODA_CMD_ENC_PIC_BB_START);
	wr_ptr = coda_read(dev, CODA_REG_BIT_WR_PTR(ctx->reg_idx));

	/* Calculate bytesused field */
	if (dst_buf->v4l2_buf.sequence == 0) {
		vb2_set_plane_payload(dst_buf, 0, wr_ptr - start_ptr +
					ctx->vpu_header_size[0] +
					ctx->vpu_header_size[1] +
					ctx->vpu_header_size[2]);
	} else {
		vb2_set_plane_payload(dst_buf, 0, wr_ptr - start_ptr);
	}

	v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev, "frame size = %u\n",
		 wr_ptr - start_ptr);

	coda_read(dev, CODA_RET_ENC_PIC_SLICE_NUM);
	coda_read(dev, CODA_RET_ENC_PIC_FLAG);

	if (coda_read(dev, CODA_RET_ENC_PIC_TYPE) == 0) {
		dst_buf->v4l2_buf.flags |= V4L2_BUF_FLAG_KEYFRAME;
		dst_buf->v4l2_buf.flags &= ~V4L2_BUF_FLAG_PFRAME;
	} else {
		dst_buf->v4l2_buf.flags |= V4L2_BUF_FLAG_PFRAME;
		dst_buf->v4l2_buf.flags &= ~V4L2_BUF_FLAG_KEYFRAME;
	}

	dst_buf->v4l2_buf.timestamp = src_buf->v4l2_buf.timestamp;
	dst_buf->v4l2_buf.flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	dst_buf->v4l2_buf.flags |=
		src_buf->v4l2_buf.flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	dst_buf->v4l2_buf.timecode = src_buf->v4l2_buf.timecode;

	v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);

	dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);

	ctx->gopcounter--;
	if (ctx->gopcounter < 0)
		ctx->gopcounter = ctx->params.gop_size - 1;

	v4l2_dbg(1, coda_debug, &dev->v4l2_dev,
		"job finished: encoding frame (%d) (%s)\n",
		dst_buf->v4l2_buf.sequence,
		(dst_buf->v4l2_buf.flags & V4L2_BUF_FLAG_KEYFRAME) ?
		"KEYFRAME" : "PFRAME");
}

static irqreturn_t coda_irq_handler(int irq, void *data)
{
	struct coda_dev *dev = data;
	struct coda_ctx *ctx;

	/* read status register to attend the IRQ */
	coda_read(dev, CODA_REG_BIT_INT_STATUS);
	coda_write(dev, CODA_REG_BIT_INT_CLEAR_SET,
		      CODA_REG_BIT_INT_CLEAR);

	ctx = v4l2_m2m_get_curr_priv(dev->m2m_dev);
	if (ctx == NULL) {
		v4l2_err(&dev->v4l2_dev, "Instance released before the end of transaction\n");
		mutex_unlock(&dev->coda_mutex);
		return IRQ_HANDLED;
	}

	if (ctx->aborting) {
		v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			 "task has been aborted\n");
	}

	if (coda_isbusy(ctx->dev)) {
		v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			 "coda is still busy!!!!\n");
		return IRQ_NONE;
	}

	complete(&ctx->completion);

	return IRQ_HANDLED;
}

static u32 coda_supported_firmwares[] = {
	CODA_FIRMWARE_VERNUM(CODA_DX6, 2, 2, 5),
	CODA_FIRMWARE_VERNUM(CODA_7541, 1, 4, 50),
	CODA_FIRMWARE_VERNUM(CODA_960, 2, 1, 5),
};

static bool coda_firmware_supported(u32 vernum)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coda_supported_firmwares); i++)
		if (vernum == coda_supported_firmwares[i])
			return true;
	return false;
}

static int coda_hw_init(struct coda_dev *dev)
{
	u32 data;
	u16 *p;
	int i, ret;

	ret = clk_prepare_enable(dev->clk_per);
	if (ret)
		goto err_clk_per;

	ret = clk_prepare_enable(dev->clk_ahb);
	if (ret)
		goto err_clk_ahb;

	if (dev->rstc)
		reset_control_reset(dev->rstc);

	/*
	 * Copy the first CODA_ISRAM_SIZE in the internal SRAM.
	 * The 16-bit chars in the code buffer are in memory access
	 * order, re-sort them to CODA order for register download.
	 * Data in this SRAM survives a reboot.
	 */
	p = (u16 *)dev->codebuf.vaddr;
	if (dev->devtype->product == CODA_DX6) {
		for (i = 0; i < (CODA_ISRAM_SIZE / 2); i++)  {
			data = CODA_DOWN_ADDRESS_SET(i) |
				CODA_DOWN_DATA_SET(p[i ^ 1]);
			coda_write(dev, data, CODA_REG_BIT_CODE_DOWN);
		}
	} else {
		for (i = 0; i < (CODA_ISRAM_SIZE / 2); i++) {
			data = CODA_DOWN_ADDRESS_SET(i) |
				CODA_DOWN_DATA_SET(p[round_down(i, 4) +
							3 - (i % 4)]);
			coda_write(dev, data, CODA_REG_BIT_CODE_DOWN);
		}
	}

	/* Clear registers */
	for (i = 0; i < 64; i++)
		coda_write(dev, 0, CODA_REG_BIT_CODE_BUF_ADDR + i * 4);

	/* Tell the BIT where to find everything it needs */
	if (dev->devtype->product == CODA_960 ||
	    dev->devtype->product == CODA_7541) {
		coda_write(dev, dev->tempbuf.paddr,
				CODA_REG_BIT_TEMP_BUF_ADDR);
		coda_write(dev, 0, CODA_REG_BIT_BIT_STREAM_PARAM);
	} else {
		coda_write(dev, dev->workbuf.paddr,
			      CODA_REG_BIT_WORK_BUF_ADDR);
	}
	coda_write(dev, dev->codebuf.paddr,
		      CODA_REG_BIT_CODE_BUF_ADDR);
	coda_write(dev, 0, CODA_REG_BIT_CODE_RUN);

	/* Set default values */
	switch (dev->devtype->product) {
	case CODA_DX6:
		coda_write(dev, CODADX6_STREAM_BUF_PIC_FLUSH, CODA_REG_BIT_STREAM_CTRL);
		break;
	default:
		coda_write(dev, CODA7_STREAM_BUF_PIC_FLUSH, CODA_REG_BIT_STREAM_CTRL);
	}
	if (dev->devtype->product == CODA_960)
		coda_write(dev, 1 << 12, CODA_REG_BIT_FRAME_MEM_CTRL);
	else
		coda_write(dev, 0, CODA_REG_BIT_FRAME_MEM_CTRL);

	if (dev->devtype->product != CODA_DX6)
		coda_write(dev, 0, CODA7_REG_BIT_AXI_SRAM_USE);

	coda_write(dev, CODA_INT_INTERRUPT_ENABLE,
		      CODA_REG_BIT_INT_ENABLE);

	/* Reset VPU and start processor */
	data = coda_read(dev, CODA_REG_BIT_CODE_RESET);
	data |= CODA_REG_RESET_ENABLE;
	coda_write(dev, data, CODA_REG_BIT_CODE_RESET);
	udelay(10);
	data &= ~CODA_REG_RESET_ENABLE;
	coda_write(dev, data, CODA_REG_BIT_CODE_RESET);
	coda_write(dev, CODA_REG_RUN_ENABLE, CODA_REG_BIT_CODE_RUN);

	clk_disable_unprepare(dev->clk_ahb);
	clk_disable_unprepare(dev->clk_per);

	return 0;

err_clk_ahb:
	clk_disable_unprepare(dev->clk_per);
err_clk_per:
	return ret;
}

static int coda_check_firmware(struct coda_dev *dev)
{
	u16 product, major, minor, release;
	u32 data;
	int ret;

	ret = clk_prepare_enable(dev->clk_per);
	if (ret)
		goto err_clk_per;

	ret = clk_prepare_enable(dev->clk_ahb);
	if (ret)
		goto err_clk_ahb;

	coda_write(dev, 0, CODA_CMD_FIRMWARE_VERNUM);
	coda_write(dev, CODA_REG_BIT_BUSY_FLAG, CODA_REG_BIT_BUSY);
	coda_write(dev, 0, CODA_REG_BIT_RUN_INDEX);
	coda_write(dev, 0, CODA_REG_BIT_RUN_COD_STD);
	coda_write(dev, CODA_COMMAND_FIRMWARE_GET, CODA_REG_BIT_RUN_COMMAND);
	if (coda_wait_timeout(dev)) {
		v4l2_err(&dev->v4l2_dev, "firmware get command error\n");
		ret = -EIO;
		goto err_run_cmd;
	}

	if (dev->devtype->product == CODA_960) {
		data = coda_read(dev, CODA9_CMD_FIRMWARE_CODE_REV);
		v4l2_info(&dev->v4l2_dev, "Firmware code revision: %d\n",
			  data);
	}

	/* Check we are compatible with the loaded firmware */
	data = coda_read(dev, CODA_CMD_FIRMWARE_VERNUM);
	product = CODA_FIRMWARE_PRODUCT(data);
	major = CODA_FIRMWARE_MAJOR(data);
	minor = CODA_FIRMWARE_MINOR(data);
	release = CODA_FIRMWARE_RELEASE(data);

	clk_disable_unprepare(dev->clk_per);
	clk_disable_unprepare(dev->clk_ahb);

	if (product != dev->devtype->product) {
		v4l2_err(&dev->v4l2_dev, "Wrong firmware. Hw: %s, Fw: %s,"
			 " Version: %u.%u.%u\n",
			 coda_product_name(dev->devtype->product),
			 coda_product_name(product), major, minor, release);
		return -EINVAL;
	}

	v4l2_info(&dev->v4l2_dev, "Initialized %s.\n",
		  coda_product_name(product));

	if (coda_firmware_supported(data)) {
		v4l2_info(&dev->v4l2_dev, "Firmware version: %u.%u.%u\n",
			  major, minor, release);
	} else {
		v4l2_warn(&dev->v4l2_dev, "Unsupported firmware version: "
			  "%u.%u.%u\n", major, minor, release);
	}

	return 0;

err_run_cmd:
	clk_disable_unprepare(dev->clk_ahb);
err_clk_ahb:
	clk_disable_unprepare(dev->clk_per);
err_clk_per:
	return ret;
}

static void coda_fw_callback(const struct firmware *fw, void *context)
{
	struct coda_dev *dev = context;
	struct platform_device *pdev = dev->plat_dev;
	int ret;

	if (!fw) {
		v4l2_err(&dev->v4l2_dev, "firmware request failed\n");
		return;
	}

	/* allocate auxiliary per-device code buffer for the BIT processor */
	ret = coda_alloc_aux_buf(dev, &dev->codebuf, fw->size, "codebuf",
				 dev->debugfs_root);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to allocate code buffer\n");
		return;
	}

	/* Copy the whole firmware image to the code buffer */
	memcpy(dev->codebuf.vaddr, fw->data, fw->size);
	release_firmware(fw);

	if (pm_runtime_enabled(&pdev->dev) && pdev->dev.pm_domain) {
		/*
		 * Enabling power temporarily will cause coda_hw_init to be
		 * called via coda_runtime_resume by the pm domain.
		 */
		ret = pm_runtime_get_sync(&dev->plat_dev->dev);
		if (ret < 0) {
			v4l2_err(&dev->v4l2_dev, "failed to power on: %d\n",
				 ret);
			return;
		}

		ret = coda_check_firmware(dev);
		if (ret < 0)
			return;

		pm_runtime_put_sync(&dev->plat_dev->dev);
	} else {
		/*
		 * If runtime pm is disabled or pm_domain is not set,
		 * initialize once manually.
		 */
		ret = coda_hw_init(dev);
		if (ret < 0) {
			v4l2_err(&dev->v4l2_dev, "HW initialization failed\n");
			return;
		}

		ret = coda_check_firmware(dev);
		if (ret < 0)
			return;
	}

	dev->vfd.fops	= &coda_fops,
	dev->vfd.ioctl_ops	= &coda_ioctl_ops;
	dev->vfd.release	= video_device_release_empty,
	dev->vfd.lock	= &dev->dev_mutex;
	dev->vfd.v4l2_dev	= &dev->v4l2_dev;
	dev->vfd.vfl_dir	= VFL_DIR_M2M;
	snprintf(dev->vfd.name, sizeof(dev->vfd.name), "%s", CODA_NAME);
	video_set_drvdata(&dev->vfd, dev);

	dev->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(dev->alloc_ctx)) {
		v4l2_err(&dev->v4l2_dev, "Failed to alloc vb2 context\n");
		return;
	}

	dev->m2m_dev = v4l2_m2m_init(&coda_m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem device\n");
		goto rel_ctx;
	}

	ret = video_register_device(&dev->vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device\n");
		goto rel_m2m;
	}
	v4l2_info(&dev->v4l2_dev, "codec registered as /dev/video%d\n",
		  dev->vfd.num);

	return;

rel_m2m:
	v4l2_m2m_release(dev->m2m_dev);
rel_ctx:
	vb2_dma_contig_cleanup_ctx(dev->alloc_ctx);
}

static int coda_firmware_request(struct coda_dev *dev)
{
	char *fw = dev->devtype->firmware;

	dev_dbg(&dev->plat_dev->dev, "requesting firmware '%s' for %s\n", fw,
		coda_product_name(dev->devtype->product));

	return request_firmware_nowait(THIS_MODULE, true,
		fw, &dev->plat_dev->dev, GFP_KERNEL, dev, coda_fw_callback);
}

enum coda_platform {
	CODA_IMX27,
	CODA_IMX53,
	CODA_IMX6Q,
	CODA_IMX6DL,
};

static const struct coda_devtype coda_devdata[] = {
	[CODA_IMX27] = {
		.firmware     = "v4l-codadx6-imx27.bin",
		.product      = CODA_DX6,
		.codecs       = codadx6_codecs,
		.num_codecs   = ARRAY_SIZE(codadx6_codecs),
		.workbuf_size = 288 * 1024 + FMO_SLICE_SAVE_BUF_SIZE * 8 * 1024,
		.iram_size    = 0xb000,
	},
	[CODA_IMX53] = {
		.firmware     = "v4l-coda7541-imx53.bin",
		.product      = CODA_7541,
		.codecs       = coda7_codecs,
		.num_codecs   = ARRAY_SIZE(coda7_codecs),
		.workbuf_size = 128 * 1024,
		.tempbuf_size = 304 * 1024,
		.iram_size    = 0x14000,
	},
	[CODA_IMX6Q] = {
		.firmware     = "v4l-coda960-imx6q.bin",
		.product      = CODA_960,
		.codecs       = coda9_codecs,
		.num_codecs   = ARRAY_SIZE(coda9_codecs),
		.workbuf_size = 80 * 1024,
		.tempbuf_size = 204 * 1024,
		.iram_size    = 0x21000,
	},
	[CODA_IMX6DL] = {
		.firmware     = "v4l-coda960-imx6dl.bin",
		.product      = CODA_960,
		.codecs       = coda9_codecs,
		.num_codecs   = ARRAY_SIZE(coda9_codecs),
		.workbuf_size = 80 * 1024,
		.tempbuf_size = 204 * 1024,
		.iram_size    = 0x20000,
	},
};

static struct platform_device_id coda_platform_ids[] = {
	{ .name = "coda-imx27", .driver_data = CODA_IMX27 },
	{ .name = "coda-imx53", .driver_data = CODA_IMX53 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, coda_platform_ids);

#ifdef CONFIG_OF
static const struct of_device_id coda_dt_ids[] = {
	{ .compatible = "fsl,imx27-vpu", .data = &coda_devdata[CODA_IMX27] },
	{ .compatible = "fsl,imx53-vpu", .data = &coda_devdata[CODA_IMX53] },
	{ .compatible = "fsl,imx6q-vpu", .data = &coda_devdata[CODA_IMX6Q] },
	{ .compatible = "fsl,imx6dl-vpu", .data = &coda_devdata[CODA_IMX6DL] },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, coda_dt_ids);
#endif

static int coda_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(of_match_ptr(coda_dt_ids), &pdev->dev);
	const struct platform_device_id *pdev_id;
	struct coda_platform_data *pdata = pdev->dev.platform_data;
	struct device_node *np = pdev->dev.of_node;
	struct gen_pool *pool;
	struct coda_dev *dev;
	struct resource *res;
	int ret, irq;

	dev = devm_kzalloc(&pdev->dev, sizeof *dev, GFP_KERNEL);
	if (!dev) {
		dev_err(&pdev->dev, "Not enough memory for %s\n",
			CODA_NAME);
		return -ENOMEM;
	}

	spin_lock_init(&dev->irqlock);
	INIT_LIST_HEAD(&dev->instances);

	dev->plat_dev = pdev;
	dev->clk_per = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(dev->clk_per)) {
		dev_err(&pdev->dev, "Could not get per clock\n");
		return PTR_ERR(dev->clk_per);
	}

	dev->clk_ahb = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(dev->clk_ahb)) {
		dev_err(&pdev->dev, "Could not get ahb clock\n");
		return PTR_ERR(dev->clk_ahb);
	}

	/* Get  memory for physical registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->regs_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->regs_base))
		return PTR_ERR(dev->regs_base);

	/* IRQ */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get irq resource\n");
		return irq;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL, coda_irq_handler,
			IRQF_ONESHOT, dev_name(&pdev->dev), dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request irq: %d\n", ret);
		return ret;
	}

	dev->rstc = devm_reset_control_get_optional(&pdev->dev, NULL);
	if (IS_ERR(dev->rstc)) {
		ret = PTR_ERR(dev->rstc);
		if (ret == -ENOENT || ret == -ENOSYS) {
			dev->rstc = NULL;
		} else {
			dev_err(&pdev->dev, "failed get reset control: %d\n", ret);
			return ret;
		}
	}

	/* Get IRAM pool from device tree or platform data */
	pool = of_get_named_gen_pool(np, "iram", 0);
	if (!pool && pdata)
		pool = dev_get_gen_pool(pdata->iram_dev);
	if (!pool) {
		dev_err(&pdev->dev, "iram pool not available\n");
		return -ENOMEM;
	}
	dev->iram_pool = pool;

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		return ret;

	mutex_init(&dev->dev_mutex);
	mutex_init(&dev->coda_mutex);

	pdev_id = of_id ? of_id->data : platform_get_device_id(pdev);

	if (of_id) {
		dev->devtype = of_id->data;
	} else if (pdev_id) {
		dev->devtype = &coda_devdata[pdev_id->driver_data];
	} else {
		v4l2_device_unregister(&dev->v4l2_dev);
		return -EINVAL;
	}

	dev->debugfs_root = debugfs_create_dir("coda", NULL);
	if (!dev->debugfs_root)
		dev_warn(&pdev->dev, "failed to create debugfs root\n");

	/* allocate auxiliary per-device buffers for the BIT processor */
	if (dev->devtype->product == CODA_DX6) {
		ret = coda_alloc_aux_buf(dev, &dev->workbuf,
					 dev->devtype->workbuf_size, "workbuf",
					 dev->debugfs_root);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to allocate work buffer\n");
			v4l2_device_unregister(&dev->v4l2_dev);
			return ret;
		}
	}

	if (dev->devtype->tempbuf_size) {
		ret = coda_alloc_aux_buf(dev, &dev->tempbuf,
					 dev->devtype->tempbuf_size, "tempbuf",
					 dev->debugfs_root);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to allocate temp buffer\n");
			v4l2_device_unregister(&dev->v4l2_dev);
			return ret;
		}
	}

	dev->iram.size = dev->devtype->iram_size;
	dev->iram.vaddr = gen_pool_dma_alloc(dev->iram_pool, dev->iram.size,
					     &dev->iram.paddr);
	if (!dev->iram.vaddr) {
		dev_err(&pdev->dev, "unable to alloc iram\n");
		return -ENOMEM;
	}

	dev->iram.blob.data = dev->iram.vaddr;
	dev->iram.blob.size = dev->iram.size;
	dev->iram.dentry = debugfs_create_blob("iram", 0644, dev->debugfs_root,
					       &dev->iram.blob);

	dev->workqueue = alloc_workqueue("coda", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!dev->workqueue) {
		dev_err(&pdev->dev, "unable to alloc workqueue\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, dev);

	pm_runtime_enable(&pdev->dev);

	return coda_firmware_request(dev);
}

static int coda_remove(struct platform_device *pdev)
{
	struct coda_dev *dev = platform_get_drvdata(pdev);

	video_unregister_device(&dev->vfd);
	if (dev->m2m_dev)
		v4l2_m2m_release(dev->m2m_dev);
	pm_runtime_disable(&pdev->dev);
	if (dev->alloc_ctx)
		vb2_dma_contig_cleanup_ctx(dev->alloc_ctx);
	v4l2_device_unregister(&dev->v4l2_dev);
	destroy_workqueue(dev->workqueue);
	if (dev->iram.vaddr)
		gen_pool_free(dev->iram_pool, (unsigned long)dev->iram.vaddr,
			      dev->iram.size);
	coda_free_aux_buf(dev, &dev->codebuf);
	coda_free_aux_buf(dev, &dev->tempbuf);
	coda_free_aux_buf(dev, &dev->workbuf);
	debugfs_remove_recursive(dev->debugfs_root);
	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int coda_runtime_resume(struct device *dev)
{
	struct coda_dev *cdev = dev_get_drvdata(dev);
	int ret = 0;

	if (dev->pm_domain) {
		ret = coda_hw_init(cdev);
		if (ret)
			v4l2_err(&cdev->v4l2_dev, "HW initialization failed\n");
	}

	return ret;
}
#endif

static const struct dev_pm_ops coda_pm_ops = {
	SET_RUNTIME_PM_OPS(NULL, coda_runtime_resume, NULL)
};

static struct platform_driver coda_driver = {
	.probe	= coda_probe,
	.remove	= coda_remove,
	.driver	= {
		.name	= CODA_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(coda_dt_ids),
		.pm	= &coda_pm_ops,
	},
	.id_table = coda_platform_ids,
};

module_platform_driver(coda_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Javier Martin <javier.martin@vista-silicon.com>");
MODULE_DESCRIPTION("Coda multi-standard codec V4L2 driver");
