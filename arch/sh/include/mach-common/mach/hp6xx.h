/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2003, 2004, 2005  Andriy Skulysh
 */
#ifndef __ASM_SH_HP6XX_H
#define __ASM_SH_HP6XX_H

#include <linux/sh_intc.h>

#define HP680_BTN_IRQ		evt2irq(0x600)	/* IRQ0_IRQ */
#define HP680_TS_IRQ		evt2irq(0x660)	/* IRQ3_IRQ */
#define HP680_HD64461_IRQ	evt2irq(0x680)	/* IRQ4_IRQ */

#define DAC_LCD_BRIGHTNESS	0
#define DAC_SPEAKER_VOLUME	1

#define PGDR_OPENED		0x01
#define PGDR_MAIN_BATTERY_OUT	0x04
#define PGDR_PLAY_BUTTON	0x08
#define PGDR_REWIND_BUTTON	0x10
#define PGDR_RECORD_BUTTON	0x20

#define PHDR_TS_PEN_DOWN	0x08

#define PJDR_LED_BLINK		0x02

#define PKDR_LED_GREEN		0x10

/* HP Palmtop 620lx/660lx speaker on/off */
#define PKDR_SPEAKER		0x20

#define SCPDR_TS_SCAN_ENABLE	0x20
#define SCPDR_TS_SCAN_Y		0x02
#define SCPDR_TS_SCAN_X		0x01

#define SCPCR_TS_ENABLE		0x405
#define SCPCR_TS_MASK		0xc0f

#define ADC_CHANNEL_TS_Y	1
#define ADC_CHANNEL_TS_X	2
#define ADC_CHANNEL_BATTERY	3
#define ADC_CHANNEL_BACKUP	4
#define ADC_CHANNEL_CHARGE	5

/* HP Jornada 680/690 speaker on/off */
#define HD64461_GPADR_SPEAKER	0x01
#define HD64461_GPADR_PCMCIA0	(0x02|0x08)

#define HD64461_GPBDR_LCDOFF	0x01
#define HD64461_GPBDR_LCD_CONTRAST_MASK	0x78
#define HD64461_GPBDR_LED_RED	0x80

#include <asm/hd64461.h>
#include <asm/io.h>

#define PJDR	0xa4000130
#define PKDR	0xa4000132

#endif /* __ASM_SH_HP6XX_H */
