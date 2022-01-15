// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015-2018, Intel Corporation.
 */

#define pr_fmt(fmt) "kcs-bmc: " fmt

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ipmi_bmc.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "kcs_bmc_client.h"

/* Different phases of the KCS BMC module.
 *  KCS_PHASE_IDLE:
 *            BMC should not be expecting nor sending any data.
 *  KCS_PHASE_WRITE_START:
 *            BMC is receiving a WRITE_START command from system software.
 *  KCS_PHASE_WRITE_DATA:
 *            BMC is receiving a data byte from system software.
 *  KCS_PHASE_WRITE_END_CMD:
 *            BMC is waiting a last data byte from system software.
 *  KCS_PHASE_WRITE_DONE:
 *            BMC has received the whole request from system software.
 *  KCS_PHASE_WAIT_READ:
 *            BMC is waiting the response from the upper IPMI service.
 *  KCS_PHASE_READ:
 *            BMC is transferring the response to system software.
 *  KCS_PHASE_ABORT_ERROR1:
 *            BMC is waiting error status request from system software.
 *  KCS_PHASE_ABORT_ERROR2:
 *            BMC is waiting for idle status afer error from system software.
 *  KCS_PHASE_ERROR:
 *            BMC has detected a protocol violation at the interface level.
 */
enum kcs_ipmi_phases {
	KCS_PHASE_IDLE,

	KCS_PHASE_WRITE_START,
	KCS_PHASE_WRITE_DATA,
	KCS_PHASE_WRITE_END_CMD,
	KCS_PHASE_WRITE_DONE,

	KCS_PHASE_WAIT_READ,
	KCS_PHASE_READ,

	KCS_PHASE_ABORT_ERROR1,
	KCS_PHASE_ABORT_ERROR2,
	KCS_PHASE_ERROR
};

/* IPMI 2.0 - Table 9-4, KCS Interface Status Codes */
enum kcs_ipmi_errors {
	KCS_NO_ERROR                = 0x00,
	KCS_ABORTED_BY_COMMAND      = 0x01,
	KCS_ILLEGAL_CONTROL_CODE    = 0x02,
	KCS_LENGTH_ERROR            = 0x06,
	KCS_UNSPECIFIED_ERROR       = 0xFF
};

struct kcs_bmc_ipmi {
	struct list_head entry;

	struct kcs_bmc_client client;

	spinlock_t lock;

	enum kcs_ipmi_phases phase;
	enum kcs_ipmi_errors error;

	wait_queue_head_t queue;
	bool data_in_avail;
	int  data_in_idx;
	u8  *data_in;

	int  data_out_idx;
	int  data_out_len;
	u8  *data_out;

	struct mutex mutex;
	u8 *kbuffer;

	struct miscdevice miscdev;
};

#define DEVICE_NAME "ipmi-kcs"

#define KCS_MSG_BUFSIZ    1000

#define KCS_ZERO_DATA     0

/* IPMI 2.0 - Table 9-1, KCS Interface Status Register Bits */
#define KCS_STATUS_STATE(state) (state << 6)
#define KCS_STATUS_STATE_MASK   GENMASK(7, 6)
#define KCS_STATUS_CMD_DAT      BIT(3)
#define KCS_STATUS_SMS_ATN      BIT(2)
#define KCS_STATUS_IBF          BIT(1)
#define KCS_STATUS_OBF          BIT(0)

/* IPMI 2.0 - Table 9-2, KCS Interface State Bits */
enum kcs_states {
	IDLE_STATE  = 0,
	READ_STATE  = 1,
	WRITE_STATE = 2,
	ERROR_STATE = 3,
};

/* IPMI 2.0 - Table 9-3, KCS Interface Control Codes */
#define KCS_CMD_GET_STATUS_ABORT  0x60
#define KCS_CMD_WRITE_START       0x61
#define KCS_CMD_WRITE_END         0x62
#define KCS_CMD_READ_BYTE         0x68

static inline void set_state(struct kcs_bmc_ipmi *priv, u8 state)
{
	kcs_bmc_update_status(priv->client.dev, KCS_STATUS_STATE_MASK, KCS_STATUS_STATE(state));
}

static void kcs_bmc_ipmi_force_abort(struct kcs_bmc_ipmi *priv)
{
	set_state(priv, ERROR_STATE);
	kcs_bmc_read_data(priv->client.dev);
	kcs_bmc_write_data(priv->client.dev, KCS_ZERO_DATA);

	priv->phase = KCS_PHASE_ERROR;
	priv->data_in_avail = false;
	priv->data_in_idx = 0;
}

