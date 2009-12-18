/*
 * sysdep.h -- centralizing compatibility issues between 2.0, 2.2, 2.4
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: sysdep.h,v 1.2 2005/08/09 06:08:51 ssmedley Exp $
 */


#ifndef _SYSDEP_H_
#define _SYSDEP_H_

#ifndef LINUX_VERSION_CODE
#  include <linux/version.h>
#endif

#ifndef KERNEL_VERSION /* pre-2.1.90 didn't have it */
#  define KERNEL_VERSION(vers,rel,seq) ( ((vers)<<16) | ((rel)<<8) | (seq) )
#endif

/* only allow 2.0.x  2.2.y and 2.4.z */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,0,0) /* not < 2.0 */
#  error "This kernel is too old: not supported by this file"
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,7,0) /* not > 2.7, by now */
#  error "This kernel is too recent: not supported by this file"
#endif
#if (LINUX_VERSION_CODE & 0xff00) == 1 /* not 2.1 */
#  error "Please don't use linux-2.1, use 2.2, 2.4 or 2.6 instead"
#endif
#if (LINUX_VERSION_CODE & 0xff00) == 3 /* not 2.3 */
#  error "Please don't use linux-2.3, use 2.4 or 2.6 instead"
#endif

/* remember about the current version */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,0)
#  define LINUX_20
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
#  define LINUX_22
#else
#  define LINUX_24
#endif

/* we can't support versioning in pre-2.4 because we #define some functions */
#if !defined(LINUX_24) && defined(CONFIG_MODVERSIONS)
#  error "This sysdep.h can't support CONFIG_MODVERSIONS"
#  error "and old kernels at the same time."
#  error "Either use 2.4 or avoid using versioning"
#endif

#ifndef LINUX_20 /* include vmalloc.h if this is 2.2/2.4 */
#  ifdef VM_READ /* a typical flag defined by mm.h */
#    include <linux/vmalloc.h>
#  endif
#endif

#include <linux/sched.h>

/* Modularization issues */
#ifdef LINUX_20
#  define __USE_OLD_SYMTAB__
#  define EXPORT_NO_SYMBOLS register_symtab(NULL);
#  define REGISTER_SYMTAB(tab) register_symtab(tab)
#else
#  define REGISTER_SYMTAB(tab) /* nothing */
#endif

#ifdef __USE_OLD_SYMTAB__
#  define __MODULE_STRING(s)         /* nothing */
#  define MODULE_PARM(v,t)           /* nothing */
#  define MODULE_PARM_DESC(v,t)      /* nothing */
#  define MODULE_AUTHOR(n)           /* nothing */
#  define MODULE_DESCRIPTION(d)      /* nothing */
#  define MODULE_SUPPORTED_DEVICE(n) /* nothing */
#endif

/*
 * In version 2.2 (up to 2.2.19, at least), the macro for request_module()
 * when no kmod is there is wrong. It's a "do {} while 0" but it shouldbe int
 */
#ifdef LINUX_22
#  ifndef CONFIG_KMOD
#    undef request_module
#    define request_module(name) -ENOSYS
#  endif
#endif


#ifndef LINUX_20
#  include <linux/init.h>     /* module_init/module_exit */
#endif

#ifndef module_init
#  define module_init(x)        int init_module(void) { return x(); }
#  define module_exit(x)        void cleanup_module(void) { x(); }
#endif

#ifndef SET_MODULE_OWNER
#  define SET_MODULE_OWNER(structure) /* nothing */
#endif

/*
 * "select" changed in 2.1.23. The implementation is twin, but this
 * header is new
 *
 */
#ifdef LINUX_20
#  define __USE_OLD_SELECT__
#else
#  include <linux/poll.h>
#endif

#ifdef LINUX_20
#  define INODE_FROM_F(filp) ((filp)->f_inode)
#else
#  define INODE_FROM_F(filp) ((filp)->f_dentry->d_inode)
#endif

/* Other changes in the fops are solved using wrappers */

/*
 * Wait queues changed with 2.3
 */
#ifndef DECLARE_WAIT_QUEUE_HEAD
#  define DECLARE_WAIT_QUEUE_HEAD(head) struct wait_queue *head = NULL
   typedef  struct wait_queue *wait_queue_head_t;
#  define init_waitqueue_head(head) (*(head)) = NULL

