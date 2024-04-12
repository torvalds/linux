// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2016, 2023
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *
 * Adjunct processor bus, queue related code.
 */

#define KMSG_COMPONENT "ap"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/init.h>
#include <linux/slab.h>
#include <asm/facility.h>

#include "ap_bus.h"
#include "ap_debug.h"

static void __ap_flush_queue(struct ap_queue *aq);

/*
 * some AP queue helper functions
 */

static inline bool ap_q_supports_bind(struct ap_queue *aq)
{
	return aq->card->hwinfo.ep11 || aq->card->hwinfo.accel;
}

static inline bool ap_q_supports_assoc(struct ap_queue *aq)
{
	return aq->card->hwinfo.ep11;
}

static inline bool ap_q_needs_bind(struct ap_queue *aq)
{
	return ap_q_supports_bind(aq) && ap_sb_available();
}

/**
 * ap_queue_enable_irq(): Enable interrupt support on this AP queue.
 * @aq: The AP queue
 * @ind: the notification indicator byte
 *
 * Enables interruption on AP queue via ap_aqic(). Based on the return
 * value it waits a while and tests the AP queue if interrupts
 * have been switched on using ap_test_queue().
 */
static int ap_queue_enable_irq(struct ap_queue *aq, void *ind)
{
	union ap_qirq_ctrl qirqctrl = { .value = 0 };
	struct ap_queue_status status;

	qirqctrl.ir = 1;
	qirqctrl.isc = AP_ISC;
	status = ap_aqic(aq->qid, qirqctrl, virt_to_phys(ind));
	if (status.async)
		return -EPERM;
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
	case AP_RESPONSE_OTHERWISE_CHANGED:
		return 0;
	case AP_RESPONSE_Q_NOT_AVAIL:
	case AP_RESPONSE_DECONFIGURED:
	case AP_RESPONSE_CHECKSTOPPED:
	case AP_RESPONSE_INVALID_ADDRESS:
		pr_err("Registering adapter interrupts for AP device %02x.%04x failed\n",
		       AP_QID_CARD(aq->qid),
		       AP_QID_QUEUE(aq->qid));
		return -EOPNOTSUPP;
	case AP_RESPONSE_RESET_IN_PROGRESS:
	case AP_RESPONSE_BUSY:
	default:
		return -EBUSY;
	}
}

/**
 * __ap_send(): Send message to adjunct processor queue.
 * @qid: The AP queue number
 * @psmid: The program supplied message identifier
 * @msg: The message text
 * @msglen: The message length
 * @special: Special Bit
 *
 * Returns AP queue status structure.
 * Condition code 1 on NQAP can't happen because the L bit is 1.
 * Condition code 2 on NQAP also means the send is incomplete,
 * because a segment boundary was reached. The NQAP is repeated.
 */
static inline struct ap_queue_status
__ap_send(ap_qid_t qid, unsigned long psmid, void *msg, size_t msglen,
	  int special)
{
	if (special)
		qid |= 0x400000UL;
	return ap_nqap(qid, psmid, msg, msglen);
}

/* State machine definitions and helpers */

static enum ap_sm_wait ap_sm_nop(struct ap_queue *aq)
{
	return AP_SM_WAIT_NONE;
}

/**
 * ap_sm_recv(): Receive pending reply messages from an AP queue but do
 *	not change the state of the device.
 * @aq: pointer to the AP queue
 *
 * Returns AP_SM_WAIT_NONE, AP_SM_WAIT_AGAIN, or AP_SM_WAIT_INTERRUPT
 */
static struct ap_queue_status ap_sm_recv(struct ap_queue *aq)
{
	struct ap_queue_status status;
	struct ap_message *ap_msg;
	bool found = false;
	size_t reslen;
	unsigned long resgr0 = 0;
	int parts = 0;

	/*
	 * DQAP loop until response code and resgr0 indicate that
	 * the msg is totally received. As we use the very same buffer
	 * the msg is overwritten with each invocation. That's intended
	 * and the receiver of the msg is informed with a msg rc code
	 * of EMSGSIZE in such a case.
	 */
	do {
		status = ap_dqap(aq->qid, &aq->reply->psmid,
				 aq->reply->msg, aq->reply->bufsize,
				 &aq->reply->len, &reslen, &resgr0);
		parts++;
	} while (status.response_code == 0xFF && resgr0 != 0);

	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		print_hex_dump_debug("aprpl: ", DUMP_PREFIX_ADDRESS, 16, 1,
				     aq->reply->msg, aq->reply->len, false);
		aq->queue_count = max_t(int, 0, aq->queue_count - 1);
		if (!status.queue_empty && !aq->queue_count)
			aq->queue_count++;
		if (aq->queue_count > 0)
			mod_timer(&aq->timeout,
				  jiffies + aq->request_timeout);
		list_for_each_entry(ap_msg, &aq->pendingq, list) {
			if (ap_msg->psmid != aq->reply->psmid)
				continue;
			list_del_init(&ap_msg->list);
			aq->pendingq_count--;
			if (parts > 1) {
				ap_msg->rc = -EMSGSIZE;
				ap_msg->receive(aq, ap_msg, NULL);
			} else {
				ap_msg->receive(aq, ap_msg, aq->reply);
			}
			found = true;
			break;
		}
		if (!found) {
			AP_DBF_WARN("%s unassociated reply psmid=0x%016lx on 0x%02x.%04x\n",
				    __func__, aq->reply->psmid,
				    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		}
		fallthrough;
	case AP_RESPONSE_NO_PENDING_REPLY:
		if (!status.queue_empty || aq->queue_count <= 0)
			break;
		/* The card shouldn't forget requests but who knows. */
		aq->queue_count = 0;
		list_splice_init(&aq->pendingq, &aq->requestq);
		aq->requestq_count += aq->pendingq_count;
		pr_debug("%s queue 0x%02x.%04x rescheduled %d reqs (new req %d)\n",
			 __func__, AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid),
			 aq->pendingq_count, aq->requestq_count);
		aq->pendingq_count = 0;
		break;
	default:
		break;
	}
	return status;
}

