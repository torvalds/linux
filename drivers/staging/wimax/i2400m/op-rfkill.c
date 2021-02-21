// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Wireless WiMAX Connection 2400m
 * Implement backend for the WiMAX stack rfkill support
 *
 * Copyright (C) 2007-2008 Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * The WiMAX kernel stack integrates into RF-Kill and keeps the
 * switches's status. We just need to:
 *
 * - report changes in the HW RF Kill switch [with
 *   wimax_rfkill_{sw,hw}_report(), which happens when we detect those
 *   indications coming through hardware reports]. We also do it on
 *   initialization to let the stack know the initial HW state.
 *
 * - implement indications from the stack to change the SW RF Kill
 *   switch (coming from sysfs, the wimax stack or user space).
 */
#include "i2400m.h"
#include "linux-wimax-i2400m.h"
#include <linux/slab.h>



#define D_SUBMODULE rfkill
#include "debug-levels.h"

/*
 * Return true if the i2400m radio is in the requested wimax_rf_state state
 *
 */
static
int i2400m_radio_is(struct i2400m *i2400m, enum wimax_rf_state state)
{
	if (state == WIMAX_RF_OFF)
		return i2400m->state == I2400M_SS_RF_OFF
			|| i2400m->state == I2400M_SS_RF_SHUTDOWN;
	else if (state == WIMAX_RF_ON)
		/* state == WIMAX_RF_ON */
		return i2400m->state != I2400M_SS_RF_OFF
			&& i2400m->state != I2400M_SS_RF_SHUTDOWN;
	else {
		BUG();
		return -EINVAL;	/* shut gcc warnings on certain arches */
	}
}


/*
 * WiMAX stack operation: implement SW RFKill toggling
 *
 * @wimax_dev: device descriptor
 * @skb: skb where the message has been received; skb->data is
 *       expected to point to the message payload.
 * @genl_info: passed by the generic netlink layer
 *
 * Generic Netlink will call this function when a message is sent from
 * userspace to change the software RF-Kill switch status.
 *
 * This function will set the device's software RF-Kill switch state to
 * match what is requested.
 *
 * NOTE: the i2400m has a strict state machine; we can only set the
 *       RF-Kill switch when it is on, the HW RF-Kill is on and the
 *       device is initialized. So we ignore errors steaming from not
 *       being in the right state (-EILSEQ).
 */
int i2400m_op_rfkill_sw_toggle(struct wimax_dev *wimax_dev,
			       enum wimax_rf_state state)
{
	int result;
	struct i2400m *i2400m = wimax_dev_to_i2400m(wimax_dev);
	struct device *dev = i2400m_dev(i2400m);
	struct sk_buff *ack_skb;
	struct {
		struct i2400m_l3l4_hdr hdr;
		struct i2400m_tlv_rf_operation sw_rf;
	} __packed *cmd;
	char strerr[32];

	d_fnstart(4, dev, "(wimax_dev %p state %d)\n", wimax_dev, state);

	result = -ENOMEM;
	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		goto error_alloc;
	cmd->hdr.type = cpu_to_le16(I2400M_MT_CMD_RF_CONTROL);
	cmd->hdr.length = cpu_to_le16(sizeof(cmd->sw_rf));
	cmd->hdr.version = cpu_to_le16(I2400M_L3L4_VERSION);
	cmd->sw_rf.hdr.type = cpu_to_le16(I2400M_TLV_RF_OPERATION);
	cmd->sw_rf.hdr.length = cpu_to_le16(sizeof(cmd->sw_rf.status));
	switch (state) {
	case WIMAX_RF_OFF:	/* RFKILL ON, radio OFF */
		cmd->sw_rf.status = cpu_to_le32(2);
		break;
	case WIMAX_RF_ON:	/* RFKILL OFF, radio ON */
		cmd->sw_rf.status = cpu_to_le32(1);
		break;
	default:
		BUG();
	}

	ack_skb = i2400m_msg_to_dev(i2400m, cmd, sizeof(*cmd));
	result = PTR_ERR(ack_skb);
	if (IS_ERR(ack_skb)) {
		dev_err(dev, "Failed to issue 'RF Control' command: %d\n",
			result);
		goto error_msg_to_dev;
	}
	result = i2400m_msg_check_status(wimax_msg_data(ack_skb),
					 strerr, sizeof(strerr));
	if (result < 0) {
		dev_err(dev, "'RF Control' (0x%04x) command failed: %d - %s\n",
			I2400M_MT_CMD_RF_CONTROL, result, strerr);
		goto error_cmd;
	}

	/* Now we wait for the state to change to RADIO_OFF or RADIO_ON */
	result = wait_event_timeout(
		i2400m->state_wq, i2400m_radio_is(i2400m, state),
		5 * HZ);
	if (result == 0)
		result = -ETIMEDOUT;
	if (result < 0)
		dev_err(dev, "Error waiting for device to toggle RF state: "
			"%d\n", result);
	result = 0;
error_cmd:
	kfree_skb(ack_skb);
error_msg_to_dev:
error_alloc:
	d_fnend(4, dev, "(wimax_dev %p state %d) = %d\n",
		wimax_dev, state, result);
	kfree(cmd);
	return result;
}


/*
 * Inform the WiMAX stack of changes in the RF Kill switches reported
 * by the device
 *
 * @i2400m: device descriptor
 * @rfss: TLV for RF Switches status; already validated
 *
 * NOTE: the reports on RF switch status cannot be trusted
 *       or used until the device is in a state of RADIO_OFF
 *       or greater.
 */
void i2400m_report_tlv_rf_switches_status(
	struct i2400m *i2400m,
	const struct i2400m_tlv_rf_switches_status *rfss)
{
	struct device *dev = i2400m_dev(i2400m);
	enum i2400m_rf_switch_status hw, sw;
	enum wimax_st wimax_state;

	sw = rfss->sw_rf_switch;
	hw = rfss->hw_rf_switch;

	d_fnstart(3, dev, "(i2400m %p rfss %p [hw %u sw %u])\n",
		  i2400m, rfss, hw, sw);
	/* We only process rw switch evens when the device has been
	 * fully initialized */
	wimax_state = wimax_state_get(&i2400m->wimax_dev);
	if (wimax_state < WIMAX_ST_RADIO_OFF) {
		d_printf(3, dev, "ignoring RF switches report, state %u\n",
			 wimax_state);
		goto out;
	}
	switch (sw) {
	case I2400M_RF_SWITCH_ON:	/* RF Kill disabled (radio on) */
		wimax_report_rfkill_sw(&i2400m->wimax_dev, WIMAX_RF_ON);
		break;
	case I2400M_RF_SWITCH_OFF:	/* RF Kill enabled (radio off) */
		wimax_report_rfkill_sw(&i2400m->wimax_dev, WIMAX_RF_OFF);
		break;
	default:
		dev_err(dev, "HW BUG? Unknown RF SW state 0x%x\n", sw);
	}

	switch (hw) {
	case I2400M_RF_SWITCH_ON:	/* RF Kill disabled (radio on) */
		wimax_report_rfkill_hw(&i2400m->wimax_dev, WIMAX_RF_ON);
		break;
	case I2400M_RF_SWITCH_OFF:	/* RF Kill enabled (radio off) */
		wimax_report_rfkill_hw(&i2400m->wimax_dev, WIMAX_RF_OFF);
		break;
	default:
		dev_err(dev, "HW BUG? Unknown RF HW state 0x%x\n", hw);
	}
out:
	d_fnend(3, dev, "(i2400m %p rfss %p [hw %u sw %u]) = void\n",
		i2400m, rfss, hw, sw);
}
