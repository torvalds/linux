///////////////////////////////////////////////////////////////////////////////
//
/// \file       lz_encoder_mf.c
/// \brief      Match finders
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "lz_encoder.h"
#include "lz_encoder_hash.h"
#include "memcmplen.h"


/// \brief      Find matches starting from the current byte
///
/// \return     The length of the longest match found
extern uint32_t
lzma_mf_find(lzma_mf *mf, uint32_t *count_ptr, lzma_match *matches)
{
	// Call the match finder. It returns the number of length-distance
	// pairs found.
	// FIXME: Minimum count is zero, what _exactly_ is the maximum?
	const uint32_t count = mf->find(mf, matches);

	// Length of the longest match; assume that no matches were found
	// and thus the maximum length is zero.
	uint32_t len_best = 0;

	if (count > 0) {
#ifndef NDEBUG
		// Validate the matches.
		for (uint32_t i = 0; i < count; ++i) {
			assert(matches[i].len <= mf->nice_len);
			assert(matches[i].dist < mf->read_pos);
			assert(memcmp(mf_ptr(mf) - 1,
				mf_ptr(mf) - matches[i].dist - 2,
				matches[i].len) == 0);
		}
#endif

		// The last used element in the array contains
		// the longest match.
		len_best = matches[count - 1].len;

		// If a match of maximum search length was found, try to
		// extend the match to maximum possible length.
		if (len_best == mf->nice_len) {
			// The limit for the match length is either the
			// maximum match length supported by the LZ-based
			// encoder or the number of bytes left in the
			// dictionary, whichever is smaller.
			uint32_t limit = mf_avail(mf) + 1;
			if (limit > mf->match_len_max)
				limit = mf->match_len_max;

			// Pointer to the byte we just ran through
			// the match finder.
			const uint8_t *p1 = mf_ptr(mf) - 1;

			// Pointer to the beginning of the match. We need -1
			// here because the match distances are zero based.
			const uint8_t *p2 = p1 - matches[count - 1].dist - 1;

			len_best = lzma_memcmplen(p1, p2, len_best, limit);
		}
	}

	*count_ptr = count;

	// Finally update the read position to indicate that match finder was
	// run for this dictionary offset.
	++mf->read_ahead;

	return len_best;
}


/// Hash value to indicate unused element in the hash. Since we start the
/// positions from dict_size + 1, zero is always too far to qualify
/// as usable match position.
#define EMPTY_HASH_VALUE 0


/// Normalization must be done when lzma_mf.offset + lzma_mf.read_pos
/// reaches MUST_NORMALIZE_POS.
#define MUST_NORMALIZE_POS UINT32_MAX


/// \brief      Normalizes hash values
///
/// The hash arrays store positions of match candidates. The positions are
/// relative to an arbitrary offset that is not the same as the absolute
/// offset in the input stream. The relative position of the current byte
/// is lzma_mf.offset + lzma_mf.read_pos. The distances of the matches are
/// the differences of the current read position and the position found from
/// the hash.
///
/// To prevent integer overflows of the offsets stored in the hash arrays,
/// we need to "normalize" the stored values now and then. During the
/// normalization, we drop values that indicate distance greater than the
/// dictionary size, thus making space for new values.
static void
normalize(lzma_mf *mf)
{
	assert(mf->read_pos + mf->offset == MUST_NORMALIZE_POS);

	// In future we may not want to touch the lowest bits, because there
	// may be match finders that use larger resolution than one byte.
	const uint32_t subvalue
			= (MUST_NORMALIZE_POS - mf->cyclic_size);
				// & (~(UINT32_C(1) << 10) - 1);

	for (uint32_t i = 0; i < mf->hash_count; ++i) {
		// If the distance is greater than the dictionary size,
		// we can simply mark the hash element as empty.
		if (mf->hash[i] <= subvalue)
			mf->hash[i] = EMPTY_HASH_VALUE;
		else
			mf->hash[i] -= subvalue;
	}

	for (uint32_t i = 0; i < mf->sons_count; ++i) {
		// Do the same for mf->son.
		//
		// NOTE: There may be uninitialized elements in mf->son.
		// Valgrind may complain that the "if" below depends on
		// an uninitialized value. In this case it is safe to ignore
		// the warning. See also the comments in lz_encoder_init()
		// in lz_encoder.c.
		if (mf->son[i] <= subvalue)
			mf->son[i] = EMPTY_HASH_VALUE;
		else
			mf->son[i] -= subvalue;
	}

	// Update offset to match the new locations.
	mf->offset -= subvalue;

	return;
}


