// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */
#include "delta-index.h"

#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/compiler.h>
#include <linux/limits.h>
#include <linux/log2.h>

#include "cpu.h"
#include "errors.h"
#include "logger.h"
#include "memory-alloc.h"
#include "numeric.h"
#include "permassert.h"
#include "string-utils.h"
#include "time-utils.h"

#include "config.h"
#include "indexer.h"

/*
 * The entries in a delta index could be stored in a single delta list, but to reduce search times
 * and update costs it uses multiple delta lists. These lists are stored in a single chunk of
 * memory managed by the delta_zone structure. The delta_zone can move the data around within its
 * memory, so the location of each delta list is recorded as a bit offset into the memory. Because
 * the volume index can contain over a million delta lists, we want to be efficient with the size
 * of the delta list header information. This information is encoded into 16 bytes per list. The
 * volume index delta list memory can easily exceed 4 gigabits, so a 64 bit value is needed to
 * address the memory. The volume index delta lists average around 6 kilobits, so 16 bits are
 * sufficient to store the size of a delta list.
 *
 * Each delta list is stored as a bit stream. Within the delta list encoding, bits and bytes are
 * numbered in little endian order. Within a byte, bit 0 is the least significant bit (0x1), and
 * bit 7 is the most significant bit (0x80). Within a bit stream, bit 7 is the most significant bit
 * of byte 0, and bit 8 is the least significant bit of byte 1. Within a byte array, a byte's
 * number corresponds to its index in the array.
 *
 * A standard delta list entry is stored as a fixed length payload (the value) followed by a
 * variable length key (the delta). A collision entry is used when two block names have the same
 * delta list address. A collision entry always follows a standard entry for the hash with which it
 * collides, and is encoded with DELTA == 0 with an additional 256 bits field at the end,
 * containing the full block name. An entry with a delta of 0 at the beginning of a delta list
 * indicates a normal entry.
 *
 * The delta in each entry is encoded with a variable-length Huffman code to minimize the memory
 * used by small deltas. The Huffman code is specified by three parameters, which can be computed
 * from the desired mean delta when the index is full. (See compute_coding_constants() for
 * details.)
 *
 * The bit field utilities used to read and write delta entries assume that it is possible to read
 * some bytes beyond the end of the bit field, so a delta_zone memory allocation is guarded by two
 * invalid delta lists to prevent reading outside the delta_zone memory. The valid delta lists are
 * numbered 1 to N, and the guard lists are numbered 0 and N+1. The function to decode the bit
 * stream include a step that skips over bits set to 0 until the first 1 bit is found. A corrupted
 * delta list could cause this step to run off the end of the delta_zone memory, so as extra
 * protection against this happening, the tail guard list is set to all ones.
 *
 * The delta_index supports two different forms. The mutable form is created by
 * uds_initialize_delta_index(), and is used for the volume index and for open chapter indexes. The
 * immutable form is created by uds_initialize_delta_index_page(), and is used for closed (and
 * cached) chapter index pages. The immutable form does not allocate delta list headers or
 * temporary offsets, and thus is somewhat more memory efficient.
 */

/*
 * This is the largest field size supported by get_field() and set_field(). Any field that is
 * larger is not guaranteed to fit in a single byte-aligned u32.
 */
#define MAX_FIELD_BITS ((sizeof(u32) - 1) * BITS_PER_BYTE + 1)

/*
 * This is the largest field size supported by get_big_field() and set_big_field(). Any field that
 * is larger is not guaranteed to fit in a single byte-aligned u64.
 */
#define MAX_BIG_FIELD_BITS ((sizeof(u64) - 1) * BITS_PER_BYTE + 1)

/*
 * This is the number of guard bytes needed at the end of the memory byte array when using the bit
 * utilities. These utilities call get_big_field() and set_big_field(), which can access up to 7
 * bytes beyond the end of the desired field. The definition is written to make it clear how this
 * value is derived.
 */
#define POST_FIELD_GUARD_BYTES (sizeof(u64) - 1)

/* The number of guard bits that are needed in the tail guard list */
#define GUARD_BITS (POST_FIELD_GUARD_BYTES * BITS_PER_BYTE)

/*
 * The maximum size of a single delta list in bytes. We count guard bytes in this value because a
 * buffer of this size can be used with move_bits().
 */
#define DELTA_LIST_MAX_BYTE_COUNT					\
	((U16_MAX + BITS_PER_BYTE) / BITS_PER_BYTE + POST_FIELD_GUARD_BYTES)

/* The number of extra bytes and bits needed to store a collision entry */
#define COLLISION_BYTES UDS_RECORD_NAME_SIZE
#define COLLISION_BITS (COLLISION_BYTES * BITS_PER_BYTE)

/*
 * Immutable delta lists are packed into pages containing a header that encodes the delta list
 * information into 19 bits per list (64KB bit offset).
 */
#define IMMUTABLE_HEADER_SIZE 19

/*
 * Constants and structures for the saved delta index. "DI" is for delta_index, and -##### is a
 * number to increment when the format of the data changes.
 */
#define MAGIC_SIZE 8

static const char DELTA_INDEX_MAGIC[] = "DI-00002";

struct delta_index_header {
	char magic[MAGIC_SIZE];
	u32 zone_number;
	u32 zone_count;
	u32 first_list;
	u32 list_count;
	u64 record_count;
	u64 collision_count;
};

/*
 * Header data used for immutable delta index pages. This data is followed by the delta list offset
 * table.
 */
struct delta_page_header {
	/* Externally-defined nonce */
	u64 nonce;
	/* The virtual chapter number */
	u64 virtual_chapter_number;
	/* Index of the first delta list on the page */
	u16 first_list;
	/* Number of delta lists on the page */
	u16 list_count;
} __packed;

static inline u64 get_delta_list_byte_start(const struct delta_list *delta_list)
{
	return delta_list->start / BITS_PER_BYTE;
}

static inline u16 get_delta_list_byte_size(const struct delta_list *delta_list)
{
	unsigned int bit_offset = delta_list->start % BITS_PER_BYTE;

	return BITS_TO_BYTES(bit_offset + delta_list->size);
}

static void rebalance_delta_zone(const struct delta_zone *delta_zone, u32 first,
				 u32 last)
{
	struct delta_list *delta_list;
	u64 new_start;

	if (first == last) {
		/* Only one list is moving, and we know there is space. */
		delta_list = &delta_zone->delta_lists[first];
		new_start = delta_zone->new_offsets[first];
		if (delta_list->start != new_start) {
			u64 source;
			u64 destination;

			source = get_delta_list_byte_start(delta_list);
			delta_list->start = new_start;
			destination = get_delta_list_byte_start(delta_list);
			memmove(delta_zone->memory + destination,
				delta_zone->memory + source,
				get_delta_list_byte_size(delta_list));
		}
	} else {
		/*
		 * There is more than one list. Divide the problem in half, and use recursive calls
		 * to process each half. Note that after this computation, first <= middle, and
		 * middle < last.
		 */
		u32 middle = (first + last) / 2;

		delta_list = &delta_zone->delta_lists[middle];
		new_start = delta_zone->new_offsets[middle];

		/*
		 * The direction that our middle list is moving determines which half of the
		 * problem must be processed first.
		 */
		if (new_start > delta_list->start) {
			rebalance_delta_zone(delta_zone, middle + 1, last);
			rebalance_delta_zone(delta_zone, first, middle);
		} else {
			rebalance_delta_zone(delta_zone, first, middle);
			rebalance_delta_zone(delta_zone, middle + 1, last);
		}
	}
}

static inline size_t get_zone_memory_size(unsigned int zone_count, size_t memory_size)
{
	/* Round up so that each zone is a multiple of 64K in size. */
	size_t ALLOC_BOUNDARY = 64 * 1024;

	return (memory_size / zone_count + ALLOC_BOUNDARY - 1) & -ALLOC_BOUNDARY;
}

void uds_reset_delta_index(const struct delta_index *delta_index)
{
	unsigned int z;

	/*
	 * Initialize all delta lists to be empty. We keep 2 extra delta list descriptors, one
	 * before the first real entry and one after so that we don't need to bounds check the
	 * array access when calculating preceding and following gap sizes.
	 */
	for (z = 0; z < delta_index->zone_count; z++) {
		u64 list_bits;
		u64 spacing;
		u64 offset;
		unsigned int i;
		struct delta_zone *zone = &delta_index->delta_zones[z];
		struct delta_list *delta_lists = zone->delta_lists;

		/* Zeroing the delta list headers initializes the head guard list correctly. */
		memset(delta_lists, 0,
		       (zone->list_count + 2) * sizeof(struct delta_list));

		/* Set all the bits in the end guard list. */
		list_bits = (u64) zone->size * BITS_PER_BYTE - GUARD_BITS;
		delta_lists[zone->list_count + 1].start = list_bits;
		delta_lists[zone->list_count + 1].size = GUARD_BITS;
		memset(zone->memory + (list_bits / BITS_PER_BYTE), ~0,
		       POST_FIELD_GUARD_BYTES);

		/* Evenly space out the real delta lists by setting regular offsets. */
		spacing = list_bits / zone->list_count;
		offset = spacing / 2;
		for (i = 1; i <= zone->list_count; i++) {
			delta_lists[i].start = offset;
			offset += spacing;
		}

		/* Update the statistics. */
		zone->discard_count += zone->record_count;
		zone->record_count = 0;
		zone->collision_count = 0;
	}
}

