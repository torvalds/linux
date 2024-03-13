// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-vbi-gen.c - vbi generator support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/string.h>
#include <linux/videodev2.h>

#include "vivid-vbi-gen.h"

static void wss_insert(u8 *wss, u32 val, unsigned size)
{
	while (size--)
		*wss++ = (val & (1 << size)) ? 0xc0 : 0x10;
}

static void vivid_vbi_gen_wss_raw(const struct v4l2_sliced_vbi_data *data,
		u8 *buf, unsigned sampling_rate)
{
	const unsigned rate = 5000000;	/* WSS has a 5 MHz transmission rate */
	u8 wss[29 + 24 + 24 + 24 + 18 + 18] = { 0 };
	const unsigned zero = 0x07;
	const unsigned one = 0x38;
	unsigned bit = 0;
	u16 wss_data;
	int i;

	wss_insert(wss + bit, 0x1f1c71c7, 29); bit += 29;
	wss_insert(wss + bit, 0x1e3c1f, 24); bit += 24;

	wss_data = (data->data[1] << 8) | data->data[0];
	for (i = 0; i <= 13; i++, bit += 6)
		wss_insert(wss + bit, (wss_data & (1 << i)) ? one : zero, 6);

	for (i = 0, bit = 0; bit < sizeof(wss); bit++) {
		unsigned n = ((bit + 1) * sampling_rate) / rate;

		while (i < n)
			buf[i++] = wss[bit];
	}
}

static void vivid_vbi_gen_teletext_raw(const struct v4l2_sliced_vbi_data *data,
		u8 *buf, unsigned sampling_rate)
{
	const unsigned rate = 6937500 / 10;	/* Teletext has a 6.9375 MHz transmission rate */
	u8 teletext[45] = { 0x55, 0x55, 0x27 };
	unsigned bit = 0;
	int i;

	memcpy(teletext + 3, data->data, sizeof(teletext) - 3);
	/* prevents 32 bit overflow */
	sampling_rate /= 10;

	for (i = 0, bit = 0; bit < sizeof(teletext) * 8; bit++) {
		unsigned n = ((bit + 1) * sampling_rate) / rate;
		u8 val = (teletext[bit / 8] & (1 << (bit & 7))) ? 0xc0 : 0x10;

		while (i < n)
			buf[i++] = val;
	}
}

static void cc_insert(u8 *cc, u8 ch)
{
	unsigned tot = 0;
	unsigned i;

	for (i = 0; i < 7; i++) {
		cc[2 * i] = cc[2 * i + 1] = (ch & (1 << i)) ? 1 : 0;
		tot += cc[2 * i];
	}
	cc[14] = cc[15] = !(tot & 1);
}

#define CC_PREAMBLE_BITS (14 + 4 + 2)

static void vivid_vbi_gen_cc_raw(const struct v4l2_sliced_vbi_data *data,
		u8 *buf, unsigned sampling_rate)
{
	const unsigned rate = 1000000;	/* CC has a 1 MHz transmission rate */

	u8 cc[CC_PREAMBLE_BITS + 2 * 16] = {
		/* Clock run-in: 7 cycles */
		0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
		/* 2 cycles of 0 */
		0, 0, 0, 0,
		/* Start bit of 1 (each bit is two cycles) */
		1, 1
	};
	unsigned bit, i;

	cc_insert(cc + CC_PREAMBLE_BITS, data->data[0]);
	cc_insert(cc + CC_PREAMBLE_BITS + 16, data->data[1]);

	for (i = 0, bit = 0; bit < sizeof(cc); bit++) {
		unsigned n = ((bit + 1) * sampling_rate) / rate;

		while (i < n)
			buf[i++] = cc[bit] ? 0xc0 : 0x10;
	}
}

