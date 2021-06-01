// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Message Protocol driver
 *
 * SCMI Message Protocol is used between the System Control Processor(SCP)
 * and the Application Processors(AP). The Message Handling Unit(MHU)
 * provides a mechanism for inter-processor communication between SCP's
 * Cortex M3 and AP.
 *
 * SCP offers control and management of the core/cluster power states,
 * various power domain DVFS including the core/cluster, certain system
 * clocks configuration, thermal sensors and many others.
 *
 * Copyright (C) 2018 ARM Ltd.
 */

#include <linux/bitmap.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/processor.h>
#include <linux/slab.h>

#include "common.h"
#include "notify.h"

#define CREATE_TRACE_POINTS
#include <trace/events/scmi.h>

enum scmi_error_codes {
	SCMI_SUCCESS = 0,	/* Success */
	SCMI_ERR_SUPPORT = -1,	/* Not supported */
	SCMI_ERR_PARAMS = -2,	/* Invalid Parameters */
	SCMI_ERR_ACCESS = -3,	/* Invalid access/permission denied */
	SCMI_ERR_ENTRY = -4,	/* Not found */
	SCMI_ERR_RANGE = -5,	/* Value out of range */
	SCMI_ERR_BUSY = -6,	/* Device busy */
	SCMI_ERR_COMMS = -7,	/* Communication Error */
	SCMI_ERR_GENERIC = -8,	/* Generic Error */
	SCMI_ERR_HARDWARE = -9,	/* Hardware Error */
	SCMI_ERR_PROTOCOL = -10,/* Protocol Error */
	SCMI_ERR_MAX
};

/* List of all SCMI devices active in system */
static LIST_HEAD(scmi_list);
/* Protection for the entire list */
static DEFINE_MUTEX(scmi_list_mutex);
/* Track the unique id for the transfers for debug & profiling purpose */
static atomic_t transfer_last_id;

/**
 * struct scmi_xfers_info - Structure to manage transfer information
 *
 * @xfer_block: Preallocated Message array
 * @xfer_alloc_table: Bitmap table for allocated messages.
 *	Index of this bitmap table is also used for message
 *	sequence identifier.
 * @xfer_lock: Protection for message allocation
 */
struct scmi_xfers_info {
	struct scmi_xfer *xfer_block;
	unsigned long *xfer_alloc_table;
	spinlock_t xfer_lock;
};

/**
 * struct scmi_info - Structure representing a SCMI instance
 *
 * @dev: Device pointer
 * @desc: SoC description for this instance
 * @version: SCMI revision information containing protocol version,
 *	implementation version and (sub-)vendor identification.
 * @handle: Instance of SCMI handle to send to clients
 * @tx_minfo: Universal Transmit Message management info
 * @rx_minfo: Universal Receive Message management info
 * @tx_idr: IDR object to map protocol id to Tx channel info pointer
 * @rx_idr: IDR object to map protocol id to Rx channel info pointer
 * @protocols_imp: List of protocols implemented, currently maximum of
 *	MAX_PROTOCOLS_IMP elements allocated by the base protocol
 * @node: List head
 * @users: Number of users of this instance
 */
struct scmi_info {
	struct device *dev;
	const struct scmi_desc *desc;
	struct scmi_revision_info version;
	struct scmi_handle handle;
	struct scmi_xfers_info tx_minfo;
	struct scmi_xfers_info rx_minfo;
	struct idr tx_idr;
	struct idr rx_idr;
	u8 *protocols_imp;
	struct list_head node;
	int users;
};

#define handle_to_scmi_info(h)	container_of(h, struct scmi_info, handle)

static const int scmi_linux_errmap[] = {
	/* better than switch case as long as return value is continuous */
	0,			/* SCMI_SUCCESS */
	-EOPNOTSUPP,		/* SCMI_ERR_SUPPORT */
	-EINVAL,		/* SCMI_ERR_PARAM */
	-EACCES,		/* SCMI_ERR_ACCESS */
	-ENOENT,		/* SCMI_ERR_ENTRY */
	-ERANGE,		/* SCMI_ERR_RANGE */
	-EBUSY,			/* SCMI_ERR_BUSY */
	-ECOMM,			/* SCMI_ERR_COMMS */
	-EIO,			/* SCMI_ERR_GENERIC */
	-EREMOTEIO,		/* SCMI_ERR_HARDWARE */
	-EPROTO,		/* SCMI_ERR_PROTOCOL */
};

