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

/*
 * Raster Reference/Protection (RP) bytes, used in Start/End Active
 * Video codes emitted from the digitzer in VIP 1.x mode, that flag the start
 * of VBI sample or VBI ancillary data regions in the digitial ratser line.
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
		memmove(dst + sd + 4, dst + sd + 12, line * 43);
		size = 4 + ((43 * line + 3) & ~3);
	} else {
		memcpy(dst + sd, "itv0", 4);
		cpu_to_le32s(&linemask[0]);
		cpu_to_le32s(&linemask[1]);
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
/* FIXME - this function ignores the input size. */
static u32 compress_raw_buf(struct cx18 *cx, u8 *buf, u32 size, u32 hdr_size)
{
	u32 line_size = vbi_active_samples;
	u32 lines = cx->vbi.count * 2;
	u8 *q = buf;
	u8 *p;
	int i;

	/* Skip the header */
	buf += hdr_size;

	for (i = 0; i < lines; i++) {
		p = buf + i * line_size;

		/* Look for SAV code */
		if (p[0] != 0xff || p[1] || p[2] ||
		    (p[3] != raw_vbi_sav_rp[0] &&
		     p[3] != raw_vbi_sav_rp[1]))
			break;
		if (i == lines - 1) {
			/* last line is hdr_size bytes short - extrapolate it */
			memcpy(q, p + 4, line_size - 4 - hdr_size);
			q += line_size - 4 - hdr_size;
			p += line_size - hdr_size - 1;
			memset(q, (int) *p, hdr_size);
		} else {
			memcpy(q, p + 4, line_size - 4);
			q += line_size - 4;
		}
	}
	return lines * (line_size - 4);
}

static u32 compress_sliced_buf(struct cx18 *cx, u8 *buf, u32 size,
			       const u32 hdr_size)
{
	struct v4l2_decode_vbi_line vbi;
	int i;
	u32 line = 0;
	u32 line_size = cx->is_60hz ? vbi_hblank_samples_60Hz
				    : vbi_hblank_samples_50Hz;

	/* find the first valid line */
	for (i = hdr_size, buf += hdr_size; i < size; i++, buf++) {
		if (buf[0] == 0xff && !buf[1] && !buf[2] &&
		    (buf[3] == sliced_vbi_eav_rp[0] ||
		     buf[3] == sliced_vbi_eav_rp[1]))
			break;
	}

	/*
	 * The last line is short by hdr_size bytes, but for the remaining
	 * checks against size, we pretend that it is not, by counting the
	 * header bytes we knowingly skipped
	 */
	size -= (i - hdr_size);
	if (size < line_size)
		return line;

	for (i = 0; i < size / line_size; i++) {
		u8 *p = buf + i * line_size;

		/* Look for EAV code  */
		if (p[0] != 0xff || p[1] || p[2] ||
		    (p[3] != sliced_vbi_eav_rp[0] &&
		     p[3] != sliced_vbi_eav_rp[1]))
			continue;
		vbi.p = p + 4;
		v4l2_subdev_call(cx->sd_av, vbi, decode_vbi_line, &vbi);
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

static void _cx18_process_vbi_data(struct cx18 *cx, struct cx18_buffer *buf)
{
	/*
	 * The CX23418 provides a 12 byte header in its raw VBI buffers to us:
	 * 0x3fffffff [4 bytes of something] [4 byte presentation time stamp]
	 */
	struct vbi_data_hdr {
		__be32 magic;
		__be32 unknown;
		__be32 pts;
	} *hdr = (struct vbi_data_hdr *) buf->buf;

	u8 *p = (u8 *) buf->buf;
	u32 size = buf->bytesused;
	u32 pts;
	int lines;

	/*
	 * The CX23418 sends us data that is 32 bit little-endian swapped,
	 * but we want the raw VBI bytes in the order they were in the raster
	 * line.  This has a side effect of making the header big endian
	 */
	cx18_buf_swap(buf);

	/* Raw VBI data */
	if (cx18_raw_vbi(cx)) {

		size = buf->bytesused =
		     compress_raw_buf(cx, p, size, sizeof(struct vbi_data_hdr));

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

	pts = (be32_to_cpu(hdr->magic) == 0x3fffffff) ? be32_to_cpu(hdr->pts)
						      : 0;

	lines = compress_sliced_buf(cx, p, size, sizeof(struct vbi_data_hdr));

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

void cx18_process_vbi_data(struct cx18 *cx, struct cx18_mdl *mdl,
			   int streamtype)
{
	struct cx18_buffer *buf;
	u32 orig_used;

	if (streamtype != CX18_ENC_STREAM_TYPE_VBI)
		return;

	/*
	 * Big assumption here:
	 * Every buffer hooked to the MDL's buf_list is a complete VBI frame
	 * that ends at the end of the buffer.
	 *
	 * To assume anything else would make the code in this file
	 * more complex, or require extra memcpy()'s to make the
	 * buffers satisfy the above assumption.  It's just simpler to set
	 * up the encoder buffer transfers to make the assumption true.
	 */
	list_for_each_entry(buf, &mdl->buf_list, list) {
		orig_used = buf->bytesused;
		if (orig_used == 0)
			break;
		_cx18_process_vbi_data(cx, buf);
		mdl->bytesused -= (orig_used - buf->bytesused);
	}
}