/// Mark the current byte as processed from point of view of the match finder.
static void
move_pos(lzma_mf *mf)
{
	if (++mf->cyclic_pos == mf->cyclic_size)
		mf->cyclic_pos = 0;

	++mf->read_pos;
	assert(mf->read_pos <= mf->write_pos);

	if (unlikely(mf->read_pos + mf->offset == UINT32_MAX))
		normalize(mf);
}


/// When flushing, we cannot run the match finder unless there is nice_len
/// bytes available in the dictionary. Instead, we skip running the match
/// finder (indicating that no match was found), and count how many bytes we
/// have ignored this way.
///
/// When new data is given after the flushing was completed, the match finder
/// is restarted by rewinding mf->read_pos backwards by mf->pending. Then
/// the missed bytes are added to the hash using the match finder's skip
/// function (with small amount of input, it may start using mf->pending
/// again if flushing).
///
/// Due to this rewinding, we don't touch cyclic_pos or test for
/// normalization. It will be done when the match finder's skip function
/// catches up after a flush.
static void
move_pending(lzma_mf *mf)
{
	++mf->read_pos;
	assert(mf->read_pos <= mf->write_pos);
	++mf->pending;
}


/// Calculate len_limit and determine if there is enough input to run
/// the actual match finder code. Sets up "cur" and "pos". This macro
/// is used by all find functions and binary tree skip functions. Hash
/// chain skip function doesn't need len_limit so a simpler code is used
/// in them.
#define header(is_bt, len_min, ret_op) \
	uint32_t len_limit = mf_avail(mf); \
	if (mf->nice_len <= len_limit) { \
		len_limit = mf->nice_len; \
	} else if (len_limit < (len_min) \
			|| (is_bt && mf->action == LZMA_SYNC_FLUSH)) { \
		assert(mf->action != LZMA_RUN); \
		move_pending(mf); \
		ret_op; \
	} \
	const uint8_t *cur = mf_ptr(mf); \
	const uint32_t pos = mf->read_pos + mf->offset


/// Header for find functions. "return 0" indicates that zero matches
/// were found.
#define header_find(is_bt, len_min) \
	header(is_bt, len_min, return 0); \
	uint32_t matches_count = 0


/// Header for a loop in a skip function. "continue" tells to skip the rest
/// of the code in the loop.
#define header_skip(is_bt, len_min) \
	header(is_bt, len_min, continue)


/// Calls hc_find_func() or bt_find_func() and calculates the total number
/// of matches found. Updates the dictionary position and returns the number
/// of matches found.
#define call_find(func, len_best) \
do { \
	matches_count = func(len_limit, pos, cur, cur_match, mf->depth, \
				mf->son, mf->cyclic_pos, mf->cyclic_size, \
				matches + matches_count, len_best) \
			- matches; \
	move_pos(mf); \
	return matches_count; \
} while (0)


////////////////
// Hash Chain //
////////////////

#if defined(HAVE_MF_HC3) || defined(HAVE_MF_HC4)
///
///
/// \param      len_limit       Don't look for matches longer than len_limit.
/// \param      pos             lzma_mf.read_pos + lzma_mf.offset
/// \param      cur             Pointer to current byte (mf_ptr(mf))
/// \param      cur_match       Start position of the current match candidate
/// \param      depth           Maximum length of the hash chain
/// \param      son             lzma_mf.son (contains the hash chain)
/// \param      cyclic_pos
/// \param      cyclic_size
/// \param      matches         Array to hold the matches.
/// \param      len_best        The length of the longest match found so far.
static lzma_match *
hc_find_func(
		const uint32_t len_limit,
		const uint32_t pos,
		const uint8_t *const cur,
		uint32_t cur_match,
		uint32_t depth,
		uint32_t *const son,
		const uint32_t cyclic_pos,
		const uint32_t cyclic_size,
		lzma_match *matches,
		uint32_t len_best)
{
	son[cyclic_pos] = cur_match;

	while (true) {
		const uint32_t delta = pos - cur_match;
		if (depth-- == 0 || delta >= cyclic_size)
			return matches;

		const uint8_t *const pb = cur - delta;
		cur_match = son[cyclic_pos - delta
				+ (delta > cyclic_pos ? cyclic_size : 0)];

		if (pb[len_best] == cur[len_best] && pb[0] == cur[0]) {
			uint32_t len = lzma_memcmplen(pb, cur, 1, len_limit);

			if (len_best < len) {
				len_best = len;
				matches->len = len;
				matches->dist = delta - 1;
				++matches;

				if (len == len_limit)
					return matches;
			}
		}
	}
}


