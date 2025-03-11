/*
 * omap_voutdef.h
 *
 * Copyright (C) 2010 Texas Instruments.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef OMAP_VOUTDEF_H
#define OMAP_VOUTDEF_H

#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-ctrls.h>
#include <video/omapfb_dss.h>
#include <video/omapvrfb.h>
#include <linux/dmaengine.h>

#define YUYV_BPP        2
#define RGB565_BPP      2
#define RGB24_BPP       3
#define RGB32_BPP       4
#define TILE_SIZE       32
#define YUYV_VRFB_BPP   2
#define RGB_VRFB_BPP    1
#define MAX_CID		3
#define MAC_VRFB_CTXS	4
#define MAX_VOUT_DEV	2
#define MAX_OVLS	3
#define MAX_DISPLAYS	10
#define MAX_MANAGERS	3

#define QQVGA_WIDTH		160
#define QQVGA_HEIGHT		120

/* Max Resolution supported by the driver */
#define VID_MAX_WIDTH		1280	/* Largest width */
#define VID_MAX_HEIGHT		720	/* Largest height */

/* Minimum requirement is 2x2 for DSS */
#define VID_MIN_WIDTH		2
#define VID_MIN_HEIGHT		2

/* 2048 x 2048 is max res supported by OMAP display controller */
#define MAX_PIXELS_PER_LINE     2048

#define VRFB_TX_TIMEOUT         1000
#define VRFB_NUM_BUFS		4

/* Max buffer size to be allocated during init */
#define OMAP_VOUT_MAX_BUF_SIZE (VID_MAX_WIDTH*VID_MAX_HEIGHT*4)

enum dma_channel_state {
	DMA_CHAN_NOT_ALLOTED,
	DMA_CHAN_ALLOTED,
};

/* Enum for Rotation
 * DSS understands rotation in 0, 1, 2, 3 context
 * while V4L2 driver understands it as 0, 90, 180, 270
 */
enum dss_rotation {
	dss_rotation_0_degree	= 0,
	dss_rotation_90_degree	= 1,
	dss_rotation_180_degree	= 2,
	dss_rotation_270_degree = 3,
};

/* Enum for choosing rotation type for vout
 * DSS2 doesn't understand no rotation as an
 * option while V4L2 driver doesn't support
 * rotation in the case where VRFB is not built in
 * the kernel
 */
enum vout_rotaion_type {
	VOUT_ROT_NONE	= 0,
	VOUT_ROT_VRFB	= 1,
};

/*
 * This structure is used to store the DMA transfer parameters
 * for VRFB hidden buffer
 */
struct vid_vrfb_dma {
	struct dma_chan *chan;
	struct dma_interleaved_template *xt;

	int req_status;
	int tx_status;
	wait_queue_head_t wait;
};

struct omapvideo_info {
	int id;
	int num_overlays;
	struct omap_overlay *overlays[MAX_OVLS];
	enum vout_rotaion_type rotation_type;
};

struct omap2video_device {
	struct mutex  mtx;

	int state;

	struct v4l2_device v4l2_dev;
	struct omap_vout_device *vouts[MAX_VOUT_DEV];

	int num_displays;
	struct omap_dss_device *displays[MAX_DISPLAYS];
	int num_overlays;
	struct omap_overlay *overlays[MAX_OVLS];
	int num_managers;
	struct omap_overlay_manager *managers[MAX_MANAGERS];
};

/* buffer for one video frame */
struct omap_vout_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer		vbuf;
	struct list_head		queue;
};

static inline struct omap_vout_buffer *vb2_to_omap_vout_buffer(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	return container_of(vbuf, struct omap_vout_buffer, vbuf);
}

/* per-device data structure */
struct omap_vout_device {

	struct omapvideo_info vid_info;
	struct video_device *vfd;
	struct omap2video_device *vid_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	int vid;

	/* allow to reuse previously allocated buffer which is big enough */
	int buffer_size;
	enum omap_color_mode dss_mode;

	u32 sequence;

	struct v4l2_pix_format pix;
	struct v4l2_rect crop;
	struct v4l2_window win;
	struct v4l2_framebuffer fbuf;

	/* Lock to protect the shared data structures in ioctl */
	struct mutex lock;

	enum dss_rotation rotation;
	bool mirror;
	int flicker_filter;

	int bpp; /* bytes per pixel */
	int vrfb_bpp; /* bytes per pixel with respect to VRFB */

	struct vid_vrfb_dma vrfb_dma_tx;
	unsigned int smsshado_phy_addr[MAC_VRFB_CTXS];
	unsigned int smsshado_virt_addr[MAC_VRFB_CTXS];
	struct vrfb vrfb_context[MAC_VRFB_CTXS];
	bool vrfb_static_allocation;
	unsigned int smsshado_size;
	unsigned char pos;

	int ps, vr_ps, line_length, first_int, field_id;
	struct omap_vout_buffer *cur_frm, *next_frm;
	spinlock_t vbq_lock;            /* spinlock for dma_queue */
	struct list_head dma_queue;
	dma_addr_t queued_buf_addr[VIDEO_MAX_FRAME];
	u32 cropped_offset;
	s32 tv_field1_offset;
	void *isr_handle;
	struct vb2_queue vq;

};

/*
 * Return true if rotation is 90 or 270
 */
static inline int is_rotation_90_or_270(const struct omap_vout_device *vout)
{
	return (vout->rotation == dss_rotation_90_degree ||
			vout->rotation == dss_rotation_270_degree);
}

/*
 * Return true if rotation is enabled
 */
static inline int is_rotation_enabled(const struct omap_vout_device *vout)
{
	return vout->rotation || vout->mirror;
}

/*
 * Reverse the rotation degree if mirroring is enabled
 */
static inline int calc_rotation(const struct omap_vout_device *vout)
{
	if (!vout->mirror)
		return vout->rotation;

	switch (vout->rotation) {
	case dss_rotation_90_degree:
		return dss_rotation_270_degree;
	case dss_rotation_270_degree:
		return dss_rotation_90_degree;
	case dss_rotation_180_degree:
		return dss_rotation_0_degree;
	default:
		return dss_rotation_180_degree;
	}
}

void omap_vout_free_buffers(struct omap_vout_device *vout);
#endif	/* ifndef OMAP_VOUTDEF_H */
