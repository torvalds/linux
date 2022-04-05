// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Intel Corporation

#include <linux/aspeed-mctp.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/peci.h>
#include <linux/platform_device.h>

#define PCIE_SET_DATA_LEN(x, val)	((x)->len_lo |= (val))
#define PCIE_GET_DATA_LEN(x)		((x)->len_lo)
#define PCIE_GET_PAD_LEN(x)		(((x)->tag >> 4) & 0x3)
#define PCIE_SET_TARGET_ID(x, val)	((x)->target |= (swab16(val)))
#define PCIE_PKT_ALIGN(x)		ALIGN(x, sizeof(u32))
#define PCIE_GET_REQUESTER_ID(x)	(swab16((x)->requester))

/*
 * PCIe header template in "network format" - Big Endian
 */
#define MSG_4DW_HDR_ROUTE_BY_ID	0x72
#define MSG_CODE_VDM_TYPE_1	0x7f
#define VENDOR_ID_DMTF_VDM	0xb41a
static const struct pcie_transport_hdr pcie_hdr_template_be = {
	.fmt_type = MSG_4DW_HDR_ROUTE_BY_ID,
	.code = MSG_CODE_VDM_TYPE_1,
	.vendor = VENDOR_ID_DMTF_VDM
};

#define MSG_TAG_MASK			GENMASK(2, 0)
#define MCTP_SET_MSG_TAG(x, val)	((x)->flags_seq_tag |= ((val) & MSG_TAG_MASK))
#define MCTP_GET_MSG_TAG(x)		((x)->flags_seq_tag & MSG_TAG_MASK)
#define MCTP_HDR_VERSION		1
#define REQUEST_FLAGS			0xc8
#define RESPONSE_FLAGS			0xc0
static const struct mctp_protocol_hdr mctp_hdr_template_be = {
	.ver = MCTP_HDR_VERSION,
	.flags_seq_tag = REQUEST_FLAGS
};

static struct mctp_peci_vdm_hdr {
	u8 type;
	u16 vendor_id;
	u8 instance_req_d;
	u8 vendor_code;
} __packed;

#define PCIE_VDM_TYPE	0x7e
#define INTEL_VENDOR_ID	0x8680
#define PECI_REQUEST	0x80
#define PECI_RESPONSE	0
#define PECI_MSG_OPCODE	0x02
static const struct mctp_peci_vdm_hdr peci_hdr_template = {
	.type = PCIE_VDM_TYPE,
	.vendor_id = INTEL_VENDOR_ID,
	.instance_req_d = PECI_REQUEST,
	.vendor_code = PECI_MSG_OPCODE
};

#define PECI_VDM_TYPE	0x0200
#define PECI_VDM_MASK	0xff00

#define CPUNODEID_CFG_LCLNODEID_MASK	GENMASK(2, 0)
#define CPUNODEID_CFG_OFFSET	0xc0
#define CPUNODEID_CFG_BUS	0x1e
#define CPUNODEID_CFG_DEV	0
#define CPUNODEID_CFG_FUNC	0

struct node_cfg {
	u8 eid;
	u16 bdf;
	u8 domain_id;
};

struct mctp_peci {
	struct peci_adapter *adapter;
	struct device *dev;
	struct mctp_client *peci_client;
	struct node_cfg cpus[PECI_OFFSET_MAX][DOMAIN_OFFSET_MAX];
	bool is_discovery_done;
	u8 tag;
};

static void
prepare_tx_packet(struct mctp_pcie_packet *tx_packet, struct node_cfg *cpu,
		  u8 tx_len, u8 rx_len, u8 *tx_buf, u8 tag)
{
	struct pcie_transport_hdr *pcie_hdr;
	struct mctp_protocol_hdr *mctp_hdr;
	struct mctp_peci_vdm_hdr *peci_hdr;
	u8 *peci_payload;
	u32 payload_len, payload_len_dw;

	BUILD_BUG_ON((sizeof(struct pcie_transport_hdr) +
		     sizeof(struct mctp_protocol_hdr)) != PCIE_VDM_HDR_SIZE);

	pcie_hdr = (struct pcie_transport_hdr *)tx_packet;
	*pcie_hdr = pcie_hdr_template_be;

	mctp_hdr = (struct mctp_protocol_hdr *)&tx_packet->data.hdr[3];
	*mctp_hdr = mctp_hdr_template_be;

	peci_hdr = (struct mctp_peci_vdm_hdr *)tx_packet->data.payload;
	*peci_hdr = peci_hdr_template;

	peci_payload = (u8 *)(tx_packet->data.payload) + sizeof(struct mctp_peci_vdm_hdr);
	peci_payload[0] = tx_len;
	peci_payload[1] = rx_len;
	memcpy(&peci_payload[2], tx_buf, tx_len);

	/*
	 * MCTP packet payload consists of PECI VDM header, WL, RL and actual
	 * PECI payload
	 */
	payload_len = sizeof(struct mctp_peci_vdm_hdr) + 2 + tx_len;
	payload_len_dw = PCIE_PKT_ALIGN(payload_len) / sizeof(u32);

	PCIE_SET_DATA_LEN(pcie_hdr, payload_len_dw);

	tx_packet->size = PCIE_PKT_ALIGN(payload_len) + PCIE_VDM_HDR_SIZE;

	mctp_hdr->dest = cpu->eid;
	PCIE_SET_TARGET_ID(pcie_hdr, cpu->bdf);
	MCTP_SET_MSG_TAG(mctp_hdr, tag);
}

