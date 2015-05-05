/* timskmod.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __TIMSKMOD_H__
#define __TIMSKMOD_H__

#include <linux/version.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <asm/irq.h>
#include <linux/io.h>
#include <asm/dma.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/poll.h>
/* #define EXPORT_SYMTAB */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fcntl.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/seq_file.h>
#include <linux/mm.h>

#endif
