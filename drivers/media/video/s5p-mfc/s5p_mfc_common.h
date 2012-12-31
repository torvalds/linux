/*
 * Samsung S5P Multi Format Codec v 5.0
 *
 * This file contains definitions of enums and structs used by the codec
 * driver.
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 * Kamil Debski, <k.debski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */

#ifndef S5P_MFC_COMMON_H_
#define S5P_MFC_COMMON_H_

#include <linux/videodev2.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>

#include <media/videobuf2-core.h>

#if defined(CONFIG_S5P_SYSTEM_MMU)
#define SYSMMU_MFC_ON
#endif

#define MFC_MAX_EXTRA_DPB       5
#define MFC_MAX_BUFFERS		32
#define MFC_MAX_REF_BUFS	2
#define MFC_FRAME_PLANES	2

#define MFC_NUM_CONTEXTS	4
/* Interrupt timeout */
#define MFC_INT_TIMEOUT		2000
/* Busy wait timeout */
#define MFC_BW_TIMEOUT		500
/* Watchdog interval */
#define MFC_WATCHDOG_INTERVAL   1000
/* After how many executions watchdog should assume lock up */
#define MFC_WATCHDOG_CNT        10

#define MFC_NO_INSTANCE_SET	-1

#define MFC_ENC_CAP_PLANE_COUNT	1
#define MFC_ENC_OUT_PLANE_COUNT	2

#define MFC_NAME_LEN		16
#define MFC_FW_NAME		"mfc_fw.bin"

#define STUFF_BYTE		4
#define MFC_WORKQUEUE_LEN	32

#define MFC_BASE_MASK		((1 << 17) - 1)

/**
 * enum s5p_mfc_inst_type - The type of an MFC device node.
 */
enum s5p_mfc_node_type {
	MFCNODE_INVALID = -1,
	MFCNODE_DECODER = 0,
	MFCNODE_ENCODER = 1,
};

/**
 * enum s5p_mfc_inst_type - The type of an MFC instance.
 */
enum s5p_mfc_inst_type {
	MFCINST_INVALID = 0,
	MFCINST_DECODER = 1,
	MFCINST_ENCODER = 2,
};

/**
 * enum s5p_mfc_inst_state - The state of an MFC instance.
 */
enum s5p_mfc_inst_state {
	MFCINST_FREE = 0,
	MFCINST_INIT = 100,
	MFCINST_GOT_INST,
	MFCINST_HEAD_PARSED,
	MFCINST_BUFS_SET,
	MFCINST_RUNNING,
	MFCINST_FINISHING,
	MFCINST_FINISHED,
	MFCINST_RETURN_INST,
	MFCINST_ERROR,
	MFCINST_ABORT,
	MFCINST_RES_CHANGE_INIT,
	MFCINST_RES_CHANGE_FLUSH,
	MFCINST_RES_CHANGE_END,
	MFCINST_RUNNING_NO_OUTPUT,
};

/**
 * enum s5p_mfc_queue_state - The state of buffer queue.
 */
enum s5p_mfc_queue_state {
	QUEUE_FREE = 0,
	QUEUE_BUFS_REQUESTED,
	QUEUE_BUFS_QUERIED,
	QUEUE_BUFS_MMAPED,
};

/**
 * enum s5p_mfc_check_state - The state for user notification
 */
enum s5p_mfc_check_state {
	MFCSTATE_PROCESSING = 0,
	MFCSTATE_DEC_RES_DETECT,
	MFCSTATE_DEC_TERMINATING,
	MFCSTATE_ENC_NO_OUTPUT,
};

/**
 * enum s5p_mfc_buf_cacheable_mask - The mask for cacheble setting
 */
enum s5p_mfc_buf_cacheable_mask {
	MFCMASK_DST_CACHE = (1 << 0),
	MFCMASK_SRC_CACHE = (1 << 1),
};

struct s5p_mfc_ctx;
struct s5p_mfc_extra_buf;

/**
 * struct s5p_mfc_buf - MFC buffer
 *
 */
struct s5p_mfc_buf {
	struct vb2_buffer vb;
	struct list_head list;
	union {
		struct {
			size_t luma;
			size_t chroma;
		} raw;
		size_t stream;
	} cookie;
	int used;
};

#define vb_to_mfc_buf(x)	\
	container_of(x, struct s5p_mfc_buf, vb)

