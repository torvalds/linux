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

/* FPGA / CPLD registers for CF socket */
#define	BALLOON3_CF_STATUS_REG		(BALLOON3_FPGA_VIRT + 0x00e00008)
#define	BALLOON3_CF_CONTROL_REG		(BALLOON3_FPGA_VIRT + 0x00e00008)
/* FPGA / CPLD version register */
#define	BALLOON3_FPGA_VER		(BALLOON3_FPGA_VIRT + 0x00e0001c)
/* FPGA / CPLD registers for NAND flash */
#define	BALLOON3_NAND_BASE		(PXA_CS4_PHYS + 0x00e00000)
#define	BALLOON3_NAND_IO_REG		(BALLOON3_FPGA_VIRT + 0x00e00000)
#define	BALLOON3_NAND_CONTROL2_REG	(BALLOON3_FPGA_VIRT + 0x00e00010)
#define	BALLOON3_NAND_STAT_REG		(BALLOON3_FPGA_VIRT + 0x00e00010)
#define	BALLOON3_NAND_CONTROL_REG	(BALLOON3_FPGA_VIRT + 0x00e00014)

/* fpga/cpld interrupt control register */
#define BALLOON3_INT_CONTROL_REG	(BALLOON3_FPGA_VIRT + 0x00e0000C)
#define BALLOON3_VERSION_REG		(BALLOON3_FPGA_VIRT + 0x00e0001c)

#define BALLOON3_SAMOSA_ADDR_REG	(BALLOON3_FPGA_VIRT + 0x00c00000)
#define BALLOON3_SAMOSA_DATA_REG	(BALLOON3_FPGA_VIRT + 0x00c00004)
#define BALLOON3_SAMOSA_STATUS_REG	(BALLOON3_FPGA_VIRT + 0x00c0001c)

/* CF Status Register bits (read-only) bits */
#define BALLOON3_CF_nIRQ		(1 << 0)
#define BALLOON3_CF_nSTSCHG_BVD1	(1 << 1)

/* CF Control Set Register bits / CF Control Clear Register bits (write-only) */
#define BALLOON3_CF_RESET		(1 << 0)
#define BALLOON3_CF_ENABLE		(1 << 1)
#define BALLOON3_CF_ADD_ENABLE		(1 << 2)

/* CF Interrupt sources */
#define BALLOON3_BP_CF_NRDY_IRQ		BALLOON3_IRQ(0)
#define BALLOON3_BP_NSTSCHG_IRQ		BALLOON3_IRQ(1)

/* NAND Control register */
#define	BALLOON3_NAND_CONTROL_FLWP	(1 << 7)
#define	BALLOON3_NAND_CONTROL_FLSE	(1 << 6)
#define	BALLOON3_NAND_CONTROL_FLCE3	(1 << 5)
#define	BALLOON3_NAND_CONTROL_FLCE2	(1 << 4)
#define	BALLOON3_NAND_CONTROL_FLCE1	(1 << 3)
#define	BALLOON3_NAND_CONTROL_FLCE0	(1 << 2)
#define	BALLOON3_NAND_CONTROL_FLALE	(1 << 1)
#define	BALLOON3_NAND_CONTROL_FLCLE	(1 << 0)

/* NAND Status register */
#define	BALLOON3_NAND_STAT_RNB		(1 << 0)

/* NAND Control2 register */
#define	BALLOON3_NAND_CONTROL2_16BIT	(1 << 0)

/* GPIOs for irqs */
#define BALLOON3_GPIO_AUX_NIRQ		(94)
#define BALLOON3_GPIO_CODEC_IRQ		(95)

/* Timer and Idle LED locations */
#define BALLOON3_GPIO_LED_NAND		(9)
#define BALLOON3_GPIO_LED_IDLE		(10)

/* backlight control */
#define BALLOON3_GPIO_RUN_BACKLIGHT	(99)

#define BALLOON3_GPIO_S0_CD		(105)

/* NAND */
#define BALLOON3_GPIO_RUN_NAND		(102)

/* PCF8574A Leds */
#define	BALLOON3_PCF_GPIO_BASE		160
#define	BALLOON3_PCF_GPIO_LED0		(BALLOON3_PCF_GPIO_BASE + 0)
#define	BALLOON3_PCF_GPIO_LED1		(BALLOON3_PCF_GPIO_BASE + 1)
#define	BALLOON3_PCF_GPIO_LED2		(BALLOON3_PCF_GPIO_BASE + 2)
#define	BALLOON3_PCF_GPIO_LED3		(BALLOON3_PCF_GPIO_BASE + 3)
#define	BALLOON3_PCF_GPIO_LED4		(BALLOON3_PCF_GPIO_BASE + 4)
#define	BALLOON3_PCF_GPIO_LED5		(BALLOON3_PCF_GPIO_BASE + 5)
#define	BALLOON3_PCF_GPIO_LED6		(BALLOON3_PCF_GPIO_BASE + 6)
#define	BALLOON3_PCF_GPIO_LED7		(BALLOON3_PCF_GPIO_BASE + 7)

/* FPGA Interrupt Mask/Acknowledge Register */
#define BALLOON3_INT_S0_IRQ		(1 << 0)  /* PCMCIA 0 IRQ */
#define BALLOON3_INT_S0_STSCHG		(1 << 1)  /* PCMCIA 0 status changed */

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

/* Balloon3 Interrupts */
#define BALLOON3_IRQ(x)		(IRQ_BOARD_START + (x))

#define BALLOON3_AUX_NIRQ	IRQ_GPIO(BALLOON3_GPIO_AUX_NIRQ)
#define BALLOON3_CODEC_IRQ	IRQ_GPIO(BALLOON3_GPIO_CODEC_IRQ)
#define BALLOON3_S0_CD_IRQ	IRQ_GPIO(BALLOON3_GPIO_S0_CD)

#define BALLOON3_NR_IRQS	(IRQ_BOARD_START + 4)

extern int balloon3_has(enum balloon3_features feature);

#endif
