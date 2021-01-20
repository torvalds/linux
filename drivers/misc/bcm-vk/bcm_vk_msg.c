// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018-2020 Broadcom.
 */

#include "bcm_vk.h"
#include "bcm_vk_msg.h"

/*
 * allocate a ctx per file struct
 */
static struct bcm_vk_ctx *bcm_vk_get_ctx(struct bcm_vk *vk, const pid_t pid)
{
	u32 i;
	struct bcm_vk_ctx *ctx = NULL;
	u32 hash_idx = hash_32(pid, VK_PID_HT_SHIFT_BIT);

	spin_lock(&vk->ctx_lock);

	for (i = 0; i < ARRAY_SIZE(vk->ctx); i++) {
		if (!vk->ctx[i].in_use) {
			vk->ctx[i].in_use = true;
			ctx = &vk->ctx[i];
			break;
		}
	}

	if (!ctx) {
		dev_err(&vk->pdev->dev, "All context in use\n");

		goto all_in_use_exit;
	}

	/* set the pid and insert it to hash table */
	ctx->pid = pid;
	ctx->hash_idx = hash_idx;
	list_add_tail(&ctx->node, &vk->pid_ht[hash_idx].head);

	/* increase kref */
	kref_get(&vk->kref);

all_in_use_exit:
	spin_unlock(&vk->ctx_lock);

	return ctx;
}

static int bcm_vk_free_ctx(struct bcm_vk *vk, struct bcm_vk_ctx *ctx)
{
	u32 idx;
	u32 hash_idx;
	pid_t pid;
	struct bcm_vk_ctx *entry;
	int count = 0;

	if (!ctx) {
		dev_err(&vk->pdev->dev, "NULL context detected\n");
		return -EINVAL;
	}
	idx = ctx->idx;
	pid = ctx->pid;

	spin_lock(&vk->ctx_lock);

	if (!vk->ctx[idx].in_use) {
		dev_err(&vk->pdev->dev, "context[%d] not in use!\n", idx);
	} else {
		vk->ctx[idx].in_use = false;
		vk->ctx[idx].miscdev = NULL;

		/* Remove it from hash list and see if it is the last one. */
		list_del(&ctx->node);
		hash_idx = ctx->hash_idx;
		list_for_each_entry(entry, &vk->pid_ht[hash_idx].head, node) {
			if (entry->pid == pid)
				count++;
		}
	}

	spin_unlock(&vk->ctx_lock);

	return count;
}

int bcm_vk_open(struct inode *inode, struct file *p_file)
{
	struct bcm_vk_ctx *ctx;
	struct miscdevice *miscdev = (struct miscdevice *)p_file->private_data;
	struct bcm_vk *vk = container_of(miscdev, struct bcm_vk, miscdev);
	struct device *dev = &vk->pdev->dev;
	int rc = 0;

	/* get a context and set it up for file */
	ctx = bcm_vk_get_ctx(vk, task_tgid_nr(current));
	if (!ctx) {
		dev_err(dev, "Error allocating context\n");
		rc = -ENOMEM;
	} else {
		/*
		 * set up context and replace private data with context for
		 * other methods to use.  Reason for the context is because
		 * it is allowed for multiple sessions to open the sysfs, and
		 * for each file open, when upper layer query the response,
		 * only those that are tied to a specific open should be
		 * returned.  The context->idx will be used for such binding
		 */
		ctx->miscdev = miscdev;
		p_file->private_data = ctx;
		dev_dbg(dev, "ctx_returned with idx %d, pid %d\n",
			ctx->idx, ctx->pid);
	}
	return rc;
}

int bcm_vk_release(struct inode *inode, struct file *p_file)
{
	int ret;
	struct bcm_vk_ctx *ctx = p_file->private_data;
	struct bcm_vk *vk = container_of(ctx->miscdev, struct bcm_vk, miscdev);

	ret = bcm_vk_free_ctx(vk, ctx);

	kref_put(&vk->kref, bcm_vk_release_data);

	return ret;
}

