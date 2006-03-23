/*
 * Definitions for SBS Palomar IV board
 *
 * Author: Dan Cox
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __PPC_PLATFORMS_PAL4_H
#define __PPC_PLATFORMS_PAL4_H

#define PAL4_NVRAM             0xfffc0000
#define PAL4_NVRAM_SIZE        0x8000

#define PAL4_DRAM              0xfff80000
#define  PAL4_DRAM_BR_MASK     0xc0
#define  PAL4_DRAM_BR_SHIFT    6
#define  PAL4_DRAM_RESET       0x10
#define  PAL4_DRAM_EREADY      0x40

#define PAL4_MISC              0xfff80004
#define  PAL4_MISC_FB_MASK     0xc0
#define  PAL4_MISC_FLASH       0x20  /* StratFlash mapping: 1->0xff80, 0->0xfff0 */
#define  PAL4_MISC_MISC        0x08
#define  PAL4_MISC_BITF        0x02
#define  PAL4_MISC_NVKS        0x01

#define PAL4_L2                0xfff80008
#define  PAL4_L2_MASK          0x07

#define PAL4_PLDR              0xfff8000c

/* Only two Ethernet devices on the board... */
#define PAL4_ETH               31
#define PAL4_INTA              20

#endif /* __PPC_PLATFORMS_PAL4_H */