struct s5p_mfc_pm {
	struct clk	*clock;
	atomic_t	power;
#ifdef CONFIG_PM_RUNTIME
	struct device	*device;
#endif
};

struct s5p_mfc_fw {
	const struct firmware	*info;
	int			state;
	int			ver;
};

struct s5p_mfc_buf_align {
	unsigned int mfc_base_align;
};

struct s5p_mfc_buf_size_v5 {
	unsigned int h264_ctx_buf;
	unsigned int non_h264_ctx_buf;
	unsigned int desc_buf;
	unsigned int shared_buf;
};

struct s5p_mfc_buf_size_v6 {
	unsigned int dev_ctx;
	unsigned int h264_dec_ctx;
	unsigned int other_dec_ctx;
	unsigned int h264_enc_ctx;
	unsigned int other_enc_ctx;
};

struct s5p_mfc_buf_size {
	unsigned int firmware_code;
	unsigned int cpb_buf;
	void *buf;
};

struct s5p_mfc_variant {
	unsigned int version;
	unsigned int port_num;
	struct s5p_mfc_buf_size *buf_size;
	struct s5p_mfc_buf_align *buf_align;
};

/**
 * struct s5p_mfc_extra_buf - represents internal used buffer
 * @alloc:		allocation-specific contexts for each buffer
 *			(videobuf2 allocator)
 * @ofs:		offset of each buffer, will be used for MFC
 * @virt:		kernel virtual address, only valid when the
 *			buffer accessed by driver
 * @dma:		DMA address, only valid when kernel DMA API used
 */
struct s5p_mfc_extra_buf {
	void		*alloc;
	unsigned long	ofs;
	void		*virt;
	dma_addr_t	dma;
};

/**
 * struct s5p_mfc_dev - The struct containing driver internal parameters.
 */
struct s5p_mfc_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	*vfd_dec;
	struct video_device	*vfd_enc;
	struct platform_device	*plat_dev;

	void __iomem		*regs_base;
	int			irq;
	struct resource		*mfc_mem;

	struct s5p_mfc_pm	pm;
	struct s5p_mfc_fw	fw;
	struct s5p_mfc_variant	*variant;

	int num_inst;
	spinlock_t irqlock;
	spinlock_t condlock;

	struct mutex mfc_mutex;

	int int_cond;
	int int_type;
	unsigned int int_err;
	wait_queue_head_t queue;

	size_t port_a;
	size_t port_b;

	unsigned long hw_lock;

	/*
	struct clk *clock1;
	struct clk *clock2;
	*/

	/* For 6.x, Added for SYS_INIT context buffer */
	struct s5p_mfc_extra_buf ctx_buf;

	struct s5p_mfc_ctx *ctx[MFC_NUM_CONTEXTS];
	int curr_ctx;
	unsigned long ctx_work_bits;

	atomic_t watchdog_cnt;
	struct timer_list watchdog_timer;
	struct workqueue_struct *watchdog_workqueue;
	struct work_struct watchdog_work;

	struct vb2_alloc_ctx **alloc_ctx;

	unsigned long clk_state;
	struct work_struct work_struct;
	struct workqueue_struct *irq_workqueue;
};

/**
 *
 */
