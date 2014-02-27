/*
 * Linux-specific abstractions to gain some independence from linux kernel versions.
 * Pave over some 2.2 versus 2.4 versus 2.6 kernel differences.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: linuxver.h 417757 2013-08-12 12:24:45Z $
 */

#ifndef _linuxver_h_
#define _linuxver_h_

#include <typedefs.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))
#include <linux/config.h>
#else
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33))
#include <generated/autoconf.h>
#else
#include <linux/autoconf.h>
#endif
#endif 
#include <linux/module.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 0))

#ifdef __UNDEF_NO_VERSION__
#undef __NO_VERSION__
#else
#define __NO_VERSION__
#endif
#endif	

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 0)
#define module_param(_name_, _type_, _perm_)	MODULE_PARM(_name_, "i")
#define module_param_string(_name_, _string_, _size_, _perm_) \
		MODULE_PARM(_string_, "c" __MODULE_STRING(_size_))
#endif


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 9))
#include <linux/malloc.h>
#else
#include <linux/slab.h>
#endif

#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
#include <linux/semaphore.h>
#else
#include <asm/semaphore.h>
#endif 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
#undef IP_TOS
#endif 
#include <asm/io.h>

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 41))
#include <linux/workqueue.h>
#else
#include <linux/tqueue.h>
#ifndef work_struct
#define work_struct tq_struct
#endif
#ifndef INIT_WORK
#define INIT_WORK(_work, _func, _data) INIT_TQUEUE((_work), (_func), (_data))
#endif
#ifndef schedule_work
#define schedule_work(_work) schedule_task((_work))
#endif
#ifndef flush_scheduled_work
#define flush_scheduled_work() flush_scheduled_tasks()
#endif
#endif	

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
#define DAEMONIZE(a)
#elif ((LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)))
#define DAEMONIZE(a) daemonize(a); \
	allow_signal(SIGKILL); \
	allow_signal(SIGTERM);
#else 
#define RAISE_RX_SOFTIRQ() \
	cpu_raise_softirq(smp_processor_id(), NET_RX_SOFTIRQ)
#define DAEMONIZE(a) daemonize(); \
	do { if (a) \
		strncpy(current->comm, a, MIN(sizeof(current->comm), (strlen(a)))); \
	} while (0);
#endif 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
#define	MY_INIT_WORK(_work, _func)	INIT_WORK(_work, _func)
#else
#define	MY_INIT_WORK(_work, _func)	INIT_WORK(_work, _func, _work)
#if !(LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 18) && defined(RHEL_MAJOR) && \
	(RHEL_MAJOR == 5))

typedef void (*work_func_t)(void *work);
#endif
#endif	

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))

#ifndef IRQ_NONE
typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)
#endif
#else
typedef irqreturn_t(*FN_ISR) (int irq, void *dev_id, struct pt_regs *ptregs);
#endif	

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18)
#define IRQF_SHARED	SA_SHIRQ
#endif 

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 17)
#ifdef	CONFIG_NET_RADIO
#define	CONFIG_WIRELESS_EXT
#endif
#endif	

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 67)
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
#include <linux/sched.h>
#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
#include <linux/sched/rt.h>
#endif 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29)
#include <net/lib80211.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29)
#include <linux/ieee80211.h>
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 14)
#include <net/ieee80211.h>
#endif
#endif 


#ifndef __exit
#define __exit
#endif
#ifndef __devexit
#define __devexit
#endif
#ifndef __devinit
#define __devinit	__init
#endif
#ifndef __devinitdata
#define __devinitdata
#endif
#ifndef __devexit_p
#define __devexit_p(x)	x
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 0))

#define pci_get_drvdata(dev)		(dev)->sysdata
#define pci_set_drvdata(dev, value)	(dev)->sysdata = (value)



struct pci_device_id {
	unsigned int vendor, device;		
	unsigned int subvendor, subdevice;	
	unsigned int class, class_mask;		
	unsigned long driver_data;		
};

struct pci_driver {
	struct list_head node;
	char *name;
	const struct pci_device_id *id_table;	
	int (*probe)(struct pci_dev *dev,
	             const struct pci_device_id *id); 
	void (*remove)(struct pci_dev *dev);	
	void (*suspend)(struct pci_dev *dev);	
	void (*resume)(struct pci_dev *dev);	
};

