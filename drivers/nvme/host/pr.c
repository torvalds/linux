// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015 Intel Corporation
 *	Keith Busch <kbusch@kernel.org>
 */
#include <linux/blkdev.h>
#include <linux/pr.h>
#include <linux/unaligned.h>

#include "nvme.h"

static enum nvme_pr_type nvme_pr_type_from_blk(enum pr_type type)
{
	switch (type) {
	case PR_WRITE_EXCLUSIVE:
		return NVME_PR_WRITE_EXCLUSIVE;
	case PR_EXCLUSIVE_ACCESS:
		return NVME_PR_EXCLUSIVE_ACCESS;
	case PR_WRITE_EXCLUSIVE_REG_ONLY:
		return NVME_PR_WRITE_EXCLUSIVE_REG_ONLY;
	case PR_EXCLUSIVE_ACCESS_REG_ONLY:
		return NVME_PR_EXCLUSIVE_ACCESS_REG_ONLY;
	case PR_WRITE_EXCLUSIVE_ALL_REGS:
		return NVME_PR_WRITE_EXCLUSIVE_ALL_REGS;
	case PR_EXCLUSIVE_ACCESS_ALL_REGS:
		return NVME_PR_EXCLUSIVE_ACCESS_ALL_REGS;
	}

	return 0;
}

static enum pr_type block_pr_type_from_nvme(enum nvme_pr_type type)
{
	switch (type) {
	case NVME_PR_WRITE_EXCLUSIVE:
		return PR_WRITE_EXCLUSIVE;
	case NVME_PR_EXCLUSIVE_ACCESS:
		return PR_EXCLUSIVE_ACCESS;
	case NVME_PR_WRITE_EXCLUSIVE_REG_ONLY:
		return PR_WRITE_EXCLUSIVE_REG_ONLY;
	case NVME_PR_EXCLUSIVE_ACCESS_REG_ONLY:
		return PR_EXCLUSIVE_ACCESS_REG_ONLY;
	case NVME_PR_WRITE_EXCLUSIVE_ALL_REGS:
		return PR_WRITE_EXCLUSIVE_ALL_REGS;
	case NVME_PR_EXCLUSIVE_ACCESS_ALL_REGS:
		return PR_EXCLUSIVE_ACCESS_ALL_REGS;
	}

	return 0;
}

static int nvme_send_ns_head_pr_command(struct block_device *bdev,
		struct nvme_command *c, void *data, unsigned int data_len)
{
	struct nvme_ns_head *head = bdev->bd_disk->private_data;
	int srcu_idx = srcu_read_lock(&head->srcu);
	struct nvme_ns *ns = nvme_find_path(head);
	int ret = -EWOULDBLOCK;

	if (ns) {
		c->common.nsid = cpu_to_le32(ns->head->ns_id);
		ret = nvme_submit_sync_cmd(ns->queue, c, data, data_len);
	}
	srcu_read_unlock(&head->srcu, srcu_idx);
	return ret;
}

static int nvme_send_ns_pr_command(struct nvme_ns *ns, struct nvme_command *c,
		void *data, unsigned int data_len)
{
	c->common.nsid = cpu_to_le32(ns->head->ns_id);
	return nvme_submit_sync_cmd(ns->queue, c, data, data_len);
}

static int nvme_status_to_pr_err(int status)
{
	if (nvme_is_path_error(status))
		return PR_STS_PATH_FAILED;

	switch (status & NVME_SCT_SC_MASK) {
	case NVME_SC_SUCCESS:
		return PR_STS_SUCCESS;
	case NVME_SC_RESERVATION_CONFLICT:
		return PR_STS_RESERVATION_CONFLICT;
	case NVME_SC_BAD_ATTRIBUTES:
	case NVME_SC_INVALID_OPCODE:
	case NVME_SC_INVALID_FIELD:
	case NVME_SC_INVALID_NS:
		return -EINVAL;
	default:
		return PR_STS_IOERR;
	}
}

static int __nvme_send_pr_command(struct block_device *bdev, u32 cdw10,
		u32 cdw11, u8 op, void *data, unsigned int data_len)
{
	struct nvme_command c = { 0 };

	c.common.opcode = op;
	c.common.cdw10 = cpu_to_le32(cdw10);
	c.common.cdw11 = cpu_to_le32(cdw11);

	if (nvme_disk_is_ns_head(bdev->bd_disk))
		return nvme_send_ns_head_pr_command(bdev, &c, data, data_len);
	return nvme_send_ns_pr_command(bdev->bd_disk->private_data, &c,
				data, data_len);
}

