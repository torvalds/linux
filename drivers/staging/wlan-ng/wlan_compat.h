/* wlan_compat.h
*
* Types and macros to aid in portability
*
* Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
* --------------------------------------------------------------------
*
* linux-wlan
*
*   The contents of this file are subject to the Mozilla Public
*   License Version 1.1 (the "License"); you may not use this file
*   except in compliance with the License. You may obtain a copy of
*   the License at http://www.mozilla.org/MPL/
*
*   Software distributed under the License is distributed on an "AS
*   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
*   implied. See the License for the specific language governing
*   rights and limitations under the License.
*
*   Alternatively, the contents of this file may be used under the
*   terms of the GNU Public License version 2 (the "GPL"), in which
*   case the provisions of the GPL are applicable instead of the
*   above.  If you wish to allow the use of your version of this file
*   only under the terms of the GPL and not to allow others to use
*   your version of this file under the MPL, indicate your decision
*   by deleting the provisions above and replace them with the notice
*   and other provisions required by the GPL.  If you do not delete
*   the provisions above, a recipient may use your version of this
*   file under either the MPL or the GPL.
*
* --------------------------------------------------------------------
*
* Inquiries regarding the linux-wlan Open Source project can be
* made directly to:
*
* AbsoluteValue Systems Inc.
* info@linux-wlan.com
* http://www.linux-wlan.com
*
* --------------------------------------------------------------------
*
* Portions of the development of this software were funded by
* Intersil Corporation as part of PRISM(R) chipset product development.
*
* --------------------------------------------------------------------
*/

#ifndef _WLAN_COMPAT_H
#define _WLAN_COMPAT_H

/*=============================================================*/
/*------ Establish Platform Identity --------------------------*/
/*=============================================================*/
/* Key macros: */
/* WLAN_CPU_FAMILY */
	#define WLAN_Ix86			1
	#define WLAN_PPC			2
	#define WLAN_Ix96			3
	#define WLAN_ARM			4
	#define WLAN_ALPHA			5
	#define WLAN_MIPS			6
	#define WLAN_HPPA			7
	#define WLAN_SPARC			8
	#define WLAN_SH    			9
	#define WLAN_x86_64                     10
/* WLAN_SYSARCH */
	#define WLAN_PCAT			1
	#define WLAN_MBX			2
	#define WLAN_RPX			3
	#define WLAN_LWARCH			4
	#define WLAN_PMAC			5
	#define WLAN_SKIFF			6
	#define WLAN_BITSY			7
	#define WLAN_ALPHAARCH			7
	#define WLAN_MIPSARCH			9
	#define WLAN_HPPAARCH			10
	#define WLAN_SPARCARCH			11
	#define WLAN_SHARCH   			12

/* Note: the PLX HOSTIF above refers to some vendors implementations for */
/*       PCI.  It's a PLX chip that is a PCI to PCMCIA adapter, but it   */
/*       isn't a real PCMCIA host interface adapter providing all the    */
/*       card&socket services.                                           */

#if (defined(CONFIG_PPC) || defined(CONFIG_8xx) || defined(__powerpc__))
#ifndef __ppc__
#define __ppc__
#endif
#endif

#if defined(__KERNEL__)

#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif

#if defined(__x86_64__)
	#define WLAN_CPU_FAMILY		WLAN_x86_64
	#define WLAN_SYSARCH		WLAN_PCAT
#elif defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
	#define WLAN_CPU_FAMILY		WLAN_Ix86
	#define WLAN_SYSARCH		WLAN_PCAT
#elif defined(__ppc__)
	#define WLAN_CPU_FAMILY		WLAN_PPC
	#if defined(CONFIG_MBX)
		#define WLAN_SYSARCH	WLAN_MBX
	#elif defined(CONFIG_RPXLITE)
		#define WLAN_SYSARCH	WLAN_RPX
	#elif defined(CONFIG_RPXCLASSIC)
		#define WLAN_SYSARCH	WLAN_RPX
	#else
		#define WLAN_SYSARCH	WLAN_PMAC
	#endif