/* Compute the Huffman coding parameters for the given mean delta. The Huffman code is specified by
 * three parameters:
 *
 *  MINBITS   The number of bits in the smallest code
 *  BASE      The number of values coded using a code of length MINBITS
 *  INCR      The number of values coded by using one additional bit
 *
 * These parameters are related by this equation:
 *
 *	BASE + INCR == 1 << MINBITS
 *
 * The math for the Huffman code of an exponential distribution says that
 *
 *	INCR = log(2) * MEAN_DELTA
 *
 * Then use the smallest MINBITS value so that
 *
 *	(1 << MINBITS) > INCR
 *
 * And then
 *
 *	BASE = (1 << MINBITS) - INCR
 *
 * Now the index can generate a code such that
 * - The first BASE values code using MINBITS bits.
 * - The next INCR values code using MINBITS+1 bits.
 * - The next INCR values code using MINBITS+2 bits.
 * - (and so on).
 */
static void compute_coding_constants(u32 mean_delta, u16 *min_bits, u32 *min_keys, u32 *incr_keys)
{
	/*
	 * We want to compute the rounded value of log(2) * mean_delta. Since we cannot always use
	 * floating point, use a really good integer approximation.
	 */
	*incr_keys = (836158UL * mean_delta + 603160UL) / 1206321UL;
	*min_bits = bits_per(*incr_keys + 1);
	*min_keys = (1 << *min_bits) - *incr_keys;
}

void uds_uninitialize_delta_index(struct delta_index *delta_index)
{
	unsigned int z;

	if (delta_index->delta_zones == NULL)
		return;

	for (z = 0; z < delta_index->zone_count; z++) {
		vdo_free(vdo_forget(delta_index->delta_zones[z].new_offsets));
		vdo_free(vdo_forget(delta_index->delta_zones[z].delta_lists));
		vdo_free(vdo_forget(delta_index->delta_zones[z].memory));
	}

	vdo_free(delta_index->delta_zones);
	memset(delta_index, 0, sizeof(struct delta_index));
}

static int initialize_delta_zone(struct delta_zone *delta_zone, size_t size,
				 u32 first_list, u32 list_count, u32 mean_delta,
				 u32 payload_bits, u8 tag)
{
	int result;

	result = vdo_allocate(size, u8, "delta list", &delta_zone->memory);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_allocate(list_count + 2, u64, "delta list temp",
			      &delta_zone->new_offsets);
	if (result != VDO_SUCCESS)
		return result;

	/* Allocate the delta lists. */
	result = vdo_allocate(list_count + 2, struct delta_list, "delta lists",
			      &delta_zone->delta_lists);
	if (result != VDO_SUCCESS)
		return result;

	compute_coding_constants(mean_delta, &delta_zone->min_bits,
				 &delta_zone->min_keys, &delta_zone->incr_keys);
	delta_zone->value_bits = payload_bits;
	delta_zone->buffered_writer = NULL;
	delta_zone->size = size;
	delta_zone->rebalance_time = 0;
	delta_zone->rebalance_count = 0;
	delta_zone->record_count = 0;
	delta_zone->collision_count = 0;
	delta_zone->discard_count = 0;
	delta_zone->overflow_count = 0;
	delta_zone->first_list = first_list;
	delta_zone->list_count = list_count;
	delta_zone->tag = tag;

	return UDS_SUCCESS;
}

int uds_initialize_delta_index(struct delta_index *delta_index, unsigned int zone_count,
			       u32 list_count, u32 mean_delta, u32 payload_bits,
			       size_t memory_size, u8 tag)
{
	int result;
	unsigned int z;
	size_t zone_memory;

	result = vdo_allocate(zone_count, struct delta_zone, "Delta Index Zones",
			      &delta_index->delta_zones);
	if (result != VDO_SUCCESS)
		return result;

	delta_index->zone_count = zone_count;
	delta_index->list_count = list_count;
	delta_index->lists_per_zone = DIV_ROUND_UP(list_count, zone_count);
	delta_index->memory_size = 0;
	delta_index->mutable = true;
	delta_index->tag = tag;

	for (z = 0; z < zone_count; z++) {
		u32 lists_in_zone = delta_index->lists_per_zone;
		u32 first_list_in_zone = z * lists_in_zone;

		if (z == zone_count - 1) {
			/*
			 * The last zone gets fewer lists if zone_count doesn't evenly divide
			 * list_count. We'll have an underflow if the assertion below doesn't hold.
			 */
			if (delta_index->list_count <= first_list_in_zone) {
				uds_uninitialize_delta_index(delta_index);
				return vdo_log_error_strerror(UDS_INVALID_ARGUMENT,
							      "%u delta lists not enough for %u zones",
							      list_count, zone_count);
			}
			lists_in_zone = delta_index->list_count - first_list_in_zone;
		}

		zone_memory = get_zone_memory_size(zone_count, memory_size);
		result = initialize_delta_zone(&delta_index->delta_zones[z], zone_memory,
					       first_list_in_zone, lists_in_zone,
					       mean_delta, payload_bits, tag);
		if (result != UDS_SUCCESS) {
			uds_uninitialize_delta_index(delta_index);
			return result;
		}

		delta_index->memory_size +=
			(sizeof(struct delta_zone) + zone_memory +
			 (lists_in_zone + 2) * (sizeof(struct delta_list) + sizeof(u64)));
	}

	uds_reset_delta_index(delta_index);
	return UDS_SUCCESS;
}

/* Read a bit field from an arbitrary bit boundary. */
static inline u32 get_field(const u8 *memory, u64 offset, u8 size)
{
	const void *addr = memory + offset / BITS_PER_BYTE;

	return (get_unaligned_le32(addr) >> (offset % BITS_PER_BYTE)) & ((1 << size) - 1);
}

/* Write a bit field to an arbitrary bit boundary. */
static inline void set_field(u32 value, u8 *memory, u64 offset, u8 size)
{
	void *addr = memory + offset / BITS_PER_BYTE;
	int shift = offset % BITS_PER_BYTE;
	u32 data = get_unaligned_le32(addr);

	data &= ~(((1 << size) - 1) << shift);
	data |= value << shift;
	put_unaligned_le32(data, addr);
}

/* Get the bit offset to the immutable delta list header. */
static inline u32 get_immutable_header_offset(u32 list_number)
{
	return sizeof(struct delta_page_header) * BITS_PER_BYTE +
		list_number * IMMUTABLE_HEADER_SIZE;
}

/* Get the bit offset to the start of the immutable delta list bit stream. */
static inline u32 get_immutable_start(const u8 *memory, u32 list_number)
{
	return get_field(memory, get_immutable_header_offset(list_number),
			 IMMUTABLE_HEADER_SIZE);
}

/* Set the bit offset to the start of the immutable delta list bit stream. */
static inline void set_immutable_start(u8 *memory, u32 list_number, u32 start)
{
	set_field(start, memory, get_immutable_header_offset(list_number),
		  IMMUTABLE_HEADER_SIZE);
}

static bool verify_delta_index_page(u64 nonce, u16 list_count, u64 expected_nonce,
				    u8 *memory, size_t memory_size)
{
	unsigned int i;

	/*
	 * Verify the nonce. A mismatch can happen here during rebuild if we haven't written the
	 * entire volume at least once.
	 */
	if (nonce != expected_nonce)
		return false;

	/* Verify that the number of delta lists can fit in the page. */
	if (list_count > ((memory_size - sizeof(struct delta_page_header)) *
			  BITS_PER_BYTE / IMMUTABLE_HEADER_SIZE))
		return false;

	/*
	 * Verify that the first delta list is immediately after the last delta
	 * list header.
	 */
	if (get_immutable_start(memory, 0) != get_immutable_header_offset(list_count + 1))
		return false;

	/* Verify that the lists are in the correct order. */
	for (i = 0; i < list_count; i++) {
		if (get_immutable_start(memory, i) > get_immutable_start(memory, i + 1))
			return false;
	}

	/*
	 * Verify that the last list ends on the page, and that there is room
	 * for the post-field guard bits.
	 */
	if (get_immutable_start(memory, list_count) >
	    (memory_size - POST_FIELD_GUARD_BYTES) * BITS_PER_BYTE)
		return false;

	/* Verify that the guard bytes are correctly set to all ones. */
	for (i = 0; i < POST_FIELD_GUARD_BYTES; i++) {
		if (memory[memory_size - POST_FIELD_GUARD_BYTES + i] != (u8) ~0)
			return false;
	}

	/* All verifications passed. */
	return true;
}

