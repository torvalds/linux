// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-cec.c - A Virtual Video Test Driver, cec emulation
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/delay.h>
#include <media/cec.h>

#include "vivid-core.h"
#include "vivid-cec.h"

#define CEC_START_BIT_US		4500
#define CEC_DATA_BIT_US			2400
#define CEC_MARGIN_US			350

struct xfer_on_bus {
	struct cec_adapter	*adap;
	u8			status;
};

static bool find_dest_adap(struct vivid_dev *dev,
			   struct cec_adapter *adap, u8 dest)
{
	unsigned int i, j;

	if (dest >= 0xf)
		return false;

	if (adap != dev->cec_rx_adap && dev->cec_rx_adap &&
	    dev->cec_rx_adap->is_configured &&
	    cec_has_log_addr(dev->cec_rx_adap, dest))
		return true;

	for (i = 0, j = 0; i < dev->num_inputs; i++) {
		unsigned int menu_idx =
			dev->input_is_connected_to_output[i];

		if (dev->input_type[i] != HDMI)
			continue;
		j++;
		if (menu_idx < FIXED_MENU_ITEMS)
			continue;

		struct vivid_dev *dev_tx = vivid_ctrl_hdmi_to_output_instance[menu_idx];
		unsigned int output = vivid_ctrl_hdmi_to_output_index[menu_idx];

		if (!dev_tx)
			continue;

		unsigned int hdmi_output = dev_tx->output_to_iface_index[output];

		if (adap == dev_tx->cec_tx_adap[hdmi_output])
			continue;
		if (!dev_tx->cec_tx_adap[hdmi_output]->is_configured)
			continue;
		if (cec_has_log_addr(dev_tx->cec_tx_adap[hdmi_output], dest))
			return true;
	}
	return false;
}

static bool xfer_ready(struct vivid_dev *dev)
{
	unsigned int i;
	bool ready = false;

	spin_lock(&dev->cec_xfers_slock);
	for (i = 0; i < ARRAY_SIZE(dev->xfers); i++) {
		if (dev->xfers[i].sft &&
		    dev->xfers[i].sft <= dev->cec_sft) {
			ready = true;
			break;
		}
	}
	spin_unlock(&dev->cec_xfers_slock);

	return ready;
}

/*
 * If an adapter tries to send successive messages, it must wait for the
 * longest signal-free time between its transmissions. But, if another
 * adapter sends a message in the interim, then the wait can be reduced
 * because the messages are no longer successive. Make these adjustments
 * if necessary. Should be called holding cec_xfers_slock.
 */
static void adjust_sfts(struct vivid_dev *dev)
{
	unsigned int i;
	u8 initiator;

	for (i = 0; i < ARRAY_SIZE(dev->xfers); i++) {
		if (dev->xfers[i].sft <= CEC_SIGNAL_FREE_TIME_RETRY)
			continue;
		initiator = dev->xfers[i].msg[0] >> 4;
		if (initiator == dev->last_initiator)
			dev->xfers[i].sft = CEC_SIGNAL_FREE_TIME_NEXT_XFER;
		else
			dev->xfers[i].sft = CEC_SIGNAL_FREE_TIME_NEW_INITIATOR;
	}
}

/*
 * The main emulation of the bus on which CEC adapters attempt to send
 * messages to each other. The bus keeps track of how long it has been
 * signal-free and accepts a pending transmission only if the state of
 * the bus matches the transmission's signal-free requirements. It calls
 * cec_transmit_attempt_done() for all transmits that enter the bus and
 * cec_received_msg() for successful transmits.
 */
