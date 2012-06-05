/*
 * drivers/video/sun3i/disp/include/bsp_debug.h
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

#ifndef  _BSP_DEBUG_H_
#define  _BSP_DEBUG_H_


/* OSAL提供的打印接口 */
extern  int  OSAL_printf( const char * str, ...);


#define bsp__msg(...)          (OSAL_printf(__VA_ARGS__))

#define bsp__wrn(...)    		(OSAL_printf("WRN:L%d(%s):", __LINE__, __FILE__),    \
							     OSAL_printf(__VA_ARGS__))

#define bsp__err(...)          (OSAL_printf("ERR:L%d(%s):", __LINE__, __FILE__),    \
    						     OSAL_printf(__VA_ARGS__))

/* 编译打印开关，4个等级 */
#define EBASE_BSP_DEBUG_LEVEL  1


#if(EBASE_BSP_DEBUG_LEVEL == 0)
#define  MSG_DBG(...)
#define  MSG_WRN(...)
#define  MSG_ERR(...)
#elif(EBASE_BSP_DEBUG_LEVEL == 1)
#define  MSG_DBG(...)
#define  MSG_WRN(...)
#define  MSG_ERR			bsp__err
#elif(EBASE_BSP_DEBUG_LEVEL == 2)
#define  MSG_DBG(...)
#define  MSG_WRN			bsp__wrn
#define  MSG_ERR			bsp__err
#elif(EBASE_BSP_DEBUG_LEVEL == 3)
#define  MSG_DBG			bsp__msg
#define  MSG_WRN			bsp__wrn
#define  MSG_ERR	       	bsp__err
#endif

#endif   //_BSP_DEBUG_H_