#define hc_find(len_best) \
	call_find(hc_find_func, len_best)


#define hc_skip() \
do { \
	mf->son[mf->cyclic_pos] = cur_match; \
	move_pos(mf); \
} while (0)

#endif


#ifdef HAVE_MF_HC3
extern uint32_t
lzma_mf_hc3_find(lzma_mf *mf, lzma_match *matches)
{
	header_find(false, 3);

	hash_3_calc();

	const uint32_t delta2 = pos - mf->hash[hash_2_value];
	const uint32_t cur_match = mf->hash[FIX_3_HASH_SIZE + hash_value];

	mf->hash[hash_2_value] = pos;
	mf->hash[FIX_3_HASH_SIZE + hash_value] = pos;

	uint32_t len_best = 2;

	if (delta2 < mf->cyclic_size && *(cur - delta2) == *cur) {
		len_best = lzma_memcmplen(cur - delta2, cur,
				len_best, len_limit);

		matches[0].len = len_best;
		matches[0].dist = delta2 - 1;
		matches_count = 1;

		if (len_best == len_limit) {
			hc_skip();
			return 1; // matches_count
		}
	}

	hc_find(len_best);
}


extern void
lzma_mf_hc3_skip(lzma_mf *mf, uint32_t amount)
{
	do {
		if (mf_avail(mf) < 3) {
			move_pending(mf);
			continue;
		}

		const uint8_t *cur = mf_ptr(mf);
		const uint32_t pos = mf->read_pos + mf->offset;

		hash_3_calc();

		const uint32_t cur_match
				= mf->hash[FIX_3_HASH_SIZE + hash_value];

		mf->hash[hash_2_value] = pos;
		mf->hash[FIX_3_HASH_SIZE + hash_value] = pos;

		hc_skip();

	} while (--amount != 0);
}
#endif


#ifdef HAVE_MF_HC4
extern uint32_t
lzma_mf_hc4_find(lzma_mf *mf, lzma_match *matches)
{
	header_find(false, 4);

	hash_4_calc();

	uint32_t delta2 = pos - mf->hash[hash_2_value];
	const uint32_t delta3
			= pos - mf->hash[FIX_3_HASH_SIZE + hash_3_value];
	const uint32_t cur_match = mf->hash[FIX_4_HASH_SIZE + hash_value];

	mf->hash[hash_2_value ] = pos;
	mf->hash[FIX_3_HASH_SIZE + hash_3_value] = pos;
	mf->hash[FIX_4_HASH_SIZE + hash_value] = pos;

	uint32_t len_best = 1;

	if (delta2 < mf->cyclic_size && *(cur - delta2) == *cur) {
		len_best = 2;
		matches[0].len = 2;
		matches[0].dist = delta2 - 1;
		matches_count = 1;
	}

	if (delta2 != delta3 && delta3 < mf->cyclic_size
			&& *(cur - delta3) == *cur) {
		len_best = 3;
		matches[matches_count++].dist = delta3 - 1;
		delta2 = delta3;
	}

	if (matches_count != 0) {
		len_best = lzma_memcmplen(cur - delta2, cur,
				len_best, len_limit);

		matches[matches_count - 1].len = len_best;

		if (len_best == len_limit) {
			hc_skip();
			return matches_count;
		}
	}

	if (len_best < 3)
		len_best = 3;

	hc_find(len_best);
}


