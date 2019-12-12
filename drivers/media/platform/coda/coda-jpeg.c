// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Coda multi-standard codec IP - JPEG support functions
 *
 * Copyright (C) 2014 Philipp Zabel, Pengutronix
 */

#include <asm/unaligned.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/swab.h>
#include <linux/videodev2.h>

#include <media/v4l2-common.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "coda.h"
#include "trace.h"

#define SOI_MARKER	0xffd8
#define DRI_MARKER	0xffdd
#define DQT_MARKER	0xffdb
#define DHT_MARKER	0xffc4
#define SOF_MARKER	0xffc0
#define EOI_MARKER	0xffd9

enum {
	CODA9_JPEG_FORMAT_420,
	CODA9_JPEG_FORMAT_422,
	CODA9_JPEG_FORMAT_224,
	CODA9_JPEG_FORMAT_444,
	CODA9_JPEG_FORMAT_400,
};

#define CODA9_JPEG_ENC_HUFF_DATA_SIZE	(256 + 256 + 16 + 16)

/*
 * Typical Huffman tables for 8-bit precision luminance and
 * chrominance from JPEG ITU-T.81 (ISO/IEC 10918-1) Annex K.3
 */

static const unsigned char luma_dc[16 + 12] = {
	/* bits */
	0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* values */
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b,
};

static const unsigned char chroma_dc[16 + 12] = {
	/* bits */
	0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* values */
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b,
};

static const unsigned char luma_ac[16 + 162 + 2] = {
	/* bits */
	0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
	0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d,
	/* values */
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
	0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
	0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
	0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
	0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
	0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
	0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
	0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
	0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
	0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
	0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
	0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa, /* padded to 32-bit */
};

static const unsigned char chroma_ac[16 + 162 + 2] = {
	/* bits */
	0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
	0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77,
	/* values */
	0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
	0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
	0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
	0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
	0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
	0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
	0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
	0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
	0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
	0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
	0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
	0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
	0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
	0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
	0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
	0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
	0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa, /* padded to 32-bit */
};

/*
 * Quantization tables for luminance and chrominance components in
 * zig-zag scan order from the Freescale i.MX VPU libraries
 */

static unsigned char luma_q[64] = {
	0x06, 0x04, 0x04, 0x04, 0x05, 0x04, 0x06, 0x05,
	0x05, 0x06, 0x09, 0x06, 0x05, 0x06, 0x09, 0x0b,
	0x08, 0x06, 0x06, 0x08, 0x0b, 0x0c, 0x0a, 0x0a,
	0x0b, 0x0a, 0x0a, 0x0c, 0x10, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x10, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
};

static unsigned char chroma_q[64] = {
	0x07, 0x07, 0x07, 0x0d, 0x0c, 0x0d, 0x18, 0x10,
	0x10, 0x18, 0x14, 0x0e, 0x0e, 0x0e, 0x14, 0x14,
	0x0e, 0x0e, 0x0e, 0x0e, 0x14, 0x11, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x11, 0x11, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x11, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
};

static const unsigned char width_align[] = {
	[CODA9_JPEG_FORMAT_420] = 16,
	[CODA9_JPEG_FORMAT_422] = 16,
	[CODA9_JPEG_FORMAT_224] = 8,
	[CODA9_JPEG_FORMAT_444] = 8,
	[CODA9_JPEG_FORMAT_400] = 8,
};

static const unsigned char height_align[] = {
	[CODA9_JPEG_FORMAT_420] = 16,
	[CODA9_JPEG_FORMAT_422] = 8,
	[CODA9_JPEG_FORMAT_224] = 16,
	[CODA9_JPEG_FORMAT_444] = 8,
	[CODA9_JPEG_FORMAT_400] = 8,
};

static int coda9_jpeg_chroma_format(u32 pixfmt)
{
	switch (pixfmt) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_NV12:
		return CODA9_JPEG_FORMAT_420;
	case V4L2_PIX_FMT_YUV422P:
		return CODA9_JPEG_FORMAT_422;
	case V4L2_PIX_FMT_YUV444:
		return CODA9_JPEG_FORMAT_444;
	case V4L2_PIX_FMT_GREY:
		return CODA9_JPEG_FORMAT_400;
	}
	return -EINVAL;
}

