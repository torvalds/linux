/*
 *	Executive OSM
 *
 * 	Copyright (C) 1999-2002	Red Hat Software
 *
 *	Written by Alan Cox, Building Number Three Ltd
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 *
 *	A lot of the I2O message side code from this is taken from the Red
 *	Creek RCPCI45 adapter driver by Red Creek Communications
 *
 *	Fixes/additions:
 *		Philipp Rumpf
 *		Juha Sievänen <Juha.Sievanen@cs.Helsinki.FI>
 *		Auvo Häkkinen <Auvo.Hakkinen@cs.Helsinki.FI>
 *		Deepak Saxena <deepak@plexity.net>
 *		Boji T Kannanthanam <boji.t.kannanthanam@intel.com>
 *		Alan Cox <alan@redhat.com>:
 *			Ported to Linux 2.5.
 *		Markus Lidel <Markus.Lidel@shadowconnect.com>:
 *			Minor fixes for 2.6.
 *		Markus Lidel <Markus.Lidel@shadowconnect.com>:
 *			Support for sysfs included.
 */

#include <linux/module.h>
#include <linux/i2o.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/param.h>		/* HZ */
#include "core.h"

#define OSM_NAME "exec-osm"

struct i2o_driver i2o_exec_driver;

static int i2o_exec_lct_notify(struct i2o_controller *c, u32 change_ind);

/* global wait list for POST WAIT */
static LIST_HEAD(i2o_exec_wait_list);

/* Wait struct needed for POST WAIT */
struct i2o_exec_wait {
	wait_queue_head_t *wq;	/* Pointer to Wait queue */
	struct i2o_dma dma;	/* DMA buffers to free on failure */
	u32 tcntxt;		/* transaction context from reply */
	int complete;		/* 1 if reply received otherwise 0 */
	u32 m;			/* message id */
	struct i2o_message *msg;	/* pointer to the reply message */
	struct list_head list;	/* node in global wait list */
};

/* Exec OSM class handling definition */
static struct i2o_class_id i2o_exec_class_id[] = {
	{I2O_CLASS_EXECUTIVE},
	{I2O_CLASS_END}
};

/**
 *	i2o_exec_wait_alloc - Allocate a i2o_exec_wait struct an initialize it
 *
 *	Allocate the i2o_exec_wait struct and initialize the wait.
 *
 *	Returns i2o_exec_wait pointer on success or negative error code on
 *	failure.
 */
static struct i2o_exec_wait *i2o_exec_wait_alloc(void)
{
	struct i2o_exec_wait *wait;

	wait = kmalloc(sizeof(*wait), GFP_KERNEL);
	if (!wait)
		return ERR_PTR(-ENOMEM);

	memset(wait, 0, sizeof(*wait));

	INIT_LIST_HEAD(&wait->list);

	return wait;
};

/**
 *	i2o_exec_wait_free - Free a i2o_exec_wait struct
 *	@i2o_exec_wait: I2O wait data which should be cleaned up
 */
static void i2o_exec_wait_free(struct i2o_exec_wait *wait)
{
	kfree(wait);
};

/**
 * 	i2o_msg_post_wait_mem - Post and wait a message with DMA buffers
 *	@c: controller
 *	@m: message to post
 *	@timeout: time in seconds to wait
 *	@dma: i2o_dma struct of the DMA buffer to free on failure
 *
 * 	This API allows an OSM to post a message and then be told whether or
 *	not the system received a successful reply. If the message times out
 *	then the value '-ETIMEDOUT' is returned. This is a special case. In
 *	this situation the message may (should) complete at an indefinite time
 *	in the future. When it completes it will use the memory buffer
 *	attached to the request. If -ETIMEDOUT is returned then the memory
 *	buffer must not be freed. Instead the event completion will free them
 *	for you. In all other cases the buffer are your problem.
 *
 *	Returns 0 on success, negative error code on timeout or positive error
 *	code from reply.
 */
