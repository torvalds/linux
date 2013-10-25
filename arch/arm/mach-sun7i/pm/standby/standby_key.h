/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        newbie Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : standby_key.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 15:16
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __STANDBY_KEY_H__
#define __STANDBY_KEY_H__

#include "standby_cfg.h"
//define key controller registers
typedef struct __STANDBY_KEY_REG
{
    // offset:0x00
    volatile __u32   Lradc_Ctrl;
    volatile __u32   Lradc_Intc;
    volatile __u32   Lradc_Ints;
    volatile __u32   Lradc_Data0;
    volatile __u32   Lradc_Data1;
} __standby_key_reg_t;

extern __s32 standby_key_init(void);
extern __s32 standby_key_exit(void);
extern __s32 standby_query_key(void);


#endif  /* __STANDBY_KEY_H__ */

