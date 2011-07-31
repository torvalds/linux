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

#include <plat/display.h>

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
#define MAX_DISPLAYS	3
#define MAX_MANAGERS	3

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
/*
 * This structure is used to store the DMA transfer parameters
 * for VRFB hidden buffer
 */
struct vid_vrfb_dma {
	int dev_id;
	int dma_ch;
	int req_status;
	int tx_status;
	wait_queue_head_t wait;
};

struct omapvideo_info {
	int id;
	int num_overlays;
	struct omap_overlay *overlays[MAX_OVLS];
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

/* per-device data structure */
struct omap_vout_device {

	struct omapvideo_info vid_info;
	struct video_device *vfd;
	struct omap2video_device *vid_dev;
	int vid;
	int opened;

	/* we don't allow to change image fmt/size once buffer has
	 * been allocated
	 */
	int buffer_allocated;
	/* allow to reuse previously allocated buffer which is big enough */
	int buffer_size;
	/* keep buffer info across opens */
	unsigned long buf_virt_addr[VIDEO_MAX_FRAME];
	unsigned long buf_phy_addr[VIDEO_MAX_FRAME];
	enum omap_color_mode dss_mode;

	/* we don't allow to request new buffer when old buffers are
	 * still mmaped
	 */
	int mmap_count;

	spinlock_t vbq_lock;		/* spinlock for videobuf queues */
	unsigned long field_count;	/* field counter for videobuf_buffer */

	/* non-NULL means streaming is in progress. */
	bool streaming;

	struct v4l2_pix_format pix;
	struct v4l2_rect crop;
	struct v4l2_window win;
	struct v4l2_framebuffer fbuf;

	/* Lock to protect the shared data structures in ioctl */
	struct mutex lock;

	/* V4L2 control structure for different control id */
	struct v4l2_control control[MAX_CID];
	enum dss_rotation rotation;
	bool mirror;
	int flicker_filter;
	/* V4L2 control structure for different control id */

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
	enum v4l2_memory memory;
	struct videobuf_buffer *cur_frm, *next_frm;
	struct list_head dma_queue;
	u8 *queued_buf_addr[VIDEO_MAX_FRAME];
	u32 cropped_offset;
	s32 tv_field1_offset;
	void *isr_handle;

	/* Buffer queue variables */
	struct omap_vout_device *vout;
	enum v4l2_buf_type type;
	struct videobuf_queue vbq;
	int io_allowed;

};
#endif	/* ifndef OMAP_VOUTDEF_H */