int i2o_msg_post_wait_mem(struct i2o_controller *c, u32 m, unsigned long
			  timeout, struct i2o_dma *dma)
{
	DECLARE_WAIT_QUEUE_HEAD(wq);
	struct i2o_exec_wait *wait;
	static u32 tcntxt = 0x80000000;
	struct i2o_message __iomem *msg = i2o_msg_in_to_virt(c, m);
	int rc = 0;

	wait = i2o_exec_wait_alloc();
	if (!wait)
		return -ENOMEM;

	if (tcntxt == 0xffffffff)
		tcntxt = 0x80000000;

	if (dma)
		wait->dma = *dma;

	/*
	 * Fill in the message initiator context and transaction context.
	 * We will only use transaction contexts >= 0x80000000 for POST WAIT,
	 * so we could find a POST WAIT reply easier in the reply handler.
	 */
	writel(i2o_exec_driver.context, &msg->u.s.icntxt);
	wait->tcntxt = tcntxt++;
	writel(wait->tcntxt, &msg->u.s.tcntxt);

	/*
	 * Post the message to the controller. At some point later it will
	 * return. If we time out before it returns then complete will be zero.
	 */
	i2o_msg_post(c, m);

	if (!wait->complete) {
		wait->wq = &wq;
		/*
		 * we add elements add the head, because if a entry in the list
		 * will never be removed, we have to iterate over it every time
		 */
		list_add(&wait->list, &i2o_exec_wait_list);

		wait_event_interruptible_timeout(wq, wait->complete,
						 timeout * HZ);

		wait->wq = NULL;
	}

	barrier();

	if (wait->complete) {
		rc = le32_to_cpu(wait->msg->body[0]) >> 24;
		i2o_flush_reply(c, wait->m);
		i2o_exec_wait_free(wait);
	} else {
		/*
		 * We cannot remove it now. This is important. When it does
		 * terminate (which it must do if the controller has not
		 * died...) then it will otherwise scribble on stuff.
		 *
		 * FIXME: try abort message
		 */
		if (dma)
			dma->virt = NULL;

		rc = -ETIMEDOUT;
	}

	return rc;
};

/**
 *	i2o_msg_post_wait_complete - Reply to a i2o_msg_post request from IOP
 *	@c: I2O controller which answers
 *	@m: message id
 *	@msg: pointer to the I2O reply message
 *	@context: transaction context of request
 *
 *	This function is called in interrupt context only. If the reply reached
 *	before the timeout, the i2o_exec_wait struct is filled with the message
 *	and the task will be waked up. The task is now responsible for returning
 *	the message m back to the controller! If the message reaches us after
 *	the timeout clean up the i2o_exec_wait struct (including allocated
 *	DMA buffer).
 *
 *	Return 0 on success and if the message m should not be given back to the
 *	I2O controller, or >0 on success and if the message should be given back
 *	afterwords. Returns negative error code on failure. In this case the
 *	message must also be given back to the controller.
 */
static int i2o_msg_post_wait_complete(struct i2o_controller *c, u32 m,
				      struct i2o_message *msg, u32 context)
{
	struct i2o_exec_wait *wait, *tmp;
	unsigned long flags;
	static spinlock_t lock = SPIN_LOCK_UNLOCKED;
	int rc = 1;

	/*
	 * We need to search through the i2o_exec_wait_list to see if the given
	 * message is still outstanding. If not, it means that the IOP took
	 * longer to respond to the message than we had allowed and timer has
	 * already expired. Not much we can do about that except log it for
	 * debug purposes, increase timeout, and recompile.
	 */
	spin_lock_irqsave(&lock, flags);
	list_for_each_entry_safe(wait, tmp, &i2o_exec_wait_list, list) {
		if (wait->tcntxt == context) {
			list_del(&wait->list);

			spin_unlock_irqrestore(&lock, flags);

			wait->m = m;
			wait->msg = msg;
			wait->complete = 1;

			barrier();

			if (wait->wq) {
				wake_up_interruptible(wait->wq);
				rc = 0;
			} else {
				struct device *dev;

				dev = &c->pdev->dev;

				pr_debug("%s: timedout reply received!\n",
					 c->name);
				i2o_dma_free(dev, &wait->dma);
				i2o_exec_wait_free(wait);
				rc = -1;
			}

			return rc;
		}
	}

	spin_unlock_irqrestore(&lock, flags);

	osm_warn("%s: Bogus reply in POST WAIT (tr-context: %08x)!\n", c->name,
		 context);

	return -1;
};

/**
 *	i2o_exec_show_vendor_id - Displays Vendor ID of controller
 *	@d: device of which the Vendor ID should be displayed
 *	@buf: buffer into which the Vendor ID should be printed
 *
 *	Returns number of bytes printed into buffer.
 */
static ssize_t i2o_exec_show_vendor_id(struct device *d, struct device_attribute *attr, char *buf)
{
	struct i2o_device *dev = to_i2o_device(d);
	u16 id;

	if (i2o_parm_field_get(dev, 0x0000, 0, &id, 2)) {
		sprintf(buf, "0x%04x", id);
		return strlen(buf) + 1;
	}

	return 0;
};

/**
 *	i2o_exec_show_product_id - Displays Product ID of controller
 *	@d: device of which the Product ID should be displayed
 *	@buf: buffer into which the Product ID should be printed
 *
 *	Returns number of bytes printed into buffer.
 */
