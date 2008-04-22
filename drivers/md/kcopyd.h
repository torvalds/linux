/*
 * Copyright (C) 2001 Sistina Software
 *
 * This file is released under the GPL.
 *
 * Kcopyd provides a simple interface for copying an area of one
 * block-device to one or more other block-devices, with an asynchronous
 * completion notification.
 */

#ifndef DM_KCOPYD_H
#define DM_KCOPYD_H

#include "dm-io.h"

/* FIXME: make this configurable */
#define KCOPYD_MAX_REGIONS 8

#define KCOPYD_IGNORE_ERROR 1

/*
 * To use kcopyd you must first create a kcopyd client object.
 */
struct kcopyd_client;
int kcopyd_client_create(unsigned int num_pages, struct kcopyd_client **result);
void kcopyd_client_destroy(struct kcopyd_client *kc);

/*
 * Submit a copy job to kcopyd.  This is built on top of the
 * previous three fns.
 *
 * read_err is a boolean,
 * write_err is a bitset, with 1 bit for each destination region
 */
typedef void (*kcopyd_notify_fn)(int read_err, unsigned long write_err,
				 void *context);

int kcopyd_copy(struct kcopyd_client *kc, struct io_region *from,
		unsigned int num_dests, struct io_region *dests,
		unsigned int flags, kcopyd_notify_fn fn, void *context);

#endif
