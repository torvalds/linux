// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2018-2019 Hisilicon Limited. */

#include <linux/debugfs.h>
#include <linux/device.h>

#include "hnae3.h"
#include "hns3_debugfs.h"
#include "hns3_enet.h"

static struct dentry *hns3_dbgfs_root;

static struct hns3_dbg_dentry_info hns3_dbg_dentry[] = {
	{
		.name = "tm"
	},
	{
		.name = "tx_bd_info"
	},
	{
		.name = "rx_bd_info"
	},
	{
		.name = "mac_list"
	},
	{
		.name = "reg"
	},
	/* keep common at the bottom and add new directory above */
	{
		.name = "common"
	},
};

static int hns3_dbg_bd_file_init(struct hnae3_handle *handle, unsigned int cmd);
static int hns3_dbg_common_file_init(struct hnae3_handle *handle,
				     unsigned int cmd);

static struct hns3_dbg_cmd_info hns3_dbg_cmd[] = {
	{
		.name = "tm_nodes",
		.cmd = HNAE3_DBG_CMD_TM_NODES,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "tm_priority",
		.cmd = HNAE3_DBG_CMD_TM_PRI,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "tm_qset",
		.cmd = HNAE3_DBG_CMD_TM_QSET,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "dev_info",
		.cmd = HNAE3_DBG_CMD_DEV_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "tx_bd_queue",
		.cmd = HNAE3_DBG_CMD_TX_BD,
		.dentry = HNS3_DBG_DENTRY_TX_BD,
		.buf_len = HNS3_DBG_READ_LEN_4MB,
		.init = hns3_dbg_bd_file_init,
	},
	{
		.name = "rx_bd_queue",
		.cmd = HNAE3_DBG_CMD_RX_BD,
		.dentry = HNS3_DBG_DENTRY_RX_BD,
		.buf_len = HNS3_DBG_READ_LEN_4MB,
		.init = hns3_dbg_bd_file_init,
	},
	{
		.name = "uc",
		.cmd = HNAE3_DBG_CMD_MAC_UC,
		.dentry = HNS3_DBG_DENTRY_MAC,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "mc",
		.cmd = HNAE3_DBG_CMD_MAC_MC,
		.dentry = HNS3_DBG_DENTRY_MAC,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "mng_tbl",
		.cmd = HNAE3_DBG_CMD_MNG_TBL,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "loopback",
		.cmd = HNAE3_DBG_CMD_LOOPBACK,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "interrupt_info",
		.cmd = HNAE3_DBG_CMD_INTERRUPT_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "reset_info",
		.cmd = HNAE3_DBG_CMD_RESET_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "imp_info",
		.cmd = HNAE3_DBG_CMD_IMP_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "ncl_config",
		.cmd = HNAE3_DBG_CMD_NCL_CONFIG,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN_128KB,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "bios_common",
		.cmd = HNAE3_DBG_CMD_REG_BIOS_COMMON,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "ssu",
		.cmd = HNAE3_DBG_CMD_REG_SSU,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "igu_egu",
		.cmd = HNAE3_DBG_CMD_REG_IGU_EGU,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "rpu",
		.cmd = HNAE3_DBG_CMD_REG_RPU,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "ncsi",
		.cmd = HNAE3_DBG_CMD_REG_NCSI,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "rtc",
		.cmd = HNAE3_DBG_CMD_REG_RTC,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "ppp",
		.cmd = HNAE3_DBG_CMD_REG_PPP,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "rcb",
		.cmd = HNAE3_DBG_CMD_REG_RCB,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "tqp",
		.cmd = HNAE3_DBG_CMD_REG_TQP,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "mac",
		.cmd = HNAE3_DBG_CMD_REG_MAC,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "dcb",
		.cmd = HNAE3_DBG_CMD_REG_DCB,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
};

static struct hns3_dbg_cap_info hns3_dbg_cap[] = {
	{
		.name = "support FD",
		.cap_bit = HNAE3_DEV_SUPPORT_FD_B,
	}, {
		.name = "support GRO",
		.cap_bit = HNAE3_DEV_SUPPORT_GRO_B,
	}, {
		.name = "support FEC",
		.cap_bit = HNAE3_DEV_SUPPORT_FEC_B,
	}, {
		.name = "support UDP GSO",
		.cap_bit = HNAE3_DEV_SUPPORT_UDP_GSO_B,
	}, {
		.name = "support PTP",
		.cap_bit = HNAE3_DEV_SUPPORT_PTP_B,
	}, {
		.name = "support INT QL",
		.cap_bit = HNAE3_DEV_SUPPORT_INT_QL_B,
	}, {
		.name = "support HW TX csum",
		.cap_bit = HNAE3_DEV_SUPPORT_HW_TX_CSUM_B,
	}, {
		.name = "support UDP tunnel csum",
		.cap_bit = HNAE3_DEV_SUPPORT_UDP_TUNNEL_CSUM_B,
	}, {
		.name = "support TX push",
		.cap_bit = HNAE3_DEV_SUPPORT_TX_PUSH_B,
	}, {
		.name = "support imp-controlled PHY",
		.cap_bit = HNAE3_DEV_SUPPORT_PHY_IMP_B,
	}, {
		.name = "support rxd advanced layout",
		.cap_bit = HNAE3_DEV_SUPPORT_RXD_ADV_LAYOUT_B,
	},
};

static void hns3_dbg_fill_content(char *content, u16 len,
				  const struct hns3_dbg_item *items,
				  const char **result, u16 size)
{
	char *pos = content;
	u16 i;

	memset(content, ' ', len);
	for (i = 0; i < size; i++) {
		if (result)
			strncpy(pos, result[i], strlen(result[i]));
		else
			strncpy(pos, items[i].name, strlen(items[i].name));

		pos += strlen(items[i].name) + items[i].interval;
	}

	*pos++ = '\n';
	*pos++ = '\0';
}

static int hns3_dbg_queue_info(struct hnae3_handle *h,
			       const char *cmd_buf)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);
	struct hns3_nic_priv *priv = h->priv;
	struct hns3_enet_ring *ring;
	u32 base_add_l, base_add_h;
	u32 queue_num, queue_max;
	u32 value, i;
	int cnt;

	if (!priv->ring) {
		dev_err(&h->pdev->dev, "priv->ring is NULL\n");
		return -EFAULT;
	}

	queue_max = h->kinfo.num_tqps;
	cnt = kstrtouint(&cmd_buf[11], 0, &queue_num);
	if (cnt)
		queue_num = 0;
	else
		queue_max = queue_num + 1;

	dev_info(&h->pdev->dev, "queue info\n");

	if (queue_num >= h->kinfo.num_tqps) {
		dev_err(&h->pdev->dev,
			"Queue number(%u) is out of range(0-%u)\n", queue_num,
			h->kinfo.num_tqps - 1);
		return -EINVAL;
	}

	for (i = queue_num; i < queue_max; i++) {
		/* Each cycle needs to determine whether the instance is reset,
		 * to prevent reference to invalid memory. And need to ensure
		 * that the following code is executed within 100ms.
		 */
		if (!test_bit(HNS3_NIC_STATE_INITED, &priv->state) ||
		    test_bit(HNS3_NIC_STATE_RESETTING, &priv->state))
			return -EPERM;

		ring = &priv->ring[(u32)(i + h->kinfo.num_tqps)];
		base_add_h = readl_relaxed(ring->tqp->io_base +
					   HNS3_RING_RX_RING_BASEADDR_H_REG);
		base_add_l = readl_relaxed(ring->tqp->io_base +
					   HNS3_RING_RX_RING_BASEADDR_L_REG);
		dev_info(&h->pdev->dev, "RX(%u) BASE ADD: 0x%08x%08x\n", i,
			 base_add_h, base_add_l);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_RX_RING_BD_NUM_REG);
		dev_info(&h->pdev->dev, "RX(%u) RING BD NUM: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_RX_RING_BD_LEN_REG);
		dev_info(&h->pdev->dev, "RX(%u) RING BD LEN: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_RX_RING_TAIL_REG);
		dev_info(&h->pdev->dev, "RX(%u) RING TAIL: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_RX_RING_HEAD_REG);
		dev_info(&h->pdev->dev, "RX(%u) RING HEAD: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_RX_RING_FBDNUM_REG);
		dev_info(&h->pdev->dev, "RX(%u) RING FBDNUM: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_RX_RING_PKTNUM_RECORD_REG);
		dev_info(&h->pdev->dev, "RX(%u) RING PKTNUM: %u\n", i, value);

		ring = &priv->ring[i];
		base_add_h = readl_relaxed(ring->tqp->io_base +
					   HNS3_RING_TX_RING_BASEADDR_H_REG);
		base_add_l = readl_relaxed(ring->tqp->io_base +
					   HNS3_RING_TX_RING_BASEADDR_L_REG);
		dev_info(&h->pdev->dev, "TX(%u) BASE ADD: 0x%08x%08x\n", i,
			 base_add_h, base_add_l);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_BD_NUM_REG);
		dev_info(&h->pdev->dev, "TX(%u) RING BD NUM: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_TC_REG);
		dev_info(&h->pdev->dev, "TX(%u) RING TC: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_TAIL_REG);
		dev_info(&h->pdev->dev, "TX(%u) RING TAIL: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_HEAD_REG);
		dev_info(&h->pdev->dev, "TX(%u) RING HEAD: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_FBDNUM_REG);
		dev_info(&h->pdev->dev, "TX(%u) RING FBDNUM: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_OFFSET_REG);
		dev_info(&h->pdev->dev, "TX(%u) RING OFFSET: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_PKTNUM_RECORD_REG);
		dev_info(&h->pdev->dev, "TX(%u) RING PKTNUM: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base + HNS3_RING_EN_REG);
		dev_info(&h->pdev->dev, "TX/RX(%u) RING EN: %s\n", i,
			 value ? "enable" : "disable");

		if (hnae3_ae_dev_tqp_txrx_indep_supported(ae_dev)) {
			value = readl_relaxed(ring->tqp->io_base +
					      HNS3_RING_TX_EN_REG);
			dev_info(&h->pdev->dev, "TX(%u) RING EN: %s\n", i,
				 value ? "enable" : "disable");

			value = readl_relaxed(ring->tqp->io_base +
					      HNS3_RING_RX_EN_REG);
			dev_info(&h->pdev->dev, "RX(%u) RING EN: %s\n", i,
				 value ? "enable" : "disable");
		}

		dev_info(&h->pdev->dev, "\n");
	}

	return 0;
}

static int hns3_dbg_queue_map(struct hnae3_handle *h)
{
	struct hns3_nic_priv *priv = h->priv;
	int i;

	if (!h->ae_algo->ops->get_global_queue_id)
		return -EOPNOTSUPP;

	dev_info(&h->pdev->dev, "map info for queue id and vector id\n");
	dev_info(&h->pdev->dev,
		 "local queue id | global queue id | vector id\n");
	for (i = 0; i < h->kinfo.num_tqps; i++) {
		u16 global_qid;

		global_qid = h->ae_algo->ops->get_global_queue_id(h, i);
		if (!priv->ring || !priv->ring[i].tqp_vector)
			continue;

		dev_info(&h->pdev->dev,
			 "      %4d            %4u            %4d\n",
			 i, global_qid, priv->ring[i].tqp_vector->vector_irq);
	}

	return 0;
}

static const struct hns3_dbg_item rx_bd_info_items[] = {
	{ "BD_IDX", 3 },
	{ "L234_INFO", 2 },
	{ "PKT_LEN", 3 },
	{ "SIZE", 4 },
	{ "RSS_HASH", 4 },
	{ "FD_ID", 2 },
	{ "VLAN_TAG", 2 },
	{ "O_DM_VLAN_ID_FB", 2 },
	{ "OT_VLAN_TAG", 2 },
	{ "BD_BASE_INFO", 2 },
	{ "PTYPE", 2 },
	{ "HW_CSUM", 2 },
};

static void hns3_dump_rx_bd_info(struct hns3_nic_priv *priv,
				 struct hns3_desc *desc, char **result, int idx)
{
	unsigned int j = 0;

	sprintf(result[j++], "%5d", idx);
	sprintf(result[j++], "%#x", le32_to_cpu(desc->rx.l234_info));
	sprintf(result[j++], "%7u", le16_to_cpu(desc->rx.pkt_len));
	sprintf(result[j++], "%4u", le16_to_cpu(desc->rx.size));
	sprintf(result[j++], "%#x", le32_to_cpu(desc->rx.rss_hash));
	sprintf(result[j++], "%5u", le16_to_cpu(desc->rx.fd_id));
	sprintf(result[j++], "%8u", le16_to_cpu(desc->rx.vlan_tag));
	sprintf(result[j++], "%15u", le16_to_cpu(desc->rx.o_dm_vlan_id_fb));
	sprintf(result[j++], "%11u", le16_to_cpu(desc->rx.ot_vlan_tag));
	sprintf(result[j++], "%#x", le32_to_cpu(desc->rx.bd_base_info));
	if (test_bit(HNS3_NIC_STATE_RXD_ADV_LAYOUT_ENABLE, &priv->state)) {
		u32 ol_info = le32_to_cpu(desc->rx.ol_info);

		sprintf(result[j++], "%5lu", hnae3_get_field(ol_info,
							     HNS3_RXD_PTYPE_M,
							     HNS3_RXD_PTYPE_S));
		sprintf(result[j++], "%7u", le16_to_cpu(desc->csum));
	} else {
		sprintf(result[j++], "NA");
		sprintf(result[j++], "NA");
	}
}

static int hns3_dbg_rx_bd_info(struct hns3_dbg_data *d, char *buf, int len)
{
	char data_str[ARRAY_SIZE(rx_bd_info_items)][HNS3_DBG_DATA_STR_LEN];
	struct hns3_nic_priv *priv = d->handle->priv;
	char *result[ARRAY_SIZE(rx_bd_info_items)];
	char content[HNS3_DBG_INFO_LEN];
	struct hns3_enet_ring *ring;
	struct hns3_desc *desc;
	unsigned int i;
	int pos = 0;

	if (d->qid >= d->handle->kinfo.num_tqps) {
		dev_err(&d->handle->pdev->dev,
			"queue%u is not in use\n", d->qid);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(rx_bd_info_items); i++)
		result[i] = &data_str[i][0];

	pos += scnprintf(buf + pos, len - pos,
			  "Queue %u rx bd info:\n", d->qid);
	hns3_dbg_fill_content(content, sizeof(content), rx_bd_info_items,
			      NULL, ARRAY_SIZE(rx_bd_info_items));
	pos += scnprintf(buf + pos, len - pos, "%s", content);

	ring = &priv->ring[d->qid + d->handle->kinfo.num_tqps];
	for (i = 0; i < ring->desc_num; i++) {
		desc = &ring->desc[i];

		hns3_dump_rx_bd_info(priv, desc, result, i);
		hns3_dbg_fill_content(content, sizeof(content),
				      rx_bd_info_items, (const char **)result,
				      ARRAY_SIZE(rx_bd_info_items));
		pos += scnprintf(buf + pos, len - pos, "%s", content);
	}

	return 0;
}

static const struct hns3_dbg_item tx_bd_info_items[] = {
	{ "BD_IDX", 5 },
	{ "ADDRESS", 2 },
	{ "VLAN_TAG", 2 },
	{ "SIZE", 2 },
	{ "T_CS_VLAN_TSO", 2 },
	{ "OT_VLAN_TAG", 3 },
	{ "TV", 2 },
	{ "OLT_VLAN_LEN", 2},
	{ "PAYLEN_OL4CS", 2},
	{ "BD_FE_SC_VLD", 2},
	{ "MSS_HW_CSUM", 0},
};

static void hns3_dump_tx_bd_info(struct hns3_nic_priv *priv,
				 struct hns3_desc *desc, char **result, int idx)
{
	unsigned int j = 0;

	sprintf(result[j++], "%6d", idx);
	sprintf(result[j++], "%#llx", le64_to_cpu(desc->addr));
	sprintf(result[j++], "%5u", le16_to_cpu(desc->tx.vlan_tag));
	sprintf(result[j++], "%5u", le16_to_cpu(desc->tx.send_size));
	sprintf(result[j++], "%#x",
		le32_to_cpu(desc->tx.type_cs_vlan_tso_len));
	sprintf(result[j++], "%5u", le16_to_cpu(desc->tx.outer_vlan_tag));
	sprintf(result[j++], "%5u", le16_to_cpu(desc->tx.tv));
	sprintf(result[j++], "%10u",
		le32_to_cpu(desc->tx.ol_type_vlan_len_msec));
	sprintf(result[j++], "%#x", le32_to_cpu(desc->tx.paylen_ol4cs));
	sprintf(result[j++], "%#x", le16_to_cpu(desc->tx.bdtp_fe_sc_vld_ra_ri));
	sprintf(result[j++], "%5u", le16_to_cpu(desc->tx.mss_hw_csum));
}

static int hns3_dbg_tx_bd_info(struct hns3_dbg_data *d, char *buf, int len)
{
	char data_str[ARRAY_SIZE(tx_bd_info_items)][HNS3_DBG_DATA_STR_LEN];
	struct hns3_nic_priv *priv = d->handle->priv;
	char *result[ARRAY_SIZE(tx_bd_info_items)];
	char content[HNS3_DBG_INFO_LEN];
	struct hns3_enet_ring *ring;
	struct hns3_desc *desc;
	unsigned int i;
	int pos = 0;

	if (d->qid >= d->handle->kinfo.num_tqps) {
		dev_err(&d->handle->pdev->dev,
			"queue%u is not in use\n", d->qid);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(tx_bd_info_items); i++)
		result[i] = &data_str[i][0];

	pos += scnprintf(buf + pos, len - pos,
			  "Queue %u tx bd info:\n", d->qid);
	hns3_dbg_fill_content(content, sizeof(content), tx_bd_info_items,
			      NULL, ARRAY_SIZE(tx_bd_info_items));
	pos += scnprintf(buf + pos, len - pos, "%s", content);

	ring = &priv->ring[d->qid];
	for (i = 0; i < ring->desc_num; i++) {
		desc = &ring->desc[i];

		hns3_dump_tx_bd_info(priv, desc, result, i);
		hns3_dbg_fill_content(content, sizeof(content),
				      tx_bd_info_items, (const char **)result,
				      ARRAY_SIZE(tx_bd_info_items));
		pos += scnprintf(buf + pos, len - pos, "%s", content);
	}

	return 0;
}

static void hns3_dbg_help(struct hnae3_handle *h)
{
	dev_info(&h->pdev->dev, "available commands\n");
	dev_info(&h->pdev->dev, "queue info <number>\n");
	dev_info(&h->pdev->dev, "queue map\n");

	if (!hns3_is_phys_func(h->pdev))
		return;

	dev_info(&h->pdev->dev, "dump fd tcam\n");
	dev_info(&h->pdev->dev, "dump tc\n");
	dev_info(&h->pdev->dev, "dump tm map <q_num>\n");
	dev_info(&h->pdev->dev, "dump tm\n");
	dev_info(&h->pdev->dev, "dump qos pause cfg\n");
	dev_info(&h->pdev->dev, "dump qos pri map\n");
	dev_info(&h->pdev->dev, "dump qos buf cfg\n");
	dev_info(&h->pdev->dev, "dump mac tnl status\n");
	dev_info(&h->pdev->dev, "dump qs shaper [qs id]\n");
}

static void
hns3_dbg_dev_caps(struct hnae3_handle *h, char *buf, int len, int *pos)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);
	static const char * const str[] = {"no", "yes"};
	unsigned long *caps = ae_dev->caps;
	u32 i, state;

	*pos += scnprintf(buf + *pos, len - *pos, "dev capability:\n");

	for (i = 0; i < ARRAY_SIZE(hns3_dbg_cap); i++) {
		state = test_bit(hns3_dbg_cap[i].cap_bit, caps);
		*pos += scnprintf(buf + *pos, len - *pos, "%s: %s\n",
				  hns3_dbg_cap[i].name, str[state]);
	}

	*pos += scnprintf(buf + *pos, len - *pos, "\n");
}

static void
hns3_dbg_dev_specs(struct hnae3_handle *h, char *buf, int len, int *pos)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);
	struct hnae3_dev_specs *dev_specs = &ae_dev->dev_specs;
	struct hnae3_knic_private_info *kinfo = &h->kinfo;

	*pos += scnprintf(buf + *pos, len - *pos, "dev_spec:\n");
	*pos += scnprintf(buf + *pos, len - *pos, "MAC entry num: %u\n",
			  dev_specs->mac_entry_num);
	*pos += scnprintf(buf + *pos, len - *pos, "MNG entry num: %u\n",
			  dev_specs->mng_entry_num);
	*pos += scnprintf(buf + *pos, len - *pos, "MAX non tso bd num: %u\n",
			  dev_specs->max_non_tso_bd_num);
	*pos += scnprintf(buf + *pos, len - *pos, "RSS ind tbl size: %u\n",
			  dev_specs->rss_ind_tbl_size);
	*pos += scnprintf(buf + *pos, len - *pos, "RSS key size: %u\n",
			  dev_specs->rss_key_size);
	*pos += scnprintf(buf + *pos, len - *pos, "RSS size: %u\n",
			  kinfo->rss_size);
	*pos += scnprintf(buf + *pos, len - *pos, "Allocated RSS size: %u\n",
			  kinfo->req_rss_size);
	*pos += scnprintf(buf + *pos, len - *pos,
			  "Task queue pairs numbers: %u\n",
			  kinfo->num_tqps);
	*pos += scnprintf(buf + *pos, len - *pos, "RX buffer length: %u\n",
			  kinfo->rx_buf_len);
	*pos += scnprintf(buf + *pos, len - *pos, "Desc num per TX queue: %u\n",
			  kinfo->num_tx_desc);
	*pos += scnprintf(buf + *pos, len - *pos, "Desc num per RX queue: %u\n",
			  kinfo->num_rx_desc);
	*pos += scnprintf(buf + *pos, len - *pos,
			  "Total number of enabled TCs: %u\n",
			  kinfo->tc_info.num_tc);
	*pos += scnprintf(buf + *pos, len - *pos, "MAX INT QL: %u\n",
			  dev_specs->int_ql_max);
	*pos += scnprintf(buf + *pos, len - *pos, "MAX INT GL: %u\n",
			  dev_specs->max_int_gl);
	*pos += scnprintf(buf + *pos, len - *pos, "MAX TM RATE: %u\n",
			  dev_specs->max_tm_rate);
	*pos += scnprintf(buf + *pos, len - *pos, "MAX QSET number: %u\n",
			  dev_specs->max_qset_num);
}

static int hns3_dbg_dev_info(struct hnae3_handle *h, char *buf, int len)
{
	int pos = 0;

	hns3_dbg_dev_caps(h, buf, len, &pos);

	hns3_dbg_dev_specs(h, buf, len, &pos);

	return 0;
}

static ssize_t hns3_dbg_cmd_read(struct file *filp, char __user *buffer,
				 size_t count, loff_t *ppos)
{
	int uncopy_bytes;
	char *buf;
	int len;

	if (*ppos != 0)
		return 0;

	if (count < HNS3_DBG_READ_LEN)
		return -ENOSPC;

	buf = kzalloc(HNS3_DBG_READ_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len = scnprintf(buf, HNS3_DBG_READ_LEN, "%s\n",
			"Please echo help to cmd to get help information");
	uncopy_bytes = copy_to_user(buffer, buf, len);

	kfree(buf);

	if (uncopy_bytes)
		return -EFAULT;

	return (*ppos = len);
}

static int hns3_dbg_check_cmd(struct hnae3_handle *handle, char *cmd_buf)
{
	int ret = 0;

	if (strncmp(cmd_buf, "help", 4) == 0)
		hns3_dbg_help(handle);
	else if (strncmp(cmd_buf, "queue info", 10) == 0)
		ret = hns3_dbg_queue_info(handle, cmd_buf);
	else if (strncmp(cmd_buf, "queue map", 9) == 0)
		ret = hns3_dbg_queue_map(handle);
	else if (handle->ae_algo->ops->dbg_run_cmd)
		ret = handle->ae_algo->ops->dbg_run_cmd(handle, cmd_buf);
	else
		ret = -EOPNOTSUPP;

	return ret;
}

static ssize_t hns3_dbg_cmd_write(struct file *filp, const char __user *buffer,
				  size_t count, loff_t *ppos)
{
	struct hnae3_handle *handle = filp->private_data;
	struct hns3_nic_priv *priv  = handle->priv;
	char *cmd_buf, *cmd_buf_tmp;
	int uncopied_bytes;
	int ret;

	if (*ppos != 0)
		return 0;

	/* Judge if the instance is being reset. */
	if (!test_bit(HNS3_NIC_STATE_INITED, &priv->state) ||
	    test_bit(HNS3_NIC_STATE_RESETTING, &priv->state))
		return 0;

	if (count > HNS3_DBG_WRITE_LEN)
		return -ENOSPC;

	cmd_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!cmd_buf)
		return count;

	uncopied_bytes = copy_from_user(cmd_buf, buffer, count);
	if (uncopied_bytes) {
		kfree(cmd_buf);
		return -EFAULT;
	}

	cmd_buf[count] = '\0';

	cmd_buf_tmp = strchr(cmd_buf, '\n');
	if (cmd_buf_tmp) {
		*cmd_buf_tmp = '\0';
		count = cmd_buf_tmp - cmd_buf + 1;
	}

	ret = hns3_dbg_check_cmd(handle, cmd_buf);
	if (ret)
		hns3_dbg_help(handle);

	kfree(cmd_buf);
	cmd_buf = NULL;

	return count;
}

static int hns3_dbg_get_cmd_index(struct hnae3_handle *handle,
				  const unsigned char *name, u32 *index)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(hns3_dbg_cmd); i++) {
		if (!strncmp(name, hns3_dbg_cmd[i].name,
			     strlen(hns3_dbg_cmd[i].name))) {
			*index = i;
			return 0;
		}
	}

	dev_err(&handle->pdev->dev, "unknown command(%s)\n", name);
	return -EINVAL;
}

static const struct hns3_dbg_func hns3_dbg_cmd_func[] = {
	{
		.cmd = HNAE3_DBG_CMD_DEV_INFO,
		.dbg_dump = hns3_dbg_dev_info,
	},
	{
		.cmd = HNAE3_DBG_CMD_TX_BD,
		.dbg_dump_bd = hns3_dbg_tx_bd_info,
	},
	{
		.cmd = HNAE3_DBG_CMD_RX_BD,
		.dbg_dump_bd = hns3_dbg_rx_bd_info,
	},
};

static int hns3_dbg_read_cmd(struct hns3_dbg_data *dbg_data,
			     enum hnae3_dbg_cmd cmd, char *buf, int len)
{
	const struct hnae3_ae_ops *ops = dbg_data->handle->ae_algo->ops;
	const struct hns3_dbg_func *cmd_func;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(hns3_dbg_cmd_func); i++) {
		if (cmd == hns3_dbg_cmd_func[i].cmd) {
			cmd_func = &hns3_dbg_cmd_func[i];
			if (cmd_func->dbg_dump)
				return cmd_func->dbg_dump(dbg_data->handle, buf,
							  len);
			else
				return cmd_func->dbg_dump_bd(dbg_data, buf,
							     len);
		}
	}

	if (!ops->dbg_read_cmd)
		return -EOPNOTSUPP;

	return ops->dbg_read_cmd(dbg_data->handle, cmd, buf, len);
}

static ssize_t hns3_dbg_read(struct file *filp, char __user *buffer,
			     size_t count, loff_t *ppos)
{
	struct hns3_dbg_data *dbg_data = filp->private_data;
	struct hnae3_handle *handle = dbg_data->handle;
	struct hns3_nic_priv *priv = handle->priv;
	ssize_t size = 0;
	char **save_buf;
	char *read_buf;
	u32 index;
	int ret;

	ret = hns3_dbg_get_cmd_index(handle, filp->f_path.dentry->d_iname,
				     &index);
	if (ret)
		return ret;

	save_buf = &hns3_dbg_cmd[index].buf;

	if (!test_bit(HNS3_NIC_STATE_INITED, &priv->state) ||
	    test_bit(HNS3_NIC_STATE_RESETTING, &priv->state)) {
		ret = -EBUSY;
		goto out;
	}

	if (*save_buf) {
		read_buf = *save_buf;
	} else {
		read_buf = kvzalloc(hns3_dbg_cmd[index].buf_len, GFP_KERNEL);
		if (!read_buf)
			return -ENOMEM;

		/* save the buffer addr until the last read operation */
		*save_buf = read_buf;
	}

	/* get data ready for the first time to read */
	if (!*ppos) {
		ret = hns3_dbg_read_cmd(dbg_data, hns3_dbg_cmd[index].cmd,
					read_buf, hns3_dbg_cmd[index].buf_len);
		if (ret)
			goto out;
	}

	size = simple_read_from_buffer(buffer, count, ppos, read_buf,
				       strlen(read_buf));
	if (size > 0)
		return size;

out:
	/* free the buffer for the last read operation */
	if (*save_buf) {
		kvfree(*save_buf);
		*save_buf = NULL;
	}

	return ret;
}

static const struct file_operations hns3_dbg_cmd_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = hns3_dbg_cmd_read,
	.write = hns3_dbg_cmd_write,
};

static const struct file_operations hns3_dbg_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = hns3_dbg_read,
};

