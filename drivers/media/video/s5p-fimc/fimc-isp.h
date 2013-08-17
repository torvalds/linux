/*
 * Samsung EXYNOS4412 FIMC-ISP driver
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * All rights reserved.
 */
#ifndef FIMC_ISP_H_
#define FIMC_ISP_H_

#include <asm/sizes.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/media-entity.h>
#include <media/videobuf2-core.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/s5p_fimc.h>

#include "fimc-core.h"

#define FIMC_IS_REQ_BUFS_MIN	2

/* Bit index definitions for struct fimc_is::state */
enum {
	ST_FIMC_IS_LPM,
	ST_FIMC_IS_PENDING,
	ST_FIMC_IS_RUN,
	ST_FIMC_IS_STREAM,
	ST_FIMC_IS_SUSPENDED,
	ST_FIMC_IS_OFF,
	ST_FIMC_IS_IN_USE,
	ST_FIMC_IS_CONFIG,
	ST_IS_SENSOR_STREAM,
};

#define FIMC_IS_SD_PAD_SINK	0
#define FIMC_IS_SD_PAD_SOURCE	1
#define FIMC_IS_SD_PADS_NUM	2

struct fimc_is_variant {
	unsigned short max_width;
	unsigned short max_height;
	unsigned short out_width_align;
	unsigned short win_hor_offs_align;
	unsigned short out_hor_offs_align;
};

struct fimc_is_drvdata {
	/* TODO : add driver data */
};

#define fimc_is_get_drvdata(_pdev) \
	((struct fimc_is_drvdata *) platform_get_device_id(_pdev)->driver_data)

struct fimc_is_events {
	unsigned int data_overflow;
};

#define FIMC_IS_MAX_PLANES	1

/*
 * struct fimc_is_frame - source/target frame properties
 * @f_width: full pixel width
 * @f_height: full pixel height
 * @rect: crop/composition rectangle
 */
struct fimc_is_frame {
	u16 f_width;
	u16 f_height;
	struct v4l2_rect rect;
};

/*
 * struct fimc_is_buffer - video buffer structure
 * @vb:    vb2 buffer
 * @list:  list head for the buffers queue
 * @paddr: precalculated physical address
 */
struct fimc_is_buffer {
	struct vb2_buffer vb;
	struct list_head list;
	dma_addr_t paddr;
};

/*
 * struct fimc_is_control - fimc_is control structure
 * @test_pattern: test pattern controls
 */
struct fimc_is_control {
	/* internal mode operations */
	struct v4l2_ctrl *set_scenaio_mode;
	/* frame control operations */
	struct v4l2_ctrl *fps;
	/* touch af position control operations */
	struct v4l2_ctrl *set_position_x;
	struct v4l2_ctrl *set_position_y;
	/* metering area control operations */
	struct v4l2_ctrl *set_metering_pos_x;
	struct v4l2_ctrl *set_metering_pos_y;
	struct v4l2_ctrl *set_metering_win_x;
	struct v4l2_ctrl *set_metering_win_y;
	/* AE/AWB lock and unlock */
	struct v4l2_ctrl *ae_awb_lock_unlock;
	/* AF */
	struct v4l2_ctrl *focus_mode;
	struct v4l2_ctrl *focus_start_stop;
	/* TODO : add handler about af */
	/* TODO : add handler about flash */
	/* Effect */
	struct v4l2_ctrl *effect;
	/* AWB */
	struct v4l2_ctrl *awb;
	/* ISO */
	struct v4l2_ctrl *iso;
	/* Adust - contrast */
	struct v4l2_ctrl *contrast;
	/* Adust - saturation */
	struct v4l2_ctrl *saturation;
	/* Adust - sharpness */
	struct v4l2_ctrl *sharpness;
	/* Adust - exposure */
	struct v4l2_ctrl *exposure;
	/* Adust - brightness */
	struct v4l2_ctrl *brightness;
	/* Adust - hue */
	struct v4l2_ctrl *hue;
	/* Metering */
	struct v4l2_ctrl *metering;
	/* AFC */
	struct v4l2_ctrl *afc;
	/* Scene mode */
	struct v4l2_ctrl *scene_mode;
	/* VT mode */
	struct v4l2_ctrl *vt_mode;
	/* DRC - on/off */
	struct v4l2_ctrl *drc;
	/* FD - on/off */
	struct v4l2_ctrl *fd;
	/* FD - face number */
	struct v4l2_ctrl *fd_set_face_number;
	/* FD - set roll angle */
	struct v4l2_ctrl *fd_set_roll_angle;
	/* FD - set yaw angle */
	struct v4l2_ctrl *fd_set_yaw_angle;
	/* FD - get face count */
	struct v4l2_ctrl *fd_get_face_cnt;
	struct v4l2_ctrl *fd_get_face_frame_number;
	struct v4l2_ctrl *fd_get_face_confidence;
	struct v4l2_ctrl *fd_get_face_blink_level;
	struct v4l2_ctrl *fd_get_face_smile_level;
	struct v4l2_ctrl *fd_get_face_pos_tl_x;
	struct v4l2_ctrl *fd_get_face_pos_tl_y;
	struct v4l2_ctrl *fd_get_face_pos_br_x;
	struct v4l2_ctrl *fd_get_face_pos_br_y;
	struct v4l2_ctrl *fd_get_left_eye_pos_tl_x;
	struct v4l2_ctrl *fd_get_left_eye_pos_tl_y;
	struct v4l2_ctrl *fd_get_left_eye_pos_br_x;
	struct v4l2_ctrl *fd_get_left_eye_pos_br_y;
	struct v4l2_ctrl *fd_get_right_eye_pos_tl_x;
	struct v4l2_ctrl *fd_get_right_eye_pos_tl_y;
	struct v4l2_ctrl *fd_get_right_eye_pos_br_x;
	struct v4l2_ctrl *fd_get_right_eye_pos_br_y;
	struct v4l2_ctrl *fd_get_mouth_pos_tl_x;
	struct v4l2_ctrl *fd_get_mouth_pos_tl_y;
	struct v4l2_ctrl *fd_get_mouth_pos_br_x;
	struct v4l2_ctrl *fd_get_mouth_pos_br_y;
	struct v4l2_ctrl *fd_get_roll_angle;
	struct v4l2_ctrl *fd_get_yaw_angle;
	struct v4l2_ctrl *fd_get_next_data;
	/* Exif info */
	struct v4l2_ctrl *exif_exptime;
	struct v4l2_ctrl *exif_flash;
	struct v4l2_ctrl *exif_iso;
	struct v4l2_ctrl *exif_shutterspeed;
	struct v4l2_ctrl *exif_brightness;
	/* AF status */
	struct v4l2_ctrl *af_status;
};

