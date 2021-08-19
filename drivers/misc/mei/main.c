// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2003-2020, Intel Corporation. All rights reserved.
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/sched/signal.h>
#include <linux/uuid.h>
#include <linux/compat.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "client.h"

static struct class *mei_class;
static dev_t mei_devt;
#define MEI_MAX_DEVS  MINORMASK
static DEFINE_MUTEX(mei_minor_lock);
static DEFINE_IDR(mei_idr);

/**
 * mei_open - the open function
 *
 * @inode: pointer to inode structure
 * @file: pointer to file structure
 *
 * Return: 0 on success, <0 on error
 */
static int mei_open(struct inode *inode, struct file *file)
{
	struct mei_device *dev;
	struct mei_cl *cl;

	int err;

	dev = container_of(inode->i_cdev, struct mei_device, cdev);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	if (dev->dev_state != MEI_DEV_ENABLED) {
		dev_dbg(dev->dev, "dev_state != MEI_ENABLED  dev_state = %s\n",
		    mei_dev_state_str(dev->dev_state));
		err = -ENODEV;
		goto err_unlock;
	}

	cl = mei_cl_alloc_linked(dev);
	if (IS_ERR(cl)) {
		err = PTR_ERR(cl);
		goto err_unlock;
	}

	cl->fp = file;
	file->private_data = cl;

	mutex_unlock(&dev->device_lock);

	return nonseekable_open(inode, file);

err_unlock:
	mutex_unlock(&dev->device_lock);
	return err;
}

/**
 * mei_cl_vtag_remove_by_fp - remove vtag that corresponds to fp from list
 *
 * @cl: host client
 * @fp: pointer to file structure
 *
 */
static void mei_cl_vtag_remove_by_fp(const struct mei_cl *cl,
				     const struct file *fp)
{
	struct mei_cl_vtag *vtag_l, *next;

	list_for_each_entry_safe(vtag_l, next, &cl->vtag_map, list) {
		if (vtag_l->fp == fp) {
			list_del(&vtag_l->list);
			kfree(vtag_l);
			return;
		}
	}
}

/**
 * mei_release - the release function
 *
 * @inode: pointer to inode structure
 * @file: pointer to file structure
 *
 * Return: 0 on success, <0 on error
 */
static int mei_release(struct inode *inode, struct file *file)
{
	struct mei_cl *cl = file->private_data;
	struct mei_device *dev;
	int rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);

	mei_cl_vtag_remove_by_fp(cl, file);

	if (!list_empty(&cl->vtag_map)) {
		cl_dbg(dev, cl, "not the last vtag\n");
		mei_cl_flush_queues(cl, file);
		rets = 0;
		goto out;
	}

	rets = mei_cl_disconnect(cl);
	/*
	 * Check again: This is necessary since disconnect releases the lock
	 * and another client can connect in the meantime.
	 */
	if (!list_empty(&cl->vtag_map)) {
		cl_dbg(dev, cl, "not the last vtag after disconnect\n");
		mei_cl_flush_queues(cl, file);
		goto out;
	}

	mei_cl_flush_queues(cl, NULL);
	cl_dbg(dev, cl, "removing\n");

	mei_cl_unlink(cl);
	kfree(cl);

out:
	file->private_data = NULL;

	mutex_unlock(&dev->device_lock);
	return rets;
}


/**
 * mei_read - the read function.
 *
 * @file: pointer to file structure
 * @ubuf: pointer to user buffer
 * @length: buffer length
 * @offset: data offset in buffer
 *
 * Return: >=0 data length on success , <0 on error
 */