static inline int scmi_to_linux_errno(int errno)
{
	if (errno < SCMI_SUCCESS && errno > SCMI_ERR_MAX)
		return scmi_linux_errmap[-errno];
	return -EIO;
}

/**
 * scmi_dump_header_dbg() - Helper to dump a message header.
 *
 * @dev: Device pointer corresponding to the SCMI entity
 * @hdr: pointer to header.
 */
static inline void scmi_dump_header_dbg(struct device *dev,
					struct scmi_msg_hdr *hdr)
{
	dev_dbg(dev, "Message ID: %x Sequence ID: %x Protocol: %x\n",
		hdr->id, hdr->seq, hdr->protocol_id);
}

/**
 * scmi_xfer_get() - Allocate one message
 *
 * @handle: Pointer to SCMI entity handle
 * @minfo: Pointer to Tx/Rx Message management info based on channel type
 *
 * Helper function which is used by various message functions that are
 * exposed to clients of this driver for allocating a message traffic event.
 *
 * This function can sleep depending on pending requests already in the system
 * for the SCMI entity. Further, this also holds a spinlock to maintain
 * integrity of internal data structures.
 *
 * Return: 0 if all went fine, else corresponding error.
 */
static struct scmi_xfer *scmi_xfer_get(const struct scmi_handle *handle,
				       struct scmi_xfers_info *minfo)
{
	u16 xfer_id;
	struct scmi_xfer *xfer;
	unsigned long flags, bit_pos;
	struct scmi_info *info = handle_to_scmi_info(handle);

	/* Keep the locked section as small as possible */
	spin_lock_irqsave(&minfo->xfer_lock, flags);
	bit_pos = find_first_zero_bit(minfo->xfer_alloc_table,
				      info->desc->max_msg);
	if (bit_pos == info->desc->max_msg) {
		spin_unlock_irqrestore(&minfo->xfer_lock, flags);
		return ERR_PTR(-ENOMEM);
	}
	set_bit(bit_pos, minfo->xfer_alloc_table);
	spin_unlock_irqrestore(&minfo->xfer_lock, flags);

	xfer_id = bit_pos;

	xfer = &minfo->xfer_block[xfer_id];
	xfer->hdr.seq = xfer_id;
	reinit_completion(&xfer->done);
	xfer->transfer_id = atomic_inc_return(&transfer_last_id);

	return xfer;
}

/**
 * __scmi_xfer_put() - Release a message
 *
 * @minfo: Pointer to Tx/Rx Message management info based on channel type
 * @xfer: message that was reserved by scmi_xfer_get
 *
 * This holds a spinlock to maintain integrity of internal data structures.
 */
static void
__scmi_xfer_put(struct scmi_xfers_info *minfo, struct scmi_xfer *xfer)
{
	unsigned long flags;

	/*
	 * Keep the locked section as small as possible
	 * NOTE: we might escape with smp_mb and no lock here..
	 * but just be conservative and symmetric.
	 */
	spin_lock_irqsave(&minfo->xfer_lock, flags);
	clear_bit(xfer->hdr.seq, minfo->xfer_alloc_table);
	spin_unlock_irqrestore(&minfo->xfer_lock, flags);
}

