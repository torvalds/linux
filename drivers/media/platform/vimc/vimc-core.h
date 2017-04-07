/*
 * vimc-core.h Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _VIMC_CORE_H_
#define _VIMC_CORE_H_

#include <linux/slab.h>
#include <media/v4l2-device.h>

/**
 * struct vimc_pix_map - maps media bus code with v4l2 pixel format
 *
 * @code:		media bus format code defined by MEDIA_BUS_FMT_* macros
 * @bbp:		number of bytes each pixel occupies
 * @pixelformat:	pixel format devined by V4L2_PIX_FMT_* macros
 *
 * Struct which matches the MEDIA_BUS_FMT_* codes with the corresponding
 * V4L2_PIX_FMT_* fourcc pixelformat and its bytes per pixel (bpp)
 */
struct vimc_pix_map {
	unsigned int code;
	unsigned int bpp;
	u32 pixelformat;
};

/**
 * struct vimc_ent_device - core struct that represents a node in the topology
 *
 * @ent:		the pointer to struct media_entity for the node
 * @pads:		the list of pads of the node
 * @destroy:		callback to destroy the node
 * @process_frame:	callback send a frame to that node
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
	void (*destroy)(struct vimc_ent_device *);
	void (*process_frame)(struct vimc_ent_device *ved,
			      struct media_pad *sink, const void *frame);
};

/**
 * vimc_propagate_frame - propagate a frame through the topology
 *
 * @src:	the source pad where the frame is being originated
 * @frame:	the frame to be propagated
 *
 * This function will call the process_frame callback from the vimc_ent_device
 * struct of the nodes directly connected to the @src pad
 */
int vimc_propagate_frame(struct media_pad *src, const void *frame);

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
 * vimc_pix_map_by_code - get vimc_pix_map struct by media bus code
 *
 * @code:		media bus format code defined by MEDIA_BUS_FMT_* macros
 */
const struct vimc_pix_map *vimc_pix_map_by_code(u32 code);

/**
 * vimc_pix_map_by_pixelformat - get vimc_pix_map struct by v4l2 pixel format
 *
 * @pixelformat:	pixel format devined by V4L2_PIX_FMT_* macros
 */
const struct vimc_pix_map *vimc_pix_map_by_pixelformat(u32 pixelformat);

#endif
