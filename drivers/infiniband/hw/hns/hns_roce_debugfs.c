// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023 Hisilicon Limited.
 */

#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/pci.h>

#include "hns_roce_device.h"
#include "hns_roce_hw_v2.h"

static struct dentry *hns_roce_dbgfs_root;

static int hns_debugfs_seqfile_open(struct inode *inode, struct file *f)
{
	struct hns_debugfs_seqfile *seqfile = inode->i_private;

	return single_open(f, seqfile->read, seqfile->data);
}

static ssize_t hns_debugfs_seqfile_write(struct file *file,
					 const char __user *buffer,
					 size_t count, loff_t *ppos)
{
	struct hns_debugfs_seqfile *seqfile = file_inode(file)->i_private;
	char buf[16] = {};

	if (!seqfile->write)
		return -EOPNOTSUPP;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, buffer, count))
		return -EFAULT;

	return seqfile->write(buf, count, seqfile->data);
}

static const struct file_operations hns_debugfs_seqfile_fops = {
	.owner = THIS_MODULE,
	.open = hns_debugfs_seqfile_open,
	.release = single_release,
	.read = seq_read,
	.write = hns_debugfs_seqfile_write,
	.llseek = seq_lseek
};

static const char * const sw_stat_info[] = {
	[HNS_ROCE_DFX_AEQE_CNT] = "aeqe",
	[HNS_ROCE_DFX_CEQE_CNT] = "ceqe",
	[HNS_ROCE_DFX_CMDS_CNT] = "cmds",
	[HNS_ROCE_DFX_CMDS_ERR_CNT] = "cmds_err",
	[HNS_ROCE_DFX_MBX_POSTED_CNT] = "posted_mbx",
	[HNS_ROCE_DFX_MBX_POLLED_CNT] = "polled_mbx",
	[HNS_ROCE_DFX_MBX_EVENT_CNT] = "mbx_event",
	[HNS_ROCE_DFX_QP_CREATE_ERR_CNT] = "qp_create_err",
	[HNS_ROCE_DFX_QP_MODIFY_ERR_CNT] = "qp_modify_err",
	[HNS_ROCE_DFX_CQ_CREATE_ERR_CNT] = "cq_create_err",
	[HNS_ROCE_DFX_CQ_MODIFY_ERR_CNT] = "cq_modify_err",
	[HNS_ROCE_DFX_SRQ_CREATE_ERR_CNT] = "srq_create_err",
	[HNS_ROCE_DFX_SRQ_MODIFY_ERR_CNT] = "srq_modify_err",
	[HNS_ROCE_DFX_XRCD_ALLOC_ERR_CNT] = "xrcd_alloc_err",
	[HNS_ROCE_DFX_MR_REG_ERR_CNT] = "mr_reg_err",
	[HNS_ROCE_DFX_MR_REREG_ERR_CNT] = "mr_rereg_err",
	[HNS_ROCE_DFX_AH_CREATE_ERR_CNT] = "ah_create_err",
	[HNS_ROCE_DFX_MMAP_ERR_CNT] = "mmap_err",
	[HNS_ROCE_DFX_UCTX_ALLOC_ERR_CNT] = "uctx_alloc_err",
};

static int sw_stat_debugfs_show(struct seq_file *file, void *offset)
{
	struct hns_roce_dev *hr_dev = file->private;
	int i;

	for (i = 0; i < HNS_ROCE_DFX_CNT_TOTAL; i++)
		seq_printf(file, "%-20s --- %lld\n", sw_stat_info[i],
			   atomic64_read(&hr_dev->dfx_cnt[i]));

	return 0;
}

static void create_sw_stat_debugfs(struct hns_roce_dev *hr_dev,
				   struct dentry *parent)
{
	struct hns_sw_stat_debugfs *dbgfs = &hr_dev->dbgfs.sw_stat_root;

	dbgfs->sw_stat.read = sw_stat_debugfs_show;
	dbgfs->sw_stat.data = hr_dev;

