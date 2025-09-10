// SPDX-License-Identifier: GPL-2.0+
/*
 * f_hid.c -- USB HID function driver
 *
 * Copyright (C) 2010 Fabien Chouteau <fabien.chouteau@barco.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hid.h>
#include <linux/idr.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/usb/func_utils.h>
#include <linux/usb/g_hid.h>
#include <uapi/linux/usb/g_hid.h>

#include "u_hid.h"

#define HIDG_MINORS	4

/*
 * Most operating systems seem to allow for 5000ms timeout, we will allow
 * userspace half that time to respond before we return an empty report.
 */
#define GET_REPORT_TIMEOUT_MS 2500

static int major, minors;

static const struct class hidg_class = {
	.name = "hidg",
};

static DEFINE_IDA(hidg_ida);
static DEFINE_MUTEX(hidg_ida_lock); /* protects access to hidg_ida */

struct report_entry {
	struct usb_hidg_report report_data;
	struct list_head node;
};

/*-------------------------------------------------------------------------*/
/*                            HID gadget struct                            */

struct f_hidg_req_list {
	struct usb_request	*req;
	unsigned int		pos;
	struct list_head 	list;
};

struct f_hidg {
	/* configuration */
	unsigned char			bInterfaceSubClass;
	unsigned char			bInterfaceProtocol;
	unsigned char			protocol;
	unsigned char			idle;
	unsigned short			report_desc_length;
	char				*report_desc;
	unsigned short			report_length;
	unsigned char			interval;
	bool				interval_user_set;

	/*
	 * use_out_ep - if true, the OUT Endpoint (interrupt out method)
	 *              will be used to receive reports from the host
	 *              using functions with the "intout" suffix.
	 *              Otherwise, the OUT Endpoint will not be configured
	 *              and the SETUP/SET_REPORT method ("ssreport" suffix)
	 *              will be used to receive reports.
	 */
	bool				use_out_ep;

	/* recv report */
	spinlock_t			read_spinlock;
	wait_queue_head_t		read_queue;
	bool				disabled;
	/* recv report - interrupt out only (use_out_ep == 1) */
	struct list_head		completed_out_req;
	unsigned int			qlen;
	/* recv report - setup set_report only (use_out_ep == 0) */
	char				*set_report_buf;
	unsigned int			set_report_length;

	/* send report */
	spinlock_t			write_spinlock;
	bool				write_pending;
	wait_queue_head_t		write_queue;
	struct usb_request		*req;

	/* get report */
	struct usb_request		*get_req;
	struct usb_hidg_report		get_report;
	bool				get_report_returned;
	int				get_report_req_report_id;
	int				get_report_req_report_length;
	spinlock_t			get_report_spinlock;
	wait_queue_head_t		get_queue;    /* Waiting for userspace response */
	wait_queue_head_t		get_id_queue; /* Get ID came in */
	struct work_struct		work;
	struct workqueue_struct		*workqueue;
	struct list_head		report_list;

	struct device			dev;
	struct cdev			cdev;
	struct usb_function		func;

	struct usb_ep			*in_ep;
	struct usb_ep			*out_ep;
};

static inline struct f_hidg *func_to_hidg(struct usb_function *f)
{
	return container_of(f, struct f_hidg, func);
}

static void hidg_release(struct device *dev)
{
	struct f_hidg *hidg = container_of(dev, struct f_hidg, dev);

	kfree(hidg->report_desc);
	kfree(hidg->set_report_buf);
	kfree(hidg);
}

/*-------------------------------------------------------------------------*/
/*                           Static descriptors                            */

static struct usb_interface_descriptor hidg_interface_desc = {
	.bLength		= sizeof hidg_interface_desc,
	.bDescriptorType	= USB_DT_INTERFACE,
	/* .bInterfaceNumber	= DYNAMIC */
	.bAlternateSetting	= 0,
	/* .bNumEndpoints	= DYNAMIC (depends on use_out_ep) */
	.bInterfaceClass	= USB_CLASS_HID,
	/* .bInterfaceSubClass	= DYNAMIC */
	/* .bInterfaceProtocol	= DYNAMIC */
	/* .iInterface		= DYNAMIC */
};

static struct hid_descriptor hidg_desc = {
	.bLength			= sizeof hidg_desc,
	.bDescriptorType		= HID_DT_HID,
	.bcdHID				= cpu_to_le16(0x0101),
	.bCountryCode			= 0x00,
	.bNumDescriptors		= 0x1,
	/*.rpt_desc.bDescriptorType	= DYNAMIC */
	/*.rpt_desc.wDescriptorLength	= DYNAMIC */
};

/* Super-Speed Support */

static struct usb_endpoint_descriptor hidg_ss_in_ep_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_INT,
	/*.wMaxPacketSize	= DYNAMIC */
	/*.bInterval		= DYNAMIC */
};

static struct usb_ss_ep_comp_descriptor hidg_ss_in_comp_desc = {
	.bLength                = sizeof(hidg_ss_in_comp_desc),
	.bDescriptorType        = USB_DT_SS_ENDPOINT_COMP,

	/* .bMaxBurst           = 0, */
	/* .bmAttributes        = 0, */
	/* .wBytesPerInterval   = DYNAMIC */
};

static struct usb_endpoint_descriptor hidg_ss_out_ep_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_INT,
	/*.wMaxPacketSize	= DYNAMIC */
	/*.bInterval		= DYNAMIC */
};

static struct usb_ss_ep_comp_descriptor hidg_ss_out_comp_desc = {
	.bLength                = sizeof(hidg_ss_out_comp_desc),
	.bDescriptorType        = USB_DT_SS_ENDPOINT_COMP,

	/* .bMaxBurst           = 0, */
	/* .bmAttributes        = 0, */
	/* .wBytesPerInterval   = DYNAMIC */
};

static struct usb_descriptor_header *hidg_ss_descriptors_intout[] = {
	(struct usb_descriptor_header *)&hidg_interface_desc,
	(struct usb_descriptor_header *)&hidg_desc,
	(struct usb_descriptor_header *)&hidg_ss_in_ep_desc,
	(struct usb_descriptor_header *)&hidg_ss_in_comp_desc,
	(struct usb_descriptor_header *)&hidg_ss_out_ep_desc,
	(struct usb_descriptor_header *)&hidg_ss_out_comp_desc,
	NULL,
};