static void scmi_handle_notification(struct scmi_chan_info *cinfo, u32 msg_hdr)
{
	struct scmi_xfer *xfer;
	struct device *dev = cinfo->dev;
	struct scmi_info *info = handle_to_scmi_info(cinfo->handle);
	struct scmi_xfers_info *minfo = &info->rx_minfo;
	ktime_t ts;

	ts = ktime_get_boottime();
	xfer = scmi_xfer_get(cinfo->handle, minfo);
	if (IS_ERR(xfer)) {
		dev_err(dev, "failed to get free message slot (%ld)\n",
			PTR_ERR(xfer));
		info->desc->ops->clear_channel(cinfo);
		return;
	}

	unpack_scmi_header(msg_hdr, &xfer->hdr);
	scmi_dump_header_dbg(dev, &xfer->hdr);
	info->desc->ops->fetch_notification(cinfo, info->desc->max_msg_size,
					    xfer);
	scmi_notify(cinfo->handle, xfer->hdr.protocol_id,
		    xfer->hdr.id, xfer->rx.buf, xfer->rx.len, ts);

	trace_scmi_rx_done(xfer->transfer_id, xfer->hdr.id,
			   xfer->hdr.protocol_id, xfer->hdr.seq,
			   MSG_TYPE_NOTIFICATION);

	__scmi_xfer_put(minfo, xfer);

	info->desc->ops->clear_channel(cinfo);
}

static void scmi_handle_response(struct scmi_chan_info *cinfo,
				 u16 xfer_id, u8 msg_type)
{
	struct scmi_xfer *xfer;
	struct device *dev = cinfo->dev;
	struct scmi_info *info = handle_to_scmi_info(cinfo->handle);
	struct scmi_xfers_info *minfo = &info->tx_minfo;

	/* Are we even expecting this? */
	if (!test_bit(xfer_id, minfo->xfer_alloc_table)) {
		dev_err(dev, "message for %d is not expected!\n", xfer_id);
		info->desc->ops->clear_channel(cinfo);
		return;
	}

	xfer = &minfo->xfer_block[xfer_id];
	/*
	 * Even if a response was indeed expected on this slot at this point,
	 * a buggy platform could wrongly reply feeding us an unexpected
	 * delayed response we're not prepared to handle: bail-out safely
	 * blaming firmware.
	 */
	if (unlikely(msg_type == MSG_TYPE_DELAYED_RESP && !xfer->async_done)) {
		dev_err(dev,
			"Delayed Response for %d not expected! Buggy F/W ?\n",
			xfer_id);
		info->desc->ops->clear_channel(cinfo);
		/* It was unexpected, so nobody will clear the xfer if not us */
		__scmi_xfer_put(minfo, xfer);
		return;
	}

	/* rx.len could be shrunk in the sync do_xfer, so reset to maxsz */
	if (msg_type == MSG_TYPE_DELAYED_RESP)
		xfer->rx.len = info->desc->max_msg_size;

	scmi_dump_header_dbg(dev, &xfer->hdr);

	info->desc->ops->fetch_response(cinfo, xfer);

	trace_scmi_rx_done(xfer->transfer_id, xfer->hdr.id,
			   xfer->hdr.protocol_id, xfer->hdr.seq,
			   msg_type);

	if (msg_type == MSG_TYPE_DELAYED_RESP) {
		info->desc->ops->clear_channel(cinfo);
		complete(xfer->async_done);
	} else {
		complete(&xfer->done);
	}
}

/**
 * scmi_rx_callback() - callback for receiving messages
 *
 * @cinfo: SCMI channel info
 * @msg_hdr: Message header
 *
 * Processes one received message to appropriate transfer information and
 * signals completion of the transfer.
 *
 * NOTE: This function will be invoked in IRQ context, hence should be
 * as optimal as possible.
 */
void scmi_rx_callback(struct scmi_chan_info *cinfo, u32 msg_hdr)
{
	u16 xfer_id = MSG_XTRACT_TOKEN(msg_hdr);
	u8 msg_type = MSG_XTRACT_TYPE(msg_hdr);

	switch (msg_type) {
	case MSG_TYPE_NOTIFICATION:
		scmi_handle_notification(cinfo, msg_hdr);
		break;
	case MSG_TYPE_COMMAND:
	case MSG_TYPE_DELAYED_RESP:
		scmi_handle_response(cinfo, xfer_id, msg_type);
		break;
	default:
		WARN_ONCE(1, "received unknown msg_type:%d\n", msg_type);
		break;
	}
}

