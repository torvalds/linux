///////////////////////////////////////////////////////////////////////////////
//
/// \file       lzma_encoder.c
/// \brief      LZMA encoder
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "lzma2_encoder.h"
#include "lzma_encoder_private.h"
#include "fastpos.h"


/////////////
// Literal //
/////////////

static inline void
literal_matched(lzma_range_encoder *rc, probability *subcoder,
		uint32_t match_byte, uint32_t symbol)
{
	uint32_t offset = 0x100;
	symbol += UINT32_C(1) << 8;

	do {
		match_byte <<= 1;
		const uint32_t match_bit = match_byte & offset;
		const uint32_t subcoder_index
				= offset + match_bit + (symbol >> 8);
		const uint32_t bit = (symbol >> 7) & 1;
		rc_bit(rc, &subcoder[subcoder_index], bit);

		symbol <<= 1;
		offset &= ~(match_byte ^ symbol);

	} while (symbol < (UINT32_C(1) << 16));
}


static inline void
literal(lzma_lzma1_encoder *coder, lzma_mf *mf, uint32_t position)
{
	// Locate the literal byte to be encoded and the subcoder.
	const uint8_t cur_byte = mf->buffer[
			mf->read_pos - mf->read_ahead];
	probability *subcoder = literal_subcoder(coder->literal,
			coder->literal_context_bits, coder->literal_pos_mask,
			position, mf->buffer[mf->read_pos - mf->read_ahead - 1]);

	if (is_literal_state(coder->state)) {
		// Previous LZMA-symbol was a literal. Encode a normal
		// literal without a match byte.
		rc_bittree(&coder->rc, subcoder, 8, cur_byte);
	} else {
		// Previous LZMA-symbol was a match. Use the last byte of
		// the match as a "match byte". That is, compare the bits
		// of the current literal and the match byte.
		const uint8_t match_byte = mf->buffer[
				mf->read_pos - coder->reps[0] - 1
				- mf->read_ahead];
		literal_matched(&coder->rc, subcoder, match_byte, cur_byte);
	}

	update_literal(coder->state);
}


//////////////////
// Match length //
//////////////////

static void
length_update_prices(lzma_length_encoder *lc, const uint32_t pos_state)
{
	const uint32_t table_size = lc->table_size;
	lc->counters[pos_state] = table_size;

	const uint32_t a0 = rc_bit_0_price(lc->choice);
	const uint32_t a1 = rc_bit_1_price(lc->choice);
	const uint32_t b0 = a1 + rc_bit_0_price(lc->choice2);
	const uint32_t b1 = a1 + rc_bit_1_price(lc->choice2);
	uint32_t *const prices = lc->prices[pos_state];

	uint32_t i;
	for (i = 0; i < table_size && i < LEN_LOW_SYMBOLS; ++i)
		prices[i] = a0 + rc_bittree_price(lc->low[pos_state],
				LEN_LOW_BITS, i);

	for (; i < table_size && i < LEN_LOW_SYMBOLS + LEN_MID_SYMBOLS; ++i)
		prices[i] = b0 + rc_bittree_price(lc->mid[pos_state],
				LEN_MID_BITS, i - LEN_LOW_SYMBOLS);

	for (; i < table_size; ++i)
		prices[i] = b1 + rc_bittree_price(lc->high, LEN_HIGH_BITS,
				i - LEN_LOW_SYMBOLS - LEN_MID_SYMBOLS);

	return;
}


static inline void
length(lzma_range_encoder *rc, lzma_length_encoder *lc,
		const uint32_t pos_state, uint32_t len, const bool fast_mode)
{
	assert(len <= MATCH_LEN_MAX);
	len -= MATCH_LEN_MIN;

	if (len < LEN_LOW_SYMBOLS) {
		rc_bit(rc, &lc->choice, 0);
		rc_bittree(rc, lc->low[pos_state], LEN_LOW_BITS, len);
	} else {
		rc_bit(rc, &lc->choice, 1);
		len -= LEN_LOW_SYMBOLS;

		if (len < LEN_MID_SYMBOLS) {
			rc_bit(rc, &lc->choice2, 0);
			rc_bittree(rc, lc->mid[pos_state], LEN_MID_BITS, len);
		} else {
			rc_bit(rc, &lc->choice2, 1);
			len -= LEN_MID_SYMBOLS;
			rc_bittree(rc, lc->high, LEN_HIGH_BITS, len);
		}
	}

	// Only getoptimum uses the prices so don't update the table when
	// in fast mode.
	if (!fast_mode)
		if (--lc->counters[pos_state] == 0)
			length_update_prices(lc, pos_state);
}


