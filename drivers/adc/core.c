/* drivers/adc/core.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
*/
#include <linux/adc.h>
#include "adc_priv.h"

struct adc_host *g_adc = NULL;
struct list_head adc_host_head;

struct adc_host *adc_alloc_host(struct device *dev, int extra, enum host_chn_mask mask)
{
	struct adc_host *adc;
	
	adc = kzalloc(sizeof(struct adc_host) + extra, GFP_KERNEL);
	if (!adc)
		return NULL;
        adc->mask = mask;
	adc->dev = dev;
        adc->chn = -1;
        spin_lock_init(&adc->lock);
        mutex_init(&adc->m_lock);
        INIT_LIST_HEAD(&adc->req_head);
        INIT_LIST_HEAD(&adc->callback_head);

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

        if(chn < 0)
                return NULL;
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
EXPORT_SYMBOL(adc_register);

void adc_unregister(struct adc_client *client)
{
        struct adc_host *adc = client->adc;

        adc->client_count--;
	kfree(client);
	client = NULL;
	return;
}
EXPORT_SYMBOL(adc_unregister);

static inline void trigger_next_adc_job_if_any(struct adc_host *adc)
{
        struct adc_request *req = NULL;

        req = list_first_entry(&adc->req_head, struct adc_request, entry);
        if(req){
                if(req->client == NULL){
                        dev_err(adc->dev, "Abnormal: client piont is NULL...............\n");
                        return;
                }
                adc->chn = req->client->chn;
        	adc->ops->start(adc);
        }

	return;
}

static int adc_request_add(struct adc_host *adc, struct adc_client *client)
{
        struct adc_request *req = NULL;

        req = kzalloc(sizeof(struct adc_request), GFP_ATOMIC);

        if(unlikely(!req))
                return -ENOMEM;
        INIT_LIST_HEAD(&req->entry);
        req->client = client;
        list_add_tail(&req->entry, &adc->req_head);
        if(adc->chn == -1)
                trigger_next_adc_job_if_any(adc);
        return 0;
}
static void
adc_sync_read_callback(struct adc_client *client, void *param, int result)
{
        client->result = result;
}

static void adc_callback(struct adc_host *adc)
{
        struct adc_request *req = NULL, *n = NULL;

        list_for_each_entry_safe(req, n, &adc->callback_head, entry) {
                if(req->client->flags & (1<<ADC_ASYNC_READ)){
                        req->client->callback(req->client, req->client->callback_param, req->client->result);
                }
                if(req->client->flags & (1<<ADC_SYNC_READ)){
                        adc_sync_read_callback(req->client, NULL, req->client->result);
                        req->client->is_finished = 1;
                        wake_up(&req->client->wait);
                }
                req->client->flags = 0;
                list_del_init(&req->entry);
                kfree(req);
        }
}

void adc_finished(struct adc_host *adc, int result)
{
	unsigned long flags;
        struct adc_request *req = NULL, *n = NULL;

        adc->ops->stop(adc);
        udelay(SAMPLE_RATE);
	spin_lock_irqsave(&adc->lock, flags);
        list_for_each_entry_safe(req, n, &adc->req_head, entry) {
                if(req->client->chn == adc->chn){
                        req->client->result = result;
                        list_move_tail(&req->entry, &adc->callback_head);
                }
        }
        adc->chn = -1;
        if(!list_empty(&adc->req_head))
                trigger_next_adc_job_if_any(adc);
	spin_unlock_irqrestore(&adc->lock, flags);

        adc_callback(adc);
}

void adc_core_irq_handle(struct adc_host *adc)
{
        int result = 0;

        WARN_ON(adc->chn == -1);

        mutex_lock(&adc->m_lock);

        result = adc->ops->read(adc);
        adc_dbg(adc->dev, "chn[%d] read value: %d\n", adc->chn, result);

        adc_finished(adc, result);

        mutex_unlock(&adc->m_lock);
}

int adc_host_read(struct adc_client *client, enum read_type type)
{
        int tmo, ret = 0;
	unsigned long flags;
        struct adc_host *adc = NULL;

	if(client == NULL) {
		printk(KERN_ERR "client is NULL");
		return -EINVAL;
	}
        adc = client->adc;
	if(adc->is_suspended == 1) {
		dev_err(adc->dev, "adc is in suspend state\n");
		return -EIO;
	}

	spin_lock_irqsave(&adc->lock, flags);
        if(client->flags & (1<<type)){
	        spin_unlock_irqrestore(&adc->lock, flags);
                adc_dbg(adc->dev, "req is exist: %s, client->index: %d\n", 
                                (type == ADC_ASYNC_READ)?"async_read":"sync_read", client->index);
                return -EEXIST;
        }else if(client->flags != 0){
                client->flags |= 1<<type;
        }else{
                client->flags = 1<<type;
                ret = adc_request_add(adc, client);
                if(ret < 0){
                        spin_unlock_irqrestore(&adc->lock, flags);
                        dev_err(adc->dev, "fail to add request\n");
                        return ret;
                }
        }
        if(type == ADC_ASYNC_READ){
	        spin_unlock_irqrestore(&adc->lock, flags);
                return 0;
        }
        client->is_finished = 0;
	spin_unlock_irqrestore(&adc->lock, flags);

        tmo = wait_event_timeout(client->wait, ( client->is_finished == 1 ), msecs_to_jiffies(ADC_READ_TMO));
        mutex_lock(&adc->m_lock);
        if(unlikely((tmo <= 0) && (client->is_finished == 0))) {
                dev_err(adc->dev, "get adc value timeout.................................\n");
                if(adc->ops->dump)
                        adc->ops->dump(adc);
                adc_finished(adc, -1);
                mutex_unlock(&adc->m_lock);
                return -ETIMEDOUT;
        } 
        mutex_unlock(&adc->m_lock);

        return client->result;
}

int adc_sync_read(struct adc_client *client)
{
        return adc_host_read(client, ADC_SYNC_READ);
}
EXPORT_SYMBOL(adc_sync_read);

int adc_async_read(struct adc_client *client)
{
        return adc_host_read(client, ADC_ASYNC_READ);
}

EXPORT_SYMBOL(adc_async_read);

int adc_get_def_ref_volt(void)
{
        return g_adc->pdata->ref_volt;
}
EXPORT_SYMBOL(adc_get_def_ref_volt);

int adc_get_curr_ref_volt(void)
{
        int v = 0, volt = 0;

        if(!g_adc)
                return -EINVAL;
        if(!g_adc->base_client)
                return g_adc->pdata->ref_volt;

        volt = g_adc->pdata->get_base_volt();
        if(volt < 0)
                return g_adc->pdata->ref_volt;
        
        v = adc_sync_read(g_adc->base_client);
        if(v < 0)
                return v;

        return volt * 1024 / v;
}
EXPORT_SYMBOL(adc_get_curr_ref_volt);

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


