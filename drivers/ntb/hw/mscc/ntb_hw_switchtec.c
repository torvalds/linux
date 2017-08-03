/*
 * Microsemi Switchtec(tm) PCIe Management Driver
 * Copyright (c) 2017, Microsemi Corporation
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

#include <linux/switchtec.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kthread.h>

MODULE_DESCRIPTION("Microsemi Switchtec(tm) NTB Driver");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Microsemi Corporation");

static bool use_lut_mws;
module_param(use_lut_mws, bool, 0644);
MODULE_PARM_DESC(use_lut_mws,
		 "Enable the use of the LUT based memory windows");

#ifndef ioread64
#ifdef readq
#define ioread64 readq
#else
#define ioread64 _ioread64
static inline u64 _ioread64(void __iomem *mmio)
{
	u64 low, high;

	low = ioread32(mmio);
	high = ioread32(mmio + sizeof(u32));
	return low | (high << 32);
}
#endif
#endif

#ifndef iowrite64
#ifdef writeq
#define iowrite64 writeq
#else
#define iowrite64 _iowrite64
static inline void _iowrite64(u64 val, void __iomem *mmio)
{
	iowrite32(val, mmio);
	iowrite32(val >> 32, mmio + sizeof(u32));
}
#endif
#endif

#define SWITCHTEC_NTB_MAGIC 0x45CC0001
#define MAX_MWS     128

struct shared_mw {
	u32 magic;
	u32 partition_id;
	u64 mw_sizes[MAX_MWS];
};

#define MAX_DIRECT_MW ARRAY_SIZE(((struct ntb_ctrl_regs *)(0))->bar_entry)
#define LUT_SIZE SZ_64K

struct switchtec_ntb {
	struct switchtec_dev *stdev;

	int self_partition;
	int peer_partition;

	struct ntb_info_regs __iomem *mmio_ntb;
	struct ntb_ctrl_regs __iomem *mmio_ctrl;
	struct ntb_dbmsg_regs __iomem *mmio_dbmsg;
	struct ntb_ctrl_regs __iomem *mmio_self_ctrl;
	struct ntb_ctrl_regs __iomem *mmio_peer_ctrl;
	struct ntb_dbmsg_regs __iomem *mmio_self_dbmsg;

	struct shared_mw *self_shared;
	struct shared_mw __iomem *peer_shared;
	dma_addr_t self_shared_dma;

	int nr_direct_mw;
	int nr_lut_mw;
	int direct_mw_to_bar[MAX_DIRECT_MW];

	int peer_nr_direct_mw;
	int peer_nr_lut_mw;
	int peer_direct_mw_to_bar[MAX_DIRECT_MW];
};

static int switchtec_ntb_part_op(struct switchtec_ntb *sndev,
				 struct ntb_ctrl_regs __iomem *ctl,
				 u32 op, int wait_status)
{
	static const char * const op_text[] = {
		[NTB_CTRL_PART_OP_LOCK] = "lock",
		[NTB_CTRL_PART_OP_CFG] = "configure",
		[NTB_CTRL_PART_OP_RESET] = "reset",
	};

	int i;
	u32 ps;
	int status;

	switch (op) {
	case NTB_CTRL_PART_OP_LOCK:
		status = NTB_CTRL_PART_STATUS_LOCKING;
		break;
	case NTB_CTRL_PART_OP_CFG:
		status = NTB_CTRL_PART_STATUS_CONFIGURING;
		break;
	case NTB_CTRL_PART_OP_RESET:
		status = NTB_CTRL_PART_STATUS_RESETTING;
		break;
	default:
		return -EINVAL;
	}

	iowrite32(op, &ctl->partition_op);

	for (i = 0; i < 1000; i++) {
		if (msleep_interruptible(50) != 0) {
			iowrite32(NTB_CTRL_PART_OP_RESET, &ctl->partition_op);
			return -EINTR;
		}

		ps = ioread32(&ctl->partition_status) & 0xFFFF;

		if (ps != status)
			break;
	}

	if (ps == wait_status)
		return 0;

	if (ps == status) {
		dev_err(&sndev->stdev->dev,
			"Timed out while peforming %s (%d). (%08x)",
			op_text[op], op,
			ioread32(&ctl->partition_status));

		return -ETIMEDOUT;
	}

	return -EIO;
}

static void switchtec_ntb_init_sndev(struct switchtec_ntb *sndev)
{
	u64 part_map;

	sndev->self_partition = sndev->stdev->partition;

	sndev->mmio_ntb = sndev->stdev->mmio_ntb;
	part_map = ioread64(&sndev->mmio_ntb->ep_map);
	part_map &= ~(1 << sndev->self_partition);
	sndev->peer_partition = ffs(part_map) - 1;

	dev_dbg(&sndev->stdev->dev, "Partition ID %d of %d (%llx)",
		sndev->self_partition, sndev->stdev->partition_count,
		part_map);

	sndev->mmio_ctrl = (void * __iomem)sndev->mmio_ntb +
		SWITCHTEC_NTB_REG_CTRL_OFFSET;
	sndev->mmio_dbmsg = (void * __iomem)sndev->mmio_ntb +
		SWITCHTEC_NTB_REG_DBMSG_OFFSET;

	sndev->mmio_self_ctrl = &sndev->mmio_ctrl[sndev->self_partition];
	sndev->mmio_peer_ctrl = &sndev->mmio_ctrl[sndev->peer_partition];
	sndev->mmio_self_dbmsg = &sndev->mmio_dbmsg[sndev->self_partition];
}

static int map_bars(int *map, struct ntb_ctrl_regs __iomem *ctrl)
{
	int i;
	int cnt = 0;

	for (i = 0; i < ARRAY_SIZE(ctrl->bar_entry); i++) {
		u32 r = ioread32(&ctrl->bar_entry[i].ctl);

		if (r & NTB_CTRL_BAR_VALID)
			map[cnt++] = i;
	}

	return cnt;
}

static void switchtec_ntb_init_mw(struct switchtec_ntb *sndev)
{
	sndev->nr_direct_mw = map_bars(sndev->direct_mw_to_bar,
				       sndev->mmio_self_ctrl);

	sndev->nr_lut_mw = ioread16(&sndev->mmio_self_ctrl->lut_table_entries);
	sndev->nr_lut_mw = rounddown_pow_of_two(sndev->nr_lut_mw);

	dev_dbg(&sndev->stdev->dev, "MWs: %d direct, %d lut",
		sndev->nr_direct_mw, sndev->nr_lut_mw);

	sndev->peer_nr_direct_mw = map_bars(sndev->peer_direct_mw_to_bar,
					    sndev->mmio_peer_ctrl);

	sndev->peer_nr_lut_mw =
		ioread16(&sndev->mmio_peer_ctrl->lut_table_entries);
	sndev->peer_nr_lut_mw = rounddown_pow_of_two(sndev->peer_nr_lut_mw);

	dev_dbg(&sndev->stdev->dev, "Peer MWs: %d direct, %d lut",
		sndev->peer_nr_direct_mw, sndev->peer_nr_lut_mw);

}

static int switchtec_ntb_init_req_id_table(struct switchtec_ntb *sndev)
{
	int rc = 0;
	u16 req_id;
	u32 error;

	req_id = ioread16(&sndev->mmio_ntb->requester_id);

	if (ioread32(&sndev->mmio_self_ctrl->req_id_table_size) < 2) {
		dev_err(&sndev->stdev->dev,
			"Not enough requester IDs available.");
		return -EFAULT;
	}

	rc = switchtec_ntb_part_op(sndev, sndev->mmio_self_ctrl,
				   NTB_CTRL_PART_OP_LOCK,
				   NTB_CTRL_PART_STATUS_LOCKED);
	if (rc)
		return rc;

	iowrite32(NTB_PART_CTRL_ID_PROT_DIS,
		  &sndev->mmio_self_ctrl->partition_ctrl);

	/*
	 * Root Complex Requester ID (which is 0:00.0)
	 */
	iowrite32(0 << 16 | NTB_CTRL_REQ_ID_EN,
		  &sndev->mmio_self_ctrl->req_id_table[0]);

	/*
	 * Host Bridge Requester ID (as read from the mmap address)
	 */
	iowrite32(req_id << 16 | NTB_CTRL_REQ_ID_EN,
		  &sndev->mmio_self_ctrl->req_id_table[1]);

	rc = switchtec_ntb_part_op(sndev, sndev->mmio_self_ctrl,
				   NTB_CTRL_PART_OP_CFG,
				   NTB_CTRL_PART_STATUS_NORMAL);
	if (rc == -EIO) {
		error = ioread32(&sndev->mmio_self_ctrl->req_id_error);
		dev_err(&sndev->stdev->dev,
			"Error setting up the requester ID table: %08x",
			error);
	}

	return rc;
}