struct coda_memcpy_desc {
	int offset;
	const void *src;
	size_t len;
};

static void coda_memcpy_parabuf(void *parabuf,
				const struct coda_memcpy_desc *desc)
{
	u32 *dst = parabuf + desc->offset;
	const u32 *src = desc->src;
	int len = desc->len / 4;
	int i;

	for (i = 0; i < len; i += 2) {
		dst[i + 1] = swab32(src[i]);
		dst[i] = swab32(src[i + 1]);
	}
}

int coda_jpeg_write_tables(struct coda_ctx *ctx)
{
	int i;
	static const struct coda_memcpy_desc huff[8] = {
		{ 0,   luma_dc,    sizeof(luma_dc)    },
		{ 32,  luma_ac,    sizeof(luma_ac)    },
		{ 216, chroma_dc,  sizeof(chroma_dc)  },
		{ 248, chroma_ac,  sizeof(chroma_ac)  },
	};
	struct coda_memcpy_desc qmat[3] = {
		{ 512, ctx->params.jpeg_qmat_tab[0], 64 },
		{ 576, ctx->params.jpeg_qmat_tab[1], 64 },
		{ 640, ctx->params.jpeg_qmat_tab[1], 64 },
	};

	/* Write huffman tables to parameter memory */
	for (i = 0; i < ARRAY_SIZE(huff); i++)
		coda_memcpy_parabuf(ctx->parabuf.vaddr, huff + i);

	/* Write Q-matrix to parameter memory */
	for (i = 0; i < ARRAY_SIZE(qmat); i++)
		coda_memcpy_parabuf(ctx->parabuf.vaddr, qmat + i);

	return 0;
}

bool coda_jpeg_check_buffer(struct coda_ctx *ctx, struct vb2_buffer *vb)
{
	void *vaddr = vb2_plane_vaddr(vb, 0);
	u16 soi, eoi;
	int len, i;

	soi = be16_to_cpup((__be16 *)vaddr);
	if (soi != SOI_MARKER)
		return false;

	len = vb2_get_plane_payload(vb, 0);
	vaddr += len - 2;
	for (i = 0; i < 32; i++) {
		eoi = be16_to_cpup((__be16 *)(vaddr - i));
		if (eoi == EOI_MARKER) {
			if (i > 0)
				vb2_set_plane_payload(vb, 0, len - i);
			return true;
		}
	}

	return false;
}

static const int bus_req_num[] = {
	[CODA9_JPEG_FORMAT_420] = 2,
	[CODA9_JPEG_FORMAT_422] = 3,
	[CODA9_JPEG_FORMAT_224] = 3,
	[CODA9_JPEG_FORMAT_444] = 4,
	[CODA9_JPEG_FORMAT_400] = 4,
};

#define MCU_INFO(mcu_block_num, comp_num, comp0_info, comp1_info, comp2_info) \
	(((mcu_block_num) << CODA9_JPEG_MCU_BLOCK_NUM_OFFSET) | \
	 ((comp_num) << CODA9_JPEG_COMP_NUM_OFFSET) | \
	 ((comp0_info) << CODA9_JPEG_COMP0_INFO_OFFSET) | \
	 ((comp1_info) << CODA9_JPEG_COMP1_INFO_OFFSET) | \
	 ((comp2_info) << CODA9_JPEG_COMP2_INFO_OFFSET))

static const u32 mcu_info[] = {
	[CODA9_JPEG_FORMAT_420] = MCU_INFO(6, 3, 10, 5, 5),
	[CODA9_JPEG_FORMAT_422] = MCU_INFO(4, 3, 9, 5, 5),
	[CODA9_JPEG_FORMAT_224] = MCU_INFO(4, 3, 6, 5, 5),
	[CODA9_JPEG_FORMAT_444] = MCU_INFO(3, 3, 5, 5, 5),
	[CODA9_JPEG_FORMAT_400] = MCU_INFO(1, 1, 5, 0, 0),
};

/*
 * Convert Huffman table specifcations to tables of codes and code lengths.
 * For reference, see JPEG ITU-T.81 (ISO/IEC 10918-1) [1]
 *
 * [1] https://www.w3.org/Graphics/JPEG/itu-t81.pdf
 */
