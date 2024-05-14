/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * vimc-common.h Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
 */

#ifndef _VIMC_COMMON_H_
#define _VIMC_COMMON_H_

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <media/media-device.h>
#include <media/v4l2-device.h>

#define VIMC_PDEV_NAME "vimc"

/* VIMC-specific controls */
#define VIMC_CID_VIMC_BASE		(0x00f00000 | 0xf000)
#define VIMC_CID_VIMC_CLASS		(0x00f00000 | 1)
#define VIMC_CID_TEST_PATTERN		(VIMC_CID_VIMC_BASE + 0)
#define VIMC_CID_MEAN_WIN_SIZE		(VIMC_CID_VIMC_BASE + 1)
#define VIMC_CID_OSD_TEXT_MODE		(VIMC_CID_VIMC_BASE + 2)

#define VIMC_FRAME_MAX_WIDTH 4096
#define VIMC_FRAME_MAX_HEIGHT 2160
#define VIMC_FRAME_MIN_WIDTH 16
#define VIMC_FRAME_MIN_HEIGHT 16

#define VIMC_FRAME_INDEX(lin, col, width, bpp) ((lin * width + col) * bpp)

/* Source and sink pad checks */
#define VIMC_IS_SRC(pad)	(pad)
#define VIMC_IS_SINK(pad)	(!(pad))

#define VIMC_PIX_FMT_MAX_CODES 8

extern unsigned int vimc_allocator;

enum vimc_allocator_type {
	VIMC_ALLOCATOR_VMALLOC = 0,
	VIMC_ALLOCATOR_DMA_CONTIG = 1,
};

/**
 * vimc_colorimetry_clamp - Adjust colorimetry parameters
 *
 * @fmt:		the pointer to struct v4l2_pix_format or
 *			struct v4l2_mbus_framefmt
 *
 * Entities must check if colorimetry given by the userspace is valid, if not
 * then set them as DEFAULT
 */
#define vimc_colorimetry_clamp(fmt)					\
do {									\
	if ((fmt)->colorspace == V4L2_COLORSPACE_DEFAULT		\
	    || (fmt)->colorspace > V4L2_COLORSPACE_DCI_P3) {		\
		(fmt)->colorspace = V4L2_COLORSPACE_DEFAULT;		\
		(fmt)->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;		\
		(fmt)->quantization = V4L2_QUANTIZATION_DEFAULT;	\
		(fmt)->xfer_func = V4L2_XFER_FUNC_DEFAULT;		\
	}								\
	if ((fmt)->ycbcr_enc > V4L2_YCBCR_ENC_SMPTE240M)		\
		(fmt)->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;		\
	if ((fmt)->quantization > V4L2_QUANTIZATION_LIM_RANGE)		\
		(fmt)->quantization = V4L2_QUANTIZATION_DEFAULT;	\
	if ((fmt)->xfer_func > V4L2_XFER_FUNC_SMPTE2084)		\
		(fmt)->xfer_func = V4L2_XFER_FUNC_DEFAULT;		\
} while (0)

/**
 * struct vimc_pix_map - maps media bus code with v4l2 pixel format
 *
 * @code:		media bus format code defined by MEDIA_BUS_FMT_* macros
 * @bpp:		number of bytes each pixel occupies
 * @pixelformat:	pixel format defined by V4L2_PIX_FMT_* macros
 * @bayer:		true if this is a bayer format
 *
 * Struct which matches the MEDIA_BUS_FMT_* codes with the corresponding
 * V4L2_PIX_FMT_* fourcc pixelformat and its bytes per pixel (bpp)
 */
struct vimc_pix_map {
	unsigned int code[VIMC_PIX_FMT_MAX_CODES];
	unsigned int bpp;
	u32 pixelformat;
	bool bayer;
};

/**
 * struct vimc_ent_device - core struct that represents an entity in the
 * topology
 *
 * @dev:		a pointer of the device struct of the driver
 * @ent:		the pointer to struct media_entity for the node
 * @process_frame:	callback send a frame to that node
 * @vdev_get_format:	callback that returns the current format a pad, used
 *			only when is_media_entity_v4l2_video_device(ent) returns
 *			true
 *
 * Each node of the topology must create a vimc_ent_device struct. Depending on
 * the node it will be of an instance of v4l2_subdev or video_device struct
 * where both contains a struct media_entity.
 * Those structures should embedded the vimc_ent_device struct through
 * v4l2_set_subdevdata() and video_set_drvdata() respectively, allowing the
 * vimc_ent_device struct to be retrieved from the corresponding struct
 * media_entity
 */