/**
 * scmi_xfer_put() - Release a transmit message
 *
 * @handle: Pointer to SCMI entity handle
 * @xfer: message that was reserved by scmi_xfer_get
 */
void scmi_xfer_put(const struct scmi_handle *handle, struct scmi_xfer *xfer)
{
	struct scmi_info *info = handle_to_scmi_info(handle);

	__scmi_xfer_put(&info->tx_minfo, xfer);
}

#define SCMI_MAX_POLL_TO_NS	(100 * NSEC_PER_USEC)

static bool scmi_xfer_done_no_timeout(struct scmi_chan_info *cinfo,
				      struct scmi_xfer *xfer, ktime_t stop)
{
	struct scmi_info *info = handle_to_scmi_info(cinfo->handle);

	return info->desc->ops->poll_done(cinfo, xfer) ||
	       ktime_after(ktime_get(), stop);
}

/**
 * scmi_do_xfer() - Do one transfer
 *
 * @handle: Pointer to SCMI entity handle
 * @xfer: Transfer to initiate and wait for response
 *
 * Return: -ETIMEDOUT in case of no response, if transmit error,
 *	return corresponding error, else if all goes well,
 *	return 0.
 */
int scmi_do_xfer(const struct scmi_handle *handle, struct scmi_xfer *xfer)
{
	int ret;
	int timeout;
	struct scmi_info *info = handle_to_scmi_info(handle);
	struct device *dev = info->dev;
	struct scmi_chan_info *cinfo;

	cinfo = idr_find(&info->tx_idr, xfer->hdr.protocol_id);
	if (unlikely(!cinfo))
		return -EINVAL;

	trace_scmi_xfer_begin(xfer->transfer_id, xfer->hdr.id,
			      xfer->hdr.protocol_id, xfer->hdr.seq,
			      xfer->hdr.poll_completion);

	ret = info->desc->ops->send_message(cinfo, xfer);
	if (ret < 0) {
		dev_dbg(dev, "Failed to send message %d\n", ret);
		return ret;
	}

	if (xfer->hdr.poll_completion) {
		ktime_t stop = ktime_add_ns(ktime_get(), SCMI_MAX_POLL_TO_NS);

		spin_until_cond(scmi_xfer_done_no_timeout(cinfo, xfer, stop));

		if (ktime_before(ktime_get(), stop))
			info->desc->ops->fetch_response(cinfo, xfer);
		else
			ret = -ETIMEDOUT;
	} else {
		/* And we wait for the response. */
		timeout = msecs_to_jiffies(info->desc->max_rx_timeout_ms);
		if (!wait_for_completion_timeout(&xfer->done, timeout)) {
			dev_err(dev, "timed out in resp(caller: %pS)\n",
				(void *)_RET_IP_);
			ret = -ETIMEDOUT;
		}
	}

	if (!ret && xfer->hdr.status)
		ret = scmi_to_linux_errno(xfer->hdr.status);

	if (info->desc->ops->mark_txdone)
		info->desc->ops->mark_txdone(cinfo, ret);

	trace_scmi_xfer_end(xfer->transfer_id, xfer->hdr.id,
			    xfer->hdr.protocol_id, xfer->hdr.seq, ret);

	return ret;
}

void scmi_reset_rx_to_maxsz(const struct scmi_handle *handle,
			    struct scmi_xfer *xfer)
{
	struct scmi_info *info = handle_to_scmi_info(handle);

	xfer->rx.len = info->desc->max_msg_size;
}

#define SCMI_MAX_RESPONSE_TIMEOUT	(2 * MSEC_PER_SEC)

/**
 * scmi_do_xfer_with_response() - Do one transfer and wait until the delayed
 *	response is received
 *
 * @handle: Pointer to SCMI entity handle
 * @xfer: Transfer to initiate and wait for response
 *
 * Return: -ETIMEDOUT in case of no delayed response, if transmit error,
 *	return corresponding error, else if all goes well, return 0.
 */
