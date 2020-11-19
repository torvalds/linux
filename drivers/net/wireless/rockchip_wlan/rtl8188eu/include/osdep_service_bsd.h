/* SPDX-License-Identifier: GPL-2.0 */
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
#ifndef __OSDEP_BSD_SERVICE_H_
#define __OSDEP_BSD_SERVICE_H_


#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kdb.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/time.h>
#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/route.h>


#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_ratectl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR rum_debug
#include <dev/usb/usb_debug.h>

#if 1 //Baron porting from linux, it's all temp solution, needs to check again
#include <sys/sema.h>
#include <sys/pcpu.h> /* XXX for PCPU_GET */
//	typedef struct 	semaphore _sema;
	typedef struct 	sema _sema;
//	typedef	spinlock_t	_lock;
	typedef	struct mtx	_lock;
	typedef struct mtx 		_mutex;
	typedef struct rtw_timer_list _timer;
	struct list_head {
	struct list_head *next, *prev;
	};
	struct	__queue	{
		struct	list_head	queue;	
		_lock	lock;
	};

	typedef	struct mbuf _pkt;
	typedef struct mbuf	_buffer;
	
	typedef struct	__queue	_queue;
	typedef struct	list_head	_list;
	typedef	int	_OS_STATUS;
	//typedef u32	_irqL;
	typedef unsigned long _irqL;
	typedef	struct	ifnet * _nic_hdl;
	
	typedef pid_t		_thread_hdl_;
//	typedef struct thread		_thread_hdl_;
	typedef void		thread_return;
	typedef void*	thread_context;

	typedef void timer_hdl_return;
	typedef void* timer_hdl_context;
	typedef struct work_struct _workitem;
	typedef struct task _tasklet;

#define   KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
/* emulate a modern version */
#define LINUX_VERSION_CODE KERNEL_VERSION(2, 6, 35)

#define WIRELESS_EXT -1
#define HZ hz
#define spin_lock_irqsave mtx_lock_irqsave
#define spin_lock_bh mtx_lock_irqsave
#define mtx_lock_irqsave(lock, x) mtx_lock(lock)//{local_irq_save((x)); mtx_lock_spin((lock));}
//#define IFT_RTW	0xf9 //ifnet allocate type for RTW
#define free_netdev if_free
#define LIST_CONTAINOR(ptr, type, member) \
        ((type *)((char *)(ptr)-(SIZE_T)(&((type *)0)->member)))
#define container_of(p,t,n) (t*)((p)-&(((t*)0)->n))
/* 
 * Linux timers are emulated using FreeBSD callout functions
 * (and taskqueue functionality).
 *
 * Currently no timer stats functionality.
 *
 * See (linux_compat) processes.c
 *
 */
struct rtw_timer_list {
	struct callout callout;
	void (*function)(void *);
	void *arg;
};

struct workqueue_struct;
struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);
/* Values for the state of an item of work (work_struct) */
typedef enum work_state {
        WORK_STATE_UNSET = 0,
        WORK_STATE_CALLOUT_PENDING = 1,
        WORK_STATE_TASK_PENDING = 2,
        WORK_STATE_WORK_CANCELLED = 3        
} work_state_t;

struct work_struct {
        struct task task; /* FreeBSD task */
        work_state_t state; /* the pending or otherwise state of work. */
        work_func_t func;       
};
#define spin_unlock_irqrestore mtx_unlock_irqrestore
#define spin_unlock_bh mtx_unlock_irqrestore
#define mtx_unlock_irqrestore(lock,x)    mtx_unlock(lock);
extern void	_rtw_spinlock_init(_lock *plock);

//modify private structure to match freebsd
#define BITS_PER_LONG 32
union ktime {
	s64	tv64;
#if BITS_PER_LONG != 64 && !defined(CONFIG_KTIME_SCALAR)
	struct {
#ifdef __BIG_ENDIAN
	s32	sec, nsec;
#else
	s32	nsec, sec;
#endif
	} tv;
#endif
};
#define kmemcheck_bitfield_begin(name)
#define kmemcheck_bitfield_end(name)
#define CHECKSUM_NONE 0
typedef unsigned char *sk_buff_data_t;
typedef union ktime ktime_t;		/* Kill this */