struct vimc_ent_device {
	struct device *dev;
	struct media_entity *ent;
	void * (*process_frame)(struct vimc_ent_device *ved,
				const void *frame);
	void (*vdev_get_format)(struct vimc_ent_device *ved,
			      struct v4l2_pix_format *fmt);
};

/**
 * struct vimc_device - main device for vimc driver
 *
 * @pipe_cfg:	pointer to the vimc pipeline configuration structure
 * @ent_devs:	array of vimc_ent_device pointers
 * @mdev:	the associated media_device parent
 * @v4l2_dev:	Internal v4l2 parent device
 */
struct vimc_device {
	const struct vimc_pipeline_config *pipe_cfg;
	struct vimc_ent_device **ent_devs;
	struct media_device mdev;
	struct v4l2_device v4l2_dev;
};

/**
 * struct vimc_ent_type		Structure for the callbacks of the entity types
 *
 *
 * @add:			initializes and registers
 *				vimc entity - called from vimc-core
 * @unregister:			unregisters vimc entity - called from vimc-core
 * @release:			releases vimc entity - called from the v4l2_dev
 *				release callback
 */
struct vimc_ent_type {
	struct vimc_ent_device *(*add)(struct vimc_device *vimc,
				       const char *vcfg_name);
	void (*unregister)(struct vimc_ent_device *ved);
	void (*release)(struct vimc_ent_device *ved);
};

/**
 * struct vimc_ent_config	Structure which describes individual
 *				configuration for each entity
 *
 * @name:			entity name
 * @type:			contain the callbacks of this entity type
 *
 */
struct vimc_ent_config {
	const char *name;
	struct vimc_ent_type *type;
};

/**
 * vimc_is_source - returns true if the entity has only source pads
 *
 * @ent: pointer to &struct media_entity
 *
 */
bool vimc_is_source(struct media_entity *ent);

extern struct vimc_ent_type vimc_sensor_type;
extern struct vimc_ent_type vimc_debayer_type;
extern struct vimc_ent_type vimc_scaler_type;
extern struct vimc_ent_type vimc_capture_type;
extern struct vimc_ent_type vimc_lens_type;

/**
 * vimc_pix_map_by_index - get vimc_pix_map struct by its index
 *
 * @i:			index of the vimc_pix_map struct in vimc_pix_map_list
 */
const struct vimc_pix_map *vimc_pix_map_by_index(unsigned int i);

/**
 * vimc_mbus_code_by_index - get mbus code by its index
 *
 * @index:		index of the mbus code in vimc_pix_map_list
 *
 * Returns 0 if no mbus code is found for the given index.
 */
u32 vimc_mbus_code_by_index(unsigned int index);

/**
 * vimc_pix_map_by_code - get vimc_pix_map struct by media bus code
 *
 * @code:		media bus format code defined by MEDIA_BUS_FMT_* macros
 */
const struct vimc_pix_map *vimc_pix_map_by_code(u32 code);

/**
 * vimc_pix_map_by_pixelformat - get vimc_pix_map struct by v4l2 pixel format
 *
 * @pixelformat:	pixel format defined by V4L2_PIX_FMT_* macros
 */
const struct vimc_pix_map *vimc_pix_map_by_pixelformat(u32 pixelformat);

/**
 * vimc_ent_sd_register - initialize and register a subdev node
 *
 * @ved:	the vimc_ent_device struct to be initialize
 * @sd:		the v4l2_subdev struct to be initialize and registered
 * @v4l2_dev:	the v4l2 device to register the v4l2_subdev
 * @name:	name of the sub-device. Please notice that the name must be
 *		unique.
 * @function:	media entity function defined by MEDIA_ENT_F_* macros
 * @num_pads:	number of pads to initialize
 * @pads:	the array of pads of the entity, the caller should set the
 *		flags of the pads
 * @sd_ops:	pointer to &struct v4l2_subdev_ops.
 *
 * Helper function initialize and register the struct vimc_ent_device and struct
 * v4l2_subdev which represents a subdev node in the topology
 */
int vimc_ent_sd_register(struct vimc_ent_device *ved,
			 struct v4l2_subdev *sd,
			 struct v4l2_device *v4l2_dev,
			 const char *const name,
			 u32 function,
			 u16 num_pads,
			 struct media_pad *pads,
			 const struct v4l2_subdev_ops *sd_ops);

/**
 * vimc_vdev_link_validate - validates a media link
 *
 * @link: pointer to &struct media_link
 *
 * This function calls validates if a media link is valid for streaming.
 */
int vimc_vdev_link_validate(struct media_link *link);

#endif