int scmi_do_xfer_with_response(const struct scmi_handle *handle,
			       struct scmi_xfer *xfer)
{
	int ret, timeout = msecs_to_jiffies(SCMI_MAX_RESPONSE_TIMEOUT);
	DECLARE_COMPLETION_ONSTACK(async_response);

	xfer->async_done = &async_response;

	ret = scmi_do_xfer(handle, xfer);
	if (!ret && !wait_for_completion_timeout(xfer->async_done, timeout))
		ret = -ETIMEDOUT;

	xfer->async_done = NULL;
	return ret;
}

/**
 * scmi_xfer_get_init() - Allocate and initialise one message for transmit
 *
 * @handle: Pointer to SCMI entity handle
 * @msg_id: Message identifier
 * @prot_id: Protocol identifier for the message
 * @tx_size: transmit message size
 * @rx_size: receive message size
 * @p: pointer to the allocated and initialised message
 *
 * This function allocates the message using @scmi_xfer_get and
 * initialise the header.
 *
 * Return: 0 if all went fine with @p pointing to message, else
 *	corresponding error.
 */
int scmi_xfer_get_init(const struct scmi_handle *handle, u8 msg_id, u8 prot_id,
		       size_t tx_size, size_t rx_size, struct scmi_xfer **p)
{
	int ret;
	struct scmi_xfer *xfer;
	struct scmi_info *info = handle_to_scmi_info(handle);
	struct scmi_xfers_info *minfo = &info->tx_minfo;
	struct device *dev = info->dev;

	/* Ensure we have sane transfer sizes */
	if (rx_size > info->desc->max_msg_size ||
	    tx_size > info->desc->max_msg_size)
		return -ERANGE;

	xfer = scmi_xfer_get(handle, minfo);
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "failed to get free message slot(%d)\n", ret);
		return ret;
	}

	xfer->tx.len = tx_size;
	xfer->rx.len = rx_size ? : info->desc->max_msg_size;
	xfer->hdr.id = msg_id;
	xfer->hdr.protocol_id = prot_id;
	xfer->hdr.poll_completion = false;

	*p = xfer;

	return 0;
}

/**
 * scmi_version_get() - command to get the revision of the SCMI entity
 *
 * @handle: Pointer to SCMI entity handle
 * @protocol: Protocol identifier for the message
 * @version: Holds returned version of protocol.
 *
 * Updates the SCMI information in the internal data structure.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
int scmi_version_get(const struct scmi_handle *handle, u8 protocol,
		     u32 *version)
{
	int ret;
	__le32 *rev_info;
	struct scmi_xfer *t;

	ret = scmi_xfer_get_init(handle, PROTOCOL_VERSION, protocol, 0,
				 sizeof(*version), &t);
	if (ret)
		return ret;

	ret = scmi_do_xfer(handle, t);
	if (!ret) {
		rev_info = t->rx.buf;
		*version = le32_to_cpu(*rev_info);
	}

	scmi_xfer_put(handle, t);
	return ret;
}

void scmi_setup_protocol_implemented(const struct scmi_handle *handle,
				     u8 *prot_imp)
{
	struct scmi_info *info = handle_to_scmi_info(handle);

	info->protocols_imp = prot_imp;
}

static bool
scmi_is_protocol_implemented(const struct scmi_handle *handle, u8 prot_id)
{
	int i;
	struct scmi_info *info = handle_to_scmi_info(handle);

	if (!info->protocols_imp)
		return false;

	for (i = 0; i < MAX_PROTOCOLS_IMP; i++)
		if (info->protocols_imp[i] == prot_id)
			return true;
	return false;
}

/**
 * scmi_handle_get() - Get the SCMI handle for a device
 *
 * @dev: pointer to device for which we want SCMI handle
 *
 * NOTE: The function does not track individual clients of the framework
 * and is expected to be maintained by caller of SCMI protocol library.
 * scmi_handle_put must be balanced with successful scmi_handle_get
 *
 * Return: pointer to handle if successful, NULL on error
 */
struct scmi_handle *scmi_handle_get(struct device *dev)
{
	struct list_head *p;
	struct scmi_info *info;
	struct scmi_handle *handle = NULL;