/**
 * ap_sm_read(): Receive pending reply messages from an AP queue.
 * @aq: pointer to the AP queue
 *
 * Returns AP_SM_WAIT_NONE, AP_SM_WAIT_AGAIN, or AP_SM_WAIT_INTERRUPT
 */
static enum ap_sm_wait ap_sm_read(struct ap_queue *aq)
{
	struct ap_queue_status status;

	if (!aq->reply)
		return AP_SM_WAIT_NONE;
	status = ap_sm_recv(aq);
	if (status.async)
		return AP_SM_WAIT_NONE;
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		if (aq->queue_count > 0) {
			aq->sm_state = AP_SM_STATE_WORKING;
			return AP_SM_WAIT_AGAIN;
		}
		aq->sm_state = AP_SM_STATE_IDLE;
		break;
	case AP_RESPONSE_NO_PENDING_REPLY:
		if (aq->queue_count > 0)
			return status.irq_enabled ?
				AP_SM_WAIT_INTERRUPT : AP_SM_WAIT_HIGH_TIMEOUT;
		aq->sm_state = AP_SM_STATE_IDLE;
		break;
	default:
		aq->dev_state = AP_DEV_STATE_ERROR;
		aq->last_err_rc = status.response_code;
		AP_DBF_WARN("%s RC 0x%02x on 0x%02x.%04x -> AP_DEV_STATE_ERROR\n",
			    __func__, status.response_code,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		return AP_SM_WAIT_NONE;
	}
	/* Check and maybe enable irq support (again) on this queue */
	if (!status.irq_enabled && status.queue_empty) {
		void *lsi_ptr = ap_airq_ptr();

		if (lsi_ptr && ap_queue_enable_irq(aq, lsi_ptr) == 0) {
			aq->sm_state = AP_SM_STATE_SETIRQ_WAIT;
			return AP_SM_WAIT_AGAIN;
		}
	}
	return AP_SM_WAIT_NONE;
}

/**
 * ap_sm_write(): Send messages from the request queue to an AP queue.
 * @aq: pointer to the AP queue
 *
 * Returns AP_SM_WAIT_NONE, AP_SM_WAIT_AGAIN, or AP_SM_WAIT_INTERRUPT
 */
static enum ap_sm_wait ap_sm_write(struct ap_queue *aq)
{
	struct ap_queue_status status;
	struct ap_message *ap_msg;
	ap_qid_t qid = aq->qid;

	if (aq->requestq_count <= 0)
		return AP_SM_WAIT_NONE;

	/* Start the next request on the queue. */
	ap_msg = list_entry(aq->requestq.next, struct ap_message, list);
	print_hex_dump_debug("apreq: ", DUMP_PREFIX_ADDRESS, 16, 1,
			     ap_msg->msg, ap_msg->len, false);
	status = __ap_send(qid, ap_msg->psmid,
			   ap_msg->msg, ap_msg->len,
			   ap_msg->flags & AP_MSG_FLAG_SPECIAL);
	if (status.async)
		return AP_SM_WAIT_NONE;
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		aq->queue_count = max_t(int, 1, aq->queue_count + 1);
		if (aq->queue_count == 1)
			mod_timer(&aq->timeout, jiffies + aq->request_timeout);
		list_move_tail(&ap_msg->list, &aq->pendingq);
		aq->requestq_count--;
		aq->pendingq_count++;
		if (aq->queue_count < aq->card->hwinfo.qd) {
			aq->sm_state = AP_SM_STATE_WORKING;
			return AP_SM_WAIT_AGAIN;
		}
		fallthrough;
	case AP_RESPONSE_Q_FULL:
		aq->sm_state = AP_SM_STATE_QUEUE_FULL;
		return status.irq_enabled ?
			AP_SM_WAIT_INTERRUPT : AP_SM_WAIT_HIGH_TIMEOUT;
	case AP_RESPONSE_RESET_IN_PROGRESS:
		aq->sm_state = AP_SM_STATE_RESET_WAIT;
		return AP_SM_WAIT_LOW_TIMEOUT;
	case AP_RESPONSE_INVALID_DOMAIN:
		AP_DBF_WARN("%s RESPONSE_INVALID_DOMAIN on NQAP\n", __func__);
		fallthrough;
	case AP_RESPONSE_MESSAGE_TOO_BIG:
	case AP_RESPONSE_REQ_FAC_NOT_INST:
		list_del_init(&ap_msg->list);
		aq->requestq_count--;
		ap_msg->rc = -EINVAL;
		ap_msg->receive(aq, ap_msg, NULL);
		return AP_SM_WAIT_AGAIN;
	default:
		aq->dev_state = AP_DEV_STATE_ERROR;
		aq->last_err_rc = status.response_code;
		AP_DBF_WARN("%s RC 0x%02x on 0x%02x.%04x -> AP_DEV_STATE_ERROR\n",
			    __func__, status.response_code,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		return AP_SM_WAIT_NONE;
	}
}

/**
 * ap_sm_read_write(): Send and receive messages to/from an AP queue.
 * @aq: pointer to the AP queue
 *
 * Returns AP_SM_WAIT_NONE, AP_SM_WAIT_AGAIN, or AP_SM_WAIT_INTERRUPT
 */
static enum ap_sm_wait ap_sm_read_write(struct ap_queue *aq)
{
	return min(ap_sm_read(aq), ap_sm_write(aq));
}

