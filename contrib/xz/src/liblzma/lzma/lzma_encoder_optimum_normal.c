///////////////////////////////////////////////////////////////////////////////
//
/// \file       lzma_encoder_optimum_normal.c
//
//  Author:     Igor Pavlov
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "lzma_encoder_private.h"
#include "fastpos.h"
#include "memcmplen.h"


////////////
// Prices //
////////////

static uint32_t
get_literal_price(const lzma_lzma1_encoder *const coder, const uint32_t pos,
		const uint32_t prev_byte, const bool match_mode,
		uint32_t match_byte, uint32_t symbol)
{
	const probability *const subcoder = literal_subcoder(coder->literal,
			coder->literal_context_bits, coder->literal_pos_mask,
			pos, prev_byte);

	uint32_t price = 0;

	if (!match_mode) {
		price = rc_bittree_price(subcoder, 8, symbol);
	} else {
		uint32_t offset = 0x100;
		symbol += UINT32_C(1) << 8;

		do {
			match_byte <<= 1;

			const uint32_t match_bit = match_byte & offset;
			const uint32_t subcoder_index
					= offset + match_bit + (symbol >> 8);
			const uint32_t bit = (symbol >> 7) & 1;
			price += rc_bit_price(subcoder[subcoder_index], bit);

			symbol <<= 1;
			offset &= ~(match_byte ^ symbol);

		} while (symbol < (UINT32_C(1) << 16));
	}

	return price;
}


static inline uint32_t
get_len_price(const lzma_length_encoder *const lencoder,
		const uint32_t len, const uint32_t pos_state)
{
	// NOTE: Unlike the other price tables, length prices are updated
	// in lzma_encoder.c
	return lencoder->prices[pos_state][len - MATCH_LEN_MIN];
}


static inline uint32_t
get_short_rep_price(const lzma_lzma1_encoder *const coder,
		const lzma_lzma_state state, const uint32_t pos_state)
{
	return rc_bit_0_price(coder->is_rep0[state])
		+ rc_bit_0_price(coder->is_rep0_long[state][pos_state]);
}


static inline uint32_t
get_pure_rep_price(const lzma_lzma1_encoder *const coder, const uint32_t rep_index,
		const lzma_lzma_state state, uint32_t pos_state)
{
	uint32_t price;

	if (rep_index == 0) {
		price = rc_bit_0_price(coder->is_rep0[state]);
		price += rc_bit_1_price(coder->is_rep0_long[state][pos_state]);
	} else {
		price = rc_bit_1_price(coder->is_rep0[state]);

		if (rep_index == 1) {
			price += rc_bit_0_price(coder->is_rep1[state]);
		} else {
			price += rc_bit_1_price(coder->is_rep1[state]);
			price += rc_bit_price(coder->is_rep2[state],
					rep_index - 2);
		}
	}

	return price;
}


static inline uint32_t
get_rep_price(const lzma_lzma1_encoder *const coder, const uint32_t rep_index,
		const uint32_t len, const lzma_lzma_state state,
		const uint32_t pos_state)
{
	return get_len_price(&coder->rep_len_encoder, len, pos_state)
		+ get_pure_rep_price(coder, rep_index, state, pos_state);
}


static inline uint32_t
get_dist_len_price(const lzma_lzma1_encoder *const coder, const uint32_t dist,
		const uint32_t len, const uint32_t pos_state)
{
	const uint32_t dist_state = get_dist_state(len);
	uint32_t price;

	if (dist < FULL_DISTANCES) {
		price = coder->dist_prices[dist_state][dist];
	} else {
		const uint32_t dist_slot = get_dist_slot_2(dist);
		price = coder->dist_slot_prices[dist_state][dist_slot]
				+ coder->align_prices[dist & ALIGN_MASK];
	}

	price += get_len_price(&coder->match_len_encoder, len, pos_state);

	return price;
}