static ssize_t mei_read(struct file *file, char __user *ubuf,
			size_t length, loff_t *offset)
{
	struct mei_cl *cl = file->private_data;
	struct mei_device *dev;
	struct mei_cl_cb *cb = NULL;
	bool nonblock = !!(file->f_flags & O_NONBLOCK);
	ssize_t rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;


	mutex_lock(&dev->device_lock);
	if (dev->dev_state != MEI_DEV_ENABLED) {
		rets = -ENODEV;
		goto out;
	}

	if (length == 0) {
		rets = 0;
		goto out;
	}

	if (ubuf == NULL) {
		rets = -EMSGSIZE;
		goto out;
	}

	cb = mei_cl_read_cb(cl, file);
	if (cb)
		goto copy_buffer;

	if (*offset > 0)
		*offset = 0;

	rets = mei_cl_read_start(cl, length, file);
	if (rets && rets != -EBUSY) {
		cl_dbg(dev, cl, "mei start read failure status = %zd\n", rets);
		goto out;
	}

	if (nonblock) {
		rets = -EAGAIN;
		goto out;
	}

	mutex_unlock(&dev->device_lock);
	if (wait_event_interruptible(cl->rx_wait,
				     mei_cl_read_cb(cl, file) ||
				     !mei_cl_is_connected(cl))) {
		if (signal_pending(current))
			return -EINTR;
		return -ERESTARTSYS;
	}
	mutex_lock(&dev->device_lock);

	if (!mei_cl_is_connected(cl)) {
		rets = -ENODEV;
		goto out;
	}

	cb = mei_cl_read_cb(cl, file);
	if (!cb) {
		rets = 0;
		goto out;
	}

copy_buffer:
	/* now copy the data to user space */
	if (cb->status) {
		rets = cb->status;
		cl_dbg(dev, cl, "read operation failed %zd\n", rets);
		goto free;
	}

	cl_dbg(dev, cl, "buf.size = %zu buf.idx = %zu offset = %lld\n",
	       cb->buf.size, cb->buf_idx, *offset);
	if (*offset >= cb->buf_idx) {
		rets = 0;
		goto free;
	}

	/* length is being truncated to PAGE_SIZE,
	 * however buf_idx may point beyond that */
	length = min_t(size_t, length, cb->buf_idx - *offset);

	if (copy_to_user(ubuf, cb->buf.data + *offset, length)) {
		dev_dbg(dev->dev, "failed to copy data to userland\n");
		rets = -EFAULT;
		goto free;
	}

	rets = length;
	*offset += length;
	/* not all data was read, keep the cb */
	if (*offset < cb->buf_idx)
		goto out;

free:
	mei_cl_del_rd_completed(cl, cb);
	*offset = 0;

out:
	cl_dbg(dev, cl, "end mei read rets = %zd\n", rets);
	mutex_unlock(&dev->device_lock);
	return rets;
}

/**
 * mei_cl_vtag_by_fp - obtain the vtag by file pointer
 *
 * @cl: host client
 * @fp: pointer to file structure
 *
 * Return: vtag value on success, otherwise 0
 */
static u8 mei_cl_vtag_by_fp(const struct mei_cl *cl, const struct file *fp)
{
	struct mei_cl_vtag *cl_vtag;

	if (!fp)
		return 0;

	list_for_each_entry(cl_vtag, &cl->vtag_map, list)
		if (cl_vtag->fp == fp)
			return cl_vtag->vtag;
	return 0;
}

/**
 * mei_write - the write function.
 *
 * @file: pointer to file structure
 * @ubuf: pointer to user buffer
 * @length: buffer length
 * @offset: data offset in buffer
 *
 * Return: >=0 data length on success , <0 on error
 */
static ssize_t mei_write(struct file *file, const char __user *ubuf,
			 size_t length, loff_t *offset)
{
	struct mei_cl *cl = file->private_data;
	struct mei_cl_cb *cb;
	struct mei_device *dev;
	ssize_t rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);

	if (dev->dev_state != MEI_DEV_ENABLED) {
		rets = -ENODEV;
		goto out;
	}

	if (!mei_cl_is_connected(cl)) {
		cl_err(dev, cl, "is not connected");
		rets = -ENODEV;
		goto out;
	}

	if (!mei_me_cl_is_active(cl->me_cl)) {
		rets = -ENOTTY;
		goto out;
	}

	if (length > mei_cl_mtu(cl)) {
		rets = -EFBIG;
		goto out;
	}

	if (length == 0) {
		rets = 0;
		goto out;
	}

	while (cl->tx_cb_queued >= dev->tx_queue_limit) {
		if (file->f_flags & O_NONBLOCK) {
			rets = -EAGAIN;
			goto out;
		}
		mutex_unlock(&dev->device_lock);
		rets = wait_event_interruptible(cl->tx_wait,
				cl->writing_state == MEI_WRITE_COMPLETE ||
				(!mei_cl_is_connected(cl)));
		mutex_lock(&dev->device_lock);
		if (rets) {
			if (signal_pending(current))
				rets = -EINTR;
			goto out;
		}
		if (!mei_cl_is_connected(cl)) {
			rets = -ENODEV;
			goto out;
		}
	}

	cb = mei_cl_alloc_cb(cl, length, MEI_FOP_WRITE, file);
	if (!cb) {
		rets = -ENOMEM;
		goto out;
	}
	cb->vtag = mei_cl_vtag_by_fp(cl, file);

	rets = copy_from_user(cb->buf.data, ubuf, length);
	if (rets) {
		dev_dbg(dev->dev, "failed to copy data from userland\n");
		rets = -EFAULT;
		mei_io_cb_free(cb);
		goto out;
	}

	rets = mei_cl_write(cl, cb);
