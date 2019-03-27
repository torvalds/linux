///////////////////////////////////////////////////////////////////////////////
//
/// \file       vli_encoder.c
/// \brief      Encodes variable-length integers
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "common.h"


extern LZMA_API(lzma_ret)
lzma_vli_encode(lzma_vli vli, size_t *vli_pos,
		uint8_t *restrict out, size_t *restrict out_pos,
		size_t out_size)
{
	// If we haven't been given vli_pos, work in single-call mode.
	size_t vli_pos_internal = 0;
	if (vli_pos == NULL) {
		vli_pos = &vli_pos_internal;

		// In single-call mode, we expect that the caller has
		// reserved enough output space.
		if (*out_pos >= out_size)
			return LZMA_PROG_ERROR;
	} else {
		// This never happens when we are called by liblzma, but
		// may happen if called directly from an application.
		if (*out_pos >= out_size)
			return LZMA_BUF_ERROR;
	}

	// Validate the arguments.
	if (*vli_pos >= LZMA_VLI_BYTES_MAX || vli > LZMA_VLI_MAX)
		return LZMA_PROG_ERROR;

	// Shift vli so that the next bits to encode are the lowest. In
	// single-call mode this never changes vli since *vli_pos is zero.
	vli >>= *vli_pos * 7;

	// Write the non-last bytes in a loop.
	while (vli >= 0x80) {
		// We don't need *vli_pos during this function call anymore,
		// but update it here so that it is ready if we need to
		// return before the whole integer has been decoded.
		++*vli_pos;
		assert(*vli_pos < LZMA_VLI_BYTES_MAX);

		// Write the next byte.
		out[*out_pos] = (uint8_t)(vli) | 0x80;
		vli >>= 7;

		if (++*out_pos == out_size)
			return vli_pos == &vli_pos_internal
					? LZMA_PROG_ERROR : LZMA_OK;
	}

	// Write the last byte.
	out[*out_pos] = (uint8_t)(vli);
	++*out_pos;
	++*vli_pos;

	return vli_pos == &vli_pos_internal ? LZMA_OK : LZMA_STREAM_END;

}
