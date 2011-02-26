/*
 * linux/arch/unicore32/include/asm/gpio.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __UNICORE_GPIO_H__
#define __UNICORE_GPIO_H__

#include <linux/io.h>
#include <asm/irq.h>
#include <mach/hardware.h>
#include <asm-generic/gpio.h>

#define GPI_OTP_INT             0
#define GPI_PCI_INTA            1
#define GPI_PCI_INTB            2
#define GPI_PCI_INTC            3
#define GPI_PCI_INTD            4
#define GPI_BAT_DET             5
#define GPI_SD_CD               6
#define GPI_SOFF_REQ            7
#define GPI_SD_WP               8
#define GPI_LCD_CASE_OFF        9
#define GPO_WIFI_EN             10
#define GPO_HDD_LED             11
#define GPO_VGA_EN              12
#define GPO_LCD_EN              13
#define GPO_LED_DATA            14
#define GPO_LED_CLK             15
#define GPO_CAM_PWR_EN          16
#define GPO_LCD_VCC_EN          17
#define GPO_SOFT_OFF            18
#define GPO_BT_EN               19
#define GPO_FAN_ON              20
#define GPO_SPKR                21
#define GPO_SET_V1              23
#define GPO_SET_V2              24
#define GPO_CPU_HEALTH          25
#define GPO_LAN_SEL             26

#ifdef CONFIG_PUV3_NB0916
#define GPI_BTN_TOUCH		14
#define GPIO_IN			0x000043ff /* 1 for input */
#define GPIO_OUT		0x0fffbc00 /* 1 for output */
#endif	/* CONFIG_PUV3_NB0916 */

#ifdef CONFIG_PUV3_SMW0919
#define GPIO_IN			0x000003ff /* 1 for input */
#define GPIO_OUT		0x0ffffc00 /* 1 for output */
#endif  /* CONFIG_PUV3_SMW0919 */

#ifdef CONFIG_PUV3_DB0913
#define GPIO_IN			0x000001df /* 1 for input */
#define GPIO_OUT		0x03fee800 /* 1 for output */
#endif  /* CONFIG_PUV3_DB0913 */

#define GPIO_DIR                (~((GPIO_IN) | 0xf0000000))
				/* 0 input, 1 output */

static inline int gpio_get_value(unsigned gpio)
{
	if (__builtin_constant_p(gpio) && (gpio <= GPIO_MAX))
		return readl(GPIO_GPLR) & GPIO_GPIO(gpio);
	else
		return __gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	if (__builtin_constant_p(gpio) && (gpio <= GPIO_MAX))
		if (value)
			writel(GPIO_GPIO(gpio), GPIO_GPSR);
		else
			writel(GPIO_GPIO(gpio), GPIO_GPCR);
	else
		__gpio_set_value(gpio, value);
}

#define gpio_cansleep	__gpio_cansleep

static inline unsigned gpio_to_irq(unsigned gpio)
{
	if ((gpio < IRQ_GPIOHIGH) && (FIELD(1, 1, gpio) & readl(GPIO_GPIR)))
		return IRQ_GPIOLOW0 + gpio;
	else
		return IRQ_GPIO0 + gpio;
}

static inline unsigned irq_to_gpio(unsigned irq)
{
	if (irq < IRQ_GPIOHIGH)
		return irq - IRQ_GPIOLOW0;
	else
		return irq - IRQ_GPIO0;
}

#endif /* __UNICORE_GPIO_H__ */
