// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */


#include "index.h"

#include "logger.h"
#include "memory-alloc.h"

#include "funnel-requestqueue.h"
#include "hash-utils.h"
#include "sparse-cache.h"

static const u64 NO_LAST_SAVE = U64_MAX;

/*
 * When searching for deduplication records, the index first searches the volume index, and then
 * searches the chapter index for the relevant chapter. If the chapter has been fully committed to
 * storage, the chapter pages are loaded into the page cache. If the chapter has not yet been
 * committed (either the open chapter or a recently closed one), the index searches the in-memory
 * representation of the chapter. Finally, if the volume index does not find a record and the index
 * is sparse, the index will search the sparse cache.
 *
 * The index send two kinds of messages to coordinate between zones: chapter close messages for the
 * chapter writer, and sparse cache barrier messages for the sparse cache.
 *
 * The chapter writer is responsible for committing chapters of records to storage. Since zones can
 * get different numbers of records, some zones may fall behind others. Each time a zone fills up
 * its available space in a chapter, it informs the chapter writer that the chapter is complete,
 * and also informs all other zones that it has closed the chapter. Each other zone will then close
 * the chapter immediately, regardless of how full it is, in order to minimize skew between zones.
 * Once every zone has closed the chapter, the chapter writer will commit that chapter to storage.
 *
 * The last zone to close the chapter also removes the oldest chapter from the volume index.
 * Although that chapter is invalid for zones that have moved on, the existence of the open chapter
 * means that those zones will never ask the volume index about it. No zone is allowed to get more
 * than one chapter ahead of any other. If a zone is so far ahead that it tries to close another
 * chapter before the previous one has been closed by all zones, it is forced to wait.
 *
 * The sparse cache relies on having the same set of chapter indexes available to all zones. When a
 * request wants to add a chapter to the sparse cache, it sends a barrier message to each zone
 * during the triage stage that acts as a rendezvous. Once every zone has reached the barrier and
 * paused its operations, the cache membership is changed and each zone is then informed that it
 * can proceed. More details can be found in the sparse cache documentation.
 *
 * If a sparse cache has only one zone, it will not create a triage queue, but it still needs the
 * barrier message to change the sparse cache membership, so the index simulates the message by
 * invoking the handler directly.
 */

struct chapter_writer {
	/* The index to which we belong */
	struct uds_index *index;
	/* The thread to do the writing */
	struct thread *thread;
	/* The lock protecting the following fields */
	struct mutex mutex;
	/* The condition signalled on state changes */
	struct cond_var cond;
	/* Set to true to stop the thread */
	bool stop;
	/* The result from the most recent write */
	int result;
	/* The number of bytes allocated by the chapter writer */
	size_t memory_size;
	/* The number of zones which have submitted a chapter for writing */
	unsigned int zones_to_write;
	/* Open chapter index used by uds_close_open_chapter() */
	struct open_chapter_index *open_chapter_index;
	/* Collated records used by uds_close_open_chapter() */
	struct uds_volume_record *collated_records;
	/* The chapters to write (one per zone) */
	struct open_chapter_zone *chapters[];
};

static bool is_zone_chapter_sparse(const struct index_zone *zone, u64 virtual_chapter)
{
	return uds_is_chapter_sparse(zone->index->volume->geometry,
				     zone->oldest_virtual_chapter,
				     zone->newest_virtual_chapter, virtual_chapter);
}

static int launch_zone_message(struct uds_zone_message message, unsigned int zone,
			       struct uds_index *index)
{
	int result;
	struct uds_request *request;

	result = vdo_allocate(1, struct uds_request, __func__, &request);
	if (result != VDO_SUCCESS)
		return result;

	request->index = index;
	request->unbatched = true;
	request->zone_number = zone;
	request->zone_message = message;

	uds_enqueue_request(request, STAGE_MESSAGE);
	return UDS_SUCCESS;
}

static void enqueue_barrier_messages(struct uds_index *index, u64 virtual_chapter)
{
	struct uds_zone_message message = {
		.type = UDS_MESSAGE_SPARSE_CACHE_BARRIER,
		.virtual_chapter = virtual_chapter,
	};
	unsigned int zone;

	for (zone = 0; zone < index->zone_count; zone++) {
		int result = launch_zone_message(message, zone, index);

		VDO_ASSERT_LOG_ONLY((result == UDS_SUCCESS), "barrier message allocation");
	}
}

/*
 * Determine whether this request should trigger a sparse cache barrier message to change the
 * membership of the sparse cache. If a change in membership is desired, the function returns the
 * chapter number to add.
 */
static u64 triage_index_request(struct uds_index *index, struct uds_request *request)
{
	u64 virtual_chapter;
	struct index_zone *zone;

	virtual_chapter = uds_lookup_volume_index_name(index->volume_index,
						       &request->record_name);
	if (virtual_chapter == NO_CHAPTER)
		return NO_CHAPTER;

	zone = index->zones[request->zone_number];
	if (!is_zone_chapter_sparse(zone, virtual_chapter))
		return NO_CHAPTER;

	/*
	 * FIXME: Optimize for a common case by remembering the chapter from the most recent
	 * barrier message and skipping this chapter if is it the same.
	 */

	return virtual_chapter;
}

/*
 * Simulate a message to change the sparse cache membership for a single-zone sparse index. This
 * allows us to forgo the complicated locking required by a multi-zone sparse index. Any other kind
 * of index does nothing here.
 */
static int simulate_index_zone_barrier_message(struct index_zone *zone,
					       struct uds_request *request)
{
	u64 sparse_virtual_chapter;

