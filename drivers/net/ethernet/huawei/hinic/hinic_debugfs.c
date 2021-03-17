// SPDX-License-Identifier: GPL-2.0-only
/* Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#include <linux/debugfs.h>
#include <linux/device.h>

#include "hinic_debugfs.h"

static struct dentry *hinic_dbgfs_root;

enum sq_dbg_info {
	GLB_SQ_ID,
	SQ_PI,
	SQ_CI,
	SQ_FI,
	SQ_MSIX_ENTRY,
};

static char *sq_fields[] = {"glb_sq_id", "sq_pi", "sq_ci", "sq_fi", "sq_msix_entry"};

static u64 hinic_dbg_get_sq_info(struct hinic_dev *nic_dev, struct hinic_sq *sq, int idx)
{
	struct hinic_wq *wq = sq->wq;

	switch (idx) {
	case GLB_SQ_ID:
		return nic_dev->hwdev->func_to_io.global_qpn + sq->qid;
	case SQ_PI:
		return atomic_read(&wq->prod_idx) & wq->mask;
	case SQ_CI:
		return atomic_read(&wq->cons_idx) & wq->mask;
	case SQ_FI:
		return be16_to_cpu(*(__be16 *)(sq->hw_ci_addr)) & wq->mask;
	case SQ_MSIX_ENTRY:
		return sq->msix_entry;
	}

	return 0;
}

enum rq_dbg_info {
	GLB_RQ_ID,
	RQ_HW_PI,
	RQ_SW_CI,
	RQ_SW_PI,
	RQ_MSIX_ENTRY,
};

static char *rq_fields[] = {"glb_rq_id", "rq_hw_pi", "rq_sw_ci", "rq_sw_pi", "rq_msix_entry"};

static u64 hinic_dbg_get_rq_info(struct hinic_dev *nic_dev, struct hinic_rq *rq, int idx)
{
	struct hinic_wq *wq = rq->wq;

	switch (idx) {
	case GLB_RQ_ID:
		return nic_dev->hwdev->func_to_io.global_qpn + rq->qid;
	case RQ_HW_PI:
		return be16_to_cpu(*(__be16 *)(rq->pi_virt_addr)) & wq->mask;
	case RQ_SW_CI:
		return atomic_read(&wq->cons_idx) & wq->mask;
	case RQ_SW_PI:
		return atomic_read(&wq->prod_idx) & wq->mask;
	case RQ_MSIX_ENTRY:
		return rq->msix_entry;
	}

	return 0;
}

enum func_tbl_info {
	VALID,
	RX_MODE,
	MTU,
	RQ_DEPTH,
	QUEUE_NUM,
};

static char *func_table_fields[] = {"valid", "rx_mode", "mtu", "rq_depth", "cfg_q_num"};

static int hinic_dbg_get_func_table(struct hinic_dev *nic_dev, int idx)
{
	struct tag_sml_funcfg_tbl *funcfg_table_elem;
	struct hinic_cmd_lt_rd *read_data;
	u16 out_size = sizeof(*read_data);
	int err;

	read_data = kzalloc(sizeof(*read_data), GFP_KERNEL);
	if (!read_data)
		return ~0;

	read_data->node = TBL_ID_FUNC_CFG_SM_NODE;
	read_data->inst = TBL_ID_FUNC_CFG_SM_INST;
	read_data->entry_size = HINIC_FUNCTION_CONFIGURE_TABLE_SIZE;
	read_data->lt_index = HINIC_HWIF_FUNC_IDX(nic_dev->hwdev->hwif);
	read_data->len = HINIC_FUNCTION_CONFIGURE_TABLE_SIZE;

	err = hinic_port_msg_cmd(nic_dev->hwdev, HINIC_PORT_CMD_RD_LINE_TBL, read_data,
				 sizeof(*read_data), read_data, &out_size);
	if (err || out_size != sizeof(*read_data) || read_data->status) {
		netif_err(nic_dev, drv, nic_dev->netdev,
			  "Failed to get func table, err: %d, status: 0x%x, out size: 0x%x\n",
			  err, read_data->status, out_size);
		kfree(read_data);
		return ~0;
	}

	funcfg_table_elem = (struct tag_sml_funcfg_tbl *)read_data->data;

	switch (idx) {
	case VALID:
		return funcfg_table_elem->dw0.bs.valid;
	case RX_MODE:
		return funcfg_table_elem->dw0.bs.nic_rx_mode;
	case MTU:
		return funcfg_table_elem->dw1.bs.mtu;
	case RQ_DEPTH:
		return funcfg_table_elem->dw13.bs.cfg_rq_depth;
	case QUEUE_NUM:
		return funcfg_table_elem->dw13.bs.cfg_q_num;
	}

	kfree(read_data);

	return ~0;
}

static ssize_t hinic_dbg_cmd_read(struct file *filp, char __user *buffer, size_t count,
				  loff_t *ppos)
{
	struct hinic_debug_priv *dbg;
	char ret_buf[20];
	int *desc;
	u64 out;
	int ret;

	desc = filp->private_data;
	dbg = container_of(desc, struct hinic_debug_priv, field_id[*desc]);

	switch (dbg->type) {
	case HINIC_DBG_SQ_INFO:
		out = hinic_dbg_get_sq_info(dbg->dev, dbg->object, *desc);
		break;

	case HINIC_DBG_RQ_INFO:
		out = hinic_dbg_get_rq_info(dbg->dev, dbg->object, *desc);
		break;

	case HINIC_DBG_FUNC_TABLE:
		out = hinic_dbg_get_func_table(dbg->dev, *desc);
		break;

	default:
		netif_warn(dbg->dev, drv, dbg->dev->netdev, "Invalid hinic debug cmd: %d\n",
			   dbg->type);
		return -EINVAL;
	}

	ret = snprintf(ret_buf, sizeof(ret_buf), "0x%llx\n", out);

	return simple_read_from_buffer(buffer, count, ppos, ret_buf, ret);
}

static const struct file_operations hinic_dbg_cmd_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = hinic_dbg_cmd_read,
};

static int create_dbg_files(struct hinic_dev *dev, enum hinic_dbg_type type, void *data,
			    struct dentry *root, struct hinic_debug_priv **dbg, char **field,
			    int nfile)
{
	struct hinic_debug_priv *tmp;
	int i;

	tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	tmp->dev = dev;
	tmp->object = data;
	tmp->type = type;
	tmp->root = root;

	for (i = 0; i < nfile; i++) {
		tmp->field_id[i] = i;
		debugfs_create_file(field[i], 0400, root, &tmp->field_id[i], &hinic_dbg_cmd_fops);
	}

	*dbg = tmp;

	return 0;
}

static void rem_dbg_files(struct hinic_debug_priv *dbg)
{
	if (dbg->type != HINIC_DBG_FUNC_TABLE)
		debugfs_remove_recursive(dbg->root);

	kfree(dbg);
}

int hinic_sq_debug_add(struct hinic_dev *dev, u16 sq_id)
{
	struct hinic_sq *sq;
	struct dentry *root;
	char sub_dir[16];

	sq = dev->txqs[sq_id].sq;

	sprintf(sub_dir, "0x%x", sq_id);

	root = debugfs_create_dir(sub_dir, dev->sq_dbgfs);

	return create_dbg_files(dev, HINIC_DBG_SQ_INFO, sq, root, &sq->dbg, sq_fields,
				ARRAY_SIZE(sq_fields));
}

void hinic_sq_debug_rem(struct hinic_sq *sq)
{
	if (sq->dbg)
		rem_dbg_files(sq->dbg);
}

int hinic_rq_debug_add(struct hinic_dev *dev, u16 rq_id)
{
	struct hinic_rq *rq;
	struct dentry *root;
	char sub_dir[16];

	rq = dev->rxqs[rq_id].rq;

	sprintf(sub_dir, "0x%x", rq_id);

	root = debugfs_create_dir(sub_dir, dev->rq_dbgfs);

	return create_dbg_files(dev, HINIC_DBG_RQ_INFO, rq, root, &rq->dbg, rq_fields,
				ARRAY_SIZE(rq_fields));
}

void hinic_rq_debug_rem(struct hinic_rq *rq)
{
	if (rq->dbg)
		rem_dbg_files(rq->dbg);
}

int hinic_func_table_debug_add(struct hinic_dev *dev)
{
	if (HINIC_IS_VF(dev->hwdev->hwif))
		return 0;

	return create_dbg_files(dev, HINIC_DBG_FUNC_TABLE, dev, dev->func_tbl_dbgfs, &dev->dbg,
				func_table_fields, ARRAY_SIZE(func_table_fields));
}

void hinic_func_table_debug_rem(struct hinic_dev *dev)
{
	if (!HINIC_IS_VF(dev->hwdev->hwif) && dev->dbg)
		rem_dbg_files(dev->dbg);
}

void hinic_sq_dbgfs_init(struct hinic_dev *nic_dev)
{
	nic_dev->sq_dbgfs = debugfs_create_dir("SQs", nic_dev->dbgfs_root);
}

void hinic_sq_dbgfs_uninit(struct hinic_dev *nic_dev)
{
	debugfs_remove_recursive(nic_dev->sq_dbgfs);
}

void hinic_rq_dbgfs_init(struct hinic_dev *nic_dev)
{
	nic_dev->rq_dbgfs = debugfs_create_dir("RQs", nic_dev->dbgfs_root);
}

void hinic_rq_dbgfs_uninit(struct hinic_dev *nic_dev)
{
	debugfs_remove_recursive(nic_dev->rq_dbgfs);
}

void hinic_func_tbl_dbgfs_init(struct hinic_dev *nic_dev)
{
	if (!HINIC_IS_VF(nic_dev->hwdev->hwif))
		nic_dev->func_tbl_dbgfs = debugfs_create_dir("func_table", nic_dev->dbgfs_root);
}

void hinic_func_tbl_dbgfs_uninit(struct hinic_dev *nic_dev)
{
	if (!HINIC_IS_VF(nic_dev->hwdev->hwif))
		debugfs_remove_recursive(nic_dev->func_tbl_dbgfs);
}

void hinic_dbg_init(struct hinic_dev *nic_dev)
{
	nic_dev->dbgfs_root = debugfs_create_dir(pci_name(nic_dev->hwdev->hwif->pdev),
						 hinic_dbgfs_root);
}

void hinic_dbg_uninit(struct hinic_dev *nic_dev)
{
	debugfs_remove_recursive(nic_dev->dbgfs_root);
	nic_dev->dbgfs_root = NULL;
}

void hinic_dbg_register_debugfs(const char *debugfs_dir_name)
{
	hinic_dbgfs_root = debugfs_create_dir(debugfs_dir_name, NULL);
}

void hinic_dbg_unregister_debugfs(void)
{
	debugfs_remove_recursive(hinic_dbgfs_root);
	hinic_dbgfs_root = NULL;
}
