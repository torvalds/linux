/*
 * Coda multi-standard codec IP
 *
 * Copyright (C) 2012 Vista Silicon S.L.
 *    Javier Martin, <javier.martin@vista-silicon.com>
 *    Xavier Duret
 * Copyright (C) 2012-2014 Philipp Zabel, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/debugfs.h>
#include <linux/irqreturn.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-core.h>

#include "coda_regs.h"

#define CODA_MAX_FRAMEBUFFERS	8
#define CODA_MAX_FRAME_SIZE	0x100000
#define FMO_SLICE_SAVE_BUF_SIZE	(32)

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

struct coda_video_device;

struct coda_devtype {
	char			*firmware;
	enum coda_product	product;
	const struct coda_codec	*codecs;
	unsigned int		num_codecs;
	const struct coda_video_device **vdevs;
	unsigned int		num_vdevs;
	size_t			workbuf_size;
	size_t			tempbuf_size;
	size_t			iram_size;
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
	struct video_device	vfd[5];
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

struct coda_codec {
	u32 mode;
	u32 src_fourcc;
	u32 dst_fourcc;
	u32 max_w;
	u32 max_h;
};

struct coda_huff_tab;

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
	u8			jpeg_quality;
	u8			jpeg_restart_interval;
	u8			*jpeg_qmat_tab[3];
	int			codec_mode;
	int			codec_mode_aux;
	enum v4l2_mpeg_video_multi_slice_mode slice_mode;
	u32			framerate;
	u16			bitrate;
	u32			slice_max_bits;
	u32			slice_max_mb;
};

struct coda_buffer_meta {
	struct list_head	list;
	u32			sequence;
	struct v4l2_timecode	timecode;
	struct timeval		timestamp;
	u32			start;
	u32			end;
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

struct coda_ctx;

struct coda_context_ops {
	int (*queue_init)(void *priv, struct vb2_queue *src_vq,
			  struct vb2_queue *dst_vq);
	int (*start_streaming)(struct coda_ctx *ctx);
	int (*prepare_run)(struct coda_ctx *ctx);
	void (*finish_run)(struct coda_ctx *ctx);
	void (*seq_end_work)(struct work_struct *work);
	void (*release)(struct coda_ctx *ctx);
};

struct coda_ctx {
	struct coda_dev			*dev;
	struct mutex			buffer_mutex;
	struct list_head		list;
	struct work_struct		pic_run_work;
	struct work_struct		seq_end_work;
	struct completion		completion;
	const struct coda_video_device	*cvd;
	const struct coda_context_ops	*ops;
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
	const struct coda_codec		*codec;
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
	struct coda_buffer_meta		frame_metas[CODA_MAX_FRAMEBUFFERS];
	u32				frame_errors[CODA_MAX_FRAMEBUFFERS];
	struct list_head		buffer_meta_list;
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

extern int coda_debug;

void coda_write(struct coda_dev *dev, u32 data, u32 reg);
unsigned int coda_read(struct coda_dev *dev, u32 reg);
void coda_write_base(struct coda_ctx *ctx, struct coda_q_data *q_data,
		     struct vb2_buffer *buf, unsigned int reg_y);

int coda_alloc_aux_buf(struct coda_dev *dev, struct coda_aux_buf *buf,
		       size_t size, const char *name, struct dentry *parent);
void coda_free_aux_buf(struct coda_dev *dev, struct coda_aux_buf *buf);

static inline int coda_alloc_context_buf(struct coda_ctx *ctx,
					 struct coda_aux_buf *buf, size_t size,
					 const char *name)
{
	return coda_alloc_aux_buf(ctx->dev, buf, size, name, ctx->debugfs_entry);
}

int coda_encoder_queue_init(void *priv, struct vb2_queue *src_vq,
			    struct vb2_queue *dst_vq);
int coda_decoder_queue_init(void *priv, struct vb2_queue *src_vq,
			    struct vb2_queue *dst_vq);

int coda_hw_reset(struct coda_ctx *ctx);

void coda_fill_bitstream(struct coda_ctx *ctx);

void coda_set_gdi_regs(struct coda_ctx *ctx);

static inline struct coda_q_data *get_q_data(struct coda_ctx *ctx,
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

const char *coda_product_name(int product);

int coda_check_firmware(struct coda_dev *dev);

static inline int coda_get_bitstream_payload(struct coda_ctx *ctx)
{
	return kfifo_len(&ctx->bitstream_fifo);
}

void coda_bit_stream_end_flag(struct coda_ctx *ctx);

int coda_h264_padding(int size, char *p);

bool coda_jpeg_check_buffer(struct coda_ctx *ctx, struct vb2_buffer *vb);
int coda_jpeg_write_tables(struct coda_ctx *ctx);
void coda_set_jpeg_compression_quality(struct coda_ctx *ctx, int quality);

extern const struct coda_context_ops coda_bit_encode_ops;
extern const struct coda_context_ops coda_bit_decode_ops;

irqreturn_t coda_irq_handler(int irq, void *data);
