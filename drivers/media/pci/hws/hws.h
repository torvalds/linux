/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HWS_PCIE_H
#define HWS_PCIE_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/sizes.h>
#include <linux/atomic.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/videobuf2-dma-sg.h>

#include "hws_reg.h"

struct hwsmem_param {
	u32 index;
	u32 type;
	u32 status;
};

struct hws_pix_state {
	u32 width;
	u32 height;
	u32 fourcc;		/* V4L2_PIX_FMT_* (YUYV only here) */
	u32 bytesperline;	/* stride */
	u32 sizeimage;		/* full frame */
	enum v4l2_field field;	/* V4L2_FIELD_NONE or INTERLACED */
	enum v4l2_colorspace colorspace;	/* e.g., REC709 */
	enum v4l2_ycbcr_encoding ycbcr_enc;	/* V4L2_YCBCR_ENC_DEFAULT */
	enum v4l2_quantization quantization;	/* V4L2_QUANTIZATION_LIM_RANGE */
	enum v4l2_xfer_func xfer_func;	/* V4L2_XFER_FUNC_DEFAULT */
	bool interlaced;	/* cached hardware state */
	u32 half_size;		/* hardware half-frame size */
};

#define	UNSET	(-1U)

struct hws_pcie_dev;
struct hws_adapter;
struct hws_video;

struct hwsvideo_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
	int slot;
};

struct hws_video {
	/* Linkage */
	struct hws_pcie_dev *parent;
	struct video_device *video_device;

	struct vb2_queue buffer_queue;
	struct list_head capture_queue;
	struct hwsvideo_buffer *active;
	struct hwsvideo_buffer *next_prepared;

	/* Locking */
	struct mutex state_lock;
	spinlock_t irq_lock;	/* Protects capture_queue and active buffers. */

	/* Indices */
	int channel_index;

	/* Color controls */
	int current_brightness;
	int current_contrast;
	int current_saturation;
	int current_hue;

	/* V4L2 controls */
	struct v4l2_ctrl_handler control_handler;
	struct v4l2_ctrl *ctrl_brightness;
	struct v4l2_ctrl *ctrl_contrast;
	struct v4l2_ctrl *ctrl_saturation;
	struct v4l2_ctrl *ctrl_hue;

	/* Capture queue status */
	struct hws_pix_state pix;
	struct v4l2_dv_timings cur_dv_timings; /* last configured/notified DV timings */
	u32 current_fps; /* Hz, updated by mode changes, not by read-only queries */

	/* Per-channel capture state */
	bool cap_active;
	bool stop_requested;
	u8 last_buf_half_toggle;
	bool half_seen;
	atomic_t sequence_number;
	u32 queued_count;

	/* Timeout and error handling */
	u32 timeout_count;
	u32 error_count;

	bool window_valid;
	u32 last_dma_hi;
	u32 last_dma_page;
	u32 last_pci_addr;
	u32 last_half16;

	/* Misc counters */
	int signal_loss_cnt;
};

static inline void hws_set_current_dv_timings(struct hws_video *vid,
					      u32 width, u32 height,
					      bool interlaced)
{
	if (!vid)
		return;

	vid->cur_dv_timings = (struct v4l2_dv_timings) {
		.type = V4L2_DV_BT_656_1120,
		.bt = {
			.width = width,
			.height = height,
			.interlaced = interlaced,
		},
	};
}

struct hws_scratch_dma {
	void *cpu;
	dma_addr_t dma;
	size_t size;
};

struct hws_pcie_dev {
	/* Core objects */
	struct pci_dev *pdev;
	struct hws_video video[MAX_VID_CHANNELS];

	/* BAR and workqueues */
	void __iomem *bar0_base;

	/* Device identity and capabilities */
	u16 vendor_id;
	u16 device_id;
	u16 device_ver;
	u16 hw_ver;
	u32 sub_ver;
	u32 port_id;
	/* Tri-state support flag used by set_video_format_size(). */
	u32 support_yv12;
	u32 max_hw_video_buf_sz;
	u8 max_channels;
	u8 cur_max_video_ch;
	bool start_run;

	bool buf_allocated;

	/* V4L2 framework objects */
	struct v4l2_device v4l2_device;

	/* Kernel thread */
	struct task_struct *main_task;
	struct hws_scratch_dma scratch_vid[MAX_VID_CHANNELS];

	bool suspended;
	int irq;

	/* Error flags */
	int pci_lost;
};

#endif
