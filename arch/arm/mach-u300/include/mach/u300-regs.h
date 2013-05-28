/*
 *
 * arch/arm/mach-u300/include/mach/u300-regs.h
 *
 *
 * Copyright (C) 2006-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * Basic register address definitions in physical memory and
 * some block definitions for core devices like the timer.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */

#ifndef __MACH_U300_REGS_H
#define __MACH_U300_REGS_H

/*
 * These are the large blocks of memory allocated for I/O.
 * the defines are used for setting up the I/O memory mapping.
 */

/* NAND Flash CS0 */
#define U300_NAND_CS0_PHYS_BASE		0x80000000

/* NFIF */
#define U300_NAND_IF_PHYS_BASE		0x9f800000

/* ALE, CLE offset for FSMC NAND */
#define PLAT_NAND_CLE			(1 << 16)
#define PLAT_NAND_ALE			(1 << 17)

/* AHB Peripherals */
#define U300_AHB_PER_PHYS_BASE		0xa0000000
#define U300_AHB_PER_VIRT_BASE		0xff010000

/* FAST Peripherals */
#define U300_FAST_PER_PHYS_BASE		0xc0000000
#define U300_FAST_PER_VIRT_BASE		0xff020000

/* SLOW Peripherals */
#define U300_SLOW_PER_PHYS_BASE		0xc0010000
#define U300_SLOW_PER_VIRT_BASE		0xff000000

/* Boot ROM */
#define U300_BOOTROM_PHYS_BASE		0xffff0000
#define U300_BOOTROM_VIRT_BASE		0xffff0000

/* SEMI config base */
#define U300_SEMI_CONFIG_BASE		0x2FFE0000

/*
 * AHB peripherals
 */

/* AHB Peripherals Bridge Controller */
#define U300_AHB_BRIDGE_BASE		(U300_AHB_PER_PHYS_BASE+0x0000)

/* Vectored Interrupt Controller 0, servicing 32 interrupts */
#define U300_INTCON0_BASE		(U300_AHB_PER_PHYS_BASE+0x1000)
#define U300_INTCON0_VBASE		IOMEM(U300_AHB_PER_VIRT_BASE+0x1000)

/* Vectored Interrupt Controller 1, servicing 32 interrupts */
#define U300_INTCON1_BASE		(U300_AHB_PER_PHYS_BASE+0x2000)
#define U300_INTCON1_VBASE		IOMEM(U300_AHB_PER_VIRT_BASE+0x2000)

/* Memory Stick Pro (MSPRO) controller */
#define U300_MSPRO_BASE			(U300_AHB_PER_PHYS_BASE+0x3000)

/* EMIF Configuration Area */
#define U300_EMIF_CFG_BASE		(U300_AHB_PER_PHYS_BASE+0x4000)


/*
 * FAST peripherals
 */

/* FAST bridge control */
#define U300_FAST_BRIDGE_BASE		(U300_FAST_PER_PHYS_BASE+0x0000)

/* MMC/SD controller */
#define U300_MMCSD_BASE			(U300_FAST_PER_PHYS_BASE+0x1000)

/* PCM I2S0 controller */
#define U300_PCM_I2S0_BASE		(U300_FAST_PER_PHYS_BASE+0x2000)

/* PCM I2S1 controller */
#define U300_PCM_I2S1_BASE		(U300_FAST_PER_PHYS_BASE+0x3000)

/* I2C0 controller */
#define U300_I2C0_BASE			(U300_FAST_PER_PHYS_BASE+0x4000)

/* I2C1 controller */
#define U300_I2C1_BASE			(U300_FAST_PER_PHYS_BASE+0x5000)

/* SPI controller */
#define U300_SPI_BASE			(U300_FAST_PER_PHYS_BASE+0x6000)

/* Fast UART1 on U335 only */
#define U300_UART1_BASE			(U300_FAST_PER_PHYS_BASE+0x7000)

/*
 * SLOW peripherals
 */

/* SLOW bridge control */
#define U300_SLOW_BRIDGE_BASE		(U300_SLOW_PER_PHYS_BASE)

/* SYSCON */
#define U300_SYSCON_BASE		(U300_SLOW_PER_PHYS_BASE+0x1000)
#define U300_SYSCON_VBASE		IOMEM(U300_SLOW_PER_VIRT_BASE+0x1000)

/* Watchdog */
#define U300_WDOG_BASE			(U300_SLOW_PER_PHYS_BASE+0x2000)

/* UART0 */
#define U300_UART0_BASE			(U300_SLOW_PER_PHYS_BASE+0x3000)

/* APP side special timer */
#define U300_TIMER_APP_BASE		(U300_SLOW_PER_PHYS_BASE+0x4000)
#define U300_TIMER_APP_VBASE		IOMEM(U300_SLOW_PER_VIRT_BASE+0x4000)

/* Keypad */
#define U300_KEYPAD_BASE		(U300_SLOW_PER_PHYS_BASE+0x5000)

/* GPIO */
#define U300_GPIO_BASE			(U300_SLOW_PER_PHYS_BASE+0x6000)

/* RTC */
#define U300_RTC_BASE			(U300_SLOW_PER_PHYS_BASE+0x7000)

/* Bus tracer */
#define U300_BUSTR_BASE			(U300_SLOW_PER_PHYS_BASE+0x8000)

/* Event handler (hardware queue) */
#define U300_EVHIST_BASE		(U300_SLOW_PER_PHYS_BASE+0x9000)

/* Genric Timer */
#define U300_TIMER_BASE			(U300_SLOW_PER_PHYS_BASE+0xa000)

/* PPM */
#define U300_PPM_BASE			(U300_SLOW_PER_PHYS_BASE+0xb000)


/*
 * REST peripherals
 */

/* ISP (image signal processor) */
#define U300_ISP_BASE			(0xA0008000)

/* DMA Controller base */
#define U300_DMAC_BASE			(0xC0020000)

/* MSL Base */
#define U300_MSL_BASE			(0xc0022000)

/* APEX Base */
#define U300_APEX_BASE			(0xc0030000)

/* Video Encoder Base */
#define U300_VIDEOENC_BASE		(0xc0080000)

/* XGAM Base */
#define U300_XGAM_BASE			(0xd0000000)

#endif
