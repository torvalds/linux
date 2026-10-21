// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023-2026 Intel Corporation */
#include <linux/cleanup.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/uuid.h>

#include "fw_client.h"
#include "host_client.h"
#include "issei_dev.h"

static inline u8 __issei_cl_fw_id(const struct issei_host_client *cl)
{
	return cl->fw_cl ? cl->fw_cl->id : 0;
}

#define ISSEI_CL_FMT "cl:host=%02d fw=%02d "

#define cl_dbg(_dev_, _cl_, format, arg...) do { \
	struct issei_host_client *_l_cl_ = _cl_; \
	dev_dbg(&(_dev_)->dev, ISSEI_CL_FMT format, _l_cl_->id, \
		__issei_cl_fw_id(_l_cl_), ##arg); \
} while (0)

#define cl_warn(_dev_, _cl_, format, arg...) do { \
	struct issei_host_client *_l_cl_ = _cl_; \
	dev_warn(&(_dev_)->dev, ISSEI_CL_FMT format, _l_cl_->id, \
		__issei_cl_fw_id(_l_cl_), ##arg); \
} while (0)

#define cl_err(_dev_, _cl_, format, arg...) do { \
	struct issei_host_client *_l_cl_ = _cl_; \
	dev_err(&(_dev_)->dev, ISSEI_CL_FMT format, _l_cl_->id, \
		__issei_cl_fw_id(_l_cl_), ##arg); \
} while (0)

static void __issei_cl_clean_wbuf(struct issei_write_buf *wbuf)
{
	list_del(&wbuf->list);
	kfree(wbuf->data);
	kfree(wbuf);
}

static void __issei_cl_release_rbuf(struct issei_host_client *cl)
{
	cl->read_data = NULL;
	cl->read_data_size = 0;
}

static void __issei_cl_clean_rbuf(struct issei_host_client *cl)
{
	kfree(cl->read_data);
	__issei_cl_release_rbuf(cl);
}

static void __issei_cl_clean_all_wbuf(struct issei_device *idev, struct issei_host_client *cl)
{
	struct issei_write_buf *wbuf, *next;

	if (!cl->write_in_progress)
		return;
	list_for_each_entry_safe(wbuf, next, &idev->write_queue, list) {
		if (wbuf->cl == cl) {
			__issei_cl_clean_wbuf(wbuf);
			break;
		}
	}
	cl->write_in_progress = false;
	/* synchronized under host client mutex */
	if (waitqueue_active(&cl->write_wait))
		wake_up_interruptible(&cl->write_wait);
}

static struct issei_host_client *__issei_cl_by_id(struct issei_device *idev, u16 id)
{
	struct issei_host_client *cl;

	list_for_each_entry(cl, &idev->host_client_list, list) {
		if (cl->id == id)
			return cl;
	}
	return NULL;
}

static void __issei_cl_disconnect(struct issei_device *idev, struct issei_host_client *cl)
{
	if (cl->state == ISSEI_HOST_CL_STATE_DISCONNECTED)
		return;

	__issei_cl_clean_all_wbuf(idev, cl);

	if (!WARN_ON(!cl->fw_cl)) {
		issei_fw_cl_disconnect(cl->fw_cl);
		cl->fw_cl = NULL;
	}
	cl->state = ISSEI_HOST_CL_STATE_DISCONNECTED;

	if (cl->read_data)
		__issei_cl_clean_rbuf(cl);
	/* synchronized under host client mutex */
	if (waitqueue_active(&cl->read_wait))
		wake_up_interruptible(&cl->read_wait);
	cl_dbg(idev, cl, "Disconnected\n");
}

static void __issei_cl_init(struct issei_host_client *cl, struct issei_device *idev,
			    u16 id, struct file *fp)
{
	INIT_LIST_HEAD(&cl->list);
	cl->idev = idev;
	cl->id = id;
	cl->state = ISSEI_HOST_CL_STATE_DISCONNECTED;
	cl->fp = fp;
	init_waitqueue_head(&cl->write_wait);
	init_waitqueue_head(&cl->read_wait);
	__issei_cl_release_rbuf(cl);
}

