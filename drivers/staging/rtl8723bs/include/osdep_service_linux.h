/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
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
#ifndef __OSDEP_LINUX_SERVICE_H_
#define __OSDEP_LINUX_SERVICE_H_

	#include <linux/spinlock.h>
	#include <linux/compiler.h>
	#include <linux/kernel.h>
	#include <linux/errno.h>
	#include <linux/init.h>
	#include <linux/slab.h>
	#include <linux/module.h>
	#include <linux/kref.h>
	/* include <linux/smp_lock.h> */
	#include <linux/netdevice.h>
	#include <linux/skbuff.h>
	#include <linux/uaccess.h>
	#include <asm/byteorder.h>
	#include <linux/atomic.h>
	#include <linux/io.h>
	#include <linux/semaphore.h>
	#include <linux/sem.h>
	#include <linux/sched.h>
	#include <linux/etherdevice.h>
	#include <linux/wireless.h>
	#include <net/iw_handler.h>
	#include <linux/if_arp.h>
	#include <linux/rtnetlink.h>
	#include <linux/delay.h>
	#include <linux/interrupt.h>	/*  for struct tasklet_struct */
	#include <linux/ip.h>
	#include <linux/kthread.h>
	#include <linux/list.h>
	#include <linux/vmalloc.h>

/* 	#include <linux/ieee80211.h> */
        #include <net/ieee80211_radiotap.h>
	#include <net/cfg80211.h>

	typedef struct	semaphore _sema;
	typedef	spinlock_t	_lock;
	typedef struct mutex		_mutex;
	typedef struct timer_list _timer;

	struct	__queue	{
		struct	list_head	queue;
		_lock	lock;
	};

	typedef	struct sk_buff	_pkt;
	typedef unsigned char _buffer;

	typedef	int	_OS_STATUS;
	/* typedef u32 _irqL; */
	typedef unsigned long _irqL;
	typedef	struct	net_device * _nic_hdl;

	#define thread_exit() complete_and_exit(NULL, 0)

	typedef void timer_hdl_return;
	typedef void* timer_hdl_context;

	typedef struct work_struct _workitem;

__inline static struct list_head *get_next(struct list_head	*list)
{
	return list->next;
}

__inline static struct list_head	*get_list_head(struct __queue	*queue)
{
	return (&(queue->queue));
}


#define LIST_CONTAINOR(ptr, type, member) \
        ((type *)((char *)(ptr)-(__kernel_size_t)(&((type *)0)->member)))

#define RTW_TIMER_HDL_ARGS void *FunctionContext

__inline static void _init_timer(_timer *ptimer, _nic_hdl nic_hdl, void *pfunc, void* cntx)
{
	/* setup_timer(ptimer, pfunc, (u32)cntx); */
	ptimer->function = pfunc;
	ptimer->data = (unsigned long)cntx;
	init_timer(ptimer);
}

__inline static void _set_timer(_timer *ptimer, u32 delay_time)
{
	mod_timer(ptimer , (jiffies+(delay_time*HZ/1000)));
}

__inline static void _cancel_timer(_timer *ptimer, u8 *bcancelled)
{
	del_timer_sync(ptimer);
	*bcancelled =  true;/* true == 1; false == 0 */
}


__inline static void _init_workitem(_workitem *pwork, void *pfunc, void *cntx)
{
	INIT_WORK(pwork, pfunc);
}

__inline static void _set_workitem(_workitem *pwork)
{
	schedule_work(pwork);
}

__inline static void _cancel_workitem_sync(_workitem *pwork)
{
	cancel_work_sync(pwork);
}

static inline int rtw_netif_queue_stopped(struct net_device *pnetdev)
{
	return (netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 0)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 1)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 2)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 3)));
}

static inline void rtw_netif_wake_queue(struct net_device *pnetdev)
{
	netif_tx_wake_all_queues(pnetdev);
}

static inline void rtw_netif_start_queue(struct net_device *pnetdev)
{
	netif_tx_start_all_queues(pnetdev);
}

static inline void rtw_netif_stop_queue(struct net_device *pnetdev)
{
	netif_tx_stop_all_queues(pnetdev);
}

static inline void rtw_merge_string(char *dst, int dst_len, char *src1, char *src2)
{
	int	len = 0;
	len += snprintf(dst+len, dst_len - len, "%s", src1);
	len += snprintf(dst+len, dst_len - len, "%s", src2);
}

#define rtw_signal_process(pid, sig) kill_pid(find_vpid((pid)), (sig), 1)

#define rtw_netdev_priv(netdev) (((struct rtw_netdev_priv_indicator *)netdev_priv(netdev))->priv)

#define NDEV_FMT "%s"
#define NDEV_ARG(ndev) ndev->name
#define ADPT_FMT "%s"
#define ADPT_ARG(adapter) adapter->pnetdev->name
#define FUNC_NDEV_FMT "%s(%s)"
#define FUNC_NDEV_ARG(ndev) __func__, ndev->name
#define FUNC_ADPT_FMT "%s(%s)"
#define FUNC_ADPT_ARG(adapter) __func__, adapter->pnetdev->name

struct rtw_netdev_priv_indicator {
	void *priv;
	u32 sizeof_priv;
};
struct net_device *rtw_alloc_etherdev_with_old_priv(int sizeof_priv, void *old_priv);
extern struct net_device * rtw_alloc_etherdev(int sizeof_priv);

#endif
