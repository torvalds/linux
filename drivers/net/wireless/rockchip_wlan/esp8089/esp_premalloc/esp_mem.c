/* Copyright (c) 2014 - 2015 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 */
#ifdef ESP_PRE_MEM

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/delay.h>

#include "esp_mem.h"
#include "esp_log.h"

#ifdef ESP_SPI
static u8 *gl_lspi_buf;
#endif

static u8 *gl_tx_aggr_buf;

static struct esp_skb_elem gl_sip_skb_arr[SIP_SKB_ARR_NUM];

int esp_pre_alloc_sip_skb_arr(void)
{
	int i;

	for (i = 0; i < SIP_SKB_ARR_NUM; i++) {
		gl_sip_skb_arr[i].skb_p = NULL;
		gl_sip_skb_arr[i].skb_size = 0;
		atomic_set(&gl_sip_skb_arr[i].inuse, 0);
	}
		
	/* 8K */
	for (i = SIP_SKB_SPOS_8K; i < (SIP_SKB_SPOS_8K + SIP_SKB_NUM_8K); i++) {
		gl_sip_skb_arr[i].skb_p = __dev_alloc_skb(SIP_SKB_SIZE_8K, GFP_KERNEL);
		if (gl_sip_skb_arr[i].skb_p == NULL)
			goto _mem_err;
		gl_sip_skb_arr[i].skb_size = SIP_SKB_SIZE_8K;
	}

	/* 16K */
	for (i = SIP_SKB_SPOS_16K; i < (SIP_SKB_SPOS_16K + SIP_SKB_NUM_16K); i++) {
		gl_sip_skb_arr[i].skb_p = __dev_alloc_skb(SIP_SKB_SIZE_16K, GFP_KERNEL);
		if (gl_sip_skb_arr[i].skb_p == NULL)
			goto _mem_err;
		gl_sip_skb_arr[i].skb_size = SIP_SKB_SIZE_16K;
	}

	/* 32K */
	for (i = SIP_SKB_SPOS_32K; i < (SIP_SKB_SPOS_32K + SIP_SKB_NUM_32K); i++) {
		gl_sip_skb_arr[i].skb_p = __dev_alloc_skb(SIP_SKB_SIZE_32K, GFP_KERNEL);
		if (gl_sip_skb_arr[i].skb_p == NULL)
			goto _mem_err;
		gl_sip_skb_arr[i].skb_size = SIP_SKB_SIZE_32K;
	}

	return 0;

_mem_err:
	loge("alloc sip skb arr [%d] failed\n", i);
	esp_pre_free_sip_skb_arr();
	return -ENOMEM;
}

void esp_pre_free_sip_skb_arr(void)
{
	int i;

	for (i = 0; i < SIP_SKB_ARR_NUM; i++) {
		if (gl_sip_skb_arr[i].skb_p)
			kfree_skb(gl_sip_skb_arr[i].skb_p);

		gl_sip_skb_arr[i].skb_p = NULL;
		gl_sip_skb_arr[i].skb_size = 0;
		atomic_set(&gl_sip_skb_arr[i].inuse, 0);
	}
}

#ifdef ESP_EXT_SHOW_SKB
void esp_pre_sip_skb_show(void)
{
	int i;

	for (i = 0; i < SIP_SKB_ARR_NUM; i++)
		logi("skb arr[%d]: p[%p] size[%d]  inuse [%d]\n", i, gl_sip_skb_arr[i].skb_p, gl_sip_skb_arr[i].skb_size, atomic_read(&gl_sip_skb_arr[i].inuse));
	
}
EXPORT_SYMBOL(esp_pre_sip_skb_show);
#endif

struct sk_buff *esp_get_sip_skb(int size, gfp_t type)
{
	int i;
	int retry = 100;

	do {
		if (size <= SIP_SKB_SIZE_8K) {
			for (i = SIP_SKB_SPOS_8K; i < (SIP_SKB_SPOS_8K + SIP_SKB_NUM_8K); i++) {
				if (atomic_read(&gl_sip_skb_arr[i].inuse) == 0) {
					atomic_set(&gl_sip_skb_arr[i].inuse, 1);
					logd("%s skb_alloc [%d]\n", __func__, i);
					return gl_sip_skb_arr[i].skb_p;
				} 
			}
		} else if (size <= SIP_SKB_SIZE_16K) {
			for (i = SIP_SKB_SPOS_16K; i < (SIP_SKB_SPOS_16K + SIP_SKB_NUM_16K); i++) {
				if (atomic_read(&gl_sip_skb_arr[i].inuse) == 0) {
					atomic_set(&gl_sip_skb_arr[i].inuse, 1);
					logd("%s skb_alloc [%d]\n", __func__, i);
					return gl_sip_skb_arr[i].skb_p;
				} 
			}
		} else if (size <= SIP_SKB_SIZE_32K) {
			for (i = SIP_SKB_SPOS_32K; i < (SIP_SKB_SPOS_32K + SIP_SKB_NUM_32K); i++) {
				if (atomic_read(&gl_sip_skb_arr[i].inuse) == 0) {
					atomic_set(&gl_sip_skb_arr[i].inuse, 1);
					logd("%s skb_alloc [%d]\n", __func__, i);
					return gl_sip_skb_arr[i].skb_p;
				} 
			}
		} else {
			loge("size[%d] is too large, get skb failed\n", size);
			break;
		}

		if (type == GFP_KERNEL)
			msleep(1);
		else
			mdelay(1);

	} while (--retry > 0);

