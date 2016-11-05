/**
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef VCHIQ_UTIL_H
#define VCHIQ_UTIL_H

#include <linux/types.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/vmalloc.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>
#include <linux/time.h>  /* for time_t */
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "vchiq_if.h"

typedef struct {
	int size;
	int read;
	int write;
	int initialized;

	struct semaphore pop;
	struct semaphore push;

	VCHIQ_HEADER_T **storage;
} VCHIU_QUEUE_T;

extern int  vchiu_queue_init(VCHIU_QUEUE_T *queue, int size);
extern void vchiu_queue_delete(VCHIU_QUEUE_T *queue);

extern int vchiu_queue_is_empty(VCHIU_QUEUE_T *queue);
extern int vchiu_queue_is_full(VCHIU_QUEUE_T *queue);

extern void vchiu_queue_push(VCHIU_QUEUE_T *queue, VCHIQ_HEADER_T *header);

extern VCHIQ_HEADER_T *vchiu_queue_peek(VCHIU_QUEUE_T *queue);
extern VCHIQ_HEADER_T *vchiu_queue_pop(VCHIU_QUEUE_T *queue);

#endif
