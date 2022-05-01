// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Apple RTKit IPC library
 * Copyright (C) The Asahi Linux Contributors
 */

#include "rtkit-internal.h"

enum {
	APPLE_RTKIT_PWR_STATE_OFF = 0x00, /* power off, cannot be restarted */
	APPLE_RTKIT_PWR_STATE_SLEEP = 0x01, /* sleeping, can be restarted */
	APPLE_RTKIT_PWR_STATE_QUIESCED = 0x10, /* running but no communication */
	APPLE_RTKIT_PWR_STATE_ON = 0x20, /* normal operating state */
};

enum {
	APPLE_RTKIT_EP_MGMT = 0,
	APPLE_RTKIT_EP_CRASHLOG = 1,
	APPLE_RTKIT_EP_SYSLOG = 2,
	APPLE_RTKIT_EP_DEBUG = 3,
	APPLE_RTKIT_EP_IOREPORT = 4,
	APPLE_RTKIT_EP_OSLOG = 8,
};

#define APPLE_RTKIT_MGMT_TYPE GENMASK_ULL(59, 52)

enum {
	APPLE_RTKIT_MGMT_HELLO = 1,
	APPLE_RTKIT_MGMT_HELLO_REPLY = 2,
	APPLE_RTKIT_MGMT_STARTEP = 5,
	APPLE_RTKIT_MGMT_SET_IOP_PWR_STATE = 6,
	APPLE_RTKIT_MGMT_SET_IOP_PWR_STATE_ACK = 7,
	APPLE_RTKIT_MGMT_EPMAP = 8,
	APPLE_RTKIT_MGMT_EPMAP_REPLY = 8,
	APPLE_RTKIT_MGMT_SET_AP_PWR_STATE = 0xb,
	APPLE_RTKIT_MGMT_SET_AP_PWR_STATE_ACK = 0xb,
};

#define APPLE_RTKIT_MGMT_HELLO_MINVER GENMASK_ULL(15, 0)
#define APPLE_RTKIT_MGMT_HELLO_MAXVER GENMASK_ULL(31, 16)

#define APPLE_RTKIT_MGMT_EPMAP_LAST   BIT_ULL(51)
#define APPLE_RTKIT_MGMT_EPMAP_BASE   GENMASK_ULL(34, 32)
#define APPLE_RTKIT_MGMT_EPMAP_BITMAP GENMASK_ULL(31, 0)

#define APPLE_RTKIT_MGMT_EPMAP_REPLY_MORE BIT_ULL(0)

#define APPLE_RTKIT_MGMT_STARTEP_EP   GENMASK_ULL(39, 32)
#define APPLE_RTKIT_MGMT_STARTEP_FLAG BIT_ULL(1)

#define APPLE_RTKIT_MGMT_PWR_STATE GENMASK_ULL(15, 0)

#define APPLE_RTKIT_CRASHLOG_CRASH 1

#define APPLE_RTKIT_BUFFER_REQUEST	1
#define APPLE_RTKIT_BUFFER_REQUEST_SIZE GENMASK_ULL(51, 44)
#define APPLE_RTKIT_BUFFER_REQUEST_IOVA GENMASK_ULL(41, 0)

#define APPLE_RTKIT_SYSLOG_TYPE GENMASK_ULL(59, 52)

#define APPLE_RTKIT_SYSLOG_LOG 5

#define APPLE_RTKIT_SYSLOG_INIT	     8
#define APPLE_RTKIT_SYSLOG_N_ENTRIES GENMASK_ULL(7, 0)
#define APPLE_RTKIT_SYSLOG_MSG_SIZE  GENMASK_ULL(31, 24)

#define APPLE_RTKIT_OSLOG_TYPE GENMASK_ULL(63, 56)
#define APPLE_RTKIT_OSLOG_INIT	1
#define APPLE_RTKIT_OSLOG_ACK	3

#define APPLE_RTKIT_MIN_SUPPORTED_VERSION 11
#define APPLE_RTKIT_MAX_SUPPORTED_VERSION 12

struct apple_rtkit_msg {
	struct completion *completion;
	struct apple_mbox_msg mbox_msg;
};

struct apple_rtkit_rx_work {
	struct apple_rtkit *rtk;
	u8 ep;
	u64 msg;
	struct work_struct work;
};

bool apple_rtkit_is_running(struct apple_rtkit *rtk)
{
	if (rtk->crashed)
		return false;
	if ((rtk->iop_power_state & 0xff) != APPLE_RTKIT_PWR_STATE_ON)
		return false;
	if ((rtk->ap_power_state & 0xff) != APPLE_RTKIT_PWR_STATE_ON)
		return false;
	return true;
}
EXPORT_SYMBOL_GPL(apple_rtkit_is_running);

