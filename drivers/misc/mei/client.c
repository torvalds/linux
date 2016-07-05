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

#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hbm.h"
#include "client.h"

/**
 * mei_me_cl_init - initialize me client
 *
 * @me_cl: me client
 */
void mei_me_cl_init(struct mei_me_client *me_cl)
{
	INIT_LIST_HEAD(&me_cl->list);
	kref_init(&me_cl->refcnt);
}

/**
 * mei_me_cl_get - increases me client refcount
 *
 * @me_cl: me client
 *
 * Locking: called under "dev->device_lock" lock
 *
 * Return: me client or NULL
 */
struct mei_me_client *mei_me_cl_get(struct mei_me_client *me_cl)
{
	if (me_cl && kref_get_unless_zero(&me_cl->refcnt))
		return me_cl;

	return NULL;
}

/**
 * mei_me_cl_release - free me client
 *
 * Locking: called under "dev->device_lock" lock
 *
 * @ref: me_client refcount
 */
static void mei_me_cl_release(struct kref *ref)
{
	struct mei_me_client *me_cl =
		container_of(ref, struct mei_me_client, refcnt);

	kfree(me_cl);
}

/**
 * mei_me_cl_put - decrease me client refcount and free client if necessary
 *
 * Locking: called under "dev->device_lock" lock
 *
 * @me_cl: me client
 */
void mei_me_cl_put(struct mei_me_client *me_cl)
{
	if (me_cl)
		kref_put(&me_cl->refcnt, mei_me_cl_release);
}

/**
 * __mei_me_cl_del  - delete me client from the list and decrease
 *     reference counter
 *
 * @dev: mei device
 * @me_cl: me client
 *
 * Locking: dev->me_clients_rwsem
 */
static void __mei_me_cl_del(struct mei_device *dev, struct mei_me_client *me_cl)
{
	if (!me_cl)
		return;

	list_del_init(&me_cl->list);
	mei_me_cl_put(me_cl);
}

/**
 * mei_me_cl_del - delete me client from the list and decrease
 *     reference counter
 *
 * @dev: mei device
 * @me_cl: me client
 */
void mei_me_cl_del(struct mei_device *dev, struct mei_me_client *me_cl)
{
	down_write(&dev->me_clients_rwsem);
	__mei_me_cl_del(dev, me_cl);
	up_write(&dev->me_clients_rwsem);
}

/**
 * mei_me_cl_add - add me client to the list
 *
 * @dev: mei device
 * @me_cl: me client
 */
void mei_me_cl_add(struct mei_device *dev, struct mei_me_client *me_cl)
{
	down_write(&dev->me_clients_rwsem);
	list_add(&me_cl->list, &dev->me_clients);
	up_write(&dev->me_clients_rwsem);
}

/**
 * __mei_me_cl_by_uuid - locate me client by uuid
 *	increases ref count
 *
 * @dev: mei device
 * @uuid: me client uuid
 *
 * Return: me client or NULL if not found
 *
 * Locking: dev->me_clients_rwsem
 */
static struct mei_me_client *__mei_me_cl_by_uuid(struct mei_device *dev,
					const uuid_le *uuid)
{
	struct mei_me_client *me_cl;
	const uuid_le *pn;

	WARN_ON(!rwsem_is_locked(&dev->me_clients_rwsem));

	list_for_each_entry(me_cl, &dev->me_clients, list) {
		pn = &me_cl->props.protocol_name;
		if (uuid_le_cmp(*uuid, *pn) == 0)
			return mei_me_cl_get(me_cl);
	}

	return NULL;
}

/**
 * mei_me_cl_by_uuid - locate me client by uuid
 *	increases ref count
 *
 * @dev: mei device
 * @uuid: me client uuid
 *
 * Return: me client or NULL if not found
 *
 * Locking: dev->me_clients_rwsem
 */
struct mei_me_client *mei_me_cl_by_uuid(struct mei_device *dev,
					const uuid_le *uuid)
{
	struct mei_me_client *me_cl;

	down_read(&dev->me_clients_rwsem);
	me_cl = __mei_me_cl_by_uuid(dev, uuid);
	up_read(&dev->me_clients_rwsem);

	return me_cl;
}

/**
 * mei_me_cl_by_id - locate me client by client id
 *	increases ref count
 *
 * @dev: the device structure
 * @client_id: me client id
 *
 * Return: me client or NULL if not found
 *
 * Locking: dev->me_clients_rwsem
 */
struct mei_me_client *mei_me_cl_by_id(struct mei_device *dev, u8 client_id)
{

	struct mei_me_client *__me_cl, *me_cl = NULL;

	down_read(&dev->me_clients_rwsem);
	list_for_each_entry(__me_cl, &dev->me_clients, list) {
		if (__me_cl->client_id == client_id) {
			me_cl = mei_me_cl_get(__me_cl);
			break;
		}
	}
	up_read(&dev->me_clients_rwsem);

	return me_cl;
}

/**
 * __mei_me_cl_by_uuid_id - locate me client by client id and uuid
 *	increases ref count
 *
 * @dev: the device structure
 * @uuid: me client uuid
 * @client_id: me client id
 *
 * Return: me client or null if not found
 *
 * Locking: dev->me_clients_rwsem
 */
static struct mei_me_client *__mei_me_cl_by_uuid_id(struct mei_device *dev,
					   const uuid_le *uuid, u8 client_id)
{
	struct mei_me_client *me_cl;
	const uuid_le *pn;

	WARN_ON(!rwsem_is_locked(&dev->me_clients_rwsem));

	list_for_each_entry(me_cl, &dev->me_clients, list) {
		pn = &me_cl->props.protocol_name;
		if (uuid_le_cmp(*uuid, *pn) == 0 &&
		    me_cl->client_id == client_id)
			return mei_me_cl_get(me_cl);
	}

	return NULL;
}


/**
 * mei_me_cl_by_uuid_id - locate me client by client id and uuid
 *	increases ref count
 *
 * @dev: the device structure
 * @uuid: me client uuid
 * @client_id: me client id
 *
 * Return: me client or null if not found
 */
