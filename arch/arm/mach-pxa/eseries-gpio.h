/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  eseries-gpio.h
 *
 *  Copyright (C) Ian Molton <spyro@f2s.com>
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
#define GPIO_E7XX_IR_OFF         38

/* e740 audio control GPIOs */
#define GPIO_E740_WM9705_nAVDD2  16
#define GPIO_E740_MIC_ON         40
#define GPIO_E740_AMP_ON         41

/* e750 audio control GPIOs */
#define GPIO_E750_HP_AMP_OFF      4
#define GPIO_E750_SPK_AMP_OFF     7
#define GPIO_E750_HP_DETECT      37

/* e800 audio control GPIOs */
#define GPIO_E800_HP_DETECT      81
#define GPIO_E800_HP_AMP_OFF     82
#define GPIO_E800_SPK_AMP_ON     83

/* ASIC related GPIOs */
#define GPIO_ESERIES_TMIO_IRQ        5
#define GPIO_ESERIES_TMIO_PCLR      19
#define GPIO_ESERIES_TMIO_SUSPEND   45
#define GPIO_E800_ANGELX_IRQ      8
