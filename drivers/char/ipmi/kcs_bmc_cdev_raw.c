// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2021 IBM Corp. */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>

#include "kcs_bmc_client.h"

#define DEVICE_NAME "raw-kcs"

struct kcs_bmc_raw {
	struct list_head entry;

	struct kcs_bmc_client client;

	wait_queue_head_t queue;
	u8 events;
	bool writable;
	bool readable;
	u8 idr;

	struct miscdevice miscdev;
};

static inline struct kcs_bmc_raw *client_to_kcs_bmc_raw(struct kcs_bmc_client *client)
{
	return container_of(client, struct kcs_bmc_raw, client);
}

/* Call under priv->queue.lock */
static void kcs_bmc_raw_update_event_mask(struct kcs_bmc_raw *priv, u8 mask, u8 state)
{
	kcs_bmc_update_event_mask(priv->client.dev, mask, state);
	priv->events &= ~mask;
	priv->events |= state & mask;
}

static irqreturn_t kcs_bmc_raw_event(struct kcs_bmc_client *client)
{
	struct kcs_bmc_raw *priv;
	struct device *dev;
	u8 status, handled;

	priv = client_to_kcs_bmc_raw(client);
	dev = priv->miscdev.this_device;

	spin_lock(&priv->queue.lock);

	status = kcs_bmc_read_status(client->dev);
	handled = 0;

	if ((priv->events & KCS_BMC_EVENT_TYPE_IBF) && (status & KCS_BMC_STR_IBF)) {
		if (priv->readable)
			dev_err(dev, "Unexpected IBF IRQ, dropping data");

		dev_dbg(dev, "Disabling IDR events for back-pressure\n");
		kcs_bmc_raw_update_event_mask(priv, KCS_BMC_EVENT_TYPE_IBF, 0);
		priv->idr = kcs_bmc_read_data(client->dev);
		priv->readable = true;

		dev_dbg(dev, "IDR read, waking waiters\n");
		wake_up_locked(&priv->queue);

		handled |= KCS_BMC_EVENT_TYPE_IBF;
	}

	if ((priv->events & KCS_BMC_EVENT_TYPE_OBE) && !(status & KCS_BMC_STR_OBF)) {
		kcs_bmc_raw_update_event_mask(priv, KCS_BMC_EVENT_TYPE_OBE, 0);
		priv->writable = true;

		dev_dbg(dev, "ODR writable, waking waiters\n");
		wake_up_locked(&priv->queue);

		handled |= KCS_BMC_EVENT_TYPE_OBE;
	}

	spin_unlock(&priv->queue.lock);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static const struct kcs_bmc_client_ops kcs_bmc_raw_client_ops = {
	.event = kcs_bmc_raw_event,
};

static inline struct kcs_bmc_raw *file_to_kcs_bmc_raw(struct file *filp)
{
	return container_of(filp->private_data, struct kcs_bmc_raw, miscdev);
}

static int kcs_bmc_raw_open(struct inode *inode, struct file *filp)
{
	struct kcs_bmc_raw *priv = file_to_kcs_bmc_raw(filp);
	int rc;

	priv->events = KCS_BMC_EVENT_TYPE_IBF;
	rc = kcs_bmc_enable_device(priv->client.dev, &priv->client);
	if (rc)
		priv->events = 0;

	return rc;
}

static bool kcs_bmc_raw_prepare_obe(struct kcs_bmc_raw *priv)
{
	bool writable;

	/* Enable the OBE event so we can catch the host clearing OBF */
	kcs_bmc_raw_update_event_mask(priv, KCS_BMC_EVENT_TYPE_OBE, KCS_BMC_EVENT_TYPE_OBE);

	/* Now that we'll catch an OBE event, check if it's already occurred */
	writable = !(kcs_bmc_read_status(priv->client.dev) & KCS_BMC_STR_OBF);

	/* If OBF is clear we've missed the OBE event, so disable it */
	if (writable)
		kcs_bmc_raw_update_event_mask(priv, KCS_BMC_EVENT_TYPE_OBE, 0);

	return writable;
}

static __poll_t kcs_bmc_raw_poll(struct file *filp, poll_table *wait)
{
	struct kcs_bmc_raw *priv;
	__poll_t events = 0;

	priv = file_to_kcs_bmc_raw(filp);

	poll_wait(filp, &priv->queue, wait);

	spin_lock_irq(&priv->queue.lock);
	if (kcs_bmc_raw_prepare_obe(priv))
		events |= (EPOLLOUT | EPOLLWRNORM);

	if (priv->readable || (kcs_bmc_read_status(priv->client.dev) & KCS_BMC_STR_IBF))
		events |= (EPOLLIN | EPOLLRDNORM);
	spin_unlock_irq(&priv->queue.lock);

	return events;
}

static ssize_t kcs_bmc_raw_read(struct file *filp, char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct kcs_bmc_device *kcs_bmc;
	struct kcs_bmc_raw *priv;
	bool read_idr, read_str;
	struct device *dev;
	u8 idr, str;
	ssize_t rc;

	priv = file_to_kcs_bmc_raw(filp);
	kcs_bmc = priv->client.dev;
	dev = priv->miscdev.this_device;

	if (!count)
		return 0;

	if (count > 2 || *ppos > 1)
		return -EINVAL;

	if (*ppos + count > 2)
		return -EINVAL;

	read_idr = (*ppos == 0);
	read_str = (*ppos == 1) || (count == 2);

	spin_lock_irq(&priv->queue.lock);
	if (read_idr) {
		dev_dbg(dev, "Waiting for IBF\n");
		str = kcs_bmc_read_status(kcs_bmc);
		if ((filp->f_flags & O_NONBLOCK) && (str & KCS_BMC_STR_IBF)) {
			rc = -EWOULDBLOCK;
			goto out;
		}

		rc = wait_event_interruptible_locked(priv->queue,
						     priv->readable || (str & KCS_BMC_STR_IBF));
		if (rc < 0)
			goto out;

		if (signal_pending(current)) {
			dev_dbg(dev, "Interrupted waiting for IBF\n");
			rc = -EINTR;
			goto out;
		}

		/*
		 * Re-enable events prior to possible read of IDR (which clears
		 * IBF) to ensure we receive interrupts for subsequent writes
		 * to IDR. Writes to IDR by the host should not occur while IBF
		 * is set.
		 */
		dev_dbg(dev, "Woken by IBF, enabling IRQ\n");
		kcs_bmc_raw_update_event_mask(priv, KCS_BMC_EVENT_TYPE_IBF,
					      KCS_BMC_EVENT_TYPE_IBF);

		/* Read data out of IDR into internal storage if necessary */
		if (!priv->readable) {
			WARN(!(str & KCS_BMC_STR_IBF), "Unknown reason for wakeup!");

			priv->idr = kcs_bmc_read_data(kcs_bmc);
		}

		/* Copy data from internal storage to userspace */
		idr = priv->idr;

		/* We're done consuming the internally stored value */
		priv->readable = false;
	}

	if (read_str) {
		str = kcs_bmc_read_status(kcs_bmc);
		if (*ppos == 0 || priv->readable)
			/*
			 * If we got this far with `*ppos == 0` then we've read
			 * data out of IDR, so set IBF when reporting back to
			 * userspace so userspace knows the IDR value is valid.
			 */
			str |= KCS_BMC_STR_IBF;

		dev_dbg(dev, "Read status 0x%x\n", str);

	}

	rc = count;
out:
	spin_unlock_irq(&priv->queue.lock);

	if (rc < 0)
		return rc;

	/* Now copy the data in to the userspace buffer */

	if (read_idr)
		if (copy_to_user(buf++, &idr, sizeof(idr)))
			return -EFAULT;

	if (read_str)
		if (copy_to_user(buf, &str, sizeof(str)))
			return -EFAULT;

	return count;
}

static ssize_t kcs_bmc_raw_write(struct file *filp, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct kcs_bmc_device *kcs_bmc;
	bool write_odr, write_str;
	struct kcs_bmc_raw *priv;
	struct device *dev;
	ssize_t result;
	u8 data[2];
	u8 str;

	priv = file_to_kcs_bmc_raw(filp);
	kcs_bmc = priv->client.dev;
	dev = priv->miscdev.this_device;

	if (!count)
		return count;

	if (count > 2)
		return -EINVAL;

	if (*ppos >= 2)
		return -EINVAL;

	if (*ppos + count > 2)
		return -EINVAL;

	if (copy_from_user(data, buf, count))
		return -EFAULT;

	write_odr = (*ppos == 0);
	write_str = (*ppos == 1) || (count == 2);

	spin_lock_irq(&priv->queue.lock);

	/* Always write status before data, we generate the SerIRQ by writing ODR */
	if (write_str) {
		/* The index of STR in the userspace buffer depends on whether ODR is written */
		str = data[*ppos == 0];
		if (!(str & KCS_BMC_STR_OBF))
			dev_warn(dev, "Clearing OBF with status write: 0x%x\n", str);
		dev_dbg(dev, "Writing status 0x%x\n", str);
		kcs_bmc_write_status(kcs_bmc, str);
	}

	if (write_odr) {
		/* If we're writing ODR it's always the first byte in the buffer */
		u8 odr = data[0];

		str = kcs_bmc_read_status(kcs_bmc);
		if (str & KCS_BMC_STR_OBF) {
			if (filp->f_flags & O_NONBLOCK) {
				result = -EWOULDBLOCK;
				goto out;
			}

			priv->writable = kcs_bmc_raw_prepare_obe(priv);

			/* Now either OBF is already clear, or we'll get an OBE event to wake us */
			dev_dbg(dev, "Waiting for OBF to clear\n");
			wait_event_interruptible_locked(priv->queue, priv->writable);

			if (signal_pending(current)) {
				kcs_bmc_raw_update_event_mask(priv, KCS_BMC_EVENT_TYPE_OBE, 0);
				result = -EINTR;
				goto out;
			}

			WARN_ON(kcs_bmc_read_status(kcs_bmc) & KCS_BMC_STR_OBF);
		}

		dev_dbg(dev, "Writing 0x%x to ODR\n", odr);
		kcs_bmc_write_data(kcs_bmc, odr);
	}

	result = count;
out:
	spin_unlock_irq(&priv->queue.lock);

	return result;
}

static int kcs_bmc_raw_release(struct inode *inode, struct file *filp)
{
	struct kcs_bmc_raw *priv = file_to_kcs_bmc_raw(filp);

	kcs_bmc_disable_device(priv->client.dev, &priv->client);
	priv->events = 0;

	return 0;
}

static const struct file_operations kcs_bmc_raw_fops = {
	.owner          = THIS_MODULE,
	.open		= kcs_bmc_raw_open,
	.llseek		= no_seek_end_llseek,
	.read           = kcs_bmc_raw_read,
	.write          = kcs_bmc_raw_write,
	.poll		= kcs_bmc_raw_poll,
	.release	= kcs_bmc_raw_release,
};

static DEFINE_SPINLOCK(kcs_bmc_raw_instances_lock);
static LIST_HEAD(kcs_bmc_raw_instances);

static int kcs_bmc_raw_add_device(struct kcs_bmc_device *kcs_bmc)
{
	struct kcs_bmc_raw *priv;
	int rc;

	priv = devm_kzalloc(kcs_bmc->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client.dev = kcs_bmc;
	priv->client.ops = &kcs_bmc_raw_client_ops;

	init_waitqueue_head(&priv->queue);
	priv->writable = false;
	priv->readable = false;

	priv->miscdev.minor = MISC_DYNAMIC_MINOR;
	priv->miscdev.name = devm_kasprintf(kcs_bmc->dev, GFP_KERNEL, "%s%u", DEVICE_NAME,
					   kcs_bmc->channel);
	if (!priv->miscdev.name)
		return -EINVAL;

	priv->miscdev.fops = &kcs_bmc_raw_fops;

	/* Disable interrupts until userspace opens the the chardev */
	kcs_bmc_raw_update_event_mask(priv, (KCS_BMC_EVENT_TYPE_IBF | KCS_BMC_EVENT_TYPE_OBE), 0);

	rc = misc_register(&priv->miscdev);
	if (rc) {
		dev_err(kcs_bmc->dev, "Unable to register device\n");
		return rc;
	}

	spin_lock_irq(&kcs_bmc_raw_instances_lock);
	list_add(&priv->entry, &kcs_bmc_raw_instances);
	spin_unlock_irq(&kcs_bmc_raw_instances_lock);

	dev_info(kcs_bmc->dev, "Initialised raw client for channel %d", kcs_bmc->channel);

	return 0;
}

static int kcs_bmc_raw_remove_device(struct kcs_bmc_device *kcs_bmc)
{
	struct kcs_bmc_raw *priv = NULL, *pos;

	spin_lock_irq(&kcs_bmc_raw_instances_lock);
	list_for_each_entry(pos, &kcs_bmc_raw_instances, entry) {
		if (pos->client.dev == kcs_bmc) {
			priv = pos;
			list_del(&pos->entry);
			break;
		}
	}
	spin_unlock_irq(&kcs_bmc_raw_instances_lock);

	if (!priv)
		return -ENODEV;

	misc_deregister(&priv->miscdev);
	kcs_bmc_disable_device(kcs_bmc, &priv->client);
	devm_kfree(priv->client.dev->dev, priv);

	return 0;
}

static const struct kcs_bmc_driver_ops kcs_bmc_raw_driver_ops = {
	.add_device = kcs_bmc_raw_add_device,
	.remove_device = kcs_bmc_raw_remove_device,
};

static struct kcs_bmc_driver kcs_bmc_raw_driver = {
	.ops = &kcs_bmc_raw_driver_ops,
};

static int kcs_bmc_raw_init(void)
{
	kcs_bmc_register_driver(&kcs_bmc_raw_driver);

	return 0;
}
module_init(kcs_bmc_raw_init);

static void kcs_bmc_raw_exit(void)
{
	kcs_bmc_unregister_driver(&kcs_bmc_raw_driver);
}
module_exit(kcs_bmc_raw_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Andrew Jeffery <andrew@aj.id.au>");
MODULE_DESCRIPTION("Character device for raw access to a KCS device");