void rtw_mtx_lock(_lock *plock);
	
void rtw_mtx_unlock(_lock *plock);

/** 
 *	struct sk_buff - socket buffer
 *	@next: Next buffer in list
 *	@prev: Previous buffer in list
 *	@sk: Socket we are owned by
 *	@tstamp: Time we arrived
 *	@dev: Device we arrived on/are leaving by
 *	@transport_header: Transport layer header
 *	@network_header: Network layer header
 *	@mac_header: Link layer header
 *	@_skb_refdst: destination entry (with norefcount bit)
 *	@sp: the security path, used for xfrm
 *	@cb: Control buffer. Free for use by every layer. Put private vars here
 *	@len: Length of actual data
 *	@data_len: Data length
 *	@mac_len: Length of link layer header
 *	@hdr_len: writable header length of cloned skb
 *	@csum: Checksum (must include start/offset pair)
 *	@csum_start: Offset from skb->head where checksumming should start
 *	@csum_offset: Offset from csum_start where checksum should be stored
 *	@local_df: allow local fragmentation
 *	@cloned: Head may be cloned (check refcnt to be sure)
 *	@nohdr: Payload reference only, must not modify header
 *	@pkt_type: Packet class
 *	@fclone: skbuff clone status
 *	@ip_summed: Driver fed us an IP checksum
 *	@priority: Packet queueing priority
 *	@users: User count - see {datagram,tcp}.c
 *	@protocol: Packet protocol from driver
 *	@truesize: Buffer size 
 *	@head: Head of buffer
 *	@data: Data head pointer
 *	@tail: Tail pointer
 *	@end: End pointer
 *	@destructor: Destruct function
 *	@mark: Generic packet mark
 *	@nfct: Associated connection, if any
 *	@ipvs_property: skbuff is owned by ipvs
 *	@peeked: this packet has been seen already, so stats have been
 *		done for it, don't do them again
 *	@nf_trace: netfilter packet trace flag
 *	@nfctinfo: Relationship of this skb to the connection
 *	@nfct_reasm: netfilter conntrack re-assembly pointer
 *	@nf_bridge: Saved data about a bridged frame - see br_netfilter.c
 *	@skb_iif: ifindex of device we arrived on
 *	@rxhash: the packet hash computed on receive
 *	@queue_mapping: Queue mapping for multiqueue devices
 *	@tc_index: Traffic control index
 *	@tc_verd: traffic control verdict
 *	@ndisc_nodetype: router type (from link layer)
 *	@dma_cookie: a cookie to one of several possible DMA operations
 *		done by skb DMA functions
 *	@secmark: security marking
 *	@vlan_tci: vlan tag control information
 */

struct sk_buff {
	/* These two members must be first. */
	struct sk_buff		*next;
	struct sk_buff		*prev;

	ktime_t			tstamp;

	struct sock		*sk;
	//struct net_device	*dev;
	struct ifnet *dev;

	/*
	 * This is the control buffer. It is free to use for every
	 * layer. Please put your private variables there. If you
	 * want to keep them across layers you have to do a skb_clone()
	 * first. This is owned by whoever has the skb queued ATM.
	 */
	char			cb[48] __aligned(8);

	unsigned long		_skb_refdst;
#ifdef CONFIG_XFRM
	struct	sec_path	*sp;
#endif
	unsigned int		len,
				data_len;
	u16			mac_len,
				hdr_len;
	union {
		u32		csum;
		struct {
			u16	csum_start;
			u16	csum_offset;
		}smbol2;
	}smbol1;
	u32			priority;
	kmemcheck_bitfield_begin(flags1);
	u8			local_df:1,
				cloned:1,
				ip_summed:2,
				nohdr:1,
				nfctinfo:3;
	u8			pkt_type:3,
				fclone:2,
				ipvs_property:1,
				peeked:1,
				nf_trace:1;
	kmemcheck_bitfield_end(flags1);
	u16			protocol;

