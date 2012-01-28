/*
 * GPIOs and interrupts for Palm T|X Handheld Computer
 *
 * Based on palmld-gpio.h by Alex Osborne
 *
 * Authors:	Marek Vasut <marek.vasut@gmail.com>
 *		Cristiano P. <cristianop@users.sourceforge.net>
 *		Jan Herman <2hp@seznam.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _INCLUDE_PALMTX_H_
#define _INCLUDE_PALMTX_H_

/** HERE ARE GPIOs **/

/* GPIOs */
#define GPIO_NR_PALMTX_GPIO_RESET		1

#define GPIO_NR_PALMTX_POWER_DETECT		12 /* 90 */
#define GPIO_NR_PALMTX_HOTSYNC_BUTTON_N		10
#define GPIO_NR_PALMTX_EARPHONE_DETECT		107

/* SD/MMC */
#define GPIO_NR_PALMTX_SD_DETECT_N		14
#define GPIO_NR_PALMTX_SD_POWER			114 /* probably */
#define GPIO_NR_PALMTX_SD_READONLY		115 /* probably */

/* TOUCHSCREEN */
#define GPIO_NR_PALMTX_WM9712_IRQ		27

/* IRDA -  disable GPIO connected to SD pin of tranceiver (TFBS4710?) ? */
#define GPIO_NR_PALMTX_IR_DISABLE		40

/* USB */
#define GPIO_NR_PALMTX_USB_DETECT_N		13
#define GPIO_NR_PALMTX_USB_PULLUP		93

/* LCD/BACKLIGHT */
#define GPIO_NR_PALMTX_BL_POWER			84
#define GPIO_NR_PALMTX_LCD_POWER		96

/* LCD BORDER */
#define GPIO_NR_PALMTX_BORDER_SWITCH		98
#define GPIO_NR_PALMTX_BORDER_SELECT		22

/* BLUETOOTH */
#define GPIO_NR_PALMTX_BT_POWER			17
#define GPIO_NR_PALMTX_BT_RESET			83

/* PCMCIA (WiFi) */
#define GPIO_NR_PALMTX_PCMCIA_POWER1		94
#define GPIO_NR_PALMTX_PCMCIA_POWER2		108
#define GPIO_NR_PALMTX_PCMCIA_RESET		79
#define GPIO_NR_PALMTX_PCMCIA_READY		116

/* NAND Flash ... this GPIO may be incorrect! */
#define GPIO_NR_PALMTX_NAND_BUFFER_DIR		79

/* INTERRUPTS */
#define IRQ_GPIO_PALMTX_SD_DETECT_N	PXA_GPIO_TO_IRQ(GPIO_NR_PALMTX_SD_DETECT_N)
#define IRQ_GPIO_PALMTX_WM9712_IRQ	PXA_GPIO_TO_IRQ(GPIO_NR_PALMTX_WM9712_IRQ)
#define IRQ_GPIO_PALMTX_USB_DETECT	PXA_GPIO_TO_IRQ(GPIO_NR_PALMTX_USB_DETECT)
#define IRQ_GPIO_PALMTX_GPIO_RESET	PXA_GPIO_TO_IRQ(GPIO_NR_PALMTX_GPIO_RESET)

/** HERE ARE INIT VALUES **/

/* Various addresses  */
#define PALMTX_PCMCIA_PHYS	0x28000000
#define PALMTX_PCMCIA_VIRT	IOMEM(0xf0000000)
#define PALMTX_PCMCIA_SIZE	0x100000

#define PALMTX_PHYS_RAM_START	0xa0000000
#define PALMTX_PHYS_IO_START	0x40000000

#define PALMTX_STR_BASE		0xa0200000

#define PALMTX_PHYS_FLASH_START	PXA_CS0_PHYS	/* ChipSelect 0 */
#define PALMTX_PHYS_NAND_START	PXA_CS1_PHYS	/* ChipSelect 1 */

#define PALMTX_NAND_ALE_PHYS	(PALMTX_PHYS_NAND_START | (1 << 24))
#define PALMTX_NAND_CLE_PHYS	(PALMTX_PHYS_NAND_START | (1 << 25))
#define PALMTX_NAND_ALE_VIRT	IOMEM(0xff100000)
#define PALMTX_NAND_CLE_VIRT	IOMEM(0xff200000)

/* TOUCHSCREEN */
#define AC97_LINK_FRAME			21


/* BATTERY */
#define PALMTX_BAT_MAX_VOLTAGE		4000	/* 4.00v current voltage */
#define PALMTX_BAT_MIN_VOLTAGE		3550	/* 3.55v critical voltage */
#define PALMTX_BAT_MAX_CURRENT		0	/* unknown */
#define PALMTX_BAT_MIN_CURRENT		0	/* unknown */
#define PALMTX_BAT_MAX_CHARGE		1	/* unknown */
#define PALMTX_BAT_MIN_CHARGE		1	/* unknown */
#define PALMTX_MAX_LIFE_MINS		360	/* on-life in minutes */

#define PALMTX_BAT_MEASURE_DELAY	(HZ * 1)

/* BACKLIGHT */
#define PALMTX_MAX_INTENSITY		0xFE
#define PALMTX_DEFAULT_INTENSITY	0x7E
#define PALMTX_LIMIT_MASK		0x7F
#define PALMTX_PRESCALER		0x3F
#define PALMTX_PERIOD_NS		3500

#endif
