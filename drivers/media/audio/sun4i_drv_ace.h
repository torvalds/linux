/*
 * drivers\media\audio\sun4i_drv_ace.h
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * huangxin <huangxin@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#ifndef __ACE_HAL_H__
#define	__ACE_HAL_H__
#include <linux/types.h>
#include <linux/clk.h>

// defule type of modules in ACE
typedef enum __ACE_MODULE_TYPE
{
    ACE_MODULE_NONE = 0,
    ACE_MODULE_CE,
    ACE_MODULE_AE,
    ACE_MODULE_PNG,
    ACE_MODULE_TSCC,
    ACE_MODULE_

}__ace_module_type_e;

typedef enum __ACE_REQUEST_MODE
{
    ACE_REQUEST_MODE_WAIT = 0,  //request hw with waiting mode
    ACE_REQUEST_MODE_NOWAIT,    //request hw with no-wait mode
    ACE_REQUEST_MODE_

} __ace_request_mode_e;

typedef enum __ACE_OPS
{
	ACE_DEV_HWREQ = 100,
	ACE_DEV_HWREL,
	ACE_DEV_GETCLKFREQ,
	ACE_DEV_GET_ADDR,
	ACE_DEV_INS_ISR,
	ACE_DEV_UNINS_ISR,
	ACE_DEV_WAIT_AE,
	ACE_DEV_CLK_OPEN,
	ACE_DEV_CLK_CLOSE,
	ACE_DEV_
}__ace_ops_e;

typedef struct ace_req{
	__ace_module_type_e module;
	__ace_request_mode_e mode;
	__u32 timeout;
}__ace_req_e;

__s32 ACE_Init(void);
__s32 ACE_Exit(void);
s32 ACE_HwReq(__ace_module_type_e module, __ace_request_mode_e mode, __u32 timeout);
__s32 ACE_HwRel(u32 hHWRes);
__u32 ACE_GetClk(void);

#endif	/* __ACE_HAL_H__ */
