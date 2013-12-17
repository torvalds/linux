/*
 * Samsung TV Mixer driver
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * Tomasz Stanislawski, <t.stanislaws@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundiation. either version 2 of the License,
 * or (at your option) any later version
 */

#ifndef SAMSUNG_MIXER_H
#define SAMSUNG_MIXER_H

#ifdef CONFIG_VIDEO_SAMSUNG_S5P_MIXER_DEBUG
	#define DEBUG
#endif

#include <linux/fb.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-core.h>

#include "regs-mixer.h"

/** maximum number of output interfaces */
#define MXR_MAX_OUTPUTS 2
/** maximum number of input interfaces (layers) */
#define MXR_MAX_LAYERS 3
#define MXR_DRIVER_NAME "s5p-mixer"
/** maximal number of planes for every layer */
#define MXR_MAX_PLANES	2

#define MXR_ENABLE 1
#define MXR_DISABLE 0

/** description of a macroblock for packed formats */
struct mxr_block {
	/** vertical number of pixels in macroblock */
	unsigned int width;
	/** horizontal number of pixels in macroblock */
	unsigned int height;
	/** size of block in bytes */
	unsigned int size;
};

/** description of supported format */
struct mxr_format {
	/** format name/mnemonic */
	const char *name;
	/** fourcc identifier */
	u32 fourcc;
	/** colorspace identifier */
	enum v4l2_colorspace colorspace;
	/** number of planes in image data */
	int num_planes;
	/** description of block for each plane */
	struct mxr_block plane[MXR_MAX_PLANES];
	/** number of subframes in image data */
	int num_subframes;
	/** specifies to which subframe belong given plane */
	int plane2subframe[MXR_MAX_PLANES];
	/** internal code, driver dependent */
	unsigned long cookie;
};

/** description of crop configuration for image */
struct mxr_crop {
	/** width of layer in pixels */
	unsigned int full_width;
	/** height of layer in pixels */
	unsigned int full_height;
	/** horizontal offset of first pixel to be displayed */
	unsigned int x_offset;
	/** vertical offset of first pixel to be displayed */
	unsigned int y_offset;
	/** width of displayed data in pixels */
	unsigned int width;
	/** height of displayed data in pixels */
	unsigned int height;
	/** indicate which fields are present in buffer */
	unsigned int field;
};

/** stages of geometry operations */
enum mxr_geometry_stage {
	MXR_GEOMETRY_SINK,
	MXR_GEOMETRY_COMPOSE,
	MXR_GEOMETRY_CROP,
	MXR_GEOMETRY_SOURCE,
};

/* flag indicating that offset should be 0 */
#define MXR_NO_OFFSET	0x80000000

/** description of transformation from source to destination image */
struct mxr_geometry {
	/** cropping for source image */
	struct mxr_crop src;
	/** cropping for destination image */
	struct mxr_crop dst;
	/** layer-dependant description of horizontal scaling */
	unsigned int x_ratio;
	/** layer-dependant description of vertical scaling */
	unsigned int y_ratio;
};

/** instance of a buffer */
struct mxr_buffer {
	/** common v4l buffer stuff -- must be first */
	struct vb2_buffer	vb;
	/** node for layer's lists */
	struct list_head	list;
};


/** internal states of layer */
enum mxr_layer_state {
	/** layers is not shown */
	MXR_LAYER_IDLE = 0,
	/** layer is shown */
	MXR_LAYER_STREAMING,
	/** state before STREAMOFF is finished */
	MXR_LAYER_STREAMING_FINISH,
};

/** forward declarations */
struct mxr_device;
struct mxr_layer;

/** callback for layers operation */
struct mxr_layer_ops {
	/* TODO: try to port it to subdev API */
	/** handler for resource release function */
	void (*release)(struct mxr_layer *);
	/** setting buffer to HW */
	void (*buffer_set)(struct mxr_layer *, struct mxr_buffer *);
	/** setting format and geometry in HW */
	void (*format_set)(struct mxr_layer *);
	/** streaming stop/start */
	void (*stream_set)(struct mxr_layer *, int);
	/** adjusting geometry */
	void (*fix_geometry)(struct mxr_layer *,
		enum mxr_geometry_stage, unsigned long);
};

