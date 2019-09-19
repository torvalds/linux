/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Driver for Renesas R-Car VIN
 *
 * Copyright (C) 2016 Renesas Electronics Corp.
 * Copyright (C) 2011-2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Cogent Embedded, Inc., <source@cogentembedded.com>
 * Copyright (C) 2008 Magnus Damm
 *
 * Based on the soc-camera rcar_vin driver
 */

#ifndef __RCAR_VIN__
#define __RCAR_VIN__

#include <linux/kref.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

/* Number of HW buffers */
#define HW_BUFFER_NUM 3

/* Address alignment mask for HW buffers */
#define HW_BUFFER_MASK 0x7f

/* Max number on VIN instances that can be in a system */
#define RCAR_VIN_NUM 8

struct rvin_group;

enum model_id {
	RCAR_H1,
	RCAR_M1,
	RCAR_GEN2,
	RCAR_GEN3,
};

enum rvin_csi_id {
	RVIN_CSI20,
	RVIN_CSI21,
	RVIN_CSI40,
	RVIN_CSI41,
	RVIN_CSI_MAX,
};

/**
 * STOPPED  - No operation in progress
 * STARTING - Capture starting up
 * RUNNING  - Operation in progress have buffers
 * STOPPING - Stopping operation
 */
enum rvin_dma_state {
	STOPPED = 0,
	STARTING,
	RUNNING,
	STOPPING,
};

/**
 * struct rvin_video_format - Data format stored in memory
 * @fourcc:	Pixelformat
 * @bpp:	Bytes per pixel
 */
struct rvin_video_format {
	u32 fourcc;
	u8 bpp;
};

/**
 * struct rvin_parallel_entity - Parallel video input endpoint descriptor
 * @asd:	sub-device descriptor for async framework
 * @subdev:	subdevice matched using async framework
 * @mbus_type:	media bus type
 * @mbus_flags:	media bus configuration flags
 * @source_pad:	source pad of remote subdevice
 * @sink_pad:	sink pad of remote subdevice
 *
 */
struct rvin_parallel_entity {
	struct v4l2_async_subdev asd;
	struct v4l2_subdev *subdev;

	enum v4l2_mbus_type mbus_type;
	unsigned int mbus_flags;

	unsigned int source_pad;
	unsigned int sink_pad;
};

/**
 * struct rvin_group_route - describes a route from a channel of a
 *	CSI-2 receiver to a VIN
 *
 * @csi:	CSI-2 receiver ID.
 * @channel:	Output channel of the CSI-2 receiver.
 * @vin:	VIN ID.
 * @mask:	Bitmask of the different CHSEL register values that
 *		allow for a route from @csi + @chan to @vin.
 *
 * .. note::
 *	Each R-Car CSI-2 receiver has four output channels facing the VIN
 *	devices, each channel can carry one CSI-2 Virtual Channel (VC).
 *	There is no correlation between channel number and CSI-2 VC. It's
 *	up to the CSI-2 receiver driver to configure which VC is output
 *	on which channel, the VIN devices only care about output channels.
 *
 *	There are in some cases multiple CHSEL register settings which would
 *	allow for the same route from @csi + @channel to @vin. For example
 *	on R-Car H3 both the CHSEL values 0 and 3 allow for a route from
 *	CSI40/VC0 to VIN0. All possible CHSEL values for a route need to be
 *	recorded as a bitmask in @mask, in this example bit 0 and 3 should
 *	be set.
 */
struct rvin_group_route {
	enum rvin_csi_id csi;
	unsigned int channel;
	unsigned int vin;
	unsigned int mask;
};

/**
 * struct rvin_info - Information about the particular VIN implementation
 * @model:		VIN model
 * @use_mc:		use media controller instead of controlling subdevice
 * @max_width:		max input width the VIN supports
 * @max_height:		max input height the VIN supports
 * @routes:		list of possible routes from the CSI-2 recivers to
 *			all VINs. The list mush be NULL terminated.
 */
struct rvin_info {
	enum model_id model;
	bool use_mc;

	unsigned int max_width;
	unsigned int max_height;
	const struct rvin_group_route *routes;
};