	mutex_lock(&scmi_list_mutex);
	list_for_each(p, &scmi_list) {
		info = list_entry(p, struct scmi_info, node);
		if (dev->parent == info->dev) {
			handle = &info->handle;
			info->users++;
			break;
		}
	}
	mutex_unlock(&scmi_list_mutex);

	return handle;
}

/**
 * scmi_handle_put() - Release the handle acquired by scmi_handle_get
 *
 * @handle: handle acquired by scmi_handle_get
 *
 * NOTE: The function does not track individual clients of the framework
 * and is expected to be maintained by caller of SCMI protocol library.
 * scmi_handle_put must be balanced with successful scmi_handle_get
 *
 * Return: 0 is successfully released
 *	if null was passed, it returns -EINVAL;
 */
int scmi_handle_put(const struct scmi_handle *handle)
{
	struct scmi_info *info;

	if (!handle)
		return -EINVAL;

	info = handle_to_scmi_info(handle);
	mutex_lock(&scmi_list_mutex);
	if (!WARN_ON(!info->users))
		info->users--;
	mutex_unlock(&scmi_list_mutex);

	return 0;
}

static int __scmi_xfer_info_init(struct scmi_info *sinfo,
				 struct scmi_xfers_info *info)
{
	int i;
	struct scmi_xfer *xfer;
	struct device *dev = sinfo->dev;
	const struct scmi_desc *desc = sinfo->desc;

	/* Pre-allocated messages, no more than what hdr.seq can support */
	if (WARN_ON(desc->max_msg >= MSG_TOKEN_MAX)) {
		dev_err(dev, "Maximum message of %d exceeds supported %ld\n",
			desc->max_msg, MSG_TOKEN_MAX);
		return -EINVAL;
	}

	info->xfer_block = devm_kcalloc(dev, desc->max_msg,
					sizeof(*info->xfer_block), GFP_KERNEL);
	if (!info->xfer_block)
		return -ENOMEM;

	info->xfer_alloc_table = devm_kcalloc(dev, BITS_TO_LONGS(desc->max_msg),
					      sizeof(long), GFP_KERNEL);
	if (!info->xfer_alloc_table)
		return -ENOMEM;

	/* Pre-initialize the buffer pointer to pre-allocated buffers */
	for (i = 0, xfer = info->xfer_block; i < desc->max_msg; i++, xfer++) {
		xfer->rx.buf = devm_kcalloc(dev, sizeof(u8), desc->max_msg_size,
					    GFP_KERNEL);
		if (!xfer->rx.buf)
			return -ENOMEM;

		xfer->tx.buf = xfer->rx.buf;
		init_completion(&xfer->done);
	}

	spin_lock_init(&info->xfer_lock);

	return 0;
}

static int scmi_xfer_info_init(struct scmi_info *sinfo)
{
	int ret = __scmi_xfer_info_init(sinfo, &sinfo->tx_minfo);

	if (!ret && idr_find(&sinfo->rx_idr, SCMI_PROTOCOL_BASE))
		ret = __scmi_xfer_info_init(sinfo, &sinfo->rx_minfo);

	return ret;
}

static int scmi_chan_setup(struct scmi_info *info, struct device *dev,
			   int prot_id, bool tx)
{
	int ret, idx;
	struct scmi_chan_info *cinfo;
	struct idr *idr;

	/* Transmit channel is first entry i.e. index 0 */
	idx = tx ? 0 : 1;
	idr = tx ? &info->tx_idr : &info->rx_idr;

	/* check if already allocated, used for multiple device per protocol */
	cinfo = idr_find(idr, prot_id);
	if (cinfo)
		return 0;

	if (!info->desc->ops->chan_available(dev, idx)) {
		cinfo = idr_find(idr, SCMI_PROTOCOL_BASE);
		if (unlikely(!cinfo)) /* Possible only if platform has no Rx */
			return -EINVAL;
		goto idr_alloc;
	}

	cinfo = devm_kzalloc(info->dev, sizeof(*cinfo), GFP_KERNEL);
	if (!cinfo)
		return -ENOMEM;

	cinfo->dev = dev;

	ret = info->desc->ops->chan_setup(cinfo, info->dev, tx);
	if (ret)
		return ret;

idr_alloc:
	ret = idr_alloc(idr, cinfo, prot_id, prot_id + 1, GFP_KERNEL);
	if (ret != prot_id) {
		dev_err(dev, "unable to allocate SCMI idr slot err %d\n", ret);
		return ret;
	}

	cinfo->handle = &info->handle;
	return 0;
}