static int hns3_dbg_bd_file_init(struct hnae3_handle *handle, u32 cmd)
{
	struct dentry *entry_dir;
	struct hns3_dbg_data *data;
	u16 max_queue_num;
	unsigned int i;

	entry_dir = hns3_dbg_dentry[hns3_dbg_cmd[cmd].dentry].dentry;
	max_queue_num = hns3_get_max_available_channels(handle);
	data = devm_kzalloc(&handle->pdev->dev, max_queue_num * sizeof(*data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	for (i = 0; i < max_queue_num; i++) {
		char name[HNS3_DBG_FILE_NAME_LEN];

		data[i].handle = handle;
		data[i].qid = i;
		sprintf(name, "%s%u", hns3_dbg_cmd[cmd].name, i);
		debugfs_create_file(name, 0400, entry_dir, &data[i],
				    &hns3_dbg_fops);
	}

	return 0;
}

static int
hns3_dbg_common_file_init(struct hnae3_handle *handle, u32 cmd)
{
	struct hns3_dbg_data *data;
	struct dentry *entry_dir;

	data = devm_kzalloc(&handle->pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->handle = handle;
	entry_dir = hns3_dbg_dentry[hns3_dbg_cmd[cmd].dentry].dentry;
	debugfs_create_file(hns3_dbg_cmd[cmd].name, 0400, entry_dir,
			    data, &hns3_dbg_fops);

	return 0;
}

int hns3_dbg_init(struct hnae3_handle *handle)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(handle->pdev);
	const char *name = pci_name(handle->pdev);
	int ret;
	u32 i;

	hns3_dbg_dentry[HNS3_DBG_DENTRY_COMMON].dentry =
				debugfs_create_dir(name, hns3_dbgfs_root);
	handle->hnae3_dbgfs = hns3_dbg_dentry[HNS3_DBG_DENTRY_COMMON].dentry;

	debugfs_create_file("cmd", 0600, handle->hnae3_dbgfs, handle,
			    &hns3_dbg_cmd_fops);

	for (i = 0; i < HNS3_DBG_DENTRY_COMMON; i++)
		hns3_dbg_dentry[i].dentry =
			debugfs_create_dir(hns3_dbg_dentry[i].name,
					   handle->hnae3_dbgfs);

	for (i = 0; i < ARRAY_SIZE(hns3_dbg_cmd); i++) {
		if (hns3_dbg_cmd[i].cmd == HNAE3_DBG_CMD_TM_NODES &&
		    ae_dev->dev_version <= HNAE3_DEVICE_VERSION_V2)
			continue;

		if (!hns3_dbg_cmd[i].init) {
			dev_err(&handle->pdev->dev,
				"cmd %s lack of init func\n",
				hns3_dbg_cmd[i].name);
			ret = -EINVAL;
			goto out;
		}

		ret = hns3_dbg_cmd[i].init(handle, i);
		if (ret) {
			dev_err(&handle->pdev->dev, "failed to init cmd %s\n",
				hns3_dbg_cmd[i].name);
			goto out;
		}
	}

	return 0;

out:
	debugfs_remove_recursive(handle->hnae3_dbgfs);
	handle->hnae3_dbgfs = NULL;
	return ret;
}

void hns3_dbg_uninit(struct hnae3_handle *handle)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(hns3_dbg_cmd); i++)
		if (hns3_dbg_cmd[i].buf) {
			kvfree(hns3_dbg_cmd[i].buf);
			hns3_dbg_cmd[i].buf = NULL;
		}

	debugfs_remove_recursive(handle->hnae3_dbgfs);
	handle->hnae3_dbgfs = NULL;
}

void hns3_dbg_register_debugfs(const char *debugfs_dir_name)
{
	hns3_dbgfs_root = debugfs_create_dir(debugfs_dir_name, NULL);
}

void hns3_dbg_unregister_debugfs(void)
{
	debugfs_remove_recursive(hns3_dbgfs_root);
	hns3_dbgfs_root = NULL;
}