/* Initialize a delta index page to refer to a supplied page. */
int uds_initialize_delta_index_page(struct delta_index_page *delta_index_page,
				    u64 expected_nonce, u32 mean_delta, u32 payload_bits,
				    u8 *memory, size_t memory_size)
{
	u64 nonce;
	u64 vcn;
	u64 first_list;
	u64 list_count;
	struct delta_page_header *header = (struct delta_page_header *) memory;
	struct delta_zone *delta_zone = &delta_index_page->delta_zone;
	const u8 *nonce_addr = (const u8 *) &header->nonce;
	const u8 *vcn_addr = (const u8 *) &header->virtual_chapter_number;
	const u8 *first_list_addr = (const u8 *) &header->first_list;
	const u8 *list_count_addr = (const u8 *) &header->list_count;

	/* First assume that the header is little endian. */
	nonce = get_unaligned_le64(nonce_addr);
	vcn = get_unaligned_le64(vcn_addr);
	first_list = get_unaligned_le16(first_list_addr);
	list_count = get_unaligned_le16(list_count_addr);
	if (!verify_delta_index_page(nonce, list_count, expected_nonce, memory,
				     memory_size)) {
		/* If that fails, try big endian. */
		nonce = get_unaligned_be64(nonce_addr);
		vcn = get_unaligned_be64(vcn_addr);
		first_list = get_unaligned_be16(first_list_addr);
		list_count = get_unaligned_be16(list_count_addr);
		if (!verify_delta_index_page(nonce, list_count, expected_nonce, memory,
					     memory_size)) {
			/*
			 * Both attempts failed. Do not log this as an error, because it can happen
			 * during a rebuild if we haven't written the entire volume at least once.
			 */
			return UDS_CORRUPT_DATA;
		}
	}

	delta_index_page->delta_index.delta_zones = delta_zone;
	delta_index_page->delta_index.zone_count = 1;
	delta_index_page->delta_index.list_count = list_count;
	delta_index_page->delta_index.lists_per_zone = list_count;
	delta_index_page->delta_index.mutable = false;
	delta_index_page->delta_index.tag = 'p';
	delta_index_page->virtual_chapter_number = vcn;
	delta_index_page->lowest_list_number = first_list;
	delta_index_page->highest_list_number = first_list + list_count - 1;

	compute_coding_constants(mean_delta, &delta_zone->min_bits,
				 &delta_zone->min_keys, &delta_zone->incr_keys);
	delta_zone->value_bits = payload_bits;
	delta_zone->memory = memory;
	delta_zone->delta_lists = NULL;
	delta_zone->new_offsets = NULL;
	delta_zone->buffered_writer = NULL;
	delta_zone->size = memory_size;
	delta_zone->rebalance_time = 0;
	delta_zone->rebalance_count = 0;
	delta_zone->record_count = 0;
	delta_zone->collision_count = 0;
	delta_zone->discard_count = 0;
	delta_zone->overflow_count = 0;
	delta_zone->first_list = 0;
	delta_zone->list_count = list_count;
	delta_zone->tag = 'p';

	return UDS_SUCCESS;
}

/* Read a large bit field from an arbitrary bit boundary. */
static inline u64 get_big_field(const u8 *memory, u64 offset, u8 size)
{
	const void *addr = memory + offset / BITS_PER_BYTE;

	return (get_unaligned_le64(addr) >> (offset % BITS_PER_BYTE)) & ((1UL << size) - 1);
}

/* Write a large bit field to an arbitrary bit boundary. */
static inline void set_big_field(u64 value, u8 *memory, u64 offset, u8 size)
{
	void *addr = memory + offset / BITS_PER_BYTE;
	u8 shift = offset % BITS_PER_BYTE;
	u64 data = get_unaligned_le64(addr);

	data &= ~(((1UL << size) - 1) << shift);
	data |= value << shift;
	put_unaligned_le64(data, addr);
}

/* Set a sequence of bits to all zeros. */
static inline void set_zero(u8 *memory, u64 offset, u32 size)
{
	if (size > 0) {
		u8 *addr = memory + offset / BITS_PER_BYTE;
		u8 shift = offset % BITS_PER_BYTE;
		u32 count = size + shift > BITS_PER_BYTE ? (u32) BITS_PER_BYTE - shift : size;

		*addr++ &= ~(((1 << count) - 1) << shift);
		for (size -= count; size > BITS_PER_BYTE; size -= BITS_PER_BYTE)
			*addr++ = 0;

		if (size > 0)
			*addr &= 0xFF << size;
	}
}

/*
 * Move several bits from a higher to a lower address, moving the lower addressed bits first. The
 * size and memory offsets are measured in bits.
 */
static void move_bits_down(const u8 *from, u64 from_offset, u8 *to, u64 to_offset, u32 size)
{
	const u8 *source;
	u8 *destination;
	u8 offset;
	u8 count;
	u64 field;

	/* Start by moving one field that ends on a to int boundary. */
	count = (MAX_BIG_FIELD_BITS - ((to_offset + MAX_BIG_FIELD_BITS) % BITS_PER_TYPE(u32)));
	field = get_big_field(from, from_offset, count);
	set_big_field(field, to, to_offset, count);
	from_offset += count;
	to_offset += count;
	size -= count;

	/* Now do the main loop to copy 32 bit chunks that are int-aligned at the destination. */
	offset = from_offset % BITS_PER_TYPE(u32);
	source = from + (from_offset - offset) / BITS_PER_BYTE;
	destination = to + to_offset / BITS_PER_BYTE;
	while (size > MAX_BIG_FIELD_BITS) {
		put_unaligned_le32(get_unaligned_le64(source) >> offset, destination);
		source += sizeof(u32);
		destination += sizeof(u32);
		from_offset += BITS_PER_TYPE(u32);
		to_offset += BITS_PER_TYPE(u32);
		size -= BITS_PER_TYPE(u32);
	}

	/* Finish up by moving any remaining bits. */
	if (size > 0) {
		field = get_big_field(from, from_offset, size);
		set_big_field(field, to, to_offset, size);
	}
}

/*
 * Move several bits from a lower to a higher address, moving the higher addressed bits first. The
 * size and memory offsets are measured in bits.
 */
static void move_bits_up(const u8 *from, u64 from_offset, u8 *to, u64 to_offset, u32 size)
{
	const u8 *source;
	u8 *destination;
	u8 offset;
	u8 count;
	u64 field;

	/* Start by moving one field that begins on a destination int boundary. */
	count = (to_offset + size) % BITS_PER_TYPE(u32);
	if (count > 0) {
		size -= count;
		field = get_big_field(from, from_offset + size, count);
		set_big_field(field, to, to_offset + size, count);
	}

	/* Now do the main loop to copy 32 bit chunks that are int-aligned at the destination. */
	offset = (from_offset + size) % BITS_PER_TYPE(u32);
	source = from + (from_offset + size - offset) / BITS_PER_BYTE;
	destination = to + (to_offset + size) / BITS_PER_BYTE;
	while (size > MAX_BIG_FIELD_BITS) {
		source -= sizeof(u32);
		destination -= sizeof(u32);
		size -= BITS_PER_TYPE(u32);
		put_unaligned_le32(get_unaligned_le64(source) >> offset, destination);
	}

	/* Finish up by moving any remaining bits. */
	if (size > 0) {
		field = get_big_field(from, from_offset, size);
		set_big_field(field, to, to_offset, size);
	}
}

/*
 * Move bits from one field to another. When the fields overlap, behave as if we first move all the
 * bits from the source to a temporary value, and then move all the bits from the temporary value
 * to the destination. The size and memory offsets are measured in bits.
 */
static void move_bits(const u8 *from, u64 from_offset, u8 *to, u64 to_offset, u32 size)
{
	u64 field;

	/* A small move doesn't require special handling. */
	if (size <= MAX_BIG_FIELD_BITS) {
		if (size > 0) {
			field = get_big_field(from, from_offset, size);
			set_big_field(field, to, to_offset, size);
		}

		return;
	}

	if (from_offset > to_offset)
		move_bits_down(from, from_offset, to, to_offset, size);
	else
		move_bits_up(from, from_offset, to, to_offset, size);
}

/*
 * Pack delta lists from a mutable delta index into an immutable delta index page. A range of delta
 * lists (starting with a specified list index) is copied from the mutable delta index into a
 * memory page used in the immutable index. The number of lists copied onto the page is returned in
 * list_count.
 */
