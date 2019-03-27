///////////////////////////////////////////////////////////////////////////////
//
/// \file       stream_flags_encoder.c
/// \brief      Encodes Stream Header and Stream Footer for .xz files
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "stream_flags_common.h"


static bool
stream_flags_encode(const lzma_stream_flags *options, uint8_t *out)
{
	if ((unsigned int)(options->check) > LZMA_CHECK_ID_MAX)
		return true;

	out[0] = 0x00;
	out[1] = options->check;

	return false;
}


extern LZMA_API(lzma_ret)
lzma_stream_header_encode(const lzma_stream_flags *options, uint8_t *out)
{
	assert(sizeof(lzma_header_magic) + LZMA_STREAM_FLAGS_SIZE
			+ 4 == LZMA_STREAM_HEADER_SIZE);

	if (options->version != 0)
		return LZMA_OPTIONS_ERROR;

	// Magic
	memcpy(out, lzma_header_magic, sizeof(lzma_header_magic));

	// Stream Flags
	if (stream_flags_encode(options, out + sizeof(lzma_header_magic)))
		return LZMA_PROG_ERROR;

	// CRC32 of the Stream Header
	const uint32_t crc = lzma_crc32(out + sizeof(lzma_header_magic),
			LZMA_STREAM_FLAGS_SIZE, 0);

	unaligned_write32le(out + sizeof(lzma_header_magic)
			+ LZMA_STREAM_FLAGS_SIZE, crc);

	return LZMA_OK;
}


extern LZMA_API(lzma_ret)
lzma_stream_footer_encode(const lzma_stream_flags *options, uint8_t *out)
{
	assert(2 * 4 + LZMA_STREAM_FLAGS_SIZE + sizeof(lzma_footer_magic)
			== LZMA_STREAM_HEADER_SIZE);

	if (options->version != 0)
		return LZMA_OPTIONS_ERROR;

	// Backward Size
	if (!is_backward_size_valid(options))
		return LZMA_PROG_ERROR;

	unaligned_write32le(out + 4, options->backward_size / 4 - 1);

	// Stream Flags
	if (stream_flags_encode(options, out + 2 * 4))
		return LZMA_PROG_ERROR;

	// CRC32
	const uint32_t crc = lzma_crc32(
			out + 4, 4 + LZMA_STREAM_FLAGS_SIZE, 0);

	unaligned_write32le(out, crc);

	// Magic
	memcpy(out + 2 * 4 + LZMA_STREAM_FLAGS_SIZE,
			lzma_footer_magic, sizeof(lzma_footer_magic));

	return LZMA_OK;
}
