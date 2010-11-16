/* linux/arch/arm/mach-s5pv310/include/mach/map.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PV310 - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H __FILE__

#include <plat/map-base.h>

/*
 * S5PV310 UART offset is 0x10000 but the older S5P SoCs are 0x400.
 * So need to define it, and here is to avoid redefinition warning.
 */
#define S3C_UART_OFFSET			(0x10000)

#include <plat/map-s5p.h>

#define S5PV310_PA_SYSRAM		(0x02025000)

#define S5PV310_PA_SROM_BANK(x)		(0x04000000 + ((x) * 0x01000000))

#define S5PC210_PA_ONENAND		(0x0C000000)
#define S5P_PA_ONENAND			S5PC210_PA_ONENAND

#define S5PC210_PA_ONENAND_DMA		(0x0C600000)
#define S5P_PA_ONENAND_DMA		S5PC210_PA_ONENAND_DMA

#define S5PV310_PA_CHIPID		(0x10000000)
#define S5P_PA_CHIPID			S5PV310_PA_CHIPID

#define S5PV310_PA_SYSCON		(0x10010000)
#define S5P_PA_SYSCON			S5PV310_PA_SYSCON

#define S5PV310_PA_CMU			(0x10030000)

#define S5PV310_PA_WATCHDOG		(0x10060000)
#define S5PV310_PA_RTC			(0x10070000)

#define S5PV310_PA_COMBINER		(0x10448000)

#define S5PV310_PA_COREPERI		(0x10500000)
#define S5PV310_PA_GIC_CPU		(0x10500100)
#define S5PV310_PA_TWD			(0x10500600)
#define S5PV310_PA_GIC_DIST		(0x10501000)
#define S5PV310_PA_L2CC			(0x10502000)

#define S5PV310_PA_GPIO1		(0x11400000)
#define S5PV310_PA_GPIO2		(0x11000000)
#define S5PV310_PA_GPIO3		(0x03860000)

#define S5PV310_PA_HSMMC(x)		(0x12510000 + ((x) * 0x10000))

#define S5PV310_PA_SROMC		(0x12570000)

#define S5PV310_PA_UART			(0x13800000)

#define S5P_PA_UART(x)			(S5PV310_PA_UART + ((x) * S3C_UART_OFFSET))
#define S5P_PA_UART0			S5P_PA_UART(0)
#define S5P_PA_UART1			S5P_PA_UART(1)
#define S5P_PA_UART2			S5P_PA_UART(2)
#define S5P_PA_UART3			S5P_PA_UART(3)
#define S5P_PA_UART4			S5P_PA_UART(4)

#define S5P_SZ_UART			SZ_256

#define S5PV310_PA_IIC(x)		(0x13860000 + ((x) * 0x10000))

#define S5PV310_PA_TIMER		(0x139D0000)
#define S5P_PA_TIMER			S5PV310_PA_TIMER

#define S5PV310_PA_SDRAM		(0x40000000)
#define S5P_PA_SDRAM			S5PV310_PA_SDRAM

/* compatibiltiy defines. */
#define S3C_PA_UART			S5PV310_PA_UART
#define S3C_PA_HSMMC0			S5PV310_PA_HSMMC(0)
#define S3C_PA_HSMMC1			S5PV310_PA_HSMMC(1)
#define S3C_PA_HSMMC2			S5PV310_PA_HSMMC(2)
#define S3C_PA_HSMMC3			S5PV310_PA_HSMMC(3)
#define S3C_PA_IIC			S5PV310_PA_IIC(0)
#define S3C_PA_IIC1			S5PV310_PA_IIC(1)
#define S3C_PA_IIC2			S5PV310_PA_IIC(2)
#define S3C_PA_IIC3			S5PV310_PA_IIC(3)
#define S3C_PA_IIC4			S5PV310_PA_IIC(4)
#define S3C_PA_IIC5			S5PV310_PA_IIC(5)
#define S3C_PA_IIC6			S5PV310_PA_IIC(6)
#define S3C_PA_IIC7			S5PV310_PA_IIC(7)
#define S3C_PA_RTC			S5PV310_PA_RTC
#define S3C_PA_WDT			S5PV310_PA_WATCHDOG

#endif /* __ASM_ARCH_MAP_H */
