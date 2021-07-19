// SPDX-License-Identifier: GPL-2.0-only
/*
 * V4L2 JPEG header parser helpers.
 *
 * Copyright (C) 2019 Pengutronix, Philipp Zabel <kernel@pengutronix.de>
 *
 * For reference, see JPEG ITU-T.81 (ISO/IEC 10918-1) [1]
 *
 * [1] https://www.w3.org/Graphics/JPEG/itu-t81.pdf
 */

#include <asm/unaligned.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <media/v4l2-jpeg.h>

MODULE_DESCRIPTION("V4L2 JPEG header parser helpers");
MODULE_AUTHOR("Philipp Zabel <kernel@pengutronix.de>");
MODULE_LICENSE("GPL");

/* Table B.1 - Marker code assignments */
#define SOF0	0xffc0	/* start of frame */
#define SOF1	0xffc1
#define SOF2	0xffc2
#define SOF3	0xffc3
#define SOF5	0xffc5
#define SOF7	0xffc7
#define JPG	0xffc8	/* extensions */
#define SOF9	0xffc9
#define SOF11	0xffcb
#define SOF13	0xffcd
#define SOF15	0xffcf
#define DHT	0xffc4	/* huffman table */
#define DAC	0xffcc	/* arithmetic coding conditioning */
#define RST0	0xffd0	/* restart */
#define RST7	0xffd7
#define SOI	0xffd8	/* start of image */
#define EOI	0xffd9	/* end of image */
#define SOS	0xffda	/* start of stream */
#define DQT	0xffdb	/* quantization table */
#define DNL	0xffdc	/* number of lines */
#define DRI	0xffdd	/* restart interval */
#define DHP	0xffde	/* hierarchical progression */
#define EXP	0xffdf	/* expand reference */
#define APP0	0xffe0	/* application data */
#define APP14	0xffee	/* application data for colour encoding */
#define APP15	0xffef
#define JPG0	0xfff0	/* extensions */
#define JPG13	0xfffd
#define COM	0xfffe	/* comment */
#define TEM	0xff01	/* temporary */

/**
 * struct jpeg_stream - JPEG byte stream
 * @curr: current position in stream
 * @end: end position, after last byte
 */
struct jpeg_stream {
	u8 *curr;
	u8 *end;
};

/* returns a value that fits into u8, or negative error */
static int jpeg_get_byte(struct jpeg_stream *stream)
{
	if (stream->curr >= stream->end)
		return -EINVAL;

	return *stream->curr++;
}

/* returns a value that fits into u16, or negative error */
static int jpeg_get_word_be(struct jpeg_stream *stream)
{
	u16 word;

	if (stream->curr + sizeof(__be16) > stream->end)
		return -EINVAL;

	word = get_unaligned_be16(stream->curr);
	stream->curr += sizeof(__be16);

	return word;
}

static int jpeg_skip(struct jpeg_stream *stream, size_t len)
{
	if (stream->curr + len > stream->end)
		return -EINVAL;

	stream->curr += len;

	return 0;
}

static int jpeg_next_marker(struct jpeg_stream *stream)
{
	int byte;
	u16 marker = 0;

	while ((byte = jpeg_get_byte(stream)) >= 0) {
		marker = (marker << 8) | byte;
		/* skip stuffing bytes and REServed markers */
		if (marker == TEM || (marker > 0xffbf && marker < 0xffff))
			return marker;
	}

	return byte;
}

/* this does not advance the current position in the stream */
static int jpeg_reference_segment(struct jpeg_stream *stream,
				  struct v4l2_jpeg_reference *segment)
{
	u16 len;

	if (stream->curr + sizeof(__be16) > stream->end)
		return -EINVAL;

	len = get_unaligned_be16(stream->curr);
	if (stream->curr + len > stream->end)
		return -EINVAL;

	segment->start = stream->curr;
	segment->length = len;

	return 0;
}