static void kcs_bmc_ipmi_handle_data(struct kcs_bmc_ipmi *priv)
{
	struct kcs_bmc_device *dev;
	u8 data;

	dev = priv->client.dev;

	switch (priv->phase) {
	case KCS_PHASE_WRITE_START:
		priv->phase = KCS_PHASE_WRITE_DATA;
		fallthrough;

	case KCS_PHASE_WRITE_DATA:
		if (priv->data_in_idx < KCS_MSG_BUFSIZ) {
			set_state(priv, WRITE_STATE);
			kcs_bmc_write_data(dev, KCS_ZERO_DATA);
			priv->data_in[priv->data_in_idx++] = kcs_bmc_read_data(dev);
		} else {
			kcs_bmc_ipmi_force_abort(priv);
			priv->error = KCS_LENGTH_ERROR;
		}
		break;

	case KCS_PHASE_WRITE_END_CMD:
		if (priv->data_in_idx < KCS_MSG_BUFSIZ) {
			set_state(priv, READ_STATE);
			priv->data_in[priv->data_in_idx++] = kcs_bmc_read_data(dev);
			priv->phase = KCS_PHASE_WRITE_DONE;
			priv->data_in_avail = true;
			wake_up_interruptible(&priv->queue);
		} else {
			kcs_bmc_ipmi_force_abort(priv);
			priv->error = KCS_LENGTH_ERROR;
		}
		break;

	case KCS_PHASE_READ:
		if (priv->data_out_idx == priv->data_out_len)
			set_state(priv, IDLE_STATE);

		data = kcs_bmc_read_data(dev);
		if (data != KCS_CMD_READ_BYTE) {
			set_state(priv, ERROR_STATE);
			kcs_bmc_write_data(dev, KCS_ZERO_DATA);
			break;
		}

		if (priv->data_out_idx == priv->data_out_len) {
			kcs_bmc_write_data(dev, KCS_ZERO_DATA);
			priv->phase = KCS_PHASE_IDLE;
			break;
		}

		kcs_bmc_write_data(dev, priv->data_out[priv->data_out_idx++]);
		break;

	case KCS_PHASE_ABORT_ERROR1:
		set_state(priv, READ_STATE);
		kcs_bmc_read_data(dev);
		kcs_bmc_write_data(dev, priv->error);
		priv->phase = KCS_PHASE_ABORT_ERROR2;
		break;

	case KCS_PHASE_ABORT_ERROR2:
		set_state(priv, IDLE_STATE);
		kcs_bmc_read_data(dev);
		kcs_bmc_write_data(dev, KCS_ZERO_DATA);
		priv->phase = KCS_PHASE_IDLE;
		break;

	default:
		kcs_bmc_ipmi_force_abort(priv);
		break;
	}
}

static void kcs_bmc_ipmi_handle_cmd(struct kcs_bmc_ipmi *priv)
{
	u8 cmd;

	set_state(priv, WRITE_STATE);
	kcs_bmc_write_data(priv->client.dev, KCS_ZERO_DATA);

	cmd = kcs_bmc_read_data(priv->client.dev);
	switch (cmd) {
	case KCS_CMD_WRITE_START:
		priv->phase = KCS_PHASE_WRITE_START;
		priv->error = KCS_NO_ERROR;
		priv->data_in_avail = false;
		priv->data_in_idx = 0;
		break;

	case KCS_CMD_WRITE_END:
		if (priv->phase != KCS_PHASE_WRITE_DATA) {
			kcs_bmc_ipmi_force_abort(priv);
			break;
		}

		priv->phase = KCS_PHASE_WRITE_END_CMD;
		break;

	case KCS_CMD_GET_STATUS_ABORT:
		if (priv->error == KCS_NO_ERROR)
			priv->error = KCS_ABORTED_BY_COMMAND;

		priv->phase = KCS_PHASE_ABORT_ERROR1;
		priv->data_in_avail = false;
		priv->data_in_idx = 0;
		break;

	default:
		kcs_bmc_ipmi_force_abort(priv);
		priv->error = KCS_ILLEGAL_CONTROL_CODE;
		break;
	}
}

static inline struct kcs_bmc_ipmi *client_to_kcs_bmc_ipmi(struct kcs_bmc_client *client)
{
	return container_of(client, struct kcs_bmc_ipmi, client);
}

static irqreturn_t kcs_bmc_ipmi_event(struct kcs_bmc_client *client)
{
	struct kcs_bmc_ipmi *priv;
	u8 status;
	int ret;

	priv = client_to_kcs_bmc_ipmi(client);
	if (!priv)
		return IRQ_NONE;

	spin_lock(&priv->lock);

	status = kcs_bmc_read_status(client->dev);
	if (status & KCS_STATUS_IBF) {
		if (status & KCS_STATUS_CMD_DAT)
			kcs_bmc_ipmi_handle_cmd(priv);
		else
			kcs_bmc_ipmi_handle_data(priv);

		ret = IRQ_HANDLED;
	} else {
		ret = IRQ_NONE;
	}

	spin_unlock(&priv->lock);

	return ret;
}

