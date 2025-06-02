/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_IO_SUBMITTER_H
#define VDO_IO_SUBMITTER_H

#include <linux/bio.h>

#include "constants.h"
#include "types.h"

struct io_submitter;

int vdo_make_io_submitter(unsigned int thread_count, unsigned int rotation_interval,
			  unsigned int max_requests_active, struct vdo *vdo,
			  struct io_submitter **io_submitter);

void vdo_cleanup_io_submitter(struct io_submitter *io_submitter);

void vdo_free_io_submitter(struct io_submitter *io_submitter);

void vdo_submit_vio(struct vdo_completion *completion);

void vdo_submit_data_vio(struct data_vio *data_vio);

void __submit_metadata_vio(struct vio *vio, physical_block_number_t physical,
			   bio_end_io_t callback, vdo_action_fn error_handler,
			   blk_opf_t operation, char *data, int size);

static inline void vdo_submit_metadata_vio(struct vio *vio, physical_block_number_t physical,
					   bio_end_io_t callback, vdo_action_fn error_handler,
					   blk_opf_t operation)
{
	__submit_metadata_vio(vio, physical, callback, error_handler,
			      operation, vio->data, vio->block_count * VDO_BLOCK_SIZE);
}

static inline void vdo_submit_metadata_vio_with_size(struct vio *vio,
						     physical_block_number_t physical,
						     bio_end_io_t callback,
						     vdo_action_fn error_handler,
						     blk_opf_t operation,
						     int size)
{
	__submit_metadata_vio(vio, physical, callback, error_handler,
			      operation, vio->data, size);
}

static inline void vdo_submit_flush_vio(struct vio *vio, bio_end_io_t callback,
					vdo_action_fn error_handler)
{
	/* FIXME: Can we just use REQ_OP_FLUSH? */
	__submit_metadata_vio(vio, 0, callback, error_handler,
			      REQ_OP_WRITE | REQ_PREFLUSH, NULL, 0);
}

#endif /* VDO_IO_SUBMITTER_H */
