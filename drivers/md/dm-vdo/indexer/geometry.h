/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_INDEX_GEOMETRY_H
#define UDS_INDEX_GEOMETRY_H

#include "indexer.h"

/*
 * The index_geometry records parameters that define the layout of a UDS index volume, and the size and
 * shape of various index structures. It is created when the index is created, and is referenced by
 * many index sub-components.
 */

struct index_geometry {
	/* Size of a chapter page, in bytes */
	size_t bytes_per_page;
	/* Number of record pages in a chapter */
	u32 record_pages_per_chapter;
	/* Total number of chapters in a volume */
	u32 chapters_per_volume;
	/* Number of sparsely-indexed chapters in a volume */
	u32 sparse_chapters_per_volume;
	/* Number of bits used to determine delta list numbers */
	u8 chapter_delta_list_bits;
	/* Virtual chapter remapped from physical chapter 0 */
	u64 remapped_virtual;
	/* New physical chapter where the remapped chapter can be found */
	u64 remapped_physical;

	/*
	 * The following properties are derived from the ones above, but they are computed and
	 * recorded as fields for convenience.
	 */
	/* Total number of pages in a volume, excluding the header */
	u32 pages_per_volume;
	/* Total number of bytes in a volume, including the header */
	size_t bytes_per_volume;
	/* Number of pages in a chapter */
	u32 pages_per_chapter;
	/* Number of index pages in a chapter index */
	u32 index_pages_per_chapter;
	/* Number of records that fit on a page */
	u32 records_per_page;
	/* Number of records that fit in a chapter */
	u32 records_per_chapter;
	/* Number of records that fit in a volume */
	u64 records_per_volume;
	/* Number of delta lists per chapter index */
	u32 delta_lists_per_chapter;
	/* Mean delta for chapter indexes */
	u32 chapter_mean_delta;
	/* Number of bits needed for record page numbers */
	u8 chapter_payload_bits;
	/* Number of bits used to compute addresses for chapter delta lists */
	u8 chapter_address_bits;
	/* Number of densely-indexed chapters in a volume */
	u32 dense_chapters_per_volume;
};

enum {
	/* The number of bytes in a record (name + metadata) */
	BYTES_PER_RECORD = (UDS_RECORD_NAME_SIZE + UDS_RECORD_DATA_SIZE),

	/* The default length of a page in a chapter, in bytes */
	DEFAULT_BYTES_PER_PAGE = 1024 * BYTES_PER_RECORD,

	/* The default maximum number of records per page */
	DEFAULT_RECORDS_PER_PAGE = DEFAULT_BYTES_PER_PAGE / BYTES_PER_RECORD,

	/* The default number of record pages in a chapter */
	DEFAULT_RECORD_PAGES_PER_CHAPTER = 256,

	/* The default number of record pages in a chapter for a small index */
	SMALL_RECORD_PAGES_PER_CHAPTER = 64,

	/* The default number of chapters in a volume */
	DEFAULT_CHAPTERS_PER_VOLUME = 1024,

	/* The default number of sparsely-indexed chapters in a volume */
	DEFAULT_SPARSE_CHAPTERS_PER_VOLUME = 0,

	/* The log2 of the default mean delta */
	DEFAULT_CHAPTER_MEAN_DELTA_BITS = 16,

	/* The log2 of the number of delta lists in a large chapter */
	DEFAULT_CHAPTER_DELTA_LIST_BITS = 12,

	/* The log2 of the number of delta lists in a small chapter */
	SMALL_CHAPTER_DELTA_LIST_BITS = 10,

	/* The number of header pages per volume */
	HEADER_PAGES_PER_VOLUME = 1,
};

int __must_check uds_make_index_geometry(size_t bytes_per_page, u32 record_pages_per_chapter,
					 u32 chapters_per_volume,
					 u32 sparse_chapters_per_volume, u64 remapped_virtual,
					 u64 remapped_physical,
					 struct index_geometry **geometry_ptr);

int __must_check uds_copy_index_geometry(struct index_geometry *source,
					 struct index_geometry **geometry_ptr);

void uds_free_index_geometry(struct index_geometry *geometry);

u32 __must_check uds_map_to_physical_chapter(const struct index_geometry *geometry,
					     u64 virtual_chapter);

/*
 * Check whether this geometry is reduced by a chapter. This will only be true if the volume was
 * converted from a non-lvm volume to an lvm volume.
 */
static inline bool __must_check
uds_is_reduced_index_geometry(const struct index_geometry *geometry)
{
	return !!(geometry->chapters_per_volume & 1);
}

static inline bool __must_check
uds_is_sparse_index_geometry(const struct index_geometry *geometry)
{
	return geometry->sparse_chapters_per_volume > 0;
}

bool __must_check uds_has_sparse_chapters(const struct index_geometry *geometry,
					  u64 oldest_virtual_chapter,
					  u64 newest_virtual_chapter);

bool __must_check uds_is_chapter_sparse(const struct index_geometry *geometry,
					u64 oldest_virtual_chapter,
					u64 newest_virtual_chapter,
					u64 virtual_chapter_number);

u32 __must_check uds_chapters_to_expire(const struct index_geometry *geometry,
					u64 newest_chapter);

#endif /* UDS_INDEX_GEOMETRY_H */
