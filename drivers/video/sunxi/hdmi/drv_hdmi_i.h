/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
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

#ifndef  _DRV_HDMI_I_H_
#define  _DRV_HDMI_I_H_

#include <linux/uaccess.h>
#include <asm/memory.h>
#include <linux/unistd.h>
#include "asm-generic/int-ll64.h"
#include "linux/kernel.h"
#include "linux/mm.h"
#include "linux/semaphore.h"
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h> /* wake_up_process() */
#include <linux/kthread.h> /* kthread_create(), kthread_run() */
#include <linux/err.h> /* IS_ERR(), PTR_ERR() */
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/i2c.h>

#include <video/sunxi_disp_ioctl.h>
#include <linux/drv_hdmi.h>

#define __inf(msg, ...) pr_debug("[DISP] " msg, ##__VA_ARGS__)
#define __wrn(msg, ...) pr_warn("[DISP] " msg, ##__VA_ARGS__)

__s32 Hdmi_init(struct platform_device *dev);
__s32 Hdmi_exit(struct platform_device *dev);

#endif
