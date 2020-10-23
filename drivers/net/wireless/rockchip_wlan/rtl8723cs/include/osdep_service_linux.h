/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __OSDEP_LINUX_SERVICE_H_
#define __OSDEP_LINUX_SERVICE_H_

#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/namei.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 5))
	#include <linux/kref.h>
#endif
/* #include <linux/smp_lock.h> */
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/skbuff.h>
#include <linux/circ_buf.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#include <asm/io.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 26))
	#include <asm/semaphore.h>
#else
	#include <linux/semaphore.h>
#endif
#include <linux/sem.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <net/addrconf.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/delay.h>
#include <linux/interrupt.h>	/* for struct tasklet_struct */
#include <linux/ip.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/vmalloc.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
	#include <uapi/linux/sched/types.h>
#endif

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 5, 41))
	#include <linux/tqueue.h>
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	#include <uapi/linux/limits.h>
#else
	#include <linux/limits.h>
#endif

#ifdef RTK_DMP_PLATFORM
	#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 12))
		#include <linux/pageremap.h>
	#endif
	#include <asm/io.h>
#endif

#ifdef CONFIG_NET_RADIO
	#define CONFIG_WIRELESS_EXT
#endif

/* Monitor mode */
#include <net/ieee80211_radiotap.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24))
	#include <linux/ieee80211.h>
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25) && \
	 LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29))
	#define CONFIG_IEEE80211_HT_ADDT_INFO
#endif

#ifdef CONFIG_IOCTL_CFG80211
	/*	#include <linux/ieee80211.h> */
	#include <net/cfg80211.h>
#else
	#ifdef CONFIG_REGD_SRC_FROM_OS
	#error "CONFIG_REGD_SRC_FROM_OS requires CONFIG_IOCTL_CFG80211"
	#endif
#endif /* CONFIG_IOCTL_CFG80211 */


#ifdef CONFIG_HAS_EARLYSUSPEND
	#include <linux/earlysuspend.h>
#endif /* CONFIG_HAS_EARLYSUSPEND */

#ifdef CONFIG_EFUSE_CONFIG_FILE
	#include <linux/fs.h>
#endif

#ifdef CONFIG_USB_HCI
	#include <linux/usb.h>
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 21))
		#include <linux/usb_ch9.h>
	#else
		#include <linux/usb/ch9.h>
	#endif
#endif

#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	#include <net/sock.h>
	#include <net/tcp.h>
	#include <linux/udp.h>
	#include <linux/in.h>
	#include <linux/netlink.h>
#endif /* CONFIG_BT_COEXIST_SOCKET_TRX */

#ifdef CONFIG_USB_HCI
	typedef struct urb   *PURB;
#endif

#if defined(CONFIG_RTW_GRO) && (!defined(CONFIG_RTW_NAPI))

	#error "Enable NAPI before enable GRO\n"

#endif


#if (KERNEL_VERSION(2, 6, 29) > LINUX_VERSION_CODE && defined(CONFIG_RTW_NAPI))

	#undef CONFIG_RTW_NAPI
	/*#warning "Linux Kernel version too old to support NAPI (should newer than 2.6.29)\n"*/

#endif

#if (KERNEL_VERSION(2, 6, 33) > LINUX_VERSION_CODE && defined(CONFIG_RTW_GRO))

	#undef CONFIG_RTW_GRO
	/*#warning "Linux Kernel version too old to support GRO(should newer than 2.6.33)\n"*/

#endif

typedef struct	semaphore _sema;
typedef	spinlock_t	_lock;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	typedef struct mutex		_mutex;
#else
	typedef struct semaphore	_mutex;
#endif
struct rtw_timer_list {
	struct timer_list timer;
	void (*function)(void *);
	void *arg;
};

typedef struct rtw_timer_list _timer;
typedef struct completion _completion;

struct	__queue	{
	struct	list_head	queue;
	_lock	lock;
};