///////////
// Match //
///////////

static inline void
match(lzma_lzma1_encoder *coder, const uint32_t pos_state,
		const uint32_t distance, const uint32_t len)
{
	update_match(coder->state);

	length(&coder->rc, &coder->match_len_encoder, pos_state, len,
			coder->fast_mode);

	const uint32_t dist_slot = get_dist_slot(distance);
	const uint32_t dist_state = get_dist_state(len);
	rc_bittree(&coder->rc, coder->dist_slot[dist_state],
			DIST_SLOT_BITS, dist_slot);

	if (dist_slot >= DIST_MODEL_START) {
		const uint32_t footer_bits = (dist_slot >> 1) - 1;
		const uint32_t base = (2 | (dist_slot & 1)) << footer_bits;
		const uint32_t dist_reduced = distance - base;

		if (dist_slot < DIST_MODEL_END) {
			// Careful here: base - dist_slot - 1 can be -1, but
			// rc_bittree_reverse starts at probs[1], not probs[0].
			rc_bittree_reverse(&coder->rc,
				coder->dist_special + base - dist_slot - 1,
				footer_bits, dist_reduced);
		} else {
			rc_direct(&coder->rc, dist_reduced >> ALIGN_BITS,
					footer_bits - ALIGN_BITS);
			rc_bittree_reverse(
					&coder->rc, coder->dist_align,
					ALIGN_BITS, dist_reduced & ALIGN_MASK);
			++coder->align_price_count;
		}
	}

	coder->reps[3] = coder->reps[2];
	coder->reps[2] = coder->reps[1];
	coder->reps[1] = coder->reps[0];
	coder->reps[0] = distance;
	++coder->match_price_count;
}


////////////////////
// Repeated match //
////////////////////

static inline void
rep_match(lzma_lzma1_encoder *coder, const uint32_t pos_state,
		const uint32_t rep, const uint32_t len)
{
	if (rep == 0) {
		rc_bit(&coder->rc, &coder->is_rep0[coder->state], 0);
		rc_bit(&coder->rc,
				&coder->is_rep0_long[coder->state][pos_state],
				len != 1);
	} else {
		const uint32_t distance = coder->reps[rep];
		rc_bit(&coder->rc, &coder->is_rep0[coder->state], 1);

		if (rep == 1) {
			rc_bit(&coder->rc, &coder->is_rep1[coder->state], 0);
		} else {
			rc_bit(&coder->rc, &coder->is_rep1[coder->state], 1);
			rc_bit(&coder->rc, &coder->is_rep2[coder->state],
					rep - 2);

			if (rep == 3)
				coder->reps[3] = coder->reps[2];

			coder->reps[2] = coder->reps[1];
		}

		coder->reps[1] = coder->reps[0];
		coder->reps[0] = distance;
	}

	if (len == 1) {
		update_short_rep(coder->state);
	} else {
		length(&coder->rc, &coder->rep_len_encoder, pos_state, len,
				coder->fast_mode);
		update_long_rep(coder->state);
	}
}


//////////
// Main //
//////////

static void
encode_symbol(lzma_lzma1_encoder *coder, lzma_mf *mf,
		uint32_t back, uint32_t len, uint32_t position)
{
	const uint32_t pos_state = position & coder->pos_mask;

	if (back == UINT32_MAX) {
		// Literal i.e. eight-bit byte
		assert(len == 1);
		rc_bit(&coder->rc,
				&coder->is_match[coder->state][pos_state], 0);
		literal(coder, mf, position);
	} else {
		// Some type of match
		rc_bit(&coder->rc,
			&coder->is_match[coder->state][pos_state], 1);

		if (back < REPS) {
			// It's a repeated match i.e. the same distance
			// has been used earlier.
			rc_bit(&coder->rc, &coder->is_rep[coder->state], 1);
			rep_match(coder, pos_state, back, len);
		} else {
			// Normal match
			rc_bit(&coder->rc, &coder->is_rep[coder->state], 0);
			match(coder, pos_state, back - REPS, len);
		}
	}

	assert(mf->read_ahead >= len);
	mf->read_ahead -= len;
}


