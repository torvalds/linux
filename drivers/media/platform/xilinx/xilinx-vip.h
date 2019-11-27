/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Video IP Core
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef __XILINX_VIP_H__
#define __XILINX_VIP_H__

#include <linux/bitops.h>
#include <linux/io.h>
#include <media/v4l2-subdev.h>

struct clk;

/*
 * Minimum and maximum width and height common to most video IP cores. IP
 * cores with different requirements must define their own values.
 */
#define XVIP_MIN_WIDTH			32
#define XVIP_MAX_WIDTH			7680
#define XVIP_MIN_HEIGHT			32
#define XVIP_MAX_HEIGHT			7680

/*
 * Pad IDs. IP cores with with multiple inputs or outputs should define
 * their own values.
 */
#define XVIP_PAD_SINK			0
#define XVIP_PAD_SOURCE			1

/* Xilinx Video IP Control Registers */
#define XVIP_CTRL_CONTROL			0x0000
#define XVIP_CTRL_CONTROL_SW_ENABLE		BIT(0)
#define XVIP_CTRL_CONTROL_REG_UPDATE		BIT(1)
#define XVIP_CTRL_CONTROL_BYPASS		BIT(4)
#define XVIP_CTRL_CONTROL_TEST_PATTERN		BIT(5)
#define XVIP_CTRL_CONTROL_FRAME_SYNC_RESET	BIT(30)
#define XVIP_CTRL_CONTROL_SW_RESET		BIT(31)
#define XVIP_CTRL_STATUS			0x0004
#define XVIP_CTRL_STATUS_PROC_STARTED		BIT(0)
#define XVIP_CTRL_STATUS_EOF			BIT(1)
#define XVIP_CTRL_ERROR				0x0008
#define XVIP_CTRL_ERROR_SLAVE_EOL_EARLY		BIT(0)
#define XVIP_CTRL_ERROR_SLAVE_EOL_LATE		BIT(1)
#define XVIP_CTRL_ERROR_SLAVE_SOF_EARLY		BIT(2)
#define XVIP_CTRL_ERROR_SLAVE_SOF_LATE		BIT(3)
#define XVIP_CTRL_IRQ_ENABLE			0x000c
#define XVIP_CTRL_IRQ_ENABLE_PROC_STARTED	BIT(0)
#define XVIP_CTRL_IRQ_EOF			BIT(1)
#define XVIP_CTRL_VERSION			0x0010
#define XVIP_CTRL_VERSION_MAJOR_MASK		(0xff << 24)
#define XVIP_CTRL_VERSION_MAJOR_SHIFT		24
#define XVIP_CTRL_VERSION_MINOR_MASK		(0xff << 16)
#define XVIP_CTRL_VERSION_MINOR_SHIFT		16
#define XVIP_CTRL_VERSION_REVISION_MASK		(0xf << 12)
#define XVIP_CTRL_VERSION_REVISION_SHIFT	12
#define XVIP_CTRL_VERSION_PATCH_MASK		(0xf << 8)
#define XVIP_CTRL_VERSION_PATCH_SHIFT		8
#define XVIP_CTRL_VERSION_INTERNAL_MASK		(0xff << 0)
#define XVIP_CTRL_VERSION_INTERNAL_SHIFT	0

/* Xilinx Video IP Timing Registers */
#define XVIP_ACTIVE_SIZE			0x0020
#define XVIP_ACTIVE_VSIZE_MASK			(0x7ff << 16)
#define XVIP_ACTIVE_VSIZE_SHIFT			16
#define XVIP_ACTIVE_HSIZE_MASK			(0x7ff << 0)
#define XVIP_ACTIVE_HSIZE_SHIFT			0
#define XVIP_ENCODING				0x0028
#define XVIP_ENCODING_NBITS_8			(0 << 4)
#define XVIP_ENCODING_NBITS_10			(1 << 4)
#define XVIP_ENCODING_NBITS_12			(2 << 4)
#define XVIP_ENCODING_NBITS_16			(3 << 4)
#define XVIP_ENCODING_NBITS_MASK		(3 << 4)
#define XVIP_ENCODING_NBITS_SHIFT		4
#define XVIP_ENCODING_VIDEO_FORMAT_YUV422	(0 << 0)
#define XVIP_ENCODING_VIDEO_FORMAT_YUV444	(1 << 0)
#define XVIP_ENCODING_VIDEO_FORMAT_RGB		(2 << 0)
#define XVIP_ENCODING_VIDEO_FORMAT_YUV420	(3 << 0)
#define XVIP_ENCODING_VIDEO_FORMAT_MASK		(3 << 0)
#define XVIP_ENCODING_VIDEO_FORMAT_SHIFT	0

/**
 * struct xvip_device - Xilinx Video IP device structure
 * @subdev: V4L2 subdevice
 * @dev: (OF) device
 * @iomem: device I/O register space remapped to kernel virtual memory
 * @clk: video core clock
 * @saved_ctrl: saved control register for resume / suspend
 */
struct xvip_device {
	struct v4l2_subdev subdev;
	struct device *dev;
	void __iomem *iomem;
	struct clk *clk;
	u32 saved_ctrl;
};

