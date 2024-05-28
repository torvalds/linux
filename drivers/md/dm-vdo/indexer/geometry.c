// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "geometry.h"

#include <linux/compiler.h>
#include <linux/log2.h>

#include "errors.h"
#include "logger.h"
#include "memory-alloc.h"
#include "permassert.h"

#include "delta-index.h"
#include "indexer.h"

/*
 * An index volume is divided into a fixed number of fixed-size chapters, each consisting of a
 * fixed number of fixed-size pages. The volume layout is defined by two constants and four
 * parameters. The constants are that index records are 32 bytes long (16-byte block name plus
 * 16-byte metadata) and that open chapter index hash slots are one byte long. The four parameters
 * are the number of bytes in a page, the number of record pages in a chapter, the number of
 * chapters in a volume, and the number of chapters that are sparse. From these parameters, we can
 * derive the rest of the layout and other index properties.
 *
 * The index volume is sized by its maximum memory footprint. For a dense index, the persistent
 * storage is about 10 times the size of the memory footprint. For a sparse index, the persistent
 * storage is about 100 times the size of the memory footprint.
 *
 * For a small index with a memory footprint less than 1GB, there are three possible memory
 * configurations: 0.25GB, 0.5GB and 0.75GB. The default geometry for each is 1024 index records
 * per 32 KB page, 1024 chapters per volume, and either 64, 128, or 192 record pages per chapter
 * (resulting in 6, 13, or 20 index pages per chapter) depending on the memory configuration. For
 * the VDO default of a 0.25 GB index, this yields a deduplication window of 256 GB using about 2.5
 * GB for the persistent storage and 256 MB of RAM.
 *
 * For a larger index with a memory footprint that is a multiple of 1 GB, the geometry is 1024
 * index records per 32 KB page, 256 record pages per chapter, 26 index pages per chapter, and 1024
 * chapters for every GB of memory footprint. For a 1 GB volume, this yields a deduplication window
 * of 1 TB using about 9GB of persistent storage and 1 GB of RAM.
 *
 * The above numbers hold for volumes which have no sparse chapters. A sparse volume has 10 times
 * as many chapters as the corresponding non-sparse volume, which provides 10 times the
 * deduplication window while using 10 times as much persistent storage as the equivalent
 * non-sparse volume with the same memory footprint.
 *
 * If the volume has been converted from a non-lvm format to an lvm volume, the number of chapters
 * per volume will have been reduced by one by eliminating physical chapter 0, and the virtual
 * chapter that formerly mapped to physical chapter 0 may be remapped to another physical chapter.
 * This remapping is expressed by storing which virtual chapter was remapped, and which physical
 * chapter it was moved to.
 */

int uds_make_index_geometry(size_t bytes_per_page, u32 record_pages_per_chapter,
			    u32 chapters_per_volume, u32 sparse_chapters_per_volume,
			    u64 remapped_virtual, u64 remapped_physical,
			    struct index_geometry **geometry_ptr)
{
	int result;
	struct index_geometry *geometry;

	result = vdo_allocate(1, struct index_geometry, "geometry", &geometry);
	if (result != VDO_SUCCESS)
		return result;

	geometry->bytes_per_page = bytes_per_page;
	geometry->record_pages_per_chapter = record_pages_per_chapter;
	geometry->chapters_per_volume = chapters_per_volume;
	geometry->sparse_chapters_per_volume = sparse_chapters_per_volume;
	geometry->dense_chapters_per_volume = chapters_per_volume - sparse_chapters_per_volume;
	geometry->remapped_virtual = remapped_virtual;
	geometry->remapped_physical = remapped_physical;

	geometry->records_per_page = bytes_per_page / BYTES_PER_RECORD;
	geometry->records_per_chapter = geometry->records_per_page * record_pages_per_chapter;
	geometry->records_per_volume = (u64) geometry->records_per_chapter * chapters_per_volume;