/* offer wake_up_sync as an alias for wake_up */
#  define wake_up_sync(head) wake_up(head)
#  define wake_up_interruptible_sync(head) wake_up_interruptible(head)

/* Pretend we have add_wait_queue_exclusive */
#  define add_wait_queue_exclusive(q,entry) add_wait_queue ((q), (entry))

#endif /* no DECLARE_WAIT_QUEUE_HEAD */

/*
 * Define wait_event for 2.0 kernels.  (This ripped off directly from
 * the 2.2.18 sched.h)
 */
#ifdef LINUX_20

#define __wait_event(wq, condition) 					\
do {									\
	struct wait_queue __wait;					\
									\
	__wait.task = current;						\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		current->state = TASK_UNINTERRUPTIBLE;			\
		mb();							\
		if (condition)						\
			break;						\
		schedule();						\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)

#define wait_event(wq, condition) 					\
do {									\
	if (condition)	 						\
		break;							\
	__wait_event(wq, condition);					\
} while (0)

#define __wait_event_interruptible(wq, condition, ret)			\
do {									\
	struct wait_queue __wait;					\
									\
	__wait.task = current;						\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		current->state = TASK_INTERRUPTIBLE;			\
		mb();							\
		if (condition)						\
			break;						\
		if (!signal_pending(current)) {				\
			schedule();					\
			continue;					\
		}							\
		ret = -ERESTARTSYS;					\
		break;							\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)

#define wait_event_interruptible(wq, condition)				\
({									\
	int __ret = 0;							\
	if (!(condition))						\
		__wait_event_interruptible(wq, condition, __ret);	\
	__ret;								\
})
#endif


/*
 * 2.3 added tasklets
 */
#ifdef LINUX_24
#  define HAVE_TASKLETS
#endif




/* FIXME: implement the other versions of wake_up etc */


/*
 * access to user space: use the 2.2 functions,
 * and implement them as macros for 2.0
 */

#ifdef LINUX_20
#  include <asm/segment.h>
#  define access_ok(t,a,sz)           (verify_area((t),(void *) (a),(sz)) ? 0 : 1)
#  define verify_area_20              verify_area
#  define   copy_to_user(t,f,n)         (memcpy_tofs((t), (f), (n)), 0)
#  define copy_from_user(t,f,n)       (memcpy_fromfs((t), (f), (n)), 0)
#  define   __copy_to_user(t,f,n)       copy_to_user((t), (f), (n))
#  define __copy_from_user(t,f,n)     copy_from_user((t), (f), (n))

#  define PUT_USER(val,add)           (put_user((val),(add)), 0)
#  define __PUT_USER(val,add)         PUT_USER((val),(add))

#  define GET_USER(dest,add)          ((dest)=get_user((add)), 0)
#  define __GET_USER(dest,add)        GET_USER((dest),(add))
#else
#  include <asm/uaccess.h>
#  include <asm/io.h>
#  define verify_area_20(t,a,sz) (0) /* == success */
#  define   PUT_USER   put_user
#  define __PUT_USER __put_user
#  define   GET_USER   get_user
#  define __GET_USER __get_user
#endif

/*
 * Allocation issues
 */
#ifdef GFP_USER /* only if mm.h has been included */
#  ifdef LINUX_20
#    define __GFP_DMA GFP_DMA /* 2.0 didn't have the leading __ */
#  endif
#  ifndef LINUX_24
#    define __GFP_HIGHMEM  0  /* was not there */
#    define GFP_HIGHUSER   0   /* idem */
#  endif

#  ifdef LINUX_20
#    define __get_free_pages(a,b) __get_free_pages((a),(b),0)
#  endif
#  ifndef LINUX_24
#    define get_zeroed_page get_free_page
#  endif
#endif

/* ioremap */
#if defined(LINUX_20) && defined(_LINUX_MM_H)
#  define ioremap_nocache ioremap
#  ifndef __i386__
   /* This simple approach works for non-PC platforms. */
#    define ioremap vremap
#    define iounmap vfree
#  else /* the PC has <expletive> ISA; 2.2 and 2.4 remap it, 2.0 needs not */
extern inline void *ioremap(unsigned long phys_addr, unsigned long size)
{
    if (phys_addr >= 0xA0000 && phys_addr + size <= 0x100000)
        return (void *)phys_addr;
    return vremap(phys_addr, size);
}

