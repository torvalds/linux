/*
*********************************************************************************************************
*											        eBase
*
*
*
*						        (c) Copyright 2006-2010, AW China
*											All	Rights Reserved
*
* File    		: 	_eBSP.h
* Date		:	2010-06-22
* By      		: 	holigun
* Version 		: 	V1.00
* Description	:	内部使用
*********************************************************************************************************
*/


#ifndef __EBSP_COMMON_INC_H__
#define __EBSP_COMMON_INC_H__

//#define ARM_GCC_COMPLIER

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

#define __bool signed char

#include "eBSP_basetype/ebase_common_inc.h"

#include "bsp_debug.h"
#include "ebsp_const.h"

#endif	//__EBSP_COMMON_INC_H__

