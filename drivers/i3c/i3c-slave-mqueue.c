// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Aspeed Technology Inc.
 */

#include <linux/i3c/master.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>

#define MQ_MSGBUF_SIZE		256
#define MQ_QUEUE_SIZE		4
#define MQ_QUEUE_NEXT(x)	(((x) + 1) & (MQ_QUEUE_SIZE - 1))

#define IBI_STATUS_LAST_FRAG	BIT(24)

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

	struct i3c_master_controller *i3c_controller;
};

static void i3c_slave_mqueue_callback(struct i3c_master_controller *master,
				      const struct i3c_slave_payload *payload)
{
	struct mq_queue *mq = dev_get_drvdata(&master->dev);
	struct mq_msg *msg = mq->curr;

	memcpy(msg->buf, (u8 *)payload->data, payload->len);
	msg->len = payload->len;

	spin_lock(&mq->lock);
	mq->in = MQ_QUEUE_NEXT(mq->in);
	mq->curr = &mq->queue[mq->in];
	mq->curr->len = 0;

	if (mq->out == mq->in)
		mq->out = MQ_QUEUE_NEXT(mq->out);
	spin_unlock(&mq->lock);
	kernfs_notify(mq->kn);
}

static ssize_t i3c_slave_mqueue_bin_read(struct file *filp, struct kobject *kobj,
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

static ssize_t i3c_slave_mqueue_bin_write(struct file *filp,
					  struct kobject *kobj,
					  struct bin_attribute *attr, char *buf,
					  loff_t pos, size_t count)
{
	struct mq_queue *mq;
	unsigned long flags;
	struct i3c_slave_payload payload;

	payload.data = buf;
	payload.len = count;
	mq = dev_get_drvdata(container_of(kobj, struct device, kobj));

	spin_lock_irqsave(&mq->lock, flags);
	i3c_master_send_sir(mq->i3c_controller, &payload);
	spin_unlock_irqrestore(&mq->lock, flags);

	return count;
}

int i3c_slave_mqueue_probe(struct i3c_master_controller *master)
{
	struct mq_queue *mq;
	int ret, i;
	void *buf;
	struct i3c_slave_setup req = {};
	struct device *dev = &master->dev;

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

	dev_set_drvdata(dev, &mq->bin);

	spin_lock_init(&mq->lock);
	mq->curr = &mq->queue[0];

	sysfs_bin_attr_init(&mq->bin);
	mq->bin.attr.name = "slave-mqueue";
	mq->bin.attr.mode = 0600;
	mq->bin.read = i3c_slave_mqueue_bin_read;
	mq->bin.write = i3c_slave_mqueue_bin_write;
	mq->bin.size = MQ_MSGBUF_SIZE * MQ_QUEUE_SIZE;

	mq->i3c_controller = master;

	ret = sysfs_create_bin_file(&dev->kobj, &mq->bin);
	if (ret)
		return ret;

	mq->kn = kernfs_find_and_get(dev->kobj.sd, mq->bin.attr.name);
	if (!mq->kn) {
		sysfs_remove_bin_file(&dev->kobj, &mq->bin);
		return -EFAULT;
	}

	req.handler = i3c_slave_mqueue_callback;
	req.max_payload_len = MQ_MSGBUF_SIZE;
	req.num_slots = MQ_QUEUE_SIZE;

	ret = i3c_master_register_slave(master, &req);

	if (ret) {
		kernfs_put(mq->kn);
		sysfs_remove_bin_file(&dev->kobj, &mq->bin);
		return ret;
	}

	return 0;
}

int i3c_slave_mqueue_remove(struct i3c_master_controller *master)
{
	struct device *dev = &master->dev;
	struct mq_queue *mq = dev_get_drvdata(dev);

	i3c_master_unregister_slave(master);

	kernfs_put(mq->kn);
	sysfs_remove_bin_file(&dev->kobj, &mq->bin);

	return 0;
}