static void
fill_dist_prices(lzma_lzma1_encoder *coder)
{
	for (uint32_t dist_state = 0; dist_state < DIST_STATES; ++dist_state) {

		uint32_t *const dist_slot_prices
				= coder->dist_slot_prices[dist_state];

		// Price to encode the dist_slot.
		for (uint32_t dist_slot = 0;
				dist_slot < coder->dist_table_size; ++dist_slot)
			dist_slot_prices[dist_slot] = rc_bittree_price(
					coder->dist_slot[dist_state],
					DIST_SLOT_BITS, dist_slot);

		// For matches with distance >= FULL_DISTANCES, add the price
		// of the direct bits part of the match distance. (Align bits
		// are handled by fill_align_prices()).
		for (uint32_t dist_slot = DIST_MODEL_END;
				dist_slot < coder->dist_table_size;
				++dist_slot)
			dist_slot_prices[dist_slot] += rc_direct_price(
					((dist_slot >> 1) - 1) - ALIGN_BITS);

		// Distances in the range [0, 3] are fully encoded with
		// dist_slot, so they are used for coder->dist_prices
		// as is.
		for (uint32_t i = 0; i < DIST_MODEL_START; ++i)
			coder->dist_prices[dist_state][i]
					= dist_slot_prices[i];
	}

	// Distances in the range [4, 127] depend on dist_slot and
	// dist_special. We do this in a loop separate from the above
	// loop to avoid redundant calls to get_dist_slot().
	for (uint32_t i = DIST_MODEL_START; i < FULL_DISTANCES; ++i) {
		const uint32_t dist_slot = get_dist_slot(i);
		const uint32_t footer_bits = ((dist_slot >> 1) - 1);
		const uint32_t base = (2 | (dist_slot & 1)) << footer_bits;
		const uint32_t price = rc_bittree_reverse_price(
				coder->dist_special + base - dist_slot - 1,
				footer_bits, i - base);

		for (uint32_t dist_state = 0; dist_state < DIST_STATES;
				++dist_state)
			coder->dist_prices[dist_state][i]
					= price + coder->dist_slot_prices[
						dist_state][dist_slot];
	}

	coder->match_price_count = 0;
	return;
}


static void
fill_align_prices(lzma_lzma1_encoder *coder)
{
	for (uint32_t i = 0; i < ALIGN_SIZE; ++i)
		coder->align_prices[i] = rc_bittree_reverse_price(
				coder->dist_align, ALIGN_BITS, i);

	coder->align_price_count = 0;
	return;
}


/////////////
// Optimal //
/////////////

static inline void
make_literal(lzma_optimal *optimal)
{
	optimal->back_prev = UINT32_MAX;
	optimal->prev_1_is_literal = false;
}


static inline void
make_short_rep(lzma_optimal *optimal)
{
	optimal->back_prev = 0;
	optimal->prev_1_is_literal = false;
}


#define is_short_rep(optimal) \
	((optimal).back_prev == 0)


static void
backward(lzma_lzma1_encoder *restrict coder, uint32_t *restrict len_res,
		uint32_t *restrict back_res, uint32_t cur)
{
	coder->opts_end_index = cur;

	uint32_t pos_mem = coder->opts[cur].pos_prev;
	uint32_t back_mem = coder->opts[cur].back_prev;

	do {
		if (coder->opts[cur].prev_1_is_literal) {
			make_literal(&coder->opts[pos_mem]);
			coder->opts[pos_mem].pos_prev = pos_mem - 1;

			if (coder->opts[cur].prev_2) {
				coder->opts[pos_mem - 1].prev_1_is_literal
						= false;
				coder->opts[pos_mem - 1].pos_prev
						= coder->opts[cur].pos_prev_2;
				coder->opts[pos_mem - 1].back_prev
						= coder->opts[cur].back_prev_2;
			}
		}

		const uint32_t pos_prev = pos_mem;
		const uint32_t back_cur = back_mem;

		back_mem = coder->opts[pos_prev].back_prev;
		pos_mem = coder->opts[pos_prev].pos_prev;

		coder->opts[pos_prev].back_prev = back_cur;
		coder->opts[pos_prev].pos_prev = cur;
		cur = pos_prev;

	} while (cur != 0);

	coder->opts_current_index = coder->opts[0].pos_prev;
	*len_res = coder->opts[0].pos_prev;
	*back_res = coder->opts[0].back_prev;

	return;
}


