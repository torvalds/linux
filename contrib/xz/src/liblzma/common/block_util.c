///////////////////////////////////////////////////////////////////////////////
//
/// \file       block_header.c
/// \brief      Utility functions to handle lzma_block
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "index.h"


extern LZMA_API(lzma_ret)
lzma_block_compressed_size(lzma_block *block, lzma_vli unpadded_size)
{
	// Validate everything but Uncompressed Size and filters.
	if (lzma_block_unpadded_size(block) == 0)
		return LZMA_PROG_ERROR;

	const uint32_t container_size = block->header_size
			+ lzma_check_size(block->check);

	// Validate that Compressed Size will be greater than zero.
	if (unpadded_size <= container_size)
		return LZMA_DATA_ERROR;

	// Calculate what Compressed Size is supposed to be.
	// If Compressed Size was present in Block Header,
	// compare that the new value matches it.
	const lzma_vli compressed_size = unpadded_size - container_size;
	if (block->compressed_size != LZMA_VLI_UNKNOWN
			&& block->compressed_size != compressed_size)
		return LZMA_DATA_ERROR;

	block->compressed_size = compressed_size;

	return LZMA_OK;
}


extern LZMA_API(lzma_vli)
lzma_block_unpadded_size(const lzma_block *block)
{
	// Validate the values that we are interested in i.e. all but
	// Uncompressed Size and the filters.
	//
	// NOTE: This function is used for validation too, so it is
	// essential that these checks are always done even if
	// Compressed Size is unknown.
	if (block == NULL || block->version > 1
			|| block->header_size < LZMA_BLOCK_HEADER_SIZE_MIN
			|| block->header_size > LZMA_BLOCK_HEADER_SIZE_MAX
			|| (block->header_size & 3)
			|| !lzma_vli_is_valid(block->compressed_size)
			|| block->compressed_size == 0
			|| (unsigned int)(block->check) > LZMA_CHECK_ID_MAX)
		return 0;

	// If Compressed Size is unknown, return that we cannot know
	// size of the Block either.
	if (block->compressed_size == LZMA_VLI_UNKNOWN)
		return LZMA_VLI_UNKNOWN;

	// Calculate Unpadded Size and validate it.
	const lzma_vli unpadded_size = block->compressed_size
				+ block->header_size
				+ lzma_check_size(block->check);

	assert(unpadded_size >= UNPADDED_SIZE_MIN);
	if (unpadded_size > UNPADDED_SIZE_MAX)
		return 0;

	return unpadded_size;
}


extern LZMA_API(lzma_vli)
lzma_block_total_size(const lzma_block *block)
{
	lzma_vli unpadded_size = lzma_block_unpadded_size(block);

	if (unpadded_size != LZMA_VLI_UNKNOWN)
		unpadded_size = vli_ceil4(unpadded_size);

	return unpadded_size;
}
