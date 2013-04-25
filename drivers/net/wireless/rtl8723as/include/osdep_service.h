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
//#define RTW_STATUS_TIMEDOUT -110

#undef _TRUE
#define _TRUE		1

#undef _FALSE
#define _FALSE		0
	

#ifdef PLATFORM_FREEBSD
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
	typedef struct timer_list _timer;
	struct list_head {
	struct list_head *next, *prev;
	};
	struct	__queue	{
		struct	list_head	queue;	
		_lock	lock;
	};

	//typedef	struct sk_buff	_pkt;
	typedef	struct mbuf	_pkt;
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

	//#define thread_exit() complete_and_exit(NULL, 0)

	typedef void timer_hdl_return;
	typedef void* timer_hdl_context;
	typedef struct work_struct _workitem;

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
struct timer_list {

        /* FreeBSD callout related fields */
        struct callout callout;

 	//timeout function
        void (*function)(void*);
	//argument
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
void *rtw_usb_buffer_alloc(struct usb_device *dev, usb_size_t size, uint16_t mem_flags, uint8_t *dma_addr);
void *rtw_usbd_get_intfdata(struct usb_interface *intf);
void rtw_usb_linux_register(void *arg);
void rtw_usb_linux_deregister(void *arg);
void rtw_usb_linux_free_device(struct usb_device *dev);
void rtw_usb_buffer_free(struct usb_device *dev, usb_size_t size,
    void *addr, uint8_t dma_addr);
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



#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22))
	#define skb_tail_pointer(skb)	skb->tail
#endif

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

__inline static void _init_timer(_timer *ptimer,_nic_hdl padapter,void *pfunc,void* cntx)
{
	ptimer->function = pfunc;
	ptimer->arg = cntx;
	callout_init(&ptimer->callout, CALLOUT_MPSAFE);
}

__inline static void _set_timer(_timer *ptimer,u32 delay_time)
{	
	//	mod_timer(ptimer , (jiffies+(delay_time*HZ/1000)));
	if(ptimer->function && ptimer->arg){
		rtw_mtx_lock(NULL);
		callout_reset(&ptimer->callout, delay_time,ptimer->function, ptimer->arg);
		rtw_mtx_unlock(NULL);
	}
}

__inline static void _cancel_timer(_timer *ptimer,u8 *bcancelled)
{
	//	del_timer_sync(ptimer); 	
	//	*bcancelled=  _TRUE;//TRUE ==1; FALSE==0	
	rtw_mtx_lock(NULL);
	callout_drain(&ptimer->callout);
	rtw_mtx_unlock(NULL);
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

#endif //PLATFORM_FREEBSD


#ifdef PLATFORM_LINUX
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
	//#include <linux/smp_lock.h>
	#include <linux/netdevice.h>
	#include <linux/skbuff.h>
	#include <linux/circ_buf.h>
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
	#include <linux/proc_fs.h>	// Necessary because we use the proc fs
	#include <linux/interrupt.h>	// for struct tasklet_struct

#ifdef CONFIG_IOCTL_CFG80211	
//	#include <linux/ieee80211.h>        
        #include <net/ieee80211_radiotap.h>
	#include <net/cfg80211.h>	
#endif //CONFIG_IOCTL_CFG80211

#ifdef CONFIG_TCP_CSUM_OFFLOAD_TX
	#include <linux/in.h>
	#include <linux/ip.h>
	#include <linux/udp.h>
#endif

#ifdef CONFIG_USB_HCI
	#include <linux/usb.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21))
	#include <linux/usb_ch9.h>
#else
	#include <linux/usb/ch9.h>
#endif
#endif

#ifdef CONFIG_PCI_HCI
	#include <linux/pci.h>
#endif

	
#ifdef CONFIG_USB_HCI
	typedef struct urb *  PURB;
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,22))
#ifdef CONFIG_USB_SUSPEND
#define CONFIG_AUTOSUSPEND	1
#endif
#endif
#endif

	typedef struct 	semaphore _sema;
	typedef	spinlock_t	_lock;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	typedef struct mutex 		_mutex;
