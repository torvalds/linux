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
 *
 ******************************************************************************/
#ifndef __OSDEP_SERVICE_H_
#define __OSDEP_SERVICE_H_

#include <drv_conf.h>
#include <basic_types.h>

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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,5))
	#include <linux/kref.h>
#endif
	#include <linux/netdevice.h>
	#include <linux/skbuff.h>
	#include <asm/uaccess.h>
	#include <asm/byteorder.h>
	#include <asm/atomic.h>
	#include <asm/io.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
	#include <asm/semaphore.h>
#else
	#include <linux/semaphore.h>
#endif
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

        #include <net/ieee80211_radiotap.h>
	#include <net/cfg80211.h>

#ifdef CONFIG_TCP_CSUM_OFFLOAD_TX
	#include <linux/in.h>
	#include <linux/udp.h>
#endif

	#include <linux/usb.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21))
	#include <linux/usb_ch9.h>
#else
	#include <linux/usb/ch9.h>
#endif

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,22))
#ifdef CONFIG_USB_SUSPEND
#define CONFIG_AUTOSUSPEND	1
#endif
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	#define _mutex	struct mutex
#else
	#define _mutex	struct semaphore
#endif
	struct	__queue	{
		struct	list_head	queue;
		spinlock_t	lock;
	};

	#define thread_exit() complete_and_exit(NULL, 0)

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22))
/*  Porting from linux kernel, for compatible with old kernel. */
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

static inline struct list_head *get_list_head(struct __queue *queue)
{
	return (&(queue->queue));
}

static inline void _enter_critical_mutex(_mutex *pmutex)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
		mutex_lock(pmutex);
#else
		down(pmutex);
#endif
}


static inline void _exit_critical_mutex(_mutex *pmutex)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
		mutex_unlock(pmutex);
#else
		up(pmutex);
#endif
}

static inline void _init_timer(struct timer_list *timer, struct net_device * nic_hdl, void *pfunc, void *cntx)
{
	timer->function = pfunc;
	timer->data = (unsigned long)cntx;
	init_timer(timer);
}

static inline void _set_timer(struct timer_list *timer,u32 delay_time)
{
	mod_timer(timer , (jiffies+(delay_time*HZ/1000)));
}

static inline void _cancel_timer(struct timer_list *timer,u8 *bcancelled)
{
	del_timer_sync(timer);
	*bcancelled=  true;
}

#define RTW_TIMER_HDL_ARGS void *FunctionContext

#define RTW_TIMER_HDL_NAME(name) rtw_##name##_timer_hdl
#define RTW_DECLARE_TIMER_HDL(name) void RTW_TIMER_HDL_NAME(name)(RTW_TIMER_HDL_ARGS)


static inline void _init_workitem(struct work_struct *pwork, void *pfunc, void * cntx)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
	INIT_WORK(pwork, pfunc);
#else
	INIT_WORK(pwork, pfunc,pwork);
#endif
}

static inline void _set_workitem(struct work_struct *pwork)
{
	schedule_work(pwork);
}

static inline void _cancel_workitem_sync(struct work_struct *pwork)
{
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,22))
	cancel_work_sync(pwork);
#else
	flush_scheduled_work();
#endif
}

/*  */
/*  Global Mutex: can only be used at PASSIVE level. */
/*  */

