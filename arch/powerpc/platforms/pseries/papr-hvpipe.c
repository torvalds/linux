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
#include <asm/rtas-work-area.h>
#include <asm/papr-sysparm.h>
#include <uapi/asm/papr-hvpipe.h>
#include "pseries.h"
#include "papr-hvpipe.h"

static DEFINE_SPINLOCK(hvpipe_src_list_lock);
static LIST_HEAD(hvpipe_src_list);

static unsigned char hvpipe_ras_buf[RTAS_ERROR_LOG_MAX];
static struct workqueue_struct *papr_hvpipe_wq;
static struct work_struct *papr_hvpipe_work;
static int hvpipe_check_exception_token;
static bool hvpipe_feature;

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

/*
 * ibm,receive-hvpipe-msg RTAS call.
 * @area: Caller-provided work area buffer for results.
 * @srcID: Source ID returned by the RTAS call.
 * @bytesw: Bytes written by RTAS call to @area.
 */
static int rtas_ibm_receive_hvpipe_msg(struct rtas_work_area *area,
					u32 *srcID, u32 *bytesw)
{
	const s32 token = rtas_function_token(RTAS_FN_IBM_RECEIVE_HVPIPE_MSG);
	u32 rets[2];
	s32 fwrc;
	int ret;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	do {
		fwrc = rtas_call(token, 2, 3, rets,
				rtas_work_area_phys(area),
				rtas_work_area_size(area));

	} while (rtas_busy_delay(fwrc));

	switch (fwrc) {
	case RTAS_SUCCESS:
		*srcID = rets[0];
		*bytesw = rets[1];
		ret = 0;
		break;
	case RTAS_HARDWARE_ERROR:
		ret = -EIO;
		break;
	case RTAS_INVALID_PARAMETER:
		ret = -EINVAL;
		break;
	case RTAS_FUNC_NOT_SUPPORTED:
		ret = -EOPNOTSUPP;
		break;
	default:
		ret = -EIO;
		pr_err_ratelimited("unexpected ibm,receive-hvpipe-msg status %d\n", fwrc);
		break;
	}

	return ret;
}

/*
 * ibm,send-hvpipe-msg RTAS call
 * @area: Caller-provided work area buffer to send.
 * @srcID: Target source for the send pipe message.
 */
static int rtas_ibm_send_hvpipe_msg(struct rtas_work_area *area, u32 srcID)
{
	const s32 token = rtas_function_token(RTAS_FN_IBM_SEND_HVPIPE_MSG);
	s32 fwrc;
	int ret;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	do {
		fwrc = rtas_call(token, 2, 1, NULL, srcID,
				rtas_work_area_phys(area));

	} while (rtas_busy_delay(fwrc));

	switch (fwrc) {
	case RTAS_SUCCESS:
		ret = 0;
		break;
	case RTAS_HARDWARE_ERROR:
		ret = -EIO;
		break;
	case RTAS_INVALID_PARAMETER:
		ret = -EINVAL;
		break;
	case RTAS_HVPIPE_CLOSED:
		ret = -EPIPE;
		break;
	case RTAS_FUNC_NOT_SUPPORTED:
		ret = -EOPNOTSUPP;
		break;
	default:
		ret = -EIO;
		pr_err_ratelimited("unexpected ibm,receive-hvpipe-msg status %d\n", fwrc);
		break;
	}

	return ret;
}

static struct hvpipe_source_info *hvpipe_find_source(u32 srcID)
{
	struct hvpipe_source_info *src_info;

	list_for_each_entry(src_info, &hvpipe_src_list, list)
		if (src_info->srcID == srcID)
			return src_info;

	return NULL;
}

/*
 * This work function collects receive buffer with recv HVPIPE
 * RTAS call. Called from read()
 * @buf: User specified buffer to copy the payload that returned
 *       from recv HVPIPE RTAS.
 * @size: Size of buffer user passed.
 */
static int hvpipe_rtas_recv_msg(char __user *buf, int size)
{
	struct rtas_work_area *work_area;
	u32 srcID, bytes_written;
	int ret;

	work_area = rtas_work_area_alloc(SZ_4K);
	if (!work_area) {
		pr_err("Could not allocate RTAS buffer for recv pipe\n");
		return -ENOMEM;
	}

	ret = rtas_ibm_receive_hvpipe_msg(work_area, &srcID,
					&bytes_written);
	if (!ret) {
		/*
		 * Recv HVPIPE RTAS is successful.
		 * When releasing FD or no one is waiting on the
		 * specific source, issue recv HVPIPE RTAS call
		 * so that pipe is not blocked - this func is called
		 * with NULL buf.
		 */
		if (buf) {
			if (size < bytes_written) {
				pr_err("Received the payload size = %d, but the buffer size = %d\n",
					bytes_written, size);
				bytes_written = size;
			}
			ret = copy_to_user(buf,
					rtas_work_area_raw_buf(work_area),
					bytes_written);
			if (!ret)
				ret = bytes_written;
		}
	} else {
		pr_err("ibm,receive-hvpipe-msg failed with %d\n",
				ret);
	}

	rtas_work_area_free(work_area);
	return ret;
}

