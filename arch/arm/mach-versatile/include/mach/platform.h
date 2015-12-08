/*
 * arch/arm/mach-versatile/include/mach/platform.h
 *
 * Copyright (c) ARM Limited 2003.  All rights reserved.
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

#ifndef __address_h
#define __address_h                     1

/*
 * Memory definitions
 */
#define VERSATILE_BOOT_ROM_LO          0x30000000		/* DoC Base (64Mb)...*/
#define VERSATILE_BOOT_ROM_HI          0x30000000
#define VERSATILE_BOOT_ROM_BASE        VERSATILE_BOOT_ROM_HI	 /*  Normal position */
#define VERSATILE_BOOT_ROM_SIZE        SZ_64M

#define VERSATILE_SSRAM_BASE           /* VERSATILE_SSMC_BASE ? */
#define VERSATILE_SSRAM_SIZE           SZ_2M

#define VERSATILE_FLASH_BASE           0x34000000
#define VERSATILE_FLASH_SIZE           SZ_64M

/* 
 *  SDRAM
 */
#define VERSATILE_SDRAM_BASE           0x00000000

/* 
 *  Logic expansion modules
 * 
 */


/* ------------------------------------------------------------------------
 *  Versatile Registers
 * ------------------------------------------------------------------------
 * 
 */
#define VERSATILE_SYS_ID_OFFSET               0x00
#define VERSATILE_SYS_SW_OFFSET               0x04
#define VERSATILE_SYS_LED_OFFSET              0x08
#define VERSATILE_SYS_OSC0_OFFSET             0x0C

#if defined(CONFIG_ARCH_VERSATILE_PB)
#define VERSATILE_SYS_OSC1_OFFSET             0x10
#define VERSATILE_SYS_OSC2_OFFSET             0x14
#define VERSATILE_SYS_OSC3_OFFSET             0x18
#define VERSATILE_SYS_OSC4_OFFSET             0x1C
#elif defined(CONFIG_MACH_VERSATILE_AB)
#define VERSATILE_SYS_OSC1_OFFSET             0x1C
#endif

#define VERSATILE_SYS_OSCCLCD_OFFSET          0x1c

#define VERSATILE_SYS_LOCK_OFFSET             0x20
#define VERSATILE_SYS_100HZ_OFFSET            0x24
#define VERSATILE_SYS_CFGDATA1_OFFSET         0x28
#define VERSATILE_SYS_CFGDATA2_OFFSET         0x2C
#define VERSATILE_SYS_FLAGS_OFFSET            0x30
#define VERSATILE_SYS_FLAGSSET_OFFSET         0x30
#define VERSATILE_SYS_FLAGSCLR_OFFSET         0x34
#define VERSATILE_SYS_NVFLAGS_OFFSET          0x38
#define VERSATILE_SYS_NVFLAGSSET_OFFSET       0x38
#define VERSATILE_SYS_NVFLAGSCLR_OFFSET       0x3C
#define VERSATILE_SYS_RESETCTL_OFFSET         0x40
#define VERSATILE_SYS_PCICTL_OFFSET           0x44
#define VERSATILE_SYS_MCI_OFFSET              0x48
#define VERSATILE_SYS_FLASH_OFFSET            0x4C
#define VERSATILE_SYS_CLCD_OFFSET             0x50
#define VERSATILE_SYS_CLCDSER_OFFSET          0x54
#define VERSATILE_SYS_BOOTCS_OFFSET           0x58
#define VERSATILE_SYS_24MHz_OFFSET            0x5C
#define VERSATILE_SYS_MISC_OFFSET             0x60
#define VERSATILE_SYS_TEST_OSC0_OFFSET        0x80
#define VERSATILE_SYS_TEST_OSC1_OFFSET        0x84
#define VERSATILE_SYS_TEST_OSC2_OFFSET        0x88
#define VERSATILE_SYS_TEST_OSC3_OFFSET        0x8C
#define VERSATILE_SYS_TEST_OSC4_OFFSET        0x90

#define VERSATILE_SYS_BASE                    0x10000000
#define VERSATILE_SYS_ID                      (VERSATILE_SYS_BASE + VERSATILE_SYS_ID_OFFSET)
#define VERSATILE_SYS_SW                      (VERSATILE_SYS_BASE + VERSATILE_SYS_SW_OFFSET)
#define VERSATILE_SYS_LED                     (VERSATILE_SYS_BASE + VERSATILE_SYS_LED_OFFSET)
#define VERSATILE_SYS_OSC0                    (VERSATILE_SYS_BASE + VERSATILE_SYS_OSC0_OFFSET)
#define VERSATILE_SYS_OSC1                    (VERSATILE_SYS_BASE + VERSATILE_SYS_OSC1_OFFSET)

