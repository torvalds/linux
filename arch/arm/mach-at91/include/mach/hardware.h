/*
 * arch/arm/mach-at91/include/mach/hardware.h
 *
 *  Copyright (C) 2003 SAN People
 *  Copyright (C) 2003 ATMEL
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/sizes.h>

/* DBGU base */
/* rm9200, 9260/9g20, 9261/9g10, 9rl */
#define AT91_BASE_DBGU0	0xfffff200
/* 9263, 9g45, sama5d3 */
#define AT91_BASE_DBGU1	0xffffee00
/* sama5d4 */
#define AT91_BASE_DBGU2	0xfc069000

#if defined(CONFIG_ARCH_AT91X40)
#include <mach/at91x40.h>
#else
#include <mach/at91rm9200.h>
#include <mach/at91sam9260.h>
#include <mach/at91sam9261.h>
#include <mach/at91sam9263.h>
#include <mach/at91sam9rl.h>
#include <mach/at91sam9g45.h>
#include <mach/at91sam9x5.h>
#include <mach/at91sam9n12.h>
#include <mach/sama5d3.h>
#include <mach/sama5d4.h>

/*
 * On all at91 except rm9200 and x40 have the System Controller starts
 * at address 0xffffc000 and has a size of 16KiB.
 *
 * On rm9200 it's start at 0xfffe4000 of 111KiB with non reserved data starting
 * at 0xfffff000
 *
 * Removes the individual definitions of AT91_BASE_SYS and
 * replaces them with a common version at base 0xfffffc000 and size 16KiB
 * and map the same memory space
 */
#define AT91_BASE_SYS	0xffffc000

#endif

/*
 * On sama5d4 there is no system controller, we map some needed peripherals
 */
#define AT91_ALT_BASE_SYS	0xfc069000

/*
 * On all at91 have the Advanced Interrupt Controller starts at address
 * 0xfffff000 and the Power Management Controller starts at 0xfffffc00
 */
#define AT91_AIC	0xfffff000
#define AT91_PMC	0xfffffc00

/*
 * Peripheral identifiers/interrupts.
 */
#define AT91_ID_FIQ		0	/* Advanced Interrupt Controller (FIQ) */
#define AT91_ID_SYS		1	/* System Peripherals */

#ifdef CONFIG_MMU
/*
 * Remap the peripherals from address 0xFFF78000 .. 0xFFFFFFFF
 * to 0xFEF78000 .. 0xFF000000.  (544Kb)
 */
#define AT91_IO_PHYS_BASE	0xFFF78000
#define AT91_IO_VIRT_BASE	IOMEM(0xFF000000 - AT91_IO_SIZE)

/*
 * On sama5d4, remap the peripherals from address 0xFC069000 .. 0xFC06F000
 * to 0xFB069000 .. 0xFB06F000.  (24Kb)
 */
#define AT91_ALT_IO_PHYS_BASE	AT91_ALT_BASE_SYS
#define AT91_ALT_IO_VIRT_BASE	IOMEM(0xFB069000)
#else
/*
 * Identity mapping for the non MMU case.
 */
#define AT91_IO_PHYS_BASE	AT91_BASE_SYS
#define AT91_IO_VIRT_BASE	IOMEM(AT91_IO_PHYS_BASE)

#define AT91_ALT_IO_PHYS_BASE	AT91_ALT_BASE_SYS
#define AT91_ALT_IO_VIRT_BASE	IOMEM(AT91_ALT_BASE_SYS)
#endif

#define AT91_IO_SIZE		(0xFFFFFFFF - AT91_IO_PHYS_BASE + 1)

 /* Convert a physical IO address to virtual IO address */
#define AT91_IO_P2V(x)		((x) - AT91_IO_PHYS_BASE + AT91_IO_VIRT_BASE)
#define AT91_ALT_IO_P2V(x)	((x) - AT91_ALT_IO_PHYS_BASE + AT91_ALT_IO_VIRT_BASE)

/*
 * Virtual to Physical Address mapping for IO devices.
 */
#define AT91_VA_BASE_SYS	AT91_IO_P2V(AT91_BASE_SYS)
#define AT91_ALT_VA_BASE_SYS	AT91_ALT_IO_P2V(AT91_ALT_BASE_SYS)

 /* Internal SRAM is mapped below the IO devices */
#define AT91_SRAM_MAX		SZ_1M
#define AT91_VIRT_BASE		(AT91_IO_VIRT_BASE - AT91_SRAM_MAX)

/* External Memory Map */
#define AT91_CHIPSELECT_0	0x10000000
#define AT91_CHIPSELECT_1	0x20000000
#define AT91_CHIPSELECT_2	0x30000000
#define AT91_CHIPSELECT_3	0x40000000
#define AT91_CHIPSELECT_4	0x50000000
#define AT91_CHIPSELECT_5	0x60000000
#define AT91_CHIPSELECT_6	0x70000000
#define AT91_CHIPSELECT_7	0x80000000

/* Clocks */
#define AT91_SLOW_CLOCK		32768		/* slow clock */

/*
 * FIXME: this is needed to communicate between the pinctrl driver and
 * the PM implementation in the machine. Possibly part of the PM
 * implementation should be moved down into the pinctrl driver and get
 * called as part of the generic suspend/resume path.
 */
#ifndef __ASSEMBLY__
#ifdef CONFIG_PINCTRL_AT91
extern void at91_pinctrl_gpio_suspend(void);
extern void at91_pinctrl_gpio_resume(void);
#else
static inline void at91_pinctrl_gpio_suspend(void) {}
static inline void at91_pinctrl_gpio_resume(void) {}
#endif
#endif

#endif
