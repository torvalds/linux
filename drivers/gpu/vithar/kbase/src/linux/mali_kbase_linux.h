/*
 *
 * (C) COPYRIGHT 2010-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_kbase_linux.h
 * Base kernel APIs, Linux implementation.
 */

#ifndef _KBASE_LINUX_H_
#define _KBASE_LINUX_H_

/* All things that are needed for the Linux port. */
#if MALI_LICENSE_IS_GPL
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#endif
#include <linux/list.h>

typedef struct kbase_os_context
{
	u64			cookies;
	osk_dlist		reg_pending;
	wait_queue_head_t	event_queue;
} kbase_os_context;


#define DEVNAME_SIZE	16

typedef struct kbase_os_device
{
#if MALI_LICENSE_IS_GPL
	struct list_head	entry;
	struct device		*dev;
	struct miscdevice	mdev;
#else
	struct cdev		*dev;
#endif
	u64					reg_start;
	size_t				reg_size;
	void __iomem		*reg;
	struct resource		*reg_res;
	struct {
		int		irq;
		int		flags;
	} irqs[3];
	char			devname[DEVNAME_SIZE];

#if MALI_NO_MALI
	void *model;
	struct kmem_cache *irq_slab;
	osk_workq irq_workq;
	osk_atomic serving_job_irq;
	osk_atomic serving_gpu_irq;
	osk_atomic serving_mmu_irq;
	osk_spinlock reg_op_lock;
#endif
} kbase_os_device;

#define KBASE_OS_SUPPORT	1

#if defined(MALI_KERNEL_TEST_API)
#if (1 == MALI_KERNEL_TEST_API)
#define KBASE_EXPORT_TEST_API(func)		EXPORT_SYMBOL(func);
#else
#define KBASE_EXPORT_TEST_API(func)
#endif
#else
#define KBASE_EXPORT_TEST_API(func)
#endif

#endif /* _KBASE_LINUX_H_ */
