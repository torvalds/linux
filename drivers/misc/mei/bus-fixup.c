/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uuid.h>

#include <linux/mei_cl_bus.h>

#include "mei_dev.h"
#include "client.h"

#define MEI_UUID_NFC_INFO UUID_LE(0xd2de1625, 0x382d, 0x417d, \
			0x48, 0xa4, 0xef, 0xab, 0xba, 0x8a, 0x12, 0x06)

static const uuid_le mei_nfc_info_guid = MEI_UUID_NFC_INFO;

#define MEI_UUID_NFC_HCI UUID_LE(0x0bb17a78, 0x2a8e, 0x4c50, \
			0x94, 0xd4, 0x50, 0x26, 0x67, 0x23, 0x77, 0x5c)

#define MEI_UUID_WD UUID_LE(0x05B79A6F, 0x4628, 0x4D7F, \
			    0x89, 0x9D, 0xA9, 0x15, 0x14, 0xCB, 0x32, 0xAB)

#define MEI_UUID_MKHIF_FIX UUID_LE(0x55213584, 0x9a29, 0x4916, \
			0xba, 0xdf, 0xf, 0xb7, 0xed, 0x68, 0x2a, 0xeb)

#define MEI_UUID_ANY NULL_UUID_LE

/**
 * number_of_connections - determine whether an client be on the bus
 *    according number of connections
 *    We support only clients:
 *       1. with single connection
 *       2. and fixed clients (max_number_of_connections == 0)
 *
 * @cldev: me clients device
 */
static void number_of_connections(struct mei_cl_device *cldev)
{
	dev_dbg(&cldev->dev, "running hook %s\n", __func__);

	if (cldev->me_cl->props.max_number_of_connections > 1)
		cldev->do_match = 0;
}

/**
 * blacklist - blacklist a client from the bus
 *
 * @cldev: me clients device
 */
static void blacklist(struct mei_cl_device *cldev)
{
	dev_dbg(&cldev->dev, "running hook %s\n", __func__);

	cldev->do_match = 0;
}

#define OSTYPE_LINUX    2
struct mei_os_ver {
	__le16 build;
	__le16 reserved1;
	u8  os_type;
	u8  major;
	u8  minor;
	u8  reserved2;
} __packed;

#define MKHI_FEATURE_PTT 0x10

struct mkhi_rule_id {
	__le16 rule_type;
	u8 feature_id;
	u8 reserved;
} __packed;

struct mkhi_fwcaps {
	struct mkhi_rule_id id;
	u8 len;
	u8 data[0];
} __packed;

#define MKHI_FWCAPS_GROUP_ID 0x3
#define MKHI_FWCAPS_SET_OS_VER_APP_RULE_CMD 6
struct mkhi_msg_hdr {
	u8  group_id;
	u8  command;
	u8  reserved;
	u8  result;
} __packed;

struct mkhi_msg {
	struct mkhi_msg_hdr hdr;
	u8 data[0];
} __packed;

static int mei_osver(struct mei_cl_device *cldev)
{
	int ret;
	const size_t size = sizeof(struct mkhi_msg_hdr) +
			    sizeof(struct mkhi_fwcaps) +
			    sizeof(struct mei_os_ver);
	size_t length = 8;
	char buf[size];
	struct mkhi_msg *req;
	struct mkhi_fwcaps *fwcaps;
	struct mei_os_ver *os_ver;
	unsigned int mode = MEI_CL_IO_TX_BLOCKING | MEI_CL_IO_TX_INTERNAL;

	memset(buf, 0, size);

	req = (struct mkhi_msg *)buf;
	req->hdr.group_id = MKHI_FWCAPS_GROUP_ID;
	req->hdr.command = MKHI_FWCAPS_SET_OS_VER_APP_RULE_CMD;

	fwcaps = (struct mkhi_fwcaps *)req->data;

	fwcaps->id.rule_type = 0x0;
	fwcaps->id.feature_id = MKHI_FEATURE_PTT;
	fwcaps->len = sizeof(*os_ver);
	os_ver = (struct mei_os_ver *)fwcaps->data;
	os_ver->os_type = OSTYPE_LINUX;

	ret = __mei_cl_send(cldev->cl, buf, size, mode);
	if (ret < 0)
		return ret;

	ret = __mei_cl_recv(cldev->cl, buf, length);
	if (ret < 0)
		return ret;

	return 0;
}

