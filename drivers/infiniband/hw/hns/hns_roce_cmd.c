/*
 * Copyright (c) 2016 Hisilicon Limited.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/dmapool.h>
#include <linux/platform_device.h>
#include "hns_roce_common.h"
#include "hns_roce_device.h"
#include "hns_roce_cmd.h"

#define CMD_POLL_TOKEN		0xffff
#define CMD_MAX_NUM		32
#define STATUS_MASK		0xff
#define CMD_TOKEN_MASK		0x1f
#define GO_BIT_TIMEOUT_MSECS	10000

enum {
	HCR_TOKEN_OFFSET	= 0x14,
	HCR_STATUS_OFFSET	= 0x18,
	HCR_GO_BIT		= 15,
};

static int cmd_pending(struct hns_roce_dev *hr_dev)
{
	u32 status = readl(hr_dev->cmd.hcr + HCR_TOKEN_OFFSET);

	return (!!(status & (1 << HCR_GO_BIT)));
}

/* this function should be serialized with "hcr_mutex" */
static int __hns_roce_cmd_mbox_post_hw(struct hns_roce_dev *hr_dev,
				       u64 in_param, u64 out_param,
				       u32 in_modifier, u8 op_modifier, u16 op,
				       u16 token, int event)
{
	struct hns_roce_cmdq *cmd = &hr_dev->cmd;
	struct device *dev = &hr_dev->pdev->dev;
	u32 __iomem *hcr = (u32 *)cmd->hcr;
	int ret = -EAGAIN;
	unsigned long end;
	u32 val = 0;

	end = msecs_to_jiffies(GO_BIT_TIMEOUT_MSECS) + jiffies;
	while (cmd_pending(hr_dev)) {
		if (time_after(jiffies, end)) {
			dev_dbg(dev, "jiffies=%d end=%d\n", (int)jiffies,
				(int)end);
			goto out;
		}
		cond_resched();
	}

	roce_set_field(val, ROCEE_MB6_ROCEE_MB_CMD_M, ROCEE_MB6_ROCEE_MB_CMD_S,
		       op);
	roce_set_field(val, ROCEE_MB6_ROCEE_MB_CMD_MDF_M,
		       ROCEE_MB6_ROCEE_MB_CMD_MDF_S, op_modifier);
	roce_set_bit(val, ROCEE_MB6_ROCEE_MB_EVENT_S, event);
	roce_set_bit(val, ROCEE_MB6_ROCEE_MB_HW_RUN_S, 1);
	roce_set_field(val, ROCEE_MB6_ROCEE_MB_TOKEN_M,
		       ROCEE_MB6_ROCEE_MB_TOKEN_S, token);

	__raw_writeq(cpu_to_le64(in_param), hcr + 0);
	__raw_writeq(cpu_to_le64(out_param), hcr + 2);
	__raw_writel(cpu_to_le32(in_modifier), hcr + 4);
	/* Memory barrier */
	wmb();

	__raw_writel(cpu_to_le32(val), hcr + 5);

	mmiowb();
	ret = 0;

out:
	return ret;
}

static int hns_roce_cmd_mbox_post_hw(struct hns_roce_dev *hr_dev, u64 in_param,
				     u64 out_param, u32 in_modifier,
				     u8 op_modifier, u16 op, u16 token,
				     int event)
{
	struct hns_roce_cmdq *cmd = &hr_dev->cmd;
	int ret = -EAGAIN;

	mutex_lock(&cmd->hcr_mutex);
	ret = __hns_roce_cmd_mbox_post_hw(hr_dev, in_param, out_param,
					  in_modifier, op_modifier, op, token,
					  event);
	mutex_unlock(&cmd->hcr_mutex);

	return ret;
}

