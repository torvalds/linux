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
 * Copyright (C) 2018-2021 ARM Ltd.
 */

#include <linux/bitmap.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/idr.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/processor.h>
#include <linux/refcount.h>
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
};

/* List of all SCMI devices active in system */
static LIST_HEAD(scmi_list);
/* Protection for the entire list */
static DEFINE_MUTEX(scmi_list_mutex);
/* Track the unique id for the transfers for debug & profiling purpose */
static atomic_t transfer_last_id;

static DEFINE_IDR(scmi_requested_devices);
static DEFINE_MUTEX(scmi_requested_devices_mtx);

struct scmi_requested_dev {
	const struct scmi_device_id *id_table;
	struct list_head node;
};

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
 * struct scmi_protocol_instance  - Describe an initialized protocol instance.
 * @handle: Reference to the SCMI handle associated to this protocol instance.
 * @proto: A reference to the protocol descriptor.
 * @gid: A reference for per-protocol devres management.
 * @users: A refcount to track effective users of this protocol.
 * @priv: Reference for optional protocol private data.
 * @ph: An embedded protocol handle that will be passed down to protocol
 *	initialization code to identify this instance.
 *
 * Each protocol is initialized independently once for each SCMI platform in
 * which is defined by DT and implemented by the SCMI server fw.
 */
struct scmi_protocol_instance {
	const struct scmi_handle	*handle;
	const struct scmi_protocol	*proto;
	void				*gid;
	refcount_t			users;
	void				*priv;
	struct scmi_protocol_handle	ph;
};

#define ph_to_pi(h)	container_of(h, struct scmi_protocol_instance, ph)

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
 * @protocols: IDR for protocols' instance descriptors initialized for
 *	       this SCMI instance: populated on protocol's first attempted
 *	       usage.
 * @protocols_mtx: A mutex to protect protocols instances initialization.
 * @protocols_imp: List of protocols implemented, currently maximum of
 *	MAX_PROTOCOLS_IMP elements allocated by the base protocol
 * @active_protocols: IDR storing device_nodes for protocols actually defined
 *		      in the DT and confirmed as implemented by fw.
 * @notify_priv: Pointer to private data structure specific to notifications.
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
	struct idr protocols;
	/* Ensure mutual exclusive access to protocols instance array */
	struct mutex protocols_mtx;
	u8 *protocols_imp;
	struct idr active_protocols;
	void *notify_priv;
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
	int err_idx = -errno;

	if (err_idx >= SCMI_SUCCESS && err_idx < ARRAY_SIZE(scmi_linux_errmap))
		return scmi_linux_errmap[err_idx];
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

void scmi_notification_instance_data_set(const struct scmi_handle *handle,
					 void *priv)
{
	struct scmi_info *info = handle_to_scmi_info(handle);

	info->notify_priv = priv;
	/* Ensure updated protocol private date are visible */
	smp_wmb();
}