	if ((zone->index->zone_count > 1) ||
	    !uds_is_sparse_index_geometry(zone->index->volume->geometry))
		return UDS_SUCCESS;

	sparse_virtual_chapter = triage_index_request(zone->index, request);
	if (sparse_virtual_chapter == NO_CHAPTER)
		return UDS_SUCCESS;

	return uds_update_sparse_cache(zone, sparse_virtual_chapter);
}

/* This is the request processing function for the triage queue. */
static void triage_request(struct uds_request *request)
{
	struct uds_index *index = request->index;
	u64 sparse_virtual_chapter = triage_index_request(index, request);

	if (sparse_virtual_chapter != NO_CHAPTER)
		enqueue_barrier_messages(index, sparse_virtual_chapter);

	uds_enqueue_request(request, STAGE_INDEX);
}

static int finish_previous_chapter(struct uds_index *index, u64 current_chapter_number)
{
	int result;
	struct chapter_writer *writer = index->chapter_writer;

	mutex_lock(&writer->mutex);
	while (index->newest_virtual_chapter < current_chapter_number)
		uds_wait_cond(&writer->cond, &writer->mutex);
	result = writer->result;
	mutex_unlock(&writer->mutex);

	if (result != UDS_SUCCESS)
		return vdo_log_error_strerror(result,
					      "Writing of previous open chapter failed");

	return UDS_SUCCESS;
}

static int swap_open_chapter(struct index_zone *zone)
{
	int result;
	struct open_chapter_zone *temporary_chapter;

	result = finish_previous_chapter(zone->index, zone->newest_virtual_chapter);
	if (result != UDS_SUCCESS)
		return result;

	temporary_chapter = zone->open_chapter;
	zone->open_chapter = zone->writing_chapter;
	zone->writing_chapter = temporary_chapter;
	return UDS_SUCCESS;
}

/*
 * Inform the chapter writer that this zone is done with this chapter. The chapter won't start
 * writing until all zones have closed it.
 */
static unsigned int start_closing_chapter(struct uds_index *index,
					  unsigned int zone_number,
					  struct open_chapter_zone *chapter)
{
	unsigned int finished_zones;
	struct chapter_writer *writer = index->chapter_writer;

	mutex_lock(&writer->mutex);
	finished_zones = ++writer->zones_to_write;
	writer->chapters[zone_number] = chapter;
	uds_broadcast_cond(&writer->cond);
	mutex_unlock(&writer->mutex);

	return finished_zones;
}

static int announce_chapter_closed(struct index_zone *zone, u64 closed_chapter)
{
	int result;
	unsigned int i;
	struct uds_zone_message zone_message = {
		.type = UDS_MESSAGE_ANNOUNCE_CHAPTER_CLOSED,
		.virtual_chapter = closed_chapter,
	};

	for (i = 0; i < zone->index->zone_count; i++) {
		if (zone->id == i)
			continue;

		result = launch_zone_message(zone_message, i, zone->index);
		if (result != UDS_SUCCESS)
			return result;
	}

	return UDS_SUCCESS;
}

static int open_next_chapter(struct index_zone *zone)
{
	int result;
	u64 closed_chapter;
	u64 expiring;
	unsigned int finished_zones;
	u32 expire_chapters;

	vdo_log_debug("closing chapter %llu of zone %u after %u entries (%u short)",
		      (unsigned long long) zone->newest_virtual_chapter, zone->id,
		      zone->open_chapter->size,
		      zone->open_chapter->capacity - zone->open_chapter->size);

	result = swap_open_chapter(zone);
	if (result != UDS_SUCCESS)
		return result;

	closed_chapter = zone->newest_virtual_chapter++;
	uds_set_volume_index_zone_open_chapter(zone->index->volume_index, zone->id,
					       zone->newest_virtual_chapter);
	uds_reset_open_chapter(zone->open_chapter);

	finished_zones = start_closing_chapter(zone->index, zone->id,
					       zone->writing_chapter);
	if ((finished_zones == 1) && (zone->index->zone_count > 1)) {
		result = announce_chapter_closed(zone, closed_chapter);
		if (result != UDS_SUCCESS)
			return result;
	}

	expiring = zone->oldest_virtual_chapter;
	expire_chapters = uds_chapters_to_expire(zone->index->volume->geometry,
						 zone->newest_virtual_chapter);
	zone->oldest_virtual_chapter += expire_chapters;

	if (finished_zones < zone->index->zone_count)
		return UDS_SUCCESS;

	while (expire_chapters-- > 0)
		uds_forget_chapter(zone->index->volume, expiring++);

	return UDS_SUCCESS;
}

static int handle_chapter_closed(struct index_zone *zone, u64 virtual_chapter)
{
	if (zone->newest_virtual_chapter == virtual_chapter)
		return open_next_chapter(zone);

	return UDS_SUCCESS;
}

static int dispatch_index_zone_control_request(struct uds_request *request)
{
	struct uds_zone_message *message = &request->zone_message;
	struct index_zone *zone = request->index->zones[request->zone_number];

	switch (message->type) {
	case UDS_MESSAGE_SPARSE_CACHE_BARRIER:
		return uds_update_sparse_cache(zone, message->virtual_chapter);

	case UDS_MESSAGE_ANNOUNCE_CHAPTER_CLOSED:
		return handle_chapter_closed(zone, message->virtual_chapter);

	default:
		vdo_log_error("invalid message type: %d", message->type);
		return UDS_INVALID_ARGUMENT;
	}
}

