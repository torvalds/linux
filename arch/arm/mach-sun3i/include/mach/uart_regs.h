/*
 * arch/arm/mach-sun3i/include/mach/uart_regs.h
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

#ifndef _UART_REGS_
#define _UART_REGS_


#define PA_UARTS_BASE		0x01c21400
#define VA_UARTS_BASE		IO_ADDRESS(PA_UARTS_BASE)

#define UART_BASE0 0xf1c21000
#define UART_BASE1 0xf1c21400
#define UART_BASE2 0xf1c21800
#define UART_BASE3 0xf1c21c00


#endif    // #ifndef _UART_REGS_