	void			(*destructor)(struct sk_buff *skb);
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	struct nf_conntrack	*nfct;
	struct sk_buff		*nfct_reasm;
#endif
#ifdef CONFIG_BRIDGE_NETFILTER
	struct nf_bridge_info	*nf_bridge;
#endif

	int			skb_iif;
#ifdef CONFIG_NET_SCHED
	u16			tc_index;	/* traffic control index */
#ifdef CONFIG_NET_CLS_ACT
	u16			tc_verd;	/* traffic control verdict */
#endif
#endif

	u32			rxhash;

	kmemcheck_bitfield_begin(flags2);
	u16			queue_mapping:16;
#ifdef CONFIG_IPV6_NDISC_NODETYPE
	u8			ndisc_nodetype:2,
				deliver_no_wcard:1;
#else
	u8			deliver_no_wcard:1;
#endif
	kmemcheck_bitfield_end(flags2);

	/* 0/14 bit hole */

#ifdef CONFIG_NET_DMA
	dma_cookie_t		dma_cookie;
#endif
#ifdef CONFIG_NETWORK_SECMARK
	u32			secmark;
#endif
	union {
		u32		mark;
		u32		dropcount;
	}symbol3;

	u16			vlan_tci;

	sk_buff_data_t		transport_header;
	sk_buff_data_t		network_header;
	sk_buff_data_t		mac_header;
	/* These elements must be at the end, see alloc_skb() for details.  */
	sk_buff_data_t		tail;
	sk_buff_data_t		end;
	unsigned char		*head,
				*data;
	unsigned int		truesize;
	atomic_t		users;
};
struct sk_buff_head {
	/* These two members must be first. */
	struct sk_buff	*next;
	struct sk_buff	*prev;

	u32		qlen;
	_lock	lock;
};
#define skb_tail_pointer(skb)	skb->tail
static inline unsigned char *skb_put(struct sk_buff *skb, unsigned int len)
{
	unsigned char *tmp = skb_tail_pointer(skb);
	//SKB_LINEAR_ASSERT(skb);
	skb->tail += len;
	skb->len  += len;
	return tmp;
}

static inline unsigned char *__skb_pull(struct sk_buff *skb, unsigned int len)
{
	skb->len -= len;
	if(skb->len < skb->data_len)
		printf("%s(),%d,error!\n",__FUNCTION__,__LINE__);
	return skb->data += len;
}
static inline unsigned char *skb_pull(struct sk_buff *skb, unsigned int len)
{
	#ifdef PLATFORM_FREEBSD
	return __skb_pull(skb, len);
	#else
	return unlikely(len > skb->len) ? NULL : __skb_pull(skb, len);
	#endif //PLATFORM_FREEBSD
}
static inline u32 skb_queue_len(const struct sk_buff_head *list_)
{
	return list_->qlen;
}
static inline void __skb_insert(struct sk_buff *newsk,
				struct sk_buff *prev, struct sk_buff *next,
				struct sk_buff_head *list)
{
	newsk->next = next;
	newsk->prev = prev;
	next->prev  = prev->next = newsk;
	list->qlen++;
}
static inline void __skb_queue_before(struct sk_buff_head *list,
				      struct sk_buff *next,
				      struct sk_buff *newsk)
{
	__skb_insert(newsk, next->prev, next, list);
}
static inline void skb_queue_tail(struct sk_buff_head *list,
				   struct sk_buff *newsk)
{
	mtx_lock(&list->lock);
	__skb_queue_before(list, (struct sk_buff *)list, newsk);
	mtx_unlock(&list->lock);
}
static inline struct sk_buff *skb_peek(struct sk_buff_head *list_)
{
	struct sk_buff *list = ((struct sk_buff *)list_)->next;
	if (list == (struct sk_buff *)list_)
		list = NULL;
	return list;
}
static inline void __skb_unlink(struct sk_buff *skb, struct sk_buff_head *list)
{
	struct sk_buff *next, *prev;

	list->qlen--;
	next	   = skb->next;
	prev	   = skb->prev;
	skb->next  = skb->prev = NULL;
	next->prev = prev;
	prev->next = next;
}