struct mei_me_client *mei_me_cl_by_uuid_id(struct mei_device *dev,
					   const uuid_le *uuid, u8 client_id)
{
	struct mei_me_client *me_cl;

	down_read(&dev->me_clients_rwsem);
	me_cl = __mei_me_cl_by_uuid_id(dev, uuid, client_id);
	up_read(&dev->me_clients_rwsem);

	return me_cl;
}

/**
 * mei_me_cl_rm_by_uuid - remove all me clients matching uuid
 *
 * @dev: the device structure
 * @uuid: me client uuid
 *
 * Locking: called under "dev->device_lock" lock
 */
void mei_me_cl_rm_by_uuid(struct mei_device *dev, const uuid_le *uuid)
{
	struct mei_me_client *me_cl;

	dev_dbg(dev->dev, "remove %pUl\n", uuid);

	down_write(&dev->me_clients_rwsem);
	me_cl = __mei_me_cl_by_uuid(dev, uuid);
	__mei_me_cl_del(dev, me_cl);
	up_write(&dev->me_clients_rwsem);
}

/**
 * mei_me_cl_rm_by_uuid_id - remove all me clients matching client id
 *
 * @dev: the device structure
 * @uuid: me client uuid
 * @id: me client id
 *
 * Locking: called under "dev->device_lock" lock
 */
void mei_me_cl_rm_by_uuid_id(struct mei_device *dev, const uuid_le *uuid, u8 id)
{
	struct mei_me_client *me_cl;

	dev_dbg(dev->dev, "remove %pUl %d\n", uuid, id);

	down_write(&dev->me_clients_rwsem);
	me_cl = __mei_me_cl_by_uuid_id(dev, uuid, id);
	__mei_me_cl_del(dev, me_cl);
	up_write(&dev->me_clients_rwsem);
}

/**
 * mei_me_cl_rm_all - remove all me clients
 *
 * @dev: the device structure
 *
 * Locking: called under "dev->device_lock" lock
 */
void mei_me_cl_rm_all(struct mei_device *dev)
{
	struct mei_me_client *me_cl, *next;

	down_write(&dev->me_clients_rwsem);
	list_for_each_entry_safe(me_cl, next, &dev->me_clients, list)
		__mei_me_cl_del(dev, me_cl);
	up_write(&dev->me_clients_rwsem);
}

/**
 * mei_cl_cmp_id - tells if the clients are the same
 *
 * @cl1: host client 1
 * @cl2: host client 2
 *
 * Return: true  - if the clients has same host and me ids
 *         false - otherwise
 */
static inline bool mei_cl_cmp_id(const struct mei_cl *cl1,
				const struct mei_cl *cl2)
{
	return cl1 && cl2 &&
		(cl1->host_client_id == cl2->host_client_id) &&
		(mei_cl_me_id(cl1) == mei_cl_me_id(cl2));
}

/**
 * mei_io_cb_free - free mei_cb_private related memory
 *
 * @cb: mei callback struct
 */
void mei_io_cb_free(struct mei_cl_cb *cb)
{
	if (cb == NULL)
		return;

	list_del(&cb->list);
	kfree(cb->buf.data);
	kfree(cb);
}

/**
 * mei_io_cb_init - allocate and initialize io callback
 *
 * @cl: mei client
 * @type: operation type
 * @fp: pointer to file structure
 *
 * Return: mei_cl_cb pointer or NULL;
 */
struct mei_cl_cb *mei_io_cb_init(struct mei_cl *cl, enum mei_cb_file_ops type,
				 struct file *fp)
{
	struct mei_cl_cb *cb;

	cb = kzalloc(sizeof(struct mei_cl_cb), GFP_KERNEL);
	if (!cb)
		return NULL;

	INIT_LIST_HEAD(&cb->list);
	cb->file_object = fp;
	cb->cl = cl;
	cb->buf_idx = 0;
	cb->fop_type = type;
	return cb;
}

/**
 * __mei_io_list_flush - removes and frees cbs belonging to cl.
 *
 * @list:  an instance of our list structure
 * @cl:    host client, can be NULL for flushing the whole list
 * @free:  whether to free the cbs
 */
static void __mei_io_list_flush(struct mei_cl_cb *list,
				struct mei_cl *cl, bool free)
{
	struct mei_cl_cb *cb, *next;

	/* enable removing everything if no cl is specified */
	list_for_each_entry_safe(cb, next, &list->list, list) {
		if (!cl || mei_cl_cmp_id(cl, cb->cl)) {
			list_del_init(&cb->list);
			if (free)
				mei_io_cb_free(cb);
		}
	}
}

/**
 * mei_io_list_flush - removes list entry belonging to cl.
 *
 * @list:  An instance of our list structure
 * @cl: host client
 */
void mei_io_list_flush(struct mei_cl_cb *list, struct mei_cl *cl)
{
	__mei_io_list_flush(list, cl, false);
}

/**
 * mei_io_list_free - removes cb belonging to cl and free them
 *
 * @list:  An instance of our list structure
 * @cl: host client
 */
static inline void mei_io_list_free(struct mei_cl_cb *list, struct mei_cl *cl)
{
	__mei_io_list_flush(list, cl, true);
}

/**
 * mei_io_cb_alloc_buf - allocate callback buffer
 *
 * @cb: io callback structure
 * @length: size of the buffer
 *
 * Return: 0 on success
 *         -EINVAL if cb is NULL
 *         -ENOMEM if allocation failed
 */
int mei_io_cb_alloc_buf(struct mei_cl_cb *cb, size_t length)
{
	if (!cb)
		return -EINVAL;

	if (length == 0)
		return 0;

	cb->buf.data = kmalloc(length, GFP_KERNEL);
	if (!cb->buf.data)
		return -ENOMEM;
	cb->buf.size = length;
	return 0;
}

/**
 * mei_cl_alloc_cb - a convenient wrapper for allocating read cb
 *
 * @cl: host client
 * @length: size of the buffer
 * @type: operation type
 * @fp: associated file pointer (might be NULL)
 *
 * Return: cb on success and NULL on failure
 */
struct mei_cl_cb *mei_cl_alloc_cb(struct mei_cl *cl, size_t length,
				  enum mei_cb_file_ops type, struct file *fp)
{
	struct mei_cl_cb *cb;