bool apple_rtkit_is_crashed(struct apple_rtkit *rtk)
{
	return rtk->crashed;
}
EXPORT_SYMBOL_GPL(apple_rtkit_is_crashed);

static void apple_rtkit_management_send(struct apple_rtkit *rtk, u8 type,
					u64 msg)
{
	msg &= ~APPLE_RTKIT_MGMT_TYPE;
	msg |= FIELD_PREP(APPLE_RTKIT_MGMT_TYPE, type);
	apple_rtkit_send_message(rtk, APPLE_RTKIT_EP_MGMT, msg, NULL, false);
}

static void apple_rtkit_management_rx_hello(struct apple_rtkit *rtk, u64 msg)
{
	u64 reply;

	int min_ver = FIELD_GET(APPLE_RTKIT_MGMT_HELLO_MINVER, msg);
	int max_ver = FIELD_GET(APPLE_RTKIT_MGMT_HELLO_MAXVER, msg);
	int want_ver = min(APPLE_RTKIT_MAX_SUPPORTED_VERSION, max_ver);

	dev_dbg(rtk->dev, "RTKit: Min ver %d, max ver %d\n", min_ver, max_ver);

	if (min_ver > APPLE_RTKIT_MAX_SUPPORTED_VERSION) {
		dev_err(rtk->dev, "RTKit: Firmware min version %d is too new\n",
			min_ver);
		goto abort_boot;
	}

	if (max_ver < APPLE_RTKIT_MIN_SUPPORTED_VERSION) {
		dev_err(rtk->dev, "RTKit: Firmware max version %d is too old\n",
			max_ver);
		goto abort_boot;
	}

	dev_info(rtk->dev, "RTKit: Initializing (protocol version %d)\n",
		 want_ver);
	rtk->version = want_ver;

	reply = FIELD_PREP(APPLE_RTKIT_MGMT_HELLO_MINVER, want_ver);
	reply |= FIELD_PREP(APPLE_RTKIT_MGMT_HELLO_MAXVER, want_ver);
	apple_rtkit_management_send(rtk, APPLE_RTKIT_MGMT_HELLO_REPLY, reply);

	return;

abort_boot:
	rtk->boot_result = -EINVAL;
	complete_all(&rtk->epmap_completion);
}

static void apple_rtkit_management_rx_epmap(struct apple_rtkit *rtk, u64 msg)
{
	int i, ep;
	u64 reply;
	unsigned long bitmap = FIELD_GET(APPLE_RTKIT_MGMT_EPMAP_BITMAP, msg);
	u32 base = FIELD_GET(APPLE_RTKIT_MGMT_EPMAP_BASE, msg);

	dev_dbg(rtk->dev,
		"RTKit: received endpoint bitmap 0x%lx with base 0x%x\n",
		bitmap, base);

	for_each_set_bit(i, &bitmap, 32) {
		ep = 32 * base + i;
		dev_dbg(rtk->dev, "RTKit: Discovered endpoint 0x%02x\n", ep);
		set_bit(ep, rtk->endpoints);
	}

	reply = FIELD_PREP(APPLE_RTKIT_MGMT_EPMAP_BASE, base);
	if (msg & APPLE_RTKIT_MGMT_EPMAP_LAST)
		reply |= APPLE_RTKIT_MGMT_EPMAP_LAST;
	else
		reply |= APPLE_RTKIT_MGMT_EPMAP_REPLY_MORE;

	apple_rtkit_management_send(rtk, APPLE_RTKIT_MGMT_EPMAP_REPLY, reply);

	if (!(msg & APPLE_RTKIT_MGMT_EPMAP_LAST))
		return;

	for_each_set_bit(ep, rtk->endpoints, APPLE_RTKIT_APP_ENDPOINT_START) {
		switch (ep) {
		/* the management endpoint is started by default */
		case APPLE_RTKIT_EP_MGMT:
			break;

		/* without starting these RTKit refuses to boot */
		case APPLE_RTKIT_EP_SYSLOG:
		case APPLE_RTKIT_EP_CRASHLOG:
		case APPLE_RTKIT_EP_DEBUG:
		case APPLE_RTKIT_EP_IOREPORT:
		case APPLE_RTKIT_EP_OSLOG:
			dev_dbg(rtk->dev,
				"RTKit: Starting system endpoint 0x%02x\n", ep);
			apple_rtkit_start_ep(rtk, ep);
			break;

		default:
			dev_warn(rtk->dev,
				 "RTKit: Unknown system endpoint: 0x%02x\n",
				 ep);
		}
	}

	rtk->boot_result = 0;
	complete_all(&rtk->epmap_completion);
}

