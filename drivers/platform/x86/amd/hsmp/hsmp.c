// SPDX-License-Identifier: GPL-2.0
/*
 * AMD HSMP Platform Driver
 * Copyright (c) 2022, AMD.
 * All Rights Reserved.
 *
 * This file provides a device implementation for HSMP interface
 */

#include <asm/amd/hsmp.h>

#include <linux/acpi.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/nospec.h>
#include <linux/rwsem.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>

#include "hsmp.h"

/* HSMP Status / Error codes */
#define HSMP_STATUS_NOT_READY	0x00
#define HSMP_STATUS_OK		0x01
#define HSMP_ERR_INVALID_MSG	0xFE
#define HSMP_ERR_INVALID_INPUT	0xFF
#define HSMP_ERR_PREREQ_NOT_SATISFIED	0xFD
#define HSMP_ERR_SMU_BUSY		0xFC

/* Timeout in millsec */
#define HSMP_MSG_TIMEOUT	100
#define HSMP_SHORT_SLEEP	1

#define HSMP_WR			true
#define HSMP_RD			false

/*
 * When same message numbers are used for both GET and SET operation,
 * bit:31 indicates whether its SET or GET operation.
 */
#define CHECK_GET_BIT		BIT(31)

static struct hsmp_plat_device hsmp_pdev;

/*
 * Gates the AMD HSMP data plane against socket bring-up and teardown.
 *
 * hsmp_send_message() takes it for read, so open /dev/hsmp fds and hwmon reads
 * run concurrently. Probe and remove take it for write: probe brings sockets
 * up (running the mailbox handshake via hsmp_send_message_locked()) and remove
 * tears them down, both excluding and draining the data plane.
 */
DECLARE_RWSEM(hsmp_sock_rwsem);
EXPORT_SYMBOL_NS_GPL(hsmp_sock_rwsem, "AMD_HSMP");

/*
 * Send a message to the HSMP port via PCI-e config space registers
 * or by writing to MMIO space.
 *
 * The caller is expected to zero out any unused arguments.
 * If a response is expected, the number of response words should be greater than 0.
 *
 * Returns 0 for success and populates the requested number of arguments.
 * Returns a negative error code for failure.
 */
