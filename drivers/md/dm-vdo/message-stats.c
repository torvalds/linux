// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "dedupe.h"
#include "indexer.h"
#include "logger.h"
#include "memory-alloc.h"
#include "message-stats.h"
#include "statistics.h"
#include "thread-device.h"
#include "vdo.h"

static void write_u64(char *prefix, u64 value, char *suffix, char **buf,
		      unsigned int *maxlen)
{
	int count;

	count = scnprintf(*buf, *maxlen, "%s%llu%s", prefix == NULL ? "" : prefix,
			  value, suffix == NULL ? "" : suffix);
	*buf += count;
	*maxlen -= count;
}

static void write_u32(char *prefix, u32 value, char *suffix, char **buf,
		      unsigned int *maxlen)
{
	int count;

	count = scnprintf(*buf, *maxlen, "%s%u%s", prefix == NULL ? "" : prefix,
			  value, suffix == NULL ? "" : suffix);
	*buf += count;
	*maxlen -= count;
}

static void write_block_count_t(char *prefix, block_count_t value, char *suffix,
				char **buf, unsigned int *maxlen)
{
	int count;

	count = scnprintf(*buf, *maxlen, "%s%llu%s", prefix == NULL ? "" : prefix,
			  value, suffix == NULL ? "" : suffix);
	*buf += count;
	*maxlen -= count;
}

static void write_string(char *prefix, char *value, char *suffix, char **buf,
			 unsigned int *maxlen)
{
	int count;

	count = scnprintf(*buf, *maxlen, "%s%s%s", prefix == NULL ? "" : prefix,
			  value, suffix == NULL ? "" : suffix);
	*buf += count;
	*maxlen -= count;
}

static void write_bool(char *prefix, bool value, char *suffix, char **buf,
		       unsigned int *maxlen)
{
	int count;

	count = scnprintf(*buf, *maxlen, "%s%d%s", prefix == NULL ? "" : prefix,
			  value, suffix == NULL ? "" : suffix);
	*buf += count;
	*maxlen -= count;
}

static void write_u8(char *prefix, u8 value, char *suffix, char **buf,
		     unsigned int *maxlen)
{
	int count;

	count = scnprintf(*buf, *maxlen, "%s%u%s", prefix == NULL ? "" : prefix,
			  value, suffix == NULL ? "" : suffix);
	*buf += count;
	*maxlen -= count;
}