out:
	mutex_unlock(&dev->device_lock);
	return rets;
}

/**
 * mei_ioctl_connect_client - the connect to fw client IOCTL function
 *
 * @file: private data of the file object
 * @in_client_uuid: requested UUID for connection
 * @client: IOCTL connect data, output parameters
 *
 * Locking: called under "dev->device_lock" lock
 *
 * Return: 0 on success, <0 on failure.
 */
static int mei_ioctl_connect_client(struct file *file,
				    const uuid_le *in_client_uuid,
				    struct mei_client *client)
{
	struct mei_device *dev;
	struct mei_me_client *me_cl;
	struct mei_cl *cl;
	int rets;

	cl = file->private_data;
	dev = cl->dev;

	if (cl->state != MEI_FILE_INITIALIZING &&
	    cl->state != MEI_FILE_DISCONNECTED)
		return  -EBUSY;

	/* find ME client we're trying to connect to */
	me_cl = mei_me_cl_by_uuid(dev, in_client_uuid);
	if (!me_cl) {
		dev_dbg(dev->dev, "Cannot connect to FW Client UUID = %pUl\n",
			in_client_uuid);
		rets = -ENOTTY;
		goto end;
	}

	if (me_cl->props.fixed_address) {
		bool forbidden = dev->override_fixed_address ?
			 !dev->allow_fixed_address : !dev->hbm_f_fa_supported;
		if (forbidden) {
			dev_dbg(dev->dev, "Connection forbidden to FW Client UUID = %pUl\n",
				in_client_uuid);
			rets = -ENOTTY;
			goto end;
		}
	}

	dev_dbg(dev->dev, "Connect to FW Client ID = %d\n",
			me_cl->client_id);
	dev_dbg(dev->dev, "FW Client - Protocol Version = %d\n",
			me_cl->props.protocol_version);
	dev_dbg(dev->dev, "FW Client - Max Msg Len = %d\n",
			me_cl->props.max_msg_length);

	/* prepare the output buffer */
	client->max_msg_length = me_cl->props.max_msg_length;
	client->protocol_version = me_cl->props.protocol_version;
	dev_dbg(dev->dev, "Can connect?\n");

	rets = mei_cl_connect(cl, me_cl, file);

end:
	mei_me_cl_put(me_cl);
	return rets;
}

/**
 * mei_vt_support_check - check if client support vtags
 *
 * Locking: called under "dev->device_lock" lock
 *
 * @dev: mei_device
 * @uuid: client UUID
 *
 * Return:
 *	0 - supported
 *	-ENOTTY - no such client
 *	-EOPNOTSUPP - vtags are not supported by client
 */
static int mei_vt_support_check(struct mei_device *dev, const uuid_le *uuid)
{
	struct mei_me_client *me_cl;
	int ret;

	if (!dev->hbm_f_vt_supported)
		return -EOPNOTSUPP;

	me_cl = mei_me_cl_by_uuid(dev, uuid);
	if (!me_cl) {
		dev_dbg(dev->dev, "Cannot connect to FW Client UUID = %pUl\n",
			uuid);
		return -ENOTTY;
	}
	ret = me_cl->props.vt_supported ? 0 : -EOPNOTSUPP;
	mei_me_cl_put(me_cl);

	return ret;
}

/**
 * mei_ioctl_connect_vtag - connect to fw client with vtag IOCTL function
 *
 * @file: private data of the file object
 * @in_client_uuid: requested UUID for connection
 * @client: IOCTL connect data, output parameters
 * @vtag: vm tag
 *
 * Locking: called under "dev->device_lock" lock
 *
 * Return: 0 on success, <0 on failure.
 */