static int coda9_jpeg_gen_enc_huff_tab(struct coda_ctx *ctx, int tab_num,
				       int *ehufsi, int *ehufco)
{
	int i, j, k, lastk, si, code, maxsymbol;
	const u8 *bits, *huffval;
	struct {
		int size[256];
		int code[256];
	} *huff;
	static const unsigned char *huff_tabs[4] = {
		luma_dc, luma_ac, chroma_dc, chroma_ac,
	};
	int ret = -EINVAL;

	huff = kzalloc(sizeof(*huff), GFP_KERNEL);
	if (!huff)
		return -ENOMEM;

	bits = huff_tabs[tab_num];
	huffval = huff_tabs[tab_num] + 16;

	maxsymbol = tab_num & 1 ? 256 : 16;

	/* Figure C.1 - Generation of table of Huffman code sizes */
	k = 0;
	for (i = 1; i <= 16; i++) {
		j = bits[i - 1];
		if (k + j > maxsymbol)
			goto out;
		while (j--)
			huff->size[k++] = i;
	}
	lastk = k;

	/* Figure C.2 - Generation of table of Huffman codes */
	k = 0;
	code = 0;
	si = huff->size[0];
	while (k < lastk) {
		while (huff->size[k] == si) {
			huff->code[k++] = code;
			code++;
		}
		if (code >= (1 << si))
			goto out;
		code <<= 1;
		si++;
	}

	/* Figure C.3 - Ordering procedure for encoding procedure code tables */
	for (k = 0; k < lastk; k++) {
		i = huffval[k];
		if (i >= maxsymbol || ehufsi[i])
			goto out;
		ehufco[i] = huff->code[k];
		ehufsi[i] = huff->size[k];
	}

	ret = 0;
out:
	kfree(huff);
	return ret;
}

#define DC_TABLE_INDEX0		    0
#define AC_TABLE_INDEX0		    1
#define DC_TABLE_INDEX1		    2
#define AC_TABLE_INDEX1		    3

static int coda9_jpeg_load_huff_tab(struct coda_ctx *ctx)
{
	struct {
		int size[4][256];
		int code[4][256];
	} *huff;
	u32 *huff_data;
	int i, j;
	int ret;

	huff = kzalloc(sizeof(*huff), GFP_KERNEL);
	if (!huff)
		return -ENOMEM;

	/* Generate all four (luma/chroma DC/AC) code/size lookup tables */
	for (i = 0; i < 4; i++) {
		ret = coda9_jpeg_gen_enc_huff_tab(ctx, i, huff->size[i],
						  huff->code[i]);
		if (ret)
			goto out;
	}

	if (!ctx->params.jpeg_huff_data) {
		ctx->params.jpeg_huff_data =
			kzalloc(sizeof(u32) * CODA9_JPEG_ENC_HUFF_DATA_SIZE,
				GFP_KERNEL);
		if (!ctx->params.jpeg_huff_data) {
			ret = -ENOMEM;
			goto out;
		}
	}
	huff_data = ctx->params.jpeg_huff_data;

	for (j = 0; j < 4; j++) {
		/* Store Huffman lookup tables in AC0, AC1, DC0, DC1 order */
		int t = (j == 0) ? AC_TABLE_INDEX0 :
			(j == 1) ? AC_TABLE_INDEX1 :
			(j == 2) ? DC_TABLE_INDEX0 :
				   DC_TABLE_INDEX1;
		/* DC tables only have 16 entries */
		int len = (j < 2) ? 256 : 16;

		for (i = 0; i < len; i++) {
			if (huff->size[t][i] == 0 && huff->code[t][i] == 0)
				*(huff_data++) = 0;
			else
				*(huff_data++) =
					((huff->size[t][i] - 1) << 16) |
					huff->code[t][i];
		}
	}

	ret = 0;
out:
	kfree(huff);
	return ret;
}

static void coda9_jpeg_write_huff_tab(struct coda_ctx *ctx)
{
	struct coda_dev *dev = ctx->dev;
	u32 *huff_data = ctx->params.jpeg_huff_data;
	int i;

	/* Write Huffman size/code lookup tables in AC0, AC1, DC0, DC1 order */
	coda_write(dev, 0x3, CODA9_REG_JPEG_HUFF_CTRL);
	for (i = 0; i < CODA9_JPEG_ENC_HUFF_DATA_SIZE; i++)
		coda_write(dev, *(huff_data++), CODA9_REG_JPEG_HUFF_DATA);
	coda_write(dev, 0x0, CODA9_REG_JPEG_HUFF_CTRL);
}

