// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015-2018, Intel Corporation.
 */

#define pr_fmt(fmt) "kcs-bmc: " fmt

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ipmi_bmc.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "kcs_bmc.h"

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

static inline u8 read_data(struct kcs_bmc *kcs_bmc)
{
	return kcs_bmc->io_inputb(kcs_bmc, kcs_bmc->ioreg.idr);
}

static inline void write_data(struct kcs_bmc *kcs_bmc, u8 data)
{
	kcs_bmc->io_outputb(kcs_bmc, kcs_bmc->ioreg.odr, data);
}

static inline u8 read_status(struct kcs_bmc *kcs_bmc)
{
	return kcs_bmc->io_inputb(kcs_bmc, kcs_bmc->ioreg.str);
}

static inline void write_status(struct kcs_bmc *kcs_bmc, u8 data)
{
	kcs_bmc->io_outputb(kcs_bmc, kcs_bmc->ioreg.str, data);
}

static void update_status_bits(struct kcs_bmc *kcs_bmc, u8 mask, u8 val)
{
	u8 tmp = read_status(kcs_bmc);

	tmp &= ~mask;
	tmp |= val & mask;

	write_status(kcs_bmc, tmp);
}

static inline void set_state(struct kcs_bmc *kcs_bmc, u8 state)
{
	update_status_bits(kcs_bmc, KCS_STATUS_STATE_MASK,
					KCS_STATUS_STATE(state));
}

static void kcs_force_abort(struct kcs_bmc *kcs_bmc)
{
	set_state(kcs_bmc, ERROR_STATE);
	read_data(kcs_bmc);
	write_data(kcs_bmc, KCS_ZERO_DATA);

	kcs_bmc->phase = KCS_PHASE_ERROR;
	kcs_bmc->data_in_avail = false;
	kcs_bmc->data_in_idx = 0;
}

static void kcs_bmc_handle_data(struct kcs_bmc *kcs_bmc)
{
	u8 data;

	switch (kcs_bmc->phase) {
	case KCS_PHASE_WRITE_START:
		kcs_bmc->phase = KCS_PHASE_WRITE_DATA;
		/* fall through */

	case KCS_PHASE_WRITE_DATA:
		if (kcs_bmc->data_in_idx < KCS_MSG_BUFSIZ) {
			set_state(kcs_bmc, WRITE_STATE);
			write_data(kcs_bmc, KCS_ZERO_DATA);
			kcs_bmc->data_in[kcs_bmc->data_in_idx++] =
						read_data(kcs_bmc);
		} else {
			kcs_force_abort(kcs_bmc);
			kcs_bmc->error = KCS_LENGTH_ERROR;
		}
		break;

	case KCS_PHASE_WRITE_END_CMD:
		if (kcs_bmc->data_in_idx < KCS_MSG_BUFSIZ) {
			set_state(kcs_bmc, READ_STATE);
			kcs_bmc->data_in[kcs_bmc->data_in_idx++] =
						read_data(kcs_bmc);
			kcs_bmc->phase = KCS_PHASE_WRITE_DONE;
			kcs_bmc->data_in_avail = true;
			wake_up_interruptible(&kcs_bmc->queue);
		} else {
			kcs_force_abort(kcs_bmc);
			kcs_bmc->error = KCS_LENGTH_ERROR;
		}
		break;

	case KCS_PHASE_READ:
		if (kcs_bmc->data_out_idx == kcs_bmc->data_out_len)
			set_state(kcs_bmc, IDLE_STATE);

		data = read_data(kcs_bmc);
		if (data != KCS_CMD_READ_BYTE) {
			set_state(kcs_bmc, ERROR_STATE);
			write_data(kcs_bmc, KCS_ZERO_DATA);
			break;
		}

		if (kcs_bmc->data_out_idx == kcs_bmc->data_out_len) {
			write_data(kcs_bmc, KCS_ZERO_DATA);
			kcs_bmc->phase = KCS_PHASE_IDLE;
			break;
		}

		write_data(kcs_bmc,
			kcs_bmc->data_out[kcs_bmc->data_out_idx++]);
		break;

	case KCS_PHASE_ABORT_ERROR1:
		set_state(kcs_bmc, READ_STATE);
		read_data(kcs_bmc);
		write_data(kcs_bmc, kcs_bmc->error);
		kcs_bmc->phase = KCS_PHASE_ABORT_ERROR2;
		break;

	case KCS_PHASE_ABORT_ERROR2:
		set_state(kcs_bmc, IDLE_STATE);
		read_data(kcs_bmc);
		write_data(kcs_bmc, KCS_ZERO_DATA);
		kcs_bmc->phase = KCS_PHASE_IDLE;
		break;

	default:
		kcs_force_abort(kcs_bmc);
		break;
	}
}

