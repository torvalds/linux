///////////////////////////////////////////////////////////////////////////////
//
/// \file       block_header_encoder.c
/// \brief      Encodes Block Header for .xz files
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "check.h"


extern LZMA_API(lzma_ret)
lzma_block_header_size(lzma_block *block)
{
	if (block->version > 1)
		return LZMA_OPTIONS_ERROR;

	// Block Header Size + Block Flags + CRC32.
	uint32_t size = 1 + 1 + 4;

	// Compressed Size
	if (block->compressed_size != LZMA_VLI_UNKNOWN) {
		const uint32_t add = lzma_vli_size(block->compressed_size);
		if (add == 0 || block->compressed_size == 0)
			return LZMA_PROG_ERROR;

		size += add;
	}

	// Uncompressed Size
	if (block->uncompressed_size != LZMA_VLI_UNKNOWN) {
		const uint32_t add = lzma_vli_size(block->uncompressed_size);
		if (add == 0)
			return LZMA_PROG_ERROR;

		size += add;
	}

	// List of Filter Flags
	if (block->filters == NULL || block->filters[0].id == LZMA_VLI_UNKNOWN)
		return LZMA_PROG_ERROR;

	for (size_t i = 0; block->filters[i].id != LZMA_VLI_UNKNOWN; ++i) {
		// Don't allow too many filters.
		if (i == LZMA_FILTERS_MAX)
			return LZMA_PROG_ERROR;

		uint32_t add;
		return_if_error(lzma_filter_flags_size(&add,
				block->filters + i));

		size += add;
	}

	// Pad to a multiple of four bytes.
	block->header_size = (size + 3) & ~UINT32_C(3);

	// NOTE: We don't verify that the encoded size of the Block stays
	// within limits. This is because it is possible that we are called
	// with exaggerated Compressed Size (e.g. LZMA_VLI_MAX) to reserve
	// space for Block Header, and later called again with lower,
	// real values.

	return LZMA_OK;
}


extern LZMA_API(lzma_ret)
lzma_block_header_encode(const lzma_block *block, uint8_t *out)
{
	// Validate everything but filters.
	if (lzma_block_unpadded_size(block) == 0
			|| !lzma_vli_is_valid(block->uncompressed_size))
		return LZMA_PROG_ERROR;

	// Indicate the size of the buffer _excluding_ the CRC32 field.
	const size_t out_size = block->header_size - 4;

	// Store the Block Header Size.
	out[0] = out_size / 4;

	// We write Block Flags in pieces.
	out[1] = 0x00;
	size_t out_pos = 2;

	// Compressed Size
	if (block->compressed_size != LZMA_VLI_UNKNOWN) {
		return_if_error(lzma_vli_encode(block->compressed_size, NULL,
				out, &out_pos, out_size));

		out[1] |= 0x40;
	}

	// Uncompressed Size
	if (block->uncompressed_size != LZMA_VLI_UNKNOWN) {
		return_if_error(lzma_vli_encode(block->uncompressed_size, NULL,
				out, &out_pos, out_size));

		out[1] |= 0x80;
	}

	// Filter Flags
	if (block->filters == NULL || block->filters[0].id == LZMA_VLI_UNKNOWN)
		return LZMA_PROG_ERROR;

	size_t filter_count = 0;
	do {
		// There can be a maximum of four filters.
		if (filter_count == LZMA_FILTERS_MAX)
			return LZMA_PROG_ERROR;

		return_if_error(lzma_filter_flags_encode(
				block->filters + filter_count,
				out, &out_pos, out_size));

	} while (block->filters[++filter_count].id != LZMA_VLI_UNKNOWN);

	out[1] |= filter_count - 1;

	// Padding
	memzero(out + out_pos, out_size - out_pos);

	// CRC32
	unaligned_write32le(out + out_size, lzma_crc32(out, out_size, 0));

	return LZMA_OK;
}