/**
 * ap_sm_reset(): Reset an AP queue.
 * @aq: The AP queue
 *
 * Submit the Reset command to an AP queue.
 */
static enum ap_sm_wait ap_sm_reset(struct ap_queue *aq)
{
	struct ap_queue_status status;

	status = ap_rapq(aq->qid, aq->rapq_fbit);
	if (status.async)
		return AP_SM_WAIT_NONE;
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
	case AP_RESPONSE_RESET_IN_PROGRESS:
		aq->sm_state = AP_SM_STATE_RESET_WAIT;
		aq->rapq_fbit = 0;
		return AP_SM_WAIT_LOW_TIMEOUT;
	default:
		aq->dev_state = AP_DEV_STATE_ERROR;
		aq->last_err_rc = status.response_code;
		AP_DBF_WARN("%s RC 0x%02x on 0x%02x.%04x -> AP_DEV_STATE_ERROR\n",
			    __func__, status.response_code,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		return AP_SM_WAIT_NONE;
	}
}

/**
 * ap_sm_reset_wait(): Test queue for completion of the reset operation
 * @aq: pointer to the AP queue
 *
 * Returns AP_POLL_IMMEDIATELY, AP_POLL_AFTER_TIMEROUT or 0.
 */
static enum ap_sm_wait ap_sm_reset_wait(struct ap_queue *aq)
{
	struct ap_queue_status status;
	struct ap_tapq_hwinfo hwinfo;
	void *lsi_ptr;

	/* Get the status with TAPQ */
	status = ap_test_queue(aq->qid, 1, &hwinfo);

	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		aq->se_bstate = hwinfo.bs;
		lsi_ptr = ap_airq_ptr();
		if (lsi_ptr && ap_queue_enable_irq(aq, lsi_ptr) == 0)
			aq->sm_state = AP_SM_STATE_SETIRQ_WAIT;
		else
			aq->sm_state = (aq->queue_count > 0) ?
				AP_SM_STATE_WORKING : AP_SM_STATE_IDLE;
		return AP_SM_WAIT_AGAIN;
	case AP_RESPONSE_BUSY:
	case AP_RESPONSE_RESET_IN_PROGRESS:
		return AP_SM_WAIT_LOW_TIMEOUT;
	case AP_RESPONSE_Q_NOT_AVAIL:
	case AP_RESPONSE_DECONFIGURED:
	case AP_RESPONSE_CHECKSTOPPED:
	default:
		aq->dev_state = AP_DEV_STATE_ERROR;
		aq->last_err_rc = status.response_code;
		AP_DBF_WARN("%s RC 0x%02x on 0x%02x.%04x -> AP_DEV_STATE_ERROR\n",
			    __func__, status.response_code,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		return AP_SM_WAIT_NONE;
	}
}

/**
 * ap_sm_setirq_wait(): Test queue for completion of the irq enablement
 * @aq: pointer to the AP queue
 *
 * Returns AP_POLL_IMMEDIATELY, AP_POLL_AFTER_TIMEROUT or 0.
 */
static enum ap_sm_wait ap_sm_setirq_wait(struct ap_queue *aq)
{
	struct ap_queue_status status;

	if (aq->queue_count > 0 && aq->reply)
		/* Try to read a completed message and get the status */
		status = ap_sm_recv(aq);
	else
		/* Get the status with TAPQ */
		status = ap_tapq(aq->qid, NULL);

	if (status.irq_enabled == 1) {
		/* Irqs are now enabled */
		aq->sm_state = (aq->queue_count > 0) ?
			AP_SM_STATE_WORKING : AP_SM_STATE_IDLE;
	}

	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		if (aq->queue_count > 0)
			return AP_SM_WAIT_AGAIN;
		fallthrough;
	case AP_RESPONSE_NO_PENDING_REPLY:
		return AP_SM_WAIT_LOW_TIMEOUT;
	default:
		aq->dev_state = AP_DEV_STATE_ERROR;
		aq->last_err_rc = status.response_code;
		AP_DBF_WARN("%s RC 0x%02x on 0x%02x.%04x -> AP_DEV_STATE_ERROR\n",
			    __func__, status.response_code,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		return AP_SM_WAIT_NONE;
	}
}

/**
 * ap_sm_assoc_wait(): Test queue for completion of a pending
 *		       association request.
 * @aq: pointer to the AP queue
 */
static enum ap_sm_wait ap_sm_assoc_wait(struct ap_queue *aq)
{
	struct ap_queue_status status;
	struct ap_tapq_hwinfo hwinfo;