typedef	struct sk_buff	_pkt;
typedef unsigned char	_buffer;

typedef struct	__queue	_queue;
typedef struct	list_head	_list;

/* hlist */
typedef struct	hlist_head	rtw_hlist_head;
typedef struct	hlist_node	rtw_hlist_node;

/* RCU */
typedef struct rcu_head rtw_rcu_head;
#define rtw_rcu_dereference(p) rcu_dereference((p))
#define rtw_rcu_dereference_protected(p, c) rcu_dereference_protected(p, c)
#define rtw_rcu_assign_pointer(p, v) rcu_assign_pointer((p), (v))
#define rtw_rcu_read_lock() rcu_read_lock()
#define rtw_rcu_read_unlock() rcu_read_unlock()
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34))
#define rtw_rcu_access_pointer(p) rcu_access_pointer(p)
#endif

/* rhashtable */
#include "../os_dep/linux/rtw_rhashtable.h"

typedef	int	_OS_STATUS;
/* typedef u32	_irqL; */
typedef unsigned long _irqL;
typedef	struct	net_device *_nic_hdl;

typedef void		*_thread_hdl_;
typedef int		thread_return;
typedef void	*thread_context;

typedef void timer_hdl_return;
typedef void *timer_hdl_context;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 41))
	typedef struct work_struct _workitem;
#else
	typedef struct tq_struct _workitem;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
	#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))
#endif

typedef unsigned long systime;
typedef ktime_t sysptime;
typedef struct tasklet_struct _tasklet;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 22))
/* Porting from linux kernel, for compatible with old kernel. */
static inline unsigned char *skb_tail_pointer(const struct sk_buff *skb)
{
	return skb->tail;
}

static inline void skb_reset_tail_pointer(struct sk_buff *skb)
{
	skb->tail = skb->data;
}

static inline void skb_set_tail_pointer(struct sk_buff *skb, const int offset)
{
	skb->tail = skb->data + offset;
}

static inline unsigned char *skb_end_pointer(const struct sk_buff *skb)
{
	return skb->end;
}
#endif

__inline static void rtw_list_delete(_list *plist)
{
	list_del_init(plist);
}

__inline static _list *get_next(_list	*list)
{
	return list->next;
}

#define LIST_CONTAINOR(ptr, type, member) \
	((type *)((char *)(ptr)-(SIZE_T)(&((type *)0)->member)))

#define rtw_list_first_entry(ptr, type, member) list_first_entry(ptr, type, member)

#define rtw_hlist_for_each_entry(pos, head, member) hlist_for_each_entry(pos, head, member)
#define rtw_hlist_for_each_safe(pos, n, head) hlist_for_each_safe(pos, n, head)
#define rtw_hlist_entry(ptr, type, member) hlist_entry(ptr, type, member)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
#define rtw_hlist_for_each_entry_safe(pos, np, n, head, member) hlist_for_each_entry_safe(pos, n, head, member)
#define rtw_hlist_for_each_entry_rcu(pos, node, head, member) hlist_for_each_entry_rcu(pos, head, member)
#else
#define rtw_hlist_for_each_entry_safe(pos, np, n, head, member) hlist_for_each_entry_safe(pos, np, n, head, member)
#define rtw_hlist_for_each_entry_rcu(pos, node, head, member) hlist_for_each_entry_rcu(pos, node, head, member)
#endif

__inline static void _enter_critical(_lock *plock, _irqL *pirqL)
{
	spin_lock_irqsave(plock, *pirqL);
}

__inline static void _exit_critical(_lock *plock, _irqL *pirqL)
{
	spin_unlock_irqrestore(plock, *pirqL);
}

__inline static void _enter_critical_ex(_lock *plock, _irqL *pirqL)
{
	spin_lock_irqsave(plock, *pirqL);
}

