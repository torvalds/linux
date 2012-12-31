/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/ 
#ifndef __OSDEP_SERVICE_H_
#define __OSDEP_SERVICE_H_

#include <drv_conf.h>
#include <basic_types.h>
//#include <rtl871x_byteorder.h>

#define _SUCCESS	1
#define _FAIL		0

#undef _TRUE
#define _TRUE		1

#undef _FALSE
#define _FALSE		0


#ifdef PLATFORM_LINUX
	#include <linux/version.h>
	#include <linux/spinlock.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
	#include <asm/semaphore.h>
#else
	#include <linux/semaphore.h>
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
	#include <linux/sched.h>
#endif
	#include <linux/sem.h>
	#include <linux/netdevice.h>
	#include <linux/etherdevice.h>
	#include <net/iw_handler.h>
	#include <linux/proc_fs.h>	// Necessary because we use the proc fs
	
#ifdef CONFIG_USB_HCI
	typedef struct urb *  PURB;
#endif

	typedef struct 	semaphore _sema;
	typedef	spinlock_t	_lock;
        typedef struct semaphore	_rwlock;
	typedef struct timer_list _timer;

	struct	__queue	{
		struct	list_head	queue;	
		_lock	lock;
	};

	typedef	struct sk_buff	_pkt;
	typedef unsigned char	_buffer;
	
	typedef struct	__queue	_queue;
	typedef struct	list_head	_list;
	typedef	int	_OS_STATUS;
	//typedef u32	_irqL;
	typedef unsigned long _irqL;
	typedef	struct	net_device * _nic_hdl;
	
	typedef pid_t		_thread_hdl_;
	typedef int		thread_return;
	typedef void*	thread_context;

	#define thread_exit() complete_and_exit(NULL, 0)

	typedef void timer_hdl_return;
	typedef void* timer_hdl_context;
	typedef struct work_struct _workitem;
	

static __inline _list *get_next(_list	*list)
{
	return list->next;
}	

static __inline _list	*get_list_head(_queue	*queue)
{
	return (&(queue->queue));
}

	
#define LIST_CONTAINOR(ptr, type, member) \
        ((type *)((char *)(ptr)-(SIZE_T)(&((type *)0)->member)))	

        
static void __inline _enter_critical(_lock *plock, _irqL *pirqL)
{
	spin_lock_irqsave(plock, *pirqL);
}

static void __inline _exit_critical(_lock *plock, _irqL *pirqL)
{
	spin_unlock_irqrestore(plock, *pirqL);
}

static void __inline _enter_critical_ex(_lock *plock, _irqL *pirqL)
{
	spin_lock_irqsave(plock, *pirqL);
}

static void __inline _exit_critical_ex(_lock *plock, _irqL *pirqL)
{
	spin_unlock_irqrestore(plock, *pirqL);
}

static void __inline _enter_hwio_critical(_rwlock *prwlock, _irqL *pirqL)
{
		down(prwlock);
}


static void __inline _exit_hwio_critical(_rwlock *prwlock, _irqL *pirqL)
{
		up(prwlock);
}

static __inline void list_delete(_list *plist)
{
	


	list_del_init(plist);
	

	
}

static __inline void _init_timer(_timer *ptimer,_nic_hdl padapter,void *pfunc,void* cntx)
{
	//setup_timer(ptimer, pfunc,(u32)cntx);	
	ptimer->function = pfunc;
	ptimer->data = (u32)cntx;
	init_timer(ptimer);
}

static __inline void _set_timer(_timer *ptimer,u32 delay_time)
{	
	mod_timer(ptimer , (jiffies+(delay_time*HZ/1000)));	
}

static __inline void _cancel_timer(_timer *ptimer,u8 *bcancelled)
{
    // Change del_timer_sync to del_timer (HARDKERNEL)
	//del_timer_sync(ptimer); 	
	del_timer(ptimer);
	*bcancelled=  _TRUE;//TRUE ==1; FALSE==0
}

static __inline void _init_workitem(_workitem *pwork, void *pfunc, PVOID cntx)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
	INIT_WORK(pwork, pfunc);
#else
	INIT_WORK(pwork, pfunc,pwork);
#endif
}

