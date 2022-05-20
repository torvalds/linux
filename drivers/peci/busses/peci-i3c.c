// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Intel Corporation.*/

#include <linux/err.h>
#include <linux/peci.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/slab.h>

#include <linux/i3c/device.h>

#include <linux/i3c/mctp/i3c-mctp.h>

#define MSG_TAG_MASK			GENMASK(2, 0)
#define MCTP_SET_MSG_TAG(x, val)	((x)->flags_seq_tag |= ((val) & MSG_TAG_MASK))
#define MCTP_GET_MSG_TAG(x)		((x)->flags_seq_tag & MSG_TAG_MASK)
#define MCTP_HDR_VERSION		1
#define REQUEST_FLAGS			0xc8
#define RESPONSE_FLAGS			0xc0
#define PECI_REQUEST			0x80
#define PECI_RESPONSE			0
#define PECI_PAYLOAD_SIZE		59

#define I3C_PECI_MCTP_TIMEOUT_VALUE_MS	800

struct mctp_peci_vdm_hdr {
	u8 type;
	__be16 vendor_id;
	u8 instance_req_d;
	u8 vendor_code;
} __packed;

static const struct mctp_protocol_hdr mctp_protocol_hdr_template = {
	.ver = MCTP_HDR_VERSION,
	.flags_seq_tag = REQUEST_FLAGS
};

static const struct mctp_peci_vdm_hdr mctp_peci_vdm_hdr_template = {
	.type = MCTP_MSG_TYPE_VDM_PCI,
	.instance_req_d = PECI_REQUEST,
	.vendor_code = MCTP_VDM_PCI_INTEL_PECI
};

struct i3c_peci {
	struct peci_adapter *adapter;
	struct device *dev;
	struct i3c_device *i3cdev;
	struct i3c_mctp_client *client;
	u8 tag;
};

static void
prepare_tx_packet(struct i3c_mctp_packet *tx_packet,
		  u8 tx_len, u8 rx_len, u8 *tx_buf, u8 dest, u8 tag)
{
	struct mctp_protocol_hdr *mctp_protocol_hdr;
	struct mctp_peci_vdm_hdr *mctp_peci_vdm_hdr;
	u8 *peci_payload;

	mctp_protocol_hdr = (struct mctp_protocol_hdr *)&tx_packet->data.protocol_hdr;
	*mctp_protocol_hdr = mctp_protocol_hdr_template;
	mctp_protocol_hdr->dest = dest;
	MCTP_SET_MSG_TAG(mctp_protocol_hdr, tag);

	mctp_peci_vdm_hdr = (struct mctp_peci_vdm_hdr *)&tx_packet->data.payload;
	*mctp_peci_vdm_hdr = mctp_peci_vdm_hdr_template;
	mctp_peci_vdm_hdr->vendor_id = cpu_to_be16(MCTP_VDM_PCI_INTEL_VENDOR_ID);

	peci_payload = (u8 *)(tx_packet->data.payload) + sizeof(struct mctp_peci_vdm_hdr);

	peci_payload[0] = tx_len;
	peci_payload[1] = rx_len;
	memcpy(&peci_payload[2], tx_buf, tx_len);

	tx_packet->size = I3C_MCTP_PACKET_SIZE;
}

static int
verify_rx_packet(struct peci_adapter *adapter, struct i3c_mctp_packet *rx_packet, u8 tag)
{
	struct i3c_peci *priv = peci_get_adapdata(adapter);
	bool invalid_packet = false;
	struct mctp_protocol_hdr *mctp_protocol_hdr;
	struct mctp_peci_vdm_hdr *mctp_message_hdr;
	u8 expected_flags;

	expected_flags = (RESPONSE_FLAGS | (tag & MSG_TAG_MASK));

	mctp_protocol_hdr = (struct mctp_protocol_hdr *)&rx_packet->data.protocol_hdr;
	mctp_message_hdr = (struct mctp_peci_vdm_hdr *)&rx_packet->data.payload;

	if (mctp_protocol_hdr->flags_seq_tag != expected_flags) {
		dev_dbg(priv->dev,
			"mismatch in mctp flags: expected: 0x%.2x, got: 0x%.2x",
			expected_flags, mctp_protocol_hdr->flags_seq_tag);
		invalid_packet = true;
	}

	if (mctp_message_hdr->instance_req_d != PECI_RESPONSE) {
		dev_dbg(priv->dev,
			"mismatch in PECI response code: expected: 0x%.2x, got: 0x%.2x",
			PECI_RESPONSE, mctp_message_hdr->instance_req_d);
		invalid_packet = true;
	}

	if (invalid_packet) {
		dev_warn_ratelimited(priv->dev, "unexpected peci response found\n");
		return -EIO;
	}

	return 0;
}