/** layer instance, a single window and content displayed on output */
struct mxr_layer {
	/** parent mixer device */
	struct mxr_device *mdev;
	/** layer index (unique identifier) */
	int idx;
	/** callbacks for layer methods */
	struct mxr_layer_ops ops;
	/** format array */
	const struct mxr_format **fmt_array;
	/** size of format array */
	unsigned long fmt_array_size;

	/** lock for protection of list and state fields */
	spinlock_t enq_slock;
	/** list for enqueued buffers */
	struct list_head enq_list;
	/** buffer currently owned by hardware in temporary registers */
	struct mxr_buffer *update_buf;
	/** buffer currently owned by hardware in shadow registers */
	struct mxr_buffer *shadow_buf;
	/** state of layer IDLE/STREAMING */
	enum mxr_layer_state state;

	/** mutex for protection of fields below */
	struct mutex mutex;
	/** handler for video node */
	struct video_device vfd;
	/** queue for output buffers */
	struct vb2_queue vb_queue;
	/** current image format */
	const struct mxr_format *fmt;
	/** current geometry of image */
	struct mxr_geometry geo;
};

/** description of mixers output interface */
struct mxr_output {
	/** name of output */
	char name[32];
	/** output subdev */
	struct v4l2_subdev *sd;
	/** cookie used for configuration of registers */
	int cookie;
};

/** specify source of output subdevs */
struct mxr_output_conf {
	/** name of output (connector) */
	char *output_name;
	/** name of module that generates output subdev */
	char *module_name;
	/** cookie need for mixer HW */
	int cookie;
};

struct clk;
struct regulator;

/** auxiliary resources used my mixer */
struct mxr_resources {
	/** interrupt index */
	int irq;
	/** pointer to Mixer registers */
	void __iomem *mxr_regs;
	/** pointer to Video Processor registers */
	void __iomem *vp_regs;
	/** other resources, should used under mxr_device.mutex */
	struct clk *mixer;
	struct clk *vp;
	struct clk *sclk_mixer;
	struct clk *sclk_hdmi;
	struct clk *sclk_dac;
};

/* event flags used  */
enum mxr_devide_flags {
	MXR_EVENT_VSYNC = 0,
	MXR_EVENT_TOP = 1,
};

/** drivers instance */
struct mxr_device {
	/** master device */
	struct device *dev;
	/** state of each layer */
	struct mxr_layer *layer[MXR_MAX_LAYERS];
	/** state of each output */
	struct mxr_output *output[MXR_MAX_OUTPUTS];
	/** number of registered outputs */
	int output_cnt;

	/* video resources */

	/** V4L2 device */
	struct v4l2_device v4l2_dev;
	/** context of allocator */
	void *alloc_ctx;
	/** event wait queue */
	wait_queue_head_t event_queue;
	/** state flags */
	unsigned long event_flags;

	/** spinlock for protection of registers */
	spinlock_t reg_slock;

	/** mutex for protection of fields below */
	struct mutex mutex;
	/** number of entities depndant on output configuration */
	int n_output;
	/** number of users that do streaming */
	int n_streamer;
	/** index of current output */
	int current_output;
	/** auxiliary resources used my mixer */
	struct mxr_resources res;
};

/** transform device structure into mixer device */
static inline struct mxr_device *to_mdev(struct device *dev)
{
	struct v4l2_device *vdev = dev_get_drvdata(dev);
	return container_of(vdev, struct mxr_device, v4l2_dev);
}

/** get current output data, should be called under mdev's mutex */
static inline struct mxr_output *to_output(struct mxr_device *mdev)
{
	return mdev->output[mdev->current_output];
}

/** get current output subdev, should be called under mdev's mutex */
static inline struct v4l2_subdev *to_outsd(struct mxr_device *mdev)
{
	struct mxr_output *out = to_output(mdev);
	return out ? out->sd : NULL;
}

