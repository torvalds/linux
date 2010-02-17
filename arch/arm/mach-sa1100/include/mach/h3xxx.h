/*
 * Definitions for Compaq iPAQ H3100 and H3600 handheld computers
 *
 * (c) 2000 Compaq Computer Corporation. (Author: Jamey Hicks)
 * (c) 2009 Dmitry Artamonow <mad_soft@inbox.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _INCLUDE_H3XXX_H_
#define _INCLUDE_H3XXX_H_

/* Physical memory regions corresponding to chip selects */
#define H3600_EGPIO_PHYS	(SA1100_CS5_PHYS + 0x01000000)
#define H3600_BANK_2_PHYS	SA1100_CS2_PHYS
#define H3600_BANK_4_PHYS	SA1100_CS4_PHYS

/* Virtual memory regions corresponding to chip selects 2 & 4 (used on sleeves) */
#define H3600_EGPIO_VIRT	0xf0000000
#define H3600_BANK_2_VIRT	0xf1000000
#define H3600_BANK_4_VIRT	0xf3800000

/*
 * gpiolib numbers for all iPAQs
 */
#define H3XXX_GPIO_PWR_BUTTON		0
#define H3XXX_GPIO_PCMCIA_CD1		10
#define H3XXX_GPIO_PCMCIA_IRQ1		11
#define H3XXX_GPIO_PCMCIA_CD0		17
#define H3XXX_GPIO_ACTION_BUTTON	18
#define H3XXX_GPIO_SYS_CLK		19
#define H3XXX_GPIO_PCMCIA_IRQ0		21
#define H3XXX_GPIO_COM_DCD		23
#define H3XXX_GPIO_OPTION		24
#define H3XXX_GPIO_COM_CTS		25
#define H3XXX_GPIO_COM_RTS		26

/* machine-specific gpios */

#define H3100_GPIO_BT_ON		2
#define H3100_GPIO_QMUTE		4
#define H3100_GPIO_LCD_3V_ON		5
#define H3100_GPIO_AUD_ON		6
#define H3100_GPIO_AUD_PWR_ON		7
#define H3100_GPIO_IR_ON		8
#define H3100_GPIO_IR_FSEL		9

#define H3600_GPIO_CLK_SET0		12	/* audio sample rate clock generator */
#define H3600_GPIO_CLK_SET1		13
#define H3600_GPIO_SOFT_RESET		20	/* also known as BATT_FAULT */
#define H3600_GPIO_OPT_LOCK		22
#define H3600_GPIO_OPT_DET		27


/* H3100 / 3600 EGPIO pins */
#define H3XXX_EGPIO_BASE		(GPIO_MAX + 1)

#define H3XXX_EGPIO_VPP_ON		(H3XXX_EGPIO_BASE + 0)
#define H3XXX_EGPIO_CARD_RESET		(H3XXX_EGPIO_BASE + 1) /* reset the attached pcmcia/compactflash card.  active high. */
#define H3XXX_EGPIO_OPT_RESET		(H3XXX_EGPIO_BASE + 2) /* reset the attached option pack.  active high. */
#define H3XXX_EGPIO_CODEC_NRESET	(H3XXX_EGPIO_BASE + 3) /* reset the onboard UDA1341.  active low. */
#define H3XXX_EGPIO_OPT_NVRAM_ON	(H3XXX_EGPIO_BASE + 4) /* apply power to optionpack nvram, active high. */
#define H3XXX_EGPIO_OPT_ON		(H3XXX_EGPIO_BASE + 5) /* full power to option pack.  active high. */
#define H3XXX_EGPIO_LCD_ON		(H3XXX_EGPIO_BASE + 6) /* enable 3.3V to LCD.  active high. */
#define H3XXX_EGPIO_RS232_ON		(H3XXX_EGPIO_BASE + 7) /* UART3 transceiver force on.  Active high. */

/* H3600 only EGPIO pins */
#define H3600_EGPIO_LCD_PCI		(H3XXX_EGPIO_BASE + 8) /* LCD control IC enable.  active high. */
#define H3600_EGPIO_IR_ON		(H3XXX_EGPIO_BASE + 9) /* apply power to IR module.  active high. */
#define H3600_EGPIO_AUD_AMP_ON		(H3XXX_EGPIO_BASE + 10) /* apply power to audio power amp.  active high. */
#define H3600_EGPIO_AUD_PWR_ON		(H3XXX_EGPIO_BASE + 11) /* apply power to reset of audio circuit.  active high. */
#define H3600_EGPIO_QMUTE		(H3XXX_EGPIO_BASE + 12) /* mute control for onboard UDA1341.  active high. */
#define H3600_EGPIO_IR_FSEL		(H3XXX_EGPIO_BASE + 13) /* IR speed select: 1->fast, 0->slow */
#define H3600_EGPIO_LCD_5V_ON		(H3XXX_EGPIO_BASE + 14) /* enable 5V to LCD. active high. */
#define H3600_EGPIO_LVDD_ON		(H3XXX_EGPIO_BASE + 15) /* enable 9V and -6.5V to LCD. */

struct gpio_default_state {
	int gpio;
	int mode;
	const char *name;
};

#define GPIO_MODE_IN	-1
#define GPIO_MODE_OUT0	0
#define GPIO_MODE_OUT1	1

void h3xxx_init_gpio(struct gpio_default_state *s, size_t n);
void __init h3xxx_map_io(void);
void __init h3xxx_mach_init(void);

#endif /* _INCLUDE_H3XXX_H_ */