static void set_request_location(struct uds_request *request,
				 enum uds_index_region new_location)
{
	request->location = new_location;
	request->found = ((new_location == UDS_LOCATION_IN_OPEN_CHAPTER) ||
			  (new_location == UDS_LOCATION_IN_DENSE) ||
			  (new_location == UDS_LOCATION_IN_SPARSE));
}

static void set_chapter_location(struct uds_request *request,
				 const struct index_zone *zone, u64 virtual_chapter)
{
	request->found = true;
	if (virtual_chapter == zone->newest_virtual_chapter)
		request->location = UDS_LOCATION_IN_OPEN_CHAPTER;
	else if (is_zone_chapter_sparse(zone, virtual_chapter))
		request->location = UDS_LOCATION_IN_SPARSE;
	else
		request->location = UDS_LOCATION_IN_DENSE;
}

static int search_sparse_cache_in_zone(struct index_zone *zone, struct uds_request *request,
				       u64 virtual_chapter, bool *found)
{
	int result;
	struct volume *volume;
	u16 record_page_number;
	u32 chapter;

	result = uds_search_sparse_cache(zone, &request->record_name, &virtual_chapter,
					 &record_page_number);
	if ((result != UDS_SUCCESS) || (virtual_chapter == NO_CHAPTER))
		return result;

	request->virtual_chapter = virtual_chapter;
	volume = zone->index->volume;
	chapter = uds_map_to_physical_chapter(volume->geometry, virtual_chapter);
	return uds_search_cached_record_page(volume, request, chapter,
					     record_page_number, found);
}

static int get_record_from_zone(struct index_zone *zone, struct uds_request *request,
				bool *found)
{
	struct volume *volume;

	if (request->location == UDS_LOCATION_RECORD_PAGE_LOOKUP) {
		*found = true;
		return UDS_SUCCESS;
	} else if (request->location == UDS_LOCATION_UNAVAILABLE) {
		*found = false;
		return UDS_SUCCESS;
	}

	if (request->virtual_chapter == zone->newest_virtual_chapter) {
		uds_search_open_chapter(zone->open_chapter, &request->record_name,
					&request->old_metadata, found);
		return UDS_SUCCESS;
	}

	if ((zone->newest_virtual_chapter > 0) &&
	    (request->virtual_chapter == (zone->newest_virtual_chapter - 1)) &&
	    (zone->writing_chapter->size > 0)) {
		uds_search_open_chapter(zone->writing_chapter, &request->record_name,
					&request->old_metadata, found);
		return UDS_SUCCESS;
	}

	volume = zone->index->volume;
	if (is_zone_chapter_sparse(zone, request->virtual_chapter) &&
	    uds_sparse_cache_contains(volume->sparse_cache, request->virtual_chapter,
				      request->zone_number))
		return search_sparse_cache_in_zone(zone, request,
						   request->virtual_chapter, found);

	return uds_search_volume_page_cache(volume, request, found);
}

static int put_record_in_zone(struct index_zone *zone, struct uds_request *request,
			      const struct uds_record_data *metadata)
{
	unsigned int remaining;

	remaining = uds_put_open_chapter(zone->open_chapter, &request->record_name,
					 metadata);
	if (remaining == 0)
		return open_next_chapter(zone);

	return UDS_SUCCESS;
}

static int search_index_zone(struct index_zone *zone, struct uds_request *request)
{
	int result;
	struct volume_index_record record;
	bool overflow_record, found = false;
	struct uds_record_data *metadata;
	u64 chapter;

	result = uds_get_volume_index_record(zone->index->volume_index,
					     &request->record_name, &record);
	if (result != UDS_SUCCESS)
		return result;

	if (record.is_found) {
		if (request->requeued && request->virtual_chapter != record.virtual_chapter)
			set_request_location(request, UDS_LOCATION_UNKNOWN);

		request->virtual_chapter = record.virtual_chapter;
		result = get_record_from_zone(zone, request, &found);
		if (result != UDS_SUCCESS)
			return result;
	}

	if (found)
		set_chapter_location(request, zone, record.virtual_chapter);

	/*
	 * If a record has overflowed a chapter index in more than one chapter (or overflowed in
	 * one chapter and collided with an existing record), it will exist as a collision record
	 * in the volume index, but we won't find it in the volume. This case needs special
	 * handling.
	 */
	overflow_record = (record.is_found && record.is_collision && !found);
	chapter = zone->newest_virtual_chapter;
	if (found || overflow_record) {
		if ((request->type == UDS_QUERY_NO_UPDATE) ||
		    ((request->type == UDS_QUERY) && overflow_record)) {
			/* There is nothing left to do. */
			return UDS_SUCCESS;
		}

		if (record.virtual_chapter != chapter) {
			/*
			 * Update the volume index to reference the new chapter for the block. If
			 * the record had been deleted or dropped from the chapter index, it will
			 * be back.
			 */
			result = uds_set_volume_index_record_chapter(&record, chapter);
		} else if (request->type != UDS_UPDATE) {
			/* The record is already in the open chapter. */
			return UDS_SUCCESS;
		}
	} else {
		/*
		 * The record wasn't in the volume index, so check whether the
		 * name is in a cached sparse chapter. If we found the name on
		 * a previous search, use that result instead.
		 */
		if (request->location == UDS_LOCATION_RECORD_PAGE_LOOKUP) {
			found = true;
		} else if (request->location == UDS_LOCATION_UNAVAILABLE) {
			found = false;
		} else if (uds_is_sparse_index_geometry(zone->index->volume->geometry) &&
			   !uds_is_volume_index_sample(zone->index->volume_index,
						       &request->record_name)) {
			result = search_sparse_cache_in_zone(zone, request, NO_CHAPTER,
							     &found);
			if (result != UDS_SUCCESS)
				return result;
		}

		if (found)
			set_request_location(request, UDS_LOCATION_IN_SPARSE);

		if ((request->type == UDS_QUERY_NO_UPDATE) ||
		    ((request->type == UDS_QUERY) && !found)) {
			/* There is nothing left to do. */
			return UDS_SUCCESS;
		}

		/*
		 * Add a new entry to the volume index referencing the open chapter. This needs to
		 * be done both for new records, and for records from cached sparse chapters.
		 */
		result = uds_put_volume_index_record(&record, chapter);
	}

	if (result == UDS_OVERFLOW) {
		/*
		 * The volume index encountered a delta list overflow. The condition was already
		 * logged. We will go on without adding the record to the open chapter.
		 */
		return UDS_SUCCESS;
	}

	if (result != UDS_SUCCESS)
		return result;

	if (!found || (request->type == UDS_UPDATE)) {
		/* This is a new record or we're updating an existing record. */
		metadata = &request->new_metadata;
	} else {
		/* Move the existing record to the open chapter. */
		metadata = &request->old_metadata;
	}

	return put_record_in_zone(zone, request, metadata);
}