static inline void coda9_jpeg_write_qmat_quotients(struct coda_dev *dev,
						   u8 *qmat, int index)
{
	int i;

	coda_write(dev, index | 0x3, CODA9_REG_JPEG_QMAT_CTRL);
	for (i = 0; i < 64; i++)
		coda_write(dev, 0x80000 / qmat[i], CODA9_REG_JPEG_QMAT_DATA);
	coda_write(dev, index, CODA9_REG_JPEG_QMAT_CTRL);
}

static void coda9_jpeg_load_qmat_tab(struct coda_ctx *ctx)
{
	struct coda_dev *dev = ctx->dev;
	u8 *luma_tab;
	u8 *chroma_tab;

	luma_tab = ctx->params.jpeg_qmat_tab[0];
	if (!luma_tab)
		luma_tab = luma_q;

	chroma_tab = ctx->params.jpeg_qmat_tab[1];
	if (!chroma_tab)
		chroma_tab = chroma_q;

	coda9_jpeg_write_qmat_quotients(dev, luma_tab, 0x00);
	coda9_jpeg_write_qmat_quotients(dev, chroma_tab, 0x40);
	coda9_jpeg_write_qmat_quotients(dev, chroma_tab, 0x80);
}

struct coda_jpeg_stream {
	u8 *curr;
	u8 *end;
};

static inline int coda_jpeg_put_byte(u8 byte, struct coda_jpeg_stream *stream)
{
	if (stream->curr >= stream->end)
		return -EINVAL;

	*stream->curr++ = byte;

	return 0;
}

static inline int coda_jpeg_put_word(u16 word, struct coda_jpeg_stream *stream)
{
	if (stream->curr + sizeof(__be16) > stream->end)
		return -EINVAL;

	put_unaligned_be16(word, stream->curr);
	stream->curr += sizeof(__be16);

	return 0;
}

static int coda_jpeg_put_table(u16 marker, u8 index, const u8 *table,
			       size_t len, struct coda_jpeg_stream *stream)
{
	int i, ret;

	ret = coda_jpeg_put_word(marker, stream);
	if (ret < 0)
		return ret;
	ret = coda_jpeg_put_word(3 + len, stream);
	if (ret < 0)
		return ret;
	ret = coda_jpeg_put_byte(index, stream);
	for (i = 0; i < len && ret == 0; i++)
		ret = coda_jpeg_put_byte(table[i], stream);

	return ret;
}

static int coda_jpeg_define_quantization_table(struct coda_ctx *ctx, u8 index,
					       struct coda_jpeg_stream *stream)
{
	return coda_jpeg_put_table(DQT_MARKER, index,
				   ctx->params.jpeg_qmat_tab[index], 64,
				   stream);
}

static int coda_jpeg_define_huffman_table(u8 index, const u8 *table, size_t len,
					  struct coda_jpeg_stream *stream)
{
	return coda_jpeg_put_table(DHT_MARKER, index, table, len, stream);
}

