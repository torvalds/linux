/*
 * drivers/usb/sunxi_usb/include/sw_usb_debug.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen <javen@allwinnertech.com>
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

#ifndef  __SW_USB_DEBUG_H__
#define  __SW_USB_DEBUG_H__

#ifdef  CONFIG_USB_SW_SUNXI_USB_DEBUG

#define  DMSG_PRINT(stuff...)		printk(stuff)

#define  DMSG_INFO_UDC(...)			(DMSG_PRINT("[sw_udc]: "), DMSG_PRINT(__VA_ARGS__))
#define  DMSG_INFO_HCD0(...)		(DMSG_PRINT("[sw_hcd0]: "), DMSG_PRINT(__VA_ARGS__))
#define  DMSG_INFO_HCD1(...)		(DMSG_PRINT("[sw_hcd1]: "), DMSG_PRINT(__VA_ARGS__))
#define  DMSG_INFO_HCD2(...)		(DMSG_PRINT("[sw_hcd2]: "), DMSG_PRINT(__VA_ARGS__))
#define  DMSG_INFO_MANAGER(...)		(DMSG_PRINT("[usb_manager]: "), DMSG_PRINT(__VA_ARGS__))

#else

#define  DMSG_PRINT(...)
#define  DMSG_INFO_UDC(...)
#define  DMSG_INFO_HCD0(...)
#define  DMSG_INFO_HCD1(...)
#define  DMSG_INFO_HCD2(...)
#define  DMSG_INFO_MANAGER(...)

#endif

#define  DMSG_PRINT_EX(stuff...)		printk(stuff)

#define  DMSG_ERR(...)        		(DMSG_PRINT_EX("WRN:L%d(%s):", __LINE__, __FILE__), DMSG_PRINT_EX(__VA_ARGS__))


/* 测试 */
#if  0
    #define DMSG_TEST         			DMSG_PRINT
#else
    #define DMSG_TEST(...)
#endif

/* 代码调试 */
#if  0
    #define DMSG_MANAGER_DEBUG          DMSG_PRINT
#else
    #define DMSG_MANAGER_DEBUG(...)
#endif

#if  0
    #define DMSG_DEBUG        			DMSG_PRINT
#else
    #define DMSG_DEBUG(...)
#endif

/* 普通信息打印 */
#if  1
    #define DMSG_INFO         			DMSG_PRINT
#else
    #define DMSG_INFO(...)
#endif

/* 严重警告 */
#if	1
    #define DMSG_PANIC        			DMSG_ERR
#else
    #define DMSG_PANIC(...)
#endif

/* 普通警告 */
#if	0
    #define DMSG_WRN        			DMSG_ERR
#else
    #define DMSG_WRN(...)
#endif

/* dma 调试打印 */
#if	0
    #define DMSG_DBG_DMA     			DMSG_PRINT
#else
    #define DMSG_DBG_DMA(...)
#endif

//---------------------------------------------------------------
//  函数 定义
//---------------------------------------------------------------
void print_usb_reg_by_ep(spinlock_t *lock, __u32 usbc_base, __s32 ep_index, char *str);
void print_all_usb_reg(spinlock_t *lock, __u32 usbc_base, __s32 ep_start, __u32 ep_end, char *str);

#endif   //__SW_USB_DEBUG_H__