static struct usb_descriptor_header *hidg_ss_descriptors_ssreport[] = {
	(struct usb_descriptor_header *)&hidg_interface_desc,
	(struct usb_descriptor_header *)&hidg_desc,
	(struct usb_descriptor_header *)&hidg_ss_in_ep_desc,
	(struct usb_descriptor_header *)&hidg_ss_in_comp_desc,
	NULL,
};

/* High-Speed Support */

static struct usb_endpoint_descriptor hidg_hs_in_ep_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_INT,
	/*.wMaxPacketSize	= DYNAMIC */
	/* .bInterval		= DYNAMIC */
};

static struct usb_endpoint_descriptor hidg_hs_out_ep_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_INT,
	/*.wMaxPacketSize	= DYNAMIC */
	/*.bInterval		= DYNAMIC */
};

static struct usb_descriptor_header *hidg_hs_descriptors_intout[] = {
	(struct usb_descriptor_header *)&hidg_interface_desc,
	(struct usb_descriptor_header *)&hidg_desc,
	(struct usb_descriptor_header *)&hidg_hs_in_ep_desc,
	(struct usb_descriptor_header *)&hidg_hs_out_ep_desc,
	NULL,
};

static struct usb_descriptor_header *hidg_hs_descriptors_ssreport[] = {
	(struct usb_descriptor_header *)&hidg_interface_desc,
	(struct usb_descriptor_header *)&hidg_desc,
	(struct usb_descriptor_header *)&hidg_hs_in_ep_desc,
	NULL,
};

/* Full-Speed Support */

static struct usb_endpoint_descriptor hidg_fs_in_ep_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_INT,
	/*.wMaxPacketSize	= DYNAMIC */
	/*.bInterval		= DYNAMIC */
};

static struct usb_endpoint_descriptor hidg_fs_out_ep_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_INT,
	/*.wMaxPacketSize	= DYNAMIC */
	/*.bInterval		= DYNAMIC */
};

static struct usb_descriptor_header *hidg_fs_descriptors_intout[] = {
	(struct usb_descriptor_header *)&hidg_interface_desc,
	(struct usb_descriptor_header *)&hidg_desc,
	(struct usb_descriptor_header *)&hidg_fs_in_ep_desc,
	(struct usb_descriptor_header *)&hidg_fs_out_ep_desc,
	NULL,
};

static struct usb_descriptor_header *hidg_fs_descriptors_ssreport[] = {
	(struct usb_descriptor_header *)&hidg_interface_desc,
	(struct usb_descriptor_header *)&hidg_desc,
	(struct usb_descriptor_header *)&hidg_fs_in_ep_desc,
	NULL,
};

/*-------------------------------------------------------------------------*/
/*                                 Strings                                 */

#define CT_FUNC_HID_IDX	0

static struct usb_string ct_func_string_defs[] = {
	[CT_FUNC_HID_IDX].s	= "HID Interface",
	{},			/* end of list */
};

static struct usb_gadget_strings ct_func_string_table = {
	.language	= 0x0409,	/* en-US */
	.strings	= ct_func_string_defs,
};

static struct usb_gadget_strings *ct_func_strings[] = {
	&ct_func_string_table,
	NULL,
};

/*-------------------------------------------------------------------------*/
/*                              Char Device                                */

static ssize_t f_hidg_intout_read(struct file *file, char __user *buffer,
				  size_t count, loff_t *ptr)
{
	struct f_hidg *hidg = file->private_data;
	struct f_hidg_req_list *list;
	struct usb_request *req;
	unsigned long flags;
	int ret;

	if (!count)
		return 0;

	spin_lock_irqsave(&hidg->read_spinlock, flags);

#define READ_COND_INTOUT (!list_empty(&hidg->completed_out_req) || hidg->disabled)

	/* wait for at least one buffer to complete */
	while (!READ_COND_INTOUT) {
		spin_unlock_irqrestore(&hidg->read_spinlock, flags);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(hidg->read_queue, READ_COND_INTOUT))
			return -ERESTARTSYS;

		spin_lock_irqsave(&hidg->read_spinlock, flags);
	}

	if (hidg->disabled) {
		spin_unlock_irqrestore(&hidg->read_spinlock, flags);
		return -ESHUTDOWN;
	}

	/* pick the first one */
	list = list_first_entry(&hidg->completed_out_req,
				struct f_hidg_req_list, list);

	/*
	 * Remove this from list to protect it from beign free()
	 * while host disables our function
	 */
	list_del(&list->list);

	req = list->req;
	count = min_t(unsigned int, count, req->actual - list->pos);
	spin_unlock_irqrestore(&hidg->read_spinlock, flags);

	/* copy to user outside spinlock */
	count -= copy_to_user(buffer, req->buf + list->pos, count);
	list->pos += count;

	/*
	 * if this request is completely handled and transfered to
	 * userspace, remove its entry from the list and requeue it
	 * again. Otherwise, we will revisit it again upon the next
	 * call, taking into account its current read position.
	 */
	if (list->pos == req->actual) {
		kfree(list);

		req->length = hidg->report_length;
		ret = usb_ep_queue(hidg->out_ep, req, GFP_KERNEL);
		if (ret < 0) {
			free_ep_req(hidg->out_ep, req);
			return ret;
		}
	} else {
		spin_lock_irqsave(&hidg->read_spinlock, flags);
		list_add(&list->list, &hidg->completed_out_req);
		spin_unlock_irqrestore(&hidg->read_spinlock, flags);

		wake_up(&hidg->read_queue);
	}

	return count;
}

#define READ_COND_SSREPORT (hidg->set_report_buf != NULL || hidg->disabled)

static ssize_t f_hidg_ssreport_read(struct file *file, char __user *buffer,
				    size_t count, loff_t *ptr)
{
	struct f_hidg *hidg = file->private_data;
	char *tmp_buf = NULL;
	unsigned long flags;

	if (!count)
		return 0;

	spin_lock_irqsave(&hidg->read_spinlock, flags);

	while (!READ_COND_SSREPORT) {
		spin_unlock_irqrestore(&hidg->read_spinlock, flags);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(hidg->read_queue, READ_COND_SSREPORT))
			return -ERESTARTSYS;

		spin_lock_irqsave(&hidg->read_spinlock, flags);
	}

	count = min_t(unsigned int, count, hidg->set_report_length);
	tmp_buf = hidg->set_report_buf;
	hidg->set_report_buf = NULL;

	spin_unlock_irqrestore(&hidg->read_spinlock, flags);

	if (tmp_buf != NULL) {
		count -= copy_to_user(buffer, tmp_buf, count);
		kfree(tmp_buf);
	} else {
		count = -ENOMEM;
	}

	wake_up(&hidg->read_queue);

	return count;
}