static int v4l2_jpeg_decode_subsampling(u8 nf, u8 h_v)
{
	if (nf == 1)
		return V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY;

	/* no chroma subsampling for 4-component images */
	if (nf == 4 && h_v != 0x11)
		return -EINVAL;

	switch (h_v) {
	case 0x11:
		return V4L2_JPEG_CHROMA_SUBSAMPLING_444;
	case 0x21:
		return V4L2_JPEG_CHROMA_SUBSAMPLING_422;
	case 0x22:
		return V4L2_JPEG_CHROMA_SUBSAMPLING_420;
	case 0x41:
		return V4L2_JPEG_CHROMA_SUBSAMPLING_411;
	default:
		return -EINVAL;
	}
}

static int jpeg_parse_frame_header(struct jpeg_stream *stream, u16 sof_marker,
				   struct v4l2_jpeg_frame_header *frame_header)
{
	int len = jpeg_get_word_be(stream);

	if (len < 0)
		return len;
	/* Lf = 8 + 3 * Nf, Nf >= 1 */
	if (len < 8 + 3)
		return -EINVAL;

	if (frame_header) {
		/* Table B.2 - Frame header parameter sizes and values */
		int p, y, x, nf;
		int i;

		p = jpeg_get_byte(stream);
		if (p < 0)
			return p;
		/*
		 * Baseline DCT only supports 8-bit precision.
		 * Extended sequential DCT also supports 12-bit precision.
		 */
		if (p != 8 && (p != 12 || sof_marker != SOF1))
			return -EINVAL;

		y = jpeg_get_word_be(stream);
		if (y < 0)
			return y;
		if (y == 0)
			return -EINVAL;

		x = jpeg_get_word_be(stream);
		if (x < 0)
			return x;
		if (x == 0)
			return -EINVAL;

		nf = jpeg_get_byte(stream);
		if (nf < 0)
			return nf;
		/*
		 * The spec allows 1 <= Nf <= 255, but we only support up to 4
		 * components.
		 */
		if (nf < 1 || nf > V4L2_JPEG_MAX_COMPONENTS)
			return -EINVAL;
		if (len != 8 + 3 * nf)
			return -EINVAL;

		frame_header->precision = p;
		frame_header->height = y;
		frame_header->width = x;
		frame_header->num_components = nf;

		for (i = 0; i < nf; i++) {
			struct v4l2_jpeg_frame_component_spec *component;
			int c, h_v, tq;

			c = jpeg_get_byte(stream);
			if (c < 0)
				return c;

			h_v = jpeg_get_byte(stream);
			if (h_v < 0)
				return h_v;
			if (i == 0) {
				int subs;

				subs = v4l2_jpeg_decode_subsampling(nf, h_v);
				if (subs < 0)
					return subs;
				frame_header->subsampling = subs;
			} else if (h_v != 0x11) {
				/* all chroma sampling factors must be 1 */
				return -EINVAL;
			}

			tq = jpeg_get_byte(stream);
			if (tq < 0)
				return tq;

			component = &frame_header->component[i];
			component->component_identifier = c;
			component->horizontal_sampling_factor =
				(h_v >> 4) & 0xf;
			component->vertical_sampling_factor = h_v & 0xf;
			component->quantization_table_selector = tq;
		}
	} else {
		return jpeg_skip(stream, len - 2);
	}

	return 0;
}

static int jpeg_parse_scan_header(struct jpeg_stream *stream,
				  struct v4l2_jpeg_scan_header *scan_header)
{
	size_t skip;
	int len = jpeg_get_word_be(stream);

	if (len < 0)
		return len;
	/* Ls = 8 + 3 * Ns, Ns >= 1 */
	if (len < 6 + 2)
		return -EINVAL;