/**
 * issei_cl_create - create the host client
 * @idev: issei device
 * @fp: file pointer to associate with host client
 *
 * Return: client pointer on success, ERR_PTR on error
 */
struct issei_host_client *issei_cl_create(struct issei_device *idev, struct file *fp)
{
	struct issei_host_client *cl;
	u16 id;

	guard(mutex)(&idev->client_lock);

	if (idev->host_client_count == ISSEI_HOST_CLIENTS_MAX) {
		dev_err(&idev->dev, "Maximum open clients %d is reached.\n",
			ISSEI_HOST_CLIENTS_MAX);
		return ERR_PTR(-EMFILE);
	}

	do {
		if (check_add_overflow(idev->host_client_last_id, 1, &id)) /* overflow */
			id = 1;
		idev->host_client_last_id = id;
		/* Not an endless loop as we have less clients then id's */
	} while (__issei_cl_by_id(idev, id));

	cl = kzalloc_obj(*cl);
	if (!cl)
		return ERR_PTR(-ENOMEM);

	__issei_cl_init(cl, idev, id, fp);
	list_add_tail(&cl->list, &idev->host_client_list);
	idev->host_client_count++;

	cl_dbg(idev, cl, "Created\n");
	return cl;
}

/**
 * issei_cl_remove - disconnect and free the host client
 * @cl: host client
 */
void issei_cl_remove(struct issei_host_client *cl)
{
	struct issei_device *idev;

	/* don't shout on error exit path */
	if (!cl)
		return;

	idev = cl->idev;

	guard(mutex)(&idev->client_lock);

	idev->host_client_count--;
	list_del(&cl->list);

	__issei_cl_disconnect(idev, cl);

	cl_dbg(idev, cl, "Removed\n");
	kfree(cl);
}

/**
 * issei_cl_connect - connect between FW and host client
 * @cl: host client
 * @uuid: FW client unique ID
 * @mtu: memory for FW client max message size
 * @ver: memory for FW client version
 * @flags: memory for FW client flags
 *
 * Search for firmware client by UUID and connect it to provided
 * host client, if not already connected to some client.
 *
 * Return: 0 on success, <0 on error
 */
int issei_cl_connect(struct issei_host_client *cl, const uuid_t *uuid, u32 *mtu, u8 *ver,
		     u32 *flags)
{
	struct issei_device *idev = cl->idev;
	struct issei_fw_client *fw_cl;
	int ret;

	guard(mutex)(&idev->client_lock);

	if (cl->state == ISSEI_HOST_CL_STATE_CONNECTED) {
		cl_err(idev, cl, "Already connected\n");
		return -EISCONN;
	}

	fw_cl = issei_fw_cl_find_by_uuid(idev, uuid);
	if (!fw_cl) {
		cl_dbg(idev, cl, "FW client %pUb not found\n", uuid);
		return -ENOTTY;
	}

	ret = issei_fw_cl_connect(fw_cl, cl); /* calls kobject_get for fw_cl on success */
	kobject_put(&fw_cl->kobj);
	if (ret) {
		cl_err(idev, cl, "FW client is already connected ret = %d\n", ret);
		return ret;
	}

	cl->fw_cl = fw_cl;
	cl->state = ISSEI_HOST_CL_STATE_CONNECTED;

	*mtu = fw_cl->mtu;
	*ver = fw_cl->ver;
	*flags = fw_cl->flags;
	cl_dbg(idev, cl, "Connected\n");
	return 0;
}

/**
 * issei_cl_disconnect - disconnect between FW and host client
 * @cl: host client
 *
 * Return: 0 on success, -ENOTCONN if not connected
 */
int issei_cl_disconnect(struct issei_host_client *cl)
{
	struct issei_device *idev = cl->idev;

	guard(mutex)(&idev->client_lock);

	if (cl->state != ISSEI_HOST_CL_STATE_CONNECTED)
		return -ENOTCONN;
	__issei_cl_disconnect(idev, cl);
	return 0;
}