	status = ap_test_queue(aq->qid, 1, &hwinfo);
	/* handle asynchronous error on this queue */
	if (status.async && status.response_code) {
		aq->dev_state = AP_DEV_STATE_ERROR;
		aq->last_err_rc = status.response_code;
		AP_DBF_WARN("%s asynch RC 0x%02x on 0x%02x.%04x -> AP_DEV_STATE_ERROR\n",
			    __func__, status.response_code,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		return AP_SM_WAIT_NONE;
	}
	if (status.response_code > AP_RESPONSE_BUSY) {
		aq->dev_state = AP_DEV_STATE_ERROR;
		aq->last_err_rc = status.response_code;
		AP_DBF_WARN("%s RC 0x%02x on 0x%02x.%04x -> AP_DEV_STATE_ERROR\n",
			    __func__, status.response_code,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		return AP_SM_WAIT_NONE;
	}

	/* update queue's SE bind state */
	aq->se_bstate = hwinfo.bs;

	/* check bs bits */
	switch (hwinfo.bs) {
	case AP_BS_Q_USABLE:
		/* association is through */
		aq->sm_state = AP_SM_STATE_IDLE;
		pr_debug("%s queue 0x%02x.%04x associated with %u\n",
			 __func__, AP_QID_CARD(aq->qid),
			 AP_QID_QUEUE(aq->qid), aq->assoc_idx);
		return AP_SM_WAIT_NONE;
	case AP_BS_Q_USABLE_NO_SECURE_KEY:
		/* association still pending */
		return AP_SM_WAIT_LOW_TIMEOUT;
	default:
		/* reset from 'outside' happened or no idea at all */
		aq->assoc_idx = ASSOC_IDX_INVALID;
		aq->dev_state = AP_DEV_STATE_ERROR;
		aq->last_err_rc = status.response_code;
		AP_DBF_WARN("%s bs 0x%02x on 0x%02x.%04x -> AP_DEV_STATE_ERROR\n",
			    __func__, hwinfo.bs,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		return AP_SM_WAIT_NONE;
	}
}

/*
 * AP state machine jump table
 */
static ap_func_t *ap_jumptable[NR_AP_SM_STATES][NR_AP_SM_EVENTS] = {
	[AP_SM_STATE_RESET_START] = {
		[AP_SM_EVENT_POLL] = ap_sm_reset,
		[AP_SM_EVENT_TIMEOUT] = ap_sm_nop,
	},
	[AP_SM_STATE_RESET_WAIT] = {
		[AP_SM_EVENT_POLL] = ap_sm_reset_wait,
		[AP_SM_EVENT_TIMEOUT] = ap_sm_nop,
	},
	[AP_SM_STATE_SETIRQ_WAIT] = {
		[AP_SM_EVENT_POLL] = ap_sm_setirq_wait,
		[AP_SM_EVENT_TIMEOUT] = ap_sm_nop,
	},
	[AP_SM_STATE_IDLE] = {
		[AP_SM_EVENT_POLL] = ap_sm_write,
		[AP_SM_EVENT_TIMEOUT] = ap_sm_nop,
	},
	[AP_SM_STATE_WORKING] = {
		[AP_SM_EVENT_POLL] = ap_sm_read_write,
		[AP_SM_EVENT_TIMEOUT] = ap_sm_reset,
	},
	[AP_SM_STATE_QUEUE_FULL] = {
		[AP_SM_EVENT_POLL] = ap_sm_read,
		[AP_SM_EVENT_TIMEOUT] = ap_sm_reset,
	},
	[AP_SM_STATE_ASSOC_WAIT] = {
		[AP_SM_EVENT_POLL] = ap_sm_assoc_wait,
		[AP_SM_EVENT_TIMEOUT] = ap_sm_reset,
	},
};

enum ap_sm_wait ap_sm_event(struct ap_queue *aq, enum ap_sm_event event)
{
	if (aq->config && !aq->chkstop &&
	    aq->dev_state > AP_DEV_STATE_UNINITIATED)
		return ap_jumptable[aq->sm_state][event](aq);
	else
		return AP_SM_WAIT_NONE;
}

enum ap_sm_wait ap_sm_event_loop(struct ap_queue *aq, enum ap_sm_event event)
{
	enum ap_sm_wait wait;

	while ((wait = ap_sm_event(aq, event)) == AP_SM_WAIT_AGAIN)
		;
	return wait;
}

/*
 * AP queue related attributes.
 */
static ssize_t request_count_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct ap_queue *aq = to_ap_queue(dev);
	bool valid = false;
	u64 req_cnt;

	spin_lock_bh(&aq->lock);
	if (aq->dev_state > AP_DEV_STATE_UNINITIATED) {
		req_cnt = aq->total_request_count;
		valid = true;
	}
	spin_unlock_bh(&aq->lock);

	if (valid)
		return sysfs_emit(buf, "%llu\n", req_cnt);
	else
		return sysfs_emit(buf, "-\n");
}

static ssize_t request_count_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct ap_queue *aq = to_ap_queue(dev);

	spin_lock_bh(&aq->lock);
	aq->total_request_count = 0;
	spin_unlock_bh(&aq->lock);

	return count;
}

static DEVICE_ATTR_RW(request_count);

static ssize_t requestq_count_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct ap_queue *aq = to_ap_queue(dev);
	unsigned int reqq_cnt = 0;

	spin_lock_bh(&aq->lock);
	if (aq->dev_state > AP_DEV_STATE_UNINITIATED)
		reqq_cnt = aq->requestq_count;
	spin_unlock_bh(&aq->lock);
	return sysfs_emit(buf, "%d\n", reqq_cnt);
}

static DEVICE_ATTR_RO(requestq_count);

static ssize_t pendingq_count_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct ap_queue *aq = to_ap_queue(dev);
	unsigned int penq_cnt = 0;

	spin_lock_bh(&aq->lock);
	if (aq->dev_state > AP_DEV_STATE_UNINITIATED)
		penq_cnt = aq->pendingq_count;
	spin_unlock_bh(&aq->lock);
	return sysfs_emit(buf, "%d\n", penq_cnt);
}

static DEVICE_ATTR_RO(pendingq_count);

static ssize_t reset_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct ap_queue *aq = to_ap_queue(dev);
	int rc = 0;

	spin_lock_bh(&aq->lock);
	switch (aq->sm_state) {
	case AP_SM_STATE_RESET_START:
	case AP_SM_STATE_RESET_WAIT:
		rc = sysfs_emit(buf, "Reset in progress.\n");
		break;
	case AP_SM_STATE_WORKING:
	case AP_SM_STATE_QUEUE_FULL:
		rc = sysfs_emit(buf, "Reset Timer armed.\n");
		break;
	default:
		rc = sysfs_emit(buf, "No Reset Timer set.\n");
	}
	spin_unlock_bh(&aq->lock);
	return rc;
}

