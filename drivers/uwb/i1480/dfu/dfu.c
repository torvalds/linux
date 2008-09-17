/*
 * Intel Wireless UWB Link 1480
 * Main driver
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * Common code for firmware upload used by the USB and PCI version;
 * i1480_fw_upload() takes a device descriptor and uses the function
 * pointers it provides to upload firmware and prepare the PHY.
 *
 * As well, provides common functions used by the rest of the code.
 */
#include "i1480-dfu.h"
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/uwb.h>
#include <linux/random.h>

#define D_LOCAL 0
#include <linux/uwb/debug.h>

/** @return 0 if If @evt is a valid reply event; otherwise complain */
int i1480_rceb_check(const struct i1480 *i1480, const struct uwb_rceb *rceb,
		     const char *cmd, u8 context,
		     unsigned expected_type, unsigned expected_event)
{
	int result = 0;
	struct device *dev = i1480->dev;
	if (rceb->bEventContext != context) {
		dev_err(dev, "%s: "
			"unexpected context id 0x%02x (expected 0x%02x)\n",
			cmd, rceb->bEventContext, context);
		result = -EINVAL;
	}
	if (rceb->bEventType != expected_type) {
		dev_err(dev, "%s: "
			"unexpected event type 0x%02x (expected 0x%02x)\n",
			cmd, rceb->bEventType, expected_type);
		result = -EINVAL;
	}
	if (le16_to_cpu(rceb->wEvent) != expected_event) {
		dev_err(dev, "%s: "
			"unexpected event 0x%04x (expected 0x%04x)\n",
			cmd, le16_to_cpu(rceb->wEvent), expected_event);
		result = -EINVAL;
	}
	return result;
}
EXPORT_SYMBOL_GPL(i1480_rceb_check);


/**
 * Execute a Radio Control Command
 *
 * Command data has to be in i1480->cmd_buf.
 *
 * @returns size of the reply data filled in i1480->evt_buf or < 0 errno
 *          code on error.
 */
ssize_t i1480_cmd(struct i1480 *i1480, const char *cmd_name, size_t cmd_size,
		  size_t reply_size)
{
	ssize_t result;
	struct uwb_rceb *reply = i1480->evt_buf;
	struct uwb_rccb *cmd = i1480->cmd_buf;
	u16 expected_event = reply->wEvent;
	u8 expected_type = reply->bEventType;
	u8 context;

	d_fnstart(3, i1480->dev, "(%p, %s, %zu)\n", i1480, cmd_name, cmd_size);
	init_completion(&i1480->evt_complete);
	i1480->evt_result = -EINPROGRESS;
	do {
		get_random_bytes(&context, 1);
	} while (context == 0x00 || context == 0xff);
	cmd->bCommandContext = context;
	result = i1480->cmd(i1480, cmd_name, cmd_size);
	if (result < 0)
		goto error;
	/* wait for the callback to report a event was received */
	result = wait_for_completion_interruptible_timeout(
		&i1480->evt_complete, HZ);
	if (result == 0) {
		result = -ETIMEDOUT;
		goto error;
	}
	if (result < 0)
		goto error;
	result = i1480->evt_result;
	if (result < 0) {
		dev_err(i1480->dev, "%s: command reply reception failed: %zd\n",
			cmd_name, result);
		goto error;
	}
	if (result != reply_size) {
		dev_err(i1480->dev, "%s returned only %zu bytes, %zu expected\n",
			cmd_name, result, reply_size);
		result = -EINVAL;
		goto error;
	}
	/* Verify we got the right event in response */
	result = i1480_rceb_check(i1480, i1480->evt_buf, cmd_name, context,
				  expected_type, expected_event);
error:
	d_fnend(3, i1480->dev, "(%p, %s, %zu) = %zd\n",
		i1480, cmd_name, cmd_size, result);
	return result;
}
EXPORT_SYMBOL_GPL(i1480_cmd);


/**
 * Get information about the MAC and PHY
 *
 * @wa:      Wired adaptor
 * @neh:     Notification/event handler
 * @reply:   Pointer to the reply event buffer
 * @returns: 0 if ok, < 0 errno code on error.
 */
static
int i1480_cmd_get_mac_phy_info(struct i1480 *i1480)
{
	int result;
	struct uwb_rccb *cmd = i1480->cmd_buf;
	struct i1480_evt_confirm_GMPI *reply = i1480->evt_buf;

	cmd->bCommandType = i1480_CET_VS1;
	cmd->wCommand = cpu_to_le16(i1480_CMD_GET_MAC_PHY_INFO);
	reply->rceb.bEventType = i1480_CET_VS1;
	reply->rceb.wEvent = i1480_EVT_GET_MAC_PHY_INFO;
	result = i1480_cmd(i1480, "GET_MAC_PHY_INFO", sizeof(*cmd),
			   sizeof(*reply));
	if (result < 0)
		goto out;
	if (le16_to_cpu(reply->status) != 0x00) {
		dev_err(i1480->dev,
			"GET_MAC_PHY_INFO: command execution failed: %d\n",
			reply->status);
		result = -EIO;
	}
out:
	return result;
}


