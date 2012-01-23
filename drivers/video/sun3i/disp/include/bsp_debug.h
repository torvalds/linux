/*
*********************************************************************************************************
*                                                     eBase
*                                           the Abstract of Hardware
*
*                                   (c) Copyright 2006-2010, AW China
*                                               All Rights Reserved
*
* File        :  bsp_debug.h
* Date        :  2010-11-15
* Author      :  Victor
* Version     :  v1.00
* Description:
*                                   Operation for debug prinf
* History     :
*      <author>          <time>             <version >          <desc>
*       Victor         2010-11-15              1.0           create this file
*
*********************************************************************************************************
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
