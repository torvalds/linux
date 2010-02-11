/*
 * Copyright (C) 2009 ST-Ericsson.
 *
 * U8500 hardware definitions
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef __MACH_HARDWARE_H
#define __MACH_HARDWARE_H

/* macros to get at IO space when running virtually
 * We dont map all the peripherals, let ioremap do
 * this for us. We map only very basic peripherals here.
 */
#define U8500_IO_VIRTUAL	0xf0000000
#define U8500_IO_PHYSICAL	0xa0000000

/* this macro is used in assembly, so no cast */
#define IO_ADDRESS(x)           \
	(((x) & 0x0fffffff) + (((x) >> 4) & 0x0f000000) + U8500_IO_VIRTUAL)

/* typesafe io address */
#define __io_address(n)		__io(IO_ADDRESS(n))

/*
 * Base address definitions for U8500 Onchip IPs. All the
 * peripherals are contained in a single 1 Mbyte region, with
 * AHB peripherals at the bottom and APB peripherals at the
 * top of the region. PER stands for PERIPHERAL region which
 * itself divided into sub regions.
 */
#define U8500_PER3_BASE		0x80000000
#define U8500_PER2_BASE		0x80110000
#define U8500_PER1_BASE		0x80120000
#define U8500_PER4_BASE		0x80150000

#define U8500_PER6_BASE		0xa03c0000
#define U8500_PER5_BASE		0xa03e0000
#define U8500_PER7_BASE		0xa03d0000

#define U8500_SVA_BASE		0xa0100000
#define U8500_SIA_BASE		0xa0200000

#define U8500_SGA_BASE		0xa0300000
#define U8500_MCDE_BASE		0xa0350000
#define U8500_DMA_BASE		0xa0362000

#define U8500_SCU_BASE		0xa0410000
#define U8500_GIC_CPU_BASE	0xa0410100
#define U8500_TWD_BASE		0xa0410600
#define U8500_GIC_DIST_BASE	0xa0411000
#define U8500_L2CC_BASE		0xa0412000

#define U8500_TWD_SIZE		0x100

/* per7 base addressess */
#define U8500_CR_BASE		(U8500_PER7_BASE + 0x8000)
#define U8500_MTU0_BASE		(U8500_PER7_BASE + 0xa000)
#define U8500_MTU1_BASE		(U8500_PER7_BASE + 0xb000)
#define U8500_TZPC0_BASE	(U8500_PER7_BASE + 0xc000)
#define U8500_CLKRST7_BASE	(U8500_PER7_BASE + 0xf000)

/* per6 base addressess */
#define U8500_RNG_BASE		(U8500_PER6_BASE + 0x0000)
#define U8500_PKA_BASE		(U8500_PER6_BASE + 0x1000)
#define U8500_PKAM_BASE		(U8500_PER6_BASE + 0x2000)
#define U8500_CRYPTO0_BASE	(U8500_PER6_BASE + 0xa000)
#define U8500_CRYPTO1_BASE	(U8500_PER6_BASE + 0xb000)
#define U8500_CLKRST6_BASE	(U8500_PER7_BASE + 0xf000)

/* per5 base addressess */
#define U8500_USBOTG_BASE	(U8500_PER5_BASE + 0x00000)
#define U8500_GPIO5_BASE	(U8500_PER5_BASE + 0x1e000)
#define U8500_CLKRST5_BASE	(U8500_PER7_BASE + 0x1f000)

/* per4 base addressess */
#define U8500_BACKUPRAM0_BASE	(U8500_PER4_BASE + 0x0000)
#define U8500_BACKUPRAM1_BASE	(U8500_PER4_BASE + 0x1000)
#define U8500_RTT0_BASE		(U8500_PER4_BASE + 0x2000)
#define U8500_RTT1_BASE		(U8500_PER4_BASE + 0x3000)
#define U8500_RTC_BASE		(U8500_PER4_BASE + 0x4000)
#define U8500_SCR_BASE		(U8500_PER4_BASE + 0x5000)
#define U8500_DMC_BASE		(U8500_PER4_BASE + 0x6000)
#define U8500_PRCMU_BASE	(U8500_PER4_BASE + 0x7000)

/* per3 base addressess */
#define U8500_FSMC_BASE		(U8500_PER3_BASE + 0x0000)
#define U8500_SSP0_BASE		(U8500_PER3_BASE + 0x2000)
#define U8500_SSP1_BASE		(U8500_PER3_BASE + 0x3000)
#define U8500_I2C0_BASE		(U8500_PER3_BASE + 0x4000)
#define U8500_SDI2_BASE		(U8500_PER3_BASE + 0x5000)
#define U8500_SKE_BASE		(U8500_PER3_BASE + 0x6000)
#define U8500_UART2_BASE	(U8500_PER3_BASE + 0x7000)
#define U8500_SDI5_BASE		(U8500_PER3_BASE + 0x8000)
#define U8500_GPIO3_BASE	(U8500_PER3_BASE + 0xe000)
#define U8500_CLKRST3_BASE	(U8500_PER7_BASE + 0xf000)

/* per2 base addressess */
#define U8500_I2C3_BASE		(U8500_PER2_BASE + 0x0000)
#define U8500_SPI2_BASE		(U8500_PER2_BASE + 0x1000)
#define U8500_SPI1_BASE		(U8500_PER2_BASE + 0x2000)
#define U8500_PWL_BASE		(U8500_PER2_BASE + 0x3000)
#define U8500_SDI4_BASE		(U8500_PER2_BASE + 0x4000)
#define U8500_MSP2_BASE		(U8500_PER2_BASE + 0x7000)
#define U8500_SDI1_BASE		(U8500_PER2_BASE + 0x8000)
#define U8500_SDI3_BASE		(U8500_PER2_BASE + 0x9000)
#define U8500_SPI0_BASE		(U8500_PER2_BASE + 0xa000)
#define U8500_HSIR_BASE		(U8500_PER2_BASE + 0xb000)
#define U8500_HSIT_BASE		(U8500_PER2_BASE + 0xc000)
#define U8500_GPIO2_BASE	(U8500_PER2_BASE + 0xe000)
#define U8500_CLKRST2_BASE	(U8500_PER2_BASE + 0xf000)

/* per1 base addresses */
#define U8500_UART0_BASE	(U8500_PER1_BASE + 0x0000)
#define U8500_UART1_BASE	(U8500_PER1_BASE + 0x1000)
#define U8500_I2C1_BASE		(U8500_PER1_BASE + 0x2000)
#define U8500_MSP0_BASE		(U8500_PER1_BASE + 0x3000)
#define U8500_MSP1_BASE		(U8500_PER1_BASE + 0x4000)
#define U8500_SDI0_BASE		(U8500_PER1_BASE + 0x6000)
#define U8500_I2C2_BASE		(U8500_PER1_BASE + 0x8000)
#define U8500_SPI3_BASE		(U8500_PER1_BASE + 0x9000)
#define U8500_SLIM0_BASE	(U8500_PER1_BASE + 0xa000)
#define U8500_GPIO1_BASE	(U8500_PER1_BASE + 0xe000)
#define U8500_CLKRST1_BASE	(U8500_PER2_BASE + 0xf000)

/* ST-Ericsson modified pl022 id */
#define SSP_PER_ID		0x01080022

#endif				/* __MACH_HARDWARE_H */