int uds_pack_delta_index_page(const struct delta_index *delta_index, u64 header_nonce,
			      u8 *memory, size_t memory_size, u64 virtual_chapter_number,
			      u32 first_list, u32 *list_count)
{
	const struct delta_zone *delta_zone;
	struct delta_list *delta_lists;
	u32 max_lists;
	u32 n_lists = 0;
	u32 offset;
	u32 i;
	int free_bits;
	int bits;
	struct delta_page_header *header;

	delta_zone = &delta_index->delta_zones[0];
	delta_lists = &delta_zone->delta_lists[first_list + 1];
	max_lists = delta_index->list_count - first_list;

	/*
	 * Compute how many lists will fit on the page. Subtract the size of the fixed header, one
	 * delta list offset, and the guard bytes from the page size to determine how much space is
	 * available for delta lists.
	 */
	free_bits = memory_size * BITS_PER_BYTE;
	free_bits -= get_immutable_header_offset(1);
	free_bits -= GUARD_BITS;
	if (free_bits < IMMUTABLE_HEADER_SIZE) {
		/* This page is too small to store any delta lists. */
		return vdo_log_error_strerror(UDS_OVERFLOW,
					      "Chapter Index Page of %zu bytes is too small",
					      memory_size);
	}

	while (n_lists < max_lists) {
		/* Each list requires a delta list offset and the list data. */
		bits = IMMUTABLE_HEADER_SIZE + delta_lists[n_lists].size;
		if (bits > free_bits)
			break;

		n_lists++;
		free_bits -= bits;
	}

	*list_count = n_lists;

	header = (struct delta_page_header *) memory;
	put_unaligned_le64(header_nonce, (u8 *) &header->nonce);
	put_unaligned_le64(virtual_chapter_number,
			   (u8 *) &header->virtual_chapter_number);
	put_unaligned_le16(first_list, (u8 *) &header->first_list);
	put_unaligned_le16(n_lists, (u8 *) &header->list_count);

	/* Construct the delta list offset table. */
	offset = get_immutable_header_offset(n_lists + 1);
	set_immutable_start(memory, 0, offset);
	for (i = 0; i < n_lists; i++) {
		offset += delta_lists[i].size;
		set_immutable_start(memory, i + 1, offset);
	}

	/* Copy the delta list data onto the memory page. */
	for (i = 0; i < n_lists; i++) {
		move_bits(delta_zone->memory, delta_lists[i].start, memory,
			  get_immutable_start(memory, i), delta_lists[i].size);
	}

	/* Set all the bits in the guard bytes. */
	memset(memory + memory_size - POST_FIELD_GUARD_BYTES, ~0,
	       POST_FIELD_GUARD_BYTES);
	return UDS_SUCCESS;
}

/* Compute the new offsets of the delta lists. */
static void compute_new_list_offsets(struct delta_zone *delta_zone, u32 growing_index,
				     size_t growing_size, size_t used_space)
{
	size_t spacing;
	u32 i;
	struct delta_list *delta_lists = delta_zone->delta_lists;
	u32 tail_guard_index = delta_zone->list_count + 1;

	spacing = (delta_zone->size - used_space) / delta_zone->list_count;
	delta_zone->new_offsets[0] = 0;
	for (i = 0; i <= delta_zone->list_count; i++) {
		delta_zone->new_offsets[i + 1] =
			(delta_zone->new_offsets[i] +
			 get_delta_list_byte_size(&delta_lists[i]) + spacing);
		delta_zone->new_offsets[i] *= BITS_PER_BYTE;
		delta_zone->new_offsets[i] += delta_lists[i].start % BITS_PER_BYTE;
		if (i == 0)
			delta_zone->new_offsets[i + 1] -= spacing / 2;
		if (i + 1 == growing_index)
			delta_zone->new_offsets[i + 1] += growing_size;
	}

	delta_zone->new_offsets[tail_guard_index] =
		(delta_zone->size * BITS_PER_BYTE - delta_lists[tail_guard_index].size);
}

static void rebalance_lists(struct delta_zone *delta_zone)
{
	struct delta_list *delta_lists;
	u32 i;
	size_t used_space = 0;

	/* Extend and balance memory to receive the delta lists */
	delta_lists = delta_zone->delta_lists;
	for (i = 0; i <= delta_zone->list_count + 1; i++)
		used_space += get_delta_list_byte_size(&delta_lists[i]);

	compute_new_list_offsets(delta_zone, 0, 0, used_space);
	for (i = 1; i <= delta_zone->list_count + 1; i++)
		delta_lists[i].start = delta_zone->new_offsets[i];
}

/* Start restoring a delta index from multiple input streams. */
int uds_start_restoring_delta_index(struct delta_index *delta_index,
				    struct buffered_reader **buffered_readers,
				    unsigned int reader_count)
{
	int result;
	unsigned int zone_count = reader_count;
	u64 record_count = 0;
	u64 collision_count = 0;
	u32 first_list[MAX_ZONES];
	u32 list_count[MAX_ZONES];
	unsigned int z;
	u32 list_next = 0;
	const struct delta_zone *delta_zone;

	/* Read and validate each header. */
	for (z = 0; z < zone_count; z++) {
		struct delta_index_header header;
		u8 buffer[sizeof(struct delta_index_header)];
		size_t offset = 0;

		result = uds_read_from_buffered_reader(buffered_readers[z], buffer,
						       sizeof(buffer));
		if (result != UDS_SUCCESS) {
			return vdo_log_warning_strerror(result,
							"failed to read delta index header");
		}

		memcpy(&header.magic, buffer, MAGIC_SIZE);
		offset += MAGIC_SIZE;
		decode_u32_le(buffer, &offset, &header.zone_number);
		decode_u32_le(buffer, &offset, &header.zone_count);
		decode_u32_le(buffer, &offset, &header.first_list);
		decode_u32_le(buffer, &offset, &header.list_count);
		decode_u64_le(buffer, &offset, &header.record_count);
		decode_u64_le(buffer, &offset, &header.collision_count);

		result = VDO_ASSERT(offset == sizeof(struct delta_index_header),
				    "%zu bytes decoded of %zu expected", offset,
				    sizeof(struct delta_index_header));
		if (result != VDO_SUCCESS) {
			return vdo_log_warning_strerror(result,
							"failed to read delta index header");
		}

		if (memcmp(header.magic, DELTA_INDEX_MAGIC, MAGIC_SIZE) != 0) {
			return vdo_log_warning_strerror(UDS_CORRUPT_DATA,
							"delta index file has bad magic number");
		}

		if (zone_count != header.zone_count) {
			return vdo_log_warning_strerror(UDS_CORRUPT_DATA,
							"delta index files contain mismatched zone counts (%u,%u)",
							zone_count, header.zone_count);
		}

		if (header.zone_number != z) {
			return vdo_log_warning_strerror(UDS_CORRUPT_DATA,
							"delta index zone %u found in slot %u",
							header.zone_number, z);
		}

		first_list[z] = header.first_list;
		list_count[z] = header.list_count;
		record_count += header.record_count;
		collision_count += header.collision_count;

		if (first_list[z] != list_next) {
			return vdo_log_warning_strerror(UDS_CORRUPT_DATA,
							"delta index file for zone %u starts with list %u instead of list %u",
							z, first_list[z], list_next);
		}

		list_next += list_count[z];
	}

	if (list_next != delta_index->list_count) {
		return vdo_log_warning_strerror(UDS_CORRUPT_DATA,
						"delta index files contain %u delta lists instead of %u delta lists",
						list_next, delta_index->list_count);
	}

	if (collision_count > record_count) {
		return vdo_log_warning_strerror(UDS_CORRUPT_DATA,
						"delta index files contain %llu collisions and %llu records",
						(unsigned long long) collision_count,
						(unsigned long long) record_count);
	}

	uds_reset_delta_index(delta_index);
	delta_index->delta_zones[0].record_count = record_count;
	delta_index->delta_zones[0].collision_count = collision_count;

	/* Read the delta lists and distribute them to the proper zones. */
	for (z = 0; z < zone_count; z++) {
		u32 i;

		delta_index->load_lists[z] = 0;
		for (i = 0; i < list_count[z]; i++) {
			u16 delta_list_size;
			u32 list_number;
			unsigned int zone_number;
			u8 size_data[sizeof(u16)];

			result = uds_read_from_buffered_reader(buffered_readers[z],
							       size_data,
							       sizeof(size_data));
			if (result != UDS_SUCCESS) {
				return vdo_log_warning_strerror(result,
								"failed to read delta index size");
			}

			delta_list_size = get_unaligned_le16(size_data);
			if (delta_list_size > 0)
				delta_index->load_lists[z] += 1;

			list_number = first_list[z] + i;
			zone_number = list_number / delta_index->lists_per_zone;
			delta_zone = &delta_index->delta_zones[zone_number];
			list_number -= delta_zone->first_list;
			delta_zone->delta_lists[list_number + 1].size = delta_list_size;
		}
	}

	/* Prepare each zone to start receiving the delta list data. */
	for (z = 0; z < delta_index->zone_count; z++)
		rebalance_lists(&delta_index->delta_zones[z]);

	return UDS_SUCCESS;
}