static int __hsmp_send_message(struct hsmp_socket *sock, struct hsmp_message *msg)
{
	struct hsmp_mbaddr_info *mbinfo;
	unsigned long timeout, short_sleep;
	u32 mbox_status;
	u32 index;
	int ret;

	mbinfo = &sock->mbinfo;

	/* Clear the status register */
	mbox_status = HSMP_STATUS_NOT_READY;
	ret = sock->amd_hsmp_rdwr(sock, mbinfo->msg_resp_off, &mbox_status, HSMP_WR);
	if (ret) {
		dev_err(sock->dev, "Error %d clearing mailbox status register\n", ret);
		return ret;
	}

	index = 0;
	/* Write any message arguments */
	while (index < msg->num_args) {
		ret = sock->amd_hsmp_rdwr(sock, mbinfo->msg_arg_off + (index << 2),
					  &msg->args[index], HSMP_WR);
		if (ret) {
			dev_err(sock->dev, "Error %d writing message argument %d\n", ret, index);
			return ret;
		}
		index++;
	}

	/* Write the message ID which starts the operation */
	ret = sock->amd_hsmp_rdwr(sock, mbinfo->msg_id_off, &msg->msg_id, HSMP_WR);
	if (ret) {
		dev_err(sock->dev, "Error %d writing message ID %u\n", ret, msg->msg_id);
		return ret;
	}

	/*
	 * Depending on when the trigger write completes relative to the SMU
	 * firmware 1 ms cycle, the operation may take from tens of us to 1 ms
	 * to complete. Some operations may take more. Therefore we will try
	 * a few short duration sleeps and switch to long sleeps if we don't
	 * succeed quickly.
	 */
	short_sleep = jiffies + msecs_to_jiffies(HSMP_SHORT_SLEEP);
	timeout	= jiffies + msecs_to_jiffies(HSMP_MSG_TIMEOUT);

	while (true) {
		ret = sock->amd_hsmp_rdwr(sock, mbinfo->msg_resp_off, &mbox_status, HSMP_RD);
		if (ret) {
			dev_err(sock->dev, "Error %d reading mailbox status\n", ret);
			return ret;
		}

		if (mbox_status != HSMP_STATUS_NOT_READY)
			break;

		if (!time_before(jiffies, timeout))
			break;

		if (time_before(jiffies, short_sleep))
			usleep_range(50, 100);
		else
			usleep_range(1000, 2000);
	}

	if (unlikely(mbox_status == HSMP_STATUS_NOT_READY)) {
		dev_err(sock->dev, "Message ID 0x%X failure : SMU timeout (status = 0x%X)\n",
			msg->msg_id, mbox_status);
		return -ETIMEDOUT;
	} else if (unlikely(mbox_status == HSMP_ERR_INVALID_MSG)) {
		dev_err(sock->dev, "Message ID 0x%X failure : Invalid message (status = 0x%X)\n",
			msg->msg_id, mbox_status);
		return -ENOMSG;
	} else if (unlikely(mbox_status == HSMP_ERR_INVALID_INPUT)) {
		dev_err(sock->dev, "Message ID 0x%X failure : Invalid arguments (status = 0x%X)\n",
			msg->msg_id, mbox_status);
		return -EINVAL;
	} else if (unlikely(mbox_status == HSMP_ERR_PREREQ_NOT_SATISFIED)) {
		dev_err(sock->dev, "Message ID 0x%X failure : Prerequisite not satisfied (status = 0x%X)\n",
			msg->msg_id, mbox_status);
		return -EREMOTEIO;
	} else if (unlikely(mbox_status == HSMP_ERR_SMU_BUSY)) {
		dev_err(sock->dev, "Message ID 0x%X failure : SMU BUSY (status = 0x%X)\n",
			msg->msg_id, mbox_status);
		return -EBUSY;
	} else if (unlikely(mbox_status != HSMP_STATUS_OK)) {
		dev_err(sock->dev, "Message ID 0x%X unknown failure (status = 0x%X)\n",
			msg->msg_id, mbox_status);
		return -EIO;
	}

	/*
	 * SMU has responded OK. Read response data.
	 * SMU reads the input arguments from eight 32 bit registers starting
	 * from SMN_HSMP_MSG_DATA and writes the response data to the same
	 * SMN_HSMP_MSG_DATA address.
	 * We copy the response data if any, back to the args[].
	 */
	index = 0;
	while (index < msg->response_sz) {
		ret = sock->amd_hsmp_rdwr(sock, mbinfo->msg_arg_off + (index << 2),
					  &msg->args[index], HSMP_RD);
		if (ret) {
			dev_err(sock->dev, "Error %d reading response %u for message ID:%u\n",
				ret, index, msg->msg_id);
			break;
		}
		index++;
	}

	return ret;
}

static int validate_message(struct hsmp_message *msg)
{
	/* msg_id against valid range of message IDs */
	if (msg->msg_id < HSMP_TEST || msg->msg_id >= HSMP_MSG_ID_MAX)
		return -ENOMSG;

	/* msg_id is a reserved message ID */
	if (hsmp_msg_desc_table[msg->msg_id].type == HSMP_RSVD)
		return -ENOMSG;

	/*
	 * num_args passed by user should match the num_args specified in
	 * message description table.
	 */
	if (msg->num_args != hsmp_msg_desc_table[msg->msg_id].num_args)
		return -EINVAL;

	/*
	 * As the HSMP protocol evolves, newer platforms may define more
	 * response arguments for existing messages.  Use an upper-bound
	 * check so that older userspace callers requesting fewer response
	 * words than what the current hsmp_msg_desc_table[] defines are
	 * still accepted, while rejecting requests that exceed the
	 * hardware capability.
	 */
	if (msg->response_sz > hsmp_msg_desc_table[msg->msg_id].response_sz)
		return -EINVAL;

	return 0;
}

