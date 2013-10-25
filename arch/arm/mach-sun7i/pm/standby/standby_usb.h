/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        newbie Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : standby_usb.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 15:17
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __STANDBY_USB_H__
#define __STANDBY_USB_H__

#include "standby_cfg.h"


extern __s32 standby_usb_init(void);
extern __s32 standby_usb_exit(void);
extern __s32 standby_query_usb_event(void);

#endif  /* __STANDBY_USB_H__ */

