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
	/* keep common at the bottom and add new directory above */
	{
		.name = "common"
	},
};

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

static int hns3_dbg_bd_info(struct hnae3_handle *h, const char *cmd_buf)
{
	struct hns3_nic_priv *priv = h->priv;
	struct hns3_desc *rx_desc, *tx_desc;
	struct device *dev = &h->pdev->dev;
	struct hns3_enet_ring *ring;
	u32 tx_index, rx_index;
	u32 q_num, value;
	dma_addr_t addr;
	u16 mss_hw_csum;
	u32 l234info;
	int cnt;

	cnt = sscanf(&cmd_buf[8], "%u %u", &q_num, &tx_index);
	if (cnt == 2) {
		rx_index = tx_index;
	} else if (cnt != 1) {
		dev_err(dev, "bd info: bad command string, cnt=%d\n", cnt);
		return -EINVAL;
	}

	if (q_num >= h->kinfo.num_tqps) {
		dev_err(dev, "Queue number(%u) is out of range(0-%u)\n", q_num,
			h->kinfo.num_tqps - 1);
		return -EINVAL;
	}

	ring = &priv->ring[q_num];
	value = readl_relaxed(ring->tqp->io_base + HNS3_RING_TX_RING_TAIL_REG);
	tx_index = (cnt == 1) ? value : tx_index;

	if (tx_index >= ring->desc_num) {
		dev_err(dev, "bd index(%u) is out of range(0-%u)\n", tx_index,
			ring->desc_num - 1);
		return -EINVAL;
	}

	tx_desc = &ring->desc[tx_index];
	addr = le64_to_cpu(tx_desc->addr);
	mss_hw_csum = le16_to_cpu(tx_desc->tx.mss_hw_csum);
	dev_info(dev, "TX Queue Num: %u, BD Index: %u\n", q_num, tx_index);
	dev_info(dev, "(TX)addr: %pad\n", &addr);
	dev_info(dev, "(TX)vlan_tag: %u\n", le16_to_cpu(tx_desc->tx.vlan_tag));
	dev_info(dev, "(TX)send_size: %u\n",
		 le16_to_cpu(tx_desc->tx.send_size));

	if (mss_hw_csum & BIT(HNS3_TXD_HW_CS_B)) {
		u32 offset = le32_to_cpu(tx_desc->tx.ol_type_vlan_len_msec);
		u32 start = le32_to_cpu(tx_desc->tx.type_cs_vlan_tso_len);

		dev_info(dev, "(TX)csum start: %u\n",
			 hnae3_get_field(start,
					 HNS3_TXD_CSUM_START_M,
					 HNS3_TXD_CSUM_START_S));
		dev_info(dev, "(TX)csum offset: %u\n",
			 hnae3_get_field(offset,
					 HNS3_TXD_CSUM_OFFSET_M,
					 HNS3_TXD_CSUM_OFFSET_S));
	} else {
		dev_info(dev, "(TX)vlan_tso: %u\n",
			 tx_desc->tx.type_cs_vlan_tso);
		dev_info(dev, "(TX)l2_len: %u\n", tx_desc->tx.l2_len);
		dev_info(dev, "(TX)l3_len: %u\n", tx_desc->tx.l3_len);
		dev_info(dev, "(TX)l4_len: %u\n", tx_desc->tx.l4_len);
		dev_info(dev, "(TX)vlan_msec: %u\n",
			 tx_desc->tx.ol_type_vlan_msec);
		dev_info(dev, "(TX)ol2_len: %u\n", tx_desc->tx.ol2_len);
		dev_info(dev, "(TX)ol3_len: %u\n", tx_desc->tx.ol3_len);
		dev_info(dev, "(TX)ol4_len: %u\n", tx_desc->tx.ol4_len);
	}

	dev_info(dev, "(TX)vlan_tag: %u\n",
		 le16_to_cpu(tx_desc->tx.outer_vlan_tag));
	dev_info(dev, "(TX)tv: %u\n", le16_to_cpu(tx_desc->tx.tv));
	dev_info(dev, "(TX)paylen_ol4cs: %u\n",
		 le32_to_cpu(tx_desc->tx.paylen_ol4cs));
	dev_info(dev, "(TX)vld_ra_ri: %u\n",
		 le16_to_cpu(tx_desc->tx.bdtp_fe_sc_vld_ra_ri));
	dev_info(dev, "(TX)mss_hw_csum: %u\n", mss_hw_csum);

	ring = &priv->ring[q_num + h->kinfo.num_tqps];
	value = readl_relaxed(ring->tqp->io_base + HNS3_RING_RX_RING_TAIL_REG);
	rx_index = (cnt == 1) ? value : tx_index;
	rx_desc = &ring->desc[rx_index];

	addr = le64_to_cpu(rx_desc->addr);
	l234info = le32_to_cpu(rx_desc->rx.l234_info);
	dev_info(dev, "RX Queue Num: %u, BD Index: %u\n", q_num, rx_index);
	dev_info(dev, "(RX)addr: %pad\n", &addr);
	dev_info(dev, "(RX)l234_info: %u\n", l234info);

	dev_info(dev, "(RX)pkt_len: %u\n", le16_to_cpu(rx_desc->rx.pkt_len));
	dev_info(dev, "(RX)size: %u\n", le16_to_cpu(rx_desc->rx.size));
	dev_info(dev, "(RX)rss_hash: %u\n", le32_to_cpu(rx_desc->rx.rss_hash));
	dev_info(dev, "(RX)fd_id: %u\n", le16_to_cpu(rx_desc->rx.fd_id));
	dev_info(dev, "(RX)vlan_tag: %u\n", le16_to_cpu(rx_desc->rx.vlan_tag));
	dev_info(dev, "(RX)o_dm_vlan_id_fb: %u\n",
		 le16_to_cpu(rx_desc->rx.o_dm_vlan_id_fb));
	dev_info(dev, "(RX)ot_vlan_tag: %u\n",
		 le16_to_cpu(rx_desc->rx.ot_vlan_tag));
	dev_info(dev, "(RX)bd_base_info: %u\n",
		 le32_to_cpu(rx_desc->rx.bd_base_info));

	return 0;
}

