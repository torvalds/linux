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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/aio.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/uuid.h>
#include <linux/compat.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>

#include "mei_dev.h"
#include <linux/mei.h>
#include "interface.h"

/* AMT device is a singleton on the platform */
static struct pci_dev *mei_pdev;

/* mei_pci_tbl - PCI Device ID Table */
static DEFINE_PCI_DEVICE_TABLE(mei_pci_tbl) = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_82946GZ)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_82G35)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_82Q965)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_82G965)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_82GM965)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_82GME965)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_82Q35)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_82G33)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_82Q33)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_82X38)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_3200)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_6)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_7)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_8)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_9)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_10)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9M_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9M_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9M_3)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9M_4)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH10_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH10_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH10_3)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH10_4)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_IBXPK_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_IBXPK_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_CPT_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_PBG_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_PPT_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_PPT_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_PPT_3)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_LPT)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_LPT_LP)},

	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, mei_pci_tbl);

static DEFINE_MUTEX(mei_mutex);


/**
 * mei_clear_list - removes all callbacks associated with file
 *		from mei_cb_list
 *
 * @dev: device structure.
 * @file: file structure
 * @mei_cb_list: callbacks list
 *
 * mei_clear_list is called to clear resources associated with file
 * when application calls close function or Ctrl-C was pressed
 *
 * returns true if callback removed from the list, false otherwise
 */
static bool mei_clear_list(struct mei_device *dev,
		struct file *file, struct list_head *mei_cb_list)
{
	struct mei_cl_cb *cb_pos = NULL;
	struct mei_cl_cb *cb_next = NULL;
	struct file *file_temp;
	bool removed = false;

	/* list all list member */
	list_for_each_entry_safe(cb_pos, cb_next, mei_cb_list, cb_list) {
		file_temp = (struct file *)cb_pos->file_object;
		/* check if list member associated with a file */
		if (file_temp == file) {
			/* remove member from the list */
			list_del(&cb_pos->cb_list);
			/* check if cb equal to current iamthif cb */
			if (dev->iamthif_current_cb == cb_pos) {
				dev->iamthif_current_cb = NULL;
				/* send flow control to iamthif client */
				mei_send_flow_control(dev, &dev->iamthif_cl);
			}
			/* free all allocated buffers */
			mei_free_cb_private(cb_pos);
			cb_pos = NULL;
			removed = true;
		}
	}
	return removed;
}

/**
 * mei_clear_lists - removes all callbacks associated with file
 *
 * @dev: device structure
 * @file: file structure
 *
 * mei_clear_lists is called to clear resources associated with file
 * when application calls close function or Ctrl-C was pressed
 *
 * returns true if callback removed from the list, false otherwise
 */
static bool mei_clear_lists(struct mei_device *dev, struct file *file)
{
	bool removed = false;

	/* remove callbacks associated with a file */
	mei_clear_list(dev, file, &dev->amthi_cmd_list.mei_cb.cb_list);
	if (mei_clear_list(dev, file,
			    &dev->amthi_read_complete_list.mei_cb.cb_list))
		removed = true;

	mei_clear_list(dev, file, &dev->ctrl_rd_list.mei_cb.cb_list);

	if (mei_clear_list(dev, file, &dev->ctrl_wr_list.mei_cb.cb_list))
		removed = true;

	if (mei_clear_list(dev, file, &dev->write_waiting_list.mei_cb.cb_list))
		removed = true;

	if (mei_clear_list(dev, file, &dev->write_list.mei_cb.cb_list))
		removed = true;

	/* check if iamthif_current_cb not NULL */
	if (dev->iamthif_current_cb && !removed) {
		/* check file and iamthif current cb association */
		if (dev->iamthif_current_cb->file_object == file) {
			/* remove cb */
			mei_free_cb_private(dev->iamthif_current_cb);
			dev->iamthif_current_cb = NULL;
			removed = true;
		}
	}
	return removed;
}
/**
 * find_read_list_entry - find read list entry
 *
 * @dev: device structure
 * @file: pointer to file structure
 *
 * returns cb on success, NULL on error
 */
static struct mei_cl_cb *find_read_list_entry(
		struct mei_device *dev,
		struct mei_cl *cl)
{
	struct mei_cl_cb *pos = NULL;
	struct mei_cl_cb *next = NULL;

