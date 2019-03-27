///////////////////////////////////////////////////////////////////////////////
//
/// \file       range_decoder.h
/// \brief      Range Decoder
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_RANGE_DECODER_H
#define LZMA_RANGE_DECODER_H

#include "range_common.h"


typedef struct {
	uint32_t range;
	uint32_t code;
	uint32_t init_bytes_left;
} lzma_range_decoder;


/// Reads the first five bytes to initialize the range decoder.
static inline lzma_ret
rc_read_init(lzma_range_decoder *rc, const uint8_t *restrict in,
		size_t *restrict in_pos, size_t in_size)
{
	while (rc->init_bytes_left > 0) {
		if (*in_pos == in_size)
			return LZMA_OK;

		// The first byte is always 0x00. It could have been omitted
		// in LZMA2 but it wasn't, so one byte is wasted in every
		// LZMA2 chunk.
		if (rc->init_bytes_left == 5 && in[*in_pos] != 0x00)
			return LZMA_DATA_ERROR;

		rc->code = (rc->code << 8) | in[*in_pos];
		++*in_pos;
		--rc->init_bytes_left;
	}

	return LZMA_STREAM_END;
}


/// Makes local copies of range decoder and *in_pos variables. Doing this
/// improves speed significantly. The range decoder macros expect also
/// variables `in' and `in_size' to be defined.
#define rc_to_local(range_decoder, in_pos) \
	lzma_range_decoder rc = range_decoder; \
	size_t rc_in_pos = (in_pos); \
	uint32_t rc_bound


/// Stores the local copes back to the range decoder structure.
#define rc_from_local(range_decoder, in_pos) \
do { \
	range_decoder = rc; \
	in_pos = rc_in_pos; \
} while (0)


/// Resets the range decoder structure.
#define rc_reset(range_decoder) \
do { \
	(range_decoder).range = UINT32_MAX; \
	(range_decoder).code = 0; \
	(range_decoder).init_bytes_left = 5; \
} while (0)


/// When decoding has been properly finished, rc.code is always zero unless
/// the input stream is corrupt. So checking this can catch some corrupt
/// files especially if they don't have any other integrity check.
#define rc_is_finished(range_decoder) \
	((range_decoder).code == 0)


/// Read the next input byte if needed. If more input is needed but there is
/// no more input available, "goto out" is used to jump out of the main
/// decoder loop.
#define rc_normalize(seq) \
do { \
	if (rc.range < RC_TOP_VALUE) { \
		if (unlikely(rc_in_pos == in_size)) { \
			coder->sequence = seq; \
			goto out; \
		} \
		rc.range <<= RC_SHIFT_BITS; \
		rc.code = (rc.code << RC_SHIFT_BITS) | in[rc_in_pos++]; \
	} \
} while (0)


/// Start decoding a bit. This must be used together with rc_update_0()
/// and rc_update_1():
///
///     rc_if_0(prob, seq) {
///         rc_update_0(prob);
///         // Do something
///     } else {
///         rc_update_1(prob);
///         // Do something else
///     }
///
#define rc_if_0(prob, seq) \
	rc_normalize(seq); \
	rc_bound = (rc.range >> RC_BIT_MODEL_TOTAL_BITS) * (prob); \
	if (rc.code < rc_bound)


/// Update the range decoder state and the used probability variable to
/// match a decoded bit of 0.
#define rc_update_0(prob) \
do { \
	rc.range = rc_bound; \
	prob += (RC_BIT_MODEL_TOTAL - (prob)) >> RC_MOVE_BITS; \
} while (0)


/// Update the range decoder state and the used probability variable to
/// match a decoded bit of 1.
#define rc_update_1(prob) \
do { \
	rc.range -= rc_bound; \
	rc.code -= rc_bound; \
	prob -= (prob) >> RC_MOVE_BITS; \
} while (0)


/// Decodes one bit and runs action0 or action1 depending on the decoded bit.
/// This macro is used as the last step in bittree reverse decoders since
/// those don't use "symbol" for anything else than indexing the probability
/// arrays.
#define rc_bit_last(prob, action0, action1, seq) \
do { \
	rc_if_0(prob, seq) { \
		rc_update_0(prob); \
		action0; \
	} else { \
		rc_update_1(prob); \
		action1; \
	} \
} while (0)


/// Decodes one bit, updates "symbol", and runs action0 or action1 depending
/// on the decoded bit.
#define rc_bit(prob, action0, action1, seq) \
	rc_bit_last(prob, \
		symbol <<= 1; action0, \
		symbol = (symbol << 1) + 1; action1, \
		seq);


/// Like rc_bit() but add "case seq:" as a prefix. This makes the unrolled
/// loops more readable because the code isn't littered with "case"
/// statements. On the other hand this also makes it less readable, since
/// spotting the places where the decoder loop may be restarted is less
/// obvious.
#define rc_bit_case(prob, action0, action1, seq) \
	case seq: rc_bit(prob, action0, action1, seq)


/// Decode a bit without using a probability.
#define rc_direct(dest, seq) \
do { \
	rc_normalize(seq); \
	rc.range >>= 1; \
	rc.code -= rc.range; \
	rc_bound = UINT32_C(0) - (rc.code >> 31); \
	rc.code += rc.range & rc_bound; \
	dest = (dest << 1) + (rc_bound + 1); \
} while (0)


// NOTE: No macros are provided for bittree decoding. It seems to be simpler
// to just write them open in the code.

#endif
