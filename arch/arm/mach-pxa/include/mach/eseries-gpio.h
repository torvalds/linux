/*
 *  eseries-gpio.h
 *
 *  Copyright (C) Ian Molton <spyro@f2s.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

/* e-series power button */
#define GPIO_ESERIES_POWERBTN     0

/* UDC GPIO definitions */
#define GPIO_E7XX_USB_DISC       13
#define GPIO_E7XX_USB_PULLUP      3

#define GPIO_E800_USB_DISC        4
#define GPIO_E800_USB_PULLUP     84

/* e740 PCMCIA GPIO definitions */
/* Note: PWR1 seems to be inverted */
#define GPIO_E740_PCMCIA_CD0      8
#define GPIO_E740_PCMCIA_CD1     44
#define GPIO_E740_PCMCIA_RDY0    11
#define GPIO_E740_PCMCIA_RDY1     6
#define GPIO_E740_PCMCIA_RST0    27
#define GPIO_E740_PCMCIA_RST1    24
#define GPIO_E740_PCMCIA_PWR0    20
#define GPIO_E740_PCMCIA_PWR1    23

/* e750 PCMCIA GPIO definitions */
#define GPIO_E750_PCMCIA_CD0      8
#define GPIO_E750_PCMCIA_RDY0    12
#define GPIO_E750_PCMCIA_RST0    27
#define GPIO_E750_PCMCIA_PWR0    20

/* e800 PCMCIA GPIO definitions */
#define GPIO_E800_PCMCIA_RST0    69
#define GPIO_E800_PCMCIA_RST1    72
#define GPIO_E800_PCMCIA_PWR0    20
#define GPIO_E800_PCMCIA_PWR1    73

/* e7xx IrDA power control */
#define GPIO_E7XX_IR_ON          38

/* ASIC related GPIOs */
#define GPIO_ESERIES_TMIO_IRQ        5
#define GPIO_E800_ANGELX_IRQ      8
