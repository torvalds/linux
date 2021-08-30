// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2013-2020, Intel Corporation. All rights reserved.
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/module.h>
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

#define MEI_UUID_HDCP UUID_LE(0xB638AB7E, 0x94E2, 0x4EA2, \
			      0xA5, 0x52, 0xD1, 0xC5, 0x4B, 0x62, 0x7F, 0x04)

#define MEI_UUID_PAVP UUID_LE(0xfbf6fcf1, 0x96cf, 0x4e2e, 0xA6, \
			      0xa6, 0x1b, 0xab, 0x8c, 0xbe, 0x36, 0xb1)

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
	cldev->do_match = 0;
}

/**
 * whitelist - forcefully whitelist client
 *
 * @cldev: me clients device
 */
static void whitelist(struct mei_cl_device *cldev)
{
	cldev->do_match = 1;
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
	u8 data[];
} __packed;

struct mkhi_fw_ver_block {
	u16 minor;
	u8 major;
	u8 platform;
	u16 buildno;
	u16 hotfix;
} __packed;

struct mkhi_fw_ver {
	struct mkhi_fw_ver_block ver[MEI_MAX_FW_VER_BLOCKS];
} __packed;

#define MKHI_FWCAPS_GROUP_ID 0x3
#define MKHI_FWCAPS_SET_OS_VER_APP_RULE_CMD 6
#define MKHI_GEN_GROUP_ID 0xFF
#define MKHI_GEN_GET_FW_VERSION_CMD 0x2
struct mkhi_msg_hdr {
	u8  group_id;
	u8  command;
	u8  reserved;
	u8  result;
} __packed;

struct mkhi_msg {
	struct mkhi_msg_hdr hdr;
	u8 data[];
} __packed;

#define MKHI_OSVER_BUF_LEN (sizeof(struct mkhi_msg_hdr) + \
			    sizeof(struct mkhi_fwcaps) + \
			    sizeof(struct mei_os_ver))
static int mei_osver(struct mei_cl_device *cldev)
{
	const size_t size = MKHI_OSVER_BUF_LEN;
	char buf[MKHI_OSVER_BUF_LEN];
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

	return __mei_cl_send(cldev->cl, buf, size, 0, mode);
}

#define MKHI_FWVER_BUF_LEN (sizeof(struct mkhi_msg_hdr) + \
			    sizeof(struct mkhi_fw_ver))
#define MKHI_FWVER_LEN(__num) (sizeof(struct mkhi_msg_hdr) + \
			       sizeof(struct mkhi_fw_ver_block) * (__num))
#define MKHI_RCV_TIMEOUT 500 /* receive timeout in msec */
static int mei_fwver(struct mei_cl_device *cldev)
{
	char buf[MKHI_FWVER_BUF_LEN];
	struct mkhi_msg req;
	struct mkhi_msg *rsp;
	struct mkhi_fw_ver *fwver;
	int bytes_recv, ret, i;

	memset(buf, 0, sizeof(buf));

	req.hdr.group_id = MKHI_GEN_GROUP_ID;
	req.hdr.command = MKHI_GEN_GET_FW_VERSION_CMD;

	ret = __mei_cl_send(cldev->cl, (u8 *)&req, sizeof(req), 0,
			    MEI_CL_IO_TX_BLOCKING);
	if (ret < 0) {
		dev_err(&cldev->dev, "Could not send ReqFWVersion cmd\n");
		return ret;
	}

	ret = 0;
	bytes_recv = __mei_cl_recv(cldev->cl, buf, sizeof(buf), NULL, 0,
				   MKHI_RCV_TIMEOUT);
	if (bytes_recv < 0 || (size_t)bytes_recv < MKHI_FWVER_LEN(1)) {
		/*
		 * Should be at least one version block,
		 * error out if nothing found
		 */
		dev_err(&cldev->dev, "Could not read FW version\n");
		return -EIO;
	}

	rsp = (struct mkhi_msg *)buf;
	fwver = (struct mkhi_fw_ver *)rsp->data;
	memset(cldev->bus->fw_ver, 0, sizeof(cldev->bus->fw_ver));
	for (i = 0; i < MEI_MAX_FW_VER_BLOCKS; i++) {
		if ((size_t)bytes_recv < MKHI_FWVER_LEN(i + 1))
			break;
		dev_dbg(&cldev->dev, "FW version%d %d:%d.%d.%d.%d\n",
			i, fwver->ver[i].platform,
			fwver->ver[i].major, fwver->ver[i].minor,
			fwver->ver[i].hotfix, fwver->ver[i].buildno);

		cldev->bus->fw_ver[i].platform = fwver->ver[i].platform;
		cldev->bus->fw_ver[i].major = fwver->ver[i].major;
		cldev->bus->fw_ver[i].minor = fwver->ver[i].minor;
		cldev->bus->fw_ver[i].hotfix = fwver->ver[i].hotfix;
		cldev->bus->fw_ver[i].buildno = fwver->ver[i].buildno;
	}

	return ret;
}

