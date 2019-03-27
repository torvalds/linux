///////////////////////////////////////////////////////////////////////////////
//
/// \file       lz_encoder.h
/// \brief      LZ in window and match finder API
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_LZ_ENCODER_H
#define LZMA_LZ_ENCODER_H

#include "common.h"


/// A table of these is used by the LZ-based encoder to hold
/// the length-distance pairs found by the match finder.
typedef struct {
	uint32_t len;
	uint32_t dist;
} lzma_match;


typedef struct lzma_mf_s lzma_mf;
struct lzma_mf_s {
	///////////////
	// In Window //
	///////////////

	/// Pointer to buffer with data to be compressed
	uint8_t *buffer;

	/// Total size of the allocated buffer (that is, including all
	/// the extra space)
	uint32_t size;

	/// Number of bytes that must be kept available in our input history.
	/// That is, once keep_size_before bytes have been processed,
	/// buffer[read_pos - keep_size_before] is the oldest byte that
	/// must be available for reading.
	uint32_t keep_size_before;

	/// Number of bytes that must be kept in buffer after read_pos.
	/// That is, read_pos <= write_pos - keep_size_after as long as
	/// action is LZMA_RUN; when action != LZMA_RUN, read_pos is allowed
	/// to reach write_pos so that the last bytes get encoded too.
	uint32_t keep_size_after;

	/// Match finders store locations of matches using 32-bit integers.
	/// To avoid adjusting several megabytes of integers every time the
	/// input window is moved with move_window, we only adjust the
	/// offset of the buffer. Thus, buffer[value_in_hash_table - offset]
	/// is the byte pointed by value_in_hash_table.
	uint32_t offset;

	/// buffer[read_pos] is the next byte to run through the match
	/// finder. This is incremented in the match finder once the byte
	/// has been processed.
	uint32_t read_pos;

	/// Number of bytes that have been ran through the match finder, but
	/// which haven't been encoded by the LZ-based encoder yet.
	uint32_t read_ahead;

	/// As long as read_pos is less than read_limit, there is enough
	/// input available in buffer for at least one encoding loop.
	///
	/// Because of the stateful API, read_limit may and will get greater
	/// than read_pos quite often. This is taken into account when
	/// calculating the value for keep_size_after.
	uint32_t read_limit;

	/// buffer[write_pos] is the first byte that doesn't contain valid
	/// uncompressed data; that is, the next input byte will be copied
	/// to buffer[write_pos].
	uint32_t write_pos;

	/// Number of bytes not hashed before read_pos. This is needed to
	/// restart the match finder after LZMA_SYNC_FLUSH.
	uint32_t pending;

	//////////////////
	// Match Finder //
	//////////////////

	/// Find matches. Returns the number of distance-length pairs written
	/// to the matches array. This is called only via lzma_mf_find().
	uint32_t (*find)(lzma_mf *mf, lzma_match *matches);

	/// Skips num bytes. This is like find() but doesn't make the
	/// distance-length pairs available, thus being a little faster.
	/// This is called only via mf_skip().
	void (*skip)(lzma_mf *mf, uint32_t num);

	uint32_t *hash;
	uint32_t *son;
	uint32_t cyclic_pos;
	uint32_t cyclic_size; // Must be dictionary size + 1.
	uint32_t hash_mask;

	/// Maximum number of loops in the match finder
	uint32_t depth;

	/// Maximum length of a match that the match finder will try to find.
	uint32_t nice_len;

	/// Maximum length of a match supported by the LZ-based encoder.
	/// If the longest match found by the match finder is nice_len,
	/// mf_find() tries to expand it up to match_len_max bytes.
	uint32_t match_len_max;

	/// When running out of input, binary tree match finders need to know
	/// if it is due to flushing or finishing. The action is used also
	/// by the LZ-based encoders themselves.
	lzma_action action;

	/// Number of elements in hash[]
	uint32_t hash_count;

	/// Number of elements in son[]
	uint32_t sons_count;
};


typedef struct {
	/// Extra amount of data to keep available before the "actual"
	/// dictionary.
	size_t before_size;

	/// Size of the history buffer
	size_t dict_size;

	/// Extra amount of data to keep available after the "actual"
	/// dictionary.
	size_t after_size;

	/// Maximum length of a match that the LZ-based encoder can accept.
	/// This is used to extend matches of length nice_len to the
	/// maximum possible length.
	size_t match_len_max;

	/// Match finder will search matches up to this length.
	/// This must be less than or equal to match_len_max.
	size_t nice_len;

	/// Type of the match finder to use
	lzma_match_finder match_finder;

	/// Maximum search depth
	uint32_t depth;

	/// TODO: Comment
	const uint8_t *preset_dict;

	uint32_t preset_dict_size;

} lzma_lz_options;