	cb = mei_io_cb_init(cl, type, fp);
	if (!cb)
		return NULL;

	if (mei_io_cb_alloc_buf(cb, length)) {
		mei_io_cb_free(cb);
		return NULL;
	}

	return cb;
}

/**
 * mei_cl_read_cb - find this cl's callback in the read list
 *     for a specific file
 *
 * @cl: host client
 * @fp: file pointer (matching cb file object), may be NULL
 *
 * Return: cb on success, NULL if cb is not found
 */
struct mei_cl_cb *mei_cl_read_cb(const struct mei_cl *cl, const struct file *fp)
{
	struct mei_cl_cb *cb;

	list_for_each_entry(cb, &cl->rd_completed, list)
		if (!fp || fp == cb->file_object)
			return cb;

	return NULL;
}

/**
 * mei_cl_read_cb_flush - free client's read pending and completed cbs
 *   for a specific file
 *
 * @cl: host client
 * @fp: file pointer (matching cb file object), may be NULL
 */
void mei_cl_read_cb_flush(const struct mei_cl *cl, const struct file *fp)
{
	struct mei_cl_cb *cb, *next;

	list_for_each_entry_safe(cb, next, &cl->rd_completed, list)
		if (!fp || fp == cb->file_object)
			mei_io_cb_free(cb);


	list_for_each_entry_safe(cb, next, &cl->rd_pending, list)
		if (!fp || fp == cb->file_object)
			mei_io_cb_free(cb);
}

/**
 * mei_cl_flush_queues - flushes queue lists belonging to cl.
 *
 * @cl: host client
 * @fp: file pointer (matching cb file object), may be NULL
 *
 * Return: 0 on success, -EINVAL if cl or cl->dev is NULL.
 */
int mei_cl_flush_queues(struct mei_cl *cl, const struct file *fp)
{
	struct mei_device *dev;

	if (WARN_ON(!cl || !cl->dev))
		return -EINVAL;

	dev = cl->dev;

	cl_dbg(dev, cl, "remove list entry belonging to cl\n");
	mei_io_list_free(&cl->dev->write_list, cl);
	mei_io_list_free(&cl->dev->write_waiting_list, cl);
	mei_io_list_flush(&cl->dev->ctrl_wr_list, cl);
	mei_io_list_flush(&cl->dev->ctrl_rd_list, cl);
	mei_io_list_flush(&cl->dev->amthif_cmd_list, cl);
	mei_io_list_flush(&cl->dev->amthif_rd_complete_list, cl);

	mei_cl_read_cb_flush(cl, fp);

	return 0;
}


/**
 * mei_cl_init - initializes cl.
 *
 * @cl: host client to be initialized
 * @dev: mei device
 */
void mei_cl_init(struct mei_cl *cl, struct mei_device *dev)
{
	memset(cl, 0, sizeof(struct mei_cl));
	init_waitqueue_head(&cl->wait);
	init_waitqueue_head(&cl->rx_wait);
	init_waitqueue_head(&cl->tx_wait);
	init_waitqueue_head(&cl->ev_wait);
	INIT_LIST_HEAD(&cl->rd_completed);
	INIT_LIST_HEAD(&cl->rd_pending);
	INIT_LIST_HEAD(&cl->link);
	cl->writing_state = MEI_IDLE;
	cl->state = MEI_FILE_INITIALIZING;
	cl->dev = dev;
}

/**
 * mei_cl_allocate - allocates cl  structure and sets it up.
 *
 * @dev: mei device
 * Return:  The allocated file or NULL on failure
 */
struct mei_cl *mei_cl_allocate(struct mei_device *dev)
{
	struct mei_cl *cl;

	cl = kmalloc(sizeof(struct mei_cl), GFP_KERNEL);
	if (!cl)
		return NULL;

	mei_cl_init(cl, dev);

	return cl;
}

/**
 * mei_cl_link - allocate host id in the host map
 *
 * @cl: host client
 * @id: fixed host id or MEI_HOST_CLIENT_ID_ANY (-1) for generic one
 *
 * Return: 0 on success
 *	-EINVAL on incorrect values
 *	-EMFILE if open count exceeded.
 */
int mei_cl_link(struct mei_cl *cl, int id)
{
	struct mei_device *dev;
	long open_handle_count;

	if (WARN_ON(!cl || !cl->dev))
		return -EINVAL;

	dev = cl->dev;

	/* If Id is not assigned get one*/
	if (id == MEI_HOST_CLIENT_ID_ANY)
		id = find_first_zero_bit(dev->host_clients_map,
					MEI_CLIENTS_MAX);

	if (id >= MEI_CLIENTS_MAX) {
		dev_err(dev->dev, "id exceeded %d", MEI_CLIENTS_MAX);
		return -EMFILE;
	}

	open_handle_count = dev->open_handle_count + dev->iamthif_open_count;
	if (open_handle_count >= MEI_MAX_OPEN_HANDLE_COUNT) {
		dev_err(dev->dev, "open_handle_count exceeded %d",
			MEI_MAX_OPEN_HANDLE_COUNT);
		return -EMFILE;
	}

	dev->open_handle_count++;

	cl->host_client_id = id;
	list_add_tail(&cl->link, &dev->file_list);

	set_bit(id, dev->host_clients_map);

	cl->state = MEI_FILE_INITIALIZING;

	cl_dbg(dev, cl, "link cl\n");
	return 0;
}

/**
 * mei_cl_unlink - remove host client from the list
 *
 * @cl: host client
 *
 * Return: always 0
 */
int mei_cl_unlink(struct mei_cl *cl)
{
	struct mei_device *dev;

	/* don't shout on error exit path */
	if (!cl)
		return 0;

	/* wd and amthif might not be initialized */
	if (!cl->dev)
		return 0;

	dev = cl->dev;

	cl_dbg(dev, cl, "unlink client");

	if (dev->open_handle_count > 0)
		dev->open_handle_count--;

	/* never clear the 0 bit */
	if (cl->host_client_id)
		clear_bit(cl->host_client_id, dev->host_clients_map);

	list_del_init(&cl->link);

	cl->state = MEI_FILE_INITIALIZING;

	return 0;
}