//////////
// Main //
//////////

static inline uint32_t
helper1(lzma_lzma1_encoder *restrict coder, lzma_mf *restrict mf,
		uint32_t *restrict back_res, uint32_t *restrict len_res,
		uint32_t position)
{
	const uint32_t nice_len = mf->nice_len;

	uint32_t len_main;
	uint32_t matches_count;

	if (mf->read_ahead == 0) {
		len_main = mf_find(mf, &matches_count, coder->matches);
	} else {
		assert(mf->read_ahead == 1);
		len_main = coder->longest_match_length;
		matches_count = coder->matches_count;
	}

	const uint32_t buf_avail = my_min(mf_avail(mf) + 1, MATCH_LEN_MAX);
	if (buf_avail < 2) {
		*back_res = UINT32_MAX;
		*len_res = 1;
		return UINT32_MAX;
	}

	const uint8_t *const buf = mf_ptr(mf) - 1;

	uint32_t rep_lens[REPS];
	uint32_t rep_max_index = 0;

	for (uint32_t i = 0; i < REPS; ++i) {
		const uint8_t *const buf_back = buf - coder->reps[i] - 1;

		if (not_equal_16(buf, buf_back)) {
			rep_lens[i] = 0;
			continue;
		}

		rep_lens[i] = lzma_memcmplen(buf, buf_back, 2, buf_avail);

		if (rep_lens[i] > rep_lens[rep_max_index])
			rep_max_index = i;
	}

	if (rep_lens[rep_max_index] >= nice_len) {
		*back_res = rep_max_index;
		*len_res = rep_lens[rep_max_index];
		mf_skip(mf, *len_res - 1);
		return UINT32_MAX;
	}


	if (len_main >= nice_len) {
		*back_res = coder->matches[matches_count - 1].dist + REPS;
		*len_res = len_main;
		mf_skip(mf, len_main - 1);
		return UINT32_MAX;
	}

	const uint8_t current_byte = *buf;
	const uint8_t match_byte = *(buf - coder->reps[0] - 1);

	if (len_main < 2 && current_byte != match_byte
			&& rep_lens[rep_max_index] < 2) {
		*back_res = UINT32_MAX;
		*len_res = 1;
		return UINT32_MAX;
	}

	coder->opts[0].state = coder->state;

	const uint32_t pos_state = position & coder->pos_mask;

	coder->opts[1].price = rc_bit_0_price(
				coder->is_match[coder->state][pos_state])
			+ get_literal_price(coder, position, buf[-1],
				!is_literal_state(coder->state),
				match_byte, current_byte);

	make_literal(&coder->opts[1]);

	const uint32_t match_price = rc_bit_1_price(
			coder->is_match[coder->state][pos_state]);
	const uint32_t rep_match_price = match_price
			+ rc_bit_1_price(coder->is_rep[coder->state]);

	if (match_byte == current_byte) {
		const uint32_t short_rep_price = rep_match_price
				+ get_short_rep_price(
					coder, coder->state, pos_state);

		if (short_rep_price < coder->opts[1].price) {
			coder->opts[1].price = short_rep_price;
			make_short_rep(&coder->opts[1]);
		}
	}

	const uint32_t len_end = my_max(len_main, rep_lens[rep_max_index]);

	if (len_end < 2) {
		*back_res = coder->opts[1].back_prev;
		*len_res = 1;
		return UINT32_MAX;
	}

	coder->opts[1].pos_prev = 0;

	for (uint32_t i = 0; i < REPS; ++i)
		coder->opts[0].backs[i] = coder->reps[i];

	uint32_t len = len_end;
	do {
		coder->opts[len].price = RC_INFINITY_PRICE;
	} while (--len >= 2);


	for (uint32_t i = 0; i < REPS; ++i) {
		uint32_t rep_len = rep_lens[i];
		if (rep_len < 2)
			continue;

		const uint32_t price = rep_match_price + get_pure_rep_price(
				coder, i, coder->state, pos_state);

		do {
			const uint32_t cur_and_len_price = price
					+ get_len_price(
						&coder->rep_len_encoder,
						rep_len, pos_state);

			if (cur_and_len_price < coder->opts[rep_len].price) {
				coder->opts[rep_len].price = cur_and_len_price;
				coder->opts[rep_len].pos_prev = 0;
				coder->opts[rep_len].back_prev = i;
				coder->opts[rep_len].prev_1_is_literal = false;
			}
		} while (--rep_len >= 2);
	}


	const uint32_t normal_match_price = match_price
			+ rc_bit_0_price(coder->is_rep[coder->state]);

	len = rep_lens[0] >= 2 ? rep_lens[0] + 1 : 2;
	if (len <= len_main) {
		uint32_t i = 0;
		while (len > coder->matches[i].len)
			++i;

		for(; ; ++len) {
			const uint32_t dist = coder->matches[i].dist;
			const uint32_t cur_and_len_price = normal_match_price
					+ get_dist_len_price(coder,
						dist, len, pos_state);

			if (cur_and_len_price < coder->opts[len].price) {
				coder->opts[len].price = cur_and_len_price;
				coder->opts[len].pos_prev = 0;
				coder->opts[len].back_prev = dist + REPS;
				coder->opts[len].prev_1_is_literal = false;
			}

			if (len == coder->matches[i].len)
				if (++i == matches_count)
					break;
		}
	}

	return len_end;
}