#define ACQUIRE_GLOBAL_MUTEX(_MutexCounter)                              \
{                                                               \
	while (atomic_inc_return((atomic_t *)&(_MutexCounter)) != 1)\
	{                                                           \
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
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
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
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
	netif_tx_wake_all_queues(pnetdev);
#else
	netif_wake_queue(pnetdev);
#endif
}

static inline void rtw_netif_start_queue(struct net_device *pnetdev)
{
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
	netif_tx_start_all_queues(pnetdev);
#else
	netif_start_queue(pnetdev);
#endif
}

static inline void rtw_netif_stop_queue(struct net_device *pnetdev)
{
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
	netif_tx_stop_all_queues(pnetdev);
#else
	netif_stop_queue(pnetdev);
#endif
}


#ifndef BIT
	#define BIT(x)	(1 << (x))
#endif

extern int RTW_STATUS_CODE(int error_code);

/* flags used for rtw_update_mem_stat() */
enum {
	MEM_STAT_VIR_ALLOC_SUCCESS,
	MEM_STAT_VIR_ALLOC_FAIL,
	MEM_STAT_VIR_FREE,
	MEM_STAT_PHY_ALLOC_SUCCESS,
	MEM_STAT_PHY_ALLOC_FAIL,
	MEM_STAT_PHY_FREE,
	MEM_STAT_TX, /* used to distinguish TX/RX, asigned from caller */
	MEM_STAT_TX_ALLOC_SUCCESS,
	MEM_STAT_TX_ALLOC_FAIL,
	MEM_STAT_TX_FREE,
	MEM_STAT_RX, /* used to distinguish TX/RX, asigned from caller */
	MEM_STAT_RX_ALLOC_SUCCESS,
	MEM_STAT_RX_ALLOC_FAIL,
	MEM_STAT_RX_FREE
};

#define rtw_update_mem_stat(flag, sz) do {} while (0)

extern int	_rtw_memcmp(void *dst, void *src, u32 sz);

extern u32	rtw_is_list_empty(struct list_head *phead);
extern void	rtw_list_insert_head(struct list_head *plist, struct list_head *phead);
extern void	rtw_list_insert_tail(struct list_head *plist, struct list_head *phead);

extern void	_rtw_init_sema(struct  semaphore *sema, int init_val);
extern void	_rtw_free_sema(struct  semaphore *sema);
extern void	_rtw_up_sema(struct  semaphore *sema);
extern u32	_rtw_down_sema(struct  semaphore *sema);
extern void	_rtw_mutex_init(_mutex *pmutex);
extern void	_rtw_mutex_free(_mutex *pmutex);
extern void	_rtw_spinlock_init(spinlock_t *plock);
extern void	_rtw_spinlock_free(spinlock_t *plock);
extern void	_rtw_spinlock(spinlock_t	*plock);
extern void	_rtw_spinunlock(spinlock_t	*plock);
extern void	_rtw_spinlock_ex(spinlock_t	*plock);
extern void	_rtw_spinunlock_ex(spinlock_t	*plock);

extern void	_rtw_init_queue(struct __queue *pqueue);
extern u32	_rtw_queue_empty(struct __queue *pqueue);
extern u32	rtw_end_of_queue_search(struct list_head *queue, struct list_head *pelement);

extern u32	rtw_get_current_time(void);
extern u32	rtw_systime_to_ms(u32 systime);
extern u32	rtw_ms_to_systime(u32 ms);
extern s32	rtw_get_passing_time_ms(u32 start);
extern s32	rtw_get_time_interval_ms(u32 start, u32 end);

extern void	rtw_sleep_schedulable(int ms);

extern void	rtw_msleep_os(int ms);
extern void	rtw_usleep_os(int us);

extern u32	rtw_atoi(u8* s);

#ifdef DBG_DELAY_OS
#define rtw_mdelay_os(ms) _rtw_mdelay_os((ms), __func__, __LINE__)
#define rtw_udelay_os(ms) _rtw_udelay_os((ms), __func__, __LINE__)
extern void _rtw_mdelay_os(int ms, const char *func, const int line);
extern void _rtw_udelay_os(int us, const char *func, const int line);
#else
extern void	rtw_mdelay_os(int ms);
extern void	rtw_udelay_os(int us);
#endif

extern void rtw_yield_os(void);


static inline unsigned char _cancel_timer_ex(struct timer_list *timer)
{
	return del_timer_sync(timer);
}

static __inline void thread_enter(char *name)
{
	#ifdef daemonize
	daemonize("%s", name);
	#endif
	allow_signal(SIGTERM);
}

static inline void flush_signals_thread(void)
{
	if (signal_pending (current))
		flush_signals(current);
}

static inline int res_to_status(int res)
{
	return res;
}

#define _RND(sz, r) ((((sz)+((r)-1))/(r))*(r))
#define RND4(x)	(((x >> 2) + (((x & 3) == 0) ?  0: 1)) << 2)

static inline u32 _RND4(u32 sz)
{

	u32	val;

	val = ((sz >> 2) + ((sz & 3) ? 1: 0)) << 2;

	return val;

}

static inline u32 _RND8(u32 sz)
{

	u32	val;

	val = ((sz >> 3) + ((sz & 7) ? 1: 0)) << 3;

	return val;

}

static inline u32 _RND128(u32 sz)
{

	u32	val;

	val = ((sz >> 7) + ((sz & 127) ? 1: 0)) << 7;

	return val;

}

static inline u32 _RND256(u32 sz)
{

	u32	val;

	val = ((sz >> 8) + ((sz & 255) ? 1: 0)) << 8;

	return val;

}

static inline u32 _RND512(u32 sz)
{

	u32	val;

	val = ((sz >> 9) + ((sz & 511) ? 1: 0)) << 9;

	return val;

}

static inline u32 bitshift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++)
		if (((bitmask>>i) &  0x1) == 1) break;

	return i;
}