__inline static void _exit_critical_ex(_lock *plock, _irqL *pirqL)
{
	spin_unlock_irqrestore(plock, *pirqL);
}

__inline static void _enter_critical_bh(_lock *plock, _irqL *pirqL)
{
	spin_lock_bh(plock);
}

__inline static void _exit_critical_bh(_lock *plock, _irqL *pirqL)
{
	spin_unlock_bh(plock);
}

__inline static void enter_critical_bh(_lock *plock)
{
	spin_lock_bh(plock);
}

__inline static void exit_critical_bh(_lock *plock)
{
	spin_unlock_bh(plock);
}

__inline static int _enter_critical_mutex(_mutex *pmutex, _irqL *pirqL)
{
	int ret = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	/* mutex_lock(pmutex); */
	ret = mutex_lock_interruptible(pmutex);
#else
	ret = down_interruptible(pmutex);
#endif
	return ret;
}


__inline static int _enter_critical_mutex_lock(_mutex *pmutex, _irqL *pirqL)
{
	int ret = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	mutex_lock(pmutex);
#else
	down(pmutex);
#endif
	return ret;
}

__inline static void _exit_critical_mutex(_mutex *pmutex, _irqL *pirqL)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	mutex_unlock(pmutex);
#else
	up(pmutex);
#endif
}

__inline static _list	*get_list_head(_queue	*queue)
{
	return &(queue->queue);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
static inline void timer_hdl(struct timer_list *in_timer)
#else
static inline void timer_hdl(unsigned long cntx)
#endif
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
	_timer *ptimer = from_timer(ptimer, in_timer, timer);
#else
	_timer *ptimer = (_timer *)cntx;
#endif
	ptimer->function(ptimer->arg);
}

__inline static void _init_timer(_timer *ptimer, _nic_hdl nic_hdl, void *pfunc, void *cntx)
{
	ptimer->function = pfunc;
	ptimer->arg = cntx;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
	timer_setup(&ptimer->timer, timer_hdl, 0);
#else
	/* setup_timer(ptimer, pfunc,(u32)cntx);	 */
	ptimer->timer.function = timer_hdl;
	ptimer->timer.data = (unsigned long)ptimer;
	init_timer(&ptimer->timer);
#endif
}

__inline static void _set_timer(_timer *ptimer, u32 delay_time)
{
	mod_timer(&ptimer->timer , (jiffies + (delay_time * HZ / 1000)));
}

__inline static void _cancel_timer(_timer *ptimer, u8 *bcancelled)
{
	*bcancelled = del_timer_sync(&ptimer->timer) == 1 ? 1 : 0;
}

__inline static void _cancel_timer_async(_timer *ptimer)
{
	del_timer(&ptimer->timer);
}

static inline void _init_workitem(_workitem *pwork, void *pfunc, void *cntx)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 20))
	INIT_WORK(pwork, pfunc);
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 41))
	INIT_WORK(pwork, pfunc, pwork);
#else
	INIT_TQUEUE(pwork, pfunc, pwork);
#endif
}

__inline static void _set_workitem(_workitem *pwork)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 41))
	schedule_work(pwork);
#else
	schedule_task(pwork);
#endif
}

__inline static void _cancel_workitem_sync(_workitem *pwork)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22))
	cancel_work_sync(pwork);
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 41))
	flush_scheduled_work();
#else
	flush_scheduled_tasks();
#endif
}
/*
 * Global Mutex: can only be used at PASSIVE level.
 *   */

#define ACQUIRE_GLOBAL_MUTEX(_MutexCounter)                              \
	{                                                               \
		while (atomic_inc_return((atomic_t *)&(_MutexCounter)) != 1) { \
			atomic_dec((atomic_t *)&(_MutexCounter));        \
			msleep(10);                          \
		}                                                           \
	}

#define RELEASE_GLOBAL_MUTEX(_MutexCounter)                              \
	{                                                               \
		atomic_dec((atomic_t *)&(_MutexCounter));        \
	}

