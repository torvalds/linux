/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Coda multi-standard codec IP
 *
 * Copyright (C) 2012 Vista Silicon S.L.
 *    Javier Martin, <javier.martin@vista-silicon.com>
 *    Xavier Duret
 * Copyright (C) 2012-2014 Philipp Zabel, Pengutronix
 */

#ifndef __CODA_H__
#define __CODA_H__

#include <linux/debugfs.h>
#include <linux/idr.h>
#include <linux/irqreturn.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-v4l2.h>

#include "coda_regs.h"

#define CODA_MAX_FRAMEBUFFERS	19
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
	CODA_HX4 = 0xf00a,
	CODA_7541 = 0xf012,
	CODA_960 = 0xf020,
};

struct coda_video_device;

struct coda_devtype {
	char			*firmware[3];
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
	struct video_device	vfd[6];
	struct device		*dev;
	const struct coda_devtype *devtype;
	int			firmware;
	struct vdoa_data	*vdoa;

	void __iomem		*regs_base;
	struct clk		*clk_per;
	struct clk		*clk_ahb;
	struct reset_control	*rstc;

	struct coda_aux_buf	codebuf;
	struct coda_aux_buf	tempbuf;
	struct coda_aux_buf	workbuf;
	struct gen_pool		*iram_pool;
	struct coda_aux_buf	iram;

	struct mutex		dev_mutex;
	struct mutex		coda_mutex;
	struct workqueue_struct	*workqueue;
	struct v4l2_m2m_dev	*m2m_dev;
	struct ida		ida;
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
	u8			h264_disable_deblocking_filter_idc;
	s8			h264_slice_alpha_c0_offset_div2;
	s8			h264_slice_beta_offset_div2;
	bool			h264_constrained_intra_pred_flag;
	s8			h264_chroma_qp_index_offset;
	u8			h264_profile_idc;
	u8			h264_level_idc;
	u8			mpeg2_profile_idc;
	u8			mpeg2_level_idc;
	u8			mpeg4_intra_qp;
	u8			mpeg4_inter_qp;
	u8			gop_size;
	int			intra_refresh;
	enum v4l2_jpeg_chroma_subsampling jpeg_chroma_subsampling;
	u8			jpeg_quality;
	u8			jpeg_restart_interval;
	u8			*jpeg_qmat_tab[3];
	int			jpeg_qmat_index[3];
	int			jpeg_huff_dc_index[3];
	int			jpeg_huff_ac_index[3];
	u32			*jpeg_huff_data;
	struct coda_huff_tab	*jpeg_huff_tab;
	int			codec_mode;
	int			codec_mode_aux;
	enum v4l2_mpeg_video_multi_slice_mode slice_mode;
	u32			framerate;
	u16			bitrate;
	u16			vbv_delay;
	u32			vbv_size;
	u32			slice_max_bits;
	u32			slice_max_mb;
	bool			force_ipicture;
	bool			gop_size_changed;
	bool			bitrate_changed;
	bool			framerate_changed;
	bool			h264_intra_qp_changed;
	bool			intra_refresh_changed;
	bool			slice_mode_changed;
	bool			frame_rc_enable;
	bool			mb_rc_enable;
};

