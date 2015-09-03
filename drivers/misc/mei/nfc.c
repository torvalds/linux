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

#include <linux/mei_cl_bus.h>

#include "mei_dev.h"
#include "client.h"

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

struct mei_nfc_connect {
	u8 fw_ivn;
	u8 vendor_id;
} __packed;

struct mei_nfc_connect_resp {
	u8 fw_ivn;
	u8 vendor_id;
	u16 me_major;
	u16 me_minor;
	u16 me_hotfix;
	u16 me_build;
} __packed;

struct mei_nfc_hci_hdr {
	u8 cmd;
	u8 status;
	u16 req_id;
	u32 reserved;
	u16 data_size;
} __packed;

#define MEI_NFC_CMD_MAINTENANCE 0x00
#define MEI_NFC_CMD_HCI_SEND 0x01
#define MEI_NFC_CMD_HCI_RECV 0x02

#define MEI_NFC_SUBCMD_CONNECT    0x00
#define MEI_NFC_SUBCMD_IF_VERSION 0x01

#define MEI_NFC_HEADER_SIZE 10

/**
 * struct mei_nfc_dev - NFC mei device
 *
 * @me_cl: NFC me client
 * @cl: NFC host client
 * @cl_info: NFC info host client
 * @init_work: perform connection to the info client
 * @fw_ivn: NFC Interface Version Number
 * @vendor_id: NFC manufacturer ID
 * @radio_type: NFC radio type
 * @bus_name: bus name
 *
 */
struct mei_nfc_dev {
	struct mei_me_client *me_cl;
	struct mei_cl *cl;
	struct mei_cl *cl_info;
	struct work_struct init_work;
	u8 fw_ivn;
	u8 vendor_id;
	u8 radio_type;
	char *bus_name;
};

/* UUIDs for NFC F/W clients */
const uuid_le mei_nfc_guid = UUID_LE(0x0bb17a78, 0x2a8e, 0x4c50,
				     0x94, 0xd4, 0x50, 0x26,
				     0x67, 0x23, 0x77, 0x5c);

static const uuid_le mei_nfc_info_guid = UUID_LE(0xd2de1625, 0x382d, 0x417d,
					0x48, 0xa4, 0xef, 0xab,
					0xba, 0x8a, 0x12, 0x06);

/* Vendors */
#define MEI_NFC_VENDOR_INSIDE 0x00
#define MEI_NFC_VENDOR_NXP    0x01

/* Radio types */
#define MEI_NFC_VENDOR_INSIDE_UREAD 0x00
#define MEI_NFC_VENDOR_NXP_PN544    0x01

static void mei_nfc_free(struct mei_nfc_dev *ndev)
{
	if (!ndev)
		return;

	if (ndev->cl) {
		list_del(&ndev->cl->device_link);
		mei_cl_unlink(ndev->cl);
		kfree(ndev->cl);
	}

	if (ndev->cl_info) {
		list_del(&ndev->cl_info->device_link);
		mei_cl_unlink(ndev->cl_info);
		kfree(ndev->cl_info);
	}

	mei_me_cl_put(ndev->me_cl);
	kfree(ndev);
}

static int mei_nfc_build_bus_name(struct mei_nfc_dev *ndev)
{
	struct mei_device *dev;

	if (!ndev->cl)
		return -ENODEV;

	dev = ndev->cl->dev;

	switch (ndev->vendor_id) {
	case MEI_NFC_VENDOR_INSIDE:
		switch (ndev->radio_type) {
		case MEI_NFC_VENDOR_INSIDE_UREAD:
			ndev->bus_name = "microread";
			return 0;

		default:
			dev_err(dev->dev, "Unknown radio type 0x%x\n",
				ndev->radio_type);

			return -EINVAL;
		}

	case MEI_NFC_VENDOR_NXP:
		switch (ndev->radio_type) {
		case MEI_NFC_VENDOR_NXP_PN544:
			ndev->bus_name = "pn544";
			return 0;
		default:
			dev_err(dev->dev, "Unknown radio type 0x%x\n",
				ndev->radio_type);

			return -EINVAL;
		}

	default:
		dev_err(dev->dev, "Unknown vendor ID 0x%x\n",
			ndev->vendor_id);

		return -EINVAL;
	}

	return 0;
}

static int mei_nfc_if_version(struct mei_nfc_dev *ndev)
{
	struct mei_device *dev;
	struct mei_cl *cl;

	struct mei_nfc_cmd cmd;
	struct mei_nfc_reply *reply = NULL;
	struct mei_nfc_if_version *version;
	size_t if_version_length;
	int bytes_recv, ret;

	cl = ndev->cl_info;
	dev = cl->dev;

	memset(&cmd, 0, sizeof(struct mei_nfc_cmd));
	cmd.command = MEI_NFC_CMD_MAINTENANCE;
	cmd.data_size = 1;
	cmd.sub_command = MEI_NFC_SUBCMD_IF_VERSION;

	ret = __mei_cl_send(cl, (u8 *)&cmd, sizeof(struct mei_nfc_cmd), 1);
	if (ret < 0) {
		dev_err(dev->dev, "Could not send IF version cmd\n");
		return ret;
	}

	/* to be sure on the stack we alloc memory */
	if_version_length = sizeof(struct mei_nfc_reply) +
		sizeof(struct mei_nfc_if_version);

	reply = kzalloc(if_version_length, GFP_KERNEL);
	if (!reply)
		return -ENOMEM;

	bytes_recv = __mei_cl_recv(cl, (u8 *)reply, if_version_length);
	if (bytes_recv < 0 || bytes_recv < sizeof(struct mei_nfc_reply)) {
		dev_err(dev->dev, "Could not read IF version\n");
		ret = -EIO;
		goto err;
	}

	version = (struct mei_nfc_if_version *)reply->data;

	ndev->fw_ivn = version->fw_ivn;
	ndev->vendor_id = version->vendor_id;
	ndev->radio_type = version->radio_type;

err:
	kfree(reply);
	return ret;
}

