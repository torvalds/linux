///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <IT6811.h>
//   @author Hermes.Wu@ite.com.tw
//   @date   2013/05/07
//   @fileversion: ITE_IT6811_6607_SAMPLE_1.06
//******************************************/
#ifndef _IT6681_ARCH_H_
#define _IT6681_ARCH_H_

//===============================================
// Detect Build environment
//===============================================
#define __ANDROID__
#if defined(__ANDROID__)
    #define ENV_ANDROID
    
#elif defined(__linux__)
    #define ENV_LINUX

#elif defined(__BORLANDC__)
    #define ENV_WINDOWS_BCB
    #pragma message("BCB")

#elif defined(__C51__)
    #define ENV_8051_KEIL_C

#else
    #define ENV_ANDROID

#endif
//===============================================


#if defined(ENV_ANDROID)

	#include <linux/module.h>
	#include <linux/delay.h>
	#include <linux/slab.h>
	#include <video/omapdss.h>
	#include <linux/i2c.h>
	#include <linux/printk.h>
	#include <linux/workqueue.h>
	#include <linux/kthread.h>
	#include <linux/input.h>
	#include <asm/atomic.h>
    #include <linux/it6681.h>

    #define _ITE_668X_DEMO_BOARD 0

    #define debug_6681	pr_err

    typedef char BOOL;
    typedef unsigned char BYTE, *PBYTE ;
    typedef short SHORT, *PSHORT ;
    typedef unsigned short USHORT, *PUSHORT ;
    typedef unsigned long ULONG, *PULONG ;

#elif defined(ENV_WINDOWS_BCB)

    #include <windows.h>
    #include <stdio.h>
    #include "it6681.h"

    #define _ITE_668X_DEMO_BOARD 0

    #define debug_6681	__myprintf
    #ifdef __cplusplus
    extern "C" {
    #endif
    void __myprintf( char *fmt, ... );
    #ifdef __cplusplus
    }
    #endif

#elif defined(ENV_8051_KEIL_C)

    #pragma message("ENV_8051_KEIL_C")

    #include <stdio.h>
    #include <string.h>
    //#include "mcu.h"
    //#include "io.h"
    //#include "Utility.h"
    //#include "It6681.h"
    #define const code
    #define mdelay(ms)  delay1ms(ms)
    #define mutex_unlock(x)
    #define mutex_lock(x)
    //#define IT6681_MHL_ADDR   0xC8

    #define _ITE_668X_DEMO_BOARD 1
    void it6681_copy_edid_ite_demo_board(void);

    #define debug_6681 printf
    //#define debug_6681 

    typedef char BOOL;
    typedef char CHAR, *PCHAR ;
    typedef unsigned char uchar, *puchar ;
    typedef unsigned char UCHAR, *PUCHAR ;
    typedef unsigned char byte, *pbyte ;
    typedef unsigned char BYTE, *PBYTE ;
    
    typedef short SHORT, *PSHORT ;
    
    typedef unsigned short USHORT, *PUSHORT ;
    typedef unsigned short word, *pword ;
    typedef unsigned short WORD, *PWORD ;
    
    typedef long LONG, *PLONG ;
    
    typedef unsigned long ULONG, *PULONG ;
    typedef unsigned long dword, *pdword ;
    typedef unsigned long DWORD, *PDWORD ;
    #define IT6681_EDID_MAX_BLOCKS 2

    #include "it6681.h"

#else

    #pragma error("No build environment was defined !")

#endif


#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/earlysuspend.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <asm/uaccess.h>
#include <mach/am_regs.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/input/mt.h>
#include <linux/vmalloc.h>
#include <linux/hrtimer.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/proc_fs.h>
#ifdef CONFIG_OF
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/of.h>
#else
#include <mach/gpio.h>
#include <mach/gpio_data.h>
#endif
#define GPIO_INT amlogic_gpio_name_map_num("GPIOZ_9")
#define GPIO_USB_MHL_SWITCH amlogic_gpio_name_map_num("NG")
#define GPIO_ENVBUS amlogic_gpio_name_map_num("NG")
#define GPIO_RESET amlogic_gpio_name_map_num("GPIOZ_10")

#define it6681_gpio_request(gpio,label) amlogic_gpio_request(gpio, label)
#define it6681_gpio_free(gpio) amlogic_gpio_free(gpio, "it6681")
#define it6681_gpio_direction_input(gpio) amlogic_gpio_direction_input(gpio, "it6681")
#define it6681_gpio_direction_output(gpio, val) amlogic_gpio_direction_output(gpio, val, "it6681")
#define it6681_gpio_get_value(gpio) amlogic_get_value(gpio, "it6681")
#define it6681_gpio_set_value(gpio,val) amlogic_set_value(gpio, val, "it6681")
#define it6681_gpio_to_irq(gpio, irq, irq_edge) 	amlogic_gpio_to_irq(gpio, "it6681", AML_GPIO_IRQ(irq,FILTER_NUM7,irq_edge))



#endif