static int remove_from_index_zone(struct index_zone *zone, struct uds_request *request)
{
	int result;
	struct volume_index_record record;

	result = uds_get_volume_index_record(zone->index->volume_index,
					     &request->record_name, &record);
	if (result != UDS_SUCCESS)
		return result;

	if (!record.is_found)
		return UDS_SUCCESS;

	/* If the request was requeued, check whether the saved state is still valid. */

	if (record.is_collision) {
		set_chapter_location(request, zone, record.virtual_chapter);
	} else {
		/* Non-collision records are hints, so resolve the name in the chapter. */
		bool found;

		if (request->requeued && request->virtual_chapter != record.virtual_chapter)
			set_request_location(request, UDS_LOCATION_UNKNOWN);

		request->virtual_chapter = record.virtual_chapter;
		result = get_record_from_zone(zone, request, &found);
		if (result != UDS_SUCCESS)
			return result;

		if (!found) {
			/* There is no record to remove. */
			return UDS_SUCCESS;
		}
	}

	set_chapter_location(request, zone, record.virtual_chapter);

	/*
	 * Delete the volume index entry for the named record only. Note that a later search might
	 * later return stale advice if there is a colliding name in the same chapter, but it's a
	 * very rare case (1 in 2^21).
	 */
	result = uds_remove_volume_index_record(&record);
	if (result != UDS_SUCCESS)
		return result;

	/*
	 * If the record is in the open chapter, we must remove it or mark it deleted to avoid
	 * trouble if the record is added again later.
	 */
	if (request->location == UDS_LOCATION_IN_OPEN_CHAPTER)
		uds_remove_from_open_chapter(zone->open_chapter, &request->record_name);

	return UDS_SUCCESS;
}

static int dispatch_index_request(struct uds_index *index, struct uds_request *request)
{
	int result;
	struct index_zone *zone = index->zones[request->zone_number];

	if (!request->requeued) {
		result = simulate_index_zone_barrier_message(zone, request);
		if (result != UDS_SUCCESS)
			return result;
	}

	switch (request->type) {
	case UDS_POST:
	case UDS_UPDATE:
	case UDS_QUERY:
	case UDS_QUERY_NO_UPDATE:
		result = search_index_zone(zone, request);
		break;

	case UDS_DELETE:
		result = remove_from_index_zone(zone, request);
		break;

	default:
		result = vdo_log_warning_strerror(UDS_INVALID_ARGUMENT,
						  "invalid request type: %d",
						  request->type);
		break;
	}

	return result;
}

/* This is the request processing function invoked by each zone's thread. */
static void execute_zone_request(struct uds_request *request)
{
	int result;
	struct uds_index *index = request->index;

	if (request->zone_message.type != UDS_MESSAGE_NONE) {
		result = dispatch_index_zone_control_request(request);
		if (result != UDS_SUCCESS) {
			vdo_log_error_strerror(result, "error executing message: %d",
					       request->zone_message.type);
		}

		/* Once the message is processed it can be freed. */
		vdo_free(vdo_forget(request));
		return;
	}

	index->need_to_save = true;
	if (request->requeued && (request->status != UDS_SUCCESS)) {
		set_request_location(request, UDS_LOCATION_UNAVAILABLE);
		index->callback(request);
		return;
	}

	result = dispatch_index_request(index, request);
	if (result == UDS_QUEUED) {
		/* The request has been requeued so don't let it complete. */
		return;
	}

	if (!request->found)
		set_request_location(request, UDS_LOCATION_UNAVAILABLE);

	request->status = result;
	index->callback(request);
}

static int initialize_index_queues(struct uds_index *index,
				   const struct index_geometry *geometry)
{
	int result;
	unsigned int i;

	for (i = 0; i < index->zone_count; i++) {
		result = uds_make_request_queue("indexW", &execute_zone_request,
						&index->zone_queues[i]);
		if (result != UDS_SUCCESS)
			return result;
	}

	/* The triage queue is only needed for sparse multi-zone indexes. */
	if ((index->zone_count > 1) && uds_is_sparse_index_geometry(geometry)) {
		result = uds_make_request_queue("triageW", &triage_request,
						&index->triage_queue);
		if (result != UDS_SUCCESS)
			return result;
	}

	return UDS_SUCCESS;
}

