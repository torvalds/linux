// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  Bluetooth virtual HCI driver
 *
 *  Copyright (C) 2000-2001  Qualcomm Incorporated
 *  Copyright (C) 2002-2003  Maxim Krasnyansky <maxk@qualcomm.com>
 *  Copyright (C) 2004-2006  Marcel Holtmann <marcel@holtmann.org>
 */

#include <linux/module.h>
#include <asm/unaligned.h>

#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/poll.h>

#include <linux/skbuff.h>
#include <linux/miscdevice.h>
#include <linux/debugfs.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#define VERSION "1.5"

static bool amp;

struct vhci_data {
	struct hci_dev *hdev;

	wait_queue_head_t read_wait;
	struct sk_buff_head readq;

	struct mutex open_mutex;
	struct delayed_work open_timeout;
	struct work_struct suspend_work;

	bool suspended;
	bool wakeup;
	__u16 msft_opcode;
	bool aosp_capable;
	atomic_t initialized;
};

static int vhci_open_dev(struct hci_dev *hdev)
{
	return 0;
}

static int vhci_close_dev(struct hci_dev *hdev)
{
	struct vhci_data *data = hci_get_drvdata(hdev);

	skb_queue_purge(&data->readq);

	return 0;
}

static int vhci_flush(struct hci_dev *hdev)
{
	struct vhci_data *data = hci_get_drvdata(hdev);

	skb_queue_purge(&data->readq);

	return 0;
}

static int vhci_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct vhci_data *data = hci_get_drvdata(hdev);

	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);

	skb_queue_tail(&data->readq, skb);

	if (atomic_read(&data->initialized))
		wake_up_interruptible(&data->read_wait);
	return 0;
}

static int vhci_get_data_path_id(struct hci_dev *hdev, u8 *data_path_id)
{
	*data_path_id = 0;
	return 0;
}

static int vhci_get_codec_config_data(struct hci_dev *hdev, __u8 type,
				      struct bt_codec *codec, __u8 *vnd_len,
				      __u8 **vnd_data)
{
	if (type != ESCO_LINK)
		return -EINVAL;

	*vnd_len = 0;
	*vnd_data = NULL;
	return 0;
}

static bool vhci_wakeup(struct hci_dev *hdev)
{
	struct vhci_data *data = hci_get_drvdata(hdev);

	return data->wakeup;
}

