/*
 * drivers/video/sun3i/disp/OSAL/OSAL_Semi.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
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