static int coda9_jpeg_encode_header(struct coda_ctx *ctx, int len, u8 *buf)
{
	struct coda_jpeg_stream stream = { buf, buf + len };
	struct coda_q_data *q_data_src;
	int chroma_format, comp_num;
	int i, ret, pad;

	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	chroma_format = coda9_jpeg_chroma_format(q_data_src->fourcc);
	if (chroma_format < 0)
		return 0;

	/* Start Of Image */
	ret = coda_jpeg_put_word(SOI_MARKER, &stream);
	if (ret < 0)
		return ret;

	/* Define Restart Interval */
	if (ctx->params.jpeg_restart_interval) {
		ret = coda_jpeg_put_word(DRI_MARKER, &stream);
		if (ret < 0)
			return ret;
		ret = coda_jpeg_put_word(4, &stream);
		if (ret < 0)
			return ret;
		ret = coda_jpeg_put_word(ctx->params.jpeg_restart_interval,
					 &stream);
		if (ret < 0)
			return ret;
	}

	/* Define Quantization Tables */
	ret = coda_jpeg_define_quantization_table(ctx, 0x00, &stream);
	if (ret < 0)
		return ret;
	if (chroma_format != CODA9_JPEG_FORMAT_400) {
		ret = coda_jpeg_define_quantization_table(ctx, 0x01, &stream);
		if (ret < 0)
			return ret;
	}

	/* Define Huffman Tables */
	ret = coda_jpeg_define_huffman_table(0x00, luma_dc, 16 + 12, &stream);
	if (ret < 0)
		return ret;
	ret = coda_jpeg_define_huffman_table(0x10, luma_ac, 16 + 162, &stream);
	if (ret < 0)
		return ret;
	if (chroma_format != CODA9_JPEG_FORMAT_400) {
		ret = coda_jpeg_define_huffman_table(0x01, chroma_dc, 16 + 12,
						     &stream);
		if (ret < 0)
			return ret;
		ret = coda_jpeg_define_huffman_table(0x11, chroma_ac, 16 + 162,
						     &stream);
		if (ret < 0)
			return ret;
	}

	/* Start Of Frame */
	ret = coda_jpeg_put_word(SOF_MARKER, &stream);
	if (ret < 0)
		return ret;
	comp_num = (chroma_format == CODA9_JPEG_FORMAT_400) ? 1 : 3;
	ret = coda_jpeg_put_word(8 + comp_num * 3, &stream);
	if (ret < 0)
		return ret;
	ret = coda_jpeg_put_byte(0x08, &stream);
	if (ret < 0)
		return ret;
	ret = coda_jpeg_put_word(q_data_src->height, &stream);
	if (ret < 0)
		return ret;
	ret = coda_jpeg_put_word(q_data_src->width, &stream);
	if (ret < 0)
		return ret;
	ret = coda_jpeg_put_byte(comp_num, &stream);
	if (ret < 0)
		return ret;
	for (i = 0; i < comp_num; i++) {
		static unsigned char subsampling[5][3] = {
			[CODA9_JPEG_FORMAT_420] = { 0x22, 0x11, 0x11 },
			[CODA9_JPEG_FORMAT_422] = { 0x21, 0x11, 0x11 },
			[CODA9_JPEG_FORMAT_224] = { 0x12, 0x11, 0x11 },
			[CODA9_JPEG_FORMAT_444] = { 0x11, 0x11, 0x11 },
			[CODA9_JPEG_FORMAT_400] = { 0x11 },
		};

		/* Component identifier, matches SOS */
		ret = coda_jpeg_put_byte(i + 1, &stream);
		if (ret < 0)
			return ret;
		ret = coda_jpeg_put_byte(subsampling[chroma_format][i],
					 &stream);
		if (ret < 0)
			return ret;
		/* Chroma table index */
		ret = coda_jpeg_put_byte((i == 0) ? 0 : 1, &stream);
		if (ret < 0)
			return ret;
	}

	/* Pad to multiple of 8 bytes */
	pad = (stream.curr - buf) % 8;
	if (pad) {
		pad = 8 - pad;
		while (pad--) {
			ret = coda_jpeg_put_byte(0x00, &stream);
			if (ret < 0)
				return ret;
		}
	}

	return stream.curr - buf;
}

/*
 * Scale quantization table using nonlinear scaling factor
 * u8 qtab[64], scale [50,190]
 */
static void coda_scale_quant_table(u8 *q_tab, int scale)
{
	unsigned int temp;
	int i;

	for (i = 0; i < 64; i++) {
		temp = DIV_ROUND_CLOSEST((unsigned int)q_tab[i] * scale, 100);
		if (temp <= 0)
			temp = 1;
		if (temp > 255)
			temp = 255;
		q_tab[i] = (unsigned char)temp;
	}
}

void coda_set_jpeg_compression_quality(struct coda_ctx *ctx, int quality)
{
	unsigned int scale;

	ctx->params.jpeg_quality = quality;

	/* Clip quality setting to [5,100] interval */
	if (quality > 100)
		quality = 100;
	if (quality < 5)
		quality = 5;

	/*
	 * Non-linear scaling factor:
	 * [5,50] -> [1000..100], [51,100] -> [98..0]
	 */
	if (quality < 50)
		scale = 5000 / quality;
	else
		scale = 200 - 2 * quality;

	if (ctx->params.jpeg_qmat_tab[0]) {
		memcpy(ctx->params.jpeg_qmat_tab[0], luma_q, 64);
		coda_scale_quant_table(ctx->params.jpeg_qmat_tab[0], scale);
	}
	if (ctx->params.jpeg_qmat_tab[1]) {
		memcpy(ctx->params.jpeg_qmat_tab[1], chroma_q, 64);
		coda_scale_quant_table(ctx->params.jpeg_qmat_tab[1], scale);
	}
}

