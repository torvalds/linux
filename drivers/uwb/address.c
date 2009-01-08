/*
 * Ultra Wide Band
 * Address management
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
 * FIXME: docs
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/random.h>
#include <linux/etherdevice.h>

#include "uwb-internal.h"


/** Device Address Management command */
struct uwb_rc_cmd_dev_addr_mgmt {
	struct uwb_rccb rccb;
	u8 bmOperationType;
	u8 baAddr[6];
} __attribute__((packed));


/**
 * Low level command for setting/getting UWB radio's addresses
 *
 * @hwarc:	HWA Radio Control interface instance
 * @bmOperationType:
 * 		Set/get, MAC/DEV (see WUSB1.0[8.6.2.2])
 * @baAddr:	address buffer--assumed to have enough data to hold
 *              the address type requested.
 * @reply:	Pointer to reply buffer (can be stack allocated)
 * @returns:	0 if ok, < 0 errno code on error.
 *
 * @cmd has to be allocated because USB cannot grok USB or vmalloc
 * buffers depending on your combination of host architecture.
 */
static
int uwb_rc_dev_addr_mgmt(struct uwb_rc *rc,
			 u8 bmOperationType, const u8 *baAddr,
			 struct uwb_rc_evt_dev_addr_mgmt *reply)
{
	int result;
	struct uwb_rc_cmd_dev_addr_mgmt *cmd;

	result = -ENOMEM;
	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		goto error_kzalloc;
	cmd->rccb.bCommandType = UWB_RC_CET_GENERAL;
	cmd->rccb.wCommand = cpu_to_le16(UWB_RC_CMD_DEV_ADDR_MGMT);
	cmd->bmOperationType = bmOperationType;
	if (baAddr) {
		size_t size = 0;
		switch (bmOperationType >> 1) {
		case 0:	size = 2; break;
		case 1:	size = 6; break;
		default: BUG();
		}
		memcpy(cmd->baAddr, baAddr, size);
	}
	reply->rceb.bEventType = UWB_RC_CET_GENERAL;
	reply->rceb.wEvent = UWB_RC_CMD_DEV_ADDR_MGMT;
	result = uwb_rc_cmd(rc, "DEV-ADDR-MGMT",
			    &cmd->rccb, sizeof(*cmd),
			    &reply->rceb, sizeof(*reply));
	if (result < 0)
		goto error_cmd;
	if (result < sizeof(*reply)) {
		dev_err(&rc->uwb_dev.dev,
			"DEV-ADDR-MGMT: not enough data replied: "
			"%d vs %zu bytes needed\n", result, sizeof(*reply));
		result = -ENOMSG;
	} else if (reply->bResultCode != UWB_RC_RES_SUCCESS) {
		dev_err(&rc->uwb_dev.dev,
			"DEV-ADDR-MGMT: command execution failed: %s (%d)\n",
			uwb_rc_strerror(reply->bResultCode),
			reply->bResultCode);
		result = -EIO;
	} else
		result = 0;
error_cmd:
	kfree(cmd);
error_kzalloc:
	return result;
}


/**
 * Set the UWB RC MAC or device address.
 *
 * @rc:      UWB Radio Controller
 * @_addr:   Pointer to address to write [assumed to be either a
 *           'struct uwb_mac_addr *' or a 'struct uwb_dev_addr *'].
 * @type:    Type of address to set (UWB_ADDR_DEV or UWB_ADDR_MAC).
 * @returns: 0 if ok, < 0 errno code on error.
 *
 * Some anal retentivity here: even if both 'struct
 * uwb_{dev,mac}_addr' have the actual byte array in the same offset
 * and I could just pass _addr to hwarc_cmd_dev_addr_mgmt(), I prefer
 * to use some syntatic sugar in case someday we decide to change the
 * format of the structs. The compiler will optimize it out anyway.
 */