extern inline void iounmap(void *addr)
{
    if ((unsigned long)addr >= 0xA0000
            && (unsigned long)addr < 0x100000)
        return;
    vfree(addr);
}
#  endif
#endif

/* Also, define check_mem_region etc */
#ifndef LINUX_24
#  define check_mem_region(a,b)     0 /* success */
#  define request_mem_region(a,b,c) /* nothing */
#  define release_mem_region(a,b)   /* nothing */
#endif

/* implement capable() for 2.0 */
#ifdef LINUX_20
#  define capable(anything)  suser()
#endif

/* The use_count of exec_domain and binfmt changed in 2.1.23 */

#ifdef LINUX_20
#  define INCRCOUNT(p)  ((p)->module ? __MOD_INC_USE_COUNT((p)->module) : 0)
#  define DECRCOUNT(p)  ((p)->module ? __MOD_DEC_USE_COUNT((p)->module) : 0)
#  define CURRCOUNT(p)  ((p)->module && (p)->module->usecount)
#else
#  define INCRCOUNT(p)  ((p)->use_count++)
#  define DECRCOUNT(p)  ((p)->use_count--)
#  define CURRCOUNT(p)  ((p)->use_count)
#endif

/*
 * /proc has changed a lot across the versions...
 */
#ifdef LINUX_20
#  define USE_PROC_REGISTER
#endif


/*
 * 2.2 didn't have create_proc_{read|info}_entry yet.
 * And it looks like there are no other "interesting" entry point, as
 * the rest is somehow esotique (mknod, symlink, ...)
 */
#ifdef LINUX_22
#  ifdef PROC_SUPER_MAGIC  /* Only if procfs is being used */
extern inline struct proc_dir_entry *create_proc_read_entry(const char *name,
                          mode_t mode, struct proc_dir_entry *base,
                          read_proc_t *read_proc, void * data)
{
    struct proc_dir_entry *res=create_proc_entry(name,mode,base);
    if (res) {
        res->read_proc=read_proc;
        res->data=data;
    }
    return res;
}

#    ifndef create_proc_info_entry /* added in 2.2.18 */
typedef int (get_info_t)(char *, char **, off_t, int, int);
extern inline struct proc_dir_entry *create_proc_info_entry(const char *name,
        mode_t mode, struct proc_dir_entry *base, get_info_t *get_info)
{
        struct proc_dir_entry *res=create_proc_entry(name,mode,base);
        if (res) res->get_info=get_info;
        return res;
}
#    endif  /* no create_proc_info_entry */
#  endif
#endif

#ifdef LINUX_20
#  define test_and_set_bit(nr,addr)  test_bit((nr),(addr))
#  define test_and_clear_bit(nr,addr) clear_bit((nr),(addr))
#  define test_and_change_bit(nr,addr) change_bit((nr),(addr))
#endif


/* 2.0 had no read and write memory barriers, and 2.2 lacks the
   set_ functions */
#ifndef LINUX_24
#  ifdef LINUX_20
#    define wmb() mb() /* this is a big penalty on non-reordering platfs */
#    define rmb() mb() /* this is a big penalty on non-reordering platfs */
#  endif /* LINUX_20 */

#define set_mb() do { var = value; mb(); } while (0)
#define set_wmb() do { var = value; wmb(); } while (0)
#endif /* ! LINUX_24 */



/* 2.1.30 removed these functions. Let's define them, just in case */
#ifndef LINUX_20
#  define queue_task_irq      queue_task
#  define queue_task_irq_off  queue_task
#endif

/* 2.1.10 and 2.1.43 introduced new functions. They are worth using */

#ifdef LINUX_20

#  include <asm/byteorder.h>
#  ifdef __LITTLE_ENDIAN
#    define cpu_to_le16(x) (x)
#    define cpu_to_le32(x) (x)
#    define cpu_to_be16(x) htons((x))
#    define cpu_to_be32(x) htonl((x))
#  else
#    define cpu_to_be16(x) (x)
#    define cpu_to_be32(x) (x)
     extern inline __u16 cpu_to_le16(__u16 x) { return (x<<8) | (x>>8);}
     extern inline __u32 cpu_to_le32(__u32 x) { return (x>>24) |
             ((x>>8)&0xff00) | ((x<<8)&0xff0000) | (x<<24);}
#  endif

