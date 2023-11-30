// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments System Control Interface Protocol Driver
 *
 * Copyright (C) 2015-2022 Texas Instruments Incorporated - https://www.ti.com/
 *	Nishanth Menon
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/bitmap.h>
#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/soc/ti/ti-msgmgr.h>
#include <linux/soc/ti/ti_sci_protocol.h>
#include <linux/reboot.h>

#include "ti_sci.h"

/* List of all TI SCI devices active in system */
static LIST_HEAD(ti_sci_list);
/* Protection for the entire list */
static DEFINE_MUTEX(ti_sci_list_mutex);

/**
 * struct ti_sci_xfer - Structure representing a message flow
 * @tx_message:	Transmit message
 * @rx_len:	Receive message length
 * @xfer_buf:	Preallocated buffer to store receive message
 *		Since we work with request-ACK protocol, we can
 *		reuse the same buffer for the rx path as we
 *		use for the tx path.
 * @done:	completion event
 */
struct ti_sci_xfer {
	struct ti_msgmgr_message tx_message;
	u8 rx_len;
	u8 *xfer_buf;
	struct completion done;
};

/**
 * struct ti_sci_xfers_info - Structure to manage transfer information
 * @sem_xfer_count:	Counting Semaphore for managing max simultaneous
 *			Messages.
 * @xfer_block:		Preallocated Message array
 * @xfer_alloc_table:	Bitmap table for allocated messages.
 *			Index of this bitmap table is also used for message
 *			sequence identifier.
 * @xfer_lock:		Protection for message allocation
 */
struct ti_sci_xfers_info {
	struct semaphore sem_xfer_count;
	struct ti_sci_xfer *xfer_block;
	unsigned long *xfer_alloc_table;
	/* protect transfer allocation */
	spinlock_t xfer_lock;
};

/**
 * struct ti_sci_desc - Description of SoC integration
 * @default_host_id:	Host identifier representing the compute entity
 * @max_rx_timeout_ms:	Timeout for communication with SoC (in Milliseconds)
 * @max_msgs: Maximum number of messages that can be pending
 *		  simultaneously in the system
 * @max_msg_size: Maximum size of data per message that can be handled.
 */
struct ti_sci_desc {
	u8 default_host_id;
	int max_rx_timeout_ms;
	int max_msgs;
	int max_msg_size;
};

/**
 * struct ti_sci_info - Structure representing a TI SCI instance
 * @dev:	Device pointer
 * @desc:	SoC description for this instance
 * @nb:	Reboot Notifier block
 * @d:		Debugfs file entry
 * @debug_region: Memory region where the debug message are available
 * @debug_region_size: Debug region size
 * @debug_buffer: Buffer allocated to copy debug messages.
 * @handle:	Instance of TI SCI handle to send to clients.
 * @cl:		Mailbox Client
 * @chan_tx:	Transmit mailbox channel
 * @chan_rx:	Receive mailbox channel
 * @minfo:	Message info
 * @node:	list head
 * @host_id:	Host ID
 * @users:	Number of users of this instance
 */
struct ti_sci_info {
	struct device *dev;
	struct notifier_block nb;
	const struct ti_sci_desc *desc;
	struct dentry *d;
	void __iomem *debug_region;
	char *debug_buffer;
	size_t debug_region_size;
	struct ti_sci_handle handle;
	struct mbox_client cl;
	struct mbox_chan *chan_tx;
	struct mbox_chan *chan_rx;
	struct ti_sci_xfers_info minfo;
	struct list_head node;
	u8 host_id;
	/* protected by ti_sci_list_mutex */
	int users;
};

#define cl_to_ti_sci_info(c)	container_of(c, struct ti_sci_info, cl)
#define handle_to_ti_sci_info(h) container_of(h, struct ti_sci_info, handle)
#define reboot_to_ti_sci_info(n) container_of(n, struct ti_sci_info, nb)

#ifdef CONFIG_DEBUG_FS

/**
 * ti_sci_debug_show() - Helper to dump the debug log
 * @s:	sequence file pointer
 * @unused:	unused.
 *
 * Return: 0
 */
static int ti_sci_debug_show(struct seq_file *s, void *unused)
{
	struct ti_sci_info *info = s->private;

	memcpy_fromio(info->debug_buffer, info->debug_region,
		      info->debug_region_size);
	/*
	 * We don't trust firmware to leave NULL terminated last byte (hence
	 * we have allocated 1 extra 0 byte). Since we cannot guarantee any
	 * specific data format for debug messages, We just present the data
	 * in the buffer as is - we expect the messages to be self explanatory.
	 */
	seq_puts(s, info->debug_buffer);
	return 0;
}

/* Provide the log file operations interface*/
DEFINE_SHOW_ATTRIBUTE(ti_sci_debug);

/**
 * ti_sci_debugfs_create() - Create log debug file
 * @pdev:	platform device pointer
 * @info:	Pointer to SCI entity information
 *
 * Return: 0 if all went fine, else corresponding error.
 */
static int ti_sci_debugfs_create(struct platform_device *pdev,
				 struct ti_sci_info *info)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	char debug_name[50] = "ti_sci_debug@";

	/* Debug region is optional */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "debug_messages");
	info->debug_region = devm_ioremap_resource(dev, res);
	if (IS_ERR(info->debug_region))
		return 0;
	info->debug_region_size = resource_size(res);

	info->debug_buffer = devm_kcalloc(dev, info->debug_region_size + 1,
					  sizeof(char), GFP_KERNEL);
	if (!info->debug_buffer)
		return -ENOMEM;
	/* Setup NULL termination */
	info->debug_buffer[info->debug_region_size] = 0;

	info->d = debugfs_create_file(strncat(debug_name, dev_name(dev),
					      sizeof(debug_name) -
					      sizeof("ti_sci_debug@")),
				      0444, NULL, info, &ti_sci_debug_fops);
	if (IS_ERR(info->d))
		return PTR_ERR(info->d);

	dev_dbg(dev, "Debug region => %p, size = %zu bytes, resource: %pr\n",
		info->debug_region, info->debug_region_size, res);
	return 0;
}

/**
 * ti_sci_debugfs_destroy() - clean up log debug file
 * @pdev:	platform device pointer
 * @info:	Pointer to SCI entity information
 */
static void ti_sci_debugfs_destroy(struct platform_device *pdev,
				   struct ti_sci_info *info)
{
	if (IS_ERR(info->debug_region))
		return;

	debugfs_remove(info->d);
}
#else /* CONFIG_DEBUG_FS */
static inline int ti_sci_debugfs_create(struct platform_device *dev,
					struct ti_sci_info *info)
{
	return 0;
}

static inline void ti_sci_debugfs_destroy(struct platform_device *dev,
					  struct ti_sci_info *info)
{
}
#endif /* CONFIG_DEBUG_FS */

/**
 * ti_sci_dump_header_dbg() - Helper to dump a message header.
 * @dev:	Device pointer corresponding to the SCI entity
 * @hdr:	pointer to header.
 */
static inline void ti_sci_dump_header_dbg(struct device *dev,
					  struct ti_sci_msg_hdr *hdr)
{
	dev_dbg(dev, "MSGHDR:type=0x%04x host=0x%02x seq=0x%02x flags=0x%08x\n",
		hdr->type, hdr->host, hdr->seq, hdr->flags);
}

/**
 * ti_sci_rx_callback() - mailbox client callback for receive messages
 * @cl:	client pointer
 * @m:	mailbox message
 *
 * Processes one received message to appropriate transfer information and
 * signals completion of the transfer.
 *
 * NOTE: This function will be invoked in IRQ context, hence should be
 * as optimal as possible.
 */
static void ti_sci_rx_callback(struct mbox_client *cl, void *m)
{
	struct ti_sci_info *info = cl_to_ti_sci_info(cl);
	struct device *dev = info->dev;
	struct ti_sci_xfers_info *minfo = &info->minfo;
	struct ti_msgmgr_message *mbox_msg = m;
	struct ti_sci_msg_hdr *hdr = (struct ti_sci_msg_hdr *)mbox_msg->buf;
	struct ti_sci_xfer *xfer;
	u8 xfer_id;

	xfer_id = hdr->seq;

	/*
	 * Are we even expecting this?
	 * NOTE: barriers were implicit in locks used for modifying the bitmap
	 */
	if (!test_bit(xfer_id, minfo->xfer_alloc_table)) {
		dev_err(dev, "Message for %d is not expected!\n", xfer_id);
		return;
	}

	xfer = &minfo->xfer_block[xfer_id];

	/* Is the message of valid length? */
	if (mbox_msg->len > info->desc->max_msg_size) {
		dev_err(dev, "Unable to handle %zu xfer(max %d)\n",
			mbox_msg->len, info->desc->max_msg_size);
		ti_sci_dump_header_dbg(dev, hdr);
		return;
	}
	if (mbox_msg->len < xfer->rx_len) {
		dev_err(dev, "Recv xfer %zu < expected %d length\n",
			mbox_msg->len, xfer->rx_len);
		ti_sci_dump_header_dbg(dev, hdr);
		return;
	}

	ti_sci_dump_header_dbg(dev, hdr);
	/* Take a copy to the rx buffer.. */
	memcpy(xfer->xfer_buf, mbox_msg->buf, xfer->rx_len);
	complete(&xfer->done);
}

/**
 * ti_sci_get_one_xfer() - Allocate one message
 * @info:	Pointer to SCI entity information
 * @msg_type:	Message type
 * @msg_flags:	Flag to set for the message
 * @tx_message_size: transmit message size
 * @rx_message_size: receive message size
 *
 * Helper function which is used by various command functions that are
 * exposed to clients of this driver for allocating a message traffic event.
 *
 * This function can sleep depending on pending requests already in the system
 * for the SCI entity. Further, this also holds a spinlock to maintain integrity
 * of internal data structures.
 *
 * Return: 0 if all went fine, else corresponding error.
 */
static struct ti_sci_xfer *ti_sci_get_one_xfer(struct ti_sci_info *info,
					       u16 msg_type, u32 msg_flags,
					       size_t tx_message_size,
					       size_t rx_message_size)
{
	struct ti_sci_xfers_info *minfo = &info->minfo;
	struct ti_sci_xfer *xfer;
	struct ti_sci_msg_hdr *hdr;
	unsigned long flags;
	unsigned long bit_pos;
	u8 xfer_id;
	int ret;
	int timeout;

	/* Ensure we have sane transfer sizes */
	if (rx_message_size > info->desc->max_msg_size ||
	    tx_message_size > info->desc->max_msg_size ||
	    rx_message_size < sizeof(*hdr) || tx_message_size < sizeof(*hdr))
		return ERR_PTR(-ERANGE);

	/*
	 * Ensure we have only controlled number of pending messages.
	 * Ideally, we might just have to wait a single message, be
	 * conservative and wait 5 times that..
	 */
	timeout = msecs_to_jiffies(info->desc->max_rx_timeout_ms) * 5;
	ret = down_timeout(&minfo->sem_xfer_count, timeout);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Keep the locked section as small as possible */
	spin_lock_irqsave(&minfo->xfer_lock, flags);
	bit_pos = find_first_zero_bit(minfo->xfer_alloc_table,
				      info->desc->max_msgs);
	set_bit(bit_pos, minfo->xfer_alloc_table);
	spin_unlock_irqrestore(&minfo->xfer_lock, flags);

	/*
	 * We already ensured in probe that we can have max messages that can
	 * fit in  hdr.seq - NOTE: this improves access latencies
	 * to predictable O(1) access, BUT, it opens us to risk if
	 * remote misbehaves with corrupted message sequence responses.
	 * If that happens, we are going to be messed up anyways..
	 */
	xfer_id = (u8)bit_pos;

	xfer = &minfo->xfer_block[xfer_id];

	hdr = (struct ti_sci_msg_hdr *)xfer->tx_message.buf;
	xfer->tx_message.len = tx_message_size;
	xfer->tx_message.chan_rx = info->chan_rx;
	xfer->tx_message.timeout_rx_ms = info->desc->max_rx_timeout_ms;
	xfer->rx_len = (u8)rx_message_size;

	reinit_completion(&xfer->done);

	hdr->seq = xfer_id;
	hdr->type = msg_type;
	hdr->host = info->host_id;
	hdr->flags = msg_flags;

	return xfer;
}

