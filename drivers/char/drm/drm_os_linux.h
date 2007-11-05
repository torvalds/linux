/**
 * \file drm_os_linux.h
 * OS abstraction macros.
 */

#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>

/** Current process ID */
#define DRM_CURRENTPID			task_pid_nr(current)
#define DRM_SUSER(p)			capable(CAP_SYS_ADMIN)
#define DRM_UDELAY(d)			udelay(d)
/** Read a byte from a MMIO region */
#define DRM_READ8(map, offset)		readb(((void __iomem *)(map)->handle) + (offset))
/** Read a word from a MMIO region */
#define DRM_READ16(map, offset)         readw(((void __iomem *)(map)->handle) + (offset))
/** Read a dword from a MMIO region */
#define DRM_READ32(map, offset)		readl(((void __iomem *)(map)->handle) + (offset))
/** Write a byte into a MMIO region */
#define DRM_WRITE8(map, offset, val)	writeb(val, ((void __iomem *)(map)->handle) + (offset))
/** Write a word into a MMIO region */
#define DRM_WRITE16(map, offset, val)   writew(val, ((void __iomem *)(map)->handle) + (offset))
/** Write a dword into a MMIO region */
#define DRM_WRITE32(map, offset, val)	writel(val, ((void __iomem *)(map)->handle) + (offset))
/** Read memory barrier */
#define DRM_READMEMORYBARRIER()		rmb()
/** Write memory barrier */
#define DRM_WRITEMEMORYBARRIER()	wmb()
/** Read/write memory barrier */
#define DRM_MEMORYBARRIER()		mb()

/** IRQ handler arguments and return type and values */
#define DRM_IRQ_ARGS		int irq, void *arg

/** AGP types */
#if __OS_HAS_AGP
#define DRM_AGP_MEM		struct agp_memory
#define DRM_AGP_KERN		struct agp_kern_info
#else
/* define some dummy types for non AGP supporting kernels */
struct no_agp_kern {
	unsigned long aper_base;
	unsigned long aper_size;
};
#define DRM_AGP_MEM             int
#define DRM_AGP_KERN            struct no_agp_kern
#endif

#if !(__OS_HAS_MTRR)
static __inline__ int mtrr_add(unsigned long base, unsigned long size,
			       unsigned int type, char increment)
{
	return -ENODEV;
}

static __inline__ int mtrr_del(int reg, unsigned long base, unsigned long size)
{
	return -ENODEV;
}

#define MTRR_TYPE_WRCOMB     1

#endif

/** Other copying of data to kernel space */
#define DRM_COPY_FROM_USER(arg1, arg2, arg3)		\
	copy_from_user(arg1, arg2, arg3)
/** Other copying of data from kernel space */
#define DRM_COPY_TO_USER(arg1, arg2, arg3)		\
	copy_to_user(arg1, arg2, arg3)
/* Macros for copyfrom user, but checking readability only once */
#define DRM_VERIFYAREA_READ( uaddr, size )		\
	(access_ok( VERIFY_READ, uaddr, size ) ? 0 : -EFAULT)
#define DRM_COPY_FROM_USER_UNCHECKED(arg1, arg2, arg3)	\
	__copy_from_user(arg1, arg2, arg3)
#define DRM_COPY_TO_USER_UNCHECKED(arg1, arg2, arg3)	\
	__copy_to_user(arg1, arg2, arg3)
#define DRM_GET_USER_UNCHECKED(val, uaddr)		\
	__get_user(val, uaddr)

#define DRM_HZ HZ

#define DRM_WAIT_ON( ret, queue, timeout, condition )		\
do {								\
	DECLARE_WAITQUEUE(entry, current);			\
	unsigned long end = jiffies + (timeout);		\
	add_wait_queue(&(queue), &entry);			\
								\
	for (;;) {						\
		__set_current_state(TASK_INTERRUPTIBLE);	\
		if (condition)					\
			break;					\
		if (time_after_eq(jiffies, end)) {		\
			ret = -EBUSY;				\
			break;					\
		}						\
		schedule_timeout((HZ/100 > 1) ? HZ/100 : 1);	\
		if (signal_pending(current)) {			\
			ret = -EINTR;				\
			break;					\
		}						\
	}							\
	__set_current_state(TASK_RUNNING);			\
	remove_wait_queue(&(queue), &entry);			\
} while (0)

#define DRM_WAKEUP( queue ) wake_up_interruptible( queue )
#define DRM_INIT_WAITQUEUE( queue ) init_waitqueue_head( queue )