static int mei_ioctl_connect_vtag(struct file *file,
				  const uuid_le *in_client_uuid,
				  struct mei_client *client,
				  u8 vtag)
{
	struct mei_device *dev;
	struct mei_cl *cl;
	struct mei_cl *pos;
	struct mei_cl_vtag *cl_vtag;

	cl = file->private_data;
	dev = cl->dev;

	dev_dbg(dev->dev, "FW Client %pUl vtag %d\n", in_client_uuid, vtag);

	switch (cl->state) {
	case MEI_FILE_DISCONNECTED:
		if (mei_cl_vtag_by_fp(cl, file) != vtag) {
			dev_err(dev->dev, "reconnect with different vtag\n");
			return -EINVAL;
		}
		break;
	case MEI_FILE_INITIALIZING:
		/* malicious connect from another thread may push vtag */
		if (!IS_ERR(mei_cl_fp_by_vtag(cl, vtag))) {
			dev_err(dev->dev, "vtag already filled\n");
			return -EINVAL;
		}

		list_for_each_entry(pos, &dev->file_list, link) {
			if (pos == cl)
				continue;
			if (!pos->me_cl)
				continue;

			/* only search for same UUID */
			if (uuid_le_cmp(*mei_cl_uuid(pos), *in_client_uuid))
				continue;

			/* if tag already exist try another fp */
			if (!IS_ERR(mei_cl_fp_by_vtag(pos, vtag)))
				continue;

			/* replace cl with acquired one */
			dev_dbg(dev->dev, "replacing with existing cl\n");
			mei_cl_unlink(cl);
			kfree(cl);
			file->private_data = pos;
			cl = pos;
			break;
		}

		cl_vtag = mei_cl_vtag_alloc(file, vtag);
		if (IS_ERR(cl_vtag))
			return -ENOMEM;

		list_add_tail(&cl_vtag->list, &cl->vtag_map);
		break;
	default:
		return -EBUSY;
	}

	while (cl->state != MEI_FILE_INITIALIZING &&
	       cl->state != MEI_FILE_DISCONNECTED &&
	       cl->state != MEI_FILE_CONNECTED) {
		mutex_unlock(&dev->device_lock);
		wait_event_timeout(cl->wait,
				   (cl->state == MEI_FILE_CONNECTED ||
				    cl->state == MEI_FILE_DISCONNECTED ||
				    cl->state == MEI_FILE_DISCONNECT_REQUIRED ||
				    cl->state == MEI_FILE_DISCONNECT_REPLY),
				   mei_secs_to_jiffies(MEI_CL_CONNECT_TIMEOUT));
		mutex_lock(&dev->device_lock);
	}

	if (!mei_cl_is_connected(cl))
		return mei_ioctl_connect_client(file, in_client_uuid, client);

	client->max_msg_length = cl->me_cl->props.max_msg_length;
	client->protocol_version = cl->me_cl->props.protocol_version;

	return 0;
}

/**
 * mei_ioctl_client_notify_request -
 *     propagate event notification request to client
 *
 * @file: pointer to file structure
 * @request: 0 - disable, 1 - enable
 *
 * Return: 0 on success , <0 on error
 */
static int mei_ioctl_client_notify_request(const struct file *file, u32 request)
{
	struct mei_cl *cl = file->private_data;

	if (request != MEI_HBM_NOTIFICATION_START &&
	    request != MEI_HBM_NOTIFICATION_STOP)
		return -EINVAL;

	return mei_cl_notify_request(cl, file, (u8)request);
}

/**
 * mei_ioctl_client_notify_get -  wait for notification request
 *
 * @file: pointer to file structure
 * @notify_get: 0 - disable, 1 - enable
 *
 * Return: 0 on success , <0 on error
 */
static int mei_ioctl_client_notify_get(const struct file *file, u32 *notify_get)
{
	struct mei_cl *cl = file->private_data;
	bool notify_ev;
	bool block = (file->f_flags & O_NONBLOCK) == 0;
	int rets;

	rets = mei_cl_notify_get(cl, block, &notify_ev);
	if (rets)
		return rets;

	*notify_get = notify_ev ? 1 : 0;
	return 0;
}

/**
 * mei_ioctl - the IOCTL function
 *
 * @file: pointer to file structure
 * @cmd: ioctl command
 * @data: pointer to mei message structure
 *
 * Return: 0 on success , <0 on error
 */
