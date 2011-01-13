/* linux/arch/arm/plat-s5p/include/plat/map-s5p.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_MAP_S5P_H
#define __ASM_PLAT_MAP_S5P_H __FILE__

#define S5P_VA_CHIPID		S3C_ADDR(0x02000000)
#define S5P_VA_CMU		S3C_ADDR(0x02100000)
#define S5P_VA_GPIO		S3C_ADDR(0x02200000)
#define S5P_VA_GPIO1		S5P_VA_GPIO
#define S5P_VA_GPIO2		S3C_ADDR(0x02240000)
#define S5P_VA_GPIO3		S3C_ADDR(0x02280000)

#define S5P_VA_SYSRAM		S3C_ADDR(0x02400000)
#define S5P_VA_DMC0		S3C_ADDR(0x02440000)
#define S5P_VA_DMC1		S3C_ADDR(0x02480000)
#define S5P_VA_SROMC		S3C_ADDR(0x024C0000)

#define S5P_VA_SYSTIMER		S3C_ADDR(0x02500000)
#define S5P_VA_L2CC		S3C_ADDR(0x02600000)

#define S5P_VA_COMBINER_BASE	S3C_ADDR(0x02700000)
#define S5P_VA_COMBINER(x)	(S5P_VA_COMBINER_BASE + ((x) >> 2) * 0x10)

#define S5P_VA_COREPERI_BASE	S3C_ADDR(0x02800000)
#define S5P_VA_COREPERI(x)	(S5P_VA_COREPERI_BASE + (x))
#define S5P_VA_SCU		S5P_VA_COREPERI(0x0)
#define S5P_VA_GIC_CPU		S5P_VA_COREPERI(0x100)
#define S5P_VA_TWD		S5P_VA_COREPERI(0x600)
#define S5P_VA_GIC_DIST		S5P_VA_COREPERI(0x1000)

#define S3C_VA_USB_HSPHY	S3C_ADDR(0x02900000)

#define VA_VIC(x)		(S3C_VA_IRQ + ((x) * 0x10000))
#define VA_VIC0			VA_VIC(0)
#define VA_VIC1			VA_VIC(1)
#define VA_VIC2			VA_VIC(2)
#define VA_VIC3			VA_VIC(3)

#define S5P_VA_UART(x)		(S3C_VA_UART + ((x) * S3C_UART_OFFSET))
#define S5P_VA_UART0		S5P_VA_UART(0)
#define S5P_VA_UART1		S5P_VA_UART(1)
#define S5P_VA_UART2		S5P_VA_UART(2)
#define S5P_VA_UART3		S5P_VA_UART(3)

#ifndef S3C_UART_OFFSET
#define S3C_UART_OFFSET		(0x400)
#endif

#endif /* __ASM_PLAT_MAP_S5P_H */