int vivid_cec_bus_thread(void *_dev)
{
	u32 last_sft;
	unsigned int i, j;
	unsigned int dest;
	ktime_t start, end;
	s64 delta_us, retry_us;
	struct vivid_dev *dev = _dev;

	dev->cec_sft = CEC_SIGNAL_FREE_TIME_NEXT_XFER;
	for (;;) {
		bool first = true;
		int wait_xfer_us = 0;
		bool valid_dest = false;
		int wait_arb_lost_us = 0;
		unsigned int first_idx = 0;
		unsigned int first_status = 0;
		struct cec_msg first_msg = {};
		struct xfer_on_bus xfers_on_bus[MAX_OUTPUTS] = {};

		wait_event_interruptible(dev->kthread_waitq_cec, xfer_ready(dev) ||
					 kthread_should_stop());
		if (kthread_should_stop())
			break;
		last_sft = dev->cec_sft;
		dev->cec_sft = 0;
		/*
		 * Move the messages that are ready onto the bus. The adapter with
		 * the most leading zeros will win control of the bus and any other
		 * adapters will lose arbitration.
		 */
		spin_lock(&dev->cec_xfers_slock);
		for (i = 0; i < ARRAY_SIZE(dev->xfers); i++) {
			if (!dev->xfers[i].sft || dev->xfers[i].sft > last_sft)
				continue;
			if (first) {
				first = false;
				first_idx = i;
				xfers_on_bus[first_idx].adap = dev->xfers[i].adap;
				memcpy(first_msg.msg, dev->xfers[i].msg, dev->xfers[i].len);
				first_msg.len = dev->xfers[i].len;
			} else {
				xfers_on_bus[i].adap = dev->xfers[i].adap;
				xfers_on_bus[i].status = CEC_TX_STATUS_ARB_LOST;
				/*
				 * For simplicity wait for all 4 bits of the initiator's
				 * address even though HDMI specification uses bit-level
				 * precision.
				 */
				wait_arb_lost_us = 4 * CEC_DATA_BIT_US + CEC_START_BIT_US;
			}
			dev->xfers[i].sft = 0;
		}
		dev->last_initiator = cec_msg_initiator(&first_msg);
		adjust_sfts(dev);
		spin_unlock(&dev->cec_xfers_slock);

		dest = cec_msg_destination(&first_msg);
		valid_dest = cec_msg_is_broadcast(&first_msg);
		if (!valid_dest)
			valid_dest = find_dest_adap(dev, xfers_on_bus[first_idx].adap, dest);
		if (valid_dest) {
			first_status = CEC_TX_STATUS_OK;
			/*
			 * Message length is in bytes, but each byte is transmitted in
			 * a block of 10 bits.
			 */
			wait_xfer_us = first_msg.len * 10 * CEC_DATA_BIT_US;
		} else {
			first_status = CEC_TX_STATUS_NACK;
			/*
			 * A message that is not acknowledged stops transmitting after
			 * the header block of 10 bits.
			 */
			wait_xfer_us = 10 * CEC_DATA_BIT_US;
		}
		wait_xfer_us += CEC_START_BIT_US;
		xfers_on_bus[first_idx].status = first_status;

		/* Sleep as if sending messages on a real hardware bus. */
		start = ktime_get();
		if (wait_arb_lost_us) {
			usleep_range(wait_arb_lost_us - CEC_MARGIN_US, wait_arb_lost_us);
			for (i = 0; i < ARRAY_SIZE(xfers_on_bus); i++) {
				if (xfers_on_bus[i].status != CEC_TX_STATUS_ARB_LOST)
					continue;
				cec_transmit_attempt_done(xfers_on_bus[i].adap,
							  CEC_TX_STATUS_ARB_LOST);
			}
			if (kthread_should_stop())
				break;
		}
		wait_xfer_us -= wait_arb_lost_us;
		usleep_range(wait_xfer_us - CEC_MARGIN_US, wait_xfer_us);
		cec_transmit_attempt_done(xfers_on_bus[first_idx].adap, first_status);
		if (kthread_should_stop())
			break;
		if (first_status == CEC_TX_STATUS_OK) {
			if (xfers_on_bus[first_idx].adap != dev->cec_rx_adap)
				cec_received_msg(dev->cec_rx_adap, &first_msg);
			for (i = 0, j = 0; i < dev->num_inputs; i++) {
				unsigned int menu_idx =
					dev->input_is_connected_to_output[i];

				if (dev->input_type[i] != HDMI)
					continue;
				j++;
				if (menu_idx < FIXED_MENU_ITEMS)
					continue;

				struct vivid_dev *dev_tx = vivid_ctrl_hdmi_to_output_instance[menu_idx];
				unsigned int output = vivid_ctrl_hdmi_to_output_index[menu_idx];

				if (!dev_tx)
					continue;

				unsigned int hdmi_output = dev_tx->output_to_iface_index[output];

				if (xfers_on_bus[first_idx].adap != dev_tx->cec_tx_adap[hdmi_output])
					cec_received_msg(dev_tx->cec_tx_adap[hdmi_output], &first_msg);
			}
		}
		end = ktime_get();
		/*
		 * If the emulated transfer took more or less time than it should
		 * have, then compensate by adjusting the wait time needed for the
		 * bus to be signal-free for 3 bit periods (the retry time).
		 */
		delta_us = div_s64(end - start, 1000);
		delta_us -= wait_xfer_us + wait_arb_lost_us;
		retry_us = CEC_SIGNAL_FREE_TIME_RETRY * CEC_DATA_BIT_US - delta_us;
		if (retry_us > CEC_MARGIN_US)
			usleep_range(retry_us - CEC_MARGIN_US, retry_us);
		dev->cec_sft = CEC_SIGNAL_FREE_TIME_RETRY;
		/*
		 * If there are no messages that need to be retried, check if any
		 * adapters that did not just transmit a message are ready to
		 * transmit. If none of these adapters are ready, then increase
		 * the signal-free time so that the bus is available to all
		 * adapters and go back to waiting for a transmission.
		 */
		while (dev->cec_sft >= CEC_SIGNAL_FREE_TIME_RETRY &&
		       dev->cec_sft < CEC_SIGNAL_FREE_TIME_NEXT_XFER &&
		       !xfer_ready(dev) && !kthread_should_stop()) {
			usleep_range(2 * CEC_DATA_BIT_US - CEC_MARGIN_US,
				     2 * CEC_DATA_BIT_US);
			dev->cec_sft += 2;
		}
	}
	return 0;
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

static int vivid_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				   u32 signal_free_time, struct cec_msg *msg)
{
	struct vivid_dev *dev = cec_get_drvdata(adap);
	struct vivid_dev *dev_rx = dev;
	u8 idx = cec_msg_initiator(msg);
	u8 output = 0;

	if (dev->cec_rx_adap != adap) {
		int i;

		for (i = 0; i < dev->num_hdmi_outputs; i++)
			if (dev->cec_tx_adap[i] == adap)
				break;
		if (i == dev->num_hdmi_outputs)
			return -ENONET;
		output = dev->hdmi_index_to_output_index[i];
		dev_rx = dev->output_to_input_instance[output];
		if (!dev_rx)
			return -ENONET;
	}
	spin_lock(&dev_rx->cec_xfers_slock);
	dev_rx->xfers[idx].adap = adap;
	memcpy(dev_rx->xfers[idx].msg, msg->msg, CEC_MAX_MSG_SIZE);
	dev_rx->xfers[idx].len = msg->len;
	dev_rx->xfers[idx].sft = CEC_SIGNAL_FREE_TIME_RETRY;
	if (signal_free_time > CEC_SIGNAL_FREE_TIME_RETRY) {
		if (idx == dev_rx->last_initiator)
			dev_rx->xfers[idx].sft = CEC_SIGNAL_FREE_TIME_NEXT_XFER;
		else
			dev_rx->xfers[idx].sft = CEC_SIGNAL_FREE_TIME_NEW_INITIATOR;
	}
	spin_unlock(&dev_rx->cec_xfers_slock);
	wake_up_interruptible(&dev_rx->kthread_waitq_cec);

	return 0;
}

