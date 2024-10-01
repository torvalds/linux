/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef STATISTICS_H
#define STATISTICS_H

#include "types.h"

enum {
	STATISTICS_VERSION = 36,
};

struct block_allocator_statistics {
	/* The total number of slabs from which blocks may be allocated */
	u64 slab_count;
	/* The total number of slabs from which blocks have ever been allocated */
	u64 slabs_opened;
	/* The number of times since loading that a slab has been re-opened */
	u64 slabs_reopened;
};

/**
 * Counters for tracking the number of items written (blocks, requests, etc.)
 * that keep track of totals at steps in the write pipeline. Three counters
 * allow the number of buffered, in-memory items and the number of in-flight,
 * unacknowledged writes to be derived, while still tracking totals for
 * reporting purposes
 */
struct commit_statistics {
	/* The total number of items on which processing has started */
	u64 started;
	/* The total number of items for which a write operation has been issued */
	u64 written;
	/* The total number of items for which a write operation has completed */
	u64 committed;
};

/** Counters for events in the recovery journal */
struct recovery_journal_statistics {
	/* Number of times the on-disk journal was full */
	u64 disk_full;
	/* Number of times the recovery journal requested slab journal commits. */
	u64 slab_journal_commits_requested;
	/* Write/Commit totals for individual journal entries */
	struct commit_statistics entries;
	/* Write/Commit totals for journal blocks */
	struct commit_statistics blocks;
};

/** The statistics for the compressed block packer. */
struct packer_statistics {
	/* Number of compressed data items written since startup */
	u64 compressed_fragments_written;
	/* Number of blocks containing compressed items written since startup */
	u64 compressed_blocks_written;
	/* Number of VIOs that are pending in the packer */
	u64 compressed_fragments_in_packer;
};

/** The statistics for the slab journals. */
struct slab_journal_statistics {
	/* Number of times the on-disk journal was full */
	u64 disk_full_count;
	/* Number of times an entry was added over the flush threshold */
	u64 flush_count;
	/* Number of times an entry was added over the block threshold */
	u64 blocked_count;
	/* Number of times a tail block was written */
	u64 blocks_written;
	/* Number of times we had to wait for the tail to write */
	u64 tail_busy_count;
};

/** The statistics for the slab summary. */
struct slab_summary_statistics {
	/* Number of blocks written */
	u64 blocks_written;
};

/** The statistics for the reference counts. */
struct ref_counts_statistics {
	/* Number of reference blocks written */
	u64 blocks_written;
};

/** The statistics for the block map. */
struct block_map_statistics {
	/* number of dirty (resident) pages */
	u32 dirty_pages;
	/* number of clean (resident) pages */
	u32 clean_pages;
	/* number of free pages */
	u32 free_pages;
	/* number of pages in failed state */
	u32 failed_pages;
	/* number of pages incoming */
	u32 incoming_pages;
	/* number of pages outgoing */
	u32 outgoing_pages;
	/* how many times free page not avail */
	u32 cache_pressure;
	/* number of get_vdo_page() calls for read */
	u64 read_count;
	/* number of get_vdo_page() calls for write */
	u64 write_count;
	/* number of times pages failed to read */
	u64 failed_reads;
	/* number of times pages failed to write */
	u64 failed_writes;
	/* number of gets that are reclaimed */
	u64 reclaimed;
	/* number of gets for outgoing pages */
	u64 read_outgoing;
	/* number of gets that were already there */
	u64 found_in_cache;
	/* number of gets requiring discard */
	u64 discard_required;
	/* number of gets enqueued for their page */
	u64 wait_for_page;
	/* number of gets that have to fetch */
	u64 fetch_required;
	/* number of page fetches */
	u64 pages_loaded;
	/* number of page saves */
	u64 pages_saved;
	/* the number of flushes issued */
	u64 flush_count;
};

/** The dedupe statistics from hash locks */
struct hash_lock_statistics {
	/* Number of times the UDS advice proved correct */
	u64 dedupe_advice_valid;
	/* Number of times the UDS advice proved incorrect */
	u64 dedupe_advice_stale;
	/* Number of writes with the same data as another in-flight write */
	u64 concurrent_data_matches;
	/* Number of writes whose hash collided with an in-flight write */
	u64 concurrent_hash_collisions;
	/* Current number of dedupe queries that are in flight */
	u32 curr_dedupe_queries;
};

