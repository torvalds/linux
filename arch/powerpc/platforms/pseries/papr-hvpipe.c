// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "papr-hvpipe: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/anon_inodes.h>
#include <linux/miscdevice.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/of.h>
#include <asm/machdep.h>
#include <asm/rtas.h>
#include <uapi/asm/papr-hvpipe.h>
#include "pseries.h"
#include "papr-hvpipe.h"

static DEFINE_SPINLOCK(hvpipe_src_list_lock);
static LIST_HEAD(hvpipe_src_list);

/*
 * New PowerPC FW provides support for partitions and various
 * sources (Ex: remote hardware management console (HMC)) to
 * exchange information through an inband hypervisor channel
 * called HVPIPE. Only HMCs are supported right now and
 * partitions can communicate with multiple HMCs and each
 * source represented by source ID.
 *
 * FW introduces send HVPIPE and recv HVPIPE RTAS calls for
 * partitions to send and receive payloads respectively.
 *
 * These RTAS functions have the following certain requirements
 * / limitations:
 * - One hvpipe per partition for all sources.
 * - Assume the return status of send HVPIPE as delivered to source
 * - Assume the return status of recv HVPIPE as ACK to source
 * - Generates HVPIPE event message when the payload is ready
 *   for the partition. The hypervisor will not deliver another
 *   event until the partition read the previous payload which
 *   means the pipe is blocked for any sources.
 *
 * Linux implementation:
 * Follow the similar interfaces that the OS has for other RTAS calls.
 * ex: /dev/papr-indices, /dev/papr-vpd, etc.
 * - /dev/papr-hvpipe is available for the user space.
 * - devfd = open("/dev/papr-hvpipe", ..)
 * - fd = ioctl(fd,HVPIPE_IOC_CREATE_HANDLE,&srcID)-for each source
 * - write(fd, buf, size) --> Issue send HVPIPE RTAS call and
 *   returns size for success or the corresponding error for RTAS
 *   return code for failure.
 * - poll(fd,..) -> wakeup FD if the payload is available to read.
 *   HVPIPE event message handler wakeup FD based on source ID in
 *   the event message
 * - read(fd, buf, size) --> Issue recv HVPIPE RTAS call and
 *   returns size for success or the corresponding error for RTAS
 *   return code for failure.
 */

static struct hvpipe_source_info *hvpipe_find_source(u32 srcID)
{
	struct hvpipe_source_info *src_info;

	list_for_each_entry(src_info, &hvpipe_src_list, list)
		if (src_info->srcID == srcID)
			return src_info;

	return NULL;
}

/*
 * papr_hvpipe_handle_write -  Issue send HVPIPE RTAS and return
 * the RTAS status to the user space
 */
static ssize_t papr_hvpipe_handle_write(struct file *file,
	const char __user *buf, size_t size, loff_t *off)
{
	struct hvpipe_source_info *src_info = file->private_data;

	if (!src_info)
		return -EIO;

	return 0;
}

/*
 * papr_hvpipe_handle_read - If the payload for the specific
 * source is pending in the hypervisor, issue recv HVPIPE RTAS
 * and return the payload to the user space.
 *
 * When the payload is available for the partition, the
 * hypervisor notifies HVPIPE event with the source ID
 * and the event handler wakeup FD(s) that are waiting.
 */
static ssize_t papr_hvpipe_handle_read(struct file *file,
		char __user *buf, size_t size, loff_t *off)
{

	struct hvpipe_source_info *src_info = file->private_data;

	if (!src_info)
		return -EIO;

	return 0;
}

/*
 * The user space waits for the payload to receive.
 * The hypervisor sends HVPIPE event message to the partition
 * when the payload is available. The event handler wakeup FD
 * depends on the source ID in the message event.
 */
static __poll_t papr_hvpipe_handle_poll(struct file *filp,
		struct poll_table_struct *wait)
{
	struct hvpipe_source_info *src_info = filp->private_data;

	if (!src_info)
		return POLLNVAL;

	return 0;
}

static int papr_hvpipe_handle_release(struct inode *inode,
				struct file *file)
{
	struct hvpipe_source_info *src_info;