static inline int
scmi_txrx_setup(struct scmi_info *info, struct device *dev, int prot_id)
{
	int ret = scmi_chan_setup(info, dev, prot_id, true);

	if (!ret) /* Rx is optional, hence no error check */
		scmi_chan_setup(info, dev, prot_id, false);

	return ret;
}

static inline void
scmi_create_protocol_device(struct device_node *np, struct scmi_info *info,
			    int prot_id, const char *name)
{
	struct scmi_device *sdev;

	sdev = scmi_device_create(np, info->dev, prot_id, name);
	if (!sdev) {
		dev_err(info->dev, "failed to create %d protocol device\n",
			prot_id);
		return;
	}

	if (scmi_txrx_setup(info, &sdev->dev, prot_id)) {
		dev_err(&sdev->dev, "failed to setup transport\n");
		scmi_device_destroy(sdev);
		return;
	}

	/* setup handle now as the transport is ready */
	scmi_set_handle(sdev);
}

#define MAX_SCMI_DEV_PER_PROTOCOL	2
struct scmi_prot_devnames {
	int protocol_id;
	char *names[MAX_SCMI_DEV_PER_PROTOCOL];
};

static struct scmi_prot_devnames devnames[] = {
	{ SCMI_PROTOCOL_POWER,  { "genpd" },},
	{ SCMI_PROTOCOL_SYSTEM, { "syspower" },},
	{ SCMI_PROTOCOL_PERF,   { "cpufreq" },},
	{ SCMI_PROTOCOL_CLOCK,  { "clocks" },},
	{ SCMI_PROTOCOL_SENSOR, { "hwmon" },},
	{ SCMI_PROTOCOL_RESET,  { "reset" },},
};

static inline void
scmi_create_protocol_devices(struct device_node *np, struct scmi_info *info,
			     int prot_id)
{
	int loop, cnt;

	for (loop = 0; loop < ARRAY_SIZE(devnames); loop++) {
		if (devnames[loop].protocol_id != prot_id)
			continue;

		for (cnt = 0; cnt < ARRAY_SIZE(devnames[loop].names); cnt++) {
			const char *name = devnames[loop].names[cnt];

			if (name)
				scmi_create_protocol_device(np, info, prot_id,
							    name);
		}
	}
}

static int scmi_probe(struct platform_device *pdev)
{
	int ret;
	struct scmi_handle *handle;
	const struct scmi_desc *desc;
	struct scmi_info *info;
	struct device *dev = &pdev->dev;
	struct device_node *child, *np = dev->of_node;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;
	info->desc = desc;
	INIT_LIST_HEAD(&info->node);

	platform_set_drvdata(pdev, info);
	idr_init(&info->tx_idr);
	idr_init(&info->rx_idr);

	handle = &info->handle;
	handle->dev = info->dev;
	handle->version = &info->version;

	ret = scmi_txrx_setup(info, dev, SCMI_PROTOCOL_BASE);
	if (ret)
		return ret;

	ret = scmi_xfer_info_init(info);
	if (ret)
		return ret;

	if (scmi_notification_init(handle))
		dev_err(dev, "SCMI Notifications NOT available.\n");

	ret = scmi_base_protocol_init(handle);
	if (ret) {
		dev_err(dev, "unable to communicate with SCMI(%d)\n", ret);
		return ret;
	}

	mutex_lock(&scmi_list_mutex);
	list_add_tail(&info->node, &scmi_list);
	mutex_unlock(&scmi_list_mutex);

	for_each_available_child_of_node(np, child) {
		u32 prot_id;

		if (of_property_read_u32(child, "reg", &prot_id))
			continue;

		if (!FIELD_FIT(MSG_PROTOCOL_ID_MASK, prot_id))
			dev_err(dev, "Out of range protocol %d\n", prot_id);

		if (!scmi_is_protocol_implemented(handle, prot_id)) {
			dev_err(dev, "SCMI protocol %d not implemented\n",
				prot_id);
			continue;
		}

		scmi_create_protocol_devices(child, info, prot_id);
	}

	return 0;
}