static int
verify_rx_packet(struct peci_adapter *adapter, struct mctp_pcie_packet *rx_packet,
		 struct node_cfg *cpu, u8 tag)
{
	struct mctp_peci *priv = peci_get_adapdata(adapter);
	bool invalid_packet = false;
	struct pcie_transport_hdr *pcie_hdr;
	struct mctp_protocol_hdr *mctp_hdr;
	struct mctp_peci_vdm_hdr *peci_hdr;
	u8 expected_flags;
	u16 requester_id;

	expected_flags = (RESPONSE_FLAGS | (tag & MSG_TAG_MASK));

	pcie_hdr = (struct pcie_transport_hdr *)rx_packet;
	mctp_hdr = (struct mctp_protocol_hdr *)&rx_packet->data.hdr[3];
	peci_hdr = (struct mctp_peci_vdm_hdr *)rx_packet->data.payload;

	requester_id = PCIE_GET_REQUESTER_ID(pcie_hdr);

	if (requester_id != cpu->bdf) {
		dev_dbg(priv->dev,
			"mismatch in src bdf: expected: 0x%.4x, got: 0x%.4x",
			cpu->bdf, requester_id);
		invalid_packet = true;
	}
	if (mctp_hdr->src != cpu->eid) {
		dev_dbg(priv->dev,
			"mismatch in src eid: expected: 0x%.2x, got: 0x%.2x",
			cpu->eid, mctp_hdr->src);
		invalid_packet = true;
	}
	if (mctp_hdr->flags_seq_tag != expected_flags) {
		dev_dbg(priv->dev,
			"mismatch in mctp flags: expected: 0x%.2x, got: 0x%.2x",
			expected_flags, mctp_hdr->flags_seq_tag);
		invalid_packet = true;
	}
	if (peci_hdr->instance_req_d != PECI_RESPONSE) {
		dev_dbg(priv->dev,
			"packet doesn't match a response: expected: 0x%.2x, got: 0x%.2x",
			PECI_RESPONSE, peci_hdr->instance_req_d);
		invalid_packet = true;
	}

	if (invalid_packet) {
		dev_warn_ratelimited(priv->dev, "unexpected peci response found\n");
		return -EIO;
	}

	return 0;
}

static struct mctp_pcie_packet *
mctp_peci_send_receive(struct peci_adapter *adapter, struct node_cfg *cpu,
		       u8 tx_len, u8 rx_len, u8 *tx_buf)
{
	struct mctp_peci *priv = peci_get_adapdata(adapter);
	/* XXX: Sporadically it can take up to 1100 ms for response to arrive */
	unsigned long timeout = msecs_to_jiffies(1100);
	u8 tag = priv->tag;
	struct mctp_pcie_packet *tx_packet, *rx_packet;
	unsigned long current_time, end_time;
	struct pcie_transport_hdr *pcie_hdr;
	u32 payload_len, rx_packet_size;
	int ret;

	tx_packet = aspeed_mctp_packet_alloc(GFP_KERNEL);
	if (!tx_packet)
		return ERR_PTR(-ENOMEM);

	prepare_tx_packet(tx_packet, cpu, tx_len, rx_len, tx_buf, tag);

	aspeed_mctp_flush_rx_queue(priv->peci_client);

	print_hex_dump_bytes("TX : ", DUMP_PREFIX_NONE, &tx_packet->data, tx_packet->size);

	ret = aspeed_mctp_send_packet(priv->peci_client, tx_packet);
	if (ret) {
		dev_dbg_ratelimited(priv->dev, "failed to send mctp packet: %d\n", ret);
		aspeed_mctp_packet_free(tx_packet);
		return ERR_PTR(ret);
	}
	priv->tag++;

	end_time = jiffies + timeout;
retry:
	rx_packet = aspeed_mctp_receive_packet(priv->peci_client, timeout);
	if (IS_ERR(rx_packet)) {
		if (PTR_ERR(rx_packet) != -ERESTARTSYS)
			dev_err_ratelimited(priv->dev, "failed to receive mctp packet: %ld\n",
					    PTR_ERR(rx_packet));

		return rx_packet;
	}
	WARN_ON(!rx_packet);

	ret = verify_rx_packet(adapter, rx_packet, cpu, tag);
	current_time = jiffies;
	if (ret && time_before(current_time, end_time)) {
		aspeed_mctp_packet_free(rx_packet);
		timeout = ((long)end_time - (long)current_time);
		goto retry;
	}

	pcie_hdr = (struct pcie_transport_hdr *)rx_packet;
	payload_len = PCIE_GET_DATA_LEN(pcie_hdr) * sizeof(u32) - PCIE_GET_PAD_LEN(pcie_hdr);
	rx_packet_size = payload_len + PCIE_VDM_HDR_SIZE;
	print_hex_dump_bytes("RX : ", DUMP_PREFIX_NONE, &rx_packet->data, rx_packet_size);

	return rx_packet;
}