#else
	typedef struct semaphore	_mutex;
#endif
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

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22))
	#define skb_tail_pointer(skb)	skb->tail
#endif

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
	spin_lock_bh(plock);
}

__inline static void _exit_critical_bh(_lock *plock, _irqL *pirqL)
{
	spin_unlock_bh(plock);
}

__inline static void _enter_critical_mutex(_mutex *pmutex, _irqL *pirqL)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
		mutex_lock(pmutex);
#else
		down(pmutex);
#endif
}


__inline static void _exit_critical_mutex(_mutex *pmutex, _irqL *pirqL)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
		mutex_unlock(pmutex);
#else
		up(pmutex);
#endif
}

__inline static void rtw_list_delete(_list *plist)
{
	list_del_init(plist);
}

__inline static void _init_timer(_timer *ptimer,_nic_hdl nic_hdl,void *pfunc,void* cntx)
{
	//setup_timer(ptimer, pfunc,(u32)cntx);	
	ptimer->function = pfunc;
	ptimer->data = (unsigned long)cntx;
	init_timer(ptimer);
}

__inline static void _set_timer(_timer *ptimer,u32 delay_time)
{	
	mod_timer(ptimer , (jiffies+(delay_time*HZ/1000)));	
}

__inline static void _cancel_timer(_timer *ptimer,u8 *bcancelled)
{
	del_timer_sync(ptimer); 	
	*bcancelled=  _TRUE;//TRUE ==1; FALSE==0
}

#ifdef PLATFORM_LINUX
#define RTW_TIMER_HDL_ARGS void *FunctionContext
#elif defined(PLATFORM_OS_CE) || defined(PLATFORM_WINDOWS)
#define RTW_TIMER_HDL_ARGS IN PVOID SystemSpecific1, IN PVOID FunctionContext, IN PVOID SystemSpecific2, IN PVOID SystemSpecific3
#endif

#define RTW_TIMER_HDL_NAME(name) rtw_##name##_timer_hdl
#define RTW_DECLARE_TIMER_HDL(name) void RTW_TIMER_HDL_NAME(name)(RTW_TIMER_HDL_ARGS)


__inline static void _init_workitem(_workitem *pwork, void *pfunc, PVOID cntx)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
	INIT_WORK(pwork, pfunc);
#else
	INIT_WORK(pwork, pfunc,pwork);
#endif
}

__inline static void _set_workitem(_workitem *pwork)
{
	schedule_work(pwork);
}

//
// Global Mutex: can only be used at PASSIVE level.
//

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

#endif	// PLATFORM_LINUX


#ifdef PLATFORM_OS_XP

	#include <ndis.h>
	#include <ntddk.h>
	#include <ntddndis.h>
	#include <ntdef.h>

#ifdef CONFIG_USB_HCI
	#include <usb.h>
	#include <usbioctl.h>
	#include <usbdlib.h>
#endif

	typedef KSEMAPHORE 	_sema;
	typedef	LIST_ENTRY	_list;
	typedef NDIS_STATUS _OS_STATUS;
	

	typedef NDIS_SPIN_LOCK	_lock;

	typedef KMUTEX 			_mutex;

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

	#define HZ			10000000
	#define SEMA_UPBND	(0x7FFFFFFF)   //8192
	
__inline static _list *get_next(_list	*list)
{
	return list->Flink;
}	

__inline static _list	*get_list_head(_queue	*queue)
{
	return (&(queue->queue));
}
	

#define LIST_CONTAINOR(ptr, type, member) CONTAINING_RECORD(ptr, type, member)
     

__inline static _enter_critical(_lock *plock, _irqL *pirqL)
{
	NdisAcquireSpinLock(plock);	
}

__inline static _exit_critical(_lock *plock, _irqL *pirqL)
{
	NdisReleaseSpinLock(plock);	
}


__inline static _enter_critical_ex(_lock *plock, _irqL *pirqL)
{
	NdisDprAcquireSpinLock(plock);	
}

__inline static _exit_critical_ex(_lock *plock, _irqL *pirqL)
{
	NdisDprReleaseSpinLock(plock);	
}