void vivid_vbi_gen_raw(const struct vivid_vbi_gen_data *vbi,
		const struct v4l2_vbi_format *vbi_fmt, u8 *buf)
{
	unsigned idx;

	for (idx = 0; idx < 25; idx++) {
		const struct v4l2_sliced_vbi_data *data = vbi->data + idx;
		unsigned start_2nd_field;
		unsigned line = data->line;
		u8 *linebuf = buf;

		start_2nd_field = (data->id & V4L2_SLICED_VBI_525) ? 263 : 313;
		if (data->field)
			line += start_2nd_field;
		line -= vbi_fmt->start[data->field];

		if (vbi_fmt->flags & V4L2_VBI_INTERLACED)
			linebuf += (line * 2 + data->field) *
				vbi_fmt->samples_per_line;
		else
			linebuf += (line + data->field * vbi_fmt->count[0]) *
				vbi_fmt->samples_per_line;
		if (data->id == V4L2_SLICED_CAPTION_525)
			vivid_vbi_gen_cc_raw(data, linebuf, vbi_fmt->sampling_rate);
		else if (data->id == V4L2_SLICED_WSS_625)
			vivid_vbi_gen_wss_raw(data, linebuf, vbi_fmt->sampling_rate);
		else if (data->id == V4L2_SLICED_TELETEXT_B)
			vivid_vbi_gen_teletext_raw(data, linebuf, vbi_fmt->sampling_rate);
	}
}

static const u8 vivid_cc_sequence1[30] = {
	0x14, 0x20,	/* Resume Caption Loading */
	'H',  'e',
	'l',  'l',
	'o',  ' ',
	'w',  'o',
	'r',  'l',
	'd',  '!',
	0x14, 0x2f,	/* End of Caption */
};

static const u8 vivid_cc_sequence2[30] = {
	0x14, 0x20,	/* Resume Caption Loading */
	'C',  'l',
	'o',  's',
	'e',  'd',
	' ',  'c',
	'a',  'p',
	't',  'i',
	'o',  'n',
	's',  ' ',
	't',  'e',
	's',  't',
	0x14, 0x2f,	/* End of Caption */
};

static u8 calc_parity(u8 val)
{
	unsigned i;
	unsigned tot = 0;

	for (i = 0; i < 7; i++)
		tot += (val & (1 << i)) ? 1 : 0;
	return val | ((tot & 1) ? 0 : 0x80);
}

static void vivid_vbi_gen_set_time_of_day(u8 *packet)
{
	struct tm tm;
	u8 checksum, i;

	time64_to_tm(ktime_get_real_seconds(), 0, &tm);
	packet[0] = calc_parity(0x07);
	packet[1] = calc_parity(0x01);
	packet[2] = calc_parity(0x40 | tm.tm_min);
	packet[3] = calc_parity(0x40 | tm.tm_hour);
	packet[4] = calc_parity(0x40 | tm.tm_mday);
	if (tm.tm_mday == 1 && tm.tm_mon == 2 &&
	    sys_tz.tz_minuteswest > tm.tm_min + tm.tm_hour * 60)
		packet[4] = calc_parity(0x60 | tm.tm_mday);
	packet[5] = calc_parity(0x40 | (1 + tm.tm_mon));
	packet[6] = calc_parity(0x40 | (1 + tm.tm_wday));
	packet[7] = calc_parity(0x40 | ((tm.tm_year - 90) & 0x3f));
	packet[8] = calc_parity(0x0f);
	for (checksum = i = 0; i <= 8; i++)
		checksum += packet[i] & 0x7f;
	packet[9] = calc_parity(0x100 - checksum);
	checksum = 0;
	packet[10] = calc_parity(0x07);
	packet[11] = calc_parity(0x04);
	if (sys_tz.tz_minuteswest >= 0)
		packet[12] = calc_parity(0x40 | ((sys_tz.tz_minuteswest / 60) & 0x1f));
	else
		packet[12] = calc_parity(0x40 | ((24 + sys_tz.tz_minuteswest / 60) & 0x1f));
	packet[13] = calc_parity(0);
	packet[14] = calc_parity(0x0f);
	for (checksum = 0, i = 10; i <= 14; i++)
		checksum += packet[i] & 0x7f;
	packet[15] = calc_parity(0x100 - checksum);
}