#define MODULE_DEVICE_TABLE(type, name)
#define PCI_ANY_ID (~0)


#define pci_module_init pci_register_driver
extern int pci_register_driver(struct pci_driver *drv);
extern void pci_unregister_driver(struct pci_driver *drv);

#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 18))
#define pci_module_init pci_register_driver
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 2, 18))
#ifdef MODULE
#define module_init(x) int init_module(void) { return x(); }
#define module_exit(x) void cleanup_module(void) { x(); }
#else
#define module_init(x)	__initcall(x);
#define module_exit(x)	__exitcall(x);
#endif
#endif	

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
#define WL_USE_NETDEV_OPS
#else
#undef WL_USE_NETDEV_OPS
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)) && defined(CONFIG_RFKILL)
#define WL_CONFIG_RFKILL
#else
#undef WL_CONFIG_RFKILL
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 48))
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 13))
#define pci_resource_start(dev, bar)	((dev)->base_address[(bar)])
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 44))
#define pci_resource_start(dev, bar)	((dev)->resource[(bar)].start)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 23))
#define pci_enable_device(dev) do { } while (0)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 14))
#define net_device device
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 42))



#ifndef PCI_DMA_TODEVICE
#define	PCI_DMA_TODEVICE	1
#define	PCI_DMA_FROMDEVICE	2
#endif

typedef u32 dma_addr_t;


static inline int get_order(unsigned long size)
{
	int order;

	size = (size-1) >> (PAGE_SHIFT-1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

static inline void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
                                         dma_addr_t *dma_handle)
{
	void *ret;
	int gfp = GFP_ATOMIC | GFP_DMA;

	ret = (void *)__get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_bus(ret);
	}
	return ret;
}
static inline void pci_free_consistent(struct pci_dev *hwdev, size_t size,
                                       void *vaddr, dma_addr_t dma_handle)
{
	free_pages((unsigned long)vaddr, get_order(size));
}
#define pci_map_single(cookie, address, size, dir)	virt_to_bus(address)
#define pci_unmap_single(cookie, address, size, dir)

#endif 

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 43))

#define dev_kfree_skb_any(a)		dev_kfree_skb(a)
#define netif_down(dev)			do { (dev)->start = 0; } while (0)


#ifndef _COMPAT_NETDEVICE_H



#define dev_kfree_skb_irq(a)	dev_kfree_skb(a)
#define netif_wake_queue(dev) \
		do { clear_bit(0, &(dev)->tbusy); mark_bh(NET_BH); } while (0)
#define netif_stop_queue(dev)	set_bit(0, &(dev)->tbusy)

static inline void netif_start_queue(struct net_device *dev)
{
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;
}

#define netif_queue_stopped(dev)	(dev)->tbusy
#define netif_running(dev)		(dev)->start

#endif 

#define netif_device_attach(dev)	netif_start_queue(dev)
#define netif_device_detach(dev)	netif_stop_queue(dev)


#define tasklet_struct				tq_struct
static inline void tasklet_schedule(struct tasklet_struct *tasklet)
{
	queue_task(tasklet, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

static inline void tasklet_init(struct tasklet_struct *tasklet,
                                void (*func)(unsigned long),
                                unsigned long data)
{
	tasklet->next = NULL;
	tasklet->sync = 0;
	tasklet->routine = (void (*)(void *))func;
	tasklet->data = (void *)data;
}
#define tasklet_kill(tasklet)	{ do {} while (0); }


#define del_timer_sync(timer) del_timer(timer)

#else

#define netif_down(dev)

#endif 

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 3))


#define PREPARE_TQUEUE(_tq, _routine, _data)			\
	do {							\
		(_tq)->routine = _routine;			\
		(_tq)->data = _data;				\
	} while (0)


#define INIT_TQUEUE(_tq, _routine, _data)			\
	do {							\
		INIT_LIST_HEAD(&(_tq)->list);			\
		(_tq)->sync = 0;				\
		PREPARE_TQUEUE((_tq), (_routine), (_data));	\
	} while (0)