__inline static void _enter_critical_bh(_lock *plock, _irqL *pirqL)
{
	NdisDprAcquireSpinLock(plock);
}

__inline static void _exit_critical_bh(_lock *plock, _irqL *pirqL)
{
	NdisDprReleaseSpinLock(plock);
}

__inline static _enter_critical_mutex(_mutex *pmutex, _irqL *pirqL)
{
	KeWaitForSingleObject(pmutex, Executive, KernelMode, FALSE, NULL);
}


__inline static _exit_critical_mutex(_mutex *pmutex, _irqL *pirqL)
{
	KeReleaseMutex(pmutex, FALSE);
}


__inline static void rtw_list_delete(_list *plist)
{
	RemoveEntryList(plist);
	InitializeListHead(plist);	
}

__inline static void _init_timer(_timer *ptimer,_nic_hdl nic_hdl,void *pfunc,PVOID cntx)
{
	NdisMInitializeTimer(ptimer, nic_hdl, pfunc, cntx);
}

__inline static void _set_timer(_timer *ptimer,u32 delay_time)
{	
 	NdisMSetTimer(ptimer,delay_time);	
}

__inline static void _cancel_timer(_timer *ptimer,u8 *bcancelled)
{
	NdisMCancelTimer(ptimer,bcancelled);
}

__inline static void _init_workitem(_workitem *pwork, void *pfunc, PVOID cntx)
{

	NdisInitializeWorkItem(pwork, pfunc, cntx);
}

__inline static void _set_workitem(_workitem *pwork)
{
	NdisScheduleWorkItem(pwork);
}


#define ATOMIC_INIT(i)  { (i) }

//
// Global Mutex: can only be used at PASSIVE level.
//

#define ACQUIRE_GLOBAL_MUTEX(_MutexCounter)                              \
{                                                               \
    while (NdisInterlockedIncrement((PULONG)&(_MutexCounter)) != 1)\
    {                                                           \
        NdisInterlockedDecrement((PULONG)&(_MutexCounter));        \
        NdisMSleep(10000);                          \
    }                                                           \
}

#define RELEASE_GLOBAL_MUTEX(_MutexCounter)                              \
{                                                               \
    NdisInterlockedDecrement((PULONG)&(_MutexCounter));              \
}

#endif // PLATFORM_OS_XP


#ifdef PLATFORM_OS_CE
#include <osdep_ce_service.h>
#endif

#include <rtw_byteorder.h>

#ifndef BIT
	#define BIT(x)	( 1 << (x))
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

#define CONFIG_USE_VMALLOC

//flags used for rtw_update_mem_stat()
enum {
	MEM_STAT_VIR_ALLOC_SUCCESS,
	MEM_STAT_VIR_ALLOC_FAIL,
	MEM_STAT_VIR_FREE,
	MEM_STAT_PHY_ALLOC_SUCCESS,
	MEM_STAT_PHY_ALLOC_FAIL,
	MEM_STAT_PHY_FREE,
	MEM_STAT_TX, //used to distinguish TX/RX, asigned from caller
	MEM_STAT_TX_ALLOC_SUCCESS,
	MEM_STAT_TX_ALLOC_FAIL,
	MEM_STAT_TX_FREE,
	MEM_STAT_RX, //used to distinguish TX/RX, asigned from caller
	MEM_STAT_RX_ALLOC_SUCCESS,
	MEM_STAT_RX_ALLOC_FAIL,
	MEM_STAT_RX_FREE
};