#elif defined(__arm__)
	#define WLAN_CPU_FAMILY		WLAN_ARM
	#define WLAN_SYSARCH		WLAN_SKIFF
#elif defined(__alpha__)
	#define WLAN_CPU_FAMILY		WLAN_ALPHA
	#define WLAN_SYSARCH		WLAN_ALPHAARCH
#elif defined(__mips__)
	#define WLAN_CPU_FAMILY		WLAN_MIPS
	#define WLAN_SYSARCH		WLAN_MIPSARCH
#elif defined(__hppa__)
	#define WLAN_CPU_FAMILY		WLAN_HPPA
	#define WLAN_SYSARCH		WLAN_HPPAARCH
#elif defined(__sparc__)
        #define WLAN_CPU_FAMILY         WLAN_SPARC
        #define WLAN_SYSARCH            WLAN_SPARC
#elif defined(__sh__)
        #define WLAN_CPU_FAMILY         WLAN_SH
        #define WLAN_SYSARCH            WLAN_SHARCH
        #ifndef __LITTLE_ENDIAN__
        #define __LITTLE_ENDIAN__
        #endif
#else
	#error "No CPU identified!"
#endif
#endif /* __KERNEL__ */

/*
   Some big endian machines implicitly do all I/O in little endian mode.

   In particular:
          Linux/PPC on PowerMacs (PCI)
	  Arm/Intel Xscale (PCI)

   This may also affect PLX boards and other BE &| PPC platforms;
   as new ones are discovered, add them below.
*/

#if defined(WLAN_HOSTIF)
#if ((WLAN_HOSTIF == WLAN_PCI) || (WLAN_HOSTIF == WLAN_PLX))
#if ((WLAN_SYSARCH == WLAN_SKIFF) || (WLAN_SYSARCH == WLAN_PMAC) || (WLAN_SYSARCH == WLAN_SPARC))
#define REVERSE_ENDIAN
#endif
#endif
#endif

/*=============================================================*/
/*------ Bit settings -----------------------------------------*/
/*=============================================================*/

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

#include <linux/types.h>

typedef u_int8_t	UINT8;
typedef u_int16_t	UINT16;
typedef u_int32_t	UINT32;

typedef int8_t		INT8;
typedef int16_t		INT16;
typedef int32_t		INT32;

typedef unsigned int    UINT;
typedef signed int      INT;

typedef u_int64_t	UINT64;
typedef int64_t		INT64;

#define UINT8_MAX	(0xffUL)
#define UINT16_MAX	(0xffffUL)
#define UINT32_MAX	(0xffffffffUL)

#define INT8_MAX	(0x7fL)
#define INT16_MAX	(0x7fffL)
#define INT32_MAX	(0x7fffffffL)

/*=============================================================*/
/*------ Compiler Portability Macros --------------------------*/
/*=============================================================*/
#define __WLAN_ATTRIB_PACK__		__attribute__ ((packed))

/*=============================================================*/
/*------ OS Portability Macros --------------------------------*/
/*=============================================================*/

#ifndef WLAN_DBVAR
#define WLAN_DBVAR	wlan_debug
#endif

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
#  if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,8))
#    include <linux/hardirq.h>
#  else
#    include <asm/hardirq.h>
#  endif
#elif defined(__KERNEL__)
#  define PREEMPT_MASK  (0x000000FFUL)
#  define preempt_count() (0UL)
#endif

#define WLAN_LOG_ERROR(x,args...) printk(KERN_ERR "%s: " x , __FUNCTION__ , ##args);

#define WLAN_LOG_WARNING(x,args...) printk(KERN_WARNING "%s: " x , __FUNCTION__ , ##args);

#define WLAN_LOG_NOTICE(x,args...) printk(KERN_NOTICE "%s: " x , __FUNCTION__ , ##args);

#define WLAN_LOG_INFO(args... ) printk(KERN_INFO args)