/** Counts of error conditions in VDO. */
struct error_statistics {
	/* number of times VDO got an invalid dedupe advice PBN from UDS */
	u64 invalid_advice_pbn_count;
	/* number of times a VIO completed with a VDO_NO_SPACE error */
	u64 no_space_error_count;
	/* number of times a VIO completed with a VDO_READ_ONLY error */
	u64 read_only_error_count;
};

struct bio_stats {
	/* Number of REQ_OP_READ bios */
	u64 read;
	/* Number of REQ_OP_WRITE bios with data */
	u64 write;
	/* Number of bios tagged with REQ_PREFLUSH and containing no data */
	u64 empty_flush;
	/* Number of REQ_OP_DISCARD bios */
	u64 discard;
	/* Number of bios tagged with REQ_PREFLUSH */
	u64 flush;
	/* Number of bios tagged with REQ_FUA */
	u64 fua;
};

struct memory_usage {
	/* Tracked bytes currently allocated. */
	u64 bytes_used;
	/* Maximum tracked bytes allocated. */
	u64 peak_bytes_used;
};

/** UDS index statistics */
struct index_statistics {
	/* Number of records stored in the index */
	u64 entries_indexed;
	/* Number of post calls that found an existing entry */
	u64 posts_found;
	/* Number of post calls that added a new entry */
	u64 posts_not_found;
	/* Number of query calls that found an existing entry */
	u64 queries_found;
	/* Number of query calls that added a new entry */
	u64 queries_not_found;
	/* Number of update calls that found an existing entry */
	u64 updates_found;
	/* Number of update calls that added a new entry */
	u64 updates_not_found;
	/* Number of entries discarded */
	u64 entries_discarded;
};

/** The statistics of the vdo service. */
struct vdo_statistics {
	u32 version;
	/* Number of blocks used for data */
	u64 data_blocks_used;
	/* Number of blocks used for VDO metadata */
	u64 overhead_blocks_used;
	/* Number of logical blocks that are currently mapped to physical blocks */
	u64 logical_blocks_used;
	/* number of physical blocks */
	block_count_t physical_blocks;
	/* number of logical blocks */
	block_count_t logical_blocks;
	/* Size of the block map page cache, in bytes */
	u64 block_map_cache_size;
	/* The physical block size */
	u64 block_size;
	/* Number of times the VDO has successfully recovered */
	u64 complete_recoveries;
	/* Number of times the VDO has recovered from read-only mode */
	u64 read_only_recoveries;
	/* String describing the operating mode of the VDO */
	char mode[15];
	/* Whether the VDO is in recovery mode */
	bool in_recovery_mode;
	/* What percentage of recovery mode work has been completed */
	u8 recovery_percentage;
	/* The statistics for the compressed block packer */
	struct packer_statistics packer;
	/* Counters for events in the block allocator */
	struct block_allocator_statistics allocator;
	/* Counters for events in the recovery journal */
	struct recovery_journal_statistics journal;
	/* The statistics for the slab journals */
	struct slab_journal_statistics slab_journal;
	/* The statistics for the slab summary */
	struct slab_summary_statistics slab_summary;
	/* The statistics for the reference counts */
	struct ref_counts_statistics ref_counts;
	/* The statistics for the block map */
	struct block_map_statistics block_map;
	/* The dedupe statistics from hash locks */
	struct hash_lock_statistics hash_lock;
	/* Counts of error conditions */
	struct error_statistics errors;
	/* The VDO instance */
	u32 instance;
	/* Current number of active VIOs */
	u32 current_vios_in_progress;
	/* Maximum number of active VIOs */
	u32 max_vios;
	/* Number of times the UDS index was too slow in responding */
	u64 dedupe_advice_timeouts;
	/* Number of flush requests submitted to the storage device */
	u64 flush_out;
	/* Logical block size */
	u64 logical_block_size;
	/* Bios submitted into VDO from above */
	struct bio_stats bios_in;
	struct bio_stats bios_in_partial;
	/* Bios submitted onward for user data */
	struct bio_stats bios_out;
	/* Bios submitted onward for metadata */
	struct bio_stats bios_meta;
	struct bio_stats bios_journal;
	struct bio_stats bios_page_cache;
	struct bio_stats bios_out_completed;
	struct bio_stats bios_meta_completed;
	struct bio_stats bios_journal_completed;
	struct bio_stats bios_page_cache_completed;
	struct bio_stats bios_acknowledged;
	struct bio_stats bios_acknowledged_partial;
	/* Current number of bios in progress */
	struct bio_stats bios_in_progress;
	/* Memory usage stats. */
	struct memory_usage memory_usage;
	/* The statistics for the UDS index */
	struct index_statistics index;
};

#endif /* not STATISTICS_H */