/*
 * Core message send. The caller must hold hsmp_sock_rwsem: the data plane
 * takes it for read so many messages run concurrently, while the probe-time
 * senders run under the write lock taken by probe. Holding it here serializes
 * every message against socket teardown, which also holds it for write.
 */
static int hsmp_send_message_locked(struct hsmp_message *msg)
{
	struct hsmp_socket *sock;
	unsigned int sock_ind;
	int ret;

	lockdep_assert_held(&hsmp_sock_rwsem);

	if (!msg)
		return -EINVAL;
	ret = validate_message(msg);
	if (ret)
		return ret;

	if (!hsmp_pdev.sock || msg->sock_ind >= hsmp_pdev.num_sockets)
		return -ENODEV;

	/*
	 * Sanitize sock_ind after the bounds check.  A mispredicted branch can
	 * still let the CPU speculatively use msg->sock_ind as an index into
	 * hsmp_pdev.sock[] (Spectre v1, CVE-2017-5753), including for callers
	 * other than hsmp_ioctl_msg() that pass a user-derived socket index.
	 */
	sock_ind = array_index_nospec(msg->sock_ind, hsmp_pdev.num_sockets);
	sock = &hsmp_pdev.sock[sock_ind];

	/*
	 * A slot exists for every possible socket, but it is only usable once
	 * that socket has actually been probed.  Reject messages aimed at a
	 * socket that was never brought up or is still in bring-up, so we never
	 * operate on a zero-initialized semaphore or an unmapped mailbox.  A
	 * non-NULL dev also guarantees virt_base_addr, the mailbox offsets and
	 * the semaphore are visible.
	 *
	 * Held under hsmp_sock_rwsem; pairs with smp_store_release(&sock->dev)
	 * in hsmp_parse_acpi_table().
	 */
	if (!smp_load_acquire(&sock->dev))
		return -ENODEV;

	ret = down_interruptible(&sock->hsmp_sem);
	if (ret < 0)
		return ret;

	ret = __hsmp_send_message(sock, msg);

	up(&sock->hsmp_sem);

	return ret;
}

int hsmp_send_message(struct hsmp_message *msg)
{
	/*
	 * Data-plane entry point: open /dev/hsmp fds and hwmon sysfs reads issue
	 * messages from here. Take hsmp_sock_rwsem for read so messages run
	 * concurrently with each other but are drained and kept out while
	 * probe/remove hold it for write to tear a socket down.
	 */
	guard(rwsem_read)(&hsmp_sock_rwsem);

	return hsmp_send_message_locked(msg);
}
EXPORT_SYMBOL_NS_GPL(hsmp_send_message, "AMD_HSMP");

int hsmp_msg_get_nargs(u16 sock_ind, u32 msg_id, u32 *data, u8 num_args)
{
	struct hsmp_message msg = {};
	unsigned int i;
	int ret;

	if (!data)
		return -EINVAL;
	msg.msg_id = msg_id;
	msg.sock_ind = sock_ind;
	msg.response_sz = num_args;

	ret = hsmp_send_message(&msg);
	if (ret)
		return ret;

	for (i = 0; i < num_args; i++)
		data[i] = msg.args[i];

	return 0;
}
EXPORT_SYMBOL_NS_GPL(hsmp_msg_get_nargs, "AMD_HSMP");