static const struct kcs_bmc_client_ops kcs_bmc_ipmi_client_ops = {
	.event = kcs_bmc_ipmi_event,
};

static inline struct kcs_bmc_ipmi *to_kcs_bmc(struct file *filp)
{
	return container_of(filp->private_data, struct kcs_bmc_ipmi, miscdev);
}

static int kcs_bmc_ipmi_open(struct inode *inode, struct file *filp)
{
	struct kcs_bmc_ipmi *priv = to_kcs_bmc(filp);

	return kcs_bmc_enable_device(priv->client.dev, &priv->client);
}

static __poll_t kcs_bmc_ipmi_poll(struct file *filp, poll_table *wait)
{
	struct kcs_bmc_ipmi *priv = to_kcs_bmc(filp);
	__poll_t mask = 0;

	poll_wait(filp, &priv->queue, wait);

	spin_lock_irq(&priv->lock);
	if (priv->data_in_avail)
		mask |= EPOLLIN;
	spin_unlock_irq(&priv->lock);

	return mask;
}

static ssize_t kcs_bmc_ipmi_read(struct file *filp, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct kcs_bmc_ipmi *priv = to_kcs_bmc(filp);
	bool data_avail;
	size_t data_len;
	ssize_t ret;

	if (!(filp->f_flags & O_NONBLOCK))
		wait_event_interruptible(priv->queue,
					 priv->data_in_avail);

	mutex_lock(&priv->mutex);

	spin_lock_irq(&priv->lock);
	data_avail = priv->data_in_avail;
	if (data_avail) {
		data_len = priv->data_in_idx;
		memcpy(priv->kbuffer, priv->data_in, data_len);
	}
	spin_unlock_irq(&priv->lock);

	if (!data_avail) {
		ret = -EAGAIN;
		goto out_unlock;
	}

	if (count < data_len) {
		pr_err("channel=%u with too large data : %zu\n",
			priv->client.dev->channel, data_len);

		spin_lock_irq(&priv->lock);
		kcs_bmc_ipmi_force_abort(priv);
		spin_unlock_irq(&priv->lock);

		ret = -EOVERFLOW;
		goto out_unlock;
	}

	if (copy_to_user(buf, priv->kbuffer, data_len)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	ret = data_len;

	spin_lock_irq(&priv->lock);
	if (priv->phase == KCS_PHASE_WRITE_DONE) {
		priv->phase = KCS_PHASE_WAIT_READ;
		priv->data_in_avail = false;
		priv->data_in_idx = 0;
	} else {
		ret = -EAGAIN;
	}
	spin_unlock_irq(&priv->lock);

out_unlock:
	mutex_unlock(&priv->mutex);

	return ret;
}

static ssize_t kcs_bmc_ipmi_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct kcs_bmc_ipmi *priv = to_kcs_bmc(filp);
	ssize_t ret;

	/* a minimum response size '3' : netfn + cmd + ccode */
	if (count < 3 || count > KCS_MSG_BUFSIZ)
		return -EINVAL;

	mutex_lock(&priv->mutex);

	if (copy_from_user(priv->kbuffer, buf, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	spin_lock_irq(&priv->lock);
	if (priv->phase == KCS_PHASE_WAIT_READ) {
		priv->phase = KCS_PHASE_READ;
		priv->data_out_idx = 1;
		priv->data_out_len = count;
		memcpy(priv->data_out, priv->kbuffer, count);
		kcs_bmc_write_data(priv->client.dev, priv->data_out[0]);
		ret = count;
	} else {
		ret = -EINVAL;
	}
	spin_unlock_irq(&priv->lock);

out_unlock:
	mutex_unlock(&priv->mutex);

	return ret;
}

static long kcs_bmc_ipmi_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	struct kcs_bmc_ipmi *priv = to_kcs_bmc(filp);
	long ret = 0;

	spin_lock_irq(&priv->lock);

	switch (cmd) {
	case IPMI_BMC_IOCTL_SET_SMS_ATN:
		kcs_bmc_update_status(priv->client.dev, KCS_STATUS_SMS_ATN, KCS_STATUS_SMS_ATN);
		break;

	case IPMI_BMC_IOCTL_CLEAR_SMS_ATN:
		kcs_bmc_update_status(priv->client.dev, KCS_STATUS_SMS_ATN, 0);
		break;

	case IPMI_BMC_IOCTL_FORCE_ABORT:
		kcs_bmc_ipmi_force_abort(priv);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock_irq(&priv->lock);

	return ret;
}

static int kcs_bmc_ipmi_release(struct inode *inode, struct file *filp)
{
	struct kcs_bmc_ipmi *priv = to_kcs_bmc(filp);

	kcs_bmc_ipmi_force_abort(priv);
	kcs_bmc_disable_device(priv->client.dev, &priv->client);

	return 0;
}

static const struct file_operations kcs_bmc_ipmi_fops = {
	.owner          = THIS_MODULE,
	.open           = kcs_bmc_ipmi_open,
	.read           = kcs_bmc_ipmi_read,
	.write          = kcs_bmc_ipmi_write,
	.release        = kcs_bmc_ipmi_release,
	.poll           = kcs_bmc_ipmi_poll,
	.unlocked_ioctl = kcs_bmc_ipmi_ioctl,
};

static DEFINE_SPINLOCK(kcs_bmc_ipmi_instances_lock);
static LIST_HEAD(kcs_bmc_ipmi_instances);

static int kcs_bmc_ipmi_add_device(struct kcs_bmc_device *kcs_bmc)
{
	struct kcs_bmc_ipmi *priv;
	int rc;

	priv = devm_kzalloc(kcs_bmc->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	mutex_init(&priv->mutex);

	init_waitqueue_head(&priv->queue);

	priv->client.dev = kcs_bmc;
	priv->client.ops = &kcs_bmc_ipmi_client_ops;
	priv->data_in = devm_kmalloc(kcs_bmc->dev, KCS_MSG_BUFSIZ, GFP_KERNEL);
	priv->data_out = devm_kmalloc(kcs_bmc->dev, KCS_MSG_BUFSIZ, GFP_KERNEL);
	priv->kbuffer = devm_kmalloc(kcs_bmc->dev, KCS_MSG_BUFSIZ, GFP_KERNEL);

	priv->miscdev.minor = MISC_DYNAMIC_MINOR;
	priv->miscdev.name = devm_kasprintf(kcs_bmc->dev, GFP_KERNEL, "%s%u", DEVICE_NAME,
					   kcs_bmc->channel);
	if (!priv->data_in || !priv->data_out || !priv->kbuffer || !priv->miscdev.name)
		return -EINVAL;

	priv->miscdev.fops = &kcs_bmc_ipmi_fops;

	rc = misc_register(&priv->miscdev);
	if (rc) {
		dev_err(kcs_bmc->dev, "Unable to register device: %d\n", rc);
		return rc;
	}

	spin_lock_irq(&kcs_bmc_ipmi_instances_lock);
	list_add(&priv->entry, &kcs_bmc_ipmi_instances);
	spin_unlock_irq(&kcs_bmc_ipmi_instances_lock);

	dev_info(kcs_bmc->dev, "Initialised IPMI client for channel %d", kcs_bmc->channel);

	return 0;
}

static int kcs_bmc_ipmi_remove_device(struct kcs_bmc_device *kcs_bmc)
{
	struct kcs_bmc_ipmi *priv = NULL, *pos;

	spin_lock_irq(&kcs_bmc_ipmi_instances_lock);
	list_for_each_entry(pos, &kcs_bmc_ipmi_instances, entry) {
		if (pos->client.dev == kcs_bmc) {
			priv = pos;
			list_del(&pos->entry);
			break;
		}
	}
	spin_unlock_irq(&kcs_bmc_ipmi_instances_lock);

	if (!priv)
		return -ENODEV;

	misc_deregister(&priv->miscdev);
	kcs_bmc_disable_device(priv->client.dev, &priv->client);
	devm_kfree(kcs_bmc->dev, priv->kbuffer);
	devm_kfree(kcs_bmc->dev, priv->data_out);
	devm_kfree(kcs_bmc->dev, priv->data_in);
	devm_kfree(kcs_bmc->dev, priv);

	return 0;
}

static const struct kcs_bmc_driver_ops kcs_bmc_ipmi_driver_ops = {
	.add_device = kcs_bmc_ipmi_add_device,
	.remove_device = kcs_bmc_ipmi_remove_device,
};

static struct kcs_bmc_driver kcs_bmc_ipmi_driver = {
	.ops = &kcs_bmc_ipmi_driver_ops,
};

static int kcs_bmc_ipmi_init(void)
{
	kcs_bmc_register_driver(&kcs_bmc_ipmi_driver);

	return 0;
}
module_init(kcs_bmc_ipmi_init);

static void kcs_bmc_ipmi_exit(void)
{
	kcs_bmc_unregister_driver(&kcs_bmc_ipmi_driver);
}
module_exit(kcs_bmc_ipmi_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Haiyue Wang <haiyue.wang@linux.intel.com>");
MODULE_AUTHOR("Andrew Jeffery <andrew@aj.id.au>");
MODULE_DESCRIPTION("KCS BMC to handle the IPMI request from system software");
