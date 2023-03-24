// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Aspeed Technology Inc.
 *
 * Aspeed Bridge IC driver
 *
 */

#include <linux/i3c/device.h>
#include <linux/i3c/master.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/sysfs.h>
#include "internals.h"
#include <linux/delay.h>
#define MQ_MSGBUF_SIZE		256
#define MQ_QUEUE_SIZE		4
#define MQ_QUEUE_NEXT(x)	(((x) + 1) & (MQ_QUEUE_SIZE - 1))

#define IBI_QUEUE_STATUS_PEC_ERR	BIT(30)
#define IBI_STATUS_LAST_FRAG	BIT(24)
#define PID_MANUF_ID_ASPEED	0x03f6
#define POLLIING_INTERVAL_MS	2000

struct mq_msg {
	int len;
	u8 *buf;
};

struct astbic {
	struct bin_attribute bin;
	struct kernfs_node *kn;

	struct i3c_device *i3cdev;

	spinlock_t lock;
	int in;
	int out;
	struct mutex mq_lock;
	struct mq_msg *curr;
	int truncated;
	struct mq_msg queue[MQ_QUEUE_SIZE];
};

static u8 mdb_table[] = {
	0xbf, /* Aspeed BIC */
	0,
};

static void i3c_ibi_mqueue_callback(struct i3c_device *dev,
				    const struct i3c_ibi_payload *payload)
{
	struct astbic *mq = dev_get_drvdata(&dev->dev);
	struct mq_msg *msg;
	u8 *buf = (u8 *)payload->data;
	struct i3c_device_info info;
	u32 status;
	const u8 *mdb;

	mutex_lock(&mq->mq_lock);
	i3c_device_get_info(dev, &info);
	msg = mq->curr;

	/* first DW is IBI status */
	status = *(u32 *)buf;

	/* then the raw data */
	buf += sizeof(status);
	memcpy(&msg->buf[msg->len], buf, payload->len - sizeof(status));
	msg->len += payload->len - sizeof(status);
	if (status & IBI_QUEUE_STATUS_PEC_ERR) {
		for (mdb = mdb_table; *mdb != 0; mdb++)
			if (buf[0] == *mdb)
				break;
		if (!(*mdb)) {
			dev_err(&dev->dev, "ibi crc/pec error: mdb = %x", buf[0]);
			mutex_unlock(&mq->mq_lock);
			return;
		}
	}
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

		mq->in = MQ_QUEUE_NEXT(mq->in);
		mq->curr = &mq->queue[mq->in];
		mq->curr->len = 0;

		if (mq->out == mq->in)
			mq->out = MQ_QUEUE_NEXT(mq->out);
		kernfs_notify(mq->kn);
	}
	mutex_unlock(&mq->mq_lock);
}

