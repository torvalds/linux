/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __TAG_GLOBAL_H_INCLUDED__
#define __TAG_GLOBAL_H_INCLUDED__

/* offsets for encoding/decoding the tag into an uint32_t */

#define TAG_CAP	1
#define TAG_EXP	2

#define TAG_NUM_CAPTURES_SIGN_SHIFT	 6
#define TAG_OFFSET_SIGN_SHIFT		 7
#define TAG_NUM_CAPTURES_SHIFT		 8
#define TAG_OFFSET_SHIFT		16
#define TAG_SKIP_SHIFT			24

#define TAG_EXP_ID_SHIFT		 8

/* Data structure containing the tagging information which is used in
 * continuous mode to specify which frames should be captured.
 * num_captures		The number of RAW frames to be processed to
 *                      YUV. Setting this to -1 will make continuous
 *                      capture run until it is stopped.
 * skip			Skip N frames in between captures. This can be
 *                      used to select a slower capture frame rate than
 *                      the sensor output frame rate.
 * offset		Start the RAW-to-YUV processing at RAW buffer
 *                      with this offset. This allows the user to
 *                      process RAW frames that were captured in the
 *                      past or future.
 * exp_id		Exposure id of the RAW frame to tag.
 *
 * NOTE: Either exp_id = 0 or all other fields are 0
 *	 (so yeah, this could be a union)
 */

struct sh_css_tag_descr {
	int num_captures;
	unsigned int skip;
	int offset;
	unsigned int exp_id;
};

#endif /* __TAG_GLOBAL_H_INCLUDED__ */