	if (scan_header) {
		int ns;
		int i;

		ns = jpeg_get_byte(stream);
		if (ns < 0)
			return ns;
		if (ns < 1 || ns > 4 || len != 6 + 2 * ns)
			return -EINVAL;

		scan_header->num_components = ns;

		for (i = 0; i < ns; i++) {
			struct v4l2_jpeg_scan_component_spec *component;
			int cs, td_ta;

			cs = jpeg_get_byte(stream);
			if (cs < 0)
				return cs;

			td_ta = jpeg_get_byte(stream);
			if (td_ta < 0)
				return td_ta;

			component = &scan_header->component[i];
			component->component_selector = cs;
			component->dc_entropy_coding_table_selector =
				(td_ta >> 4) & 0xf;
			component->ac_entropy_coding_table_selector =
				td_ta & 0xf;
		}

		skip = 3; /* skip Ss, Se, Ah, and Al */
	} else {
		skip = len - 2;
	}

	return jpeg_skip(stream, skip);
}

/* B.2.4.1 Quantization table-specification syntax */
static int jpeg_parse_quantization_tables(struct jpeg_stream *stream,
					  u8 precision,
					  struct v4l2_jpeg_reference *tables)
{
	int len = jpeg_get_word_be(stream);

	if (len < 0)
		return len;
	/* Lq = 2 + n * 65 (for baseline DCT), n >= 1 */
	if (len < 2 + 65)
		return -EINVAL;

	len -= 2;
	while (len >= 65) {
		u8 pq, tq, *qk;
		int ret;
		int pq_tq = jpeg_get_byte(stream);

		if (pq_tq < 0)
			return pq_tq;

		/* quantization table element precision */
		pq = (pq_tq >> 4) & 0xf;
		/*
		 * Only 8-bit Qk values for 8-bit sample precision. Extended
		 * sequential DCT with 12-bit sample precision also supports
		 * 16-bit Qk values.
		 */
		if (pq != 0 && (pq != 1 || precision != 12))
			return -EINVAL;

		/* quantization table destination identifier */
		tq = pq_tq & 0xf;
		if (tq > 3)
			return -EINVAL;

		/* quantization table element */
		qk = stream->curr;
		ret = jpeg_skip(stream, pq ? 128 : 64);
		if (ret < 0)
			return -EINVAL;

		if (tables) {
			tables[tq].start = qk;
			tables[tq].length = pq ? 128 : 64;
		}

		len -= pq ? 129 : 65;
	}

	return 0;
}

/* B.2.4.2 Huffman table-specification syntax */
static int jpeg_parse_huffman_tables(struct jpeg_stream *stream,
				     struct v4l2_jpeg_reference *tables)
{
	int mt;
	int len = jpeg_get_word_be(stream);

	if (len < 0)
		return len;
	/* Table B.5 - Huffman table specification parameter sizes and values */
	if (len < 2 + 17)
		return -EINVAL;

	for (len -= 2; len >= 17; len -= 17 + mt) {
		u8 tc, th, *table;
		int tc_th = jpeg_get_byte(stream);
		int i, ret;

		if (tc_th < 0)
			return tc_th;

		/* table class - 0 = DC, 1 = AC */
		tc = (tc_th >> 4) & 0xf;
		if (tc > 1)
			return -EINVAL;

		/* huffman table destination identifier */
		th = tc_th & 0xf;
		/* only two Huffman tables for baseline DCT */
		if (th > 1)
			return -EINVAL;

		/* BITS - number of Huffman codes with length i */
		table = stream->curr;
		mt = 0;
		for (i = 0; i < 16; i++) {
			int li;

			li = jpeg_get_byte(stream);
			if (li < 0)
				return li;

			mt += li;
		}
		/* HUFFVAL - values associated with each Huffman code */
		ret = jpeg_skip(stream, mt);
		if (ret < 0)
			return ret;

		if (tables) {
			tables[(tc << 1) | th].start = table;
			tables[(tc << 1) | th].length = stream->curr - table;
		}
	}

	return jpeg_skip(stream, len - 2);
}

/* B.2.4.4 Restart interval definition syntax */
static int jpeg_parse_restart_interval(struct jpeg_stream *stream,
				       u16 *restart_interval)
{
	int len = jpeg_get_word_be(stream);
	int ri;

	if (len < 0)
		return len;
	if (len != 4)
		return -EINVAL;

	ri = jpeg_get_word_be(stream);
	if (ri < 0)
		return ri;

	*restart_interval = ri;

	return 0;
}