/**
 * struct rvin_dev - Renesas VIN device structure
 * @dev:		(OF) device
 * @base:		device I/O register space remapped to virtual memory
 * @info:		info about VIN instance
 *
 * @vdev:		V4L2 video device associated with VIN
 * @v4l2_dev:		V4L2 device
 * @ctrl_handler:	V4L2 control handler
 * @notifier:		V4L2 asynchronous subdevs notifier
 *
 * @parallel:		parallel input subdevice descriptor
 *
 * @group:		Gen3 CSI group
 * @id:			Gen3 group id for this VIN
 * @pad:		media pad for the video device entity
 *
 * @lock:		protects @queue
 * @queue:		vb2 buffers queue
 * @scratch:		cpu address for scratch buffer
 * @scratch_phys:	physical address of the scratch buffer
 *
 * @qlock:		protects @queue_buf, @buf_list, @sequence
 *			@state
 * @queue_buf:		Keeps track of buffers given to HW slot
 * @buf_list:		list of queued buffers
 * @sequence:		V4L2 buffers sequence number
 * @state:		keeps track of operation state
 *
 * @is_csi:		flag to mark the VIN as using a CSI-2 subdevice
 *
 * @mbus_code:		media bus format code
 * @format:		active V4L2 pixel format
 *
 * @crop:		active cropping
 * @compose:		active composing
 * @source:		active size of the video source
 * @std:		active video standard of the video source
 *
 * @alpha:		Alpha component to fill in for supported pixel formats
 */
struct rvin_dev {
	struct device *dev;
	void __iomem *base;
	const struct rvin_info *info;

	struct video_device vdev;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_async_notifier notifier;

	struct rvin_parallel_entity *parallel;

	struct rvin_group *group;
	unsigned int id;
	struct media_pad pad;

	struct mutex lock;
	struct vb2_queue queue;
	void *scratch;
	dma_addr_t scratch_phys;

	spinlock_t qlock;
	struct vb2_v4l2_buffer *queue_buf[HW_BUFFER_NUM];
	struct list_head buf_list;
	unsigned int sequence;
	enum rvin_dma_state state;

	bool is_csi;

	u32 mbus_code;
	struct v4l2_pix_format format;

	struct v4l2_rect crop;
	struct v4l2_rect compose;
	struct v4l2_rect source;
	v4l2_std_id std;

	unsigned int alpha;
};

#define vin_to_source(vin)		((vin)->parallel->subdev)

/* Debug */
#define vin_dbg(d, fmt, arg...)		dev_dbg(d->dev, fmt, ##arg)
#define vin_info(d, fmt, arg...)	dev_info(d->dev, fmt, ##arg)
#define vin_warn(d, fmt, arg...)	dev_warn(d->dev, fmt, ##arg)
#define vin_err(d, fmt, arg...)		dev_err(d->dev, fmt, ##arg)

/**
 * struct rvin_group - VIN CSI2 group information
 * @refcount:		number of VIN instances using the group
 *
 * @mdev:		media device which represents the group
 *
 * @lock:		protects the count, notifier, vin and csi members
 * @count:		number of enabled VIN instances found in DT
 * @notifier:		group notifier for CSI-2 async subdevices
 * @vin:		VIN instances which are part of the group
 * @csi:		array of pairs of fwnode and subdev pointers
 *			to all CSI-2 subdevices.
 */
struct rvin_group {
	struct kref refcount;

	struct media_device mdev;

	struct mutex lock;
	unsigned int count;
	struct v4l2_async_notifier notifier;
	struct rvin_dev *vin[RCAR_VIN_NUM];

	struct {
		struct fwnode_handle *fwnode;
		struct v4l2_subdev *subdev;
	} csi[RVIN_CSI_MAX];
};

int rvin_dma_register(struct rvin_dev *vin, int irq);
void rvin_dma_unregister(struct rvin_dev *vin);

int rvin_v4l2_register(struct rvin_dev *vin);
void rvin_v4l2_unregister(struct rvin_dev *vin);

const struct rvin_video_format *rvin_format_from_pixel(struct rvin_dev *vin,
						       u32 pixelformat);


/* Cropping, composing and scaling */
void rvin_crop_scale_comp(struct rvin_dev *vin);

int rvin_set_channel_routing(struct rvin_dev *vin, u8 chsel);
void rvin_set_alpha(struct rvin_dev *vin, unsigned int alpha);

#endif