static void apple_rtkit_management_rx_iop_pwr_ack(struct apple_rtkit *rtk,
						  u64 msg)
{
	unsigned int new_state = FIELD_GET(APPLE_RTKIT_MGMT_PWR_STATE, msg);

	dev_dbg(rtk->dev, "RTKit: IOP power state transition: 0x%x -> 0x%x\n",
		rtk->iop_power_state, new_state);
	rtk->iop_power_state = new_state;

	complete_all(&rtk->iop_pwr_ack_completion);
}

static void apple_rtkit_management_rx_ap_pwr_ack(struct apple_rtkit *rtk,
						 u64 msg)
{
	unsigned int new_state = FIELD_GET(APPLE_RTKIT_MGMT_PWR_STATE, msg);

	dev_dbg(rtk->dev, "RTKit: AP power state transition: 0x%x -> 0x%x\n",
		rtk->ap_power_state, new_state);
	rtk->ap_power_state = new_state;

	complete_all(&rtk->ap_pwr_ack_completion);
}

static void apple_rtkit_management_rx(struct apple_rtkit *rtk, u64 msg)
{
	u8 type = FIELD_GET(APPLE_RTKIT_MGMT_TYPE, msg);

	switch (type) {
	case APPLE_RTKIT_MGMT_HELLO:
		apple_rtkit_management_rx_hello(rtk, msg);
		break;
	case APPLE_RTKIT_MGMT_EPMAP:
		apple_rtkit_management_rx_epmap(rtk, msg);
		break;
	case APPLE_RTKIT_MGMT_SET_IOP_PWR_STATE_ACK:
		apple_rtkit_management_rx_iop_pwr_ack(rtk, msg);
		break;
	case APPLE_RTKIT_MGMT_SET_AP_PWR_STATE_ACK:
		apple_rtkit_management_rx_ap_pwr_ack(rtk, msg);
		break;
	default:
		dev_warn(
			rtk->dev,
			"RTKit: unknown management message: 0x%llx (type: 0x%02x)\n",
			msg, type);
	}
}

static int apple_rtkit_common_rx_get_buffer(struct apple_rtkit *rtk,
					    struct apple_rtkit_shmem *buffer,
					    u8 ep, u64 msg)
{
	size_t n_4kpages = FIELD_GET(APPLE_RTKIT_BUFFER_REQUEST_SIZE, msg);
	u64 reply;
	int err;

	buffer->buffer = NULL;
	buffer->iomem = NULL;
	buffer->is_mapped = false;
	buffer->iova = FIELD_GET(APPLE_RTKIT_BUFFER_REQUEST_IOVA, msg);
	buffer->size = n_4kpages << 12;

	dev_dbg(rtk->dev, "RTKit: buffer request for 0x%zx bytes at %pad\n",
		buffer->size, &buffer->iova);

	if (buffer->iova &&
	    (!rtk->ops->shmem_setup || !rtk->ops->shmem_destroy)) {
		err = -EINVAL;
		goto error;
	}

	if (rtk->ops->shmem_setup) {
		err = rtk->ops->shmem_setup(rtk->cookie, buffer);
		if (err)
			goto error;
	} else {
		buffer->buffer = dma_alloc_coherent(rtk->dev, buffer->size,
						    &buffer->iova, GFP_KERNEL);
		if (!buffer->buffer) {
			err = -ENOMEM;
			goto error;
		}
	}

	if (!buffer->is_mapped) {
		reply = FIELD_PREP(APPLE_RTKIT_SYSLOG_TYPE,
				   APPLE_RTKIT_BUFFER_REQUEST);
		reply |= FIELD_PREP(APPLE_RTKIT_BUFFER_REQUEST_SIZE, n_4kpages);
		reply |= FIELD_PREP(APPLE_RTKIT_BUFFER_REQUEST_IOVA,
				    buffer->iova);
		apple_rtkit_send_message(rtk, ep, reply, NULL, false);
	}

	return 0;

error:
	buffer->buffer = NULL;
	buffer->iomem = NULL;
	buffer->iova = 0;
	buffer->size = 0;
	buffer->is_mapped = false;
	return err;
}

static void apple_rtkit_free_buffer(struct apple_rtkit *rtk,
				    struct apple_rtkit_shmem *bfr)
{
	if (bfr->size == 0)
		return;

	if (rtk->ops->shmem_destroy)
		rtk->ops->shmem_destroy(rtk->cookie, bfr);
	else if (bfr->buffer)
		dma_free_coherent(rtk->dev, bfr->size, bfr->buffer, bfr->iova);