	dev_dbg(&dev->pdev->dev, "remove read_list CB\n");
	list_for_each_entry_safe(pos, next,
			&dev->read_list.mei_cb.cb_list, cb_list) {
		struct mei_cl *cl_temp;
		cl_temp = (struct mei_cl *)pos->file_private;

		if (mei_cl_cmp_id(cl, cl_temp))
			return pos;
	}
	return NULL;
}

/**
 * mei_open - the open function
 *
 * @inode: pointer to inode structure
 * @file: pointer to file structure
 *
 * returns 0 on success, <0 on error
 */
static int mei_open(struct inode *inode, struct file *file)
{
	struct mei_cl *cl;
	struct mei_device *dev;
	unsigned long cl_id;
	int err;

	err = -ENODEV;
	if (!mei_pdev)
		goto out;

	dev = pci_get_drvdata(mei_pdev);
	if (!dev)
		goto out;

	mutex_lock(&dev->device_lock);
	err = -ENOMEM;
	cl = mei_cl_allocate(dev);
	if (!cl)
		goto out_unlock;

	err = -ENODEV;
	if (dev->dev_state != MEI_DEV_ENABLED) {
		dev_dbg(&dev->pdev->dev, "dev_state != MEI_ENABLED  dev_state = %s\n",
		    mei_dev_state_str(dev->dev_state));
		goto out_unlock;
	}
	err = -EMFILE;
	if (dev->open_handle_count >= MEI_MAX_OPEN_HANDLE_COUNT)
		goto out_unlock;

	cl_id = find_first_zero_bit(dev->host_clients_map, MEI_CLIENTS_MAX);
	if (cl_id >= MEI_CLIENTS_MAX)
		goto out_unlock;

	cl->host_client_id  = cl_id;

	dev_dbg(&dev->pdev->dev, "client_id = %d\n", cl->host_client_id);

	dev->open_handle_count++;

	list_add_tail(&cl->link, &dev->file_list);

	set_bit(cl->host_client_id, dev->host_clients_map);
	cl->state = MEI_FILE_INITIALIZING;
	cl->sm_state = 0;

	file->private_data = cl;
	mutex_unlock(&dev->device_lock);

	return nonseekable_open(inode, file);

out_unlock:
	mutex_unlock(&dev->device_lock);
	kfree(cl);
out:
	return err;
}

/**
 * mei_release - the release function
 *
 * @inode: pointer to inode structure
 * @file: pointer to file structure
 *
 * returns 0 on success, <0 on error
 */
static int mei_release(struct inode *inode, struct file *file)
{
	struct mei_cl *cl = file->private_data;
	struct mei_cl_cb *cb;
	struct mei_device *dev;
	int rets = 0;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);
	if (cl != &dev->iamthif_cl) {
		if (cl->state == MEI_FILE_CONNECTED) {
			cl->state = MEI_FILE_DISCONNECTING;
			dev_dbg(&dev->pdev->dev,
				"disconnecting client host client = %d, "
			    "ME client = %d\n",
			    cl->host_client_id,
			    cl->me_client_id);
			rets = mei_disconnect_host_client(dev, cl);
		}
		mei_cl_flush_queues(cl);
		dev_dbg(&dev->pdev->dev, "remove client host client = %d, ME client = %d\n",
		    cl->host_client_id,
		    cl->me_client_id);

		if (dev->open_handle_count > 0) {
			clear_bit(cl->host_client_id, dev->host_clients_map);
			dev->open_handle_count--;
		}
		mei_remove_client_from_file_list(dev, cl->host_client_id);

		/* free read cb */
		cb = NULL;
		if (cl->read_cb) {
			cb = find_read_list_entry(dev, cl);
			/* Remove entry from read list */
			if (cb)
				list_del(&cb->cb_list);

			cb = cl->read_cb;
			cl->read_cb = NULL;
		}

		file->private_data = NULL;

		if (cb) {
			mei_free_cb_private(cb);
			cb = NULL;
		}

		kfree(cl);
	} else {
		if (dev->open_handle_count > 0)
			dev->open_handle_count--;

		if (dev->iamthif_file_object == file &&
		    dev->iamthif_state != MEI_IAMTHIF_IDLE) {

			dev_dbg(&dev->pdev->dev, "amthi canceled iamthif state %d\n",
			    dev->iamthif_state);
			dev->iamthif_canceled = true;
			if (dev->iamthif_state == MEI_IAMTHIF_READ_COMPLETE) {
				dev_dbg(&dev->pdev->dev, "run next amthi iamthif cb\n");
				mei_run_next_iamthif_cmd(dev);
			}
		}

		if (mei_clear_lists(dev, file))
			dev->iamthif_state = MEI_IAMTHIF_IDLE;

	}
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
 * returns >=0 data length on success , <0 on error
 */