static int nvme_send_pr_command(struct block_device *bdev, u32 cdw10, u32 cdw11,
		u8 op, void *data, unsigned int data_len)
{
	int ret;

	ret = __nvme_send_pr_command(bdev, cdw10, cdw11, op, data, data_len);
	return ret < 0 ? ret : nvme_status_to_pr_err(ret);
}

static int nvme_pr_register(struct block_device *bdev, u64 old_key, u64 new_key,
		unsigned int flags)
{
	struct nvmet_pr_register_data data = { 0 };
	u32 cdw10;

	if (flags & ~PR_FL_IGNORE_KEY)
		return -EOPNOTSUPP;

	data.crkey = cpu_to_le64(old_key);
	data.nrkey = cpu_to_le64(new_key);

	cdw10 = old_key ? NVME_PR_REGISTER_ACT_REPLACE :
		NVME_PR_REGISTER_ACT_REG;
	cdw10 |= (flags & PR_FL_IGNORE_KEY) ? NVME_PR_IGNORE_KEY : 0;
	cdw10 |= NVME_PR_CPTPL_PERSIST;

	return nvme_send_pr_command(bdev, cdw10, 0, nvme_cmd_resv_register,
			&data, sizeof(data));
}

static int nvme_pr_reserve(struct block_device *bdev, u64 key,
		enum pr_type type, unsigned flags)
{
	struct nvmet_pr_acquire_data data = { 0 };
	u32 cdw10;

	if (flags & ~PR_FL_IGNORE_KEY)
		return -EOPNOTSUPP;

	data.crkey = cpu_to_le64(key);

	cdw10 = NVME_PR_ACQUIRE_ACT_ACQUIRE;
	cdw10 |= nvme_pr_type_from_blk(type) << 8;
	cdw10 |= (flags & PR_FL_IGNORE_KEY) ? NVME_PR_IGNORE_KEY : 0;

	return nvme_send_pr_command(bdev, cdw10, 0, nvme_cmd_resv_acquire,
			&data, sizeof(data));
}

static int nvme_pr_preempt(struct block_device *bdev, u64 old, u64 new,
		enum pr_type type, bool abort)
{
	struct nvmet_pr_acquire_data data = { 0 };
	u32 cdw10;

	data.crkey = cpu_to_le64(old);
	data.prkey = cpu_to_le64(new);

	cdw10 = abort ? NVME_PR_ACQUIRE_ACT_PREEMPT_AND_ABORT :
			NVME_PR_ACQUIRE_ACT_PREEMPT;
	cdw10 |= nvme_pr_type_from_blk(type) << 8;

	return nvme_send_pr_command(bdev, cdw10, 0, nvme_cmd_resv_acquire,
			&data, sizeof(data));
}

static int nvme_pr_clear(struct block_device *bdev, u64 key)
{
	struct nvmet_pr_release_data data = { 0 };
	u32 cdw10;

	data.crkey = cpu_to_le64(key);

	cdw10 = NVME_PR_RELEASE_ACT_CLEAR;
	cdw10 |= key ? 0 : NVME_PR_IGNORE_KEY;

	return nvme_send_pr_command(bdev, cdw10, 0, nvme_cmd_resv_release,
			&data, sizeof(data));
}

static int nvme_pr_release(struct block_device *bdev, u64 key, enum pr_type type)
{
	struct nvmet_pr_release_data data = { 0 };
	u32 cdw10;

	data.crkey = cpu_to_le64(key);

	cdw10 = NVME_PR_RELEASE_ACT_RELEASE;
	cdw10 |= nvme_pr_type_from_blk(type) << 8;
	cdw10 |= key ? 0 : NVME_PR_IGNORE_KEY;

	return nvme_send_pr_command(bdev, cdw10, 0, nvme_cmd_resv_release,
			&data, sizeof(data));
}