static void kcs_bmc_handle_cmd(struct kcs_bmc *kcs_bmc)
{
	u8 cmd;

	set_state(kcs_bmc, WRITE_STATE);
	write_data(kcs_bmc, KCS_ZERO_DATA);

	cmd = read_data(kcs_bmc);
	switch (cmd) {
	case KCS_CMD_WRITE_START:
		kcs_bmc->phase = KCS_PHASE_WRITE_START;
		kcs_bmc->error = KCS_NO_ERROR;
		kcs_bmc->data_in_avail = false;
		kcs_bmc->data_in_idx = 0;
		break;

	case KCS_CMD_WRITE_END:
		if (kcs_bmc->phase != KCS_PHASE_WRITE_DATA) {
			kcs_force_abort(kcs_bmc);
			break;
		}

		kcs_bmc->phase = KCS_PHASE_WRITE_END_CMD;
		break;

	case KCS_CMD_GET_STATUS_ABORT:
		if (kcs_bmc->error == KCS_NO_ERROR)
			kcs_bmc->error = KCS_ABORTED_BY_COMMAND;

		kcs_bmc->phase = KCS_PHASE_ABORT_ERROR1;
		kcs_bmc->data_in_avail = false;
		kcs_bmc->data_in_idx = 0;
		break;

	default:
		kcs_force_abort(kcs_bmc);
		kcs_bmc->error = KCS_ILLEGAL_CONTROL_CODE;
		break;
	}
}

int kcs_bmc_handle_event(struct kcs_bmc *kcs_bmc)
{
	unsigned long flags;
	int ret = -ENODATA;
	u8 status;

	spin_lock_irqsave(&kcs_bmc->lock, flags);

	status = read_status(kcs_bmc);
	if (status & KCS_STATUS_IBF) {
		if (!kcs_bmc->running)
			kcs_force_abort(kcs_bmc);
		else if (status & KCS_STATUS_CMD_DAT)
			kcs_bmc_handle_cmd(kcs_bmc);
		else
			kcs_bmc_handle_data(kcs_bmc);

		ret = 0;
	}

	spin_unlock_irqrestore(&kcs_bmc->lock, flags);

	return ret;
}
EXPORT_SYMBOL(kcs_bmc_handle_event);

static inline struct kcs_bmc *to_kcs_bmc(struct file *filp)
{
	return container_of(filp->private_data, struct kcs_bmc, miscdev);
}

static int kcs_bmc_open(struct inode *inode, struct file *filp)
{
	struct kcs_bmc *kcs_bmc = to_kcs_bmc(filp);
	int ret = 0;

	spin_lock_irq(&kcs_bmc->lock);
	if (!kcs_bmc->running)
		kcs_bmc->running = 1;
	else
		ret = -EBUSY;
	spin_unlock_irq(&kcs_bmc->lock);

	return ret;
}

static __poll_t kcs_bmc_poll(struct file *filp, poll_table *wait)
{
	struct kcs_bmc *kcs_bmc = to_kcs_bmc(filp);
	__poll_t mask = 0;

	poll_wait(filp, &kcs_bmc->queue, wait);

	spin_lock_irq(&kcs_bmc->lock);
	if (kcs_bmc->data_in_avail)
		mask |= EPOLLIN;
	spin_unlock_irq(&kcs_bmc->lock);

	return mask;
}