static int jpeg_skip_segment(struct jpeg_stream *stream)
{
	int len = jpeg_get_word_be(stream);

	if (len < 0)
		return len;
	if (len < 2)
		return -EINVAL;

	return jpeg_skip(stream, len - 2);
}

/* Rec. ITU-T T.872 (06/2012) 6.5.3 */
static int jpeg_parse_app14_data(struct jpeg_stream *stream,
				 enum v4l2_jpeg_app14_tf *tf)
{
	int ret;
	int lp;
	int skip;

	lp = jpeg_get_word_be(stream);
	if (lp < 0)
		return lp;

	/* Check for "Adobe\0" in Ap1..6 */
	if (stream->curr + 6 > stream->end ||
	    strncmp(stream->curr, "Adobe\0", 6))
		return -EINVAL;

	/* get to Ap12 */
	ret = jpeg_skip(stream, 11);
	if (ret < 0)
		return ret;

	ret = jpeg_get_byte(stream);
	if (ret < 0)
		return ret;

	*tf = ret;

	/* skip the rest of the segment, this ensures at least it is complete */
	skip = lp - 2 - 11;
	return jpeg_skip(stream, skip);
}

/**
 * v4l2_jpeg_parse_header - locate marker segments and optionally parse headers
 * @buf: address of the JPEG buffer, should start with a SOI marker
 * @len: length of the JPEG buffer
 * @out: returns marker segment positions and optionally parsed headers
 *
 * The out->scan_header pointer must be initialized to NULL or point to a valid
 * v4l2_jpeg_scan_header structure. The out->huffman_tables and
 * out->quantization_tables pointers must be initialized to NULL or point to a
 * valid array of 4 v4l2_jpeg_reference structures each.
 *
 * Returns 0 or negative error if parsing failed.
 */
int v4l2_jpeg_parse_header(void *buf, size_t len, struct v4l2_jpeg_header *out)
{
	struct jpeg_stream stream;
	int marker;
	int ret = 0;

	stream.curr = buf;
	stream.end = stream.curr + len;

	out->num_dht = 0;
	out->num_dqt = 0;

	/* the first bytes must be SOI, B.2.1 High-level syntax */
	if (jpeg_get_word_be(&stream) != SOI)
		return -EINVAL;

	/* init value to signal if this marker is not present */
	out->app14_tf = V4L2_JPEG_APP14_TF_UNKNOWN;

	/* loop through marker segments */
	while ((marker = jpeg_next_marker(&stream)) >= 0) {
		switch (marker) {
		/* baseline DCT, extended sequential DCT */
		case SOF0 ... SOF1:
			ret = jpeg_reference_segment(&stream, &out->sof);
			if (ret < 0)
				return ret;
			ret = jpeg_parse_frame_header(&stream, marker,
						      &out->frame);
			break;
		/* progressive, lossless */
		case SOF2 ... SOF3:
		/* differential coding */
		case SOF5 ... SOF7:
		/* arithmetic coding */
		case SOF9 ... SOF11:
		case SOF13 ... SOF15:
		case DAC:
		case TEM:
			return -EINVAL;

		case DHT:
			ret = jpeg_reference_segment(&stream,
					&out->dht[out->num_dht++ % 4]);
			if (ret < 0)
				return ret;
			if (!out->huffman_tables) {
				ret = jpeg_skip_segment(&stream);
				break;
			}
			ret = jpeg_parse_huffman_tables(&stream,
							out->huffman_tables);
			break;
		case DQT:
			ret = jpeg_reference_segment(&stream,
					&out->dqt[out->num_dqt++ % 4]);
			if (ret < 0)
				return ret;
			if (!out->quantization_tables) {
				ret = jpeg_skip_segment(&stream);
				break;
			}
			ret = jpeg_parse_quantization_tables(&stream,
					out->frame.precision,
					out->quantization_tables);
			break;
		case DRI:
			ret = jpeg_parse_restart_interval(&stream,
							&out->restart_interval);
			break;
		case APP14:
			ret = jpeg_parse_app14_data(&stream,
						    &out->app14_tf);
			break;
		case SOS:
			ret = jpeg_reference_segment(&stream, &out->sos);
			if (ret < 0)
				return ret;
			ret = jpeg_parse_scan_header(&stream, out->scan);
			/*
			 * stop parsing, the scan header marks the beginning of
			 * the entropy coded segment
			 */
			out->ecs_offset = stream.curr - (u8 *)buf;
			return ret;

		/* markers without parameters */
		case RST0 ... RST7: /* restart */
		case SOI: /* start of image */
		case EOI: /* end of image */
			break;

		/* skip unknown or unsupported marker segments */
		default:
			ret = jpeg_skip_segment(&stream);
			break;
		}
		if (ret < 0)
			return ret;
	}

	return marker;
}
EXPORT_SYMBOL_GPL(v4l2_jpeg_parse_header);

