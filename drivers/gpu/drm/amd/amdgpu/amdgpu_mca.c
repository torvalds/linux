/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "amdgpu_ras.h"
#include "amdgpu.h"
#include "amdgpu_mca.h"

#include "umc/umc_6_7_0_offset.h"
#include "umc/umc_6_7_0_sh_mask.h"

void amdgpu_mca_query_correctable_error_count(struct amdgpu_device *adev,
					      uint64_t mc_status_addr,
					      unsigned long *error_count)
{
	uint64_t mc_status = RREG64_PCIE(mc_status_addr);

	if (REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1)
		*error_count += 1;
}

void amdgpu_mca_query_uncorrectable_error_count(struct amdgpu_device *adev,
						uint64_t mc_status_addr,
						unsigned long *error_count)
{
	uint64_t mc_status = RREG64_PCIE(mc_status_addr);

	if ((REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1) &&
	    (REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Deferred) == 1 ||
	    REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1 ||
	    REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, PCC) == 1 ||
	    REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UC) == 1 ||
	    REG_GET_FIELD(mc_status, MCA_UMC_UMC0_MCUMC_STATUST0, TCC) == 1))
		*error_count += 1;
}

void amdgpu_mca_reset_error_count(struct amdgpu_device *adev,
				  uint64_t mc_status_addr)
{
	WREG64_PCIE(mc_status_addr, 0x0ULL);
}

void amdgpu_mca_query_ras_error_count(struct amdgpu_device *adev,
				      uint64_t mc_status_addr,
				      void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;

	amdgpu_mca_query_correctable_error_count(adev, mc_status_addr, &(err_data->ce_count));
	amdgpu_mca_query_uncorrectable_error_count(adev, mc_status_addr, &(err_data->ue_count));

	amdgpu_mca_reset_error_count(adev, mc_status_addr);
}

int amdgpu_mca_mp0_ras_sw_init(struct amdgpu_device *adev)
{
	int err;
	struct amdgpu_mca_ras_block *ras;

	if (!adev->mca.mp0.ras)
		return 0;

	ras = adev->mca.mp0.ras;

	err = amdgpu_ras_register_ras_block(adev, &ras->ras_block);
	if (err) {
		dev_err(adev->dev, "Failed to register mca.mp0 ras block!\n");
		return err;
	}

	strcpy(ras->ras_block.ras_comm.name, "mca.mp0");
	ras->ras_block.ras_comm.block = AMDGPU_RAS_BLOCK__MCA;
	ras->ras_block.ras_comm.type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
	adev->mca.mp0.ras_if = &ras->ras_block.ras_comm;

	return 0;
}

int amdgpu_mca_mp1_ras_sw_init(struct amdgpu_device *adev)
{
	int err;
	struct amdgpu_mca_ras_block *ras;

	if (!adev->mca.mp1.ras)
		return 0;

	ras = adev->mca.mp1.ras;

	err = amdgpu_ras_register_ras_block(adev, &ras->ras_block);
	if (err) {
		dev_err(adev->dev, "Failed to register mca.mp1 ras block!\n");
		return err;
	}

	strcpy(ras->ras_block.ras_comm.name, "mca.mp1");
	ras->ras_block.ras_comm.block = AMDGPU_RAS_BLOCK__MCA;
	ras->ras_block.ras_comm.type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
	adev->mca.mp1.ras_if = &ras->ras_block.ras_comm;

	return 0;
}

int amdgpu_mca_mpio_ras_sw_init(struct amdgpu_device *adev)
{
	int err;
	struct amdgpu_mca_ras_block *ras;

	if (!adev->mca.mpio.ras)
		return 0;

	ras = adev->mca.mpio.ras;

	err = amdgpu_ras_register_ras_block(adev, &ras->ras_block);
	if (err) {
		dev_err(adev->dev, "Failed to register mca.mpio ras block!\n");
		return err;
	}

	strcpy(ras->ras_block.ras_comm.name, "mca.mpio");
	ras->ras_block.ras_comm.block = AMDGPU_RAS_BLOCK__MCA;
	ras->ras_block.ras_comm.type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
	adev->mca.mpio.ras_if = &ras->ras_block.ras_comm;

	return 0;
}

