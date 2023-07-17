// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 *         Rick Chang <rick.chang@mediatek.com>
 */

#include <linux/kernel.h>
#include <linux/videodev2.h>
#include <media/jpeg.h>

#include "mtk_jpeg_dec_parse.h"

struct mtk_jpeg_stream {
	u8 *addr;
	u32 size;
	u32 curr;
};

static int read_byte(struct mtk_jpeg_stream *stream)
{
	if (stream->curr >= stream->size)
		return -1;
	return stream->addr[stream->curr++];
}

static int read_word_be(struct mtk_jpeg_stream *stream, u32 *word)
{
	u32 temp;
	int byte;

	byte = read_byte(stream);
	if (byte == -1)
		return -1;
	temp = byte << 8;
	byte = read_byte(stream);
	if (byte == -1)
		return -1;
	*word = (u32)byte | temp;

	return 0;
}

static void read_skip(struct mtk_jpeg_stream *stream, long len)
{
	if (len <= 0)
		return;
	while (len--)
		read_byte(stream);
}

static bool mtk_jpeg_do_parse(struct mtk_jpeg_dec_param *param, u8 *src_addr_va,
			      u32 src_size)
{
	bool notfound = true;
	struct mtk_jpeg_stream stream;

	stream.addr = src_addr_va;
	stream.size = src_size;
	stream.curr = 0;

	while (notfound) {
		int i, length, byte;
		u32 word;

		byte = read_byte(&stream);
		if (byte == -1)
			return false;
		if (byte != 0xff)
			continue;
		do
			byte = read_byte(&stream);
		while (byte == 0xff);
		if (byte == -1)
			return false;
		if (byte == 0)
			continue;

		length = 0;
		switch (byte) {
		case JPEG_MARKER_SOF0:
			/* length */
			if (read_word_be(&stream, &word))
				break;

			/* precision */
			if (read_byte(&stream) == -1)
				break;

			if (read_word_be(&stream, &word))
				break;
			param->pic_h = word;

			if (read_word_be(&stream, &word))
				break;
			param->pic_w = word;

			param->comp_num = read_byte(&stream);
			if (param->comp_num != 1 && param->comp_num != 3)
				break;

			for (i = 0; i < param->comp_num; i++) {
				param->comp_id[i] = read_byte(&stream);
				if (param->comp_id[i] == -1)
					break;

				/* sampling */
				byte = read_byte(&stream);
				if (byte == -1)
					break;
				param->sampling_w[i] = (byte >> 4) & 0x0F;
				param->sampling_h[i] = byte & 0x0F;

				param->qtbl_num[i] = read_byte(&stream);
				if (param->qtbl_num[i] == -1)
					break;
			}

			notfound = !(i == param->comp_num);
			break;
		case JPEG_MARKER_RST ... JPEG_MARKER_RST + 7:
		case JPEG_MARKER_SOI:
		case JPEG_MARKER_EOI:
		case JPEG_MARKER_TEM:
			break;
		default:
			if (read_word_be(&stream, &word))
				break;
			length = (long)word - 2;
			read_skip(&stream, length);
			break;
		}
	}

	return !notfound;
}

bool mtk_jpeg_parse(struct mtk_jpeg_dec_param *param, u8 *src_addr_va,
		    u32 src_size)
{
	if (!mtk_jpeg_do_parse(param, src_addr_va, src_size))
		return false;
	if (mtk_jpeg_dec_fill_param(param))
		return false;

	return true;
}