static ssize_t mei_read(struct file *file, char __user *ubuf,
			size_t length, loff_t *offset)
{
	struct mei_cl *cl = file->private_data;
	struct mei_cl_cb *cb_pos = NULL;
	struct mei_cl_cb *cb = NULL;
	struct mei_device *dev;
	int i;
	int rets;
	int err;


	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);
	if (dev->dev_state != MEI_DEV_ENABLED) {
		rets = -ENODEV;
		goto out;
	}

	if ((cl->sm_state & MEI_WD_STATE_INDEPENDENCE_MSG_SENT) == 0) {
		/* Do not allow to read watchdog client */
		i = mei_me_cl_by_uuid(dev, &mei_wd_guid);
		if (i >= 0) {
			struct mei_me_client *me_client = &dev->me_clients[i];
			if (cl->me_client_id == me_client->client_id) {
				rets = -EBADF;
				goto out;
			}
		}
	} else {
		cl->sm_state &= ~MEI_WD_STATE_INDEPENDENCE_MSG_SENT;
	}

	if (cl == &dev->iamthif_cl) {
		rets = amthi_read(dev, file, ubuf, length, offset);
		goto out;
	}

	if (cl->read_cb && cl->read_cb->information > *offset) {
		cb = cl->read_cb;
		goto copy_buffer;
	} else if (cl->read_cb && cl->read_cb->information > 0 &&
		   cl->read_cb->information <= *offset) {
		cb = cl->read_cb;
		rets = 0;
		goto free;
	} else if ((!cl->read_cb || !cl->read_cb->information) &&
		    *offset > 0) {
		/*Offset needs to be cleaned for contiguous reads*/
		*offset = 0;
		rets = 0;
		goto out;
	}

	err = mei_start_read(dev, cl);
	if (err && err != -EBUSY) {
		dev_dbg(&dev->pdev->dev,
			"mei start read failure with status = %d\n", err);
		rets = err;
		goto out;
	}

	if (MEI_READ_COMPLETE != cl->reading_state &&
			!waitqueue_active(&cl->rx_wait)) {
		if (file->f_flags & O_NONBLOCK) {
			rets = -EAGAIN;
			goto out;
		}

		mutex_unlock(&dev->device_lock);

		if (wait_event_interruptible(cl->rx_wait,
			(MEI_READ_COMPLETE == cl->reading_state ||
			 MEI_FILE_INITIALIZING == cl->state ||
			 MEI_FILE_DISCONNECTED == cl->state ||
			 MEI_FILE_DISCONNECTING == cl->state))) {
			if (signal_pending(current))
				return -EINTR;
			return -ERESTARTSYS;
		}

		mutex_lock(&dev->device_lock);
		if (MEI_FILE_INITIALIZING == cl->state ||
		    MEI_FILE_DISCONNECTED == cl->state ||
		    MEI_FILE_DISCONNECTING == cl->state) {
			rets = -EBUSY;
			goto out;
		}
	}

	cb = cl->read_cb;

	if (!cb) {
		rets = -ENODEV;
		goto out;
	}
	if (cl->reading_state != MEI_READ_COMPLETE) {
		rets = 0;
		goto out;
	}
	/* now copy the data to user space */
copy_buffer:
	dev_dbg(&dev->pdev->dev, "cb->response_buffer size - %d\n",
	    cb->response_buffer.size);
	dev_dbg(&dev->pdev->dev, "cb->information - %lu\n",
	    cb->information);
	if (length == 0 || ubuf == NULL || *offset > cb->information) {
		rets = -EMSGSIZE;
		goto free;
	}

	/* length is being truncated to PAGE_SIZE, however, */
	/* information size may be longer */
	length = min_t(size_t, length, (cb->information - *offset));

	if (copy_to_user(ubuf, cb->response_buffer.data + *offset, length)) {
		rets = -EFAULT;
		goto free;
	}

	rets = length;
	*offset += length;
	if ((unsigned long)*offset < cb->information)
		goto out;