static long mei_ioctl(struct file *file, unsigned int cmd, unsigned long data)
{
	struct mei_device *dev;
	struct mei_cl *cl = file->private_data;
	struct mei_connect_client_data conn;
	struct mei_connect_client_data_vtag conn_vtag;
	const uuid_le *cl_uuid;
	struct mei_client *props;
	u8 vtag;
	u32 notify_get, notify_req;
	int rets;


	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	dev_dbg(dev->dev, "IOCTL cmd = 0x%x", cmd);

	mutex_lock(&dev->device_lock);
	if (dev->dev_state != MEI_DEV_ENABLED) {
		rets = -ENODEV;
		goto out;
	}

	switch (cmd) {
	case IOCTL_MEI_CONNECT_CLIENT:
		dev_dbg(dev->dev, ": IOCTL_MEI_CONNECT_CLIENT.\n");
		if (copy_from_user(&conn, (char __user *)data, sizeof(conn))) {
			dev_dbg(dev->dev, "failed to copy data from userland\n");
			rets = -EFAULT;
			goto out;
		}
		cl_uuid = &conn.in_client_uuid;
		props = &conn.out_client_properties;
		vtag = 0;

		rets = mei_vt_support_check(dev, cl_uuid);
		if (rets == -ENOTTY)
			goto out;
		if (!rets)
			rets = mei_ioctl_connect_vtag(file, cl_uuid, props,
						      vtag);
		else
			rets = mei_ioctl_connect_client(file, cl_uuid, props);
		if (rets)
			goto out;

		/* if all is ok, copying the data back to user. */
		if (copy_to_user((char __user *)data, &conn, sizeof(conn))) {
			dev_dbg(dev->dev, "failed to copy data to userland\n");
			rets = -EFAULT;
			goto out;
		}

		break;

	case IOCTL_MEI_CONNECT_CLIENT_VTAG:
		dev_dbg(dev->dev, "IOCTL_MEI_CONNECT_CLIENT_VTAG\n");
		if (copy_from_user(&conn_vtag, (char __user *)data,
				   sizeof(conn_vtag))) {
			dev_dbg(dev->dev, "failed to copy data from userland\n");
			rets = -EFAULT;
			goto out;
		}

		cl_uuid = &conn_vtag.connect.in_client_uuid;
		props = &conn_vtag.out_client_properties;
		vtag = conn_vtag.connect.vtag;

		rets = mei_vt_support_check(dev, cl_uuid);
		if (rets == -EOPNOTSUPP)
			dev_dbg(dev->dev, "FW Client %pUl does not support vtags\n",
				cl_uuid);
		if (rets)
			goto out;

		if (!vtag) {
			dev_dbg(dev->dev, "vtag can't be zero\n");
			rets = -EINVAL;
			goto out;
		}

		rets = mei_ioctl_connect_vtag(file, cl_uuid, props, vtag);
		if (rets)
			goto out;

		/* if all is ok, copying the data back to user. */
		if (copy_to_user((char __user *)data, &conn_vtag,
				 sizeof(conn_vtag))) {
			dev_dbg(dev->dev, "failed to copy data to userland\n");
			rets = -EFAULT;
			goto out;
		}

		break;

	case IOCTL_MEI_NOTIFY_SET:
		dev_dbg(dev->dev, ": IOCTL_MEI_NOTIFY_SET.\n");
		if (copy_from_user(&notify_req,
				   (char __user *)data, sizeof(notify_req))) {
			dev_dbg(dev->dev, "failed to copy data from userland\n");
			rets = -EFAULT;
			goto out;
		}
		rets = mei_ioctl_client_notify_request(file, notify_req);
		break;

	case IOCTL_MEI_NOTIFY_GET:
		dev_dbg(dev->dev, ": IOCTL_MEI_NOTIFY_GET.\n");
		rets = mei_ioctl_client_notify_get(file, &notify_get);
		if (rets)
			goto out;

		dev_dbg(dev->dev, "copy connect data to user\n");
		if (copy_to_user((char __user *)data,
				&notify_get, sizeof(notify_get))) {
			dev_dbg(dev->dev, "failed to copy data to userland\n");
			rets = -EFAULT;
			goto out;

		}
		break;

	default:
		rets = -ENOIOCTLCMD;
	}

out:
	mutex_unlock(&dev->device_lock);
	return rets;
}

/**
 * mei_poll - the poll function
 *
 * @file: pointer to file structure
 * @wait: pointer to poll_table structure
 *
 * Return: poll mask
 */
static __poll_t mei_poll(struct file *file, poll_table *wait)
{
	__poll_t req_events = poll_requested_events(wait);
	struct mei_cl *cl = file->private_data;
	struct mei_device *dev;
	__poll_t mask = 0;
	bool notify_en;

	if (WARN_ON(!cl || !cl->dev))
		return EPOLLERR;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);

	notify_en = cl->notify_en && (req_events & EPOLLPRI);

	if (dev->dev_state != MEI_DEV_ENABLED ||
	    !mei_cl_is_connected(cl)) {
		mask = EPOLLERR;
		goto out;
	}

	if (notify_en) {
		poll_wait(file, &cl->ev_wait, wait);
		if (cl->notify_ev)
			mask |= EPOLLPRI;
	}

	if (req_events & (EPOLLIN | EPOLLRDNORM)) {
		poll_wait(file, &cl->rx_wait, wait);

		if (mei_cl_read_cb(cl, file))
			mask |= EPOLLIN | EPOLLRDNORM;
		else
			mei_cl_read_start(cl, mei_cl_mtu(cl), file);
	}

	if (req_events & (EPOLLOUT | EPOLLWRNORM)) {
		poll_wait(file, &cl->tx_wait, wait);
		if (cl->tx_cb_queued < dev->tx_queue_limit)
			mask |= EPOLLOUT | EPOLLWRNORM;
	}