static void switchtec_ntb_init_shared(struct switchtec_ntb *sndev)
{
	int i;

	memset(sndev->self_shared, 0, LUT_SIZE);
	sndev->self_shared->magic = SWITCHTEC_NTB_MAGIC;
	sndev->self_shared->partition_id = sndev->stdev->partition;

	for (i = 0; i < sndev->nr_direct_mw; i++) {
		int bar = sndev->direct_mw_to_bar[i];
		resource_size_t sz = pci_resource_len(sndev->stdev->pdev, bar);

		if (i == 0)
			sz = min_t(resource_size_t, sz,
				   LUT_SIZE * sndev->nr_lut_mw);

		sndev->self_shared->mw_sizes[i] = sz;
	}

	for (i = 0; i < sndev->nr_lut_mw; i++) {
		int idx = sndev->nr_direct_mw + i;

		sndev->self_shared->mw_sizes[idx] = LUT_SIZE;
	}
}

static int switchtec_ntb_init_shared_mw(struct switchtec_ntb *sndev)
{
	struct ntb_ctrl_regs __iomem *ctl = sndev->mmio_peer_ctrl;
	int bar = sndev->direct_mw_to_bar[0];
	u32 ctl_val;
	int rc;

	sndev->self_shared = dma_zalloc_coherent(&sndev->stdev->pdev->dev,
						 LUT_SIZE,
						 &sndev->self_shared_dma,
						 GFP_KERNEL);
	if (!sndev->self_shared) {
		dev_err(&sndev->stdev->dev,
			"unable to allocate memory for shared mw");
		return -ENOMEM;
	}

	switchtec_ntb_init_shared(sndev);

	rc = switchtec_ntb_part_op(sndev, ctl, NTB_CTRL_PART_OP_LOCK,
				   NTB_CTRL_PART_STATUS_LOCKED);
	if (rc)
		goto unalloc_and_exit;

	ctl_val = ioread32(&ctl->bar_entry[bar].ctl);
	ctl_val &= 0xFF;
	ctl_val |= NTB_CTRL_BAR_LUT_WIN_EN;
	ctl_val |= ilog2(LUT_SIZE) << 8;
	ctl_val |= (sndev->nr_lut_mw - 1) << 14;
	iowrite32(ctl_val, &ctl->bar_entry[bar].ctl);

	iowrite64((NTB_CTRL_LUT_EN | (sndev->self_partition << 1) |
		   sndev->self_shared_dma),
		  &ctl->lut_entry[0]);

	rc = switchtec_ntb_part_op(sndev, ctl, NTB_CTRL_PART_OP_CFG,
				   NTB_CTRL_PART_STATUS_NORMAL);
	if (rc) {
		u32 bar_error, lut_error;

		bar_error = ioread32(&ctl->bar_error);
		lut_error = ioread32(&ctl->lut_error);
		dev_err(&sndev->stdev->dev,
			"Error setting up shared MW: %08x / %08x",
			bar_error, lut_error);
		goto unalloc_and_exit;
	}

	sndev->peer_shared = pci_iomap(sndev->stdev->pdev, bar, LUT_SIZE);
	if (!sndev->peer_shared) {
		rc = -ENOMEM;
		goto unalloc_and_exit;
	}

	dev_dbg(&sndev->stdev->dev, "Shared MW Ready");
	return 0;

unalloc_and_exit:
	dma_free_coherent(&sndev->stdev->pdev->dev, LUT_SIZE,
			  sndev->self_shared, sndev->self_shared_dma);

	return rc;
}