free:
	cb_pos = find_read_list_entry(dev, cl);
	/* Remove entry from read list */
	if (cb_pos)
		list_del(&cb_pos->cb_list);
	mei_free_cb_private(cb);
	cl->reading_state = MEI_IDLE;
	cl->read_cb = NULL;
	cl->read_pending = 0;
out:
	dev_dbg(&dev->pdev->dev, "end mei read rets= %d\n", rets);
	mutex_unlock(&dev->device_lock);
	return rets;
}

/**
 * mei_write - the write function.
 *
 * @file: pointer to file structure
 * @ubuf: pointer to user buffer
 * @length: buffer length
 * @offset: data offset in buffer
 *
 * returns >=0 data length on success , <0 on error
 */
static ssize_t mei_write(struct file *file, const char __user *ubuf,
			 size_t length, loff_t *offset)
{
	struct mei_cl *cl = file->private_data;
	struct mei_cl_cb *write_cb = NULL;
	struct mei_msg_hdr mei_hdr;
	struct mei_device *dev;
	unsigned long timeout = 0;
	int rets;
	int i;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);

	if (dev->dev_state != MEI_DEV_ENABLED) {
		mutex_unlock(&dev->device_lock);
		return -ENODEV;
	}

	if (cl == &dev->iamthif_cl) {
		write_cb = find_amthi_read_list_entry(dev, file);

		if (write_cb) {
			timeout = write_cb->read_time +
					msecs_to_jiffies(IAMTHIF_READ_TIMER);

			if (time_after(jiffies, timeout) ||
				 cl->reading_state == MEI_READ_COMPLETE) {
					*offset = 0;
					list_del(&write_cb->cb_list);
					mei_free_cb_private(write_cb);
					write_cb = NULL;
			}
		}
	}

	/* free entry used in read */
	if (cl->reading_state == MEI_READ_COMPLETE) {
		*offset = 0;
		write_cb = find_read_list_entry(dev, cl);
		if (write_cb) {
			list_del(&write_cb->cb_list);
			mei_free_cb_private(write_cb);
			write_cb = NULL;
			cl->reading_state = MEI_IDLE;
			cl->read_cb = NULL;
			cl->read_pending = 0;
		}
	} else if (cl->reading_state == MEI_IDLE && !cl->read_pending)
		*offset = 0;


	write_cb = kzalloc(sizeof(struct mei_cl_cb), GFP_KERNEL);
	if (!write_cb) {
		mutex_unlock(&dev->device_lock);
		return -ENOMEM;
	}

	write_cb->file_object = file;
	write_cb->file_private = cl;
	write_cb->request_buffer.data = kmalloc(length, GFP_KERNEL);
	rets = -ENOMEM;
	if (!write_cb->request_buffer.data)
		goto unlock_dev;

	dev_dbg(&dev->pdev->dev, "length =%d\n", (int) length);

	rets = -EFAULT;
	if (copy_from_user(write_cb->request_buffer.data, ubuf, length))
		goto unlock_dev;

	cl->sm_state = 0;
	if (length == 4 &&
	    ((memcmp(mei_wd_state_independence_msg[0],
				 write_cb->request_buffer.data, 4) == 0) ||
	     (memcmp(mei_wd_state_independence_msg[1],
				 write_cb->request_buffer.data, 4) == 0) ||
	     (memcmp(mei_wd_state_independence_msg[2],
				 write_cb->request_buffer.data, 4) == 0)))
		cl->sm_state |= MEI_WD_STATE_INDEPENDENCE_MSG_SENT;

	INIT_LIST_HEAD(&write_cb->cb_list);
	if (cl == &dev->iamthif_cl) {
		write_cb->response_buffer.data =
		    kmalloc(dev->iamthif_mtu, GFP_KERNEL);
		if (!write_cb->response_buffer.data) {
			rets = -ENOMEM;
			goto unlock_dev;
		}
		if (dev->dev_state != MEI_DEV_ENABLED) {
			rets = -ENODEV;
			goto unlock_dev;
		}
		i = mei_me_cl_by_id(dev, dev->iamthif_cl.me_client_id);
		if (i < 0) {
			rets = -ENODEV;
			goto unlock_dev;
		}
		if (length > dev->me_clients[i].props.max_msg_length ||
			   length <= 0) {
			rets = -EMSGSIZE;
			goto unlock_dev;
		}

		write_cb->response_buffer.size = dev->iamthif_mtu;
		write_cb->major_file_operations = MEI_IOCTL;
		write_cb->information = 0;
		write_cb->request_buffer.size = length;
		if (dev->iamthif_cl.state != MEI_FILE_CONNECTED) {
			rets = -ENODEV;
			goto unlock_dev;
		}

		if (!list_empty(&dev->amthi_cmd_list.mei_cb.cb_list) ||
				dev->iamthif_state != MEI_IAMTHIF_IDLE) {
			dev_dbg(&dev->pdev->dev, "amthi_state = %d\n",
					(int) dev->iamthif_state);
			dev_dbg(&dev->pdev->dev, "add amthi cb to amthi cmd waiting list\n");
			list_add_tail(&write_cb->cb_list,
					&dev->amthi_cmd_list.mei_cb.cb_list);
			rets = length;
		} else {
			dev_dbg(&dev->pdev->dev, "call amthi write\n");
			rets = amthi_write(dev, write_cb);

			if (rets) {
				dev_dbg(&dev->pdev->dev, "amthi write failed with status = %d\n",
				    rets);
				goto unlock_dev;
			}
			rets = length;
		}
		mutex_unlock(&dev->device_lock);
		return rets;
	}

	write_cb->major_file_operations = MEI_WRITE;
	/* make sure information is zero before we start */

	write_cb->information = 0;
	write_cb->request_buffer.size = length;

	dev_dbg(&dev->pdev->dev, "host client = %d, ME client = %d\n",
	    cl->host_client_id, cl->me_client_id);
	if (cl->state != MEI_FILE_CONNECTED) {
		rets = -ENODEV;
		dev_dbg(&dev->pdev->dev, "host client = %d,  is not connected to ME client = %d",
		    cl->host_client_id,
		    cl->me_client_id);
		goto unlock_dev;
	}
	i = mei_me_cl_by_id(dev, cl->me_client_id);
	if (i < 0) {
		rets = -ENODEV;
		goto unlock_dev;
	}
	if (length > dev->me_clients[i].props.max_msg_length || length <= 0) {
		rets = -EINVAL;
		goto unlock_dev;
	}
	write_cb->file_private = cl;

	rets = mei_flow_ctrl_creds(dev, cl);
	if (rets < 0)
		goto unlock_dev;

	if (rets && dev->mei_host_buffer_is_empty) {
		rets = 0;
		dev->mei_host_buffer_is_empty = false;
		if (length >  mei_hbuf_max_data(dev)) {
			mei_hdr.length = mei_hbuf_max_data(dev);
			mei_hdr.msg_complete = 0;
		} else {
			mei_hdr.length = length;
			mei_hdr.msg_complete = 1;
		}
		mei_hdr.host_addr = cl->host_client_id;
		mei_hdr.me_addr = cl->me_client_id;
		mei_hdr.reserved = 0;
		dev_dbg(&dev->pdev->dev, "call mei_write_message header=%08x.\n",
		    *((u32 *) &mei_hdr));
		if (mei_write_message(dev, &mei_hdr,
			(unsigned char *) (write_cb->request_buffer.data),
			mei_hdr.length)) {
			rets = -ENODEV;
			goto unlock_dev;
		}
		cl->writing_state = MEI_WRITING;
		write_cb->information = mei_hdr.length;
		if (mei_hdr.msg_complete) {
			if (mei_flow_ctrl_reduce(dev, cl)) {
				rets = -ENODEV;
				goto unlock_dev;
			}
			list_add_tail(&write_cb->cb_list,
				      &dev->write_waiting_list.mei_cb.cb_list);
		} else {
			list_add_tail(&write_cb->cb_list,
				      &dev->write_list.mei_cb.cb_list);
		}

	} else {

		write_cb->information = 0;
		cl->writing_state = MEI_WRITING;
		list_add_tail(&write_cb->cb_list,
			      &dev->write_list.mei_cb.cb_list);
	}
	mutex_unlock(&dev->device_lock);
	return length;

