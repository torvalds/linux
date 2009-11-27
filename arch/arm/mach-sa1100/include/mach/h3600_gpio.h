/*
 *
 * Definitions for H3600 Handheld Computer
 *
 * Copyright 2000 Compaq Computer Corporation.
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 * COMPAQ COMPUTER CORPORATION MAKES NO WARRANTIES, EXPRESSED OR IMPLIED,
 * AS TO THE USEFULNESS OR CORRECTNESS OF THIS CODE OR ITS
 * FITNESS FOR ANY PARTICULAR PURPOSE.
 *
 * Author: Jamey Hicks.
 *
 * History:
 *
 * 2001-10-??   Andrew Christian   Added support for iPAQ H3800
 *
 */

#ifndef _INCLUDE_H3600_GPIO_H_
#define _INCLUDE_H3600_GPIO_H_

/*
 * gpiolib numbers for all iPAQs
 */
#define H3XXX_GPIO_PWR_BUTTON		0
#define H3XXX_GPIO_PCMCIA_CD1		10
#define H3XXX_GPIO_PCMCIA_IRQ1		11
#define H3XXX_GPIO_PCMCIA_CD0		17
#define H3XXX_GPIO_SYS_CLK		19
#define H3XXX_GPIO_PCMCIA_IRQ0		21
#define H3XXX_GPIO_COM_DCD		23
#define H3XXX_GPIO_OPTION		24
#define H3XXX_GPIO_COM_CTS		25
#define H3XXX_GPIO_COM_RTS		26

/*
 * GPIO lines that are common across ALL iPAQ models are in "h3600.h"
 * This file contains machine-specific definitions
 */

#define GPIO_H3600_SUSPEND              GPIO_GPIO (0)
/* GPIO[2:9] used by LCD on H3600/3800, used as GPIO on H3100 */
#define GPIO_H3100_BT_ON		GPIO_GPIO (2)
#define GPIO_H3100_GPIO3		GPIO_GPIO (3)
#define GPIO_H3100_QMUTE		GPIO_GPIO (4)
#define GPIO_H3100_LCD_3V_ON		GPIO_GPIO (5)
#define GPIO_H3100_AUD_ON		GPIO_GPIO (6)
#define GPIO_H3100_AUD_PWR_ON		GPIO_GPIO (7)
#define GPIO_H3100_IR_ON		GPIO_GPIO (8)
#define GPIO_H3100_IR_FSEL		GPIO_GPIO (9)

/* gpiolib versions of the above */
#define H3100_GPIO_BT_ON		2
#define H3100_GPIO_QMUTE		4
#define H3100_GPIO_LCD_3V_ON		5
#define H3100_GPIO_AUD_ON		6
#define H3100_GPIO_AUD_PWR_ON		7
#define H3100_GPIO_IR_ON		8
#define H3100_GPIO_IR_FSEL		9

/* for H3600, audio sample rate clock generator */
#define GPIO_H3600_CLK_SET0		GPIO_GPIO (12)
#define GPIO_H3600_CLK_SET1		GPIO_GPIO (13)

#define GPIO_H3600_ACTION_BUTTON	GPIO_GPIO (18)
#define GPIO_H3600_SOFT_RESET           GPIO_GPIO (20)   /* Also known as BATT_FAULT */
#define GPIO_H3600_OPT_LOCK		GPIO_GPIO (22)
#define GPIO_H3600_OPT_DET		GPIO_GPIO (27)

/****************************************************/

#define IRQ_GPIO_H3600_ACTION_BUTTON    IRQ_GPIO18
#define IRQ_GPIO_H3600_OPT_DET		IRQ_GPIO27


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

#endif /* _INCLUDE_H3600_GPIO_H_ */