	/*
	 * Hold the lock, remove source from src_list, reset the
	 * hvpipe status and release the lock to prevent any race
	 * with message event IRQ.
	 */
	spin_lock(&hvpipe_src_list_lock);
	src_info = file->private_data;
	list_del(&src_info->list);
	file->private_data = NULL;
	spin_unlock(&hvpipe_src_list_lock);
	kfree(src_info);
	return 0;
}

static const struct file_operations papr_hvpipe_handle_ops = {
	.read		=	papr_hvpipe_handle_read,
	.write		=	papr_hvpipe_handle_write,
	.release	=	papr_hvpipe_handle_release,
	.poll		=	papr_hvpipe_handle_poll,
};

static int papr_hvpipe_dev_create_handle(u32 srcID)
{
	struct hvpipe_source_info *src_info;
	struct file *file;
	long err;
	int fd;

	spin_lock(&hvpipe_src_list_lock);
	/*
	 * Do not allow more than one process communicates with
	 * each source.
	 */
	src_info = hvpipe_find_source(srcID);
	if (src_info) {
		spin_unlock(&hvpipe_src_list_lock);
		pr_err("pid(%d) is already using the source(%d)\n",
				src_info->tsk->pid, srcID);
		return -EALREADY;
	}
	spin_unlock(&hvpipe_src_list_lock);

	src_info = kzalloc(sizeof(*src_info), GFP_KERNEL_ACCOUNT);
	if (!src_info)
		return -ENOMEM;

	src_info->srcID = srcID;
	src_info->tsk = current;
	init_waitqueue_head(&src_info->recv_wqh);

	fd = get_unused_fd_flags(O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		err = fd;
		goto free_buf;
	}

	file = anon_inode_getfile("[papr-hvpipe]",
			&papr_hvpipe_handle_ops, (void *)src_info,
			O_RDWR);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		goto free_fd;
	}

	spin_lock(&hvpipe_src_list_lock);
	/*
	 * If two processes are executing ioctl() for the same
	 * source ID concurrently, prevent the second process to
	 * acquire FD.
	 */
	if (hvpipe_find_source(srcID)) {
		spin_unlock(&hvpipe_src_list_lock);
		err = -EALREADY;
		goto free_file;
	}
	list_add(&src_info->list, &hvpipe_src_list);
	spin_unlock(&hvpipe_src_list_lock);

	fd_install(fd, file);
	return fd;

free_file:
	fput(file);
free_fd:
	put_unused_fd(fd);
free_buf:
	kfree(src_info);
	return err;
}

/*
 * Top-level ioctl handler for /dev/papr_hvpipe
 *
 * Use separate FD for each source (exa :HMC). So ioctl is called
 * with source ID which returns FD.
 */
static long papr_hvpipe_dev_ioctl(struct file *filp, unsigned int ioctl,
		unsigned long arg)
{
	u32 __user *argp = (void __user *)arg;
	u32 srcID;
	long ret;

	if (get_user(srcID, argp))
		return -EFAULT;

	/*
	 * Support only HMC source right now
	 */
	if (!(srcID & HVPIPE_HMC_ID_MASK))
		return -EINVAL;

	switch (ioctl) {
	case PAPR_HVPIPE_IOC_CREATE_HANDLE:
		ret = papr_hvpipe_dev_create_handle(srcID);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

static const struct file_operations papr_hvpipe_ops = {
	.unlocked_ioctl	=	papr_hvpipe_dev_ioctl,
};

static struct miscdevice papr_hvpipe_dev = {
	.minor	=	MISC_DYNAMIC_MINOR,
	.name	=	"papr-hvpipe",
	.fops	=	&papr_hvpipe_ops,
};

static int __init papr_hvpipe_init(void)
{
	int ret;

	if (!of_find_property(rtas.dev, "ibm,hypervisor-pipe-capable",
		NULL))
		return -ENODEV;

	if (!rtas_function_implemented(RTAS_FN_IBM_SEND_HVPIPE_MSG) ||
		!rtas_function_implemented(RTAS_FN_IBM_RECEIVE_HVPIPE_MSG))
		return -ENODEV;

	ret = misc_register(&papr_hvpipe_dev);
	if (ret) {
		pr_err("misc-dev registration failed %d\n", ret);
		return ret;
	}

	return 0;
}
machine_device_initcall(pseries, papr_hvpipe_init);