/*
 * Encoder context operations
 */

static int coda9_jpeg_start_encoding(struct coda_ctx *ctx)
{
	struct coda_dev *dev = ctx->dev;
	int ret;

	ret = coda9_jpeg_load_huff_tab(ctx);
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev, "error loading Huffman tables\n");
		return ret;
	}
	if (!ctx->params.jpeg_qmat_tab[0])
		ctx->params.jpeg_qmat_tab[0] = kmalloc(64, GFP_KERNEL);
	if (!ctx->params.jpeg_qmat_tab[1])
		ctx->params.jpeg_qmat_tab[1] = kmalloc(64, GFP_KERNEL);
	coda_set_jpeg_compression_quality(ctx, ctx->params.jpeg_quality);

	return 0;
}

static int coda9_jpeg_prepare_encode(struct coda_ctx *ctx)
{
	struct coda_q_data *q_data_src;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct coda_dev *dev = ctx->dev;
	u32 start_addr, end_addr;
	u16 aligned_width, aligned_height;
	bool chroma_interleave;
	int chroma_format;
	int header_len;
	int ret;
	ktime_t timeout;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);

	if (vb2_get_plane_payload(&src_buf->vb2_buf, 0) == 0)
		vb2_set_plane_payload(&src_buf->vb2_buf, 0,
				      vb2_plane_size(&src_buf->vb2_buf, 0));

	src_buf->sequence = ctx->osequence;
	dst_buf->sequence = ctx->osequence;
	ctx->osequence++;

	src_buf->flags |= V4L2_BUF_FLAG_KEYFRAME;
	src_buf->flags &= ~V4L2_BUF_FLAG_PFRAME;

	coda_set_gdi_regs(ctx);

	start_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	end_addr = start_addr + vb2_plane_size(&dst_buf->vb2_buf, 0);

	chroma_format = coda9_jpeg_chroma_format(q_data_src->fourcc);
	if (chroma_format < 0)
		return chroma_format;

	/* Round image dimensions to multiple of MCU size */
	aligned_width = round_up(q_data_src->width, width_align[chroma_format]);
	aligned_height = round_up(q_data_src->height,
				  height_align[chroma_format]);
	if (aligned_width != q_data_src->bytesperline) {
		v4l2_err(&dev->v4l2_dev, "wrong stride: %d instead of %d\n",
			 aligned_width, q_data_src->bytesperline);
	}

	header_len =
		coda9_jpeg_encode_header(ctx,
					 vb2_plane_size(&dst_buf->vb2_buf, 0),
					 vb2_plane_vaddr(&dst_buf->vb2_buf, 0));
	if (header_len < 0)
		return header_len;

	coda_write(dev, start_addr + header_len, CODA9_REG_JPEG_BBC_BAS_ADDR);
	coda_write(dev, end_addr, CODA9_REG_JPEG_BBC_END_ADDR);
	coda_write(dev, start_addr + header_len, CODA9_REG_JPEG_BBC_WR_PTR);
	coda_write(dev, start_addr + header_len, CODA9_REG_JPEG_BBC_RD_PTR);
	coda_write(dev, 0, CODA9_REG_JPEG_BBC_CUR_POS);
	/* 64 words per 256-byte page */
	coda_write(dev, 64, CODA9_REG_JPEG_BBC_DATA_CNT);
	coda_write(dev, start_addr, CODA9_REG_JPEG_BBC_EXT_ADDR);
	coda_write(dev, 0, CODA9_REG_JPEG_BBC_INT_ADDR);

	coda_write(dev, 0, CODA9_REG_JPEG_GBU_BT_PTR);
	coda_write(dev, 0, CODA9_REG_JPEG_GBU_WD_PTR);
	coda_write(dev, 0, CODA9_REG_JPEG_GBU_BBSR);
	coda_write(dev, 0, CODA9_REG_JPEG_BBC_STRM_CTRL);
	coda_write(dev, 0, CODA9_REG_JPEG_GBU_CTRL);
	coda_write(dev, 0, CODA9_REG_JPEG_GBU_FF_RPTR);
	coda_write(dev, 127, CODA9_REG_JPEG_GBU_BBER);
	coda_write(dev, 64, CODA9_REG_JPEG_GBU_BBIR);
	coda_write(dev, 64, CODA9_REG_JPEG_GBU_BBHR);

	chroma_interleave = (q_data_src->fourcc == V4L2_PIX_FMT_NV12);
	coda_write(dev, CODA9_JPEG_PIC_CTRL_TC_DIRECTION |
		   CODA9_JPEG_PIC_CTRL_ENCODER_EN, CODA9_REG_JPEG_PIC_CTRL);
	coda_write(dev, 0, CODA9_REG_JPEG_SCL_INFO);
	coda_write(dev, chroma_interleave, CODA9_REG_JPEG_DPB_CONFIG);
	coda_write(dev, ctx->params.jpeg_restart_interval,
		   CODA9_REG_JPEG_RST_INTVAL);
	coda_write(dev, 1, CODA9_REG_JPEG_BBC_CTRL);

	coda_write(dev, bus_req_num[chroma_format], CODA9_REG_JPEG_OP_INFO);

	coda9_jpeg_write_huff_tab(ctx);
	coda9_jpeg_load_qmat_tab(ctx);

	if (ctx->params.rot_mode & CODA_ROT_90) {
		aligned_width = aligned_height;
		aligned_height = q_data_src->bytesperline;
		if (chroma_format == CODA9_JPEG_FORMAT_422)
			chroma_format = CODA9_JPEG_FORMAT_224;
		else if (chroma_format == CODA9_JPEG_FORMAT_224)
			chroma_format = CODA9_JPEG_FORMAT_422;
	}
	/* These need to be multiples of MCU size */
	coda_write(dev, aligned_width << 16 | aligned_height,
		   CODA9_REG_JPEG_PIC_SIZE);
	coda_write(dev, ctx->params.rot_mode ?
		   (CODA_ROT_MIR_ENABLE | ctx->params.rot_mode) : 0,
		   CODA9_REG_JPEG_ROT_INFO);

	coda_write(dev, mcu_info[chroma_format], CODA9_REG_JPEG_MCU_INFO);

	coda_write(dev, 1, CODA9_GDI_CONTROL);
	timeout = ktime_add_us(ktime_get(), 100000);
	do {
		ret = coda_read(dev, CODA9_GDI_STATUS);
		if (ktime_compare(ktime_get(), timeout) > 0) {
			v4l2_err(&dev->v4l2_dev, "timeout waiting for GDI\n");
			return -ETIMEDOUT;
		}
	} while (!ret);

	coda_write(dev, (chroma_format << 17) | (chroma_interleave << 16) |
		   q_data_src->bytesperline, CODA9_GDI_INFO_CONTROL);
	/* The content of this register seems to be irrelevant: */
	coda_write(dev, aligned_width << 16 | aligned_height,
		   CODA9_GDI_INFO_PIC_SIZE);

	coda_write_base(ctx, q_data_src, src_buf, CODA9_GDI_INFO_BASE_Y);

	coda_write(dev, 0, CODA9_REG_JPEG_DPB_BASE00);
	coda_write(dev, 0, CODA9_GDI_CONTROL);
	coda_write(dev, 1, CODA9_GDI_PIC_INIT_HOST);

	coda_write(dev, 1, CODA9_GDI_WPROT_ERR_CLR);
	coda_write(dev, 0, CODA9_GDI_WPROT_RGN_EN);

	trace_coda_jpeg_run(ctx, src_buf);

	coda_write(dev, 1, CODA9_REG_JPEG_PIC_START);

	return 0;
}