/**
 * ti_sci_put_one_xfer() - Release a message
 * @minfo:	transfer info pointer
 * @xfer:	message that was reserved by ti_sci_get_one_xfer
 *
 * This holds a spinlock to maintain integrity of internal data structures.
 */
static void ti_sci_put_one_xfer(struct ti_sci_xfers_info *minfo,
				struct ti_sci_xfer *xfer)
{
	unsigned long flags;
	struct ti_sci_msg_hdr *hdr;
	u8 xfer_id;

	hdr = (struct ti_sci_msg_hdr *)xfer->tx_message.buf;
	xfer_id = hdr->seq;

	/*
	 * Keep the locked section as small as possible
	 * NOTE: we might escape with smp_mb and no lock here..
	 * but just be conservative and symmetric.
	 */
	spin_lock_irqsave(&minfo->xfer_lock, flags);
	clear_bit(xfer_id, minfo->xfer_alloc_table);
	spin_unlock_irqrestore(&minfo->xfer_lock, flags);

	/* Increment the count for the next user to get through */
	up(&minfo->sem_xfer_count);
}

/**
 * ti_sci_do_xfer() - Do one transfer
 * @info:	Pointer to SCI entity information
 * @xfer:	Transfer to initiate and wait for response
 *
 * Return: -ETIMEDOUT in case of no response, if transmit error,
 *	   return corresponding error, else if all goes well,
 *	   return 0.
 */
static inline int ti_sci_do_xfer(struct ti_sci_info *info,
				 struct ti_sci_xfer *xfer)
{
	int ret;
	int timeout;
	struct device *dev = info->dev;
	bool done_state = true;

	ret = mbox_send_message(info->chan_tx, &xfer->tx_message);
	if (ret < 0)
		return ret;

	ret = 0;

	if (system_state <= SYSTEM_RUNNING) {
		/* And we wait for the response. */
		timeout = msecs_to_jiffies(info->desc->max_rx_timeout_ms);
		if (!wait_for_completion_timeout(&xfer->done, timeout))
			ret = -ETIMEDOUT;
	} else {
		/*
		 * If we are !running, we cannot use wait_for_completion_timeout
		 * during noirq phase, so we must manually poll the completion.
		 */
		ret = read_poll_timeout_atomic(try_wait_for_completion, done_state,
					       done_state, 1,
					       info->desc->max_rx_timeout_ms * 1000,
					       false, &xfer->done);
	}

	if (ret == -ETIMEDOUT)
		dev_err(dev, "Mbox timedout in resp(caller: %pS)\n",
			(void *)_RET_IP_);

	/*
	 * NOTE: we might prefer not to need the mailbox ticker to manage the
	 * transfer queueing since the protocol layer queues things by itself.
	 * Unfortunately, we have to kick the mailbox framework after we have
	 * received our message.
	 */
	mbox_client_txdone(info->chan_tx, ret);

	return ret;
}

/**
 * ti_sci_cmd_get_revision() - command to get the revision of the SCI entity
 * @info:	Pointer to SCI entity information
 *
 * Updates the SCI information in the internal data structure.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_get_revision(struct ti_sci_info *info)
{
	struct device *dev = info->dev;
	struct ti_sci_handle *handle = &info->handle;
	struct ti_sci_version_info *ver = &handle->version;
	struct ti_sci_msg_resp_version *rev_info;
	struct ti_sci_xfer *xfer;
	int ret;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_VERSION,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(struct ti_sci_msg_hdr),
				   sizeof(*rev_info));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}

	rev_info = (struct ti_sci_msg_resp_version *)xfer->xfer_buf;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	ver->abi_major = rev_info->abi_major;
	ver->abi_minor = rev_info->abi_minor;
	ver->firmware_revision = rev_info->firmware_revision;
	strncpy(ver->firmware_description, rev_info->firmware_description,
		sizeof(ver->firmware_description));

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);
	return ret;
}

/**
 * ti_sci_is_response_ack() - Generic ACK/NACK message checkup
 * @r:	pointer to response buffer
 *
 * Return: true if the response was an ACK, else returns false.
 */
static inline bool ti_sci_is_response_ack(void *r)
{
	struct ti_sci_msg_hdr *hdr = r;

	return hdr->flags & TI_SCI_FLAG_RESP_GENERIC_ACK ? true : false;
}

/**
 * ti_sci_set_device_state() - Set device state helper
 * @handle:	pointer to TI SCI handle
 * @id:		Device identifier
 * @flags:	flags to setup for the device
 * @state:	State to move the device to
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_set_device_state(const struct ti_sci_handle *handle,
				   u32 id, u32 flags, u8 state)
{
	struct ti_sci_info *info;
	struct ti_sci_msg_req_set_device_state *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_SET_DEVICE_STATE,
				   flags | TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_set_device_state *)xfer->xfer_buf;
	req->id = id;
	req->state = state;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->xfer_buf;

	ret = ti_sci_is_response_ack(resp) ? 0 : -ENODEV;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_get_device_state() - Get device state helper
 * @handle:	Handle to the device
 * @id:		Device Identifier
 * @clcnt:	Pointer to Context Loss Count
 * @resets:	pointer to resets
 * @p_state:	pointer to p_state
 * @c_state:	pointer to c_state
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_get_device_state(const struct ti_sci_handle *handle,
				   u32 id,  u32 *clcnt,  u32 *resets,
				    u8 *p_state,  u8 *c_state)
{
	struct ti_sci_info *info;
	struct ti_sci_msg_req_get_device_state *req;
	struct ti_sci_msg_resp_get_device_state *resp;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle)
		return -EINVAL;

	if (!clcnt && !resets && !p_state && !c_state)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_GET_DEVICE_STATE,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_get_device_state *)xfer->xfer_buf;
	req->id = id;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_resp_get_device_state *)xfer->xfer_buf;
	if (!ti_sci_is_response_ack(resp)) {
		ret = -ENODEV;
		goto fail;
	}

	if (clcnt)
		*clcnt = resp->context_loss_count;
	if (resets)
		*resets = resp->resets;
	if (p_state)
		*p_state = resp->programmed_state;
	if (c_state)
		*c_state = resp->current_state;
fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_get_device() - command to request for device managed by TISCI
 *			     that can be shared with other hosts.
 * @handle:	Pointer to TISCI handle as retrieved by *ti_sci_get_handle
 * @id:		Device Identifier
 *
 * Request for the device - NOTE: the client MUST maintain integrity of
 * usage count by balancing get_device with put_device. No refcounting is
 * managed by driver for that purpose.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_get_device(const struct ti_sci_handle *handle, u32 id)
{
	return ti_sci_set_device_state(handle, id, 0,
				       MSG_DEVICE_SW_STATE_ON);
}

/**
 * ti_sci_cmd_get_device_exclusive() - command to request for device managed by
 *				       TISCI that is exclusively owned by the
 *				       requesting host.
 * @handle:	Pointer to TISCI handle as retrieved by *ti_sci_get_handle
 * @id:		Device Identifier
 *
 * Request for the device - NOTE: the client MUST maintain integrity of
 * usage count by balancing get_device with put_device. No refcounting is
 * managed by driver for that purpose.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_get_device_exclusive(const struct ti_sci_handle *handle,
					   u32 id)
{
	return ti_sci_set_device_state(handle, id,
				       MSG_FLAG_DEVICE_EXCLUSIVE,
				       MSG_DEVICE_SW_STATE_ON);
}

/**
 * ti_sci_cmd_idle_device() - Command to idle a device managed by TISCI
 * @handle:	Pointer to TISCI handle as retrieved by *ti_sci_get_handle
 * @id:		Device Identifier
 *
 * Request for the device - NOTE: the client MUST maintain integrity of
 * usage count by balancing get_device with put_device. No refcounting is
 * managed by driver for that purpose.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_idle_device(const struct ti_sci_handle *handle, u32 id)
{
	return ti_sci_set_device_state(handle, id, 0,
				       MSG_DEVICE_SW_STATE_RETENTION);
}

/**
 * ti_sci_cmd_idle_device_exclusive() - Command to idle a device managed by
 *					TISCI that is exclusively owned by
 *					requesting host.
 * @handle:	Pointer to TISCI handle as retrieved by *ti_sci_get_handle
 * @id:		Device Identifier
 *
 * Request for the device - NOTE: the client MUST maintain integrity of
 * usage count by balancing get_device with put_device. No refcounting is
 * managed by driver for that purpose.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_idle_device_exclusive(const struct ti_sci_handle *handle,
					    u32 id)
{
	return ti_sci_set_device_state(handle, id,
				       MSG_FLAG_DEVICE_EXCLUSIVE,
				       MSG_DEVICE_SW_STATE_RETENTION);
}

/**
 * ti_sci_cmd_put_device() - command to release a device managed by TISCI
 * @handle:	Pointer to TISCI handle as retrieved by *ti_sci_get_handle
 * @id:		Device Identifier
 *
 * Request for the device - NOTE: the client MUST maintain integrity of
 * usage count by balancing get_device with put_device. No refcounting is
 * managed by driver for that purpose.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_put_device(const struct ti_sci_handle *handle, u32 id)
{
	return ti_sci_set_device_state(handle, id,
				       0, MSG_DEVICE_SW_STATE_AUTO_OFF);
}

/**
 * ti_sci_cmd_dev_is_valid() - Is the device valid
 * @handle:	Pointer to TISCI handle as retrieved by *ti_sci_get_handle
 * @id:		Device Identifier
 *
 * Return: 0 if all went fine and the device ID is valid, else return
 * appropriate error.
 */
static int ti_sci_cmd_dev_is_valid(const struct ti_sci_handle *handle, u32 id)
{
	u8 unused;

	/* check the device state which will also tell us if the ID is valid */
	return ti_sci_get_device_state(handle, id, NULL, NULL, NULL, &unused);
}

