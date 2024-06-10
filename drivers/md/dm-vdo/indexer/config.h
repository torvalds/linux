/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_CONFIG_H
#define UDS_CONFIG_H

#include "geometry.h"
#include "indexer.h"
#include "io-factory.h"

/*
 * The uds_configuration records a variety of parameters used to configure a new UDS index. Some
 * parameters are provided by the client, while others are fixed or derived from user-supplied
 * values. It is created when an index is created, and it is recorded in the index metadata.
 */

enum {
	DEFAULT_VOLUME_INDEX_MEAN_DELTA = 4096,
	DEFAULT_CACHE_CHAPTERS = 7,
	DEFAULT_SPARSE_SAMPLE_RATE = 32,
	MAX_ZONES = 16,
};

/* A set of configuration parameters for the indexer. */
struct uds_configuration {
	/* Storage device for the index */
	struct block_device *bdev;

	/* The maximum allowable size of the index */
	size_t size;

	/* The offset where the index should start */
	off_t offset;

	/* Parameters for the volume */

	/* The volume layout */
	struct index_geometry *geometry;

	/* Index owner's nonce */
	u64 nonce;

	/* The number of threads used to process index requests */
	unsigned int zone_count;

	/* The number of threads used to read volume pages */
	unsigned int read_threads;

	/* Size of the page cache and sparse chapter index cache in chapters */
	u32 cache_chapters;

	/* Parameters for the volume index */

	/* The mean delta for the volume index */
	u32 volume_index_mean_delta;

	/* Sampling rate for sparse indexing */
	u32 sparse_sample_rate;
};

/* On-disk structure of data for a version 8.02 index. */
struct uds_configuration_8_02 {
	/* Smaller (16), Small (64) or large (256) indices */
	u32 record_pages_per_chapter;
	/* Total number of chapters per volume */
	u32 chapters_per_volume;
	/* Number of sparse chapters per volume */
	u32 sparse_chapters_per_volume;
	/* Size of the page cache, in chapters */
	u32 cache_chapters;
	/* Unused field */
	u32 unused;
	/* The volume index mean delta to use */
	u32 volume_index_mean_delta;
	/* Size of a page, used for both record pages and index pages */
	u32 bytes_per_page;
	/* Sampling rate for sparse indexing */
	u32 sparse_sample_rate;
	/* Index owner's nonce */
	u64 nonce;
	/* Virtual chapter remapped from physical chapter 0 */
	u64 remapped_virtual;
	/* New physical chapter which remapped chapter was moved to */
	u64 remapped_physical;
} __packed;

/* On-disk structure of data for a version 6.02 index. */
struct uds_configuration_6_02 {
	/* Smaller (16), Small (64) or large (256) indices */
	u32 record_pages_per_chapter;
	/* Total number of chapters per volume */
	u32 chapters_per_volume;
	/* Number of sparse chapters per volume */
	u32 sparse_chapters_per_volume;
	/* Size of the page cache, in chapters */
	u32 cache_chapters;
	/* Unused field */
	u32 unused;
	/* The volume index mean delta to use */
	u32 volume_index_mean_delta;
	/* Size of a page, used for both record pages and index pages */
	u32 bytes_per_page;
	/* Sampling rate for sparse indexing */
	u32 sparse_sample_rate;
	/* Index owner's nonce */
	u64 nonce;
} __packed;

int __must_check uds_make_configuration(const struct uds_parameters *params,
					struct uds_configuration **config_ptr);

void uds_free_configuration(struct uds_configuration *config);

int __must_check uds_validate_config_contents(struct buffered_reader *reader,
					      struct uds_configuration *config);

int __must_check uds_write_config_contents(struct buffered_writer *writer,
					   struct uds_configuration *config, u32 version);

void uds_log_configuration(struct uds_configuration *config);

#endif /* UDS_CONFIG_H */
