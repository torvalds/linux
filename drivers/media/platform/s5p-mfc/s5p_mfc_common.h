/*
 * Samsung S5P Multi Format Codec v 5.0
 *
 * This file contains definitions of enums and structs used by the codec
 * driver.
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * Kamil Debski, <k.debski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */

#ifndef S5P_MFC_COMMON_H_
#define S5P_MFC_COMMON_H_

#include "regs-mfc.h"
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>

/* Definitions related to MFC memory */

/* Offset base used to differentiate between CAPTURE and OUTPUT
*  while mmaping */
#define DST_QUEUE_OFF_BASE      (TASK_SIZE / 2)

/* Offset used by the hardware to store addresses */
#define MFC_OFFSET_SHIFT	11

#define FIRMWARE_ALIGN		0x20000		/* 128KB */
#define MFC_H264_CTX_BUF_SIZE	0x96000		/* 600KB per H264 instance */
#define MFC_CTX_BUF_SIZE	0x2800		/* 10KB per instance */
#define DESC_BUF_SIZE		0x20000		/* 128KB for DESC buffer */
#define SHARED_BUF_SIZE		0x2000		/* 8KB for shared buffer */

#define DEF_CPB_SIZE		0x40000		/* 512KB */

#define MFC_BANK1_ALLOC_CTX	0
#define MFC_BANK2_ALLOC_CTX	1

#define MFC_BANK1_ALIGN_ORDER	13
#define MFC_BANK2_ALIGN_ORDER	13
#define MFC_BASE_ALIGN_ORDER	17

#include <media/videobuf2-dma-contig.h>

static inline dma_addr_t s5p_mfc_mem_cookie(void *a, void *b)
{
	/* Same functionality as the vb2_dma_contig_plane_paddr */
	dma_addr_t *paddr = vb2_dma_contig_memops.cookie(b);

	return *paddr;
}

/* MFC definitions */
#define MFC_MAX_EXTRA_DPB       5
#define MFC_MAX_BUFFERS		32
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
#define STUFF_BYTE		4
#define MFC_MAX_CTRLS		64

#define mfc_read(dev, offset)		readl(dev->regs_base + (offset))
#define mfc_write(dev, data, offset)	writel((data), dev->regs_base + \
								(offset))

/**
 * enum s5p_mfc_fmt_type - type of the pixelformat
 */
enum s5p_mfc_fmt_type {
	MFC_FMT_DEC,
	MFC_FMT_ENC,
	MFC_FMT_RAW,
};

/**
 * enum s5p_mfc_node_type - The type of an MFC device node.
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
	MFCINST_INVALID,
	MFCINST_DECODER,
	MFCINST_ENCODER,
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
};

/**
 * enum s5p_mfc_queue_state - The state of buffer queue.
 */
enum s5p_mfc_queue_state {
	QUEUE_FREE,
	QUEUE_BUFS_REQUESTED,
	QUEUE_BUFS_QUERIED,
	QUEUE_BUFS_MMAPED,
};

/**
 * enum s5p_mfc_decode_arg - type of frame decoding
 */
enum s5p_mfc_decode_arg {
	MFC_DEC_FRAME,
	MFC_DEC_LAST_FRAME,
	MFC_DEC_RES_CHANGE,
};

struct s5p_mfc_ctx;

/**
 * struct s5p_mfc_buf - MFC buffer
 */
struct s5p_mfc_buf {
	struct list_head list;
	struct vb2_buffer *b;
	union {
		struct {
			size_t luma;
			size_t chroma;
		} raw;
		size_t stream;
	} cookie;
	int used;
};

/**
 * struct s5p_mfc_pm - power management data structure
 */
struct s5p_mfc_pm {
	struct clk	*clock;
	struct clk	*clock_gate;
	atomic_t	power;
	struct device	*device;
};