static void mei_nfc_init(struct work_struct *work)
{
	struct mei_device *dev;
	struct mei_cl_device *cldev;
	struct mei_nfc_dev *ndev;
	struct mei_cl *cl_info;
	struct mei_me_client *me_cl_info;

	ndev = container_of(work, struct mei_nfc_dev, init_work);

	cl_info = ndev->cl_info;
	dev = cl_info->dev;

	mutex_lock(&dev->device_lock);

	/* check for valid client id */
	me_cl_info = mei_me_cl_by_uuid(dev, &mei_nfc_info_guid);
	if (!me_cl_info) {
		mutex_unlock(&dev->device_lock);
		dev_info(dev->dev, "nfc: failed to find the info client\n");
		goto err;
	}

	if (mei_cl_connect(cl_info, me_cl_info, NULL) < 0) {
		mei_me_cl_put(me_cl_info);
		mutex_unlock(&dev->device_lock);
		dev_err(dev->dev, "Could not connect to the NFC INFO ME client");

		goto err;
	}
	mei_me_cl_put(me_cl_info);
	mutex_unlock(&dev->device_lock);

	if (mei_nfc_if_version(ndev) < 0) {
		dev_err(dev->dev, "Could not get the NFC interface version");

		goto err;
	}

	dev_info(dev->dev, "NFC MEI VERSION: IVN 0x%x Vendor ID 0x%x Type 0x%x\n",
		ndev->fw_ivn, ndev->vendor_id, ndev->radio_type);

	mutex_lock(&dev->device_lock);

	if (mei_cl_disconnect(cl_info) < 0) {
		mutex_unlock(&dev->device_lock);
		dev_err(dev->dev, "Could not disconnect the NFC INFO ME client");

		goto err;
	}

	mutex_unlock(&dev->device_lock);

	if (mei_nfc_build_bus_name(ndev) < 0) {
		dev_err(dev->dev, "Could not build the bus ID name\n");
		return;
	}

	cldev = mei_cl_add_device(dev, ndev->me_cl, ndev->cl,
				  ndev->bus_name);
	if (!cldev) {
		dev_err(dev->dev, "Could not add the NFC device to the MEI bus\n");

		goto err;
	}

	cldev->priv_data = ndev;


	return;

err:
	mutex_lock(&dev->device_lock);
	mei_nfc_free(ndev);
	mutex_unlock(&dev->device_lock);

}


int mei_nfc_host_init(struct mei_device *dev, struct mei_me_client *me_cl)
{
	struct mei_nfc_dev *ndev;
	struct mei_cl *cl_info, *cl;
	int ret;


	/* in case of internal reset bail out
	 * as the device is already setup
	 */
	cl = mei_cl_bus_find_cl_by_uuid(dev, mei_nfc_guid);
	if (cl)
		return 0;

	ndev = kzalloc(sizeof(struct mei_nfc_dev), GFP_KERNEL);
	if (!ndev) {
		ret = -ENOMEM;
		goto err;
	}

	ndev->me_cl = mei_me_cl_get(me_cl);
	if (!ndev->me_cl) {
		ret = -ENODEV;
		goto err;
	}

	cl_info = mei_cl_alloc_linked(dev, MEI_HOST_CLIENT_ID_ANY);
	if (IS_ERR(cl_info)) {
		ret = PTR_ERR(cl_info);
		goto err;
	}

	list_add_tail(&cl_info->device_link, &dev->device_list);

	ndev->cl_info = cl_info;

	cl = mei_cl_alloc_linked(dev, MEI_HOST_CLIENT_ID_ANY);
	if (IS_ERR(cl)) {
		ret = PTR_ERR(cl);
		goto err;
	}

	list_add_tail(&cl->device_link, &dev->device_list);

	ndev->cl = cl;

	INIT_WORK(&ndev->init_work, mei_nfc_init);
	schedule_work(&ndev->init_work);

	return 0;

err:
	mei_nfc_free(ndev);

	return ret;
}

void mei_nfc_host_exit(struct mei_device *dev)
{
	struct mei_nfc_dev *ndev;
	struct mei_cl *cl;
	struct mei_cl_device *cldev;

	cl = mei_cl_bus_find_cl_by_uuid(dev, mei_nfc_guid);
	if (!cl)
		return;

	cldev = cl->device;
	if (!cldev)
		return;

	ndev = (struct mei_nfc_dev *)cldev->priv_data;
	if (ndev)
		cancel_work_sync(&ndev->init_work);

	cldev->priv_data = NULL;

	/* Need to remove the device here
	 * since mei_nfc_free will unlink the clients
	 */
	mei_cl_remove_device(cldev);

	mutex_lock(&dev->device_lock);
	mei_nfc_free(ndev);
	mutex_unlock(&dev->device_lock);
}


