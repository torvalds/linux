/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef MSM_FB_DEF_H
#define MSM_FB_DEF_H

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include "msm_mdp.h"
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/debugfs.h>
#include <linux/console.h>

#include <linux/sched.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <mach/hardware.h>
#include <linux/io.h>
#include <asm/system.h>
#include <asm/mach-types.h>
#include <linux/platform_device.h>

typedef s64 int64;
typedef s32 int32;
typedef s16 int16;
typedef s8 int8;

typedef u64 uint64;
typedef u32 uint32;
typedef u16 uint16;
typedef u8 uint8;

typedef s32 int4;
typedef s16 int2;
typedef s8 int1;

typedef u32 uint4;
typedef u16 uint2;
typedef u8 uint1;

typedef u32 dword;
typedef u16 word;
typedef u8 byte;

typedef unsigned int boolean;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define MSM_FB_ENABLE_DBGFS
#define FEATURE_MDDI

#define outp32(addr, val) writel(val, addr)
#define outp16(addr, val) writew(val, addr)
#define outp8(addr, val) writeb(val, addr)
#define outp(addr, val) outp32(addr, val)

#ifndef MAX
#define  MAX( x, y ) (((x) > (y)) ? (x) : (y))
#endif

#ifndef MIN
#define  MIN( x, y ) (((x) < (y)) ? (x) : (y))
#endif

/*--------------------------------------------------------------------------*/

#define inp32(addr) readl(addr)
#define inp16(addr) readw(addr)
#define inp8(addr) readb(addr)
#define inp(addr) inp32(addr)

#define inpw(port)             readw(port)
#define outpw(port, val)       writew(val, port)
#define inpdw(port)            readl(port)
#define outpdw(port, val)      writel(val, port)


#define clk_busy_wait(x) msleep_interruptible((x)/1000)

#define memory_barrier()

#define assert(expr) \
	if(!(expr)) { \
		printk(KERN_ERR "msm_fb: assertion failed! %s,%s,%s,line=%d\n",\
			#expr, __FILE__, __func__, __LINE__); \
	}

#define ASSERT(x)   assert(x)

#define DISP_EBI2_LOCAL_DEFINE
#ifdef DISP_EBI2_LOCAL_DEFINE
#define LCD_PRIM_BASE_PHYS 0x98000000
#define LCD_SECD_BASE_PHYS 0x9c000000
#define EBI2_PRIM_LCD_RS_PIN 0x20000
#define EBI2_SECD_LCD_RS_PIN 0x20000

#define EBI2_PRIM_LCD_CLR 0xC0
#define EBI2_PRIM_LCD_SEL 0x40

#define EBI2_SECD_LCD_CLR 0x300
#define EBI2_SECD_LCD_SEL 0x100
#endif

extern u32 msm_fb_msg_level;

/*
 * Message printing priorities:
 * LEVEL 0 KERN_EMERG (highest priority)
 * LEVEL 1 KERN_ALERT
 * LEVEL 2 KERN_CRIT
 * LEVEL 3 KERN_ERR
 * LEVEL 4 KERN_WARNING
 * LEVEL 5 KERN_NOTICE
 * LEVEL 6 KERN_INFO
 * LEVEL 7 KERN_DEBUG (Lowest priority)
 */
#define MSM_FB_EMERG(msg, ...)    \
	if (msm_fb_msg_level > 0)  \
		printk(KERN_EMERG msg, ## __VA_ARGS__);
#define MSM_FB_ALERT(msg, ...)    \
	if (msm_fb_msg_level > 1)  \
		printk(KERN_ALERT msg, ## __VA_ARGS__);
#define MSM_FB_CRIT(msg, ...)    \
	if (msm_fb_msg_level > 2)  \
		printk(KERN_CRIT msg, ## __VA_ARGS__);
#define MSM_FB_ERR(msg, ...)    \
	if (msm_fb_msg_level > 3)  \
		printk(KERN_ERR msg, ## __VA_ARGS__);
#define MSM_FB_WARNING(msg, ...)    \
	if (msm_fb_msg_level > 4)  \
		printk(KERN_WARNING msg, ## __VA_ARGS__);
#define MSM_FB_NOTICE(msg, ...)    \
	if (msm_fb_msg_level > 5)  \
		printk(KERN_NOTICE msg, ## __VA_ARGS__);
#define MSM_FB_INFO(msg, ...)    \
	if (msm_fb_msg_level > 6)  \
		printk(KERN_INFO msg, ## __VA_ARGS__);
#define MSM_FB_DEBUG(msg, ...)    \
	if (msm_fb_msg_level > 7)  \
		printk(KERN_DEBUG msg, ## __VA_ARGS__);

#ifdef MSM_FB_C
unsigned char *msm_mdp_base;
unsigned char *msm_pmdh_base;
unsigned char *msm_emdh_base;
#else
extern unsigned char *msm_mdp_base;
extern unsigned char *msm_pmdh_base;
extern unsigned char *msm_emdh_base;
#endif

#endif /* MSM_FB_DEF_H */
