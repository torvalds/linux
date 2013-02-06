/**
 * Header file of MobiCore Driver Kernel Module.
 *
 * @addtogroup MobiCore_Driver_Kernel_Module
 * @{
 * Wrapper for Linux API
 * @file
 *
 * Some convenient wrappers for memory functions
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MC_DRV_MODULE_LINUX_API_H_
#define _MC_DRV_MODULE_LINUX_API_H_

#include <linux/version.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/kthread.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <asm/sizes.h>
#include <asm/pgtable.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/workqueue.h>
#include <linux/timer.h>

/* make some nice types */
#if !defined(TRUE)
#define TRUE (1 == 1)
#endif

#if !defined(FALSE)
#define FALSE (1 != 1)
#endif


/* Linux GCC modifiers */
#if !defined(__init)
#warning "missing definition: __init"
/* define a dummy */
#define __init
#endif


#if !defined(__exit)
#warning "missing definition: __exit"
/* define a dummy */
#define __exit
#endif


#if !defined(__must_check)
#warning "missing definition: __must_check"
/* define a dummy */
#define __must_check
#endif


#if !defined(__user)
#warning "missing definition: __user"
/* define a dummy */
#define __user
#endif

#define INVALID_ORDER       ((unsigned int)(-1))

/*----------------------------------------------------------------------------*/
/* get start address of the 4 KiB page where the given addres is located in. */
static inline void *getPageStart(
	void *addr
)
{
	return (void *)(((unsigned long)(addr)) & PAGE_MASK);
}

/*----------------------------------------------------------------------------*/
/* get offset into the 4 KiB page where the given addres is located in. */
static inline unsigned int getOffsetInPage(
	void *addr
)
{
	return (unsigned int)(((unsigned long)(addr)) & (~PAGE_MASK));
}

/*----------------------------------------------------------------------------*/
/* get number of pages for a given buffer. */
static inline unsigned int getNrOfPagesForBuffer(
	void		*addrStart, /* may be null */
	unsigned int	len
)
{
	/* calculate used number of pages. Example:
	offset+size    newSize+PAGE_SIZE-1    nrOfPages
	   0              4095                   0
	   1              4096                   1
	  4095            8190                   1
	  4096            8191                   1
	  4097            8192                   2 */

	return (getOffsetInPage(addrStart) + len + PAGE_SIZE-1) / PAGE_SIZE;
}


/*----------------------------------------------------------------------------*/
/**
 * convert a given size to page order, which is equivalent to finding log_2(x).
 * The maximum for order was 5 in Linux 2.0 corresponding to 32 pages.
 * Later versions allow 9 corresponding to 512 pages, which is 2 MB on
 * most platforms). Anyway, the bigger order is, the more likely it is
 * that the allocation will fail.
 * Size       0           1  4097  8193  12289  24577  28673   40961   61441
 * Pages      -           1     2     3      4      7      8      15      16
 * Order  INVALID_ORDER   0     1     1      2      2      3       3       4
 *
 * @param  size
 * @return order
 */
static inline unsigned int sizeToOrder(
	unsigned int size
)
{
	unsigned int order = INVALID_ORDER;

	if (0 != size) {
		/* ARMv5 as a CLZ instruction which count the leading zeros of
		the binary representation of a value. It return a value
		between 0 and 32.
		Value   0   1   2   3   4   5   6   7   8   9  10 ...
		CLZ    32  31  30  30  29  29  29  29  28  28  28 ...

		We have excluded Size==0 before, so this is safe. */
		order = __builtin_clz(
				getNrOfPagesForBuffer(NULL, size));

		/* there is a size overflow in getNrOfPagesForBuffer when
		 * the size is too large */
		if (unlikely(order > 31))
			return INVALID_ORDER;
		order = 31 - order;

		/* above algorithm rounds down: clz(5)=2 instead of 3 */
		/* quick correction to fix it: */
		if (((1<<order)*PAGE_SIZE) < size)
			order++;
	}
	return order;
}

/* magic linux macro */
#if !defined(list_for_each_entry)
/* stop compiler */
#error "missing macro: list_for_each_entry()"
/* define a dummy */
#define list_for_each_entry(a, b, c)    if (0)
#endif

/*----------------------------------------------------------------------------*/
/* return the page frame number of an address */
static inline unsigned int addrToPfn(
	void *addr
)
{
	/* there is no real API for this */
	return ((unsigned int)(addr)) >> PAGE_SHIFT;
}


/*----------------------------------------------------------------------------*/
/* return the address of a page frame number */
static inline void *pfnToAddr(
	unsigned int pfn
)
{
	/* there is no real API for this */
	return (void *)(pfn << PAGE_SHIFT);
}

#endif /* _MC_DRV_MODULE_LINUX_API_H_ */
/** @} */
