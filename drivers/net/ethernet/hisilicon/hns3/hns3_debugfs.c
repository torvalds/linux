// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2018-2019 Hisilicon Limited. */

#include <linux/debugfs.h>
#include <linux/device.h>

#include "hnae3.h"
#include "hns3_enet.h"

#define HNS3_DBG_READ_LEN 256

static struct dentry *hns3_dbgfs_root;

static int hns3_dbg_queue_info(struct hnae3_handle *h, char *cmd_buf)
{
	struct hns3_nic_priv *priv = h->priv;
	struct hns3_nic_ring_data *ring_data;
	struct hns3_enet_ring *ring;
	u32 base_add_l, base_add_h;
	u32 queue_num, queue_max;
	u32 value, i = 0;
	int cnt;

	if (!priv->ring_data) {
		dev_err(&h->pdev->dev, "ring_data is NULL\n");
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
			"Queue number(%u) is out of range(%u)\n", queue_num,
			h->kinfo.num_tqps - 1);
		return -EINVAL;
	}

	ring_data = priv->ring_data;
	for (i = queue_num; i < queue_max; i++) {
		/* Each cycle needs to determine whether the instance is reset,
		 * to prevent reference to invalid memory. And need to ensure
		 * that the following code is executed within 100ms.
		 */
		if (test_bit(HNS3_NIC_STATE_INITED, &priv->state) ||
		    test_bit(HNS3_NIC_STATE_RESETTING, &priv->state))
			return -EPERM;

		ring = ring_data[i + h->kinfo.num_tqps].ring;
		base_add_h = readl_relaxed(ring->tqp->io_base +
					   HNS3_RING_RX_RING_BASEADDR_H_REG);
		base_add_l = readl_relaxed(ring->tqp->io_base +
					   HNS3_RING_RX_RING_BASEADDR_L_REG);
		dev_info(&h->pdev->dev, "RX(%d) BASE ADD: 0x%08x%08x\n", i,
			 base_add_h, base_add_l);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_RX_RING_BD_NUM_REG);
		dev_info(&h->pdev->dev, "RX(%d) RING BD NUM: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_RX_RING_BD_LEN_REG);
		dev_info(&h->pdev->dev, "RX(%d) RING BD LEN: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_RX_RING_TAIL_REG);
		dev_info(&h->pdev->dev, "RX(%d) RING TAIL: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_RX_RING_HEAD_REG);
		dev_info(&h->pdev->dev, "RX(%d) RING HEAD: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_RX_RING_FBDNUM_REG);
		dev_info(&h->pdev->dev, "RX(%d) RING FBDNUM: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_RX_RING_PKTNUM_RECORD_REG);
		dev_info(&h->pdev->dev, "RX(%d) RING PKTNUM: %u\n", i, value);

		ring = ring_data[i].ring;
		base_add_h = readl_relaxed(ring->tqp->io_base +
					   HNS3_RING_TX_RING_BASEADDR_H_REG);
		base_add_l = readl_relaxed(ring->tqp->io_base +
					   HNS3_RING_TX_RING_BASEADDR_L_REG);
		dev_info(&h->pdev->dev, "TX(%d) BASE ADD: 0x%08x%08x\n", i,
			 base_add_h, base_add_l);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_BD_NUM_REG);
		dev_info(&h->pdev->dev, "TX(%d) RING BD NUM: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_TC_REG);
		dev_info(&h->pdev->dev, "TX(%d) RING TC: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_TAIL_REG);
		dev_info(&h->pdev->dev, "TX(%d) RING TAIL: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_HEAD_REG);
		dev_info(&h->pdev->dev, "TX(%d) RING HEAD: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_FBDNUM_REG);
		dev_info(&h->pdev->dev, "TX(%d) RING FBDNUM: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_OFFSET_REG);
		dev_info(&h->pdev->dev, "TX(%d) RING OFFSET: %u\n", i, value);

		value = readl_relaxed(ring->tqp->io_base +
				      HNS3_RING_TX_RING_PKTNUM_RECORD_REG);
		dev_info(&h->pdev->dev, "TX(%d) RING PKTNUM: %u\n\n", i,
			 value);
	}

	return 0;
}

static void hns3_dbg_help(struct hnae3_handle *h)
{
	dev_info(&h->pdev->dev, "available commands\n");
	dev_info(&h->pdev->dev, "queue info [number]\n");
	dev_info(&h->pdev->dev, "dump fd tcam\n");
	dev_info(&h->pdev->dev, "dump tc\n");
	dev_info(&h->pdev->dev, "dump tm\n");
	dev_info(&h->pdev->dev, "dump qos pause cfg\n");
	dev_info(&h->pdev->dev, "dump qos pri map\n");
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

	len = snprintf(buf, HNS3_DBG_READ_LEN, "%s\n",
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
	if (test_bit(HNS3_NIC_STATE_INITED, &priv->state) ||
	    test_bit(HNS3_NIC_STATE_RESETTING, &priv->state))
		return 0;

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
	else if (handle->ae_algo->ops->dbg_run_cmd)
		ret = handle->ae_algo->ops->dbg_run_cmd(handle, cmd_buf);

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
	struct dentry *pfile;

	handle->hnae3_dbgfs = debugfs_create_dir(name, hns3_dbgfs_root);
	if (!handle->hnae3_dbgfs)
		return;

	pfile = debugfs_create_file("cmd", 0600, handle->hnae3_dbgfs, handle,
				    &hns3_dbg_cmd_fops);
	if (!pfile) {
		debugfs_remove_recursive(handle->hnae3_dbgfs);
		handle->hnae3_dbgfs = NULL;
		dev_warn(&handle->pdev->dev, "create file for %s fail\n",
			 name);
	}
}

void hns3_dbg_uninit(struct hnae3_handle *handle)
{
	debugfs_remove_recursive(handle->hnae3_dbgfs);
	handle->hnae3_dbgfs = NULL;
}

void hns3_dbg_register_debugfs(const char *debugfs_dir_name)
{
	hns3_dbgfs_root = debugfs_create_dir(debugfs_dir_name, NULL);
	if (!hns3_dbgfs_root) {
		pr_warn("Register debugfs for %s fail\n", debugfs_dir_name);
		return;
	}
}

void hns3_dbg_unregister_debugfs(void)
{
	debugfs_remove_recursive(hns3_dbgfs_root);
	hns3_dbgfs_root = NULL;
}
