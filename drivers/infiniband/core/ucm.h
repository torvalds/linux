/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: ucm.h 2208 2005-04-22 23:24:31Z libor $
 */

#ifndef UCM_H
#define UCM_H

#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/idr.h>

#include <rdma/ib_cm.h>
#include <rdma/ib_user_cm.h>

struct ib_ucm_file {
	struct semaphore mutex;
	struct file *filp;

	struct list_head  ctxs;   /* list of active connections */
	struct list_head  events; /* list of pending events */
	wait_queue_head_t poll_wait;
};

struct ib_ucm_context {
	int                 id;
	wait_queue_head_t   wait;
	atomic_t            ref;
	int		    events_reported;

	struct ib_ucm_file *file;
	struct ib_cm_id    *cm_id;
	__u64		   uid;

	struct list_head    events;    /* list of pending events. */
	struct list_head    file_list; /* member in file ctx list */
};

struct ib_ucm_event {
	struct ib_ucm_context *ctx;
	struct list_head file_list; /* member in file event list */
	struct list_head ctx_list;  /* member in ctx event list */

	struct ib_cm_id *cm_id;
	struct ib_ucm_event_resp resp;
	void *data;
	void *info;
	int data_len;
	int info_len;
};

#endif /* UCM_H */