	dbgfs->root = debugfs_create_dir("sw_stat", parent);
	debugfs_create_file("sw_stat", 0400, dbgfs->root, &dbgfs->sw_stat,
			    &hns_debugfs_seqfile_fops);
}

#define __HNS_SCC_ATTR(_name, _type, _offset, _size, _min, _max) {	\
	.name = _name,							\
	.algo_type = _type,						\
	.offset = _offset,						\
	.size = _size,							\
	.min = _min,							\
	.max = _max,							\
}

#define HNS_DCQCN_CC_ATTR_RW(_name, NAME)				\
	__HNS_SCC_ATTR(_name, HNS_ROCE_SCC_ALGO_DCQCN,			\
			HNS_ROCE_DCQCN_##NAME##_OFS,			\
			HNS_ROCE_DCQCN_##NAME##_SZ,			\
			0, HNS_ROCE_DCQCN_##NAME##_MAX)

#define HNS_LDCP_CC_ATTR_RW(_name, NAME)				\
	__HNS_SCC_ATTR(_name, HNS_ROCE_SCC_ALGO_LDCP,			\
			HNS_ROCE_LDCP_##NAME##_OFS,			\
			HNS_ROCE_LDCP_##NAME##_SZ,			\
			0, HNS_ROCE_LDCP_##NAME##_MAX)

#define HNS_HC3_CC_ATTR_RW(_name, NAME)				\
	__HNS_SCC_ATTR(_name, HNS_ROCE_SCC_ALGO_HC3,			\
			HNS_ROCE_HC3_##NAME##_OFS,			\
			HNS_ROCE_HC3_##NAME##_SZ,			\
			HNS_ROCE_HC3_##NAME##_MIN,			\
			HNS_ROCE_HC3_##NAME##_MAX)

#define HNS_DIP_CC_ATTR_RW(_name, NAME)				\
	__HNS_SCC_ATTR(_name, HNS_ROCE_SCC_ALGO_DIP,			\
			HNS_ROCE_DIP_##NAME##_OFS,			\
			HNS_ROCE_DIP_##NAME##_SZ,			\
			0, HNS_ROCE_DIP_##NAME##_MAX)

static const struct hns_roce_cong_attr {
	enum hns_roce_cong_type cong_type;
	const char *name;
	struct hns_roce_cc_param_attr params[HNS_ROCE_CC_PARAM_MAX_NUM];
} cong_attrs[] = {
	{ CONG_TYPE_DCQCN, "dcqcn_cc_param",
		{
			HNS_DCQCN_CC_ATTR_RW("ai", AI),
			HNS_DCQCN_CC_ATTR_RW("f", F),
			HNS_DCQCN_CC_ATTR_RW("tkp", TKP),
			HNS_DCQCN_CC_ATTR_RW("tmp", TMP),
			HNS_DCQCN_CC_ATTR_RW("alp", ALP),
			HNS_DCQCN_CC_ATTR_RW("max_speed", MAX_SPEED),
			HNS_DCQCN_CC_ATTR_RW("g", G),
			HNS_DCQCN_CC_ATTR_RW("al", AL),
			HNS_DCQCN_CC_ATTR_RW("cnp_time", CNP_TIME),
			HNS_DCQCN_CC_ATTR_RW("ashift", ASHIFT),
		}
	},
	{ CONG_TYPE_LDCP, "ldcp_cc_param",
		{
			HNS_LDCP_CC_ATTR_RW("cwd0", CWD0),
			HNS_LDCP_CC_ATTR_RW("alpha", ALPHA),
			HNS_LDCP_CC_ATTR_RW("gamma", GAMMA),
			HNS_LDCP_CC_ATTR_RW("beta", BETA),
			HNS_LDCP_CC_ATTR_RW("eta", ETA),
		}
	},
	{ CONG_TYPE_HC3, "hc3_cc_param",
		{
			HNS_HC3_CC_ATTR_RW("initial_window", INITIAL_WINDOW),
			HNS_HC3_CC_ATTR_RW("bandwidth", BANDWIDTH),
			HNS_HC3_CC_ATTR_RW("qlen_shift", QLEN_SHIFT),
			HNS_HC3_CC_ATTR_RW("port_usage_shift", PORT_USAGE_SHIFT),
			HNS_HC3_CC_ATTR_RW("over_period", OVER_PERIOD),
			HNS_HC3_CC_ATTR_RW("max_stage", MAX_STAGE),
			HNS_HC3_CC_ATTR_RW("gamma_shift", GAMMA_SHIFT),
		}
	},
	{ CONG_TYPE_DIP, "dip_cc_param",
		{
			HNS_DIP_CC_ATTR_RW("ai", AI),
			HNS_DIP_CC_ATTR_RW("f", F),
			HNS_DIP_CC_ATTR_RW("tkp", TKP),
			HNS_DIP_CC_ATTR_RW("tmp", TMP),
			HNS_DIP_CC_ATTR_RW("alp", ALP),
			HNS_DIP_CC_ATTR_RW("max_speed", MAX_SPEED),
			HNS_DIP_CC_ATTR_RW("g", G),
			HNS_DIP_CC_ATTR_RW("al", AL),
			HNS_DIP_CC_ATTR_RW("cnp_time", CNP_TIME),
			HNS_DIP_CC_ATTR_RW("ashift", ASHIFT),
		}
	}
};

static int cc_param_debugfs_show(struct seq_file *file, void *offset)
{
	struct hns_cc_param_seqfile *param_seqfile = file->private;
	const struct hns_roce_cc_param_attr *param_attr = param_seqfile->param_attr;
	int algo_type = param_attr->algo_type;
	int index = param_seqfile->index;
	struct hns_roce_dev *hr_dev =
		container_of(param_seqfile, struct hns_roce_dev,
			     dbgfs.cc_param_root[algo_type].params[index]);
	struct hns_roce_scc_param *scc_param;
	__le32 val = 0;

	scc_param = &hr_dev->scc_param[algo_type];

	scoped_guard(mutex, &scc_param->scc_mutex) {
		memcpy(&val,
		       (void *)scc_param->param + param_attr->offset,
		       param_attr->size);
	}

	seq_printf(file, "%u\n", le32_to_cpu(val));

	return 0;
}

static ssize_t cc_param_debugfs_store(char *buf, size_t count, void *data)
{
	struct hns_cc_param_seqfile *param_seqfile = data;
	const struct hns_roce_cc_param_attr *param_attr = param_seqfile->param_attr;
	int algo_type = param_attr->algo_type;
	int index = param_seqfile->index;
	struct hns_roce_dev *hr_dev =
		container_of(param_seqfile, struct hns_roce_dev,
			     dbgfs.cc_param_root[algo_type].params[index]);
	struct hns_roce_scc_param *scc_param;
	__le32 attr_val, old_val = 0;
	u32 val;
	int ret;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	if (val > param_attr->max || val < param_attr->min)
		return -EINVAL;

	attr_val = cpu_to_le32(val);
	scc_param = &hr_dev->scc_param[algo_type];
	guard(mutex)(&scc_param->scc_mutex);

	memcpy(&old_val, (void *)scc_param->param + param_attr->offset,
	       param_attr->size);
	memcpy((void *)scc_param->param + param_attr->offset, &attr_val,
	       param_attr->size);

	ret = hr_dev->hw->config_scc_param(hr_dev, algo_type);
	if (ret) {
		memcpy((void *)scc_param->param + param_attr->offset, &old_val,
		       param_attr->size);
		return ret;
	}

	return count;
}

static void get_default_scc_param(struct hns_roce_dev *hr_dev)
{
	int ret;
	int i;

	for (i = 0; i < HNS_ROCE_SCC_ALGO_TOTAL; i++) {
		ret = hr_dev->hw->query_scc_param(hr_dev, i);
		if (ret && ret != -EOPNOTSUPP)
			ibdev_warn_ratelimited(&hr_dev->ib_dev,
					       "failed to get default parameters of scc algo %d, ret = %d.\n",
					       i, ret);
	}
}

static int hns_roce_alloc_scc_param(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_scc_param *scc_param;
	int i;

	scc_param = kvcalloc(HNS_ROCE_SCC_ALGO_TOTAL, sizeof(*scc_param),
			     GFP_KERNEL);
	if (!scc_param)
		return -ENOMEM;

	for (i = 0; i < HNS_ROCE_SCC_ALGO_TOTAL; i++) {
		scc_param[i].algo_type = i;
		scc_param[i].hr_dev = hr_dev;
		mutex_init(&scc_param[i].scc_mutex);
	}

	hr_dev->scc_param = scc_param;

	get_default_scc_param(hr_dev);

	return 0;
}

static void hns_roce_dealloc_scc_param(struct hns_roce_dev *hr_dev)
{
	int i;

	if (!hr_dev->scc_param)
		return;

	for (i = 0; i < HNS_ROCE_SCC_ALGO_TOTAL; i++)
		mutex_destroy(&hr_dev->scc_param[i].scc_mutex);

	kvfree(hr_dev->scc_param);
}

static void create_cc_param_debugfs(struct hns_roce_dev *hr_dev,
				    struct dentry *parent)
{
	const struct hns_roce_cong_attr *cong_attr;
	struct hns_cc_param_debugfs *dbgfs;
	int i, j;
	int ret;

	if (hr_dev->pci_dev->revision <= PCI_REVISION_ID_HIP08 ||
	    !(hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_QP_FLOW_CTRL) ||
	    hr_dev->is_vf)
		return;

	ret = hns_roce_alloc_scc_param(hr_dev);
	if (ret)
		return;

	for (i = 0; i < CONG_TYPE_MAX_NUM; i++) {
		cong_attr = &cong_attrs[i];
		if (!test_bit(cong_attr->cong_type,
			      (unsigned long *)&hr_dev->caps.cong_cap))
			continue;

		dbgfs = &hr_dev->dbgfs.cc_param_root[i];
		dbgfs->root = debugfs_create_dir(cong_attr->name, parent);
		for (j = 0; j < HNS_ROCE_CC_PARAM_MAX_NUM; j++) {
			if (!cong_attr->params[j].name)
				break;
			dbgfs->params[j].param_attr = &cong_attr->params[j];
			dbgfs->params[j].index = j;
			dbgfs->params[j].seqfile.read = cc_param_debugfs_show;
			dbgfs->params[j].seqfile.write = cc_param_debugfs_store;
			dbgfs->params[j].seqfile.data = &dbgfs->params[j];
			debugfs_create_file(cong_attr->params[j].name, 0600,
					    dbgfs->root,
					    &dbgfs->params[j].seqfile,
					    &hns_debugfs_seqfile_fops);
		}
	}
}

/* debugfs for device */
void hns_roce_register_debugfs(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_dev_debugfs *dbgfs = &hr_dev->dbgfs;

	dbgfs->root = debugfs_create_dir(pci_name(hr_dev->pci_dev),
					 hns_roce_dbgfs_root);

	create_sw_stat_debugfs(hr_dev, dbgfs->root);
	create_cc_param_debugfs(hr_dev, dbgfs->root);
}

void hns_roce_unregister_debugfs(struct hns_roce_dev *hr_dev)
{
	debugfs_remove_recursive(hr_dev->dbgfs.root);
	hns_roce_dealloc_scc_param(hr_dev);
}

/* debugfs for hns module */
void hns_roce_init_debugfs(void)
{
	hns_roce_dbgfs_root = debugfs_create_dir("hns_roce", NULL);
}

void hns_roce_cleanup_debugfs(void)
{
	debugfs_remove_recursive(hns_roce_dbgfs_root);
	hns_roce_dbgfs_root = NULL;
}