static void mctp_peci_cpu_discovery(struct peci_adapter *adapter)
{
	const u8 eids[] = { 0x1d, 0x3d, 0x5d, 0x7d, 0x9d, 0xbd, 0xdd, 0xfd };
	struct mctp_peci *priv = peci_get_adapdata(adapter);
	u8 tx_buf[PECI_RDENDPTCFG_PCI_WRITE_LEN];
	struct mctp_pcie_packet *rx_packet;
	struct node_cfg cpu;
	int i, domain_id, node_id, ret;
	bool is_discovery_done = false;
	u8 *rx_buf;
	u32 addr;

	addr = CPUNODEID_CFG_OFFSET;     /* [11:0] offset */
	addr |= CPUNODEID_CFG_FUNC << 12;/* [14:12] function */
	addr |= CPUNODEID_CFG_DEV << 15; /* [19:15] device */
	addr |= CPUNODEID_CFG_BUS << 20; /* [27:20] bus, [31:28] reserved */

	tx_buf[0] = PECI_RDENDPTCFG_CMD;
	tx_buf[1] = 0;
	tx_buf[2] = PECI_ENDPTCFG_TYPE_LOCAL_PCI;
	tx_buf[3] = 0; /* Endpoint ID */
	tx_buf[4] = 0; /* Reserved */
	tx_buf[5] = 0; /* Reserved */
	tx_buf[6] = PECI_ENDPTCFG_ADDR_TYPE_PCI;
	tx_buf[7] = 0; /* PCI Segment */
	tx_buf[8] = (u8)addr;
	tx_buf[9] = (u8)(addr >> 8);
	tx_buf[10] = (u8)(addr >> 16);
	tx_buf[11] = (u8)(addr >> 24);

	for (i = 0; i < PECI_OFFSET_MAX; i++) {
		memset(&cpu, 0, sizeof(cpu));
		cpu.eid = eids[i];
		ret = aspeed_mctp_get_eid_bdf(priv->peci_client, cpu.eid, &cpu.bdf);
		if (ret)
			continue;

		for (domain_id = 0; domain_id < DOMAIN_OFFSET_MAX; domain_id++) {
			ret = aspeed_mctp_get_eid(priv->peci_client,
						  cpu.bdf, domain_id,
						  &cpu.eid);

			/* No entries for specific BDF/domain_Id. */
			if (ret)
				continue;

			rx_packet = mctp_peci_send_receive(adapter, &cpu,
							   PECI_RDENDPTCFG_PCI_WRITE_LEN,
							   PECI_RDENDPTCFG_READ_LEN_BASE + 4,
							   tx_buf);

			if (IS_ERR(rx_packet)) {
				dev_warn(priv->dev, "Device EID=%d DomainId=%d not discovered\n",
					 cpu.eid, cpu.domain_id);
				continue;
			}

			rx_buf = (u8 *)(rx_packet->data.payload) + sizeof(struct mctp_peci_vdm_hdr);
			node_id = rx_buf[1] & CPUNODEID_CFG_LCLNODEID_MASK;
			if (node_id < PECI_OFFSET_MAX) {
				is_discovery_done = true;
				priv->cpus[node_id][domain_id] = cpu;
			} else {
				dev_warn(priv->dev, "Incorrect node_id=%d (EID=%d DomainId=%d)\n",
					 node_id, cpu.eid, cpu.domain_id);
			}
			aspeed_mctp_packet_free(rx_packet);
		}
	}
	priv->is_discovery_done = is_discovery_done;
}