static const u8 hamming[16] = {
	0x15, 0x02, 0x49, 0x5e, 0x64, 0x73, 0x38, 0x2f,
	0xd0, 0xc7, 0x8c, 0x9b, 0xa1, 0xb6, 0xfd, 0xea
};

static void vivid_vbi_gen_teletext(u8 *packet, unsigned line, unsigned frame)
{
	unsigned offset = 2;
	unsigned i;

	packet[0] = hamming[1 + ((line & 1) << 3)];
	packet[1] = hamming[line >> 1];
	memset(packet + 2, 0x20, 40);
	if (line == 0) {
		/* subcode */
		packet[2] = hamming[frame % 10];
		packet[3] = hamming[frame / 10];
		packet[4] = hamming[0];
		packet[5] = hamming[0];
		packet[6] = hamming[0];
		packet[7] = hamming[0];
		packet[8] = hamming[0];
		packet[9] = hamming[1];
		offset = 10;
	}
	packet += offset;
	memcpy(packet, "Page: 100 Row: 10", 17);
	packet[7] = '0' + frame / 10;
	packet[8] = '0' + frame % 10;
	packet[15] = '0' + line / 10;
	packet[16] = '0' + line % 10;
	for (i = 0; i < 42 - offset; i++)
		packet[i] = calc_parity(packet[i]);
}

void vivid_vbi_gen_sliced(struct vivid_vbi_gen_data *vbi,
		bool is_60hz, unsigned seqnr)
{
	struct v4l2_sliced_vbi_data *data0 = vbi->data;
	struct v4l2_sliced_vbi_data *data1 = vbi->data + 1;
	unsigned frame = seqnr % 60;

	memset(vbi->data, 0, sizeof(vbi->data));

	if (!is_60hz) {
		unsigned i;

		for (i = 0; i <= 11; i++) {
			data0->id = V4L2_SLICED_TELETEXT_B;
			data0->line = 7 + i;
			vivid_vbi_gen_teletext(data0->data, i, frame);
			data0++;
		}
		data0->id = V4L2_SLICED_WSS_625;
		data0->line = 23;
		/* 4x3 video aspect ratio */
		data0->data[0] = 0x08;
		data0++;
		for (i = 0; i <= 11; i++) {
			data0->id = V4L2_SLICED_TELETEXT_B;
			data0->field = 1;
			data0->line = 7 + i;
			vivid_vbi_gen_teletext(data0->data, 12 + i, frame);
			data0++;
		}
		return;
	}

	data0->id = V4L2_SLICED_CAPTION_525;
	data0->line = 21;
	data1->id = V4L2_SLICED_CAPTION_525;
	data1->field = 1;
	data1->line = 21;

	if (frame < 15) {
		data0->data[0] = calc_parity(vivid_cc_sequence1[2 * frame]);
		data0->data[1] = calc_parity(vivid_cc_sequence1[2 * frame + 1]);
	} else if (frame >= 30 && frame < 45) {
		frame -= 30;
		data0->data[0] = calc_parity(vivid_cc_sequence2[2 * frame]);
		data0->data[1] = calc_parity(vivid_cc_sequence2[2 * frame + 1]);
	} else {
		data0->data[0] = calc_parity(0);
		data0->data[1] = calc_parity(0);
	}

	frame = seqnr % (30 * 60);
	switch (frame) {
	case 0:
		vivid_vbi_gen_set_time_of_day(vbi->time_of_day_packet);
		fallthrough;
	case 1 ... 7:
		data1->data[0] = vbi->time_of_day_packet[frame * 2];
		data1->data[1] = vbi->time_of_day_packet[frame * 2 + 1];
		break;
	default:
		data1->data[0] = calc_parity(0);
		data1->data[1] = calc_parity(0);
		break;
	}
}