static ssize_t i2o_exec_show_product_id(struct device *d, struct device_attribute *attr, char *buf)
{
	struct i2o_device *dev = to_i2o_device(d);
	u16 id;

	if (i2o_parm_field_get(dev, 0x0000, 1, &id, 2)) {
		sprintf(buf, "0x%04x", id);
		return strlen(buf) + 1;
	}

	return 0;
};

/* Exec-OSM device attributes */
static DEVICE_ATTR(vendor_id, S_IRUGO, i2o_exec_show_vendor_id, NULL);
static DEVICE_ATTR(product_id, S_IRUGO, i2o_exec_show_product_id, NULL);

/**
 *	i2o_exec_probe - Called if a new I2O device (executive class) appears
 *	@dev: I2O device which should be probed
 *
 *	Registers event notification for every event from Executive device. The
 *	return is always 0, because we want all devices of class Executive.
 *
 *	Returns 0 on success.
 */
static int i2o_exec_probe(struct device *dev)
{
	struct i2o_device *i2o_dev = to_i2o_device(dev);
	struct i2o_controller *c = i2o_dev->iop;

	i2o_event_register(i2o_dev, &i2o_exec_driver, 0, 0xffffffff);

	c->exec = i2o_dev;

	i2o_exec_lct_notify(c, c->lct->change_ind + 1);

	device_create_file(dev, &dev_attr_vendor_id);
	device_create_file(dev, &dev_attr_product_id);

	return 0;
};

/**
 *	i2o_exec_remove - Called on I2O device removal
 *	@dev: I2O device which was removed
 *
 *	Unregisters event notification from Executive I2O device.
 *
 *	Returns 0 on success.
 */
static int i2o_exec_remove(struct device *dev)
{
	device_remove_file(dev, &dev_attr_product_id);
	device_remove_file(dev, &dev_attr_vendor_id);

	i2o_event_register(to_i2o_device(dev), &i2o_exec_driver, 0, 0);

	return 0;
};

/**
 *	i2o_exec_lct_modified - Called on LCT NOTIFY reply
 *	@c: I2O controller on which the LCT has modified
 *
 *	This function handles asynchronus LCT NOTIFY replies. It parses the
 *	new LCT and if the buffer for the LCT was to small sends a LCT NOTIFY
 *	again, otherwise send LCT NOTIFY to get informed on next LCT change.
 */
static void i2o_exec_lct_modified(struct i2o_controller *c)
{
	u32 change_ind = 0;

	if (i2o_device_parse_lct(c) != -EAGAIN)
		change_ind = c->lct->change_ind + 1;

	i2o_exec_lct_notify(c, change_ind);
};

/**
 *	i2o_exec_reply -  I2O Executive reply handler
 *	@c: I2O controller from which the reply comes
 *	@m: message id
 *	@msg: pointer to the I2O reply message
 *
 *	This function is always called from interrupt context. If a POST WAIT
 *	reply was received, pass it to the complete function. If a LCT NOTIFY
 *	reply was received, a new event is created to handle the update.
 *
 *	Returns 0 on success and if the reply should not be flushed or > 0
 *	on success and if the reply should be flushed. Returns negative error
 *	code on failure and if the reply should be flushed.
 */
static int i2o_exec_reply(struct i2o_controller *c, u32 m,
			  struct i2o_message *msg)
{
	u32 context;

	if (le32_to_cpu(msg->u.head[0]) & MSG_FAIL) {
		/*
		 * If Fail bit is set we must take the transaction context of
		 * the preserved message to find the right request again.
		 */
		struct i2o_message __iomem *pmsg;
		u32 pm;

		pm = le32_to_cpu(msg->body[3]);

		pmsg = i2o_msg_in_to_virt(c, pm);

		i2o_report_status(KERN_INFO, "i2o_core", msg);

		context = readl(&pmsg->u.s.tcntxt);

		/* Release the preserved msg */
		i2o_msg_nop(c, pm);
	} else
		context = le32_to_cpu(msg->u.s.tcntxt);

	if (context & 0x80000000)
		return i2o_msg_post_wait_complete(c, m, msg, context);

	if ((le32_to_cpu(msg->u.head[1]) >> 24) == I2O_CMD_LCT_NOTIFY) {
		struct work_struct *work;

		pr_debug("%s: LCT notify received\n", c->name);

		work = kmalloc(sizeof(*work), GFP_ATOMIC);
		if (!work)
			return -ENOMEM;

		INIT_WORK(work, (void (*)(void *))i2o_exec_lct_modified, c);
		queue_work(i2o_exec_driver.event_queue, work);
		return 1;
	}

	/*
	 * If this happens, we want to dump the message to the syslog so
	 * it can be sent back to the card manufacturer by the end user
	 * to aid in debugging.
	 *
	 */
	printk(KERN_WARNING "%s: Unsolicited message reply sent to core!"
	       "Message dumped to syslog\n", c->name);
	i2o_dump_message(msg);