static bool
encode_init(lzma_lzma1_encoder *coder, lzma_mf *mf)
{
	assert(mf_position(mf) == 0);

	if (mf->read_pos == mf->read_limit) {
		if (mf->action == LZMA_RUN)
			return false; // We cannot do anything.

		// We are finishing (we cannot get here when flushing).
		assert(mf->write_pos == mf->read_pos);
		assert(mf->action == LZMA_FINISH);
	} else {
		// Do the actual initialization. The first LZMA symbol must
		// always be a literal.
		mf_skip(mf, 1);
		mf->read_ahead = 0;
		rc_bit(&coder->rc, &coder->is_match[0][0], 0);
		rc_bittree(&coder->rc, coder->literal[0], 8, mf->buffer[0]);
	}

	// Initialization is done (except if empty file).
	coder->is_initialized = true;

	return true;
}


static void
encode_eopm(lzma_lzma1_encoder *coder, uint32_t position)
{
	const uint32_t pos_state = position & coder->pos_mask;
	rc_bit(&coder->rc, &coder->is_match[coder->state][pos_state], 1);
	rc_bit(&coder->rc, &coder->is_rep[coder->state], 0);
	match(coder, pos_state, UINT32_MAX, MATCH_LEN_MIN);
}


/// Number of bytes that a single encoding loop in lzma_lzma_encode() can
/// consume from the dictionary. This limit comes from lzma_lzma_optimum()
/// and may need to be updated if that function is significantly modified.
#define LOOP_INPUT_MAX (OPTS + 1)


extern lzma_ret
lzma_lzma_encode(lzma_lzma1_encoder *restrict coder, lzma_mf *restrict mf,
		uint8_t *restrict out, size_t *restrict out_pos,
		size_t out_size, uint32_t limit)
{
	// Initialize the stream if no data has been encoded yet.
	if (!coder->is_initialized && !encode_init(coder, mf))
		return LZMA_OK;

	// Get the lowest bits of the uncompressed offset from the LZ layer.
	uint32_t position = mf_position(mf);

	while (true) {
		// Encode pending bits, if any. Calling this before encoding
		// the next symbol is needed only with plain LZMA, since
		// LZMA2 always provides big enough buffer to flush
		// everything out from the range encoder. For the same reason,
		// rc_encode() never returns true when this function is used
		// as part of LZMA2 encoder.
		if (rc_encode(&coder->rc, out, out_pos, out_size)) {
			assert(limit == UINT32_MAX);
			return LZMA_OK;
		}

		// With LZMA2 we need to take care that compressed size of
		// a chunk doesn't get too big.
		// FIXME? Check if this could be improved.
		if (limit != UINT32_MAX
				&& (mf->read_pos - mf->read_ahead >= limit
					|| *out_pos + rc_pending(&coder->rc)
						>= LZMA2_CHUNK_MAX
							- LOOP_INPUT_MAX))
			break;

		// Check that there is some input to process.
		if (mf->read_pos >= mf->read_limit) {
			if (mf->action == LZMA_RUN)
				return LZMA_OK;

			if (mf->read_ahead == 0)
				break;
		}

		// Get optimal match (repeat position and length).
		// Value ranges for pos:
		//   - [0, REPS): repeated match
		//   - [REPS, UINT32_MAX):
		//     match at (pos - REPS)
		//   - UINT32_MAX: not a match but a literal
		// Value ranges for len:
		//   - [MATCH_LEN_MIN, MATCH_LEN_MAX]
		uint32_t len;
		uint32_t back;

		if (coder->fast_mode)
			lzma_lzma_optimum_fast(coder, mf, &back, &len);
		else
			lzma_lzma_optimum_normal(
					coder, mf, &back, &len, position);

		encode_symbol(coder, mf, back, len, position);

		position += len;
	}

	if (!coder->is_flushed) {
		coder->is_flushed = true;

		// We don't support encoding plain LZMA streams without EOPM,
		// and LZMA2 doesn't use EOPM at LZMA level.
		if (limit == UINT32_MAX)
			encode_eopm(coder, position);

		// Flush the remaining bytes from the range encoder.
		rc_flush(&coder->rc);

		// Copy the remaining bytes to the output buffer. If there
		// isn't enough output space, we will copy out the remaining
		// bytes on the next call to this function by using
		// the rc_encode() call in the encoding loop above.
		if (rc_encode(&coder->rc, out, out_pos, out_size)) {
			assert(limit == UINT32_MAX);
			return LZMA_OK;
		}
	}

	// Make it ready for the next LZMA2 chunk.
	coder->is_flushed = false;

	return LZMA_STREAM_END;
}