int hsmp_test(u16 sock_ind, u32 value)
{
	struct hsmp_message msg = { 0 };
	int ret;

	/*
	 * Test the hsmp port by performing TEST command. The test message
	 * takes one argument and returns the value of that argument + 1.
	 */
	msg.msg_id	= HSMP_TEST;
	msg.num_args	= 1;
	msg.response_sz	= 1;
	msg.args[0]	= value;
	msg.sock_ind	= sock_ind;

	ret = hsmp_send_message_locked(&msg);
	if (ret)
		return ret;

	/* Check the response value */
	if (msg.args[0] != (value + 1)) {
		dev_err(hsmp_pdev.sock[sock_ind].dev,
			"Socket %d test message failed, Expected 0x%08X, received 0x%08X\n",
			sock_ind, (value + 1), msg.args[0]);
		return -EBADE;
	}

	return ret;
}
EXPORT_SYMBOL_NS_GPL(hsmp_test, "AMD_HSMP");

static bool is_get_msg(struct hsmp_message *msg)
{
	if (hsmp_msg_desc_table[msg->msg_id].type == HSMP_GET)
		return true;

	if (hsmp_msg_desc_table[msg->msg_id].type == HSMP_SET_GET &&
	    (msg->args[0] & CHECK_GET_BIT))
		return true;

	return false;
}

static long hsmp_ioctl_msg(struct file *fp, unsigned long arg)
{
	int __user *arguser = (int  __user *)arg;
	struct hsmp_message msg = { 0 };
	int ret;

	if (copy_struct_from_user(&msg, sizeof(msg), arguser, sizeof(struct hsmp_message)))
		return -EFAULT;

	/*
	 * Check msg_id is within the range of supported msg ids
	 * i.e within the array bounds of hsmp_msg_desc_table
	 */
	if (msg.msg_id < HSMP_TEST || msg.msg_id >= HSMP_MSG_ID_MAX)
		return -ENOMSG;

	/*
	 * Sanitize the user-controlled msg_id against speculative
	 * execution.  The bounds check above retires the out-of-range
	 * case with -ENOMSG, but a mispredicted branch can still let the
	 * CPU speculatively use msg_id as an index into
	 * hsmp_msg_desc_table[] (here and in validate_message() /
	 * is_get_msg() called downstream via hsmp_send_message()), and
	 * pull arbitrary kernel memory into the cache (Spectre v1,
	 * CVE-2017-5753).  Clamp once into msg.msg_id so every downstream
	 * dereference sees the sanitized value.
	 */
	msg.msg_id = array_index_nospec(msg.msg_id, HSMP_MSG_ID_MAX);

	switch (fp->f_mode & (FMODE_WRITE | FMODE_READ)) {
	case FMODE_WRITE:
		/*
		 * Device is opened in O_WRONLY mode
		 * Execute only set/configure commands
		 */
		if (is_get_msg(&msg))
			return -EPERM;
		break;
	case FMODE_READ:
		/*
		 * Device is opened in O_RDONLY mode
		 * Execute only get/monitor commands
		 */
		if (!is_get_msg(&msg))
			return -EPERM;
		break;
	case FMODE_READ | FMODE_WRITE:
		/*
		 * Device is opened in O_RDWR mode
		 * Execute both get/monitor and set/configure commands
		 */
		break;
	default:
		return -EPERM;
	}

	ret = hsmp_send_message(&msg);
	if (ret)
		return ret;

	if (hsmp_msg_desc_table[msg.msg_id].response_sz > 0) {
		/* Copy results back to user for get/monitor commands */
		if (copy_to_user(arguser, &msg, sizeof(struct hsmp_message)))
			return -EFAULT;
	}

	return 0;
}

static ssize_t hsmp_metric_tbl_read_locked(struct hsmp_socket *sock, char *buf,
					   size_t size);

/*
 * Fetch the firmware metric (telemetry) table for the requested socket and
 * copy it to the userspace buffer described by the request.
 *
 * The metric table size is variable across HSMP protocol versions and on
 * Family 1Ah Model 50h-5Fh exceeds PAGE_SIZE.  The request carries the buffer
 * size, which may be anything up to the size firmware reported for this
 * socket's table.
 */