void scmi_free_channel(struct scmi_chan_info *cinfo, struct idr *idr, int id)
{
	idr_remove(idr, id);
}

static int scmi_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct scmi_info *info = platform_get_drvdata(pdev);
	struct idr *idr = &info->tx_idr;

	mutex_lock(&scmi_list_mutex);
	if (info->users)
		ret = -EBUSY;
	else
		list_del(&info->node);
	mutex_unlock(&scmi_list_mutex);

	if (ret)
		return ret;

	scmi_notification_exit(&info->handle);

	/* Safe to free channels since no more users */
	ret = idr_for_each(idr, info->desc->ops->chan_free, idr);
	idr_destroy(&info->tx_idr);

	idr = &info->rx_idr;
	ret = idr_for_each(idr, info->desc->ops->chan_free, idr);
	idr_destroy(&info->rx_idr);

	return ret;
}

static ssize_t protocol_version_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct scmi_info *info = dev_get_drvdata(dev);

	return sprintf(buf, "%u.%u\n", info->version.major_ver,
		       info->version.minor_ver);
}
static DEVICE_ATTR_RO(protocol_version);

static ssize_t firmware_version_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct scmi_info *info = dev_get_drvdata(dev);

	return sprintf(buf, "0x%x\n", info->version.impl_ver);
}
static DEVICE_ATTR_RO(firmware_version);

static ssize_t vendor_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct scmi_info *info = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", info->version.vendor_id);
}
static DEVICE_ATTR_RO(vendor_id);

static ssize_t sub_vendor_id_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct scmi_info *info = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", info->version.sub_vendor_id);
}
static DEVICE_ATTR_RO(sub_vendor_id);

static struct attribute *versions_attrs[] = {
	&dev_attr_firmware_version.attr,
	&dev_attr_protocol_version.attr,
	&dev_attr_vendor_id.attr,
	&dev_attr_sub_vendor_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(versions);

/* Each compatible listed below must have descriptor associated with it */
static const struct of_device_id scmi_of_match[] = {
	{ .compatible = "arm,scmi", .data = &scmi_mailbox_desc },
#ifdef CONFIG_HAVE_ARM_SMCCC_DISCOVERY
	{ .compatible = "arm,scmi-smc", .data = &scmi_smc_desc},
#endif
	{ /* Sentinel */ },
};

MODULE_DEVICE_TABLE(of, scmi_of_match);

static struct platform_driver scmi_driver = {
	.driver = {
		   .name = "arm-scmi",
		   .of_match_table = scmi_of_match,
		   .dev_groups = versions_groups,
		   },
	.probe = scmi_probe,
	.remove = scmi_remove,
};

static int __init scmi_driver_init(void)
{
	scmi_bus_init();

	scmi_clock_register();
	scmi_perf_register();
	scmi_power_register();
	scmi_reset_register();
	scmi_sensors_register();
	scmi_system_register();

	return platform_driver_register(&scmi_driver);
}
subsys_initcall(scmi_driver_init);

static void __exit scmi_driver_exit(void)
{
	scmi_bus_exit();

	scmi_clock_unregister();
	scmi_perf_unregister();
	scmi_power_unregister();
	scmi_reset_unregister();
	scmi_sensors_unregister();
	scmi_system_unregister();

	platform_driver_unregister(&scmi_driver);
}
module_exit(scmi_driver_exit);

MODULE_ALIAS("platform: arm-scmi");
MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("ARM SCMI protocol driver");
MODULE_LICENSE("GPL v2");