	bfr->buffer = NULL;
	bfr->iomem = NULL;
	bfr->iova = 0;
	bfr->size = 0;
	bfr->is_mapped = false;
}

static void apple_rtkit_memcpy(struct apple_rtkit *rtk, void *dst,
			       struct apple_rtkit_shmem *bfr, size_t offset,
			       size_t len)
{
	if (bfr->iomem)
		memcpy_fromio(dst, bfr->iomem + offset, len);
	else
		memcpy(dst, bfr->buffer + offset, len);
}

static void apple_rtkit_crashlog_rx(struct apple_rtkit *rtk, u64 msg)
{
	u8 type = FIELD_GET(APPLE_RTKIT_SYSLOG_TYPE, msg);
	u8 *bfr;

	if (type != APPLE_RTKIT_CRASHLOG_CRASH) {
		dev_warn(rtk->dev, "RTKit: Unknown crashlog message: %llx\n",
			 msg);
		return;
	}

	if (!rtk->crashlog_buffer.size) {
		apple_rtkit_common_rx_get_buffer(rtk, &rtk->crashlog_buffer,
						 APPLE_RTKIT_EP_CRASHLOG, msg);
		return;
	}

	dev_err(rtk->dev, "RTKit: co-processor has crashed\n");

	/*
	 * create a shadow copy here to make sure the co-processor isn't able
	 * to change the log while we're dumping it. this also ensures
	 * the buffer is in normal memory and not iomem for e.g. the SMC
	 */
	bfr = kzalloc(rtk->crashlog_buffer.size, GFP_KERNEL);
	if (bfr) {
		apple_rtkit_memcpy(rtk, bfr, &rtk->crashlog_buffer, 0,
				   rtk->crashlog_buffer.size);
		apple_rtkit_crashlog_dump(rtk, bfr, rtk->crashlog_buffer.size);
		kfree(bfr);
	} else {
		dev_err(rtk->dev,
			"RTKit: Couldn't allocate crashlog shadow buffer\n");
	}

	rtk->crashed = true;
	if (rtk->ops->crashed)
		rtk->ops->crashed(rtk->cookie);
}

static void apple_rtkit_ioreport_rx(struct apple_rtkit *rtk, u64 msg)
{
	u8 type = FIELD_GET(APPLE_RTKIT_SYSLOG_TYPE, msg);

	switch (type) {
	case APPLE_RTKIT_BUFFER_REQUEST:
		apple_rtkit_common_rx_get_buffer(rtk, &rtk->ioreport_buffer,
						 APPLE_RTKIT_EP_IOREPORT, msg);
		break;
	/* unknown, must be ACKed or the co-processor will hang */
	case 0x8:
	case 0xc:
		apple_rtkit_send_message(rtk, APPLE_RTKIT_EP_IOREPORT, msg,
					 NULL, false);
		break;
	default:
		dev_warn(rtk->dev, "RTKit: Unknown ioreport message: %llx\n",
			 msg);
	}
}

static void apple_rtkit_syslog_rx_init(struct apple_rtkit *rtk, u64 msg)
{
	rtk->syslog_n_entries = FIELD_GET(APPLE_RTKIT_SYSLOG_N_ENTRIES, msg);
	rtk->syslog_msg_size = FIELD_GET(APPLE_RTKIT_SYSLOG_MSG_SIZE, msg);

	rtk->syslog_msg_buffer = kzalloc(rtk->syslog_msg_size, GFP_KERNEL);

	dev_dbg(rtk->dev,
		"RTKit: syslog initialized: entries: %zd, msg_size: %zd\n",
		rtk->syslog_n_entries, rtk->syslog_msg_size);
}

static void apple_rtkit_syslog_rx_log(struct apple_rtkit *rtk, u64 msg)
{
	u8 idx = msg & 0xff;
	char log_context[24];
	size_t entry_size = 0x20 + rtk->syslog_msg_size;

	if (!rtk->syslog_msg_buffer) {
		dev_warn(
			rtk->dev,
			"RTKit: received syslog message but no syslog_msg_buffer\n");
		goto done;
	}
	if (!rtk->syslog_buffer.size) {
		dev_warn(
			rtk->dev,
			"RTKit: received syslog message but syslog_buffer.size is zero\n");
		goto done;
	}
	if (!rtk->syslog_buffer.buffer && !rtk->syslog_buffer.iomem) {
		dev_warn(
			rtk->dev,
			"RTKit: received syslog message but no syslog_buffer.buffer or syslog_buffer.iomem\n");
		goto done;
	}
	if (idx > rtk->syslog_n_entries) {
		dev_warn(rtk->dev, "RTKit: syslog index %d out of range\n",
			 idx);
		goto done;
	}

	apple_rtkit_memcpy(rtk, log_context, &rtk->syslog_buffer,
			   idx * entry_size + 8, sizeof(log_context));
	apple_rtkit_memcpy(rtk, rtk->syslog_msg_buffer, &rtk->syslog_buffer,
			   idx * entry_size + 8 + sizeof(log_context),
			   rtk->syslog_msg_size);

	log_context[sizeof(log_context) - 1] = 0;
	rtk->syslog_msg_buffer[rtk->syslog_msg_size - 1] = 0;
	dev_info(rtk->dev, "RTKit: syslog message: %s: %s\n", log_context,
		 rtk->syslog_msg_buffer);

done:
	apple_rtkit_send_message(rtk, APPLE_RTKIT_EP_SYSLOG, msg, NULL, false);
}

