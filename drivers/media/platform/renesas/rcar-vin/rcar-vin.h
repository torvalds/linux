/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Driver for Renesas R-Car VIN
 *
 * Copyright (C) 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 * Copyright (C) 2016 Renesas Electronics Corp.
 * Copyright (C) 2011-2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Cogent Embedded, Inc., <source@cogentembedded.com>
 * Copyright (C) 2008 Magnus Damm
 */

#ifndef __RCAR_VIN__
#define __RCAR_VIN__

#include <linux/kref.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/videobuf2-v4l2.h>

/* Number of HW buffers */
#define HW_BUFFER_NUM 3

/* Address alignment mask for HW buffers */
#define HW_BUFFER_MASK 0x7f

/* Max number on VIN instances that can be in a system */
#define RCAR_VIN_NUM 32

struct rvin_dev;
struct rvin_group;

enum model_id {
	RCAR_H1,
	RCAR_M1,
	RCAR_GEN2,
	RCAR_GEN3,
	RCAR_GEN4,
};

enum rvin_csi_id {
	RVIN_CSI20,
	RVIN_CSI21,
	RVIN_CSI40,
	RVIN_CSI41,
	RVIN_CSI_MAX,
};

enum rvin_isp_id {
	RVIN_ISP0,
	RVIN_ISP1,
	RVIN_ISP2,
	RVIN_ISP4,
	RVIN_ISP_MAX,
};

#define RVIN_REMOTES_MAX \
	(((unsigned int)RVIN_CSI_MAX) > ((unsigned int)RVIN_ISP_MAX) ? \
	 (unsigned int)RVIN_CSI_MAX : (unsigned int)RVIN_ISP_MAX)

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
 * @asc:	async connection descriptor for async framework
 * @subdev:	subdevice matched using async framework
 * @mbus_type:	media bus type
 * @bus:	media bus parallel configuration
 * @source_pad:	source pad of remote subdevice
 */
struct rvin_parallel_entity {
	struct v4l2_async_connection *asc;
	struct v4l2_subdev *subdev;

	enum v4l2_mbus_type mbus_type;
	struct v4l2_mbus_config_parallel bus;

	unsigned int source_pad;
};

/**
 * struct rvin_group_route - describes a route from a channel of a
 *	CSI-2 receiver to a VIN
 *
 * @master:	VIN group master ID.
 * @csi:	CSI-2 receiver ID.
 * @chsel:	CHSEL register values that connects VIN group to CSI-2.
 *
 * .. note::
 *	Each R-Car CSI-2 receiver has four output channels facing the VIN
 *	devices, each channel can carry one CSI-2 Virtual Channel (VC).
 *	There is no correlation between channel number and CSI-2 VC. It's
 *	up to the CSI-2 receiver driver to configure which VC is output
 *	on which channel, the VIN devices only care about output channels.
 */
struct rvin_group_route {
	unsigned int master;
	enum rvin_csi_id csi;
	unsigned int chsel;
};

/**
 * struct rvin_info - Information about the particular VIN implementation
 * @model:		VIN model
 * @use_isp:		the VIN is connected to the ISP and not to the CSI-2
 * @nv12:		support outputting NV12 pixel format
 * @raw10:		support outputting RAW10 pixel format
 * @max_width:		max input width the VIN supports
 * @max_height:		max input height the VIN supports
 * @routes:		list of possible routes from the CSI-2 recivers to
 *			all VINs. The list mush be NULL terminated.
 * @scaler:		Optional scaler
 */
struct rvin_info {
	enum model_id model;
	bool use_isp;
	bool nv12;
	bool raw10;

	unsigned int max_width;
	unsigned int max_height;
	const struct rvin_group_route *routes;
	void (*scaler)(struct rvin_dev *vin);
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
 * @qlock:		Protects @buf_hw, @buf_list, @sequence and @running
 * @buf_hw:		Keeps track of buffers given to HW slot
 * @buf_list:		list of queued buffers
 * @sequence:		V4L2 buffers sequence number
 * @running:		Keeps track of if the VIN is running
 *
 * @is_csi:		flag to mark the VIN as using a CSI-2 subdevice
 * @chsel:		Cached value of the current CSI-2 channel selection
 *
 * @mbus_code:		media bus format code
 * @format:		active V4L2 pixel format
 *
 * @crop:		active cropping
 * @compose:		active composing
 * @scaler:		Optional scaler
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

	struct rvin_parallel_entity parallel;

	struct rvin_group *group;
	unsigned int id;
	struct media_pad pad;

	struct mutex lock;
	struct vb2_queue queue;
	void *scratch;
	dma_addr_t scratch_phys;

	spinlock_t qlock;
	struct {
		struct vb2_v4l2_buffer *buffer;
		dma_addr_t phys;
	} buf_hw[HW_BUFFER_NUM];
	struct list_head buf_list;
	unsigned int sequence;
	bool running;

	bool is_csi;
	unsigned int chsel;

	u32 mbus_code;
	struct v4l2_pix_format format;

	struct v4l2_rect crop;
	struct v4l2_rect compose;
	void (*scaler)(struct rvin_dev *vin);

	unsigned int alpha;
};

#define vin_to_source(vin)		((vin)->parallel.subdev)

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
 * @notifier:		group notifier for CSI-2 async connections
 * @info:		Platform dependent information about the VIN instances
 * @vin:		VIN instances which are part of the group
 * @link_setup:		Callback to create all links for the media graph
 * @remotes:		array of pairs of async connection and subdev pointers
 *			to all remote subdevices.
 */
struct rvin_group {
	struct kref refcount;

	struct media_device mdev;

	struct mutex lock;
	unsigned int count;
	struct v4l2_async_notifier notifier;
	const struct rvin_info *info;
	struct rvin_dev *vin[RCAR_VIN_NUM];

	int (*link_setup)(struct rvin_group *group);

	struct {
		struct v4l2_async_connection *asc;
		struct v4l2_subdev *subdev;
	} remotes[RVIN_REMOTES_MAX];
};

int rvin_dma_register(struct rvin_dev *vin, int irq);
void rvin_dma_unregister(struct rvin_dev *vin);

int rvin_v4l2_register(struct rvin_dev *vin);
void rvin_v4l2_unregister(struct rvin_dev *vin);

const struct rvin_video_format *rvin_format_from_pixel(struct rvin_dev *vin,
						       u32 pixelformat);


/* Cropping, composing and scaling */
void rvin_scaler_gen2(struct rvin_dev *vin);
void rvin_scaler_gen3(struct rvin_dev *vin);
void rvin_crop_scale_comp(struct rvin_dev *vin);

int rvin_set_channel_routing(struct rvin_dev *vin, u8 chsel);
void rvin_set_alpha(struct rvin_dev *vin, unsigned int alpha);

int rvin_start_streaming(struct rvin_dev *vin);
void rvin_stop_streaming(struct rvin_dev *vin);

#endif
