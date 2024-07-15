/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_INDEX_H
#define UDS_INDEX_H

#include "index-layout.h"
#include "index-session.h"
#include "open-chapter.h"
#include "volume.h"
#include "volume-index.h"

/*
 * The index is a high-level structure which represents the totality of the UDS index. It manages
 * the queues for incoming requests and dispatches them to the appropriate sub-components like the
 * volume or the volume index. It also manages administrative tasks such as saving and loading the
 * index.
 *
 * The index is divided into a number of independent zones and assigns each request to a zone based
 * on its name. Most sub-components are similarly divided into zones as well so that requests in
 * each zone usually operate without interference or coordination between zones.
 */

typedef void (*index_callback_fn)(struct uds_request *request);

struct index_zone {
	struct uds_index *index;
	struct open_chapter_zone *open_chapter;
	struct open_chapter_zone *writing_chapter;
	u64 oldest_virtual_chapter;
	u64 newest_virtual_chapter;
	unsigned int id;
};

struct uds_index {
	bool has_saved_open_chapter;
	bool need_to_save;
	struct index_load_context *load_context;
	struct index_layout *layout;
	struct volume_index *volume_index;
	struct volume *volume;
	unsigned int zone_count;
	struct index_zone **zones;

	u64 oldest_virtual_chapter;
	u64 newest_virtual_chapter;

	u64 last_save;
	u64 prev_save;
	struct chapter_writer *chapter_writer;

	index_callback_fn callback;
	struct uds_request_queue *triage_queue;
	struct uds_request_queue *zone_queues[];
};

enum request_stage {
	STAGE_TRIAGE,
	STAGE_INDEX,
	STAGE_MESSAGE,
};

int __must_check uds_make_index(struct uds_configuration *config,
				enum uds_open_index_type open_type,
				struct index_load_context *load_context,
				index_callback_fn callback, struct uds_index **new_index);

int __must_check uds_save_index(struct uds_index *index);

void uds_free_index(struct uds_index *index);

int __must_check uds_replace_index_storage(struct uds_index *index,
					   struct block_device *bdev);

void uds_get_index_stats(struct uds_index *index, struct uds_index_stats *counters);

void uds_enqueue_request(struct uds_request *request, enum request_stage stage);

void uds_wait_for_idle_index(struct uds_index *index);

#endif /* UDS_INDEX_H */
