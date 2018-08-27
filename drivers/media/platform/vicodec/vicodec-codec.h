/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2016 Tom aan de Wiel
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef VICODEC_RLC_H
#define VICODEC_RLC_H

#include <linux/types.h>
#include <linux/bitops.h>
#include <asm/byteorder.h>

/*
 * The compressed format consists of a cframe_hdr struct followed by the
 * compressed frame data. The header contains the size of that data.
 * Each Y, Cb and Cr plane is compressed separately. If the compressed
 * size of each plane becomes larger than the uncompressed size, then
 * that plane is stored uncompressed and the corresponding bit is set
 * in the flags field of the header.
 *
 * Each compressed plane consists of macroblocks and each macroblock
 * is run-length-encoded. Each macroblock starts with a 16 bit value.
 * Bit 15 indicates if this is a P-coded macroblock (1) or not (0).
 * P-coded macroblocks contain a delta against the previous frame.
 *
 * Bits 1-12 contain a number. If non-zero, then this same macroblock
 * repeats that number of times. This results in a high degree of
 * compression for generated images like colorbars.
 *
 * Following this macroblock header the MB coefficients are run-length
 * encoded: the top 12 bits contain the coefficient, the bottom 4 bits
 * tell how many times this coefficient occurs. The value 0xf indicates
 * that the remainder of the macroblock should be filled with zeroes.
 *
 * All 16 and 32 bit values are stored in big-endian (network) order.
 *
 * Each cframe_hdr starts with an 8 byte magic header that is
 * guaranteed not to occur in the compressed frame data. This header
 * can be used to sync to the next frame.
 *
 * This codec uses the Fast Walsh Hadamard Transform. Tom aan de Wiel
 * developed this as part of a university project, specifically for use
 * with this driver. His project report can be found here:
 *
 * https://hverkuil.home.xs4all.nl/fwht.pdf
 */

/*
 * Note: bit 0 of the header must always be 0. Otherwise it cannot
 * be guaranteed that the magic 8 byte sequence (see below) can
 * never occur in the rlc output.
 */
#define PFRAME_BIT (1 << 15)
#define DUPS_MASK 0x1ffe

/*
 * This is a sequence of 8 bytes with the low 4 bits set to 0xf.
 *
 * This sequence cannot occur in the encoded data
 */
#define VICODEC_MAGIC1 0x4f4f4f4f
#define VICODEC_MAGIC2 0xffffffff

#define VICODEC_VERSION 1

#define VICODEC_MAX_WIDTH 3840
#define VICODEC_MAX_HEIGHT 2160
#define VICODEC_MIN_WIDTH 640
#define VICODEC_MIN_HEIGHT 480

#define PBLOCK 0
#define IBLOCK 1

/* Set if this is an interlaced format */
#define VICODEC_FL_IS_INTERLACED	BIT(0)
/* Set if this is a bottom-first (NTSC) interlaced format */
#define VICODEC_FL_IS_BOTTOM_FIRST	BIT(1)
/* Set if each 'frame' contains just one field */
#define VICODEC_FL_IS_ALTERNATE		BIT(2)
/*
 * If VICODEC_FL_IS_ALTERNATE was set, then this is set if this
 * 'frame' is the bottom field, else it is the top field.
 */
#define VICODEC_FL_IS_BOTTOM_FIELD	BIT(3)
/* Set if this frame is uncompressed */
#define VICODEC_FL_LUMA_IS_UNCOMPRESSED	BIT(4)
#define VICODEC_FL_CB_IS_UNCOMPRESSED	BIT(5)
#define VICODEC_FL_CR_IS_UNCOMPRESSED	BIT(6)

struct cframe_hdr {
	u32 magic1;
	u32 magic2;
	__be32 version;
	__be32 width, height;
	__be32 flags;
	__be32 colorspace;
	__be32 xfer_func;
	__be32 ycbcr_enc;
	__be32 quantization;
	__be32 size;
};

struct cframe {
	unsigned int width, height;
	__be16 *rlc_data;
	s16 coeffs[8 * 8];
	s16 de_coeffs[8 * 8];
	s16 de_fwht[8 * 8];
	u32 size;
};

struct raw_frame {
	unsigned int width, height;
	unsigned int chroma_step;
	u8 *luma, *cb, *cr;
};

#define FRAME_PCODED	BIT(0)
#define FRAME_UNENCODED	BIT(1)
#define LUMA_UNENCODED	BIT(2)
#define CB_UNENCODED	BIT(3)
#define CR_UNENCODED	BIT(4)

u32 encode_frame(struct raw_frame *frm, struct raw_frame *ref_frm,
		 struct cframe *cf, bool is_intra, bool next_is_intra);
void decode_frame(struct cframe *cf, struct raw_frame *ref, u32 hdr_flags);

#endif