struct coda_buffer_meta {
	struct list_head	list;
	u32			sequence;
	struct v4l2_timecode	timecode;
	u64			timestamp;
	unsigned int		start;
	unsigned int		end;
	bool			last;
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

#define GDI_LINEAR_FRAME_MAP 0
#define GDI_TILED_FRAME_MB_RASTER_MAP 1

struct coda_ctx;

struct coda_context_ops {
	int (*queue_init)(void *priv, struct vb2_queue *src_vq,
			  struct vb2_queue *dst_vq);
	int (*reqbufs)(struct coda_ctx *ctx, struct v4l2_requestbuffers *rb);
	int (*start_streaming)(struct coda_ctx *ctx);
	int (*prepare_run)(struct coda_ctx *ctx);
	void (*finish_run)(struct coda_ctx *ctx);
	void (*run_timeout)(struct coda_ctx *ctx);
	void (*seq_init_work)(struct work_struct *work);
	void (*seq_end_work)(struct work_struct *work);
	void (*release)(struct coda_ctx *ctx);
};

struct coda_internal_frame {
	struct coda_aux_buf		buf;
	struct coda_buffer_meta		meta;
	u32				type;
	u32				error;
};

struct coda_ctx {
	struct coda_dev			*dev;
	struct mutex			buffer_mutex;
	struct work_struct		pic_run_work;
	struct work_struct		seq_init_work;
	struct work_struct		seq_end_work;
	struct completion		completion;
	const struct coda_video_device	*cvd;
	const struct coda_context_ops	*ops;
	int				aborting;
	int				initialized;
	int				streamon_out;
	int				streamon_cap;
	u32				qsequence;
	u32				osequence;
	u32				sequence_offset;
	struct coda_q_data		q_data[2];
	enum coda_inst_type		inst_type;
	const struct coda_codec		*codec;
	enum v4l2_colorspace		colorspace;
	enum v4l2_xfer_func		xfer_func;
	enum v4l2_ycbcr_encoding	ycbcr_enc;
	enum v4l2_quantization		quantization;
	struct coda_params		params;
	struct v4l2_ctrl_handler	ctrls;
	struct v4l2_ctrl		*h264_profile_ctrl;
	struct v4l2_ctrl		*h264_level_ctrl;
	struct v4l2_ctrl		*mpeg2_profile_ctrl;
	struct v4l2_ctrl		*mpeg2_level_ctrl;
	struct v4l2_ctrl		*mpeg4_profile_ctrl;
	struct v4l2_ctrl		*mpeg4_level_ctrl;
	struct v4l2_fh			fh;
	int				gopcounter;
	int				runcounter;
	int				jpeg_ecs_offset;
	char				vpu_header[3][64];
	int				vpu_header_size[3];
	struct kfifo			bitstream_fifo;
	struct mutex			bitstream_mutex;
	struct coda_aux_buf		bitstream;
	bool				hold;
	struct coda_aux_buf		parabuf;
	struct coda_aux_buf		psbuf;
	struct coda_aux_buf		slicebuf;
	struct coda_internal_frame	internal_frames[CODA_MAX_FRAMEBUFFERS];
	struct list_head		buffer_meta_list;
	spinlock_t			buffer_meta_lock;
	int				num_metas;
	struct coda_aux_buf		workbuf;
	int				num_internal_frames;
	int				idx;
	int				reg_idx;
	struct coda_iram_info		iram_info;
	int				tiled_map_type;
	u32				bit_stream_param;
	u32				frm_dis_flg;
	u32				frame_mem_ctrl;
	u32				para_change;
	int				display_idx;
	struct dentry			*debugfs_entry;
	bool				use_bit;
	bool				use_vdoa;
	struct vdoa_ctx			*vdoa;
	/*
	 * wakeup mutex used to serialize encoder stop command and finish_run,
	 * ensures that finish_run always either flags the last returned buffer
	 * or wakes up the capture queue to signal EOS afterwards.
	 */
	struct mutex			wakeup_mutex;
};

extern int coda_debug;

#define coda_dbg(level, ctx, fmt, arg...)				\
	do {								\
		if (coda_debug >= (level))				\
			v4l2_dbg((level), coda_debug, &(ctx)->dev->v4l2_dev, \
			 "%u: " fmt, (ctx)->idx, ##arg);		\
	} while (0)

void coda_write(struct coda_dev *dev, u32 data, u32 reg);
unsigned int coda_read(struct coda_dev *dev, u32 reg);
void coda_write_base(struct coda_ctx *ctx, struct coda_q_data *q_data,
		     struct vb2_v4l2_buffer *buf, unsigned int reg_y);

int coda_alloc_aux_buf(struct coda_dev *dev, struct coda_aux_buf *buf,
		       size_t size, const char *name, struct dentry *parent);
void coda_free_aux_buf(struct coda_dev *dev, struct coda_aux_buf *buf);

int coda_encoder_queue_init(void *priv, struct vb2_queue *src_vq,
			    struct vb2_queue *dst_vq);
int coda_decoder_queue_init(void *priv, struct vb2_queue *src_vq,
			    struct vb2_queue *dst_vq);

int coda_hw_reset(struct coda_ctx *ctx);

void coda_fill_bitstream(struct coda_ctx *ctx, struct list_head *buffer_list);

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

static inline unsigned int coda_get_bitstream_payload(struct coda_ctx *ctx)
{
	return kfifo_len(&ctx->bitstream_fifo);
}

/*
 * The bitstream prefetcher needs to read at least 2 256 byte periods past
 * the desired bitstream position for all data to reach the decoder.
 */
static inline bool coda_bitstream_can_fetch_past(struct coda_ctx *ctx,
						 unsigned int pos)
{
	return (int)(ctx->bitstream_fifo.kfifo.in - ALIGN(pos, 256)) > 512;
}

bool coda_bitstream_can_fetch_past(struct coda_ctx *ctx, unsigned int pos);
int coda_bitstream_flush(struct coda_ctx *ctx);

void coda_bit_stream_end_flag(struct coda_ctx *ctx);

void coda_m2m_buf_done(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf,
		       enum vb2_buffer_state state);

int coda_h264_filler_nal(int size, char *p);
int coda_h264_padding(int size, char *p);
int coda_h264_profile(int profile_idc);
int coda_h264_level(int level_idc);
int coda_sps_parse_profile(struct coda_ctx *ctx, struct vb2_buffer *vb);
int coda_h264_sps_fixup(struct coda_ctx *ctx, int width, int height, char *buf,
			int *size, int max_size);

int coda_mpeg2_profile(int profile_idc);
int coda_mpeg2_level(int level_idc);
u32 coda_mpeg2_parse_headers(struct coda_ctx *ctx, u8 *buf, u32 size);
int coda_mpeg4_profile(int profile_idc);
int coda_mpeg4_level(int level_idc);
u32 coda_mpeg4_parse_headers(struct coda_ctx *ctx, u8 *buf, u32 size);

void coda_update_profile_level_ctrls(struct coda_ctx *ctx, u8 profile_idc,
				     u8 level_idc);

bool coda_jpeg_check_buffer(struct coda_ctx *ctx, struct vb2_buffer *vb);
int coda_jpeg_decode_header(struct coda_ctx *ctx, struct vb2_buffer *vb);
int coda_jpeg_write_tables(struct coda_ctx *ctx);
void coda_set_jpeg_compression_quality(struct coda_ctx *ctx, int quality);

extern const struct coda_context_ops coda_bit_encode_ops;
extern const struct coda_context_ops coda_bit_decode_ops;
extern const struct coda_context_ops coda9_jpeg_encode_ops;
extern const struct coda_context_ops coda9_jpeg_decode_ops;

irqreturn_t coda_irq_handler(int irq, void *data);
irqreturn_t coda9_jpeg_irq_handler(int irq, void *data);

#endif /* __CODA_H__ */
