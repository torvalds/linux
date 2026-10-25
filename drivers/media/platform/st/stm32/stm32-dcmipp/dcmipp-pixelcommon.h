/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Driver for STM32 Digital Camera Memory Interface Pixel Processor
 *
 * Copyright (C) STMicroelectronics SA 2026
 * Authors: Hugues Fruchet <hugues.fruchet@foss.st.com>
 *          Alain Volmat <alain.volmat@foss.st.com>
 *          for STMicroelectronics.
 */

#ifndef _DCMIPP_PIXELCOMMON_H
#define _DCMIPP_PIXELCOMMON_H

#define IS_SINK(pad) (!(pad))
#define IS_SRC(pad)  ((pad))

#define DCMIPP_ISP		0
#define DCMIPP_MAIN		1
#define DCMIPP_AUX		2

struct dcmipp_pixelpipe_pix_map {
	__u32 code;
	__u32 pipes;
};

const struct dcmipp_pixelpipe_pix_map *
dcmipp_pixelpipe_pix_map_by_code(__u32 code, unsigned int id, unsigned int pad);

int dcmipp_pixelpipe_enum_mbus_code(unsigned int id,
				    struct v4l2_subdev_mbus_code_enum *code);

int dcmipp_pixelpipe_enum_frame_size(unsigned int id,
				     struct v4l2_subdev_frame_size_enum *fse);

int dcmipp_pixelpipe_get_selection(struct v4l2_subdev *sd,
				   const struct v4l2_subdev_client_info *ci,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_selection *s);

__u32 dcmipp_pixelpipe_src_format(__u32 input_format);

#endif