static ssize_t f_hidg_read(struct file *file, char __user *buffer,
			   size_t count, loff_t *ptr)
{
	struct f_hidg *hidg = file->private_data;

	if (hidg->use_out_ep)
		return f_hidg_intout_read(file, buffer, count, ptr);
	else
		return f_hidg_ssreport_read(file, buffer, count, ptr);
}

static void f_hidg_req_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_hidg *hidg = (struct f_hidg *)ep->driver_data;
	unsigned long flags;

	if (req->status != 0) {
		ERROR(hidg->func.config->cdev,
			"End Point Request ERROR: %d\n", req->status);
	}

	spin_lock_irqsave(&hidg->write_spinlock, flags);
	hidg->write_pending = 0;
	spin_unlock_irqrestore(&hidg->write_spinlock, flags);
	wake_up(&hidg->write_queue);
}

static ssize_t f_hidg_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *offp)
{
	struct f_hidg *hidg  = file->private_data;
	struct usb_request *req;
	unsigned long flags;
	ssize_t status = -ENOMEM;

	spin_lock_irqsave(&hidg->write_spinlock, flags);

	if (!hidg->req) {
		spin_unlock_irqrestore(&hidg->write_spinlock, flags);
		return -ESHUTDOWN;
	}

#define WRITE_COND (!hidg->write_pending)
try_again:
	/* write queue */
	while (!WRITE_COND) {
		spin_unlock_irqrestore(&hidg->write_spinlock, flags);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible_exclusive(
				hidg->write_queue, WRITE_COND))
			return -ERESTARTSYS;

		spin_lock_irqsave(&hidg->write_spinlock, flags);
	}

	hidg->write_pending = 1;
	req = hidg->req;
	count  = min_t(unsigned, count, hidg->report_length);

	spin_unlock_irqrestore(&hidg->write_spinlock, flags);

	if (!req) {
		ERROR(hidg->func.config->cdev, "hidg->req is NULL\n");
		status = -ESHUTDOWN;
		goto release_write_pending;
	}

	status = copy_from_user(req->buf, buffer, count);
	if (status != 0) {
		ERROR(hidg->func.config->cdev,
			"copy_from_user error\n");
		status = -EINVAL;
		goto release_write_pending;
	}

	spin_lock_irqsave(&hidg->write_spinlock, flags);

	/* when our function has been disabled by host */
	if (!hidg->req) {
		free_ep_req(hidg->in_ep, req);
		/*
		 * TODO
		 * Should we fail with error here?
		 */
		goto try_again;
	}

	req->status   = 0;
	req->zero     = 0;
	req->length   = count;
	req->complete = f_hidg_req_complete;
	req->context  = hidg;

	spin_unlock_irqrestore(&hidg->write_spinlock, flags);

	if (!hidg->in_ep->enabled) {
		ERROR(hidg->func.config->cdev, "in_ep is disabled\n");
		status = -ESHUTDOWN;
		goto release_write_pending;
	}

	status = usb_ep_queue(hidg->in_ep, req, GFP_ATOMIC);
	if (status < 0)
		goto release_write_pending;
	else
		status = count;

	return status;
release_write_pending:
	spin_lock_irqsave(&hidg->write_spinlock, flags);
	hidg->write_pending = 0;
	spin_unlock_irqrestore(&hidg->write_spinlock, flags);

	wake_up(&hidg->write_queue);

	return status;
}

static struct report_entry *f_hidg_search_for_report(struct f_hidg *hidg, u8 report_id)
{
	struct list_head	*ptr;
	struct report_entry	*entry;

	list_for_each(ptr, &hidg->report_list) {
		entry = list_entry(ptr, struct report_entry, node);
		if (entry->report_data.report_id == report_id)
			return entry;
	}

	return NULL;
}

static void get_report_workqueue_handler(struct work_struct *work)
{
	struct f_hidg *hidg = container_of(work, struct f_hidg, work);
	struct usb_composite_dev *cdev = hidg->func.config->cdev;
	struct usb_request		*req;
	struct report_entry *ptr;
	unsigned long	flags;

	int status = 0;

	spin_lock_irqsave(&hidg->get_report_spinlock, flags);
	req = hidg->get_req;
	if (!req) {
		spin_unlock_irqrestore(&hidg->get_report_spinlock, flags);
		return;
	}

	req->zero = 0;
	req->length = min_t(unsigned int, min_t(unsigned int, hidg->get_report_req_report_length,
							      hidg->report_length),
					  MAX_REPORT_LENGTH);

	/* Check if there is a response available for immediate response */
	ptr = f_hidg_search_for_report(hidg, hidg->get_report_req_report_id);
	if (ptr && !ptr->report_data.userspace_req) {
		/* Report exists in list and it is to be used for immediate response */
		req->buf = ptr->report_data.data;
		status = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		hidg->get_report_returned = true;
		spin_unlock_irqrestore(&hidg->get_report_spinlock, flags);
	} else {
		/*
		 * Report does not exist in list or should not be immediately sent
		 * i.e. give userspace time to respond
		 */
		hidg->get_report_returned = false;
		spin_unlock_irqrestore(&hidg->get_report_spinlock, flags);
		wake_up(&hidg->get_id_queue);
#define GET_REPORT_COND (!hidg->get_report_returned)
		/* Wait until userspace has responded or timeout */
		status = wait_event_interruptible_timeout(hidg->get_queue, !GET_REPORT_COND,
					msecs_to_jiffies(GET_REPORT_TIMEOUT_MS));
		spin_lock_irqsave(&hidg->get_report_spinlock, flags);
		req = hidg->get_req;
		if (!req) {
			spin_unlock_irqrestore(&hidg->get_report_spinlock, flags);
			return;
		}
		if (status == 0 && !hidg->get_report_returned) {
			/* GET_REPORT request was not serviced by userspace within timeout period */
			VDBG(cdev, "get_report : userspace timeout.\n");
			hidg->get_report_returned = true;
		}

		/* Search again for report ID in list and respond to GET_REPORT request */
		ptr = f_hidg_search_for_report(hidg, hidg->get_report_req_report_id);
		if (ptr) {
			/*
			 * Either get an updated response just serviced by userspace
			 * or send the latest response in the list
			 */
			req->buf = ptr->report_data.data;
		} else {
			/* If there are no prevoiusly sent reports send empty report */
			req->buf = hidg->get_report.data;
			memset(req->buf, 0x0, req->length);
		}

		status = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		spin_unlock_irqrestore(&hidg->get_report_spinlock, flags);
	}

	if (status < 0)
		VDBG(cdev, "usb_ep_queue error on ep0 responding to GET_REPORT\n");
}

