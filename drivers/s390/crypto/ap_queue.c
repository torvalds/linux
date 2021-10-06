// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2016
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

/**
 * ap_queue_enable_irq(): Enable interrupt support on this AP queue.
 * @qid: The AP queue number
 * @ind: the notification indicator byte
 *
 * Enables interruption on AP queue via ap_aqic(). Based on the return
 * value it waits a while and tests the AP queue if interrupts
 * have been switched on using ap_test_queue().
 */
static int ap_queue_enable_irq(struct ap_queue *aq, void *ind)
{
	struct ap_queue_status status;
	struct ap_qirq_ctrl qirqctrl = { 0 };

	qirqctrl.ir = 1;
	qirqctrl.isc = AP_ISC;
	status = ap_aqic(aq->qid, qirqctrl, ind);
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
 * @length: The message length
 * @special: Special Bit
 *
 * Returns AP queue status structure.
 * Condition code 1 on NQAP can't happen because the L bit is 1.
 * Condition code 2 on NQAP also means the send is incomplete,
 * because a segment boundary was reached. The NQAP is repeated.
 */
static inline struct ap_queue_status
__ap_send(ap_qid_t qid, unsigned long long psmid, void *msg, size_t length,
	  int special)
{
	if (special)
		qid |= 0x400000UL;
	return ap_nqap(qid, psmid, msg, length);
}

int ap_send(ap_qid_t qid, unsigned long long psmid, void *msg, size_t length)
{
	struct ap_queue_status status;

	status = __ap_send(qid, psmid, msg, length, 0);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		return 0;
	case AP_RESPONSE_Q_FULL:
	case AP_RESPONSE_RESET_IN_PROGRESS:
		return -EBUSY;
	case AP_RESPONSE_REQ_FAC_NOT_INST:
		return -EINVAL;
	default:	/* Device is gone. */
		return -ENODEV;
	}
}
EXPORT_SYMBOL(ap_send);

