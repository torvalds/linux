/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#ifndef __OSDEP_SERVICE_H_
#define __OSDEP_SERVICE_H_

#define _FAIL		0
#define _SUCCESS	1
#define RTW_RX_HANDLED 2

#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/circ_buf.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <linux/semaphore.h>
#include <linux/sem.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>	/*  Necessary because we use the proc fs */
#include <linux/interrupt.h>	/*  for struct tasklet_struct */
#include <linux/ip.h>
#include <linux/kthread.h>


/*	#include <linux/ieee80211.h> */
#include <net/ieee80211_radiotap.h>
#include <net/cfg80211.h>
#include <linux/usb.h>
#include <linux/usb/ch9.h>

struct rtw_adapter;
struct c2h_evt_hdr;

typedef s32 (*c2h_id_filter)(u8 id);

struct rtw_queue {
	struct	list_head	queue;
	spinlock_t		lock;
};

static inline struct list_head *get_list_head(struct rtw_queue *queue)
{
	return (&queue->queue);
}

static inline int rtw_netif_queue_stopped(struct net_device *pnetdev)
{
	return (netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 0)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 1)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 2)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 3)) );
}

#ifndef BIT
#define BIT(x)	( 1 << (x))
#endif
static inline u32 CHKBIT(u32 x)
{
	WARN_ON(x >= 32);
	if (x >= 32)
		return 0;
	return BIT(x);
}

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

int RTW_STATUS_CODE23a(int error_code);

u8*	_rtw_vmalloc(u32 sz);
u8*	_rtw_zvmalloc(u32 sz);
void	_rtw_vmfree(u8 *pbuf, u32 sz);
#define rtw_vmalloc(sz)			_rtw_vmalloc((sz))
#define rtw_zvmalloc(sz)			_rtw_zvmalloc((sz))
#define rtw_vmfree(pbuf, sz)		_rtw_vmfree((pbuf), (sz))

extern unsigned char REALTEK_96B_IE23A[];
extern unsigned char MCS_rate_2R23A[16];
extern unsigned char RTW_WPA_OUI23A[];
extern unsigned char WPA_TKIP_CIPHER23A[4];
extern unsigned char RSN_TKIP_CIPHER23A[4];

extern unsigned char	MCS_rate_2R23A[16];
extern unsigned char	MCS_rate_1R23A[16];

void	_rtw_init_queue23a(struct rtw_queue *pqueue);
u32	_rtw_queue_empty23a(struct rtw_queue *pqueue);

static inline u32 bitshift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++)
		if (((bitmask>>i) &  0x1) == 1) break;

	return i;
}

void rtw_suspend_lock_init(void);
void rtw_suspend_lock_uninit(void);
void rtw_lock_suspend(void);
void rtw_unlock_suspend(void);


#define NDEV_FMT "%s"
#define NDEV_ARG(ndev) ndev->name
#define ADPT_FMT "%s"
#define ADPT_ARG(adapter) adapter->pnetdev->name
#define FUNC_NDEV_FMT "%s(%s)"
#define FUNC_NDEV_ARG(ndev) __func__, ndev->name
#define FUNC_ADPT_FMT "%s(%s)"
#define FUNC_ADPT_ARG(adapter) __func__, adapter->pnetdev->name

#define rtw_signal_process(pid, sig) kill_pid(find_vpid((pid)),(sig), 1)

u64 rtw_modular6423a(u64 x, u64 y);
u64 rtw_division6423a(u64 x, u64 y);


/* Macros for handling unaligned memory accesses */

#define RTW_GET_BE24(a) ((((u32) (a)[0]) << 16) | (((u32) (a)[1]) << 8) | \
			 ((u32) (a)[2]))


struct rtw_cbuf {
	u32 write;
	u32 read;
	u32 size;
	void *bufs[0];
};

bool rtw_cbuf_full23a(struct rtw_cbuf *cbuf);
bool rtw_cbuf_empty23a(struct rtw_cbuf *cbuf);
bool rtw_cbuf_push23a(struct rtw_cbuf *cbuf, void *buf);
void *rtw_cbuf_pop23a(struct rtw_cbuf *cbuf);
struct rtw_cbuf *rtw_cbuf_alloc23a(u32 size);
void rtw_cbuf_free(struct rtw_cbuf *cbuf);
int rtw_change_ifname(struct rtw_adapter *padapter, const char *ifname);
s32 c2h_evt_hdl(struct rtw_adapter *adapter, struct c2h_evt_hdr *c2h_evt, c2h_id_filter filter);
void indicate_wx_scan_complete_event(struct rtw_adapter *padapter);
u8 rtw_do_join23a(struct rtw_adapter *padapter);

#endif
