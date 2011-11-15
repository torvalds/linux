/*
**************************************************************************************************************
*											         eLDK
*						            the Easy Portable/Player Develop Kits
*									           desktop system
*
*						        	 (c) Copyright 2009-2012, ,HUANGXIN China
*											 All Rights Reserved
*
* File    	: sun4i_drv_ace.h
* By      	: HUANGXIN
* Func		:
* Version	: v1.0
* ============================================================================================================
* 2011-6-2 16:09:00  HUANGXIN create this file, implements the fundemental interface;
**************************************************************************************************************
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

/*
*********************************************************************************************************
*                                   ACE INIT
*
* Description: initialise ACE moudle, create the manager for resource management.
*
* Arguments  : none
*
* Returns    : result;
*               EPDK_OK   - init ACE successed;
*               EPDK_FAIL - init ACE failed;
*
* Note       : This funciton just create manager, without hardware operation;
*********************************************************************************************************
*/
__s32 ACE_Init(void);

/*
*********************************************************************************************************
*                                   ACE EXIT
*
* Description: exit ACE module, destroy resource manager;
*
* Arguments  : none
*
* Returns    : result;
*               EPDK_OK   - exit ace module successed;
*               EPDK_FAIL - exit ace module failed;
*
* Note       :
*********************************************************************************************************
*/
__s32 ACE_Exit(void);

/*
*********************************************************************************************************
*                                   REQUEST HARDWARE RESOURCE
*
* Description: require hardware resource.
*
* Arguments  : module   the hardware module which need be requested;
*              mode     mode of hardware module requested;
                            ACE_REQUEST_MODE_WAIT   - request hw resource with waiting mode;
                            ACE_REQUEST_MODE_NOWAIT - request hw resource with no-wait mode;
*              timeout  limitation of time out, just used under ACE_REQUEST_MODE_WAIT mode;
*
* Returns    : handle of hardware resource, NULL means request hw-resource failed;
*
* Note       :
*********************************************************************************************************
*/
s32 ACE_HwReq(__ace_module_type_e module, __ace_request_mode_e mode, __u32 timeout);

/*
*********************************************************************************************************
*                                   RELEASE HARDWARE RESOURCE
*
* Description: release hardware resource;
*
* Arguments  : moudle   the module need be released;
*
* Returns    : result;
*                   EPDK_OK,    release hardware resource successed;
*                   EPDK_FAIL,  release hardware resource failed;
*
* Note       :
*********************************************************************************************************
*/
__s32 ACE_HwRel(u32 hHWRes);

/*
*********************************************************************************************************
*                                       GET ACE MODULE CLOCK
*
* Description: This function Get ACE module clock;
*
* Arguments  :
*
* Returns    : nFreq    ce module clk freqrence value;
*********************************************************************************************************
*/
__u32 ACE_GetClk(void);

#endif	/* __ACE_HAL_H__ */

