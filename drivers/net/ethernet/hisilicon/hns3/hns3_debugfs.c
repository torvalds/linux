// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2018-2019 Hisilicon Limited. */

#include <linux/debugfs.h>
#include <linux/device.h>

#include "hnae3.h"
#include "hns3_enet.h"

#define HNS3_DBG_READ_LEN 256
#define HNS3_DBG_WRITE_LEN 1024

static struct dentry *hns3_dbgfs_root;

static int hns3_dbg_queue_info(struct hnae3_handle *h,
			       const char *cmd_buf)
{
	struct hns3_nic_priv *priv = h->priv;
	struct hns3_enet_ring *ring;
	u32 base_add_l, base_add_h;
	u32 queue_num, queue_max;
	u32 value, i = 0;
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
		dev_info(&h->pdev->dev, "TX(%u) RING PKTNUM: %u\n\n", i,
			 value);
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
			 "      %4d            %4d            %4d\n",
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

	ring  = &priv->ring[q_num];
	value = readl_relaxed(ring->tqp->io_base + HNS3_RING_TX_RING_TAIL_REG);
	tx_index = (cnt == 1) ? value : tx_index;

	if (tx_index >= ring->desc_num) {
		dev_err(dev, "bd index(%u) is out of range(0-%u)\n", tx_index,
			ring->desc_num - 1);
		return -EINVAL;
	}

	tx_desc = &ring->desc[tx_index];
	addr = le64_to_cpu(tx_desc->addr);
	dev_info(dev, "TX Queue Num: %u, BD Index: %u\n", q_num, tx_index);
	dev_info(dev, "(TX)addr: %pad\n", &addr);
	dev_info(dev, "(TX)vlan_tag: %u\n", le16_to_cpu(tx_desc->tx.vlan_tag));
	dev_info(dev, "(TX)send_size: %u\n",
		 le16_to_cpu(tx_desc->tx.send_size));
	dev_info(dev, "(TX)vlan_tso: %u\n", tx_desc->tx.type_cs_vlan_tso);
	dev_info(dev, "(TX)l2_len: %u\n", tx_desc->tx.l2_len);
	dev_info(dev, "(TX)l3_len: %u\n", tx_desc->tx.l3_len);
	dev_info(dev, "(TX)l4_len: %u\n", tx_desc->tx.l4_len);
	dev_info(dev, "(TX)vlan_tag: %u\n",
		 le16_to_cpu(tx_desc->tx.outer_vlan_tag));
	dev_info(dev, "(TX)tv: %u\n", le16_to_cpu(tx_desc->tx.tv));
	dev_info(dev, "(TX)vlan_msec: %u\n", tx_desc->tx.ol_type_vlan_msec);
	dev_info(dev, "(TX)ol2_len: %u\n", tx_desc->tx.ol2_len);
	dev_info(dev, "(TX)ol3_len: %u\n", tx_desc->tx.ol3_len);
	dev_info(dev, "(TX)ol4_len: %u\n", tx_desc->tx.ol4_len);
	dev_info(dev, "(TX)paylen: %u\n", le32_to_cpu(tx_desc->tx.paylen));
	dev_info(dev, "(TX)vld_ra_ri: %u\n",
		 le16_to_cpu(tx_desc->tx.bdtp_fe_sc_vld_ra_ri));
	dev_info(dev, "(TX)mss: %u\n", le16_to_cpu(tx_desc->tx.mss));

	ring  = &priv->ring[q_num + h->kinfo.num_tqps];
	value = readl_relaxed(ring->tqp->io_base + HNS3_RING_RX_RING_TAIL_REG);
	rx_index = (cnt == 1) ? value : tx_index;
	rx_desc	 = &ring->desc[rx_index];

	addr = le64_to_cpu(rx_desc->addr);
	dev_info(dev, "RX Queue Num: %u, BD Index: %u\n", q_num, rx_index);
	dev_info(dev, "(RX)addr: %pad\n", &addr);
	dev_info(dev, "(RX)l234_info: %u\n",
		 le32_to_cpu(rx_desc->rx.l234_info));
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

	memset(printf_buf, 0, HNS3_DBG_BUF_LEN);
	strncat(printf_buf, "dump reg [[bios common] [ssu <port_id>]",
		HNS3_DBG_BUF_LEN - 1);
	strncat(printf_buf + strlen(printf_buf),
		" [igu egu <port_id>] [rpu <tc_queue_num>]",
		HNS3_DBG_BUF_LEN - strlen(printf_buf) - 1);
	strncat(printf_buf + strlen(printf_buf),
		" [rtc] [ppp] [rcb] [tqp <queue_num>]]\n",
		HNS3_DBG_BUF_LEN - strlen(printf_buf) - 1);
	dev_info(&h->pdev->dev, "%s", printf_buf);

	memset(printf_buf, 0, HNS3_DBG_BUF_LEN);
	strncat(printf_buf, "dump reg dcb <port_id> <pri_id> <pg_id>",
		HNS3_DBG_BUF_LEN - 1);
	strncat(printf_buf + strlen(printf_buf), " <rq_id> <nq_id> <qset_id>\n",
		HNS3_DBG_BUF_LEN - strlen(printf_buf) - 1);
	dev_info(&h->pdev->dev, "%s", printf_buf);
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

static ssize_t hns3_dbg_cmd_write(struct file *filp, const char __user *buffer,
				  size_t count, loff_t *ppos)
{
	struct hnae3_handle *handle = filp->private_data;
	struct hns3_nic_priv *priv  = handle->priv;
	char *cmd_buf, *cmd_buf_tmp;
	int uncopied_bytes;
	int ret = 0;

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

	if (ret)
		hns3_dbg_help(handle);

	kfree(cmd_buf);
	cmd_buf = NULL;

	return count;
}

static const struct file_operations hns3_dbg_cmd_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = hns3_dbg_cmd_read,
	.write = hns3_dbg_cmd_write,
};

void hns3_dbg_init(struct hnae3_handle *handle)
{
	const char *name = pci_name(handle->pdev);

	handle->hnae3_dbgfs = debugfs_create_dir(name, hns3_dbgfs_root);

	debugfs_create_file("cmd", 0600, handle->hnae3_dbgfs, handle,
			    &hns3_dbg_cmd_fops);
}

void hns3_dbg_uninit(struct hnae3_handle *handle)
{
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