/*
 * struct fimc_isp - fimc isp structure
 * @pdev: pointer to FIMC-LITE platform device
 * @variant: variant information for this IP
 * @v4l2_dev: pointer to top the level v4l2_device
 * @vfd: video device node
 * @fh: v4l2 file handle
 * @alloc_ctx: videobuf2 memory allocator context
 * @subdev: FIMC-LITE subdev
 * @vd_pad: media (sink) pad for the capture video node
 * @subdev_pads: the subdev media pads
 * @ctrl_handler: v4l2 control handler
 * @index: FIMC-LITE platform device index
 * @pipeline: video capture pipeline data structure
 * @slock: spinlock protecting this data structure and the hw registers
 * @lock: mutex serializing video device and the subdev operations
 * @clock: FIMC-LITE gate clock
 * @regs: memory mapped io registers
 * @irq_queue: interrupt handler waitqueue
 * @fmt: pointer to color format description structure
 * @payload: image size in bytes (w x h x bpp)
 * @inp_frame: camera input frame structure
 * @out_frame: DMA output frame structure
 * @out_path: output data path (DMA or FIFO)
 * @source_subdev_grp_id: source subdev group id
 * @state: driver state flags
 * @pending_buf_q: pending buffers queue head
 * @active_buf_q: the queue head of buffers scheduled in hardware
 * @vb_queue: vb2 buffers queue
 * @active_buf_count: number of video buffers scheduled in hardware
 * @frame_count: the captured frames counter
 * @reqbufs_count: the number of buffers requested with REQBUFS ioctl
 * @ref_count: driver's private reference counter
 */
struct fimc_isp {
	struct platform_device	*pdev;
	struct fimc_is_variant	*variant;
	struct v4l2_device	*v4l2_dev;
	struct video_device	*vfd;
	struct v4l2_fh		fh;
	struct vb2_alloc_ctx	*alloc_ctx;
	struct v4l2_subdev	subdev;
	struct media_pad	vd_pad;
	struct media_pad	subdev_pads[FIMC_IS_SD_PADS_NUM];
	struct v4l2_ctrl_handler ctrl_handler;
	struct fimc_is_control	ctrl_isp;

	u32			index;
	struct fimc_pipeline	pipeline;

	struct mutex		lock;
	spinlock_t		slock;

	struct clk		*clock;
	void __iomem		*regs;
	wait_queue_head_t	irq_queue;

	const struct fimc_fmt	*fmt;
	unsigned long		payload[FIMC_IS_MAX_PLANES];
	struct fimc_is_frame	inp_frame;
	struct fimc_is_frame	out_frame;
	enum fimc_datapath	out_path;
	unsigned int		source_subdev_grp_id;

	unsigned long		state;
	struct list_head	pending_buf_q;
	struct list_head	active_buf_q;
	struct vb2_queue	vb_queue;
	unsigned int		frame_count;
	unsigned int		reqbufs_count;
	int			ref_count;

	struct fimc_is_events	events;
};

static inline bool fimc_is_active(struct fimc_isp *isp)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&isp->slock, flags);
	ret = isp->state & (1 << ST_FIMC_IS_RUN) ||
		isp->state & (1 << ST_FIMC_IS_PENDING);
	spin_unlock_irqrestore(&isp->slock, flags);
	return ret;
}

static inline void fimc_is_active_queue_add(struct fimc_isp *isp,
					 struct fimc_is_buffer *buf)
{
	list_add_tail(&buf->list, &isp->active_buf_q);
}

static inline struct fimc_is_buffer *fimc_is_active_queue_pop(
					struct fimc_isp *isp)
{
	struct fimc_is_buffer *buf = list_entry(isp->active_buf_q.next,
					      struct fimc_is_buffer, list);
	list_del(&buf->list);
	return buf;
}

static inline void fimc_is_pending_queue_add(struct fimc_isp *isp,
					struct fimc_is_buffer *buf)
{
	list_add_tail(&buf->list, &isp->pending_buf_q);
}

static inline struct fimc_is_buffer *fimc_is_pending_queue_pop(
					struct fimc_isp *isp)
{
	struct fimc_is_buffer *buf = list_entry(isp->pending_buf_q.next,
					      struct fimc_is_buffer, list);
	list_del(&buf->list);
	return buf;
}

struct fimc_is;

int fimc_isp_subdev_create(struct fimc_isp *isp);
void fimc_isp_subdev_destroy(struct fimc_isp *isp);
void fimc_isp_irq_handler(struct fimc_is *is);
int fimc_is_create_controls(struct fimc_isp *isp);
int fimc_is_delete_controls(struct fimc_isp *isp);
#endif /* FIMC_ISP_H_ */