static int f_hidg_get_report_id(struct file *file, __u8 __user *buffer)
{
	struct f_hidg			*hidg = file->private_data;
	int ret = 0;

	ret = put_user(hidg->get_report_req_report_id, buffer);

	return ret;
}

static int f_hidg_get_report(struct file *file, struct usb_hidg_report __user *buffer)
{
	struct f_hidg			*hidg = file->private_data;
	struct usb_composite_dev	*cdev = hidg->func.config->cdev;
	unsigned long	flags;
	struct report_entry *entry;
	struct report_entry *ptr;
	__u8 report_id;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	if (copy_from_user(&entry->report_data, buffer,
				sizeof(struct usb_hidg_report))) {
		ERROR(cdev, "copy_from_user error\n");
		kfree(entry);
		return -EINVAL;
	}

	report_id = entry->report_data.report_id;

	spin_lock_irqsave(&hidg->get_report_spinlock, flags);
	ptr = f_hidg_search_for_report(hidg, report_id);

	if (ptr) {
		/* Report already exists in list - update it */
		if (copy_from_user(&ptr->report_data, buffer,
				sizeof(struct usb_hidg_report))) {
			spin_unlock_irqrestore(&hidg->get_report_spinlock, flags);
			ERROR(cdev, "copy_from_user error\n");
			kfree(entry);
			return -EINVAL;
		}
		kfree(entry);
	} else {
		/* Report does not exist in list - add it */
		list_add_tail(&entry->node, &hidg->report_list);
	}

	/* If there is no response pending then do nothing further */
	if (hidg->get_report_returned) {
		spin_unlock_irqrestore(&hidg->get_report_spinlock, flags);
		return 0;
	}

	/* If this userspace response serves the current pending report */
	if (hidg->get_report_req_report_id == report_id) {
		hidg->get_report_returned = true;
		wake_up(&hidg->get_queue);
	}

	spin_unlock_irqrestore(&hidg->get_report_spinlock, flags);
	return 0;
}

static long f_hidg_ioctl(struct file *file, unsigned int code, unsigned long arg)
{
	switch (code) {
	case GADGET_HID_READ_GET_REPORT_ID:
		return f_hidg_get_report_id(file, (__u8 __user *)arg);
	case GADGET_HID_WRITE_GET_REPORT:
		return f_hidg_get_report(file, (struct usb_hidg_report __user *)arg);
	default:
		return -ENOTTY;
	}
}

static __poll_t f_hidg_poll(struct file *file, poll_table *wait)
{
	struct f_hidg	*hidg  = file->private_data;
	__poll_t	ret = 0;

	poll_wait(file, &hidg->read_queue, wait);
	poll_wait(file, &hidg->write_queue, wait);
	poll_wait(file, &hidg->get_queue, wait);
	poll_wait(file, &hidg->get_id_queue, wait);

	if (WRITE_COND)
		ret |= EPOLLOUT | EPOLLWRNORM;

	if (hidg->use_out_ep) {
		if (READ_COND_INTOUT)
			ret |= EPOLLIN | EPOLLRDNORM;
	} else {
		if (READ_COND_SSREPORT)
			ret |= EPOLLIN | EPOLLRDNORM;
	}

	if (GET_REPORT_COND)
		ret |= EPOLLPRI;

	return ret;
}

#undef WRITE_COND
#undef READ_COND_SSREPORT
#undef READ_COND_INTOUT
#undef GET_REPORT_COND

static int f_hidg_release(struct inode *inode, struct file *fd)
{
	fd->private_data = NULL;
	return 0;
}

static int f_hidg_open(struct inode *inode, struct file *fd)
{
	struct f_hidg *hidg =
		container_of(inode->i_cdev, struct f_hidg, cdev);

	fd->private_data = hidg;

	return 0;
}

/*-------------------------------------------------------------------------*/
/*                                usb_function                             */

static inline struct usb_request *hidg_alloc_ep_req(struct usb_ep *ep,
						    unsigned length)
{
	return alloc_ep_req(ep, length);
}

static void hidg_intout_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_hidg *hidg = (struct f_hidg *) req->context;
	struct usb_composite_dev *cdev = hidg->func.config->cdev;
	struct f_hidg_req_list *req_list;
	unsigned long flags;

	switch (req->status) {
	case 0:
		req_list = kzalloc(sizeof(*req_list), GFP_ATOMIC);
		if (!req_list) {
			ERROR(cdev, "Unable to allocate mem for req_list\n");
			goto free_req;
		}

		req_list->req = req;

		spin_lock_irqsave(&hidg->read_spinlock, flags);
		list_add_tail(&req_list->list, &hidg->completed_out_req);
		spin_unlock_irqrestore(&hidg->read_spinlock, flags);

		wake_up(&hidg->read_queue);
		break;
	default:
		ERROR(cdev, "Set report failed %d\n", req->status);
		fallthrough;
	case -ECONNABORTED:		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued */
	case -ESHUTDOWN:		/* disconnect from host */
free_req:
		free_ep_req(ep, req);
		return;
	}
}

static void hidg_ssreport_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_hidg *hidg = (struct f_hidg *)req->context;
	struct usb_composite_dev *cdev = hidg->func.config->cdev;
	char *new_buf = NULL;
	unsigned long flags;

	if (req->status != 0 || req->buf == NULL || req->actual == 0) {
		ERROR(cdev,
		      "%s FAILED: status=%d, buf=%p, actual=%d\n",
		      __func__, req->status, req->buf, req->actual);
		return;
	}

	spin_lock_irqsave(&hidg->read_spinlock, flags);

	new_buf = krealloc(hidg->set_report_buf, req->actual, GFP_ATOMIC);
	if (new_buf == NULL) {
		spin_unlock_irqrestore(&hidg->read_spinlock, flags);
		return;
	}
	hidg->set_report_buf = new_buf;

	hidg->set_report_length = req->actual;
	memcpy(hidg->set_report_buf, req->buf, req->actual);

	spin_unlock_irqrestore(&hidg->read_spinlock, flags);

	wake_up(&hidg->read_queue);
}

static void hidg_get_report_complete(struct usb_ep *ep, struct usb_request *req)
{
}

