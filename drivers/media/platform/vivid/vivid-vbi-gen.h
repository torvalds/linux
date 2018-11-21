/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-vbi-gen.h - vbi generator support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _VIVID_VBI_GEN_H_
#define _VIVID_VBI_GEN_H_

struct vivid_vbi_gen_data {
	struct v4l2_sliced_vbi_data data[25];
	u8 time_of_day_packet[16];
};

void vivid_vbi_gen_sliced(struct vivid_vbi_gen_data *vbi,
		bool is_60hz, unsigned seqnr);
void vivid_vbi_gen_raw(const struct vivid_vbi_gen_data *vbi,
		const struct v4l2_vbi_format *vbi_fmt, u8 *buf);

#endif
