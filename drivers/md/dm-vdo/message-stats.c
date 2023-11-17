// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "dedupe.h"
#include "logger.h"
#include "memory-alloc.h"
#include "message-stats.h"
#include "statistics.h"
#include "thread-device.h"
#include "vdo.h"

static int write_u64(char *prefix,
		     u64 value,
		     char *suffix,
		     char **buf,
		     unsigned int *maxlen)
{
	int count = scnprintf(*buf, *maxlen, "%s%llu%s",
			      prefix == NULL ? "" : prefix,
			      value,
			      suffix == NULL ? "" : suffix);
	*buf += count;
	*maxlen -= count;
	if (count >= *maxlen)
		return VDO_UNEXPECTED_EOF;
	return VDO_SUCCESS;
}

static int write_u32(char *prefix,
		     u32 value,
		     char *suffix,
		     char **buf,
		     unsigned int *maxlen)
{
	int count = scnprintf(*buf, *maxlen, "%s%u%s",
			      prefix == NULL ? "" : prefix,
			      value,
			      suffix == NULL ? "" : suffix);
	*buf += count;
	*maxlen -= count;
	if (count >= *maxlen)
		return VDO_UNEXPECTED_EOF;
	return VDO_SUCCESS;
}

static int write_block_count_t(char *prefix,
			       block_count_t value,
			       char *suffix,
			       char **buf,
			       unsigned int *maxlen)
{
	int count = scnprintf(*buf, *maxlen, "%s%llu%s",
			      prefix == NULL ? "" : prefix,
			      value,
			      suffix == NULL ? "" : suffix);
	*buf += count;
	*maxlen -= count;
	if (count >= *maxlen)
		return VDO_UNEXPECTED_EOF;
	return VDO_SUCCESS;
}

static int write_string(char *prefix,
			char *value,
			char *suffix,
			char **buf,
			unsigned int *maxlen)
{
	int count = scnprintf(*buf, *maxlen, "%s%s%s",
			      prefix == NULL ? "" : prefix,
			      value,
			      suffix == NULL ? "" : suffix);
	*buf += count;
	*maxlen -= count;
	if (count >= *maxlen)
		return VDO_UNEXPECTED_EOF;
	return VDO_SUCCESS;
}

static int write_bool(char *prefix,
		      bool value,
		      char *suffix,
		      char **buf,
		      unsigned int *maxlen)
{
	int count = scnprintf(*buf, *maxlen, "%s%d%s",
			      prefix == NULL ? "" : prefix,
			      value,
			      suffix == NULL ? "" : suffix);
	*buf += count;
	*maxlen -= count;
	if (count >= *maxlen)
		return VDO_UNEXPECTED_EOF;
	return VDO_SUCCESS;
}

static int write_u8(char *prefix,
		    u8 value,
		    char *suffix,
		    char **buf,
		    unsigned int *maxlen)
{
	int count = scnprintf(*buf, *maxlen, "%s%u%s",
			      prefix == NULL ? "" : prefix,
			      value,
			      suffix == NULL ? "" : suffix);
	*buf += count;
	*maxlen -= count;
	if (count >= *maxlen)
		return VDO_UNEXPECTED_EOF;
	return VDO_SUCCESS;
}