static lzma_ret
lzma_encode(void *coder, lzma_mf *restrict mf,
		uint8_t *restrict out, size_t *restrict out_pos,
		size_t out_size)
{
	// Plain LZMA has no support for sync-flushing.
	if (unlikely(mf->action == LZMA_SYNC_FLUSH))
		return LZMA_OPTIONS_ERROR;

	return lzma_lzma_encode(coder, mf, out, out_pos, out_size, UINT32_MAX);
}


////////////////////
// Initialization //
////////////////////

static bool
is_options_valid(const lzma_options_lzma *options)
{
	// Validate some of the options. LZ encoder validates nice_len too
	// but we need a valid value here earlier.
	return is_lclppb_valid(options)
			&& options->nice_len >= MATCH_LEN_MIN
			&& options->nice_len <= MATCH_LEN_MAX
			&& (options->mode == LZMA_MODE_FAST
				|| options->mode == LZMA_MODE_NORMAL);
}


static void
set_lz_options(lzma_lz_options *lz_options, const lzma_options_lzma *options)
{
	// LZ encoder initialization does the validation for these so we
	// don't need to validate here.
	lz_options->before_size = OPTS;
	lz_options->dict_size = options->dict_size;
	lz_options->after_size = LOOP_INPUT_MAX;
	lz_options->match_len_max = MATCH_LEN_MAX;
	lz_options->nice_len = options->nice_len;
	lz_options->match_finder = options->mf;
	lz_options->depth = options->depth;
	lz_options->preset_dict = options->preset_dict;
	lz_options->preset_dict_size = options->preset_dict_size;
	return;
}


static void
length_encoder_reset(lzma_length_encoder *lencoder,
		const uint32_t num_pos_states, const bool fast_mode)
{
	bit_reset(lencoder->choice);
	bit_reset(lencoder->choice2);

	for (size_t pos_state = 0; pos_state < num_pos_states; ++pos_state) {
		bittree_reset(lencoder->low[pos_state], LEN_LOW_BITS);
		bittree_reset(lencoder->mid[pos_state], LEN_MID_BITS);
	}

	bittree_reset(lencoder->high, LEN_HIGH_BITS);

	if (!fast_mode)
		for (uint32_t pos_state = 0; pos_state < num_pos_states;
				++pos_state)
			length_update_prices(lencoder, pos_state);

	return;
}


extern lzma_ret
lzma_lzma_encoder_reset(lzma_lzma1_encoder *coder,
		const lzma_options_lzma *options)
{
	if (!is_options_valid(options))
		return LZMA_OPTIONS_ERROR;

	coder->pos_mask = (1U << options->pb) - 1;
	coder->literal_context_bits = options->lc;
	coder->literal_pos_mask = (1U << options->lp) - 1;

	// Range coder
	rc_reset(&coder->rc);

	// State
	coder->state = STATE_LIT_LIT;
	for (size_t i = 0; i < REPS; ++i)
		coder->reps[i] = 0;

	literal_init(coder->literal, options->lc, options->lp);

	// Bit encoders
	for (size_t i = 0; i < STATES; ++i) {
		for (size_t j = 0; j <= coder->pos_mask; ++j) {
			bit_reset(coder->is_match[i][j]);
			bit_reset(coder->is_rep0_long[i][j]);
		}

		bit_reset(coder->is_rep[i]);
		bit_reset(coder->is_rep0[i]);
		bit_reset(coder->is_rep1[i]);
		bit_reset(coder->is_rep2[i]);
	}

	for (size_t i = 0; i < FULL_DISTANCES - DIST_MODEL_END; ++i)
		bit_reset(coder->dist_special[i]);

	// Bit tree encoders
	for (size_t i = 0; i < DIST_STATES; ++i)
		bittree_reset(coder->dist_slot[i], DIST_SLOT_BITS);

	bittree_reset(coder->dist_align, ALIGN_BITS);

	// Length encoders
	length_encoder_reset(&coder->match_len_encoder,
			1U << options->pb, coder->fast_mode);

	length_encoder_reset(&coder->rep_len_encoder,
			1U << options->pb, coder->fast_mode);

	// Price counts are incremented every time appropriate probabilities
	// are changed. price counts are set to zero when the price tables
	// are updated, which is done when the appropriate price counts have
	// big enough value, and lzma_mf.read_ahead == 0 which happens at
	// least every OPTS (a few thousand) possible price count increments.
	//
	// By resetting price counts to UINT32_MAX / 2, we make sure that the
	// price tables will be initialized before they will be used (since
	// the value is definitely big enough), and that it is OK to increment
	// price counts without risk of integer overflow (since UINT32_MAX / 2
	// is small enough). The current code doesn't increment price counts
	// before initializing price tables, but it maybe done in future if
	// we add support for saving the state between LZMA2 chunks.
	coder->match_price_count = UINT32_MAX / 2;
	coder->align_price_count = UINT32_MAX / 2;

	coder->opts_end_index = 0;
	coder->opts_current_index = 0;

	return LZMA_OK;
}


