/* SPDX-License-Identifier: GPL-2.0 */
/*
 * camss-format.h
 *
 * Qualcomm MSM Camera Subsystem - Format helpers
 *
 * Copyright (c) 2023, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Technologies, Inc.
 */
#ifndef __CAMSS_FORMAT_H__
#define __CAMSS_FORMAT_H__

#include <linux/types.h>

#define PER_PLANE_DATA(plane, h_fract_num, h_fract_den, v_fract_num, v_fract_den, _bpp)         \
	.hsub[(plane)].numerator	= (h_fract_num),                                        \
	.hsub[(plane)].denominator	= (h_fract_den),                                        \
	.vsub[(plane)].numerator	= (v_fract_num),                                        \
	.vsub[(plane)].denominator	= (v_fract_den),                                        \
	.bpp[(plane)]			= (_bpp)

/*
 * struct fract - Represents a fraction
 * @numerator: Store the numerator part of the fraction
 * @denominator: Store the denominator part of the fraction
 */
struct fract {
	u8 numerator;
	u8 denominator;
};

/*
 * struct camss_format_info - ISP media bus format information
 * @code: V4L2 media bus format code
 * @mbus_bpp: Media bus bits per pixel
 * @pixelformat: V4L2 pixel format FCC identifier
 * @planes: Number of planes
 * @hsub: Horizontal subsampling (for each plane)
 * @vsub: Vertical subsampling (for each plane)
 * @bpp: Bits per pixel when stored in memory (for each plane)
 */
struct camss_format_info {
	u32 code;
	u32 mbus_bpp;
	u32 pixelformat;
	u8 planes;
	struct fract hsub[3];
	struct fract vsub[3];
	unsigned int bpp[3];
};

struct camss_formats {
	unsigned int nformats;
	const struct camss_format_info *formats;
};

u8 camss_format_get_bpp(const struct camss_format_info *formats, unsigned int nformats, u32 code);
u32 camss_format_find_code(u32 *code, unsigned int n_code, unsigned int index, u32 req_code);
int camss_format_find_format(u32 code, u32 pixelformat, const struct camss_format_info *formats,
			     unsigned int nformats);

#endif /* __CAMSS_FORMAT_H__ */