/**
 * struct s5p_mfc_dev - The struct containing driver internal parameters.
 *
 * @v4l2_dev:		v4l2_device
 * @vfd_dec:		video device for decoding
 * @vfd_enc:		video device for encoding
 * @plat_dev:		platform device
 * @mem_dev_l:		child device of the left memory bank (0)
 * @mem_dev_r:		child device of the right memory bank (1)
 * @regs_base:		base address of the MFC hw registers
 * @irq:		irq resource
 * @dec_ctrl_handler:	control framework handler for decoding
 * @enc_ctrl_handler:	control framework handler for encoding
 * @pm:			power management control
 * @num_inst:		couter of active MFC instances
 * @irqlock:		lock for operations on videobuf2 queues
 * @condlock:		lock for changing/checking if a context is ready to be
 *			processed
 * @mfc_mutex:		lock for video_device
 * @int_cond:		variable used by the waitqueue
 * @int_type:		type of last interrupt
 * @int_err:		error number for last interrupt
 * @queue:		waitqueue for waiting for completion of device commands
 * @fw_size:		size of firmware
 * @bank1:		address of the beggining of bank 1 memory
 * @bank2:		address of the beggining of bank 2 memory
 * @hw_lock:		used for hardware locking
 * @ctx:		array of driver contexts
 * @curr_ctx:		number of the currently running context
 * @ctx_work_bits:	used to mark which contexts are waiting for hardware
 * @watchdog_cnt:	counter for the watchdog
 * @watchdog_workqueue:	workqueue for the watchdog
 * @watchdog_work:	worker for the watchdog
 * @alloc_ctx:		videobuf2 allocator contexts for two memory banks
 * @enter_suspend:	flag set when entering suspend
 *
 */
struct s5p_mfc_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	*vfd_dec;
	struct video_device	*vfd_enc;
	struct platform_device	*plat_dev;
	struct device		*mem_dev_l;
	struct device		*mem_dev_r;
	void __iomem		*regs_base;
	int			irq;
	struct v4l2_ctrl_handler dec_ctrl_handler;
	struct v4l2_ctrl_handler enc_ctrl_handler;
	struct s5p_mfc_pm	pm;
	int num_inst;
	spinlock_t irqlock;	/* lock when operating on videobuf2 queues */
	spinlock_t condlock;	/* lock when changing/checking if a context is
					ready to be processed */
	struct mutex mfc_mutex; /* video_device lock */
	int int_cond;
	int int_type;
	unsigned int int_err;
	wait_queue_head_t queue;
	size_t fw_size;
	size_t bank1;
	size_t bank2;
	unsigned long hw_lock;
	struct s5p_mfc_ctx *ctx[MFC_NUM_CONTEXTS];
	int curr_ctx;
	unsigned long ctx_work_bits;
	atomic_t watchdog_cnt;
	struct timer_list watchdog_timer;
	struct workqueue_struct *watchdog_workqueue;
	struct work_struct watchdog_work;
	void *alloc_ctx[2];
	unsigned long enter_suspend;
};

/**
 * struct s5p_mfc_h264_enc_params - encoding parameters for h264
 */
struct s5p_mfc_h264_enc_params {
	enum v4l2_mpeg_video_h264_profile profile;
	enum v4l2_mpeg_video_h264_loop_filter_mode loop_filter_mode;
	s8 loop_filter_alpha;
	s8 loop_filter_beta;
	enum v4l2_mpeg_video_h264_entropy_mode entropy_mode;
	u8 max_ref_pic;
	u8 num_ref_pic_4p;
	int _8x8_transform;
	int rc_mb;
	int rc_mb_dark;
	int rc_mb_smooth;
	int rc_mb_static;
	int rc_mb_activity;
	int vui_sar;
	u8 vui_sar_idc;
	u16 vui_ext_sar_width;
	u16 vui_ext_sar_height;
	int open_gop;
	u16 open_gop_size;
	u8 rc_frame_qp;
	u8 rc_min_qp;
	u8 rc_max_qp;
	u8 rc_p_frame_qp;
	u8 rc_b_frame_qp;
	enum v4l2_mpeg_video_h264_level level_v4l2;
	int level;
	u16 cpb_size;
};

/**
 * struct s5p_mfc_mpeg4_enc_params - encoding parameters for h263 and mpeg4
 */
struct s5p_mfc_mpeg4_enc_params {
	/* MPEG4 Only */
	enum v4l2_mpeg_video_mpeg4_profile profile;
	int quarter_pixel;
	/* Common for MPEG4, H263 */
	u16 vop_time_res;
	u16 vop_frm_delta;
	u8 rc_frame_qp;
	u8 rc_min_qp;
	u8 rc_max_qp;
	u8 rc_p_frame_qp;
	u8 rc_b_frame_qp;
	enum v4l2_mpeg_video_mpeg4_level level_v4l2;
	int level;
};