struct s5p_mfc_h264_enc_params {
	u8 num_b_frame;
	enum v4l2_codec_mfc5x_enc_h264_profile profile;
	u8 level;
	enum v4l2_codec_mfc5x_enc_switch interlace;
	enum v4l2_codec_mfc5x_enc_h264_loop_filter loop_filter_mode;
	s8 loop_filter_alpha;
	s8 loop_filter_beta;
	enum v4l2_codec_mfc5x_enc_h264_entropy_mode entropy_mode;
	u8 max_ref_pic;
	u8 num_ref_pic_4p;
	enum v4l2_codec_mfc5x_enc_switch _8x8_transform;
	enum v4l2_codec_mfc5x_enc_switch rc_mb;
	u32 rc_framerate;
	u8 rc_frame_qp;
	u8 rc_min_qp;
	u8 rc_max_qp;
	enum v4l2_codec_mfc5x_enc_switch_inv rc_mb_dark;
	enum v4l2_codec_mfc5x_enc_switch_inv rc_mb_smooth;
	enum v4l2_codec_mfc5x_enc_switch_inv rc_mb_static;
	enum v4l2_codec_mfc5x_enc_switch_inv rc_mb_activity;
	u8 rc_p_frame_qp;
	u8 rc_b_frame_qp;
	enum v4l2_codec_mfc5x_enc_switch ar_vui;
	u8 ar_vui_idc;
	u16 ext_sar_width;
	u16 ext_sar_height;
	enum v4l2_codec_mfc5x_enc_switch open_gop;
	u16 open_gop_size;
	enum v4l2_codec_mfc5x_enc_switch hier_p_enable;
	u32 hier_layer0_qp;
	u32 hier_layer1_qp;
	u32 hier_layer2_qp;
	enum v4l2_codec_mfc5x_enc_switch sei_gen_enable;
	u32 curr_frame_frm0_flag;
	u32 frame_pack_arrgment_type;
	u32 fmo_enable;
	u32 fmo_slice_map_type;
	u32 fmo_slice_num_grp;
	u32 fmo_run_length[4];
	u32 fmo_sg_dir;
	u32 fmo_sg_rate;
	u32 aso_enable;
	u32 aso_slice_order[8];
};

/**
 *
 */
struct s5p_mfc_mpeg4_enc_params {
	/* MPEG4 Only */
	u8 num_b_frame;
	enum v4l2_codec_mfc5x_enc_mpeg4_profile profile;
	u8 level;
	enum v4l2_codec_mfc5x_enc_switch quarter_pixel; /* MFC5.x */
	u16 vop_time_res;
	u16 vop_frm_delta;
	u8 rc_b_frame_qp;
	/* Common for MPEG4, H263 */
	u32 rc_framerate;
	enum v4l2_codec_mfc5x_enc_switch rc_mb; /* MFC6.1 Only */
	u8 rc_frame_qp;
	u8 rc_min_qp;
	u8 rc_max_qp;
	u8 rc_p_frame_qp;
};

/**
 *
 */
struct s5p_mfc_enc_params {
	u16 width;
	u16 height;

	u16 gop_size;
	enum v4l2_codec_mfc5x_enc_multi_slice_mode slice_mode;
	u16 slice_mb;
	u32 slice_bit;
	u16 intra_refresh_mb;
	enum v4l2_codec_mfc5x_enc_switch pad;
	u8 pad_luma;
	u8 pad_cb;
	u8 pad_cr;
	enum v4l2_codec_mfc5x_enc_switch rc_frame;
	u32 rc_bitrate;
	u16 rc_reaction_coeff;
	u8 frame_tag;

	u16 vbv_buf_size;
	enum v4l2_codec_mfc5x_enc_seq_hdr_mode seq_hdr_mode;
	enum v4l2_codec_mfc5x_enc_frame_skip_mode frame_skip_mode;
	enum v4l2_codec_mfc5x_enc_switch fixed_target_bit;

	u16 rc_frame_delta;   /* MFC6.1 Only */

	union {
		struct s5p_mfc_h264_enc_params h264;
		struct s5p_mfc_mpeg4_enc_params mpeg4;
	} codec;
};

enum s5p_mfc_ctrl_type {
	MFC_CTRL_TYPE_GET_SRC	= 0x1,
	MFC_CTRL_TYPE_GET_DST	= 0x2,
	MFC_CTRL_TYPE_SET	= 0x4,
};

#define	MFC_CTRL_TYPE_GET	(MFC_CTRL_TYPE_GET_SRC | MFC_CTRL_TYPE_GET_DST)
#define	MFC_CTRL_TYPE_SRC	(MFC_CTRL_TYPE_SET | MFC_CTRL_TYPE_GET_SRC)
#define	MFC_CTRL_TYPE_DST	(MFC_CTRL_TYPE_GET_DST)

enum s5p_mfc_ctrl_mode {
	MFC_CTRL_MODE_NONE	= 0x0,
	MFC_CTRL_MODE_SFR	= 0x1,
	MFC_CTRL_MODE_SHM	= 0x2,
	MFC_CTRL_MODE_CST	= 0x4,
};

