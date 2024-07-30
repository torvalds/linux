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

/* Luma and chroma qp tables to achieve 50% compression quality
 * This is as per example in Annex K.1 of ITU-T.81
 */
const u8 v4l2_jpeg_ref_table_luma_qt[V4L2_JPEG_PIXELS_IN_BLOCK] = {
	16, 11, 10, 16, 24, 40, 51, 61,
	12, 12, 14, 19, 26, 58, 60, 55,
	14, 13, 16, 24, 40, 57, 69, 56,
	14, 17, 22, 29, 51, 87, 80, 62,
	18, 22, 37, 56, 68, 109, 103, 77,
	24, 35, 55, 64, 81, 104, 113, 92,
	49, 64, 78, 87, 103, 121, 120, 101,
	72, 92, 95, 98, 112, 100, 103, 99
};
EXPORT_SYMBOL_GPL(v4l2_jpeg_ref_table_luma_qt);

const u8 v4l2_jpeg_ref_table_chroma_qt[V4L2_JPEG_PIXELS_IN_BLOCK] = {
	17, 18, 24, 47, 99, 99, 99, 99,
	18, 21, 26, 66, 99, 99, 99, 99,
	24, 26, 56, 99, 99, 99, 99, 99,
	47, 66, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99
};
EXPORT_SYMBOL_GPL(v4l2_jpeg_ref_table_chroma_qt);

/* Zigzag scan pattern indexes */
const u8 v4l2_jpeg_zigzag_scan_index[V4L2_JPEG_PIXELS_IN_BLOCK] = {
	0,   1,  8, 16,  9,  2,  3, 10,
	17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63
};
EXPORT_SYMBOL_GPL(v4l2_jpeg_zigzag_scan_index);

/*
 * Contains the data that needs to be sent in the marker segment of an
 * interchange format JPEG stream or an abbreviated format table specification
 * data stream. Specifies the huffman table used for encoding the luminance DC
 * coefficient differences. The table represents Table K.3 of ITU-T.81
 */
const u8 v4l2_jpeg_ref_table_luma_dc_ht[V4L2_JPEG_REF_HT_DC_LEN] = {
	0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B
};
EXPORT_SYMBOL_GPL(v4l2_jpeg_ref_table_luma_dc_ht);

/*
 * Contains the data that needs to be sent in the marker segment of an
 * interchange format JPEG stream or an abbreviated format table specification
 * data stream. Specifies the huffman table used for encoding the luminance AC
 * coefficients. The table represents Table K.5 of ITU-T.81
 */
const u8 v4l2_jpeg_ref_table_luma_ac_ht[V4L2_JPEG_REF_HT_AC_LEN] = {
	0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04,
	0x00, 0x00, 0x01, 0x7D, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
	0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32,
	0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
	0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A,
	0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55,
	0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85,
	0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
	0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2,
	0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
	0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8,
	0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
	0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA
};
EXPORT_SYMBOL_GPL(v4l2_jpeg_ref_table_luma_ac_ht);

/*
 * Contains the data that needs to be sent in the marker segment of an interchange format JPEG
 * stream or an abbreviated format table specification data stream.
 * Specifies the huffman table used for encoding the chrominance DC coefficient differences.
 * The table represents Table K.4 of ITU-T.81
 */
const u8 v4l2_jpeg_ref_table_chroma_dc_ht[V4L2_JPEG_REF_HT_DC_LEN] = {
	0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B
};
EXPORT_SYMBOL_GPL(v4l2_jpeg_ref_table_chroma_dc_ht);

/*
 * Contains the data that needs to be sent in the marker segment of an
 * interchange format JPEG stream or an abbreviated format table specification
 * data stream. Specifies the huffman table used for encoding the chrominance
 * AC coefficients. The table represents Table K.6 of ITU-T.81
 */
const u8 v4l2_jpeg_ref_table_chroma_ac_ht[V4L2_JPEG_REF_HT_AC_LEN] = {
	0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04,
	0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
	0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81,
	0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
	0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17,
	0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38,
	0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54,
	0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83,
	0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,
	0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9,
	0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
	0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6,
	0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
	0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA
};
EXPORT_SYMBOL_GPL(v4l2_jpeg_ref_table_chroma_ac_ht);

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
		return jpeg_skip(stream, lp - 2);

	/* get to Ap12 */
	ret = jpeg_skip(stream, 11);
	if (ret < 0)
		return ret;

	ret = jpeg_get_byte(stream);
	if (ret < 0)
		return ret;

	*tf = ret;

	/* skip the rest of the segment, this ensures at least it is complete */
	skip = lp - 2 - 11 - 1;
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