int ap_recv(ap_qid_t qid, unsigned long long *psmid, void *msg, size_t length)
{
	struct ap_queue_status status;

	if (msg == NULL)
		return -EINVAL;
	status = ap_dqap(qid, psmid, msg, length, NULL, NULL);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		return 0;
	case AP_RESPONSE_NO_PENDING_REPLY:
		if (status.queue_empty)
			return -ENOENT;
		return -EBUSY;
	case AP_RESPONSE_RESET_IN_PROGRESS:
		return -EBUSY;
	default:
		return -ENODEV;
	}
}
EXPORT_SYMBOL(ap_recv);

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
				 &reslen, &resgr0);
		parts++;
	} while (status.response_code == 0xFF && resgr0 != 0);

	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		aq->queue_count = max_t(int, 0, aq->queue_count - 1);
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
			AP_DBF_WARN("%s unassociated reply psmid=0x%016llx on 0x%02x.%04x\n",
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
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		if (aq->queue_count > 0) {
			aq->sm_state = AP_SM_STATE_WORKING;
			return AP_SM_WAIT_AGAIN;
		}
		aq->sm_state = AP_SM_STATE_IDLE;
		return AP_SM_WAIT_NONE;
	case AP_RESPONSE_NO_PENDING_REPLY:
		if (aq->queue_count > 0)
			return aq->interrupt ?
				AP_SM_WAIT_INTERRUPT : AP_SM_WAIT_TIMEOUT;
		aq->sm_state = AP_SM_STATE_IDLE;
		return AP_SM_WAIT_NONE;
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
#ifdef CONFIG_ZCRYPT_DEBUG
	if (ap_msg->fi.action == AP_FI_ACTION_NQAP_QID_INVAL) {
		AP_DBF_WARN("%s fi cmd 0x%04x: forcing invalid qid 0xFF00\n",
			    __func__, ap_msg->fi.cmd);
		qid = 0xFF00;
	}
#endif
	status = __ap_send(qid, ap_msg->psmid,
			   ap_msg->msg, ap_msg->len,
			   ap_msg->flags & AP_MSG_FLAG_SPECIAL);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		aq->queue_count = max_t(int, 1, aq->queue_count + 1);
		if (aq->queue_count == 1)
			mod_timer(&aq->timeout, jiffies + aq->request_timeout);
		list_move_tail(&ap_msg->list, &aq->pendingq);
		aq->requestq_count--;
		aq->pendingq_count++;
		if (aq->queue_count < aq->card->queue_depth) {
			aq->sm_state = AP_SM_STATE_WORKING;
			return AP_SM_WAIT_AGAIN;
		}
		fallthrough;
	case AP_RESPONSE_Q_FULL:
		aq->sm_state = AP_SM_STATE_QUEUE_FULL;
		return aq->interrupt ?
			AP_SM_WAIT_INTERRUPT : AP_SM_WAIT_TIMEOUT;
	case AP_RESPONSE_RESET_IN_PROGRESS:
		aq->sm_state = AP_SM_STATE_RESET_WAIT;
		return AP_SM_WAIT_TIMEOUT;
	case AP_RESPONSE_INVALID_DOMAIN:
		AP_DBF(DBF_WARN, "AP_RESPONSE_INVALID_DOMAIN on NQAP\n");
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
 * @qid: The AP queue number
 *
 * Submit the Reset command to an AP queue.
 */
static enum ap_sm_wait ap_sm_reset(struct ap_queue *aq)
{
	struct ap_queue_status status;

	status = ap_rapq(aq->qid);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
	case AP_RESPONSE_RESET_IN_PROGRESS:
		aq->sm_state = AP_SM_STATE_RESET_WAIT;
		aq->interrupt = false;
		return AP_SM_WAIT_TIMEOUT;
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
	void *lsi_ptr;

	if (aq->queue_count > 0 && aq->reply)
		/* Try to read a completed message and get the status */
		status = ap_sm_recv(aq);
	else
		/* Get the status with TAPQ */
		status = ap_tapq(aq->qid, NULL);

	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		lsi_ptr = ap_airq_ptr();
		if (lsi_ptr && ap_queue_enable_irq(aq, lsi_ptr) == 0)
			aq->sm_state = AP_SM_STATE_SETIRQ_WAIT;
		else
			aq->sm_state = (aq->queue_count > 0) ?
				AP_SM_STATE_WORKING : AP_SM_STATE_IDLE;
		return AP_SM_WAIT_AGAIN;
	case AP_RESPONSE_BUSY:
	case AP_RESPONSE_RESET_IN_PROGRESS:
		return AP_SM_WAIT_TIMEOUT;
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
		aq->interrupt = true;
		aq->sm_state = (aq->queue_count > 0) ?
			AP_SM_STATE_WORKING : AP_SM_STATE_IDLE;
	}

	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		if (aq->queue_count > 0)
			return AP_SM_WAIT_AGAIN;
		fallthrough;
	case AP_RESPONSE_NO_PENDING_REPLY:
		return AP_SM_WAIT_TIMEOUT;
	default:
		aq->dev_state = AP_DEV_STATE_ERROR;
		aq->last_err_rc = status.response_code;
		AP_DBF_WARN("%s RC 0x%02x on 0x%02x.%04x -> AP_DEV_STATE_ERROR\n",
			    __func__, status.response_code,
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
};

enum ap_sm_wait ap_sm_event(struct ap_queue *aq, enum ap_sm_event event)
{
	if (aq->dev_state > AP_DEV_STATE_UNINITIATED)
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
		return scnprintf(buf, PAGE_SIZE, "%llu\n", req_cnt);
	else
		return scnprintf(buf, PAGE_SIZE, "-\n");
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
	return scnprintf(buf, PAGE_SIZE, "%d\n", reqq_cnt);
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
	return scnprintf(buf, PAGE_SIZE, "%d\n", penq_cnt);
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
		rc = scnprintf(buf, PAGE_SIZE, "Reset in progress.\n");
		break;
	case AP_SM_STATE_WORKING:
	case AP_SM_STATE_QUEUE_FULL:
		rc = scnprintf(buf, PAGE_SIZE, "Reset Timer armed.\n");
		break;
	default:
		rc = scnprintf(buf, PAGE_SIZE, "No Reset Timer set.\n");
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

	AP_DBF(DBF_INFO, "reset queue=%02x.%04x triggered by user\n",
	       AP_QID_CARD(aq->qid), AP_QID_QUEUE(aq->qid));

	return count;
}

static DEVICE_ATTR_RW(reset);

static ssize_t interrupt_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ap_queue *aq = to_ap_queue(dev);
	int rc = 0;

	spin_lock_bh(&aq->lock);
	if (aq->sm_state == AP_SM_STATE_SETIRQ_WAIT)
		rc = scnprintf(buf, PAGE_SIZE, "Enable Interrupt pending.\n");
	else if (aq->interrupt)
		rc = scnprintf(buf, PAGE_SIZE, "Interrupts enabled.\n");
	else
		rc = scnprintf(buf, PAGE_SIZE, "Interrupts disabled.\n");
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
	rc = scnprintf(buf, PAGE_SIZE, "%d\n", aq->config ? 1 : 0);
	spin_unlock_bh(&aq->lock);
	return rc;
}

