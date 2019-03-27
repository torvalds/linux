///////////////////////////////////////////////////////////////////////////////
//
/// \file       lzma_decoder.c
/// \brief      LZMA decoder
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "lz_decoder.h"
#include "lzma_common.h"
#include "lzma_decoder.h"
#include "range_decoder.h"

// The macros unroll loops with switch statements.
// Silence warnings about missing fall-through comments.
#if TUKLIB_GNUC_REQ(7, 0)
#	pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif


#ifdef HAVE_SMALL

// Macros for (somewhat) size-optimized code.
#define seq_4(seq) seq

#define seq_6(seq) seq

#define seq_8(seq) seq

#define seq_len(seq) \
	seq ## _CHOICE, \
	seq ## _CHOICE2, \
	seq ## _BITTREE

#define len_decode(target, ld, pos_state, seq) \
do { \
case seq ## _CHOICE: \
	rc_if_0(ld.choice, seq ## _CHOICE) { \
		rc_update_0(ld.choice); \
		probs = ld.low[pos_state];\
		limit = LEN_LOW_SYMBOLS; \
		target = MATCH_LEN_MIN; \
	} else { \
		rc_update_1(ld.choice); \
case seq ## _CHOICE2: \
		rc_if_0(ld.choice2, seq ## _CHOICE2) { \
			rc_update_0(ld.choice2); \
			probs = ld.mid[pos_state]; \
			limit = LEN_MID_SYMBOLS; \
			target = MATCH_LEN_MIN + LEN_LOW_SYMBOLS; \
		} else { \
			rc_update_1(ld.choice2); \
			probs = ld.high; \
			limit = LEN_HIGH_SYMBOLS; \
			target = MATCH_LEN_MIN + LEN_LOW_SYMBOLS \
					+ LEN_MID_SYMBOLS; \
		} \
	} \
	symbol = 1; \
case seq ## _BITTREE: \
	do { \
		rc_bit(probs[symbol], , , seq ## _BITTREE); \
	} while (symbol < limit); \
	target += symbol - limit; \
} while (0)

#else // HAVE_SMALL

// Unrolled versions
#define seq_4(seq) \
	seq ## 0, \
	seq ## 1, \
	seq ## 2, \
	seq ## 3

#define seq_6(seq) \
	seq ## 0, \
	seq ## 1, \
	seq ## 2, \
	seq ## 3, \
	seq ## 4, \
	seq ## 5

#define seq_8(seq) \
	seq ## 0, \
	seq ## 1, \
	seq ## 2, \
	seq ## 3, \
	seq ## 4, \
	seq ## 5, \
	seq ## 6, \
	seq ## 7

#define seq_len(seq) \
	seq ## _CHOICE, \
	seq ## _LOW0, \
	seq ## _LOW1, \
	seq ## _LOW2, \
	seq ## _CHOICE2, \
	seq ## _MID0, \
	seq ## _MID1, \
	seq ## _MID2, \
	seq ## _HIGH0, \
	seq ## _HIGH1, \
	seq ## _HIGH2, \
	seq ## _HIGH3, \
	seq ## _HIGH4, \
	seq ## _HIGH5, \
	seq ## _HIGH6, \
	seq ## _HIGH7

#define len_decode(target, ld, pos_state, seq) \
do { \
	symbol = 1; \
case seq ## _CHOICE: \
	rc_if_0(ld.choice, seq ## _CHOICE) { \
		rc_update_0(ld.choice); \
		rc_bit_case(ld.low[pos_state][symbol], , , seq ## _LOW0); \
		rc_bit_case(ld.low[pos_state][symbol], , , seq ## _LOW1); \
		rc_bit_case(ld.low[pos_state][symbol], , , seq ## _LOW2); \
		target = symbol - LEN_LOW_SYMBOLS + MATCH_LEN_MIN; \
	} else { \
		rc_update_1(ld.choice); \
case seq ## _CHOICE2: \
		rc_if_0(ld.choice2, seq ## _CHOICE2) { \
			rc_update_0(ld.choice2); \
			rc_bit_case(ld.mid[pos_state][symbol], , , \
					seq ## _MID0); \
			rc_bit_case(ld.mid[pos_state][symbol], , , \
					seq ## _MID1); \
			rc_bit_case(ld.mid[pos_state][symbol], , , \
					seq ## _MID2); \
			target = symbol - LEN_MID_SYMBOLS \
					+ MATCH_LEN_MIN + LEN_LOW_SYMBOLS; \
		} else { \
			rc_update_1(ld.choice2); \
			rc_bit_case(ld.high[symbol], , , seq ## _HIGH0); \
			rc_bit_case(ld.high[symbol], , , seq ## _HIGH1); \
			rc_bit_case(ld.high[symbol], , , seq ## _HIGH2); \
			rc_bit_case(ld.high[symbol], , , seq ## _HIGH3); \
			rc_bit_case(ld.high[symbol], , , seq ## _HIGH4); \
			rc_bit_case(ld.high[symbol], , , seq ## _HIGH5); \
			rc_bit_case(ld.high[symbol], , , seq ## _HIGH6); \
			rc_bit_case(ld.high[symbol], , , seq ## _HIGH7); \
			target = symbol - LEN_HIGH_SYMBOLS \
					+ MATCH_LEN_MIN \
					+ LEN_LOW_SYMBOLS + LEN_MID_SYMBOLS; \
		} \
	} \
} while (0)

#endif // HAVE_SMALL


/// Length decoder probabilities; see comments in lzma_common.h.
typedef struct {
	probability choice;
	probability choice2;
	probability low[POS_STATES_MAX][LEN_LOW_SYMBOLS];
	probability mid[POS_STATES_MAX][LEN_MID_SYMBOLS];
	probability high[LEN_HIGH_SYMBOLS];
} lzma_length_decoder;


typedef struct {
	///////////////////
	// Probabilities //
	///////////////////

	/// Literals; see comments in lzma_common.h.
	probability literal[LITERAL_CODERS_MAX][LITERAL_CODER_SIZE];

	/// If 1, it's a match. Otherwise it's a single 8-bit literal.
	probability is_match[STATES][POS_STATES_MAX];

	/// If 1, it's a repeated match. The distance is one of rep0 .. rep3.
	probability is_rep[STATES];

	/// If 0, distance of a repeated match is rep0.
	/// Otherwise check is_rep1.
	probability is_rep0[STATES];

	/// If 0, distance of a repeated match is rep1.
	/// Otherwise check is_rep2.
	probability is_rep1[STATES];

	/// If 0, distance of a repeated match is rep2. Otherwise it is rep3.
	probability is_rep2[STATES];

	/// If 1, the repeated match has length of one byte. Otherwise
	/// the length is decoded from rep_len_decoder.
	probability is_rep0_long[STATES][POS_STATES_MAX];

	/// Probability tree for the highest two bits of the match distance.
	/// There is a separate probability tree for match lengths of
	/// 2 (i.e. MATCH_LEN_MIN), 3, 4, and [5, 273].
	probability dist_slot[DIST_STATES][DIST_SLOTS];

	/// Probability trees for additional bits for match distance when the
	/// distance is in the range [4, 127].
	probability pos_special[FULL_DISTANCES - DIST_MODEL_END];

	/// Probability tree for the lowest four bits of a match distance
	/// that is equal to or greater than 128.
	probability pos_align[ALIGN_SIZE];

	/// Length of a normal match
	lzma_length_decoder match_len_decoder;

	/// Length of a repeated match
	lzma_length_decoder rep_len_decoder;

	///////////////////
	// Decoder state //
	///////////////////

	// Range coder
	lzma_range_decoder rc;

	// Types of the most recently seen LZMA symbols
	lzma_lzma_state state;

	uint32_t rep0;      ///< Distance of the latest match
	uint32_t rep1;      ///< Distance of second latest match
	uint32_t rep2;      ///< Distance of third latest match
	uint32_t rep3;      ///< Distance of fourth latest match

	uint32_t pos_mask; // (1U << pb) - 1
	uint32_t literal_context_bits;
	uint32_t literal_pos_mask;

	/// Uncompressed size as bytes, or LZMA_VLI_UNKNOWN if end of
	/// payload marker is expected.
	lzma_vli uncompressed_size;

	////////////////////////////////
	// State of incomplete symbol //
	////////////////////////////////

	/// Position where to continue the decoder loop
	enum {
		SEQ_NORMALIZE,
		SEQ_IS_MATCH,
		seq_8(SEQ_LITERAL),
		seq_8(SEQ_LITERAL_MATCHED),
		SEQ_LITERAL_WRITE,
		SEQ_IS_REP,
		seq_len(SEQ_MATCH_LEN),
		seq_6(SEQ_DIST_SLOT),
		SEQ_DIST_MODEL,
		SEQ_DIRECT,
		seq_4(SEQ_ALIGN),
		SEQ_EOPM,
		SEQ_IS_REP0,
		SEQ_SHORTREP,
		SEQ_IS_REP0_LONG,
		SEQ_IS_REP1,
		SEQ_IS_REP2,
		seq_len(SEQ_REP_LEN),
		SEQ_COPY,
	} sequence;

	/// Base of the current probability tree
	probability *probs;

	/// Symbol being decoded. This is also used as an index variable in
	/// bittree decoders: probs[symbol]
	uint32_t symbol;

	/// Used as a loop termination condition on bittree decoders and
	/// direct bits decoder.
	uint32_t limit;

	/// Matched literal decoder: 0x100 or 0 to help avoiding branches.
	/// Bittree reverse decoders: Offset of the next bit: 1 << offset
	uint32_t offset;

	/// If decoding a literal: match byte.
	/// If decoding a match: length of the match.
	uint32_t len;
} lzma_lzma1_decoder;


static lzma_ret
lzma_decode(void *coder_ptr, lzma_dict *restrict dictptr,
		const uint8_t *restrict in,
		size_t *restrict in_pos, size_t in_size)
{
	lzma_lzma1_decoder *restrict coder = coder_ptr;

	////////////////////
	// Initialization //
	////////////////////

	{
		const lzma_ret ret = rc_read_init(
				&coder->rc, in, in_pos, in_size);
		if (ret != LZMA_STREAM_END)
			return ret;
	}

	///////////////
	// Variables //
	///////////////

	// Making local copies of often-used variables improves both
	// speed and readability.

	lzma_dict dict = *dictptr;

	const size_t dict_start = dict.pos;

	// Range decoder
	rc_to_local(coder->rc, *in_pos);

	// State
	uint32_t state = coder->state;
	uint32_t rep0 = coder->rep0;
	uint32_t rep1 = coder->rep1;
	uint32_t rep2 = coder->rep2;
	uint32_t rep3 = coder->rep3;

	const uint32_t pos_mask = coder->pos_mask;

	// These variables are actually needed only if we last time ran
	// out of input in the middle of the decoder loop.
	probability *probs = coder->probs;
	uint32_t symbol = coder->symbol;
	uint32_t limit = coder->limit;
	uint32_t offset = coder->offset;
	uint32_t len = coder->len;

	const uint32_t literal_pos_mask = coder->literal_pos_mask;
	const uint32_t literal_context_bits = coder->literal_context_bits;

	// Temporary variables
	uint32_t pos_state = dict.pos & pos_mask;

	lzma_ret ret = LZMA_OK;

	// If uncompressed size is known, there must be no end of payload
	// marker.
	const bool no_eopm = coder->uncompressed_size
			!= LZMA_VLI_UNKNOWN;
	if (no_eopm && coder->uncompressed_size < dict.limit - dict.pos)
		dict.limit = dict.pos + (size_t)(coder->uncompressed_size);

	// The main decoder loop. The "switch" is used to restart the decoder at
	// correct location. Once restarted, the "switch" is no longer used.
	switch (coder->sequence)
	while (true) {
		// Calculate new pos_state. This is skipped on the first loop
		// since we already calculated it when setting up the local
		// variables.
		pos_state = dict.pos & pos_mask;

	case SEQ_NORMALIZE:
	case SEQ_IS_MATCH:
		if (unlikely(no_eopm && dict.pos == dict.limit))
			break;

		rc_if_0(coder->is_match[state][pos_state], SEQ_IS_MATCH) {
			rc_update_0(coder->is_match[state][pos_state]);

			// It's a literal i.e. a single 8-bit byte.

			probs = literal_subcoder(coder->literal,
					literal_context_bits, literal_pos_mask,
					dict.pos, dict_get(&dict, 0));
			symbol = 1;

			if (is_literal_state(state)) {
				// Decode literal without match byte.
#ifdef HAVE_SMALL
	case SEQ_LITERAL:
				do {
					rc_bit(probs[symbol], , , SEQ_LITERAL);
				} while (symbol < (1 << 8));
#else
				rc_bit_case(probs[symbol], , , SEQ_LITERAL0);
				rc_bit_case(probs[symbol], , , SEQ_LITERAL1);
				rc_bit_case(probs[symbol], , , SEQ_LITERAL2);
				rc_bit_case(probs[symbol], , , SEQ_LITERAL3);
				rc_bit_case(probs[symbol], , , SEQ_LITERAL4);
				rc_bit_case(probs[symbol], , , SEQ_LITERAL5);
				rc_bit_case(probs[symbol], , , SEQ_LITERAL6);
				rc_bit_case(probs[symbol], , , SEQ_LITERAL7);
#endif
			} else {
				// Decode literal with match byte.
				//
				// We store the byte we compare against
				// ("match byte") to "len" to minimize the
				// number of variables we need to store
				// between decoder calls.
				len = dict_get(&dict, rep0) << 1;

				// The usage of "offset" allows omitting some
				// branches, which should give tiny speed
				// improvement on some CPUs. "offset" gets
				// set to zero if match_bit didn't match.
				offset = 0x100;

#ifdef HAVE_SMALL
	case SEQ_LITERAL_MATCHED:
				do {
					const uint32_t match_bit
							= len & offset;
					const uint32_t subcoder_index
							= offset + match_bit
							+ symbol;

					rc_bit(probs[subcoder_index],
							offset &= ~match_bit,
							offset &= match_bit,
							SEQ_LITERAL_MATCHED);

					// It seems to be faster to do this
					// here instead of putting it to the
					// beginning of the loop and then
					// putting the "case" in the middle
					// of the loop.
					len <<= 1;

				} while (symbol < (1 << 8));
#else
				// Unroll the loop.
				uint32_t match_bit;
				uint32_t subcoder_index;

#	define d(seq) \
		case seq: \
			match_bit = len & offset; \
			subcoder_index = offset + match_bit + symbol; \
			rc_bit(probs[subcoder_index], \
					offset &= ~match_bit, \
					offset &= match_bit, \
					seq)

				d(SEQ_LITERAL_MATCHED0);
				len <<= 1;
				d(SEQ_LITERAL_MATCHED1);
				len <<= 1;
				d(SEQ_LITERAL_MATCHED2);
				len <<= 1;
				d(SEQ_LITERAL_MATCHED3);
				len <<= 1;
				d(SEQ_LITERAL_MATCHED4);
				len <<= 1;
				d(SEQ_LITERAL_MATCHED5);
				len <<= 1;
				d(SEQ_LITERAL_MATCHED6);
				len <<= 1;
				d(SEQ_LITERAL_MATCHED7);
#	undef d
#endif
			}

			//update_literal(state);
			// Use a lookup table to update to literal state,
			// since compared to other state updates, this would
			// need two branches.
			static const lzma_lzma_state next_state[] = {
				STATE_LIT_LIT,
				STATE_LIT_LIT,
				STATE_LIT_LIT,
				STATE_LIT_LIT,
				STATE_MATCH_LIT_LIT,
				STATE_REP_LIT_LIT,
				STATE_SHORTREP_LIT_LIT,
				STATE_MATCH_LIT,
				STATE_REP_LIT,
				STATE_SHORTREP_LIT,
				STATE_MATCH_LIT,
				STATE_REP_LIT
			};
			state = next_state[state];

	case SEQ_LITERAL_WRITE:
			if (unlikely(dict_put(&dict, symbol))) {
				coder->sequence = SEQ_LITERAL_WRITE;
				goto out;
			}

			continue;
		}

		// Instead of a new byte we are going to get a byte range
		// (distance and length) which will be repeated from our
		// output history.

		rc_update_1(coder->is_match[state][pos_state]);

	case SEQ_IS_REP:
		rc_if_0(coder->is_rep[state], SEQ_IS_REP) {
			// Not a repeated match
			rc_update_0(coder->is_rep[state]);
			update_match(state);

			// The latest three match distances are kept in
			// memory in case there are repeated matches.
			rep3 = rep2;
			rep2 = rep1;
			rep1 = rep0;

			// Decode the length of the match.
			len_decode(len, coder->match_len_decoder,
					pos_state, SEQ_MATCH_LEN);

			// Prepare to decode the highest two bits of the
			// match distance.
			probs = coder->dist_slot[get_dist_state(len)];
			symbol = 1;

#ifdef HAVE_SMALL
	case SEQ_DIST_SLOT:
			do {
				rc_bit(probs[symbol], , , SEQ_DIST_SLOT);
			} while (symbol < DIST_SLOTS);
#else
			rc_bit_case(probs[symbol], , , SEQ_DIST_SLOT0);
			rc_bit_case(probs[symbol], , , SEQ_DIST_SLOT1);
			rc_bit_case(probs[symbol], , , SEQ_DIST_SLOT2);
			rc_bit_case(probs[symbol], , , SEQ_DIST_SLOT3);
			rc_bit_case(probs[symbol], , , SEQ_DIST_SLOT4);
			rc_bit_case(probs[symbol], , , SEQ_DIST_SLOT5);
#endif
			// Get rid of the highest bit that was needed for
			// indexing of the probability array.
			symbol -= DIST_SLOTS;
			assert(symbol <= 63);

			if (symbol < DIST_MODEL_START) {
				// Match distances [0, 3] have only two bits.
				rep0 = symbol;
			} else {
				// Decode the lowest [1, 29] bits of
				// the match distance.
				limit = (symbol >> 1) - 1;
				assert(limit >= 1 && limit <= 30);
				rep0 = 2 + (symbol & 1);

				if (symbol < DIST_MODEL_END) {
					// Prepare to decode the low bits for
					// a distance of [4, 127].
					assert(limit <= 5);
					rep0 <<= limit;
					assert(rep0 <= 96);
					// -1 is fine, because we start
					// decoding at probs[1], not probs[0].
					// NOTE: This violates the C standard,
					// since we are doing pointer
					// arithmetic past the beginning of
					// the array.
					assert((int32_t)(rep0 - symbol - 1)
							>= -1);
					assert((int32_t)(rep0 - symbol - 1)
							<= 82);
					probs = coder->pos_special + rep0
							- symbol - 1;
					symbol = 1;
					offset = 0;
	case SEQ_DIST_MODEL:
#ifdef HAVE_SMALL
					do {
						rc_bit(probs[symbol], ,
							rep0 += 1 << offset,
							SEQ_DIST_MODEL);
					} while (++offset < limit);
#else
					switch (limit) {
					case 5:
						assert(offset == 0);
						rc_bit(probs[symbol], ,
							rep0 += 1,
							SEQ_DIST_MODEL);
						++offset;
						--limit;
					case 4:
						rc_bit(probs[symbol], ,
							rep0 += 1 << offset,
							SEQ_DIST_MODEL);
						++offset;
						--limit;
					case 3:
						rc_bit(probs[symbol], ,
							rep0 += 1 << offset,
							SEQ_DIST_MODEL);
						++offset;
						--limit;
					case 2:
						rc_bit(probs[symbol], ,
							rep0 += 1 << offset,
							SEQ_DIST_MODEL);
						++offset;
						--limit;
					case 1:
						// We need "symbol" only for
						// indexing the probability
						// array, thus we can use
						// rc_bit_last() here to omit
						// the unneeded updating of
						// "symbol".
						rc_bit_last(probs[symbol], ,
							rep0 += 1 << offset,
							SEQ_DIST_MODEL);
					}
#endif
				} else {
					// The distance is >= 128. Decode the
					// lower bits without probabilities
					// except the lowest four bits.
					assert(symbol >= 14);
					assert(limit >= 6);
					limit -= ALIGN_BITS;
					assert(limit >= 2);
	case SEQ_DIRECT:
					// Not worth manual unrolling
					do {
						rc_direct(rep0, SEQ_DIRECT);
					} while (--limit > 0);

					// Decode the lowest four bits using
					// probabilities.
					rep0 <<= ALIGN_BITS;
					symbol = 1;
#ifdef HAVE_SMALL
					offset = 0;
	case SEQ_ALIGN:
					do {
						rc_bit(coder->pos_align[
								symbol], ,
							rep0 += 1 << offset,
							SEQ_ALIGN);
					} while (++offset < ALIGN_BITS);
#else
	case SEQ_ALIGN0:
					rc_bit(coder->pos_align[symbol], ,
							rep0 += 1, SEQ_ALIGN0);
	case SEQ_ALIGN1:
					rc_bit(coder->pos_align[symbol], ,
							rep0 += 2, SEQ_ALIGN1);
	case SEQ_ALIGN2:
					rc_bit(coder->pos_align[symbol], ,
							rep0 += 4, SEQ_ALIGN2);
	case SEQ_ALIGN3:
					// Like in SEQ_DIST_MODEL, we don't
					// need "symbol" for anything else
					// than indexing the probability array.
					rc_bit_last(coder->pos_align[symbol], ,
							rep0 += 8, SEQ_ALIGN3);
#endif

					if (rep0 == UINT32_MAX) {
						// End of payload marker was
						// found. It must not be
						// present if uncompressed
						// size is known.
						if (coder->uncompressed_size
						!= LZMA_VLI_UNKNOWN) {
							ret = LZMA_DATA_ERROR;
							goto out;
						}

	case SEQ_EOPM:
						// LZMA1 stream with
						// end-of-payload marker.
						rc_normalize(SEQ_EOPM);
						ret = LZMA_STREAM_END;
						goto out;
					}
				}
			}

			// Validate the distance we just decoded.
			if (unlikely(!dict_is_distance_valid(&dict, rep0))) {
				ret = LZMA_DATA_ERROR;
				goto out;
			}

		} else {
			rc_update_1(coder->is_rep[state]);

			// Repeated match
			//
			// The match distance is a value that we have had
			// earlier. The latest four match distances are
			// available as rep0, rep1, rep2 and rep3. We will
			// now decode which of them is the new distance.
			//
			// There cannot be a match if we haven't produced
			// any output, so check that first.
			if (unlikely(!dict_is_distance_valid(&dict, 0))) {
				ret = LZMA_DATA_ERROR;
				goto out;
			}

	case SEQ_IS_REP0:
			rc_if_0(coder->is_rep0[state], SEQ_IS_REP0) {
				rc_update_0(coder->is_rep0[state]);
				// The distance is rep0.

	case SEQ_IS_REP0_LONG:
				rc_if_0(coder->is_rep0_long[state][pos_state],
						SEQ_IS_REP0_LONG) {
					rc_update_0(coder->is_rep0_long[
							state][pos_state]);

					update_short_rep(state);

	case SEQ_SHORTREP:
					if (unlikely(dict_put(&dict, dict_get(
							&dict, rep0)))) {
						coder->sequence = SEQ_SHORTREP;
						goto out;
					}

					continue;
				}

				// Repeating more than one byte at
				// distance of rep0.
				rc_update_1(coder->is_rep0_long[
						state][pos_state]);

			} else {
				rc_update_1(coder->is_rep0[state]);

	case SEQ_IS_REP1:
				// The distance is rep1, rep2 or rep3. Once
				// we find out which one of these three, it
				// is stored to rep0 and rep1, rep2 and rep3
				// are updated accordingly.
				rc_if_0(coder->is_rep1[state], SEQ_IS_REP1) {
					rc_update_0(coder->is_rep1[state]);

					const uint32_t distance = rep1;
					rep1 = rep0;
					rep0 = distance;

				} else {
					rc_update_1(coder->is_rep1[state]);
	case SEQ_IS_REP2:
					rc_if_0(coder->is_rep2[state],
							SEQ_IS_REP2) {
						rc_update_0(coder->is_rep2[
								state]);

						const uint32_t distance = rep2;
						rep2 = rep1;
						rep1 = rep0;
						rep0 = distance;

					} else {
						rc_update_1(coder->is_rep2[
								state]);

						const uint32_t distance = rep3;
						rep3 = rep2;
						rep2 = rep1;
						rep1 = rep0;
						rep0 = distance;
					}
				}
			}

			update_long_rep(state);

			// Decode the length of the repeated match.
			len_decode(len, coder->rep_len_decoder,
					pos_state, SEQ_REP_LEN);
		}

		/////////////////////////////////
		// Repeat from history buffer. //
		/////////////////////////////////

		// The length is always between these limits. There is no way
		// to trigger the algorithm to set len outside this range.
		assert(len >= MATCH_LEN_MIN);
		assert(len <= MATCH_LEN_MAX);

	case SEQ_COPY:
		// Repeat len bytes from distance of rep0.
		if (unlikely(dict_repeat(&dict, rep0, &len))) {
			coder->sequence = SEQ_COPY;
			goto out;
		}
	}

	rc_normalize(SEQ_NORMALIZE);
	coder->sequence = SEQ_IS_MATCH;

out:
	// Save state

	// NOTE: Must not copy dict.limit.
	dictptr->pos = dict.pos;
	dictptr->full = dict.full;

	rc_from_local(coder->rc, *in_pos);

	coder->state = state;
	coder->rep0 = rep0;
	coder->rep1 = rep1;
	coder->rep2 = rep2;
	coder->rep3 = rep3;

	coder->probs = probs;
	coder->symbol = symbol;
	coder->limit = limit;
	coder->offset = offset;
	coder->len = len;

	// Update the remaining amount of uncompressed data if uncompressed
	// size was known.
	if (coder->uncompressed_size != LZMA_VLI_UNKNOWN) {
		coder->uncompressed_size -= dict.pos - dict_start;

		// Since there cannot be end of payload marker if the
		// uncompressed size was known, we check here if we
		// finished decoding.
		if (coder->uncompressed_size == 0 && ret == LZMA_OK
				&& coder->sequence != SEQ_NORMALIZE)
			ret = coder->sequence == SEQ_IS_MATCH
					? LZMA_STREAM_END : LZMA_DATA_ERROR;
	}

	// We can do an additional check in the range decoder to catch some
	// corrupted files.
	if (ret == LZMA_STREAM_END) {
		if (!rc_is_finished(coder->rc))
			ret = LZMA_DATA_ERROR;

		// Reset the range decoder so that it is ready to reinitialize
		// for a new LZMA2 chunk.
		rc_reset(coder->rc);
	}

	return ret;
}



static void
lzma_decoder_uncompressed(void *coder_ptr, lzma_vli uncompressed_size)
{
	lzma_lzma1_decoder *coder = coder_ptr;
	coder->uncompressed_size = uncompressed_size;
}


static void
lzma_decoder_reset(void *coder_ptr, const void *opt)
{
	lzma_lzma1_decoder *coder = coder_ptr;
	const lzma_options_lzma *options = opt;

	// NOTE: We assume that lc/lp/pb are valid since they were
	// successfully decoded with lzma_lzma_decode_properties().

	// Calculate pos_mask. We don't need pos_bits as is for anything.
	coder->pos_mask = (1U << options->pb) - 1;

	// Initialize the literal decoder.
	literal_init(coder->literal, options->lc, options->lp);

	coder->literal_context_bits = options->lc;
	coder->literal_pos_mask = (1U << options->lp) - 1;

	// State
	coder->state = STATE_LIT_LIT;
	coder->rep0 = 0;
	coder->rep1 = 0;
	coder->rep2 = 0;
	coder->rep3 = 0;
	coder->pos_mask = (1U << options->pb) - 1;

	// Range decoder
	rc_reset(coder->rc);

	// Bit and bittree decoders
	for (uint32_t i = 0; i < STATES; ++i) {
		for (uint32_t j = 0; j <= coder->pos_mask; ++j) {
			bit_reset(coder->is_match[i][j]);
			bit_reset(coder->is_rep0_long[i][j]);
		}

		bit_reset(coder->is_rep[i]);
		bit_reset(coder->is_rep0[i]);
		bit_reset(coder->is_rep1[i]);
		bit_reset(coder->is_rep2[i]);
	}

	for (uint32_t i = 0; i < DIST_STATES; ++i)
		bittree_reset(coder->dist_slot[i], DIST_SLOT_BITS);

	for (uint32_t i = 0; i < FULL_DISTANCES - DIST_MODEL_END; ++i)
		bit_reset(coder->pos_special[i]);

	bittree_reset(coder->pos_align, ALIGN_BITS);

	// Len decoders (also bit/bittree)
	const uint32_t num_pos_states = 1U << options->pb;
	bit_reset(coder->match_len_decoder.choice);
	bit_reset(coder->match_len_decoder.choice2);
	bit_reset(coder->rep_len_decoder.choice);
	bit_reset(coder->rep_len_decoder.choice2);

	for (uint32_t pos_state = 0; pos_state < num_pos_states; ++pos_state) {
		bittree_reset(coder->match_len_decoder.low[pos_state],
				LEN_LOW_BITS);
		bittree_reset(coder->match_len_decoder.mid[pos_state],
				LEN_MID_BITS);

		bittree_reset(coder->rep_len_decoder.low[pos_state],
				LEN_LOW_BITS);
		bittree_reset(coder->rep_len_decoder.mid[pos_state],
				LEN_MID_BITS);
	}

	bittree_reset(coder->match_len_decoder.high, LEN_HIGH_BITS);
	bittree_reset(coder->rep_len_decoder.high, LEN_HIGH_BITS);

	coder->sequence = SEQ_IS_MATCH;
	coder->probs = NULL;
	coder->symbol = 0;
	coder->limit = 0;
	coder->offset = 0;
	coder->len = 0;

	return;
}


extern lzma_ret
lzma_lzma_decoder_create(lzma_lz_decoder *lz, const lzma_allocator *allocator,
		const void *opt, lzma_lz_options *lz_options)
{
	if (lz->coder == NULL) {
		lz->coder = lzma_alloc(sizeof(lzma_lzma1_decoder), allocator);
		if (lz->coder == NULL)
			return LZMA_MEM_ERROR;

		lz->code = &lzma_decode;
		lz->reset = &lzma_decoder_reset;
		lz->set_uncompressed = &lzma_decoder_uncompressed;
	}

	// All dictionary sizes are OK here. LZ decoder will take care of
	// the special cases.
	const lzma_options_lzma *options = opt;
	lz_options->dict_size = options->dict_size;
	lz_options->preset_dict = options->preset_dict;
	lz_options->preset_dict_size = options->preset_dict_size;

	return LZMA_OK;
}


/// Allocate and initialize LZMA decoder. This is used only via LZ
/// initialization (lzma_lzma_decoder_init() passes function pointer to
/// the LZ initialization).
static lzma_ret
lzma_decoder_init(lzma_lz_decoder *lz, const lzma_allocator *allocator,
		const void *options, lzma_lz_options *lz_options)
{
	if (!is_lclppb_valid(options))
		return LZMA_PROG_ERROR;

	return_if_error(lzma_lzma_decoder_create(
			lz, allocator, options, lz_options));

	lzma_decoder_reset(lz->coder, options);
	lzma_decoder_uncompressed(lz->coder, LZMA_VLI_UNKNOWN);

	return LZMA_OK;
}


extern lzma_ret
lzma_lzma_decoder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters)
{
	// LZMA can only be the last filter in the chain. This is enforced
	// by the raw_decoder initialization.
	assert(filters[1].init == NULL);

	return lzma_lz_decoder_init(next, allocator, filters,
			&lzma_decoder_init);
}


extern bool
lzma_lzma_lclppb_decode(lzma_options_lzma *options, uint8_t byte)
{
	if (byte > (4 * 5 + 4) * 9 + 8)
		return true;

	// See the file format specification to understand this.
	options->pb = byte / (9 * 5);
	byte -= options->pb * 9 * 5;
	options->lp = byte / 9;
	options->lc = byte - options->lp * 9;

	return options->lc + options->lp > LZMA_LCLP_MAX;
}


extern uint64_t
lzma_lzma_decoder_memusage_nocheck(const void *options)
{
	const lzma_options_lzma *const opt = options;
	return sizeof(lzma_lzma1_decoder)
			+ lzma_lz_decoder_memusage(opt->dict_size);
}


extern uint64_t
lzma_lzma_decoder_memusage(const void *options)
{
	if (!is_lclppb_valid(options))
		return UINT64_MAX;

	return lzma_lzma_decoder_memusage_nocheck(options);
}


extern lzma_ret
lzma_lzma_props_decode(void **options, const lzma_allocator *allocator,
		const uint8_t *props, size_t props_size)
{
	if (props_size != 5)
		return LZMA_OPTIONS_ERROR;

	lzma_options_lzma *opt
			= lzma_alloc(sizeof(lzma_options_lzma), allocator);
	if (opt == NULL)
		return LZMA_MEM_ERROR;

	if (lzma_lzma_lclppb_decode(opt, props[0]))
		goto error;

	// All dictionary sizes are accepted, including zero. LZ decoder
	// will automatically use a dictionary at least a few KiB even if
	// a smaller dictionary is requested.
	opt->dict_size = unaligned_read32le(props + 1);

	opt->preset_dict = NULL;
	opt->preset_dict_size = 0;

	*options = opt;

	return LZMA_OK;

error:
	lzma_free(opt, allocator);
	return LZMA_OPTIONS_ERROR;
}
