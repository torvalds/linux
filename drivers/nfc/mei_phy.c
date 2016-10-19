/*
 * MEI Library for mei bus nfc device access
 *
 * Copyright (C) 2013  Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/nfc.h>

#include "mei_phy.h"

struct mei_nfc_hdr {
	u8 cmd;
	u8 status;
	u16 req_id;
	u32 reserved;
	u16 data_size;
} __packed;

struct mei_nfc_cmd {
	struct mei_nfc_hdr hdr;
	u8 sub_command;
	u8 data[];
} __packed;

struct mei_nfc_reply {
	struct mei_nfc_hdr hdr;
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


#define MEI_NFC_CMD_MAINTENANCE 0x00
#define MEI_NFC_CMD_HCI_SEND 0x01
#define MEI_NFC_CMD_HCI_RECV 0x02

#define MEI_NFC_SUBCMD_CONNECT    0x00
#define MEI_NFC_SUBCMD_IF_VERSION 0x01

#define MEI_NFC_MAX_READ (MEI_NFC_HEADER_SIZE + MEI_NFC_MAX_HCI_PAYLOAD)

#define MEI_DUMP_SKB_IN(info, skb)				\
do {								\
	pr_debug("%s:\n", info);				\
	print_hex_dump_debug("mei in : ", DUMP_PREFIX_OFFSET,	\
			16, 1, (skb)->data, (skb)->len, false);	\
} while (0)

#define MEI_DUMP_SKB_OUT(info, skb)				\
do {								\
	pr_debug("%s:\n", info);				\
	print_hex_dump_debug("mei out: ", DUMP_PREFIX_OFFSET,	\
			16, 1, (skb)->data, (skb)->len, false);	\
} while (0)

#define MEI_DUMP_NFC_HDR(info, _hdr)                                \
do {                                                                \
	pr_debug("%s:\n", info);                                    \
	pr_debug("cmd=%02d status=%d req_id=%d rsvd=%d size=%d\n",  \
		 (_hdr)->cmd, (_hdr)->status, (_hdr)->req_id,       \
		 (_hdr)->reserved, (_hdr)->data_size);              \
} while (0)

static int mei_nfc_if_version(struct nfc_mei_phy *phy)
{

	struct mei_nfc_cmd cmd;
	struct mei_nfc_reply *reply = NULL;
	struct mei_nfc_if_version *version;
	size_t if_version_length;
	int bytes_recv, r;

	pr_info("%s\n", __func__);

	memset(&cmd, 0, sizeof(struct mei_nfc_cmd));
	cmd.hdr.cmd = MEI_NFC_CMD_MAINTENANCE;
	cmd.hdr.data_size = 1;
	cmd.sub_command = MEI_NFC_SUBCMD_IF_VERSION;

	MEI_DUMP_NFC_HDR("version", &cmd.hdr);
	r = mei_cldev_send(phy->cldev, (u8 *)&cmd, sizeof(struct mei_nfc_cmd));
	if (r < 0) {
		pr_err("Could not send IF version cmd\n");
		return r;
	}

	/* to be sure on the stack we alloc memory */
	if_version_length = sizeof(struct mei_nfc_reply) +
		sizeof(struct mei_nfc_if_version);

	reply = kzalloc(if_version_length, GFP_KERNEL);
	if (!reply)
		return -ENOMEM;

	bytes_recv = mei_cldev_recv(phy->cldev, (u8 *)reply, if_version_length);
	if (bytes_recv < 0 || bytes_recv < sizeof(struct mei_nfc_reply)) {
		pr_err("Could not read IF version\n");
		r = -EIO;
		goto err;
	}

	version = (struct mei_nfc_if_version *)reply->data;

	phy->fw_ivn = version->fw_ivn;
	phy->vendor_id = version->vendor_id;
	phy->radio_type = version->radio_type;

err:
	kfree(reply);
	return r;
}