static void coda9_jpeg_finish_encode(struct coda_ctx *ctx)
{
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct coda_dev *dev = ctx->dev;
	u32 wr_ptr, start_ptr;
	u32 err_mb;

	if (ctx->aborting) {
		coda_write(ctx->dev, 0, CODA9_REG_JPEG_BBC_FLUSH_CMD);
		return;
	}

	/*
	 * Lock to make sure that an encoder stop command running in parallel
	 * will either already have marked src_buf as last, or it will wake up
	 * the capture queue after the buffers are returned.
	 */
	mutex_lock(&ctx->wakeup_mutex);
	src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	trace_coda_jpeg_done(ctx, dst_buf);

	/*
	 * Set plane payload to the number of bytes written out
	 * by the JPEG processing unit
	 */
	start_ptr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	wr_ptr = coda_read(dev, CODA9_REG_JPEG_BBC_WR_PTR);
	vb2_set_plane_payload(&dst_buf->vb2_buf, 0, wr_ptr - start_ptr);

	err_mb = coda_read(dev, CODA9_REG_JPEG_PIC_ERRMB);
	if (err_mb)
		coda_dbg(1, ctx, "ERRMB: 0x%x\n", err_mb);

	coda_write(dev, 0, CODA9_REG_JPEG_BBC_FLUSH_CMD);

	dst_buf->flags &= ~(V4L2_BUF_FLAG_PFRAME | V4L2_BUF_FLAG_LAST);
	dst_buf->flags |= V4L2_BUF_FLAG_KEYFRAME;
	dst_buf->flags |= src_buf->flags & V4L2_BUF_FLAG_LAST;

	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, false);

	v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
	coda_m2m_buf_done(ctx, dst_buf, err_mb ? VB2_BUF_STATE_ERROR :
						 VB2_BUF_STATE_DONE);
	mutex_unlock(&ctx->wakeup_mutex);

	coda_dbg(1, ctx, "job finished: encoded frame (%u)%s\n",
		 dst_buf->sequence,
		 (dst_buf->flags & V4L2_BUF_FLAG_LAST) ? " (last)" : "");
}