/* ifdef __GNUC__ */
#define STRUCT_PACKED __attribute__ ((packed))

/*  limitation of path length */
	#define PATH_LENGTH_MAX PATH_MAX

/*  Suspend lock prevent system from going suspend */
#ifdef CONFIG_WAKELOCK
#include <linux/wakelock.h>
#elif defined(CONFIG_ANDROID_POWER)
#include <linux/android_power.h>
#endif

extern void rtw_suspend_lock_init(void);
extern void rtw_suspend_lock_uninit(void);
extern void rtw_lock_suspend(void);
extern void rtw_unlock_suspend(void);


/* Atomic integer operations */
	#define ATOMIC_T atomic_t

extern void ATOMIC_SET(ATOMIC_T *v, int i);
extern int ATOMIC_READ(ATOMIC_T *v);
extern void ATOMIC_ADD(ATOMIC_T *v, int i);
extern void ATOMIC_SUB(ATOMIC_T *v, int i);
extern void ATOMIC_INC(ATOMIC_T *v);
extern void ATOMIC_DEC(ATOMIC_T *v);
extern int ATOMIC_ADD_RETURN(ATOMIC_T *v, int i);
extern int ATOMIC_SUB_RETURN(ATOMIC_T *v, int i);
extern int ATOMIC_INC_RETURN(ATOMIC_T *v);
extern int ATOMIC_DEC_RETURN(ATOMIC_T *v);

/* File operation APIs, just for linux now */
int rtw_is_file_readable(char *path);
int rtw_retrive_from_file(char *path, u8 __user *buf, u32 sz);
int rtw_store_to_file(char *path, u8 __user *buf, u32 sz);


struct rtw_netdev_priv_indicator {
	void *priv;
	u32 sizeof_priv;
};
struct net_device *rtw_alloc_etherdev_with_old_priv(int sizeof_priv, void *old_priv);
extern struct net_device * rtw_alloc_etherdev(int sizeof_priv);

#define rtw_netdev_priv(netdev) (((struct rtw_netdev_priv_indicator *)netdev_priv(netdev))->priv)

extern void rtw_free_netdev(struct net_device * netdev);

#define NDEV_FMT "%s"
#define NDEV_ARG(ndev) ndev->name
#define ADPT_FMT "%s"
#define ADPT_ARG(adapter) adapter->pnetdev->name
#define FUNC_NDEV_FMT "%s(%s)"
#define FUNC_NDEV_ARG(ndev) __func__, ndev->name
#define FUNC_ADPT_FMT "%s(%s)"
#define FUNC_ADPT_ARG(adapter) __func__, adapter->pnetdev->name

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
#define rtw_signal_process(pid, sig) kill_pid(find_vpid((pid)),(sig), 1)
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)) */
#define rtw_signal_process(pid, sig) kill_proc((pid), (sig), 1)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)) */

extern u64 rtw_modular64(u64 x, u64 y);
extern u64 rtw_division64(u64 x, u64 y);