static void apple_rtkit_syslog_rx(struct apple_rtkit *rtk, u64 msg)
{
	u8 type = FIELD_GET(APPLE_RTKIT_SYSLOG_TYPE, msg);

	switch (type) {
	case APPLE_RTKIT_BUFFER_REQUEST:
		apple_rtkit_common_rx_get_buffer(rtk, &rtk->syslog_buffer,
						 APPLE_RTKIT_EP_SYSLOG, msg);
		break;
	case APPLE_RTKIT_SYSLOG_INIT:
		apple_rtkit_syslog_rx_init(rtk, msg);
		break;
	case APPLE_RTKIT_SYSLOG_LOG:
		apple_rtkit_syslog_rx_log(rtk, msg);
		break;
	default:
		dev_warn(rtk->dev, "RTKit: Unknown syslog message: %llx\n",
			 msg);
	}
}

static void apple_rtkit_oslog_rx_init(struct apple_rtkit *rtk, u64 msg)
{
	u64 ack;

	dev_dbg(rtk->dev, "RTKit: oslog init: msg: 0x%llx\n", msg);
	ack = FIELD_PREP(APPLE_RTKIT_OSLOG_TYPE, APPLE_RTKIT_OSLOG_ACK);
	apple_rtkit_send_message(rtk, APPLE_RTKIT_EP_OSLOG, ack, NULL, false);
}

static void apple_rtkit_oslog_rx(struct apple_rtkit *rtk, u64 msg)
{
	u8 type = FIELD_GET(APPLE_RTKIT_OSLOG_TYPE, msg);

	switch (type) {
	case APPLE_RTKIT_OSLOG_INIT:
		apple_rtkit_oslog_rx_init(rtk, msg);
		break;
	default:
		dev_warn(rtk->dev, "RTKit: Unknown oslog message: %llx\n", msg);
	}
}

static void apple_rtkit_rx_work(struct work_struct *work)
{
	struct apple_rtkit_rx_work *rtk_work =
		container_of(work, struct apple_rtkit_rx_work, work);
	struct apple_rtkit *rtk = rtk_work->rtk;

	switch (rtk_work->ep) {
	case APPLE_RTKIT_EP_MGMT:
		apple_rtkit_management_rx(rtk, rtk_work->msg);
		break;
	case APPLE_RTKIT_EP_CRASHLOG:
		apple_rtkit_crashlog_rx(rtk, rtk_work->msg);
		break;
	case APPLE_RTKIT_EP_SYSLOG:
		apple_rtkit_syslog_rx(rtk, rtk_work->msg);
		break;
	case APPLE_RTKIT_EP_IOREPORT:
		apple_rtkit_ioreport_rx(rtk, rtk_work->msg);
		break;
	case APPLE_RTKIT_EP_OSLOG:
		apple_rtkit_oslog_rx(rtk, rtk_work->msg);
		break;
	case APPLE_RTKIT_APP_ENDPOINT_START ... 0xff:
		if (rtk->ops->recv_message)
			rtk->ops->recv_message(rtk->cookie, rtk_work->ep,
					       rtk_work->msg);
		else
			dev_warn(
				rtk->dev,
				"Received unexpected message to EP%02d: %llx\n",
				rtk_work->ep, rtk_work->msg);
		break;
	default:
		dev_warn(rtk->dev,
			 "RTKit: message to unknown endpoint %02x: %llx\n",
			 rtk_work->ep, rtk_work->msg);
	}

	kfree(rtk_work);
}