/* This is the driver function for the chapter writer thread. */
static void close_chapters(void *arg)
{
	int result;
	struct chapter_writer *writer = arg;
	struct uds_index *index = writer->index;

	vdo_log_debug("chapter writer starting");
	mutex_lock(&writer->mutex);
	for (;;) {
		while (writer->zones_to_write < index->zone_count) {
			if (writer->stop && (writer->zones_to_write == 0)) {
				/*
				 * We've been told to stop, and all of the zones are in the same
				 * open chapter, so we can exit now.
				 */
				mutex_unlock(&writer->mutex);
				vdo_log_debug("chapter writer stopping");
				return;
			}
			uds_wait_cond(&writer->cond, &writer->mutex);
		}

		/*
		 * Release the lock while closing a chapter. We probably don't need to do this, but
		 * it seems safer in principle. It's OK to access the chapter and chapter_number
		 * fields without the lock since those aren't allowed to change until we're done.
		 */
		mutex_unlock(&writer->mutex);

		if (index->has_saved_open_chapter) {
			/*
			 * Remove the saved open chapter the first time we close an open chapter
			 * after loading from a clean shutdown, or after doing a clean save. The
			 * lack of the saved open chapter will indicate that a recovery is
			 * necessary.
			 */
			index->has_saved_open_chapter = false;
			result = uds_discard_open_chapter(index->layout);
			if (result == UDS_SUCCESS)
				vdo_log_debug("Discarding saved open chapter");
		}

		result = uds_close_open_chapter(writer->chapters, index->zone_count,
						index->volume,
						writer->open_chapter_index,
						writer->collated_records,
						index->newest_virtual_chapter);

		mutex_lock(&writer->mutex);
		index->newest_virtual_chapter++;
		index->oldest_virtual_chapter +=
			uds_chapters_to_expire(index->volume->geometry,
					       index->newest_virtual_chapter);
		writer->result = result;
		writer->zones_to_write = 0;
		uds_broadcast_cond(&writer->cond);
	}
}

static void stop_chapter_writer(struct chapter_writer *writer)
{
	struct thread *writer_thread = NULL;

	mutex_lock(&writer->mutex);
	if (writer->thread != NULL) {
		writer_thread = writer->thread;
		writer->thread = NULL;
		writer->stop = true;
		uds_broadcast_cond(&writer->cond);
	}
	mutex_unlock(&writer->mutex);

	if (writer_thread != NULL)
		vdo_join_threads(writer_thread);
}

static void free_chapter_writer(struct chapter_writer *writer)
{
	if (writer == NULL)
		return;

	stop_chapter_writer(writer);
	uds_free_open_chapter_index(writer->open_chapter_index);
	vdo_free(writer->collated_records);
	vdo_free(writer);
}

static int make_chapter_writer(struct uds_index *index,
			       struct chapter_writer **writer_ptr)
{
	int result;
	struct chapter_writer *writer;
	size_t collated_records_size =
		(sizeof(struct uds_volume_record) * index->volume->geometry->records_per_chapter);

	result = vdo_allocate_extended(struct chapter_writer, index->zone_count,
				       struct open_chapter_zone *, "Chapter Writer",
				       &writer);
	if (result != VDO_SUCCESS)
		return result;

	writer->index = index;
	mutex_init(&writer->mutex);
	uds_init_cond(&writer->cond);

	result = vdo_allocate_cache_aligned(collated_records_size, "collated records",
					    &writer->collated_records);
	if (result != VDO_SUCCESS) {
		free_chapter_writer(writer);
		return result;
	}

	result = uds_make_open_chapter_index(&writer->open_chapter_index,
					     index->volume->geometry,
					     index->volume->nonce);
	if (result != UDS_SUCCESS) {
		free_chapter_writer(writer);
		return result;
	}

	writer->memory_size = (sizeof(struct chapter_writer) +
			       index->zone_count * sizeof(struct open_chapter_zone *) +
			       collated_records_size +
			       writer->open_chapter_index->memory_size);

	result = vdo_create_thread(close_chapters, writer, "writer", &writer->thread);
	if (result != VDO_SUCCESS) {
		free_chapter_writer(writer);
		return result;
	}

	*writer_ptr = writer;
	return UDS_SUCCESS;
}

static int load_index(struct uds_index *index)
{
	int result;
	u64 last_save_chapter;

	result = uds_load_index_state(index->layout, index);
	if (result != UDS_SUCCESS)
		return UDS_INDEX_NOT_SAVED_CLEANLY;

	last_save_chapter = ((index->last_save != NO_LAST_SAVE) ? index->last_save : 0);

	vdo_log_info("loaded index from chapter %llu through chapter %llu",
		     (unsigned long long) index->oldest_virtual_chapter,
		     (unsigned long long) last_save_chapter);

	return UDS_SUCCESS;
}

static int rebuild_index_page_map(struct uds_index *index, u64 vcn)
{
	int result;
	struct delta_index_page *chapter_index_page;
	struct index_geometry *geometry = index->volume->geometry;
	u32 chapter = uds_map_to_physical_chapter(geometry, vcn);
	u32 expected_list_number = 0;
	u32 index_page_number;
	u32 lowest_delta_list;
	u32 highest_delta_list;

	for (index_page_number = 0;
	     index_page_number < geometry->index_pages_per_chapter;
	     index_page_number++) {
		result = uds_get_volume_index_page(index->volume, chapter,
						   index_page_number,
						   &chapter_index_page);
		if (result != UDS_SUCCESS) {
			return vdo_log_error_strerror(result,
						      "failed to read index page %u in chapter %u",
						      index_page_number, chapter);
		}

		lowest_delta_list = chapter_index_page->lowest_list_number;
		highest_delta_list = chapter_index_page->highest_list_number;
		if (lowest_delta_list != expected_list_number) {
			return vdo_log_error_strerror(UDS_CORRUPT_DATA,
						      "chapter %u index page %u is corrupt",
						      chapter, index_page_number);
		}

		uds_update_index_page_map(index->volume->index_page_map, vcn, chapter,
					  index_page_number, highest_delta_list);
		expected_list_number = highest_delta_list + 1;
	}

	return UDS_SUCCESS;
}