/* Macros for handling unaligned memory accesses */

#define RTW_GET_BE16(a) ((u16) (((a)[0] << 8) | (a)[1]))
#define RTW_PUT_BE16(a, val)			\
	do {					\
		(a)[0] = ((u16) (val)) >> 8;	\
		(a)[1] = ((u16) (val)) & 0xff;	\
	} while (0)

#define RTW_GET_LE16(a) ((u16) (((a)[1] << 8) | (a)[0]))
#define RTW_PUT_LE16(a, val)			\
	do {					\
		(a)[1] = ((u16) (val)) >> 8;	\
		(a)[0] = ((u16) (val)) & 0xff;	\
	} while (0)

#define RTW_GET_BE24(a) ((((u32) (a)[0]) << 16) | (((u32) (a)[1]) << 8) | \
			 ((u32) (a)[2]))
#define RTW_PUT_BE24(a, val)					\
	do {							\
		(a)[0] = (u8) ((((u32) (val)) >> 16) & 0xff);	\
		(a)[1] = (u8) ((((u32) (val)) >> 8) & 0xff);	\
		(a)[2] = (u8) (((u32) (val)) & 0xff);		\
	} while (0)

#define RTW_GET_BE32(a) ((((u32) (a)[0]) << 24) | (((u32) (a)[1]) << 16) | \
			 (((u32) (a)[2]) << 8) | ((u32) (a)[3]))
#define RTW_PUT_BE32(a, val)					\
	do {							\
		(a)[0] = (u8) ((((u32) (val)) >> 24) & 0xff);	\
		(a)[1] = (u8) ((((u32) (val)) >> 16) & 0xff);	\
		(a)[2] = (u8) ((((u32) (val)) >> 8) & 0xff);	\
		(a)[3] = (u8) (((u32) (val)) & 0xff);		\
	} while (0)

#define RTW_GET_LE32(a) ((((u32) (a)[3]) << 24) | (((u32) (a)[2]) << 16) | \
			 (((u32) (a)[1]) << 8) | ((u32) (a)[0]))
#define RTW_PUT_LE32(a, val)					\
	do {							\
		(a)[3] = (u8) ((((u32) (val)) >> 24) & 0xff);	\
		(a)[2] = (u8) ((((u32) (val)) >> 16) & 0xff);	\
		(a)[1] = (u8) ((((u32) (val)) >> 8) & 0xff);	\
		(a)[0] = (u8) (((u32) (val)) & 0xff);		\
	} while (0)

#define RTW_GET_BE64(a) ((((u64) (a)[0]) << 56) | (((u64) (a)[1]) << 48) | \
			 (((u64) (a)[2]) << 40) | (((u64) (a)[3]) << 32) | \
			 (((u64) (a)[4]) << 24) | (((u64) (a)[5]) << 16) | \
			 (((u64) (a)[6]) << 8) | ((u64) (a)[7]))
#define RTW_PUT_BE64(a, val)				\
	do {						\
		(a)[0] = (u8) (((u64) (val)) >> 56);	\
		(a)[1] = (u8) (((u64) (val)) >> 48);	\
		(a)[2] = (u8) (((u64) (val)) >> 40);	\
		(a)[3] = (u8) (((u64) (val)) >> 32);	\
		(a)[4] = (u8) (((u64) (val)) >> 24);	\
		(a)[5] = (u8) (((u64) (val)) >> 16);	\
		(a)[6] = (u8) (((u64) (val)) >> 8);	\
		(a)[7] = (u8) (((u64) (val)) & 0xff);	\
	} while (0)

#define RTW_GET_LE64(a) ((((u64) (a)[7]) << 56) | (((u64) (a)[6]) << 48) | \
			 (((u64) (a)[5]) << 40) | (((u64) (a)[4]) << 32) | \
			 (((u64) (a)[3]) << 24) | (((u64) (a)[2]) << 16) | \
			 (((u64) (a)[1]) << 8) | ((u64) (a)[0]))

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
void rtw_cbuf_free(struct rtw_cbuf *cbuf);

#endif