static void apple_rtkit_rx(struct mbox_client *cl, void *mssg)
{
	struct apple_rtkit *rtk = container_of(cl, struct apple_rtkit, mbox_cl);
	struct apple_mbox_msg *msg = mssg;
	struct apple_rtkit_rx_work *work;
	u8 ep = msg->msg1;

	/*
	 * The message was read from a MMIO FIFO and we have to make
	 * sure all reads from buffers sent with that message happen
	 * afterwards.
	 */
	dma_rmb();

	if (!test_bit(ep, rtk->endpoints))
		dev_warn(rtk->dev,
			 "RTKit: Message to undiscovered endpoint 0x%02x\n",
			 ep);

	if (ep >= APPLE_RTKIT_APP_ENDPOINT_START &&
	    rtk->ops->recv_message_early &&
	    rtk->ops->recv_message_early(rtk->cookie, ep, msg->msg0))
		return;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return;

	work->rtk = rtk;
	work->ep = ep;
	work->msg = msg->msg0;
	INIT_WORK(&work->work, apple_rtkit_rx_work);
	queue_work(rtk->wq, &work->work);
}

static void apple_rtkit_tx_done(struct mbox_client *cl, void *mssg, int r)
{
	struct apple_rtkit_msg *msg =
		container_of(mssg, struct apple_rtkit_msg, mbox_msg);

	if (r == -ETIME)
		return;

	if (msg->completion)
		complete(msg->completion);
	kfree(msg);
}

int apple_rtkit_send_message(struct apple_rtkit *rtk, u8 ep, u64 message,
			     struct completion *completion, bool atomic)
{
	struct apple_rtkit_msg *msg;
	int ret;
	gfp_t flags;

	if (rtk->crashed)
		return -EINVAL;
	if (ep >= APPLE_RTKIT_APP_ENDPOINT_START &&
	    !apple_rtkit_is_running(rtk))
		return -EINVAL;

	if (atomic)
		flags = GFP_ATOMIC;
	else
		flags = GFP_KERNEL;

	msg = kzalloc(sizeof(*msg), flags);
	if (!msg)
		return -ENOMEM;

	msg->mbox_msg.msg0 = message;
	msg->mbox_msg.msg1 = ep;
	msg->completion = completion;

	/*
	 * The message will be sent with a MMIO write. We need the barrier
	 * here to ensure any previous writes to buffers are visible to the
	 * device before that MMIO write happens.
	 */
	dma_wmb();