static __inline void _set_workitem(_workitem *pwork)
{
	schedule_work(pwork);
}

#endif	


#ifdef PLATFORM_OS_XP

	#include <ndis.h>
	#include <ntddk.h>
	#include <ntddsd.h>
	#include <ntddndis.h>
	#include <ntdef.h>
	

	typedef KSEMAPHORE 	_sema;
	typedef	LIST_ENTRY	_list;
	typedef NDIS_STATUS _OS_STATUS;
	

	typedef NDIS_SPIN_LOCK	_lock;

	typedef KMUTEX 			_rwlock;

	typedef KIRQL	_irqL;

	// USB_PIPE for WINCE , but handle can be use just integer under windows
	typedef NDIS_HANDLE  _nic_hdl;


	typedef NDIS_MINIPORT_TIMER    _timer;

	struct	__queue	{
		LIST_ENTRY	queue;	
		_lock	lock;
	};

	typedef	NDIS_PACKET	_pkt;
	typedef NDIS_BUFFER	_buffer;
	typedef struct	__queue	_queue;
	
	typedef PKTHREAD _thread_hdl_;
	typedef void	thread_return;
	typedef void* thread_context;

	typedef NDIS_WORK_ITEM _workitem;

	#define thread_exit() PsTerminateSystemThread(STATUS_SUCCESS);

	
	#define SEMA_UPBND	(0x7FFFFFFF)   //8192
	
static __inline _list *get_next(_list	*list)
{
	return list->Flink;
}	

static __inline _list	*get_list_head(_queue	*queue)
{
	return (&(queue->queue));
}
	

#define LIST_CONTAINOR(ptr, type, member) CONTAINING_RECORD(ptr, type, member)
     

static __inline _enter_critical(_lock *plock, _irqL *pirqL)
{
	NdisAcquireSpinLock(plock);	
}

static __inline _exit_critical(_lock *plock, _irqL *pirqL)
{
	NdisReleaseSpinLock(plock);	
}


static __inline _enter_critical_ex(_lock *plock, _irqL *pirqL)
{
	NdisDprAcquireSpinLock(plock);	
}

static __inline _exit_critical_ex(_lock *plock, _irqL *pirqL)
{
	NdisDprReleaseSpinLock(plock);	
}


static __inline _enter_hwio_critical(_rwlock *prwlock, _irqL *pirqL)
{
	KeWaitForSingleObject(prwlock, Executive, KernelMode, FALSE, NULL);
}


static __inline _exit_hwio_critical(_rwlock *prwlock, _irqL *pirqL)
{
	KeReleaseMutex(prwlock, FALSE);
}


static __inline void list_delete(_list *plist)
{
	RemoveEntryList(plist);
	InitializeListHead(plist);	
}

static __inline void _init_timer(_timer *ptimer,_nic_hdl padapter,void *pfunc,PVOID cntx)
{
	NdisMInitializeTimer(ptimer, padapter, pfunc, cntx);
}

static __inline void _set_timer(_timer *ptimer,u32 delay_time)
{	
 	NdisMSetTimer(ptimer,delay_time);	
}

static __inline void _cancel_timer(_timer *ptimer,u8 *bcancelled)
{
	NdisMCancelTimer(ptimer,bcancelled);
}

static __inline void _init_workitem(_workitem *pwork, void *pfunc, PVOID cntx)
{

	NdisInitializeWorkItem(pwork, pfunc, cntx);
}

static __inline void _set_workitem(_workitem *pwork)
{
	NdisScheduleWorkItem(pwork);
}

#endif


#ifdef PLATFORM_OS_CE
#include <osdep_ce_service.h>
#endif

#include <rtl871x_byteorder.h>

#ifdef CONFIG_IOCTL_CFG80211	
//	#include <linux/ieee80211.h>        
        #include <net/ieee80211_radiotap.h>
	#include <net/cfg80211.h>	
#endif //CONFIG_IOCTL_CFG80211

#ifndef BIT
	#define BIT(x)	( 1 << (x))
#endif

extern u8*	_malloc(u32 sz);
extern void	_mfree(u8 *pbuf, u32 sz);
extern void	_memcpy(void* dec, void* sour, u32 sz);
extern int	_memcmp(void *dst, void *src, u32 sz);
extern void	_memset(void *pbuf, int c, u32 sz);

