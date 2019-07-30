/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHIQ_UTIL_H
#define VCHIQ_UTIL_H

#include <linux/types.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/vmalloc.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/random.h>
#include <linux/sched/signal.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>
#include <linux/time.h>  /* for time_t */
#include <linux/slab.h>

#include "vchiq_if.h"

struct vchiu_queue {
	int size;
	int read;
	int write;
	int initialized;

	struct completion pop;
	struct completion push;

	struct vchiq_header **storage;
};

extern int  vchiu_queue_init(struct vchiu_queue *queue, int size);
extern void vchiu_queue_delete(struct vchiu_queue *queue);

extern int vchiu_queue_is_empty(struct vchiu_queue *queue);

extern void vchiu_queue_push(struct vchiu_queue *queue,
			     struct vchiq_header *header);

extern struct vchiq_header *vchiu_queue_peek(struct vchiu_queue *queue);
extern struct vchiq_header *vchiu_queue_pop(struct vchiu_queue *queue);

#endif