static int restore_delta_list_to_zone(struct delta_zone *delta_zone,
				      const struct delta_list_save_info *save_info,
				      const u8 *data)
{
	struct delta_list *delta_list;
	u16 bit_count;
	u16 byte_count;
	u32 list_number = save_info->index - delta_zone->first_list;

	if (list_number >= delta_zone->list_count) {
		return vdo_log_warning_strerror(UDS_CORRUPT_DATA,
						"invalid delta list number %u not in range [%u,%u)",
						save_info->index, delta_zone->first_list,
						delta_zone->first_list + delta_zone->list_count);
	}

	delta_list = &delta_zone->delta_lists[list_number + 1];
	if (delta_list->size == 0) {
		return vdo_log_warning_strerror(UDS_CORRUPT_DATA,
						"unexpected delta list number %u",
						save_info->index);
	}

	bit_count = delta_list->size + save_info->bit_offset;
	byte_count = BITS_TO_BYTES(bit_count);
	if (save_info->byte_count != byte_count) {
		return vdo_log_warning_strerror(UDS_CORRUPT_DATA,
						"unexpected delta list size %u != %u",
						save_info->byte_count, byte_count);
	}

	move_bits(data, save_info->bit_offset, delta_zone->memory, delta_list->start,
		  delta_list->size);
	return UDS_SUCCESS;
}

static int restore_delta_list_data(struct delta_index *delta_index, unsigned int load_zone,
				   struct buffered_reader *buffered_reader, u8 *data)
{
	int result;
	struct delta_list_save_info save_info;
	u8 buffer[sizeof(struct delta_list_save_info)];
	unsigned int new_zone;

	result = uds_read_from_buffered_reader(buffered_reader, buffer, sizeof(buffer));
	if (result != UDS_SUCCESS) {
		return vdo_log_warning_strerror(result,
						"failed to read delta list data");
	}

	save_info = (struct delta_list_save_info) {
		.tag = buffer[0],
		.bit_offset = buffer[1],
		.byte_count = get_unaligned_le16(&buffer[2]),
		.index = get_unaligned_le32(&buffer[4]),
	};

	if ((save_info.bit_offset >= BITS_PER_BYTE) ||
	    (save_info.byte_count > DELTA_LIST_MAX_BYTE_COUNT)) {
		return vdo_log_warning_strerror(UDS_CORRUPT_DATA,
						"corrupt delta list data");
	}

	/* Make sure the data is intended for this delta index. */
	if (save_info.tag != delta_index->tag)
		return UDS_CORRUPT_DATA;

	if (save_info.index >= delta_index->list_count) {
		return vdo_log_warning_strerror(UDS_CORRUPT_DATA,
						"invalid delta list number %u of %u",
						save_info.index,
						delta_index->list_count);
	}

	result = uds_read_from_buffered_reader(buffered_reader, data,
					       save_info.byte_count);
	if (result != UDS_SUCCESS) {
		return vdo_log_warning_strerror(result,
						"failed to read delta list data");
	}

	delta_index->load_lists[load_zone] -= 1;
	new_zone = save_info.index / delta_index->lists_per_zone;
	return restore_delta_list_to_zone(&delta_index->delta_zones[new_zone],
					  &save_info, data);
}

/* Restore delta lists from saved data. */
int uds_finish_restoring_delta_index(struct delta_index *delta_index,
				     struct buffered_reader **buffered_readers,
				     unsigned int reader_count)
{
	int result;
	int saved_result = UDS_SUCCESS;
	unsigned int z;
	u8 *data;

	result = vdo_allocate(DELTA_LIST_MAX_BYTE_COUNT, u8, __func__, &data);
	if (result != VDO_SUCCESS)
		return result;

	for (z = 0; z < reader_count; z++) {
		while (delta_index->load_lists[z] > 0) {
			result = restore_delta_list_data(delta_index, z,
							 buffered_readers[z], data);
			if (result != UDS_SUCCESS) {
				saved_result = result;
				break;
			}
		}
	}

	vdo_free(data);
	return saved_result;
}

int uds_check_guard_delta_lists(struct buffered_reader **buffered_readers,
				unsigned int reader_count)
{
	int result;
	unsigned int z;
	u8 buffer[sizeof(struct delta_list_save_info)];

	for (z = 0; z < reader_count; z++) {
		result = uds_read_from_buffered_reader(buffered_readers[z], buffer,
						       sizeof(buffer));
		if (result != UDS_SUCCESS)
			return result;

		if (buffer[0] != 'z')
			return UDS_CORRUPT_DATA;
	}

	return UDS_SUCCESS;
}

static int flush_delta_list(struct delta_zone *zone, u32 flush_index)
{
	struct delta_list *delta_list;
	u8 buffer[sizeof(struct delta_list_save_info)];
	int result;

	delta_list = &zone->delta_lists[flush_index + 1];

	buffer[0] = zone->tag;
	buffer[1] = delta_list->start % BITS_PER_BYTE;
	put_unaligned_le16(get_delta_list_byte_size(delta_list), &buffer[2]);
	put_unaligned_le32(zone->first_list + flush_index, &buffer[4]);

	result = uds_write_to_buffered_writer(zone->buffered_writer, buffer,
					      sizeof(buffer));
	if (result != UDS_SUCCESS) {
		vdo_log_warning_strerror(result, "failed to write delta list memory");
		return result;
	}

	result = uds_write_to_buffered_writer(zone->buffered_writer,
					      zone->memory + get_delta_list_byte_start(delta_list),
					      get_delta_list_byte_size(delta_list));
	if (result != UDS_SUCCESS)
		vdo_log_warning_strerror(result, "failed to write delta list memory");

	return result;
}

/* Start saving a delta index zone to a buffered output stream. */
int uds_start_saving_delta_index(const struct delta_index *delta_index,
				 unsigned int zone_number,
				 struct buffered_writer *buffered_writer)
{
	int result;
	u32 i;
	struct delta_zone *delta_zone;
	u8 buffer[sizeof(struct delta_index_header)];
	size_t offset = 0;

	delta_zone = &delta_index->delta_zones[zone_number];
	memcpy(buffer, DELTA_INDEX_MAGIC, MAGIC_SIZE);
	offset += MAGIC_SIZE;
	encode_u32_le(buffer, &offset, zone_number);
	encode_u32_le(buffer, &offset, delta_index->zone_count);
	encode_u32_le(buffer, &offset, delta_zone->first_list);
	encode_u32_le(buffer, &offset, delta_zone->list_count);
	encode_u64_le(buffer, &offset, delta_zone->record_count);
	encode_u64_le(buffer, &offset, delta_zone->collision_count);

	result = VDO_ASSERT(offset == sizeof(struct delta_index_header),
			    "%zu bytes encoded of %zu expected", offset,
			    sizeof(struct delta_index_header));
	if (result != VDO_SUCCESS)
		return result;

	result = uds_write_to_buffered_writer(buffered_writer, buffer, offset);
	if (result != UDS_SUCCESS)
		return vdo_log_warning_strerror(result,
						"failed to write delta index header");

	for (i = 0; i < delta_zone->list_count; i++) {
		u8 data[sizeof(u16)];
		struct delta_list *delta_list;

		delta_list = &delta_zone->delta_lists[i + 1];
		put_unaligned_le16(delta_list->size, data);
		result = uds_write_to_buffered_writer(buffered_writer, data,
						      sizeof(data));
		if (result != UDS_SUCCESS)
			return vdo_log_warning_strerror(result,
							"failed to write delta list size");
	}

	delta_zone->buffered_writer = buffered_writer;
	return UDS_SUCCESS;
}

int uds_finish_saving_delta_index(const struct delta_index *delta_index,
				  unsigned int zone_number)
{
	int result;
	int first_error = UDS_SUCCESS;
	u32 i;
	struct delta_zone *delta_zone;
	struct delta_list *delta_list;

	delta_zone = &delta_index->delta_zones[zone_number];
	for (i = 0; i < delta_zone->list_count; i++) {
		delta_list = &delta_zone->delta_lists[i + 1];
		if (delta_list->size > 0) {
			result = flush_delta_list(delta_zone, i);
			if ((result != UDS_SUCCESS) && (first_error == UDS_SUCCESS))
				first_error = result;
		}
	}

	delta_zone->buffered_writer = NULL;
	return first_error;
}

int uds_write_guard_delta_list(struct buffered_writer *buffered_writer)
{
	int result;
	u8 buffer[sizeof(struct delta_list_save_info)];

	memset(buffer, 0, sizeof(struct delta_list_save_info));
	buffer[0] = 'z';

	result = uds_write_to_buffered_writer(buffered_writer, buffer, sizeof(buffer));
	if (result != UDS_SUCCESS)
		vdo_log_warning_strerror(result, "failed to write guard delta list");

	return UDS_SUCCESS;
}

