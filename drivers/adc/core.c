/* drivers/adc/core.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/
#include <linux/adc.h>
#include "adc_priv.h"

struct list_head adc_host_head;

struct adc_host *adc_alloc_host(struct device *dev, int extra, enum host_chn_mask mask)
{
	struct adc_host *adc;
	
	adc = kzalloc(sizeof(struct adc_host) + extra, GFP_KERNEL);
	if (!adc)
		return NULL;
        adc->mask = mask;
	adc->dev = dev;
        spin_lock_init(&adc->lock);
        INIT_LIST_HEAD(&adc->request_head);

        list_add_tail(&adc->entry, &adc_host_head);

	return adc;
}

void adc_free_host(struct adc_host *adc)
{
        list_del(&adc->entry);
	kfree(adc);
	adc = NULL;
	return;
}

struct adc_client *adc_register(int chn,
				void (*callback)(struct adc_client *, void *, int), 
				void *callback_param)

{
        struct adc_client *client = NULL;
        struct adc_host *adc = NULL;

        list_for_each_entry(adc, &adc_host_head, entry) {
                if((chn == 0 && adc->mask == SARADC_CHN_MASK) ||
                (chn & adc->mask)){
	                client = kzalloc(sizeof(struct adc_client), GFP_KERNEL);
                        if(!client)
                                return NULL;
	                client->callback = callback;
	                client->callback_param = callback_param;
	                client->chn = chn;
	                client->adc = adc;
                        client->index = adc->client_count;
                        init_waitqueue_head(&client->wait);
                        adc->client_count++;

                        return client;
                }
        }
        dev_err(adc->dev, "chn(%d) is not support\n", chn);
        return NULL;
}

void adc_unregister(struct adc_client *client)
{
        struct adc_host *adc = client->adc;

        adc->client_count--;
	kfree(client);
	client = NULL;
	return;
}

static inline void trigger_next_adc_job_if_any(struct adc_host *adc)
{
        struct adc_request *req = NULL;

        if(list_empty(&adc->request_head))
                return;

        req = list_first_entry(&adc->request_head, struct adc_request, entry);

        if(req == NULL)
                return;
        list_del(&req->entry);
	adc->cur = req->client;
	kfree(req);
	adc->ops->start(adc);
	return;
}
static int adc_request_add(struct adc_host *adc, struct adc_client *client)
{
        struct adc_request *req = NULL;

        list_for_each_entry(req, &adc->request_head, entry) {
                if(req->client->index == client->index)
                        return 0;
        }
        req = kzalloc(sizeof(struct adc_request), GFP_KERNEL);

        if(!req)
                return -ENOMEM;
        req->client = client;
        list_add_tail(&req->entry, &adc->request_head);

        trigger_next_adc_job_if_any(adc);

        return 0;
}
static void
adc_sync_read_callback(struct adc_client *client, void *param, int result)
{
        client->result = result;
}
void adc_core_irq_handle(struct adc_host *adc)
{
        int result = adc->ops->read(adc);

        adc->ops->stop(adc);
        adc->cur->callback(adc->cur, adc->cur->callback_param, result);
        adc_sync_read_callback(adc->cur, NULL, result);
        adc->cur->is_finished = 1;
        wake_up(&adc->cur->wait);

        trigger_next_adc_job_if_any(adc);
}

int adc_host_read(struct adc_client *client, enum read_type type)
{
        int tmo;
	unsigned long flags;
        struct adc_host *adc = NULL;

	if(client == NULL) {
		printk(KERN_ERR "client is NULL");
		return -EINVAL;
	}
        adc = client->adc;
	if(adc->is_suspended == 1) {
		dev_dbg(adc->dev, "system enter sleep\n");
		return -EIO;
	}

	spin_lock_irqsave(&adc->lock, flags);
        adc_request_add(adc, client);
        client->is_finished = 0;
	spin_unlock_irqrestore(&adc->lock, flags);

        if(type == ADC_ASYNC_READ)
                return 0;

        tmo = wait_event_timeout(client->wait, ( client->is_finished == 1 ), msecs_to_jiffies(ADC_READ_TMO));
        if(tmo <= 0) {
                adc->ops->stop(adc);
                dev_dbg(adc->dev, "get adc value timeout\n");
                return -ETIMEDOUT;
        } 

        return client->result;
}

int adc_sync_read(struct adc_client *client)
{
        return adc_host_read(client, ADC_SYNC_READ);
}

int adc_async_read(struct adc_client *client)
{
        return adc_host_read(client, ADC_ASYNC_READ);
}


static int __init adc_core_init(void)
{
        INIT_LIST_HEAD(&adc_host_head);
        return 0;
}
subsys_initcall(adc_core_init);

static void __exit adc_core_exit(void)
{
        return;
}
module_exit(adc_core_exit);  


