/*
 * DaVinci pin multiplexing defines
 *
 * Author: Vladimir Barinov, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASM_ARCH_MUX_H
#define __ASM_ARCH_MUX_H

#define DAVINCI_MUX_AEAW0	0
#define DAVINCI_MUX_AEAW1	1
#define DAVINCI_MUX_AEAW2	2
#define DAVINCI_MUX_AEAW3	3
#define DAVINCI_MUX_AEAW4	4
#define DAVINCI_MUX_AECS4	10
#define DAVINCI_MUX_AECS5	11
#define DAVINCI_MUX_VLYNQWD0	12
#define DAVINCI_MUX_VLYNQWD1	13
#define DAVINCI_MUX_VLSCREN	14
#define DAVINCI_MUX_VLYNQEN	15
#define DAVINCI_MUX_HDIREN	16
#define DAVINCI_MUX_ATAEN	17
#define DAVINCI_MUX_RGB666	22
#define DAVINCI_MUX_RGB888	23
#define DAVINCI_MUX_LOEEN	24
#define DAVINCI_MUX_LFLDEN	25
#define DAVINCI_MUX_CWEN	26
#define DAVINCI_MUX_CFLDEN	27
#define DAVINCI_MUX_HPIEN	29
#define DAVINCI_MUX_1394EN	30
#define DAVINCI_MUX_EMACEN	31

#define DAVINCI_MUX_LEVEL2	32
#define DAVINCI_MUX_UART0	(DAVINCI_MUX_LEVEL2 + 0)
#define DAVINCI_MUX_UART1	(DAVINCI_MUX_LEVEL2 + 1)
#define DAVINCI_MUX_UART2	(DAVINCI_MUX_LEVEL2 + 2)
#define DAVINCI_MUX_U2FLO	(DAVINCI_MUX_LEVEL2 + 3)
#define DAVINCI_MUX_PWM0	(DAVINCI_MUX_LEVEL2 + 4)
#define DAVINCI_MUX_PWM1	(DAVINCI_MUX_LEVEL2 + 5)
#define DAVINCI_MUX_PWM2	(DAVINCI_MUX_LEVEL2 + 6)
#define DAVINCI_MUX_I2C		(DAVINCI_MUX_LEVEL2 + 7)
#define DAVINCI_MUX_SPI		(DAVINCI_MUX_LEVEL2 + 8)
#define DAVINCI_MUX_MSTK	(DAVINCI_MUX_LEVEL2 + 9)
#define DAVINCI_MUX_ASP		(DAVINCI_MUX_LEVEL2 + 10)
#define DAVINCI_MUX_CLK0	(DAVINCI_MUX_LEVEL2 + 16)
#define DAVINCI_MUX_CLK1	(DAVINCI_MUX_LEVEL2 + 17)
#define DAVINCI_MUX_TIMIN	(DAVINCI_MUX_LEVEL2 + 18)

extern void davinci_mux_peripheral(unsigned int mux, unsigned int enable);

#endif /* __ASM_ARCH_MUX_H */