	geometry->chapter_mean_delta = 1 << DEFAULT_CHAPTER_MEAN_DELTA_BITS;
	geometry->chapter_payload_bits = bits_per(record_pages_per_chapter - 1);
	/*
	 * We want 1 delta list for every 64 records in the chapter.
	 * The "| 077" ensures that the chapter_delta_list_bits computation
	 * does not underflow.
	 */
	geometry->chapter_delta_list_bits =
		bits_per((geometry->records_per_chapter - 1) | 077) - 6;
	geometry->delta_lists_per_chapter = 1 << geometry->chapter_delta_list_bits;
	/* We need enough address bits to achieve the desired mean delta. */
	geometry->chapter_address_bits =
		(DEFAULT_CHAPTER_MEAN_DELTA_BITS -
		 geometry->chapter_delta_list_bits +
		 bits_per(geometry->records_per_chapter - 1));
	geometry->index_pages_per_chapter =
		uds_get_delta_index_page_count(geometry->records_per_chapter,
					       geometry->delta_lists_per_chapter,
					       geometry->chapter_mean_delta,
					       geometry->chapter_payload_bits,
					       bytes_per_page);

	geometry->pages_per_chapter = geometry->index_pages_per_chapter + record_pages_per_chapter;
	geometry->pages_per_volume = geometry->pages_per_chapter * chapters_per_volume;
	geometry->bytes_per_volume =
		bytes_per_page * (geometry->pages_per_volume + HEADER_PAGES_PER_VOLUME);

	*geometry_ptr = geometry;
	return UDS_SUCCESS;
}

int uds_copy_index_geometry(struct index_geometry *source,
			    struct index_geometry **geometry_ptr)
{
	return uds_make_index_geometry(source->bytes_per_page,
				       source->record_pages_per_chapter,
				       source->chapters_per_volume,
				       source->sparse_chapters_per_volume,
				       source->remapped_virtual, source->remapped_physical,
				       geometry_ptr);
}

void uds_free_index_geometry(struct index_geometry *geometry)
{
	vdo_free(geometry);
}

u32 __must_check uds_map_to_physical_chapter(const struct index_geometry *geometry,
					     u64 virtual_chapter)
{
	u64 delta;

	if (!uds_is_reduced_index_geometry(geometry))
		return virtual_chapter % geometry->chapters_per_volume;

	if (likely(virtual_chapter > geometry->remapped_virtual)) {
		delta = virtual_chapter - geometry->remapped_virtual;
		if (likely(delta > geometry->remapped_physical))
			return delta % geometry->chapters_per_volume;
		else
			return delta - 1;
	}

	if (virtual_chapter == geometry->remapped_virtual)
		return geometry->remapped_physical;

	delta = geometry->remapped_virtual - virtual_chapter;
	if (delta < geometry->chapters_per_volume)
		return geometry->chapters_per_volume - delta;

	/* This chapter is so old the answer doesn't matter. */
	return 0;
}

/* Check whether any sparse chapters are in use. */
bool uds_has_sparse_chapters(const struct index_geometry *geometry,
			     u64 oldest_virtual_chapter, u64 newest_virtual_chapter)
{
	return uds_is_sparse_index_geometry(geometry) &&
		((newest_virtual_chapter - oldest_virtual_chapter + 1) >
		 geometry->dense_chapters_per_volume);
}

bool uds_is_chapter_sparse(const struct index_geometry *geometry,
			   u64 oldest_virtual_chapter, u64 newest_virtual_chapter,
			   u64 virtual_chapter_number)
{
	return uds_has_sparse_chapters(geometry, oldest_virtual_chapter,
				       newest_virtual_chapter) &&
		((virtual_chapter_number + geometry->dense_chapters_per_volume) <=
		 newest_virtual_chapter);
}

/* Calculate how many chapters to expire after opening the newest chapter. */
u32 uds_chapters_to_expire(const struct index_geometry *geometry, u64 newest_chapter)
{
	/* If the index isn't full yet, don't expire anything. */
	if (newest_chapter < geometry->chapters_per_volume)
		return 0;

	/* If a chapter is out of order... */
	if (geometry->remapped_physical > 0) {
		u64 oldest_chapter = newest_chapter - geometry->chapters_per_volume;

		/*
		 * ... expire an extra chapter when expiring the moved chapter to free physical
		 * space for the new chapter ...
		 */
		if (oldest_chapter == geometry->remapped_virtual)
			return 2;

		/*
		 * ... but don't expire anything when the new chapter will use the physical chapter
		 * freed by expiring the moved chapter.
		 */
		if (oldest_chapter == (geometry->remapped_virtual + geometry->remapped_physical))
			return 0;
	}

	/* Normally, just expire one. */
	return 1;
}
