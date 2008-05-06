/*
 *  cx18 Vertical Blank Interval support functions
 *
 *  Derived from ivtv-vbi.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include "cx18-driver.h"
#include "cx18-vbi.h"
#include "cx18-ioctl.h"
#include "cx18-queue.h"
#include "cx18-av-core.h"

static void copy_vbi_data(struct cx18 *cx, int lines, u32 pts_stamp)
{
	int line = 0;
	int i;
	u32 linemask[2] = { 0, 0 };
	unsigned short size;
	static const u8 mpeg_hdr_data[] = {
		0x00, 0x00, 0x01, 0xba, 0x44, 0x00, 0x0c, 0x66,
		0x24, 0x01, 0x01, 0xd1, 0xd3, 0xfa, 0xff, 0xff,
		0x00, 0x00, 0x01, 0xbd, 0x00, 0x1a, 0x84, 0x80,
		0x07, 0x21, 0x00, 0x5d, 0x63, 0xa7, 0xff, 0xff
	};
	const int sd = sizeof(mpeg_hdr_data);	/* start of vbi data */
	int idx = cx->vbi.frame % CX18_VBI_FRAMES;
	u8 *dst = &cx->vbi.sliced_mpeg_data[idx][0];

	for (i = 0; i < lines; i++) {
		struct v4l2_sliced_vbi_data *sdata = cx->vbi.sliced_data + i;
		int f, l;

		if (sdata->id == 0)
			continue;

		l = sdata->line - 6;
		f = sdata->field;
		if (f)
			l += 18;
		if (l < 32)
			linemask[0] |= (1 << l);
		else
			linemask[1] |= (1 << (l - 32));
		dst[sd + 12 + line * 43] = cx18_service2vbi(sdata->id);
		memcpy(dst + sd + 12 + line * 43 + 1, sdata->data, 42);
		line++;
	}
	memcpy(dst, mpeg_hdr_data, sizeof(mpeg_hdr_data));
	if (line == 36) {
		/* All lines are used, so there is no space for the linemask
		   (the max size of the VBI data is 36 * 43 + 4 bytes).
		   So in this case we use the magic number 'ITV0'. */
		memcpy(dst + sd, "ITV0", 4);
		memcpy(dst + sd + 4, dst + sd + 12, line * 43);
		size = 4 + ((43 * line + 3) & ~3);
	} else {
		memcpy(dst + sd, "cx0", 4);
		memcpy(dst + sd + 4, &linemask[0], 8);
		size = 12 + ((43 * line + 3) & ~3);
	}
	dst[4+16] = (size + 10) >> 8;
	dst[5+16] = (size + 10) & 0xff;
	dst[9+16] = 0x21 | ((pts_stamp >> 29) & 0x6);
	dst[10+16] = (pts_stamp >> 22) & 0xff;
	dst[11+16] = 1 | ((pts_stamp >> 14) & 0xff);
	dst[12+16] = (pts_stamp >> 7) & 0xff;
	dst[13+16] = 1 | ((pts_stamp & 0x7f) << 1);
	cx->vbi.sliced_mpeg_size[idx] = sd + size;
}

/* Compress raw VBI format, removes leading SAV codes and surplus space
   after the field.
   Returns new compressed size. */
static u32 compress_raw_buf(struct cx18 *cx, u8 *buf, u32 size)
{
	u32 line_size = cx->vbi.raw_decoder_line_size;
	u32 lines = cx->vbi.count;
	u8 sav1 = cx->vbi.raw_decoder_sav_odd_field;
	u8 sav2 = cx->vbi.raw_decoder_sav_even_field;
	u8 *q = buf;
	u8 *p;
	int i;

	for (i = 0; i < lines; i++) {
		p = buf + i * line_size;

		/* Look for SAV code */
		if (p[0] != 0xff || p[1] || p[2] ||
		    (p[3] != sav1 && p[3] != sav2))
			break;
		memcpy(q, p + 4, line_size - 4);
		q += line_size - 4;
	}
	return lines * (line_size - 4);
}


/* Compressed VBI format, all found sliced blocks put next to one another
   Returns new compressed size */
static u32 compress_sliced_buf(struct cx18 *cx, u32 line, u8 *buf,
			       u32 size, u8 sav)
{
	u32 line_size = cx->vbi.sliced_decoder_line_size;
	struct v4l2_decode_vbi_line vbi;
	int i;

	/* find the first valid line */
	for (i = 0; i < size; i++, buf++) {
		if (buf[0] == 0xff && !buf[1] && !buf[2] && buf[3] == sav)
			break;
	}

	size -= i;
	if (size < line_size)
		return line;
	for (i = 0; i < size / line_size; i++) {
		u8 *p = buf + i * line_size;

		/* Look for SAV code  */
		if (p[0] != 0xff || p[1] || p[2] || p[3] != sav)
			continue;
		vbi.p = p + 4;
		cx18_av_cmd(cx, VIDIOC_INT_DECODE_VBI_LINE, &vbi);
		if (vbi.type) {
			cx->vbi.sliced_data[line].id = vbi.type;
			cx->vbi.sliced_data[line].field = vbi.is_second_field;
			cx->vbi.sliced_data[line].line = vbi.line;
			memcpy(cx->vbi.sliced_data[line].data, vbi.p, 42);
			line++;
		}
	}
	return line;
}

void cx18_process_vbi_data(struct cx18 *cx, struct cx18_buffer *buf,
			   u64 pts_stamp, int streamtype)
{
	u8 *p = (u8 *) buf->buf;
	u32 size = buf->bytesused;
	int lines;

	if (streamtype != CX18_ENC_STREAM_TYPE_VBI)
		return;

	/* Raw VBI data */
	if (cx->vbi.sliced_in->service_set == 0) {
		u8 type;

		cx18_buf_swap(buf);

		type = p[3];

		size = buf->bytesused = compress_raw_buf(cx, p, size);

		/* second field of the frame? */
		if (type == cx->vbi.raw_decoder_sav_even_field) {
			/* Dirty hack needed for backwards
			   compatibility of old VBI software. */
			p += size - 4;
			memcpy(p, &cx->vbi.frame, 4);
			cx->vbi.frame++;
		}
		return;
	}

	/* Sliced VBI data with data insertion */
	cx18_buf_swap(buf);

	/* first field */
	lines = compress_sliced_buf(cx, 0, p, size / 2,
			cx->vbi.sliced_decoder_sav_odd_field);
	/* second field */
	/* experimentation shows that the second half does not always
	   begin at the exact address. So start a bit earlier
	   (hence 32). */
	lines = compress_sliced_buf(cx, lines, p + size / 2 - 32,
			size / 2 + 32, cx->vbi.sliced_decoder_sav_even_field);
	/* always return at least one empty line */
	if (lines == 0) {
		cx->vbi.sliced_data[0].id = 0;
		cx->vbi.sliced_data[0].line = 0;
		cx->vbi.sliced_data[0].field = 0;
		lines = 1;
	}
	buf->bytesused = size = lines * sizeof(cx->vbi.sliced_data[0]);
	memcpy(p, &cx->vbi.sliced_data[0], size);

	if (cx->vbi.insert_mpeg)
		copy_vbi_data(cx, lines, pts_stamp);
	cx->vbi.frame++;
}