static int replay_record(struct uds_index *index, const struct uds_record_name *name,
			 u64 virtual_chapter, bool will_be_sparse_chapter)
{
	int result;
	struct volume_index_record record;
	bool update_record;

	if (will_be_sparse_chapter &&
	    !uds_is_volume_index_sample(index->volume_index, name)) {
		/*
		 * This entry will be in a sparse chapter after the rebuild completes, and it is
		 * not a sample, so just skip over it.
		 */
		return UDS_SUCCESS;
	}

	result = uds_get_volume_index_record(index->volume_index, name, &record);
	if (result != UDS_SUCCESS)
		return result;

	if (record.is_found) {
		if (record.is_collision) {
			if (record.virtual_chapter == virtual_chapter) {
				/* The record is already correct. */
				return UDS_SUCCESS;
			}

			update_record = true;
		} else if (record.virtual_chapter == virtual_chapter) {
			/*
			 * There is a volume index entry pointing to the current chapter, but we
			 * don't know if it is for the same name as the one we are currently
			 * working on or not. For now, we're just going to assume that it isn't.
			 * This will create one extra collision record if there was a deleted
			 * record in the current chapter.
			 */
			update_record = false;
		} else {
			/*
			 * If we're rebuilding, we don't normally want to go to disk to see if the
			 * record exists, since we will likely have just read the record from disk
			 * (i.e. we know it's there). The exception to this is when we find an
			 * entry in the volume index that has a different chapter. In this case, we
			 * need to search that chapter to determine if the volume index entry was
			 * for the same record or a different one.
			 */
			result = uds_search_volume_page_cache_for_rebuild(index->volume,
									  name,
									  record.virtual_chapter,
									  &update_record);
			if (result != UDS_SUCCESS)
				return result;
			}
	} else {
		update_record = false;
	}

	if (update_record) {
		/*
		 * Update the volume index to reference the new chapter for the block. If the
		 * record had been deleted or dropped from the chapter index, it will be back.
		 */
		result = uds_set_volume_index_record_chapter(&record, virtual_chapter);
	} else {
		/*
		 * Add a new entry to the volume index referencing the open chapter. This should be
		 * done regardless of whether we are a brand new record or a sparse record, i.e.
		 * one that doesn't exist in the index but does on disk, since for a sparse record,
		 * we would want to un-sparsify if it did exist.
		 */
		result = uds_put_volume_index_record(&record, virtual_chapter);
	}

	if ((result == UDS_DUPLICATE_NAME) || (result == UDS_OVERFLOW)) {
		/* The rebuilt index will lose these records. */
		return UDS_SUCCESS;
	}

	return result;
}

static bool check_for_suspend(struct uds_index *index)
{
	bool closing;

	if (index->load_context == NULL)
		return false;

	mutex_lock(&index->load_context->mutex);
	if (index->load_context->status != INDEX_SUSPENDING) {
		mutex_unlock(&index->load_context->mutex);
		return false;
	}

	/* Notify that we are suspended and wait for the resume. */
	index->load_context->status = INDEX_SUSPENDED;
	uds_broadcast_cond(&index->load_context->cond);

	while ((index->load_context->status != INDEX_OPENING) &&
	       (index->load_context->status != INDEX_FREEING))
		uds_wait_cond(&index->load_context->cond, &index->load_context->mutex);

	closing = (index->load_context->status == INDEX_FREEING);
	mutex_unlock(&index->load_context->mutex);
	return closing;
}

static int replay_chapter(struct uds_index *index, u64 virtual, bool sparse)
{
	int result;
	u32 i;
	u32 j;
	const struct index_geometry *geometry;
	u32 physical_chapter;

	if (check_for_suspend(index)) {
		vdo_log_info("Replay interrupted by index shutdown at chapter %llu",
			     (unsigned long long) virtual);
		return -EBUSY;
	}

	geometry = index->volume->geometry;
	physical_chapter = uds_map_to_physical_chapter(geometry, virtual);
	uds_prefetch_volume_chapter(index->volume, physical_chapter);
	uds_set_volume_index_open_chapter(index->volume_index, virtual);

	result = rebuild_index_page_map(index, virtual);
	if (result != UDS_SUCCESS) {
		return vdo_log_error_strerror(result,
					      "could not rebuild index page map for chapter %u",
					      physical_chapter);
	}

	for (i = 0; i < geometry->record_pages_per_chapter; i++) {
		u8 *record_page;
		u32 record_page_number;

		record_page_number = geometry->index_pages_per_chapter + i;
		result = uds_get_volume_record_page(index->volume, physical_chapter,
						    record_page_number, &record_page);
		if (result != UDS_SUCCESS) {
			return vdo_log_error_strerror(result, "could not get page %d",
						      record_page_number);
		}

		for (j = 0; j < geometry->records_per_page; j++) {
			const u8 *name_bytes;
			struct uds_record_name name;

			name_bytes = record_page + (j * BYTES_PER_RECORD);
			memcpy(&name.name, name_bytes, UDS_RECORD_NAME_SIZE);
			result = replay_record(index, &name, virtual, sparse);
			if (result != UDS_SUCCESS)
				return result;
		}
	}

	return UDS_SUCCESS;
}

