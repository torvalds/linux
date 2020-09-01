/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Houlong Wei <houlong.wei@mediatek.com>
 *         Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 */

#ifndef __MTK_MDP_CORE_H__
#define __MTK_MDP_CORE_H__

#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "mtk_mdp_vpu.h"
#include "mtk_mdp_comp.h"


#define MTK_MDP_MODULE_NAME		"mtk-mdp"

#define MTK_MDP_SHUTDOWN_TIMEOUT	((100*HZ)/1000) /* 100ms */
#define MTK_MDP_MAX_CTRL_NUM		10

#define MTK_MDP_FMT_FLAG_OUTPUT		BIT(0)
#define MTK_MDP_FMT_FLAG_CAPTURE	BIT(1)

#define MTK_MDP_VPU_INIT		BIT(0)
#define MTK_MDP_CTX_ERROR		BIT(5)

/**
 *  struct mtk_mdp_pix_align - alignment of image
 *  @org_w: source alignment of width
 *  @org_h: source alignment of height
 *  @target_w: dst alignment of width
 *  @target_h: dst alignment of height
 */
struct mtk_mdp_pix_align {
	u16 org_w;
	u16 org_h;
	u16 target_w;
	u16 target_h;
};

/**
 * struct mtk_mdp_fmt - the driver's internal color format data
 * @pixelformat: the fourcc code for this format, 0 if not applicable
 * @num_planes: number of physically non-contiguous data planes
 * @num_comp: number of logical data planes
 * @depth: per plane driver's private 'number of bits per pixel'
 * @row_depth: per plane driver's private 'number of bits per pixel per row'
 * @flags: flags indicating which operation mode format applies to
	   MTK_MDP_FMT_FLAG_OUTPUT is used in OUTPUT stream
	   MTK_MDP_FMT_FLAG_CAPTURE is used in CAPTURE stream
 * @align: pointer to a pixel alignment struct, NULL if using default value
 */
struct mtk_mdp_fmt {
	u32	pixelformat;
	u16	num_planes;
	u16	num_comp;
	u8	depth[VIDEO_MAX_PLANES];
	u8	row_depth[VIDEO_MAX_PLANES];
	u32	flags;
	struct mtk_mdp_pix_align *align;
};

/**
 * struct mtk_mdp_addr - the image processor physical address set
 * @addr:	address of planes
 */
struct mtk_mdp_addr {
	dma_addr_t addr[MTK_MDP_MAX_NUM_PLANE];
};

/* struct mtk_mdp_ctrls - the image processor control set
 * @rotate: rotation degree
 * @hflip: horizontal flip
 * @vflip: vertical flip
 * @global_alpha: the alpha value of current frame
 */
struct mtk_mdp_ctrls {
	struct v4l2_ctrl *rotate;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *global_alpha;
};

/**
 * struct mtk_mdp_frame - source/target frame properties
 * @width:	SRC : SRCIMG_WIDTH, DST : OUTPUTDMA_WHOLE_IMG_WIDTH
 * @height:	SRC : SRCIMG_HEIGHT, DST : OUTPUTDMA_WHOLE_IMG_HEIGHT
 * @crop:	cropped(source)/scaled(destination) size
 * @payload:	image size in bytes (w x h x bpp)
 * @pitch:	bytes per line of image in memory
 * @addr:	image frame buffer physical addresses
 * @fmt:	color format pointer
 * @alpha:	frame's alpha value
 */
struct mtk_mdp_frame {
	u32				width;
	u32				height;
	struct v4l2_rect		crop;
	unsigned long			payload[VIDEO_MAX_PLANES];
	unsigned int			pitch[VIDEO_MAX_PLANES];
	struct mtk_mdp_addr		addr;
	const struct mtk_mdp_fmt	*fmt;
	u8				alpha;
};

/**
 * struct mtk_mdp_variant - image processor variant information
 * @pix_max:		maximum limit of image size
 * @pix_min:		minimum limit of image size
 * @pix_align:		alignment of image
 * @h_scale_up_max:	maximum scale-up in horizontal
 * @v_scale_up_max:	maximum scale-up in vertical
 * @h_scale_down_max:	maximum scale-down in horizontal
 * @v_scale_down_max:	maximum scale-down in vertical
 */
struct mtk_mdp_variant {
	struct mtk_mdp_pix_limit	*pix_max;
	struct mtk_mdp_pix_limit	*pix_min;
	struct mtk_mdp_pix_align	*pix_align;
	u16				h_scale_up_max;
	u16				v_scale_up_max;
	u16				h_scale_down_max;
	u16				v_scale_down_max;
};

