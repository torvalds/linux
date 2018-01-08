/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2002 Integrated Device Technology, Inc.
 *	All rights reserved.
 *
 * GPIO register definition.
 *
 * Author : ryan.holmQVist@idt.com
 * Date	  : 20011005
 * Copyright (C) 2001, 2002 Ryan Holm <ryan.holmQVist@idt.com>
 * Copyright (C) 2008 Florian Fainelli <florian@openwrt.org>
 */

#ifndef _RC32434_GPIO_H_
#define _RC32434_GPIO_H_

struct rb532_gpio_reg {
	u32   gpiofunc;	  /* GPIO Function Register
			   * gpiofunc[x]==0 bit = gpio
			   * func[x]==1	 bit = altfunc
			   */
	u32   gpiocfg;	  /* GPIO Configuration Register
			   * gpiocfg[x]==0 bit = input
			   * gpiocfg[x]==1 bit = output
			   */
	u32   gpiod;	  /* GPIO Data Register
			   * gpiod[x] read/write gpio pinX status
			   */
	u32   gpioilevel; /* GPIO Interrupt Status Register
			   * interrupt level (see gpioistat)
			   */
	u32   gpioistat;  /* Gpio Interrupt Status Register
			   * istat[x] = (gpiod[x] == level[x])
			   * cleared in ISR (STICKY bits)
			   */
	u32   gpionmien;  /* GPIO Non-maskable Interrupt Enable Register */
};

/* UART GPIO signals */
#define RC32434_UART0_SOUT	(1 << 0)
#define RC32434_UART0_SIN	(1 << 1)
#define RC32434_UART0_RTS	(1 << 2)
#define RC32434_UART0_CTS	(1 << 3)

/* M & P bus GPIO signals */
#define RC32434_MP_BIT_22	(1 << 4)
#define RC32434_MP_BIT_23	(1 << 5)
#define RC32434_MP_BIT_24	(1 << 6)
#define RC32434_MP_BIT_25	(1 << 7)

/* CPU GPIO signals */
#define RC32434_CPU_GPIO	(1 << 8)

/* Reserved GPIO signals */
#define RC32434_AF_SPARE_6	(1 << 9)
#define RC32434_AF_SPARE_4	(1 << 10)
#define RC32434_AF_SPARE_3	(1 << 11)
#define RC32434_AF_SPARE_2	(1 << 12)

/* PCI messaging unit */
#define RC32434_PCI_MSU_GPIO	(1 << 13)

/* NAND GPIO signals */
#define GPIO_RDY		8
#define GPIO_WPX	9
#define GPIO_ALE		10
#define GPIO_CLE		11

/* Compact Flash GPIO pin */
#define CF_GPIO_NUM		13

/* S1 button GPIO (shared with UART0_SIN) */
#define GPIO_BTN_S1		1

extern void rb532_gpio_set_ilevel(int bit, unsigned gpio);
extern void rb532_gpio_set_istat(int bit, unsigned gpio);
extern void rb532_gpio_set_func(unsigned gpio);

#endif /* _RC32434_GPIO_H_ */