static inline uint32_t
helper2(lzma_lzma1_encoder *coder, uint32_t *reps, const uint8_t *buf,
		uint32_t len_end, uint32_t position, const uint32_t cur,
		const uint32_t nice_len, const uint32_t buf_avail_full)
{
	uint32_t matches_count = coder->matches_count;
	uint32_t new_len = coder->longest_match_length;
	uint32_t pos_prev = coder->opts[cur].pos_prev;
	lzma_lzma_state state;

	if (coder->opts[cur].prev_1_is_literal) {
		--pos_prev;

		if (coder->opts[cur].prev_2) {
			state = coder->opts[coder->opts[cur].pos_prev_2].state;

			if (coder->opts[cur].back_prev_2 < REPS)
				update_long_rep(state);
			else
				update_match(state);

		} else {
			state = coder->opts[pos_prev].state;
		}

		update_literal(state);

	} else {
		state = coder->opts[pos_prev].state;
	}

	if (pos_prev == cur - 1) {
		if (is_short_rep(coder->opts[cur]))
			update_short_rep(state);
		else
			update_literal(state);
	} else {
		uint32_t pos;
		if (coder->opts[cur].prev_1_is_literal
				&& coder->opts[cur].prev_2) {
			pos_prev = coder->opts[cur].pos_prev_2;
			pos = coder->opts[cur].back_prev_2;
			update_long_rep(state);
		} else {
			pos = coder->opts[cur].back_prev;
			if (pos < REPS)
				update_long_rep(state);
			else
				update_match(state);
		}

		if (pos < REPS) {
			reps[0] = coder->opts[pos_prev].backs[pos];

			uint32_t i;
			for (i = 1; i <= pos; ++i)
				reps[i] = coder->opts[pos_prev].backs[i - 1];

			for (; i < REPS; ++i)
				reps[i] = coder->opts[pos_prev].backs[i];

		} else {
			reps[0] = pos - REPS;

			for (uint32_t i = 1; i < REPS; ++i)
				reps[i] = coder->opts[pos_prev].backs[i - 1];
		}
	}

	coder->opts[cur].state = state;

	for (uint32_t i = 0; i < REPS; ++i)
		coder->opts[cur].backs[i] = reps[i];

	const uint32_t cur_price = coder->opts[cur].price;

	const uint8_t current_byte = *buf;
	const uint8_t match_byte = *(buf - reps[0] - 1);

	const uint32_t pos_state = position & coder->pos_mask;

	const uint32_t cur_and_1_price = cur_price
			+ rc_bit_0_price(coder->is_match[state][pos_state])
			+ get_literal_price(coder, position, buf[-1],
			!is_literal_state(state), match_byte, current_byte);

	bool next_is_literal = false;

	if (cur_and_1_price < coder->opts[cur + 1].price) {
		coder->opts[cur + 1].price = cur_and_1_price;
		coder->opts[cur + 1].pos_prev = cur;
		make_literal(&coder->opts[cur + 1]);
		next_is_literal = true;
	}

	const uint32_t match_price = cur_price
			+ rc_bit_1_price(coder->is_match[state][pos_state]);
	const uint32_t rep_match_price = match_price
			+ rc_bit_1_price(coder->is_rep[state]);

	if (match_byte == current_byte
			&& !(coder->opts[cur + 1].pos_prev < cur
				&& coder->opts[cur + 1].back_prev == 0)) {

		const uint32_t short_rep_price = rep_match_price
				+ get_short_rep_price(coder, state, pos_state);

		if (short_rep_price <= coder->opts[cur + 1].price) {
			coder->opts[cur + 1].price = short_rep_price;
			coder->opts[cur + 1].pos_prev = cur;
			make_short_rep(&coder->opts[cur + 1]);
			next_is_literal = true;
		}
	}

	if (buf_avail_full < 2)
		return len_end;

	const uint32_t buf_avail = my_min(buf_avail_full, nice_len);

	if (!next_is_literal && match_byte != current_byte) { // speed optimization
		// try literal + rep0
		const uint8_t *const buf_back = buf - reps[0] - 1;
		const uint32_t limit = my_min(buf_avail_full, nice_len + 1);

		const uint32_t len_test = lzma_memcmplen(buf, buf_back, 1, limit) - 1;

		if (len_test >= 2) {
			lzma_lzma_state state_2 = state;
			update_literal(state_2);

			const uint32_t pos_state_next = (position + 1) & coder->pos_mask;
			const uint32_t next_rep_match_price = cur_and_1_price
					+ rc_bit_1_price(coder->is_match[state_2][pos_state_next])
					+ rc_bit_1_price(coder->is_rep[state_2]);

			//for (; len_test >= 2; --len_test) {
			const uint32_t offset = cur + 1 + len_test;

			while (len_end < offset)
				coder->opts[++len_end].price = RC_INFINITY_PRICE;

			const uint32_t cur_and_len_price = next_rep_match_price
					+ get_rep_price(coder, 0, len_test,
						state_2, pos_state_next);

			if (cur_and_len_price < coder->opts[offset].price) {
				coder->opts[offset].price = cur_and_len_price;
				coder->opts[offset].pos_prev = cur + 1;
				coder->opts[offset].back_prev = 0;
				coder->opts[offset].prev_1_is_literal = true;
				coder->opts[offset].prev_2 = false;
			}
			//}
		}
	}


	uint32_t start_len = 2; // speed optimization

	for (uint32_t rep_index = 0; rep_index < REPS; ++rep_index) {
		const uint8_t *const buf_back = buf - reps[rep_index] - 1;
		if (not_equal_16(buf, buf_back))
			continue;

		uint32_t len_test = lzma_memcmplen(buf, buf_back, 2, buf_avail);

		while (len_end < cur + len_test)
			coder->opts[++len_end].price = RC_INFINITY_PRICE;

		const uint32_t len_test_temp = len_test;
		const uint32_t price = rep_match_price + get_pure_rep_price(
				coder, rep_index, state, pos_state);

		do {
			const uint32_t cur_and_len_price = price
					+ get_len_price(&coder->rep_len_encoder,
							len_test, pos_state);

			if (cur_and_len_price < coder->opts[cur + len_test].price) {
				coder->opts[cur + len_test].price = cur_and_len_price;
				coder->opts[cur + len_test].pos_prev = cur;
				coder->opts[cur + len_test].back_prev = rep_index;
				coder->opts[cur + len_test].prev_1_is_literal = false;
			}
		} while (--len_test >= 2);

		len_test = len_test_temp;

		if (rep_index == 0)
			start_len = len_test + 1;


		uint32_t len_test_2 = len_test + 1;
		const uint32_t limit = my_min(buf_avail_full,
				len_test_2 + nice_len);
		for (; len_test_2 < limit
				&& buf[len_test_2] == buf_back[len_test_2];
				++len_test_2) ;

		len_test_2 -= len_test + 1;

		if (len_test_2 >= 2) {
			lzma_lzma_state state_2 = state;
			update_long_rep(state_2);

			uint32_t pos_state_next = (position + len_test) & coder->pos_mask;

			const uint32_t cur_and_len_literal_price = price
					+ get_len_price(&coder->rep_len_encoder,
						len_test, pos_state)
					+ rc_bit_0_price(coder->is_match[state_2][pos_state_next])
					+ get_literal_price(coder, position + len_test,
						buf[len_test - 1], true,
						buf_back[len_test], buf[len_test]);

			update_literal(state_2);

			pos_state_next = (position + len_test + 1) & coder->pos_mask;

			const uint32_t next_rep_match_price = cur_and_len_literal_price
					+ rc_bit_1_price(coder->is_match[state_2][pos_state_next])
					+ rc_bit_1_price(coder->is_rep[state_2]);

			//for(; len_test_2 >= 2; len_test_2--) {
			const uint32_t offset = cur + len_test + 1 + len_test_2;

			while (len_end < offset)
				coder->opts[++len_end].price = RC_INFINITY_PRICE;

			const uint32_t cur_and_len_price = next_rep_match_price
					+ get_rep_price(coder, 0, len_test_2,
						state_2, pos_state_next);

			if (cur_and_len_price < coder->opts[offset].price) {
				coder->opts[offset].price = cur_and_len_price;
				coder->opts[offset].pos_prev = cur + len_test + 1;
				coder->opts[offset].back_prev = 0;
				coder->opts[offset].prev_1_is_literal = true;
				coder->opts[offset].prev_2 = true;
				coder->opts[offset].pos_prev_2 = cur;
				coder->opts[offset].back_prev_2 = rep_index;
			}
			//}
		}
	}


	//for (uint32_t len_test = 2; len_test <= new_len; ++len_test)
	if (new_len > buf_avail) {
		new_len = buf_avail;

		matches_count = 0;
		while (new_len > coder->matches[matches_count].len)
			++matches_count;

		coder->matches[matches_count++].len = new_len;
	}


	if (new_len >= start_len) {
		const uint32_t normal_match_price = match_price
				+ rc_bit_0_price(coder->is_rep[state]);

		while (len_end < cur + new_len)
			coder->opts[++len_end].price = RC_INFINITY_PRICE;

		uint32_t i = 0;
		while (start_len > coder->matches[i].len)
			++i;

		for (uint32_t len_test = start_len; ; ++len_test) {
			const uint32_t cur_back = coder->matches[i].dist;
			uint32_t cur_and_len_price = normal_match_price
					+ get_dist_len_price(coder,
						cur_back, len_test, pos_state);

			if (cur_and_len_price < coder->opts[cur + len_test].price) {
				coder->opts[cur + len_test].price = cur_and_len_price;
				coder->opts[cur + len_test].pos_prev = cur;
				coder->opts[cur + len_test].back_prev
						= cur_back + REPS;
				coder->opts[cur + len_test].prev_1_is_literal = false;
			}

			if (len_test == coder->matches[i].len) {
				// Try Match + Literal + Rep0
				const uint8_t *const buf_back = buf - cur_back - 1;
				uint32_t len_test_2 = len_test + 1;
				const uint32_t limit = my_min(buf_avail_full,
						len_test_2 + nice_len);

				for (; len_test_2 < limit &&
						buf[len_test_2] == buf_back[len_test_2];
						++len_test_2) ;

				len_test_2 -= len_test + 1;

				if (len_test_2 >= 2) {
					lzma_lzma_state state_2 = state;
					update_match(state_2);
					uint32_t pos_state_next
							= (position + len_test) & coder->pos_mask;

					const uint32_t cur_and_len_literal_price = cur_and_len_price
							+ rc_bit_0_price(
								coder->is_match[state_2][pos_state_next])
							+ get_literal_price(coder,
								position + len_test,
								buf[len_test - 1],
								true,
								buf_back[len_test],
								buf[len_test]);

					update_literal(state_2);
					pos_state_next = (pos_state_next + 1) & coder->pos_mask;

					const uint32_t next_rep_match_price
							= cur_and_len_literal_price
							+ rc_bit_1_price(
								coder->is_match[state_2][pos_state_next])
							+ rc_bit_1_price(coder->is_rep[state_2]);

					// for(; len_test_2 >= 2; --len_test_2) {
					const uint32_t offset = cur + len_test + 1 + len_test_2;

					while (len_end < offset)
						coder->opts[++len_end].price = RC_INFINITY_PRICE;

					cur_and_len_price = next_rep_match_price
							+ get_rep_price(coder, 0, len_test_2,
								state_2, pos_state_next);

					if (cur_and_len_price < coder->opts[offset].price) {
						coder->opts[offset].price = cur_and_len_price;
						coder->opts[offset].pos_prev = cur + len_test + 1;
						coder->opts[offset].back_prev = 0;
						coder->opts[offset].prev_1_is_literal = true;
						coder->opts[offset].prev_2 = true;
						coder->opts[offset].pos_prev_2 = cur;
						coder->opts[offset].back_prev_2
								= cur_back + REPS;
					}
					//}
				}

				if (++i == matches_count)
					break;
			}
		}
	}

	return len_end;
}