static long hsmp_ioctl_get_telemetry(struct file *fp, unsigned long arg)
{
	void *kbuf __free(kvfree) = NULL;
	void __user *arguser = (void __user *)arg;
	struct hsmp_telemetry_data req;
	struct hsmp_socket *sock;
	void __user *user_buf;
	size_t tbl_size;
	unsigned int sock_ind;
	int ret;

	/* Telemetry data is read-only; require read access on the fd. */
	if (!(fp->f_mode & FMODE_READ))
		return -EPERM;

	if (copy_from_user(&req, arguser, sizeof(req)))
		return -EFAULT;

	/*
	 * Reserved fields must be zero so future kernels can safely
	 * repurpose them without breaking already-deployed userspace.
	 */
	if (req.reserved)
		return -EINVAL;

	user_buf = u64_to_user_ptr(req.buf);

	/*
	 * /dev/hsmp is a singleton character device that outlives an individual
	 * socket unbind, so an ioctl on an already-open fd can run concurrently
	 * with socket teardown.  Hold hsmp_sock_rwsem for read across the socket
	 * lookup, the checks on its metric-table state and the read itself:
	 * probe and remove take the same lock for write, so they cannot free the
	 * socket array, unmap the table or destroy the per-socket mutex while
	 * this runs.
	 *
	 * The lock is dropped before the copy_to_user() below.  Faulting in the
	 * destination can block indefinitely on a userfaultfd-backed buffer,
	 * which would leave a socket unbind waiting for the write lock.
	 */
	scoped_guard(rwsem_read, &hsmp_sock_rwsem) {
		if (!hsmp_pdev.sock || req.sock_ind >= hsmp_pdev.num_sockets)
			return -ENODEV;

		/*
		 * Sanitize the user-controlled socket index against speculative
		 * execution.  The bounds check above retires the out-of-range
		 * case with -ENODEV, but a mispredicted branch can still let the
		 * CPU speculatively use sock_ind as an index into
		 * hsmp_pdev.sock[] and pull arbitrary kernel memory into the
		 * cache (Spectre v1, CVE-2017-5753).  array_index_nospec() turns
		 * the bounds check into a data-flow clamp so the speculative
		 * load is in-range too.
		 */
		sock_ind = array_index_nospec(req.sock_ind, hsmp_pdev.num_sockets);
		sock = &hsmp_pdev.sock[sock_ind];
		if (!sock->metric_tbl_addr)
			return -ENODEV;

		tbl_size = sock->metric_tbl_size;
		if (!tbl_size)
			return -ENODEV;

		/*
		 * A request shorter than the firmware table is served with the
		 * leading @size bytes of the snapshot, so userspace built
		 * against an older table layout keeps working on firmware that
		 * grew the table.  Asking for more than firmware provides is
		 * rejected rather than short-written, so a caller can never
		 * mistake a partial copy for a full one.
		 */
		if (!req.size || req.size > tbl_size)
			return -EINVAL;

		/*
		 * The bounce buffer is overwritten in full by memcpy_fromio()
		 * inside hsmp_metric_tbl_read_locked(); use kvmalloc() to avoid
		 * the zeroing cost of kvzalloc() on the ~13 KB allocation done
		 * on every ioctl call.
		 */
		kbuf = kvmalloc(tbl_size, GFP_KERNEL);
		if (!kbuf)
			return -ENOMEM;

		ret = hsmp_metric_tbl_read_locked(sock, kbuf, tbl_size);
	}

	if (ret < 0)
		return ret;

	if (copy_to_user(user_buf, kbuf, req.size))
		return -EFAULT;

	return 0;
}

long hsmp_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case HSMP_IOCTL_CMD:
		return hsmp_ioctl_msg(fp, arg);
	case HSMP_IOCTL_GET_TELEMETRY_DATA:
		return hsmp_ioctl_get_telemetry(fp, arg);
	default:
		return -ENOTTY;
	}
}