	ret = mbox_send_message(rtk->mbox_chan, &msg->mbox_msg);
	if (ret < 0) {
		kfree(msg);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(apple_rtkit_send_message);

int apple_rtkit_send_message_wait(struct apple_rtkit *rtk, u8 ep, u64 message,
				  unsigned long timeout, bool atomic)
{
	DECLARE_COMPLETION_ONSTACK(completion);
	int ret;
	long t;

	ret = apple_rtkit_send_message(rtk, ep, message, &completion, atomic);
	if (ret < 0)
		return ret;

	if (atomic) {
		ret = mbox_flush(rtk->mbox_chan, timeout);
		if (ret < 0)
			return ret;

		if (try_wait_for_completion(&completion))
			return 0;

		return -ETIME;
	} else {
		t = wait_for_completion_interruptible_timeout(
			&completion, msecs_to_jiffies(timeout));
		if (t < 0)
			return t;
		else if (t == 0)
			return -ETIME;
		return 0;
	}
}
EXPORT_SYMBOL_GPL(apple_rtkit_send_message_wait);

int apple_rtkit_start_ep(struct apple_rtkit *rtk, u8 endpoint)
{
	u64 msg;

	if (!test_bit(endpoint, rtk->endpoints))
		return -EINVAL;
	if (endpoint >= APPLE_RTKIT_APP_ENDPOINT_START &&
	    !apple_rtkit_is_running(rtk))
		return -EINVAL;

	msg = FIELD_PREP(APPLE_RTKIT_MGMT_STARTEP_EP, endpoint);
	msg |= APPLE_RTKIT_MGMT_STARTEP_FLAG;
	apple_rtkit_management_send(rtk, APPLE_RTKIT_MGMT_STARTEP, msg);

	return 0;
}
EXPORT_SYMBOL_GPL(apple_rtkit_start_ep);

static int apple_rtkit_request_mbox_chan(struct apple_rtkit *rtk)
{
	if (rtk->mbox_name)
		rtk->mbox_chan = mbox_request_channel_byname(&rtk->mbox_cl,
							     rtk->mbox_name);
	else
		rtk->mbox_chan =
			mbox_request_channel(&rtk->mbox_cl, rtk->mbox_idx);

	if (IS_ERR(rtk->mbox_chan))
		return PTR_ERR(rtk->mbox_chan);
	return 0;
}

static struct apple_rtkit *apple_rtkit_init(struct device *dev, void *cookie,
					    const char *mbox_name, int mbox_idx,
					    const struct apple_rtkit_ops *ops)
{
	struct apple_rtkit *rtk;
	int ret;

	if (!ops)
		return ERR_PTR(-EINVAL);

	rtk = kzalloc(sizeof(*rtk), GFP_KERNEL);
	if (!rtk)
		return ERR_PTR(-ENOMEM);

	rtk->dev = dev;
	rtk->cookie = cookie;
	rtk->ops = ops;

	init_completion(&rtk->epmap_completion);
	init_completion(&rtk->iop_pwr_ack_completion);
	init_completion(&rtk->ap_pwr_ack_completion);

	bitmap_zero(rtk->endpoints, APPLE_RTKIT_MAX_ENDPOINTS);
	set_bit(APPLE_RTKIT_EP_MGMT, rtk->endpoints);

	rtk->mbox_name = mbox_name;
	rtk->mbox_idx = mbox_idx;
	rtk->mbox_cl.dev = dev;
	rtk->mbox_cl.tx_block = false;
	rtk->mbox_cl.knows_txdone = false;
	rtk->mbox_cl.rx_callback = &apple_rtkit_rx;
	rtk->mbox_cl.tx_done = &apple_rtkit_tx_done;

	rtk->wq = alloc_ordered_workqueue("rtkit-%s", WQ_MEM_RECLAIM,
					  dev_name(rtk->dev));
	if (!rtk->wq) {
		ret = -ENOMEM;
		goto free_rtk;
	}

	ret = apple_rtkit_request_mbox_chan(rtk);
	if (ret)
		goto destroy_wq;

	return rtk;

destroy_wq:
	destroy_workqueue(rtk->wq);
free_rtk:
	kfree(rtk);
	return ERR_PTR(ret);
}

static int apple_rtkit_wait_for_completion(struct completion *c)
{
	long t;

	t = wait_for_completion_interruptible_timeout(c,
						      msecs_to_jiffies(1000));
	if (t < 0)
		return t;
	else if (t == 0)
		return -ETIME;
	else
		return 0;
}

int apple_rtkit_reinit(struct apple_rtkit *rtk)
{
	/* make sure we don't handle any messages while reinitializing */
	mbox_free_channel(rtk->mbox_chan);
	flush_workqueue(rtk->wq);

	apple_rtkit_free_buffer(rtk, &rtk->ioreport_buffer);
	apple_rtkit_free_buffer(rtk, &rtk->crashlog_buffer);
	apple_rtkit_free_buffer(rtk, &rtk->syslog_buffer);

	kfree(rtk->syslog_msg_buffer);

	rtk->syslog_msg_buffer = NULL;
	rtk->syslog_n_entries = 0;
	rtk->syslog_msg_size = 0;

	bitmap_zero(rtk->endpoints, APPLE_RTKIT_MAX_ENDPOINTS);
	set_bit(APPLE_RTKIT_EP_MGMT, rtk->endpoints);

	reinit_completion(&rtk->epmap_completion);
	reinit_completion(&rtk->iop_pwr_ack_completion);
	reinit_completion(&rtk->ap_pwr_ack_completion);

	rtk->crashed = false;
	rtk->iop_power_state = APPLE_RTKIT_PWR_STATE_OFF;
	rtk->ap_power_state = APPLE_RTKIT_PWR_STATE_OFF;

	return apple_rtkit_request_mbox_chan(rtk);
}
EXPORT_SYMBOL_GPL(apple_rtkit_reinit);

static int apple_rtkit_set_ap_power_state(struct apple_rtkit *rtk,
					  unsigned int state)
{
	u64 msg;
	int ret;

	reinit_completion(&rtk->ap_pwr_ack_completion);

	msg = FIELD_PREP(APPLE_RTKIT_MGMT_PWR_STATE, state);
	apple_rtkit_management_send(rtk, APPLE_RTKIT_MGMT_SET_AP_PWR_STATE,
				    msg);

	ret = apple_rtkit_wait_for_completion(&rtk->ap_pwr_ack_completion);
	if (ret)
		return ret;

	if (rtk->ap_power_state != state)
		return -EINVAL;
	return 0;
}

static int apple_rtkit_set_iop_power_state(struct apple_rtkit *rtk,
					   unsigned int state)
{
	u64 msg;
	int ret;

	reinit_completion(&rtk->iop_pwr_ack_completion);

	msg = FIELD_PREP(APPLE_RTKIT_MGMT_PWR_STATE, state);
	apple_rtkit_management_send(rtk, APPLE_RTKIT_MGMT_SET_IOP_PWR_STATE,
				    msg);

	ret = apple_rtkit_wait_for_completion(&rtk->iop_pwr_ack_completion);
	if (ret)
		return ret;

	if (rtk->iop_power_state != state)
		return -EINVAL;
	return 0;
}

int apple_rtkit_boot(struct apple_rtkit *rtk)
{
	int ret;

	if (apple_rtkit_is_running(rtk))
		return 0;
	if (rtk->crashed)
		return -EINVAL;

	dev_dbg(rtk->dev, "RTKit: waiting for boot to finish\n");
	ret = apple_rtkit_wait_for_completion(&rtk->epmap_completion);
	if (ret)
		return ret;
	if (rtk->boot_result)
		return rtk->boot_result;

	dev_dbg(rtk->dev, "RTKit: waiting for IOP power state ACK\n");
	ret = apple_rtkit_wait_for_completion(&rtk->iop_pwr_ack_completion);
	if (ret)
		return ret;

	return apple_rtkit_set_ap_power_state(rtk, APPLE_RTKIT_PWR_STATE_ON);
}
EXPORT_SYMBOL_GPL(apple_rtkit_boot);

int apple_rtkit_shutdown(struct apple_rtkit *rtk)
{
	int ret;

	/* if OFF is used here the co-processor will not wake up again */
	ret = apple_rtkit_set_ap_power_state(rtk,
					     APPLE_RTKIT_PWR_STATE_QUIESCED);
	if (ret)
		return ret;

	ret = apple_rtkit_set_iop_power_state(rtk, APPLE_RTKIT_PWR_STATE_SLEEP);
	if (ret)
		return ret;

	return apple_rtkit_reinit(rtk);
}
EXPORT_SYMBOL_GPL(apple_rtkit_shutdown);

int apple_rtkit_quiesce(struct apple_rtkit *rtk)
{
	int ret;

	ret = apple_rtkit_set_ap_power_state(rtk,
					     APPLE_RTKIT_PWR_STATE_QUIESCED);
	if (ret)
		return ret;

	ret = apple_rtkit_set_iop_power_state(rtk,
					      APPLE_RTKIT_PWR_STATE_QUIESCED);
	if (ret)
		return ret;

	ret = apple_rtkit_reinit(rtk);
	if (ret)
		return ret;

	rtk->iop_power_state = APPLE_RTKIT_PWR_STATE_QUIESCED;
	rtk->ap_power_state = APPLE_RTKIT_PWR_STATE_QUIESCED;
	return 0;
}
EXPORT_SYMBOL_GPL(apple_rtkit_quiesce);

int apple_rtkit_wake(struct apple_rtkit *rtk)
{
	u64 msg;

	if (apple_rtkit_is_running(rtk))
		return -EINVAL;

	reinit_completion(&rtk->iop_pwr_ack_completion);

	/*
	 * Use open-coded apple_rtkit_set_iop_power_state since apple_rtkit_boot
	 * will wait for the completion anyway.
	 */
	msg = FIELD_PREP(APPLE_RTKIT_MGMT_PWR_STATE, APPLE_RTKIT_PWR_STATE_ON);
	apple_rtkit_management_send(rtk, APPLE_RTKIT_MGMT_SET_IOP_PWR_STATE,
				    msg);

	return apple_rtkit_boot(rtk);
}
EXPORT_SYMBOL_GPL(apple_rtkit_wake);

static void apple_rtkit_free(struct apple_rtkit *rtk)
{
	mbox_free_channel(rtk->mbox_chan);
	destroy_workqueue(rtk->wq);

	apple_rtkit_free_buffer(rtk, &rtk->ioreport_buffer);
	apple_rtkit_free_buffer(rtk, &rtk->crashlog_buffer);
	apple_rtkit_free_buffer(rtk, &rtk->syslog_buffer);

	kfree(rtk->syslog_msg_buffer);
	kfree(rtk);
}

struct apple_rtkit *devm_apple_rtkit_init(struct device *dev, void *cookie,
					  const char *mbox_name, int mbox_idx,
					  const struct apple_rtkit_ops *ops)
{
	struct apple_rtkit *rtk;
	int ret;

	rtk = apple_rtkit_init(dev, cookie, mbox_name, mbox_idx, ops);
	if (IS_ERR(rtk))
		return rtk;

	ret = devm_add_action_or_reset(dev, (void (*)(void *))apple_rtkit_free,
				       rtk);
	if (ret)
		return ERR_PTR(ret);

	return rtk;
}
EXPORT_SYMBOL_GPL(devm_apple_rtkit_init);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Sven Peter <sven@svenpeter.dev>");
MODULE_DESCRIPTION("Apple RTKit driver");