/**
 * ti_sci_cmd_dev_get_clcnt() - Get context loss counter
 * @handle:	Pointer to TISCI handle
 * @id:		Device Identifier
 * @count:	Pointer to Context Loss counter to populate
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_dev_get_clcnt(const struct ti_sci_handle *handle, u32 id,
				    u32 *count)
{
	return ti_sci_get_device_state(handle, id, count, NULL, NULL, NULL);
}

/**
 * ti_sci_cmd_dev_is_idle() - Check if the device is requested to be idle
 * @handle:	Pointer to TISCI handle
 * @id:		Device Identifier
 * @r_state:	true if requested to be idle
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_dev_is_idle(const struct ti_sci_handle *handle, u32 id,
				  bool *r_state)
{
	int ret;
	u8 state;

	if (!r_state)
		return -EINVAL;

	ret = ti_sci_get_device_state(handle, id, NULL, NULL, &state, NULL);
	if (ret)
		return ret;

	*r_state = (state == MSG_DEVICE_SW_STATE_RETENTION);

	return 0;
}

/**
 * ti_sci_cmd_dev_is_stop() - Check if the device is requested to be stopped
 * @handle:	Pointer to TISCI handle
 * @id:		Device Identifier
 * @r_state:	true if requested to be stopped
 * @curr_state:	true if currently stopped.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_dev_is_stop(const struct ti_sci_handle *handle, u32 id,
				  bool *r_state,  bool *curr_state)
{
	int ret;
	u8 p_state, c_state;

	if (!r_state && !curr_state)
		return -EINVAL;

	ret =
	    ti_sci_get_device_state(handle, id, NULL, NULL, &p_state, &c_state);
	if (ret)
		return ret;

	if (r_state)
		*r_state = (p_state == MSG_DEVICE_SW_STATE_AUTO_OFF);
	if (curr_state)
		*curr_state = (c_state == MSG_DEVICE_HW_STATE_OFF);

	return 0;
}

/**
 * ti_sci_cmd_dev_is_on() - Check if the device is requested to be ON
 * @handle:	Pointer to TISCI handle
 * @id:		Device Identifier
 * @r_state:	true if requested to be ON
 * @curr_state:	true if currently ON and active
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_dev_is_on(const struct ti_sci_handle *handle, u32 id,
				bool *r_state,  bool *curr_state)
{
	int ret;
	u8 p_state, c_state;

	if (!r_state && !curr_state)
		return -EINVAL;

	ret =
	    ti_sci_get_device_state(handle, id, NULL, NULL, &p_state, &c_state);
	if (ret)
		return ret;

	if (r_state)
		*r_state = (p_state == MSG_DEVICE_SW_STATE_ON);
	if (curr_state)
		*curr_state = (c_state == MSG_DEVICE_HW_STATE_ON);

	return 0;
}

/**
 * ti_sci_cmd_dev_is_trans() - Check if the device is currently transitioning
 * @handle:	Pointer to TISCI handle
 * @id:		Device Identifier
 * @curr_state:	true if currently transitioning.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_dev_is_trans(const struct ti_sci_handle *handle, u32 id,
				   bool *curr_state)
{
	int ret;
	u8 state;

	if (!curr_state)
		return -EINVAL;

	ret = ti_sci_get_device_state(handle, id, NULL, NULL, NULL, &state);
	if (ret)
		return ret;

	*curr_state = (state == MSG_DEVICE_HW_STATE_TRANS);

	return 0;
}

/**
 * ti_sci_cmd_set_device_resets() - command to set resets for device managed
 *				    by TISCI
 * @handle:	Pointer to TISCI handle as retrieved by *ti_sci_get_handle
 * @id:		Device Identifier
 * @reset_state: Device specific reset bit field
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_set_device_resets(const struct ti_sci_handle *handle,
					u32 id, u32 reset_state)
{
	struct ti_sci_info *info;
	struct ti_sci_msg_req_set_device_resets *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_SET_DEVICE_RESETS,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_set_device_resets *)xfer->xfer_buf;
	req->id = id;
	req->resets = reset_state;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->xfer_buf;

	ret = ti_sci_is_response_ack(resp) ? 0 : -ENODEV;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_get_device_resets() - Get reset state for device managed
 *				    by TISCI
 * @handle:		Pointer to TISCI handle
 * @id:			Device Identifier
 * @reset_state:	Pointer to reset state to populate
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_get_device_resets(const struct ti_sci_handle *handle,
					u32 id, u32 *reset_state)
{
	return ti_sci_get_device_state(handle, id, NULL, reset_state, NULL,
				       NULL);
}

/**
 * ti_sci_set_clock_state() - Set clock state helper
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @flags:	Header flags as needed
 * @state:	State to request for the clock.
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_set_clock_state(const struct ti_sci_handle *handle,
				  u32 dev_id, u32 clk_id,
				  u32 flags, u8 state)
{
	struct ti_sci_info *info;
	struct ti_sci_msg_req_set_clock_state *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_SET_CLOCK_STATE,
				   flags | TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_set_clock_state *)xfer->xfer_buf;
	req->dev_id = dev_id;
	if (clk_id < 255) {
		req->clk_id = clk_id;
	} else {
		req->clk_id = 255;
		req->clk_id_32 = clk_id;
	}
	req->request_state = state;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->xfer_buf;

	ret = ti_sci_is_response_ack(resp) ? 0 : -ENODEV;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_get_clock_state() - Get clock state helper
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @programmed_state:	State requested for clock to move to
 * @current_state:	State that the clock is currently in
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_get_clock_state(const struct ti_sci_handle *handle,
				      u32 dev_id, u32 clk_id,
				      u8 *programmed_state, u8 *current_state)
{
	struct ti_sci_info *info;
	struct ti_sci_msg_req_get_clock_state *req;
	struct ti_sci_msg_resp_get_clock_state *resp;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle)
		return -EINVAL;

	if (!programmed_state && !current_state)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_GET_CLOCK_STATE,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_get_clock_state *)xfer->xfer_buf;
	req->dev_id = dev_id;
	if (clk_id < 255) {
		req->clk_id = clk_id;
	} else {
		req->clk_id = 255;
		req->clk_id_32 = clk_id;
	}

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_resp_get_clock_state *)xfer->xfer_buf;

	if (!ti_sci_is_response_ack(resp)) {
		ret = -ENODEV;
		goto fail;
	}

	if (programmed_state)
		*programmed_state = resp->programmed_state;
	if (current_state)
		*current_state = resp->current_state;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_get_clock() - Get control of a clock from TI SCI
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @needs_ssc: 'true' if Spread Spectrum clock is desired, else 'false'
 * @can_change_freq: 'true' if frequency change is desired, else 'false'
 * @enable_input_term: 'true' if input termination is desired, else 'false'
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_get_clock(const struct ti_sci_handle *handle, u32 dev_id,
				u32 clk_id, bool needs_ssc,
				bool can_change_freq, bool enable_input_term)
{
	u32 flags = 0;

	flags |= needs_ssc ? MSG_FLAG_CLOCK_ALLOW_SSC : 0;
	flags |= can_change_freq ? MSG_FLAG_CLOCK_ALLOW_FREQ_CHANGE : 0;
	flags |= enable_input_term ? MSG_FLAG_CLOCK_INPUT_TERM : 0;

	return ti_sci_set_clock_state(handle, dev_id, clk_id, flags,
				      MSG_CLOCK_SW_STATE_REQ);
}

/**
 * ti_sci_cmd_idle_clock() - Idle a clock which is in our control
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 *
 * NOTE: This clock must have been requested by get_clock previously.
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_idle_clock(const struct ti_sci_handle *handle,
				 u32 dev_id, u32 clk_id)
{
	return ti_sci_set_clock_state(handle, dev_id, clk_id,
				      MSG_FLAG_CLOCK_ALLOW_FREQ_CHANGE,
				      MSG_CLOCK_SW_STATE_UNREQ);
}

/**
 * ti_sci_cmd_put_clock() - Release a clock from our control back to TISCI
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 *
 * NOTE: This clock must have been requested by get_clock previously.
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_put_clock(const struct ti_sci_handle *handle,
				u32 dev_id, u32 clk_id)
{
	return ti_sci_set_clock_state(handle, dev_id, clk_id,
				      MSG_FLAG_CLOCK_ALLOW_FREQ_CHANGE,
				      MSG_CLOCK_SW_STATE_AUTO);
}

/**
 * ti_sci_cmd_clk_is_auto() - Is the clock being auto managed
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @req_state: state indicating if the clock is auto managed
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_clk_is_auto(const struct ti_sci_handle *handle,
				  u32 dev_id, u32 clk_id, bool *req_state)
{
	u8 state = 0;
	int ret;

	if (!req_state)
		return -EINVAL;

	ret = ti_sci_cmd_get_clock_state(handle, dev_id, clk_id, &state, NULL);
	if (ret)
		return ret;

	*req_state = (state == MSG_CLOCK_SW_STATE_AUTO);
	return 0;
}

/**
 * ti_sci_cmd_clk_is_on() - Is the clock ON
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @req_state: state indicating if the clock is managed by us and enabled
 * @curr_state: state indicating if the clock is ready for operation
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_clk_is_on(const struct ti_sci_handle *handle, u32 dev_id,
				u32 clk_id, bool *req_state, bool *curr_state)
{
	u8 c_state = 0, r_state = 0;
	int ret;

	if (!req_state && !curr_state)
		return -EINVAL;

	ret = ti_sci_cmd_get_clock_state(handle, dev_id, clk_id,
					 &r_state, &c_state);
	if (ret)
		return ret;

	if (req_state)
		*req_state = (r_state == MSG_CLOCK_SW_STATE_REQ);
	if (curr_state)
		*curr_state = (c_state == MSG_CLOCK_HW_STATE_READY);
	return 0;
}

/**
 * ti_sci_cmd_clk_is_off() - Is the clock OFF
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @req_state: state indicating if the clock is managed by us and disabled
 * @curr_state: state indicating if the clock is NOT ready for operation
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_clk_is_off(const struct ti_sci_handle *handle, u32 dev_id,
				 u32 clk_id, bool *req_state, bool *curr_state)
{
	u8 c_state = 0, r_state = 0;
	int ret;

	if (!req_state && !curr_state)
		return -EINVAL;

	ret = ti_sci_cmd_get_clock_state(handle, dev_id, clk_id,
					 &r_state, &c_state);
	if (ret)
		return ret;

	if (req_state)
		*req_state = (r_state == MSG_CLOCK_SW_STATE_UNREQ);
	if (curr_state)
		*curr_state = (c_state == MSG_CLOCK_HW_STATE_NOT_READY);
	return 0;
}

/**
 * ti_sci_cmd_clk_set_parent() - Set the clock source of a specific device clock
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @parent_id:	Parent clock identifier to set
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_clk_set_parent(const struct ti_sci_handle *handle,
				     u32 dev_id, u32 clk_id, u32 parent_id)
{
	struct ti_sci_info *info;
	struct ti_sci_msg_req_set_clock_parent *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_SET_CLOCK_PARENT,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_set_clock_parent *)xfer->xfer_buf;
	req->dev_id = dev_id;
	if (clk_id < 255) {
		req->clk_id = clk_id;
	} else {
		req->clk_id = 255;
		req->clk_id_32 = clk_id;
	}
	if (parent_id < 255) {
		req->parent_id = parent_id;
	} else {
		req->parent_id = 255;
		req->parent_id_32 = parent_id;
	}

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->xfer_buf;

	ret = ti_sci_is_response_ack(resp) ? 0 : -ENODEV;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_clk_get_parent() - Get current parent clock source
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @parent_id:	Current clock parent
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_clk_get_parent(const struct ti_sci_handle *handle,
				     u32 dev_id, u32 clk_id, u32 *parent_id)
{
	struct ti_sci_info *info;
	struct ti_sci_msg_req_get_clock_parent *req;
	struct ti_sci_msg_resp_get_clock_parent *resp;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle || !parent_id)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_GET_CLOCK_PARENT,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_get_clock_parent *)xfer->xfer_buf;
	req->dev_id = dev_id;
	if (clk_id < 255) {
		req->clk_id = clk_id;
	} else {
		req->clk_id = 255;
		req->clk_id_32 = clk_id;
	}

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_resp_get_clock_parent *)xfer->xfer_buf;

	if (!ti_sci_is_response_ack(resp)) {
		ret = -ENODEV;
	} else {
		if (resp->parent_id < 255)
			*parent_id = resp->parent_id;
		else
			*parent_id = resp->parent_id_32;
	}

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_clk_get_num_parents() - Get num parents of the current clk source
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @num_parents: Returns he number of parents to the current clock.
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_clk_get_num_parents(const struct ti_sci_handle *handle,
					  u32 dev_id, u32 clk_id,
					  u32 *num_parents)
{
	struct ti_sci_info *info;
	struct ti_sci_msg_req_get_clock_num_parents *req;
	struct ti_sci_msg_resp_get_clock_num_parents *resp;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle || !num_parents)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_GET_NUM_CLOCK_PARENTS,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_get_clock_num_parents *)xfer->xfer_buf;
	req->dev_id = dev_id;
	if (clk_id < 255) {
		req->clk_id = clk_id;
	} else {
		req->clk_id = 255;
		req->clk_id_32 = clk_id;
	}

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_resp_get_clock_num_parents *)xfer->xfer_buf;

	if (!ti_sci_is_response_ack(resp)) {
		ret = -ENODEV;
	} else {
		if (resp->num_parents < 255)
			*num_parents = resp->num_parents;
		else
			*num_parents = resp->num_parents_32;
	}

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_clk_get_match_freq() - Find a good match for frequency
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @min_freq:	The minimum allowable frequency in Hz. This is the minimum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 * @target_freq: The target clock frequency in Hz. A frequency will be
 *		processed as close to this target frequency as possible.
 * @max_freq:	The maximum allowable frequency in Hz. This is the maximum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 * @match_freq:	Frequency match in Hz response.
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_clk_get_match_freq(const struct ti_sci_handle *handle,
					 u32 dev_id, u32 clk_id, u64 min_freq,
					 u64 target_freq, u64 max_freq,
					 u64 *match_freq)
{
	struct ti_sci_info *info;
	struct ti_sci_msg_req_query_clock_freq *req;
	struct ti_sci_msg_resp_query_clock_freq *resp;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle || !match_freq)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_QUERY_CLOCK_FREQ,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_query_clock_freq *)xfer->xfer_buf;
	req->dev_id = dev_id;
	if (clk_id < 255) {
		req->clk_id = clk_id;
	} else {
		req->clk_id = 255;
		req->clk_id_32 = clk_id;
	}
	req->min_freq_hz = min_freq;
	req->target_freq_hz = target_freq;
	req->max_freq_hz = max_freq;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_resp_query_clock_freq *)xfer->xfer_buf;

	if (!ti_sci_is_response_ack(resp))
		ret = -ENODEV;
	else
		*match_freq = resp->freq_hz;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_clk_set_freq() - Set a frequency for clock
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @min_freq:	The minimum allowable frequency in Hz. This is the minimum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 * @target_freq: The target clock frequency in Hz. A frequency will be
 *		processed as close to this target frequency as possible.
 * @max_freq:	The maximum allowable frequency in Hz. This is the maximum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_clk_set_freq(const struct ti_sci_handle *handle,
				   u32 dev_id, u32 clk_id, u64 min_freq,
				   u64 target_freq, u64 max_freq)
{
	struct ti_sci_info *info;
	struct ti_sci_msg_req_set_clock_freq *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_SET_CLOCK_FREQ,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_set_clock_freq *)xfer->xfer_buf;
	req->dev_id = dev_id;
	if (clk_id < 255) {
		req->clk_id = clk_id;
	} else {
		req->clk_id = 255;
		req->clk_id_32 = clk_id;
	}
	req->min_freq_hz = min_freq;
	req->target_freq_hz = target_freq;
	req->max_freq_hz = max_freq;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->xfer_buf;

	ret = ti_sci_is_response_ack(resp) ? 0 : -ENODEV;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_clk_get_freq() - Get current frequency
 * @handle:	pointer to TI SCI handle
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify.
 * @freq:	Currently frequency in Hz
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_clk_get_freq(const struct ti_sci_handle *handle,
				   u32 dev_id, u32 clk_id, u64 *freq)
{
	struct ti_sci_info *info;
	struct ti_sci_msg_req_get_clock_freq *req;
	struct ti_sci_msg_resp_get_clock_freq *resp;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle || !freq)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_GET_CLOCK_FREQ,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_get_clock_freq *)xfer->xfer_buf;
	req->dev_id = dev_id;
	if (clk_id < 255) {
		req->clk_id = clk_id;
	} else {
		req->clk_id = 255;
		req->clk_id_32 = clk_id;
	}

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_resp_get_clock_freq *)xfer->xfer_buf;

	if (!ti_sci_is_response_ack(resp))
		ret = -ENODEV;
	else
		*freq = resp->freq_hz;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

static int ti_sci_cmd_core_reboot(const struct ti_sci_handle *handle)
{
	struct ti_sci_info *info;
	struct ti_sci_msg_req_reboot *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_SYS_RESET,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_reboot *)xfer->xfer_buf;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->xfer_buf;

	if (!ti_sci_is_response_ack(resp))
		ret = -ENODEV;
	else
		ret = 0;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_get_resource_range - Helper to get a range of resources assigned
 *			       to a host. Resource is uniquely identified by
 *			       type and subtype.
 * @handle:		Pointer to TISCI handle.
 * @dev_id:		TISCI device ID.
 * @subtype:		Resource assignment subtype that is being requested
 *			from the given device.
 * @s_host:		Host processor ID to which the resources are allocated
 * @desc:		Pointer to ti_sci_resource_desc to be updated with the
 *			resource range start index and number of resources
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_get_resource_range(const struct ti_sci_handle *handle,
				     u32 dev_id, u8 subtype, u8 s_host,
				     struct ti_sci_resource_desc *desc)
{
	struct ti_sci_msg_resp_get_resource_range *resp;
	struct ti_sci_msg_req_get_resource_range *req;
	struct ti_sci_xfer *xfer;
	struct ti_sci_info *info;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle || !desc)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_GET_RESOURCE_RANGE,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}

	req = (struct ti_sci_msg_req_get_resource_range *)xfer->xfer_buf;
	req->secondary_host = s_host;
	req->type = dev_id & MSG_RM_RESOURCE_TYPE_MASK;
	req->subtype = subtype & MSG_RM_RESOURCE_SUBTYPE_MASK;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_resp_get_resource_range *)xfer->xfer_buf;

	if (!ti_sci_is_response_ack(resp)) {
		ret = -ENODEV;
	} else if (!resp->range_num && !resp->range_num_sec) {
		/* Neither of the two resource range is valid */
		ret = -ENODEV;
	} else {
		desc->start = resp->range_start;
		desc->num = resp->range_num;
		desc->start_sec = resp->range_start_sec;
		desc->num_sec = resp->range_num_sec;
	}

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_get_resource_range - Get a range of resources assigned to host
 *				   that is same as ti sci interface host.
 * @handle:		Pointer to TISCI handle.
 * @dev_id:		TISCI device ID.
 * @subtype:		Resource assignment subtype that is being requested
 *			from the given device.
 * @desc:		Pointer to ti_sci_resource_desc to be updated with the
 *			resource range start index and number of resources
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_get_resource_range(const struct ti_sci_handle *handle,
					 u32 dev_id, u8 subtype,
					 struct ti_sci_resource_desc *desc)
{
	return ti_sci_get_resource_range(handle, dev_id, subtype,
					 TI_SCI_IRQ_SECONDARY_HOST_INVALID,
					 desc);
}