static ssize_t i3c_astbic_bin_read(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *attr, char *buf,
				   loff_t pos, size_t count)
{
	struct astbic *mq;
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

static ssize_t i3c_astbic_bin_write(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *attr, char *buf,
				    loff_t pos, size_t count)
{
	struct astbic *astbic;
	// struct i3c_device *i3c;
	struct i3c_priv_xfer xfers = {
		.rnw = false,
		.len = count,
	};
	int ret = -EACCES;

	astbic = dev_get_drvdata(container_of(kobj, struct device, kobj));
	if (!astbic) {
		count = -1;
		goto out;
	}

	xfers.data.out = buf;
	ret = i3c_device_do_priv_xfers(astbic->i3cdev, &xfers, 1);
out:
	return (!ret) ? count : ret;
}

static void i3c_ast_bridgeic_remove(struct i3c_device *i3cdev)
{
	struct device *dev = &i3cdev->dev;
	struct astbic *astbic;

	astbic = dev_get_drvdata(dev);

	i3c_device_disable_ibi(i3cdev);
	i3c_device_free_ibi(i3cdev);

	kernfs_put(astbic->kn);
	sysfs_remove_bin_file(&dev->kobj, &astbic->bin);
	devm_kfree(dev, astbic);
}

static int i3c_ast_bridgeic_probe(struct i3c_device *i3cdev)
{
	struct device *dev = &i3cdev->dev;
	struct astbic *astbic;
	struct i3c_ibi_setup ibireq = {};
	int ret, i;
	struct i3c_device_info info;
	void *buf;

	if (dev->type == &i3c_masterdev_type)
		return -ENOTSUPP;

	astbic = devm_kzalloc(dev, sizeof(*astbic), GFP_KERNEL);
	if (!astbic)
		return -ENOMEM;

	BUILD_BUG_ON(!is_power_of_2(MQ_QUEUE_SIZE));

	buf = devm_kmalloc_array(dev, MQ_QUEUE_SIZE, MQ_MSGBUF_SIZE,
				 GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < MQ_QUEUE_SIZE; i++) {
		astbic->queue[i].buf = (u8 *)buf + i * MQ_MSGBUF_SIZE;
		astbic->queue[i].len = 0;
	}
	spin_lock_init(&astbic->lock);
	mutex_init(&astbic->mq_lock);
	astbic->curr = &astbic->queue[0];

	astbic->i3cdev = i3cdev;

	sysfs_bin_attr_init(&astbic->bin);
	astbic->bin.attr.name = "mqueue";
	astbic->bin.attr.mode = 0600;
	astbic->bin.read = i3c_astbic_bin_read;
	astbic->bin.write = i3c_astbic_bin_write;
	astbic->bin.size = MQ_MSGBUF_SIZE * MQ_QUEUE_SIZE;
	ret = sysfs_create_bin_file(&dev->kobj, &astbic->bin);

	astbic->kn = kernfs_find_and_get(dev->kobj.sd, astbic->bin.attr.name);
	if (!astbic->kn) {
		sysfs_remove_bin_file(&dev->kobj, &astbic->bin);
		return -EFAULT;
	}

	i3c_device_get_info(i3cdev, &info);

	ret = i3c_device_setmrl_ccc(i3cdev, &info, MQ_MSGBUF_SIZE,
					    min(MQ_MSGBUF_SIZE, __UINT8_MAX__));
	if (ret) {
		ret = i3c_device_getmrl_ccc(i3cdev, &info);
		if (ret)
			return ret;
	}

	dev_set_drvdata(dev, astbic);

	ibireq.handler = i3c_ibi_mqueue_callback;
	ibireq.max_payload_len = MQ_MSGBUF_SIZE;
	ibireq.num_slots = MQ_QUEUE_SIZE;

	ret = i3c_device_request_ibi(astbic->i3cdev, &ibireq);
	ret |= i3c_device_enable_ibi(astbic->i3cdev);
	if (ret) {
		kernfs_put(astbic->kn);
		sysfs_remove_bin_file(&dev->kobj, &astbic->bin);
		return ret;
	}
	return 0;
}

static const struct i3c_device_id i3c_ast_bridgeic_ids[] = {
	I3C_DEVICE(0x3f6, 0x7341, (void *)0),
	I3C_DEVICE(0x3f6, 0x8000, (void *)0),
	I3C_DEVICE(0x3f6, 0x8001, (void *)0),
	I3C_DEVICE(0x3f6, 0x0503, (void *)0),
	I3C_DEVICE(0x3f6, 0xA001, (void *)0),
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i3c, i3c_ast_bridgeic_ids);

static struct i3c_driver astbic_driver = {
	.driver = {
		.name = "i3c-ast-bridgeic",
	},
	.probe = i3c_ast_bridgeic_probe,
	.remove = i3c_ast_bridgeic_remove,
	.id_table = i3c_ast_bridgeic_ids,
};
module_i3c_driver(astbic_driver);

MODULE_AUTHOR("Andy Chung <Andy_Chung@wiwynn.com>");
MODULE_DESCRIPTION("I3C Aspeed bridge IC driver");
MODULE_LICENSE("GPL v2");
