/*
 * drivers/video/sun4i/disp/OSAL/OSAL_Int.c
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



#include "OSAL.h"

/*
*******************************************************************************
*                     OSAL_RegISR
*
* Description:
*    注册中断服务程序
*
* Parameters:
*    irqno    	    ：input.  中断号
*    flags    	    ：input.  中断类型，默认值为0。
*    Handler  	    ：input.  中断处理程序入口，或者中断事件句柄
*    pArg 	        ：input.  参数
*    DataSize 	    ：input.  参数的长度
*    prio	        ：input.  中断优先级

*
* Return value:
*     返回成功或者失败。
*
* note:
*    中断处理函数原型，typedef __s32 (*ISRCallback)( void *pArg)。
*
*******************************************************************************
*/
int OSAL_RegISR(__u32 IrqNo, __u32 Flags,ISRCallback Handler,void *pArg,__u32 DataSize,__u32 Prio)
{
    return request_irq(IrqNo, (irq_handler_t)Handler, IRQF_DISABLED, "dev_name", pArg);
}

/*
*******************************************************************************
*                     OSAL_UnRegISR
*
* Description:
*    注销中断服务程序
*
* Parameters:
*    irqno    	：input.  中断号
*    handler  	：input.  中断处理程序入口，或者中断事件句柄
*    Argment 	：input.  参数
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void OSAL_UnRegISR(__u32 IrqNo, ISRCallback Handler, void *pArg)
{
    free_irq(IrqNo, pArg);
}

/*
*******************************************************************************
*                     OSAL_InterruptEnable
*
* Description:
*    中断使能
*
* Parameters:
*    irqno ：input.  中断号
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void OSAL_InterruptEnable(__u32 IrqNo)
{
    enable_irq(IrqNo);
}

/*
*******************************************************************************
*                     OSAL_InterruptDisable
*
* Description:
*    中断禁止
*
* Parameters:
*     irqno ：input.  中断号
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void OSAL_InterruptDisable(__u32 IrqNo)
{
    disable_irq(IrqNo);
}