unlock_dev:
	mutex_unlock(&dev->device_lock);
	mei_free_cb_private(write_cb);
	return rets;
}


/**
 * mei_ioctl - the IOCTL function
 *
 * @file: pointer to file structure
 * @cmd: ioctl command
 * @data: pointer to mei message structure
 *
 * returns 0 on success , <0 on error
 */
static long mei_ioctl(struct file *file, unsigned int cmd, unsigned long data)
{
	struct mei_device *dev;
	struct mei_cl *cl = file->private_data;
	struct mei_connect_client_data *connect_data = NULL;
	int rets;

	if (cmd != IOCTL_MEI_CONNECT_CLIENT)
		return -EINVAL;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	dev_dbg(&dev->pdev->dev, "IOCTL cmd = 0x%x", cmd);

	mutex_lock(&dev->device_lock);
	if (dev->dev_state != MEI_DEV_ENABLED) {
		rets = -ENODEV;
		goto out;
	}

	dev_dbg(&dev->pdev->dev, ": IOCTL_MEI_CONNECT_CLIENT.\n");

	connect_data = kzalloc(sizeof(struct mei_connect_client_data),
							GFP_KERNEL);
	if (!connect_data) {
		rets = -ENOMEM;
		goto out;
	}
	dev_dbg(&dev->pdev->dev, "copy connect data from user\n");
	if (copy_from_user(connect_data, (char __user *)data,
				sizeof(struct mei_connect_client_data))) {
		dev_dbg(&dev->pdev->dev, "failed to copy data from userland\n");
		rets = -EFAULT;
		goto out;
	}
	rets = mei_ioctl_connect_client(file, connect_data);

	/* if all is ok, copying the data back to user. */
	if (rets)
		goto out;

	dev_dbg(&dev->pdev->dev, "copy connect data to user\n");
	if (copy_to_user((char __user *)data, connect_data,
				sizeof(struct mei_connect_client_data))) {
		dev_dbg(&dev->pdev->dev, "failed to copy data to userland\n");
		rets = -EFAULT;
		goto out;
	}

out:
	kfree(connect_data);
	mutex_unlock(&dev->device_lock);
	return rets;
}