size_t uds_compute_delta_index_save_bytes(u32 list_count, size_t memory_size)
{
	/* One zone will use at least as much memory as other zone counts. */
	return (sizeof(struct delta_index_header) +
		list_count * (sizeof(struct delta_list_save_info) + 1) +
		get_zone_memory_size(1, memory_size));
}

static int assert_not_at_end(const struct delta_index_entry *delta_entry)
{
	int result = VDO_ASSERT(!delta_entry->at_end,
				"operation is invalid because the list entry is at the end of the delta list");
	if (result != VDO_SUCCESS)
		result = UDS_BAD_STATE;

	return result;
}

/*
 * Prepare to search for an entry in the specified delta list.
 *
 * This is always the first function to be called when dealing with delta index entries. It is
 * always followed by calls to uds_next_delta_index_entry() to iterate through a delta list. The
 * fields of the delta_index_entry argument will be set up for iteration, but will not contain an
 * entry from the list.
 */
int uds_start_delta_index_search(const struct delta_index *delta_index, u32 list_number,
				 u32 key, struct delta_index_entry *delta_entry)
{
	int result;
	unsigned int zone_number;
	struct delta_zone *delta_zone;
	struct delta_list *delta_list;

	result = VDO_ASSERT((list_number < delta_index->list_count),
			    "Delta list number (%u) is out of range (%u)", list_number,
			    delta_index->list_count);
	if (result != VDO_SUCCESS)
		return UDS_CORRUPT_DATA;

	zone_number = list_number / delta_index->lists_per_zone;
	delta_zone = &delta_index->delta_zones[zone_number];
	list_number -= delta_zone->first_list;
	result = VDO_ASSERT((list_number < delta_zone->list_count),
			    "Delta list number (%u) is out of range (%u) for zone (%u)",
			    list_number, delta_zone->list_count, zone_number);
	if (result != VDO_SUCCESS)
		return UDS_CORRUPT_DATA;

	if (delta_index->mutable) {
		delta_list = &delta_zone->delta_lists[list_number + 1];
	} else {
		u32 end_offset;

		/*
		 * Translate the immutable delta list header into a temporary
		 * full delta list header.
		 */
		delta_list = &delta_entry->temp_delta_list;
		delta_list->start = get_immutable_start(delta_zone->memory, list_number);
		end_offset = get_immutable_start(delta_zone->memory, list_number + 1);
		delta_list->size = end_offset - delta_list->start;
		delta_list->save_key = 0;
		delta_list->save_offset = 0;
	}

	if (key > delta_list->save_key) {
		delta_entry->key = delta_list->save_key;
		delta_entry->offset = delta_list->save_offset;
	} else {
		delta_entry->key = 0;
		delta_entry->offset = 0;
		if (key == 0) {
			/*
			 * This usually means we're about to walk the entire delta list, so get all
			 * of it into the CPU cache.
			 */
			uds_prefetch_range(&delta_zone->memory[delta_list->start / BITS_PER_BYTE],
					   delta_list->size / BITS_PER_BYTE, false);
		}
	}

	delta_entry->at_end = false;
	delta_entry->delta_zone = delta_zone;
	delta_entry->delta_list = delta_list;
	delta_entry->entry_bits = 0;
	delta_entry->is_collision = false;
	delta_entry->list_number = list_number;
	delta_entry->list_overflow = false;
	delta_entry->value_bits = delta_zone->value_bits;
	return UDS_SUCCESS;
}

static inline u64 get_delta_entry_offset(const struct delta_index_entry *delta_entry)
{
	return delta_entry->delta_list->start + delta_entry->offset;
}

/*
 * Decode a delta index entry delta value. The delta_index_entry basically describes the previous
 * list entry, and has had its offset field changed to point to the subsequent entry. We decode the
 * bit stream and update the delta_list_entry to describe the entry.
 */
static inline void decode_delta(struct delta_index_entry *delta_entry)
{
	int key_bits;
	u32 delta;
	const struct delta_zone *delta_zone = delta_entry->delta_zone;
	const u8 *memory = delta_zone->memory;
	u64 delta_offset = get_delta_entry_offset(delta_entry) + delta_entry->value_bits;
	const u8 *addr = memory + delta_offset / BITS_PER_BYTE;
	int offset = delta_offset % BITS_PER_BYTE;
	u32 data = get_unaligned_le32(addr) >> offset;

	addr += sizeof(u32);
	key_bits = delta_zone->min_bits;
	delta = data & ((1 << key_bits) - 1);
	if (delta >= delta_zone->min_keys) {
		data >>= key_bits;
		if (data == 0) {
			key_bits = sizeof(u32) * BITS_PER_BYTE - offset;
			while ((data = get_unaligned_le32(addr)) == 0) {
				addr += sizeof(u32);
				key_bits += sizeof(u32) * BITS_PER_BYTE;
			}
		}
		key_bits += ffs(data);
		delta += ((key_bits - delta_zone->min_bits - 1) * delta_zone->incr_keys);
	}
	delta_entry->delta = delta;
	delta_entry->key += delta;

	/* Check for a collision, a delta of zero after the start. */
	if (unlikely((delta == 0) && (delta_entry->offset > 0))) {
		delta_entry->is_collision = true;
		delta_entry->entry_bits = delta_entry->value_bits + key_bits + COLLISION_BITS;
	} else {
		delta_entry->is_collision = false;
		delta_entry->entry_bits = delta_entry->value_bits + key_bits;
	}
}

noinline int uds_next_delta_index_entry(struct delta_index_entry *delta_entry)
{
	int result;
	const struct delta_list *delta_list;
	u32 next_offset;
	u16 size;

	result = assert_not_at_end(delta_entry);
	if (result != UDS_SUCCESS)
		return result;

	delta_list = delta_entry->delta_list;
	delta_entry->offset += delta_entry->entry_bits;
	size = delta_list->size;
	if (unlikely(delta_entry->offset >= size)) {
		delta_entry->at_end = true;
		delta_entry->delta = 0;
		delta_entry->is_collision = false;
		result = VDO_ASSERT((delta_entry->offset == size),
				    "next offset past end of delta list");
		if (result != VDO_SUCCESS)
			result = UDS_CORRUPT_DATA;

		return result;
	}

	decode_delta(delta_entry);

	next_offset = delta_entry->offset + delta_entry->entry_bits;
	if (next_offset > size) {
		/*
		 * This is not an assertion because uds_validate_chapter_index_page() wants to
		 * handle this error.
		 */
		vdo_log_warning("Decoded past the end of the delta list");
		return UDS_CORRUPT_DATA;
	}

	return UDS_SUCCESS;
}

int uds_remember_delta_index_offset(const struct delta_index_entry *delta_entry)
{
	int result;
	struct delta_list *delta_list = delta_entry->delta_list;

	result = VDO_ASSERT(!delta_entry->is_collision, "entry is not a collision");
	if (result != VDO_SUCCESS)
		return result;

	delta_list->save_key = delta_entry->key - delta_entry->delta;
	delta_list->save_offset = delta_entry->offset;
	return UDS_SUCCESS;
}

static void set_delta(struct delta_index_entry *delta_entry, u32 delta)
{
	const struct delta_zone *delta_zone = delta_entry->delta_zone;
	u32 key_bits = (delta_zone->min_bits +
			((delta_zone->incr_keys - delta_zone->min_keys + delta) /
			 delta_zone->incr_keys));

	delta_entry->delta = delta;
	delta_entry->entry_bits = delta_entry->value_bits + key_bits;
}

static void get_collision_name(const struct delta_index_entry *entry, u8 *name)
{
	u64 offset = get_delta_entry_offset(entry) + entry->entry_bits - COLLISION_BITS;
	const u8 *addr = entry->delta_zone->memory + offset / BITS_PER_BYTE;
	int size = COLLISION_BYTES;
	int shift = offset % BITS_PER_BYTE;

	while (--size >= 0)
		*name++ = get_unaligned_le16(addr++) >> shift;
}

static void set_collision_name(const struct delta_index_entry *entry, const u8 *name)
{
	u64 offset = get_delta_entry_offset(entry) + entry->entry_bits - COLLISION_BITS;
	u8 *addr = entry->delta_zone->memory + offset / BITS_PER_BYTE;
	int size = COLLISION_BYTES;
	int shift = offset % BITS_PER_BYTE;
	u16 mask = ~((u16) 0xFF << shift);
	u16 data;

	while (--size >= 0) {
		data = (get_unaligned_le16(addr) & mask) | (*name++ << shift);
		put_unaligned_le16(data, addr++);
	}
}

int uds_get_delta_index_entry(const struct delta_index *delta_index, u32 list_number,
			      u32 key, const u8 *name,
			      struct delta_index_entry *delta_entry)
{
	int result;

	result = uds_start_delta_index_search(delta_index, list_number, key,
					      delta_entry);
	if (result != UDS_SUCCESS)
		return result;

