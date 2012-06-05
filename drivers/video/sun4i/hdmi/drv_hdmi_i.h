/*
 * drivers/video/sun4i/hdmi/drv_hdmi_i.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Danling <danliang@allwinnertech.com>
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

#include <asm/uaccess.h>
#include <asm/memory.h>
#include <asm/unistd.h>
#include "asm-generic/int-ll64.h"
#include "linux/kernel.h"
#include "linux/mm.h"
#include "linux/semaphore.h"
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>   //wake_up_process()
#include <linux/kthread.h> //kthread_create()、kthread_run()
#include <linux/err.h> //IS_ERR()、PTR_ERR()
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

#include <linux/drv_display.h>
#include <linux/drv_hdmi.h>

#if 1
#define OSAL_PRINTF(msg...) {printk(KERN_WARNING "[HDMI] ");printk(msg);}
#define __inf(msg...)
#define __msg(msg...)
#define __wrn(msg...)       {printk(KERN_WARNING "[HDMI WRN] file:%s,line:%d:    ",__FILE__,__LINE__);printk(msg);}
#define __here__
#else
#define OSAL_PRINTF(msg...) {printk(KERN_WARNING "[HDMI] ");printk(msg);}
#define __inf(msg...)       {printk(KERN_WARNING "[HDMI] ");printk(msg);}
#define __msg(msg...)       {printk(KERN_WARNING "[HDMI] file:%s,line:%d:    ",__FILE__,__LINE__);printk(msg);}
#define __wrn(msg...)       {printk(KERN_WARNING "[HDMI WRN] file:%s,line:%d:    ",__FILE__,__LINE__);printk(msg);}
#define __here__            {printk(KERN_WARNING "[HDMI] file:%s,line:%d\n",__FILE__,__LINE__);}
#endif


__s32 Hdmi_init(void);
__s32 Hdmi_exit(void);

__s32 Hdmi_open(void);
__s32 Hdmi_close(void);
__s32 Hdmi_set_display_mode(__disp_tv_mode_t mode);
__s32 Hdmi_mode_support(__disp_tv_mode_t mode);
__s32 Hdmi_get_HPD_status(void);
__s32 Hdmi_Audio_Enable(__u8 mode, __u8 channel);
__s32 Hdmi_Set_Audio_Para(hdmi_audio_t * audio_para);


extern __s32 hdmi_i2c_add_driver(void);
extern __s32 hdmi_i2c_del_driver(void);

#define sys_get_wvalue(n)   (*((volatile __u32 *)(n)))          /* word input */
#define sys_put_wvalue(n,c) (*((volatile __u32 *)(n))  = (c))   /* word output */


#endif
