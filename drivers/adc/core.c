/* drivers/adc/core.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/adc.h>


static struct adc_host *g_adc = NULL;

struct adc_host *adc_alloc_host(int extra, struct device *dev)
{
	struct adc_host *adc;
	
	adc = kzalloc(sizeof(struct adc_host) + extra, GFP_KERNEL);
	if (!adc)
		return NULL;
	adc->dev = dev;
	g_adc = adc;
	return adc;
}
EXPORT_SYMBOL(adc_alloc_host);
void adc_free_host(struct adc_host *adc)
{
	kfree(adc);
	adc = NULL;
	return;
}
EXPORT_SYMBOL(adc_free_host);


struct adc_client *adc_register(int chn,
				void (*callback)(struct adc_client *, void *, int), 
				void *callback_param)
{
	struct adc_client *client;
	if(!g_adc)
	{
		printk(KERN_ERR "adc host has not initialized\n");
		return NULL;
	}
	if(chn >= MAX_ADC_CHN)
	{
		dev_err(g_adc->dev, "channel[%d] is greater than the maximum[%d]\n", chn, MAX_ADC_CHN);
		return NULL;
	}
	client = kzalloc(sizeof(struct adc_client), GFP_KERNEL);
	if(!client)
	{
		dev_err(g_adc->dev, "no memory for adc client\n");
		return NULL;
	}
	client->callback = callback;
	client->callback_param = callback_param;
	client->chn = chn;

	client->adc = g_adc;

	return client;
}
EXPORT_SYMBOL(adc_register);

void adc_unregister(struct adc_client *client)
{
	kfree(client);
	client = NULL;
	return;
}
EXPORT_SYMBOL(adc_unregister);


static void trigger_next_adc_job_if_any(struct adc_host *adc)
{
	int head = adc->queue_head;

	if (!adc->queue[head])
		return;
	adc->cur = adc->queue[head]->client;
	adc->ops->start(adc);
	return;
}

static int
adc_enqueue_request(struct adc_host *adc, struct adc_request *req)
{
	int head, tail;
	unsigned long flags;
	
	spin_lock_irqsave(&adc->lock, flags);
	head = adc->queue_head;
	tail = adc->queue_tail;

	if (adc->queue[tail]) {
		spin_unlock_irqrestore(&adc->lock,flags);
		dev_err(adc->dev, "ADC queue is full, dropping request\n");
		return -EBUSY;
	}

	adc->queue[tail] = req;
	if (head == tail)
		trigger_next_adc_job_if_any(adc);
	adc->queue_tail = (tail + 1) & (MAX_ADC_FIFO_DEPTH - 1);

	spin_unlock_irqrestore(&adc->lock,flags);

	return 0;
}

static void
adc_sync_read_callback(struct adc_client *client, void *param, int result)
{
	struct adc_request *req = param;

	client->result = result;
	complete(&req->completion);
}

int adc_sync_read(struct adc_client *client)
{
	struct adc_request *req = NULL;
	int err, tmo, tail;

	if(client == NULL) {
		printk(KERN_ERR "client point is NULL");
		return -EINVAL;
	}
	if(client->adc->is_suspended == 1) {
		dev_dbg(client->adc->dev, "system enter sleep\n");
		return -1;
	}
	req = kzalloc(sizeof(*req), GFP_ATOMIC);
	if (!req){
		dev_err(client->adc->dev, "no memory for adc request\n");
		return -ENOMEM;
	}
	req->chn = client->chn;
	req->callback =  adc_sync_read_callback;
	req->callback_param = req;
	req->client = client;
	req->status = SYNC_READ;

	init_completion(&req->completion);
	err = adc_enqueue_request(client->adc, req);
	if (err)
	{
		dev_err(client->adc->dev, "fail to enqueue request\n");
		kfree(req);
		return err;
	}
	tmo = wait_for_completion_timeout(&req->completion,msecs_to_jiffies(100));
	kfree(req);
	req = NULL;
	if(tmo == 0) {
		tail = (client->adc->queue_tail - 1) & (MAX_ADC_FIFO_DEPTH - 1);
		client->adc->queue[tail] = NULL;
		client->adc->queue_tail = tail;
		return -ETIMEDOUT;
	}
	return client->result;
}
EXPORT_SYMBOL(adc_sync_read);

int adc_async_read(struct adc_client *client)
{
	int ret = 0;
	struct adc_request *req = NULL;
	
	if(client == NULL) {
		printk(KERN_ERR "client point is NULL");
		return -EINVAL;
	}
	if(client->adc->is_suspended == 1) {
		dev_dbg(client->adc->dev, "system enter sleep\n");
		return -1;
	}
	req = kzalloc(sizeof(*req), GFP_ATOMIC);
	if (!req) {
		dev_err(client->adc->dev, "no memory for adc request\n");
		return -ENOMEM;
	}
	req->chn = client->chn;
	req->callback = client->callback;
	req->callback_param = client->callback_param;
	req->client = client;
	req->status = ASYNC_READ;

	ret = adc_enqueue_request(client->adc, req);
	if(ret < 0)
		kfree(req);

	return ret;
}
EXPORT_SYMBOL(adc_async_read);

void adc_core_irq_handle(struct adc_host *adc)
{
	struct adc_request *req;
	int head, res;
	spin_lock(&adc->lock);
	head = adc->queue_head;

	req = adc->queue[head];
	if (WARN_ON(!req)) {
		spin_unlock(&adc->lock);
		dev_err(adc->dev, "adc irq: ADC queue empty!\n");
		return;
	}
	adc->queue[head] = NULL;
	adc->queue_head = (head + 1) & (MAX_ADC_FIFO_DEPTH - 1);
	
	res = adc->ops->read(adc);
	adc->ops->stop(adc);
	trigger_next_adc_job_if_any(adc);

	req->callback(adc->cur, req->callback_param, res);
	if(req->status == ASYNC_READ) {
		kfree(req);
		req = NULL;
	}
	spin_unlock(&adc->lock);
}
EXPORT_SYMBOL(adc_core_irq_handle);


