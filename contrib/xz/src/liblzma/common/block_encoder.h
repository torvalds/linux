///////////////////////////////////////////////////////////////////////////////
//
/// \file       block_encoder.h
/// \brief      Encodes .xz Blocks
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_BLOCK_ENCODER_H
#define LZMA_BLOCK_ENCODER_H

#include "common.h"


/// \brief      Biggest Compressed Size value that the Block encoder supports
///
/// The maximum size of a single Block is limited by the maximum size of
/// a Stream, which in theory is 2^63 - 3 bytes (i.e. LZMA_VLI_MAX - 3).
/// While the size is really big and no one should hit it in practice, we
/// take it into account in some places anyway to catch some errors e.g. if
/// application passes insanely big value to some function.
///
/// We could take into account the headers etc. to determine the exact
/// maximum size of the Compressed Data field, but the complexity would give
/// us nothing useful. Instead, limit the size of Compressed Data so that
/// even with biggest possible Block Header and Check fields the total
/// encoded size of the Block stays as a valid VLI. This doesn't guarantee
/// that the size of the Stream doesn't grow too big, but that problem is
/// taken care outside the Block handling code.
///
/// ~LZMA_VLI_C(3) is to guarantee that if we need padding at the end of
/// the Compressed Data field, it will still stay in the proper limit.
///
/// This constant is in this file because it is needed in both
/// block_encoder.c and block_buffer_encoder.c.
#define COMPRESSED_SIZE_MAX ((LZMA_VLI_MAX - LZMA_BLOCK_HEADER_SIZE_MAX \
		- LZMA_CHECK_SIZE_MAX) & ~LZMA_VLI_C(3))


extern lzma_ret lzma_block_encoder_init(lzma_next_coder *next,
		const lzma_allocator *allocator, lzma_block *block);

#endif
