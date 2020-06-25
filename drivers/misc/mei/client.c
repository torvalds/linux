// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2003-2019, Intel Corporation. All rights reserved.
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */

#include <linux/sched/signal.h>
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
	mei_me_cl_put(me_cl);
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
	mei_me_cl_put(me_cl);
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
 * mei_tx_cb_queue - queue tx callback
 *
 * Locking: called under "dev->device_lock" lock
 *
 * @cb: mei callback struct
 * @head: an instance of list to queue on
 */
static inline void mei_tx_cb_enqueue(struct mei_cl_cb *cb,
				     struct list_head *head)
{
	list_add_tail(&cb->list, head);
	cb->cl->tx_cb_queued++;
}

/**
 * mei_tx_cb_dequeue - dequeue tx callback
 *
 * Locking: called under "dev->device_lock" lock
 *
 * @cb: mei callback struct to dequeue and free
 */
static inline void mei_tx_cb_dequeue(struct mei_cl_cb *cb)
{
	if (!WARN_ON(cb->cl->tx_cb_queued == 0))
		cb->cl->tx_cb_queued--;

	mei_io_cb_free(cb);
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
static struct mei_cl_cb *mei_io_cb_init(struct mei_cl *cl,
					enum mei_cb_file_ops type,
					const struct file *fp)
{
	struct mei_cl_cb *cb;

	cb = kzalloc(sizeof(struct mei_cl_cb), GFP_KERNEL);
	if (!cb)
		return NULL;

	INIT_LIST_HEAD(&cb->list);
	cb->fp = fp;
	cb->cl = cl;
	cb->buf_idx = 0;
	cb->fop_type = type;
	return cb;
}

/**
 * mei_io_list_flush_cl - removes cbs belonging to the cl.
 *
 * @head:  an instance of our list structure
 * @cl:    host client
 */
static void mei_io_list_flush_cl(struct list_head *head,
				 const struct mei_cl *cl)
{
	struct mei_cl_cb *cb, *next;

	list_for_each_entry_safe(cb, next, head, list) {
		if (cl == cb->cl) {
			list_del_init(&cb->list);
			if (cb->fop_type == MEI_FOP_READ)
				mei_io_cb_free(cb);
		}
	}
}

/**
 * mei_io_tx_list_free_cl - removes cb belonging to the cl and free them
 *
 * @head: An instance of our list structure
 * @cl: host client
 */
static void mei_io_tx_list_free_cl(struct list_head *head,
				   const struct mei_cl *cl)
{
	struct mei_cl_cb *cb, *next;

	list_for_each_entry_safe(cb, next, head, list) {
		if (cl == cb->cl)
			mei_tx_cb_dequeue(cb);
	}
}

/**
 * mei_io_list_free_fp - free cb from a list that matches file pointer
 *
 * @head: io list
 * @fp: file pointer (matching cb file object), may be NULL
 */
static void mei_io_list_free_fp(struct list_head *head, const struct file *fp)
{
	struct mei_cl_cb *cb, *next;

	list_for_each_entry_safe(cb, next, head, list)
		if (!fp || fp == cb->fp)
			mei_io_cb_free(cb);
}

/**
 * mei_cl_alloc_cb - a convenient wrapper for allocating read cb
 *
 * @cl: host client
 * @length: size of the buffer
 * @fop_type: operation type
 * @fp: associated file pointer (might be NULL)
 *
 * Return: cb on success and NULL on failure
 */
struct mei_cl_cb *mei_cl_alloc_cb(struct mei_cl *cl, size_t length,
				  enum mei_cb_file_ops fop_type,
				  const struct file *fp)
{
	struct mei_cl_cb *cb;

	cb = mei_io_cb_init(cl, fop_type, fp);
	if (!cb)
		return NULL;

	if (length == 0)
		return cb;

	cb->buf.data = kmalloc(roundup(length, MEI_SLOT_SIZE), GFP_KERNEL);
	if (!cb->buf.data) {
		mei_io_cb_free(cb);
		return NULL;
	}
	cb->buf.size = length;

	return cb;
}

/**
 * mei_cl_enqueue_ctrl_wr_cb - a convenient wrapper for allocating
 *     and enqueuing of the control commands cb
 *
 * @cl: host client
 * @length: size of the buffer
 * @fop_type: operation type
 * @fp: associated file pointer (might be NULL)
 *
 * Return: cb on success and NULL on failure
 * Locking: called under "dev->device_lock" lock
 */
struct mei_cl_cb *mei_cl_enqueue_ctrl_wr_cb(struct mei_cl *cl, size_t length,
					    enum mei_cb_file_ops fop_type,
					    const struct file *fp)
{
	struct mei_cl_cb *cb;

	/* for RX always allocate at least client's mtu */
	if (length)
		length = max_t(size_t, length, mei_cl_mtu(cl));

	cb = mei_cl_alloc_cb(cl, length, fop_type, fp);
	if (!cb)
		return NULL;

	list_add_tail(&cb->list, &cl->dev->ctrl_wr_list);
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
		if (!fp || fp == cb->fp)
			return cb;

	return NULL;
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
	mei_io_tx_list_free_cl(&cl->dev->write_list, cl);
	mei_io_tx_list_free_cl(&cl->dev->write_waiting_list, cl);
	mei_io_list_flush_cl(&cl->dev->ctrl_wr_list, cl);
	mei_io_list_flush_cl(&cl->dev->ctrl_rd_list, cl);
	mei_io_list_free_fp(&cl->rd_pending, fp);
	mei_io_list_free_fp(&cl->rd_completed, fp);

	return 0;
}

/**
 * mei_cl_init - initializes cl.
 *
 * @cl: host client to be initialized
 * @dev: mei device
 */
static void mei_cl_init(struct mei_cl *cl, struct mei_device *dev)
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
	cl->state = MEI_FILE_UNINITIALIZED;
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
 *
 * Return: 0 on success
 *	-EINVAL on incorrect values
 *	-EMFILE if open count exceeded.
 */
int mei_cl_link(struct mei_cl *cl)
{
	struct mei_device *dev;
	int id;

	if (WARN_ON(!cl || !cl->dev))
		return -EINVAL;

	dev = cl->dev;

	id = find_first_zero_bit(dev->host_clients_map, MEI_CLIENTS_MAX);
	if (id >= MEI_CLIENTS_MAX) {
		dev_err(dev->dev, "id exceeded %d", MEI_CLIENTS_MAX);
		return -EMFILE;
	}

	if (dev->open_handle_count >= MEI_MAX_OPEN_HANDLE_COUNT) {
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

	if (WARN_ON(!cl->dev))
		return 0;

	dev = cl->dev;

	cl_dbg(dev, cl, "unlink client");

	if (dev->open_handle_count > 0)
		dev->open_handle_count--;

	/* never clear the 0 bit */
	if (cl->host_client_id)
		clear_bit(cl->host_client_id, dev->host_clients_map);

	list_del_init(&cl->link);

	cl->state = MEI_FILE_UNINITIALIZED;
	cl->writing_state = MEI_IDLE;

	WARN_ON(!list_empty(&cl->rd_completed) ||
		!list_empty(&cl->rd_pending) ||
		!list_empty(&cl->link));

	return 0;
}

void mei_host_client_init(struct mei_device *dev)
{
	mei_set_devstate(dev, MEI_DEV_ENABLED);
	dev->reset_count = 0;

	schedule_work(&dev->bus_rescan_work);

	pm_runtime_mark_last_busy(dev->dev);
	dev_dbg(dev->dev, "rpm: autosuspend\n");
	pm_request_autosuspend(dev->dev);
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
 * mei_cl_wake_all - wake up readers, writers and event waiters so
 *                 they can be interrupted
 *
 * @cl: host client
 */
static void mei_cl_wake_all(struct mei_cl *cl)
{
	struct mei_device *dev = cl->dev;

	/* synchronized under device mutex */
	if (waitqueue_active(&cl->rx_wait)) {
		cl_dbg(dev, cl, "Waking up reading client!\n");
		wake_up_interruptible(&cl->rx_wait);
	}
	/* synchronized under device mutex */
	if (waitqueue_active(&cl->tx_wait)) {
		cl_dbg(dev, cl, "Waking up writing client!\n");
		wake_up_interruptible(&cl->tx_wait);
	}
	/* synchronized under device mutex */
	if (waitqueue_active(&cl->ev_wait)) {
		cl_dbg(dev, cl, "Waking up waiting for event clients!\n");
		wake_up_interruptible(&cl->ev_wait);
	}
	/* synchronized under device mutex */
	if (waitqueue_active(&cl->wait)) {
		cl_dbg(dev, cl, "Waking up ctrl write clients!\n");
		wake_up(&cl->wait);
	}
}

/**
 * mei_cl_set_disconnected - set disconnected state and clear
 *   associated states and resources
 *
 * @cl: host client
 */
static void mei_cl_set_disconnected(struct mei_cl *cl)
{
	struct mei_device *dev = cl->dev;

	if (cl->state == MEI_FILE_DISCONNECTED ||
	    cl->state <= MEI_FILE_INITIALIZING)
		return;

	cl->state = MEI_FILE_DISCONNECTED;
	mei_io_tx_list_free_cl(&dev->write_list, cl);
	mei_io_tx_list_free_cl(&dev->write_waiting_list, cl);
	mei_io_list_flush_cl(&dev->ctrl_rd_list, cl);
	mei_io_list_flush_cl(&dev->ctrl_wr_list, cl);
	mei_cl_wake_all(cl);
	cl->rx_flow_ctrl_creds = 0;
	cl->tx_flow_ctrl_creds = 0;
	cl->timer_count = 0;

	if (!cl->me_cl)
		return;

	if (!WARN_ON(cl->me_cl->connect_count == 0))
		cl->me_cl->connect_count--;

	if (cl->me_cl->connect_count == 0)
		cl->me_cl->tx_flow_ctrl_creds = 0;

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

	list_move_tail(&cb->list, &dev->ctrl_rd_list);
	cl->timer_count = MEI_CONNECT_TIMEOUT;
	mei_schedule_stall_timer(dev);

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
			  struct list_head *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	u32 msg_slots;
	int slots;
	int ret;

	msg_slots = mei_hbm2slots(sizeof(struct hbm_client_connect_request));
	slots = mei_hbuf_empty_slots(dev);
	if (slots < 0)
		return -EOVERFLOW;

	if ((u32)slots < msg_slots)
		return -EMSGSIZE;

	ret = mei_cl_send_disconnect(cl, cb);
	if (ret)
		list_move_tail(&cb->list, cmpl_list);

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

	cb = mei_cl_enqueue_ctrl_wr_cb(cl, 0, MEI_FOP_DISCONNECT, NULL);
	if (!cb) {
		rets = -ENOMEM;
		goto out;
	}

	if (mei_hbuf_acquire(dev)) {
		rets = mei_cl_send_disconnect(cl, cb);
		if (rets) {
			cl_err(dev, cl, "failed to disconnect.\n");
			goto out;
		}
	}

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(cl->wait,
			   cl->state == MEI_FILE_DISCONNECT_REPLY ||
			   cl->state == MEI_FILE_DISCONNECTED,
			   mei_secs_to_jiffies(MEI_CL_CONNECT_TIMEOUT));
	mutex_lock(&dev->device_lock);

	rets = cl->status;
	if (cl->state != MEI_FILE_DISCONNECT_REPLY &&
	    cl->state != MEI_FILE_DISCONNECTED) {
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

	if (dev->dev_state == MEI_DEV_POWER_DOWN) {
		cl_dbg(dev, cl, "Device is powering down, don't bother with disconnection\n");
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

	list_for_each_entry(cb, &dev->ctrl_rd_list, list) {
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

	list_move_tail(&cb->list, &dev->ctrl_rd_list);
	cl->timer_count = MEI_CONNECT_TIMEOUT;
	mei_schedule_stall_timer(dev);
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
		       struct list_head *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	u32 msg_slots;
	int slots;
	int rets;

	if (mei_cl_is_other_connecting(cl))
		return 0;

	msg_slots = mei_hbm2slots(sizeof(struct hbm_client_connect_request));
	slots = mei_hbuf_empty_slots(dev);
	if (slots < 0)
		return -EOVERFLOW;

	if ((u32)slots < msg_slots)
		return -EMSGSIZE;

	rets = mei_cl_send_connect(cl, cb);
	if (rets)
		list_move_tail(&cb->list, cmpl_list);

	return rets;
}

/**
 * mei_cl_connect - connect host client to the me one
 *
 * @cl: host client
 * @me_cl: me client
 * @fp: pointer to file structure
 *
 * Locking: called under "dev->device_lock" lock
 *
 * Return: 0 on success, <0 on failure.
 */
int mei_cl_connect(struct mei_cl *cl, struct mei_me_client *me_cl,
		   const struct file *fp)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;
	int rets;

	if (WARN_ON(!cl || !cl->dev || !me_cl))
		return -ENODEV;

	dev = cl->dev;

	rets = mei_cl_set_connecting(cl, me_cl);
	if (rets)
		goto nortpm;

	if (mei_cl_is_fixed_address(cl)) {
		cl->state = MEI_FILE_CONNECTED;
		rets = 0;
		goto nortpm;
	}

	rets = pm_runtime_get(dev->dev);
	if (rets < 0 && rets != -EINPROGRESS) {
		pm_runtime_put_noidle(dev->dev);
		cl_err(dev, cl, "rpm: get failed %d\n", rets);
		goto nortpm;
	}

	cb = mei_cl_enqueue_ctrl_wr_cb(cl, 0, MEI_FOP_CONNECT, fp);
	if (!cb) {
		rets = -ENOMEM;
		goto out;
	}

	/* run hbuf acquire last so we don't have to undo */
	if (!mei_cl_is_other_connecting(cl) && mei_hbuf_acquire(dev)) {
		rets = mei_cl_send_connect(cl, cb);
		if (rets)
			goto out;
	}

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(cl->wait,
			(cl->state == MEI_FILE_CONNECTED ||
			 cl->state == MEI_FILE_DISCONNECTED ||
			 cl->state == MEI_FILE_DISCONNECT_REQUIRED ||
			 cl->state == MEI_FILE_DISCONNECT_REPLY),
			mei_secs_to_jiffies(MEI_CL_CONNECT_TIMEOUT));
	mutex_lock(&dev->device_lock);

	if (!mei_cl_is_connected(cl)) {
		if (cl->state == MEI_FILE_DISCONNECT_REQUIRED) {
			mei_io_list_flush_cl(&dev->ctrl_rd_list, cl);
			mei_io_list_flush_cl(&dev->ctrl_wr_list, cl);
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
 *
 * Return: cl on success ERR_PTR on failure
 */
struct mei_cl *mei_cl_alloc_linked(struct mei_device *dev)
{
	struct mei_cl *cl;
	int ret;

	cl = mei_cl_allocate(dev);
	if (!cl) {
		ret = -ENOMEM;
		goto err;
	}

	ret = mei_cl_link(cl);
	if (ret)
		goto err;

	return cl;
err:
	kfree(cl);
	return ERR_PTR(ret);
}

/**
 * mei_cl_tx_flow_ctrl_creds - checks flow_control credits for cl.
 *
 * @cl: host client
 *
 * Return: 1 if tx_flow_ctrl_creds >0, 0 - otherwise.
 */
static int mei_cl_tx_flow_ctrl_creds(struct mei_cl *cl)
{
	if (WARN_ON(!cl || !cl->me_cl))
		return -EINVAL;

	if (cl->tx_flow_ctrl_creds > 0)
		return 1;

	if (mei_cl_is_fixed_address(cl))
		return 1;

	if (mei_cl_is_single_recv_buf(cl)) {
		if (cl->me_cl->tx_flow_ctrl_creds > 0)
			return 1;
	}
	return 0;
}

/**
 * mei_cl_tx_flow_ctrl_creds_reduce - reduces transmit flow control credits
 *   for a client
 *
 * @cl: host client
 *
 * Return:
 *	0 on success
 *	-EINVAL when ctrl credits are <= 0
 */
static int mei_cl_tx_flow_ctrl_creds_reduce(struct mei_cl *cl)
{
	if (WARN_ON(!cl || !cl->me_cl))
		return -EINVAL;

	if (mei_cl_is_fixed_address(cl))
		return 0;

	if (mei_cl_is_single_recv_buf(cl)) {
		if (WARN_ON(cl->me_cl->tx_flow_ctrl_creds <= 0))
			return -EINVAL;
		cl->me_cl->tx_flow_ctrl_creds--;
	} else {
		if (WARN_ON(cl->tx_flow_ctrl_creds <= 0))
			return -EINVAL;
		cl->tx_flow_ctrl_creds--;
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
		      struct list_head *cmpl_list)
{
	struct mei_device *dev = cl->dev;
	u32 msg_slots;
	int slots;
	int ret;
	bool request;

	msg_slots = mei_hbm2slots(sizeof(struct hbm_client_connect_request));
	slots = mei_hbuf_empty_slots(dev);
	if (slots < 0)
		return -EOVERFLOW;

	if ((u32)slots < msg_slots)
		return -EMSGSIZE;

	request = mei_cl_notify_fop2req(cb->fop_type);
	ret = mei_hbm_cl_notify_req(dev, cl, request);
	if (ret) {
		cl->status = ret;
		list_move_tail(&cb->list, cmpl_list);
		return ret;
	}

	list_move_tail(&cb->list, &dev->ctrl_rd_list);
	return 0;
}

/**
 * mei_cl_notify_request - send notification stop/start request
 *
 * @cl: host client
 * @fp: associate request with file
 * @request: 1 for start or 0 for stop
 *
 * Locking: called under "dev->device_lock" lock
 *
 * Return: 0 on such and error otherwise.
 */
int mei_cl_notify_request(struct mei_cl *cl,
			  const struct file *fp, u8 request)
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

	if (!mei_cl_is_connected(cl))
		return -ENODEV;

	rets = pm_runtime_get(dev->dev);
	if (rets < 0 && rets != -EINPROGRESS) {
		pm_runtime_put_noidle(dev->dev);
		cl_err(dev, cl, "rpm: get failed %d\n", rets);
		return rets;
	}

	fop_type = mei_cl_notify_req2fop(request);
	cb = mei_cl_enqueue_ctrl_wr_cb(cl, 0, fop_type, fp);
	if (!cb) {
		rets = -ENOMEM;
		goto out;
	}

	if (mei_hbuf_acquire(dev)) {
		if (mei_hbm_cl_notify_req(dev, cl, request)) {
			rets = -ENODEV;
			goto out;
		}
		list_move_tail(&cb->list, &dev->ctrl_rd_list);
	}

	mutex_unlock(&dev->device_lock);
	wait_event_timeout(cl->wait,
			   cl->notify_en == request ||
			   cl->status ||
			   !mei_cl_is_connected(cl),
			   mei_secs_to_jiffies(MEI_CL_CONNECT_TIMEOUT));
	mutex_lock(&dev->device_lock);

	if (cl->notify_en != request && !cl->status)
		cl->status = -EFAULT;

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
	if (!mei_cl_bus_notify_event(cl))
		wake_up_interruptible(&cl->ev_wait);

	if (cl->ev_async)
		kill_fasync(&cl->ev_async, SIGIO, POLL_PRI);

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

	if (!dev->hbm_f_ev_supported) {
		cl_dbg(dev, cl, "notifications not supported\n");
		return -EOPNOTSUPP;
	}

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
int mei_cl_read_start(struct mei_cl *cl, size_t length, const struct file *fp)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;
	int rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	if (!mei_cl_is_connected(cl))
		return -ENODEV;

	if (!mei_me_cl_is_active(cl->me_cl)) {
		cl_err(dev, cl, "no such me client\n");
		return  -ENOTTY;
	}

	if (mei_cl_is_fixed_address(cl))
		return 0;

	/* HW currently supports only one pending read */
	if (cl->rx_flow_ctrl_creds)
		return -EBUSY;

	cb = mei_cl_enqueue_ctrl_wr_cb(cl, length, MEI_FOP_READ, fp);
	if (!cb)
		return -ENOMEM;

	rets = pm_runtime_get(dev->dev);
	if (rets < 0 && rets != -EINPROGRESS) {
		pm_runtime_put_noidle(dev->dev);
		cl_err(dev, cl, "rpm: get failed %d\n", rets);
		goto nortpm;
	}

	rets = 0;
	if (mei_hbuf_acquire(dev)) {
		rets = mei_hbm_cl_flow_control_req(dev, cl);
		if (rets < 0)
			goto out;

		list_move_tail(&cb->list, &cl->rd_pending);
	}
	cl->rx_flow_ctrl_creds++;

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
 * mei_msg_hdr_init - initialize mei message header
 *
 * @mei_hdr: mei message header
 * @cb: message callback structure
 */
static void mei_msg_hdr_init(struct mei_msg_hdr *mei_hdr, struct mei_cl_cb *cb)
{
	mei_hdr->host_addr = mei_cl_host_addr(cb->cl);
	mei_hdr->me_addr = mei_cl_me_id(cb->cl);
	mei_hdr->length = 0;
	mei_hdr->reserved = 0;
	mei_hdr->msg_complete = 0;
	mei_hdr->dma_ring = 0;
	mei_hdr->internal = cb->internal;
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
		     struct list_head *cmpl_list)
{
	struct mei_device *dev;
	struct mei_msg_data *buf;
	struct mei_msg_hdr mei_hdr;
	size_t hdr_len = sizeof(mei_hdr);
	size_t len;
	size_t hbuf_len, dr_len;
	int hbuf_slots;
	u32 dr_slots;
	u32 dma_len;
	int rets;
	bool first_chunk;
	const void *data;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	buf = &cb->buf;

	first_chunk = cb->buf_idx == 0;

	rets = first_chunk ? mei_cl_tx_flow_ctrl_creds(cl) : 1;
	if (rets < 0)
		goto err;

	if (rets == 0) {
		cl_dbg(dev, cl, "No flow control credentials: not sending.\n");
		return 0;
	}

	len = buf->size - cb->buf_idx;
	data = buf->data + cb->buf_idx;
	hbuf_slots = mei_hbuf_empty_slots(dev);
	if (hbuf_slots < 0) {
		rets = -EOVERFLOW;
		goto err;
	}

	hbuf_len = mei_slots2data(hbuf_slots) & MEI_MSG_MAX_LEN_MASK;
	dr_slots = mei_dma_ring_empty_slots(dev);
	dr_len = mei_slots2data(dr_slots);

	mei_msg_hdr_init(&mei_hdr, cb);

	/**
	 * Split the message only if we can write the whole host buffer
	 * otherwise wait for next time the host buffer is empty.
	 */
	if (len + hdr_len <= hbuf_len) {
		mei_hdr.length = len;
		mei_hdr.msg_complete = 1;
	} else if (dr_slots && hbuf_len >= hdr_len + sizeof(dma_len)) {
		mei_hdr.dma_ring = 1;
		if (len > dr_len)
			len = dr_len;
		else
			mei_hdr.msg_complete = 1;

		mei_hdr.length = sizeof(dma_len);
		dma_len = len;
		data = &dma_len;
	} else if ((u32)hbuf_slots == mei_hbuf_depth(dev)) {
		len = hbuf_len - hdr_len;
		mei_hdr.length = len;
	} else {
		return 0;
	}

	if (mei_hdr.dma_ring)
		mei_dma_ring_write(dev, buf->data + cb->buf_idx, len);

	rets = mei_write_message(dev, &mei_hdr, hdr_len, data, mei_hdr.length);
	if (rets)
		goto err;

	cl->status = 0;
	cl->writing_state = MEI_WRITING;
	cb->buf_idx += len;

	if (first_chunk) {
		if (mei_cl_tx_flow_ctrl_creds_reduce(cl)) {
			rets = -EIO;
			goto err;
		}
	}

	if (mei_hdr.msg_complete)
		list_move_tail(&cb->list, &dev->write_waiting_list);

	return 0;

err:
	cl->status = rets;
	list_move_tail(&cb->list, cmpl_list);
	return rets;
}

/**
 * mei_cl_write - submit a write cb to mei device
 *	assumes device_lock is locked
 *
 * @cl: host client
 * @cb: write callback with filled data
 *
 * Return: number of bytes sent on success, <0 on failure.
 */
ssize_t mei_cl_write(struct mei_cl *cl, struct mei_cl_cb *cb)
{
	struct mei_device *dev;
	struct mei_msg_data *buf;
	struct mei_msg_hdr mei_hdr;
	size_t hdr_len = sizeof(mei_hdr);
	size_t len, hbuf_len, dr_len;
	int hbuf_slots;
	u32 dr_slots;
	u32 dma_len;
	ssize_t rets;
	bool blocking;
	const void *data;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	if (WARN_ON(!cb))
		return -EINVAL;

	dev = cl->dev;

	buf = &cb->buf;
	len = buf->size;

	cl_dbg(dev, cl, "len=%zd\n", len);

	blocking = cb->blocking;
	data = buf->data;

	rets = pm_runtime_get(dev->dev);
	if (rets < 0 && rets != -EINPROGRESS) {
		pm_runtime_put_noidle(dev->dev);
		cl_err(dev, cl, "rpm: get failed %zd\n", rets);
		goto free;
	}

	cb->buf_idx = 0;
	cl->writing_state = MEI_IDLE;


	rets = mei_cl_tx_flow_ctrl_creds(cl);
	if (rets < 0)
		goto err;

	mei_msg_hdr_init(&mei_hdr, cb);

	if (rets == 0) {
		cl_dbg(dev, cl, "No flow control credentials: not sending.\n");
		rets = len;
		goto out;
	}

	if (!mei_hbuf_acquire(dev)) {
		cl_dbg(dev, cl, "Cannot acquire the host buffer: not sending.\n");
		rets = len;
		goto out;
	}

	hbuf_slots = mei_hbuf_empty_slots(dev);
	if (hbuf_slots < 0) {
		rets = -EOVERFLOW;
		goto out;
	}

	hbuf_len = mei_slots2data(hbuf_slots) & MEI_MSG_MAX_LEN_MASK;
	dr_slots = mei_dma_ring_empty_slots(dev);
	dr_len =  mei_slots2data(dr_slots);

	if (len + hdr_len <= hbuf_len) {
		mei_hdr.length = len;
		mei_hdr.msg_complete = 1;
	} else if (dr_slots && hbuf_len >= hdr_len + sizeof(dma_len)) {
		mei_hdr.dma_ring = 1;
		if (len > dr_len)
			len = dr_len;
		else
			mei_hdr.msg_complete = 1;

		mei_hdr.length = sizeof(dma_len);
		dma_len = len;
		data = &dma_len;
	} else {
		len = hbuf_len - hdr_len;
		mei_hdr.length = len;
	}

	if (mei_hdr.dma_ring)
		mei_dma_ring_write(dev, buf->data, len);

	rets = mei_write_message(dev, &mei_hdr, hdr_len,
				 data, mei_hdr.length);
	if (rets)
		goto err;

	rets = mei_cl_tx_flow_ctrl_creds_reduce(cl);
	if (rets)
		goto err;

	cl->writing_state = MEI_WRITING;
	cb->buf_idx = len;
	/* restore return value */
	len = buf->size;

out:
	if (mei_hdr.msg_complete)
		mei_tx_cb_enqueue(cb, &dev->write_waiting_list);
	else
		mei_tx_cb_enqueue(cb, &dev->write_list);

	cb = NULL;
	if (blocking && cl->writing_state != MEI_WRITE_COMPLETE) {

		mutex_unlock(&dev->device_lock);
		rets = wait_event_interruptible(cl->tx_wait,
				cl->writing_state == MEI_WRITE_COMPLETE ||
				(!mei_cl_is_connected(cl)));
		mutex_lock(&dev->device_lock);
		/* wait_event_interruptible returns -ERESTARTSYS */
		if (rets) {
			if (signal_pending(current))
				rets = -EINTR;
			goto err;
		}
		if (cl->writing_state != MEI_WRITE_COMPLETE) {
			rets = -EFAULT;
			goto err;
		}
	}

	rets = len;
err:
	cl_dbg(dev, cl, "rpm: autosuspend\n");
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
free:
	mei_io_cb_free(cb);

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
		mei_tx_cb_dequeue(cb);
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
		if (!mei_cl_is_fixed_address(cl) &&
		    !WARN_ON(!cl->rx_flow_ctrl_creds))
			cl->rx_flow_ctrl_creds--;
		if (!mei_cl_bus_rx_event(cl))
			wake_up_interruptible(&cl->rx_wait);
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
