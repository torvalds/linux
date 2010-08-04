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
/* used by some plat-nomadik code */
#define io_p2v(n)		__io_address(n)

#include <mach/db8500-regs.h>
#include <mach/db5500-regs.h>

#ifdef CONFIG_UX500_SOC_DB8500
#define UX500(periph)		U8500_##periph##_BASE
#elif defined(CONFIG_UX500_SOC_DB5500)
#define UX500(periph)		U5500_##periph##_BASE
#endif

#define UX500_BACKUPRAM0_BASE	UX500(BACKUPRAM0)
#define UX500_BACKUPRAM1_BASE	UX500(BACKUPRAM1)
#define UX500_B2R2_BASE		UX500(B2R2)

#define UX500_CLKRST1_BASE	UX500(CLKRST1)
#define UX500_CLKRST2_BASE	UX500(CLKRST2)
#define UX500_CLKRST3_BASE	UX500(CLKRST3)
#define UX500_CLKRST5_BASE	UX500(CLKRST5)
#define UX500_CLKRST6_BASE	UX500(CLKRST6)

#define UX500_DMA_BASE		UX500(DMA)
#define UX500_FSMC_BASE		UX500(FSMC)

#define UX500_GIC_CPU_BASE	UX500(GIC_CPU)
#define UX500_GIC_DIST_BASE	UX500(GIC_DIST)

#define UX500_I2C1_BASE		UX500(I2C1)
#define UX500_I2C2_BASE		UX500(I2C2)
#define UX500_I2C3_BASE		UX500(I2C3)

#define UX500_L2CC_BASE		UX500(L2CC)
#define UX500_MCDE_BASE		UX500(MCDE)
#define UX500_MTU0_BASE		UX500(MTU0)
#define UX500_MTU1_BASE		UX500(MTU1)
#define UX500_PRCMU_BASE	UX500(PRCMU)

#define UX500_RNG_BASE		UX500(RNG)
#define UX500_RTC_BASE		UX500(RTC)

#define UX500_SCU_BASE		UX500(SCU)

#define UX500_SDI0_BASE		UX500(SDI0)
#define UX500_SDI1_BASE		UX500(SDI1)
#define UX500_SDI2_BASE		UX500(SDI2)
#define UX500_SDI3_BASE		UX500(SDI3)
#define UX500_SDI4_BASE		UX500(SDI4)

#define UX500_SPI0_BASE		UX500(SPI0)
#define UX500_SPI1_BASE		UX500(SPI1)
#define UX500_SPI2_BASE		UX500(SPI2)
#define UX500_SPI3_BASE		UX500(SPI3)

#define UX500_SIA_BASE		UX500(SIA)
#define UX500_SVA_BASE		UX500(SVA)

#define UX500_TWD_BASE		UX500(TWD)

#define UX500_UART0_BASE	UX500(UART0)
#define UX500_UART1_BASE	UX500(UART1)
#define UX500_UART2_BASE	UX500(UART2)

#define UX500_USBOTG_BASE	UX500(USBOTG)

/* ST-Ericsson modified pl022 id */
#define SSP_PER_ID		0x01080022

#ifndef __ASSEMBLY__

#include <asm/cputype.h>

static inline bool cpu_is_u8500(void)
{
#ifdef CONFIG_UX500_SOC_DB8500
	return 1;
#else
	return 0;
#endif
}

static inline bool cpu_is_u8500ed(void)
{
	return cpu_is_u8500() && (read_cpuid_id() & 15) == 0;
}

static inline bool cpu_is_u8500v1(void)
{
	return cpu_is_u8500() && (read_cpuid_id() & 15) == 1;
}

static inline bool cpu_is_u5500(void)
{
#ifdef CONFIG_UX500_SOC_DB5500
	return 1;
#else
	return 0;
#endif
}

#endif

#endif				/* __MACH_HARDWARE_H */