static inline int rtw_netif_queue_stopped(struct net_device *pnetdev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	return (netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 0)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 1)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 2)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 3)));
#else
	return netif_queue_stopped(pnetdev);
#endif
}

static inline void rtw_netif_wake_queue(struct net_device *pnetdev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	netif_tx_wake_all_queues(pnetdev);
#else
	netif_wake_queue(pnetdev);
#endif
}

static inline void rtw_netif_start_queue(struct net_device *pnetdev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	netif_tx_start_all_queues(pnetdev);
#else
	netif_start_queue(pnetdev);
#endif
}

static inline void rtw_netif_stop_queue(struct net_device *pnetdev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	netif_tx_stop_all_queues(pnetdev);
#else
	netif_stop_queue(pnetdev);
#endif
}
static inline void rtw_netif_device_attach(struct net_device *pnetdev)
{
	netif_device_attach(pnetdev);
}
static inline void rtw_netif_device_detach(struct net_device *pnetdev)
{
	netif_device_detach(pnetdev);
}
static inline void rtw_netif_carrier_on(struct net_device *pnetdev)
{
	netif_carrier_on(pnetdev);
}
static inline void rtw_netif_carrier_off(struct net_device *pnetdev)
{
	netif_carrier_off(pnetdev);
}

static inline int rtw_merge_string(char *dst, int dst_len, const char *src1, const char *src2)
{
	int	len = 0;
	len += snprintf(dst + len, dst_len - len, "%s", src1);
	len += snprintf(dst + len, dst_len - len, "%s", src2);

	return len;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	#define rtw_signal_process(pid, sig) kill_pid(find_vpid((pid)), (sig), 1)
#else /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) */
	#define rtw_signal_process(pid, sig) kill_proc((pid), (sig), 1)
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) */


/* Suspend lock prevent system from going suspend */
#ifdef CONFIG_WAKELOCK
	#include <linux/wakelock.h>
#elif defined(CONFIG_ANDROID_POWER)
	#include <linux/android_power.h>
#endif

/* limitation of path length */
#define PATH_LENGTH_MAX PATH_MAX

/* Atomic integer operations */
#define ATOMIC_T atomic_t

#define rtw_netdev_priv(netdev) (((struct rtw_netdev_priv_indicator *)netdev_priv(netdev))->priv)

#define NDEV_FMT "%s"
#define NDEV_ARG(ndev) ndev->name
#define ADPT_FMT "%s"
#define ADPT_ARG(adapter) (adapter->pnetdev ? adapter->pnetdev->name : NULL)
#define FUNC_NDEV_FMT "%s(%s)"
#define FUNC_NDEV_ARG(ndev) __func__, ndev->name
#define FUNC_ADPT_FMT "%s(%s)"
#define FUNC_ADPT_ARG(adapter) __func__, (adapter->pnetdev ? adapter->pnetdev->name : NULL)

struct rtw_netdev_priv_indicator {
	void *priv;
	u32 sizeof_priv;
};
struct net_device *rtw_alloc_etherdev_with_old_priv(int sizeof_priv, void *old_priv);
extern struct net_device *rtw_alloc_etherdev(int sizeof_priv);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
#define rtw_get_same_net_ndev_by_name(ndev, name) dev_get_by_name(name)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 26))
#define rtw_get_same_net_ndev_by_name(ndev, name) dev_get_by_name(ndev->nd_net, name)
#else
#define rtw_get_same_net_ndev_by_name(ndev, name) dev_get_by_name(dev_net(ndev), name)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
#define rtw_get_bridge_ndev_by_name(name) dev_get_by_name(name)
#else
#define rtw_get_bridge_ndev_by_name(name) dev_get_by_name(&init_net, name)
#endif

#define STRUCT_PACKED __attribute__ ((packed))


#endif /* __OSDEP_LINUX_SERVICE_H_ */