/**
 * ti_sci_cmd_get_resource_range_from_shost - Get a range of resources
 *					      assigned to a specified host.
 * @handle:		Pointer to TISCI handle.
 * @dev_id:		TISCI device ID.
 * @subtype:		Resource assignment subtype that is being requested
 *			from the given device.
 * @s_host:		Host processor ID to which the resources are allocated
 * @desc:		Pointer to ti_sci_resource_desc to be updated with the
 *			resource range start index and number of resources
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static
int ti_sci_cmd_get_resource_range_from_shost(const struct ti_sci_handle *handle,
					     u32 dev_id, u8 subtype, u8 s_host,
					     struct ti_sci_resource_desc *desc)
{
	return ti_sci_get_resource_range(handle, dev_id, subtype, s_host, desc);
}

/**
 * ti_sci_manage_irq() - Helper api to configure/release the irq route between
 *			 the requested source and destination
 * @handle:		Pointer to TISCI handle.
 * @valid_params:	Bit fields defining the validity of certain params
 * @src_id:		Device ID of the IRQ source
 * @src_index:		IRQ source index within the source device
 * @dst_id:		Device ID of the IRQ destination
 * @dst_host_irq:	IRQ number of the destination device
 * @ia_id:		Device ID of the IA, if the IRQ flows through this IA
 * @vint:		Virtual interrupt to be used within the IA
 * @global_event:	Global event number to be used for the requesting event
 * @vint_status_bit:	Virtual interrupt status bit to be used for the event
 * @s_host:		Secondary host ID to which the irq/event is being
 *			requested for.
 * @type:		Request type irq set or release.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_manage_irq(const struct ti_sci_handle *handle,
			     u32 valid_params, u16 src_id, u16 src_index,
			     u16 dst_id, u16 dst_host_irq, u16 ia_id, u16 vint,
			     u16 global_event, u8 vint_status_bit, u8 s_host,
			     u16 type)
{
	struct ti_sci_msg_req_manage_irq *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_xfer *xfer;
	struct ti_sci_info *info;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, type, TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_manage_irq *)xfer->xfer_buf;
	req->valid_params = valid_params;
	req->src_id = src_id;
	req->src_index = src_index;
	req->dst_id = dst_id;
	req->dst_host_irq = dst_host_irq;
	req->ia_id = ia_id;
	req->vint = vint;
	req->global_event = global_event;
	req->vint_status_bit = vint_status_bit;
	req->secondary_host = s_host;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->xfer_buf;

	ret = ti_sci_is_response_ack(resp) ? 0 : -ENODEV;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_set_irq() - Helper api to configure the irq route between the
 *		      requested source and destination
 * @handle:		Pointer to TISCI handle.
 * @valid_params:	Bit fields defining the validity of certain params
 * @src_id:		Device ID of the IRQ source
 * @src_index:		IRQ source index within the source device
 * @dst_id:		Device ID of the IRQ destination
 * @dst_host_irq:	IRQ number of the destination device
 * @ia_id:		Device ID of the IA, if the IRQ flows through this IA
 * @vint:		Virtual interrupt to be used within the IA
 * @global_event:	Global event number to be used for the requesting event
 * @vint_status_bit:	Virtual interrupt status bit to be used for the event
 * @s_host:		Secondary host ID to which the irq/event is being
 *			requested for.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_set_irq(const struct ti_sci_handle *handle, u32 valid_params,
			  u16 src_id, u16 src_index, u16 dst_id,
			  u16 dst_host_irq, u16 ia_id, u16 vint,
			  u16 global_event, u8 vint_status_bit, u8 s_host)
{
	pr_debug("%s: IRQ set with valid_params = 0x%x from src = %d, index = %d, to dst = %d, irq = %d,via ia_id = %d, vint = %d, global event = %d,status_bit = %d\n",
		 __func__, valid_params, src_id, src_index,
		 dst_id, dst_host_irq, ia_id, vint, global_event,
		 vint_status_bit);

	return ti_sci_manage_irq(handle, valid_params, src_id, src_index,
				 dst_id, dst_host_irq, ia_id, vint,
				 global_event, vint_status_bit, s_host,
				 TI_SCI_MSG_SET_IRQ);
}

/**
 * ti_sci_free_irq() - Helper api to free the irq route between the
 *			   requested source and destination
 * @handle:		Pointer to TISCI handle.
 * @valid_params:	Bit fields defining the validity of certain params
 * @src_id:		Device ID of the IRQ source
 * @src_index:		IRQ source index within the source device
 * @dst_id:		Device ID of the IRQ destination
 * @dst_host_irq:	IRQ number of the destination device
 * @ia_id:		Device ID of the IA, if the IRQ flows through this IA
 * @vint:		Virtual interrupt to be used within the IA
 * @global_event:	Global event number to be used for the requesting event
 * @vint_status_bit:	Virtual interrupt status bit to be used for the event
 * @s_host:		Secondary host ID to which the irq/event is being
 *			requested for.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_free_irq(const struct ti_sci_handle *handle, u32 valid_params,
			   u16 src_id, u16 src_index, u16 dst_id,
			   u16 dst_host_irq, u16 ia_id, u16 vint,
			   u16 global_event, u8 vint_status_bit, u8 s_host)
{
	pr_debug("%s: IRQ release with valid_params = 0x%x from src = %d, index = %d, to dst = %d, irq = %d,via ia_id = %d, vint = %d, global event = %d,status_bit = %d\n",
		 __func__, valid_params, src_id, src_index,
		 dst_id, dst_host_irq, ia_id, vint, global_event,
		 vint_status_bit);

	return ti_sci_manage_irq(handle, valid_params, src_id, src_index,
				 dst_id, dst_host_irq, ia_id, vint,
				 global_event, vint_status_bit, s_host,
				 TI_SCI_MSG_FREE_IRQ);
}

/**
 * ti_sci_cmd_set_irq() - Configure a host irq route between the requested
 *			  source and destination.
 * @handle:		Pointer to TISCI handle.
 * @src_id:		Device ID of the IRQ source
 * @src_index:		IRQ source index within the source device
 * @dst_id:		Device ID of the IRQ destination
 * @dst_host_irq:	IRQ number of the destination device
 * @vint_irq:		Boolean specifying if this interrupt belongs to
 *			Interrupt Aggregator.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_set_irq(const struct ti_sci_handle *handle, u16 src_id,
			      u16 src_index, u16 dst_id, u16 dst_host_irq)
{
	u32 valid_params = MSG_FLAG_DST_ID_VALID | MSG_FLAG_DST_HOST_IRQ_VALID;

	return ti_sci_set_irq(handle, valid_params, src_id, src_index, dst_id,
			      dst_host_irq, 0, 0, 0, 0, 0);
}

/**
 * ti_sci_cmd_set_event_map() - Configure an event based irq route between the
 *				requested source and Interrupt Aggregator.
 * @handle:		Pointer to TISCI handle.
 * @src_id:		Device ID of the IRQ source
 * @src_index:		IRQ source index within the source device
 * @ia_id:		Device ID of the IA, if the IRQ flows through this IA
 * @vint:		Virtual interrupt to be used within the IA
 * @global_event:	Global event number to be used for the requesting event
 * @vint_status_bit:	Virtual interrupt status bit to be used for the event
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_set_event_map(const struct ti_sci_handle *handle,
				    u16 src_id, u16 src_index, u16 ia_id,
				    u16 vint, u16 global_event,
				    u8 vint_status_bit)
{
	u32 valid_params = MSG_FLAG_IA_ID_VALID | MSG_FLAG_VINT_VALID |
			   MSG_FLAG_GLB_EVNT_VALID |
			   MSG_FLAG_VINT_STS_BIT_VALID;

	return ti_sci_set_irq(handle, valid_params, src_id, src_index, 0, 0,
			      ia_id, vint, global_event, vint_status_bit, 0);
}

/**
 * ti_sci_cmd_free_irq() - Free a host irq route between the between the
 *			   requested source and destination.
 * @handle:		Pointer to TISCI handle.
 * @src_id:		Device ID of the IRQ source
 * @src_index:		IRQ source index within the source device
 * @dst_id:		Device ID of the IRQ destination
 * @dst_host_irq:	IRQ number of the destination device
 * @vint_irq:		Boolean specifying if this interrupt belongs to
 *			Interrupt Aggregator.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_free_irq(const struct ti_sci_handle *handle, u16 src_id,
			       u16 src_index, u16 dst_id, u16 dst_host_irq)
{
	u32 valid_params = MSG_FLAG_DST_ID_VALID | MSG_FLAG_DST_HOST_IRQ_VALID;

	return ti_sci_free_irq(handle, valid_params, src_id, src_index, dst_id,
			       dst_host_irq, 0, 0, 0, 0, 0);
}

/**
 * ti_sci_cmd_free_event_map() - Free an event map between the requested source
 *				 and Interrupt Aggregator.
 * @handle:		Pointer to TISCI handle.
 * @src_id:		Device ID of the IRQ source
 * @src_index:		IRQ source index within the source device
 * @ia_id:		Device ID of the IA, if the IRQ flows through this IA
 * @vint:		Virtual interrupt to be used within the IA
 * @global_event:	Global event number to be used for the requesting event
 * @vint_status_bit:	Virtual interrupt status bit to be used for the event
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int ti_sci_cmd_free_event_map(const struct ti_sci_handle *handle,
				     u16 src_id, u16 src_index, u16 ia_id,
				     u16 vint, u16 global_event,
				     u8 vint_status_bit)
{
	u32 valid_params = MSG_FLAG_IA_ID_VALID |
			   MSG_FLAG_VINT_VALID | MSG_FLAG_GLB_EVNT_VALID |
			   MSG_FLAG_VINT_STS_BIT_VALID;

	return ti_sci_free_irq(handle, valid_params, src_id, src_index, 0, 0,
			       ia_id, vint, global_event, vint_status_bit, 0);
}

/**
 * ti_sci_cmd_rm_ring_cfg() - Configure a NAVSS ring
 * @handle:	Pointer to TI SCI handle.
 * @params:	Pointer to ti_sci_msg_rm_ring_cfg ring config structure
 *
 * Return: 0 if all went well, else returns appropriate error value.
 *
 * See @ti_sci_msg_rm_ring_cfg and @ti_sci_msg_rm_ring_cfg_req for
 * more info.
 */