#endif	


#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 9)
#define	PCI_SAVE_STATE(a, b)	pci_save_state(a)
#define	PCI_RESTORE_STATE(a, b)	pci_restore_state(a)
#else
#define	PCI_SAVE_STATE(a, b)	pci_save_state(a, b)
#define	PCI_RESTORE_STATE(a, b)	pci_restore_state(a, b)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 6))
static inline int
pci_save_state(struct pci_dev *dev, u32 *buffer)
{
	int i;
	if (buffer) {
		for (i = 0; i < 16; i++)
			pci_read_config_dword(dev, i * 4, &buffer[i]);
	}
	return 0;
}

static inline int
pci_restore_state(struct pci_dev *dev, u32 *buffer)
{
	int i;

	if (buffer) {
		for (i = 0; i < 16; i++)
			pci_write_config_dword(dev, i * 4, buffer[i]);
	}
	
	else {
		for (i = 0; i < 6; i ++)
			pci_write_config_dword(dev,
			                       PCI_BASE_ADDRESS_0 + (i * 4),
			                       pci_resource_start(dev, i));
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
	}
	return 0;
}
#endif 


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 19))
#define read_c0_count() read_32bit_cp0_register(CP0_COUNT)
#endif


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
#ifndef SET_MODULE_OWNER
#define SET_MODULE_OWNER(dev)		do {} while (0)
#define OLD_MOD_INC_USE_COUNT		MOD_INC_USE_COUNT
#define OLD_MOD_DEC_USE_COUNT		MOD_DEC_USE_COUNT
#else
#define OLD_MOD_INC_USE_COUNT		do {} while (0)
#define OLD_MOD_DEC_USE_COUNT		do {} while (0)
#endif
#else 
#ifndef SET_MODULE_OWNER
#define SET_MODULE_OWNER(dev)		do {} while (0)
#endif
#ifndef MOD_INC_USE_COUNT
#define MOD_INC_USE_COUNT			do {} while (0)
#endif
#ifndef MOD_DEC_USE_COUNT
#define MOD_DEC_USE_COUNT			do {} while (0)
#endif
#define OLD_MOD_INC_USE_COUNT		MOD_INC_USE_COUNT
#define OLD_MOD_DEC_USE_COUNT		MOD_DEC_USE_COUNT
#endif 

#ifndef SET_NETDEV_DEV
#define SET_NETDEV_DEV(net, pdev)	do {} while (0)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0))
#ifndef HAVE_FREE_NETDEV
#define free_netdev(dev)		kfree(dev)
#endif
#endif 

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))

#define af_packet_priv			data
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
#define DRV_SUSPEND_STATE_TYPE pm_message_t
#else
#define DRV_SUSPEND_STATE_TYPE uint32
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
#define CHECKSUM_HW	CHECKSUM_PARTIAL
#endif

typedef struct {
	void	*parent;  
	char	*proc_name;
	struct	task_struct *p_task;
	long	thr_pid;
	int		prio; 
	struct	semaphore sema;
	int	terminated;
	struct	completion completed;
	spinlock_t	spinlock;
	int		up_cnt;
} tsk_ctl_t;




#ifdef DHD_DEBUG
#define DBG_THR(x) printk x
#else
#define DBG_THR(x)
#endif

static inline bool binary_sema_down(tsk_ctl_t *tsk)
{
	if (down_interruptible(&tsk->sema) == 0) {
		unsigned long flags = 0;
		spin_lock_irqsave(&tsk->spinlock, flags);
		if (tsk->up_cnt == 1)
			tsk->up_cnt--;
		else {
			DBG_THR(("dhd_dpc_thread: Unexpected up_cnt %d\n", tsk->up_cnt));
		}
		spin_unlock_irqrestore(&tsk->spinlock, flags);
		return FALSE;
	} else
		return TRUE;
}