#if defined(WLAN_INCLUDE_DEBUG)
	#define WLAN_ASSERT(c) if ((!(c)) && WLAN_DBVAR >= 1) { \
		WLAN_LOG_DEBUG(1, "Assertion failure!\n"); }
	#define WLAN_HEX_DUMP( l, x, p, n)	if( WLAN_DBVAR >= (l) ){ \
		int __i__; \
		printk(KERN_DEBUG x ":"); \
		for( __i__=0; __i__ < (n); __i__++) \
			printk( " %02x", ((UINT8*)(p))[__i__]); \
		printk("\n"); }
	#define DBFENTER { if ( WLAN_DBVAR >= 5 ){ WLAN_LOG_DEBUG(3,"---->\n"); } }
	#define DBFEXIT  { if ( WLAN_DBVAR >= 5 ){ WLAN_LOG_DEBUG(3,"<----\n"); } }

	#define WLAN_LOG_DEBUG(l,x,args...) if ( WLAN_DBVAR >= (l)) printk(KERN_DEBUG "%s(%lu): " x ,  __FUNCTION__, (preempt_count() & PREEMPT_MASK), ##args );
#else
	#define WLAN_ASSERT(c)
	#define WLAN_HEX_DUMP( l, s, p, n)
	#define DBFENTER
	#define DBFEXIT

	#define WLAN_LOG_DEBUG(l, s, args...)
#endif

#ifdef CONFIG_SMP
#define __SMP__			1
#endif

#if defined(__KERNEL__)

#if ((LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)) || (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)))
#define URB_ONLY_CALLBACK
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19))
#define PT_REGS    , struct pt_regs *regs
#else
#define PT_REGS
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,7))
#  define del_singleshot_timer_sync(a)  del_timer_sync(a)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,17))
#define CONFIG_NETLINK		1
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0))
#define kfree_s(a, b)	kfree((a))
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,2,18))
#ifndef init_waitqueue_head
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,0,16))
#define init_waitqueue_head(p)  (*(p) = NULL)
#else
#define init_waitqueue_head(p)  init_waitqueue(p)
#endif
typedef struct wait_queue *wait_queue_head_t;
typedef struct wait_queue wait_queue_t;
#define set_current_state(b)  { current->state = (b); mb(); }
#define init_waitqueue_entry(a, b) { (a)->task = current; }
#endif
#endif

#ifndef wait_event_interruptible_timeout
// retval == 0; signal met; we're good.
// retval < 0; interrupted by signal.
// retval > 0; timed out.

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0))  // fixme?

#define __wait_event_interruptible_timeout(wq, condition, ret)            \
do {                                                                      \
          wait_queue_t __wait;                                            \
          init_waitqueue_entry(&__wait, current);                         \
	                                                                  \
          add_wait_queue(&wq, &__wait);                                   \
          for (;;) {                                                      \
                  set_current_state(TASK_INTERRUPTIBLE);                  \
                  if (condition)                                          \
                          break;                                          \
                  if (!signal_pending(current)) {                         \
                          ret = schedule_timeout(ret)    ;                \
                          if (!ret)                                       \
                                 break;                                   \
                          continue;                                       \
                  }                                                       \
                  ret = -ERESTARTSYS;                                     \
                  break;                                                  \
          }                                                               \
          set_current_state(TASK_RUNNING);                                \
          remove_wait_queue(&wq, &__wait);                                \
} while (0)

#else // 2.2


#define __wait_event_interruptible_timeout(wq, condition, ret)          \
do {                                                                    \
        struct wait_queue __wait;                                       \
                                                                        \
        __wait.task = current;                                          \
        add_wait_queue(&wq, &__wait);                                   \
        for (;;) {                                                      \
                current->state = TASK_INTERRUPTIBLE;                    \
                if (condition)                                          \
                        break;                                          \
                if (!signal_pending(current)) {                         \
                        ret = schedule_timeout(ret);                    \
                        if (!ret)                                       \
                               break;                                   \
                        continue;                                       \
                }                                                       \
                ret = -ERESTARTSYS;                                     \
                break;                                                  \
        }                                                               \
        current->state = TASK_RUNNING;                                  \
        remove_wait_queue(&wq, &__wait);                                \
} while (0)