out:
	mutex_unlock(&dev->device_lock);
	return mask;
}

/**
 * mei_cl_is_write_queued - check if the client has pending writes.
 *
 * @cl: writing host client
 *
 * Return: true if client is writing, false otherwise.
 */
static bool mei_cl_is_write_queued(struct mei_cl *cl)
{
	struct mei_device *dev = cl->dev;
	struct mei_cl_cb *cb;

	list_for_each_entry(cb, &dev->write_list, list)
		if (cb->cl == cl)
			return true;
	list_for_each_entry(cb, &dev->write_waiting_list, list)
		if (cb->cl == cl)
			return true;
	return false;
}

/**
 * mei_fsync - the fsync handler
 *
 * @fp:       pointer to file structure
 * @start:    unused
 * @end:      unused
 * @datasync: unused
 *
 * Return: 0 on success, -ENODEV if client is not connected
 */
static int mei_fsync(struct file *fp, loff_t start, loff_t end, int datasync)
{
	struct mei_cl *cl = fp->private_data;
	struct mei_device *dev;
	int rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);

	if (dev->dev_state != MEI_DEV_ENABLED || !mei_cl_is_connected(cl)) {
		rets = -ENODEV;
		goto out;
	}

	while (mei_cl_is_write_queued(cl)) {
		mutex_unlock(&dev->device_lock);
		rets = wait_event_interruptible(cl->tx_wait,
				cl->writing_state == MEI_WRITE_COMPLETE ||
				!mei_cl_is_connected(cl));
		mutex_lock(&dev->device_lock);
		if (rets) {
			if (signal_pending(current))
				rets = -EINTR;
			goto out;
		}
		if (!mei_cl_is_connected(cl)) {
			rets = -ENODEV;
			goto out;
		}
	}
	rets = 0;
out:
	mutex_unlock(&dev->device_lock);
	return rets;
}

/**
 * mei_fasync - asynchronous io support
 *
 * @fd: file descriptor
 * @file: pointer to file structure
 * @band: band bitmap
 *
 * Return: negative on error,
 *         0 if it did no changes,
 *         and positive a process was added or deleted
 */
static int mei_fasync(int fd, struct file *file, int band)
{

	struct mei_cl *cl = file->private_data;

	if (!mei_cl_is_connected(cl))
		return -ENODEV;

	return fasync_helper(fd, file, band, &cl->ev_async);
}

/**
 * trc_show - mei device trc attribute show method
 *
 * @device: device pointer
 * @attr: attribute pointer
 * @buf:  char out buffer
 *
 * Return: number of the bytes printed into buf or error
 */
static ssize_t trc_show(struct device *device,
			struct device_attribute *attr, char *buf)
{
	struct mei_device *dev = dev_get_drvdata(device);
	u32 trc;
	int ret;

	ret = mei_trc_status(dev, &trc);
	if (ret)
		return ret;
	return sprintf(buf, "%08X\n", trc);
}
static DEVICE_ATTR_RO(trc);

/**
 * fw_status_show - mei device fw_status attribute show method
 *
 * @device: device pointer
 * @attr: attribute pointer
 * @buf:  char out buffer
 *
 * Return: number of the bytes printed into buf or error
 */
static ssize_t fw_status_show(struct device *device,
		struct device_attribute *attr, char *buf)
{
	struct mei_device *dev = dev_get_drvdata(device);
	struct mei_fw_status fw_status;
	int err, i;
	ssize_t cnt = 0;

	mutex_lock(&dev->device_lock);
	err = mei_fw_status(dev, &fw_status);
	mutex_unlock(&dev->device_lock);
	if (err) {
		dev_err(device, "read fw_status error = %d\n", err);
		return err;
	}

	for (i = 0; i < fw_status.count; i++)
		cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "%08X\n",
				fw_status.status[i]);
	return cnt;
}
static DEVICE_ATTR_RO(fw_status);

/**
 * hbm_ver_show - display HBM protocol version negotiated with FW
 *
 * @device: device pointer
 * @attr: attribute pointer
 * @buf:  char out buffer
 *
 * Return: number of the bytes printed into buf or error
 */
