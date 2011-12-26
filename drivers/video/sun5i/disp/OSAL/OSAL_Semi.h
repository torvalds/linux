/*
*************************************************************************************
*                         			eBsp
*					   Operation System Adapter Layer
*
*				(c) Copyright 2006-2010, All winners Co,Ld.
*							All	Rights Reserved
*
* File Name 	: OSAL_Semi.h
*
* Author 		: javen
*
* Description 	: 信号量操作
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	   2010-09-07          1.0         create this word
*
*************************************************************************************
*/
#ifndef  __OSAL_SEMI_H__
#define  __OSAL_SEMI_H__


typedef void*  OSAL_SemHdle;

/*
*******************************************************************************
*                     eBase_CreateSemaphore
*
* Description:
*    创建信号量
*
* Parameters:
*    Count  :  input.  信号量的初始值。
* 
* Return value:
*    成功，返回信号量句柄。失败，返回NULL。
*
* note:
*    void
*
*******************************************************************************
*/
OSAL_SemHdle OSAL_CreateSemaphore(__u32 Count);

/*
*******************************************************************************
*                     OSAL_DeleteSemaphore
*
* Description:
*    删除信号量
*
* Parameters:
*    SemHdle  :  input.  OSAL_CreateSemaphore 申请的 信号量句柄
* 
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void OSAL_DeleteSemaphore(OSAL_SemHdle SemHdle);

/*
*******************************************************************************
*                     OSAL_SemPend
*
* Description:
*    锁信号量
*
* Parameters:
*    SemHdle  :  input.  OSAL_CreateSemaphore 申请的 信号量句柄
* 
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void OSAL_SemPend(OSAL_SemHdle SemHdle, __u16 TimeOut);

/*
*******************************************************************************
*                     OSAL_SemPost
*
* Description:
*    信号量解锁
*
* Parameters:
*    SemHdle  :  input.  OSAL_CreateSemaphore 申请的 信号量句柄
* 
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void OSAL_SemPost(OSAL_SemHdle SemHdle);


#endif   //__OSAL_SEMI_H__

