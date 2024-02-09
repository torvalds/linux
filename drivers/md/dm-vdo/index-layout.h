/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_INDEX_LAYOUT_H
#define UDS_INDEX_LAYOUT_H

#include "config.h"
#include "indexer.h"
#include "io-factory.h"

/*
 * The index layout describes the format of the index on the underlying storage, and is responsible
 * for creating those structures when the index is first created. It also validates the index data
 * when loading a saved index, and updates it when saving the index.
 */

struct index_layout;

int __must_check uds_make_index_layout(struct uds_configuration *config, bool new_layout,
				       struct index_layout **layout_ptr);

void uds_free_index_layout(struct index_layout *layout);

int __must_check uds_replace_index_layout_storage(struct index_layout *layout,
						  struct block_device *bdev);

int __must_check uds_load_index_state(struct index_layout *layout,
				      struct uds_index *index);

int __must_check uds_save_index_state(struct index_layout *layout,
				      struct uds_index *index);

int __must_check uds_discard_open_chapter(struct index_layout *layout);

u64 __must_check uds_get_volume_nonce(struct index_layout *layout);

int __must_check uds_open_volume_bufio(struct index_layout *layout, size_t block_size,
				       unsigned int reserved_buffers,
				       struct dm_bufio_client **client_ptr);

#endif /* UDS_INDEX_LAYOUT_H */