/**
 * mei_compat_ioctl - the compat IOCTL function
 *
 * @file: pointer to file structure
 * @cmd: ioctl command
 * @data: pointer to mei message structure
 *
 * returns 0 on success , <0 on error
 */
#ifdef CONFIG_COMPAT
static long mei_compat_ioctl(struct file *file,
			unsigned int cmd, unsigned long data)
{
	return mei_ioctl(file, cmd, (unsigned long)compat_ptr(data));
}
#endif


/**
 * mei_poll - the poll function
 *
 * @file: pointer to file structure
 * @wait: pointer to poll_table structure
 *
 * returns poll mask
 */
static unsigned int mei_poll(struct file *file, poll_table *wait)
{
	struct mei_cl *cl = file->private_data;
	struct mei_device *dev;
	unsigned int mask = 0;

	if (WARN_ON(!cl || !cl->dev))
		return mask;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);

	if (dev->dev_state != MEI_DEV_ENABLED)
		goto out;


	if (cl == &dev->iamthif_cl) {
		mutex_unlock(&dev->device_lock);
		poll_wait(file, &dev->iamthif_cl.wait, wait);
		mutex_lock(&dev->device_lock);
		if (dev->iamthif_state == MEI_IAMTHIF_READ_COMPLETE &&
			dev->iamthif_file_object == file) {
			mask |= (POLLIN | POLLRDNORM);
			dev_dbg(&dev->pdev->dev, "run next amthi cb\n");
			mei_run_next_iamthif_cmd(dev);
		}
		goto out;
	}

	mutex_unlock(&dev->device_lock);
	poll_wait(file, &cl->tx_wait, wait);
	mutex_lock(&dev->device_lock);
	if (MEI_WRITE_COMPLETE == cl->writing_state)
		mask |= (POLLIN | POLLRDNORM);

out:
	mutex_unlock(&dev->device_lock);
	return mask;
}

/*
 * file operations structure will be used for mei char device.
 */
static const struct file_operations mei_fops = {
	.owner = THIS_MODULE,
	.read = mei_read,
	.unlocked_ioctl = mei_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = mei_compat_ioctl,
#endif
	.open = mei_open,
	.release = mei_release,
	.write = mei_write,
	.poll = mei_poll,
	.llseek = no_llseek
};