static inline bool binary_sema_up(tsk_ctl_t *tsk)
{
	bool sem_up = FALSE;
	unsigned long flags = 0;

	spin_lock_irqsave(&tsk->spinlock, flags);
	if (tsk->up_cnt == 0) {
		tsk->up_cnt++;
		sem_up = TRUE;
	} else if (tsk->up_cnt == 1) {
		
	} else
		DBG_THR(("dhd_sched_dpc: unexpected up cnt %d!\n", tsk->up_cnt));

	spin_unlock_irqrestore(&tsk->spinlock, flags);

	if (sem_up)
		up(&tsk->sema);

	return sem_up;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define SMP_RD_BARRIER_DEPENDS(x) smp_read_barrier_depends(x)
#else
#define SMP_RD_BARRIER_DEPENDS(x) smp_rmb(x)
#endif

#define PROC_START(thread_func, owner, tsk_ctl, flags, name) \
{ \
	sema_init(&((tsk_ctl)->sema), 0); \
	init_completion(&((tsk_ctl)->completed)); \
	(tsk_ctl)->parent = owner; \
	(tsk_ctl)->proc_name = name;  \
	(tsk_ctl)->terminated = FALSE; \
	(tsk_ctl)->p_task  = kthread_run(thread_func, tsk_ctl, (char*)name); \
	(tsk_ctl)->thr_pid = (tsk_ctl)->p_task->pid; \
	spin_lock_init(&((tsk_ctl)->spinlock)); \
	DBG_THR(("%s(): thread:%s:%lx started\n", __FUNCTION__, \
		(tsk_ctl)->proc_name, (tsk_ctl)->thr_pid)); \
}

#define PROC_STOP(tsk_ctl) \
{ \
	(tsk_ctl)->terminated = TRUE; \
	smp_wmb(); \
	up(&((tsk_ctl)->sema));	\
	wait_for_completion(&((tsk_ctl)->completed)); \
	DBG_THR(("%s(): thread:%s:%lx terminated OK\n", __FUNCTION__, \
			 (tsk_ctl)->proc_name, (tsk_ctl)->thr_pid)); \
	(tsk_ctl)->thr_pid = -1; \
}



#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
#define KILL_PROC(nr, sig) \
{ \
struct task_struct *tsk; \
struct pid *pid;    \
pid = find_get_pid((pid_t)nr);    \
tsk = pid_task(pid, PIDTYPE_PID);    \
if (tsk) send_sig(sig, tsk, 1); \
}
#else
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && (LINUX_VERSION_CODE <= \
	KERNEL_VERSION(2, 6, 30))
#define KILL_PROC(pid, sig) \
{ \
	struct task_struct *tsk; \
	tsk = find_task_by_vpid(pid); \
	if (tsk) send_sig(sig, tsk, 1); \
}
#else
#define KILL_PROC(pid, sig) \
{ \
	kill_proc(pid, sig, 1); \
}
#endif
#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#include <linux/time.h>
#include <linux/wait.h>
#else
#include <linux/sched.h>

#define __wait_event_interruptible_timeout(wq, condition, ret)		\
do {									\
	wait_queue_t __wait;						\
	init_waitqueue_entry(&__wait, current);				\
									\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		set_current_state(TASK_INTERRUPTIBLE);			\
		if (condition)						\
			break;						\
		if (!signal_pending(current)) {				\
			ret = schedule_timeout(ret);			\
			if (!ret)					\
				break;					\
			continue;					\
		}							\
		ret = -ERESTARTSYS;					\
		break;							\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)

#define wait_event_interruptible_timeout(wq, condition, timeout)	\
({									\
	long __ret = timeout;						\
	if (!(condition))						\
		__wait_event_interruptible_timeout(wq, condition, __ret); \
	__ret;								\
})

#endif 


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
#define DEV_PRIV(dev)	(dev->priv)
#else
#define DEV_PRIV(dev)	netdev_priv(dev)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 20)
#define WL_ISR(i, d, p)         wl_isr((i), (d))
#else
#define WL_ISR(i, d, p)         wl_isr((i), (d), (p))
#endif  

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))
#define netdev_priv(dev) dev->priv
#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
#define RANDOM32	prandom_u32
#else
#define RANDOM32	random32
#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
#define SRANDOM32(entropy)	prandom_seed(entropy)
#else
#define SRANDOM32(entropy)	srandom32(entropy)
#endif 

#endif 