static int vivid_received(struct cec_adapter *adap, struct cec_msg *msg)
{
	struct vivid_dev *dev = cec_get_drvdata(adap);
	struct cec_msg reply;
	u8 dest = cec_msg_destination(msg);

	if (cec_msg_is_broadcast(msg))
		dest = adap->log_addrs.log_addr[0];
	cec_msg_init(&reply, dest, cec_msg_initiator(msg));

	switch (cec_msg_opcode(msg)) {
	case CEC_MSG_SET_OSD_STRING: {
		u8 disp_ctl;
		char osd[14];

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
	}
	case CEC_MSG_VENDOR_COMMAND_WITH_ID: {
		u32 vendor_id;
		u8 size;
		const u8 *vendor_cmd;

		/*
		 * If we receive <Vendor Command With ID> with our vendor ID
		 * and with a payload of size 1, and the payload value is odd,
		 * then we reply with the same message, but with the payload
		 * byte incremented by 1.
		 *
		 * If the size is 1 and the payload value is even, then we
		 * ignore the message.
		 *
		 * The reason we reply to odd instead of even payload values
		 * is that it allows for testing of the corner case where the
		 * reply value is 0 (0xff + 1 % 256).
		 *
		 * For other sizes we Feature Abort.
		 *
		 * This is added for the specific purpose of testing the
		 * CEC_MSG_FL_REPLY_VENDOR_ID flag using vivid.
		 */
		cec_ops_vendor_command_with_id(msg, &vendor_id, &size, &vendor_cmd);
		if (vendor_id != adap->log_addrs.vendor_id)
			break;
		if (size == 1) {
			// Ignore even op values
			if (!(vendor_cmd[0] & 1))
				break;
			reply.len = msg->len;
			memcpy(reply.msg + 1, msg->msg + 1, msg->len - 1);
			reply.msg[msg->len - 1]++;
		} else {
			cec_msg_feature_abort(&reply, cec_msg_opcode(msg),
					      CEC_OP_ABORT_INVALID_OP);
		}
		cec_transmit_msg(adap, &reply, false);
		break;
	}
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
