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

/*
 * Raster Reference/Protection (RP) bytes, used in Start/End Active
 * Video codes emitted from the digitzer in VIP 1.x mode, that flag the start
 * of VBI sample or VBI ancilliary data regions in the digitial ratser line.
 *
 * Task FieldEven VerticalBlank HorizontalBlank 0 0 0 0
 */
static const u8 raw_vbi_sav_rp[2] = { 0x20, 0x60 };    /* __V_, _FV_ */
static const u8 sliced_vbi_eav_rp[2] = { 0xb0, 0xf0 }; /* T_VH, TFVH */

static void copy_vbi_data(struct cx18 *cx, int lines, u32 pts_stamp)
{
	int line = 0;
	int i;
	u32 linemask[2] = { 0, 0 };
	unsigned short size;
	static const u8 mpeg_hdr_data[] = {
		/* MPEG-2 Program Pack */
		0x00, 0x00, 0x01, 0xba,		    /* Prog Pack start code */
		0x44, 0x00, 0x0c, 0x66, 0x24, 0x01, /* SCR, SCR Ext, markers */
		0x01, 0xd1, 0xd3,		    /* Mux Rate, markers */
		0xfa, 0xff, 0xff,		    /* Res, Suff cnt, Stuff */
		/* MPEG-2 Private Stream 1 PES Packet */
		0x00, 0x00, 0x01, 0xbd,		    /* Priv Stream 1 start */
		0x00, 0x1a,			    /* length */
		0x84, 0x80, 0x07,		    /* flags, hdr data len */
		0x21, 0x00, 0x5d, 0x63, 0xa7, 	    /* PTS, markers */
		0xff, 0xff			    /* stuffing */
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
		memcpy(dst + sd, "itv0", 4);
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
   after the frame.  Returns new compressed size. */
static u32 compress_raw_buf(struct cx18 *cx, u8 *buf, u32 size)
{
	u32 line_size = vbi_active_samples;
	u32 lines = cx->vbi.count * 2;
	u8 sav1 = raw_vbi_sav_rp[0];
	u8 sav2 = raw_vbi_sav_rp[1];
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
			       u32 size, u8 eav)
{
	struct v4l2_decode_vbi_line vbi;
	int i;
	u32 line_size = cx->is_60hz ? vbi_hblank_samples_60Hz
				    : vbi_hblank_samples_50Hz;

	/* find the first valid line */
	for (i = 0; i < size; i++, buf++) {
		if (buf[0] == 0xff && !buf[1] && !buf[2] && buf[3] == eav)
			break;
	}

	size -= i;
	if (size < line_size)
		return line;
	for (i = 0; i < size / line_size; i++) {
		u8 *p = buf + i * line_size;

		/* Look for EAV code  */
		if (p[0] != 0xff || p[1] || p[2] || p[3] != eav)
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
			   int streamtype)
{
	u8 *p = (u8 *) buf->buf;
	u32 *q = (u32 *) buf->buf;
	u32 size = buf->bytesused;
	u32 pts;
	int lines;

	if (streamtype != CX18_ENC_STREAM_TYPE_VBI)
		return;

	/*
	 * The CX23418 sends us data that is 32 bit LE swapped, but we want
	 * the raw VBI bytes in the order they were in the raster line
	 */
	cx18_buf_swap(buf);

	/*
	 * The CX23418 provides a 12 byte header in it's raw VBI buffers to us:
	 * 0x3fffffff [4 bytes of something] [4 byte presentation time stamp?]
	 */

	/* Raw VBI data */
	if (cx18_raw_vbi(cx)) {
		u8 type;

		/*
		 * We've set up to get a frame's worth of VBI data at a time.
		 * Skip 12 bytes of header prefixing the first field.
		 */
		size -= 12;
		memcpy(p, &buf->buf[12], size);
		type = p[3];

		/* Extrapolate the last 12 bytes of the frame's last line */
		memset(&p[size], (int) p[size - 1], 12);
		size += 12;

		size = buf->bytesused = compress_raw_buf(cx, p, size);

		/*
		 * Hack needed for compatibility with old VBI software.
		 * Write the frame # at the last 4 bytes of the frame
		 */
		p += size - 4;
		memcpy(p, &cx->vbi.frame, 4);
		cx->vbi.frame++;
		return;
	}

	/* Sliced VBI data with data insertion */

	pts = (be32_to_cpu(q[0] == 0x3fffffff)) ? be32_to_cpu(q[2]) : 0;

	/*
	 * For calls to compress_sliced_buf(), ensure there are an integral
	 * number of lines by shifting the real data up over the 12 bytes header
	 * that got stuffed in.
	 * FIXME - there's a smarter way to do this with pointers, but for some
	 * reason I can't get it to work correctly right now.
	 */
	memcpy(p, &buf->buf[12], size-12);

	/* first field */
	lines = compress_sliced_buf(cx, 0, p, size / 2, sliced_vbi_eav_rp[0]);
	/*
	 * second field
	 * In case the second half does not always begin at the exact address,
	 * start a bit earlier (hence 32).
	 */
	lines = compress_sliced_buf(cx, lines, p + size / 2 - 32,
			size / 2 + 32, sliced_vbi_eav_rp[1]);
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
		copy_vbi_data(cx, lines, pts);
	cx->vbi.frame++;
}