static int write_block_allocator_statistics(char *prefix,
					    struct block_allocator_statistics *stats,
					    char *suffix,
					    char **buf,
					    unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The total number of slabs from which blocks may be allocated */
	result = write_u64("slabCount : ",
			   stats->slab_count,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The total number of slabs from which blocks have ever been allocated */
	result = write_u64("slabsOpened : ",
			   stats->slabs_opened,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The number of times since loading that a slab has been re-opened */
	result = write_u64("slabsReopened : ",
			   stats->slabs_reopened,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

static int write_commit_statistics(char *prefix,
				   struct commit_statistics *stats,
				   char *suffix,
				   char **buf,
				   unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The total number of items on which processing has started */
	result = write_u64("started : ",
			   stats->started,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The total number of items for which a write operation has been issued */
	result = write_u64("written : ",
			   stats->written,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The total number of items for which a write operation has completed */
	result = write_u64("committed : ",
			   stats->committed,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

static int write_recovery_journal_statistics(char *prefix,
					     struct recovery_journal_statistics *stats,
					     char *suffix,
					     char **buf,
					     unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of times the on-disk journal was full */
	result = write_u64("diskFull : ",
			   stats->disk_full,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of times the recovery journal requested slab journal commits. */
	result = write_u64("slabJournalCommitsRequested : ",
			   stats->slab_journal_commits_requested,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Write/Commit totals for individual journal entries */
	result = write_commit_statistics("entries : ",
					 &stats->entries,
					 ", ",
					 buf,
					 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Write/Commit totals for journal blocks */
	result = write_commit_statistics("blocks : ",
					 &stats->blocks,
					 ", ",
					 buf,
					 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

static int write_packer_statistics(char *prefix,
				   struct packer_statistics *stats,
				   char *suffix,
				   char **buf,
				   unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of compressed data items written since startup */
	result = write_u64("compressedFragmentsWritten : ",
			   stats->compressed_fragments_written,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of blocks containing compressed items written since startup */
	result = write_u64("compressedBlocksWritten : ",
			   stats->compressed_blocks_written,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of VIOs that are pending in the packer */
	result = write_u64("compressedFragmentsInPacker : ",
			   stats->compressed_fragments_in_packer,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

static int write_slab_journal_statistics(char *prefix,
					 struct slab_journal_statistics *stats,
					 char *suffix,
					 char **buf,
					 unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of times the on-disk journal was full */
	result = write_u64("diskFullCount : ",
			   stats->disk_full_count,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of times an entry was added over the flush threshold */
	result = write_u64("flushCount : ",
			   stats->flush_count,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of times an entry was added over the block threshold */
	result = write_u64("blockedCount : ",
			   stats->blocked_count,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of times a tail block was written */
	result = write_u64("blocksWritten : ",
			   stats->blocks_written,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of times we had to wait for the tail to write */
	result = write_u64("tailBusyCount : ",
			   stats->tail_busy_count,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

static int write_slab_summary_statistics(char *prefix,
					 struct slab_summary_statistics *stats,
					 char *suffix,
					 char **buf,
					 unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of blocks written */
	result = write_u64("blocksWritten : ",
			   stats->blocks_written,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

static int write_ref_counts_statistics(char *prefix,
				       struct ref_counts_statistics *stats,
				       char *suffix,
				       char **buf,
				       unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of reference blocks written */
	result = write_u64("blocksWritten : ",
			   stats->blocks_written,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

static int write_block_map_statistics(char *prefix,
				      struct block_map_statistics *stats,
				      char *suffix,
				      char **buf,
				      unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of dirty (resident) pages */
	result = write_u32("dirtyPages : ",
			   stats->dirty_pages,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of clean (resident) pages */
	result = write_u32("cleanPages : ",
			   stats->clean_pages,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of free pages */
	result = write_u32("freePages : ",
			   stats->free_pages,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of pages in failed state */
	result = write_u32("failedPages : ",
			   stats->failed_pages,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of pages incoming */
	result = write_u32("incomingPages : ",
			   stats->incoming_pages,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of pages outgoing */
	result = write_u32("outgoingPages : ",
			   stats->outgoing_pages,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* how many times free page not avail */
	result = write_u32("cachePressure : ",
			   stats->cache_pressure,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of get_vdo_page() calls for read */
	result = write_u64("readCount : ",
			   stats->read_count,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of get_vdo_page() calls for write */
	result = write_u64("writeCount : ",
			   stats->write_count,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of times pages failed to read */
	result = write_u64("failedReads : ",
			   stats->failed_reads,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of times pages failed to write */
	result = write_u64("failedWrites : ",
			   stats->failed_writes,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of gets that are reclaimed */
	result = write_u64("reclaimed : ",
			   stats->reclaimed,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of gets for outgoing pages */
	result = write_u64("readOutgoing : ",
			   stats->read_outgoing,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of gets that were already there */
	result = write_u64("foundInCache : ",
			   stats->found_in_cache,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of gets requiring discard */
	result = write_u64("discardRequired : ",
			   stats->discard_required,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of gets enqueued for their page */
	result = write_u64("waitForPage : ",
			   stats->wait_for_page,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of gets that have to fetch */
	result = write_u64("fetchRequired : ",
			   stats->fetch_required,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of page fetches */
	result = write_u64("pagesLoaded : ",
			   stats->pages_loaded,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of page saves */
	result = write_u64("pagesSaved : ",
			   stats->pages_saved,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* the number of flushes issued */
	result = write_u64("flushCount : ",
			   stats->flush_count,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

static int write_hash_lock_statistics(char *prefix,
				      struct hash_lock_statistics *stats,
				      char *suffix,
				      char **buf,
				      unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of times the UDS advice proved correct */
	result = write_u64("dedupeAdviceValid : ",
			   stats->dedupe_advice_valid,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of times the UDS advice proved incorrect */
	result = write_u64("dedupeAdviceStale : ",
			   stats->dedupe_advice_stale,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of writes with the same data as another in-flight write */
	result = write_u64("concurrentDataMatches : ",
			   stats->concurrent_data_matches,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of writes whose hash collided with an in-flight write */
	result = write_u64("concurrentHashCollisions : ",
			   stats->concurrent_hash_collisions,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Current number of dedupe queries that are in flight */
	result = write_u32("currDedupeQueries : ",
			   stats->curr_dedupe_queries,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

static int write_error_statistics(char *prefix,
				  struct error_statistics *stats,
				  char *suffix,
				  char **buf,
				  unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of times VDO got an invalid dedupe advice PBN from UDS */
	result = write_u64("invalidAdvicePBNCount : ",
			   stats->invalid_advice_pbn_count,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of times a VIO completed with a VDO_NO_SPACE error */
	result = write_u64("noSpaceErrorCount : ",
			   stats->no_space_error_count,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of times a VIO completed with a VDO_READ_ONLY error */
	result = write_u64("readOnlyErrorCount : ",
			   stats->read_only_error_count,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

static int write_bio_stats(char *prefix,
			   struct bio_stats *stats,
			   char *suffix,
			   char **buf,
			   unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of REQ_OP_READ bios */
	result = write_u64("read : ",
			   stats->read,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of REQ_OP_WRITE bios with data */
	result = write_u64("write : ",
			   stats->write,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of bios tagged with REQ_PREFLUSH and containing no data */
	result = write_u64("emptyFlush : ",
			   stats->empty_flush,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of REQ_OP_DISCARD bios */
	result = write_u64("discard : ",
			   stats->discard,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of bios tagged with REQ_PREFLUSH */
	result = write_u64("flush : ",
			   stats->flush,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of bios tagged with REQ_FUA */
	result = write_u64("fua : ",
			   stats->fua,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

static int write_memory_usage(char *prefix,
			      struct memory_usage *stats,
			      char *suffix,
			      char **buf,
			      unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Tracked bytes currently allocated. */
	result = write_u64("bytesUsed : ",
			   stats->bytes_used,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Maximum tracked bytes allocated. */
	result = write_u64("peakBytesUsed : ",
			   stats->peak_bytes_used,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

static int write_index_statistics(char *prefix,
				  struct index_statistics *stats,
				  char *suffix,
				  char **buf,
				  unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of records stored in the index */
	result = write_u64("entriesIndexed : ",
			   stats->entries_indexed,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of post calls that found an existing entry */
	result = write_u64("postsFound : ",
			   stats->posts_found,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of post calls that added a new entry */
	result = write_u64("postsNotFound : ",
			   stats->posts_not_found,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of query calls that found an existing entry */
	result = write_u64("queriesFound : ",
			   stats->queries_found,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of query calls that added a new entry */
	result = write_u64("queriesNotFound : ",
			   stats->queries_not_found,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of update calls that found an existing entry */
	result = write_u64("updatesFound : ",
			   stats->updates_found,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of update calls that added a new entry */
	result = write_u64("updatesNotFound : ",
			   stats->updates_not_found,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of entries discarded */
	result = write_u64("entriesDiscarded : ",
			   stats->entries_discarded,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

static int write_vdo_statistics(char *prefix,
				struct vdo_statistics *stats,
				char *suffix,
				char **buf,
				unsigned int *maxlen)
{
	int result;

	result = write_string(prefix, "{ ", NULL, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_u32("version : ",
			   stats->version,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of blocks used for data */
	result = write_u64("dataBlocksUsed : ",
			   stats->data_blocks_used,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of blocks used for VDO metadata */
	result = write_u64("overheadBlocksUsed : ",
			   stats->overhead_blocks_used,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of logical blocks that are currently mapped to physical blocks */
	result = write_u64("logicalBlocksUsed : ",
			   stats->logical_blocks_used,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of physical blocks */
	result = write_block_count_t("physicalBlocks : ",
				     stats->physical_blocks,
				     ", ",
				     buf,
				     maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* number of logical blocks */
	result = write_block_count_t("logicalBlocks : ",
				     stats->logical_blocks,
				     ", ",
				     buf,
				     maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Size of the block map page cache, in bytes */
	result = write_u64("blockMapCacheSize : ",
			   stats->block_map_cache_size,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The physical block size */
	result = write_u64("blockSize : ",
			   stats->block_size,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of times the VDO has successfully recovered */
	result = write_u64("completeRecoveries : ",
			   stats->complete_recoveries,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of times the VDO has recovered from read-only mode */
	result = write_u64("readOnlyRecoveries : ",
			   stats->read_only_recoveries,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* String describing the operating mode of the VDO */
	result = write_string("mode : ",
			      stats->mode,
			      ", ",
			      buf,
			      maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Whether the VDO is in recovery mode */
	result = write_bool("inRecoveryMode : ",
			    stats->in_recovery_mode,
			    ", ",
			    buf,
			    maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* What percentage of recovery mode work has been completed */
	result = write_u8("recoveryPercentage : ",
			  stats->recovery_percentage,
			  ", ",
			  buf,
			  maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The statistics for the compressed block packer */
	result = write_packer_statistics("packer : ",
					 &stats->packer,
					 ", ",
					 buf,
					 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Counters for events in the block allocator */
	result = write_block_allocator_statistics("allocator : ",
						  &stats->allocator,
						  ", ",
						  buf,
						  maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Counters for events in the recovery journal */
	result = write_recovery_journal_statistics("journal : ",
						   &stats->journal,
						   ", ",
						   buf,
						   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The statistics for the slab journals */
	result = write_slab_journal_statistics("slabJournal : ",
					       &stats->slab_journal,
					       ", ",
					       buf,
					       maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The statistics for the slab summary */
	result = write_slab_summary_statistics("slabSummary : ",
					       &stats->slab_summary,
					       ", ",
					       buf,
					       maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The statistics for the reference counts */
	result = write_ref_counts_statistics("refCounts : ",
					     &stats->ref_counts,
					     ", ",
					     buf,
					     maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The statistics for the block map */
	result = write_block_map_statistics("blockMap : ",
					    &stats->block_map,
					    ", ",
					    buf,
					    maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The dedupe statistics from hash locks */
	result = write_hash_lock_statistics("hashLock : ",
					    &stats->hash_lock,
					    ", ",
					    buf,
					    maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Counts of error conditions */
	result = write_error_statistics("errors : ",
					&stats->errors,
					", ",
					buf,
					maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The VDO instance */
	result = write_u32("instance : ",
			   stats->instance,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Current number of active VIOs */
	result = write_u32("currentVIOsInProgress : ",
			   stats->current_vios_in_progress,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Maximum number of active VIOs */
	result = write_u32("maxVIOs : ",
			   stats->max_vios,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of times the UDS index was too slow in responding */
	result = write_u64("dedupeAdviceTimeouts : ",
			   stats->dedupe_advice_timeouts,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Number of flush requests submitted to the storage device */
	result = write_u64("flushOut : ",
			   stats->flush_out,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Logical block size */
	result = write_u64("logicalBlockSize : ",
			   stats->logical_block_size,
			   ", ",
			   buf,
			   maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Bios submitted into VDO from above */
	result = write_bio_stats("biosIn : ",
				 &stats->bios_in,
				 ", ",
				 buf,
				 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_bio_stats("biosInPartial : ",
				 &stats->bios_in_partial,
				 ", ",
				 buf,
				 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Bios submitted onward for user data */
	result = write_bio_stats("biosOut : ",
				 &stats->bios_out,
				 ", ",
				 buf,
				 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Bios submitted onward for metadata */
	result = write_bio_stats("biosMeta : ",
				 &stats->bios_meta,
				 ", ",
				 buf,
				 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_bio_stats("biosJournal : ",
				 &stats->bios_journal,
				 ", ",
				 buf,
				 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_bio_stats("biosPageCache : ",
				 &stats->bios_page_cache,
				 ", ",
				 buf,
				 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_bio_stats("biosOutCompleted : ",
				 &stats->bios_out_completed,
				 ", ",
				 buf,
				 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_bio_stats("biosMetaCompleted : ",
				 &stats->bios_meta_completed,
				 ", ",
				 buf,
				 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_bio_stats("biosJournalCompleted : ",
				 &stats->bios_journal_completed,
				 ", ",
				 buf,
				 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_bio_stats("biosPageCacheCompleted : ",
				 &stats->bios_page_cache_completed,
				 ", ",
				 buf,
				 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_bio_stats("biosAcknowledged : ",
				 &stats->bios_acknowledged,
				 ", ",
				 buf,
				 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_bio_stats("biosAcknowledgedPartial : ",
				 &stats->bios_acknowledged_partial,
				 ", ",
				 buf,
				 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Current number of bios in progress */
	result = write_bio_stats("biosInProgress : ",
				 &stats->bios_in_progress,
				 ", ",
				 buf,
				 maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* Memory usage stats. */
	result = write_memory_usage("memoryUsage : ",
				    &stats->memory_usage,
				    ", ",
				    buf,
				    maxlen);
	if (result != VDO_SUCCESS)
		return result;
	/* The statistics for the UDS index */
	result = write_index_statistics("index : ",
					&stats->index,
					", ",
					buf,
					maxlen);
	if (result != VDO_SUCCESS)
		return result;
	result = write_string(NULL, "}", suffix, buf, maxlen);
	if (result != VDO_SUCCESS)
		return result;
	return VDO_SUCCESS;
}

int vdo_write_stats(struct vdo *vdo,
		    char *buf,
		    unsigned int maxlen)
{
	struct vdo_statistics *stats;
	int result;

	result = uds_allocate(1, struct vdo_statistics, __func__, &stats);
	if (result != VDO_SUCCESS)
		return result;

	vdo_fetch_statistics(vdo, stats);
	result = write_vdo_statistics(NULL, stats, NULL, &buf, &maxlen);
	uds_free(stats);
	return result;
}
