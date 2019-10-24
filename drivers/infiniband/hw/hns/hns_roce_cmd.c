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
#define CMD_TOKEN_MASK		0x1f

static int hns_roce_cmd_mbox_post_hw(struct hns_roce_dev *hr_dev, u64 in_param,
				     u64 out_param, u32 in_modifier,
				     u8 op_modifier, u16 op, u16 token,
				     int event)
{
	struct hns_roce_cmdq *cmd = &hr_dev->cmd;
	int ret;

	mutex_lock(&cmd->hcr_mutex);
	ret = hr_dev->hw->post_mbox(hr_dev, in_param, out_param, in_modifier,
				    op_modifier, op, token, event);
	mutex_unlock(&cmd->hcr_mutex);

	return ret;
}

/* this should be called with "poll_sem" */
static int __hns_roce_cmd_mbox_poll(struct hns_roce_dev *hr_dev, u64 in_param,
				    u64 out_param, unsigned long in_modifier,
				    u8 op_modifier, u16 op,
				    unsigned long timeout)
{
	struct device *dev = hr_dev->dev;
	int ret;

	ret = hns_roce_cmd_mbox_post_hw(hr_dev, in_param, out_param,
					in_modifier, op_modifier, op,
					CMD_POLL_TOKEN, 0);
	if (ret) {
		dev_err(dev, "[cmd_poll]hns_roce_cmd_mbox_post_hw failed\n");
		return ret;
	}

	return hr_dev->hw->chk_mbox(hr_dev, timeout);
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
	struct hns_roce_cmd_context *context;
	struct device *dev = hr_dev->dev;
	int ret;

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
	int ret;

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
	int ret;

	if (hr_dev->hw->rst_prc_mbox) {
		ret = hr_dev->hw->rst_prc_mbox(hr_dev);
		if (ret == CMD_RST_PRC_SUCCESS)
			return 0;
		else if (ret == CMD_RST_PRC_EBUSY)
			return -EBUSY;
	}

	if (hr_dev->cmd.use_events)
		ret = hns_roce_cmd_mbox_wait(hr_dev, in_param, out_param,
					     in_modifier, op_modifier, op,
					     timeout);
	else
		ret = hns_roce_cmd_mbox_poll(hr_dev, in_param, out_param,
					     in_modifier, op_modifier, op,
					     timeout);

	if (ret == CMD_RST_PRC_EBUSY)
		return -EBUSY;

	if (ret && (hr_dev->hw->rst_prc_mbox &&
		    hr_dev->hw->rst_prc_mbox(hr_dev) == CMD_RST_PRC_SUCCESS))
		return 0;

	return ret;
}

int hns_roce_cmd_init(struct hns_roce_dev *hr_dev)
{
	struct device *dev = hr_dev->dev;

	mutex_init(&hr_dev->cmd.hcr_mutex);
	sema_init(&hr_dev->cmd.poll_sem, 1);
	hr_dev->cmd.use_events = 0;
	hr_dev->cmd.toggle = 1;
	hr_dev->cmd.max_cmds = CMD_MAX_NUM;
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

	hr_cmd->context = kmalloc_array(hr_cmd->max_cmds,
					sizeof(*hr_cmd->context),
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