static int uwb_rc_addr_set(struct uwb_rc *rc,
		    const void *_addr, enum uwb_addr_type type)
{
	int result;
	u8 bmOperationType = 0x1; 		/* Set address */
	const struct uwb_dev_addr *dev_addr = _addr;
	const struct uwb_mac_addr *mac_addr = _addr;
	struct uwb_rc_evt_dev_addr_mgmt reply;
	const u8 *baAddr;

	result = -EINVAL;
	switch (type) {
	case UWB_ADDR_DEV:
		baAddr = dev_addr->data;
		break;
	case UWB_ADDR_MAC:
		baAddr = mac_addr->data;
		bmOperationType |= 0x2;
		break;
	default:
		return result;
	}
	return uwb_rc_dev_addr_mgmt(rc, bmOperationType, baAddr, &reply);
}


/**
 * Get the UWB radio's MAC or device address.
 *
 * @rc:      UWB Radio Controller
 * @_addr:   Where to write the address data [assumed to be either a
 *           'struct uwb_mac_addr *' or a 'struct uwb_dev_addr *'].
 * @type:    Type of address to get (UWB_ADDR_DEV or UWB_ADDR_MAC).
 * @returns: 0 if ok (and *_addr set), < 0 errno code on error.
 *
 * See comment in uwb_rc_addr_set() about anal retentivity in the
 * type handling of the address variables.
 */
static int uwb_rc_addr_get(struct uwb_rc *rc,
		    void *_addr, enum uwb_addr_type type)
{
	int result;
	u8 bmOperationType = 0x0; 		/* Get address */
	struct uwb_rc_evt_dev_addr_mgmt evt;
	struct uwb_dev_addr *dev_addr = _addr;
	struct uwb_mac_addr *mac_addr = _addr;
	u8 *baAddr;

	result = -EINVAL;
	switch (type) {
	case UWB_ADDR_DEV:
		baAddr = dev_addr->data;
		break;
	case UWB_ADDR_MAC:
		bmOperationType |= 0x2;
		baAddr = mac_addr->data;
		break;
	default:
		return result;
	}
	result = uwb_rc_dev_addr_mgmt(rc, bmOperationType, baAddr, &evt);
	if (result == 0)
		switch (type) {
		case UWB_ADDR_DEV:
			memcpy(&dev_addr->data, evt.baAddr,
			       sizeof(dev_addr->data));
			break;
		case UWB_ADDR_MAC:
			memcpy(&mac_addr->data, evt.baAddr,
			       sizeof(mac_addr->data));
			break;
		default:		/* shut gcc up */
			BUG();
		}
	return result;
}


/** Get @rc's MAC address to @addr */
int uwb_rc_mac_addr_get(struct uwb_rc *rc,
			struct uwb_mac_addr *addr) {
	return uwb_rc_addr_get(rc, addr, UWB_ADDR_MAC);
}
EXPORT_SYMBOL_GPL(uwb_rc_mac_addr_get);


/** Get @rc's device address to @addr */
int uwb_rc_dev_addr_get(struct uwb_rc *rc,
			struct uwb_dev_addr *addr) {
	return uwb_rc_addr_get(rc, addr, UWB_ADDR_DEV);
}
EXPORT_SYMBOL_GPL(uwb_rc_dev_addr_get);


/** Set @rc's address to @addr */
int uwb_rc_mac_addr_set(struct uwb_rc *rc,
			const struct uwb_mac_addr *addr)
{
	int result = -EINVAL;
	mutex_lock(&rc->uwb_dev.mutex);
	result = uwb_rc_addr_set(rc, addr, UWB_ADDR_MAC);
	mutex_unlock(&rc->uwb_dev.mutex);
	return result;
}


/** Set @rc's address to @addr */
int uwb_rc_dev_addr_set(struct uwb_rc *rc,
			const struct uwb_dev_addr *addr)
{
	int result = -EINVAL;
	mutex_lock(&rc->uwb_dev.mutex);
	result = uwb_rc_addr_set(rc, addr, UWB_ADDR_DEV);
	rc->uwb_dev.dev_addr = *addr;
	mutex_unlock(&rc->uwb_dev.mutex);
	return result;
}

/* Returns !0 if given address is already assigned to device. */
int __uwb_mac_addr_assigned_check(struct device *dev, void *_addr)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_mac_addr *addr = _addr;

	if (!uwb_mac_addr_cmp(addr, &uwb_dev->mac_addr))
		return !0;
	return 0;
}