/**
 * struct s5p_mfc_enc_params - general encoding parameters
 */
struct s5p_mfc_enc_params {
	u16 width;
	u16 height;

	u16 gop_size;
	enum v4l2_mpeg_video_multi_slice_mode slice_mode;
	u16 slice_mb;
	u32 slice_bit;
	u16 intra_refresh_mb;
	int pad;
	u8 pad_luma;
	u8 pad_cb;
	u8 pad_cr;
	int rc_frame;
	u32 rc_bitrate;
	u16 rc_reaction_coeff;
	u16 vbv_size;

	enum v4l2_mpeg_video_header_mode seq_hdr_mode;
	enum v4l2_mpeg_mfc51_video_frame_skip_mode frame_skip_mode;
	int fixed_target_bit;

	u8 num_b_frame;
	u32 rc_framerate_num;
	u32 rc_framerate_denom;
	int interlace;

	union {
		struct s5p_mfc_h264_enc_params h264;
		struct s5p_mfc_mpeg4_enc_params mpeg4;
	} codec;

};

/**
 * struct s5p_mfc_codec_ops - codec ops, used by encoding
 */
struct s5p_mfc_codec_ops {
	/* initialization routines */
	int (*pre_seq_start) (struct s5p_mfc_ctx *ctx);
	int (*post_seq_start) (struct s5p_mfc_ctx *ctx);
	/* execution routines */
	int (*pre_frame_start) (struct s5p_mfc_ctx *ctx);
	int (*post_frame_start) (struct s5p_mfc_ctx *ctx);
};

#define call_cop(c, op, args...)				\
	(((c)->c_ops->op) ?					\
		((c)->c_ops->op(args)) : 0)

