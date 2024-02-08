/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_REQUEST_QUEUE_H
#define UDS_REQUEST_QUEUE_H

#include "indexer.h"

/*
 * A simple request queue which will handle new requests in the order in which they are received,
 * and will attempt to handle requeued requests before new ones. However, the nature of the
 * implementation means that it cannot guarantee this ordering; the prioritization is merely a
 * hint.
 */

struct uds_request_queue;

typedef void (*uds_request_queue_processor_fn)(struct uds_request *);

int __must_check uds_make_request_queue(const char *queue_name,
					uds_request_queue_processor_fn processor,
					struct uds_request_queue **queue_ptr);

void uds_request_queue_enqueue(struct uds_request_queue *queue,
			       struct uds_request *request);

void uds_request_queue_finish(struct uds_request_queue *queue);

#endif /* UDS_REQUEST_QUEUE_H */