struct s5p_mfc_ctrl_cfg {
	enum s5p_mfc_ctrl_type type;
	unsigned int id;
	/*
	unsigned int is_dynamic;
	*/
	unsigned int is_volatile;	/* only for MFC_CTRL_TYPE_SET */
	unsigned int mode;
	unsigned int addr;
	unsigned int mask;
	unsigned int shft;
	unsigned int flag_mode;		/* only for MFC_CTRL_TYPE_SET */
	unsigned int flag_addr;		/* only for MFC_CTRL_TYPE_SET */
	unsigned int flag_shft;		/* only for MFC_CTRL_TYPE_SET */
};

struct s5p_mfc_ctx_ctrl {
	struct list_head list;
	enum s5p_mfc_ctrl_type type;
	unsigned int id;
	unsigned int addr;
	/*
	unsigned int is_dynamic;
	*/
	int has_new;
	int val;
};

struct s5p_mfc_buf_ctrl {
	struct list_head list;
	unsigned int id;
	enum s5p_mfc_ctrl_type type;
	int has_new;
	int val;
	unsigned int old_val;		/* only for MFC_CTRL_TYPE_SET */
	unsigned int is_volatile;	/* only for MFC_CTRL_TYPE_SET */
	unsigned int updated;
	unsigned int mode;
	unsigned int addr;
	unsigned int mask;
	unsigned int shft;
	unsigned int flag_mode;		/* only for MFC_CTRL_TYPE_SET */
	unsigned int flag_addr;		/* only for MFC_CTRL_TYPE_SET */
	unsigned int flag_shft;		/* only for MFC_CTRL_TYPE_SET */
};

struct s5p_mfc_codec_ops {
	/* initialization routines */
	int (*alloc_ctx_buf) (struct s5p_mfc_ctx *ctx);
	int (*alloc_desc_buf) (struct s5p_mfc_ctx *ctx);
	int (*get_init_arg) (struct s5p_mfc_ctx *ctx, void *arg);
	int (*pre_seq_start) (struct s5p_mfc_ctx *ctx);
	int (*post_seq_start) (struct s5p_mfc_ctx *ctx);
	int (*set_init_arg) (struct s5p_mfc_ctx *ctx, void *arg);
	int (*set_codec_bufs) (struct s5p_mfc_ctx *ctx);
	int (*set_dpbs) (struct s5p_mfc_ctx *ctx);		/* decoder */
	/* execution routines */
	int (*get_exe_arg) (struct s5p_mfc_ctx *ctx, void *arg);
	int (*pre_frame_start) (struct s5p_mfc_ctx *ctx);
	int (*post_frame_start) (struct s5p_mfc_ctx *ctx);
	int (*multi_data_frame) (struct s5p_mfc_ctx *ctx);
	int (*set_exe_arg) (struct s5p_mfc_ctx *ctx, void *arg);
	/* configuration routines */
	int (*get_codec_cfg) (struct s5p_mfc_ctx *ctx, unsigned int type, int *value);
	int (*set_codec_cfg) (struct s5p_mfc_ctx *ctx, unsigned int type, int *value);
	/* controls per buffer */
	int (*init_ctx_ctrls) (struct s5p_mfc_ctx *ctx);
	int (*cleanup_ctx_ctrls) (struct s5p_mfc_ctx *ctx);
	int (*init_buf_ctrls) (struct s5p_mfc_ctx *ctx, enum s5p_mfc_ctrl_type type, unsigned int index);
	int (*cleanup_buf_ctrls) (struct s5p_mfc_ctx *ctx, struct list_head *head);
	int (*to_buf_ctrls) (struct s5p_mfc_ctx *ctx, struct list_head *head);
	int (*to_ctx_ctrls) (struct s5p_mfc_ctx *ctx, struct list_head *head);
	int (*set_buf_ctrls_val) (struct s5p_mfc_ctx *ctx, struct list_head *head);
	int (*get_buf_ctrls_val) (struct s5p_mfc_ctx *ctx, struct list_head *head);
	int (*recover_buf_ctrls_val) (struct s5p_mfc_ctx *ctx, struct list_head *head);
};

#define call_cop(c, op, args...)				\
	(((c)->c_ops->op) ?					\
		((c)->c_ops->op(args)) : 0)

struct s5p_mfc_dec {
	int total_dpb_count;

	struct list_head dpb_queue;
	unsigned int dpb_queue_cnt;

	size_t src_buf_size;

	int loop_filter_mpeg4;
	int display_delay;
	int is_packedpb;
	int slice_enable;