static int ti_sci_cmd_rm_ring_cfg(const struct ti_sci_handle *handle,
				  const struct ti_sci_msg_rm_ring_cfg *params)
{
	struct ti_sci_msg_rm_ring_cfg_req *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_xfer *xfer;
	struct ti_sci_info *info;
	struct device *dev;
	int ret = 0;

	if (IS_ERR_OR_NULL(handle))
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_RM_RING_CFG,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "RM_RA:Message config failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_rm_ring_cfg_req *)xfer->xfer_buf;
	req->valid_params = params->valid_params;
	req->nav_id = params->nav_id;
	req->index = params->index;
	req->addr_lo = params->addr_lo;
	req->addr_hi = params->addr_hi;
	req->count = params->count;
	req->mode = params->mode;
	req->size = params->size;
	req->order_id = params->order_id;
	req->virtid = params->virtid;
	req->asel = params->asel;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "RM_RA:Mbox config send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->xfer_buf;
	ret = ti_sci_is_response_ack(resp) ? 0 : -EINVAL;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);
	dev_dbg(dev, "RM_RA:config ring %u ret:%d\n", params->index, ret);
	return ret;
}

/**
 * ti_sci_cmd_rm_psil_pair() - Pair PSI-L source to destination thread
 * @handle:	Pointer to TI SCI handle.
 * @nav_id:	Device ID of Navigator Subsystem which should be used for
 *		pairing
 * @src_thread:	Source PSI-L thread ID
 * @dst_thread: Destination PSI-L thread ID
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_rm_psil_pair(const struct ti_sci_handle *handle,
				   u32 nav_id, u32 src_thread, u32 dst_thread)
{
	struct ti_sci_msg_psil_pair *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_xfer *xfer;
	struct ti_sci_info *info;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_RM_PSIL_PAIR,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "RM_PSIL:Message reconfig failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_psil_pair *)xfer->xfer_buf;
	req->nav_id = nav_id;
	req->src_thread = src_thread;
	req->dst_thread = dst_thread;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "RM_PSIL:Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->xfer_buf;
	ret = ti_sci_is_response_ack(resp) ? 0 : -EINVAL;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_rm_psil_unpair() - Unpair PSI-L source from destination thread
 * @handle:	Pointer to TI SCI handle.
 * @nav_id:	Device ID of Navigator Subsystem which should be used for
 *		unpairing
 * @src_thread:	Source PSI-L thread ID
 * @dst_thread:	Destination PSI-L thread ID
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_rm_psil_unpair(const struct ti_sci_handle *handle,
				     u32 nav_id, u32 src_thread, u32 dst_thread)
{
	struct ti_sci_msg_psil_unpair *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_xfer *xfer;
	struct ti_sci_info *info;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_RM_PSIL_UNPAIR,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "RM_PSIL:Message reconfig failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_psil_unpair *)xfer->xfer_buf;
	req->nav_id = nav_id;
	req->src_thread = src_thread;
	req->dst_thread = dst_thread;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "RM_PSIL:Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->xfer_buf;
	ret = ti_sci_is_response_ack(resp) ? 0 : -EINVAL;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_rm_udmap_tx_ch_cfg() - Configure a UDMAP TX channel
 * @handle:	Pointer to TI SCI handle.
 * @params:	Pointer to ti_sci_msg_rm_udmap_tx_ch_cfg TX channel config
 *		structure
 *
 * Return: 0 if all went well, else returns appropriate error value.
 *
 * See @ti_sci_msg_rm_udmap_tx_ch_cfg and @ti_sci_msg_rm_udmap_tx_ch_cfg_req for
 * more info.
 */
static int ti_sci_cmd_rm_udmap_tx_ch_cfg(const struct ti_sci_handle *handle,
			const struct ti_sci_msg_rm_udmap_tx_ch_cfg *params)
{
	struct ti_sci_msg_rm_udmap_tx_ch_cfg_req *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_xfer *xfer;
	struct ti_sci_info *info;
	struct device *dev;
	int ret = 0;

	if (IS_ERR_OR_NULL(handle))
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TISCI_MSG_RM_UDMAP_TX_CH_CFG,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message TX_CH_CFG alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_rm_udmap_tx_ch_cfg_req *)xfer->xfer_buf;
	req->valid_params = params->valid_params;
	req->nav_id = params->nav_id;
	req->index = params->index;
	req->tx_pause_on_err = params->tx_pause_on_err;
	req->tx_filt_einfo = params->tx_filt_einfo;
	req->tx_filt_pswords = params->tx_filt_pswords;
	req->tx_atype = params->tx_atype;
	req->tx_chan_type = params->tx_chan_type;
	req->tx_supr_tdpkt = params->tx_supr_tdpkt;
	req->tx_fetch_size = params->tx_fetch_size;
	req->tx_credit_count = params->tx_credit_count;
	req->txcq_qnum = params->txcq_qnum;
	req->tx_priority = params->tx_priority;
	req->tx_qos = params->tx_qos;
	req->tx_orderid = params->tx_orderid;
	req->fdepth = params->fdepth;
	req->tx_sched_priority = params->tx_sched_priority;
	req->tx_burst_size = params->tx_burst_size;
	req->tx_tdtype = params->tx_tdtype;
	req->extended_ch_type = params->extended_ch_type;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send TX_CH_CFG fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->xfer_buf;
	ret = ti_sci_is_response_ack(resp) ? 0 : -EINVAL;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);
	dev_dbg(dev, "TX_CH_CFG: chn %u ret:%u\n", params->index, ret);
	return ret;
}

/**
 * ti_sci_cmd_rm_udmap_rx_ch_cfg() - Configure a UDMAP RX channel
 * @handle:	Pointer to TI SCI handle.
 * @params:	Pointer to ti_sci_msg_rm_udmap_rx_ch_cfg RX channel config
 *		structure
 *
 * Return: 0 if all went well, else returns appropriate error value.
 *
 * See @ti_sci_msg_rm_udmap_rx_ch_cfg and @ti_sci_msg_rm_udmap_rx_ch_cfg_req for
 * more info.
 */