/*
 * Misc Device Struct
 */
static struct miscdevice  mei_misc_device = {
		.name = "mei",
		.fops = &mei_fops,
		.minor = MISC_DYNAMIC_MINOR,
};

/**
 * mei_quirk_probe - probe for devices that doesn't valid ME interface
 * @pdev: PCI device structure
 * @ent: entry into pci_device_table
 *
 * returns true if ME Interface is valid, false otherwise
 */
static bool __devinit mei_quirk_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	u32 reg;
	if (ent->device == MEI_DEV_ID_PBG_1) {
		pci_read_config_dword(pdev, 0x48, &reg);
		/* make sure that bit 9 is up and bit 10 is down */
		if ((reg & 0x600) == 0x200) {
			dev_info(&pdev->dev, "Device doesn't have valid ME Interface\n");
			return false;
		}
	}
	return true;
}
/**
 * mei_probe - Device Initialization Routine
 *
 * @pdev: PCI device structure
 * @ent: entry in kcs_pci_tbl
 *
 * returns 0 on success, <0 on failure.
 */
static int __devinit mei_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct mei_device *dev;
	int err;

	mutex_lock(&mei_mutex);

	if (!mei_quirk_probe(pdev, ent)) {
		err = -ENODEV;
		goto end;
	}

	if (mei_pdev) {
		err = -EEXIST;
		goto end;
	}
	/* enable pci dev */
	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "failed to enable pci device.\n");
		goto end;
	}
	/* set PCI host mastering  */
	pci_set_master(pdev);
	/* pci request regions for mei driver */
	err = pci_request_regions(pdev, KBUILD_MODNAME);
	if (err) {
		dev_err(&pdev->dev, "failed to get pci regions.\n");
		goto disable_device;
	}
	/* allocates and initializes the mei dev structure */
	dev = mei_device_init(pdev);
	if (!dev) {
		err = -ENOMEM;
		goto release_regions;
	}
	/* mapping  IO device memory */
	dev->mem_addr = pci_iomap(pdev, 0, 0);
	if (!dev->mem_addr) {
		dev_err(&pdev->dev, "mapping I/O device memory failure.\n");
		err = -ENOMEM;
		goto free_device;
	}
	pci_enable_msi(pdev);

	 /* request and enable interrupt */
	if (pci_dev_msi_enabled(pdev))
		err = request_threaded_irq(pdev->irq,
			NULL,
			mei_interrupt_thread_handler,
			IRQF_ONESHOT, KBUILD_MODNAME, dev);
	else
		err = request_threaded_irq(pdev->irq,
			mei_interrupt_quick_handler,
			mei_interrupt_thread_handler,
			IRQF_SHARED, KBUILD_MODNAME, dev);

	if (err) {
		dev_err(&pdev->dev, "request_threaded_irq failure. irq = %d\n",
		       pdev->irq);
		goto disable_msi;
	}
	INIT_DELAYED_WORK(&dev->timer_work, mei_timer);
	if (mei_hw_init(dev)) {
		dev_err(&pdev->dev, "init hw failure.\n");
		err = -ENODEV;
		goto release_irq;
	}

	err = misc_register(&mei_misc_device);
	if (err)
		goto release_irq;

	mei_pdev = pdev;
	pci_set_drvdata(pdev, dev);


	schedule_delayed_work(&dev->timer_work, HZ);

	mutex_unlock(&mei_mutex);

	pr_debug("initialization successful.\n");

	return 0;

release_irq:
	/* disable interrupts */
	dev->host_hw_state = mei_hcsr_read(dev);
	mei_disable_interrupts(dev);
	flush_scheduled_work();
	free_irq(pdev->irq, dev);
disable_msi:
	pci_disable_msi(pdev);
	pci_iounmap(pdev, dev->mem_addr);
free_device:
	kfree(dev);
release_regions:
	pci_release_regions(pdev);
disable_device:
	pci_disable_device(pdev);
end:
	mutex_unlock(&mei_mutex);
	dev_err(&pdev->dev, "initialization failed.\n");
	return err;
}

/**
 * mei_remove - Device Removal Routine
 *
 * @pdev: PCI device structure
 *
 * mei_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.
 */