// The total usable buffer space at any moment outside the match finder:
// before_size + dict_size + after_size + match_len_max
//
// In reality, there's some extra space allocated to prevent the number of
// memmove() calls reasonable. The bigger the dict_size is, the bigger
// this extra buffer will be since with bigger dictionaries memmove() would
// also take longer.
//
// A single encoder loop in the LZ-based encoder may call the match finder
// (mf_find() or mf_skip()) at most after_size times. In other words,
// a single encoder loop may increment lzma_mf.read_pos at most after_size
// times. Since matches are looked up to
// lzma_mf.buffer[lzma_mf.read_pos + match_len_max - 1], the total
// amount of extra buffer needed after dict_size becomes
// after_size + match_len_max.
//
// before_size has two uses. The first one is to keep literals available
// in cases when the LZ-based encoder has made some read ahead.
// TODO: Maybe this could be changed by making the LZ-based encoders to
// store the actual literals as they do with length-distance pairs.
//
// Algorithms such as LZMA2 first try to compress a chunk, and then check
// if the encoded result is smaller than the uncompressed one. If the chunk
// was uncompressible, it is better to store it in uncompressed form in
// the output stream. To do this, the whole uncompressed chunk has to be
// still available in the history buffer. before_size achieves that.


typedef struct {
	/// Data specific to the LZ-based encoder
	void *coder;

	/// Function to encode from *dict to out[]
	lzma_ret (*code)(void *coder,
			lzma_mf *restrict mf, uint8_t *restrict out,
			size_t *restrict out_pos, size_t out_size);

	/// Free allocated resources
	void (*end)(void *coder, const lzma_allocator *allocator);

	/// Update the options in the middle of the encoding.
	lzma_ret (*options_update)(void *coder, const lzma_filter *filter);

} lzma_lz_encoder;


// Basic steps:
//  1. Input gets copied into the dictionary.
//  2. Data in dictionary gets run through the match finder byte by byte.
//  3. The literals and matches are encoded using e.g. LZMA.
//
// The bytes that have been ran through the match finder, but not encoded yet,
// are called `read ahead'.


/// Get pointer to the first byte not ran through the match finder
static inline const uint8_t *
mf_ptr(const lzma_mf *mf)
{
	return mf->buffer + mf->read_pos;
}


/// Get the number of bytes that haven't been ran through the match finder yet.
static inline uint32_t
mf_avail(const lzma_mf *mf)
{
	return mf->write_pos - mf->read_pos;
}


/// Get the number of bytes that haven't been encoded yet (some of these
/// bytes may have been ran through the match finder though).
static inline uint32_t
mf_unencoded(const lzma_mf *mf)
{
	return mf->write_pos - mf->read_pos + mf->read_ahead;
}


/// Calculate the absolute offset from the beginning of the most recent
/// dictionary reset. Only the lowest four bits are important, so there's no
/// problem that we don't know the 64-bit size of the data encoded so far.
///
/// NOTE: When moving the input window, we need to do it so that the lowest
/// bits of dict->read_pos are not modified to keep this macro working
/// as intended.
static inline uint32_t
mf_position(const lzma_mf *mf)
{
	return mf->read_pos - mf->read_ahead;
}


/// Since everything else begins with mf_, use it also for lzma_mf_find().
#define mf_find lzma_mf_find


/// Skip the given number of bytes. This is used when a good match was found.
/// For example, if mf_find() finds a match of 200 bytes long, the first byte
/// of that match was already consumed by mf_find(), and the rest 199 bytes
/// have to be skipped with mf_skip(mf, 199).
static inline void
mf_skip(lzma_mf *mf, uint32_t amount)
{
	if (amount != 0) {
		mf->skip(mf, amount);
		mf->read_ahead += amount;
	}
}


/// Copies at most *left number of bytes from the history buffer
/// to out[]. This is needed by LZMA2 to encode uncompressed chunks.
static inline void
mf_read(lzma_mf *mf, uint8_t *out, size_t *out_pos, size_t out_size,
		size_t *left)
{
	const size_t out_avail = out_size - *out_pos;
	const size_t copy_size = my_min(out_avail, *left);

	assert(mf->read_ahead == 0);
	assert(mf->read_pos >= *left);

	memcpy(out + *out_pos, mf->buffer + mf->read_pos - *left,
			copy_size);

	*out_pos += copy_size;
	*left -= copy_size;
	return;
}


extern lzma_ret lzma_lz_encoder_init(
		lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters,
		lzma_ret (*lz_init)(lzma_lz_encoder *lz,
			const lzma_allocator *allocator, const void *options,
			lzma_lz_options *lz_options));


extern uint64_t lzma_lz_encoder_memusage(const lzma_lz_options *lz_options);


// These are only for LZ encoder's internal use.
extern uint32_t lzma_mf_find(
		lzma_mf *mf, uint32_t *count, lzma_match *matches);

extern uint32_t lzma_mf_hc3_find(lzma_mf *dict, lzma_match *matches);
extern void lzma_mf_hc3_skip(lzma_mf *dict, uint32_t amount);

extern uint32_t lzma_mf_hc4_find(lzma_mf *dict, lzma_match *matches);
extern void lzma_mf_hc4_skip(lzma_mf *dict, uint32_t amount);

extern uint32_t lzma_mf_bt2_find(lzma_mf *dict, lzma_match *matches);
extern void lzma_mf_bt2_skip(lzma_mf *dict, uint32_t amount);

extern uint32_t lzma_mf_bt3_find(lzma_mf *dict, lzma_match *matches);
extern void lzma_mf_bt3_skip(lzma_mf *dict, uint32_t amount);

extern uint32_t lzma_mf_bt4_find(lzma_mf *dict, lzma_match *matches);
extern void lzma_mf_bt4_skip(lzma_mf *dict, uint32_t amount);

#endif