static int ti_sci_cmd_rm_udmap_rx_ch_cfg(const struct ti_sci_handle *handle,
			const struct ti_sci_msg_rm_udmap_rx_ch_cfg *params)
{
	struct ti_sci_msg_rm_udmap_rx_ch_cfg_req *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_xfer *xfer;
	struct ti_sci_info *info;
	struct device *dev;
	int ret = 0;

	if (IS_ERR_OR_NULL(handle))
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TISCI_MSG_RM_UDMAP_RX_CH_CFG,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message RX_CH_CFG alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_rm_udmap_rx_ch_cfg_req *)xfer->xfer_buf;
	req->valid_params = params->valid_params;
	req->nav_id = params->nav_id;
	req->index = params->index;
	req->rx_fetch_size = params->rx_fetch_size;
	req->rxcq_qnum = params->rxcq_qnum;
	req->rx_priority = params->rx_priority;
	req->rx_qos = params->rx_qos;
	req->rx_orderid = params->rx_orderid;
	req->rx_sched_priority = params->rx_sched_priority;
	req->flowid_start = params->flowid_start;
	req->flowid_cnt = params->flowid_cnt;
	req->rx_pause_on_err = params->rx_pause_on_err;
	req->rx_atype = params->rx_atype;
	req->rx_chan_type = params->rx_chan_type;
	req->rx_ignore_short = params->rx_ignore_short;
	req->rx_ignore_long = params->rx_ignore_long;
	req->rx_burst_size = params->rx_burst_size;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send RX_CH_CFG fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->xfer_buf;
	ret = ti_sci_is_response_ack(resp) ? 0 : -EINVAL;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);
	dev_dbg(dev, "RX_CH_CFG: chn %u ret:%d\n", params->index, ret);
	return ret;
}

/**
 * ti_sci_cmd_rm_udmap_rx_flow_cfg() - Configure UDMAP RX FLOW
 * @handle:	Pointer to TI SCI handle.
 * @params:	Pointer to ti_sci_msg_rm_udmap_flow_cfg RX FLOW config
 *		structure
 *
 * Return: 0 if all went well, else returns appropriate error value.
 *
 * See @ti_sci_msg_rm_udmap_flow_cfg and @ti_sci_msg_rm_udmap_flow_cfg_req for
 * more info.
 */
static int ti_sci_cmd_rm_udmap_rx_flow_cfg(const struct ti_sci_handle *handle,
			const struct ti_sci_msg_rm_udmap_flow_cfg *params)
{
	struct ti_sci_msg_rm_udmap_flow_cfg_req *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_xfer *xfer;
	struct ti_sci_info *info;
	struct device *dev;
	int ret = 0;

	if (IS_ERR_OR_NULL(handle))
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TISCI_MSG_RM_UDMAP_FLOW_CFG,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "RX_FL_CFG: Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_rm_udmap_flow_cfg_req *)xfer->xfer_buf;
	req->valid_params = params->valid_params;
	req->nav_id = params->nav_id;
	req->flow_index = params->flow_index;
	req->rx_einfo_present = params->rx_einfo_present;
	req->rx_psinfo_present = params->rx_psinfo_present;
	req->rx_error_handling = params->rx_error_handling;
	req->rx_desc_type = params->rx_desc_type;
	req->rx_sop_offset = params->rx_sop_offset;
	req->rx_dest_qnum = params->rx_dest_qnum;
	req->rx_src_tag_hi = params->rx_src_tag_hi;
	req->rx_src_tag_lo = params->rx_src_tag_lo;
	req->rx_dest_tag_hi = params->rx_dest_tag_hi;
	req->rx_dest_tag_lo = params->rx_dest_tag_lo;
	req->rx_src_tag_hi_sel = params->rx_src_tag_hi_sel;
	req->rx_src_tag_lo_sel = params->rx_src_tag_lo_sel;
	req->rx_dest_tag_hi_sel = params->rx_dest_tag_hi_sel;
	req->rx_dest_tag_lo_sel = params->rx_dest_tag_lo_sel;
	req->rx_fdq0_sz0_qnum = params->rx_fdq0_sz0_qnum;
	req->rx_fdq1_qnum = params->rx_fdq1_qnum;
	req->rx_fdq2_qnum = params->rx_fdq2_qnum;
	req->rx_fdq3_qnum = params->rx_fdq3_qnum;
	req->rx_ps_location = params->rx_ps_location;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "RX_FL_CFG: Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->xfer_buf;
	ret = ti_sci_is_response_ack(resp) ? 0 : -EINVAL;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);
	dev_dbg(info->dev, "RX_FL_CFG: %u ret:%d\n", params->flow_index, ret);
	return ret;
}

/**
 * ti_sci_cmd_proc_request() - Command to request a physical processor control
 * @handle:	Pointer to TI SCI handle
 * @proc_id:	Processor ID this request is for
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_proc_request(const struct ti_sci_handle *handle,
				   u8 proc_id)
{
	struct ti_sci_msg_req_proc_request *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_info *info;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (!handle)
		return -EINVAL;
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_PROC_REQUEST,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_proc_request *)xfer->xfer_buf;
	req->processor_id = proc_id;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->tx_message.buf;

	ret = ti_sci_is_response_ack(resp) ? 0 : -ENODEV;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_proc_release() - Command to release a physical processor control
 * @handle:	Pointer to TI SCI handle
 * @proc_id:	Processor ID this request is for
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_proc_release(const struct ti_sci_handle *handle,
				   u8 proc_id)
{
	struct ti_sci_msg_req_proc_release *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_info *info;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (!handle)
		return -EINVAL;
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_PROC_RELEASE,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_proc_release *)xfer->xfer_buf;
	req->processor_id = proc_id;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->tx_message.buf;

	ret = ti_sci_is_response_ack(resp) ? 0 : -ENODEV;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_proc_handover() - Command to handover a physical processor
 *				control to a host in the processor's access
 *				control list.
 * @handle:	Pointer to TI SCI handle
 * @proc_id:	Processor ID this request is for
 * @host_id:	Host ID to get the control of the processor
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_proc_handover(const struct ti_sci_handle *handle,
				    u8 proc_id, u8 host_id)
{
	struct ti_sci_msg_req_proc_handover *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_info *info;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (!handle)
		return -EINVAL;
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_PROC_HANDOVER,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_proc_handover *)xfer->xfer_buf;
	req->processor_id = proc_id;
	req->host_id = host_id;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->tx_message.buf;

	ret = ti_sci_is_response_ack(resp) ? 0 : -ENODEV;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_proc_set_config() - Command to set the processor boot
 *				    configuration flags
 * @handle:		Pointer to TI SCI handle
 * @proc_id:		Processor ID this request is for
 * @config_flags_set:	Configuration flags to be set
 * @config_flags_clear:	Configuration flags to be cleared.
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_proc_set_config(const struct ti_sci_handle *handle,
				      u8 proc_id, u64 bootvector,
				      u32 config_flags_set,
				      u32 config_flags_clear)
{
	struct ti_sci_msg_req_set_config *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_info *info;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (!handle)
		return -EINVAL;
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_SET_CONFIG,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_set_config *)xfer->xfer_buf;
	req->processor_id = proc_id;
	req->bootvector_low = bootvector & TI_SCI_ADDR_LOW_MASK;
	req->bootvector_high = (bootvector & TI_SCI_ADDR_HIGH_MASK) >>
				TI_SCI_ADDR_HIGH_SHIFT;
	req->config_flags_set = config_flags_set;
	req->config_flags_clear = config_flags_clear;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->tx_message.buf;

	ret = ti_sci_is_response_ack(resp) ? 0 : -ENODEV;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_proc_set_control() - Command to set the processor boot
 *				     control flags
 * @handle:			Pointer to TI SCI handle
 * @proc_id:			Processor ID this request is for
 * @control_flags_set:		Control flags to be set
 * @control_flags_clear:	Control flags to be cleared
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_proc_set_control(const struct ti_sci_handle *handle,
				       u8 proc_id, u32 control_flags_set,
				       u32 control_flags_clear)
{
	struct ti_sci_msg_req_set_ctrl *req;
	struct ti_sci_msg_hdr *resp;
	struct ti_sci_info *info;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (!handle)
		return -EINVAL;
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_SET_CTRL,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_set_ctrl *)xfer->xfer_buf;
	req->processor_id = proc_id;
	req->control_flags_set = control_flags_set;
	req->control_flags_clear = control_flags_clear;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_hdr *)xfer->tx_message.buf;

	ret = ti_sci_is_response_ack(resp) ? 0 : -ENODEV;

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/**
 * ti_sci_cmd_get_boot_status() - Command to get the processor boot status
 * @handle:	Pointer to TI SCI handle
 * @proc_id:	Processor ID this request is for
 *
 * Return: 0 if all went well, else returns appropriate error value.
 */
static int ti_sci_cmd_proc_get_status(const struct ti_sci_handle *handle,
				      u8 proc_id, u64 *bv, u32 *cfg_flags,
				      u32 *ctrl_flags, u32 *sts_flags)
{
	struct ti_sci_msg_resp_get_status *resp;
	struct ti_sci_msg_req_get_status *req;
	struct ti_sci_info *info;
	struct ti_sci_xfer *xfer;
	struct device *dev;
	int ret = 0;

	if (!handle)
		return -EINVAL;
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	info = handle_to_ti_sci_info(handle);
	dev = info->dev;

	xfer = ti_sci_get_one_xfer(info, TI_SCI_MSG_GET_STATUS,
				   TI_SCI_FLAG_REQ_ACK_ON_PROCESSED,
				   sizeof(*req), sizeof(*resp));
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "Message alloc failed(%d)\n", ret);
		return ret;
	}
	req = (struct ti_sci_msg_req_get_status *)xfer->xfer_buf;
	req->processor_id = proc_id;

	ret = ti_sci_do_xfer(info, xfer);
	if (ret) {
		dev_err(dev, "Mbox send fail %d\n", ret);
		goto fail;
	}

	resp = (struct ti_sci_msg_resp_get_status *)xfer->tx_message.buf;

	if (!ti_sci_is_response_ack(resp)) {
		ret = -ENODEV;
	} else {
		*bv = (resp->bootvector_low & TI_SCI_ADDR_LOW_MASK) |
		      (((u64)resp->bootvector_high << TI_SCI_ADDR_HIGH_SHIFT) &
		       TI_SCI_ADDR_HIGH_MASK);
		*cfg_flags = resp->config_flags;
		*ctrl_flags = resp->control_flags;
		*sts_flags = resp->status_flags;
	}

fail:
	ti_sci_put_one_xfer(&info->minfo, xfer);

	return ret;
}

/*
 * ti_sci_setup_ops() - Setup the operations structures
 * @info:	pointer to TISCI pointer
 */