/* this should be called with "poll_sem" */
static int __hns_roce_cmd_mbox_poll(struct hns_roce_dev *hr_dev, u64 in_param,
				    u64 out_param, unsigned long in_modifier,
				    u8 op_modifier, u16 op,
				    unsigned long timeout)
{
	struct device *dev = &hr_dev->pdev->dev;
	u8 __iomem *hcr = hr_dev->cmd.hcr;
	unsigned long end = 0;
	u32 status = 0;
	int ret;

	ret = hns_roce_cmd_mbox_post_hw(hr_dev, in_param, out_param,
					in_modifier, op_modifier, op,
					CMD_POLL_TOKEN, 0);
	if (ret) {
		dev_err(dev, "[cmd_poll]hns_roce_cmd_mbox_post_hw failed\n");
		goto out;
	}

	end = msecs_to_jiffies(timeout) + jiffies;
	while (cmd_pending(hr_dev) && time_before(jiffies, end))
		cond_resched();

	if (cmd_pending(hr_dev)) {
		dev_err(dev, "[cmd_poll]hw run cmd TIMEDOUT!\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	status = le32_to_cpu((__force __be32)
			      __raw_readl(hcr + HCR_STATUS_OFFSET));
	if ((status & STATUS_MASK) != 0x1) {
		dev_err(dev, "mailbox status 0x%x!\n", status);
		ret = -EBUSY;
		goto out;
	}

out:
	return ret;
}

static int hns_roce_cmd_mbox_poll(struct hns_roce_dev *hr_dev, u64 in_param,
				  u64 out_param, unsigned long in_modifier,
				  u8 op_modifier, u16 op, unsigned long timeout)
{
	int ret;

	down(&hr_dev->cmd.poll_sem);
	ret = __hns_roce_cmd_mbox_poll(hr_dev, in_param, out_param, in_modifier,
				       op_modifier, op, timeout);
	up(&hr_dev->cmd.poll_sem);

	return ret;
}

void hns_roce_cmd_event(struct hns_roce_dev *hr_dev, u16 token, u8 status,
			u64 out_param)
{
	struct hns_roce_cmd_context
		*context = &hr_dev->cmd.context[token & hr_dev->cmd.token_mask];

	if (token != context->token)
		return;

	context->result = (status == HNS_ROCE_CMD_SUCCESS) ? 0 : (-EIO);
	context->out_param = out_param;
	complete(&context->done);
}

/* this should be called with "use_events" */
static int __hns_roce_cmd_mbox_wait(struct hns_roce_dev *hr_dev, u64 in_param,
				    u64 out_param, unsigned long in_modifier,
				    u8 op_modifier, u16 op,
				    unsigned long timeout)
{
	struct hns_roce_cmdq *cmd = &hr_dev->cmd;
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_cmd_context *context;
	int ret = 0;

	spin_lock(&cmd->context_lock);
	WARN_ON(cmd->free_head < 0);
	context = &cmd->context[cmd->free_head];
	context->token += cmd->token_mask + 1;
	cmd->free_head = context->next;
	spin_unlock(&cmd->context_lock);

	init_completion(&context->done);

	ret = hns_roce_cmd_mbox_post_hw(hr_dev, in_param, out_param,
					in_modifier, op_modifier, op,
					context->token, 1);
	if (ret)
		goto out;

	/*
	 * It is timeout when wait_for_completion_timeout return 0
	 * The return value is the time limit set in advance
	 * how many seconds showing
	 */
	if (!wait_for_completion_timeout(&context->done,
					 msecs_to_jiffies(timeout))) {
		dev_err(dev, "[cmd]wait_for_completion_timeout timeout\n");
		ret = -EBUSY;
		goto out;
	}

	ret = context->result;
	if (ret) {
		dev_err(dev, "[cmd]event mod cmd process error!err=%d\n", ret);
		goto out;
	}

out:
	spin_lock(&cmd->context_lock);
	context->next = cmd->free_head;
	cmd->free_head = context - cmd->context;
	spin_unlock(&cmd->context_lock);

	return ret;
}

static int hns_roce_cmd_mbox_wait(struct hns_roce_dev *hr_dev, u64 in_param,
				  u64 out_param, unsigned long in_modifier,
				  u8 op_modifier, u16 op, unsigned long timeout)
{
	int ret = 0;

	down(&hr_dev->cmd.event_sem);
	ret = __hns_roce_cmd_mbox_wait(hr_dev, in_param, out_param,
				       in_modifier, op_modifier, op, timeout);
	up(&hr_dev->cmd.event_sem);

	return ret;
}

int hns_roce_cmd_mbox(struct hns_roce_dev *hr_dev, u64 in_param, u64 out_param,
		      unsigned long in_modifier, u8 op_modifier, u16 op,
		      unsigned long timeout)
{
	if (hr_dev->cmd.use_events)
		return hns_roce_cmd_mbox_wait(hr_dev, in_param, out_param,
					      in_modifier, op_modifier, op,
					      timeout);
	else
		return hns_roce_cmd_mbox_poll(hr_dev, in_param, out_param,
					      in_modifier, op_modifier, op,
					      timeout);
}

int hns_roce_cmd_init(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;

	mutex_init(&hr_dev->cmd.hcr_mutex);
	sema_init(&hr_dev->cmd.poll_sem, 1);
	hr_dev->cmd.use_events = 0;
	hr_dev->cmd.toggle = 1;
	hr_dev->cmd.max_cmds = CMD_MAX_NUM;
	hr_dev->cmd.hcr = hr_dev->reg_base + ROCEE_MB1_REG;
	hr_dev->cmd.pool = dma_pool_create("hns_roce_cmd", dev,
					   HNS_ROCE_MAILBOX_SIZE,
					   HNS_ROCE_MAILBOX_SIZE, 0);
	if (!hr_dev->cmd.pool)
		return -ENOMEM;

	return 0;
}

void hns_roce_cmd_cleanup(struct hns_roce_dev *hr_dev)
{
	dma_pool_destroy(hr_dev->cmd.pool);
}

int hns_roce_cmd_use_events(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cmdq *hr_cmd = &hr_dev->cmd;
	int i;

	hr_cmd->context = kmalloc(hr_cmd->max_cmds *
				  sizeof(struct hns_roce_cmd_context),
				  GFP_KERNEL);
	if (!hr_cmd->context)
		return -ENOMEM;

	for (i = 0; i < hr_cmd->max_cmds; ++i) {
		hr_cmd->context[i].token = i;
		hr_cmd->context[i].next = i + 1;
	}

	hr_cmd->context[hr_cmd->max_cmds - 1].next = -1;
	hr_cmd->free_head = 0;

	sema_init(&hr_cmd->event_sem, hr_cmd->max_cmds);
	spin_lock_init(&hr_cmd->context_lock);

	hr_cmd->token_mask = CMD_TOKEN_MASK;
	hr_cmd->use_events = 1;

	down(&hr_cmd->poll_sem);

	return 0;
}

void hns_roce_cmd_use_polling(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cmdq *hr_cmd = &hr_dev->cmd;
	int i;

	hr_cmd->use_events = 0;

	for (i = 0; i < hr_cmd->max_cmds; ++i)
		down(&hr_cmd->event_sem);

	kfree(hr_cmd->context);
	up(&hr_cmd->poll_sem);
}

struct hns_roce_cmd_mailbox
	*hns_roce_alloc_cmd_mailbox(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cmd_mailbox *mailbox;

	mailbox = kmalloc(sizeof(*mailbox), GFP_KERNEL);
	if (!mailbox)
		return ERR_PTR(-ENOMEM);

	mailbox->buf = dma_pool_alloc(hr_dev->cmd.pool, GFP_KERNEL,
				      &mailbox->dma);
	if (!mailbox->buf) {
		kfree(mailbox);
		return ERR_PTR(-ENOMEM);
	}

	return mailbox;
}

void hns_roce_free_cmd_mailbox(struct hns_roce_dev *hr_dev,
			       struct hns_roce_cmd_mailbox *mailbox)
{
	if (!mailbox)
		return;

	dma_pool_free(hr_dev->cmd.pool, mailbox->buf, mailbox->dma);
	kfree(mailbox);
}