#  define le16_to_cpu(x)  cpu_to_le16(x)
#  define le32_to_cpu(x)  cpu_to_le32(x)
#  define be16_to_cpu(x)  cpu_to_be16(x)
#  define be32_to_cpu(x)  cpu_to_be32(x)

#  define cpu_to_le16p(addr) (cpu_to_le16(*(addr)))
#  define cpu_to_le32p(addr) (cpu_to_le32(*(addr)))
#  define cpu_to_be16p(addr) (cpu_to_be16(*(addr)))
#  define cpu_to_be32p(addr) (cpu_to_be32(*(addr)))

   extern inline void cpu_to_le16s(__u16 *a) {*a = cpu_to_le16(*a);}
   extern inline void cpu_to_le32s(__u16 *a) {*a = cpu_to_le32(*a);}
   extern inline void cpu_to_be16s(__u16 *a) {*a = cpu_to_be16(*a);}
   extern inline void cpu_to_be32s(__u16 *a) {*a = cpu_to_be32(*a);}

#  define le16_to_cpup(x) cpu_to_le16p(x)
#  define le32_to_cpup(x) cpu_to_le32p(x)
#  define be16_to_cpup(x) cpu_to_be16p(x)
#  define be32_to_cpup(x) cpu_to_be32p(x)

#  define le16_to_cpus(x) cpu_to_le16s(x)
#  define le32_to_cpus(x) cpu_to_le32s(x)
#  define be16_to_cpus(x) cpu_to_be16s(x)
#  define be32_to_cpus(x) cpu_to_be32s(x)

#endif

#ifdef LINUX_20
#  define __USE_OLD_REBUILD_HEADER__
#endif

/*
 * 2.0 didn't include sema_init, so we make our own - but only if it
 * looks like semaphore.h got included.
 */
#ifdef LINUX_20
#  ifdef MUTEX_LOCKED   /* Only if semaphore.h included */
     extern inline void sema_init (struct semaphore *sem, int val)
     {
         sem->count = val;
         sem->waking = sem->lock = 0;
         sem->wait = NULL;
     }
#  endif
#endif /* LINUX_20 */

/*
 * In 2.0, there is no real need for spinlocks, and they weren't really
 * implemented anyway.
 *
 * XXX the _irqsave variant should be defined eventually to do the
 * right thing.
 */
#ifdef LINUX_20
typedef int spinlock_t;
#  define spin_lock(lock)
#  define spin_unlock(lock)
#  define spin_lock_init(lock)

#  define spin_lock_irqsave(lock,flags) do { \
        save_flags(flags); cli(); } while (0);
#  define spin_unlock_irqrestore(lock,flags) restore_flags(flags);
#endif

/*
 * 2.1 stuffed the "flush" method into the middle of the file_operations
 * structure.  The FOP_NO_FLUSH symbol is for drivers that do not implement
 * flush (most of them), it can be inserted in initializers for all 2.x
 * kernel versions.
 */
#ifdef LINUX_20
#  define FOP_NO_FLUSH   /* nothing */
#  define TAG_LLSEEK    lseek
#  define TAG_POLL      select
#else
#  define FOP_NO_FLUSH  NULL,
#  define TAG_LLSEEK    llseek
#  define TAG_POLL      poll
#endif



/*
 * fasync changed in 2.2.
 */
#ifdef LINUX_20
/*  typedef struct inode *fasync_file; */
#  define fasync_file struct inode *
#else
  typedef int fasync_file;
#endif

/* kill_fasync had less arguments, and a different indirection in the first */
#ifndef LINUX_24
#  define kill_fasync(ptrptr,sig,band)  kill_fasync(*(ptrptr),(sig))
#endif

/* other things that are virtualized: define the new functions for the old k */
#ifdef LINUX_20
#  define in_interrupt() (intr_count!=0)
#  define mdelay(x) udelay((x)*1000)
#  define signal_pending(current)  ((current)->signal & ~(current)->blocked)
#endif

#ifdef LINUX_PCI_H /* only if PCI stuff is being used */
#  ifdef LINUX_20
#    include "pci-compat.h" /* a whole set of replacement functions */
#  else
#    define  pci_release_device(d) /* placeholder, used in 2.0 to free stuff */
#  endif
#endif



/*
 * Some task state stuff
 */

#ifndef set_current_state
#  define set_current_state(s) current->state = (s);
#endif