extern void	_init_listhead(_list *list);
extern u32	is_list_empty(_list *phead);
extern void	list_insert_tail(_list *plist, _list *phead);
extern void	list_delete(_list *plist);
extern void	_init_sema(_sema *sema, int init_val);
extern void	_free_sema(_sema	*sema);
extern void	_up_sema(_sema	*sema);
extern u32	_down_sema(_sema *sema);
extern void	_rtl_rwlock_init(_rwlock *prwlock);
extern void	_spinlock_init(_lock *plock);
extern void	_spinlock_free(_lock *plock);
extern void	_spinlock(_lock	*plock);
extern void	_spinunlock(_lock	*plock);
extern void	_spinlock_ex(_lock	*plock);
extern void	_spinunlock_ex(_lock	*plock);
extern void	_init_queue(_queue	*pqueue);
extern u32	_queue_empty(_queue	*pqueue);
extern u32	end_of_queue_search(_list *queue, _list *pelement);
extern u32	get_current_time(void);

extern void	sleep_schedulable(int ms);

extern void	msleep_os(int ms);
extern void	usleep_os(int us);
extern void	mdelay_os(int ms);
extern void	udelay_os(int us);
extern u8*	_zmalloc(u32 sz);
extern u8	*	_vmalloc(u32 sz);
extern u8	*	_zvmalloc(u32 sz);
extern void	_vmfree(u8 * pbuf, u32 sz);

struct rtw_netdev_priv_indicator {
	void *priv;
	u32 sizeof_priv;
};
extern struct net_device * rtw_alloc_etherdev(int sizeof_priv);
#define rtw_netdev_priv(netdev) ( ((struct rtw_netdev_priv_indicator *)netdev_priv(netdev))->priv )
extern void rtw_free_netdev(struct net_device * netdev);

static __inline unsigned char _cancel_timer_ex(_timer *ptimer)
{	
#ifdef PLATFORM_LINUX
    // Change del_timer_sync to del_timer (HARDKERNEL)
    //	return del_timer_sync(ptimer);
    return del_timer(ptimer);
#endif

#ifdef PLATFORM_WINDOWS
	u8 bool;
	
	_cancel_timer(ptimer, &bool);
	
	return bool;
#endif
}

static __inline void thread_enter(void *context)
{
#ifdef PLATFORM_LINUX
	//struct net_device *pnetdev = (struct net_device *)context;
	//daemonize("%s", pnetdev->name);
	daemonize("%s", "RTKTHREAD");
	allow_signal(SIGTERM);
#endif
}

static __inline void flush_signals_thread(void) 
{
#ifdef PLATFORM_LINUX
	if (signal_pending (current)) 
	{
		flush_signals(current);
	}
#endif
}

static __inline _OS_STATUS res_to_status(sint res)
{


#if defined (PLATFORM_LINUX) || defined (PLATFORM_MPIXEL)
	return res;
#endif

#ifdef PLATFORM_WINDOWS

	if (res == _SUCCESS)
		return NDIS_STATUS_SUCCESS;
	else
		return NDIS_STATUS_FAILURE;

#endif	
	
}


static __inline u32 _RND8(u32 sz)
{

	u32	val;

	val = ((sz >> 3) + ((sz & 7) ? 1: 0)) << 3;
	
	return val;

}

static __inline u32 _RND128(u32 sz)
{

	u32	val;

	val = ((sz >> 7) + ((sz & 127) ? 1: 0)) << 7;
	
	return val;

}

static __inline u32 _RND256(u32 sz)
{

	u32	val;

	val = ((sz >> 8) + ((sz & 255) ? 1: 0)) << 8;
	
	return val;

}

static __inline u32 _RND512(u32 sz)
{

	u32	val;

	val = ((sz >> 9) + ((sz & 511) ? 1: 0)) << 9;
	
	return val;

}

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

//#ifdef __GNUC__
#ifdef PLATFORM_LINUX
#define STRUCT_PACKED __attribute__ ((packed))
#else
#define STRUCT_PACKED
#endif


#endif