/**
 * struct s5p_mfc_ctx - This struct contains the instance context
 *
 * @dev:		pointer to the s5p_mfc_dev of the device
 * @fh:			struct v4l2_fh
 * @num:		number of the context that this structure describes
 * @int_cond:		variable used by the waitqueue
 * @int_type:		type of the last interrupt
 * @int_err:		error number received from MFC hw in the interrupt
 * @queue:		waitqueue that can be used to wait for this context to
 *			finish
 * @src_fmt:		source pixelformat information
 * @dst_fmt:		destination pixelformat information
 * @vq_src:		vb2 queue for source buffers
 * @vq_dst:		vb2 queue for destination buffers
 * @src_queue:		driver internal queue for source buffers
 * @dst_queue:		driver internal queue for destination buffers
 * @src_queue_cnt:	number of buffers queued on the source internal queue
 * @dst_queue_cnt:	number of buffers queued on the dest internal queue
 * @type:		type of the instance - decoder or encoder
 * @state:		state of the context
 * @inst_no:		number of hw instance associated with the context
 * @img_width:		width of the image that is decoded or encoded
 * @img_height:		height of the image that is decoded or encoded
 * @buf_width:		width of the buffer for processed image
 * @buf_height:		height of the buffer for processed image
 * @luma_size:		size of a luma plane
 * @chroma_size:	size of a chroma plane
 * @mv_size:		size of a motion vectors buffer
 * @consumed_stream:	number of bytes that have been used so far from the
 *			decoding buffer
 * @dpb_flush_flag:	flag used to indicate that a DPB buffers are being
 *			flushed
 * @bank1_buf:		handle to memory allocated for temporary buffers from
 *			memory bank 1
 * @bank1_phys:		address of the temporary buffers from memory bank 1
 * @bank1_size:		size of the memory allocated for temporary buffers from
 *			memory bank 1
 * @bank2_buf:		handle to memory allocated for temporary buffers from
 *			memory bank 2
 * @bank2_phys:		address of the temporary buffers from memory bank 2
 * @bank2_size:		size of the memory allocated for temporary buffers from
 *			memory bank 2
 * @capture_state:	state of the capture buffers queue
 * @output_state:	state of the output buffers queue
 * @src_bufs:		information on allocated source buffers
 * @dst_bufs:		information on allocated destination buffers
 * @sequence:		counter for the sequence number for v4l2
 * @dec_dst_flag:	flags for buffers queued in the hardware
 * @dec_src_buf_size:	size of the buffer for source buffers in decoding
 * @codec_mode:		number of codec mode used by MFC hw
 * @slice_interface:	slice interface flag
 * @loop_filter_mpeg4:	loop filter for MPEG4 flag
 * @display_delay:	value of the display delay for H264
 * @display_delay_enable:	display delay for H264 enable flag
 * @after_packed_pb:	flag used to track buffer when stream is in
 *			Packed PB format
 * @dpb_count:		count of the DPB buffers required by MFC hw
 * @total_dpb_count:	count of DPB buffers with additional buffers
 *			requested by the application
 * @ctx_buf:		handle to the memory associated with this context
 * @ctx_phys:		address of the memory associated with this context
 * @ctx_size:		size of the memory associated with this context
 * @desc_buf:		description buffer for decoding handle
 * @desc_phys:		description buffer for decoding address
 * @shm_alloc:		handle for the shared memory buffer
 * @shm:		virtual address for the shared memory buffer
 * @shm_ofs:		address offset for shared memory
 * @enc_params:		encoding parameters for MFC
 * @enc_dst_buf_size:	size of the buffers for encoder output
 * @frame_type:		used to force the type of the next encoded frame
 * @ref_queue:		list of the reference buffers for encoding
 * @ref_queue_cnt:	number of the buffers in the reference list
 * @c_ops:		ops for encoding
 * @ctrls:		array of controls, used when adding controls to the
 *			v4l2 control framework
 * @ctrl_handler:	handler for v4l2 framework
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

	/* Image parameters */
	int img_width;
	int img_height;
	int buf_width;
	int buf_height;

	int luma_size;
	int chroma_size;
	int mv_size;

	unsigned long consumed_stream;

	unsigned int dpb_flush_flag;

	/* Buffers */
	void *bank1_buf;
	size_t bank1_phys;
	size_t bank1_size;

	void *bank2_buf;
	size_t bank2_phys;
	size_t bank2_size;

	enum s5p_mfc_queue_state capture_state;
	enum s5p_mfc_queue_state output_state;

	struct s5p_mfc_buf src_bufs[MFC_MAX_BUFFERS];
	int src_bufs_cnt;
	struct s5p_mfc_buf dst_bufs[MFC_MAX_BUFFERS];
	int dst_bufs_cnt;

	unsigned int sequence;
	unsigned long dec_dst_flag;
	size_t dec_src_buf_size;

	/* Control values */
	int codec_mode;
	int slice_interface;
	int loop_filter_mpeg4;
	int display_delay;
	int display_delay_enable;
	int after_packed_pb;

	int dpb_count;
	int total_dpb_count;

	/* Buffers */
	void *ctx_buf;
	size_t ctx_phys;
	size_t ctx_ofs;
	size_t ctx_size;

	void *desc_buf;
	size_t desc_phys;


	void *shm_alloc;
	void *shm;
	size_t shm_ofs;

	struct s5p_mfc_enc_params enc_params;

	size_t enc_dst_buf_size;

	enum v4l2_mpeg_mfc51_video_force_frame_type force_frame_type;

	struct list_head ref_queue;
	unsigned int ref_queue_cnt;

	struct s5p_mfc_codec_ops *c_ops;

	struct v4l2_ctrl *ctrls[MFC_MAX_CTRLS];
	struct v4l2_ctrl_handler ctrl_handler;
};

/*
 * struct s5p_mfc_fmt -	structure used to store information about pixelformats
 *			used by the MFC
 */
struct s5p_mfc_fmt {
	char *name;
	u32 fourcc;
	u32 codec_mode;
	enum s5p_mfc_fmt_type type;
	u32 num_planes;
};

/**
 * struct mfc_control -	structure used to store information about MFC controls
 *			it is used to initialize the control framework.
 */
struct mfc_control {
	__u32			id;
	enum v4l2_ctrl_type	type;
	__u8			name[32];  /* Whatever */
	__s32			minimum;   /* Note signedness */
	__s32			maximum;
	__s32			step;
	__u32			menu_skip_mask;
	__s32			default_value;
	__u32			flags;
	__u32			reserved[2];
	__u8			is_volatile;
};


#define fh_to_ctx(__fh) container_of(__fh, struct s5p_mfc_ctx, fh)
#define ctrl_to_ctx(__ctrl) \
	container_of((__ctrl)->handler, struct s5p_mfc_ctx, ctrl_handler)

#endif /* S5P_MFC_COMMON_H_ */