	do {
		result = uds_next_delta_index_entry(delta_entry);
		if (result != UDS_SUCCESS)
			return result;
	} while (!delta_entry->at_end && (key > delta_entry->key));

	result = uds_remember_delta_index_offset(delta_entry);
	if (result != UDS_SUCCESS)
		return result;

	if (!delta_entry->at_end && (key == delta_entry->key)) {
		struct delta_index_entry collision_entry = *delta_entry;

		for (;;) {
			u8 full_name[COLLISION_BYTES];

			result = uds_next_delta_index_entry(&collision_entry);
			if (result != UDS_SUCCESS)
				return result;

			if (collision_entry.at_end || !collision_entry.is_collision)
				break;

			get_collision_name(&collision_entry, full_name);
			if (memcmp(full_name, name, COLLISION_BYTES) == 0) {
				*delta_entry = collision_entry;
				break;
			}
		}
	}

	return UDS_SUCCESS;
}

int uds_get_delta_entry_collision(const struct delta_index_entry *delta_entry, u8 *name)
{
	int result;

	result = assert_not_at_end(delta_entry);
	if (result != UDS_SUCCESS)
		return result;

	result = VDO_ASSERT(delta_entry->is_collision,
			    "Cannot get full block name from a non-collision delta index entry");
	if (result != VDO_SUCCESS)
		return UDS_BAD_STATE;

	get_collision_name(delta_entry, name);
	return UDS_SUCCESS;
}

u32 uds_get_delta_entry_value(const struct delta_index_entry *delta_entry)
{
	return get_field(delta_entry->delta_zone->memory,
			 get_delta_entry_offset(delta_entry), delta_entry->value_bits);
}

static int assert_mutable_entry(const struct delta_index_entry *delta_entry)
{
	int result = VDO_ASSERT((delta_entry->delta_list != &delta_entry->temp_delta_list),
			        "delta index is mutable");
	if (result != VDO_SUCCESS)
		result = UDS_BAD_STATE;

	return result;
}

int uds_set_delta_entry_value(const struct delta_index_entry *delta_entry, u32 value)
{
	int result;
	u32 value_mask = (1 << delta_entry->value_bits) - 1;

	result = assert_mutable_entry(delta_entry);
	if (result != UDS_SUCCESS)
		return result;

	result = assert_not_at_end(delta_entry);
	if (result != UDS_SUCCESS)
		return result;

	result = VDO_ASSERT((value & value_mask) == value,
			    "Value (%u) being set in a delta index is too large (must fit in %u bits)",
			    value, delta_entry->value_bits);
	if (result != VDO_SUCCESS)
		return UDS_INVALID_ARGUMENT;

	set_field(value, delta_entry->delta_zone->memory,
		  get_delta_entry_offset(delta_entry), delta_entry->value_bits);
	return UDS_SUCCESS;
}

/*
 * Extend the memory used by the delta lists by adding growing_size bytes before the list indicated
 * by growing_index, then rebalancing the lists in the new chunk.
 */
static int extend_delta_zone(struct delta_zone *delta_zone, u32 growing_index,
			     size_t growing_size)
{
	ktime_t start_time;
	ktime_t end_time;
	struct delta_list *delta_lists;
	u32 i;
	size_t used_space;


	/* Calculate the amount of space that is or will be in use. */
	start_time = current_time_ns(CLOCK_MONOTONIC);
	delta_lists = delta_zone->delta_lists;
	used_space = growing_size;
	for (i = 0; i <= delta_zone->list_count + 1; i++)
		used_space += get_delta_list_byte_size(&delta_lists[i]);

	if (delta_zone->size < used_space)
		return UDS_OVERFLOW;

	/* Compute the new offsets of the delta lists. */
	compute_new_list_offsets(delta_zone, growing_index, growing_size, used_space);

	/*
	 * When we rebalance the delta list, we will include the end guard list in the rebalancing.
	 * It contains the end guard data, which must be copied.
	 */
	rebalance_delta_zone(delta_zone, 1, delta_zone->list_count + 1);
	end_time = current_time_ns(CLOCK_MONOTONIC);
	delta_zone->rebalance_count++;
	delta_zone->rebalance_time += ktime_sub(end_time, start_time);
	return UDS_SUCCESS;
}

static int insert_bits(struct delta_index_entry *delta_entry, u16 size)
{
	u64 free_before;
	u64 free_after;
	u64 source;
	u64 destination;
	u32 count;
	bool before_flag;
	u8 *memory;
	struct delta_zone *delta_zone = delta_entry->delta_zone;
	struct delta_list *delta_list = delta_entry->delta_list;
	/* Compute bits in use before and after the inserted bits. */
	u32 total_size = delta_list->size;
	u32 before_size = delta_entry->offset;
	u32 after_size = total_size - delta_entry->offset;

	if (total_size + size > U16_MAX) {
		delta_entry->list_overflow = true;
		delta_zone->overflow_count++;
		return UDS_OVERFLOW;
	}

	/* Compute bits available before and after the delta list. */
	free_before = (delta_list[0].start - (delta_list[-1].start + delta_list[-1].size));
	free_after = (delta_list[1].start - (delta_list[0].start + delta_list[0].size));

	if ((size <= free_before) && (size <= free_after)) {
		/*
		 * We have enough space to use either before or after the list. Select the smaller
		 * amount of data. If it is exactly the same, try to take from the larger amount of
		 * free space.
		 */
		if (before_size < after_size)
			before_flag = true;
		else if (after_size < before_size)
			before_flag = false;
		else
			before_flag = free_before > free_after;
	} else if (size <= free_before) {
		/* There is space before but not after. */
		before_flag = true;
	} else if (size <= free_after) {
		/* There is space after but not before. */
		before_flag = false;
	} else {
		/*
		 * Neither of the surrounding spaces is large enough for this request. Extend
		 * and/or rebalance the delta list memory choosing to move the least amount of
		 * data.
		 */
		int result;
		u32 growing_index = delta_entry->list_number + 1;

		before_flag = before_size < after_size;
		if (!before_flag)
			growing_index++;
		result = extend_delta_zone(delta_zone, growing_index,
					   BITS_TO_BYTES(size));
		if (result != UDS_SUCCESS)
			return result;
	}

	delta_list->size += size;
	if (before_flag) {
		source = delta_list->start;
		destination = source - size;
		delta_list->start -= size;
		count = before_size;
	} else {
		source = delta_list->start + delta_entry->offset;
		destination = source + size;
		count = after_size;
	}

	memory = delta_zone->memory;
	move_bits(memory, source, memory, destination, count);
	return UDS_SUCCESS;
}

static void encode_delta(const struct delta_index_entry *delta_entry)
{
	u32 temp;
	u32 t1;
	u32 t2;
	u64 offset;
	const struct delta_zone *delta_zone = delta_entry->delta_zone;
	u8 *memory = delta_zone->memory;

	offset = get_delta_entry_offset(delta_entry) + delta_entry->value_bits;
	if (delta_entry->delta < delta_zone->min_keys) {
		set_field(delta_entry->delta, memory, offset, delta_zone->min_bits);
		return;
	}

	temp = delta_entry->delta - delta_zone->min_keys;
	t1 = (temp % delta_zone->incr_keys) + delta_zone->min_keys;
	t2 = temp / delta_zone->incr_keys;
	set_field(t1, memory, offset, delta_zone->min_bits);
	set_zero(memory, offset + delta_zone->min_bits, t2);
	set_field(1, memory, offset + delta_zone->min_bits + t2, 1);
}

static void encode_entry(const struct delta_index_entry *delta_entry, u32 value,
			 const u8 *name)
{
	u8 *memory = delta_entry->delta_zone->memory;
	u64 offset = get_delta_entry_offset(delta_entry);

	set_field(value, memory, offset, delta_entry->value_bits);
	encode_delta(delta_entry);
	if (name != NULL)
		set_collision_name(delta_entry, name);
}

/*
 * Create a new entry in the delta index. If the entry is a collision, the full 256 bit name must
 * be provided.
 */
