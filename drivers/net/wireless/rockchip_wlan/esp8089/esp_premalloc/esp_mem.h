/* Copyright (c) 2014 - 2015 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 */
#ifndef _ESP_MEM_H_
#define _ESP_MEM_H_

#include <linux/skbuff.h>

#ifdef ESP_SPI
#define LSPI_BUF_SIZE    (48*1024)
#endif

#define TX_AGGR_BUF_SIZE (4 * PAGE_SIZE)
#define RX_AGGR_BUF_SIZE (4 * PAGE_SIZE)


#define SIP_SKB_SIZE_8K (8<<10)           /* 8K * 8 */
#define SIP_SKB_SIZE_16K (16<<10)           /* 16K * 4 */
#define SIP_SKB_SIZE_32K (32<<10)           /* 32K * 2 */
#define SIP_SKB_NUM_8K  8
#define SIP_SKB_NUM_16K  4
#define SIP_SKB_NUM_32K  2
#define SIP_SKB_SPOS_8K  0
#define SIP_SKB_SPOS_16K  8
#define SIP_SKB_SPOS_32K  12

#define SIP_SKB_ARR_NUM 14        /* (SIP_SKB_NUM_8k + SIP_SKB_NUM_16k + SIP_SKB_NUM_32k) */

struct esp_skb_elem {
	struct sk_buff *skb_p;
	int skb_size;
	atomic_t inuse;
};

#ifdef ESP_SPI
u8 *esp_pre_alloc_lspi_buf(void);
void esp_pre_free_lspi_buf(void);
#endif
int esp_pre_alloc_sip_skb_arr(void);
void esp_pre_free_sip_skb_arr(void);
u8 *esp_pre_alloc_tx_aggr_buf(void);
void esp_pre_free_tx_aggr_buf(void);

struct sk_buff *esp_get_sip_skb(int size, gfp_t type);
void esp_put_sip_skb(struct sk_buff **skb);
u8* esp_get_tx_aggr_buf(void);
void esp_put_tx_aggr_buf(u8 **p);
#ifdef ESP_SPI
u8 *esp_get_lspi_buf(void);
void esp_put_lspi_buf(u8 **p);
#endif

int esp_indi_pre_mem_init(void);
void esp_indi_pre_mem_deinit(void);

#endif /* _ESP_MEM_H_ */