	if (retry <= 0)
		loge("skb is all in use, size[%d]  get skb failed\n", size);

	return NULL;
}
EXPORT_SYMBOL(esp_get_sip_skb);

void esp_put_sip_skb(struct sk_buff **skb)
{
	int i;

	for (i = 0; i < SIP_SKB_ARR_NUM; i++) {
		if (gl_sip_skb_arr[i].skb_p == *skb) {
			gl_sip_skb_arr[i].skb_p->data = gl_sip_skb_arr[i].skb_p->head;
			#if BITS_PER_LONG > 32
                        gl_sip_skb_arr[i].skb_p->tail = 0;
                        #else	
			gl_sip_skb_arr[i].skb_p->tail = gl_sip_skb_arr[i].skb_p->head;
                        #endif
			gl_sip_skb_arr[i].skb_p->data_len = 0;
			gl_sip_skb_arr[i].skb_p->len = 0;
			skb_trim(gl_sip_skb_arr[i].skb_p, 0);
			atomic_set(&gl_sip_skb_arr[i].inuse, 0);
			break;
		}
	}

	if (i == SIP_SKB_ARR_NUM)
		loge("warnning : no skb to put\n");

	*skb = NULL; /* set input skb NULL */

	logd("%s skb_free [%d]\n", __func__, i);
}
EXPORT_SYMBOL(esp_put_sip_skb);

u8 *esp_pre_alloc_tx_aggr_buf(void)
{
	int po;

	po = get_order(TX_AGGR_BUF_SIZE);
        gl_tx_aggr_buf = (u8 *)__get_free_pages(GFP_ATOMIC, po);

        if (gl_tx_aggr_buf == NULL) {
                loge("%s no mem for gl_tx_aggr_buf! \n", __func__);
                return NULL;
	}

	return gl_tx_aggr_buf;
}

void esp_pre_free_tx_aggr_buf(void)
{
	int po;

	if (!gl_tx_aggr_buf) {
                loge("%s need not free gl_tx_aggr_buf! \n", __func__);
		return;
	}

	po = get_order(TX_AGGR_BUF_SIZE);
       	free_pages((unsigned long)gl_tx_aggr_buf, po);
	gl_tx_aggr_buf =  NULL;
}

u8* esp_get_tx_aggr_buf(void)
{
	if (!gl_tx_aggr_buf) {
                loge(KERN_ERR "%s gl_tx_aggr_buf is NULL failed! \n", __func__);
		return NULL;
	}

	return gl_tx_aggr_buf;
}
EXPORT_SYMBOL(esp_get_tx_aggr_buf);

void esp_put_tx_aggr_buf(u8 **p)
{
	*p = NULL; /*input point which return by get~() , then set it null */
}
EXPORT_SYMBOL(esp_put_tx_aggr_buf);

#ifdef ESP_SPI
u8 *esp_pre_alloc_lspi_buf(void)
{
	gl_lspi_buf = (u8 *)kmalloc(LSPI_BUF_SIZE, GFP_KERNEL);

	if (gl_lspi_buf == NULL) {
                loge("%s no mem for gl_rlspi_buf! \n", __func__);
		return NULL;
	}

	return gl_lspi_buf;
}

void esp_pre_free_lspi_buf(void)
{
	if (!gl_lspi_buf) {
                loge("%s need not free gl_lspi_buf! \n", __func__);
		return;
	}

	kfree(gl_lspi_buf);
	gl_lspi_buf = NULL;
}

u8 *esp_get_lspi_buf(void)
{
	if (!gl_lspi_buf) {
                loge(KERN_ERR "%s gl_lspi_buf is NULL failed! \n", __func__);
		return NULL;
	}

	return gl_lspi_buf;
}
EXPORT_SYMBOL(esp_get_lspi_buf);

void esp_put_lspi_buf(u8 **p)
{
	*p = NULL; /*input point which return by get~() , then set it null */
}
EXPORT_SYMBOL(esp_put_lspi_buf);

#endif /* ESP_SPI */

int esp_indi_pre_mem_init()
{
	int err = 0;

#ifdef ESP_SPI
	if (esp_pre_alloc_lspi_buf() == NULL)
		err = -ENOMEM;
#endif	
	if (esp_pre_alloc_tx_aggr_buf() == NULL)
		err = -ENOMEM;

	if (esp_pre_alloc_sip_skb_arr() != 0)
		err = -ENOMEM;

	if (err)
		esp_indi_pre_mem_deinit();    /* release the mem , as protect assigned atomic */

	return err;
}

void esp_indi_pre_mem_deinit()
{
#ifdef ESP_SPI
	esp_pre_free_lspi_buf();
#endif
	esp_pre_free_tx_aggr_buf();
	esp_pre_free_sip_skb_arr();
}

#endif /* ESP_PRE_MEM */