#endif  // version >= 2.4

#define wait_event_interruptible_timeout(wq, condition, timeout)	  \
({									  \
	long __ret = timeout;						  \
	if (!(condition))						  \
		__wait_event_interruptible_timeout(wq, condition, __ret); \
	__ret;								  \
})

#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,20))
#ifdef _LINUX_LIST_H

static inline void list_move_tail(struct list_head *list,
          struct list_head *head)
{
        __list_del(list->prev, list->next);
        list_add_tail(list, head);
}

static inline void __list_splice(struct list_head *list,
				 struct list_head *head)
{
      struct list_head *first = list->next;
      struct list_head *last = list->prev;
      struct list_head *at = head->next;

      first->prev = head;
      head->next = first;

      last->next = at;
      at->prev = last;
}

static inline void list_move(struct list_head *list, struct list_head *head)
{
      __list_del(list->prev, list->next);
      list_add(list, head);
}

static inline void list_splice_init(struct list_head *list,
            struct list_head *head)
{
	if (!list_empty(list)) {
		__list_splice(list, head);
		INIT_LIST_HEAD(list);
	}
}


#endif  // LIST_H
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,1,90))
#define spin_lock(l)            do { } while (0)
#define spin_unlock(l)          do { } while (0)
#define spin_lock_irqsave(l,f)  do { save_flags(f); cli(); } while (0)
#define spin_unlock_irqrestore(l,f) do { restore_flags(f); } while (0)
#define spin_lock_init(s)       do { } while (0)
#define spin_trylock(l)         (1)
typedef int spinlock_t;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)) // XXX ???
#define spin_lock_bh         spin_lock
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0))
#ifdef CONFIG_SMP
#define spin_is_locked(x)       (*(volatile char *)(&(x)->lock) <= 0)
#else
#define spin_is_locked(l)       (0)
#endif
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,28))
#define __user
#define __iomem
#endif

#ifdef _LINUX_PROC_FS_H
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,25))

extern inline struct proc_dir_entry *
create_proc_read_entry(const char *name, mode_t mode,
                       struct proc_dir_entry *base,
                       read_proc_t *read_proc, void *data)
{
    struct proc_dir_entry *res = create_proc_entry(name, mode, base);
    if (res) {
        res->read_proc = read_proc;
        res->data = data;
    }
    return res;
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,29))
#ifndef proc_mkdir
#define proc_mkdir(name, root) create_proc_entry(name, S_IFDIR, root)
#endif
#endif
#endif /* _LINUX_PROC_FS_H */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
#ifndef INIT_TQUEUE
#define PREPARE_TQUEUE(_tq, _routine, _data)                    \
        do {                                                    \
                (_tq)->routine = _routine;                      \
                (_tq)->data = _data;                            \
        } while (0)
#define INIT_TQUEUE(_tq, _routine, _data)                       \
        do {                                                    \
                INIT_LIST_HEAD(&(_tq)->list);                   \
                (_tq)->sync = 0;                                \
                PREPARE_TQUEUE((_tq), (_routine), (_data));     \
        } while (0)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({			\
        const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
        (type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#ifndef INIT_WORK
#define work_struct tq_struct

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0))
#define schedule_work(a)   queue_task(a, &tq_scheduler)
#else
#define schedule_work(a)  schedule_task(a)
#endif

#define flush_scheduled_work  flush_scheduled_tasks
#define INIT_WORK2(_wq, _routine)  INIT_TQUEUE(_wq, (void (*)(void *))_routine, _wq)
#endif

#else // >= 2.5 kernel

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
#define INIT_WORK2(_wq, _routine)	INIT_WORK(_wq, (void (*)(void *))_routine, _wq)
#else
#define INIT_WORK2(_wq, _routine)	INIT_WORK(_wq, _routine)
#endif

#endif // >= 2.5 kernel

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,38))
typedef struct device netdevice_t;
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,4))
typedef struct net_device netdevice_t;
#else
#undef netdevice_t
typedef struct net_device netdevice_t;
#endif