#ifdef DBG_MEM_ALLOC
void rtw_update_mem_stat(u8 flag, u32 sz);
void rtw_dump_mem_stat (void);
extern u8* dbg_rtw_vmalloc(u32 sz, const char *func, int line);
extern u8* dbg_rtw_zvmalloc(u32 sz, const char *func, int line);
extern void dbg_rtw_vmfree(u8 *pbuf, u32 sz, const char *func, int line);
extern u8* dbg_rtw_malloc(u32 sz, const char *func, int line);
extern u8* dbg_rtw_zmalloc(u32 sz, const char *func, int line);
extern void dbg_rtw_mfree(u8 *pbuf, u32 sz, const char *func, int line);
#ifdef CONFIG_USE_VMALLOC
#define rtw_vmalloc(sz)			dbg_rtw_vmalloc((sz), __FUNCTION__, __LINE__)
#define rtw_zvmalloc(sz)			dbg_rtw_zvmalloc((sz), __FUNCTION__, __LINE__)
#define rtw_vmfree(pbuf, sz)		dbg_rtw_vmfree((pbuf), (sz), __FUNCTION__, __LINE__)
#else //CONFIG_USE_VMALLOC
#define rtw_vmalloc(sz)			dbg_rtw_malloc((sz), __FUNCTION__, __LINE__)
#define rtw_zvmalloc(sz)			dbg_rtw_zmalloc((sz), __FUNCTION__, __LINE__)
#define rtw_vmfree(pbuf, sz)		dbg_rtw_mfree((pbuf), (sz), __FUNCTION__, __LINE__)
#endif //CONFIG_USE_VMALLOC
#define rtw_malloc(sz)			dbg_rtw_malloc((sz), __FUNCTION__, __LINE__)
#define rtw_zmalloc(sz)			dbg_rtw_zmalloc((sz), __FUNCTION__, __LINE__)
#define rtw_mfree(pbuf, sz)		dbg_rtw_mfree((pbuf), (sz), __FUNCTION__, __LINE__)
#else
#define rtw_update_mem_stat(flag, sz) do {} while(0)
extern u8*	_rtw_vmalloc(u32 sz);
extern u8*	_rtw_zvmalloc(u32 sz);
extern void	_rtw_vmfree(u8 *pbuf, u32 sz);
extern u8*	_rtw_zmalloc(u32 sz);
extern u8*	_rtw_malloc(u32 sz);
extern void	_rtw_mfree(u8 *pbuf, u32 sz);
#ifdef CONFIG_USE_VMALLOC
#define rtw_vmalloc(sz)			_rtw_vmalloc((sz))
#define rtw_zvmalloc(sz)			_rtw_zvmalloc((sz))
#define rtw_vmfree(pbuf, sz)		_rtw_vmfree((pbuf), (sz))
#else //CONFIG_USE_VMALLOC
#define rtw_vmalloc(sz)			_rtw_malloc((sz))
#define rtw_zvmalloc(sz)			_rtw_zmalloc((sz))
#define rtw_vmfree(pbuf, sz)		_rtw_mfree((pbuf), (sz))
#endif //CONFIG_USE_VMALLOC
#define rtw_malloc(sz)			_rtw_malloc((sz))
#define rtw_zmalloc(sz)			_rtw_zmalloc((sz))
#define rtw_mfree(pbuf, sz)		_rtw_mfree((pbuf), (sz))
#endif

extern void*	rtw_malloc2d(int h, int w, int size);
extern void	rtw_mfree2d(void *pbuf, int h, int w, int size);

extern void	_rtw_memcpy(void* dec, void* sour, u32 sz);
extern int	_rtw_memcmp(void *dst, void *src, u32 sz);
extern void	_rtw_memset(void *pbuf, int c, u32 sz);

extern void	_rtw_init_listhead(_list *list);
extern u32	rtw_is_list_empty(_list *phead);
extern void	rtw_list_insert_head(_list *plist, _list *phead);
extern void	rtw_list_insert_tail(_list *plist, _list *phead);
#ifndef PLATFORM_FREEBSD
extern void	rtw_list_delete(_list *plist);
#endif //PLATFORM_FREEBSD

extern void	_rtw_init_sema(_sema *sema, int init_val);
extern void	_rtw_free_sema(_sema	*sema);
extern void	_rtw_up_sema(_sema	*sema);
extern u32	_rtw_down_sema(_sema *sema);
extern void	_rtw_mutex_init(_mutex *pmutex);
extern void	_rtw_mutex_free(_mutex *pmutex);
#ifndef PLATFORM_FREEBSD
extern void	_rtw_spinlock_init(_lock *plock);
#endif //PLATFORM_FREEBSD
extern void	_rtw_spinlock_free(_lock *plock);
extern void	_rtw_spinlock(_lock	*plock);
extern void	_rtw_spinunlock(_lock	*plock);
extern void	_rtw_spinlock_ex(_lock	*plock);
extern void	_rtw_spinunlock_ex(_lock	*plock);