static void mei_mkhi_fix(struct mei_cl_device *cldev)
{
	int ret;

	ret = mei_cldev_enable(cldev);
	if (ret)
		return;

	ret = mei_osver(cldev);
	if (ret)
		dev_err(&cldev->dev, "OS version command failed %d\n", ret);

	mei_cldev_disable(cldev);
}

/**
 * mei_wd - wd client on the bus, change protocol version
 *   as the API has changed.
 *
 * @cldev: me clients device
 */
#if IS_ENABLED(CONFIG_INTEL_MEI_ME)
#include <linux/pci.h>
#include "hw-me-regs.h"
static void mei_wd(struct mei_cl_device *cldev)
{
	struct pci_dev *pdev = to_pci_dev(cldev->dev.parent);

	dev_dbg(&cldev->dev, "running hook %s\n", __func__);
	if (pdev->device == MEI_DEV_ID_WPT_LP ||
	    pdev->device == MEI_DEV_ID_SPT ||
	    pdev->device == MEI_DEV_ID_SPT_H)
		cldev->me_cl->props.protocol_version = 0x2;

	cldev->do_match = 1;
}
#else
static inline void mei_wd(struct mei_cl_device *cldev) {}
#endif /* CONFIG_INTEL_MEI_ME */

struct mei_nfc_cmd {
	u8 command;
	u8 status;
	u16 req_id;
	u32 reserved;
	u16 data_size;
	u8 sub_command;
	u8 data[];
} __packed;

struct mei_nfc_reply {
	u8 command;
	u8 status;
	u16 req_id;
	u32 reserved;
	u16 data_size;
	u8 sub_command;
	u8 reply_status;
	u8 data[];
} __packed;

struct mei_nfc_if_version {
	u8 radio_version_sw[3];
	u8 reserved[3];
	u8 radio_version_hw[3];
	u8 i2c_addr;
	u8 fw_ivn;
	u8 vendor_id;
	u8 radio_type;
} __packed;


#define MEI_NFC_CMD_MAINTENANCE 0x00
#define MEI_NFC_SUBCMD_IF_VERSION 0x01

/* Vendors */
#define MEI_NFC_VENDOR_INSIDE 0x00
#define MEI_NFC_VENDOR_NXP    0x01

/* Radio types */
#define MEI_NFC_VENDOR_INSIDE_UREAD 0x00
#define MEI_NFC_VENDOR_NXP_PN544    0x01

/**
 * mei_nfc_if_version - get NFC interface version
 *
 * @cl: host client (nfc info)
 * @ver: NFC interface version to be filled in
 *
 * Return: 0 on success; < 0 otherwise
 */
static int mei_nfc_if_version(struct mei_cl *cl,
			      struct mei_nfc_if_version *ver)
{
	struct mei_device *bus;
	struct mei_nfc_cmd cmd = {
		.command = MEI_NFC_CMD_MAINTENANCE,
		.data_size = 1,
		.sub_command = MEI_NFC_SUBCMD_IF_VERSION,
	};
	struct mei_nfc_reply *reply = NULL;
	size_t if_version_length;
	int bytes_recv, ret;

	bus = cl->dev;

	WARN_ON(mutex_is_locked(&bus->device_lock));

	ret = __mei_cl_send(cl, (u8 *)&cmd, sizeof(struct mei_nfc_cmd),
			    MEI_CL_IO_TX_BLOCKING);
	if (ret < 0) {
		dev_err(bus->dev, "Could not send IF version cmd\n");
		return ret;
	}

	/* to be sure on the stack we alloc memory */
	if_version_length = sizeof(struct mei_nfc_reply) +
		sizeof(struct mei_nfc_if_version);

	reply = kzalloc(if_version_length, GFP_KERNEL);
	if (!reply)
		return -ENOMEM;

	ret = 0;
	bytes_recv = __mei_cl_recv(cl, (u8 *)reply, if_version_length);
	if (bytes_recv < if_version_length) {
		dev_err(bus->dev, "Could not read IF version\n");
		ret = -EIO;
		goto err;
	}

	memcpy(ver, reply->data, sizeof(struct mei_nfc_if_version));

	dev_info(bus->dev, "NFC MEI VERSION: IVN 0x%x Vendor ID 0x%x Type 0x%x\n",
		ver->fw_ivn, ver->vendor_id, ver->radio_type);

err:
	kfree(reply);
	return ret;
}