static DEVICE_ATTR_RO(config);

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
		rc = scnprintf(buf, PAGE_SIZE, "UNINITIATED\n");
		break;
	case AP_DEV_STATE_OPERATING:
		rc = scnprintf(buf, PAGE_SIZE, "OPERATING");
		break;
	case AP_DEV_STATE_SHUTDOWN:
		rc = scnprintf(buf, PAGE_SIZE, "SHUTDOWN");
		break;
	case AP_DEV_STATE_ERROR:
		rc = scnprintf(buf, PAGE_SIZE, "ERROR");
		break;
	default:
		rc = scnprintf(buf, PAGE_SIZE, "UNKNOWN");
	}
	/* state machine state */
	if (aq->dev_state) {
		switch (aq->sm_state) {
		case AP_SM_STATE_RESET_START:
			rc += scnprintf(buf + rc, PAGE_SIZE - rc,
					" [RESET_START]\n");
			break;
		case AP_SM_STATE_RESET_WAIT:
			rc += scnprintf(buf + rc, PAGE_SIZE - rc,
					" [RESET_WAIT]\n");
			break;
		case AP_SM_STATE_SETIRQ_WAIT:
			rc += scnprintf(buf + rc, PAGE_SIZE - rc,
					" [SETIRQ_WAIT]\n");
			break;
		case AP_SM_STATE_IDLE:
			rc += scnprintf(buf + rc, PAGE_SIZE - rc,
					" [IDLE]\n");
			break;
		case AP_SM_STATE_WORKING:
			rc += scnprintf(buf + rc, PAGE_SIZE - rc,
					" [WORKING]\n");
			break;
		case AP_SM_STATE_QUEUE_FULL:
			rc += scnprintf(buf + rc, PAGE_SIZE - rc,
					" [FULL]\n");
			break;
		default:
			rc += scnprintf(buf + rc, PAGE_SIZE - rc,
					" [UNKNOWN]\n");
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
		return scnprintf(buf, PAGE_SIZE, "NORMAL\n");
	case AP_RESPONSE_Q_NOT_AVAIL:
		return scnprintf(buf, PAGE_SIZE, "Q_NOT_AVAIL\n");
	case AP_RESPONSE_RESET_IN_PROGRESS:
		return scnprintf(buf, PAGE_SIZE, "RESET_IN_PROGRESS\n");
	case AP_RESPONSE_DECONFIGURED:
		return scnprintf(buf, PAGE_SIZE, "DECONFIGURED\n");
	case AP_RESPONSE_CHECKSTOPPED:
		return scnprintf(buf, PAGE_SIZE, "CHECKSTOPPED\n");
	case AP_RESPONSE_BUSY:
		return scnprintf(buf, PAGE_SIZE, "BUSY\n");
	case AP_RESPONSE_INVALID_ADDRESS:
		return scnprintf(buf, PAGE_SIZE, "INVALID_ADDRESS\n");
	case AP_RESPONSE_OTHERWISE_CHANGED:
		return scnprintf(buf, PAGE_SIZE, "OTHERWISE_CHANGED\n");
	case AP_RESPONSE_Q_FULL:
		return scnprintf(buf, PAGE_SIZE, "Q_FULL/NO_PENDING_REPLY\n");
	case AP_RESPONSE_INDEX_TOO_BIG:
		return scnprintf(buf, PAGE_SIZE, "INDEX_TOO_BIG\n");
	case AP_RESPONSE_NO_FIRST_PART:
		return scnprintf(buf, PAGE_SIZE, "NO_FIRST_PART\n");
	case AP_RESPONSE_MESSAGE_TOO_BIG:
		return scnprintf(buf, PAGE_SIZE, "MESSAGE_TOO_BIG\n");
	case AP_RESPONSE_REQ_FAC_NOT_INST:
		return scnprintf(buf, PAGE_SIZE, "REQ_FAC_NOT_INST\n");
	default:
		return scnprintf(buf, PAGE_SIZE, "response code %d\n", rc);
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
	aq->qid = qid;
	aq->interrupt = false;
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
	} else
		rc = -ENODEV;

	/* Send/receive as many request from the queue as possible. */
	ap_wait(ap_sm_event_loop(aq, AP_SM_EVENT_POLL));

	spin_unlock_bh(&aq->lock);

	return rc;
}
EXPORT_SYMBOL(ap_queue_message);

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
	ap_zapq(aq->qid);
	aq->dev_state = AP_DEV_STATE_UNINITIATED;
	spin_unlock_bh(&aq->lock);
}

void ap_queue_init_state(struct ap_queue *aq)
{
	spin_lock_bh(&aq->lock);
	aq->dev_state = AP_DEV_STATE_OPERATING;
	aq->sm_state = AP_SM_STATE_RESET_START;
	ap_wait(ap_sm_event(aq, AP_SM_EVENT_POLL));
	spin_unlock_bh(&aq->lock);
}
EXPORT_SYMBOL(ap_queue_init_state);