static int mei_nfc_connect(struct nfc_mei_phy *phy)
{
	struct mei_nfc_cmd *cmd, *reply;
	struct mei_nfc_connect *connect;
	struct mei_nfc_connect_resp *connect_resp;
	size_t connect_length, connect_resp_length;
	int bytes_recv, r;

	pr_info("%s\n", __func__);

	connect_length = sizeof(struct mei_nfc_cmd) +
			sizeof(struct mei_nfc_connect);

	connect_resp_length = sizeof(struct mei_nfc_cmd) +
			sizeof(struct mei_nfc_connect_resp);

	cmd = kzalloc(connect_length, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	connect = (struct mei_nfc_connect *)cmd->data;

	reply = kzalloc(connect_resp_length, GFP_KERNEL);
	if (!reply) {
		kfree(cmd);
		return -ENOMEM;
	}

	connect_resp = (struct mei_nfc_connect_resp *)reply->data;

	cmd->hdr.cmd = MEI_NFC_CMD_MAINTENANCE;
	cmd->hdr.data_size = 3;
	cmd->sub_command = MEI_NFC_SUBCMD_CONNECT;
	connect->fw_ivn = phy->fw_ivn;
	connect->vendor_id = phy->vendor_id;

	MEI_DUMP_NFC_HDR("connect request", &cmd->hdr);
	r = mei_cldev_send(phy->cldev, (u8 *)cmd, connect_length);
	if (r < 0) {
		pr_err("Could not send connect cmd %d\n", r);
		goto err;
	}

	bytes_recv = mei_cldev_recv(phy->cldev, (u8 *)reply,
				    connect_resp_length);
	if (bytes_recv < 0) {
		r = bytes_recv;
		pr_err("Could not read connect response %d\n", r);
		goto err;
	}

	MEI_DUMP_NFC_HDR("connect reply", &reply->hdr);

	pr_info("IVN 0x%x Vendor ID 0x%x\n",
		 connect_resp->fw_ivn, connect_resp->vendor_id);

	pr_info("ME FW %d.%d.%d.%d\n",
		connect_resp->me_major, connect_resp->me_minor,
		connect_resp->me_hotfix, connect_resp->me_build);

	r = 0;

err:
	kfree(reply);
	kfree(cmd);

	return r;
}

static int mei_nfc_send(struct nfc_mei_phy *phy, u8 *buf, size_t length)
{
	struct mei_nfc_hdr *hdr;
	u8 *mei_buf;
	int err;

	err = -ENOMEM;
	mei_buf = kzalloc(length + MEI_NFC_HEADER_SIZE, GFP_KERNEL);
	if (!mei_buf)
		goto out;

	hdr = (struct mei_nfc_hdr *)mei_buf;
	hdr->cmd = MEI_NFC_CMD_HCI_SEND;
	hdr->status = 0;
	hdr->req_id = phy->req_id;
	hdr->reserved = 0;
	hdr->data_size = length;

	MEI_DUMP_NFC_HDR("send", hdr);

	memcpy(mei_buf + MEI_NFC_HEADER_SIZE, buf, length);
	err = mei_cldev_send(phy->cldev, mei_buf, length + MEI_NFC_HEADER_SIZE);
	if (err < 0)
		goto out;

	if (!wait_event_interruptible_timeout(phy->send_wq,
				phy->recv_req_id == phy->req_id, HZ)) {
		pr_err("NFC MEI command timeout\n");
		err = -ETIME;
	} else {
		phy->req_id++;
	}
out:
	kfree(mei_buf);
	return err;
}

/*
 * Writing a frame must not return the number of written bytes.
 * It must return either zero for success, or <0 for error.
 * In addition, it must not alter the skb
 */
static int nfc_mei_phy_write(void *phy_id, struct sk_buff *skb)
{
	struct nfc_mei_phy *phy = phy_id;
	int r;

	MEI_DUMP_SKB_OUT("mei frame sent", skb);

	r = mei_nfc_send(phy, skb->data, skb->len);
	if (r > 0)
		r = 0;

	return r;
}

static int mei_nfc_recv(struct nfc_mei_phy *phy, u8 *buf, size_t length)
{
	struct mei_nfc_hdr *hdr;
	int received_length;

	received_length = mei_cldev_recv(phy->cldev, buf, length);
	if (received_length < 0)
		return received_length;

	hdr = (struct mei_nfc_hdr *) buf;

	MEI_DUMP_NFC_HDR("receive", hdr);
	if (hdr->cmd == MEI_NFC_CMD_HCI_SEND) {
		phy->recv_req_id = hdr->req_id;
		wake_up(&phy->send_wq);

		return 0;
	}

	return received_length;
}


static void nfc_mei_event_cb(struct mei_cl_device *cldev, u32 events,
			     void *context)
{
	struct nfc_mei_phy *phy = mei_cldev_get_drvdata(cldev);

	if (!phy)
		return;

	if (phy->hard_fault != 0)
		return;

	if (events & BIT(MEI_CL_EVENT_RX)) {
		struct sk_buff *skb;
		int reply_size;

		skb = alloc_skb(MEI_NFC_MAX_READ, GFP_KERNEL);
		if (!skb)
			return;

		reply_size = mei_nfc_recv(phy, skb->data, MEI_NFC_MAX_READ);
		if (reply_size < MEI_NFC_HEADER_SIZE) {
			kfree_skb(skb);
			return;
		}

		skb_put(skb, reply_size);
		skb_pull(skb, MEI_NFC_HEADER_SIZE);

		MEI_DUMP_SKB_IN("mei frame read", skb);

		nfc_hci_recv_frame(phy->hdev, skb);
	}
}

static int nfc_mei_phy_enable(void *phy_id)
{
	int r;
	struct nfc_mei_phy *phy = phy_id;

	pr_info("%s\n", __func__);

	if (phy->powered == 1)
		return 0;

	r = mei_cldev_enable(phy->cldev);
	if (r < 0) {
		pr_err("Could not enable device %d\n", r);
		return r;
	}

	r = mei_nfc_if_version(phy);
	if (r < 0) {
		pr_err("Could not enable device %d\n", r);
		goto err;
	}

	r = mei_nfc_connect(phy);
	if (r < 0) {
		pr_err("Could not connect to device %d\n", r);
		goto err;
	}

	r = mei_cldev_register_event_cb(phy->cldev, BIT(MEI_CL_EVENT_RX),
				     nfc_mei_event_cb, phy);
	if (r) {
		pr_err("Event cb registration failed %d\n", r);
		goto err;
	}

	phy->powered = 1;

	return 0;

err:
	phy->powered = 0;
	mei_cldev_disable(phy->cldev);
	return r;
}

static void nfc_mei_phy_disable(void *phy_id)
{
	struct nfc_mei_phy *phy = phy_id;

	pr_info("%s\n", __func__);

	mei_cldev_disable(phy->cldev);

	phy->powered = 0;
}

struct nfc_phy_ops mei_phy_ops = {
	.write = nfc_mei_phy_write,
	.enable = nfc_mei_phy_enable,
	.disable = nfc_mei_phy_disable,
};
EXPORT_SYMBOL_GPL(mei_phy_ops);

struct nfc_mei_phy *nfc_mei_phy_alloc(struct mei_cl_device *cldev)
{
	struct nfc_mei_phy *phy;

	phy = kzalloc(sizeof(struct nfc_mei_phy), GFP_KERNEL);
	if (!phy)
		return NULL;

	phy->cldev = cldev;
	init_waitqueue_head(&phy->send_wq);
	mei_cldev_set_drvdata(cldev, phy);

	return phy;
}
EXPORT_SYMBOL_GPL(nfc_mei_phy_alloc);

void nfc_mei_phy_free(struct nfc_mei_phy *phy)
{
	mei_cldev_disable(phy->cldev);
	kfree(phy);
}
EXPORT_SYMBOL_GPL(nfc_mei_phy_free);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("mei bus NFC device interface");
