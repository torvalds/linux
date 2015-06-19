/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2015 Imagination Technologies, Inc.
 *   written by Ralf Baechle <ralf@linux-mips.org>
 */
#ifndef __ASM_MIPS_BOARDS_SEAD3_ADDR_H
#define __ASM_MIPS_BOARDS_SEAD3_ADDR_H

/*
 * Target #0 Register Decode
 */
#define SEAD3_SD_SPDCNF				0xbb000040
#define SEAD3_SD_SPADDR				0xbb000048
#define SEAD3_SD_DATA				0xbb000050

/*
 * Target #1 Register Decode
 */
#define SEAD3_CFG				0xbb100110
#define SEAD3_GIC_BASE_ADDRESS			0xbb1c0000
#define SEAD3_SHARED_SECTION			0xbb1c0000
#define SEAD3_VPE_LOCAL_SECTION			0xbb1c8000
#define SEAD3_VPE_OTHER_SECTION			0xbb1cc000
#define SEAD3_USER_MODE_VISIBLE_SECTION		0xbb1d0000

/*
 * Target #3 Register Decode
 */
#define SEAD3_USB_HS_BASE			0xbb200000
#define SEAD3_USB_HS_IDENTIFICATION_REGS	0xbb200000
#define SEAD3_USB_HS_CAPABILITY_REGS		0xbb200100
#define SEAD3_USB_HS_OPERATIONAL_REGS		0xbb200140
#define SEAD3_RESERVED				0xbe800000

/*
 * Target #3 Register Decode
 */
#define SEAD3_SRAM				0xbe000000
#define SEAD3_OPTIONAL_SRAM			0xbe400000
#define SEAD3_FPGA				0xbf000000

#define SEAD3_PI_PIC32_USB_STATUS		0xbf000060
#define   SEAD3_PI_PIC32_USB_STATUS_IO_RDY	(1 << 0)
#define   SEAD3_PI_PIC32_USB_STATUS_SPL_INT	(1 << 1)
#define   SEAD3_PI_PIC32_USB_STATUS_GPIOA_INT	(1 << 2)
#define   SEAD3_PI_PIC32_USB_STATUS_GPIOB_INT	(1 << 3)

#define SEAD3_PI_SOFT_ENDIAN			0xbf000070

#define SEAD3_CPLD_P_SWITCH			0xbf000200
#define SEAD3_CPLD_F_SWITCH			0xbf000208
#define SEAD3_CPLD_P_LED			0xbf000210
#define SEAD3_CPLD_F_LED			0xbf000218
#define SEAD3_NEWSC_LIVE			0xbf000220
#define SEAD3_NEWSC_REG				0xbf000228
#define SEAD3_NEWSC_CTRL			0xbf000230

#define SEAD3_LCD_CONTROL			0xbf000400
#define SEAD3_LCD_DATA				0xbf000408
#define SEAD3_CPLD_LCD_STATUS			0xbf000410
#define SEAD3_CPLD_LCD_DATA			0xbf000418

#define SEAD3_CPLD_PI_DEVRST			0xbf000480
#define SEAD3_CPLD_PI_DEVRST_IC32_RST		(1 << 0)
#define SEAD3_RESERVED_0			0xbf000500

#define SEAD3_PIC32_REGISTERS			0xbf000600
#define SEAD3_RESERVED_1			0xbf000700
#define SEAD3_UART_CH_0				0xbf000800
#define SEAD3_UART_CH_1				0xbf000900
#define SEAD3_RESERVED_2			0xbf000a00
#define SEAD3_ETHERNET				0xbf010000
#define SEAD3_RESERVED_3			0xbf020000
#define SEAD3_USER_EXPANSION			0xbf400000
#define SEAD3_RESERVED_4			0xbf800000
#define SEAD3_BOOT_FLASH_EXTENSION		0xbfa00000
#define SEAD3_BOOT_FLASH			0xbfc00000
#define SEAD3_REVISION_REGISTER			0xbfc00010

#endif /* __ASM_MIPS_BOARDS_SEAD3_ADDR_H  */