static void switchtec_ntb_deinit_shared_mw(struct switchtec_ntb *sndev)
{
	if (sndev->peer_shared)
		pci_iounmap(sndev->stdev->pdev, sndev->peer_shared);

	if (sndev->self_shared)
		dma_free_coherent(&sndev->stdev->pdev->dev, LUT_SIZE,
				  sndev->self_shared,
				  sndev->self_shared_dma);
}

static int switchtec_ntb_add(struct device *dev,
			     struct class_interface *class_intf)
{
	struct switchtec_dev *stdev = to_stdev(dev);
	struct switchtec_ntb *sndev;
	int rc;

	stdev->sndev = NULL;

	if (stdev->pdev->class != MICROSEMI_NTB_CLASSCODE)
		return -ENODEV;

	if (stdev->partition_count != 2)
		dev_warn(dev, "ntb driver only supports 2 partitions");

	sndev = kzalloc_node(sizeof(*sndev), GFP_KERNEL, dev_to_node(dev));
	if (!sndev)
		return -ENOMEM;

	sndev->stdev = stdev;

	switchtec_ntb_init_sndev(sndev);
	switchtec_ntb_init_mw(sndev);

	rc = switchtec_ntb_init_req_id_table(sndev);
	if (rc)
		goto free_and_exit;

	rc = switchtec_ntb_init_shared_mw(sndev);
	if (rc)
		goto free_and_exit;

	stdev->sndev = sndev;
	dev_info(dev, "NTB device registered");

	return 0;

free_and_exit:
	kfree(sndev);
	dev_err(dev, "failed to register ntb device: %d", rc);
	return rc;
}

void switchtec_ntb_remove(struct device *dev,
			  struct class_interface *class_intf)
{
	struct switchtec_dev *stdev = to_stdev(dev);
	struct switchtec_ntb *sndev = stdev->sndev;

	if (!sndev)
		return;

	stdev->sndev = NULL;
	switchtec_ntb_deinit_shared_mw(sndev);
	kfree(sndev);
	dev_info(dev, "ntb device unregistered");
}

static struct class_interface switchtec_interface  = {
	.add_dev = switchtec_ntb_add,
	.remove_dev = switchtec_ntb_remove,
};

static int __init switchtec_ntb_init(void)
{
	switchtec_interface.class = switchtec_class;
	return class_interface_register(&switchtec_interface);
}
module_init(switchtec_ntb_init);

static void __exit switchtec_ntb_exit(void)
{
	class_interface_unregister(&switchtec_interface);
}
module_exit(switchtec_ntb_exit);
