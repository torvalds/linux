/*
 *  arch/arm/mach-clps711x/include/mach/hardware.h
 *
 *  This file contains the hardware definitions of the Prospector P720T.
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H


#define CLPS7111_VIRT_BASE	0xff000000
#define CLPS7111_BASE		CLPS7111_VIRT_BASE

/*
 * The physical addresses that the external chip select signals map to is
 * dependent on the setting of the nMEDCHG signal on EP7211 and EP7212
 * processors.  CONFIG_EP72XX_BOOT_ROM is only available if these
 * processors are in use.
 */
#ifndef CONFIG_EP72XX_ROM_BOOT
#define CS0_PHYS_BASE		(0x00000000)
#define CS1_PHYS_BASE		(0x10000000)
#define CS2_PHYS_BASE		(0x20000000)
#define CS3_PHYS_BASE		(0x30000000)
#define CS4_PHYS_BASE		(0x40000000)
#define CS5_PHYS_BASE		(0x50000000)
#define CS6_PHYS_BASE		(0x60000000)
#define CS7_PHYS_BASE		(0x70000000)
#else
#define CS0_PHYS_BASE		(0x70000000)
#define CS1_PHYS_BASE		(0x60000000)
#define CS2_PHYS_BASE		(0x50000000)
#define CS3_PHYS_BASE		(0x40000000)
#define CS4_PHYS_BASE		(0x30000000)
#define CS5_PHYS_BASE		(0x20000000)
#define CS6_PHYS_BASE		(0x10000000)
#define CS7_PHYS_BASE		(0x00000000)
#endif

#if defined (CONFIG_ARCH_EP7211)

#define EP7211_VIRT_BASE	CLPS7111_VIRT_BASE
#define EP7211_BASE		CLPS7111_VIRT_BASE
#include <asm/hardware/ep7211.h>

#elif defined (CONFIG_ARCH_EP7212)

#define EP7212_VIRT_BASE	CLPS7111_VIRT_BASE
#define EP7212_BASE		CLPS7111_VIRT_BASE
#include <asm/hardware/ep7212.h>

#endif

#define SYSPLD_VIRT_BASE	0xfe000000
#define SYSPLD_BASE		SYSPLD_VIRT_BASE

#ifndef __ASSEMBLER__

#define PCIO_BASE		IO_BASE

#endif


#if  defined (CONFIG_ARCH_AUTCPU12)

#define  CS89712_VIRT_BASE	CLPS7111_VIRT_BASE
#define  CS89712_BASE		CLPS7111_VIRT_BASE

#include <asm/hardware/clps7111.h>
#include <asm/hardware/ep7212.h>
#include <asm/hardware/cs89712.h>

#endif


#if defined (CONFIG_ARCH_CDB89712)

#include <asm/hardware/clps7111.h>
#include <asm/hardware/ep7212.h>
#include <asm/hardware/cs89712.h>

/* static cdb89712_map_io() areas */
#define REGISTER_START   0x80000000
#define REGISTER_SIZE    0x4000
#define REGISTER_BASE    0xff000000

#define ETHER_START      0x20000000
#define ETHER_SIZE       0x1000
#define ETHER_BASE       0xfe000000

#endif


#if defined (CONFIG_ARCH_EDB7211)

/*
 * The extra 8 lines of the keyboard matrix are wired to chip select 3 (nCS3) 
 * and repeat across it. This is the mapping for it.
 *
 * In jumpered boot mode, nCS3 is mapped to 0x4000000, not 0x3000000. This 
 * was cause for much consternation and headscratching. This should probably
 * be made a compile/run time kernel option.
 */
#define EP7211_PHYS_EXTKBD		CS3_PHYS_BASE	/* physical */

#define EP7211_VIRT_EXTKBD		(0xfd000000)	/* virtual */


/*
 * The CS8900A ethernet chip has its I/O registers wired to chip select 2 
 * (nCS2). This is the mapping for it.
 *
 * In jumpered boot mode, nCS2 is mapped to 0x5000000, not 0x2000000. This 
 * was cause for much consternation and headscratching. This should probably
 * be made a compile/run time kernel option.
 */
#define EP7211_PHYS_CS8900A		CS2_PHYS_BASE	/* physical */

#define EP7211_VIRT_CS8900A		(0xfc000000)	/* virtual */


/*
 * The two flash banks are wired to chip selects 0 and 1. This is the mapping
 * for them.
 *
 * nCS0 and nCS1 are at 0x70000000 and 0x60000000, respectively, when running
 * in jumpered boot mode.
 */
#define EP7211_PHYS_FLASH1		CS0_PHYS_BASE	/* physical */
#define EP7211_PHYS_FLASH2		CS1_PHYS_BASE	/* physical */

#define EP7211_VIRT_FLASH1		(0xfa000000)	/* virtual */
#define EP7211_VIRT_FLASH2		(0xfb000000)	/* virtual */

#endif /* CONFIG_ARCH_EDB7211 */


/*
 * Relevant bits in port D, which controls power to the various parts of
 * the LCD on the EDB7211.
 */
#define EDB_PD1_LCD_DC_DC_EN	(1<<1)
#define EDB_PD2_LCDEN		(1<<2)
#define EDB_PD3_LCDBL		(1<<3)


#if defined (CONFIG_ARCH_CEIVA)

#define  CEIVA_VIRT_BASE	CLPS7111_VIRT_BASE
#define  CEIVA_BASE		CLPS7111_VIRT_BASE

#include <asm/hardware/clps7111.h>
#include <asm/hardware/ep7212.h>


/*
 * The two flash banks are wired to chip selects 0 and 1. This is the mapping
 * for them.
 *
 * nCS0 and nCS1 are at 0x70000000 and 0x60000000, respectively, when running
 * in jumpered boot mode.
 */
#define CEIVA_PHYS_FLASH1	CS0_PHYS_BASE	/* physical */
#define CEIVA_PHYS_FLASH2	CS1_PHYS_BASE	/* physical */

#define CEIVA_VIRT_FLASH1	(0xfa000000)	/* virtual */
#define CEIVA_VIRT_FLASH2	(0xfb000000)	/* virtual */

#define CEIVA_FLASH_SIZE        0x100000
#define CEIVA_FLASH_WIDTH       2

/*
 * SED1355 LCD controller
 */
#define CEIVA_PHYS_SED1355	CS2_PHYS_BASE
#define CEIVA_VIRT_SED1355	(0xfc000000)

/*
 * Relevant bits in port D, which controls power to the various parts of
 * the LCD on the Ceiva Photo Max, and reset to the LCD controller.
 */

// Reset line to SED1355 (must be high to operate)
#define CEIVA_PD1_LCDRST	(1<<1)
// LCD panel enable (set to one, to enable LCD)
#define CEIVA_PD4_LCDEN		(1<<4)
// Backlight (set to one, to turn on backlight
#define CEIVA_PD5_LCDBL		(1<<5)

/*
 * Relevant bits in port B, which report the status of the buttons.
 */

// White button
#define CEIVA_PB4_WHT_BTN	(1<<4)
// Black button
#define CEIVA_PB0_BLK_BTN	(1<<0)
#endif // #if defined (CONFIG_ARCH_CEIVA)

#endif