extern lzma_ret
lzma_lzma_encoder_create(void **coder_ptr,
		const lzma_allocator *allocator,
		const lzma_options_lzma *options, lzma_lz_options *lz_options)
{
	// Allocate lzma_lzma1_encoder if it wasn't already allocated.
	if (*coder_ptr == NULL) {
		*coder_ptr = lzma_alloc(sizeof(lzma_lzma1_encoder), allocator);
		if (*coder_ptr == NULL)
			return LZMA_MEM_ERROR;
	}

	lzma_lzma1_encoder *coder = *coder_ptr;

	// Set compression mode. We haven't validates the options yet,
	// but it's OK here, since nothing bad happens with invalid
	// options in the code below, and they will get rejected by
	// lzma_lzma_encoder_reset() call at the end of this function.
	switch (options->mode) {
		case LZMA_MODE_FAST:
			coder->fast_mode = true;
			break;

		case LZMA_MODE_NORMAL: {
			coder->fast_mode = false;

			// Set dist_table_size.
			// Round the dictionary size up to next 2^n.
			uint32_t log_size = 0;
			while ((UINT32_C(1) << log_size) < options->dict_size)
				++log_size;

			coder->dist_table_size = log_size * 2;

			// Length encoders' price table size
			coder->match_len_encoder.table_size
				= options->nice_len + 1 - MATCH_LEN_MIN;
			coder->rep_len_encoder.table_size
				= options->nice_len + 1 - MATCH_LEN_MIN;
			break;
		}

		default:
			return LZMA_OPTIONS_ERROR;
	}

	// We don't need to write the first byte as literal if there is
	// a non-empty preset dictionary. encode_init() wouldn't even work
	// if there is a non-empty preset dictionary, because encode_init()
	// assumes that position is zero and previous byte is also zero.
	coder->is_initialized = options->preset_dict != NULL
			&& options->preset_dict_size > 0;
	coder->is_flushed = false;

	set_lz_options(lz_options, options);

	return lzma_lzma_encoder_reset(coder, options);
}


static lzma_ret
lzma_encoder_init(lzma_lz_encoder *lz, const lzma_allocator *allocator,
		const void *options, lzma_lz_options *lz_options)
{
	lz->code = &lzma_encode;
	return lzma_lzma_encoder_create(
			&lz->coder, allocator, options, lz_options);
}


extern lzma_ret
lzma_lzma_encoder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters)
{
	return lzma_lz_encoder_init(
			next, allocator, filters, &lzma_encoder_init);
}


extern uint64_t
lzma_lzma_encoder_memusage(const void *options)
{
	if (!is_options_valid(options))
		return UINT64_MAX;

	lzma_lz_options lz_options;
	set_lz_options(&lz_options, options);

	const uint64_t lz_memusage = lzma_lz_encoder_memusage(&lz_options);
	if (lz_memusage == UINT64_MAX)
		return UINT64_MAX;

	return (uint64_t)(sizeof(lzma_lzma1_encoder)) + lz_memusage;
}


extern bool
lzma_lzma_lclppb_encode(const lzma_options_lzma *options, uint8_t *byte)
{
	if (!is_lclppb_valid(options))
		return true;

	*byte = (options->pb * 5 + options->lp) * 9 + options->lc;
	assert(*byte <= (4 * 5 + 4) * 9 + 8);

	return false;
}


#ifdef HAVE_ENCODER_LZMA1
extern lzma_ret
lzma_lzma_props_encode(const void *options, uint8_t *out)
{
	const lzma_options_lzma *const opt = options;

	if (lzma_lzma_lclppb_encode(opt, out))
		return LZMA_PROG_ERROR;

	unaligned_write32le(out + 1, opt->dict_size);

	return LZMA_OK;
}
#endif


extern LZMA_API(lzma_bool)
lzma_mode_is_supported(lzma_mode mode)
{
	return mode == LZMA_MODE_FAST || mode == LZMA_MODE_NORMAL;
}