extern void
lzma_lzma_optimum_normal(lzma_lzma1_encoder *restrict coder,
		lzma_mf *restrict mf,
		uint32_t *restrict back_res, uint32_t *restrict len_res,
		uint32_t position)
{
	// If we have symbols pending, return the next pending symbol.
	if (coder->opts_end_index != coder->opts_current_index) {
		assert(mf->read_ahead > 0);
		*len_res = coder->opts[coder->opts_current_index].pos_prev
				- coder->opts_current_index;
		*back_res = coder->opts[coder->opts_current_index].back_prev;
		coder->opts_current_index = coder->opts[
				coder->opts_current_index].pos_prev;
		return;
	}

	// Update the price tables. In LZMA SDK <= 4.60 (and possibly later)
	// this was done in both initialization function and in the main loop.
	// In liblzma they were moved into this single place.
	if (mf->read_ahead == 0) {
		if (coder->match_price_count >= (1 << 7))
			fill_dist_prices(coder);

		if (coder->align_price_count >= ALIGN_SIZE)
			fill_align_prices(coder);
	}

	// TODO: This needs quite a bit of cleaning still. But splitting
	// the original function into two pieces makes it at least a little
	// more readable, since those two parts don't share many variables.

	uint32_t len_end = helper1(coder, mf, back_res, len_res, position);
	if (len_end == UINT32_MAX)
		return;

	uint32_t reps[REPS];
	memcpy(reps, coder->reps, sizeof(reps));

	uint32_t cur;
	for (cur = 1; cur < len_end; ++cur) {
		assert(cur < OPTS);

		coder->longest_match_length = mf_find(
				mf, &coder->matches_count, coder->matches);

		if (coder->longest_match_length >= mf->nice_len)
			break;

		len_end = helper2(coder, reps, mf_ptr(mf) - 1, len_end,
				position + cur, cur, mf->nice_len,
				my_min(mf_avail(mf) + 1, OPTS - 1 - cur));
	}

	backward(coder, len_res, back_res, cur);
	return;
}
