/* SPDX-License-Identifier: LGPL-2.1+ */
/*
 * Copyright 2016 Tom aan de Wiel
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef CODEC_FWHT_H
#define CODEC_FWHT_H

#include <linux/types.h>
#include <linux/bitops.h>
#include <asm/byteorder.h>

/*
 * The compressed format consists of a fwht_cframe_hdr struct followed by the
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
 * Each fwht_cframe_hdr starts with an 8 byte magic header that is
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
 * This is a sequence of 8 bytes with the low 4 bits set to 0xf.
 *
 * This sequence cannot occur in the encoded data
 *
 * Note that these two magic values are symmetrical so endian issues here.
 */
#define FWHT_MAGIC1 0x4f4f4f4f
#define FWHT_MAGIC2 0xffffffff

#define FWHT_VERSION 1

/* Set if this is an interlaced format */
#define FWHT_FL_IS_INTERLACED		BIT(0)
/* Set if this is a bottom-first (NTSC) interlaced format */
#define FWHT_FL_IS_BOTTOM_FIRST		BIT(1)
/* Set if each 'frame' contains just one field */
#define FWHT_FL_IS_ALTERNATE		BIT(2)
/*
 * If FWHT_FL_IS_ALTERNATE was set, then this is set if this
 * 'frame' is the bottom field, else it is the top field.
 */
#define FWHT_FL_IS_BOTTOM_FIELD		BIT(3)
/* Set if this frame is uncompressed */
#define FWHT_FL_LUMA_IS_UNCOMPRESSED	BIT(4)
#define FWHT_FL_CB_IS_UNCOMPRESSED	BIT(5)
#define FWHT_FL_CR_IS_UNCOMPRESSED	BIT(6)
#define FWHT_FL_CHROMA_FULL_HEIGHT	BIT(7)
#define FWHT_FL_CHROMA_FULL_WIDTH	BIT(8)

struct fwht_cframe_hdr {
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

struct fwht_cframe {
	unsigned int width, height;
	u16 i_frame_qp;
	u16 p_frame_qp;
	__be16 *rlc_data;
	s16 coeffs[8 * 8];
	s16 de_coeffs[8 * 8];
	s16 de_fwht[8 * 8];
	u32 size;
};

struct fwht_raw_frame {
	unsigned int width, height;
	unsigned int width_div;
	unsigned int height_div;
	unsigned int luma_step;
	unsigned int chroma_step;
	u8 *luma, *cb, *cr;
};

#define FWHT_FRAME_PCODED	BIT(0)
#define FWHT_FRAME_UNENCODED	BIT(1)
#define FWHT_LUMA_UNENCODED	BIT(2)
#define FWHT_CB_UNENCODED	BIT(3)
#define FWHT_CR_UNENCODED	BIT(4)

u32 fwht_encode_frame(struct fwht_raw_frame *frm,
		      struct fwht_raw_frame *ref_frm,
		      struct fwht_cframe *cf,
		      bool is_intra, bool next_is_intra);
void fwht_decode_frame(struct fwht_cframe *cf, struct fwht_raw_frame *ref,
		       u32 hdr_flags);

#endif