static void ti_sci_setup_ops(struct ti_sci_info *info)
{
	struct ti_sci_ops *ops = &info->handle.ops;
	struct ti_sci_core_ops *core_ops = &ops->core_ops;
	struct ti_sci_dev_ops *dops = &ops->dev_ops;
	struct ti_sci_clk_ops *cops = &ops->clk_ops;
	struct ti_sci_rm_core_ops *rm_core_ops = &ops->rm_core_ops;
	struct ti_sci_rm_irq_ops *iops = &ops->rm_irq_ops;
	struct ti_sci_rm_ringacc_ops *rops = &ops->rm_ring_ops;
	struct ti_sci_rm_psil_ops *psilops = &ops->rm_psil_ops;
	struct ti_sci_rm_udmap_ops *udmap_ops = &ops->rm_udmap_ops;
	struct ti_sci_proc_ops *pops = &ops->proc_ops;

	core_ops->reboot_device = ti_sci_cmd_core_reboot;

	dops->get_device = ti_sci_cmd_get_device;
	dops->get_device_exclusive = ti_sci_cmd_get_device_exclusive;
	dops->idle_device = ti_sci_cmd_idle_device;
	dops->idle_device_exclusive = ti_sci_cmd_idle_device_exclusive;
	dops->put_device = ti_sci_cmd_put_device;

	dops->is_valid = ti_sci_cmd_dev_is_valid;
	dops->get_context_loss_count = ti_sci_cmd_dev_get_clcnt;
	dops->is_idle = ti_sci_cmd_dev_is_idle;
	dops->is_stop = ti_sci_cmd_dev_is_stop;
	dops->is_on = ti_sci_cmd_dev_is_on;
	dops->is_transitioning = ti_sci_cmd_dev_is_trans;
	dops->set_device_resets = ti_sci_cmd_set_device_resets;
	dops->get_device_resets = ti_sci_cmd_get_device_resets;

	cops->get_clock = ti_sci_cmd_get_clock;
	cops->idle_clock = ti_sci_cmd_idle_clock;
	cops->put_clock = ti_sci_cmd_put_clock;
	cops->is_auto = ti_sci_cmd_clk_is_auto;
	cops->is_on = ti_sci_cmd_clk_is_on;
	cops->is_off = ti_sci_cmd_clk_is_off;

	cops->set_parent = ti_sci_cmd_clk_set_parent;
	cops->get_parent = ti_sci_cmd_clk_get_parent;
	cops->get_num_parents = ti_sci_cmd_clk_get_num_parents;

	cops->get_best_match_freq = ti_sci_cmd_clk_get_match_freq;
	cops->set_freq = ti_sci_cmd_clk_set_freq;
	cops->get_freq = ti_sci_cmd_clk_get_freq;

	rm_core_ops->get_range = ti_sci_cmd_get_resource_range;
	rm_core_ops->get_range_from_shost =
				ti_sci_cmd_get_resource_range_from_shost;

	iops->set_irq = ti_sci_cmd_set_irq;
	iops->set_event_map = ti_sci_cmd_set_event_map;
	iops->free_irq = ti_sci_cmd_free_irq;
	iops->free_event_map = ti_sci_cmd_free_event_map;

	rops->set_cfg = ti_sci_cmd_rm_ring_cfg;

	psilops->pair = ti_sci_cmd_rm_psil_pair;
	psilops->unpair = ti_sci_cmd_rm_psil_unpair;

	udmap_ops->tx_ch_cfg = ti_sci_cmd_rm_udmap_tx_ch_cfg;
	udmap_ops->rx_ch_cfg = ti_sci_cmd_rm_udmap_rx_ch_cfg;
	udmap_ops->rx_flow_cfg = ti_sci_cmd_rm_udmap_rx_flow_cfg;

	pops->request = ti_sci_cmd_proc_request;
	pops->release = ti_sci_cmd_proc_release;
	pops->handover = ti_sci_cmd_proc_handover;
	pops->set_config = ti_sci_cmd_proc_set_config;
	pops->set_control = ti_sci_cmd_proc_set_control;
	pops->get_status = ti_sci_cmd_proc_get_status;
}

/**
 * ti_sci_get_handle() - Get the TI SCI handle for a device
 * @dev:	Pointer to device for which we want SCI handle
 *
 * NOTE: The function does not track individual clients of the framework
 * and is expected to be maintained by caller of TI SCI protocol library.
 * ti_sci_put_handle must be balanced with successful ti_sci_get_handle
 * Return: pointer to handle if successful, else:
 * -EPROBE_DEFER if the instance is not ready
 * -ENODEV if the required node handler is missing
 * -EINVAL if invalid conditions are encountered.
 */
const struct ti_sci_handle *ti_sci_get_handle(struct device *dev)
{
	struct device_node *ti_sci_np;
	struct list_head *p;
	struct ti_sci_handle *handle = NULL;
	struct ti_sci_info *info;

	if (!dev) {
		pr_err("I need a device pointer\n");
		return ERR_PTR(-EINVAL);
	}
	ti_sci_np = of_get_parent(dev->of_node);
	if (!ti_sci_np) {
		dev_err(dev, "No OF information\n");
		return ERR_PTR(-EINVAL);
	}

	mutex_lock(&ti_sci_list_mutex);
	list_for_each(p, &ti_sci_list) {
		info = list_entry(p, struct ti_sci_info, node);
		if (ti_sci_np == info->dev->of_node) {
			handle = &info->handle;
			info->users++;
			break;
		}
	}
	mutex_unlock(&ti_sci_list_mutex);
	of_node_put(ti_sci_np);

	if (!handle)
		return ERR_PTR(-EPROBE_DEFER);

	return handle;
}
EXPORT_SYMBOL_GPL(ti_sci_get_handle);

/**
 * ti_sci_put_handle() - Release the handle acquired by ti_sci_get_handle
 * @handle:	Handle acquired by ti_sci_get_handle
 *
 * NOTE: The function does not track individual clients of the framework
 * and is expected to be maintained by caller of TI SCI protocol library.
 * ti_sci_put_handle must be balanced with successful ti_sci_get_handle
 *
 * Return: 0 is successfully released
 * if an error pointer was passed, it returns the error value back,
 * if null was passed, it returns -EINVAL;
 */