#if defined(CONFIG_ARCH_VERSATILE_PB)
#define VERSATILE_SYS_OSC2                    (VERSATILE_SYS_BASE + VERSATILE_SYS_OSC2_OFFSET)
#define VERSATILE_SYS_OSC3                    (VERSATILE_SYS_BASE + VERSATILE_SYS_OSC3_OFFSET)
#define VERSATILE_SYS_OSC4                    (VERSATILE_SYS_BASE + VERSATILE_SYS_OSC4_OFFSET)
#endif

#define VERSATILE_SYS_LOCK                    (VERSATILE_SYS_BASE + VERSATILE_SYS_LOCK_OFFSET)
#define VERSATILE_SYS_100HZ                   (VERSATILE_SYS_BASE + VERSATILE_SYS_100HZ_OFFSET)
#define VERSATILE_SYS_CFGDATA1                (VERSATILE_SYS_BASE + VERSATILE_SYS_CFGDATA1_OFFSET)
#define VERSATILE_SYS_CFGDATA2                (VERSATILE_SYS_BASE + VERSATILE_SYS_CFGDATA2_OFFSET)
#define VERSATILE_SYS_FLAGS                   (VERSATILE_SYS_BASE + VERSATILE_SYS_FLAGS_OFFSET)
#define VERSATILE_SYS_FLAGSSET                (VERSATILE_SYS_BASE + VERSATILE_SYS_FLAGSSET_OFFSET)
#define VERSATILE_SYS_FLAGSCLR                (VERSATILE_SYS_BASE + VERSATILE_SYS_FLAGSCLR_OFFSET)
#define VERSATILE_SYS_NVFLAGS                 (VERSATILE_SYS_BASE + VERSATILE_SYS_NVFLAGS_OFFSET)
#define VERSATILE_SYS_NVFLAGSSET              (VERSATILE_SYS_BASE + VERSATILE_SYS_NVFLAGSSET_OFFSET)
#define VERSATILE_SYS_NVFLAGSCLR              (VERSATILE_SYS_BASE + VERSATILE_SYS_NVFLAGSCLR_OFFSET)
#define VERSATILE_SYS_RESETCTL                (VERSATILE_SYS_BASE + VERSATILE_SYS_RESETCTL_OFFSET)
#define VERSATILE_SYS_PCICTL                  (VERSATILE_SYS_BASE + VERSATILE_SYS_PCICTL_OFFSET)
#define VERSATILE_SYS_MCI                     (VERSATILE_SYS_BASE + VERSATILE_SYS_MCI_OFFSET)
#define VERSATILE_SYS_FLASH                   (VERSATILE_SYS_BASE + VERSATILE_SYS_FLASH_OFFSET)
#define VERSATILE_SYS_CLCD                    (VERSATILE_SYS_BASE + VERSATILE_SYS_CLCD_OFFSET)
#define VERSATILE_SYS_CLCDSER                 (VERSATILE_SYS_BASE + VERSATILE_SYS_CLCDSER_OFFSET)
#define VERSATILE_SYS_BOOTCS                  (VERSATILE_SYS_BASE + VERSATILE_SYS_BOOTCS_OFFSET)
#define VERSATILE_SYS_24MHz                   (VERSATILE_SYS_BASE + VERSATILE_SYS_24MHz_OFFSET)
#define VERSATILE_SYS_MISC                    (VERSATILE_SYS_BASE + VERSATILE_SYS_MISC_OFFSET)
#define VERSATILE_SYS_TEST_OSC0               (VERSATILE_SYS_BASE + VERSATILE_SYS_TEST_OSC0_OFFSET)
#define VERSATILE_SYS_TEST_OSC1               (VERSATILE_SYS_BASE + VERSATILE_SYS_TEST_OSC1_OFFSET)
#define VERSATILE_SYS_TEST_OSC2               (VERSATILE_SYS_BASE + VERSATILE_SYS_TEST_OSC2_OFFSET)
#define VERSATILE_SYS_TEST_OSC3               (VERSATILE_SYS_BASE + VERSATILE_SYS_TEST_OSC3_OFFSET)
#define VERSATILE_SYS_TEST_OSC4               (VERSATILE_SYS_BASE + VERSATILE_SYS_TEST_OSC4_OFFSET)

/* 
 * Values for VERSATILE_SYS_RESET_CTRL
 */
#define VERSATILE_SYS_CTRL_RESET_CONFIGCLR    0x01
#define VERSATILE_SYS_CTRL_RESET_CONFIGINIT   0x02
#define VERSATILE_SYS_CTRL_RESET_DLLRESET     0x03
#define VERSATILE_SYS_CTRL_RESET_PLLRESET     0x04
#define VERSATILE_SYS_CTRL_RESET_POR          0x05
#define VERSATILE_SYS_CTRL_RESET_DoC          0x06