/**
 * struct mtk_mdp_dev - abstraction for image processor entity
 * @lock:	the mutex protecting this data structure
 * @vpulock:	the mutex protecting the communication with VPU
 * @pdev:	pointer to the image processor platform device
 * @variant:	the IP variant information
 * @id:		image processor device index (0..MTK_MDP_MAX_DEVS)
 * @comp_list:	list of MDP function components
 * @m2m_dev:	v4l2 memory-to-memory device data
 * @ctx_list:	list of struct mtk_mdp_ctx
 * @vdev:	video device for image processor driver
 * @v4l2_dev:	V4L2 device to register video devices for.
 * @job_wq:	processor work queue
 * @vpu_dev:	VPU platform device
 * @ctx_num:	counter of active MTK MDP context
 * @id_counter:	An integer id given to the next opened context
 * @wdt_wq:	work queue for VPU watchdog
 * @wdt_work:	worker for VPU watchdog
 */
struct mtk_mdp_dev {
	struct mutex			lock;
	struct mutex			vpulock;
	struct platform_device		*pdev;
	struct mtk_mdp_variant		*variant;
	u16				id;
	struct list_head		comp_list;
	struct v4l2_m2m_dev		*m2m_dev;
	struct list_head		ctx_list;
	struct video_device		*vdev;
	struct v4l2_device		v4l2_dev;
	struct workqueue_struct		*job_wq;
	struct platform_device		*vpu_dev;
	int				ctx_num;
	unsigned long			id_counter;
	struct workqueue_struct		*wdt_wq;
	struct work_struct		wdt_work;
};

/**
 * mtk_mdp_ctx - the device context data
 * @list:		link to ctx_list of mtk_mdp_dev
 * @s_frame:		source frame properties
 * @d_frame:		destination frame properties
 * @id:			index of the context that this structure describes
 * @flags:		additional flags for image conversion
 * @state:		flags to keep track of user configuration
			Protected by slock
 * @rotation:		rotates the image by specified angle
 * @hflip:		mirror the picture horizontally
 * @vflip:		mirror the picture vertically
 * @mdp_dev:		the image processor device this context applies to
 * @m2m_ctx:		memory-to-memory device context
 * @fh:			v4l2 file handle
 * @ctrl_handler:	v4l2 controls handler
 * @ctrls		image processor control set
 * @ctrls_rdy:		true if the control handler is initialized
 * @colorspace:		enum v4l2_colorspace; supplemental to pixelformat
 * @ycbcr_enc:		enum v4l2_ycbcr_encoding, Y'CbCr encoding
 * @xfer_func:		enum v4l2_xfer_func, colorspace transfer function
 * @quant:		enum v4l2_quantization, colorspace quantization
 * @vpu:		VPU instance
 * @slock:		the mutex protecting mtp_mdp_ctx.state
 * @work:		worker for image processing
 */
struct mtk_mdp_ctx {
	struct list_head		list;
	struct mtk_mdp_frame		s_frame;
	struct mtk_mdp_frame		d_frame;
	u32				flags;
	u32				state;
	int				id;
	int				rotation;
	u32				hflip:1;
	u32				vflip:1;
	struct mtk_mdp_dev		*mdp_dev;
	struct v4l2_m2m_ctx		*m2m_ctx;
	struct v4l2_fh			fh;
	struct v4l2_ctrl_handler	ctrl_handler;
	struct mtk_mdp_ctrls		ctrls;
	bool				ctrls_rdy;
	enum v4l2_colorspace		colorspace;
	enum v4l2_ycbcr_encoding	ycbcr_enc;
	enum v4l2_xfer_func		xfer_func;
	enum v4l2_quantization		quant;

	struct mtk_mdp_vpu		vpu;
	struct mutex			slock;
	struct work_struct		work;
};

extern int mtk_mdp_dbg_level;

void mtk_mdp_register_component(struct mtk_mdp_dev *mdp,
				struct mtk_mdp_comp *comp);

void mtk_mdp_unregister_component(struct mtk_mdp_dev *mdp,
				  struct mtk_mdp_comp *comp);

#if defined(DEBUG)

#define mtk_mdp_dbg(level, fmt, args...)				 \
	do {								 \
		if (mtk_mdp_dbg_level >= level)				 \
			pr_info("[MTK_MDP] level=%d %s(),%d: " fmt "\n", \
				level, __func__, __LINE__, ##args);	 \
	} while (0)

#define mtk_mdp_err(fmt, args...)					\
	pr_err("[MTK_MDP][ERROR] %s:%d: " fmt "\n", __func__, __LINE__, \
	       ##args)


#define mtk_mdp_dbg_enter()  mtk_mdp_dbg(3, "+")
#define mtk_mdp_dbg_leave()  mtk_mdp_dbg(3, "-")

#else

#define mtk_mdp_dbg(level, fmt, args...) {}
#define mtk_mdp_err(fmt, args...)
#define mtk_mdp_dbg_enter()
#define mtk_mdp_dbg_leave()

#endif

#endif /* __MTK_MDP_CORE_H__ */