int uds_put_delta_index_entry(struct delta_index_entry *delta_entry, u32 key, u32 value,
			      const u8 *name)
{
	int result;
	struct delta_zone *delta_zone;

	result = assert_mutable_entry(delta_entry);
	if (result != UDS_SUCCESS)
		return result;

	if (delta_entry->is_collision) {
		/*
		 * The caller wants us to insert a collision entry onto a collision entry. This
		 * happens when we find a collision and attempt to add the name again to the index.
		 * This is normally a fatal error unless we are replaying a closed chapter while we
		 * are rebuilding a volume index.
		 */
		return UDS_DUPLICATE_NAME;
	}

	if (delta_entry->offset < delta_entry->delta_list->save_offset) {
		/*
		 * The saved entry offset is after the new entry and will no longer be valid, so
		 * replace it with the insertion point.
		 */
		result = uds_remember_delta_index_offset(delta_entry);
		if (result != UDS_SUCCESS)
			return result;
	}

	if (name != NULL) {
		/* Insert a collision entry which is placed after this entry. */
		result = assert_not_at_end(delta_entry);
		if (result != UDS_SUCCESS)
			return result;

		result = VDO_ASSERT((key == delta_entry->key),
				    "incorrect key for collision entry");
		if (result != VDO_SUCCESS)
			return result;

		delta_entry->offset += delta_entry->entry_bits;
		set_delta(delta_entry, 0);
		delta_entry->is_collision = true;
		delta_entry->entry_bits += COLLISION_BITS;
		result = insert_bits(delta_entry, delta_entry->entry_bits);
	} else if (delta_entry->at_end) {
		/* Insert a new entry at the end of the delta list. */
		result = VDO_ASSERT((key >= delta_entry->key), "key past end of list");
		if (result != VDO_SUCCESS)
			return result;

		set_delta(delta_entry, key - delta_entry->key);
		delta_entry->key = key;
		delta_entry->at_end = false;
		result = insert_bits(delta_entry, delta_entry->entry_bits);
	} else {
		u16 old_entry_size;
		u16 additional_size;
		struct delta_index_entry next_entry;
		u32 next_value;

		/*
		 * Insert a new entry which requires the delta in the following entry to be
		 * updated.
		 */
		result = VDO_ASSERT((key < delta_entry->key),
				    "key precedes following entry");
		if (result != VDO_SUCCESS)
			return result;

		result = VDO_ASSERT((key >= delta_entry->key - delta_entry->delta),
				    "key effects following entry's delta");
		if (result != VDO_SUCCESS)
			return result;

		old_entry_size = delta_entry->entry_bits;
		next_entry = *delta_entry;
		next_value = uds_get_delta_entry_value(&next_entry);
		set_delta(delta_entry, key - (delta_entry->key - delta_entry->delta));
		delta_entry->key = key;
		set_delta(&next_entry, next_entry.key - key);
		next_entry.offset += delta_entry->entry_bits;
		/* The two new entries are always bigger than the single entry being replaced. */
		additional_size = (delta_entry->entry_bits +
				   next_entry.entry_bits - old_entry_size);
		result = insert_bits(delta_entry, additional_size);
		if (result != UDS_SUCCESS)
			return result;

		encode_entry(&next_entry, next_value, NULL);
	}

	if (result != UDS_SUCCESS)
		return result;

	encode_entry(delta_entry, value, name);
	delta_zone = delta_entry->delta_zone;
	delta_zone->record_count++;
	delta_zone->collision_count += delta_entry->is_collision ? 1 : 0;
	return UDS_SUCCESS;
}

static void delete_bits(const struct delta_index_entry *delta_entry, int size)
{
	u64 source;
	u64 destination;
	u32 count;
	bool before_flag;
	struct delta_list *delta_list = delta_entry->delta_list;
	u8 *memory = delta_entry->delta_zone->memory;
	/* Compute bits retained before and after the deleted bits. */
	u32 total_size = delta_list->size;
	u32 before_size = delta_entry->offset;
	u32 after_size = total_size - delta_entry->offset - size;

	/*
	 * Determine whether to add to the available space either before or after the delta list.
	 * We prefer to move the least amount of data. If it is exactly the same, try to add to the
	 * smaller amount of free space.
	 */
	if (before_size < after_size) {
		before_flag = true;
	} else if (after_size < before_size) {
		before_flag = false;
	} else {
		u64 free_before =
			(delta_list[0].start - (delta_list[-1].start + delta_list[-1].size));
		u64 free_after =
			(delta_list[1].start - (delta_list[0].start + delta_list[0].size));

		before_flag = (free_before < free_after);
	}

	delta_list->size -= size;
	if (before_flag) {
		source = delta_list->start;
		destination = source + size;
		delta_list->start += size;
		count = before_size;
	} else {
		destination = delta_list->start + delta_entry->offset;
		source = destination + size;
		count = after_size;
	}

	move_bits(memory, source, memory, destination, count);
}

int uds_remove_delta_index_entry(struct delta_index_entry *delta_entry)
{
	int result;
	struct delta_index_entry next_entry;
	struct delta_zone *delta_zone;
	struct delta_list *delta_list;

	result = assert_mutable_entry(delta_entry);
	if (result != UDS_SUCCESS)
		return result;

	next_entry = *delta_entry;
	result = uds_next_delta_index_entry(&next_entry);
	if (result != UDS_SUCCESS)
		return result;

	delta_zone = delta_entry->delta_zone;

	if (delta_entry->is_collision) {
		/* This is a collision entry, so just remove it. */
		delete_bits(delta_entry, delta_entry->entry_bits);
		next_entry.offset = delta_entry->offset;
		delta_zone->collision_count -= 1;
	} else if (next_entry.at_end) {
		/* This entry is at the end of the list, so just remove it. */
		delete_bits(delta_entry, delta_entry->entry_bits);
		next_entry.key -= delta_entry->delta;
		next_entry.offset = delta_entry->offset;
	} else {
		/* The delta in the next entry needs to be updated. */
		u32 next_value = uds_get_delta_entry_value(&next_entry);
		u16 old_size = delta_entry->entry_bits + next_entry.entry_bits;

		if (next_entry.is_collision) {
			next_entry.is_collision = false;
			delta_zone->collision_count -= 1;
		}

		set_delta(&next_entry, delta_entry->delta + next_entry.delta);
		next_entry.offset = delta_entry->offset;
		/* The one new entry is always smaller than the two entries being replaced. */
		delete_bits(delta_entry, old_size - next_entry.entry_bits);
		encode_entry(&next_entry, next_value, NULL);
	}

	delta_zone->record_count--;
	delta_zone->discard_count++;
	*delta_entry = next_entry;

	delta_list = delta_entry->delta_list;
	if (delta_entry->offset < delta_list->save_offset) {
		/* The saved entry offset is no longer valid. */
		delta_list->save_key = 0;
		delta_list->save_offset = 0;
	}

	return UDS_SUCCESS;
}

void uds_get_delta_index_stats(const struct delta_index *delta_index,
			       struct delta_index_stats *stats)
{
	unsigned int z;
	const struct delta_zone *delta_zone;

	memset(stats, 0, sizeof(struct delta_index_stats));
	for (z = 0; z < delta_index->zone_count; z++) {
		delta_zone = &delta_index->delta_zones[z];
		stats->rebalance_time += delta_zone->rebalance_time;
		stats->rebalance_count += delta_zone->rebalance_count;
		stats->record_count += delta_zone->record_count;
		stats->collision_count += delta_zone->collision_count;
		stats->discard_count += delta_zone->discard_count;
		stats->overflow_count += delta_zone->overflow_count;
		stats->list_count += delta_zone->list_count;
	}
}

size_t uds_compute_delta_index_size(u32 entry_count, u32 mean_delta, u32 payload_bits)
{
	u16 min_bits;
	u32 incr_keys;
	u32 min_keys;

	compute_coding_constants(mean_delta, &min_bits, &min_keys, &incr_keys);
	/* On average, each delta is encoded into about min_bits + 1.5 bits. */
	return entry_count * (payload_bits + min_bits + 1) + entry_count / 2;
}

u32 uds_get_delta_index_page_count(u32 entry_count, u32 list_count, u32 mean_delta,
				   u32 payload_bits, size_t bytes_per_page)
{
	unsigned int bits_per_delta_list;
	unsigned int bits_per_page;
	size_t bits_per_index;

	/* Compute the expected number of bits needed for all the entries. */
	bits_per_index = uds_compute_delta_index_size(entry_count, mean_delta,
						      payload_bits);
	bits_per_delta_list = bits_per_index / list_count;

	/* Add in the immutable delta list headers. */
	bits_per_index += list_count * IMMUTABLE_HEADER_SIZE;
	/* Compute the number of usable bits on an immutable index page. */
	bits_per_page = ((bytes_per_page - sizeof(struct delta_page_header)) * BITS_PER_BYTE);
	/*
	 * Reduce the bits per page by one immutable delta list header and one delta list to
	 * account for internal fragmentation.
	 */
	bits_per_page -= IMMUTABLE_HEADER_SIZE + bits_per_delta_list;
	/* Now compute the number of pages needed. */
	return DIV_ROUND_UP(bits_per_index, bits_per_page);
}

void uds_log_delta_index_entry(struct delta_index_entry *delta_entry)
{
	vdo_log_ratelimit(vdo_log_info,
			  "List 0x%X Key 0x%X Offset 0x%X%s%s List_size 0x%X%s",
			  delta_entry->list_number, delta_entry->key,
			  delta_entry->offset, delta_entry->at_end ? " end" : "",
			  delta_entry->is_collision ? " collision" : "",
			  delta_entry->delta_list->size,
			  delta_entry->list_overflow ? " overflow" : "");
	delta_entry->list_overflow = false;
}
