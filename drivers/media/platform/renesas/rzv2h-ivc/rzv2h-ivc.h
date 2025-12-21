/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Renesas RZ/V2H(P) Input Video Control Block driver
 *
 * Copyright (C) 2025 Ideas on Board Oy
 */

#include <linux/clk.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>

#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>

#define RZV2H_IVC_REG_AXIRX_PLNUM			0x0000
#define RZV2H_IVC_ONE_EXPOSURE				0x00
#define RZV2H_IVC_TWO_EXPOSURE				0x01
#define RZV2H_IVC_REG_AXIRX_PXFMT			0x0004
#define RZV2H_IVC_INPUT_FMT_MIPI			(0 << 16)
#define RZV2H_IVC_INPUT_FMT_CRU_PACKED			BIT(16)
#define RZV2H_IVC_PXFMT_DTYPE				GENMASK(7, 0)
#define RZV2H_IVC_REG_AXIRX_SADDL_P0			0x0010
#define RZV2H_IVC_REG_AXIRX_SADDH_P0			0x0014
#define RZV2H_IVC_REG_AXIRX_SADDL_P1			0x0018
#define RZV2H_IVC_REG_AXIRX_SADDH_P1			0x001c
#define RZV2H_IVC_REG_AXIRX_HSIZE			0x0020
#define RZV2H_IVC_REG_AXIRX_VSIZE			0x0024
#define RZV2H_IVC_REG_AXIRX_BLANK			0x0028
#define RZV2H_IVC_VBLANK(x)				((x) << 16)
#define RZV2H_IVC_REG_AXIRX_STRD			0x0030
#define RZV2H_IVC_REG_AXIRX_ISSU			0x0040
#define RZV2H_IVC_REG_AXIRX_ERACT			0x0048
#define RZV2H_IVC_REG_FM_CONTEXT			0x0100
#define RZV2H_IVC_SOFTWARE_CFG				0x00
#define RZV2H_IVC_SINGLE_CONTEXT_SW_HW_CFG		BIT(0)
#define RZV2H_IVC_MULTI_CONTEXT_SW_HW_CFG		BIT(1)
#define RZV2H_IVC_REG_FM_MCON				0x0104
#define RZV2H_IVC_REG_FM_FRCON				0x0108
#define RZV2H_IVC_REG_FM_STOP				0x010c
#define RZV2H_IVC_REG_FM_INT_EN				0x0120
#define RZV2H_IVC_VVAL_IFPE				BIT(0)
#define RZV2H_IVC_REG_FM_INT_STA			0x0124
#define RZV2H_IVC_REG_AXIRX_FIFOCAP0			0x0208
#define RZV2H_IVC_REG_CORE_CAPCON			0x020c
#define RZV2H_IVC_REG_CORE_FIFOCAP0			0x0228
#define RZV2H_IVC_REG_CORE_FIFOCAP1			0x022c

#define RZV2H_IVC_MIN_WIDTH				640
#define RZV2H_IVC_MAX_WIDTH				4096
#define RZV2H_IVC_MIN_HEIGHT				480
#define RZV2H_IVC_MAX_HEIGHT				4096
#define RZV2H_IVC_DEFAULT_WIDTH				1920
#define RZV2H_IVC_DEFAULT_HEIGHT			1080

#define RZV2H_IVC_NUM_HW_RESOURCES			3

struct device;

enum rzv2h_ivc_subdev_pads {
	RZV2H_IVC_SUBDEV_SINK_PAD,
	RZV2H_IVC_SUBDEV_SOURCE_PAD,
	RZV2H_IVC_NUM_SUBDEV_PADS
};

struct rzv2h_ivc_format {
	u32 fourcc;
	/*
	 * The CRU packed pixel formats are bayer-order agnostic, so each could
	 * support any one of the 4 possible media bus formats.
	 */
	u32 mbus_codes[4];
	u8 dtype;
};

struct rzv2h_ivc {
	struct device *dev;
	void __iomem *base;
	struct clk_bulk_data clks[RZV2H_IVC_NUM_HW_RESOURCES];
	struct reset_control_bulk_data resets[RZV2H_IVC_NUM_HW_RESOURCES];
	int irqnum;
	u8 vvalid_ifp;

	struct {
		struct video_device dev;
		struct vb2_queue vb2q;
		struct media_pad pad;
	} vdev;

	struct {
		struct v4l2_subdev sd;
		struct media_pad pads[RZV2H_IVC_NUM_SUBDEV_PADS];
	} subdev;

	struct {
		/* Spinlock to guard buffer queue */
		spinlock_t lock;
		struct workqueue_struct *async_wq;
		struct work_struct work;
		struct list_head queue;
		struct rzv2h_ivc_buf *curr;
		unsigned int sequence;
	} buffers;

	struct {
		struct v4l2_pix_format_mplane pix;
		const struct rzv2h_ivc_format *fmt;
	} format;

	/* Mutex to provide to vb2 */
	struct mutex lock;
	/* Lock to protect the interrupt counter */
	spinlock_t spinlock;
};

int rzv2h_ivc_init_vdev(struct rzv2h_ivc *ivc, struct v4l2_device *v4l2_dev);
void rzv2h_deinit_video_dev_and_queue(struct rzv2h_ivc *ivc);
void rzv2h_ivc_buffer_done(struct rzv2h_ivc *ivc);
int rzv2h_ivc_initialise_subdevice(struct rzv2h_ivc *ivc);
void rzv2h_ivc_deinit_subdevice(struct rzv2h_ivc *ivc);
void rzv2h_ivc_write(struct rzv2h_ivc *ivc, u32 addr, u32 val);
void rzv2h_ivc_update_bits(struct rzv2h_ivc *ivc, unsigned int addr,
			   u32 mask, u32 val);
