// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Aspeed Technology Inc.
 */

#include <linux/i3c/device.h>
#include <linux/i3c/master.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include "internals.h"

#define MQ_MSGBUF_SIZE		256
#define MQ_QUEUE_SIZE		4
#define MQ_QUEUE_NEXT(x)	(((x) + 1) & (MQ_QUEUE_SIZE - 1))

#define IBI_STATUS_LAST_FRAG	BIT(24)
#define PID_MANUF_ID_ASPEED	0x03f6

struct mq_msg {
	int len;
	u8 *buf;
};

struct mq_queue {
	struct bin_attribute bin;
	struct kernfs_node *kn;

	spinlock_t lock;
	int in;
	int out;

	struct mq_msg *curr;
	int truncated;
	struct mq_msg queue[MQ_QUEUE_SIZE];
};

static void i3c_ibi_mqueue_callback(struct i3c_device *dev,
				    const struct i3c_ibi_payload *payload)
{
	struct mq_queue *mq = dev_get_drvdata(&dev->dev);
	struct mq_msg *msg = mq->curr;
	u8 *buf = (u8 *)payload->data;
	struct i3c_device_info info;
	u32 status;

	i3c_device_get_info(dev, &info);
	/* first DW is IBI status */
	status = *(u32 *)buf;

	/* then the raw data */
	buf += sizeof(status);
	memcpy(&msg->buf[msg->len], buf, payload->len - sizeof(status));
	msg->len += payload->len - sizeof(status);

	/* if last fragment, notify and update pointers */
	if (status & IBI_STATUS_LAST_FRAG) {
		/* check pending-read-notification */
		if (IS_MDB_PENDING_READ_NOTIFY(msg->buf[0])) {
			struct i3c_priv_xfer xfers[1] = {
				{
					.rnw = true,
					.len = info.max_read_len,
					.data.in = msg->buf,
				},
			};

			i3c_device_do_priv_xfers(dev, xfers, 1);

			msg->len = xfers[0].len;
		}

		spin_lock(&mq->lock);
		mq->in = MQ_QUEUE_NEXT(mq->in);
		mq->curr = &mq->queue[mq->in];
		mq->curr->len = 0;

		if (mq->out == mq->in)
			mq->out = MQ_QUEUE_NEXT(mq->out);
		spin_unlock(&mq->lock);
		kernfs_notify(mq->kn);
	}
}

static ssize_t i3c_ibi_mqueue_bin_read(struct file *filp, struct kobject *kobj,
				       struct bin_attribute *attr, char *buf,
				       loff_t pos, size_t count)
{
	struct mq_queue *mq;
	struct mq_msg *msg;
	unsigned long flags;
	bool more = false;
	ssize_t ret = 0;

	mq = dev_get_drvdata(container_of(kobj, struct device, kobj));

	spin_lock_irqsave(&mq->lock, flags);
	if (mq->out != mq->in) {
		msg = &mq->queue[mq->out];

		if (msg->len <= count) {
			ret = msg->len;
			memcpy(buf, msg->buf, ret);
		} else {
			ret = -EOVERFLOW; /* Drop this HUGE one. */
		}

		mq->out = MQ_QUEUE_NEXT(mq->out);
		if (mq->out != mq->in)
			more = true;
	}
	spin_unlock_irqrestore(&mq->lock, flags);

	if (more)
		kernfs_notify(mq->kn);

	return ret;
}

static int i3c_ibi_mqueue_probe(struct i3c_device *i3cdev)
{
	struct device *dev = &i3cdev->dev;
	struct mq_queue *mq;
	struct i3c_ibi_setup ibireq = {};
	int ret, i;
	struct i3c_device_info info;
	void *buf;

	if (dev->type == &i3c_masterdev_type)
		return -ENOTSUPP;

	mq = devm_kzalloc(dev, sizeof(*mq), GFP_KERNEL);
	if (!mq)
		return -ENOMEM;

	BUILD_BUG_ON(!is_power_of_2(MQ_QUEUE_SIZE));

	buf = devm_kmalloc_array(dev, MQ_QUEUE_SIZE, MQ_MSGBUF_SIZE,
				 GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < MQ_QUEUE_SIZE; i++) {
		mq->queue[i].buf = buf + i * MQ_MSGBUF_SIZE;
		mq->queue[i].len = 0;
	}

	i3c_device_get_info(i3cdev, &info);

	ret = i3c_device_setmrl_ccc(i3cdev, &info, MQ_MSGBUF_SIZE,
					    min(MQ_MSGBUF_SIZE, __UINT8_MAX__));
	if (ret) {
		ret = i3c_device_getmrl_ccc(i3cdev, &info);
		if (ret)
			return ret;
	}

	dev_set_drvdata(dev, mq);

	spin_lock_init(&mq->lock);
	mq->curr = &mq->queue[0];

	sysfs_bin_attr_init(&mq->bin);
	mq->bin.attr.name = "ibi-mqueue";
	mq->bin.attr.mode = 0400;
	mq->bin.read = i3c_ibi_mqueue_bin_read;
	mq->bin.size = MQ_MSGBUF_SIZE * MQ_QUEUE_SIZE;

	ret = sysfs_create_bin_file(&dev->kobj, &mq->bin);
	if (ret)
		return ret;

	mq->kn = kernfs_find_and_get(dev->kobj.sd, mq->bin.attr.name);
	if (!mq->kn) {
		sysfs_remove_bin_file(&dev->kobj, &mq->bin);
		return -EFAULT;
	}

	ibireq.handler = i3c_ibi_mqueue_callback;
	ibireq.max_payload_len = MQ_MSGBUF_SIZE;
	ibireq.num_slots = MQ_QUEUE_SIZE;

	ret = i3c_device_request_ibi(i3cdev, &ibireq);
	ret |= i3c_device_enable_ibi(i3cdev);

	if (ret) {
		kernfs_put(mq->kn);
		sysfs_remove_bin_file(&dev->kobj, &mq->bin);
		return ret;
	}

	return 0;
}

static void i3c_ibi_mqueue_remove(struct i3c_device *i3cdev)
{
	struct mq_queue *mq = dev_get_drvdata(&i3cdev->dev);

	i3c_device_disable_ibi(i3cdev);
	i3c_device_free_ibi(i3cdev);

	kernfs_put(mq->kn);
	sysfs_remove_bin_file(&i3cdev->dev.kobj, &mq->bin);
}

static const struct i3c_device_id i3c_ibi_mqueue_ids[] = {
	I3C_DEVICE(0x3f6, 0x8000, (void *)0),
	I3C_DEVICE(0x3f6, 0x8001, (void *)0),
	I3C_DEVICE(0x3f6, 0x0503, (void *)0),
	I3C_DEVICE(0x3f6, 0xA001, (void *)0),
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i3c, i3c_ibi_mqueue_ids);

static struct i3c_driver ibi_mqueue_driver = {
	.driver = {
		.name = "i3c-ibi-mqueue",
	},
	.probe = i3c_ibi_mqueue_probe,
	.remove = i3c_ibi_mqueue_remove,
	.id_table = i3c_ibi_mqueue_ids,
};
module_i3c_driver(ibi_mqueue_driver);

MODULE_AUTHOR("Dylan Hung <dylan_hung@aspeedtech.com>");
MODULE_DESCRIPTION("I3C IBI mqueue driver");
MODULE_LICENSE("GPL v2");