/*
 * Caller must hold hsmp_sock_rwsem. It keeps @sock, its metric-table mapping
 * and its metric_read_lock alive: probe and remove take the same lock for
 * write while they bring sockets up and tear them down.
 */
static ssize_t hsmp_metric_tbl_read_locked(struct hsmp_socket *sock, char *buf,
					   size_t size)
{
	struct hsmp_message msg = { 0 };
	int ret;

	lockdep_assert_held(&hsmp_sock_rwsem);

	if (!sock || !buf)
		return -EINVAL;

	if (!sock->metric_tbl_addr) {
		dev_err(sock->dev, "Metrics table address not available\n");
		return -ENOMEM;
	}

	if (size != sock->metric_tbl_size) {
		dev_err(sock->dev, "Wrong buffer size\n");
		return -EINVAL;
	}

	msg.msg_id	= HSMP_GET_METRIC_TABLE;
	msg.sock_ind	= sock->sock_ind;

	/*
	 * HSMP_GET_METRIC_TABLE makes firmware refill this socket's shared
	 * metric DRAM region, which is then copied out below.  Hold the
	 * per-socket lock across the fill-and-copy so concurrent readers of the
	 * same socket cannot return a torn snapshot.
	 */
	guard(mutex)(&sock->metric_read_lock);

	ret = hsmp_send_message_locked(&msg);
	if (ret)
		return ret;
	memcpy_fromio(buf, sock->metric_tbl_addr, size);

	return size;
}

ssize_t hsmp_metric_tbl_read(struct hsmp_socket *sock, char *buf, size_t size)
{
	guard(rwsem_read)(&hsmp_sock_rwsem);

	return hsmp_metric_tbl_read_locked(sock, buf, size);
}
EXPORT_SYMBOL_NS_GPL(hsmp_metric_tbl_read, "AMD_HSMP");

void hsmp_init_metric_read_locks(struct hsmp_plat_device *pdev)
{
	u16 i;

	for (i = 0; i < pdev->num_sockets; i++)
		mutex_init(&pdev->sock[i].metric_read_lock);
}
EXPORT_SYMBOL_NS_GPL(hsmp_init_metric_read_locks, "AMD_HSMP");

void hsmp_destroy_metric_read_locks(struct hsmp_plat_device *pdev)
{
	u16 i;

	for (i = 0; i < pdev->num_sockets; i++)
		mutex_destroy(&pdev->sock[i].metric_read_lock);
}
EXPORT_SYMBOL_NS_GPL(hsmp_destroy_metric_read_locks, "AMD_HSMP");

void hsmp_unmap_metric_tbls(struct hsmp_plat_device *pdev)
{
	struct hsmp_socket *sock;
	u16 i;

	for (i = 0; i < pdev->num_sockets; i++) {
		sock = &pdev->sock[i];
		if (sock->metric_tbl_addr) {
			iounmap(sock->metric_tbl_addr);
			sock->metric_tbl_addr = NULL;
		}
		sock->metric_tbl_size = 0;
	}
}
EXPORT_SYMBOL_NS_GPL(hsmp_unmap_metric_tbls, "AMD_HSMP");