void *scmi_notification_instance_data_get(const struct scmi_handle *handle)
{
	struct scmi_info *info = handle_to_scmi_info(handle);

	/* Ensure protocols_private_data has been updated */
	smp_rmb();
	return info->notify_priv;
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
 * xfer_put() - Release a transmit message
 *
 * @ph: Pointer to SCMI protocol handle
 * @xfer: message that was reserved by scmi_xfer_get
 */
static void xfer_put(const struct scmi_protocol_handle *ph,
		     struct scmi_xfer *xfer)
{
	const struct scmi_protocol_instance *pi = ph_to_pi(ph);
	struct scmi_info *info = handle_to_scmi_info(pi->handle);

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
 * do_xfer() - Do one transfer
 *
 * @ph: Pointer to SCMI protocol handle
 * @xfer: Transfer to initiate and wait for response
 *
 * Return: -ETIMEDOUT in case of no response, if transmit error,
 *	return corresponding error, else if all goes well,
 *	return 0.
 */
static int do_xfer(const struct scmi_protocol_handle *ph,
		   struct scmi_xfer *xfer)
{
	int ret;
	int timeout;
	const struct scmi_protocol_instance *pi = ph_to_pi(ph);
	struct scmi_info *info = handle_to_scmi_info(pi->handle);
	struct device *dev = info->dev;
	struct scmi_chan_info *cinfo;

	/*
	 * Initialise protocol id now from protocol handle to avoid it being
	 * overridden by mistake (or malice) by the protocol code mangling with
	 * the scmi_xfer structure prior to this.
	 */
	xfer->hdr.protocol_id = pi->proto->id;
	reinit_completion(&xfer->done);

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

static void reset_rx_to_maxsz(const struct scmi_protocol_handle *ph,
			      struct scmi_xfer *xfer)
{
	const struct scmi_protocol_instance *pi = ph_to_pi(ph);
	struct scmi_info *info = handle_to_scmi_info(pi->handle);

	xfer->rx.len = info->desc->max_msg_size;
}

#define SCMI_MAX_RESPONSE_TIMEOUT	(2 * MSEC_PER_SEC)

/**
 * do_xfer_with_response() - Do one transfer and wait until the delayed
 *	response is received
 *
 * @ph: Pointer to SCMI protocol handle
 * @xfer: Transfer to initiate and wait for response
 *
 * Return: -ETIMEDOUT in case of no delayed response, if transmit error,
 *	return corresponding error, else if all goes well, return 0.
 */
static int do_xfer_with_response(const struct scmi_protocol_handle *ph,
				 struct scmi_xfer *xfer)
{
	int ret, timeout = msecs_to_jiffies(SCMI_MAX_RESPONSE_TIMEOUT);
	DECLARE_COMPLETION_ONSTACK(async_response);

	xfer->async_done = &async_response;

	ret = do_xfer(ph, xfer);
	if (!ret) {
		if (!wait_for_completion_timeout(xfer->async_done, timeout))
			ret = -ETIMEDOUT;
		else if (xfer->hdr.status)
			ret = scmi_to_linux_errno(xfer->hdr.status);
	}

	xfer->async_done = NULL;
	return ret;
}

/**
 * xfer_get_init() - Allocate and initialise one message for transmit
 *
 * @ph: Pointer to SCMI protocol handle
 * @msg_id: Message identifier
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
static int xfer_get_init(const struct scmi_protocol_handle *ph,
			 u8 msg_id, size_t tx_size, size_t rx_size,
			 struct scmi_xfer **p)
{
	int ret;
	struct scmi_xfer *xfer;
	const struct scmi_protocol_instance *pi = ph_to_pi(ph);
	struct scmi_info *info = handle_to_scmi_info(pi->handle);
	struct scmi_xfers_info *minfo = &info->tx_minfo;
	struct device *dev = info->dev;

	/* Ensure we have sane transfer sizes */
	if (rx_size > info->desc->max_msg_size ||
	    tx_size > info->desc->max_msg_size)
		return -ERANGE;

	xfer = scmi_xfer_get(pi->handle, minfo);
	if (IS_ERR(xfer)) {
		ret = PTR_ERR(xfer);
		dev_err(dev, "failed to get free message slot(%d)\n", ret);
		return ret;
	}

	xfer->tx.len = tx_size;
	xfer->rx.len = rx_size ? : info->desc->max_msg_size;
	xfer->hdr.id = msg_id;
	xfer->hdr.poll_completion = false;

	*p = xfer;

	return 0;
}

/**
 * version_get() - command to get the revision of the SCMI entity
 *
 * @ph: Pointer to SCMI protocol handle
 * @version: Holds returned version of protocol.
 *
 * Updates the SCMI information in the internal data structure.
 *
 * Return: 0 if all went fine, else return appropriate error.
 */
static int version_get(const struct scmi_protocol_handle *ph, u32 *version)
{
	int ret;
	__le32 *rev_info;
	struct scmi_xfer *t;

	ret = xfer_get_init(ph, PROTOCOL_VERSION, 0, sizeof(*version), &t);
	if (ret)
		return ret;

	ret = do_xfer(ph, t);
	if (!ret) {
		rev_info = t->rx.buf;
		*version = le32_to_cpu(*rev_info);
	}

	xfer_put(ph, t);
	return ret;
}

/**
 * scmi_set_protocol_priv  - Set protocol specific data at init time
 *
 * @ph: A reference to the protocol handle.
 * @priv: The private data to set.
 *
 * Return: 0 on Success
 */
static int scmi_set_protocol_priv(const struct scmi_protocol_handle *ph,
				  void *priv)
{
	struct scmi_protocol_instance *pi = ph_to_pi(ph);

	pi->priv = priv;

	return 0;
}

/**
 * scmi_get_protocol_priv  - Set protocol specific data at init time
 *
 * @ph: A reference to the protocol handle.
 *
 * Return: Protocol private data if any was set.
 */
static void *scmi_get_protocol_priv(const struct scmi_protocol_handle *ph)
{
	const struct scmi_protocol_instance *pi = ph_to_pi(ph);

	return pi->priv;
}

static const struct scmi_xfer_ops xfer_ops = {
	.version_get = version_get,
	.xfer_get_init = xfer_get_init,
	.reset_rx_to_maxsz = reset_rx_to_maxsz,
	.do_xfer = do_xfer,
	.do_xfer_with_response = do_xfer_with_response,
	.xfer_put = xfer_put,
};

/**
 * scmi_revision_area_get  - Retrieve version memory area.
 *
 * @ph: A reference to the protocol handle.
 *
 * A helper to grab the version memory area reference during SCMI Base protocol
 * initialization.
 *
 * Return: A reference to the version memory area associated to the SCMI
 *	   instance underlying this protocol handle.
 */
struct scmi_revision_info *
scmi_revision_area_get(const struct scmi_protocol_handle *ph)
{
	const struct scmi_protocol_instance *pi = ph_to_pi(ph);

	return pi->handle->version;
}

/**
 * scmi_alloc_init_protocol_instance  - Allocate and initialize a protocol
 * instance descriptor.
 * @info: The reference to the related SCMI instance.
 * @proto: The protocol descriptor.
 *
 * Allocate a new protocol instance descriptor, using the provided @proto
 * description, against the specified SCMI instance @info, and initialize it;
 * all resources management is handled via a dedicated per-protocol devres
 * group.
 *
 * Context: Assumes to be called with @protocols_mtx already acquired.
 * Return: A reference to a freshly allocated and initialized protocol instance
 *	   or ERR_PTR on failure. On failure the @proto reference is at first
 *	   put using @scmi_protocol_put() before releasing all the devres group.
 */
static struct scmi_protocol_instance *
scmi_alloc_init_protocol_instance(struct scmi_info *info,
				  const struct scmi_protocol *proto)
{
	int ret = -ENOMEM;
	void *gid;
	struct scmi_protocol_instance *pi;
	const struct scmi_handle *handle = &info->handle;

	/* Protocol specific devres group */
	gid = devres_open_group(handle->dev, NULL, GFP_KERNEL);
	if (!gid) {
		scmi_protocol_put(proto->id);
		goto out;
	}

	pi = devm_kzalloc(handle->dev, sizeof(*pi), GFP_KERNEL);
	if (!pi)
		goto clean;

	pi->gid = gid;
	pi->proto = proto;
	pi->handle = handle;
	pi->ph.dev = handle->dev;
	pi->ph.xops = &xfer_ops;
	pi->ph.set_priv = scmi_set_protocol_priv;
	pi->ph.get_priv = scmi_get_protocol_priv;
	refcount_set(&pi->users, 1);
	/* proto->init is assured NON NULL by scmi_protocol_register */
	ret = pi->proto->instance_init(&pi->ph);
	if (ret)
		goto clean;

	ret = idr_alloc(&info->protocols, pi, proto->id, proto->id + 1,
			GFP_KERNEL);
	if (ret != proto->id)
		goto clean;

	/*
	 * Warn but ignore events registration errors since we do not want
	 * to skip whole protocols if their notifications are messed up.
	 */
	if (pi->proto->events) {
		ret = scmi_register_protocol_events(handle, pi->proto->id,
						    &pi->ph,
						    pi->proto->events);
		if (ret)
			dev_warn(handle->dev,
				 "Protocol:%X - Events Registration Failed - err:%d\n",
				 pi->proto->id, ret);
	}

	devres_close_group(handle->dev, pi->gid);
	dev_dbg(handle->dev, "Initialized protocol: 0x%X\n", pi->proto->id);

	return pi;

clean:
	/* Take care to put the protocol module's owner before releasing all */
	scmi_protocol_put(proto->id);
	devres_release_group(handle->dev, gid);
out:
	return ERR_PTR(ret);
}

/**
 * scmi_get_protocol_instance  - Protocol initialization helper.
 * @handle: A reference to the SCMI platform instance.
 * @protocol_id: The protocol being requested.
 *
 * In case the required protocol has never been requested before for this
 * instance, allocate and initialize all the needed structures while handling
 * resource allocation with a dedicated per-protocol devres subgroup.
 *
 * Return: A reference to an initialized protocol instance or error on failure:
 *	   in particular returns -EPROBE_DEFER when the desired protocol could
 *	   NOT be found.
 */
static struct scmi_protocol_instance * __must_check
scmi_get_protocol_instance(const struct scmi_handle *handle, u8 protocol_id)
{
	struct scmi_protocol_instance *pi;
	struct scmi_info *info = handle_to_scmi_info(handle);

	mutex_lock(&info->protocols_mtx);
	pi = idr_find(&info->protocols, protocol_id);

	if (pi) {
		refcount_inc(&pi->users);
	} else {
		const struct scmi_protocol *proto;

		/* Fails if protocol not registered on bus */
		proto = scmi_protocol_get(protocol_id);
		if (proto)
			pi = scmi_alloc_init_protocol_instance(info, proto);
		else
			pi = ERR_PTR(-EPROBE_DEFER);
	}
	mutex_unlock(&info->protocols_mtx);

	return pi;
}

/**
 * scmi_protocol_acquire  - Protocol acquire
 * @handle: A reference to the SCMI platform instance.
 * @protocol_id: The protocol being requested.
 *
 * Register a new user for the requested protocol on the specified SCMI
 * platform instance, possibly triggering its initialization on first user.
 *
 * Return: 0 if protocol was acquired successfully.
 */
int scmi_protocol_acquire(const struct scmi_handle *handle, u8 protocol_id)
{
	return PTR_ERR_OR_ZERO(scmi_get_protocol_instance(handle, protocol_id));
}

/**
 * scmi_protocol_release  - Protocol de-initialization helper.
 * @handle: A reference to the SCMI platform instance.
 * @protocol_id: The protocol being requested.
 *
 * Remove one user for the specified protocol and triggers de-initialization
 * and resources de-allocation once the last user has gone.
 */
void scmi_protocol_release(const struct scmi_handle *handle, u8 protocol_id)
{
	struct scmi_info *info = handle_to_scmi_info(handle);
	struct scmi_protocol_instance *pi;

	mutex_lock(&info->protocols_mtx);
	pi = idr_find(&info->protocols, protocol_id);
	if (WARN_ON(!pi))
		goto out;

	if (refcount_dec_and_test(&pi->users)) {
		void *gid = pi->gid;

		if (pi->proto->events)
			scmi_deregister_protocol_events(handle, protocol_id);

		if (pi->proto->instance_deinit)
			pi->proto->instance_deinit(&pi->ph);

		idr_remove(&info->protocols, protocol_id);

		scmi_protocol_put(protocol_id);

		devres_release_group(handle->dev, gid);
		dev_dbg(handle->dev, "De-Initialized protocol: 0x%X\n",
			protocol_id);
	}

out:
	mutex_unlock(&info->protocols_mtx);
}

void scmi_setup_protocol_implemented(const struct scmi_protocol_handle *ph,
				     u8 *prot_imp)
{
	const struct scmi_protocol_instance *pi = ph_to_pi(ph);
	struct scmi_info *info = handle_to_scmi_info(pi->handle);

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

struct scmi_protocol_devres {
	const struct scmi_handle *handle;
	u8 protocol_id;
};

static void scmi_devm_release_protocol(struct device *dev, void *res)
{
	struct scmi_protocol_devres *dres = res;

	scmi_protocol_release(dres->handle, dres->protocol_id);
}

/**
 * scmi_devm_protocol_get  - Devres managed get protocol operations and handle
 * @sdev: A reference to an scmi_device whose embedded struct device is to
 *	  be used for devres accounting.
 * @protocol_id: The protocol being requested.
 * @ph: A pointer reference used to pass back the associated protocol handle.
 *
 * Get hold of a protocol accounting for its usage, eventually triggering its
 * initialization, and returning the protocol specific operations and related
 * protocol handle which will be used as first argument in most of the
 * protocols operations methods.
 * Being a devres based managed method, protocol hold will be automatically
 * released, and possibly de-initialized on last user, once the SCMI driver
 * owning the scmi_device is unbound from it.
 *
 * Return: A reference to the requested protocol operations or error.
 *	   Must be checked for errors by caller.
 */
static const void __must_check *
scmi_devm_protocol_get(struct scmi_device *sdev, u8 protocol_id,
		       struct scmi_protocol_handle **ph)
{
	struct scmi_protocol_instance *pi;
	struct scmi_protocol_devres *dres;
	struct scmi_handle *handle = sdev->handle;

	if (!ph)
		return ERR_PTR(-EINVAL);

	dres = devres_alloc(scmi_devm_release_protocol,
			    sizeof(*dres), GFP_KERNEL);
	if (!dres)
		return ERR_PTR(-ENOMEM);

	pi = scmi_get_protocol_instance(handle, protocol_id);
	if (IS_ERR(pi)) {
		devres_free(dres);
		return pi;
	}

	dres->handle = handle;
	dres->protocol_id = protocol_id;
	devres_add(&sdev->dev, dres);

	*ph = &pi->ph;

	return pi->proto->ops;
}

static int scmi_devm_protocol_match(struct device *dev, void *res, void *data)
{
	struct scmi_protocol_devres *dres = res;

	if (WARN_ON(!dres || !data))
		return 0;

	return dres->protocol_id == *((u8 *)data);
}

/**
 * scmi_devm_protocol_put  - Devres managed put protocol operations and handle
 * @sdev: A reference to an scmi_device whose embedded struct device is to
 *	  be used for devres accounting.
 * @protocol_id: The protocol being requested.
 *
 * Explicitly release a protocol hold previously obtained calling the above
 * @scmi_devm_protocol_get.
 */
static void scmi_devm_protocol_put(struct scmi_device *sdev, u8 protocol_id)
{
	int ret;

	ret = devres_release(&sdev->dev, scmi_devm_release_protocol,
			     scmi_devm_protocol_match, &protocol_id);
	WARN_ON(ret);
}

static inline
struct scmi_handle *scmi_handle_get_from_info_unlocked(struct scmi_info *info)
{
	info->users++;
	return &info->handle;
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
			handle = scmi_handle_get_from_info_unlocked(info);
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
	if (WARN_ON(!desc->max_msg || desc->max_msg > MSG_TOKEN_MAX)) {
		dev_err(dev,
			"Invalid maximum messages %d, not in range [1 - %lu]\n",
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

/**
 * scmi_get_protocol_device  - Helper to get/create an SCMI device.
 *
 * @np: A device node representing a valid active protocols for the referred
 * SCMI instance.
 * @info: The referred SCMI instance for which we are getting/creating this
 * device.
 * @prot_id: The protocol ID.
 * @name: The device name.
 *
 * Referring to the specific SCMI instance identified by @info, this helper
 * takes care to return a properly initialized device matching the requested
 * @proto_id and @name: if device was still not existent it is created as a
 * child of the specified SCMI instance @info and its transport properly
 * initialized as usual.
 *
 * Return: A properly initialized scmi device, NULL otherwise.
 */
static inline struct scmi_device *
scmi_get_protocol_device(struct device_node *np, struct scmi_info *info,
			 int prot_id, const char *name)
{
	struct scmi_device *sdev;

	/* Already created for this parent SCMI instance ? */
	sdev = scmi_child_dev_find(info->dev, prot_id, name);
	if (sdev)
		return sdev;

	pr_debug("Creating SCMI device (%s) for protocol %x\n", name, prot_id);

	sdev = scmi_device_create(np, info->dev, prot_id, name);
	if (!sdev) {
		dev_err(info->dev, "failed to create %d protocol device\n",
			prot_id);
		return NULL;
	}

	if (scmi_txrx_setup(info, &sdev->dev, prot_id)) {
		dev_err(&sdev->dev, "failed to setup transport\n");
		scmi_device_destroy(sdev);
		return NULL;
	}

	return sdev;
}

static inline void
scmi_create_protocol_device(struct device_node *np, struct scmi_info *info,
			    int prot_id, const char *name)
{
	struct scmi_device *sdev;

	sdev = scmi_get_protocol_device(np, info, prot_id, name);
	if (!sdev)
		return;

	/* setup handle now as the transport is ready */
	scmi_set_handle(sdev);
}

/**
 * scmi_create_protocol_devices  - Create devices for all pending requests for
 * this SCMI instance.
 *
 * @np: The device node describing the protocol
 * @info: The SCMI instance descriptor
 * @prot_id: The protocol ID
 *
 * All devices previously requested for this instance (if any) are found and
 * created by scanning the proper @&scmi_requested_devices entry.
 */
static void scmi_create_protocol_devices(struct device_node *np,
					 struct scmi_info *info, int prot_id)
{
	struct list_head *phead;

	mutex_lock(&scmi_requested_devices_mtx);
	phead = idr_find(&scmi_requested_devices, prot_id);
	if (phead) {
		struct scmi_requested_dev *rdev;

		list_for_each_entry(rdev, phead, node)
			scmi_create_protocol_device(np, info, prot_id,
						    rdev->id_table->name);
	}
	mutex_unlock(&scmi_requested_devices_mtx);
}

/**
 * scmi_protocol_device_request  - Helper to request a device
 *
 * @id_table: A protocol/name pair descriptor for the device to be created.
 *
 * This helper let an SCMI driver request specific devices identified by the
 * @id_table to be created for each active SCMI instance.
 *
 * The requested device name MUST NOT be already existent for any protocol;
 * at first the freshly requested @id_table is annotated in the IDR table
 * @scmi_requested_devices, then a matching device is created for each already
 * active SCMI instance. (if any)
 *
 * This way the requested device is created straight-away for all the already
 * initialized(probed) SCMI instances (handles) and it remains also annotated
 * as pending creation if the requesting SCMI driver was loaded before some
 * SCMI instance and related transports were available: when such late instance
 * is probed, its probe will take care to scan the list of pending requested
 * devices and create those on its own (see @scmi_create_protocol_devices and
 * its enclosing loop)
 *
 * Return: 0 on Success
 */
int scmi_protocol_device_request(const struct scmi_device_id *id_table)
{
	int ret = 0;
	unsigned int id = 0;
	struct list_head *head, *phead = NULL;
	struct scmi_requested_dev *rdev;
	struct scmi_info *info;

	pr_debug("Requesting SCMI device (%s) for protocol %x\n",
		 id_table->name, id_table->protocol_id);

	/*
	 * Search for the matching protocol rdev list and then search
	 * of any existent equally named device...fails if any duplicate found.
	 */
	mutex_lock(&scmi_requested_devices_mtx);
	idr_for_each_entry(&scmi_requested_devices, head, id) {
		if (!phead) {
			/* A list found registered in the IDR is never empty */
			rdev = list_first_entry(head, struct scmi_requested_dev,
						node);
			if (rdev->id_table->protocol_id ==
			    id_table->protocol_id)
				phead = head;
		}
		list_for_each_entry(rdev, head, node) {
			if (!strcmp(rdev->id_table->name, id_table->name)) {
				pr_err("Ignoring duplicate request [%d] %s\n",
				       rdev->id_table->protocol_id,
				       rdev->id_table->name);
				ret = -EINVAL;
				goto out;
			}
		}
	}

	/*
	 * No duplicate found for requested id_table, so let's create a new
	 * requested device entry for this new valid request.
	 */
	rdev = kzalloc(sizeof(*rdev), GFP_KERNEL);
	if (!rdev) {
		ret = -ENOMEM;
		goto out;
	}
	rdev->id_table = id_table;

	/*
	 * Append the new requested device table descriptor to the head of the
	 * related protocol list, eventually creating such head if not already
	 * there.
	 */
	if (!phead) {
		phead = kzalloc(sizeof(*phead), GFP_KERNEL);
		if (!phead) {
			kfree(rdev);
			ret = -ENOMEM;
			goto out;
		}
		INIT_LIST_HEAD(phead);

		ret = idr_alloc(&scmi_requested_devices, (void *)phead,
				id_table->protocol_id,
				id_table->protocol_id + 1, GFP_KERNEL);
		if (ret != id_table->protocol_id) {
			pr_err("Failed to save SCMI device - ret:%d\n", ret);
			kfree(rdev);
			kfree(phead);
			ret = -EINVAL;
			goto out;
		}
		ret = 0;
	}
	list_add(&rdev->node, phead);

	/*
	 * Now effectively create and initialize the requested device for every
	 * already initialized SCMI instance which has registered the requested
	 * protocol as a valid active one: i.e. defined in DT and supported by
	 * current platform FW.
	 */
	mutex_lock(&scmi_list_mutex);
	list_for_each_entry(info, &scmi_list, node) {
		struct device_node *child;

		child = idr_find(&info->active_protocols,
				 id_table->protocol_id);
		if (child) {
			struct scmi_device *sdev;

			sdev = scmi_get_protocol_device(child, info,
							id_table->protocol_id,
							id_table->name);
			/* Set handle if not already set: device existed */
			if (sdev && !sdev->handle)
				sdev->handle =
					scmi_handle_get_from_info_unlocked(info);
		} else {
			dev_err(info->dev,
				"Failed. SCMI protocol %d not active.\n",
				id_table->protocol_id);
		}
	}
	mutex_unlock(&scmi_list_mutex);

out:
	mutex_unlock(&scmi_requested_devices_mtx);

	return ret;
}

/**
 * scmi_protocol_device_unrequest  - Helper to unrequest a device
 *
 * @id_table: A protocol/name pair descriptor for the device to be unrequested.
 *
 * An helper to let an SCMI driver release its request about devices; note that
 * devices are created and initialized once the first SCMI driver request them
 * but they destroyed only on SCMI core unloading/unbinding.
 *
 * The current SCMI transport layer uses such devices as internal references and
 * as such they could be shared as same transport between multiple drivers so
 * that cannot be safely destroyed till the whole SCMI stack is removed.
 * (unless adding further burden of refcounting.)
 */
void scmi_protocol_device_unrequest(const struct scmi_device_id *id_table)
{
	struct list_head *phead;

	pr_debug("Unrequesting SCMI device (%s) for protocol %x\n",
		 id_table->name, id_table->protocol_id);

	mutex_lock(&scmi_requested_devices_mtx);
	phead = idr_find(&scmi_requested_devices, id_table->protocol_id);
	if (phead) {
		struct scmi_requested_dev *victim, *tmp;

		list_for_each_entry_safe(victim, tmp, phead, node) {
			if (!strcmp(victim->id_table->name, id_table->name)) {
				list_del(&victim->node);
				kfree(victim);
				break;
			}
		}

		if (list_empty(phead)) {
			idr_remove(&scmi_requested_devices,
				   id_table->protocol_id);
			kfree(phead);
		}
	}
	mutex_unlock(&scmi_requested_devices_mtx);
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
	idr_init(&info->protocols);
	mutex_init(&info->protocols_mtx);
	idr_init(&info->active_protocols);

	platform_set_drvdata(pdev, info);
	idr_init(&info->tx_idr);
	idr_init(&info->rx_idr);

	handle = &info->handle;
	handle->dev = info->dev;
	handle->version = &info->version;
	handle->devm_protocol_get = scmi_devm_protocol_get;
	handle->devm_protocol_put = scmi_devm_protocol_put;

	ret = scmi_txrx_setup(info, dev, SCMI_PROTOCOL_BASE);
	if (ret)
		return ret;

	ret = scmi_xfer_info_init(info);
	if (ret)
		return ret;

	if (scmi_notification_init(handle))
		dev_err(dev, "SCMI Notifications NOT available.\n");

	/*
	 * Trigger SCMI Base protocol initialization.
	 * It's mandatory and won't be ever released/deinit until the
	 * SCMI stack is shutdown/unloaded as a whole.
	 */
	ret = scmi_protocol_acquire(handle, SCMI_PROTOCOL_BASE);
	if (ret) {
		dev_err(dev, "unable to communicate with SCMI\n");
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

		/*
		 * Save this valid DT protocol descriptor amongst
		 * @active_protocols for this SCMI instance/
		 */
		ret = idr_alloc(&info->active_protocols, child,
				prot_id, prot_id + 1, GFP_KERNEL);
		if (ret != prot_id) {
			dev_err(dev, "SCMI protocol %d already activated. Skip\n",
				prot_id);
			continue;
		}

		of_node_get(child);
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
	int ret = 0, id;
	struct scmi_info *info = platform_get_drvdata(pdev);
	struct idr *idr = &info->tx_idr;
	struct device_node *child;

	mutex_lock(&scmi_list_mutex);
	if (info->users)
		ret = -EBUSY;
	else
		list_del(&info->node);
	mutex_unlock(&scmi_list_mutex);

	if (ret)
		return ret;

	scmi_notification_exit(&info->handle);

	mutex_lock(&info->protocols_mtx);
	idr_destroy(&info->protocols);
	mutex_unlock(&info->protocols_mtx);

	idr_for_each_entry(&info->active_protocols, child, id)
		of_node_put(child);
	idr_destroy(&info->active_protocols);

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
#ifdef CONFIG_MAILBOX
	{ .compatible = "arm,scmi", .data = &scmi_mailbox_desc },
#endif
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

	scmi_base_register();

	scmi_clock_register();
	scmi_perf_register();
	scmi_power_register();
	scmi_reset_register();
	scmi_sensors_register();
	scmi_voltage_register();
	scmi_system_register();

	return platform_driver_register(&scmi_driver);
}
subsys_initcall(scmi_driver_init);

static void __exit scmi_driver_exit(void)
{
	scmi_base_unregister();

	scmi_clock_unregister();
	scmi_perf_unregister();
	scmi_power_unregister();
	scmi_reset_unregister();
	scmi_sensors_unregister();
	scmi_voltage_unregister();
	scmi_system_unregister();

	scmi_bus_exit();

	platform_driver_unregister(&scmi_driver);
}
module_exit(scmi_driver_exit);

MODULE_ALIAS("platform: arm-scmi");
MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("ARM SCMI protocol driver");
MODULE_LICENSE("GPL v2");
