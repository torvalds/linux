/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_HASH_UTILS_H
#define UDS_HASH_UTILS_H

#include "numeric.h"

#include "geometry.h"
#include "indexer.h"

/* Utilities for extracting portions of a request name for various uses. */

/* How various portions of a record name are apportioned. */
enum {
	VOLUME_INDEX_BYTES_OFFSET = 0,
	VOLUME_INDEX_BYTES_COUNT = 8,
	CHAPTER_INDEX_BYTES_OFFSET = 8,
	CHAPTER_INDEX_BYTES_COUNT = 6,
	SAMPLE_BYTES_OFFSET = 14,
	SAMPLE_BYTES_COUNT = 2,
};

static inline u64 uds_extract_chapter_index_bytes(const struct uds_record_name *name)
{
	const u8 *chapter_bits = &name->name[CHAPTER_INDEX_BYTES_OFFSET];
	u64 bytes = (u64) get_unaligned_be16(chapter_bits) << 32;

	bytes |= get_unaligned_be32(chapter_bits + 2);
	return bytes;
}

static inline u64 uds_extract_volume_index_bytes(const struct uds_record_name *name)
{
	return get_unaligned_be64(&name->name[VOLUME_INDEX_BYTES_OFFSET]);
}

static inline u32 uds_extract_sampling_bytes(const struct uds_record_name *name)
{
	return get_unaligned_be16(&name->name[SAMPLE_BYTES_OFFSET]);
}

/* Compute the chapter delta list for a given name. */
static inline u32 uds_hash_to_chapter_delta_list(const struct uds_record_name *name,
						 const struct index_geometry *geometry)
{
	return ((uds_extract_chapter_index_bytes(name) >> geometry->chapter_address_bits) &
		((1 << geometry->chapter_delta_list_bits) - 1));
}

/* Compute the chapter delta address for a given name. */
static inline u32 uds_hash_to_chapter_delta_address(const struct uds_record_name *name,
						    const struct index_geometry *geometry)
{
	return uds_extract_chapter_index_bytes(name) & ((1 << geometry->chapter_address_bits) - 1);
}

static inline unsigned int uds_name_to_hash_slot(const struct uds_record_name *name,
						 unsigned int slot_count)
{
	return (unsigned int) (uds_extract_chapter_index_bytes(name) % slot_count);
}

#endif /* UDS_HASH_UTILS_H */
