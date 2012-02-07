/********************************************************************************
*********************************************************************************
			COPYRIGHT (c)   2004 BY ROCK-CHIP FUZHOU
				--  ALL RIGHTS RESERVED  --

File Name:  nand_config.h
Author:     RK28XX Driver Develop Group
Created:    25th OCT 2008
Modified:
Revision:   1.00
********************************************************************************
********************************************************************************/
#ifndef     _NAND_CONFIG_H
#define     _NAND_CONFIG_H
#define     DRIVERS_NAND
#define     LINUX

#include    <linux/kernel.h>
#include    <linux/string.h>
#include    <linux/sched.h>
#include    <linux/delay.h>
#include    <linux/irq.h>
#include    <mach/board.h>
#include    <mach/gpio.h>
#include    <asm/dma.h>
#include    "typedef.h"

#ifdef CONFIG_MACH_RK30_SDK
#include    <mach/io.h>
#include    <mach/irqs.h>
#else
#include    <mach/rk29_iomap.h>
#include    <mach/iomux.h>
#endif

#include    <linux/interrupt.h>
#include    "epphal.h"

//#include    "epphal.h"
#ifndef 	TRUE
#define 	TRUE    1
#endif

#ifndef	 	FALSE
#define 	FALSE   0
#endif

#ifndef	 	NULL
#define 	NULL   (void*)0
#endif

#include    "FTL_OSDepend.h"
#include    "flash.h"
#include    "ftl.h"

#ifdef CONFIG_MTD_NAND_RK29XX_DEBUG
#undef RKNAND_DEBUG
#define DEBUG_MSG
#define RKNAND_DEBUG(format, arg...) \
		printk(KERN_NOTICE format, ## arg);
#else
#undef RKNAND_DEBUG
#define RKNAND_DEBUG(n, arg...)
#endif

extern void rk29_power_reset(void);
extern void rkNand_cond_resched(void);

#define COND_RESCHED() rkNand_cond_resched()//cond_resched()

extern unsigned long rk_dma_mem_alloc(int size);
extern unsigned long rk_dma_mem_free(unsigned long buf);
#undef PRINTF
#define PRINTF RKNAND_DEBUG
#endif

