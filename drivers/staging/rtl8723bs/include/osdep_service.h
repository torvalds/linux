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

#ifndef BIT
	#define BIT(x)	(1 << (x))
#endif

#define BIT0	0x00000001
#define BIT1	0x00000002
#define BIT2	0x00000004
#define BIT3	0x00000008
#define BIT4	0x00000010
#define BIT5	0x00000020
#define BIT6	0x00000040
#define BIT7	0x00000080
#define BIT8	0x00000100
#define BIT9	0x00000200
#define BIT10	0x00000400
#define BIT11	0x00000800
#define BIT12	0x00001000
#define BIT13	0x00002000
#define BIT14	0x00004000
#define BIT15	0x00008000
#define BIT16	0x00010000
#define BIT17	0x00020000
#define BIT18	0x00040000
#define BIT19	0x00080000
#define BIT20	0x00100000
#define BIT21	0x00200000
#define BIT22	0x00400000
#define BIT23	0x00800000
#define BIT24	0x01000000
#define BIT25	0x02000000
#define BIT26	0x04000000
#define BIT27	0x08000000
#define BIT28	0x10000000
#define BIT29	0x20000000
#define BIT30	0x40000000
#define BIT31	0x80000000
#define BIT32	0x0100000000
#define BIT33	0x0200000000
#define BIT34	0x0400000000
#define BIT35	0x0800000000
#define BIT36	0x1000000000

extern int RTW_STATUS_CODE(int error_code);

void *_rtw_zmalloc(u32 sz);
void *_rtw_malloc(u32 sz);
void _kfree(u8 *pbuf, u32 sz);

struct sk_buff *_rtw_skb_alloc(u32 sz);
struct sk_buff *_rtw_skb_copy(const struct sk_buff *skb);
int _rtw_netif_rx(struct net_device *ndev, struct sk_buff *skb);

#define rtw_malloc(sz)			_rtw_malloc((sz))
#define rtw_zmalloc(sz)			_rtw_zmalloc((sz))

#define rtw_skb_alloc(size) _rtw_skb_alloc((size))
#define rtw_skb_alloc_f(size, mstat_f)	_rtw_skb_alloc((size))
#define rtw_skb_copy(skb)	_rtw_skb_copy((skb))
#define rtw_skb_copy_f(skb, mstat_f)	_rtw_skb_copy((skb))
#define rtw_netif_rx(ndev, skb) _rtw_netif_rx(ndev, skb)

extern void _rtw_init_queue(struct __queue	*pqueue);

static inline void thread_enter(char *name)
{
	allow_signal(SIGTERM);
}

static inline void flush_signals_thread(void)
{
	if (signal_pending(current))
	{
		flush_signals(current);
	}
}

#define rtw_warn_on(condition) WARN_ON(condition)

static inline int rtw_bug_check(void *parg1, void *parg2, void *parg3, void *parg4)
{
	int ret = true;

	return ret;

}

#define _RND(sz, r) ((((sz)+((r)-1))/(r))*(r))

#ifndef MAC_ARG
#define MAC_ARG(x) (x)
#endif

extern void rtw_free_netdev(struct net_device * netdev);

/* Macros for handling unaligned memory accesses */

void rtw_buf_free(u8 **buf, u32 *buf_len);
void rtw_buf_update(u8 **buf, u32 *buf_len, u8 *src, u32 src_len);

struct rtw_cbuf {
	u32 write;
	u32 read;
	u32 size;
	void *bufs[0];
};

bool rtw_cbuf_full(struct rtw_cbuf *cbuf);
bool rtw_cbuf_empty(struct rtw_cbuf *cbuf);
bool rtw_cbuf_push(struct rtw_cbuf *cbuf, void *buf);
void *rtw_cbuf_pop(struct rtw_cbuf *cbuf);
struct rtw_cbuf *rtw_cbuf_alloc(u32 size);

/*  String handler */
/*
 * Write formatted output to sized buffer
 */
#define rtw_sprintf(buf, size, format, arg...)	snprintf(buf, size, format, ##arg)

#endif