static void hns3_dbg_help(struct hnae3_handle *h)
{
#define HNS3_DBG_BUF_LEN 256

	char printf_buf[HNS3_DBG_BUF_LEN];

	dev_info(&h->pdev->dev, "available commands\n");
	dev_info(&h->pdev->dev, "queue info <number>\n");
	dev_info(&h->pdev->dev, "queue map\n");
	dev_info(&h->pdev->dev, "bd info <q_num> <bd index>\n");

	if (!hns3_is_phys_func(h->pdev))
		return;

	dev_info(&h->pdev->dev, "dump fd tcam\n");
	dev_info(&h->pdev->dev, "dump tc\n");
	dev_info(&h->pdev->dev, "dump tm map <q_num>\n");
	dev_info(&h->pdev->dev, "dump tm\n");
	dev_info(&h->pdev->dev, "dump qos pause cfg\n");
	dev_info(&h->pdev->dev, "dump qos pri map\n");
	dev_info(&h->pdev->dev, "dump qos buf cfg\n");
	dev_info(&h->pdev->dev, "dump mng tbl\n");
	dev_info(&h->pdev->dev, "dump reset info\n");
	dev_info(&h->pdev->dev, "dump m7 info\n");
	dev_info(&h->pdev->dev, "dump ncl_config <offset> <length>(in hex)\n");
	dev_info(&h->pdev->dev, "dump mac tnl status\n");
	dev_info(&h->pdev->dev, "dump loopback\n");
	dev_info(&h->pdev->dev, "dump qs shaper [qs id]\n");
	dev_info(&h->pdev->dev, "dump uc mac list <func id>\n");
	dev_info(&h->pdev->dev, "dump mc mac list <func id>\n");
	dev_info(&h->pdev->dev, "dump intr\n");

	memset(printf_buf, 0, HNS3_DBG_BUF_LEN);
	strncat(printf_buf, "dump reg [[bios common] [ssu <port_id>]",
		HNS3_DBG_BUF_LEN - 1);
	strncat(printf_buf + strlen(printf_buf),
		" [igu egu <port_id>] [rpu <tc_queue_num>]",
		HNS3_DBG_BUF_LEN - strlen(printf_buf) - 1);
	strncat(printf_buf + strlen(printf_buf),
		" [rtc] [ppp] [rcb] [tqp <queue_num>] [mac]]\n",
		HNS3_DBG_BUF_LEN - strlen(printf_buf) - 1);
	dev_info(&h->pdev->dev, "%s", printf_buf);

	memset(printf_buf, 0, HNS3_DBG_BUF_LEN);
	strncat(printf_buf, "dump reg dcb <port_id> <pri_id> <pg_id>",
		HNS3_DBG_BUF_LEN - 1);
	strncat(printf_buf + strlen(printf_buf), " <rq_id> <nq_id> <qset_id>\n",
		HNS3_DBG_BUF_LEN - strlen(printf_buf) - 1);
	dev_info(&h->pdev->dev, "%s", printf_buf);
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
	else if (strncmp(cmd_buf, "bd info", 7) == 0)
		ret = hns3_dbg_bd_info(handle, cmd_buf);
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
};

static int hns3_dbg_read_cmd(struct hnae3_handle *handle,
			     enum hnae3_dbg_cmd cmd, char *buf, int len)
{
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(hns3_dbg_cmd_func); i++) {
		if (cmd == hns3_dbg_cmd_func[i].cmd)
			return hns3_dbg_cmd_func[i].dbg_dump(handle, buf, len);
	}

	if (!ops->dbg_read_cmd)
		return -EOPNOTSUPP;

	return ops->dbg_read_cmd(handle, cmd, buf, len);
}

static ssize_t hns3_dbg_read(struct file *filp, char __user *buffer,
			     size_t count, loff_t *ppos)
{
	struct hnae3_handle *handle = filp->private_data;
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
		ret = hns3_dbg_read_cmd(handle, hns3_dbg_cmd[index].cmd,
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

static int
hns3_dbg_common_file_init(struct hnae3_handle *handle, u32 cmd)
{
	struct dentry *entry_dir;

	entry_dir = hns3_dbg_dentry[hns3_dbg_cmd[cmd].dentry].dentry;
	debugfs_create_file(hns3_dbg_cmd[cmd].name, 0400, entry_dir,
			    handle, &hns3_dbg_fops);

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