static int replay_volume(struct uds_index *index)
{
	int result;
	u64 old_map_update;
	u64 new_map_update;
	u64 virtual;
	u64 from_virtual = index->oldest_virtual_chapter;
	u64 upto_virtual = index->newest_virtual_chapter;
	bool will_be_sparse;

	vdo_log_info("Replaying volume from chapter %llu through chapter %llu",
		     (unsigned long long) from_virtual,
		     (unsigned long long) upto_virtual);

	/*
	 * The index failed to load, so the volume index is empty. Add records to the volume index
	 * in order, skipping non-hooks in chapters which will be sparse to save time.
	 *
	 * Go through each record page of each chapter and add the records back to the volume
	 * index. This should not cause anything to be written to either the open chapter or the
	 * on-disk volume. Also skip the on-disk chapter corresponding to upto_virtual, as this
	 * would have already been purged from the volume index when the chapter was opened.
	 *
	 * Also, go through each index page for each chapter and rebuild the index page map.
	 */
	old_map_update = index->volume->index_page_map->last_update;
	for (virtual = from_virtual; virtual < upto_virtual; virtual++) {
		will_be_sparse = uds_is_chapter_sparse(index->volume->geometry,
						       from_virtual, upto_virtual,
						       virtual);
		result = replay_chapter(index, virtual, will_be_sparse);
		if (result != UDS_SUCCESS)
			return result;
	}

	/* Also reap the chapter being replaced by the open chapter. */
	uds_set_volume_index_open_chapter(index->volume_index, upto_virtual);

	new_map_update = index->volume->index_page_map->last_update;
	if (new_map_update != old_map_update) {
		vdo_log_info("replay changed index page map update from %llu to %llu",
			     (unsigned long long) old_map_update,
			     (unsigned long long) new_map_update);
	}

	return UDS_SUCCESS;
}

static int rebuild_index(struct uds_index *index)
{
	int result;
	u64 lowest;
	u64 highest;
	bool is_empty = false;
	u32 chapters_per_volume = index->volume->geometry->chapters_per_volume;

	index->volume->lookup_mode = LOOKUP_FOR_REBUILD;
	result = uds_find_volume_chapter_boundaries(index->volume, &lowest, &highest,
						    &is_empty);
	if (result != UDS_SUCCESS) {
		return vdo_log_fatal_strerror(result,
					      "cannot rebuild index: unknown volume chapter boundaries");
	}

	if (is_empty) {
		index->newest_virtual_chapter = 0;
		index->oldest_virtual_chapter = 0;
		index->volume->lookup_mode = LOOKUP_NORMAL;
		return UDS_SUCCESS;
	}

	index->newest_virtual_chapter = highest + 1;
	index->oldest_virtual_chapter = lowest;
	if (index->newest_virtual_chapter ==
	    (index->oldest_virtual_chapter + chapters_per_volume)) {
		/* Skip the chapter shadowed by the open chapter. */
		index->oldest_virtual_chapter++;
	}

	result = replay_volume(index);
	if (result != UDS_SUCCESS)
		return result;

	index->volume->lookup_mode = LOOKUP_NORMAL;
	return UDS_SUCCESS;
}

static void free_index_zone(struct index_zone *zone)
{
	if (zone == NULL)
		return;

	uds_free_open_chapter(zone->open_chapter);
	uds_free_open_chapter(zone->writing_chapter);
	vdo_free(zone);
}

static int make_index_zone(struct uds_index *index, unsigned int zone_number)
{
	int result;
	struct index_zone *zone;

	result = vdo_allocate(1, struct index_zone, "index zone", &zone);
	if (result != VDO_SUCCESS)
		return result;

	result = uds_make_open_chapter(index->volume->geometry, index->zone_count,
				       &zone->open_chapter);
	if (result != UDS_SUCCESS) {
		free_index_zone(zone);
		return result;
	}

	result = uds_make_open_chapter(index->volume->geometry, index->zone_count,
				       &zone->writing_chapter);
	if (result != UDS_SUCCESS) {
		free_index_zone(zone);
		return result;
	}

	zone->index = index;
	zone->id = zone_number;
	index->zones[zone_number] = zone;

	return UDS_SUCCESS;
}

int uds_make_index(struct uds_configuration *config, enum uds_open_index_type open_type,
		   struct index_load_context *load_context, index_callback_fn callback,
		   struct uds_index **new_index)
{
	int result;
	bool loaded = false;
	bool new = (open_type == UDS_CREATE);
	struct uds_index *index = NULL;
	struct index_zone *zone;
	u64 nonce;
	unsigned int z;

	result = vdo_allocate_extended(struct uds_index, config->zone_count,
				       struct uds_request_queue *, "index", &index);
	if (result != VDO_SUCCESS)
		return result;

	index->zone_count = config->zone_count;

	result = uds_make_index_layout(config, new, &index->layout);
	if (result != UDS_SUCCESS) {
		uds_free_index(index);
		return result;
	}

	result = vdo_allocate(index->zone_count, struct index_zone *, "zones",
			      &index->zones);
	if (result != VDO_SUCCESS) {
		uds_free_index(index);
		return result;
	}

	result = uds_make_volume(config, index->layout, &index->volume);
	if (result != UDS_SUCCESS) {
		uds_free_index(index);
		return result;
	}

	index->volume->lookup_mode = LOOKUP_NORMAL;
	for (z = 0; z < index->zone_count; z++) {
		result = make_index_zone(index, z);
		if (result != UDS_SUCCESS) {
			uds_free_index(index);
			return vdo_log_error_strerror(result,
						      "Could not create index zone");
		}
	}