extern void	_rtw_init_queue(_queue	*pqueue);
extern u32	_rtw_queue_empty(_queue	*pqueue);
extern u32	rtw_end_of_queue_search(_list *queue, _list *pelement);

extern u32	rtw_get_current_time(void);
extern u32	rtw_systime_to_ms(u32 systime);
extern s32	rtw_get_passing_time_ms(u32 start);
extern s32	rtw_get_time_interval_ms(u32 start, u32 end);

extern void	rtw_sleep_schedulable(int ms);

extern void	rtw_msleep_os(int ms);
extern void	rtw_usleep_os(int us);

#ifdef DBG_DELAY_OS
#define rtw_mdelay_os(ms) _rtw_mdelay_os((ms), __FUNCTION__, __LINE__)
#define rtw_udelay_os(ms) _rtw_udelay_os((ms), __FUNCTION__, __LINE__)
extern void _rtw_mdelay_os(int ms, const char *func, const int line);
extern void _rtw_udelay_os(int us, const char *func, const int line);
#else
extern void	rtw_mdelay_os(int ms);
extern void	rtw_udelay_os(int us);
#endif

extern void rtw_yield_os(void);


__inline static unsigned char _cancel_timer_ex(_timer *ptimer)
{
#ifdef PLATFORM_LINUX
	return del_timer_sync(ptimer);
#endif
#ifdef PLATFORM_FREEBSD
	_cancel_timer(ptimer,0);
	return 0;
#endif
#ifdef PLATFORM_WINDOWS
	u8 bcancelled;
	
	_cancel_timer(ptimer, &bcancelled);
	
	return bcancelled;
#endif
}
#ifdef PLATFORM_FREEBSD
static __inline void thread_enter(void *context);
#endif //PLATFORM_FREEBSD
static __inline void thread_enter(void *context)
{
#ifdef PLATFORM_LINUX
	//struct net_device *pnetdev = (struct net_device *)context;
	//daemonize("%s", pnetdev->name);
	daemonize("%s", "RTKTHREAD");
	allow_signal(SIGTERM);
#endif
#ifdef PLATFORM_FREEBSD
	printf("%s", "RTKTHREAD_enter");
#endif
}
#ifdef PLATFORM_FREEBSD
#define thread_exit() do{printf("%s", "RTKTHREAD_exit");}while(0)
#endif //PLATFORM_FREEBSD
__inline static void flush_signals_thread(void) 
{
#ifdef PLATFORM_LINUX
	if (signal_pending (current)) 
	{
		flush_signals(current);
	}
#endif
}

__inline static _OS_STATUS res_to_status(sint res)
{


#if defined (PLATFORM_LINUX) || defined (PLATFORM_MPIXEL) || defined (PLATFORM_FREEBSD)
	return res;
#endif

#ifdef PLATFORM_WINDOWS

	if (res == _SUCCESS)
		return NDIS_STATUS_SUCCESS;
	else
		return NDIS_STATUS_FAILURE;

#endif	
	
}

#define _RND(sz, r) ((((sz)+((r)-1))/(r))*(r))
#define RND4(x)	(((x >> 2) + (((x & 3) == 0) ?  0: 1)) << 2)

__inline static u32 _RND4(u32 sz)
{

	u32	val;

	val = ((sz >> 2) + ((sz & 3) ? 1: 0)) << 2;
	
	return val;

}

__inline static u32 _RND8(u32 sz)
{

	u32	val;

	val = ((sz >> 3) + ((sz & 7) ? 1: 0)) << 3;
	
	return val;

}

__inline static u32 _RND128(u32 sz)
{

	u32	val;

	val = ((sz >> 7) + ((sz & 127) ? 1: 0)) << 7;
	
	return val;

}

