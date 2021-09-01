// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-cec.c - A Virtual Video Test Driver, cec emulation
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <media/cec.h>

#include "vivid-core.h"
#include "vivid-cec.h"

#define CEC_TIM_START_BIT_TOTAL		4500
#define CEC_TIM_START_BIT_LOW		3700
#define CEC_TIM_START_BIT_HIGH		800
#define CEC_TIM_DATA_BIT_TOTAL		2400
#define CEC_TIM_DATA_BIT_0_LOW		1500
#define CEC_TIM_DATA_BIT_0_HIGH		900
#define CEC_TIM_DATA_BIT_1_LOW		600
#define CEC_TIM_DATA_BIT_1_HIGH		1800

void vivid_cec_bus_free_work(struct vivid_dev *dev)
{
	spin_lock(&dev->cec_slock);
	while (!list_empty(&dev->cec_work_list)) {
		struct vivid_cec_work *cw =
			list_first_entry(&dev->cec_work_list,
					 struct vivid_cec_work, list);

		spin_unlock(&dev->cec_slock);
		cancel_delayed_work_sync(&cw->work);
		spin_lock(&dev->cec_slock);
		list_del(&cw->list);
		cec_transmit_attempt_done(cw->adap, CEC_TX_STATUS_LOW_DRIVE);
		kfree(cw);
	}
	spin_unlock(&dev->cec_slock);
}

static bool vivid_cec_find_dest_adap(struct vivid_dev *dev,
				     struct cec_adapter *adap, u8 dest)
{
	unsigned int i;

	if (dest >= 0xf)
		return false;

	if (adap != dev->cec_rx_adap && dev->cec_rx_adap &&
	    dev->cec_rx_adap->is_configured &&
	    cec_has_log_addr(dev->cec_rx_adap, dest))
		return true;

	for (i = 0; i < MAX_OUTPUTS && dev->cec_tx_adap[i]; i++) {
		if (adap == dev->cec_tx_adap[i])
			continue;
		if (!dev->cec_tx_adap[i]->is_configured)
			continue;
		if (cec_has_log_addr(dev->cec_tx_adap[i], dest))
			return true;
	}
	return false;
}

static void vivid_cec_pin_adap_events(struct cec_adapter *adap, ktime_t ts,
				      const struct cec_msg *msg, bool nacked)
{
	unsigned int len = nacked ? 1 : msg->len;
	unsigned int i;
	bool bit;

	if (adap == NULL)
		return;

	/*
	 * Suffix ULL on constant 10 makes the expression
	 * CEC_TIM_START_BIT_TOTAL + 10ULL * len * CEC_TIM_DATA_BIT_TOTAL
	 * to be evaluated using 64-bit unsigned arithmetic (u64), which
	 * is what ktime_sub_us expects as second argument.
	 */
	ts = ktime_sub_us(ts, CEC_TIM_START_BIT_TOTAL +
			       10ULL * len * CEC_TIM_DATA_BIT_TOTAL);
	cec_queue_pin_cec_event(adap, false, false, ts);
	ts = ktime_add_us(ts, CEC_TIM_START_BIT_LOW);
	cec_queue_pin_cec_event(adap, true, false, ts);
	ts = ktime_add_us(ts, CEC_TIM_START_BIT_HIGH);

	for (i = 0; i < 10 * len; i++) {
		switch (i % 10) {
		case 0 ... 7:
			bit = msg->msg[i / 10] & (0x80 >> (i % 10));
			break;
		case 8: /* EOM */
			bit = i / 10 == msg->len - 1;
			break;
		case 9: /* ACK */
			bit = cec_msg_is_broadcast(msg) ^ nacked;
			break;
		}
		cec_queue_pin_cec_event(adap, false, false, ts);
		if (bit)
			ts = ktime_add_us(ts, CEC_TIM_DATA_BIT_1_LOW);
		else
			ts = ktime_add_us(ts, CEC_TIM_DATA_BIT_0_LOW);
		cec_queue_pin_cec_event(adap, true, false, ts);
		if (bit)
			ts = ktime_add_us(ts, CEC_TIM_DATA_BIT_1_HIGH);
		else
			ts = ktime_add_us(ts, CEC_TIM_DATA_BIT_0_HIGH);
	}
}

static void vivid_cec_pin_events(struct vivid_dev *dev,
				 const struct cec_msg *msg, bool nacked)
{
	ktime_t ts = ktime_get();
	unsigned int i;

	vivid_cec_pin_adap_events(dev->cec_rx_adap, ts, msg, nacked);
	for (i = 0; i < MAX_OUTPUTS; i++)
		vivid_cec_pin_adap_events(dev->cec_tx_adap[i], ts, msg, nacked);
}

static void vivid_cec_xfer_done_worker(struct work_struct *work)
{
	struct vivid_cec_work *cw =
		container_of(work, struct vivid_cec_work, work.work);
	struct vivid_dev *dev = cw->dev;
	struct cec_adapter *adap = cw->adap;
	u8 dest = cec_msg_destination(&cw->msg);
	bool valid_dest;
	unsigned int i;

	valid_dest = cec_msg_is_broadcast(&cw->msg);
	if (!valid_dest)
		valid_dest = vivid_cec_find_dest_adap(dev, adap, dest);

	cw->tx_status = valid_dest ? CEC_TX_STATUS_OK : CEC_TX_STATUS_NACK;
	spin_lock(&dev->cec_slock);
	dev->cec_xfer_time_jiffies = 0;
	dev->cec_xfer_start_jiffies = 0;
	list_del(&cw->list);
	spin_unlock(&dev->cec_slock);
	vivid_cec_pin_events(dev, &cw->msg, !valid_dest);
	cec_transmit_attempt_done(cw->adap, cw->tx_status);

	/* Broadcast message */
	if (adap != dev->cec_rx_adap)
		cec_received_msg(dev->cec_rx_adap, &cw->msg);
	for (i = 0; i < MAX_OUTPUTS && dev->cec_tx_adap[i]; i++)
		if (adap != dev->cec_tx_adap[i])
			cec_received_msg(dev->cec_tx_adap[i], &cw->msg);
	kfree(cw);
}

