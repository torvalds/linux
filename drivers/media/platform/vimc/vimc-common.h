/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * vimc-common.h Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
 */

#ifndef _VIMC_COMMON_H_
#define _VIMC_COMMON_H_

#include <linux/slab.h>
#include <media/media-device.h>
#include <media/v4l2-device.h>

#include "vimc-streamer.h"

#define VIMC_PDEV_NAME "vimc"

/* VIMC-specific controls */
#define VIMC_CID_VIMC_BASE		(0x00f00000 | 0xf000)
#define VIMC_CID_VIMC_CLASS		(0x00f00000 | 1)
#define VIMC_CID_TEST_PATTERN		(VIMC_CID_VIMC_BASE + 0)

#define VIMC_FRAME_MAX_WIDTH 4096
#define VIMC_FRAME_MAX_HEIGHT 2160
#define VIMC_FRAME_MIN_WIDTH 16
#define VIMC_FRAME_MIN_HEIGHT 16

#define VIMC_FRAME_INDEX(lin, col, width, bpp) ((lin * width + col) * bpp)

/**
 * struct vimc_colorimetry_clamp - Adjust colorimetry parameters
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
 * struct vimc_platform_data - platform data to components
 *
 * @entity_name:	The name of the entity to be created
 *
 * Board setup code will often provide additional information using the device's
 * platform_data field to hold additional information.
 * When injecting a new platform_device in the component system the core needs
 * to provide to the corresponding submodules the name of the entity that should
 * be used when registering the subdevice in the Media Controller system.
 */
struct vimc_platform_data {
	char entity_name[32];
};

/**
 * struct vimc_ent_device - core struct that represents a node in the topology
 *
 * @ent:		the pointer to struct media_entity for the node
 * @pads:		the list of pads of the node
 * @process_frame:	callback send a frame to that node
 * @vdev_get_format:	callback that returns the current format a pad, used
 *			only when is_media_entity_v4l2_video_device(ent) returns
 *			true
 *
 * Each node of the topology must create a vimc_ent_device struct. Depending on
 * the node it will be of an instance of v4l2_subdev or video_device struct
 * where both contains a struct media_entity.
 * Those structures should embedded the vimc_ent_device struct through
 * v4l2_set_subdevdata() and video_set_drvdata() respectivaly, allowing the
 * vimc_ent_device struct to be retrieved from the corresponding struct
 * media_entity
 */
struct vimc_ent_device {
	struct media_entity *ent;
	struct media_pad *pads;
	struct vimc_stream *stream;
	void * (*process_frame)(struct vimc_ent_device *ved,
				const void *frame);
	void (*vdev_get_format)(struct vimc_ent_device *ved,
			      struct v4l2_pix_format *fmt);
};

/**
 * vimc_mbus_code_supported - helper to check supported mbus codes
 *
 * Helper function to check if mbus code is enumerated by vimc_enum_mbus_code()
 */
bool vimc_mbus_code_supported(__u32 code);

/**
 * vimc_enum_mbus_code - enumerate mbus codes
 *
 * Helper function to be pluged in .enum_mbus_code from
 * struct v4l2_subdev_pad_ops.
 */
int vimc_enum_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_mbus_code_enum *code);

/**
 * vimc_pads_init - initialize pads
 *
 * @num_pads:	number of pads to initialize
 * @pads_flags:	flags to use in each pad
 *
 * Helper functions to allocate/initialize pads
 */
struct media_pad *vimc_pads_init(u16 num_pads,
				 const unsigned long *pads_flag);

/**
 * vimc_pads_cleanup - free pads
 *
 * @pads: pointer to the pads
 *
 * Helper function to free the pads initialized with vimc_pads_init
 */
static inline void vimc_pads_cleanup(struct media_pad *pads)
{
	kfree(pads);
}

/**
 * vimc_pipeline_s_stream - start stream through the pipeline
 *
 * @ent:		the pointer to struct media_entity for the node
 * @enable:		1 to start the stream and 0 to stop
 *
 * Helper function to call the s_stream of the subdevices connected
 * in all the sink pads of the entity
 */
int vimc_pipeline_s_stream(struct media_entity *ent, int enable);

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
 * @pads_flag:	flags to use in each pad
 * @sd_int_ops:	pointer to &struct v4l2_subdev_internal_ops
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
			 const unsigned long *pads_flag,
			 const struct v4l2_subdev_internal_ops *sd_int_ops,
			 const struct v4l2_subdev_ops *sd_ops);

/**
 * vimc_ent_sd_unregister - cleanup and unregister a subdev node
 *
 * @ved:	the vimc_ent_device struct to be cleaned up
 * @sd:		the v4l2_subdev struct to be unregistered
 *
 * Helper function cleanup and unregister the struct vimc_ent_device and struct
 * v4l2_subdev which represents a subdev node in the topology
 */
void vimc_ent_sd_unregister(struct vimc_ent_device *ved,
			    struct v4l2_subdev *sd);

/**
 * vimc_link_validate - validates a media link
 *
 * @link: pointer to &struct media_link
 *
 * This function calls validates if a media link is valid for streaming.
 */
int vimc_link_validate(struct media_link *link);

#endif