static ssize_t hbm_ver_show(struct device *device,
			    struct device_attribute *attr, char *buf)
{
	struct mei_device *dev = dev_get_drvdata(device);
	struct hbm_version ver;

	mutex_lock(&dev->device_lock);
	ver = dev->version;
	mutex_unlock(&dev->device_lock);

	return sprintf(buf, "%u.%u\n", ver.major_version, ver.minor_version);
}
static DEVICE_ATTR_RO(hbm_ver);

/**
 * hbm_ver_drv_show - display HBM protocol version advertised by driver
 *
 * @device: device pointer
 * @attr: attribute pointer
 * @buf:  char out buffer
 *
 * Return: number of the bytes printed into buf or error
 */
static ssize_t hbm_ver_drv_show(struct device *device,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u.%u\n", HBM_MAJOR_VERSION, HBM_MINOR_VERSION);
}
static DEVICE_ATTR_RO(hbm_ver_drv);

static ssize_t tx_queue_limit_show(struct device *device,
				   struct device_attribute *attr, char *buf)
{
	struct mei_device *dev = dev_get_drvdata(device);
	u8 size = 0;

	mutex_lock(&dev->device_lock);
	size = dev->tx_queue_limit;
	mutex_unlock(&dev->device_lock);

	return sysfs_emit(buf, "%u\n", size);
}

static ssize_t tx_queue_limit_store(struct device *device,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct mei_device *dev = dev_get_drvdata(device);
	u8 limit;
	unsigned int inp;
	int err;

	err = kstrtouint(buf, 10, &inp);
	if (err)
		return err;
	if (inp > MEI_TX_QUEUE_LIMIT_MAX || inp < MEI_TX_QUEUE_LIMIT_MIN)
		return -EINVAL;
	limit = inp;

	mutex_lock(&dev->device_lock);
	dev->tx_queue_limit = limit;
	mutex_unlock(&dev->device_lock);

	return count;
}
static DEVICE_ATTR_RW(tx_queue_limit);

/**
 * fw_ver_show - display ME FW version
 *
 * @device: device pointer
 * @attr: attribute pointer
 * @buf:  char out buffer
 *
 * Return: number of the bytes printed into buf or error
 */
static ssize_t fw_ver_show(struct device *device,
			   struct device_attribute *attr, char *buf)
{
	struct mei_device *dev = dev_get_drvdata(device);
	struct mei_fw_version *ver;
	ssize_t cnt = 0;
	int i;

	ver = dev->fw_ver;

	for (i = 0; i < MEI_MAX_FW_VER_BLOCKS; i++)
		cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "%u:%u.%u.%u.%u\n",
				 ver[i].platform, ver[i].major, ver[i].minor,
				 ver[i].hotfix, ver[i].buildno);
	return cnt;
}
static DEVICE_ATTR_RO(fw_ver);

/**
 * dev_state_show - display device state
 *
 * @device: device pointer
 * @attr: attribute pointer
 * @buf:  char out buffer
 *
 * Return: number of the bytes printed into buf or error
 */
static ssize_t dev_state_show(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	struct mei_device *dev = dev_get_drvdata(device);
	enum mei_dev_state dev_state;

	mutex_lock(&dev->device_lock);
	dev_state = dev->dev_state;
	mutex_unlock(&dev->device_lock);

	return sprintf(buf, "%s", mei_dev_state_str(dev_state));
}
static DEVICE_ATTR_RO(dev_state);

/**
 * dev_set_devstate: set to new device state and notify sysfs file.
 *
 * @dev: mei_device
 * @state: new device state
 */
void mei_set_devstate(struct mei_device *dev, enum mei_dev_state state)
{
	struct device *clsdev;

	if (dev->dev_state == state)
		return;

	dev->dev_state = state;

	clsdev = class_find_device_by_devt(mei_class, dev->cdev.dev);
	if (clsdev) {
		sysfs_notify(&clsdev->kobj, NULL, "dev_state");
		put_device(clsdev);
	}
}

/**
 * kind_show - display device kind
 *
 * @device: device pointer
 * @attr: attribute pointer
 * @buf: char out buffer
 *
 * Return: number of the bytes printed into buf or error
 */
static ssize_t kind_show(struct device *device,
			 struct device_attribute *attr, char *buf)
{
	struct mei_device *dev = dev_get_drvdata(device);
	ssize_t ret;

	if (dev->kind)
		ret = sprintf(buf, "%s\n", dev->kind);
	else
		ret = sprintf(buf, "%s\n", "mei");

	return ret;
}
static DEVICE_ATTR_RO(kind);