static int hidg_setup(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct f_hidg			*hidg = func_to_hidg(f);
	struct usb_composite_dev	*cdev = f->config->cdev;
	struct usb_request		*req  = cdev->req;
	int status = 0;
	__u16 value, length;
	unsigned long	flags;

	value	= __le16_to_cpu(ctrl->wValue);
	length	= __le16_to_cpu(ctrl->wLength);

	VDBG(cdev,
	     "%s crtl_request : bRequestType:0x%x bRequest:0x%x Value:0x%x\n",
	     __func__, ctrl->bRequestType, ctrl->bRequest, value);

	switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
		  | HID_REQ_GET_REPORT):
		VDBG(cdev, "get_report | wLength=%d\n", ctrl->wLength);

		/*
		 * Update GET_REPORT ID so that an ioctl can be used to determine what
		 * GET_REPORT the request was actually for.
		 */
		spin_lock_irqsave(&hidg->get_report_spinlock, flags);
		hidg->get_report_req_report_id = value & 0xff;
		hidg->get_report_req_report_length = length;
		spin_unlock_irqrestore(&hidg->get_report_spinlock, flags);

		queue_work(hidg->workqueue, &hidg->work);

		return status;

	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
		  | HID_REQ_GET_PROTOCOL):
		VDBG(cdev, "get_protocol\n");
		length = min_t(unsigned int, length, 1);
		((u8 *) req->buf)[0] = hidg->protocol;
		goto respond;
		break;

	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
		  | HID_REQ_GET_IDLE):
		VDBG(cdev, "get_idle\n");
		length = min_t(unsigned int, length, 1);
		((u8 *) req->buf)[0] = hidg->idle;
		goto respond;
		break;

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
		  | HID_REQ_SET_REPORT):
		VDBG(cdev, "set_report | wLength=%d\n", ctrl->wLength);
		if (hidg->use_out_ep)
			goto stall;
		req->complete = hidg_ssreport_complete;
		req->context  = hidg;
		goto respond;
		break;

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
		  | HID_REQ_SET_PROTOCOL):
		VDBG(cdev, "set_protocol\n");
		if (value > HID_REPORT_PROTOCOL)
			goto stall;
		length = 0;
		/*
		 * We assume that programs implementing the Boot protocol
		 * are also compatible with the Report Protocol
		 */
		if (hidg->bInterfaceSubClass == USB_INTERFACE_SUBCLASS_BOOT) {
			hidg->protocol = value;
			goto respond;
		}
		goto stall;
		break;

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8
		  | HID_REQ_SET_IDLE):
		VDBG(cdev, "set_idle\n");
		length = 0;
		hidg->idle = value >> 8;
		goto respond;
		break;

	case ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8
		  | USB_REQ_GET_DESCRIPTOR):
		switch (value >> 8) {
		case HID_DT_HID:
		{
			struct hid_descriptor hidg_desc_copy = hidg_desc;

			VDBG(cdev, "USB_REQ_GET_DESCRIPTOR: HID\n");
			hidg_desc_copy.rpt_desc.bDescriptorType = HID_DT_REPORT;
			hidg_desc_copy.rpt_desc.wDescriptorLength =
				cpu_to_le16(hidg->report_desc_length);

			length = min_t(unsigned short, length,
						   hidg_desc_copy.bLength);
			memcpy(req->buf, &hidg_desc_copy, length);
			goto respond;
			break;
		}
		case HID_DT_REPORT:
			VDBG(cdev, "USB_REQ_GET_DESCRIPTOR: REPORT\n");
			length = min_t(unsigned short, length,
						   hidg->report_desc_length);
			memcpy(req->buf, hidg->report_desc, length);
			goto respond;
			break;

		default:
			VDBG(cdev, "Unknown descriptor request 0x%x\n",
				 value >> 8);
			goto stall;
			break;
		}
		break;

	default:
		VDBG(cdev, "Unknown request 0x%x\n",
			 ctrl->bRequest);
		goto stall;
		break;
	}

stall:
	return -EOPNOTSUPP;

respond:
	req->zero = 0;
	req->length = length;
	status = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
	if (status < 0)
		ERROR(cdev, "usb_ep_queue error on ep0 %d\n", value);
	return status;
}

static void hidg_disable(struct usb_function *f)
{
	struct f_hidg *hidg = func_to_hidg(f);
	struct f_hidg_req_list *list, *next;
	unsigned long flags;

	usb_ep_disable(hidg->in_ep);

	if (hidg->out_ep) {
		usb_ep_disable(hidg->out_ep);

		spin_lock_irqsave(&hidg->read_spinlock, flags);
		list_for_each_entry_safe(list, next, &hidg->completed_out_req, list) {
			free_ep_req(hidg->out_ep, list->req);
			list_del(&list->list);
			kfree(list);
		}
		spin_unlock_irqrestore(&hidg->read_spinlock, flags);
	}

	spin_lock_irqsave(&hidg->get_report_spinlock, flags);
	if (!hidg->get_report_returned) {
		usb_ep_free_request(f->config->cdev->gadget->ep0, hidg->get_req);
		hidg->get_req = NULL;
		hidg->get_report_returned = true;
	}
	spin_unlock_irqrestore(&hidg->get_report_spinlock, flags);

	spin_lock_irqsave(&hidg->read_spinlock, flags);
	hidg->disabled = true;
	spin_unlock_irqrestore(&hidg->read_spinlock, flags);
	wake_up(&hidg->read_queue);

	spin_lock_irqsave(&hidg->write_spinlock, flags);
	if (!hidg->write_pending) {
		free_ep_req(hidg->in_ep, hidg->req);
		hidg->write_pending = 1;
	}

	hidg->req = NULL;
	spin_unlock_irqrestore(&hidg->write_spinlock, flags);
}