/*
 * papr_hvpipe_handle_write -  Issue send HVPIPE RTAS and return
 * the size (payload + HVPIPE_HDR_LEN) for RTAS success.
 * Otherwise returns the status of RTAS to the user space
 */
static ssize_t papr_hvpipe_handle_write(struct file *file,
	const char __user *buf, size_t size, loff_t *off)
{
	struct hvpipe_source_info *src_info = file->private_data;
	struct rtas_work_area *work_area, *work_buf;
	unsigned long ret, len;
	__be64 *area_be;

	/*
	 * Return -ENXIO during migration
	 */
	if (!hvpipe_feature)
		return -ENXIO;

	if (!src_info)
		return -EIO;

	/*
	 * Send HVPIPE RTAS is used to send payload to the specific
	 * source with the input parameters source ID and the payload
	 * as buffer list. Each entry in the buffer list contains
	 * address/length pair of the buffer.
	 *
	 * The buffer list format is as follows:
	 *
	 * Header (length of address/length pairs and the header length)
	 * Address of 4K buffer 1
	 * Length of 4K buffer 1 used
	 * ...
	 * Address of 4K buffer n
	 * Length of 4K buffer n used
	 *
	 * See PAPR 7.3.32.2 ibm,send-hvpipe-msg
	 *
	 * Even though can support max 1MB payload, the hypervisor
	 * supports only 4048 bytes payload at present and also
	 * just one address/length entry.
	 *
	 * writev() interface can be added in future when the
	 * hypervisor supports multiple buffer list entries.
	 */
	/* HVPIPE_MAX_WRITE_BUFFER_SIZE = 4048 bytes */
	if ((size > (HVPIPE_HDR_LEN + HVPIPE_MAX_WRITE_BUFFER_SIZE)) ||
		(size <= HVPIPE_HDR_LEN))
		return -EINVAL;

	/*
	 * The length of (address + length) pair + the length of header
	 */
	len = (2 * sizeof(u64)) + sizeof(u64);
	size -= HVPIPE_HDR_LEN;
	buf += HVPIPE_HDR_LEN;
	mutex_lock(&rtas_ibm_send_hvpipe_msg_lock);
	work_area = rtas_work_area_alloc(SZ_4K);
	if (!work_area) {
		ret = -ENOMEM;
		goto out;
	}
	area_be = (__be64 *)rtas_work_area_raw_buf(work_area);
	/* header */
	area_be[0] = cpu_to_be64(len);

	work_buf = rtas_work_area_alloc(SZ_4K);
	if (!work_buf) {
		ret = -ENOMEM;
		goto out_work;
	}
	/* First buffer address */
	area_be[1] = cpu_to_be64(rtas_work_area_phys(work_buf));
	/* First buffer address length */
	area_be[2] = cpu_to_be64(size);

	if (!copy_from_user(rtas_work_area_raw_buf(work_buf), buf, size)) {
		ret = rtas_ibm_send_hvpipe_msg(work_area, src_info->srcID);
		if (!ret)
			ret = size + HVPIPE_HDR_LEN;
	} else
		ret = -EPERM;

	rtas_work_area_free(work_buf);
out_work:
	rtas_work_area_free(work_area);
out:
	mutex_unlock(&rtas_ibm_send_hvpipe_msg_lock);
	return ret;
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
	struct papr_hvpipe_hdr hdr;
	long ret;

	/*
	 * Return -ENXIO during migration
	 */
	if (!hvpipe_feature)
		return -ENXIO;

	if (!src_info)
		return -EIO;

	/*
	 * Max payload is 4048 (HVPIPE_MAX_WRITE_BUFFER_SIZE)
	 */
	if ((size > (HVPIPE_HDR_LEN + HVPIPE_MAX_WRITE_BUFFER_SIZE)) ||
		(size < HVPIPE_HDR_LEN))
		return -EINVAL;

	/*
	 * Payload is not available to receive or source pipe
	 * is not closed.
	 */
	if (!src_info->hvpipe_status)
		return 0;

	hdr.version = 0;
	hdr.flags = 0;

	/*
	 * In case if the hvpipe has payload and also the
	 * hypervisor closed the pipe to the source, retrieve
	 * the payload and return to the user space first and
	 * then notify the userspace about the hvpipe close in
	 * next read().
	 */
	if (src_info->hvpipe_status & HVPIPE_MSG_AVAILABLE)
		hdr.flags = HVPIPE_MSG_AVAILABLE;
	else if (src_info->hvpipe_status & HVPIPE_LOST_CONNECTION)
		hdr.flags = HVPIPE_LOST_CONNECTION;
	else
		/*
		 * Should not be here without one of the above
		 * flags set
		 */
		return -EIO;

	ret = copy_to_user(buf, &hdr, HVPIPE_HDR_LEN);
	if (ret)
		return ret;

	/*
	 * Message event has payload, so get the payload with
	 * recv HVPIPE RTAS.
	 */
	if (hdr.flags & HVPIPE_MSG_AVAILABLE) {
		ret = hvpipe_rtas_recv_msg(buf + HVPIPE_HDR_LEN,
				size - HVPIPE_HDR_LEN);
		if (ret > 0) {
			src_info->hvpipe_status &= ~HVPIPE_MSG_AVAILABLE;
			ret += HVPIPE_HDR_LEN;
		}
	} else if (hdr.flags & HVPIPE_LOST_CONNECTION) {
		/*
		 * Hypervisor is closing the pipe for the specific
		 * source. So notify user space.
		 */
		src_info->hvpipe_status &= ~HVPIPE_LOST_CONNECTION;
		ret = HVPIPE_HDR_LEN;
	}

	return ret;
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

	/*
	 * HVPIPE is disabled during SUSPEND and enabled after migration.
	 * So return POLLRDHUP during migration
	 */
	if (!hvpipe_feature)
		return POLLRDHUP;

	if (!src_info)
		return POLLNVAL;

	/*
	 * If hvpipe already has pending payload, return so that
	 * the user space can issue read().
	 */
	if (src_info->hvpipe_status)
		return POLLIN | POLLRDNORM;

	/*
	 * Wait for the message event
	 * hvpipe_event_interrupt() wakes up this wait_queue
	 */
	poll_wait(filp, &src_info->recv_wqh, wait);
	if (src_info->hvpipe_status)
		return POLLIN | POLLRDNORM;

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
	/*
	 * If the pipe for this specific source has any pending
	 * payload, issue recv HVPIPE RTAS so that pipe will not
	 * be blocked.
	 */
	if (src_info->hvpipe_status & HVPIPE_MSG_AVAILABLE) {
		src_info->hvpipe_status = 0;
		spin_unlock(&hvpipe_src_list_lock);
		hvpipe_rtas_recv_msg(NULL, 0);
	} else
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

	/*
	 * Return -ENXIO during migration
	 */
	if (!hvpipe_feature)
		return -ENXIO;

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

/*
 * papr_hvpipe_work_fn - called to issue recv HVPIPE RTAS for
 * sources that are not monitored by user space so that pipe
 * will not be blocked.
 */
static void papr_hvpipe_work_fn(struct work_struct *work)
{
	hvpipe_rtas_recv_msg(NULL, 0);
}

/*
 * HVPIPE event message IRQ handler.
 * The hypervisor sends event IRQ if the partition has payload
 * and generates another event only after payload is read with
 * recv HVPIPE RTAS.
 */
static irqreturn_t hvpipe_event_interrupt(int irq, void *dev_id)
{
	struct hvpipe_event_buf *hvpipe_event;
	struct pseries_errorlog *pseries_log;
	struct hvpipe_source_info *src_info;
	struct rtas_error_log *elog;
	int rc;

	rc = rtas_call(hvpipe_check_exception_token, 6, 1, NULL,
		RTAS_VECTOR_EXTERNAL_INTERRUPT, virq_to_hw(irq),
		RTAS_HVPIPE_MSG_EVENTS, 1, __pa(&hvpipe_ras_buf),
		rtas_get_error_log_max());

	if (rc != 0) {
		pr_err_ratelimited("unexpected hvpipe-event-notification failed %d\n", rc);
		return IRQ_HANDLED;
	}

	elog = (struct rtas_error_log *)hvpipe_ras_buf;
	if (unlikely(rtas_error_type(elog) != RTAS_TYPE_HVPIPE)) {
		pr_warn_ratelimited("Unexpected event type %d\n",
				rtas_error_type(elog));
		return IRQ_HANDLED;
	}

	pseries_log = get_pseries_errorlog(elog,
				PSERIES_ELOG_SECT_ID_HVPIPE_EVENT);
	hvpipe_event = (struct hvpipe_event_buf *)pseries_log->data;

	/*
	 * The hypervisor notifies partition when the payload is
	 * available to read with recv HVPIPE RTAS and it will not
	 * notify another event for any source until the previous
	 * payload is read. Means the pipe is blocked in the
	 * hypervisor until the payload is read.
	 *
	 * If the source is ready to accept payload and wakeup the
	 * corresponding FD. Hold lock and update hvpipe_status
	 * and this lock is needed in case the user space process
	 * is in release FD instead of poll() so that release()
	 * reads the payload to unblock pipe before closing FD.
	 *
	 * otherwise (means no other user process waiting for the
	 * payload, issue recv HVPIPE RTAS (papr_hvpipe_work_fn())
	 * to unblock pipe.
	 */
	spin_lock(&hvpipe_src_list_lock);
	src_info = hvpipe_find_source(be32_to_cpu(hvpipe_event->srcID));
	if (src_info) {
		u32 flags = 0;

		if (hvpipe_event->event_type & HVPIPE_LOST_CONNECTION)
			flags = HVPIPE_LOST_CONNECTION;
		else if (hvpipe_event->event_type & HVPIPE_MSG_AVAILABLE)
			flags = HVPIPE_MSG_AVAILABLE;

		src_info->hvpipe_status |= flags;
		wake_up(&src_info->recv_wqh);
		spin_unlock(&hvpipe_src_list_lock);
	} else {
		spin_unlock(&hvpipe_src_list_lock);
		/*
		 * user space is not waiting on this source. So
		 * execute receive pipe RTAS so that pipe will not
		 * be blocked.
		 */
		if (hvpipe_event->event_type & HVPIPE_MSG_AVAILABLE)
			queue_work(papr_hvpipe_wq, papr_hvpipe_work);
	}

	return IRQ_HANDLED;
}

/*
 * Enable hvpipe by system parameter set with parameter
 * token = 64 and with 1 byte buffer data:
 * 0 = hvpipe not in use/disable
 * 1 = hvpipe in use/enable
 */
static int set_hvpipe_sys_param(u8 val)
{
	struct papr_sysparm_buf *buf;
	int ret;

	buf = papr_sysparm_buf_alloc();
	if (!buf)
		return -ENOMEM;

	buf->len = cpu_to_be16(1);
	buf->val[0] = val;
	ret = papr_sysparm_set(PAPR_SYSPARM_HVPIPE_ENABLE, buf);
	if (ret)
		pr_err("Can not enable hvpipe %d\n", ret);

	papr_sysparm_buf_free(buf);

	return ret;
}

static int __init enable_hvpipe_IRQ(void)
{
	struct device_node *np;

	hvpipe_check_exception_token = rtas_function_token(RTAS_FN_CHECK_EXCEPTION);
	if (hvpipe_check_exception_token  == RTAS_UNKNOWN_SERVICE)
		return -ENODEV;

	/* hvpipe events */
	np = of_find_node_by_path("/event-sources/ibm,hvpipe-msg-events");
	if (np != NULL) {
		request_event_sources_irqs(np, hvpipe_event_interrupt,
					"HPIPE_EVENT");
		of_node_put(np);
	} else {
		pr_err("Can not enable hvpipe event IRQ\n");
		return -ENODEV;
	}

	return 0;
}

void hvpipe_migration_handler(int action)
{
	pr_info("hvpipe migration event %d\n", action);

	/*
	 * HVPIPE is not used (Failed to create /dev/papr-hvpipe).
	 * So nothing to do for migration.
	 */
	if (!papr_hvpipe_work)
		return;

	switch (action) {
	case HVPIPE_SUSPEND:
		if (hvpipe_feature) {
			/*
			 * Disable hvpipe_feature to the user space.
			 * It will be enabled with RESUME event.
			 */
			hvpipe_feature = false;
			/*
			 * set system parameter hvpipe 'disable'
			 */
			set_hvpipe_sys_param(0);
		}
		break;
	case HVPIPE_RESUME:
		/*
		 * set system parameter hvpipe 'enable'
		 */
		if (!set_hvpipe_sys_param(1))
			hvpipe_feature = true;
		else
			pr_err("hvpipe is not enabled after migration\n");

		break;
	}
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

	papr_hvpipe_work = kzalloc(sizeof(struct work_struct), GFP_ATOMIC);
	if (!papr_hvpipe_work)
		return -ENOMEM;

	INIT_WORK(papr_hvpipe_work, papr_hvpipe_work_fn);

	papr_hvpipe_wq = alloc_ordered_workqueue("papr hvpipe workqueue", 0);
	if (!papr_hvpipe_wq) {
		ret = -ENOMEM;
		goto out;
	}

	ret = enable_hvpipe_IRQ();
	if (!ret) {
		ret = set_hvpipe_sys_param(1);
		if (!ret)
			ret = misc_register(&papr_hvpipe_dev);
	}

	if (!ret) {
		pr_info("hvpipe feature is enabled\n");
		hvpipe_feature = true;
		return 0;
	}

	pr_err("hvpipe feature is not enabled %d\n", ret);
	destroy_workqueue(papr_hvpipe_wq);
out:
	kfree(papr_hvpipe_work);
	papr_hvpipe_work = NULL;
	return ret;
}
machine_device_initcall(pseries, papr_hvpipe_init);
