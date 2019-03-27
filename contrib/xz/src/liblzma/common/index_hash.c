///////////////////////////////////////////////////////////////////////////////
//
/// \file       index_hash.c
/// \brief      Validates Index by using a hash function
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "index.h"
#include "check.h"


typedef struct {
	/// Sum of the Block sizes (including Block Padding)
	lzma_vli blocks_size;

	/// Sum of the Uncompressed Size fields
	lzma_vli uncompressed_size;

	/// Number of Records
	lzma_vli count;

	/// Size of the List of Index Records as bytes
	lzma_vli index_list_size;

	/// Check calculated from Unpadded Sizes and Uncompressed Sizes.
	lzma_check_state check;

} lzma_index_hash_info;


struct lzma_index_hash_s {
	enum {
		SEQ_BLOCK,
		SEQ_COUNT,
		SEQ_UNPADDED,
		SEQ_UNCOMPRESSED,
		SEQ_PADDING_INIT,
		SEQ_PADDING,
		SEQ_CRC32,
	} sequence;

	/// Information collected while decoding the actual Blocks.
	lzma_index_hash_info blocks;

	/// Information collected from the Index field.
	lzma_index_hash_info records;

	/// Number of Records not fully decoded
	lzma_vli remaining;

	/// Unpadded Size currently being read from an Index Record.
	lzma_vli unpadded_size;

	/// Uncompressed Size currently being read from an Index Record.
	lzma_vli uncompressed_size;

	/// Position in variable-length integers when decoding them from
	/// the List of Records.
	size_t pos;

	/// CRC32 of the Index
	uint32_t crc32;
};


extern LZMA_API(lzma_index_hash *)
lzma_index_hash_init(lzma_index_hash *index_hash,
		const lzma_allocator *allocator)
{
	if (index_hash == NULL) {
		index_hash = lzma_alloc(sizeof(lzma_index_hash), allocator);
		if (index_hash == NULL)
			return NULL;
	}

	index_hash->sequence = SEQ_BLOCK;
	index_hash->blocks.blocks_size = 0;
	index_hash->blocks.uncompressed_size = 0;
	index_hash->blocks.count = 0;
	index_hash->blocks.index_list_size = 0;
	index_hash->records.blocks_size = 0;
	index_hash->records.uncompressed_size = 0;
	index_hash->records.count = 0;
	index_hash->records.index_list_size = 0;
	index_hash->unpadded_size = 0;
	index_hash->uncompressed_size = 0;
	index_hash->pos = 0;
	index_hash->crc32 = 0;

	// These cannot fail because LZMA_CHECK_BEST is known to be supported.
	(void)lzma_check_init(&index_hash->blocks.check, LZMA_CHECK_BEST);
	(void)lzma_check_init(&index_hash->records.check, LZMA_CHECK_BEST);

	return index_hash;
}


extern LZMA_API(void)
lzma_index_hash_end(lzma_index_hash *index_hash,
		const lzma_allocator *allocator)
{
	lzma_free(index_hash, allocator);
	return;
}


extern LZMA_API(lzma_vli)
lzma_index_hash_size(const lzma_index_hash *index_hash)
{
	// Get the size of the Index from ->blocks instead of ->records for
	// cases where application wants to know the Index Size before
	// decoding the Index.
	return index_size(index_hash->blocks.count,
			index_hash->blocks.index_list_size);
}


/// Updates the sizes and the hash without any validation.
static lzma_ret
hash_append(lzma_index_hash_info *info, lzma_vli unpadded_size,
		lzma_vli uncompressed_size)
{
	info->blocks_size += vli_ceil4(unpadded_size);
	info->uncompressed_size += uncompressed_size;
	info->index_list_size += lzma_vli_size(unpadded_size)
			+ lzma_vli_size(uncompressed_size);
	++info->count;

	const lzma_vli sizes[2] = { unpadded_size, uncompressed_size };
	lzma_check_update(&info->check, LZMA_CHECK_BEST,
			(const uint8_t *)(sizes), sizeof(sizes));

	return LZMA_OK;
}


extern LZMA_API(lzma_ret)
lzma_index_hash_append(lzma_index_hash *index_hash, lzma_vli unpadded_size,
		lzma_vli uncompressed_size)
{
	// Validate the arguments.
	if (index_hash->sequence != SEQ_BLOCK
			|| unpadded_size < UNPADDED_SIZE_MIN
			|| unpadded_size > UNPADDED_SIZE_MAX
			|| uncompressed_size > LZMA_VLI_MAX)
		return LZMA_PROG_ERROR;

	// Update the hash.
	return_if_error(hash_append(&index_hash->blocks,
			unpadded_size, uncompressed_size));

	// Validate the properties of *info are still in allowed limits.
	if (index_hash->blocks.blocks_size > LZMA_VLI_MAX
			|| index_hash->blocks.uncompressed_size > LZMA_VLI_MAX
			|| index_size(index_hash->blocks.count,
					index_hash->blocks.index_list_size)
				> LZMA_BACKWARD_SIZE_MAX
			|| index_stream_size(index_hash->blocks.blocks_size,
					index_hash->blocks.count,
					index_hash->blocks.index_list_size)
				> LZMA_VLI_MAX)
		return LZMA_DATA_ERROR;

	return LZMA_OK;
}


