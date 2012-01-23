/*
*********************************************************************************************************
*                                                    ePDK
*                                    the Easy Portable/Player Develop Kits
*                                                standby module
*
*                                   (c) Copyright 2008-2009, kevin China
*                                            All Rights Reserved
*
* File    : standby_twi.h
* By      : kevin
* Version : V1.0
* Date    : 2009-6-16 17:07
*********************************************************************************************************
*/
#ifndef _STANDBY_TWI_H_
#define _STANDBY_TWI_H_

#include "ePDK.h"
#include "standby_cfg.h"
#include "standby_reg.h"



#define TWI_OP_RD                   (0)
#define TWI_OP_WR                   (1)


extern __s32 standby_twi_init(void);
extern __s32 standby_twi_exit(void);
extern __s32 twi_byte_rw(__s32 op_type, __u8 saddr, __u8 baddr, __u8 *data);

#endif  //_STANDBY_TWI_H_

