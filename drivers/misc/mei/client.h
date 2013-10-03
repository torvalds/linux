/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef _MEI_CLIENT_H_
#define _MEI_CLIENT_H_

#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/poll.h>
#include <linux/mei.h>

#include "mei_dev.h"

int mei_me_cl_by_uuid(const struct mei_device *dev, const uuid_le *cuuid);
int mei_me_cl_by_id(struct mei_device *dev, u8 client_id);

/*
 * MEI IO Functions
 */
struct mei_cl_cb *mei_io_cb_init(struct mei_cl *cl, struct file *fp);
void mei_io_cb_free(struct mei_cl_cb *priv_cb);
int mei_io_cb_alloc_req_buf(struct mei_cl_cb *cb, size_t length);
int mei_io_cb_alloc_resp_buf(struct mei_cl_cb *cb, size_t length);


/**
 * mei_io_list_init - Sets up a queue list.
 *
 * @list: An instance cl callback structure
 */
static inline void mei_io_list_init(struct mei_cl_cb *list)
{
	INIT_LIST_HEAD(&list->list);
}
void mei_io_list_flush(struct mei_cl_cb *list, struct mei_cl *cl);

/*
 * MEI Host Client Functions
 */

struct mei_cl *mei_cl_allocate(struct mei_device *dev);
void mei_cl_init(struct mei_cl *cl, struct mei_device *dev);


int mei_cl_link(struct mei_cl *cl, int id);
int mei_cl_unlink(struct mei_cl *cl);

int mei_cl_flush_queues(struct mei_cl *cl);
struct mei_cl_cb *mei_cl_find_read_cb(struct mei_cl *cl);

/**
 * mei_cl_cmp_id - tells if file private data have same id
 *
 * @fe1: private data of 1. file object
 * @fe2: private data of 2. file object
 *
 * returns true  - if ids are the same and not NULL
 */
static inline bool mei_cl_cmp_id(const struct mei_cl *cl1,
				const struct mei_cl *cl2)
{
	return cl1 && cl2 &&
		(cl1->host_client_id == cl2->host_client_id) &&
		(cl1->me_client_id == cl2->me_client_id);
}


int mei_cl_flow_ctrl_creds(struct mei_cl *cl);

int mei_cl_flow_ctrl_reduce(struct mei_cl *cl);
/*
 *  MEI input output function prototype
 */
static inline bool mei_cl_is_connected(struct mei_cl *cl)
{
	return (cl->dev &&
		cl->dev->dev_state == MEI_DEV_ENABLED &&
		cl->state == MEI_FILE_CONNECTED);
}
static inline bool mei_cl_is_transitioning(struct mei_cl *cl)
{
	return (MEI_FILE_INITIALIZING == cl->state ||
		MEI_FILE_DISCONNECTED == cl->state ||
		MEI_FILE_DISCONNECTING == cl->state);
}

bool mei_cl_is_other_connecting(struct mei_cl *cl);
int mei_cl_disconnect(struct mei_cl *cl);
int mei_cl_connect(struct mei_cl *cl, struct file *file);
int mei_cl_read_start(struct mei_cl *cl, size_t length);
int mei_cl_write(struct mei_cl *cl, struct mei_cl_cb *cb, bool blocking);
int mei_cl_irq_write_complete(struct mei_cl *cl, struct mei_cl_cb *cb,
				s32 *slots, struct mei_cl_cb *cmpl_list);

void mei_cl_complete(struct mei_cl *cl, struct mei_cl_cb *cb);

void mei_host_client_init(struct work_struct *work);



void mei_cl_all_disconnect(struct mei_device *dev);
void mei_cl_all_wakeup(struct mei_device *dev);
void mei_cl_all_write_clear(struct mei_device *dev);

#endif /* _MEI_CLIENT_H_ */