static void vivid_cec_xfer_try_worker(struct work_struct *work)
{
	struct vivid_cec_work *cw =
		container_of(work, struct vivid_cec_work, work.work);
	struct vivid_dev *dev = cw->dev;

	spin_lock(&dev->cec_slock);
	if (dev->cec_xfer_time_jiffies) {
		list_del(&cw->list);
		spin_unlock(&dev->cec_slock);
		cec_transmit_attempt_done(cw->adap, CEC_TX_STATUS_ARB_LOST);
		kfree(cw);
	} else {
		INIT_DELAYED_WORK(&cw->work, vivid_cec_xfer_done_worker);
		dev->cec_xfer_start_jiffies = jiffies;
		dev->cec_xfer_time_jiffies = usecs_to_jiffies(cw->usecs);
		spin_unlock(&dev->cec_slock);
		schedule_delayed_work(&cw->work, dev->cec_xfer_time_jiffies);
	}
}

static int vivid_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	adap->cec_pin_is_high = true;
	return 0;
}

static int vivid_cec_adap_log_addr(struct cec_adapter *adap, u8 log_addr)
{
	return 0;
}

/*
 * One data bit takes 2400 us, each byte needs 10 bits so that's 24000 us
 * per byte.
 */
#define USECS_PER_BYTE 24000

static int vivid_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				   u32 signal_free_time, struct cec_msg *msg)
{
	struct vivid_dev *dev = cec_get_drvdata(adap);
	struct vivid_cec_work *cw = kzalloc(sizeof(*cw), GFP_KERNEL);
	long delta_jiffies = 0;

	if (cw == NULL)
		return -ENOMEM;
	cw->dev = dev;
	cw->adap = adap;
	cw->usecs = CEC_FREE_TIME_TO_USEC(signal_free_time) +
		    msg->len * USECS_PER_BYTE;
	cw->msg = *msg;

	spin_lock(&dev->cec_slock);
	list_add(&cw->list, &dev->cec_work_list);
	if (dev->cec_xfer_time_jiffies == 0) {
		INIT_DELAYED_WORK(&cw->work, vivid_cec_xfer_done_worker);
		dev->cec_xfer_start_jiffies = jiffies;
		dev->cec_xfer_time_jiffies = usecs_to_jiffies(cw->usecs);
		delta_jiffies = dev->cec_xfer_time_jiffies;
	} else {
		INIT_DELAYED_WORK(&cw->work, vivid_cec_xfer_try_worker);
		delta_jiffies = dev->cec_xfer_start_jiffies +
			dev->cec_xfer_time_jiffies - jiffies;
	}
	spin_unlock(&dev->cec_slock);
	schedule_delayed_work(&cw->work, delta_jiffies < 0 ? 0 : delta_jiffies);
	return 0;
}

static int vivid_received(struct cec_adapter *adap, struct cec_msg *msg)
{
	struct vivid_dev *dev = cec_get_drvdata(adap);
	struct cec_msg reply;
	u8 dest = cec_msg_destination(msg);
	u8 disp_ctl;
	char osd[14];

	if (cec_msg_is_broadcast(msg))
		dest = adap->log_addrs.log_addr[0];
	cec_msg_init(&reply, dest, cec_msg_initiator(msg));

	switch (cec_msg_opcode(msg)) {
	case CEC_MSG_SET_OSD_STRING:
		if (!cec_is_sink(adap))
			return -ENOMSG;
		cec_ops_set_osd_string(msg, &disp_ctl, osd);
		switch (disp_ctl) {
		case CEC_OP_DISP_CTL_DEFAULT:
			strscpy(dev->osd, osd, sizeof(dev->osd));
			dev->osd_jiffies = jiffies;
			break;
		case CEC_OP_DISP_CTL_UNTIL_CLEARED:
			strscpy(dev->osd, osd, sizeof(dev->osd));
			dev->osd_jiffies = 0;
			break;
		case CEC_OP_DISP_CTL_CLEAR:
			dev->osd[0] = 0;
			dev->osd_jiffies = 0;
			break;
		default:
			cec_msg_feature_abort(&reply, cec_msg_opcode(msg),
					      CEC_OP_ABORT_INVALID_OP);
			cec_transmit_msg(adap, &reply, false);
			break;
		}
		break;
	default:
		return -ENOMSG;
	}
	return 0;
}

static const struct cec_adap_ops vivid_cec_adap_ops = {
	.adap_enable = vivid_cec_adap_enable,
	.adap_log_addr = vivid_cec_adap_log_addr,
	.adap_transmit = vivid_cec_adap_transmit,
	.received = vivid_received,
};

struct cec_adapter *vivid_cec_alloc_adap(struct vivid_dev *dev,
					 unsigned int idx,
					 bool is_source)
{
	u32 caps = CEC_CAP_DEFAULTS | CEC_CAP_MONITOR_ALL | CEC_CAP_MONITOR_PIN;
	char name[32];

	snprintf(name, sizeof(name), "vivid-%03d-vid-%s%d",
		 dev->inst, is_source ? "out" : "cap", idx);
	return cec_allocate_adapter(&vivid_cec_adap_ops, dev,
				    name, caps, CEC_MAX_LOG_ADDRS);
}