/**
 * issei_cl_all_disconnect - disconnect all FW clients
 * @idev: issei device
 */
void issei_cl_all_disconnect(struct issei_device *idev)
{
	struct issei_host_client *cl;

	guard(mutex)(&idev->client_lock);

	list_for_each_entry(cl, &idev->host_client_list, list)
		__issei_cl_disconnect(idev, cl);
}

/**
 * issei_cl_write - enqueue write request
 * @cl: host client
 * @buf: buffer to write
 * @buf_size: buffer size
 *
 * Add write request to the write queue and wakes working thread.
 * This call takes ownership of buf memory, if succeeded.
 *
 * Return: size of data on success, <0 on error
 */
ssize_t issei_cl_write(struct issei_host_client *cl, const u8 *buf, size_t buf_size)
{
	struct issei_device *idev = cl->idev;
	struct issei_write_buf *wbuf;

	guard(mutex)(&idev->client_lock);

	if (cl->state != ISSEI_HOST_CL_STATE_CONNECTED)
		return -ENOTCONN;

	if (cl->write_in_progress) {
		cl_dbg(idev, cl, "Another write is in progress\n");
		return -EAGAIN;
	}

	if (buf_size > cl->fw_cl->mtu) {
		cl_err(idev, cl, "Write is too big %zu > %u\n", buf_size, cl->fw_cl->mtu);
		return -EFBIG;
	}

	wbuf = kmalloc_obj(*wbuf);
	if (!wbuf)
		return -ENOMEM;
	wbuf->cl = cl;
	wbuf->data = buf;
	wbuf->data_size = buf_size;
	list_add_tail(&wbuf->list, &idev->write_queue);
	cl->write_in_progress = true;
	cl_dbg(idev, cl, "Write queued %zu bytes\n", buf_size);

	issei_poke_process_thread(idev);

	return buf_size;
}

/**
 * issei_cl_write_from_queue - writes first request from queue to firmware
 * @idev: issei device
 *
 * Tries to write first request from the write queue to firmware.
 * Releases buf memory, if succeeded.
 *
 * Return: 0 on success, <0 on error
 */
int issei_cl_write_from_queue(struct issei_device *idev)
{
	struct issei_write_buf *wbuf;
	struct issei_dma_data data;
	struct issei_host_client *cl;
	int ret;

	guard(mutex)(&idev->client_lock);

	wbuf = list_first_entry_or_null(&idev->write_queue, struct issei_write_buf, list);
	if (!wbuf)
		return 0;

	cl = wbuf->cl;

	data.fw_id = cl->fw_cl->id;
	data.host_id = cl->id;
	data.flags = 0;
	data.status = 0;
	data.length = wbuf->data_size;
	data.buf = (void *)wbuf->data;
	ret = issei_dma_write(idev, &data);
	if (ret == -EBUSY)
		return 0;
	if (ret == -EIO)
		return ret;
	if (ret >= 0)
		idev->ops->irq_write_generate(idev);
	cl->write_in_progress = false;
	/* synchronized under host client mutex */
	if (waitqueue_active(&cl->write_wait))
		wake_up_interruptible(&cl->write_wait);
	cl_dbg(idev, cl, "Write %zu bytes\n", wbuf->data_size);
	__issei_cl_clean_wbuf(wbuf);
	return 0;
}

static struct issei_host_client *__issei_cl_read_buf_check(struct issei_device *idev, u16 fw_id,
							   u16 host_id, size_t buf_size)
{
	struct issei_host_client *cl;

	cl = __issei_cl_by_id(idev, host_id);
	if (!cl) {
		dev_dbg(&idev->dev, "No client %u\n", host_id);
		return ERR_PTR(-ENOTTY);
	}

	if (cl->state != ISSEI_HOST_CL_STATE_CONNECTED) {
		cl_dbg(idev, cl, "Not connected\n");
		return ERR_PTR(-ENODEV);
	}
	if (cl->fw_cl->id != fw_id) {
		cl_dbg(idev, cl, "Wrong firmware client %u ?= %u\n", cl->fw_cl->id, fw_id);
		return ERR_PTR(-ENODEV);
	}

	if (buf_size > cl->fw_cl->mtu) {
		cl_err(idev, cl, "Read is too big %zu > %u\n", buf_size, cl->fw_cl->mtu);
		__issei_cl_disconnect(idev, cl);
		return NULL;
	}

	if (cl->read_data) {
		cl_err(idev, cl, "Previous data was not read by user-space, disconnecting\n");
		__issei_cl_disconnect(idev, cl);
		return NULL;
	}

	return cl;
}