	nonce = uds_get_volume_nonce(index->layout);
	result = uds_make_volume_index(config, nonce, &index->volume_index);
	if (result != UDS_SUCCESS) {
		uds_free_index(index);
		return vdo_log_error_strerror(result, "could not make volume index");
	}

	index->load_context = load_context;
	index->callback = callback;

	result = initialize_index_queues(index, config->geometry);
	if (result != UDS_SUCCESS) {
		uds_free_index(index);
		return result;
	}

	result = make_chapter_writer(index, &index->chapter_writer);
	if (result != UDS_SUCCESS) {
		uds_free_index(index);
		return result;
	}

	if (!new) {
		result = load_index(index);
		switch (result) {
		case UDS_SUCCESS:
			loaded = true;
			break;
		case -ENOMEM:
			/* We should not try a rebuild for this error. */
			vdo_log_error_strerror(result, "index could not be loaded");
			break;
		default:
			vdo_log_error_strerror(result, "index could not be loaded");
			if (open_type == UDS_LOAD) {
				result = rebuild_index(index);
				if (result != UDS_SUCCESS) {
					vdo_log_error_strerror(result,
							       "index could not be rebuilt");
				}
			}
			break;
		}
	}

	if (result != UDS_SUCCESS) {
		uds_free_index(index);
		return vdo_log_error_strerror(result, "fatal error in %s()", __func__);
	}

	for (z = 0; z < index->zone_count; z++) {
		zone = index->zones[z];
		zone->oldest_virtual_chapter = index->oldest_virtual_chapter;
		zone->newest_virtual_chapter = index->newest_virtual_chapter;
	}

	if (index->load_context != NULL) {
		mutex_lock(&index->load_context->mutex);
		index->load_context->status = INDEX_READY;
		/*
		 * If we get here, suspend is meaningless, but notify any thread trying to suspend
		 * us so it doesn't hang.
		 */
		uds_broadcast_cond(&index->load_context->cond);
		mutex_unlock(&index->load_context->mutex);
	}

	index->has_saved_open_chapter = loaded;
	index->need_to_save = !loaded;
	*new_index = index;
	return UDS_SUCCESS;
}

void uds_free_index(struct uds_index *index)
{
	unsigned int i;

	if (index == NULL)
		return;

	uds_request_queue_finish(index->triage_queue);
	for (i = 0; i < index->zone_count; i++)
		uds_request_queue_finish(index->zone_queues[i]);

	free_chapter_writer(index->chapter_writer);

	uds_free_volume_index(index->volume_index);
	if (index->zones != NULL) {
		for (i = 0; i < index->zone_count; i++)
			free_index_zone(index->zones[i]);
		vdo_free(index->zones);
	}

	uds_free_volume(index->volume);
	uds_free_index_layout(vdo_forget(index->layout));
	vdo_free(index);
}

/* Wait for the chapter writer to complete any outstanding writes. */
void uds_wait_for_idle_index(struct uds_index *index)
{
	struct chapter_writer *writer = index->chapter_writer;

	mutex_lock(&writer->mutex);
	while (writer->zones_to_write > 0)
		uds_wait_cond(&writer->cond, &writer->mutex);
	mutex_unlock(&writer->mutex);
}

/* This function assumes that all requests have been drained. */
int uds_save_index(struct uds_index *index)
{
	int result;

	if (!index->need_to_save)
		return UDS_SUCCESS;

	uds_wait_for_idle_index(index);
	index->prev_save = index->last_save;
	index->last_save = ((index->newest_virtual_chapter == 0) ?
			    NO_LAST_SAVE : index->newest_virtual_chapter - 1);
	vdo_log_info("beginning save (vcn %llu)", (unsigned long long) index->last_save);

	result = uds_save_index_state(index->layout, index);
	if (result != UDS_SUCCESS) {
		vdo_log_info("save index failed");
		index->last_save = index->prev_save;
	} else {
		index->has_saved_open_chapter = true;
		index->need_to_save = false;
		vdo_log_info("finished save (vcn %llu)",
			     (unsigned long long) index->last_save);
	}

	return result;
}

int uds_replace_index_storage(struct uds_index *index, struct block_device *bdev)
{
	return uds_replace_volume_storage(index->volume, index->layout, bdev);
}

/* Accessing statistics should be safe from any thread. */
void uds_get_index_stats(struct uds_index *index, struct uds_index_stats *counters)
{
	struct volume_index_stats stats;

	uds_get_volume_index_stats(index->volume_index, &stats);
	counters->entries_indexed = stats.record_count;
	counters->collisions = stats.collision_count;
	counters->entries_discarded = stats.discard_count;

	counters->memory_used = (index->volume_index->memory_size +
				 index->volume->cache_size +
				 index->chapter_writer->memory_size);
}

void uds_enqueue_request(struct uds_request *request, enum request_stage stage)
{
	struct uds_index *index = request->index;
	struct uds_request_queue *queue;

	switch (stage) {
	case STAGE_TRIAGE:
		if (index->triage_queue != NULL) {
			queue = index->triage_queue;
			break;
		}

		fallthrough;

	case STAGE_INDEX:
		request->zone_number =
			uds_get_volume_index_zone(index->volume_index, &request->record_name);
		fallthrough;

	case STAGE_MESSAGE:
		queue = index->zone_queues[request->zone_number];
		break;

	default:
		VDO_ASSERT_LOG_ONLY(false, "invalid index stage: %d", stage);
		return;
	}

	uds_request_queue_enqueue(queue, request);
}