int hsmp_get_tbl_dram_base(u16 sock_ind)
{
	struct hsmp_socket *sock = &hsmp_pdev.sock[sock_ind];
	struct hsmp_message msg = { 0 };
	phys_addr_t dram_addr;
	size_t tbl_size;
	int ret;

	msg.sock_ind	= sock_ind;
	msg.response_sz	= hsmp_msg_desc_table[HSMP_GET_METRIC_TABLE_DRAM_ADDR].response_sz;
	msg.msg_id	= HSMP_GET_METRIC_TABLE_DRAM_ADDR;

	ret = hsmp_send_message_locked(&msg);
	if (ret)
		return ret;

	/*
	 * calculate the metric table DRAM address from lower and upper 32 bits
	 * sent from SMU and ioremap it to virtual address.
	 */
	dram_addr = msg.args[0] | ((u64)(msg.args[1]) << 32);
	if (!dram_addr) {
		dev_err(sock->dev, "Invalid DRAM address for metric table\n");
		return -ENOMEM;
	}
	/*
	 * The ACPI socket array is shared across sockets and outlives a
	 * per-socket unbind, so metric_tbl_addr may hold a mapping from an
	 * earlier bind of this socket. Unmap it before remapping so an
	 * unbind/rebind cycle does not leak a metric-table mapping. This runs
	 * during probe before the metric sysfs attribute is exposed, so no
	 * reader can be using it.
	 */
	if (sock->metric_tbl_addr) {
		iounmap(sock->metric_tbl_addr);
		sock->metric_tbl_addr = NULL;
	}
	sock->metric_tbl_size = 0;

	/* SMU returns table size from Family 1Ah Model 50h and forward */
	if (msg.args[2])
		tbl_size = msg.args[2];
	else
		tbl_size = sizeof(struct hsmp_metric_table);

	sock->metric_tbl_addr = ioremap(dram_addr, tbl_size);
	if (!sock->metric_tbl_addr) {
		dev_err(sock->dev, "Failed to ioremap metric table addr\n");
		return -ENOMEM;
	}
	sock->metric_tbl_size = tbl_size;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(hsmp_get_tbl_dram_base, "AMD_HSMP");

int hsmp_cache_proto_ver(u16 sock_ind)
{
	struct hsmp_message msg = { 0 };
	int ret;

	msg.msg_id	= HSMP_GET_PROTO_VER;
	msg.sock_ind	= sock_ind;
	msg.response_sz = hsmp_msg_desc_table[HSMP_GET_PROTO_VER].response_sz;

	ret = hsmp_send_message_locked(&msg);
	if (!ret)
		hsmp_pdev.proto_ver = msg.args[0];

	return ret;
}
EXPORT_SYMBOL_NS_GPL(hsmp_cache_proto_ver, "AMD_HSMP");

static const struct file_operations hsmp_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= hsmp_ioctl,
	.compat_ioctl	= hsmp_ioctl,
};

int hsmp_misc_register(struct device *dev)
{
	hsmp_pdev.mdev.name	= HSMP_CDEV_NAME;
	hsmp_pdev.mdev.minor	= MISC_DYNAMIC_MINOR;
	hsmp_pdev.mdev.fops	= &hsmp_fops;
	/*
	 * The caller chooses the parent. The platform driver has a single
	 * device whose lifetime matches /dev/hsmp and parents it there. The
	 * ACPI driver passes NULL: its /dev/hsmp is a singleton shared by
	 * per-socket devices that can be unbound individually and out of order,
	 * so parenting it to one would leave it attached to an already-removed
	 * device.
	 */
	hsmp_pdev.mdev.parent	= dev;
	hsmp_pdev.mdev.nodename	= HSMP_DEVNODE_NAME;
	hsmp_pdev.mdev.mode	= 0644;

	return misc_register(&hsmp_pdev.mdev);
}
EXPORT_SYMBOL_NS_GPL(hsmp_misc_register, "AMD_HSMP");

void hsmp_misc_deregister(void)
{
	misc_deregister(&hsmp_pdev.mdev);
	hsmp_pdev.mdev.this_device = NULL;
}
EXPORT_SYMBOL_NS_GPL(hsmp_misc_deregister, "AMD_HSMP");

struct hsmp_plat_device *get_hsmp_pdev(void)
{
	return &hsmp_pdev;
}
EXPORT_SYMBOL_NS_GPL(get_hsmp_pdev, "AMD_HSMP");

MODULE_DESCRIPTION("AMD HSMP Common driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
