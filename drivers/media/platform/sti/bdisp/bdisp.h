/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Fabien Dessenne <fabien.dessenne@st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/ktime.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>

#include <media/videobuf2-dma-contig.h>

#define BDISP_NAME              "bdisp"

/*
 *  Max nb of nodes in node-list:
 *   - 2 nodes to handle wide 4K pictures
 *   - 2 nodes to handle two planes (Y & CbCr) */
#define MAX_OUTPUT_PLANES       2
#define MAX_VERTICAL_STRIDES    2
#define MAX_NB_NODE             (MAX_OUTPUT_PLANES * MAX_VERTICAL_STRIDES)

/* struct bdisp_ctrls - bdisp control set
 * @hflip:      horizontal flip
 * @vflip:      vertical flip
 */
struct bdisp_ctrls {
	struct v4l2_ctrl        *hflip;
	struct v4l2_ctrl        *vflip;
};

/**
 * struct bdisp_fmt - driver's internal color format data
 * @pixelformat:fourcc code for this format
 * @nb_planes:  number of planes  (ex: [0]=RGB/Y - [1]=Cb/Cr, ...)
 * @bpp:        bits per pixel (general)
 * @bpp_plane0: byte per pixel for the 1st plane
 * @w_align:    width alignment in pixel (multiple of)
 * @h_align:    height alignment in pixel (multiple of)
 */
struct bdisp_fmt {
	u32                     pixelformat;
	u8                      nb_planes;
	u8                      bpp;
	u8                      bpp_plane0;
	u8                      w_align;
	u8                      h_align;
};

/**
 * struct bdisp_frame - frame properties
 *
 * @width:      frame width (including padding)
 * @height:     frame height (including padding)
 * @fmt:        pointer to frame format descriptor
 * @field:      frame / field type
 * @bytesperline: stride of the 1st plane
 * @sizeimage:  image size in bytes
 * @colorspace: colorspace
 * @crop:       crop area
 * @paddr:      image physical addresses per plane ([0]=RGB/Y - [1]=Cb/Cr, ...)
 */
struct bdisp_frame {
	u32                     width;
	u32                     height;
	const struct bdisp_fmt  *fmt;
	enum v4l2_field         field;
	u32                     bytesperline;
	u32                     sizeimage;
	enum v4l2_colorspace    colorspace;
	struct v4l2_rect        crop;
	dma_addr_t              paddr[4];
};

/**
 * struct bdisp_request - bdisp request
 *
 * @src:        source frame properties
 * @dst:        destination frame properties
 * @hflip:      horizontal flip
 * @vflip:      vertical flip
 * @nb_req:     number of run request
 */
struct bdisp_request {
	struct bdisp_frame      src;
	struct bdisp_frame      dst;
	unsigned int            hflip:1;
	unsigned int            vflip:1;
	int                     nb_req;
};

/**
 * struct bdisp_ctx - device context data
 *
 * @src:        source frame properties
 * @dst:        destination frame properties
 * @state:      flags to keep track of user configuration
 * @hflip:      horizontal flip
 * @vflip:      vertical flip
 * @bdisp_dev:  the device this context applies to
 * @node:       node array
 * @node_paddr: node physical address array
 * @fh:         v4l2 file handle
 * @ctrl_handler: v4l2 controls handler
 * @bdisp_ctrls: bdisp control set
 * @ctrls_rdy:  true if the control handler is initialized
 */
struct bdisp_ctx {
	struct bdisp_frame      src;
	struct bdisp_frame      dst;
	u32                     state;
	unsigned int            hflip:1;
	unsigned int            vflip:1;
	struct bdisp_dev        *bdisp_dev;
	struct bdisp_node       *node[MAX_NB_NODE];
	dma_addr_t              node_paddr[MAX_NB_NODE];
	struct v4l2_fh          fh;
	struct v4l2_ctrl_handler ctrl_handler;
	struct bdisp_ctrls      bdisp_ctrls;
	bool                    ctrls_rdy;
};

/**
 * struct bdisp_m2m_device - v4l2 memory-to-memory device data
 *
 * @vdev:       video device node for v4l2 m2m mode
 * @m2m_dev:    v4l2 m2m device data
 * @ctx:        hardware context data
 * @refcnt:     reference counter
 */
struct bdisp_m2m_device {
	struct video_device     *vdev;
	struct v4l2_m2m_dev     *m2m_dev;
	struct bdisp_ctx        *ctx;
	int                     refcnt;
};

/**
 * struct bdisp_dbg - debug info
 *
 * @debugfs_entry: debugfs
 * @copy_node:     array of last used nodes
 * @copy_request:  last bdisp request
 * @hw_start:      start time of last HW request
 * @last_duration: last HW processing duration in microsecs
 * @min_duration:  min HW processing duration in microsecs
 * @max_duration:  max HW processing duration in microsecs
 * @tot_duration:  total HW processing duration in microsecs
 */
struct bdisp_dbg {
	struct dentry           *debugfs_entry;
	struct bdisp_node       *copy_node[MAX_NB_NODE];
	struct bdisp_request    copy_request;
	ktime_t                 hw_start;
	s64                     last_duration;
	s64                     min_duration;
	s64                     max_duration;
	s64                     tot_duration;
};

/**
 * struct bdisp_dev - abstraction for bdisp entity
 *
 * @v4l2_dev:   v4l2 device
 * @vdev:       video device
 * @pdev:       platform device
 * @dev:        device
 * @lock:       mutex protecting this data structure
 * @slock:      spinlock protecting this data structure
 * @id:         device index
 * @m2m:        memory-to-memory V4L2 device information
 * @state:      flags used to synchronize m2m and capture mode operation
 * @clock:      IP clock
 * @regs:       registers
 * @irq_queue:  interrupt handler waitqueue
 * @work_queue: workqueue to handle timeouts
 * @timeout_work: IRQ timeout structure
 * @dbg:        debug info
 */
struct bdisp_dev {
	struct v4l2_device      v4l2_dev;
	struct video_device     vdev;
	struct platform_device  *pdev;
	struct device           *dev;
	spinlock_t              slock;
	struct mutex            lock;
	u16                     id;
	struct bdisp_m2m_device m2m;
	unsigned long           state;
	struct clk              *clock;
	void __iomem            *regs;
	wait_queue_head_t       irq_queue;
	struct workqueue_struct *work_queue;
	struct delayed_work     timeout_work;
	struct bdisp_dbg        dbg;
};

void bdisp_hw_free_nodes(struct bdisp_ctx *ctx);
int bdisp_hw_alloc_nodes(struct bdisp_ctx *ctx);
void bdisp_hw_free_filters(struct device *dev);
int bdisp_hw_alloc_filters(struct device *dev);
int bdisp_hw_reset(struct bdisp_dev *bdisp);
int bdisp_hw_get_and_clear_irq(struct bdisp_dev *bdisp);
int bdisp_hw_update(struct bdisp_ctx *ctx);

void bdisp_debugfs_remove(struct bdisp_dev *bdisp);
int bdisp_debugfs_create(struct bdisp_dev *bdisp);
void bdisp_dbg_perf_begin(struct bdisp_dev *bdisp);
void bdisp_dbg_perf_end(struct bdisp_dev *bdisp);
