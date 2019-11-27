// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2018 MediaTek Inc.

#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/mailbox_controller.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#define CMDQ_ARG_A_WRITE_MASK	0xffff
#define CMDQ_WRITE_ENABLE_MASK	BIT(0)
#define CMDQ_EOC_IRQ_EN		BIT(0)
#define CMDQ_EOC_CMD		((u64)((CMDQ_CODE_EOC << CMDQ_OP_CODE_SHIFT)) \
				<< 32 | CMDQ_EOC_IRQ_EN)

static void cmdq_client_timeout(struct timer_list *t)
{
	struct cmdq_client *client = from_timer(client, t, timer);

	dev_err(client->client.dev, "cmdq timeout!\n");
}

struct cmdq_client *cmdq_mbox_create(struct device *dev, int index, u32 timeout)
{
	struct cmdq_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return (struct cmdq_client *)-ENOMEM;

	client->timeout_ms = timeout;
	if (timeout != CMDQ_NO_TIMEOUT) {
		spin_lock_init(&client->lock);
		timer_setup(&client->timer, cmdq_client_timeout, 0);
	}
	client->pkt_cnt = 0;
	client->client.dev = dev;
	client->client.tx_block = false;
	client->chan = mbox_request_channel(&client->client, index);

	if (IS_ERR(client->chan)) {
		long err;

		dev_err(dev, "failed to request channel\n");
		err = PTR_ERR(client->chan);
		kfree(client);

		return ERR_PTR(err);
	}

	return client;
}
EXPORT_SYMBOL(cmdq_mbox_create);

void cmdq_mbox_destroy(struct cmdq_client *client)
{
	if (client->timeout_ms != CMDQ_NO_TIMEOUT) {
		spin_lock(&client->lock);
		del_timer_sync(&client->timer);
		spin_unlock(&client->lock);
	}
	mbox_free_channel(client->chan);
	kfree(client);
}
EXPORT_SYMBOL(cmdq_mbox_destroy);

struct cmdq_pkt *cmdq_pkt_create(struct cmdq_client *client, size_t size)
{
	struct cmdq_pkt *pkt;
	struct device *dev;
	dma_addr_t dma_addr;

	pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
	if (!pkt)
		return ERR_PTR(-ENOMEM);
	pkt->va_base = kzalloc(size, GFP_KERNEL);
	if (!pkt->va_base) {
		kfree(pkt);
		return ERR_PTR(-ENOMEM);
	}
	pkt->buf_size = size;
	pkt->cl = (void *)client;

	dev = client->chan->mbox->dev;
	dma_addr = dma_map_single(dev, pkt->va_base, pkt->buf_size,
				  DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_addr)) {
		dev_err(dev, "dma map failed, size=%u\n", (u32)(u64)size);
		kfree(pkt->va_base);
		kfree(pkt);
		return ERR_PTR(-ENOMEM);
	}

	pkt->pa_base = dma_addr;

	return pkt;
}
EXPORT_SYMBOL(cmdq_pkt_create);

void cmdq_pkt_destroy(struct cmdq_pkt *pkt)
{
	struct cmdq_client *client = (struct cmdq_client *)pkt->cl;

	dma_unmap_single(client->chan->mbox->dev, pkt->pa_base, pkt->buf_size,
			 DMA_TO_DEVICE);
	kfree(pkt->va_base);
	kfree(pkt);
}
EXPORT_SYMBOL(cmdq_pkt_destroy);

static int cmdq_pkt_append_command(struct cmdq_pkt *pkt, enum cmdq_code code,
				   u32 arg_a, u32 arg_b)
{
	u64 *cmd_ptr;

	if (unlikely(pkt->cmd_buf_size + CMDQ_INST_SIZE > pkt->buf_size)) {
		/*
		 * In the case of allocated buffer size (pkt->buf_size) is used
		 * up, the real required size (pkt->cmdq_buf_size) is still
		 * increased, so that the user knows how much memory should be
		 * ultimately allocated after appending all commands and
		 * flushing the command packet. Therefor, the user can call
		 * cmdq_pkt_create() again with the real required buffer size.
		 */
		pkt->cmd_buf_size += CMDQ_INST_SIZE;
		WARN_ONCE(1, "%s: buffer size %u is too small !\n",
			__func__, (u32)pkt->buf_size);
		return -ENOMEM;
	}
	cmd_ptr = pkt->va_base + pkt->cmd_buf_size;
	(*cmd_ptr) = (u64)((code << CMDQ_OP_CODE_SHIFT) | arg_a) << 32 | arg_b;
	pkt->cmd_buf_size += CMDQ_INST_SIZE;

	return 0;
}

int cmdq_pkt_write(struct cmdq_pkt *pkt, u8 subsys, u16 offset, u32 value)
{
	u32 arg_a = (offset & CMDQ_ARG_A_WRITE_MASK) |
		    (subsys << CMDQ_SUBSYS_SHIFT);

	return cmdq_pkt_append_command(pkt, CMDQ_CODE_WRITE, arg_a, value);
}
EXPORT_SYMBOL(cmdq_pkt_write);