/**
 * mei_nfc_radio_name - derive nfc radio name from the interface version
 *
 * @ver: NFC radio version
 *
 * Return: radio name string
 */
static const char *mei_nfc_radio_name(struct mei_nfc_if_version *ver)
{

	if (ver->vendor_id == MEI_NFC_VENDOR_INSIDE) {
		if (ver->radio_type == MEI_NFC_VENDOR_INSIDE_UREAD)
			return "microread";
	}

	if (ver->vendor_id == MEI_NFC_VENDOR_NXP) {
		if (ver->radio_type == MEI_NFC_VENDOR_NXP_PN544)
			return "pn544";
	}

	return NULL;
}

/**
 * mei_nfc - The nfc fixup function. The function retrieves nfc radio
 *    name and set is as device attribute so we can load
 *    the proper device driver for it
 *
 * @cldev: me client device (nfc)
 */
static void mei_nfc(struct mei_cl_device *cldev)
{
	struct mei_device *bus;
	struct mei_cl *cl;
	struct mei_me_client *me_cl = NULL;
	struct mei_nfc_if_version ver;
	const char *radio_name = NULL;
	int ret;

	bus = cldev->bus;

	dev_dbg(&cldev->dev, "running hook %s\n", __func__);

	mutex_lock(&bus->device_lock);
	/* we need to connect to INFO GUID */
	cl = mei_cl_alloc_linked(bus);
	if (IS_ERR(cl)) {
		ret = PTR_ERR(cl);
		cl = NULL;
		dev_err(bus->dev, "nfc hook alloc failed %d\n", ret);
		goto out;
	}

	me_cl = mei_me_cl_by_uuid(bus, &mei_nfc_info_guid);
	if (!me_cl) {
		ret = -ENOTTY;
		dev_err(bus->dev, "Cannot find nfc info %d\n", ret);
		goto out;
	}

	ret = mei_cl_connect(cl, me_cl, NULL);
	if (ret < 0) {
		dev_err(&cldev->dev, "Can't connect to the NFC INFO ME ret = %d\n",
			ret);
		goto out;
	}

	mutex_unlock(&bus->device_lock);

	ret = mei_nfc_if_version(cl, &ver);
	if (ret)
		goto disconnect;

	radio_name = mei_nfc_radio_name(&ver);

	if (!radio_name) {
		ret = -ENOENT;
		dev_err(&cldev->dev, "Can't get the NFC interface version ret = %d\n",
			ret);
		goto disconnect;
	}

	dev_dbg(bus->dev, "nfc radio %s\n", radio_name);
	strlcpy(cldev->name, radio_name, sizeof(cldev->name));

disconnect:
	mutex_lock(&bus->device_lock);
	if (mei_cl_disconnect(cl) < 0)
		dev_err(bus->dev, "Can't disconnect the NFC INFO ME\n");

	mei_cl_flush_queues(cl, NULL);

out:
	mei_cl_unlink(cl);
	mutex_unlock(&bus->device_lock);
	mei_me_cl_put(me_cl);
	kfree(cl);

	if (ret)
		cldev->do_match = 0;

	dev_dbg(bus->dev, "end of fixup match = %d\n", cldev->do_match);
}

#define MEI_FIXUP(_uuid, _hook) { _uuid, _hook }

static struct mei_fixup {

	const uuid_le uuid;
	void (*hook)(struct mei_cl_device *cldev);
} mei_fixups[] = {
	MEI_FIXUP(MEI_UUID_ANY, number_of_connections),
	MEI_FIXUP(MEI_UUID_NFC_INFO, blacklist),
	MEI_FIXUP(MEI_UUID_NFC_HCI, mei_nfc),
	MEI_FIXUP(MEI_UUID_WD, mei_wd),
	MEI_FIXUP(MEI_UUID_MKHIF_FIX, mei_mkhi_fix),
};

/**
 * mei_cldev_fixup - run fixup handlers
 *
 * @cldev: me client device
 */
void mei_cl_bus_dev_fixup(struct mei_cl_device *cldev)
{
	struct mei_fixup *f;
	const uuid_le *uuid = mei_me_cl_uuid(cldev->me_cl);
	int i;

	for (i = 0; i < ARRAY_SIZE(mei_fixups); i++) {

		f = &mei_fixups[i];
		if (uuid_le_cmp(f->uuid, MEI_UUID_ANY) == 0 ||
		    uuid_le_cmp(f->uuid, *uuid) == 0)
			f->hook(cldev);
	}
}