static ssize_t reset_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct ap_queue *aq = to_ap_queue(dev);

	spin_lock_bh(&aq->lock);
	__ap_flush_queue(aq);
	aq->sm_state = AP_SM_STATE_RESET_START;
	ap_wait(ap_sm_event(aq, AP_SM_EVENT_POLL));
	spin_unlock_bh(&aq->lock);

	AP_DBF_INFO("%s reset queue=%02x.%04x triggered by user\n",
		    __func__, AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));

	return count;
}

static DEVICE_ATTR_RW(reset);

static ssize_t interrupt_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ap_queue *aq = to_ap_queue(dev);
	struct ap_queue_status status;
	int rc = 0;

	spin_lock_bh(&aq->lock);
	if (aq->sm_state == AP_SM_STATE_SETIRQ_WAIT) {
		rc = sysfs_emit(buf, "Enable Interrupt pending.\n");
	} else {
		status = ap_tapq(aq->qid, NULL);
		if (status.irq_enabled)
			rc = sysfs_emit(buf, "Interrupts enabled.\n");
		else
			rc = sysfs_emit(buf, "Interrupts disabled.\n");
	}
	spin_unlock_bh(&aq->lock);

	return rc;
}

static DEVICE_ATTR_RO(interrupt);

static ssize_t config_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct ap_queue *aq = to_ap_queue(dev);
	int rc;

	spin_lock_bh(&aq->lock);
	rc = sysfs_emit(buf, "%d\n", aq->config ? 1 : 0);
	spin_unlock_bh(&aq->lock);
	return rc;
}

static DEVICE_ATTR_RO(config);

static ssize_t chkstop_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct ap_queue *aq = to_ap_queue(dev);
	int rc;

	spin_lock_bh(&aq->lock);
	rc = sysfs_emit(buf, "%d\n", aq->chkstop ? 1 : 0);
	spin_unlock_bh(&aq->lock);
	return rc;
}

static DEVICE_ATTR_RO(chkstop);

static ssize_t ap_functions_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ap_queue *aq = to_ap_queue(dev);
	struct ap_queue_status status;
	struct ap_tapq_hwinfo hwinfo;

	status = ap_test_queue(aq->qid, 1, &hwinfo);
	if (status.response_code > AP_RESPONSE_BUSY) {
		pr_debug("%s RC 0x%02x on tapq(0x%02x.%04x)\n",
			 __func__, status.response_code,
			 AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		return -EIO;
	}

	return sysfs_emit(buf, "0x%08X\n", hwinfo.fac);
}

static DEVICE_ATTR_RO(ap_functions);

#ifdef CONFIG_ZCRYPT_DEBUG
static ssize_t states_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct ap_queue *aq = to_ap_queue(dev);
	int rc = 0;

	spin_lock_bh(&aq->lock);
	/* queue device state */
	switch (aq->dev_state) {
	case AP_DEV_STATE_UNINITIATED:
		rc = sysfs_emit(buf, "UNINITIATED\n");
		break;
	case AP_DEV_STATE_OPERATING:
		rc = sysfs_emit(buf, "OPERATING");
		break;
	case AP_DEV_STATE_SHUTDOWN:
		rc = sysfs_emit(buf, "SHUTDOWN");
		break;
	case AP_DEV_STATE_ERROR:
		rc = sysfs_emit(buf, "ERROR");
		break;
	default:
		rc = sysfs_emit(buf, "UNKNOWN");
	}
	/* state machine state */
	if (aq->dev_state) {
		switch (aq->sm_state) {
		case AP_SM_STATE_RESET_START:
			rc += sysfs_emit_at(buf, rc, " [RESET_START]\n");
			break;
		case AP_SM_STATE_RESET_WAIT:
			rc += sysfs_emit_at(buf, rc, " [RESET_WAIT]\n");
			break;
		case AP_SM_STATE_SETIRQ_WAIT:
			rc += sysfs_emit_at(buf, rc, " [SETIRQ_WAIT]\n");
			break;
		case AP_SM_STATE_IDLE:
			rc += sysfs_emit_at(buf, rc, " [IDLE]\n");
			break;
		case AP_SM_STATE_WORKING:
			rc += sysfs_emit_at(buf, rc, " [WORKING]\n");
			break;
		case AP_SM_STATE_QUEUE_FULL:
			rc += sysfs_emit_at(buf, rc, " [FULL]\n");
			break;
		case AP_SM_STATE_ASSOC_WAIT:
			rc += sysfs_emit_at(buf, rc, " [ASSOC_WAIT]\n");
			break;
		default:
			rc += sysfs_emit_at(buf, rc, " [UNKNOWN]\n");
		}
	}
	spin_unlock_bh(&aq->lock);

	return rc;
}
static DEVICE_ATTR_RO(states);

static ssize_t last_err_rc_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ap_queue *aq = to_ap_queue(dev);
	int rc;

	spin_lock_bh(&aq->lock);
	rc = aq->last_err_rc;
	spin_unlock_bh(&aq->lock);

	switch (rc) {
	case AP_RESPONSE_NORMAL:
		return sysfs_emit(buf, "NORMAL\n");
	case AP_RESPONSE_Q_NOT_AVAIL:
		return sysfs_emit(buf, "Q_NOT_AVAIL\n");
	case AP_RESPONSE_RESET_IN_PROGRESS:
		return sysfs_emit(buf, "RESET_IN_PROGRESS\n");
	case AP_RESPONSE_DECONFIGURED:
		return sysfs_emit(buf, "DECONFIGURED\n");
	case AP_RESPONSE_CHECKSTOPPED:
		return sysfs_emit(buf, "CHECKSTOPPED\n");
	case AP_RESPONSE_BUSY:
		return sysfs_emit(buf, "BUSY\n");
	case AP_RESPONSE_INVALID_ADDRESS:
		return sysfs_emit(buf, "INVALID_ADDRESS\n");
	case AP_RESPONSE_OTHERWISE_CHANGED:
		return sysfs_emit(buf, "OTHERWISE_CHANGED\n");
	case AP_RESPONSE_Q_FULL:
		return sysfs_emit(buf, "Q_FULL/NO_PENDING_REPLY\n");
	case AP_RESPONSE_INDEX_TOO_BIG:
		return sysfs_emit(buf, "INDEX_TOO_BIG\n");
	case AP_RESPONSE_NO_FIRST_PART:
		return sysfs_emit(buf, "NO_FIRST_PART\n");
	case AP_RESPONSE_MESSAGE_TOO_BIG:
		return sysfs_emit(buf, "MESSAGE_TOO_BIG\n");
	case AP_RESPONSE_REQ_FAC_NOT_INST:
		return sysfs_emit(buf, "REQ_FAC_NOT_INST\n");
	default:
		return sysfs_emit(buf, "response code %d\n", rc);
	}
}
static DEVICE_ATTR_RO(last_err_rc);
#endif