	int crc_enable;
	int crc_luma0;
	int crc_chroma0;
	int crc_luma1;
	int crc_chroma1;

	struct s5p_mfc_extra_buf dsc;
	unsigned long consumed;
	unsigned long dpb_status;
	unsigned int dpb_flush;

	enum v4l2_memory dst_memtype;
	int sei_parse;

	/* For 6.x */
	int remained;
};

struct s5p_mfc_enc {
	struct s5p_mfc_enc_params params;

	size_t dst_buf_size;

	int frame_count;
	enum v4l2_codec_mfc5x_enc_frame_type frame_type;
	enum v4l2_codec_mfc5x_enc_force_frame_type force_frame_type;

	struct list_head ref_queue;
	unsigned int ref_queue_cnt;

	/* For 6.x */
	size_t luma_dpb_size;
	size_t chroma_dpb_size;
	size_t me_buffer_size;
	size_t tmv_buffer_size;

	unsigned int slice_mode;
	union {
		unsigned int mb;
		unsigned int bits;
	} slice_size;
};

/**
 * struct s5p_mfc_ctx - This struct contains the instance context
 */
struct s5p_mfc_ctx {
	struct s5p_mfc_dev *dev;
	struct v4l2_fh fh;
	int num;

	int int_cond;
	int int_type;
	unsigned int int_err;
	wait_queue_head_t queue;

	struct s5p_mfc_fmt *src_fmt;
	struct s5p_mfc_fmt *dst_fmt;

	struct vb2_queue vq_src;
	struct vb2_queue vq_dst;

	struct list_head src_queue;
	struct list_head dst_queue;

	unsigned int src_queue_cnt;
	unsigned int dst_queue_cnt;

	enum s5p_mfc_inst_type type;
	enum s5p_mfc_inst_state state;
	int inst_no;

	int img_width;
	int img_height;
	int buf_width;
	int buf_height;
	int dpb_count;

	int luma_size;
	int chroma_size;
	int mv_size;

	/* Buffers */
	void *port_a_buf;
	size_t port_a_phys;
	size_t port_a_size;

	void *port_b_buf;
	size_t port_b_phys;
	size_t port_b_size;

	enum s5p_mfc_queue_state capture_state;
	enum s5p_mfc_queue_state output_state;

	struct list_head ctrls;

	struct list_head src_ctrls[MFC_MAX_BUFFERS];
	struct list_head dst_ctrls[MFC_MAX_BUFFERS];

	int src_ctrls_flag[MFC_MAX_BUFFERS];
	int dst_ctrls_flag[MFC_MAX_BUFFERS];

	unsigned int sequence;

	/* Control values */
	int codec_mode;
	__u32 pix_format;
	int cacheable;

	/* Extra Buffers */
	unsigned int ctx_buf_size;
	struct s5p_mfc_extra_buf ctx;
	struct s5p_mfc_extra_buf shm;

	struct s5p_mfc_dec *dec_priv;
	struct s5p_mfc_enc *enc_priv;

	struct s5p_mfc_codec_ops *c_ops;

	/* For 6.x */
	size_t scratch_buf_size;

	/* ION file descriptor */
	int fd_ion;
};

#define fh_to_mfc_ctx(x)	\
	container_of(x, struct s5p_mfc_ctx, fh)

#define MFC_FMT_DEC	0
#define MFC_FMT_ENC	1
#define MFC_FMT_RAW	2

#define HAS_PORTNUM(dev)	(dev ? (dev->variant ? \
				(dev->variant->port_num ? 1 : 0) : 0 ) : 0 )
#define IS_TWOPORT(dev)		(dev->variant->port_num == 2 ? 1 : 0)
#define IS_MFCV6(dev)		(dev->variant->version >= 0x60 ? 1 : 0)

struct s5p_mfc_fmt {
	char *name;
	u32 fourcc;
	u32 codec_mode;
	u32 type;
	u32 num_planes;
};

#if defined(CONFIG_S5P_MFC_V5)
#include "regs-mfc-v5.h"
#include "s5p_mfc_opr_v5.h"
#include "s5p_mfc_shm.h"
#elif defined(CONFIG_S5P_MFC_V6)
#include "regs-mfc-v6.h"
#include "s5p_mfc_opr_v6.h"
#endif

#endif /* S5P_MFC_COMMON_H_ */