void amdgpu_mca_bank_set_init(struct mca_bank_set *mca_set)
{
	if (!mca_set)
		return;

	memset(mca_set, 0, sizeof(*mca_set));
	INIT_LIST_HEAD(&mca_set->list);
}

int amdgpu_mca_bank_set_add_entry(struct mca_bank_set *mca_set, struct mca_bank_entry *entry)
{
	struct mca_bank_node *node;

	if (!entry)
		return -EINVAL;

	node = kvzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	memcpy(&node->entry, entry, sizeof(*entry));

	INIT_LIST_HEAD(&node->node);
	list_add_tail(&node->node, &mca_set->list);

	mca_set->nr_entries++;

	return 0;
}

void amdgpu_mca_bank_set_release(struct mca_bank_set *mca_set)
{
	struct mca_bank_node *node, *tmp;

	list_for_each_entry_safe(node, tmp, &mca_set->list, node) {
		list_del(&node->node);
		kvfree(node);
	}
}

void amdgpu_mca_smu_init_funcs(struct amdgpu_device *adev, const struct amdgpu_mca_smu_funcs *mca_funcs)
{
	struct amdgpu_mca *mca = &adev->mca;

	mca->mca_funcs = mca_funcs;
}

int amdgpu_mca_smu_set_debug_mode(struct amdgpu_device *adev, bool enable)
{
	const struct amdgpu_mca_smu_funcs *mca_funcs = adev->mca.mca_funcs;

	if (mca_funcs && mca_funcs->mca_set_debug_mode)
		return mca_funcs->mca_set_debug_mode(adev, enable);

	return -EOPNOTSUPP;
}

static void amdgpu_mca_smu_mca_bank_dump(struct amdgpu_device *adev, int idx, struct mca_bank_entry *entry)
{
	dev_info(adev->dev, "[Hardware error] Accelerator Check Architecture events logged\n");
	dev_info(adev->dev, "[Hardware error] aca entry[%02d].STATUS=0x%016llx\n",
		 idx, entry->regs[MCA_REG_IDX_STATUS]);
	dev_info(adev->dev, "[Hardware error] aca entry[%02d].ADDR=0x%016llx\n",
		 idx, entry->regs[MCA_REG_IDX_ADDR]);
	dev_info(adev->dev, "[Hardware error] aca entry[%02d].MISC0=0x%016llx\n",
		 idx, entry->regs[MCA_REG_IDX_MISC0]);
	dev_info(adev->dev, "[Hardware error] aca entry[%02d].IPID=0x%016llx\n",
		 idx, entry->regs[MCA_REG_IDX_IPID]);
	dev_info(adev->dev, "[Hardware error] aca entry[%02d].SYND=0x%016llx\n",
		 idx, entry->regs[MCA_REG_IDX_SYND]);
}

int amdgpu_mca_smu_log_ras_error(struct amdgpu_device *adev, enum amdgpu_ras_block blk, enum amdgpu_mca_error_type type, struct ras_err_data *err_data)
{
	struct amdgpu_smuio_mcm_config_info mcm_info;
	struct ras_err_addr err_addr = {0};
	struct mca_bank_set mca_set;
	struct mca_bank_node *node;
	struct mca_bank_entry *entry;
	uint32_t count;
	int ret, i = 0;

	amdgpu_mca_bank_set_init(&mca_set);

	ret = amdgpu_mca_smu_get_mca_set(adev, blk, type, &mca_set);
	if (ret)
		goto out_mca_release;

	list_for_each_entry(node, &mca_set.list, node) {
		entry = &node->entry;

		amdgpu_mca_smu_mca_bank_dump(adev, i++, entry);

		count = 0;
		ret = amdgpu_mca_smu_parse_mca_error_count(adev, blk, type, entry, &count);
		if (ret)
			goto out_mca_release;

		if (!count)
			continue;

		mcm_info.socket_id = entry->info.socket_id;
		mcm_info.die_id = entry->info.aid;

		if (blk == AMDGPU_RAS_BLOCK__UMC) {
			err_addr.err_status = entry->regs[MCA_REG_IDX_STATUS];
			err_addr.err_ipid = entry->regs[MCA_REG_IDX_IPID];
			err_addr.err_addr = entry->regs[MCA_REG_IDX_ADDR];
		}

		if (type == AMDGPU_MCA_ERROR_TYPE_UE)
			amdgpu_ras_error_statistic_ue_count(err_data,
				&mcm_info, &err_addr, (uint64_t)count);
		else
			amdgpu_ras_error_statistic_ce_count(err_data,
				&mcm_info, &err_addr, (uint64_t)count);
	}

out_mca_release:
	amdgpu_mca_bank_set_release(&mca_set);

	return ret;
}