static ssize_t force_suspend_read(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct vhci_data *data = file->private_data;
	char buf[3];

	buf[0] = data->suspended ? 'Y' : 'N';
	buf[1] = '\n';
	buf[2] = '\0';
	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static void vhci_suspend_work(struct work_struct *work)
{
	struct vhci_data *data = container_of(work, struct vhci_data,
					      suspend_work);

	if (data->suspended)
		hci_suspend_dev(data->hdev);
	else
		hci_resume_dev(data->hdev);
}

static ssize_t force_suspend_write(struct file *file,
				   const char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct vhci_data *data = file->private_data;
	bool enable;
	int err;

	err = kstrtobool_from_user(user_buf, count, &enable);
	if (err)
		return err;

	if (data->suspended == enable)
		return -EALREADY;

	data->suspended = enable;

	schedule_work(&data->suspend_work);

	return count;
}

static const struct file_operations force_suspend_fops = {
	.open		= simple_open,
	.read		= force_suspend_read,
	.write		= force_suspend_write,
	.llseek		= default_llseek,
};

static ssize_t force_wakeup_read(struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct vhci_data *data = file->private_data;
	char buf[3];

	buf[0] = data->wakeup ? 'Y' : 'N';
	buf[1] = '\n';
	buf[2] = '\0';
	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t force_wakeup_write(struct file *file,
				  const char __user *user_buf, size_t count,
				  loff_t *ppos)
{
	struct vhci_data *data = file->private_data;
	bool enable;
	int err;

	err = kstrtobool_from_user(user_buf, count, &enable);
	if (err)
		return err;

	if (data->wakeup == enable)
		return -EALREADY;

	data->wakeup = enable;

	return count;
}

static const struct file_operations force_wakeup_fops = {
	.open		= simple_open,
	.read		= force_wakeup_read,
	.write		= force_wakeup_write,
	.llseek		= default_llseek,
};

static int msft_opcode_set(void *data, u64 val)
{
	struct vhci_data *vhci = data;

	if (val > 0xffff || hci_opcode_ogf(val) != 0x3f)
		return -EINVAL;

	if (vhci->msft_opcode)
		return -EALREADY;

	vhci->msft_opcode = val;

	return 0;
}

static int msft_opcode_get(void *data, u64 *val)
{
	struct vhci_data *vhci = data;

	*val = vhci->msft_opcode;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(msft_opcode_fops, msft_opcode_get, msft_opcode_set,
			 "%llu\n");

static ssize_t aosp_capable_read(struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct vhci_data *vhci = file->private_data;
	char buf[3];

	buf[0] = vhci->aosp_capable ? 'Y' : 'N';
	buf[1] = '\n';
	buf[2] = '\0';
	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t aosp_capable_write(struct file *file,
				  const char __user *user_buf, size_t count,
				  loff_t *ppos)
{
	struct vhci_data *vhci = file->private_data;
	bool enable;
	int err;

	err = kstrtobool_from_user(user_buf, count, &enable);
	if (err)
		return err;

	if (!enable)
		return -EINVAL;

	if (vhci->aosp_capable)
		return -EALREADY;

	vhci->aosp_capable = enable;

	return count;
}

static const struct file_operations aosp_capable_fops = {
	.open		= simple_open,
	.read		= aosp_capable_read,
	.write		= aosp_capable_write,
	.llseek		= default_llseek,
};

static int vhci_setup(struct hci_dev *hdev)
{
	struct vhci_data *vhci = hci_get_drvdata(hdev);

	if (vhci->msft_opcode)
		hci_set_msft_opcode(hdev, vhci->msft_opcode);

	if (vhci->aosp_capable)
		hci_set_aosp_capable(hdev);

	return 0;
}

static void vhci_coredump(struct hci_dev *hdev)
{
	/* No need to do anything */
}

static void vhci_coredump_hdr(struct hci_dev *hdev, struct sk_buff *skb)
{
	char buf[80];

	snprintf(buf, sizeof(buf), "Controller Name: vhci_ctrl\n");
	skb_put_data(skb, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "Firmware Version: vhci_fw\n");
	skb_put_data(skb, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "Driver: vhci_drv\n");
	skb_put_data(skb, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "Vendor: vhci\n");
	skb_put_data(skb, buf, strlen(buf));
}

#define MAX_COREDUMP_LINE_LEN	40

struct devcoredump_test_data {
	enum devcoredump_state state;
	unsigned int timeout;
	char data[MAX_COREDUMP_LINE_LEN];
};

static inline void force_devcd_timeout(struct hci_dev *hdev,
				       unsigned int timeout)
{
#ifdef CONFIG_DEV_COREDUMP
	hdev->dump.timeout = msecs_to_jiffies(timeout * 1000);
#endif
}

static ssize_t force_devcd_write(struct file *file, const char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct vhci_data *data = file->private_data;
	struct hci_dev *hdev = data->hdev;
	struct sk_buff *skb = NULL;
	struct devcoredump_test_data dump_data;
	size_t data_size;
	int ret;

	if (count < offsetof(struct devcoredump_test_data, data) ||
	    count > sizeof(dump_data))
		return -EINVAL;

	if (copy_from_user(&dump_data, user_buf, count))
		return -EFAULT;

	data_size = count - offsetof(struct devcoredump_test_data, data);
	skb = alloc_skb(data_size, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;
	skb_put_data(skb, &dump_data.data, data_size);

	hci_devcd_register(hdev, vhci_coredump, vhci_coredump_hdr, NULL);

	/* Force the devcoredump timeout */
	if (dump_data.timeout)
		force_devcd_timeout(hdev, dump_data.timeout);

	ret = hci_devcd_init(hdev, skb->len);
	if (ret) {
		BT_ERR("Failed to generate devcoredump");
		kfree_skb(skb);
		return ret;
	}

	hci_devcd_append(hdev, skb);

	switch (dump_data.state) {
	case HCI_DEVCOREDUMP_DONE:
		hci_devcd_complete(hdev);
		break;
	case HCI_DEVCOREDUMP_ABORT:
		hci_devcd_abort(hdev);
		break;
	case HCI_DEVCOREDUMP_TIMEOUT:
		/* Do nothing */
		break;
	default:
		return -EINVAL;
	}

	return count;
}

static const struct file_operations force_devcoredump_fops = {
	.open		= simple_open,
	.write		= force_devcd_write,
};

static int __vhci_create_device(struct vhci_data *data, __u8 opcode)
{
	struct hci_dev *hdev;
	struct sk_buff *skb;

	if (data->hdev)
		return -EBADFD;

	/* bits 2-5 are reserved (must be zero) */
	if (opcode & 0x3c)
		return -EINVAL;

	skb = bt_skb_alloc(4, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdev = hci_alloc_dev();
	if (!hdev) {
		kfree_skb(skb);
		return -ENOMEM;
	}

	data->hdev = hdev;

	hdev->bus = HCI_VIRTUAL;
	hci_set_drvdata(hdev, data);

	hdev->open  = vhci_open_dev;
	hdev->close = vhci_close_dev;
	hdev->flush = vhci_flush;
	hdev->send  = vhci_send_frame;
	hdev->get_data_path_id = vhci_get_data_path_id;
	hdev->get_codec_config_data = vhci_get_codec_config_data;
	hdev->wakeup = vhci_wakeup;
	hdev->setup = vhci_setup;
	set_bit(HCI_QUIRK_NON_PERSISTENT_SETUP, &hdev->quirks);

	/* bit 6 is for external configuration */
	if (opcode & 0x40)
		set_bit(HCI_QUIRK_EXTERNAL_CONFIG, &hdev->quirks);

	/* bit 7 is for raw device */
	if (opcode & 0x80)
		set_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks);

	if (hci_register_dev(hdev) < 0) {
		BT_ERR("Can't register HCI device");
		hci_free_dev(hdev);
		data->hdev = NULL;
		kfree_skb(skb);
		return -EBUSY;
	}

	debugfs_create_file("force_suspend", 0644, hdev->debugfs, data,
			    &force_suspend_fops);

	debugfs_create_file("force_wakeup", 0644, hdev->debugfs, data,
			    &force_wakeup_fops);

	if (IS_ENABLED(CONFIG_BT_MSFTEXT))
		debugfs_create_file("msft_opcode", 0644, hdev->debugfs, data,
				    &msft_opcode_fops);

	if (IS_ENABLED(CONFIG_BT_AOSPEXT))
		debugfs_create_file("aosp_capable", 0644, hdev->debugfs, data,
				    &aosp_capable_fops);

	debugfs_create_file("force_devcoredump", 0644, hdev->debugfs, data,
			    &force_devcoredump_fops);

	hci_skb_pkt_type(skb) = HCI_VENDOR_PKT;

	skb_put_u8(skb, 0xff);
	skb_put_u8(skb, opcode);
	put_unaligned_le16(hdev->id, skb_put(skb, 2));
	skb_queue_head(&data->readq, skb);
	atomic_inc(&data->initialized);

	wake_up_interruptible(&data->read_wait);
	return 0;
}

static int vhci_create_device(struct vhci_data *data, __u8 opcode)
{
	int err;

	mutex_lock(&data->open_mutex);
	err = __vhci_create_device(data, opcode);
	mutex_unlock(&data->open_mutex);

	return err;
}

static inline ssize_t vhci_get_user(struct vhci_data *data,
				    struct iov_iter *from)
{
	size_t len = iov_iter_count(from);
	struct sk_buff *skb;
	__u8 pkt_type, opcode;
	int ret;

	if (len < 2 || len > HCI_MAX_FRAME_SIZE)
		return -EINVAL;

	skb = bt_skb_alloc(len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	if (!copy_from_iter_full(skb_put(skb, len), len, from)) {
		kfree_skb(skb);
		return -EFAULT;
	}

	pkt_type = *((__u8 *) skb->data);
	skb_pull(skb, 1);

	switch (pkt_type) {
	case HCI_EVENT_PKT:
	case HCI_ACLDATA_PKT:
	case HCI_SCODATA_PKT:
	case HCI_ISODATA_PKT:
		if (!data->hdev) {
			kfree_skb(skb);
			return -ENODEV;
		}

		hci_skb_pkt_type(skb) = pkt_type;

		ret = hci_recv_frame(data->hdev, skb);
		break;

	case HCI_VENDOR_PKT:
		cancel_delayed_work_sync(&data->open_timeout);

		opcode = *((__u8 *) skb->data);
		skb_pull(skb, 1);

		if (skb->len > 0) {
			kfree_skb(skb);
			return -EINVAL;
		}

		kfree_skb(skb);

		ret = vhci_create_device(data, opcode);
		break;

	default:
		kfree_skb(skb);
		return -EINVAL;
	}

	return (ret < 0) ? ret : len;
}

static inline ssize_t vhci_put_user(struct vhci_data *data,
				    struct sk_buff *skb,
				    char __user *buf, int count)
{
	char __user *ptr = buf;
	int len;

	len = min_t(unsigned int, skb->len, count);

	if (copy_to_user(ptr, skb->data, len))
		return -EFAULT;

	if (!data->hdev)
		return len;

	data->hdev->stat.byte_tx += len;

	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
		data->hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		data->hdev->stat.acl_tx++;
		break;
	case HCI_SCODATA_PKT:
		data->hdev->stat.sco_tx++;
		break;
	}

	return len;
}

static ssize_t vhci_read(struct file *file,
			 char __user *buf, size_t count, loff_t *pos)
{
	struct vhci_data *data = file->private_data;
	struct sk_buff *skb;
	ssize_t ret = 0;

	while (count) {
		skb = skb_dequeue(&data->readq);
		if (skb) {
			ret = vhci_put_user(data, skb, buf, count);
			if (ret < 0)
				skb_queue_head(&data->readq, skb);
			else
				kfree_skb(skb);
			break;
		}

		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}

		ret = wait_event_interruptible(data->read_wait,
					       !skb_queue_empty(&data->readq));
		if (ret < 0)
			break;
	}

	return ret;
}

static ssize_t vhci_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct vhci_data *data = file->private_data;

	return vhci_get_user(data, from);
}

static __poll_t vhci_poll(struct file *file, poll_table *wait)
{
	struct vhci_data *data = file->private_data;

	poll_wait(file, &data->read_wait, wait);

	if (!skb_queue_empty(&data->readq))
		return EPOLLIN | EPOLLRDNORM;

	return EPOLLOUT | EPOLLWRNORM;
}

static void vhci_open_timeout(struct work_struct *work)
{
	struct vhci_data *data = container_of(work, struct vhci_data,
					      open_timeout.work);

	vhci_create_device(data, 0x00);
}

static int vhci_open(struct inode *inode, struct file *file)
{
	struct vhci_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	skb_queue_head_init(&data->readq);
	init_waitqueue_head(&data->read_wait);

	mutex_init(&data->open_mutex);
	INIT_DELAYED_WORK(&data->open_timeout, vhci_open_timeout);
	INIT_WORK(&data->suspend_work, vhci_suspend_work);

	file->private_data = data;
	nonseekable_open(inode, file);

	schedule_delayed_work(&data->open_timeout, msecs_to_jiffies(1000));

	return 0;
}

static int vhci_release(struct inode *inode, struct file *file)
{
	struct vhci_data *data = file->private_data;
	struct hci_dev *hdev;

	cancel_delayed_work_sync(&data->open_timeout);
	flush_work(&data->suspend_work);

	hdev = data->hdev;

	if (hdev) {
		hci_unregister_dev(hdev);
		hci_free_dev(hdev);
	}

	skb_queue_purge(&data->readq);
	file->private_data = NULL;
	kfree(data);

	return 0;
}

static const struct file_operations vhci_fops = {
	.owner		= THIS_MODULE,
	.read		= vhci_read,
	.write_iter	= vhci_write,
	.poll		= vhci_poll,
	.open		= vhci_open,
	.release	= vhci_release,
	.llseek		= no_llseek,
};

static struct miscdevice vhci_miscdev = {
	.name	= "vhci",
	.fops	= &vhci_fops,
	.minor	= VHCI_MINOR,
};
module_misc_device(vhci_miscdev);

module_param(amp, bool, 0644);
MODULE_PARM_DESC(amp, "Create AMP controller device");

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth virtual HCI driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("devname:vhci");
MODULE_ALIAS_MISCDEV(VHCI_MINOR);