__inline static u32 _RND256(u32 sz)
{

	u32	val;

	val = ((sz >> 8) + ((sz & 255) ? 1: 0)) << 8;
	
	return val;

}

__inline static u32 _RND512(u32 sz)
{

	u32	val;

	val = ((sz >> 9) + ((sz & 511) ? 1: 0)) << 9;
	
	return val;

}

__inline static u32 bitshift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++)
		if (((bitmask>>i) &  0x1) == 1) break;

	return i;
}

#ifndef MAC_FMT
#define MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#endif
#ifndef MAC_ARG
#define MAC_ARG(x) ((u8*)(x))[0],((u8*)(x))[1],((u8*)(x))[2],((u8*)(x))[3],((u8*)(x))[4],((u8*)(x))[5]
#endif

//#ifdef __GNUC__
#ifdef PLATFORM_LINUX
#define STRUCT_PACKED __attribute__ ((packed))
#else
#define STRUCT_PACKED
#endif


// limitation of path length
#ifdef PLATFORM_LINUX
	#define PATH_LENGTH_MAX PATH_MAX
#elif defined(PLATFORM_WINDOWS)
	#define PATH_LENGTH_MAX MAX_PATH
#endif


// Suspend lock prevent system from going suspend
#ifdef CONFIG_WAKELOCK
#include <linux/wakelock.h>
#elif defined(CONFIG_ANDROID_POWER)
#include <linux/android_power.h>
#endif

extern void rtw_suspend_lock_init(void);
extern void rtw_suspend_lock_uninit(void);
extern void rtw_lock_suspend(void);
extern void rtw_unlock_suspend(void);


//Atomic integer operations
#ifdef PLATFORM_LINUX
	#define ATOMIC_T atomic_t
#elif defined(PLATFORM_WINDOWS)
	#define ATOMIC_T LONG
#elif defined(PLATFORM_FREEBSD)
	typedef uint32_t ATOMIC_T ;
#endif

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

//File operation APIs, just for linux now
extern int rtw_is_file_readable(char *path);
extern int rtw_retrive_from_file(char *path, u8* buf, u32 sz);
extern int rtw_store_to_file(char *path, u8* buf, u32 sz);


#if 1 //#ifdef MEM_ALLOC_REFINE_ADAPTOR
struct rtw_netdev_priv_indicator {
	void *priv;
	u32 sizeof_priv;
};
struct net_device *rtw_alloc_etherdev_with_old_priv(int sizeof_priv, void *old_priv);
extern struct net_device * rtw_alloc_etherdev(int sizeof_priv);

#ifndef PLATFORM_FREEBSD
#define rtw_netdev_priv(netdev) ( ((struct rtw_netdev_priv_indicator *)netdev_priv(netdev))->priv )
#else //PLATFORM_FREEBSD
#define rtw_netdev_priv(netdev) (((struct ifnet *)netdev)->if_softc)
#endif //PLATFORM_FREEBSD

#ifndef PLATFORM_FREEBSD
extern void rtw_free_netdev(struct net_device * netdev);
#else //PLATFORM_FREEBSD
#define rtw_free_netdev(netdev) if_free((netdev))
#endif //PLATFORM_FREEBSD

#else //MEM_ALLOC_REFINE_ADAPTOR

#define rtw_alloc_etherdev(sizeof_priv) alloc_etherdev((sizeof_priv))

#ifndef PLATFORM_FREEBSD
#define rtw_netdev_priv(netdev) netdev_priv((netdev))
#define rtw_free_netdev(netdev) free_netdev((netdev))
#else //PLATFORM_FREEBSD
#define rtw_netdev_priv(netdev) (((struct ifnet *)netdev)->if_softc)
#define rtw_free_netdev(netdev) if_free((netdev))
#endif //PLATFORM_FREEBSD
#endif

#ifdef PLATFORM_LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
#define rtw_signal_process(pid, sig) kill_pid(find_vpid((pid)),(sig), 1)
#else //(LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
#define rtw_signal_process(pid, sig) kill_proc((pid), (sig), 1)
#endif //(LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
#endif //PLATFORM_LINUX

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

#endif