int amdgpu_mca_smu_get_valid_mca_count(struct amdgpu_device *adev, enum amdgpu_mca_error_type type, uint32_t *count)
{
	const struct amdgpu_mca_smu_funcs *mca_funcs = adev->mca.mca_funcs;

	if (!count)
		return -EINVAL;

	if (mca_funcs && mca_funcs->mca_get_valid_mca_count)
		return mca_funcs->mca_get_valid_mca_count(adev, type, count);

	return -EOPNOTSUPP;
}

int amdgpu_mca_smu_get_mca_set_error_count(struct amdgpu_device *adev, enum amdgpu_ras_block blk,
					    enum amdgpu_mca_error_type type, uint32_t *total)
{
	const struct amdgpu_mca_smu_funcs *mca_funcs = adev->mca.mca_funcs;
	struct mca_bank_set mca_set;
	struct mca_bank_node *node;
	struct mca_bank_entry *entry;
	uint32_t count;
	int ret;

	if (!total)
		return -EINVAL;

	if (!mca_funcs)
		return -EOPNOTSUPP;

	if (!mca_funcs->mca_get_ras_mca_set || !mca_funcs->mca_get_valid_mca_count)
		return -EOPNOTSUPP;

	amdgpu_mca_bank_set_init(&mca_set);

	ret = mca_funcs->mca_get_ras_mca_set(adev, blk, type, &mca_set);
	if (ret)
		goto err_mca_set_release;

	*total = 0;
	list_for_each_entry(node, &mca_set.list, node) {
		entry = &node->entry;

		count = 0;
		ret = mca_funcs->mca_parse_mca_error_count(adev, blk, type, entry, &count);
		if (ret)
			goto err_mca_set_release;

		*total += count;
	}

err_mca_set_release:
	amdgpu_mca_bank_set_release(&mca_set);

	return ret;
}

int amdgpu_mca_smu_parse_mca_error_count(struct amdgpu_device *adev, enum amdgpu_ras_block blk,
					 enum amdgpu_mca_error_type type, struct mca_bank_entry *entry, uint32_t *count)
{
	const struct amdgpu_mca_smu_funcs *mca_funcs = adev->mca.mca_funcs;
	if (!count || !entry)
		return -EINVAL;

	if (!mca_funcs || !mca_funcs->mca_parse_mca_error_count)
		return -EOPNOTSUPP;


	return mca_funcs->mca_parse_mca_error_count(adev, blk, type, entry, count);
}

int amdgpu_mca_smu_get_mca_set(struct amdgpu_device *adev, enum amdgpu_ras_block blk,
			       enum amdgpu_mca_error_type type, struct mca_bank_set *mca_set)
{
	const struct amdgpu_mca_smu_funcs *mca_funcs = adev->mca.mca_funcs;

	if (!mca_set)
		return -EINVAL;

	if (!mca_funcs || !mca_funcs->mca_get_ras_mca_set)
		return -EOPNOTSUPP;

	WARN_ON(!list_empty(&mca_set->list));

	return mca_funcs->mca_get_ras_mca_set(adev, blk, type, mca_set);
}

int amdgpu_mca_smu_get_mca_entry(struct amdgpu_device *adev, enum amdgpu_mca_error_type type,
				 int idx, struct mca_bank_entry *entry)
{
	const struct amdgpu_mca_smu_funcs *mca_funcs = adev->mca.mca_funcs;
	int count;

	switch (type) {
	case AMDGPU_MCA_ERROR_TYPE_UE:
		count = mca_funcs->max_ue_count;
		break;
	case AMDGPU_MCA_ERROR_TYPE_CE:
		count = mca_funcs->max_ce_count;
		break;
	default:
		return -EINVAL;
	}

	if (idx >= count)
		return -EINVAL;

	if (mca_funcs && mca_funcs->mca_get_mca_entry)
		return mca_funcs->mca_get_mca_entry(adev, type, idx, entry);

	return -EOPNOTSUPP;
}

