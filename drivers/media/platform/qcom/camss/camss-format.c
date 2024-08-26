// SPDX-License-Identifier: GPL-2.0
/*
 * camss-format.c
 *
 * Qualcomm MSM Camera Subsystem - Format helpers
 *
 * Copyright (c) 2023, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Technologies, Inc.
 */
#include <linux/bug.h>
#include <linux/errno.h>

#include "camss-format.h"

/*
 * camss_format_get_bpp - Map media bus format to bits per pixel
 * @formats: supported media bus formats array
 * @nformats: size of @formats array
 * @code: media bus format code
 *
 * Return number of bits per pixel
 */
u8 camss_format_get_bpp(const struct camss_format_info *formats, unsigned int nformats, u32 code)
{
	unsigned int i;

	for (i = 0; i < nformats; i++)
		if (code == formats[i].code)
			return formats[i].mbus_bpp;

	WARN(1, "Unknown format\n");

	return formats[0].mbus_bpp;
}

/*
 * camss_format_find_code - Find a format code in an array
 * @code: a pointer to media bus format codes array
 * @n_code: size of @code array
 * @index: index of code in the array
 * @req_code: required code
 *
 * Return media bus format code
 */
u32 camss_format_find_code(u32 *code, unsigned int n_code, unsigned int index, u32 req_code)
{
	unsigned int i;

	if (!req_code && index >= n_code)
		return 0;

	for (i = 0; i < n_code; i++) {
		if (req_code) {
			if (req_code == code[i])
				return req_code;
		} else {
			if (i == index)
				return code[i];
		}
	}

	return code[0];
}

/*
 * camss_format_find_format - Find a format in an array
 * @code: media bus format code
 * @pixelformat: V4L2 pixel format FCC identifier
 * @formats: a pointer to formats array
 * @nformats: size of @formats array
 *
 * Return index of a format or a negative error code otherwise
 */
int camss_format_find_format(u32 code, u32 pixelformat, const struct camss_format_info *formats,
			     unsigned int nformats)
{
	unsigned int i;

	for (i = 0; i < nformats; i++) {
		if (formats[i].code == code &&
		    formats[i].pixelformat == pixelformat)
			return i;
	}

	for (i = 0; i < nformats; i++) {
		if (formats[i].code == code)
			return i;
	}

	return -EINVAL;
}