static struct attribute *ap_queue_dev_attrs[] = {
	&dev_attr_request_count.attr,
	&dev_attr_requestq_count.attr,
	&dev_attr_pendingq_count.attr,
	&dev_attr_reset.attr,
	&dev_attr_interrupt.attr,
	&dev_attr_config.attr,
	&dev_attr_chkstop.attr,
	&dev_attr_ap_functions.attr,
#ifdef CONFIG_ZCRYPT_DEBUG
	&dev_attr_states.attr,
	&dev_attr_last_err_rc.attr,
#endif
	NULL
};

static struct attribute_group ap_queue_dev_attr_group = {
	.attrs = ap_queue_dev_attrs
};

static const struct attribute_group *ap_queue_dev_attr_groups[] = {
	&ap_queue_dev_attr_group,
	NULL
};

static struct device_type ap_queue_type = {
	.name = "ap_queue",
	.groups = ap_queue_dev_attr_groups,
};

static ssize_t se_bind_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct ap_queue *aq = to_ap_queue(dev);
	struct ap_queue_status status;
	struct ap_tapq_hwinfo hwinfo;

	if (!ap_q_supports_bind(aq))
		return sysfs_emit(buf, "-\n");

	status = ap_test_queue(aq->qid, 1, &hwinfo);
	if (status.response_code > AP_RESPONSE_BUSY) {
		pr_debug("%s RC 0x%02x on tapq(0x%02x.%04x)\n",
			 __func__, status.response_code,
			 AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		return -EIO;
	}

	/* update queue's SE bind state */
	spin_lock_bh(&aq->lock);
	aq->se_bstate = hwinfo.bs;
	spin_unlock_bh(&aq->lock);

	switch (hwinfo.bs) {
	case AP_BS_Q_USABLE:
	case AP_BS_Q_USABLE_NO_SECURE_KEY:
		return sysfs_emit(buf, "bound\n");
	default:
		return sysfs_emit(buf, "unbound\n");
	}
}