int cmdq_pkt_write_mask(struct cmdq_pkt *pkt, u8 subsys,
			u16 offset, u32 value, u32 mask)
{
	u32 offset_mask = offset;
	int err = 0;

	if (mask != 0xffffffff) {
		err = cmdq_pkt_append_command(pkt, CMDQ_CODE_MASK, 0, ~mask);
		offset_mask |= CMDQ_WRITE_ENABLE_MASK;
	}
	err |= cmdq_pkt_write(pkt, subsys, offset_mask, value);

	return err;
}
EXPORT_SYMBOL(cmdq_pkt_write_mask);

int cmdq_pkt_wfe(struct cmdq_pkt *pkt, u16 event)
{
	u32 arg_b;

	if (event >= CMDQ_MAX_EVENT)
		return -EINVAL;

	/*
	 * WFE arg_b
	 * bit 0-11: wait value
	 * bit 15: 1 - wait, 0 - no wait
	 * bit 16-27: update value
	 * bit 31: 1 - update, 0 - no update
	 */
	arg_b = CMDQ_WFE_UPDATE | CMDQ_WFE_WAIT | CMDQ_WFE_WAIT_VALUE;

	return cmdq_pkt_append_command(pkt, CMDQ_CODE_WFE, event, arg_b);
}
EXPORT_SYMBOL(cmdq_pkt_wfe);

int cmdq_pkt_clear_event(struct cmdq_pkt *pkt, u16 event)
{
	if (event >= CMDQ_MAX_EVENT)
		return -EINVAL;

	return cmdq_pkt_append_command(pkt, CMDQ_CODE_WFE, event,
				       CMDQ_WFE_UPDATE);
}
EXPORT_SYMBOL(cmdq_pkt_clear_event);

static int cmdq_pkt_finalize(struct cmdq_pkt *pkt)
{
	int err;

	/* insert EOC and generate IRQ for each command iteration */
	err = cmdq_pkt_append_command(pkt, CMDQ_CODE_EOC, 0, CMDQ_EOC_IRQ_EN);

	/* JUMP to end */
	err |= cmdq_pkt_append_command(pkt, CMDQ_CODE_JUMP, 0, CMDQ_JUMP_PASS);

	return err;
}

static void cmdq_pkt_flush_async_cb(struct cmdq_cb_data data)
{
	struct cmdq_pkt *pkt = (struct cmdq_pkt *)data.data;
	struct cmdq_task_cb *cb = &pkt->cb;
	struct cmdq_client *client = (struct cmdq_client *)pkt->cl;

	if (client->timeout_ms != CMDQ_NO_TIMEOUT) {
		unsigned long flags = 0;

		spin_lock_irqsave(&client->lock, flags);
		if (--client->pkt_cnt == 0)
			del_timer(&client->timer);
		else
			mod_timer(&client->timer, jiffies +
				  msecs_to_jiffies(client->timeout_ms));
		spin_unlock_irqrestore(&client->lock, flags);
	}

	dma_sync_single_for_cpu(client->chan->mbox->dev, pkt->pa_base,
				pkt->cmd_buf_size, DMA_TO_DEVICE);
	if (cb->cb) {
		data.data = cb->data;
		cb->cb(data);
	}
}

int cmdq_pkt_flush_async(struct cmdq_pkt *pkt, cmdq_async_flush_cb cb,
			 void *data)
{
	int err;
	unsigned long flags = 0;
	struct cmdq_client *client = (struct cmdq_client *)pkt->cl;

	err = cmdq_pkt_finalize(pkt);
	if (err < 0)
		return err;

	pkt->cb.cb = cb;
	pkt->cb.data = data;
	pkt->async_cb.cb = cmdq_pkt_flush_async_cb;
	pkt->async_cb.data = pkt;

	dma_sync_single_for_device(client->chan->mbox->dev, pkt->pa_base,
				   pkt->cmd_buf_size, DMA_TO_DEVICE);

	if (client->timeout_ms != CMDQ_NO_TIMEOUT) {
		spin_lock_irqsave(&client->lock, flags);
		if (client->pkt_cnt++ == 0)
			mod_timer(&client->timer, jiffies +
				  msecs_to_jiffies(client->timeout_ms));
		spin_unlock_irqrestore(&client->lock, flags);
	}

	mbox_send_message(client->chan, pkt);
	/* We can send next packet immediately, so just call txdone. */
	mbox_client_txdone(client->chan, 0);

	return 0;
}
EXPORT_SYMBOL(cmdq_pkt_flush_async);

struct cmdq_flush_completion {
	struct completion cmplt;
	bool err;
};

static void cmdq_pkt_flush_cb(struct cmdq_cb_data data)
{
	struct cmdq_flush_completion *cmplt;

	cmplt = (struct cmdq_flush_completion *)data.data;
	if (data.sta != CMDQ_CB_NORMAL)
		cmplt->err = true;
	else
		cmplt->err = false;
	complete(&cmplt->cmplt);
}

int cmdq_pkt_flush(struct cmdq_pkt *pkt)
{
	struct cmdq_flush_completion cmplt;
	int err;

	init_completion(&cmplt.cmplt);
	err = cmdq_pkt_flush_async(pkt, cmdq_pkt_flush_cb, &cmplt);
	if (err < 0)
		return err;
	wait_for_completion(&cmplt.cmplt);

	return cmplt.err ? -EFAULT : 0;
}
EXPORT_SYMBOL(cmdq_pkt_flush);

MODULE_LICENSE("GPL v2");