void mei_host_client_init(struct work_struct *work)
{
	struct mei_device *dev =
		container_of(work, struct mei_device, init_work);
	struct mei_me_client *me_cl;

	mutex_lock(&dev->device_lock);


	me_cl = mei_me_cl_by_uuid(dev, &mei_amthif_guid);
	if (me_cl)
		mei_amthif_host_init(dev, me_cl);
	mei_me_cl_put(me_cl);

	me_cl = mei_me_cl_by_uuid(dev, &mei_wd_guid);
	if (me_cl)
		mei_wd_host_init(dev, me_cl);
	mei_me_cl_put(me_cl);

	dev->dev_state = MEI_DEV_ENABLED;
	dev->reset_count = 0;
	mutex_unlock(&dev->device_lock);

	mei_cl_bus_rescan(dev);

	pm_runtime_mark_last_busy(dev->dev);
	dev_dbg(dev->dev, "rpm: autosuspend\n");
	pm_runtime_autosuspend(dev->dev);
}

/**
 * mei_hbuf_acquire - try to acquire host buffer
 *
 * @dev: the device structure
 * Return: true if host buffer was acquired
 */
bool mei_hbuf_acquire(struct mei_device *dev)
{
	if (mei_pg_state(dev) == MEI_PG_ON ||
	    mei_pg_in_transition(dev)) {
		dev_dbg(dev->dev, "device is in pg\n");
		return false;
	}

	if (!dev->hbuf_is_ready) {
		dev_dbg(dev->dev, "hbuf is not ready\n");
		return false;
	}

	dev->hbuf_is_ready = false;

	return true;
}

/**
 * mei_cl_set_disconnected - set disconnected state and clear
 *   associated states and resources
 *
 * @cl: host client
 */
void mei_cl_set_disconnected(struct mei_cl *cl)
{
	struct mei_device *dev = cl->dev;

	if (cl->state == MEI_FILE_DISCONNECTED ||
	    cl->state == MEI_FILE_INITIALIZING)
		return;

	cl->state = MEI_FILE_DISCONNECTED;
	mei_io_list_flush(&dev->ctrl_rd_list, cl);
	mei_io_list_flush(&dev->ctrl_wr_list, cl);
	cl->mei_flow_ctrl_creds = 0;
	cl->timer_count = 0;

	if (!cl->me_cl)
		return;

	if (!WARN_ON(cl->me_cl->connect_count == 0))
		cl->me_cl->connect_count--;

	if (cl->me_cl->connect_count == 0)
		cl->me_cl->mei_flow_ctrl_creds = 0;

	mei_me_cl_put(cl->me_cl);
	cl->me_cl = NULL;
}

static int mei_cl_set_connecting(struct mei_cl *cl, struct mei_me_client *me_cl)
{
	if (!mei_me_cl_get(me_cl))
		return -ENOENT;

	/* only one connection is allowed for fixed address clients */
	if (me_cl->props.fixed_address) {
		if (me_cl->connect_count) {
			mei_me_cl_put(me_cl);
			return -EBUSY;
		}
	}

	cl->me_cl = me_cl;
	cl->state = MEI_FILE_CONNECTING;
	cl->me_cl->connect_count++;

	return 0;
}

/*
 * mei_cl_send_disconnect - send disconnect request
 *
 * @cl: host client
 * @cb: callback block
 *
 * Return: 0, OK; otherwise, error.
 */
static int mei_cl_send_disconnect(struct mei_cl *cl, struct mei_cl_cb *cb)
{
	struct mei_device *dev;
	int ret;

	dev = cl->dev;

	ret = mei_hbm_cl_disconnect_req(dev, cl);
	cl->status = ret;
	if (ret) {
		cl->state = MEI_FILE_DISCONNECT_REPLY;
		return ret;
	}

	list_move_tail(&cb->list, &dev->ctrl_rd_list.list);
	cl->timer_count = MEI_CONNECT_TIMEOUT;

	return 0;
}

/**
 * mei_cl_irq_disconnect - processes close related operation from
 *	interrupt thread context - send disconnect request
 *
 * @cl: client
 * @cb: callback block.
 * @cmpl_list: complete list.
 *
 * Return: 0, OK; otherwise, error.
 */
int mei_cl_irq_disconnect(struct mei_cl *cl, struct mei_cl_cb *cb,
			    struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	u32 msg_slots;
	int slots;
	int ret;

	msg_slots = mei_data2slots(sizeof(struct hbm_client_connect_request));
	slots = mei_hbuf_empty_slots(dev);

	if (slots < msg_slots)
		return -EMSGSIZE;

	ret = mei_cl_send_disconnect(cl, cb);
	if (ret)
		list_move_tail(&cb->list, &cmpl_list->list);

	return ret;
}

/**
 * __mei_cl_disconnect - disconnect host client from the me one
 *     internal function runtime pm has to be already acquired
 *
 * @cl: host client
 *
 * Return: 0 on success, <0 on failure.
 */
static int __mei_cl_disconnect(struct mei_cl *cl)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;
	int rets;

	dev = cl->dev;

	cl->state = MEI_FILE_DISCONNECTING;

	cb = mei_io_cb_init(cl, MEI_FOP_DISCONNECT, NULL);
	rets = cb ? 0 : -ENOMEM;
	if (rets)
		goto out;

	cl_dbg(dev, cl, "add disconnect cb to control write list\n");
	list_add_tail(&cb->list, &dev->ctrl_wr_list.list);

	if (mei_hbuf_acquire(dev)) {
		rets = mei_cl_send_disconnect(cl, cb);
		if (rets) {
			cl_err(dev, cl, "failed to disconnect.\n");
			goto out;
		}
	}

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(cl->wait, cl->state == MEI_FILE_DISCONNECT_REPLY,
			   mei_secs_to_jiffies(MEI_CL_CONNECT_TIMEOUT));
	mutex_lock(&dev->device_lock);

	rets = cl->status;
	if (cl->state != MEI_FILE_DISCONNECT_REPLY) {
		cl_dbg(dev, cl, "timeout on disconnect from FW client.\n");
		rets = -ETIME;
	}