static void mei_mkhi_fix(struct mei_cl_device *cldev)
{
	int ret;

	/* No need to enable the client if nothing is needed from it */
	if (!cldev->bus->fw_f_fw_ver_supported &&
	    !cldev->bus->hbm_f_os_supported)
		return;

	ret = mei_cldev_enable(cldev);
	if (ret)
		return;

	if (cldev->bus->fw_f_fw_ver_supported) {
		ret = mei_fwver(cldev);
		if (ret < 0)
			dev_err(&cldev->dev, "FW version command failed %d\n",
				ret);
	}

	if (cldev->bus->hbm_f_os_supported) {
		ret = mei_osver(cldev);
		if (ret < 0)
			dev_err(&cldev->dev, "OS version command failed %d\n",
				ret);
	}
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
	u8 vtag;
	int bytes_recv, ret;

	bus = cl->dev;

	WARN_ON(mutex_is_locked(&bus->device_lock));

	ret = __mei_cl_send(cl, (u8 *)&cmd, sizeof(cmd), 0,
			    MEI_CL_IO_TX_BLOCKING);
	if (ret < 0) {
		dev_err(bus->dev, "Could not send IF version cmd\n");
		return ret;
	}

	/* to be sure on the stack we alloc memory */
	if_version_length = sizeof(*reply) + sizeof(*ver);

	reply = kzalloc(if_version_length, GFP_KERNEL);
	if (!reply)
		return -ENOMEM;

	ret = 0;
	bytes_recv = __mei_cl_recv(cl, (u8 *)reply, if_version_length, &vtag,
				   0, 0);
	if (bytes_recv < 0 || (size_t)bytes_recv < if_version_length) {
		dev_err(bus->dev, "Could not read IF version\n");
		ret = -EIO;
		goto err;
	}

	memcpy(ver, reply->data, sizeof(*ver));

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

/**
 * vt_support - enable on bus clients with vtag support
 *
 * @cldev: me clients device
 */
static void vt_support(struct mei_cl_device *cldev)
{
	if (cldev->me_cl->props.vt_supported == 1)
		cldev->do_match = 1;
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
	MEI_FIXUP(MEI_UUID_HDCP, whitelist),
	MEI_FIXUP(MEI_UUID_ANY, vt_support),
	MEI_FIXUP(MEI_UUID_PAVP, whitelist),
};

/**
 * mei_cl_bus_dev_fixup - run fixup handlers
 *
 * @cldev: me client device
 */
void mei_cl_bus_dev_fixup(struct mei_cl_device *cldev)
{
	struct mei_fixup *f;
	const uuid_le *uuid = mei_me_cl_uuid(cldev->me_cl);
	size_t i;

	for (i = 0; i < ARRAY_SIZE(mei_fixups); i++) {

		f = &mei_fixups[i];
		if (uuid_le_cmp(f->uuid, MEI_UUID_ANY) == 0 ||
		    uuid_le_cmp(f->uuid, *uuid) == 0)
			f->hook(cldev);
	}
}