static ssize_t se_bind_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ap_queue *aq = to_ap_queue(dev);
	struct ap_queue_status status;
	struct ap_tapq_hwinfo hwinfo;
	bool value;
	int rc;

	if (!ap_q_supports_bind(aq))
		return -EINVAL;

	/* only 0 (unbind) and 1 (bind) allowed */
	rc = kstrtobool(buf, &value);
	if (rc)
		return rc;

	if (!value) {
		/* Unbind. Set F bit arg and trigger RAPQ */
		spin_lock_bh(&aq->lock);
		__ap_flush_queue(aq);
		aq->rapq_fbit = 1;
		_ap_queue_init_state(aq);
		rc = count;
		goto out;
	}

	/* Bind. Check current SE bind state */
	status = ap_test_queue(aq->qid, 1, &hwinfo);
	if (status.response_code) {
		AP_DBF_WARN("%s RC 0x%02x on tapq(0x%02x.%04x)\n",
			    __func__, status.response_code,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		return -EIO;
	}

	/* Update BS state */
	spin_lock_bh(&aq->lock);
	aq->se_bstate = hwinfo.bs;
	if (hwinfo.bs != AP_BS_Q_AVAIL_FOR_BINDING) {
		AP_DBF_WARN("%s bind attempt with bs %d on queue 0x%02x.%04x\n",
			    __func__, hwinfo.bs,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		rc = -EINVAL;
		goto out;
	}

	/* Check SM state */
	if (aq->sm_state < AP_SM_STATE_IDLE) {
		rc = -EBUSY;
		goto out;
	}

	/* invoke BAPQ */
	status = ap_bapq(aq->qid);
	if (status.response_code) {
		AP_DBF_WARN("%s RC 0x%02x on bapq(0x%02x.%04x)\n",
			    __func__, status.response_code,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		rc = -EIO;
		goto out;
	}
	aq->assoc_idx = ASSOC_IDX_INVALID;

	/* verify SE bind state */
	status = ap_test_queue(aq->qid, 1, &hwinfo);
	if (status.response_code) {
		AP_DBF_WARN("%s RC 0x%02x on tapq(0x%02x.%04x)\n",
			    __func__, status.response_code,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		rc = -EIO;
		goto out;
	}
	aq->se_bstate = hwinfo.bs;
	if (!(hwinfo.bs == AP_BS_Q_USABLE ||
	      hwinfo.bs == AP_BS_Q_USABLE_NO_SECURE_KEY)) {
		AP_DBF_WARN("%s BAPQ success, but bs shows %d on queue 0x%02x.%04x\n",
			    __func__, hwinfo.bs,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		rc = -EIO;
		goto out;
	}

	/* SE bind was successful */
	AP_DBF_INFO("%s bapq(0x%02x.%04x) success\n", __func__,
		    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
	rc = count;

out:
	spin_unlock_bh(&aq->lock);
	return rc;
}

static DEVICE_ATTR_RW(se_bind);

static ssize_t se_associate_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ap_queue *aq = to_ap_queue(dev);
	struct ap_queue_status status;
	struct ap_tapq_hwinfo hwinfo;

	if (!ap_q_supports_assoc(aq))
		return sysfs_emit(buf, "-\n");

	status = ap_test_queue(aq->qid, 1, &hwinfo);
	if (status.response_code > AP_RESPONSE_BUSY) {
		pr_debug("%s RC 0x%02x on tapq(0x%02x.%04x)\n",
			 __func__, status.response_code,
			 AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		return -EIO;
	}

	/* update queue's SE bind state */
	spin_lock_bh(&aq->lock);
	aq->se_bstate = hwinfo.bs;
	spin_unlock_bh(&aq->lock);

	switch (hwinfo.bs) {
	case AP_BS_Q_USABLE:
		if (aq->assoc_idx == ASSOC_IDX_INVALID) {
			AP_DBF_WARN("%s AP_BS_Q_USABLE but invalid assoc_idx\n", __func__);
			return -EIO;
		}
		return sysfs_emit(buf, "associated %u\n", aq->assoc_idx);
	case AP_BS_Q_USABLE_NO_SECURE_KEY:
		if (aq->assoc_idx != ASSOC_IDX_INVALID)
			return sysfs_emit(buf, "association pending\n");
		fallthrough;
	default:
		return sysfs_emit(buf, "unassociated\n");
	}
}

static ssize_t se_associate_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct ap_queue *aq = to_ap_queue(dev);
	struct ap_queue_status status;
	struct ap_tapq_hwinfo hwinfo;
	unsigned int value;
	int rc;

	if (!ap_q_supports_assoc(aq))
		return -EINVAL;

	/* association index needs to be >= 0 */
	rc = kstrtouint(buf, 0, &value);
	if (rc)
		return rc;
	if (value >= ASSOC_IDX_INVALID)
		return -EINVAL;

	/* check current SE bind state */
	status = ap_test_queue(aq->qid, 1, &hwinfo);
	if (status.response_code) {
		AP_DBF_WARN("%s RC 0x%02x on tapq(0x%02x.%04x)\n",
			    __func__, status.response_code,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		return -EIO;
	}
	spin_lock_bh(&aq->lock);
	aq->se_bstate = hwinfo.bs;
	if (hwinfo.bs != AP_BS_Q_USABLE_NO_SECURE_KEY) {
		AP_DBF_WARN("%s association attempt with bs %d on queue 0x%02x.%04x\n",
			    __func__, hwinfo.bs,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		rc = -EINVAL;
		goto out;
	}

	/* check SM state */
	if (aq->sm_state != AP_SM_STATE_IDLE) {
		rc = -EBUSY;
		goto out;
	}

	/* trigger the asynchronous association request */
	status = ap_aapq(aq->qid, value);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
	case AP_RESPONSE_STATE_CHANGE_IN_PROGRESS:
		aq->sm_state = AP_SM_STATE_ASSOC_WAIT;
		aq->assoc_idx = value;
		ap_wait(ap_sm_event(aq, AP_SM_EVENT_POLL));
		break;
	default:
		AP_DBF_WARN("%s RC 0x%02x on aapq(0x%02x.%04x)\n",
			    __func__, status.response_code,
			    AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));
		rc = -EIO;
		goto out;
	}

	rc = count;

out:
	spin_unlock_bh(&aq->lock);
	return rc;
}

static DEVICE_ATTR_RW(se_associate);

static struct attribute *ap_queue_dev_sb_attrs[] = {
	&dev_attr_se_bind.attr,
	&dev_attr_se_associate.attr,
	NULL
};

static struct attribute_group ap_queue_dev_sb_attr_group = {
	.attrs = ap_queue_dev_sb_attrs
};

static const struct attribute_group *ap_queue_dev_sb_attr_groups[] = {
	&ap_queue_dev_sb_attr_group,
	NULL
};

static void ap_queue_device_release(struct device *dev)
{
	struct ap_queue *aq = to_ap_queue(dev);

	spin_lock_bh(&ap_queues_lock);
	hash_del(&aq->hnode);
	spin_unlock_bh(&ap_queues_lock);

	kfree(aq);
}

struct ap_queue *ap_queue_create(ap_qid_t qid, int device_type)
{
	struct ap_queue *aq;

	aq = kzalloc(sizeof(*aq), GFP_KERNEL);
	if (!aq)
		return NULL;
	aq->ap_dev.device.release = ap_queue_device_release;
	aq->ap_dev.device.type = &ap_queue_type;
	aq->ap_dev.device_type = device_type;
	// add optional SE secure binding attributes group
	if (ap_sb_available() && is_prot_virt_guest())
		aq->ap_dev.device.groups = ap_queue_dev_sb_attr_groups;
	aq->qid = qid;
	spin_lock_init(&aq->lock);
	INIT_LIST_HEAD(&aq->pendingq);
	INIT_LIST_HEAD(&aq->requestq);
	timer_setup(&aq->timeout, ap_request_timeout, 0);

	return aq;
}

void ap_queue_init_reply(struct ap_queue *aq, struct ap_message *reply)
{
	aq->reply = reply;

	spin_lock_bh(&aq->lock);
	ap_wait(ap_sm_event(aq, AP_SM_EVENT_POLL));
	spin_unlock_bh(&aq->lock);
}
EXPORT_SYMBOL(ap_queue_init_reply);

/**
 * ap_queue_message(): Queue a request to an AP device.
 * @aq: The AP device to queue the message to
 * @ap_msg: The message that is to be added
 */
int ap_queue_message(struct ap_queue *aq, struct ap_message *ap_msg)
{
	int rc = 0;

	/* msg needs to have a valid receive-callback */
	BUG_ON(!ap_msg->receive);

	spin_lock_bh(&aq->lock);

	/* only allow to queue new messages if device state is ok */
	if (aq->dev_state == AP_DEV_STATE_OPERATING) {
		list_add_tail(&ap_msg->list, &aq->requestq);
		aq->requestq_count++;
		aq->total_request_count++;
		atomic64_inc(&aq->card->total_request_count);
	} else {
		rc = -ENODEV;
	}

	/* Send/receive as many request from the queue as possible. */
	ap_wait(ap_sm_event_loop(aq, AP_SM_EVENT_POLL));

	spin_unlock_bh(&aq->lock);

	return rc;
}
EXPORT_SYMBOL(ap_queue_message);

/**
 * ap_queue_usable(): Check if queue is usable just now.
 * @aq: The AP queue device to test for usability.
 * This function is intended for the scheduler to query if it makes
 * sense to enqueue a message into this AP queue device by calling
 * ap_queue_message(). The perspective is very short-term as the
 * state machine and device state(s) may change at any time.
 */
bool ap_queue_usable(struct ap_queue *aq)
{
	bool rc = true;

	spin_lock_bh(&aq->lock);

	/* check for not configured or checkstopped */
	if (!aq->config || aq->chkstop) {
		rc = false;
		goto unlock_and_out;
	}

	/* device state needs to be ok */
	if (aq->dev_state != AP_DEV_STATE_OPERATING) {
		rc = false;
		goto unlock_and_out;
	}

	/* SE guest's queues additionally need to be bound */
	if (ap_q_needs_bind(aq) &&
	    !(aq->se_bstate == AP_BS_Q_USABLE ||
	      aq->se_bstate == AP_BS_Q_USABLE_NO_SECURE_KEY))
		rc = false;

unlock_and_out:
	spin_unlock_bh(&aq->lock);
	return rc;
}
EXPORT_SYMBOL(ap_queue_usable);

/**
 * ap_cancel_message(): Cancel a crypto request.
 * @aq: The AP device that has the message queued
 * @ap_msg: The message that is to be removed
 *
 * Cancel a crypto request. This is done by removing the request
 * from the device pending or request queue. Note that the
 * request stays on the AP queue. When it finishes the message
 * reply will be discarded because the psmid can't be found.
 */
void ap_cancel_message(struct ap_queue *aq, struct ap_message *ap_msg)
{
	struct ap_message *tmp;

	spin_lock_bh(&aq->lock);
	if (!list_empty(&ap_msg->list)) {
		list_for_each_entry(tmp, &aq->pendingq, list)
			if (tmp->psmid == ap_msg->psmid) {
				aq->pendingq_count--;
				goto found;
			}
		aq->requestq_count--;
found:
		list_del_init(&ap_msg->list);
	}
	spin_unlock_bh(&aq->lock);
}
EXPORT_SYMBOL(ap_cancel_message);

/**
 * __ap_flush_queue(): Flush requests.
 * @aq: Pointer to the AP queue
 *
 * Flush all requests from the request/pending queue of an AP device.
 */
static void __ap_flush_queue(struct ap_queue *aq)
{
	struct ap_message *ap_msg, *next;

	list_for_each_entry_safe(ap_msg, next, &aq->pendingq, list) {
		list_del_init(&ap_msg->list);
		aq->pendingq_count--;
		ap_msg->rc = -EAGAIN;
		ap_msg->receive(aq, ap_msg, NULL);
	}
	list_for_each_entry_safe(ap_msg, next, &aq->requestq, list) {
		list_del_init(&ap_msg->list);
		aq->requestq_count--;
		ap_msg->rc = -EAGAIN;
		ap_msg->receive(aq, ap_msg, NULL);
	}
	aq->queue_count = 0;
}

void ap_flush_queue(struct ap_queue *aq)
{
	spin_lock_bh(&aq->lock);
	__ap_flush_queue(aq);
	spin_unlock_bh(&aq->lock);
}
EXPORT_SYMBOL(ap_flush_queue);

void ap_queue_prepare_remove(struct ap_queue *aq)
{
	spin_lock_bh(&aq->lock);
	/* flush queue */
	__ap_flush_queue(aq);
	/* move queue device state to SHUTDOWN in progress */
	aq->dev_state = AP_DEV_STATE_SHUTDOWN;
	spin_unlock_bh(&aq->lock);
	del_timer_sync(&aq->timeout);
}

void ap_queue_remove(struct ap_queue *aq)
{
	/*
	 * all messages have been flushed and the device state
	 * is SHUTDOWN. Now reset with zero which also clears
	 * the irq registration and move the device state
	 * to the initial value AP_DEV_STATE_UNINITIATED.
	 */
	spin_lock_bh(&aq->lock);
	ap_zapq(aq->qid, 0);
	aq->dev_state = AP_DEV_STATE_UNINITIATED;
	spin_unlock_bh(&aq->lock);
}

void _ap_queue_init_state(struct ap_queue *aq)
{
	aq->dev_state = AP_DEV_STATE_OPERATING;
	aq->sm_state = AP_SM_STATE_RESET_START;
	aq->last_err_rc = 0;
	aq->assoc_idx = ASSOC_IDX_INVALID;
	ap_wait(ap_sm_event(aq, AP_SM_EVENT_POLL));
}

void ap_queue_init_state(struct ap_queue *aq)
{
	spin_lock_bh(&aq->lock);
	_ap_queue_init_state(aq);
	spin_unlock_bh(&aq->lock);
}
EXPORT_SYMBOL(ap_queue_init_state);