extern void
lzma_mf_hc4_skip(lzma_mf *mf, uint32_t amount)
{
	do {
		if (mf_avail(mf) < 4) {
			move_pending(mf);
			continue;
		}

		const uint8_t *cur = mf_ptr(mf);
		const uint32_t pos = mf->read_pos + mf->offset;

		hash_4_calc();

		const uint32_t cur_match
				= mf->hash[FIX_4_HASH_SIZE + hash_value];

		mf->hash[hash_2_value] = pos;
		mf->hash[FIX_3_HASH_SIZE + hash_3_value] = pos;
		mf->hash[FIX_4_HASH_SIZE + hash_value] = pos;

		hc_skip();

	} while (--amount != 0);
}
#endif


/////////////////
// Binary Tree //
/////////////////

#if defined(HAVE_MF_BT2) || defined(HAVE_MF_BT3) || defined(HAVE_MF_BT4)
static lzma_match *
bt_find_func(
		const uint32_t len_limit,
		const uint32_t pos,
		const uint8_t *const cur,
		uint32_t cur_match,
		uint32_t depth,
		uint32_t *const son,
		const uint32_t cyclic_pos,
		const uint32_t cyclic_size,
		lzma_match *matches,
		uint32_t len_best)
{
	uint32_t *ptr0 = son + (cyclic_pos << 1) + 1;
	uint32_t *ptr1 = son + (cyclic_pos << 1);

	uint32_t len0 = 0;
	uint32_t len1 = 0;

	while (true) {
		const uint32_t delta = pos - cur_match;
		if (depth-- == 0 || delta >= cyclic_size) {
			*ptr0 = EMPTY_HASH_VALUE;
			*ptr1 = EMPTY_HASH_VALUE;
			return matches;
		}

		uint32_t *const pair = son + ((cyclic_pos - delta
				+ (delta > cyclic_pos ? cyclic_size : 0))
				<< 1);

		const uint8_t *const pb = cur - delta;
		uint32_t len = my_min(len0, len1);

		if (pb[len] == cur[len]) {
			len = lzma_memcmplen(pb, cur, len + 1, len_limit);

			if (len_best < len) {
				len_best = len;
				matches->len = len;
				matches->dist = delta - 1;
				++matches;

				if (len == len_limit) {
					*ptr1 = pair[0];
					*ptr0 = pair[1];
					return matches;
				}
			}
		}

		if (pb[len] < cur[len]) {
			*ptr1 = cur_match;
			ptr1 = pair + 1;
			cur_match = *ptr1;
			len1 = len;
		} else {
			*ptr0 = cur_match;
			ptr0 = pair;
			cur_match = *ptr0;
			len0 = len;
		}
	}
}


static void
bt_skip_func(
		const uint32_t len_limit,
		const uint32_t pos,
		const uint8_t *const cur,
		uint32_t cur_match,
		uint32_t depth,
		uint32_t *const son,
		const uint32_t cyclic_pos,
		const uint32_t cyclic_size)
{
	uint32_t *ptr0 = son + (cyclic_pos << 1) + 1;
	uint32_t *ptr1 = son + (cyclic_pos << 1);

	uint32_t len0 = 0;
	uint32_t len1 = 0;

	while (true) {
		const uint32_t delta = pos - cur_match;
		if (depth-- == 0 || delta >= cyclic_size) {
			*ptr0 = EMPTY_HASH_VALUE;
			*ptr1 = EMPTY_HASH_VALUE;
			return;
		}

		uint32_t *pair = son + ((cyclic_pos - delta
				+ (delta > cyclic_pos ? cyclic_size : 0))
				<< 1);
		const uint8_t *pb = cur - delta;
		uint32_t len = my_min(len0, len1);

		if (pb[len] == cur[len]) {
			len = lzma_memcmplen(pb, cur, len + 1, len_limit);

			if (len == len_limit) {
				*ptr1 = pair[0];
				*ptr0 = pair[1];
				return;
			}
		}

		if (pb[len] < cur[len]) {
			*ptr1 = cur_match;
			ptr1 = pair + 1;
			cur_match = *ptr1;
			len1 = len;
		} else {
			*ptr0 = cur_match;
			ptr0 = pair;
			cur_match = *ptr0;
			len0 = len;
		}
	}
}


#define bt_find(len_best) \
	call_find(bt_find_func, len_best)

#define bt_skip() \
do { \
	bt_skip_func(len_limit, pos, cur, cur_match, mf->depth, \
			mf->son, mf->cyclic_pos, \
			mf->cyclic_size); \
	move_pos(mf); \
} while (0)