static inline struct sk_buff *skb_dequeue(struct sk_buff_head *list)
{
	mtx_lock(&list->lock);

	struct sk_buff *skb = skb_peek(list);
	if (skb)
		__skb_unlink(skb, list);

	mtx_unlock(&list->lock);

	return skb;
}
static inline void skb_reserve(struct sk_buff *skb, int len)
{
	skb->data += len;
	skb->tail += len;
}
static inline void __skb_queue_head_init(struct sk_buff_head *list)
{
	list->prev = list->next = (struct sk_buff *)list;
	list->qlen = 0;
}
/*
 * This function creates a split out lock class for each invocation;
 * this is needed for now since a whole lot of users of the skb-queue
 * infrastructure in drivers have different locking usage (in hardirq)
 * than the networking core (in softirq only). In the long run either the
 * network layer or drivers should need annotation to consolidate the
 * main types of usage into 3 classes.
 */
static inline void skb_queue_head_init(struct sk_buff_head *list)
{
	_rtw_spinlock_init(&list->lock);
	__skb_queue_head_init(list);
}
unsigned long copy_from_user(void *to, const void *from, unsigned long n);
unsigned long copy_to_user(void *to, const void *from, unsigned long n);
struct sk_buff * dev_alloc_skb(unsigned int size);
struct sk_buff *skb_clone(const struct sk_buff *skb);
void dev_kfree_skb_any(struct sk_buff *skb);
#endif //Baron porting from linux, it's all temp solution, needs to check again


#if 1 // kenny add Linux compatibility code for Linux USB driver
#include <dev/usb/usb_compat_linux.h>

#define __init		// __attribute ((constructor))
#define __exit		// __attribute ((destructor))

/*
 * Definitions for module_init and module_exit macros.
 *
 * These macros will use the SYSINIT framework to call a specified
 * function (with no arguments) on module loading or unloading.
 * 
 */

void module_init_exit_wrapper(void *arg);

#define module_init(initfn)                             \
        SYSINIT(mod_init_ ## initfn,                    \
                SI_SUB_KLD, SI_ORDER_FIRST,             \
                module_init_exit_wrapper, initfn)

#define module_exit(exitfn)                             \
        SYSUNINIT(mod_exit_ ## exitfn,                  \
                  SI_SUB_KLD, SI_ORDER_ANY,             \
                  module_init_exit_wrapper, exitfn)

/*
 * The usb_register and usb_deregister functions are used to register
 * usb drivers with the usb subsystem. 
 */
int usb_register(struct usb_driver *driver);
int usb_deregister(struct usb_driver *driver);

/*
 * usb_get_dev and usb_put_dev - increment/decrement the reference count 
 * of the usb device structure.
 *
 * Original body of usb_get_dev:
 *
 *       if (dev)
 *               get_device(&dev->dev);
 *       return dev;
 *
 * Reference counts are not currently used in this compatibility
 * layer. So these functions will do nothing.
 */
static inline struct usb_device *
usb_get_dev(struct usb_device *dev)
{
        return dev;
}

static inline void 
usb_put_dev(struct usb_device *dev)
{
        return;
}


// rtw_usb_compat_linux
int rtw_usb_submit_urb(struct urb *urb, uint16_t mem_flags);
int rtw_usb_unlink_urb(struct urb *urb);
int rtw_usb_clear_halt(struct usb_device *dev, struct usb_host_endpoint *uhe);
int rtw_usb_control_msg(struct usb_device *dev, struct usb_host_endpoint *uhe,
    uint8_t request, uint8_t requesttype,
    uint16_t value, uint16_t index, void *data,
    uint16_t size, usb_timeout_t timeout);