extern LZMA_API(lzma_ret)
lzma_index_hash_decode(lzma_index_hash *index_hash, const uint8_t *in,
		size_t *in_pos, size_t in_size)
{
	// Catch zero input buffer here, because in contrast to Index encoder
	// and decoder functions, applications call this function directly
	// instead of via lzma_code(), which does the buffer checking.
	if (*in_pos >= in_size)
		return LZMA_BUF_ERROR;

	// NOTE: This function has many similarities to index_encode() and
	// index_decode() functions found from index_encoder.c and
	// index_decoder.c. See the comments especially in index_encoder.c.
	const size_t in_start = *in_pos;
	lzma_ret ret = LZMA_OK;

	while (*in_pos < in_size)
	switch (index_hash->sequence) {
	case SEQ_BLOCK:
		// Check the Index Indicator is present.
		if (in[(*in_pos)++] != 0x00)
			return LZMA_DATA_ERROR;

		index_hash->sequence = SEQ_COUNT;
		break;

	case SEQ_COUNT: {
		ret = lzma_vli_decode(&index_hash->remaining,
				&index_hash->pos, in, in_pos, in_size);
		if (ret != LZMA_STREAM_END)
			goto out;

		// The count must match the count of the Blocks decoded.
		if (index_hash->remaining != index_hash->blocks.count)
			return LZMA_DATA_ERROR;

		ret = LZMA_OK;
		index_hash->pos = 0;

		// Handle the special case when there are no Blocks.
		index_hash->sequence = index_hash->remaining == 0
				? SEQ_PADDING_INIT : SEQ_UNPADDED;
		break;
	}

	case SEQ_UNPADDED:
	case SEQ_UNCOMPRESSED: {
		lzma_vli *size = index_hash->sequence == SEQ_UNPADDED
				? &index_hash->unpadded_size
				: &index_hash->uncompressed_size;

		ret = lzma_vli_decode(size, &index_hash->pos,
				in, in_pos, in_size);
		if (ret != LZMA_STREAM_END)
			goto out;

		ret = LZMA_OK;
		index_hash->pos = 0;

		if (index_hash->sequence == SEQ_UNPADDED) {
			if (index_hash->unpadded_size < UNPADDED_SIZE_MIN
					|| index_hash->unpadded_size
						> UNPADDED_SIZE_MAX)
				return LZMA_DATA_ERROR;

			index_hash->sequence = SEQ_UNCOMPRESSED;
		} else {
			// Update the hash.
			return_if_error(hash_append(&index_hash->records,
					index_hash->unpadded_size,
					index_hash->uncompressed_size));

			// Verify that we don't go over the known sizes. Note
			// that this validation is simpler than the one used
			// in lzma_index_hash_append(), because here we know
			// that values in index_hash->blocks are already
			// validated and we are fine as long as we don't
			// exceed them in index_hash->records.
			if (index_hash->blocks.blocks_size
					< index_hash->records.blocks_size
					|| index_hash->blocks.uncompressed_size
					< index_hash->records.uncompressed_size
					|| index_hash->blocks.index_list_size
					< index_hash->records.index_list_size)
				return LZMA_DATA_ERROR;

			// Check if this was the last Record.
			index_hash->sequence = --index_hash->remaining == 0
					? SEQ_PADDING_INIT : SEQ_UNPADDED;
		}

		break;
	}

	case SEQ_PADDING_INIT:
		index_hash->pos = (LZMA_VLI_C(4) - index_size_unpadded(
				index_hash->records.count,
				index_hash->records.index_list_size)) & 3;
		index_hash->sequence = SEQ_PADDING;

	// Fall through

	case SEQ_PADDING:
		if (index_hash->pos > 0) {
			--index_hash->pos;
			if (in[(*in_pos)++] != 0x00)
				return LZMA_DATA_ERROR;

			break;
		}

		// Compare the sizes.
		if (index_hash->blocks.blocks_size
				!= index_hash->records.blocks_size
				|| index_hash->blocks.uncompressed_size
				!= index_hash->records.uncompressed_size
				|| index_hash->blocks.index_list_size
				!= index_hash->records.index_list_size)
			return LZMA_DATA_ERROR;

		// Finish the hashes and compare them.
		lzma_check_finish(&index_hash->blocks.check, LZMA_CHECK_BEST);
		lzma_check_finish(&index_hash->records.check, LZMA_CHECK_BEST);
		if (memcmp(index_hash->blocks.check.buffer.u8,
				index_hash->records.check.buffer.u8,
				lzma_check_size(LZMA_CHECK_BEST)) != 0)
			return LZMA_DATA_ERROR;

		// Finish the CRC32 calculation.
		index_hash->crc32 = lzma_crc32(in + in_start,
				*in_pos - in_start, index_hash->crc32);

		index_hash->sequence = SEQ_CRC32;

	// Fall through

	case SEQ_CRC32:
		do {
			if (*in_pos == in_size)
				return LZMA_OK;

			if (((index_hash->crc32 >> (index_hash->pos * 8))
					& 0xFF) != in[(*in_pos)++])
				return LZMA_DATA_ERROR;

		} while (++index_hash->pos < 4);

		return LZMA_STREAM_END;

	default:
		assert(0);
		return LZMA_PROG_ERROR;
	}

out:
	// Update the CRC32,
	index_hash->crc32 = lzma_crc32(in + in_start,
			*in_pos - in_start, index_hash->crc32);

	return ret;
}