out:
	/* we disconnect also on error */
	mei_cl_set_disconnected(cl);
	if (!rets)
		cl_dbg(dev, cl, "successfully disconnected from FW client.\n");

	mei_io_cb_free(cb);
	return rets;
}

/**
 * mei_cl_disconnect - disconnect host client from the me one
 *
 * @cl: host client
 *
 * Locking: called under "dev->device_lock" lock
 *
 * Return: 0 on success, <0 on failure.
 */
int mei_cl_disconnect(struct mei_cl *cl)
{
	struct mei_device *dev;
	int rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	cl_dbg(dev, cl, "disconnecting");

	if (!mei_cl_is_connected(cl))
		return 0;

	if (mei_cl_is_fixed_address(cl)) {
		mei_cl_set_disconnected(cl);
		return 0;
	}

	rets = pm_runtime_get(dev->dev);
	if (rets < 0 && rets != -EINPROGRESS) {
		pm_runtime_put_noidle(dev->dev);
		cl_err(dev, cl, "rpm: get failed %d\n", rets);
		return rets;
	}

	rets = __mei_cl_disconnect(cl);

	cl_dbg(dev, cl, "rpm: autosuspend\n");
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return rets;
}


/**
 * mei_cl_is_other_connecting - checks if other
 *    client with the same me client id is connecting
 *
 * @cl: private data of the file object
 *
 * Return: true if other client is connected, false - otherwise.
 */
static bool mei_cl_is_other_connecting(struct mei_cl *cl)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;

	dev = cl->dev;

	list_for_each_entry(cb, &dev->ctrl_rd_list.list, list) {
		if (cb->fop_type == MEI_FOP_CONNECT &&
		    mei_cl_me_id(cl) == mei_cl_me_id(cb->cl))
			return true;
	}

	return false;
}

/**
 * mei_cl_send_connect - send connect request
 *
 * @cl: host client
 * @cb: callback block
 *
 * Return: 0, OK; otherwise, error.
 */
static int mei_cl_send_connect(struct mei_cl *cl, struct mei_cl_cb *cb)
{
	struct mei_device *dev;
	int ret;

	dev = cl->dev;

	ret = mei_hbm_cl_connect_req(dev, cl);
	cl->status = ret;
	if (ret) {
		cl->state = MEI_FILE_DISCONNECT_REPLY;
		return ret;
	}

	list_move_tail(&cb->list, &dev->ctrl_rd_list.list);
	cl->timer_count = MEI_CONNECT_TIMEOUT;
	return 0;
}

/**
 * mei_cl_irq_connect - send connect request in irq_thread context
 *
 * @cl: host client
 * @cb: callback block
 * @cmpl_list: complete list
 *
 * Return: 0, OK; otherwise, error.
 */
int mei_cl_irq_connect(struct mei_cl *cl, struct mei_cl_cb *cb,
			      struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	u32 msg_slots;
	int slots;
	int rets;

	msg_slots = mei_data2slots(sizeof(struct hbm_client_connect_request));
	slots = mei_hbuf_empty_slots(dev);

	if (mei_cl_is_other_connecting(cl))
		return 0;

	if (slots < msg_slots)
		return -EMSGSIZE;

	rets = mei_cl_send_connect(cl, cb);
	if (rets)
		list_move_tail(&cb->list, &cmpl_list->list);

	return rets;
}

/**
 * mei_cl_connect - connect host client to the me one
 *
 * @cl: host client
 * @me_cl: me client
 * @file: pointer to file structure
 *
 * Locking: called under "dev->device_lock" lock
 *
 * Return: 0 on success, <0 on failure.
 */
int mei_cl_connect(struct mei_cl *cl, struct mei_me_client *me_cl,
		   struct file *file)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;
	int rets;

	if (WARN_ON(!cl || !cl->dev || !me_cl))
		return -ENODEV;

	dev = cl->dev;

	rets = mei_cl_set_connecting(cl, me_cl);
	if (rets)
		return rets;

	if (mei_cl_is_fixed_address(cl)) {
		cl->state = MEI_FILE_CONNECTED;
		return 0;
	}

	rets = pm_runtime_get(dev->dev);
	if (rets < 0 && rets != -EINPROGRESS) {
		pm_runtime_put_noidle(dev->dev);
		cl_err(dev, cl, "rpm: get failed %d\n", rets);
		goto nortpm;
	}

	cb = mei_io_cb_init(cl, MEI_FOP_CONNECT, file);
	rets = cb ? 0 : -ENOMEM;
	if (rets)
		goto out;

	list_add_tail(&cb->list, &dev->ctrl_wr_list.list);

	/* run hbuf acquire last so we don't have to undo */
	if (!mei_cl_is_other_connecting(cl) && mei_hbuf_acquire(dev)) {
		rets = mei_cl_send_connect(cl, cb);
		if (rets)
			goto out;
	}

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(cl->wait,
			(cl->state == MEI_FILE_CONNECTED ||
			 cl->state == MEI_FILE_DISCONNECT_REQUIRED ||
			 cl->state == MEI_FILE_DISCONNECT_REPLY),
			mei_secs_to_jiffies(MEI_CL_CONNECT_TIMEOUT));
	mutex_lock(&dev->device_lock);

	if (!mei_cl_is_connected(cl)) {
		if (cl->state == MEI_FILE_DISCONNECT_REQUIRED) {
			mei_io_list_flush(&dev->ctrl_rd_list, cl);
			mei_io_list_flush(&dev->ctrl_wr_list, cl);
			 /* ignore disconnect return valuue;
			  * in case of failure reset will be invoked
			  */
			__mei_cl_disconnect(cl);
			rets = -EFAULT;
			goto out;
		}

		/* timeout or something went really wrong */
		if (!cl->status)
			cl->status = -EFAULT;
	}

	rets = cl->status;
out:
	cl_dbg(dev, cl, "rpm: autosuspend\n");
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	mei_io_cb_free(cb);

nortpm:
	if (!mei_cl_is_connected(cl))
		mei_cl_set_disconnected(cl);

	return rets;
}

/**
 * mei_cl_alloc_linked - allocate and link host client
 *
 * @dev: the device structure
 * @id: fixed host id or MEI_HOST_CLIENT_ID_ANY (-1) for generic one
 *
 * Return: cl on success ERR_PTR on failure
 */
