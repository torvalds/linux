/*
 * vivid-vbi-gen.h - vbi generator support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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