static int
mctp_peci_get_address(struct peci_adapter *adapter, u8 peci_addr, u8 domain_id,
		      struct node_cfg *cpu)
{
	struct mctp_peci *priv = peci_get_adapdata(adapter);
	int node_id = peci_addr - 0x30;

	/*
	 * XXX: Is it possible we're able to communicate with CPU 0 before other
	 * CPUs are up? Make sure we're always discovering all CPUs.
	 */
	if (!priv->is_discovery_done)
		mctp_peci_cpu_discovery(adapter);

	if (node_id < PECI_OFFSET_MAX && domain_id < DOMAIN_OFFSET_MAX &&
	    priv->is_discovery_done && priv->cpus[node_id][domain_id].eid) {
		*cpu = priv->cpus[node_id][domain_id];
		return 0;
	}

	return -EINVAL;
}

static int
mctp_peci_xfer(struct peci_adapter *adapter, struct peci_xfer_msg *msg)
{
	u32 max_len = sizeof(struct mctp_pcie_packet_data) -
		PCIE_VDM_HDR_SIZE - sizeof(struct mctp_peci_vdm_hdr);
	struct mctp_pcie_packet *rx_packet;
	struct node_cfg cpu;
	u8 domain_id = 0;
	int ret;

	if (msg->tx_len > max_len || msg->rx_len > max_len)
		return -EINVAL;

	if (msg->tx_len > 2)
		domain_id = (msg->tx_buf[1]  >> 1);

	ret = mctp_peci_get_address(adapter, msg->addr, domain_id, &cpu);
	if (ret)
		return ret;

	rx_packet = mctp_peci_send_receive(adapter, &cpu, msg->tx_len, msg->rx_len, msg->tx_buf);
	if (IS_ERR(rx_packet))
		return PTR_ERR(rx_packet);

	memcpy(msg->rx_buf,
	       (u8 *)(rx_packet->data.payload) + sizeof(struct mctp_peci_vdm_hdr),
	       msg->rx_len);

	aspeed_mctp_packet_free(rx_packet);

	return 0;
}

static int mctp_peci_init_peci_client(struct mctp_peci *priv)
{
	struct device *parent = priv->dev->parent;
	int ret;

	priv->peci_client = aspeed_mctp_create_client(dev_get_drvdata(parent));
	if (IS_ERR(priv->peci_client))
		return -ENOMEM;

	ret = aspeed_mctp_add_type_handler(priv->peci_client, PCIE_VDM_TYPE,
					   INTEL_VENDOR_ID, PECI_VDM_TYPE,
					   PECI_VDM_MASK);
	if (ret)
		aspeed_mctp_delete_client(priv->peci_client);

	return ret;
}

static int mctp_peci_probe(struct platform_device *pdev)
{
	struct peci_adapter *adapter;
	struct mctp_peci *priv;
	int ret;

	adapter = peci_alloc_adapter(&pdev->dev, sizeof(*priv));
	if (!adapter)
		return -ENOMEM;

	priv = peci_get_adapdata(adapter);
	priv->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, priv);

	adapter->owner = THIS_MODULE;
	strlcpy(adapter->name, pdev->name, sizeof(adapter->name));

	adapter->xfer = mctp_peci_xfer;
	adapter->peci_revision = 0x41;

	priv->adapter = adapter;

	ret = mctp_peci_init_peci_client(priv);
	if (ret)
		goto out_put_device;

	ret = peci_add_adapter(adapter);
	if (ret)
		goto out_del_client;

	return 0;

out_del_client:
	aspeed_mctp_delete_client(priv->peci_client);
out_put_device:
	put_device(&adapter->dev);
	return ret;
}

static int mctp_peci_remove(struct platform_device *pdev)
{
	struct mctp_peci *priv = dev_get_drvdata(&pdev->dev);

	if (!priv)
		goto out;

	aspeed_mctp_delete_client(priv->peci_client);

	peci_del_adapter(priv->adapter);
out:
	return 0;
}

static struct platform_driver mctp_peci_driver = {
	.probe  = mctp_peci_probe,
	.remove = mctp_peci_remove,
	.driver = {
		.name = "peci-mctp",
	},
};
module_platform_driver(mctp_peci_driver);

MODULE_ALIAS("platform:peci-mctp");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Iwona Winiarska <iwona.winiarska@intel.com>");
MODULE_DESCRIPTION("PECI MCTP driver");