static int hidg_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct usb_composite_dev		*cdev = f->config->cdev;
	struct f_hidg				*hidg = func_to_hidg(f);
	struct usb_request			*req_in = NULL;
	unsigned long				flags;
	int i, status = 0;

	VDBG(cdev, "hidg_set_alt intf:%d alt:%d\n", intf, alt);

	if (hidg->in_ep != NULL) {
		/* restart endpoint */
		usb_ep_disable(hidg->in_ep);

		status = config_ep_by_speed(f->config->cdev->gadget, f,
					    hidg->in_ep);
		if (status) {
			ERROR(cdev, "config_ep_by_speed FAILED!\n");
			goto fail;
		}
		status = usb_ep_enable(hidg->in_ep);
		if (status < 0) {
			ERROR(cdev, "Enable IN endpoint FAILED!\n");
			goto fail;
		}
		hidg->in_ep->driver_data = hidg;

		req_in = hidg_alloc_ep_req(hidg->in_ep, hidg->report_length);
		if (!req_in) {
			status = -ENOMEM;
			goto disable_ep_in;
		}
	}

	if (hidg->use_out_ep && hidg->out_ep != NULL) {
		/* restart endpoint */
		usb_ep_disable(hidg->out_ep);

		status = config_ep_by_speed(f->config->cdev->gadget, f,
					    hidg->out_ep);
		if (status) {
			ERROR(cdev, "config_ep_by_speed FAILED!\n");
			goto free_req_in;
		}
		status = usb_ep_enable(hidg->out_ep);
		if (status < 0) {
			ERROR(cdev, "Enable OUT endpoint FAILED!\n");
			goto free_req_in;
		}
		hidg->out_ep->driver_data = hidg;

		/*
		 * allocate a bunch of read buffers and queue them all at once.
		 */
		for (i = 0; i < hidg->qlen && status == 0; i++) {
			struct usb_request *req =
					hidg_alloc_ep_req(hidg->out_ep,
							  hidg->report_length);
			if (req) {
				req->complete = hidg_intout_complete;
				req->context  = hidg;
				status = usb_ep_queue(hidg->out_ep, req,
						      GFP_ATOMIC);
				if (status) {
					ERROR(cdev, "%s queue req --> %d\n",
						hidg->out_ep->name, status);
					free_ep_req(hidg->out_ep, req);
				}
			} else {
				status = -ENOMEM;
				goto disable_out_ep;
			}
		}
	}

	spin_lock_irqsave(&hidg->read_spinlock, flags);
	hidg->disabled = false;
	spin_unlock_irqrestore(&hidg->read_spinlock, flags);

	if (hidg->in_ep != NULL) {
		spin_lock_irqsave(&hidg->write_spinlock, flags);
		hidg->req = req_in;
		hidg->write_pending = 0;
		spin_unlock_irqrestore(&hidg->write_spinlock, flags);

		wake_up(&hidg->write_queue);
	}
	return 0;
disable_out_ep:
	if (hidg->out_ep)
		usb_ep_disable(hidg->out_ep);
free_req_in:
	if (req_in)
		free_ep_req(hidg->in_ep, req_in);

disable_ep_in:
	if (hidg->in_ep)
		usb_ep_disable(hidg->in_ep);

fail:
	return status;
}

#ifdef CONFIG_COMPAT
static long f_hidg_compat_ioctl(struct file *file, unsigned int code,
		unsigned long value)
{
	return f_hidg_ioctl(file, code, value);
}
#endif

static const struct file_operations f_hidg_fops = {
	.owner		= THIS_MODULE,
	.open		= f_hidg_open,
	.release	= f_hidg_release,
	.write		= f_hidg_write,
	.read		= f_hidg_read,
	.poll		= f_hidg_poll,
	.unlocked_ioctl	= f_hidg_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = f_hidg_compat_ioctl,
#endif
	.llseek		= noop_llseek,
};

static int hidg_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_ep		*ep;
	struct f_hidg		*hidg = func_to_hidg(f);
	struct usb_string	*us;
	int			status;

	hidg->get_req = usb_ep_alloc_request(c->cdev->gadget->ep0, GFP_ATOMIC);
	if (!hidg->get_req)
		return -ENOMEM;

	hidg->get_req->zero = 0;
	hidg->get_req->complete = hidg_get_report_complete;
	hidg->get_req->context = hidg;
	hidg->get_report_returned = true;

	/* maybe allocate device-global string IDs, and patch descriptors */
	us = usb_gstrings_attach(c->cdev, ct_func_strings,
				 ARRAY_SIZE(ct_func_string_defs));
	if (IS_ERR(us))
		return PTR_ERR(us);
	hidg_interface_desc.iInterface = us[CT_FUNC_HID_IDX].id;

	/* allocate instance-specific interface IDs, and patch descriptors */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	hidg_interface_desc.bInterfaceNumber = status;

	/* allocate instance-specific endpoints */
	status = -ENODEV;
	ep = usb_ep_autoconfig(c->cdev->gadget, &hidg_fs_in_ep_desc);
	if (!ep)
		goto fail;
	hidg->in_ep = ep;

	hidg->out_ep = NULL;
	if (hidg->use_out_ep) {
		ep = usb_ep_autoconfig(c->cdev->gadget, &hidg_fs_out_ep_desc);
		if (!ep)
			goto fail;
		hidg->out_ep = ep;
	}

	/* used only if use_out_ep == 1 */
	hidg->set_report_buf = NULL;

	/* set descriptor dynamic values */
	hidg_interface_desc.bInterfaceSubClass = hidg->bInterfaceSubClass;
	hidg_interface_desc.bInterfaceProtocol = hidg->bInterfaceProtocol;
	hidg_interface_desc.bNumEndpoints = hidg->use_out_ep ? 2 : 1;
	hidg->protocol = HID_REPORT_PROTOCOL;
	hidg->idle = 1;
	hidg_ss_in_ep_desc.wMaxPacketSize = cpu_to_le16(hidg->report_length);
	hidg_ss_in_comp_desc.wBytesPerInterval =
				cpu_to_le16(hidg->report_length);
	hidg_hs_in_ep_desc.wMaxPacketSize = cpu_to_le16(hidg->report_length);
	hidg_fs_in_ep_desc.wMaxPacketSize = cpu_to_le16(hidg->report_length);
	hidg_ss_out_ep_desc.wMaxPacketSize = cpu_to_le16(hidg->report_length);

	/* IN endpoints: FS default=10ms, HS default=4µ-frame; user override if set */
	if (!hidg->interval_user_set) {
		hidg_fs_in_ep_desc.bInterval = 10;
		hidg_hs_in_ep_desc.bInterval = 4;
	} else {
		hidg_fs_in_ep_desc.bInterval = hidg->interval;
		hidg_hs_in_ep_desc.bInterval = hidg->interval;
	}

	hidg_ss_out_comp_desc.wBytesPerInterval =
				cpu_to_le16(hidg->report_length);
	hidg_hs_out_ep_desc.wMaxPacketSize = cpu_to_le16(hidg->report_length);
	hidg_fs_out_ep_desc.wMaxPacketSize = cpu_to_le16(hidg->report_length);
	/*
	 * We can use hidg_desc struct here but we should not relay
	 * that its content won't change after returning from this function.
	 */
	hidg_desc.rpt_desc.bDescriptorType = HID_DT_REPORT;
	hidg_desc.rpt_desc.wDescriptorLength =
		cpu_to_le16(hidg->report_desc_length);

	hidg_hs_in_ep_desc.bEndpointAddress =
		hidg_fs_in_ep_desc.bEndpointAddress;
	hidg_hs_out_ep_desc.bEndpointAddress =
		hidg_fs_out_ep_desc.bEndpointAddress;

	hidg_ss_in_ep_desc.bEndpointAddress =
		hidg_fs_in_ep_desc.bEndpointAddress;
	hidg_ss_out_ep_desc.bEndpointAddress =
		hidg_fs_out_ep_desc.bEndpointAddress;

	if (hidg->use_out_ep) {
		/* OUT endpoints: same defaults (FS=10, HS=4) unless user set */
		if (!hidg->interval_user_set) {
			hidg_fs_out_ep_desc.bInterval = 10;
			hidg_hs_out_ep_desc.bInterval = 4;
		} else {
			hidg_fs_out_ep_desc.bInterval = hidg->interval;
			hidg_hs_out_ep_desc.bInterval = hidg->interval;
		}
		status = usb_assign_descriptors(f,
			    hidg_fs_descriptors_intout,
			    hidg_hs_descriptors_intout,
			    hidg_ss_descriptors_intout,
			    hidg_ss_descriptors_intout);
	} else {
		status = usb_assign_descriptors(f,
			hidg_fs_descriptors_ssreport,
			hidg_hs_descriptors_ssreport,
			hidg_ss_descriptors_ssreport,
			hidg_ss_descriptors_ssreport);
	}
	if (status)
		goto fail;

	spin_lock_init(&hidg->write_spinlock);
	hidg->write_pending = 1;
	hidg->req = NULL;
	spin_lock_init(&hidg->read_spinlock);
	spin_lock_init(&hidg->get_report_spinlock);
	init_waitqueue_head(&hidg->write_queue);
	init_waitqueue_head(&hidg->read_queue);
	init_waitqueue_head(&hidg->get_queue);
	init_waitqueue_head(&hidg->get_id_queue);
	INIT_LIST_HEAD(&hidg->completed_out_req);
	INIT_LIST_HEAD(&hidg->report_list);

	INIT_WORK(&hidg->work, get_report_workqueue_handler);
	hidg->workqueue = alloc_workqueue("report_work",
					  WQ_FREEZABLE |
					  WQ_MEM_RECLAIM,
					  1);

	if (!hidg->workqueue) {
		status = -ENOMEM;
		goto fail_free_descs;
	}

	/* create char device */
	cdev_init(&hidg->cdev, &f_hidg_fops);
	status = cdev_device_add(&hidg->cdev, &hidg->dev);
	if (status)
		goto fail_free_all;

	return 0;
