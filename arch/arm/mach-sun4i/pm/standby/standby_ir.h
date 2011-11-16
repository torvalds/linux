/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : standby_ir.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 15:15
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/

#ifndef __STANDBY_IR_H__
#define __STANDBY_IR_H__

#include "standby_cfg.h"

extern __s32 standby_ir_init(void);
extern __s32 standby_ir_exit(void);
extern __s32 standby_ir_detect(void);
extern __s32 standby_ir_verify(void);

#endif  /*__STANDBY_IR_H__*/
