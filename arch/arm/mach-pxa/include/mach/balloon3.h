/*
 *  linux/include/asm-arm/arch-pxa/balloon3.h
 *
 *  Authors:	Nick Bane and Wookey
 *  Created:	Oct, 2005
 *  Copyright:	Toby Churchill Ltd
 *  Cribbed from mainstone.c, by Nicholas Pitre
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ASM_ARCH_BALLOON3_H
#define ASM_ARCH_BALLOON3_H

enum balloon3_features {
	BALLOON3_FEATURE_OHCI,
	BALLOON3_FEATURE_MMC,
	BALLOON3_FEATURE_CF,
	BALLOON3_FEATURE_AUDIO,
	BALLOON3_FEATURE_TOPPOLY,
};

#define BALLOON3_FPGA_PHYS	PXA_CS4_PHYS
#define BALLOON3_FPGA_VIRT	(0xf1000000)	/* as per balloon2 */
#define BALLOON3_FPGA_LENGTH	0x01000000

/* FPGA/CPLD registers */
#define BALLOON3_PCMCIA0_REG		(BALLOON3_FPGA_VIRT + 0x00e00008)
/* fixme - same for now */
#define BALLOON3_PCMCIA1_REG		(BALLOON3_FPGA_VIRT + 0x00e00008)
#define BALLOON3_NANDIO_IO_REG		(BALLOON3_FPGA_VIRT + 0x00e00000)
/* fpga/cpld interrupt control register */
#define BALLOON3_INT_CONTROL_REG	(BALLOON3_FPGA_VIRT + 0x00e0000C)
#define BALLOON3_NANDIO_CTL2_REG 	(BALLOON3_FPGA_VIRT + 0x00e00010)
#define BALLOON3_NANDIO_CTL_REG 	(BALLOON3_FPGA_VIRT + 0x00e00014)
#define BALLOON3_VERSION_REG		(BALLOON3_FPGA_VIRT + 0x00e0001c)

#define BALLOON3_SAMOSA_ADDR_REG	(BALLOON3_FPGA_VIRT + 0x00c00000)
#define BALLOON3_SAMOSA_DATA_REG	(BALLOON3_FPGA_VIRT + 0x00c00004)
#define BALLOON3_SAMOSA_STATUS_REG	(BALLOON3_FPGA_VIRT + 0x00c0001c)

/* GPIOs for irqs */
#define BALLOON3_GPIO_AUX_NIRQ		(94)
#define BALLOON3_GPIO_CODEC_IRQ		(95)

/* Timer and Idle LED locations */
#define BALLOON3_GPIO_LED_NAND		(9)
#define BALLOON3_GPIO_LED_IDLE		(10)

/* backlight control */
#define BALLOON3_GPIO_RUN_BACKLIGHT	(99)

#define BALLOON3_GPIO_S0_CD		(105)

/* FPGA Interrupt Mask/Acknowledge Register */
#define BALLOON3_INT_S0_IRQ		(1 << 0)  /* PCMCIA 0 IRQ */
#define BALLOON3_INT_S0_STSCHG		(1 << 1)  /* PCMCIA 0 status changed */

/* CF Status Register */
#define BALLOON3_PCMCIA_nIRQ		(1 << 0)  /* IRQ / ready signal */
#define BALLOON3_PCMCIA_nSTSCHG_BVD1	(1 << 1)
					/* VDD sense / card status changed */

/* CF control register (write) */
#define BALLOON3_PCMCIA_RESET		(1 << 0)   /* Card reset signal */
#define BALLOON3_PCMCIA_ENABLE		(1 << 1)
#define BALLOON3_PCMCIA_ADD_ENABLE	(1 << 2)

/* CPLD (and FPGA) interface definitions */
#define CPLD_LCD0_DATA_SET             0x00
#define CPLD_LCD0_DATA_CLR             0x10
#define CPLD_LCD0_COMMAND_SET          0x01
#define CPLD_LCD0_COMMAND_CLR          0x11
#define CPLD_LCD1_DATA_SET             0x02
#define CPLD_LCD1_DATA_CLR             0x12
#define CPLD_LCD1_COMMAND_SET          0x03
#define CPLD_LCD1_COMMAND_CLR          0x13

#define CPLD_MISC_SET                  0x07
#define CPLD_MISC_CLR                  0x17
#define CPLD_MISC_LOON_NRESET_BIT      0
#define CPLD_MISC_LOON_UNSUSP_BIT      1
#define CPLD_MISC_RUN_5V_BIT           2
#define CPLD_MISC_CHG_D0_BIT           3
#define CPLD_MISC_CHG_D1_BIT           4
#define CPLD_MISC_DAC_NCS_BIT          5

#define CPLD_LCD_SET                   0x08
#define CPLD_LCD_CLR                   0x18
#define CPLD_LCD_BACKLIGHT_EN_0_BIT    0
#define CPLD_LCD_BACKLIGHT_EN_1_BIT    1
#define CPLD_LCD_LED_RED_BIT           4
#define CPLD_LCD_LED_GREEN_BIT         5
#define CPLD_LCD_NRESET_BIT            7

#define CPLD_LCD_RO_SET                0x09
#define CPLD_LCD_RO_CLR                0x19
#define CPLD_LCD_RO_LCD0_nWAIT_BIT     0
#define CPLD_LCD_RO_LCD1_nWAIT_BIT     1

#define CPLD_SERIAL_SET                0x0a
#define CPLD_SERIAL_CLR                0x1a
#define CPLD_SERIAL_GSM_RI_BIT         0
#define CPLD_SERIAL_GSM_CTS_BIT        1
#define CPLD_SERIAL_GSM_DTR_BIT        2
#define CPLD_SERIAL_LPR_CTS_BIT        3
#define CPLD_SERIAL_TC232_CTS_BIT      4
#define CPLD_SERIAL_TC232_DSR_BIT      5

#define CPLD_SROUTING_SET              0x0b
#define CPLD_SROUTING_CLR              0x1b
#define CPLD_SROUTING_MSP430_LPR       0
#define CPLD_SROUTING_MSP430_TC232     1
#define CPLD_SROUTING_MSP430_GSM       2
#define CPLD_SROUTING_LOON_LPR         (0 << 4)
#define CPLD_SROUTING_LOON_TC232       (1 << 4)
#define CPLD_SROUTING_LOON_GSM         (2 << 4)

#define CPLD_AROUTING_SET              0x0c
#define CPLD_AROUTING_CLR              0x1c
#define CPLD_AROUTING_MIC2PHONE_BIT    0
#define CPLD_AROUTING_PHONE2INT_BIT    1
#define CPLD_AROUTING_PHONE2EXT_BIT    2
#define CPLD_AROUTING_LOONL2INT_BIT    3
#define CPLD_AROUTING_LOONL2EXT_BIT    4
#define CPLD_AROUTING_LOONR2PHONE_BIT  5
#define CPLD_AROUTING_LOONR2INT_BIT    6
#define CPLD_AROUTING_LOONR2EXT_BIT    7

extern int balloon3_has(enum balloon3_features feature);

#endif
