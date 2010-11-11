/****************************************************************************
*
*    Copyright (C) 2005 - 2010 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/




#ifndef __gc_hal_kernel_linux_h_
#define __gc_hal_kernel_linux_h_

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/signal.h>
#ifdef FLAREON
#   include <asm/arch-realview/dove_gpio_irq.h>
#endif
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>

#ifdef MODVERSIONS
#  include <linux/modversions.h>
#endif
#include <asm/io.h>
#include <asm/uaccess.h>

#if ENABLE_GPU_CLOCK_BY_DRIVER && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
#include <linux/clk.h>
#endif

#define NTSTRSAFE_NO_CCH_FUNCTIONS
#include "gc_hal.h"
#include "gc_hal_driver.h"
#include "gc_hal_kernel.h"
#include "gc_hal_kernel_device.h"
#include "gc_hal_kernel_os.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
#define FIND_TASK_BY_PID(x) pid_task(find_vpid(x), PIDTYPE_PID)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#define FIND_TASK_BY_PID(x) find_task_by_vpid(x)
#else
#define FIND_TASK_BY_PID(x) find_task_by_pid(x)
#endif

#define _WIDE(string)				L##string
#define WIDE(string)				_WIDE(string)

#define countof(a)					(sizeof(a) / sizeof(a[0]))

#define DRV_NAME          			"galcore"

#define GetPageCount(size, offset) 	((((size) + ((offset) & ~PAGE_CACHE_MASK)) + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT)

static inline gctINT
GetOrder(
	IN gctINT numPages
	)
{
    gctINT order = 0;

	while ((1 << order) <  numPages) order++;

	return order;
}

#endif /* __gc_hal_kernel_linux_h_ */