/**
 * struct xvip_video_format - Xilinx Video IP video format description
 * @vf_code: AXI4 video format code
 * @width: AXI4 format width in bits per component
 * @pattern: CFA pattern for Mono/Sensor formats
 * @code: media bus format code
 * @bpp: bytes per pixel (when stored in memory)
 * @fourcc: V4L2 pixel format FCC identifier
 */
struct xvip_video_format {
	unsigned int vf_code;
	unsigned int width;
	const char *pattern;
	unsigned int code;
	unsigned int bpp;
	u32 fourcc;
};

const struct xvip_video_format *xvip_get_format_by_code(unsigned int code);
const struct xvip_video_format *xvip_get_format_by_fourcc(u32 fourcc);
const struct xvip_video_format *xvip_of_get_format(struct device_node *node);
void xvip_set_format_size(struct v4l2_mbus_framefmt *format,
			  const struct v4l2_subdev_format *fmt);
int xvip_enum_mbus_code(struct v4l2_subdev *subdev,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_mbus_code_enum *code);
int xvip_enum_frame_size(struct v4l2_subdev *subdev,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_frame_size_enum *fse);

static inline u32 xvip_read(struct xvip_device *xvip, u32 addr)
{
	return ioread32(xvip->iomem + addr);
}

static inline void xvip_write(struct xvip_device *xvip, u32 addr, u32 value)
{
	iowrite32(value, xvip->iomem + addr);
}

static inline void xvip_clr(struct xvip_device *xvip, u32 addr, u32 clr)
{
	xvip_write(xvip, addr, xvip_read(xvip, addr) & ~clr);
}

static inline void xvip_set(struct xvip_device *xvip, u32 addr, u32 set)
{
	xvip_write(xvip, addr, xvip_read(xvip, addr) | set);
}

void xvip_clr_or_set(struct xvip_device *xvip, u32 addr, u32 mask, bool set);
void xvip_clr_and_set(struct xvip_device *xvip, u32 addr, u32 clr, u32 set);

int xvip_init_resources(struct xvip_device *xvip);
void xvip_cleanup_resources(struct xvip_device *xvip);

static inline void xvip_reset(struct xvip_device *xvip)
{
	xvip_write(xvip, XVIP_CTRL_CONTROL, XVIP_CTRL_CONTROL_SW_RESET);
}

static inline void xvip_start(struct xvip_device *xvip)
{
	xvip_set(xvip, XVIP_CTRL_CONTROL,
		 XVIP_CTRL_CONTROL_SW_ENABLE | XVIP_CTRL_CONTROL_REG_UPDATE);
}

static inline void xvip_stop(struct xvip_device *xvip)
{
	xvip_clr(xvip, XVIP_CTRL_CONTROL, XVIP_CTRL_CONTROL_SW_ENABLE);
}

static inline void xvip_resume(struct xvip_device *xvip)
{
	xvip_write(xvip, XVIP_CTRL_CONTROL,
		   xvip->saved_ctrl | XVIP_CTRL_CONTROL_SW_ENABLE);
}

static inline void xvip_suspend(struct xvip_device *xvip)
{
	xvip->saved_ctrl = xvip_read(xvip, XVIP_CTRL_CONTROL);
	xvip_write(xvip, XVIP_CTRL_CONTROL,
		   xvip->saved_ctrl & ~XVIP_CTRL_CONTROL_SW_ENABLE);
}

static inline void xvip_set_frame_size(struct xvip_device *xvip,
				       const struct v4l2_mbus_framefmt *format)
{
	xvip_write(xvip, XVIP_ACTIVE_SIZE,
		   (format->height << XVIP_ACTIVE_VSIZE_SHIFT) |
		   (format->width << XVIP_ACTIVE_HSIZE_SHIFT));
}

static inline void xvip_get_frame_size(struct xvip_device *xvip,
				       struct v4l2_mbus_framefmt *format)
{
	u32 reg;

	reg = xvip_read(xvip, XVIP_ACTIVE_SIZE);
	format->width = (reg & XVIP_ACTIVE_HSIZE_MASK) >>
			XVIP_ACTIVE_HSIZE_SHIFT;
	format->height = (reg & XVIP_ACTIVE_VSIZE_MASK) >>
			 XVIP_ACTIVE_VSIZE_SHIFT;
}

static inline void xvip_enable_reg_update(struct xvip_device *xvip)
{
	xvip_set(xvip, XVIP_CTRL_CONTROL, XVIP_CTRL_CONTROL_REG_UPDATE);
}

static inline void xvip_disable_reg_update(struct xvip_device *xvip)
{
	xvip_clr(xvip, XVIP_CTRL_CONTROL, XVIP_CTRL_CONTROL_REG_UPDATE);
}

static inline void xvip_print_version(struct xvip_device *xvip)
{
	u32 version;

	version = xvip_read(xvip, XVIP_CTRL_VERSION);

	dev_info(xvip->dev, "device found, version %u.%02x%x\n",
		 ((version & XVIP_CTRL_VERSION_MAJOR_MASK) >>
		  XVIP_CTRL_VERSION_MAJOR_SHIFT),
		 ((version & XVIP_CTRL_VERSION_MINOR_MASK) >>
		  XVIP_CTRL_VERSION_MINOR_SHIFT),
		 ((version & XVIP_CTRL_VERSION_REVISION_MASK) >>
		  XVIP_CTRL_VERSION_REVISION_SHIFT));
}

#endif /* __XILINX_VIP_H__ */