/**
 * Get i1480's info and print it
 *
 * @wa:      Wire Adapter
 * @neh:     Notification/event handler
 * @returns: 0 if ok, < 0 errno code on error.
 */
static
int i1480_check_info(struct i1480 *i1480)
{
	struct i1480_evt_confirm_GMPI *reply = i1480->evt_buf;
	int result;
	unsigned mac_fw_rev;
#if i1480_FW <= 0x00000302
	unsigned phy_fw_rev;
#endif
	if (i1480->quirk_no_check_info) {
		dev_err(i1480->dev, "firmware info check disabled\n");
		return 0;
	}

	result = i1480_cmd_get_mac_phy_info(i1480);
	if (result < 0) {
		dev_err(i1480->dev, "Cannot get MAC & PHY information: %d\n",
			result);
		goto out;
	}
	mac_fw_rev = le16_to_cpu(reply->mac_fw_rev);
#if i1480_FW > 0x00000302
	dev_info(i1480->dev,
		 "HW v%02hx  "
		 "MAC FW v%02hx.%02hx caps %04hx  "
		 "PHY type %02hx v%02hx caps %02hx %02hx %02hx\n",
		 reply->hw_rev, mac_fw_rev >> 8, mac_fw_rev & 0xff,
		 le16_to_cpu(reply->mac_caps),
		 reply->phy_vendor, reply->phy_rev,
		 reply->phy_caps[0], reply->phy_caps[1], reply->phy_caps[2]);
#else
	phy_fw_rev = le16_to_cpu(reply->phy_fw_rev);
	dev_info(i1480->dev, "MAC FW v%02hx.%02hx caps %04hx "
		 " PHY FW v%02hx.%02hx caps %04hx\n",
		 mac_fw_rev >> 8, mac_fw_rev & 0xff,
		 le16_to_cpu(reply->mac_caps),
		 phy_fw_rev >> 8, phy_fw_rev & 0xff,
		 le16_to_cpu(reply->phy_caps));
#endif
	dev_dbg(i1480->dev,
		"key-stores:%hu mcast-addr-stores:%hu sec-modes:%hu\n",
		(unsigned short) reply->key_stores,
		le16_to_cpu(reply->mcast_addr_stores),
		(unsigned short) reply->sec_mode_supported);
	/* FIXME: complain if fw version too low -- pending for
	 * numbering to stabilize */
out:
	return result;
}


static
int i1480_print_state(struct i1480 *i1480)
{
	int result;
	u32 *buf = (u32 *) i1480->cmd_buf;

	result = i1480->read(i1480, 0x80080000, 2 * sizeof(*buf));
	if (result < 0) {
		dev_err(i1480->dev, "cannot read U & L states: %d\n", result);
		goto error;
	}
	dev_info(i1480->dev, "state U 0x%08x, L 0x%08x\n", buf[0], buf[1]);
error:
	return result;
}


/*
 * PCI probe, firmware uploader
 *
 * _mac_fw_upload() will call rc_setup(), which needs an rc_release().
 */
int i1480_fw_upload(struct i1480 *i1480)
{
	int result;

	result = i1480_pre_fw_upload(i1480);	/* PHY pre fw */
	if (result < 0 && result != -ENOENT) {
		i1480_print_state(i1480);
		goto error;
	}
	result = i1480_mac_fw_upload(i1480);	/* MAC fw */
	if (result < 0) {
		if (result == -ENOENT)
			dev_err(i1480->dev, "Cannot locate MAC FW file '%s'\n",
				i1480->mac_fw_name);
		else
			i1480_print_state(i1480);
		goto error;
	}
	result = i1480_phy_fw_upload(i1480);	/* PHY fw */
	if (result < 0 && result != -ENOENT) {
		i1480_print_state(i1480);
		goto error_rc_release;
	}
	result = i1480_check_info(i1480);
	if (result < 0) {
		dev_warn(i1480->dev, "Warning! Cannot check firmware info: %d\n",
			 result);
		result = 0;
	}
	dev_info(i1480->dev, "firmware uploaded successfully\n");
error_rc_release:
	if (i1480->rc_release)
		i1480->rc_release(i1480);
	result = 0;
error:
	return result;
}
EXPORT_SYMBOL_GPL(i1480_fw_upload);
