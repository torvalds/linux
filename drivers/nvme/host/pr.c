// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015 Intel Corporation
 *	Keith Busch <kbusch@kernel.org>
 */
#include <linux/blkdev.h>
#include <linux/pr.h>
#include <asm/unaligned.h>

#include "nvme.h"

static char nvme_pr_type(enum pr_type type)
{
	switch (type) {
	case PR_WRITE_EXCLUSIVE:
		return 1;
	case PR_EXCLUSIVE_ACCESS:
		return 2;
	case PR_WRITE_EXCLUSIVE_REG_ONLY:
		return 3;
	case PR_EXCLUSIVE_ACCESS_REG_ONLY:
		return 4;
	case PR_WRITE_EXCLUSIVE_ALL_REGS:
		return 5;
	case PR_EXCLUSIVE_ACCESS_ALL_REGS:
		return 6;
	default:
		return 0;
	}
}

static int nvme_send_ns_head_pr_command(struct block_device *bdev,
		struct nvme_command *c, u8 *data, unsigned int data_len)
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
		u8 *data, unsigned int data_len)
{
	c->common.nsid = cpu_to_le32(ns->head->ns_id);
	return nvme_submit_sync_cmd(ns->queue, c, data, data_len);
}

static int nvme_sc_to_pr_err(int nvme_sc)
{
	if (nvme_is_path_error(nvme_sc))
		return PR_STS_PATH_FAILED;

	switch (nvme_sc) {
	case NVME_SC_SUCCESS:
		return PR_STS_SUCCESS;
	case NVME_SC_RESERVATION_CONFLICT:
		return PR_STS_RESERVATION_CONFLICT;
	case NVME_SC_ONCS_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case NVME_SC_BAD_ATTRIBUTES:
	case NVME_SC_INVALID_OPCODE:
	case NVME_SC_INVALID_FIELD:
	case NVME_SC_INVALID_NS:
		return -EINVAL;
	default:
		return PR_STS_IOERR;
	}
}

static int nvme_pr_command(struct block_device *bdev, u32 cdw10,
				u64 key, u64 sa_key, u8 op)
{
	struct nvme_command c = { };
	u8 data[16] = { 0, };
	int ret;

	put_unaligned_le64(key, &data[0]);
	put_unaligned_le64(sa_key, &data[8]);

	c.common.opcode = op;
	c.common.cdw10 = cpu_to_le32(cdw10);

	if (IS_ENABLED(CONFIG_NVME_MULTIPATH) &&
	    bdev->bd_disk->fops == &nvme_ns_head_ops)
		ret = nvme_send_ns_head_pr_command(bdev, &c, data,
						   sizeof(data));
	else
		ret = nvme_send_ns_pr_command(bdev->bd_disk->private_data, &c,
					      data, sizeof(data));
	if (ret < 0)
		return ret;

	return nvme_sc_to_pr_err(ret);
}

static int nvme_pr_register(struct block_device *bdev, u64 old,
		u64 new, unsigned flags)
{
	u32 cdw10;

	if (flags & ~PR_FL_IGNORE_KEY)
		return -EOPNOTSUPP;

	cdw10 = old ? 2 : 0;
	cdw10 |= (flags & PR_FL_IGNORE_KEY) ? 1 << 3 : 0;
	cdw10 |= (1 << 30) | (1 << 31); /* PTPL=1 */
	return nvme_pr_command(bdev, cdw10, old, new, nvme_cmd_resv_register);
}

static int nvme_pr_reserve(struct block_device *bdev, u64 key,
		enum pr_type type, unsigned flags)
{
	u32 cdw10;

	if (flags & ~PR_FL_IGNORE_KEY)
		return -EOPNOTSUPP;

	cdw10 = nvme_pr_type(type) << 8;
	cdw10 |= ((flags & PR_FL_IGNORE_KEY) ? 1 << 3 : 0);
	return nvme_pr_command(bdev, cdw10, key, 0, nvme_cmd_resv_acquire);
}

static int nvme_pr_preempt(struct block_device *bdev, u64 old, u64 new,
		enum pr_type type, bool abort)
{
	u32 cdw10 = nvme_pr_type(type) << 8 | (abort ? 2 : 1);

	return nvme_pr_command(bdev, cdw10, old, new, nvme_cmd_resv_acquire);
}

static int nvme_pr_clear(struct block_device *bdev, u64 key)
{
	u32 cdw10 = 1 | (key ? 0 : 1 << 3);

	return nvme_pr_command(bdev, cdw10, key, 0, nvme_cmd_resv_release);
}

static int nvme_pr_release(struct block_device *bdev, u64 key, enum pr_type type)
{
	u32 cdw10 = nvme_pr_type(type) << 8 | (key ? 0 : 1 << 3);

	return nvme_pr_command(bdev, cdw10, key, 0, nvme_cmd_resv_release);
}

const struct pr_ops nvme_pr_ops = {
	.pr_register	= nvme_pr_register,
	.pr_reserve	= nvme_pr_reserve,
	.pr_release	= nvme_pr_release,
	.pr_preempt	= nvme_pr_preempt,
	.pr_clear	= nvme_pr_clear,
};