int rtw_usb_set_interface(struct usb_device *dev, uint8_t iface_no, uint8_t alt_index);
int rtw_usb_setup_endpoint(struct usb_device *dev,
    struct usb_host_endpoint *uhe, usb_size_t bufsize);
struct urb *rtw_usb_alloc_urb(uint16_t iso_packets, uint16_t mem_flags);
struct usb_host_endpoint *rtw_usb_find_host_endpoint(struct usb_device *dev, uint8_t type, uint8_t ep);
struct usb_host_interface *rtw_usb_altnum_to_altsetting(const struct usb_interface *intf, uint8_t alt_index);
struct usb_interface *rtw_usb_ifnum_to_if(struct usb_device *dev, uint8_t iface_no);
void *rtw_usbd_get_intfdata(struct usb_interface *intf);
void rtw_usb_linux_register(void *arg);
void rtw_usb_linux_deregister(void *arg);
void rtw_usb_linux_free_device(struct usb_device *dev);
void rtw_usb_free_urb(struct urb *urb);
void rtw_usb_init_urb(struct urb *urb);
void rtw_usb_kill_urb(struct urb *urb);
void rtw_usb_set_intfdata(struct usb_interface *intf, void *data);
void rtw_usb_fill_bulk_urb(struct urb *urb, struct usb_device *udev,
    struct usb_host_endpoint *uhe, void *buf,
    int length, usb_complete_t callback, void *arg);
int rtw_usb_bulk_msg(struct usb_device *udev, struct usb_host_endpoint *uhe,
    void *data, int len, uint16_t *pactlen, usb_timeout_t timeout);
void *usb_get_intfdata(struct usb_interface *intf);
int usb_linux_init_endpoints(struct usb_device *udev);



typedef struct urb *  PURB;

typedef unsigned gfp_t;
#define __GFP_WAIT      ((gfp_t)0x10u)  /* Can wait and reschedule? */
#define __GFP_HIGH      ((gfp_t)0x20u)  /* Should access emergency pools? */
#define __GFP_IO        ((gfp_t)0x40u)  /* Can start physical IO? */
#define __GFP_FS        ((gfp_t)0x80u)  /* Can call down to low-level FS? */
#define __GFP_COLD      ((gfp_t)0x100u) /* Cache-cold page required */
#define __GFP_NOWARN    ((gfp_t)0x200u) /* Suppress page allocation failure warning */
#define __GFP_REPEAT    ((gfp_t)0x400u) /* Retry the allocation.  Might fail */
#define __GFP_NOFAIL    ((gfp_t)0x800u) /* Retry for ever.  Cannot fail */
#define __GFP_NORETRY   ((gfp_t)0x1000u)/* Do not retry.  Might fail */
#define __GFP_NO_GROW   ((gfp_t)0x2000u)/* Slab internal usage */
#define __GFP_COMP      ((gfp_t)0x4000u)/* Add compound page metadata */
#define __GFP_ZERO      ((gfp_t)0x8000u)/* Return zeroed page on success */
#define __GFP_NOMEMALLOC ((gfp_t)0x10000u) /* Don't use emergency reserves */
#define __GFP_HARDWALL   ((gfp_t)0x20000u) /* Enforce hardwall cpuset memory allocs */

/* This equals 0, but use constants in case they ever change */
#define GFP_NOWAIT      (GFP_ATOMIC & ~__GFP_HIGH)
/* GFP_ATOMIC means both !wait (__GFP_WAIT not set) and use emergency pool */
#define GFP_ATOMIC      (__GFP_HIGH)
#define GFP_NOIO        (__GFP_WAIT)
#define GFP_NOFS        (__GFP_WAIT | __GFP_IO)
#define GFP_KERNEL      (__GFP_WAIT | __GFP_IO | __GFP_FS)
#define GFP_USER        (__GFP_WAIT | __GFP_IO | __GFP_FS | __GFP_HARDWALL)
#define GFP_HIGHUSER    (__GFP_WAIT | __GFP_IO | __GFP_FS | __GFP_HARDWALL | \
                         __GFP_HIGHMEM)