static struct i3c_mctp_packet *i3c_peci_send_receive(struct peci_adapter *adapter,
						     struct i3c_device *i3cdev,
						     u8 tx_len, u8 rx_len, u8 *tx_buf, u8 dest_eid)
{
	unsigned long timeout = msecs_to_jiffies(I3C_PECI_MCTP_TIMEOUT_VALUE_MS);
	struct i3c_peci *priv = peci_get_adapdata(adapter);
	struct i3c_mctp_packet *tx_packet;
	struct i3c_mctp_packet *rx_packet;
	u8 tag = priv->tag;
	int ret;

	tx_packet = i3c_mctp_packet_alloc(GFP_KERNEL);
	if (!tx_packet)
		return ERR_PTR(-ENOMEM);

	prepare_tx_packet(tx_packet, tx_len, rx_len, tx_buf, dest_eid, tag);

	print_hex_dump_bytes("TX : ", DUMP_PREFIX_NONE, &tx_packet->data, tx_packet->size);

	ret = i3c_mctp_send_packet(i3cdev, tx_packet);
	if (ret) {
		i3c_mctp_packet_free(tx_packet);
		return ERR_PTR(ret);
	}

	i3c_mctp_packet_free(tx_packet);
	priv->tag++;
	rx_packet = i3c_mctp_receive_packet(priv->client, timeout);
	if (IS_ERR(rx_packet))
		return rx_packet;

	ret = verify_rx_packet(adapter, rx_packet, tag);
	if (ret) {
		i3c_mctp_packet_free(rx_packet);
		return ERR_PTR(ret);
	}

	print_hex_dump_bytes("RX : ", DUMP_PREFIX_NONE, &rx_packet->data, rx_packet->size);

	return rx_packet;
}

static int
i3c_peci_xfer(struct peci_adapter *adapter, struct peci_xfer_msg *msg)
{
	struct i3c_peci *priv = peci_get_adapdata(adapter);
	struct i3c_mctp_packet *rx_packet;
	u8 domain_id = 0;
	u8 dest_eid;
	int ret;

	if (msg->tx_len > PECI_PAYLOAD_SIZE || msg->rx_len > PECI_PAYLOAD_SIZE)
		return -EINVAL;

	if (msg->tx_len > 2)
		domain_id = msg->tx_buf[1] >> 1;

	ret = i3c_mctp_get_eid(priv->client, domain_id, &dest_eid);
	if (ret)
		return -ENODEV;

	rx_packet = i3c_peci_send_receive(adapter, priv->i3cdev,
					  msg->tx_len, msg->rx_len, msg->tx_buf, dest_eid);
	if (IS_ERR(rx_packet))
		return PTR_ERR(rx_packet);

	memcpy(msg->rx_buf, (u8 *)(rx_packet->data.payload) + sizeof(struct mctp_peci_vdm_hdr),
	       msg->rx_len);

	i3c_mctp_packet_free(rx_packet);

	return 0;
}

static int i3c_peci_probe(struct platform_device *pdev)
{
	struct peci_adapter *adapter;
	struct i3c_peci *priv;
	int ret;

	adapter = peci_alloc_adapter(&pdev->dev, sizeof(*priv));
	if (!adapter)
		return -ENOMEM;

	priv = peci_get_adapdata(adapter);

	platform_set_drvdata(pdev, priv);

	priv->i3cdev = dev_to_i3cdev(pdev->dev.parent);
	priv->dev = &pdev->dev;

	adapter->owner = THIS_MODULE;
	strscpy(adapter->name, pdev->name, sizeof(adapter->name));

	adapter->xfer = i3c_peci_xfer;
	adapter->peci_revision = 0x41;

	priv->adapter = adapter;

	priv->client = i3c_mctp_add_peci_client(priv->i3cdev);
	if (IS_ERR(priv->client)) {
		ret = -ENOMEM;
		goto out_put_device;
	}

	ret = peci_add_adapter(adapter);
	if (ret)
		goto out_del_client;

	return 0;

out_del_client:
	i3c_mctp_remove_peci_client(priv->client);
out_put_device:
	put_device(&adapter->dev);
	return ret;
}

static int i3c_peci_remove(struct platform_device *pdev)
{
	struct i3c_peci *priv = platform_get_drvdata(pdev);

	peci_del_adapter(priv->adapter);

	i3c_mctp_remove_peci_client(priv->client);

	return 0;
}

static struct platform_driver i3c_peci_driver = {
	.probe = i3c_peci_probe,
	.remove = i3c_peci_remove,
	.driver = {
		.name = "peci-i3c",
	},
};
module_platform_driver(i3c_peci_driver);

MODULE_AUTHOR("Oleksandr Shulzhenko <oleksandr.shulzhenko.viktorovych@intel.com>");
MODULE_DESCRIPTION("I3C PECI driver");
MODULE_LICENSE("GPL");