#ifdef WIRELESS_EXT
#if (WIRELESS_EXT < 13)
struct iw_request_info
{
        __u16           cmd;            /* Wireless Extension command */
        __u16           flags;          /* More to come ;-) */
};
#endif
#endif


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,1,18))
#define MODULE_PARM(a,b)        extern int __bogus_decl
#define MODULE_AUTHOR(a)        extern int __bogus_decl
#define MODULE_DESCRIPTION(a)   extern int __bogus_decl
#define MODULE_SUPPORTED_DEVICE(a) extern int __bogus_decl
#undef  GET_USE_COUNT
#define GET_USE_COUNT(m)        mod_use_count_
#endif

#ifndef MODULE_OWNER
#define MODULE_OWNER(a)         extern int __bogus_decl
#define ANCIENT_MODULE_CODE
#endif

#ifndef MODULE_LICENSE
#define MODULE_LICENSE(m)       extern int __bogus_decl
#endif

/* TODO:  Do we care about this? */
#ifndef MODULE_DEVICE_TABLE
#define MODULE_DEVICE_TABLE(foo,bar)
#endif

#define wlan_minutes2ticks(a) ((a)*(wlan_ticks_per_sec *  60))
#define wlan_seconds2ticks(a) ((a)*(wlan_ticks_per_sec))

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,47))
#define NEW_MODULE_CODE
#ifdef ANCIENT_MODULE_CODE
#undef ANCIENT_MODULE_CODE
#endif
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,25))
#define module_param(name, type, perm)                                       \
        static inline void *__check_existence_##name(void) { return &name; } \
        MODULE_PARM(name, _MODULE_PARM_STRING_ ## type)

#define _MODULE_PARM_STRING_byte "b"
#define _MODULE_PARM_STRING_short "h"
#define _MODULE_PARM_STRING_ushort "h"
#define _MODULE_PARM_STRING_int "i"
#define _MODULE_PARM_STRING_uint "i"
#define _MODULE_PARM_STRING_long "l"
#define _MODULE_PARM_STRING_ulong "l"
#define _MODULE_PARM_STRING_bool "i"
#endif

/* linux < 2.5.69 */
#ifndef IRQ_NONE
typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)
#endif

#ifndef in_atomic
#define in_atomic()  0
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,13))
#define URB_ASYNC_UNLINK 0
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,7))
#define URB_ASYNC_UNLINK  USB_ASYNC_UNLINK
#define usb_fill_bulk_urb  FILL_BULK_URB
#define usb_kill_urb  usb_unlink_urb
#else
#define USB_QUEUE_BULK 0
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11))
typedef u32 pm_message_t;
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,9))
#define hotplug_path  "/etc/hotplug/wlan.agent"
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
#define free_netdev(x)       kfree(x)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9))
#define eth_hdr(x)           (x)->mac.ethernet
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0))
#define del_timer_sync(a)       del_timer(a)
#endif

#ifndef might_sleep
#define might_sleep(a)   do { } while (0)
#endif

/* Apparently 2.4.2 ethtool is quite different, maybe newer too? */
#if (defined(SIOETHTOOL) && !defined(ETHTOOL_GDRVINFO))
#undef SIOETHTOOL
#endif

// pcmcia-cs stuff
#if ((LINUX_VERSION_CODE < KERNEL_VERSION(2,5,68)) && \
     !defined(pcmcia_access_configuration_register))
#define pcmcia_access_configuration_register(handle, reg) \
        CardServices(AccessConfigurationRegister, handle, reg)
#define pcmcia_register_client(handle, reg) \
        CardServices(RegisterClient, handle, reg)
#define pcmcia_deregister_client(handle) \
        CardServices(DeregisterClient, handle)
#define pcmcia_get_first_tuple(handle, tuple) \
        CardServices(GetFirstTuple, handle, tuple)
#define pcmcia_get_next_tuple(handle, tuple) \
        CardServices(GetNextTuple, handle, tuple)
#define pcmcia_get_tuple_data(handle, tuple) \
        CardServices(GetTupleData, handle, tuple)