struct mei_cl *mei_cl_alloc_linked(struct mei_device *dev, int id)
{
	struct mei_cl *cl;
	int ret;

	cl = mei_cl_allocate(dev);
	if (!cl) {
		ret = -ENOMEM;
		goto err;
	}

	ret = mei_cl_link(cl, id);
	if (ret)
		goto err;

	return cl;
err:
	kfree(cl);
	return ERR_PTR(ret);
}



/**
 * mei_cl_flow_ctrl_creds - checks flow_control credits for cl.
 *
 * @cl: private data of the file object
 *
 * Return: 1 if mei_flow_ctrl_creds >0, 0 - otherwise.
 */
int mei_cl_flow_ctrl_creds(struct mei_cl *cl)
{
	int rets;

	if (WARN_ON(!cl || !cl->me_cl))
		return -EINVAL;

	if (cl->mei_flow_ctrl_creds > 0)
		return 1;

	if (mei_cl_is_fixed_address(cl)) {
		rets = mei_cl_read_start(cl, mei_cl_mtu(cl), NULL);
		if (rets && rets != -EBUSY)
			return rets;
		return 1;
	}

	if (mei_cl_is_single_recv_buf(cl)) {
		if (cl->me_cl->mei_flow_ctrl_creds > 0)
			return 1;
	}
	return 0;
}

/**
 * mei_cl_flow_ctrl_reduce - reduces flow_control.
 *
 * @cl: private data of the file object
 *
 * Return:
 *	0 on success
 *	-EINVAL when ctrl credits are <= 0
 */
int mei_cl_flow_ctrl_reduce(struct mei_cl *cl)
{
	if (WARN_ON(!cl || !cl->me_cl))
		return -EINVAL;

	if (mei_cl_is_fixed_address(cl))
		return 0;

	if (mei_cl_is_single_recv_buf(cl)) {
		if (WARN_ON(cl->me_cl->mei_flow_ctrl_creds <= 0))
			return -EINVAL;
		cl->me_cl->mei_flow_ctrl_creds--;
	} else {
		if (WARN_ON(cl->mei_flow_ctrl_creds <= 0))
			return -EINVAL;
		cl->mei_flow_ctrl_creds--;
	}
	return 0;
}

/**
 *  mei_cl_notify_fop2req - convert fop to proper request
 *
 * @fop: client notification start response command
 *
 * Return:  MEI_HBM_NOTIFICATION_START/STOP
 */
u8 mei_cl_notify_fop2req(enum mei_cb_file_ops fop)
{
	if (fop == MEI_FOP_NOTIFY_START)
		return MEI_HBM_NOTIFICATION_START;
	else
		return MEI_HBM_NOTIFICATION_STOP;
}

/**
 *  mei_cl_notify_req2fop - convert notification request top file operation type
 *
 * @req: hbm notification request type
 *
 * Return:  MEI_FOP_NOTIFY_START/STOP
 */
enum mei_cb_file_ops mei_cl_notify_req2fop(u8 req)
{
	if (req == MEI_HBM_NOTIFICATION_START)
		return MEI_FOP_NOTIFY_START;
	else
		return MEI_FOP_NOTIFY_STOP;
}

/**
 * mei_cl_irq_notify - send notification request in irq_thread context
 *
 * @cl: client
 * @cb: callback block.
 * @cmpl_list: complete list.
 *
 * Return: 0 on such and error otherwise.
 */
int mei_cl_irq_notify(struct mei_cl *cl, struct mei_cl_cb *cb,
		      struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	u32 msg_slots;
	int slots;
	int ret;
	bool request;

	msg_slots = mei_data2slots(sizeof(struct hbm_client_connect_request));
	slots = mei_hbuf_empty_slots(dev);

	if (slots < msg_slots)
		return -EMSGSIZE;

	request = mei_cl_notify_fop2req(cb->fop_type);
	ret = mei_hbm_cl_notify_req(dev, cl, request);
	if (ret) {
		cl->status = ret;
		list_move_tail(&cb->list, &cmpl_list->list);
		return ret;
	}

	list_move_tail(&cb->list, &dev->ctrl_rd_list.list);
	return 0;
}

/**
 * mei_cl_notify_request - send notification stop/start request
 *
 * @cl: host client
 * @file: associate request with file
 * @request: 1 for start or 0 for stop
 *
 * Locking: called under "dev->device_lock" lock
 *
 * Return: 0 on such and error otherwise.
 */
int mei_cl_notify_request(struct mei_cl *cl, struct file *file, u8 request)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;
	enum mei_cb_file_ops fop_type;
	int rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	if (!dev->hbm_f_ev_supported) {
		cl_dbg(dev, cl, "notifications not supported\n");
		return -EOPNOTSUPP;
	}

	rets = pm_runtime_get(dev->dev);
	if (rets < 0 && rets != -EINPROGRESS) {
		pm_runtime_put_noidle(dev->dev);
		cl_err(dev, cl, "rpm: get failed %d\n", rets);
		return rets;
	}

	fop_type = mei_cl_notify_req2fop(request);
	cb = mei_io_cb_init(cl, fop_type, file);
	if (!cb) {
		rets = -ENOMEM;
		goto out;
	}

	if (mei_hbuf_acquire(dev)) {
		if (mei_hbm_cl_notify_req(dev, cl, request)) {
			rets = -ENODEV;
			goto out;
		}
		list_add_tail(&cb->list, &dev->ctrl_rd_list.list);
	} else {
		list_add_tail(&cb->list, &dev->ctrl_wr_list.list);
	}

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(cl->wait, cl->notify_en == request,
			mei_secs_to_jiffies(MEI_CL_CONNECT_TIMEOUT));
	mutex_lock(&dev->device_lock);

	if (cl->notify_en != request) {
		mei_io_list_flush(&dev->ctrl_rd_list, cl);
		mei_io_list_flush(&dev->ctrl_wr_list, cl);
		if (!cl->status)
			cl->status = -EFAULT;
	}

	rets = cl->status;

out:
	cl_dbg(dev, cl, "rpm: autosuspend\n");
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	mei_io_cb_free(cb);
	return rets;
}

