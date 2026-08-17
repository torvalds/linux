/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __OSDEP_SERVICE_H_
#define __OSDEP_SERVICE_H_


#define _FAIL		0
#define _SUCCESS	1
#define RTW_RX_HANDLED 2

#include <osdep_service_linux.h>


extern int RTW_STATUS_CODE(int error_code);

int _rtw_netif_rx(struct net_device *ndev, struct sk_buff *skb);

#define rtw_netif_rx(ndev, skb) _rtw_netif_rx(ndev, skb)

static inline void flush_signals_thread(void)
{
	if (signal_pending(current))
		flush_signals(current);
}

#define _RND(sz, r) ((((sz)+((r)-1))/(r))*(r))

extern void rtw_free_netdev(struct net_device *netdev);

/* Macros for handling unaligned memory accesses */

void rtw_buf_free(u8 **buf, u32 *buf_len);
void rtw_buf_update(u8 **buf, u32 *buf_len, u8 *src, u32 src_len);

struct rtw_cbuf {
	u32 write;
	u32 read;
	u32 size;
	void *bufs[];
};

bool rtw_cbuf_full(struct rtw_cbuf *cbuf);
bool rtw_cbuf_empty(struct rtw_cbuf *cbuf);
bool rtw_cbuf_push(struct rtw_cbuf *cbuf, void *buf);
void *rtw_cbuf_pop(struct rtw_cbuf *cbuf);
struct rtw_cbuf *rtw_cbuf_alloc(u32 size);


#endif