int ti_sci_put_handle(const struct ti_sci_handle *handle)
{
	struct ti_sci_info *info;

	if (IS_ERR(handle))
		return PTR_ERR(handle);
	if (!handle)
		return -EINVAL;

	info = handle_to_ti_sci_info(handle);
	mutex_lock(&ti_sci_list_mutex);
	if (!WARN_ON(!info->users))
		info->users--;
	mutex_unlock(&ti_sci_list_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(ti_sci_put_handle);

static void devm_ti_sci_release(struct device *dev, void *res)
{
	const struct ti_sci_handle **ptr = res;
	const struct ti_sci_handle *handle = *ptr;
	int ret;

	ret = ti_sci_put_handle(handle);
	if (ret)
		dev_err(dev, "failed to put handle %d\n", ret);
}

/**
 * devm_ti_sci_get_handle() - Managed get handle
 * @dev:	device for which we want SCI handle for.
 *
 * NOTE: This releases the handle once the device resources are
 * no longer needed. MUST NOT BE released with ti_sci_put_handle.
 * The function does not track individual clients of the framework
 * and is expected to be maintained by caller of TI SCI protocol library.
 *
 * Return: 0 if all went fine, else corresponding error.
 */
const struct ti_sci_handle *devm_ti_sci_get_handle(struct device *dev)
{
	const struct ti_sci_handle **ptr;
	const struct ti_sci_handle *handle;

	ptr = devres_alloc(devm_ti_sci_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);
	handle = ti_sci_get_handle(dev);

	if (!IS_ERR(handle)) {
		*ptr = handle;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return handle;
}
EXPORT_SYMBOL_GPL(devm_ti_sci_get_handle);

/**
 * ti_sci_get_by_phandle() - Get the TI SCI handle using DT phandle
 * @np:		device node
 * @property:	property name containing phandle on TISCI node
 *
 * NOTE: The function does not track individual clients of the framework
 * and is expected to be maintained by caller of TI SCI protocol library.
 * ti_sci_put_handle must be balanced with successful ti_sci_get_by_phandle
 * Return: pointer to handle if successful, else:
 * -EPROBE_DEFER if the instance is not ready
 * -ENODEV if the required node handler is missing
 * -EINVAL if invalid conditions are encountered.
 */
const struct ti_sci_handle *ti_sci_get_by_phandle(struct device_node *np,
						  const char *property)
{
	struct ti_sci_handle *handle = NULL;
	struct device_node *ti_sci_np;
	struct ti_sci_info *info;
	struct list_head *p;

	if (!np) {
		pr_err("I need a device pointer\n");
		return ERR_PTR(-EINVAL);
	}

	ti_sci_np = of_parse_phandle(np, property, 0);
	if (!ti_sci_np)
		return ERR_PTR(-ENODEV);

	mutex_lock(&ti_sci_list_mutex);
	list_for_each(p, &ti_sci_list) {
		info = list_entry(p, struct ti_sci_info, node);
		if (ti_sci_np == info->dev->of_node) {
			handle = &info->handle;
			info->users++;
			break;
		}
	}
	mutex_unlock(&ti_sci_list_mutex);
	of_node_put(ti_sci_np);

	if (!handle)
		return ERR_PTR(-EPROBE_DEFER);

	return handle;
}
EXPORT_SYMBOL_GPL(ti_sci_get_by_phandle);

/**
 * devm_ti_sci_get_by_phandle() - Managed get handle using phandle
 * @dev:	Device pointer requesting TISCI handle
 * @property:	property name containing phandle on TISCI node
 *
 * NOTE: This releases the handle once the device resources are
 * no longer needed. MUST NOT BE released with ti_sci_put_handle.
 * The function does not track individual clients of the framework
 * and is expected to be maintained by caller of TI SCI protocol library.
 *
 * Return: 0 if all went fine, else corresponding error.
 */
const struct ti_sci_handle *devm_ti_sci_get_by_phandle(struct device *dev,
						       const char *property)
{
	const struct ti_sci_handle *handle;
	const struct ti_sci_handle **ptr;

	ptr = devres_alloc(devm_ti_sci_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);
	handle = ti_sci_get_by_phandle(dev_of_node(dev), property);

	if (!IS_ERR(handle)) {
		*ptr = handle;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return handle;
}
EXPORT_SYMBOL_GPL(devm_ti_sci_get_by_phandle);

/**
 * ti_sci_get_free_resource() - Get a free resource from TISCI resource.
 * @res:	Pointer to the TISCI resource
 *
 * Return: resource num if all went ok else TI_SCI_RESOURCE_NULL.
 */
u16 ti_sci_get_free_resource(struct ti_sci_resource *res)
{
	unsigned long flags;
	u16 set, free_bit;

	raw_spin_lock_irqsave(&res->lock, flags);
	for (set = 0; set < res->sets; set++) {
		struct ti_sci_resource_desc *desc = &res->desc[set];
		int res_count = desc->num + desc->num_sec;

		free_bit = find_first_zero_bit(desc->res_map, res_count);
		if (free_bit != res_count) {
			set_bit(free_bit, desc->res_map);
			raw_spin_unlock_irqrestore(&res->lock, flags);

			if (desc->num && free_bit < desc->num)
				return desc->start + free_bit;
			else
				return desc->start_sec + free_bit;
		}
	}
	raw_spin_unlock_irqrestore(&res->lock, flags);

	return TI_SCI_RESOURCE_NULL;
}
EXPORT_SYMBOL_GPL(ti_sci_get_free_resource);

/**
 * ti_sci_release_resource() - Release a resource from TISCI resource.
 * @res:	Pointer to the TISCI resource
 * @id:		Resource id to be released.
 */
void ti_sci_release_resource(struct ti_sci_resource *res, u16 id)
{
	unsigned long flags;
	u16 set;

	raw_spin_lock_irqsave(&res->lock, flags);
	for (set = 0; set < res->sets; set++) {
		struct ti_sci_resource_desc *desc = &res->desc[set];

		if (desc->num && desc->start <= id &&
		    (desc->start + desc->num) > id)
			clear_bit(id - desc->start, desc->res_map);
		else if (desc->num_sec && desc->start_sec <= id &&
			 (desc->start_sec + desc->num_sec) > id)
			clear_bit(id - desc->start_sec, desc->res_map);
	}
	raw_spin_unlock_irqrestore(&res->lock, flags);
}
EXPORT_SYMBOL_GPL(ti_sci_release_resource);

/**
 * ti_sci_get_num_resources() - Get the number of resources in TISCI resource
 * @res:	Pointer to the TISCI resource
 *
 * Return: Total number of available resources.
 */
u32 ti_sci_get_num_resources(struct ti_sci_resource *res)
{
	u32 set, count = 0;

	for (set = 0; set < res->sets; set++)
		count += res->desc[set].num + res->desc[set].num_sec;

	return count;
}
EXPORT_SYMBOL_GPL(ti_sci_get_num_resources);

/**
 * devm_ti_sci_get_resource_sets() - Get a TISCI resources assigned to a device
 * @handle:	TISCI handle
 * @dev:	Device pointer to which the resource is assigned
 * @dev_id:	TISCI device id to which the resource is assigned
 * @sub_types:	Array of sub_types assigned corresponding to device
 * @sets:	Number of sub_types
 *
 * Return: Pointer to ti_sci_resource if all went well else appropriate
 *	   error pointer.
 */
static struct ti_sci_resource *
devm_ti_sci_get_resource_sets(const struct ti_sci_handle *handle,
			      struct device *dev, u32 dev_id, u32 *sub_types,
			      u32 sets)
{
	struct ti_sci_resource *res;
	bool valid_set = false;
	int i, ret, res_count;

	res = devm_kzalloc(dev, sizeof(*res), GFP_KERNEL);
	if (!res)
		return ERR_PTR(-ENOMEM);

	res->sets = sets;
	res->desc = devm_kcalloc(dev, res->sets, sizeof(*res->desc),
				 GFP_KERNEL);
	if (!res->desc)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < res->sets; i++) {
		ret = handle->ops.rm_core_ops.get_range(handle, dev_id,
							sub_types[i],
							&res->desc[i]);
		if (ret) {
			dev_dbg(dev, "dev = %d subtype %d not allocated for this host\n",
				dev_id, sub_types[i]);
			memset(&res->desc[i], 0, sizeof(res->desc[i]));
			continue;
		}

		dev_dbg(dev, "dev/sub_type: %d/%d, start/num: %d/%d | %d/%d\n",
			dev_id, sub_types[i], res->desc[i].start,
			res->desc[i].num, res->desc[i].start_sec,
			res->desc[i].num_sec);

		valid_set = true;
		res_count = res->desc[i].num + res->desc[i].num_sec;
		res->desc[i].res_map =
			devm_kzalloc(dev, BITS_TO_LONGS(res_count) *
				     sizeof(*res->desc[i].res_map), GFP_KERNEL);
		if (!res->desc[i].res_map)
			return ERR_PTR(-ENOMEM);
	}
	raw_spin_lock_init(&res->lock);

	if (valid_set)
		return res;

	return ERR_PTR(-EINVAL);
}

/**
 * devm_ti_sci_get_of_resource() - Get a TISCI resource assigned to a device
 * @handle:	TISCI handle
 * @dev:	Device pointer to which the resource is assigned
 * @dev_id:	TISCI device id to which the resource is assigned
 * @of_prop:	property name by which the resource are represented
 *
 * Return: Pointer to ti_sci_resource if all went well else appropriate
 *	   error pointer.
 */
struct ti_sci_resource *
devm_ti_sci_get_of_resource(const struct ti_sci_handle *handle,
			    struct device *dev, u32 dev_id, char *of_prop)
{
	struct ti_sci_resource *res;
	u32 *sub_types;
	int sets;

	sets = of_property_count_elems_of_size(dev_of_node(dev), of_prop,
					       sizeof(u32));
	if (sets < 0) {
		dev_err(dev, "%s resource type ids not available\n", of_prop);
		return ERR_PTR(sets);
	}

	sub_types = kcalloc(sets, sizeof(*sub_types), GFP_KERNEL);
	if (!sub_types)
		return ERR_PTR(-ENOMEM);

	of_property_read_u32_array(dev_of_node(dev), of_prop, sub_types, sets);
	res = devm_ti_sci_get_resource_sets(handle, dev, dev_id, sub_types,
					    sets);

	kfree(sub_types);
	return res;
}
EXPORT_SYMBOL_GPL(devm_ti_sci_get_of_resource);

/**
 * devm_ti_sci_get_resource() - Get a resource range assigned to the device
 * @handle:	TISCI handle
 * @dev:	Device pointer to which the resource is assigned
 * @dev_id:	TISCI device id to which the resource is assigned
 * @suub_type:	TISCI resource subytpe representing the resource.
 *
 * Return: Pointer to ti_sci_resource if all went well else appropriate
 *	   error pointer.
 */
struct ti_sci_resource *
devm_ti_sci_get_resource(const struct ti_sci_handle *handle, struct device *dev,
			 u32 dev_id, u32 sub_type)
{
	return devm_ti_sci_get_resource_sets(handle, dev, dev_id, &sub_type, 1);
}
EXPORT_SYMBOL_GPL(devm_ti_sci_get_resource);

static int tisci_reboot_handler(struct notifier_block *nb, unsigned long mode,
				void *cmd)
{
	struct ti_sci_info *info = reboot_to_ti_sci_info(nb);
	const struct ti_sci_handle *handle = &info->handle;

	ti_sci_cmd_core_reboot(handle);

	/* call fail OR pass, we should not be here in the first place */
	return NOTIFY_BAD;
}

/* Description for K2G */
static const struct ti_sci_desc ti_sci_pmmc_k2g_desc = {
	.default_host_id = 2,
	/* Conservative duration */
	.max_rx_timeout_ms = 1000,
	/* Limited by MBOX_TX_QUEUE_LEN. K2G can handle upto 128 messages! */
	.max_msgs = 20,
	.max_msg_size = 64,
};

/* Description for AM654 */
static const struct ti_sci_desc ti_sci_pmmc_am654_desc = {
	.default_host_id = 12,
	/* Conservative duration */
	.max_rx_timeout_ms = 10000,
	/* Limited by MBOX_TX_QUEUE_LEN. K2G can handle upto 128 messages! */
	.max_msgs = 20,
	.max_msg_size = 60,
};

static const struct of_device_id ti_sci_of_match[] = {
	{.compatible = "ti,k2g-sci", .data = &ti_sci_pmmc_k2g_desc},
	{.compatible = "ti,am654-sci", .data = &ti_sci_pmmc_am654_desc},
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, ti_sci_of_match);

static int ti_sci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *of_id;
	const struct ti_sci_desc *desc;
	struct ti_sci_xfer *xfer;
	struct ti_sci_info *info = NULL;
	struct ti_sci_xfers_info *minfo;
	struct mbox_client *cl;
	int ret = -EINVAL;
	int i;
	int reboot = 0;
	u32 h_id;

	of_id = of_match_device(ti_sci_of_match, dev);
	if (!of_id) {
		dev_err(dev, "OF data missing\n");
		return -EINVAL;
	}
	desc = of_id->data;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;
	info->desc = desc;
	ret = of_property_read_u32(dev->of_node, "ti,host-id", &h_id);
	/* if the property is not present in DT, use a default from desc */
	if (ret < 0) {
		info->host_id = info->desc->default_host_id;
	} else {
		if (!h_id) {
			dev_warn(dev, "Host ID 0 is reserved for firmware\n");
			info->host_id = info->desc->default_host_id;
		} else {
			info->host_id = h_id;
		}
	}

	reboot = of_property_read_bool(dev->of_node,
				       "ti,system-reboot-controller");
	INIT_LIST_HEAD(&info->node);
	minfo = &info->minfo;

	/*
	 * Pre-allocate messages
	 * NEVER allocate more than what we can indicate in hdr.seq
	 * if we have data description bug, force a fix..
	 */
	if (WARN_ON(desc->max_msgs >=
		    1 << 8 * sizeof(((struct ti_sci_msg_hdr *)0)->seq)))
		return -EINVAL;

	minfo->xfer_block = devm_kcalloc(dev,
					 desc->max_msgs,
					 sizeof(*minfo->xfer_block),
					 GFP_KERNEL);
	if (!minfo->xfer_block)
		return -ENOMEM;

	minfo->xfer_alloc_table = devm_kcalloc(dev,
					       BITS_TO_LONGS(desc->max_msgs),
					       sizeof(unsigned long),
					       GFP_KERNEL);
	if (!minfo->xfer_alloc_table)
		return -ENOMEM;
	bitmap_zero(minfo->xfer_alloc_table, desc->max_msgs);

	/* Pre-initialize the buffer pointer to pre-allocated buffers */
	for (i = 0, xfer = minfo->xfer_block; i < desc->max_msgs; i++, xfer++) {
		xfer->xfer_buf = devm_kcalloc(dev, 1, desc->max_msg_size,
					      GFP_KERNEL);
		if (!xfer->xfer_buf)
			return -ENOMEM;

		xfer->tx_message.buf = xfer->xfer_buf;
		init_completion(&xfer->done);
	}

	ret = ti_sci_debugfs_create(pdev, info);
	if (ret)
		dev_warn(dev, "Failed to create debug file\n");

	platform_set_drvdata(pdev, info);

	cl = &info->cl;
	cl->dev = dev;
	cl->tx_block = false;
	cl->rx_callback = ti_sci_rx_callback;
	cl->knows_txdone = true;

	spin_lock_init(&minfo->xfer_lock);
	sema_init(&minfo->sem_xfer_count, desc->max_msgs);

	info->chan_rx = mbox_request_channel_byname(cl, "rx");
	if (IS_ERR(info->chan_rx)) {
		ret = PTR_ERR(info->chan_rx);
		goto out;
	}

	info->chan_tx = mbox_request_channel_byname(cl, "tx");
	if (IS_ERR(info->chan_tx)) {
		ret = PTR_ERR(info->chan_tx);
		goto out;
	}
	ret = ti_sci_cmd_get_revision(info);
	if (ret) {
		dev_err(dev, "Unable to communicate with TISCI(%d)\n", ret);
		goto out;
	}

	ti_sci_setup_ops(info);

	if (reboot) {
		info->nb.notifier_call = tisci_reboot_handler;
		info->nb.priority = 128;

		ret = register_restart_handler(&info->nb);
		if (ret) {
			dev_err(dev, "reboot registration fail(%d)\n", ret);
			goto out;
		}
	}

	dev_info(dev, "ABI: %d.%d (firmware rev 0x%04x '%s')\n",
		 info->handle.version.abi_major, info->handle.version.abi_minor,
		 info->handle.version.firmware_revision,
		 info->handle.version.firmware_description);

	mutex_lock(&ti_sci_list_mutex);
	list_add_tail(&info->node, &ti_sci_list);
	mutex_unlock(&ti_sci_list_mutex);

	return of_platform_populate(dev->of_node, NULL, NULL, dev);
out:
	if (!IS_ERR(info->chan_tx))
		mbox_free_channel(info->chan_tx);
	if (!IS_ERR(info->chan_rx))
		mbox_free_channel(info->chan_rx);
	debugfs_remove(info->d);
	return ret;
}

static int ti_sci_remove(struct platform_device *pdev)
{
	struct ti_sci_info *info;
	struct device *dev = &pdev->dev;
	int ret = 0;

	of_platform_depopulate(dev);

	info = platform_get_drvdata(pdev);

	if (info->nb.notifier_call)
		unregister_restart_handler(&info->nb);

	mutex_lock(&ti_sci_list_mutex);
	if (info->users)
		ret = -EBUSY;
	else
		list_del(&info->node);
	mutex_unlock(&ti_sci_list_mutex);

	if (!ret) {
		ti_sci_debugfs_destroy(pdev, info);

		/* Safe to free channels since no more users */
		mbox_free_channel(info->chan_tx);
		mbox_free_channel(info->chan_rx);
	}

	return ret;
}

static struct platform_driver ti_sci_driver = {
	.probe = ti_sci_probe,
	.remove = ti_sci_remove,
	.driver = {
		   .name = "ti-sci",
		   .of_match_table = of_match_ptr(ti_sci_of_match),
	},
};
module_platform_driver(ti_sci_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI System Control Interface(SCI) driver");
MODULE_AUTHOR("Nishanth Menon");
MODULE_ALIAS("platform:ti-sci");