static void write_block_allocator_statistics(char *prefix,
					     struct block_allocator_statistics *stats,
					     char *suffix, char **buf,
					     unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	/* The total number of slabs from which blocks may be allocated */
	write_u64("slabCount : ", stats->slab_count, ", ", buf, maxlen);
	/* The total number of slabs from which blocks have ever been allocated */
	write_u64("slabsOpened : ", stats->slabs_opened, ", ", buf, maxlen);
	/* The number of times since loading that a slab has been re-opened */
	write_u64("slabsReopened : ", stats->slabs_reopened, ", ", buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

static void write_commit_statistics(char *prefix, struct commit_statistics *stats,
				    char *suffix, char **buf, unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	/* The total number of items on which processing has started */
	write_u64("started : ", stats->started, ", ", buf, maxlen);
	/* The total number of items for which a write operation has been issued */
	write_u64("written : ", stats->written, ", ", buf, maxlen);
	/* The total number of items for which a write operation has completed */
	write_u64("committed : ", stats->committed, ", ", buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

static void write_recovery_journal_statistics(char *prefix,
					      struct recovery_journal_statistics *stats,
					      char *suffix, char **buf,
					      unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	/* Number of times the on-disk journal was full */
	write_u64("diskFull : ", stats->disk_full, ", ", buf, maxlen);
	/* Number of times the recovery journal requested slab journal commits. */
	write_u64("slabJournalCommitsRequested : ",
		  stats->slab_journal_commits_requested, ", ", buf, maxlen);
	/* Write/Commit totals for individual journal entries */
	write_commit_statistics("entries : ", &stats->entries, ", ", buf, maxlen);
	/* Write/Commit totals for journal blocks */
	write_commit_statistics("blocks : ", &stats->blocks, ", ", buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

static void write_packer_statistics(char *prefix, struct packer_statistics *stats,
				    char *suffix, char **buf, unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	/* Number of compressed data items written since startup */
	write_u64("compressedFragmentsWritten : ",
		  stats->compressed_fragments_written, ", ", buf, maxlen);
	/* Number of blocks containing compressed items written since startup */
	write_u64("compressedBlocksWritten : ",
		  stats->compressed_blocks_written, ", ", buf, maxlen);
	/* Number of VIOs that are pending in the packer */
	write_u64("compressedFragmentsInPacker : ",
		  stats->compressed_fragments_in_packer, ", ", buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

static void write_slab_journal_statistics(char *prefix,
					  struct slab_journal_statistics *stats,
					  char *suffix, char **buf, unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	/* Number of times the on-disk journal was full */
	write_u64("diskFullCount : ", stats->disk_full_count, ", ", buf, maxlen);
	/* Number of times an entry was added over the flush threshold */
	write_u64("flushCount : ", stats->flush_count, ", ", buf, maxlen);
	/* Number of times an entry was added over the block threshold */
	write_u64("blockedCount : ", stats->blocked_count, ", ", buf, maxlen);
	/* Number of times a tail block was written */
	write_u64("blocksWritten : ", stats->blocks_written, ", ", buf, maxlen);
	/* Number of times we had to wait for the tail to write */
	write_u64("tailBusyCount : ", stats->tail_busy_count, ", ", buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

static void write_slab_summary_statistics(char *prefix,
					  struct slab_summary_statistics *stats,
					  char *suffix, char **buf, unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	/* Number of blocks written */
	write_u64("blocksWritten : ", stats->blocks_written, ", ", buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

static void write_ref_counts_statistics(char *prefix, struct ref_counts_statistics *stats,
					char *suffix, char **buf, unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	/* Number of reference blocks written */
	write_u64("blocksWritten : ", stats->blocks_written, ", ", buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

static void write_block_map_statistics(char *prefix, struct block_map_statistics *stats,
				       char *suffix, char **buf, unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	/* number of dirty (resident) pages */
	write_u32("dirtyPages : ", stats->dirty_pages, ", ", buf, maxlen);
	/* number of clean (resident) pages */
	write_u32("cleanPages : ", stats->clean_pages, ", ", buf, maxlen);
	/* number of free pages */
	write_u32("freePages : ", stats->free_pages, ", ", buf, maxlen);
	/* number of pages in failed state */
	write_u32("failedPages : ", stats->failed_pages, ", ", buf, maxlen);
	/* number of pages incoming */
	write_u32("incomingPages : ", stats->incoming_pages, ", ", buf, maxlen);
	/* number of pages outgoing */
	write_u32("outgoingPages : ", stats->outgoing_pages, ", ", buf, maxlen);
	/* how many times free page not avail */
	write_u32("cachePressure : ", stats->cache_pressure, ", ", buf, maxlen);
	/* number of get_vdo_page() calls for read */
	write_u64("readCount : ", stats->read_count, ", ", buf, maxlen);
	/* number of get_vdo_page() calls for write */
	write_u64("writeCount : ", stats->write_count, ", ", buf, maxlen);
	/* number of times pages failed to read */
	write_u64("failedReads : ", stats->failed_reads, ", ", buf, maxlen);
	/* number of times pages failed to write */
	write_u64("failedWrites : ", stats->failed_writes, ", ", buf, maxlen);
	/* number of gets that are reclaimed */
	write_u64("reclaimed : ", stats->reclaimed, ", ", buf, maxlen);
	/* number of gets for outgoing pages */
	write_u64("readOutgoing : ", stats->read_outgoing, ", ", buf, maxlen);
	/* number of gets that were already there */
	write_u64("foundInCache : ", stats->found_in_cache, ", ", buf, maxlen);
	/* number of gets requiring discard */
	write_u64("discardRequired : ", stats->discard_required, ", ", buf, maxlen);
	/* number of gets enqueued for their page */
	write_u64("waitForPage : ", stats->wait_for_page, ", ", buf, maxlen);
	/* number of gets that have to fetch */
	write_u64("fetchRequired : ", stats->fetch_required, ", ", buf, maxlen);
	/* number of page fetches */
	write_u64("pagesLoaded : ", stats->pages_loaded, ", ", buf, maxlen);
	/* number of page saves */
	write_u64("pagesSaved : ", stats->pages_saved, ", ", buf, maxlen);
	/* the number of flushes issued */
	write_u64("flushCount : ", stats->flush_count, ", ", buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

static void write_hash_lock_statistics(char *prefix, struct hash_lock_statistics *stats,
				       char *suffix, char **buf, unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	/* Number of times the UDS advice proved correct */
	write_u64("dedupeAdviceValid : ", stats->dedupe_advice_valid, ", ", buf, maxlen);
	/* Number of times the UDS advice proved incorrect */
	write_u64("dedupeAdviceStale : ", stats->dedupe_advice_stale, ", ", buf, maxlen);
	/* Number of writes with the same data as another in-flight write */
	write_u64("concurrentDataMatches : ", stats->concurrent_data_matches,
		  ", ", buf, maxlen);
	/* Number of writes whose hash collided with an in-flight write */
	write_u64("concurrentHashCollisions : ",
		  stats->concurrent_hash_collisions, ", ", buf, maxlen);
	/* Current number of dedupe queries that are in flight */
	write_u32("currDedupeQueries : ", stats->curr_dedupe_queries, ", ", buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

static void write_error_statistics(char *prefix, struct error_statistics *stats,
				   char *suffix, char **buf, unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	/* number of times VDO got an invalid dedupe advice PBN from UDS */
	write_u64("invalidAdvicePBNCount : ", stats->invalid_advice_pbn_count,
		  ", ", buf, maxlen);
	/* number of times a VIO completed with a VDO_NO_SPACE error */
	write_u64("noSpaceErrorCount : ", stats->no_space_error_count, ", ",
		  buf, maxlen);
	/* number of times a VIO completed with a VDO_READ_ONLY error */
	write_u64("readOnlyErrorCount : ", stats->read_only_error_count, ", ",
		  buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

static void write_bio_stats(char *prefix, struct bio_stats *stats, char *suffix,
			    char **buf, unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	/* Number of REQ_OP_READ bios */
	write_u64("read : ", stats->read, ", ", buf, maxlen);
	/* Number of REQ_OP_WRITE bios with data */
	write_u64("write : ", stats->write, ", ", buf, maxlen);
	/* Number of bios tagged with REQ_PREFLUSH and containing no data */
	write_u64("emptyFlush : ", stats->empty_flush, ", ", buf, maxlen);
	/* Number of REQ_OP_DISCARD bios */
	write_u64("discard : ", stats->discard, ", ", buf, maxlen);
	/* Number of bios tagged with REQ_PREFLUSH */
	write_u64("flush : ", stats->flush, ", ", buf, maxlen);
	/* Number of bios tagged with REQ_FUA */
	write_u64("fua : ", stats->fua, ", ", buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

static void write_memory_usage(char *prefix, struct memory_usage *stats, char *suffix,
			       char **buf, unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	/* Tracked bytes currently allocated. */
	write_u64("bytesUsed : ", stats->bytes_used, ", ", buf, maxlen);
	/* Maximum tracked bytes allocated. */
	write_u64("peakBytesUsed : ", stats->peak_bytes_used, ", ", buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

static void write_index_statistics(char *prefix, struct index_statistics *stats,
				   char *suffix, char **buf, unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	/* Number of records stored in the index */
	write_u64("entriesIndexed : ", stats->entries_indexed, ", ", buf, maxlen);
	/* Number of post calls that found an existing entry */
	write_u64("postsFound : ", stats->posts_found, ", ", buf, maxlen);
	/* Number of post calls that added a new entry */
	write_u64("postsNotFound : ", stats->posts_not_found, ", ", buf, maxlen);
	/* Number of query calls that found an existing entry */
	write_u64("queriesFound : ", stats->queries_found, ", ", buf, maxlen);
	/* Number of query calls that added a new entry */
	write_u64("queriesNotFound : ", stats->queries_not_found, ", ", buf, maxlen);
	/* Number of update calls that found an existing entry */
	write_u64("updatesFound : ", stats->updates_found, ", ", buf, maxlen);
	/* Number of update calls that added a new entry */
	write_u64("updatesNotFound : ", stats->updates_not_found, ", ", buf, maxlen);
	/* Number of entries discarded */
	write_u64("entriesDiscarded : ", stats->entries_discarded, ", ", buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

static void write_vdo_statistics(char *prefix, struct vdo_statistics *stats, char *suffix,
				 char **buf, unsigned int *maxlen)
{
	write_string(prefix, "{ ", NULL, buf, maxlen);
	write_u32("version : ", stats->version, ", ", buf, maxlen);
	/* Number of blocks used for data */
	write_u64("dataBlocksUsed : ", stats->data_blocks_used, ", ", buf, maxlen);
	/* Number of blocks used for VDO metadata */
	write_u64("overheadBlocksUsed : ", stats->overhead_blocks_used, ", ",
		  buf, maxlen);
	/* Number of logical blocks that are currently mapped to physical blocks */
	write_u64("logicalBlocksUsed : ", stats->logical_blocks_used, ", ", buf, maxlen);
	/* number of physical blocks */
	write_block_count_t("physicalBlocks : ", stats->physical_blocks, ", ",
			    buf, maxlen);
	/* number of logical blocks */
	write_block_count_t("logicalBlocks : ", stats->logical_blocks, ", ",
			    buf, maxlen);
	/* Size of the block map page cache, in bytes */
	write_u64("blockMapCacheSize : ", stats->block_map_cache_size, ", ",
		  buf, maxlen);
	/* The physical block size */
	write_u64("blockSize : ", stats->block_size, ", ", buf, maxlen);
	/* Number of times the VDO has successfully recovered */
	write_u64("completeRecoveries : ", stats->complete_recoveries, ", ",
		  buf, maxlen);
	/* Number of times the VDO has recovered from read-only mode */
	write_u64("readOnlyRecoveries : ", stats->read_only_recoveries, ", ",
		  buf, maxlen);
	/* String describing the operating mode of the VDO */
	write_string("mode : ", stats->mode, ", ", buf, maxlen);
	/* Whether the VDO is in recovery mode */
	write_bool("inRecoveryMode : ", stats->in_recovery_mode, ", ", buf, maxlen);
	/* What percentage of recovery mode work has been completed */
	write_u8("recoveryPercentage : ", stats->recovery_percentage, ", ", buf, maxlen);
	/* The statistics for the compressed block packer */
	write_packer_statistics("packer : ", &stats->packer, ", ", buf, maxlen);
	/* Counters for events in the block allocator */
	write_block_allocator_statistics("allocator : ", &stats->allocator,
					 ", ", buf, maxlen);
	/* Counters for events in the recovery journal */
	write_recovery_journal_statistics("journal : ", &stats->journal, ", ",
					  buf, maxlen);
	/* The statistics for the slab journals */
	write_slab_journal_statistics("slabJournal : ", &stats->slab_journal,
				      ", ", buf, maxlen);
	/* The statistics for the slab summary */
	write_slab_summary_statistics("slabSummary : ", &stats->slab_summary,
				      ", ", buf, maxlen);
	/* The statistics for the reference counts */
	write_ref_counts_statistics("refCounts : ", &stats->ref_counts, ", ",
				    buf, maxlen);
	/* The statistics for the block map */
	write_block_map_statistics("blockMap : ", &stats->block_map, ", ", buf, maxlen);
	/* The dedupe statistics from hash locks */
	write_hash_lock_statistics("hashLock : ", &stats->hash_lock, ", ", buf, maxlen);
	/* Counts of error conditions */
	write_error_statistics("errors : ", &stats->errors, ", ", buf, maxlen);
	/* The VDO instance */
	write_u32("instance : ", stats->instance, ", ", buf, maxlen);
	/* Current number of active VIOs */
	write_u32("currentVIOsInProgress : ", stats->current_vios_in_progress,
		  ", ", buf, maxlen);
	/* Maximum number of active VIOs */
	write_u32("maxVIOs : ", stats->max_vios, ", ", buf, maxlen);
	/* Number of times the UDS index was too slow in responding */
	write_u64("dedupeAdviceTimeouts : ", stats->dedupe_advice_timeouts,
		  ", ", buf, maxlen);
	/* Number of flush requests submitted to the storage device */
	write_u64("flushOut : ", stats->flush_out, ", ", buf, maxlen);
	/* Logical block size */
	write_u64("logicalBlockSize : ", stats->logical_block_size, ", ", buf, maxlen);
	/* Bios submitted into VDO from above */
	write_bio_stats("biosIn : ", &stats->bios_in, ", ", buf, maxlen);
	write_bio_stats("biosInPartial : ", &stats->bios_in_partial, ", ", buf, maxlen);
	/* Bios submitted onward for user data */
	write_bio_stats("biosOut : ", &stats->bios_out, ", ", buf, maxlen);
	/* Bios submitted onward for metadata */
	write_bio_stats("biosMeta : ", &stats->bios_meta, ", ", buf, maxlen);
	write_bio_stats("biosJournal : ", &stats->bios_journal, ", ", buf, maxlen);
	write_bio_stats("biosPageCache : ", &stats->bios_page_cache, ", ", buf, maxlen);
	write_bio_stats("biosOutCompleted : ", &stats->bios_out_completed, ", ",
			buf, maxlen);
	write_bio_stats("biosMetaCompleted : ", &stats->bios_meta_completed,
			", ", buf, maxlen);
	write_bio_stats("biosJournalCompleted : ",
			&stats->bios_journal_completed, ", ", buf, maxlen);
	write_bio_stats("biosPageCacheCompleted : ",
			&stats->bios_page_cache_completed, ", ", buf, maxlen);
	write_bio_stats("biosAcknowledged : ", &stats->bios_acknowledged, ", ",
			buf, maxlen);
	write_bio_stats("biosAcknowledgedPartial : ",
			&stats->bios_acknowledged_partial, ", ", buf, maxlen);
	/* Current number of bios in progress */
	write_bio_stats("biosInProgress : ", &stats->bios_in_progress, ", ",
			buf, maxlen);
	/* Memory usage stats. */
	write_memory_usage("memoryUsage : ", &stats->memory_usage, ", ", buf, maxlen);
	/* The statistics for the UDS index */
	write_index_statistics("index : ", &stats->index, ", ", buf, maxlen);
	write_string(NULL, "}", suffix, buf, maxlen);
}

int vdo_write_stats(struct vdo *vdo, char *buf, unsigned int maxlen)
{
	struct vdo_statistics *stats;
	int result;

	result = vdo_allocate(1, struct vdo_statistics, __func__, &stats);
	if (result != VDO_SUCCESS) {
		vdo_log_error("Cannot allocate memory to write VDO statistics");
		return result;
	}

	vdo_fetch_statistics(vdo, stats);
	write_vdo_statistics(NULL, stats, NULL, &buf, &maxlen);
	vdo_free(stats);
	return VDO_SUCCESS;
}

static void write_index_memory(u32 mem, char **buf, unsigned int *maxlen)
{
	char *prefix = "memorySize : ";

	/* Convert index memory to fractional value */
	if (mem == (u32)UDS_MEMORY_CONFIG_256MB)
		write_string(prefix, "0.25, ", NULL, buf, maxlen);
	else if (mem == (u32)UDS_MEMORY_CONFIG_512MB)
		write_string(prefix, "0.50, ", NULL, buf, maxlen);
	else if (mem == (u32)UDS_MEMORY_CONFIG_768MB)
		write_string(prefix, "0.75, ", NULL, buf, maxlen);
	else
		write_u32(prefix, mem, ", ", buf, maxlen);
}

static void write_index_config(struct index_config *config, char **buf,
			       unsigned int *maxlen)
{
	write_string("index :  ", "{ ", NULL, buf, maxlen);
	/* index mem size */
	write_index_memory(config->mem, buf, maxlen);
	/* whether the index is sparse or not */
	write_bool("isSparse : ", config->sparse, ", ", buf, maxlen);
	write_string(NULL, "}", ", ", buf, maxlen);
}

int vdo_write_config(struct vdo *vdo, char **buf, unsigned int *maxlen)
{
	struct vdo_config *config = &vdo->states.vdo.config;

	write_string(NULL, "{ ", NULL, buf, maxlen);
	/* version */
	write_u32("version : ", 1, ", ", buf, maxlen);
	/* physical size */
	write_block_count_t("physicalSize : ", config->physical_blocks * VDO_BLOCK_SIZE, ", ",
			    buf, maxlen);
	/* logical size */
	write_block_count_t("logicalSize : ", config->logical_blocks * VDO_BLOCK_SIZE, ", ",
			    buf, maxlen);
	/* slab size */
	write_block_count_t("slabSize : ", config->slab_size, ", ", buf, maxlen);
	/* index config */
	write_index_config(&vdo->geometry.index_config, buf, maxlen);
	write_string(NULL, "}", NULL, buf, maxlen);
	return VDO_SUCCESS;
}