#endif // kenny add Linux compatibility code for Linux USB

__inline static _list *get_next(_list	*list)
{
	return list->next;
}	

__inline static _list	*get_list_head(_queue	*queue)
{
	return (&(queue->queue));
}

	
#define LIST_CONTAINOR(ptr, type, member) \
        ((type *)((char *)(ptr)-(SIZE_T)(&((type *)0)->member)))	

        
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
	spin_lock_bh(plock, *pirqL);
}

__inline static void _exit_critical_bh(_lock *plock, _irqL *pirqL)
{
	spin_unlock_bh(plock, *pirqL);
}

__inline static void _enter_critical_mutex(_mutex *pmutex, _irqL *pirqL)
{

		mtx_lock(pmutex);

}


__inline static void _exit_critical_mutex(_mutex *pmutex, _irqL *pirqL)
{

		mtx_unlock(pmutex);

}
static inline void __list_del(struct list_head * prev, struct list_head * next)
{
	next->prev = prev;
	prev->next = next;
}
static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}
__inline static void rtw_list_delete(_list *plist)
{
	__list_del(plist->prev, plist->next);
	INIT_LIST_HEAD(plist);
}

static inline void timer_hdl(void *ctx)
{
	_timer *timer = (_timer *)ctx;

	rtw_mtx_lock(NULL);
	if (callout_pending(&timer->callout)) {
		/* callout was reset */
		rtw_mtx_unlock(NULL);
		return;
	}

	if (!callout_active(&timer->callout)) {
		/* callout was stopped */
		rtw_mtx_unlock(NULL);
		return;
	}

	callout_deactivate(&timer->callout);

	timer->function(timer->arg);

	rtw_mtx_unlock(NULL);
}

static inline void _init_timer(_timer *ptimer, _nic_hdl padapter, void *pfunc, void *cntx)
{
	ptimer->function = pfunc;
	ptimer->arg = cntx;
	callout_init(&ptimer->callout, CALLOUT_MPSAFE);
}

__inline static void _set_timer(_timer *ptimer,u32 delay_time)
{	
	if (ptimer->function && ptimer->arg) {
		rtw_mtx_lock(NULL);
		callout_reset(&ptimer->callout, delay_time, timer_hdl, ptimer);
		rtw_mtx_unlock(NULL);
	}
}

__inline static void _cancel_timer(_timer *ptimer,u8 *bcancelled)
{
	rtw_mtx_lock(NULL);
	callout_drain(&ptimer->callout);
	rtw_mtx_unlock(NULL);
	*bcancelled = 1; /* assume an pending timer to be canceled */
}

__inline static void _init_workitem(_workitem *pwork, void *pfunc, PVOID cntx)
{
	printf("%s Not implement yet! \n",__FUNCTION__);
}

__inline static void _set_workitem(_workitem *pwork)
{
	printf("%s Not implement yet! \n",__FUNCTION__);
//	schedule_work(pwork);
}

//
// Global Mutex: can only be used at PASSIVE level.
//

#define ACQUIRE_GLOBAL_MUTEX(_MutexCounter)                              \
{                                                               \
}

#define RELEASE_GLOBAL_MUTEX(_MutexCounter)                              \
{                                                               \
}

#define ATOMIC_INIT(i)  { (i) }

static __inline void thread_enter(char *name);

//Atomic integer operations
typedef uint32_t ATOMIC_T ;

#define rtw_netdev_priv(netdev) (((struct ifnet *)netdev)->if_softc)

#define rtw_free_netdev(netdev) if_free((netdev))

#define NDEV_FMT "%s"
#define NDEV_ARG(ndev) ""
#define ADPT_FMT "%s"
#define ADPT_ARG(adapter) ""
#define FUNC_NDEV_FMT "%s"
#define FUNC_NDEV_ARG(ndev) __func__
#define FUNC_ADPT_FMT "%s"
#define FUNC_ADPT_ARG(adapter) __func__

#define STRUCT_PACKED

#endif