static int nvme_pr_resv_report(struct block_device *bdev, void *data,
		u32 data_len, bool *eds)
{
	u32 cdw10, cdw11;
	int ret;

	cdw10 = nvme_bytes_to_numd(data_len);
	cdw11 = NVME_EXTENDED_DATA_STRUCT;
	*eds = true;

retry:
	ret = __nvme_send_pr_command(bdev, cdw10, cdw11, nvme_cmd_resv_report,
			data, data_len);
	if (ret == NVME_SC_HOST_ID_INCONSIST &&
	    cdw11 == NVME_EXTENDED_DATA_STRUCT) {
		cdw11 = 0;
		*eds = false;
		goto retry;
	}

	return ret < 0 ? ret : nvme_status_to_pr_err(ret);
}

static int nvme_pr_read_keys(struct block_device *bdev,
		struct pr_keys *keys_info)
{
	u32 rse_len, num_keys = keys_info->num_keys;
	struct nvme_reservation_status_ext *rse;
	int ret, i;
	bool eds;

	/*
	 * Assume we are using 128-bit host IDs and allocate a buffer large
	 * enough to get enough keys to fill the return keys buffer.
	 */
	rse_len = struct_size(rse, regctl_eds, num_keys);
	rse = kzalloc(rse_len, GFP_KERNEL);
	if (!rse)
		return -ENOMEM;

	ret = nvme_pr_resv_report(bdev, rse, rse_len, &eds);
	if (ret)
		goto free_rse;

	keys_info->generation = le32_to_cpu(rse->gen);
	keys_info->num_keys = get_unaligned_le16(&rse->regctl);

	num_keys = min(num_keys, keys_info->num_keys);
	for (i = 0; i < num_keys; i++) {
		if (eds) {
			keys_info->keys[i] =
					le64_to_cpu(rse->regctl_eds[i].rkey);
		} else {
			struct nvme_reservation_status *rs;

			rs = (struct nvme_reservation_status *)rse;
			keys_info->keys[i] = le64_to_cpu(rs->regctl_ds[i].rkey);
		}
	}

free_rse:
	kfree(rse);
	return ret;
}

static int nvme_pr_read_reservation(struct block_device *bdev,
		struct pr_held_reservation *resv)
{
	struct nvme_reservation_status_ext tmp_rse, *rse;
	int ret, i, num_regs;
	u32 rse_len;
	bool eds;

get_num_regs:
	/*
	 * Get the number of registrations so we know how big to allocate
	 * the response buffer.
	 */
	ret = nvme_pr_resv_report(bdev, &tmp_rse, sizeof(tmp_rse), &eds);
	if (ret)
		return ret;

	num_regs = get_unaligned_le16(&tmp_rse.regctl);
	if (!num_regs) {
		resv->generation = le32_to_cpu(tmp_rse.gen);
		return 0;
	}

	rse_len = struct_size(rse, regctl_eds, num_regs);
	rse = kzalloc(rse_len, GFP_KERNEL);
	if (!rse)
		return -ENOMEM;

	ret = nvme_pr_resv_report(bdev, rse, rse_len, &eds);
	if (ret)
		goto free_rse;

	if (num_regs != get_unaligned_le16(&rse->regctl)) {
		kfree(rse);
		goto get_num_regs;
	}

	resv->generation = le32_to_cpu(rse->gen);
	resv->type = block_pr_type_from_nvme(rse->rtype);

	for (i = 0; i < num_regs; i++) {
		if (eds) {
			if (rse->regctl_eds[i].rcsts) {
				resv->key = le64_to_cpu(rse->regctl_eds[i].rkey);
				break;
			}
		} else {
			struct nvme_reservation_status *rs;

			rs = (struct nvme_reservation_status *)rse;
			if (rs->regctl_ds[i].rcsts) {
				resv->key = le64_to_cpu(rs->regctl_ds[i].rkey);
				break;
			}
		}
	}

free_rse:
	kfree(rse);
	return ret;
}

const struct pr_ops nvme_pr_ops = {
	.pr_register	= nvme_pr_register,
	.pr_reserve	= nvme_pr_reserve,
	.pr_release	= nvme_pr_release,
	.pr_preempt	= nvme_pr_preempt,
	.pr_clear	= nvme_pr_clear,
	.pr_read_keys	= nvme_pr_read_keys,
	.pr_read_reservation = nvme_pr_read_reservation,
};