/** forward declaration for mixer platform data */
struct mxr_platform_data;

/** acquiring common video resources */
int mxr_acquire_video(struct mxr_device *mdev,
	struct mxr_output_conf *output_cont, int output_count);

/** releasing common video resources */
void mxr_release_video(struct mxr_device *mdev);

struct mxr_layer *mxr_graph_layer_create(struct mxr_device *mdev, int idx);
struct mxr_layer *mxr_vp_layer_create(struct mxr_device *mdev, int idx);
struct mxr_layer *mxr_base_layer_create(struct mxr_device *mdev,
	int idx, char *name, struct mxr_layer_ops *ops);

void mxr_base_layer_release(struct mxr_layer *layer);
void mxr_layer_release(struct mxr_layer *layer);

int mxr_base_layer_register(struct mxr_layer *layer);
void mxr_base_layer_unregister(struct mxr_layer *layer);

unsigned long mxr_get_plane_size(const struct mxr_block *blk,
	unsigned int width, unsigned int height);

/** adds new consumer for mixer's power */
int __must_check mxr_power_get(struct mxr_device *mdev);
/** removes consumer for mixer's power */
void mxr_power_put(struct mxr_device *mdev);
/** add new client for output configuration */
void mxr_output_get(struct mxr_device *mdev);
/** removes new client for output configuration */
void mxr_output_put(struct mxr_device *mdev);
/** add new client for streaming */
void mxr_streamer_get(struct mxr_device *mdev);
/** removes new client for streaming */
void mxr_streamer_put(struct mxr_device *mdev);
/** returns format of data delivared to current output */
void mxr_get_mbus_fmt(struct mxr_device *mdev,
	struct v4l2_mbus_framefmt *mbus_fmt);

/* Debug */

#define mxr_err(mdev, fmt, ...)  dev_err(mdev->dev, fmt, ##__VA_ARGS__)
#define mxr_warn(mdev, fmt, ...) dev_warn(mdev->dev, fmt, ##__VA_ARGS__)
#define mxr_info(mdev, fmt, ...) dev_info(mdev->dev, fmt, ##__VA_ARGS__)

#ifdef CONFIG_VIDEO_SAMSUNG_S5P_MIXER_DEBUG
	#define mxr_dbg(mdev, fmt, ...)  dev_dbg(mdev->dev, fmt, ##__VA_ARGS__)
#else
	#define mxr_dbg(mdev, fmt, ...)  do { (void) mdev; } while (0)
#endif

/* accessing Mixer's and Video Processor's registers */

void mxr_vsync_set_update(struct mxr_device *mdev, int en);
void mxr_reg_reset(struct mxr_device *mdev);
irqreturn_t mxr_irq_handler(int irq, void *dev_data);
void mxr_reg_s_output(struct mxr_device *mdev, int cookie);
void mxr_reg_streamon(struct mxr_device *mdev);
void mxr_reg_streamoff(struct mxr_device *mdev);
int mxr_reg_wait4vsync(struct mxr_device *mdev);
void mxr_reg_set_mbus_fmt(struct mxr_device *mdev,
	struct v4l2_mbus_framefmt *fmt);
void mxr_reg_graph_layer_stream(struct mxr_device *mdev, int idx, int en);
void mxr_reg_graph_buffer(struct mxr_device *mdev, int idx, dma_addr_t addr);
void mxr_reg_graph_format(struct mxr_device *mdev, int idx,
	const struct mxr_format *fmt, const struct mxr_geometry *geo);

void mxr_reg_vp_layer_stream(struct mxr_device *mdev, int en);
void mxr_reg_vp_buffer(struct mxr_device *mdev,
	dma_addr_t luma_addr[2], dma_addr_t chroma_addr[2]);
void mxr_reg_vp_format(struct mxr_device *mdev,
	const struct mxr_format *fmt, const struct mxr_geometry *geo);
void mxr_reg_dump(struct mxr_device *mdev);

#endif /* SAMSUNG_MIXER_H */