static ssize_t kcs_bmc_read(struct file *filp, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct kcs_bmc *kcs_bmc = to_kcs_bmc(filp);
	bool data_avail;
	size_t data_len;
	ssize_t ret;

	if (!(filp->f_flags & O_NONBLOCK))
		wait_event_interruptible(kcs_bmc->queue,
					 kcs_bmc->data_in_avail);

	mutex_lock(&kcs_bmc->mutex);

	spin_lock_irq(&kcs_bmc->lock);
	data_avail = kcs_bmc->data_in_avail;
	if (data_avail) {
		data_len = kcs_bmc->data_in_idx;
		memcpy(kcs_bmc->kbuffer, kcs_bmc->data_in, data_len);
	}
	spin_unlock_irq(&kcs_bmc->lock);

	if (!data_avail) {
		ret = -EAGAIN;
		goto out_unlock;
	}

	if (count < data_len) {
		pr_err("channel=%u with too large data : %zu\n",
			kcs_bmc->channel, data_len);

		spin_lock_irq(&kcs_bmc->lock);
		kcs_force_abort(kcs_bmc);
		spin_unlock_irq(&kcs_bmc->lock);

		ret = -EOVERFLOW;
		goto out_unlock;
	}

	if (copy_to_user(buf, kcs_bmc->kbuffer, data_len)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	ret = data_len;

	spin_lock_irq(&kcs_bmc->lock);
	if (kcs_bmc->phase == KCS_PHASE_WRITE_DONE) {
		kcs_bmc->phase = KCS_PHASE_WAIT_READ;
		kcs_bmc->data_in_avail = false;
		kcs_bmc->data_in_idx = 0;
	} else {
		ret = -EAGAIN;
	}
	spin_unlock_irq(&kcs_bmc->lock);

out_unlock:
	mutex_unlock(&kcs_bmc->mutex);

	return ret;
}

static ssize_t kcs_bmc_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct kcs_bmc *kcs_bmc = to_kcs_bmc(filp);
	ssize_t ret;

	/* a minimum response size '3' : netfn + cmd + ccode */
	if (count < 3 || count > KCS_MSG_BUFSIZ)
		return -EINVAL;

	mutex_lock(&kcs_bmc->mutex);

	if (copy_from_user(kcs_bmc->kbuffer, buf, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	spin_lock_irq(&kcs_bmc->lock);
	if (kcs_bmc->phase == KCS_PHASE_WAIT_READ) {
		kcs_bmc->phase = KCS_PHASE_READ;
		kcs_bmc->data_out_idx = 1;
		kcs_bmc->data_out_len = count;
		memcpy(kcs_bmc->data_out, kcs_bmc->kbuffer, count);
		write_data(kcs_bmc, kcs_bmc->data_out[0]);
		ret = count;
	} else {
		ret = -EINVAL;
	}
	spin_unlock_irq(&kcs_bmc->lock);

out_unlock:
	mutex_unlock(&kcs_bmc->mutex);

	return ret;
}

static long kcs_bmc_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	struct kcs_bmc *kcs_bmc = to_kcs_bmc(filp);
	long ret = 0;

	spin_lock_irq(&kcs_bmc->lock);

	switch (cmd) {
	case IPMI_BMC_IOCTL_SET_SMS_ATN:
		update_status_bits(kcs_bmc, KCS_STATUS_SMS_ATN,
				   KCS_STATUS_SMS_ATN);
		break;

	case IPMI_BMC_IOCTL_CLEAR_SMS_ATN:
		update_status_bits(kcs_bmc, KCS_STATUS_SMS_ATN,
				   0);
		break;

	case IPMI_BMC_IOCTL_FORCE_ABORT:
		kcs_force_abort(kcs_bmc);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock_irq(&kcs_bmc->lock);

	return ret;
}

static int kcs_bmc_release(struct inode *inode, struct file *filp)
{
	struct kcs_bmc *kcs_bmc = to_kcs_bmc(filp);

	spin_lock_irq(&kcs_bmc->lock);
	kcs_bmc->running = 0;
	kcs_force_abort(kcs_bmc);
	spin_unlock_irq(&kcs_bmc->lock);

	return 0;
}

static const struct file_operations kcs_bmc_fops = {
	.owner          = THIS_MODULE,
	.open           = kcs_bmc_open,
	.read           = kcs_bmc_read,
	.write          = kcs_bmc_write,
	.release        = kcs_bmc_release,
	.poll           = kcs_bmc_poll,
	.unlocked_ioctl = kcs_bmc_ioctl,
};

struct kcs_bmc *kcs_bmc_alloc(struct device *dev, int sizeof_priv, u32 channel)
{
	struct kcs_bmc *kcs_bmc;

	kcs_bmc = devm_kzalloc(dev, sizeof(*kcs_bmc) + sizeof_priv, GFP_KERNEL);
	if (!kcs_bmc)
		return NULL;

	spin_lock_init(&kcs_bmc->lock);
	kcs_bmc->channel = channel;

	mutex_init(&kcs_bmc->mutex);
	init_waitqueue_head(&kcs_bmc->queue);

	kcs_bmc->data_in = devm_kmalloc(dev, KCS_MSG_BUFSIZ, GFP_KERNEL);
	kcs_bmc->data_out = devm_kmalloc(dev, KCS_MSG_BUFSIZ, GFP_KERNEL);
	kcs_bmc->kbuffer = devm_kmalloc(dev, KCS_MSG_BUFSIZ, GFP_KERNEL);
	if (!kcs_bmc->data_in || !kcs_bmc->data_out || !kcs_bmc->kbuffer)
		return NULL;

	kcs_bmc->miscdev.minor = MISC_DYNAMIC_MINOR;
	kcs_bmc->miscdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s%u",
					       DEVICE_NAME, channel);
	kcs_bmc->miscdev.fops = &kcs_bmc_fops;

	return kcs_bmc;
}
EXPORT_SYMBOL(kcs_bmc_alloc);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Haiyue Wang <haiyue.wang@linux.intel.com>");
MODULE_DESCRIPTION("KCS BMC to handle the IPMI request from system software");