/* Returns !0 if given address is already assigned to device. */
int __uwb_dev_addr_assigned_check(struct device *dev, void *_addr)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_dev_addr *addr = _addr;
	if (!uwb_dev_addr_cmp(addr, &uwb_dev->dev_addr))
		return !0;
	return 0;
}

/**
 * uwb_dev_addr_assign - assigned a generated DevAddr to a radio controller
 * @rc:      the (local) radio controller device requiring a new DevAddr
 *
 * A new DevAddr is required when:
 *    - first setting up a radio controller
 *    - if the hardware reports a DevAddr conflict
 *
 * The DevAddr is randomly generated in the generated DevAddr range
 * [0x100, 0xfeff]. The number of devices in a beacon group is limited
 * by mMaxBPLength (96) so this address space will never be exhausted.
 *
 * [ECMA-368] 17.1.1, 17.16.
 */
int uwb_rc_dev_addr_assign(struct uwb_rc *rc)
{
	struct uwb_dev_addr new_addr;

	do {
		get_random_bytes(new_addr.data, sizeof(new_addr.data));
	} while (new_addr.data[0] == 0x00 || new_addr.data[0] == 0xff
		 || __uwb_dev_addr_assigned(rc, &new_addr));

	return uwb_rc_dev_addr_set(rc, &new_addr);
}

/**
 * uwbd_evt_handle_rc_dev_addr_conflict - handle a DEV_ADDR_CONFLICT event
 * @evt: the DEV_ADDR_CONFLICT notification from the radio controller
 *
 * A new (non-conflicting) DevAddr is assigned to the radio controller.
 *
 * [ECMA-368] 17.1.1.1.
 */
int uwbd_evt_handle_rc_dev_addr_conflict(struct uwb_event *evt)
{
	struct uwb_rc *rc = evt->rc;

	return uwb_rc_dev_addr_assign(rc);
}

/*
 * Print the 48-bit EUI MAC address of the radio controller when
 * reading /sys/class/uwb_rc/XX/mac_address
 */
static ssize_t uwb_rc_mac_addr_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_rc *rc = uwb_dev->rc;
	struct uwb_mac_addr addr;
	ssize_t result;

	mutex_lock(&rc->uwb_dev.mutex);
	result = uwb_rc_addr_get(rc, &addr, UWB_ADDR_MAC);
	mutex_unlock(&rc->uwb_dev.mutex);
	if (result >= 0) {
		result = uwb_mac_addr_print(buf, UWB_ADDR_STRSIZE, &addr);
		buf[result++] = '\n';
	}
	return result;
}

/*
 * Parse a 48 bit address written to /sys/class/uwb_rc/XX/mac_address
 * and if correct, set it.
 */
static ssize_t uwb_rc_mac_addr_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_rc *rc = uwb_dev->rc;
	struct uwb_mac_addr addr;
	ssize_t result;

	result = sscanf(buf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx\n",
			&addr.data[0], &addr.data[1], &addr.data[2],
			&addr.data[3], &addr.data[4], &addr.data[5]);
	if (result != 6) {
		result = -EINVAL;
		goto out;
	}
	if (is_multicast_ether_addr(addr.data)) {
		dev_err(&rc->uwb_dev.dev, "refusing to set multicast "
			"MAC address %s\n", buf);
		result = -EINVAL;
		goto out;
	}
	result = uwb_rc_mac_addr_set(rc, &addr);
	if (result == 0)
		rc->uwb_dev.mac_addr = addr;
out:
	return result < 0 ? result : size;
}
DEVICE_ATTR(mac_address, S_IRUGO | S_IWUSR, uwb_rc_mac_addr_show, uwb_rc_mac_addr_store);

/** Print @addr to @buf, @return bytes written */
size_t __uwb_addr_print(char *buf, size_t buf_size, const unsigned char *addr,
			int type)
{
	size_t result;
	if (type)
		result = scnprintf(buf, buf_size,
				  "%02x:%02x:%02x:%02x:%02x:%02x",
				  addr[0], addr[1], addr[2],
				  addr[3], addr[4], addr[5]);
	else
		result = scnprintf(buf, buf_size, "%02x:%02x",
				  addr[1], addr[0]);
	return result;
}
EXPORT_SYMBOL_GPL(__uwb_addr_print);