#ifdef LINUX_20
extern inline void schedule_timeout(int timeout)
{
    current->timeout = jiffies + timeout;
    current->state = TASK_INTERRUPTIBLE;
    schedule();
    current->timeout = 0;
}

extern inline long sleep_on_timeout(wait_queue_head_t *q, signed long timeout)
{
    signed long early = 0;

    current->timeout = jiffies + timeout;
    sleep_on (q);
    if (current->timeout > 0) {
        early = current->timeout - jiffies;
        current->timeout = 0;
    }
    return early;
}


extern inline long interruptible_sleep_on_timeout(wait_queue_head_t *q,
                signed long timeout)
{
    signed long early = 0;

    current->timeout = jiffies + timeout;
    interruptible_sleep_on (q);
    if (current->timeout > 0) {
        early = current->timeout - jiffies;
        current->timeout = 0;
    }
    return early;
}

#endif /* LINUX_20 */

/*
 * Schedule_task was a late 2.4 addition.
 */
#ifndef LINUX_24
extern inline int schedule_task(struct tq_struct *task)
{
        queue_task(task, &tq_scheduler);
        return 1;
}
#endif


/*
 * Timing issues
 */
#ifdef LINUX_20
#  define get_fast_time do_gettimeofday
#endif

#ifdef _LINUX_DELAY_H /* only if linux/delay.h is included */
#  ifndef mdelay /* linux-2.0 */
#    ifndef MAX_UDELAY_MS
#      define MAX_UDELAY_MS   5
#    endif
#    define mdelay(n) (\
        (__builtin_constant_p(n) && (n)<=MAX_UDELAY_MS) ? udelay((n)*1000) : \
        ({unsigned long msec=(n); while (msec--) udelay(1000);}))
#  endif /* mdelay */
#endif /* _LINUX_DELAY_H */


/*
 * No del_timer_sync before 2.4
 */
#ifndef LINUX_24
#  define del_timer_sync(timer) del_timer(timer)  /* and hope */
#endif

/*
 * mod_timer wasn't present in 2.0
 */
#ifdef LINUX_20
static inline int mod_timer(struct timer_list *timer, unsigned long expires)
{
    int pending = del_timer(timer);
    if (pending) {
        timer->expires = expires;
        add_timer(timer);
    }
    return pending;
}
#endif
/*
 * Various changes in mmap and friends.
 */

#ifndef NOPAGE_SIGBUS
#  define NOPAGE_SIGBUS  NULL  /* return value of the nopage memory method */
#  define NOPAGE_OOM     NULL  /* No real equivalent in older kernels */
#endif

#ifndef VM_RESERVED            /* Added 2.4.0-test10 */
#  define VM_RESERVED 0
#endif

#ifdef LINUX_24 /* use "vm_pgoff" to get an offset */
#define VMA_OFFSET(vma)  ((vma)->vm_pgoff << PAGE_SHIFT)
#else /* use "vm_offset" */
#define VMA_OFFSET(vma)  ((vma)->vm_offset)
#endif

#ifdef MAP_NR
#define virt_to_page(page) (mem_map + MAP_NR(page))
#endif

#ifndef get_page
#  define get_page(p) atomic_inc(&(p)->count)
#endif

/*
 * No DMA lock in 2.0.
 */
#ifdef LINUX_20
static inline unsigned long claim_dma_lock(void)
{
    unsigned long flags;
    save_flags(flags);
    cli();
    return flags;
}

static inline void release_dma_lock(unsigned long flags)
{
    restore_flags(flags);
}
#endif


/*
 * I/O memory was not managed by ealier kernels, define them as success
 */

#if 0 /* FIXME: what is the right way to do request_mem_region? */
#ifndef LINUX_24
#  define check_mem_region(start, len)          0
#  define request_mem_region(start, len, name)  0
#  define release_mem_region(start, len)        0

   /*
    * Also, request_ and release_ region used to return void. Return 0 instead
    */
#  define request_region(s, l, n)  ({request_region((s),(l),(n));0;})
#  define release_region(s, l)     ({release_region((s),(l));0;})

#endif /* not LINUX_24 */
#endif

/*
 * Block layer stuff.
 */
#ifndef LINUX_24

/* BLK_DEFAULT_QUEUE for use with these macros only!!!! */
#define BLK_DEFAULT_QUEUE(major) blk_dev[(major)].request_fn
#define blk_init_queue(where,request_fn) where = request_fn;
#define blk_cleanup_queue(where) where = NULL;