static struct attribute *mei_attrs[] = {
	&dev_attr_fw_status.attr,
	&dev_attr_hbm_ver.attr,
	&dev_attr_hbm_ver_drv.attr,
	&dev_attr_tx_queue_limit.attr,
	&dev_attr_fw_ver.attr,
	&dev_attr_dev_state.attr,
	&dev_attr_trc.attr,
	&dev_attr_kind.attr,
	NULL
};
ATTRIBUTE_GROUPS(mei);

/*
 * file operations structure will be used for mei char device.
 */
static const struct file_operations mei_fops = {
	.owner = THIS_MODULE,
	.read = mei_read,
	.unlocked_ioctl = mei_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.open = mei_open,
	.release = mei_release,
	.write = mei_write,
	.poll = mei_poll,
	.fsync = mei_fsync,
	.fasync = mei_fasync,
	.llseek = no_llseek
};

/**
 * mei_minor_get - obtain next free device minor number
 *
 * @dev:  device pointer
 *
 * Return: allocated minor, or -ENOSPC if no free minor left
 */
static int mei_minor_get(struct mei_device *dev)
{
	int ret;

	mutex_lock(&mei_minor_lock);
	ret = idr_alloc(&mei_idr, dev, 0, MEI_MAX_DEVS, GFP_KERNEL);
	if (ret >= 0)
		dev->minor = ret;
	else if (ret == -ENOSPC)
		dev_err(dev->dev, "too many mei devices\n");

	mutex_unlock(&mei_minor_lock);
	return ret;
}

/**
 * mei_minor_free - mark device minor number as free
 *
 * @dev:  device pointer
 */
static void mei_minor_free(struct mei_device *dev)
{
	mutex_lock(&mei_minor_lock);
	idr_remove(&mei_idr, dev->minor);
	mutex_unlock(&mei_minor_lock);
}

int mei_register(struct mei_device *dev, struct device *parent)
{
	struct device *clsdev; /* class device */
	int ret, devno;

	ret = mei_minor_get(dev);
	if (ret < 0)
		return ret;

	/* Fill in the data structures */
	devno = MKDEV(MAJOR(mei_devt), dev->minor);
	cdev_init(&dev->cdev, &mei_fops);
	dev->cdev.owner = parent->driver->owner;

	/* Add the device */
	ret = cdev_add(&dev->cdev, devno, 1);
	if (ret) {
		dev_err(parent, "unable to add device %d:%d\n",
			MAJOR(mei_devt), dev->minor);
		goto err_dev_add;
	}

	clsdev = device_create_with_groups(mei_class, parent, devno,
					   dev, mei_groups,
					   "mei%d", dev->minor);

	if (IS_ERR(clsdev)) {
		dev_err(parent, "unable to create device %d:%d\n",
			MAJOR(mei_devt), dev->minor);
		ret = PTR_ERR(clsdev);
		goto err_dev_create;
	}

	mei_dbgfs_register(dev, dev_name(clsdev));

	return 0;

err_dev_create:
	cdev_del(&dev->cdev);
err_dev_add:
	mei_minor_free(dev);
	return ret;
}
EXPORT_SYMBOL_GPL(mei_register);

void mei_deregister(struct mei_device *dev)
{
	int devno;

	devno = dev->cdev.dev;
	cdev_del(&dev->cdev);

	mei_dbgfs_deregister(dev);

	device_destroy(mei_class, devno);

	mei_minor_free(dev);
}
EXPORT_SYMBOL_GPL(mei_deregister);

static int __init mei_init(void)
{
	int ret;

	mei_class = class_create(THIS_MODULE, "mei");
	if (IS_ERR(mei_class)) {
		pr_err("couldn't create class\n");
		ret = PTR_ERR(mei_class);
		goto err;
	}

	ret = alloc_chrdev_region(&mei_devt, 0, MEI_MAX_DEVS, "mei");
	if (ret < 0) {
		pr_err("unable to allocate char dev region\n");
		goto err_class;
	}

	ret = mei_cl_bus_init();
	if (ret < 0) {
		pr_err("unable to initialize bus\n");
		goto err_chrdev;
	}

	return 0;

err_chrdev:
	unregister_chrdev_region(mei_devt, MEI_MAX_DEVS);
err_class:
	class_destroy(mei_class);
err:
	return ret;
}

static void __exit mei_exit(void)
{
	unregister_chrdev_region(mei_devt, MEI_MAX_DEVS);
	class_destroy(mei_class);
	mei_cl_bus_exit();
}

module_init(mei_init);
module_exit(mei_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) Management Engine Interface");
MODULE_LICENSE("GPL v2");