/**
 * v4l2_jpeg_parse_frame_header - parse frame header
 * @buf: address of the frame header, after the SOF0 marker
 * @len: length of the frame header
 * @frame_header: returns the parsed frame header
 *
 * Returns 0 or negative error if parsing failed.
 */
int v4l2_jpeg_parse_frame_header(void *buf, size_t len,
				 struct v4l2_jpeg_frame_header *frame_header)
{
	struct jpeg_stream stream;

	stream.curr = buf;
	stream.end = stream.curr + len;
	return jpeg_parse_frame_header(&stream, SOF0, frame_header);
}
EXPORT_SYMBOL_GPL(v4l2_jpeg_parse_frame_header);

/**
 * v4l2_jpeg_parse_scan_header - parse scan header
 * @buf: address of the scan header, after the SOS marker
 * @len: length of the scan header
 * @scan_header: returns the parsed scan header
 *
 * Returns 0 or negative error if parsing failed.
 */
int v4l2_jpeg_parse_scan_header(void *buf, size_t len,
				struct v4l2_jpeg_scan_header *scan_header)
{
	struct jpeg_stream stream;

	stream.curr = buf;
	stream.end = stream.curr + len;
	return jpeg_parse_scan_header(&stream, scan_header);
}
EXPORT_SYMBOL_GPL(v4l2_jpeg_parse_scan_header);

/**
 * v4l2_jpeg_parse_quantization_tables - parse quantization tables segment
 * @buf: address of the quantization table segment, after the DQT marker
 * @len: length of the quantization table segment
 * @precision: sample precision (P) in bits per component
 * @q_tables: returns four references into the buffer for the
 *            four possible quantization table destinations
 *
 * Returns 0 or negative error if parsing failed.
 */
int v4l2_jpeg_parse_quantization_tables(void *buf, size_t len, u8 precision,
					struct v4l2_jpeg_reference *q_tables)
{
	struct jpeg_stream stream;

	stream.curr = buf;
	stream.end = stream.curr + len;
	return jpeg_parse_quantization_tables(&stream, precision, q_tables);
}
EXPORT_SYMBOL_GPL(v4l2_jpeg_parse_quantization_tables);

/**
 * v4l2_jpeg_parse_huffman_tables - parse huffman tables segment
 * @buf: address of the Huffman table segment, after the DHT marker
 * @len: length of the Huffman table segment
 * @huffman_tables: returns four references into the buffer for the
 *                  four possible Huffman table destinations, in
 *                  the order DC0, DC1, AC0, AC1
 *
 * Returns 0 or negative error if parsing failed.
 */
int v4l2_jpeg_parse_huffman_tables(void *buf, size_t len,
				   struct v4l2_jpeg_reference *huffman_tables)
{
	struct jpeg_stream stream;

	stream.curr = buf;
	stream.end = stream.curr + len;
	return jpeg_parse_huffman_tables(&stream, huffman_tables);
}
EXPORT_SYMBOL_GPL(v4l2_jpeg_parse_huffman_tables);