/* No QUEUE_EMPTY in older kernels */
#ifndef QUEUE_EMPTY  /* Driver can redefine it too */
#  define QUEUE_EMPTY (CURRENT != NULL)
#endif

#ifdef RO_IOCTLS
static inline int blk_ioctl(kdev_t dev, unsigned int cmd, unsigned long arg)
{
    int err;

    switch (cmd) {
      case BLKRAGET: /* return the readahead value */
        if (!arg)  return -EINVAL;
        err = ! access_ok(VERIFY_WRITE, arg, sizeof(long));
        if (err) return -EFAULT;
        PUT_USER(read_ahead[MAJOR(dev)],(long *) arg);
        return 0;

      case BLKRASET: /* set the readahead value */
        if (!capable(CAP_SYS_ADMIN)) return -EACCES;
        if (arg > 0xff) return -EINVAL; /* limit it */
        read_ahead[MAJOR(dev)] = arg;
        return 0;

      case BLKFLSBUF: /* flush */
        if (! capable(CAP_SYS_ADMIN)) return -EACCES; /* only root */
        fsync_dev(dev);
        invalidate_buffers(dev);
        return 0;

        RO_IOCTLS(dev, arg);
    }
    return -ENOTTY;
}
#endif  /* RO_IOCTLS */

#ifdef LINUX_EXTENDED_PARTITION /* defined in genhd.h */
static inline void register_disk(struct gendisk *gdev, kdev_t dev,
                unsigned minors, struct file_operations *ops, long size)
{
    if (! gdev)
        return;
    resetup_one_dev(gdev, MINOR(dev) >> gdev->minor_shift);
}
#endif /* LINUX_EXTENDED_PARTITION */


#else  /* it is Linux 2.4 */
#define HAVE_BLKPG_H
#endif /* LINUX_24 */



#ifdef LINUX_20 /* physical and virtual addresses had the same value */
#  define __pa(a) (a)
#  define __va(a) (a)
#endif

/*
 * Network driver compatibility
 */

/*
 * 2.0 dev_kfree_skb had an extra arg.  The following is a little dangerous
 * in that it assumes that FREE_WRITE is always wanted.  Very few 2.0 drivers
 * use FREE_READ, but the number is *not* zero...
 *
 * Also: implement the non-checking versions of a couple skb functions -
 * but they still check in 2.0.
 */
#ifdef LINUX_20
#  define dev_kfree_skb(skb) dev_kfree_skb((skb), FREE_WRITE);

#  define __skb_push(skb, len) skb_push((skb), (len))
#  define __skb_put(skb, len)  skb_put((skb), (len))
#endif

/*
 * Softnet changes in 2.4
 */
#ifndef LINUX_24
#  ifdef _LINUX_NETDEVICE_H /* only if netdevice.h was included */
#  define netif_start_queue(dev) clear_bit(0, (void *) &(dev)->tbusy);
#  define netif_stop_queue(dev)  set_bit(0, (void *) &(dev)->tbusy);

static inline void netif_wake_queue(struct device *dev)
{
    clear_bit(0, (void *) &(dev)->tbusy);
    mark_bh(NET_BH);
}

/* struct device became struct net_device */
#  define net_device device
#  endif /* netdevice.h */
#endif /* ! LINUX_24 */

/*
 * Memory barrier stuff, define what's missing from older kernel versions
 */
#ifdef switch_to /* this is always a macro, defined in <asm/sysstem.h> */

#  ifndef set_mb
#    define set_mb(var, value) do {(var) = (value); mb();}  while 0
#  endif
#  ifndef set_rmb
#    define set_rmb(var, value) do {(var) = (value); rmb();}  while 0
#  endif
#  ifndef set_wmb
#    define set_wmb(var, value) do {(var) = (value); wmb();}  while 0
#  endif

/* The hw barriers are defined as sw barriers. A correct thing if this
   specific kernel/platform is supported but has no specific instruction */
#  ifndef mb
#    define mb barrier
#  endif
#  ifndef rmb
#    define rmb barrier
#  endif
#  ifndef wmb
#    define wmb barrier
#  endif

#endif /* switch to (i.e. <asm/system.h>) */


#endif /* _SYSDEP_H_ */