static void coda9_jpeg_release(struct coda_ctx *ctx)
{
	int i;

	if (ctx->params.jpeg_qmat_tab[0] == luma_q)
		ctx->params.jpeg_qmat_tab[0] = NULL;
	if (ctx->params.jpeg_qmat_tab[1] == chroma_q)
		ctx->params.jpeg_qmat_tab[1] = NULL;
	for (i = 0; i < 3; i++)
		kfree(ctx->params.jpeg_qmat_tab[i]);
	kfree(ctx->params.jpeg_huff_data);
}

const struct coda_context_ops coda9_jpeg_encode_ops = {
	.queue_init = coda_encoder_queue_init,
	.start_streaming = coda9_jpeg_start_encoding,
	.prepare_run = coda9_jpeg_prepare_encode,
	.finish_run = coda9_jpeg_finish_encode,
	.release = coda9_jpeg_release,
};

irqreturn_t coda9_jpeg_irq_handler(int irq, void *data)
{
	struct coda_dev *dev = data;
	struct coda_ctx *ctx;
	int status;
	int err_mb;

	status = coda_read(dev, CODA9_REG_JPEG_PIC_STATUS);
	if (status == 0)
		return IRQ_HANDLED;
	coda_write(dev, status, CODA9_REG_JPEG_PIC_STATUS);

	if (status & CODA9_JPEG_STATUS_OVERFLOW)
		v4l2_err(&dev->v4l2_dev, "JPEG overflow\n");

	if (status & CODA9_JPEG_STATUS_BBC_INT)
		v4l2_err(&dev->v4l2_dev, "JPEG BBC interrupt\n");

	if (status & CODA9_JPEG_STATUS_ERROR) {
		v4l2_err(&dev->v4l2_dev, "JPEG error\n");

		err_mb = coda_read(dev, CODA9_REG_JPEG_PIC_ERRMB);
		if (err_mb) {
			v4l2_err(&dev->v4l2_dev,
				 "ERRMB: 0x%x: rst idx %d, mcu pos (%d,%d)\n",
				 err_mb, err_mb >> 24, (err_mb >> 12) & 0xfff,
				 err_mb & 0xfff);
		}
	}

	ctx = v4l2_m2m_get_curr_priv(dev->m2m_dev);
	if (!ctx) {
		v4l2_err(&dev->v4l2_dev,
			 "Instance released before the end of transaction\n");
		mutex_unlock(&dev->coda_mutex);
		return IRQ_HANDLED;
	}

	complete(&ctx->completion);

	return IRQ_HANDLED;
}