static void __devexit mei_remove(struct pci_dev *pdev)
{
	struct mei_device *dev;

	if (mei_pdev != pdev)
		return;

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return;

	mutex_lock(&dev->device_lock);

	cancel_delayed_work(&dev->timer_work);

	mei_wd_stop(dev);

	mei_pdev = NULL;

	if (dev->iamthif_cl.state == MEI_FILE_CONNECTED) {
		dev->iamthif_cl.state = MEI_FILE_DISCONNECTING;
		mei_disconnect_host_client(dev, &dev->iamthif_cl);
	}
	if (dev->wd_cl.state == MEI_FILE_CONNECTED) {
		dev->wd_cl.state = MEI_FILE_DISCONNECTING;
		mei_disconnect_host_client(dev, &dev->wd_cl);
	}

	/* Unregistering watchdog device */
	mei_watchdog_unregister(dev);

	/* remove entry if already in list */
	dev_dbg(&pdev->dev, "list del iamthif and wd file list.\n");
	mei_remove_client_from_file_list(dev, dev->wd_cl.host_client_id);
	mei_remove_client_from_file_list(dev, dev->iamthif_cl.host_client_id);

	dev->iamthif_current_cb = NULL;
	dev->me_clients_num = 0;

	mutex_unlock(&dev->device_lock);

	flush_scheduled_work();

	/* disable interrupts */
	mei_disable_interrupts(dev);

	free_irq(pdev->irq, dev);
	pci_disable_msi(pdev);
	pci_set_drvdata(pdev, NULL);

	if (dev->mem_addr)
		pci_iounmap(pdev, dev->mem_addr);

	kfree(dev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	misc_deregister(&mei_misc_device);
}
#ifdef CONFIG_PM
static int mei_pci_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mei_device *dev = pci_get_drvdata(pdev);
	int err;

	if (!dev)
		return -ENODEV;
	mutex_lock(&dev->device_lock);

	cancel_delayed_work(&dev->timer_work);

	/* Stop watchdog if exists */
	err = mei_wd_stop(dev);
	/* Set new mei state */
	if (dev->dev_state == MEI_DEV_ENABLED ||
	    dev->dev_state == MEI_DEV_RECOVERING_FROM_RESET) {
		dev->dev_state = MEI_DEV_POWER_DOWN;
		mei_reset(dev, 0);
	}
	mutex_unlock(&dev->device_lock);

	free_irq(pdev->irq, dev);
	pci_disable_msi(pdev);

	return err;
}

static int mei_pci_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mei_device *dev;
	int err;

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return -ENODEV;

	pci_enable_msi(pdev);

	/* request and enable interrupt */
	if (pci_dev_msi_enabled(pdev))
		err = request_threaded_irq(pdev->irq,
			NULL,
			mei_interrupt_thread_handler,
			IRQF_ONESHOT, KBUILD_MODNAME, dev);
	else
		err = request_threaded_irq(pdev->irq,
			mei_interrupt_quick_handler,
			mei_interrupt_thread_handler,
			IRQF_SHARED, KBUILD_MODNAME, dev);

	if (err) {
		dev_err(&pdev->dev, "request_threaded_irq failed: irq = %d.\n",
				pdev->irq);
		return err;
	}

	mutex_lock(&dev->device_lock);
	dev->dev_state = MEI_DEV_POWER_UP;
	mei_reset(dev, 1);
	mutex_unlock(&dev->device_lock);

	/* Start timer if stopped in suspend */
	schedule_delayed_work(&dev->timer_work, HZ);

	return err;
}
static SIMPLE_DEV_PM_OPS(mei_pm_ops, mei_pci_suspend, mei_pci_resume);
#define MEI_PM_OPS	(&mei_pm_ops)
#else
#define MEI_PM_OPS	NULL
#endif /* CONFIG_PM */
/*
 *  PCI driver structure
 */
static struct pci_driver mei_driver = {
	.name = KBUILD_MODNAME,
	.id_table = mei_pci_tbl,
	.probe = mei_probe,
	.remove = __devexit_p(mei_remove),
	.shutdown = __devexit_p(mei_remove),
	.driver.pm = MEI_PM_OPS,
};

module_pci_driver(mei_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) Management Engine Interface");
MODULE_LICENSE("GPL v2");