fail_free_all:
	destroy_workqueue(hidg->workqueue);
fail_free_descs:
	usb_free_all_descriptors(f);
fail:
	ERROR(f->config->cdev, "hidg_bind FAILED\n");
	if (hidg->req != NULL)
		free_ep_req(hidg->in_ep, hidg->req);

	usb_ep_free_request(c->cdev->gadget->ep0, hidg->get_req);
	hidg->get_req = NULL;

	return status;
}

static inline int hidg_get_minor(void)
{
	int ret;

	ret = ida_alloc(&hidg_ida, GFP_KERNEL);
	if (ret >= HIDG_MINORS) {
		ida_free(&hidg_ida, ret);
		ret = -ENODEV;
	}

	return ret;
}

static inline struct f_hid_opts *to_f_hid_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_hid_opts,
			    func_inst.group);
}

static void hid_attr_release(struct config_item *item)
{
	struct f_hid_opts *opts = to_f_hid_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations hidg_item_ops = {
	.release	= hid_attr_release,
};

#define F_HID_OPT(name, prec, limit)					\
static ssize_t f_hid_opts_##name##_show(struct config_item *item, char *page)\
{									\
	struct f_hid_opts *opts = to_f_hid_opts(item);			\
	int result;							\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%d\n", opts->name);			\
	mutex_unlock(&opts->lock);					\
									\
	return result;							\
}									\
									\
static ssize_t f_hid_opts_##name##_store(struct config_item *item,	\
					 const char *page, size_t len)	\
{									\
	struct f_hid_opts *opts = to_f_hid_opts(item);			\
	int ret;							\
	u##prec num;							\
									\
	mutex_lock(&opts->lock);					\
	if (opts->refcnt) {						\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtou##prec(page, 0, &num);				\
	if (ret)							\
		goto end;						\
									\
	if (num > limit) {						\
		ret = -EINVAL;						\
		goto end;						\
	}								\
	opts->name = num;						\
	ret = len;							\
									\
end:									\
	mutex_unlock(&opts->lock);					\
	return ret;							\
}									\
									\
CONFIGFS_ATTR(f_hid_opts_, name)

F_HID_OPT(subclass, 8, 255);
F_HID_OPT(protocol, 8, 255);
F_HID_OPT(no_out_endpoint, 8, 1);
F_HID_OPT(report_length, 16, 65535);

static ssize_t f_hid_opts_report_desc_show(struct config_item *item, char *page)
{
	struct f_hid_opts *opts = to_f_hid_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = opts->report_desc_length;
	memcpy(page, opts->report_desc, opts->report_desc_length);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_hid_opts_report_desc_store(struct config_item *item,
					    const char *page, size_t len)
{
	struct f_hid_opts *opts = to_f_hid_opts(item);
	int ret = -EBUSY;
	char *d;

	mutex_lock(&opts->lock);

	if (opts->refcnt)
		goto end;
	if (len > PAGE_SIZE) {
		ret = -ENOSPC;
		goto end;
	}
	d = kmemdup(page, len, GFP_KERNEL);
	if (!d) {
		ret = -ENOMEM;
		goto end;
	}
	kfree(opts->report_desc);
	opts->report_desc = d;
	opts->report_desc_length = len;
	opts->report_desc_alloc = true;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_hid_opts_, report_desc);

static ssize_t f_hid_opts_interval_show(struct config_item *item, char *page)
{
	struct f_hid_opts *opts = to_f_hid_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%d\n", opts->interval);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_hid_opts_interval_store(struct config_item *item,
		const char *page, size_t len)
{
	struct f_hid_opts *opts = to_f_hid_opts(item);
	int ret;
	unsigned int tmp;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	/* parse into a wider type first */
	ret = kstrtouint(page, 0, &tmp);
	if (ret)
		goto end;

	/* range-check against unsigned char max */
	if (tmp > 255) {
		ret = -EINVAL;
		goto end;
	}

	opts->interval = (unsigned char)tmp;
	opts->interval_user_set = true;
	ret = len;

end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_hid_opts_, interval);

static ssize_t f_hid_opts_dev_show(struct config_item *item, char *page)
{
	struct f_hid_opts *opts = to_f_hid_opts(item);

	return sprintf(page, "%d:%d\n", major, opts->minor);
}

CONFIGFS_ATTR_RO(f_hid_opts_, dev);

static struct configfs_attribute *hid_attrs[] = {
	&f_hid_opts_attr_subclass,
	&f_hid_opts_attr_protocol,
	&f_hid_opts_attr_no_out_endpoint,
	&f_hid_opts_attr_report_length,
	&f_hid_opts_attr_interval,
	&f_hid_opts_attr_report_desc,
	&f_hid_opts_attr_dev,
	NULL,
};

static const struct config_item_type hid_func_type = {
	.ct_item_ops	= &hidg_item_ops,
	.ct_attrs	= hid_attrs,
	.ct_owner	= THIS_MODULE,
};

static inline void hidg_put_minor(int minor)
{
	ida_free(&hidg_ida, minor);
}

static void hidg_free_inst(struct usb_function_instance *f)
{
	struct f_hid_opts *opts;

	opts = container_of(f, struct f_hid_opts, func_inst);

	mutex_lock(&hidg_ida_lock);

	hidg_put_minor(opts->minor);
	if (ida_is_empty(&hidg_ida))
		ghid_cleanup();

	mutex_unlock(&hidg_ida_lock);

	if (opts->report_desc_alloc)
		kfree(opts->report_desc);

	kfree(opts);
}

static struct usb_function_instance *hidg_alloc_inst(void)
{
	struct f_hid_opts *opts;
	struct usb_function_instance *ret;
	int status = 0;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);
	mutex_init(&opts->lock);

	opts->interval = 4;
	opts->interval_user_set = false;

	opts->func_inst.free_func_inst = hidg_free_inst;
	ret = &opts->func_inst;

	mutex_lock(&hidg_ida_lock);

	if (ida_is_empty(&hidg_ida)) {
		status = ghid_setup(NULL, HIDG_MINORS);
		if (status)  {
			ret = ERR_PTR(status);
			kfree(opts);
			goto unlock;
		}
	}

	opts->minor = hidg_get_minor();
	if (opts->minor < 0) {
		ret = ERR_PTR(opts->minor);
		kfree(opts);
		if (ida_is_empty(&hidg_ida))
			ghid_cleanup();
		goto unlock;
	}
	config_group_init_type_name(&opts->func_inst.group, "", &hid_func_type);

unlock:
	mutex_unlock(&hidg_ida_lock);
	return ret;
}

static void hidg_free(struct usb_function *f)
{
	struct f_hidg *hidg;
	struct f_hid_opts *opts;

	hidg = func_to_hidg(f);
	opts = container_of(f->fi, struct f_hid_opts, func_inst);
	put_device(&hidg->dev);
	mutex_lock(&opts->lock);
	--opts->refcnt;
	mutex_unlock(&opts->lock);
}

static void hidg_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_hidg *hidg = func_to_hidg(f);

	cdev_device_del(&hidg->cdev, &hidg->dev);
	destroy_workqueue(hidg->workqueue);
	usb_free_all_descriptors(f);
}

static struct usb_function *hidg_alloc(struct usb_function_instance *fi)
{
	struct f_hidg *hidg;
	struct f_hid_opts *opts;
	int ret;

	/* allocate and initialize one new instance */
	hidg = kzalloc(sizeof(*hidg), GFP_KERNEL);
	if (!hidg)
		return ERR_PTR(-ENOMEM);

	opts = container_of(fi, struct f_hid_opts, func_inst);

	mutex_lock(&opts->lock);

	device_initialize(&hidg->dev);
	hidg->dev.release = hidg_release;
	hidg->dev.class = &hidg_class;
	hidg->dev.devt = MKDEV(major, opts->minor);
	ret = dev_set_name(&hidg->dev, "hidg%d", opts->minor);
	if (ret)
		goto err_unlock;

	hidg->bInterfaceSubClass = opts->subclass;
	hidg->bInterfaceProtocol = opts->protocol;
	hidg->report_length = opts->report_length;
	hidg->report_desc_length = opts->report_desc_length;
	hidg->interval = opts->interval;
	hidg->interval_user_set = opts->interval_user_set;
	if (opts->report_desc) {
		hidg->report_desc = kmemdup(opts->report_desc,
					    opts->report_desc_length,
					    GFP_KERNEL);
		if (!hidg->report_desc) {
			ret = -ENOMEM;
			goto err_put_device;
		}
	}
	hidg->use_out_ep = !opts->no_out_endpoint;

	++opts->refcnt;
	mutex_unlock(&opts->lock);

	hidg->func.name    = "hid";
	hidg->func.bind    = hidg_bind;
	hidg->func.unbind  = hidg_unbind;
	hidg->func.set_alt = hidg_set_alt;
	hidg->func.disable = hidg_disable;
	hidg->func.setup   = hidg_setup;
	hidg->func.free_func = hidg_free;

	/* this could be made configurable at some point */
	hidg->qlen	   = 4;

	return &hidg->func;

err_put_device:
	put_device(&hidg->dev);
err_unlock:
	mutex_unlock(&opts->lock);
	return ERR_PTR(ret);
}

DECLARE_USB_FUNCTION_INIT(hid, hidg_alloc_inst, hidg_alloc);
MODULE_DESCRIPTION("USB HID function driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fabien Chouteau");

int ghid_setup(struct usb_gadget *g, int count)
{
	int status;
	dev_t dev;

	status = class_register(&hidg_class);
	if (status)
		return status;

	status = alloc_chrdev_region(&dev, 0, count, "hidg");
	if (status) {
		class_unregister(&hidg_class);
		return status;
	}

	major = MAJOR(dev);
	minors = count;

	return 0;
}

void ghid_cleanup(void)
{
	if (major) {
		unregister_chrdev_region(MKDEV(major, 0), minors);
		major = minors = 0;
	}

	class_unregister(&hidg_class);
}