	return -EFAULT;
}

/**
 *	i2o_exec_event - Event handling function
 *	@evt: Event which occurs
 *
 *	Handles events send by the Executive device. At the moment does not do
 *	anything useful.
 */
static void i2o_exec_event(struct i2o_event *evt)
{
	if (likely(evt->i2o_dev))
		osm_debug("Event received from device: %d\n",
			  evt->i2o_dev->lct_data.tid);
	kfree(evt);
};

/**
 *	i2o_exec_lct_get - Get the IOP's Logical Configuration Table
 *	@c: I2O controller from which the LCT should be fetched
 *
 *	Send a LCT NOTIFY request to the controller, and wait
 *	I2O_TIMEOUT_LCT_GET seconds until arrival of response. If the LCT is
 *	to large, retry it.
 *
 *	Returns 0 on success or negative error code on failure.
 */
int i2o_exec_lct_get(struct i2o_controller *c)
{
	struct i2o_message __iomem *msg;
	u32 m;
	int i = 0;
	int rc = -EAGAIN;

	for (i = 1; i <= I2O_LCT_GET_TRIES; i++) {
		m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
		if (m == I2O_QUEUE_EMPTY)
			return -ETIMEDOUT;

		writel(EIGHT_WORD_MSG_SIZE | SGL_OFFSET_6, &msg->u.head[0]);
		writel(I2O_CMD_LCT_NOTIFY << 24 | HOST_TID << 12 | ADAPTER_TID,
		       &msg->u.head[1]);
		writel(0xffffffff, &msg->body[0]);
		writel(0x00000000, &msg->body[1]);
		writel(0xd0000000 | c->dlct.len, &msg->body[2]);
		writel(c->dlct.phys, &msg->body[3]);

		rc = i2o_msg_post_wait(c, m, I2O_TIMEOUT_LCT_GET);
		if (rc < 0)
			break;

		rc = i2o_device_parse_lct(c);
		if (rc != -EAGAIN)
			break;
	}

	return rc;
}

/**
 *	i2o_exec_lct_notify - Send a asynchronus LCT NOTIFY request
 *	@c: I2O controller to which the request should be send
 *	@change_ind: change indicator
 *
 *	This function sends a LCT NOTIFY request to the I2O controller with
 *	the change indicator change_ind. If the change_ind == 0 the controller
 *	replies immediately after the request. If change_ind > 0 the reply is
 *	send after change indicator of the LCT is > change_ind.
 */
static int i2o_exec_lct_notify(struct i2o_controller *c, u32 change_ind)
{
	i2o_status_block *sb = c->status_block.virt;
	struct device *dev;
	struct i2o_message __iomem *msg;
	u32 m;

	dev = &c->pdev->dev;

	if (i2o_dma_realloc(dev, &c->dlct, sb->expected_lct_size, GFP_KERNEL))
		return -ENOMEM;

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	writel(EIGHT_WORD_MSG_SIZE | SGL_OFFSET_6, &msg->u.head[0]);
	writel(I2O_CMD_LCT_NOTIFY << 24 | HOST_TID << 12 | ADAPTER_TID,
	       &msg->u.head[1]);
	writel(i2o_exec_driver.context, &msg->u.s.icntxt);
	writel(0, &msg->u.s.tcntxt);	/* FIXME */
	writel(0xffffffff, &msg->body[0]);
	writel(change_ind, &msg->body[1]);
	writel(0xd0000000 | c->dlct.len, &msg->body[2]);
	writel(c->dlct.phys, &msg->body[3]);

	i2o_msg_post(c, m);

	return 0;
};

/* Exec OSM driver struct */
struct i2o_driver i2o_exec_driver = {
	.name = OSM_NAME,
	.reply = i2o_exec_reply,
	.event = i2o_exec_event,
	.classes = i2o_exec_class_id,
	.driver = {
		   .probe = i2o_exec_probe,
		   .remove = i2o_exec_remove,
		   },
};

/**
 *	i2o_exec_init - Registers the Exec OSM
 *
 *	Registers the Exec OSM in the I2O core.
 *
 *	Returns 0 on success or negative error code on failure.
 */
int __init i2o_exec_init(void)
{
	return i2o_driver_register(&i2o_exec_driver);
};

/**
 *	i2o_exec_exit - Removes the Exec OSM
 *
 *	Unregisters the Exec OSM from the I2O core.
 */
void __exit i2o_exec_exit(void)
{
	i2o_driver_unregister(&i2o_exec_driver);
};

EXPORT_SYMBOL(i2o_msg_post_wait_mem);
EXPORT_SYMBOL(i2o_exec_lct_get);