/**
 * issei_cl_read_buf - process data from firmware
 * @idev: issei device
 * @fw_id: firmware client id
 * @host_id: host client id
 * @buf: buffer with data
 * @buf_size: buffer size
 *
 * Puts data from firmware into provided host client storage.
 * Free buffer or consume it.
 *
 * Return: 0 on success or recoverable error, <0 on unrecoverable error
 */
int issei_cl_read_buf(struct issei_device *idev, u16 fw_id, u16 host_id, u8 *buf, size_t buf_size)
{
	struct issei_host_client *cl;

	guard(mutex)(&idev->client_lock);

	cl = __issei_cl_read_buf_check(idev, fw_id, host_id, buf_size);
	if (IS_ERR_OR_NULL(cl)) {
		kfree(buf);
		return PTR_ERR(cl);
	}

	cl->read_data = buf;
	cl->read_data_size = buf_size;

	/* synchronized under host client mutex */
	if (waitqueue_active(&cl->read_wait))
		wake_up_interruptible(&cl->read_wait);
	cl_dbg(idev, cl, "Read %zu bytes\n", buf_size);

	return 0;
}

/**
 * issei_cl_read - read data from queue to provided buffer
 * @cl: host client
 * @buf: buffer to store data
 * @buf_size: buffer size
 *
 * Tries to take data buffer from client and return it to caller.
 * The caller receives ownership of the data buffer.
 *
 * Return: read data size on success, <0 on error
 */
ssize_t issei_cl_read(struct issei_host_client *cl, u8 **buf, size_t buf_size)
{
	struct issei_device *idev = cl->idev;
	size_t read_data_size;

	guard(mutex)(&idev->client_lock);

	if (cl->state != ISSEI_HOST_CL_STATE_CONNECTED)
		return -ENOTCONN;

	if (!cl->read_data)
		return -ENOENT;

	if (cl->read_data_size > buf_size) {
		cl_err(idev, cl, "Buffer is too small %zu > %zu\n",
		       cl->read_data_size, buf_size);
		return -EFBIG;
	}

	*buf = cl->read_data;
	read_data_size = cl->read_data_size;
	cl_dbg(idev, cl, "Read by client %zu bytes\n", read_data_size);
	__issei_cl_release_rbuf(cl);
	return read_data_size;
}

/**
 * issei_cl_check_read - check if client has data to read
 *
 * @cl: host client
 *
 * Return: 1 - data available, 0 - no data, < 0 on error
 */
int issei_cl_check_read(struct issei_host_client *cl)
{
	struct issei_device *idev = cl->idev;

	guard(mutex)(&idev->client_lock);

	if (cl->state != ISSEI_HOST_CL_STATE_CONNECTED)
		return -ENOTCONN;
	if (!cl->read_data)
		return 0;
	return 1;
}

/**
 * issei_cl_check_write - check if client is ready to write
 *
 * @cl: host client
 *
 * Return: 1 - can not write, 0 - can write, < 0 on error
 */
int issei_cl_check_write(struct issei_host_client *cl)
{
	struct issei_device *idev = cl->idev;

	guard(mutex)(&idev->client_lock);

	if (cl->state != ISSEI_HOST_CL_STATE_CONNECTED)
		return -ENOTCONN;
	if (cl->write_in_progress)
		return 1;
	return 0;
}

void issei_cl_clean_all_wbuf(struct issei_host_client *cl)
{
	struct issei_device *idev = cl->idev;

	guard(mutex)(&idev->client_lock);

	__issei_cl_clean_all_wbuf(idev, cl);
}