/**
 * mei_cl_notify - raise notification
 *
 * @cl: host client
 *
 * Locking: called under "dev->device_lock" lock
 */
void mei_cl_notify(struct mei_cl *cl)
{
	struct mei_device *dev;

	if (!cl || !cl->dev)
		return;

	dev = cl->dev;

	if (!cl->notify_en)
		return;

	cl_dbg(dev, cl, "notify event");
	cl->notify_ev = true;
	wake_up_interruptible_all(&cl->ev_wait);

	if (cl->ev_async)
		kill_fasync(&cl->ev_async, SIGIO, POLL_PRI);

	mei_cl_bus_notify_event(cl);
}

/**
 * mei_cl_notify_get - get or wait for notification event
 *
 * @cl: host client
 * @block: this request is blocking
 * @notify_ev: true if notification event was received
 *
 * Locking: called under "dev->device_lock" lock
 *
 * Return: 0 on such and error otherwise.
 */
int mei_cl_notify_get(struct mei_cl *cl, bool block, bool *notify_ev)
{
	struct mei_device *dev;
	int rets;

	*notify_ev = false;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	if (!mei_cl_is_connected(cl))
		return -ENODEV;

	if (cl->notify_ev)
		goto out;

	if (!block)
		return -EAGAIN;

	mutex_unlock(&dev->device_lock);
	rets = wait_event_interruptible(cl->ev_wait, cl->notify_ev);
	mutex_lock(&dev->device_lock);

	if (rets < 0)
		return rets;

out:
	*notify_ev = cl->notify_ev;
	cl->notify_ev = false;
	return 0;
}

/**
 * mei_cl_read_start - the start read client message function.
 *
 * @cl: host client
 * @length: number of bytes to read
 * @fp: pointer to file structure
 *
 * Return: 0 on success, <0 on failure.
 */
int mei_cl_read_start(struct mei_cl *cl, size_t length, struct file *fp)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;
	int rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	if (!mei_cl_is_connected(cl))
		return -ENODEV;

	/* HW currently supports only one pending read */
	if (!list_empty(&cl->rd_pending))
		return -EBUSY;

	if (!mei_me_cl_is_active(cl->me_cl)) {
		cl_err(dev, cl, "no such me client\n");
		return  -ENOTTY;
	}

	/* always allocate at least client max message */
	length = max_t(size_t, length, mei_cl_mtu(cl));
	cb = mei_cl_alloc_cb(cl, length, MEI_FOP_READ, fp);
	if (!cb)
		return -ENOMEM;

	if (mei_cl_is_fixed_address(cl)) {
		list_add_tail(&cb->list, &cl->rd_pending);
		return 0;
	}

	rets = pm_runtime_get(dev->dev);
	if (rets < 0 && rets != -EINPROGRESS) {
		pm_runtime_put_noidle(dev->dev);
		cl_err(dev, cl, "rpm: get failed %d\n", rets);
		goto nortpm;
	}

	if (mei_hbuf_acquire(dev)) {
		rets = mei_hbm_cl_flow_control_req(dev, cl);
		if (rets < 0)
			goto out;

		list_add_tail(&cb->list, &cl->rd_pending);
	} else {
		rets = 0;
		list_add_tail(&cb->list, &dev->ctrl_wr_list.list);
	}

out:
	cl_dbg(dev, cl, "rpm: autosuspend\n");
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
nortpm:
	if (rets)
		mei_io_cb_free(cb);

	return rets;
}

/**
 * mei_cl_irq_write - write a message to device
 *	from the interrupt thread context
 *
 * @cl: client
 * @cb: callback block.
 * @cmpl_list: complete list.
 *
 * Return: 0, OK; otherwise error.
 */
int mei_cl_irq_write(struct mei_cl *cl, struct mei_cl_cb *cb,
		     struct mei_cl_cb *cmpl_list)
{
	struct mei_device *dev;
	struct mei_msg_data *buf;
	struct mei_msg_hdr mei_hdr;
	size_t len;
	u32 msg_slots;
	int slots;
	int rets;
	bool first_chunk;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	buf = &cb->buf;

	first_chunk = cb->buf_idx == 0;

	rets = first_chunk ? mei_cl_flow_ctrl_creds(cl) : 1;
	if (rets < 0)
		return rets;

	if (rets == 0) {
		cl_dbg(dev, cl, "No flow control credentials: not sending.\n");
		return 0;
	}

	slots = mei_hbuf_empty_slots(dev);
	len = buf->size - cb->buf_idx;
	msg_slots = mei_data2slots(len);

	mei_hdr.host_addr = mei_cl_host_addr(cl);
	mei_hdr.me_addr = mei_cl_me_id(cl);
	mei_hdr.reserved = 0;
	mei_hdr.internal = cb->internal;

	if (slots >= msg_slots) {
		mei_hdr.length = len;
		mei_hdr.msg_complete = 1;
	/* Split the message only if we can write the whole host buffer */
	} else if (slots == dev->hbuf_depth) {
		msg_slots = slots;
		len = (slots * sizeof(u32)) - sizeof(struct mei_msg_hdr);
		mei_hdr.length = len;
		mei_hdr.msg_complete = 0;
	} else {
		/* wait for next time the host buffer is empty */
		return 0;
	}

	cl_dbg(dev, cl, "buf: size = %d idx = %lu\n",
			cb->buf.size, cb->buf_idx);

	rets = mei_write_message(dev, &mei_hdr, buf->data + cb->buf_idx);
	if (rets) {
		cl->status = rets;
		list_move_tail(&cb->list, &cmpl_list->list);
		return rets;
	}

	cl->status = 0;
	cl->writing_state = MEI_WRITING;
	cb->buf_idx += mei_hdr.length;
	cb->completed = mei_hdr.msg_complete == 1;

	if (first_chunk) {
		if (mei_cl_flow_ctrl_reduce(cl))
			return -EIO;
	}

	if (mei_hdr.msg_complete)
		list_move_tail(&cb->list, &dev->write_waiting_list.list);

	return 0;
}