#define VERSATILE_SYS_CTRL_LED         (1 << 0)


/* ------------------------------------------------------------------------
 *  Versatile control registers
 * ------------------------------------------------------------------------
 */

/* 
 * VERSATILE_IDFIELD
 *
 * 31:24 = manufacturer (0x41 = ARM)
 * 23:16 = architecture (0x08 = AHB system bus, ASB processor bus)
 * 15:12 = FPGA (0x3 = XVC600 or XVC600E)
 * 11:4  = build value
 * 3:0   = revision number (0x1 = rev B (AHB))
 */

/*
 * VERSATILE_SYS_LOCK
 *     control access to SYS_OSCx, SYS_CFGDATAx, SYS_RESETCTL, 
 *     SYS_CLD, SYS_BOOTCS
 */
#define VERSATILE_SYS_LOCK_LOCKED    (1 << 16)
#define VERSATILE_SYS_LOCKVAL_MASK	0xFFFF		/* write 0xA05F to enable write access */

/*
 * VERSATILE_SYS_FLASH
 */
#define VERSATILE_FLASHPROG_FLVPPEN	(1 << 0)	/* Enable writing to flash */

/*
 * VERSATILE_INTREG
 *     - used to acknowledge and control MMCI and UART interrupts 
 */
#define VERSATILE_INTREG_WPROT        0x00    /* MMC protection status (no interrupt generated) */
#define VERSATILE_INTREG_RI0          0x01    /* Ring indicator UART0 is asserted,              */
#define VERSATILE_INTREG_CARDIN       0x08    /* MMCI card in detect                            */
                                                /* write 1 to acknowledge and clear               */
#define VERSATILE_INTREG_RI1          0x02    /* Ring indicator UART1 is asserted,              */
#define VERSATILE_INTREG_CARDINSERT   0x03    /* Signal insertion of MMC card                   */

/*
 * VERSATILE peripheral addresses
 */
#define VERSATILE_MMCI0_BASE           0x10005000	/* MMC interface */
#define VERSATILE_MMCI1_BASE           0x1000B000    /* MMC Interface */
#define VERSATILE_CLCD_BASE            0x10120000	/* CLCD */
#define VERSATILE_SCTL_BASE            0x101E0000	/* System controller */
#define VERSATILE_IB2_BASE             0x24000000	/* IB2 module */

/*
 *  LED settings, bits [7:0]
 */
#define VERSATILE_SYS_LED0             (1 << 0)
#define VERSATILE_SYS_LED1             (1 << 1)
#define VERSATILE_SYS_LED2             (1 << 2)
#define VERSATILE_SYS_LED3             (1 << 3)
#define VERSATILE_SYS_LED4             (1 << 4)
#define VERSATILE_SYS_LED5             (1 << 5)
#define VERSATILE_SYS_LED6             (1 << 6)
#define VERSATILE_SYS_LED7             (1 << 7)

#define ALL_LEDS                  0xFF

#define LED_BANK                  VERSATILE_SYS_LED

/* 
 * Control registers
 */
#define VERSATILE_IDFIELD_OFFSET	0x0	/* Versatile build information */
#define VERSATILE_FLASHPROG_OFFSET	0x4	/* Flash devices */
#define VERSATILE_INTREG_OFFSET		0x8	/* Interrupt control */
#define VERSATILE_DECODE_OFFSET		0xC	/* Fitted logic modules */

/*
 * System controller bit assignment
 */
#define VERSATILE_REFCLK	0
#define VERSATILE_TIMCLK	1

#define VERSATILE_TIMER1_EnSel	15
#define VERSATILE_TIMER2_EnSel	17
#define VERSATILE_TIMER3_EnSel	19
#define VERSATILE_TIMER4_EnSel	21


/*
 * IB2 Versatile/AB expansion board definitions
 */
/* VICINTSOURCE27 */
#define VERSATILE_IB2_INT_BASE		(VERSATILE_IB2_BASE + 0x02000000)
#define VERSATILE_IB2_IER		(VERSATILE_IB2_INT_BASE + 0)
#define VERSATILE_IB2_ISR		(VERSATILE_IB2_INT_BASE + 4)

#define VERSATILE_IB2_CTL_BASE		(VERSATILE_IB2_BASE + 0x03000000)
#define VERSATILE_IB2_CTRL		(VERSATILE_IB2_CTL_BASE + 0)
#define VERSATILE_IB2_STAT		(VERSATILE_IB2_CTL_BASE + 4)

#endif