#endif


#ifdef HAVE_MF_BT2
extern uint32_t
lzma_mf_bt2_find(lzma_mf *mf, lzma_match *matches)
{
	header_find(true, 2);

	hash_2_calc();

	const uint32_t cur_match = mf->hash[hash_value];
	mf->hash[hash_value] = pos;

	bt_find(1);
}


extern void
lzma_mf_bt2_skip(lzma_mf *mf, uint32_t amount)
{
	do {
		header_skip(true, 2);

		hash_2_calc();

		const uint32_t cur_match = mf->hash[hash_value];
		mf->hash[hash_value] = pos;

		bt_skip();

	} while (--amount != 0);
}
#endif


#ifdef HAVE_MF_BT3
extern uint32_t
lzma_mf_bt3_find(lzma_mf *mf, lzma_match *matches)
{
	header_find(true, 3);

	hash_3_calc();

	const uint32_t delta2 = pos - mf->hash[hash_2_value];
	const uint32_t cur_match = mf->hash[FIX_3_HASH_SIZE + hash_value];

	mf->hash[hash_2_value] = pos;
	mf->hash[FIX_3_HASH_SIZE + hash_value] = pos;

	uint32_t len_best = 2;

	if (delta2 < mf->cyclic_size && *(cur - delta2) == *cur) {
		len_best = lzma_memcmplen(
				cur, cur - delta2, len_best, len_limit);

		matches[0].len = len_best;
		matches[0].dist = delta2 - 1;
		matches_count = 1;

		if (len_best == len_limit) {
			bt_skip();
			return 1; // matches_count
		}
	}

	bt_find(len_best);
}


extern void
lzma_mf_bt3_skip(lzma_mf *mf, uint32_t amount)
{
	do {
		header_skip(true, 3);

		hash_3_calc();

		const uint32_t cur_match
				= mf->hash[FIX_3_HASH_SIZE + hash_value];

		mf->hash[hash_2_value] = pos;
		mf->hash[FIX_3_HASH_SIZE + hash_value] = pos;

		bt_skip();

	} while (--amount != 0);
}
#endif


#ifdef HAVE_MF_BT4
extern uint32_t
lzma_mf_bt4_find(lzma_mf *mf, lzma_match *matches)
{
	header_find(true, 4);

	hash_4_calc();

	uint32_t delta2 = pos - mf->hash[hash_2_value];
	const uint32_t delta3
			= pos - mf->hash[FIX_3_HASH_SIZE + hash_3_value];
	const uint32_t cur_match = mf->hash[FIX_4_HASH_SIZE + hash_value];

	mf->hash[hash_2_value] = pos;
	mf->hash[FIX_3_HASH_SIZE + hash_3_value] = pos;
	mf->hash[FIX_4_HASH_SIZE + hash_value] = pos;

	uint32_t len_best = 1;

	if (delta2 < mf->cyclic_size && *(cur - delta2) == *cur) {
		len_best = 2;
		matches[0].len = 2;
		matches[0].dist = delta2 - 1;
		matches_count = 1;
	}

	if (delta2 != delta3 && delta3 < mf->cyclic_size
			&& *(cur - delta3) == *cur) {
		len_best = 3;
		matches[matches_count++].dist = delta3 - 1;
		delta2 = delta3;
	}

	if (matches_count != 0) {
		len_best = lzma_memcmplen(
				cur, cur - delta2, len_best, len_limit);

		matches[matches_count - 1].len = len_best;

		if (len_best == len_limit) {
			bt_skip();
			return matches_count;
		}
	}

	if (len_best < 3)
		len_best = 3;

	bt_find(len_best);
}


extern void
lzma_mf_bt4_skip(lzma_mf *mf, uint32_t amount)
{
	do {
		header_skip(true, 4);

		hash_4_calc();

		const uint32_t cur_match
				= mf->hash[FIX_4_HASH_SIZE + hash_value];

		mf->hash[hash_2_value] = pos;
		mf->hash[FIX_3_HASH_SIZE + hash_3_value] = pos;
		mf->hash[FIX_4_HASH_SIZE + hash_value] = pos;

		bt_skip();

	} while (--amount != 0);
}
#endif