#define pcmcia_parse_tuple(handle, tuple, parse) \
        CardServices(ParseTuple, handle, tuple, parse)
#define pcmcia_get_configuration_info(handle, config) \
        CardServices(GetConfigurationInfo, handle, config)
#define pcmcia_request_io(handle, req) \
        CardServices(RequestIO, handle, req)
#define pcmcia_request_irq(handle, req) \
        CardServices(RequestIRQ, handle, req)
#define pcmcia_request_configuration(handle, req) \
        CardServices(RequestConfiguration, handle, req)
#define pcmcia_release_configuration(handle) \
        CardServices(ReleaseConfiguration, handle)
#define pcmcia_release_io(handle, req) \
        CardServices(ReleaseIO, handle, req)
#define pcmcia_release_irq(handle, req) \
        CardServices(ReleaseIRQ, handle, req)
#define pcmcia_release_window(win) \
        CardServices(ReleaseWindow, win)
#define pcmcia_get_card_services_info(info) \
        CardServices(GetCardServicesInfo, info)
#define pcmcia_report_error(handle, err) \
        CardServices(ReportError, handle, err)
#endif

#endif /* __KERNEL__ */

/*=============================================================*/
/*------ Hardware Portability Macros --------------------------*/
/*=============================================================*/

#define ieee2host16(n)	__le16_to_cpu(n)
#define ieee2host32(n)	__le32_to_cpu(n)
#define host2ieee16(n)	__cpu_to_le16(n)
#define host2ieee32(n)	__cpu_to_le32(n)

#if (WLAN_CPU_FAMILY != WLAN_MIPS)
typedef UINT32 phys_t;
#endif

#if (WLAN_CPU_FAMILY == WLAN_PPC)
       #define wlan_inw(a)                     in_be16((unsigned short *)((a)+_IO_BASE))
       #define wlan_inw_le16_to_cpu(a)         inw((a))
       #define wlan_outw(v,a)                  out_be16((unsigned short *)((a)+_IO_BASE), (v))
       #define wlan_outw_cpu_to_le16(v,a)      outw((v),(a))
#else
       #define wlan_inw(a)                     inw((a))
       #define wlan_inw_le16_to_cpu(a)         __cpu_to_le16(inw((a)))
       #define wlan_outw(v,a)                  outw((v),(a))
       #define wlan_outw_cpu_to_le16(v,a)      outw(__cpu_to_le16((v)),(a))
#endif

/*=============================================================*/
/*--- General Macros ------------------------------------------*/
/*=============================================================*/

#define wlan_max(a, b) (((a) > (b)) ? (a) : (b))
#define wlan_min(a, b) (((a) < (b)) ? (a) : (b))

#define wlan_isprint(c)	(((c) > (0x19)) && ((c) < (0x7f)))

#define wlan_hexchar(x) (((x) < 0x0a) ? ('0' + (x)) : ('a' + ((x) - 0x0a)))

/* Create a string of printable chars from something that might not be */
/* It's recommended that the str be 4*len + 1 bytes long */
#define wlan_mkprintstr(buf, buflen, str, strlen) \
{ \
	int i = 0; \
	int j = 0; \
	memset(str, 0, (strlen)); \
	for (i = 0; i < (buflen); i++) { \
		if ( wlan_isprint((buf)[i]) ) { \
			(str)[j] = (buf)[i]; \
			j++; \
		} else { \
			(str)[j] = '\\'; \
			(str)[j+1] = 'x'; \
			(str)[j+2] = wlan_hexchar(((buf)[i] & 0xf0) >> 4); \
			(str)[j+3] = wlan_hexchar(((buf)[i] & 0x0f)); \
			j += 4; \
		} \
	} \
}

/*=============================================================*/
/*--- Variables -----------------------------------------------*/
/*=============================================================*/

#ifdef WLAN_INCLUDE_DEBUG
extern int wlan_debug;
#endif

extern int wlan_ethconv;		/* What's the default ethconv? */

/*=============================================================*/
/*--- Functions -----------------------------------------------*/
/*=============================================================*/
#endif /* _WLAN_COMPAT_H */

