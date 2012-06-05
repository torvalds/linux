/*
 * drivers/video/sun3i/disp/OSAL/OSAL.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef  __OSAL_H__
#define  __OSAL_H__

#include "linux/kernel.h"
#include "linux/mm.h"
#include <asm/uaccess.h>
#include <asm/memory.h>
#include <asm/unistd.h>
#include "linux/semaphore.h"
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/sched.h>   //wake_up_process()
#include <linux/kthread.h> //kthread_create()、kthread_run()
#include <linux/err.h> //IS_ERR()、PTR_ERR()
#include <linux/delay.h>
#include <linux/platform_device.h>
#include "asm-generic/int-ll64.h"

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cdev.h>


#include "../include/eBSP_common_inc.h"
#include "csp_include_all_para.h"



#include  "OSAL_Cache.h"
#include  "OSAL_Clock.h"
#include  "OSAL_Dma.h"
#include  "OSAL_Pin.h"
#include  "OSAL_Semi.h"
#include  "OSAL_Thread.h"
#include  "OSAL_Time.h"
#include  "OSAL_Lib_C.h"
#include  "OSAL_Int.h"
#include  "OSAL_IrqLock.h"

#endif   //__OSAL_H__