#if defined(CONFIG_DEBUG_FS)
static int amdgpu_mca_smu_debug_mode_set(void *data, u64 val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)data;
	int ret;

	ret = amdgpu_ras_set_mca_debug_mode(adev, val ? true : false);
	if (ret)
		return ret;

	dev_info(adev->dev, "amdgpu set smu mca debug mode %s success\n", val ? "on" : "off");

	return 0;
}

static void mca_dump_entry(struct seq_file *m, struct mca_bank_entry *entry)
{
	int i, idx = entry->idx;
	int reg_idx_array[] = {
		MCA_REG_IDX_STATUS,
		MCA_REG_IDX_ADDR,
		MCA_REG_IDX_MISC0,
		MCA_REG_IDX_IPID,
		MCA_REG_IDX_SYND,
	};

	seq_printf(m, "mca entry[%d].type: %s\n", idx, entry->type == AMDGPU_MCA_ERROR_TYPE_UE ? "UE" : "CE");
	seq_printf(m, "mca entry[%d].ip: %d\n", idx, entry->ip);
	seq_printf(m, "mca entry[%d].info: socketid:%d aid:%d hwid:0x%03x mcatype:0x%04x\n",
		   idx, entry->info.socket_id, entry->info.aid, entry->info.hwid, entry->info.mcatype);

	for (i = 0; i < ARRAY_SIZE(reg_idx_array); i++)
		seq_printf(m, "mca entry[%d].regs[%d]: 0x%016llx\n", idx, reg_idx_array[i], entry->regs[reg_idx_array[i]]);
}

static int mca_dump_show(struct seq_file *m, enum amdgpu_mca_error_type type)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)m->private;
	struct mca_bank_entry *entry;
	uint32_t count = 0;
	int i, ret;

	ret = amdgpu_mca_smu_get_valid_mca_count(adev, type, &count);
	if (ret)
		return ret;

	seq_printf(m, "amdgpu smu %s valid mca count: %d\n",
		   type == AMDGPU_MCA_ERROR_TYPE_UE ? "UE" : "CE", count);

	if (!count)
		return 0;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		memset(entry, 0, sizeof(*entry));

		ret = amdgpu_mca_smu_get_mca_entry(adev, type, i, entry);
		if (ret)
			goto err_free_entry;

		mca_dump_entry(m, entry);
	}

err_free_entry:
	kfree(entry);

	return ret;
}

static int mca_dump_ce_show(struct seq_file *m, void *unused)
{
	return mca_dump_show(m, AMDGPU_MCA_ERROR_TYPE_CE);
}

static int mca_dump_ce_open(struct inode *inode, struct file *file)
{
	return single_open(file, mca_dump_ce_show, inode->i_private);
}

static const struct file_operations mca_ce_dump_debug_fops = {
	.owner = THIS_MODULE,
	.open = mca_dump_ce_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int mca_dump_ue_show(struct seq_file *m, void *unused)
{
	return mca_dump_show(m, AMDGPU_MCA_ERROR_TYPE_UE);
}

static int mca_dump_ue_open(struct inode *inode, struct file *file)
{
	return single_open(file, mca_dump_ue_show, inode->i_private);
}

static const struct file_operations mca_ue_dump_debug_fops = {
	.owner = THIS_MODULE,
	.open = mca_dump_ue_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

DEFINE_DEBUGFS_ATTRIBUTE(mca_debug_mode_fops, NULL, amdgpu_mca_smu_debug_mode_set, "%llu\n");
#endif

void amdgpu_mca_smu_debugfs_init(struct amdgpu_device *adev, struct dentry *root)
{
#if defined(CONFIG_DEBUG_FS)
	if (!root || amdgpu_ip_version(adev, MP1_HWIP, 0) != IP_VERSION(13, 0, 6))
		return;

	debugfs_create_file("mca_debug_mode", 0200, root, adev, &mca_debug_mode_fops);
	debugfs_create_file("mca_ue_dump", 0400, root, adev, &mca_ue_dump_debug_fops);
	debugfs_create_file("mca_ce_dump", 0400, root, adev, &mca_ce_dump_debug_fops);
#endif
}