/**
 * mei_cl_write - submit a write cb to mei device
 *	assumes device_lock is locked
 *
 * @cl: host client
 * @cb: write callback with filled data
 * @blocking: block until completed
 *
 * Return: number of bytes sent on success, <0 on failure.
 */
int mei_cl_write(struct mei_cl *cl, struct mei_cl_cb *cb, bool blocking)
{
	struct mei_device *dev;
	struct mei_msg_data *buf;
	struct mei_msg_hdr mei_hdr;
	int size;
	int rets;


	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	if (WARN_ON(!cb))
		return -EINVAL;

	dev = cl->dev;

	buf = &cb->buf;
	size = buf->size;

	cl_dbg(dev, cl, "size=%d\n", size);

	rets = pm_runtime_get(dev->dev);
	if (rets < 0 && rets != -EINPROGRESS) {
		pm_runtime_put_noidle(dev->dev);
		cl_err(dev, cl, "rpm: get failed %d\n", rets);
		return rets;
	}

	cb->buf_idx = 0;
	cl->writing_state = MEI_IDLE;

	mei_hdr.host_addr = mei_cl_host_addr(cl);
	mei_hdr.me_addr = mei_cl_me_id(cl);
	mei_hdr.reserved = 0;
	mei_hdr.msg_complete = 0;
	mei_hdr.internal = cb->internal;

	rets = mei_cl_flow_ctrl_creds(cl);
	if (rets < 0)
		goto err;

	if (rets == 0) {
		cl_dbg(dev, cl, "No flow control credentials: not sending.\n");
		rets = size;
		goto out;
	}
	if (!mei_hbuf_acquire(dev)) {
		cl_dbg(dev, cl, "Cannot acquire the host buffer: not sending.\n");
		rets = size;
		goto out;
	}

	/* Check for a maximum length */
	if (size > mei_hbuf_max_len(dev)) {
		mei_hdr.length = mei_hbuf_max_len(dev);
		mei_hdr.msg_complete = 0;
	} else {
		mei_hdr.length = size;
		mei_hdr.msg_complete = 1;
	}

	rets = mei_write_message(dev, &mei_hdr, buf->data);
	if (rets)
		goto err;

	rets = mei_cl_flow_ctrl_reduce(cl);
	if (rets)
		goto err;

	cl->writing_state = MEI_WRITING;
	cb->buf_idx = mei_hdr.length;
	cb->completed = mei_hdr.msg_complete == 1;

out:
	if (mei_hdr.msg_complete)
		list_add_tail(&cb->list, &dev->write_waiting_list.list);
	else
		list_add_tail(&cb->list, &dev->write_list.list);

	cb = NULL;
	if (blocking && cl->writing_state != MEI_WRITE_COMPLETE) {

		mutex_unlock(&dev->device_lock);
		rets = wait_event_interruptible(cl->tx_wait,
				cl->writing_state == MEI_WRITE_COMPLETE);
		mutex_lock(&dev->device_lock);
		/* wait_event_interruptible returns -ERESTARTSYS */
		if (rets) {
			if (signal_pending(current))
				rets = -EINTR;
			goto err;
		}
	}

	rets = size;
err:
	cl_dbg(dev, cl, "rpm: autosuspend\n");
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return rets;
}


/**
 * mei_cl_complete - processes completed operation for a client
 *
 * @cl: private data of the file object.
 * @cb: callback block.
 */
void mei_cl_complete(struct mei_cl *cl, struct mei_cl_cb *cb)
{
	struct mei_device *dev = cl->dev;

	switch (cb->fop_type) {
	case MEI_FOP_WRITE:
		mei_io_cb_free(cb);
		cl->writing_state = MEI_WRITE_COMPLETE;
		if (waitqueue_active(&cl->tx_wait)) {
			wake_up_interruptible(&cl->tx_wait);
		} else {
			pm_runtime_mark_last_busy(dev->dev);
			pm_request_autosuspend(dev->dev);
		}
		break;

	case MEI_FOP_READ:
		list_add_tail(&cb->list, &cl->rd_completed);
		if (waitqueue_active(&cl->rx_wait))
			wake_up_interruptible_all(&cl->rx_wait);
		else
			mei_cl_bus_rx_event(cl);
		break;

	case MEI_FOP_CONNECT:
	case MEI_FOP_DISCONNECT:
	case MEI_FOP_NOTIFY_STOP:
	case MEI_FOP_NOTIFY_START:
		if (waitqueue_active(&cl->wait))
			wake_up(&cl->wait);

		break;
	case MEI_FOP_DISCONNECT_RSP:
		mei_io_cb_free(cb);
		mei_cl_set_disconnected(cl);
		break;
	default:
		BUG_ON(0);
	}
}


/**
 * mei_cl_all_disconnect - disconnect forcefully all connected clients
 *
 * @dev: mei device
 */
void mei_cl_all_disconnect(struct mei_device *dev)
{
	struct mei_cl *cl;

	list_for_each_entry(cl, &dev->file_list, link)
		mei_cl_set_disconnected(cl);
}


/**
 * mei_cl_all_wakeup  - wake up all readers and writers they can be interrupted
 *
 * @dev: mei device
 */
void mei_cl_all_wakeup(struct mei_device *dev)
{
	struct mei_cl *cl;

	list_for_each_entry(cl, &dev->file_list, link) {
		if (waitqueue_active(&cl->rx_wait)) {
			cl_dbg(dev, cl, "Waking up reading client!\n");
			wake_up_interruptible(&cl->rx_wait);
		}
		if (waitqueue_active(&cl->tx_wait)) {
			cl_dbg(dev, cl, "Waking up writing client!\n");
			wake_up_interruptible(&cl->tx_wait);
		}

		/* synchronized under device mutex */
		if (waitqueue_active(&cl->ev_wait)) {
			cl_dbg(dev, cl, "Waking up waiting for event clients!\n");
			wake_up_interruptible(&cl->ev_wait);
		}
	}
}

/**
 * mei_cl_all_write_clear - clear all pending writes
 *
 * @dev: mei device
 */
void mei_cl_all_write_clear(struct mei_device *dev)
{
	mei_io_list_free(&dev->write_list, NULL);
	mei_io_list_free(&dev->write_waiting_list, NULL);
}


