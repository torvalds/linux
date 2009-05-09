/*
* Copyright (C) 2008 by Sascha Hauer <kernel@pengutronix.de>
* Copyright (C) 2009 by Holger Schurig <hs4233@mail.mn-solutions.de>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
* MA 02110-1301, USA.
*/

#ifndef _MXC_IOMUX_H
#define _MXC_IOMUX_H

/*
*  GPIO Module and I/O Multiplexer
*  x = 0..3 for reg_A, reg_B, reg_C, reg_D
*/
#define VA_GPIO_BASE	IO_ADDRESS(GPIO_BASE_ADDR)
#define MXC_DDIR(x)    (0x00 + ((x) << 8))
#define MXC_OCR1(x)    (0x04 + ((x) << 8))
#define MXC_OCR2(x)    (0x08 + ((x) << 8))
#define MXC_ICONFA1(x) (0x0c + ((x) << 8))
#define MXC_ICONFA2(x) (0x10 + ((x) << 8))
#define MXC_ICONFB1(x) (0x14 + ((x) << 8))
#define MXC_ICONFB2(x) (0x18 + ((x) << 8))
#define MXC_DR(x)      (0x1c + ((x) << 8))
#define MXC_GIUS(x)    (0x20 + ((x) << 8))
#define MXC_SSR(x)     (0x24 + ((x) << 8))
#define MXC_ICR1(x)    (0x28 + ((x) << 8))
#define MXC_ICR2(x)    (0x2c + ((x) << 8))
#define MXC_IMR(x)     (0x30 + ((x) << 8))
#define MXC_ISR(x)     (0x34 + ((x) << 8))
#define MXC_GPR(x)     (0x38 + ((x) << 8))
#define MXC_SWR(x)     (0x3c + ((x) << 8))
#define MXC_PUEN(x)    (0x40 + ((x) << 8))

#ifdef CONFIG_ARCH_MX1
# define GPIO_PORT_MAX  3
#endif
#ifdef CONFIG_ARCH_MX2
# define GPIO_PORT_MAX  5
#endif

#ifndef GPIO_PORT_MAX
# error "GPIO config port count unknown!"
#endif

#define GPIO_PIN_MASK 0x1f

#define GPIO_PORT_SHIFT 5
#define GPIO_PORT_MASK (0x7 << GPIO_PORT_SHIFT)

#define GPIO_PORTA (0 << GPIO_PORT_SHIFT)
#define GPIO_PORTB (1 << GPIO_PORT_SHIFT)
#define GPIO_PORTC (2 << GPIO_PORT_SHIFT)
#define GPIO_PORTD (3 << GPIO_PORT_SHIFT)
#define GPIO_PORTE (4 << GPIO_PORT_SHIFT)
#define GPIO_PORTF (5 << GPIO_PORT_SHIFT)

#define GPIO_OUT   (1 << 8)
#define GPIO_IN    (0 << 8)
#define GPIO_PUEN  (1 << 9)

#define GPIO_PF    (1 << 10)
#define GPIO_AF    (1 << 11)

#define GPIO_OCR_SHIFT 12
#define GPIO_OCR_MASK (3 << GPIO_OCR_SHIFT)
#define GPIO_AIN   (0 << GPIO_OCR_SHIFT)
#define GPIO_BIN   (1 << GPIO_OCR_SHIFT)
#define GPIO_CIN   (2 << GPIO_OCR_SHIFT)
#define GPIO_GPIO  (3 << GPIO_OCR_SHIFT)

#define GPIO_AOUT_SHIFT 14
#define GPIO_AOUT_MASK (3 << GPIO_AOUT_SHIFT)
#define GPIO_AOUT     (0 << GPIO_AOUT_SHIFT)
#define GPIO_AOUT_ISR (1 << GPIO_AOUT_SHIFT)
#define GPIO_AOUT_0   (2 << GPIO_AOUT_SHIFT)
#define GPIO_AOUT_1   (3 << GPIO_AOUT_SHIFT)

#define GPIO_BOUT_SHIFT 16
#define GPIO_BOUT_MASK (3 << GPIO_BOUT_SHIFT)
#define GPIO_BOUT      (0 << GPIO_BOUT_SHIFT)
#define GPIO_BOUT_ISR  (1 << GPIO_BOUT_SHIFT)
#define GPIO_BOUT_0    (2 << GPIO_BOUT_SHIFT)
#define GPIO_BOUT_1    (3 << GPIO_BOUT_SHIFT)


#ifdef CONFIG_ARCH_MX1
#include <mach/iomux-mx1.h>
#endif
#ifdef CONFIG_ARCH_MX2
#include <mach/iomux-mx2x.h>
#ifdef CONFIG_MACH_MX21
#include <mach/iomux-mx21.h>
#endif
#ifdef CONFIG_MACH_MX27
#include <mach/iomux-mx27.h>
#endif
#endif


/* decode irq number to use with IMR(x), ISR(x) and friends */
#define IRQ_TO_REG(irq) ((irq - MXC_INTERNAL_IRQS) >> 5)

#define IRQ_GPIOA(x)  (MXC_GPIO_IRQ_START + x)
#define IRQ_GPIOB(x)  (IRQ_GPIOA(32) + x)
#define IRQ_GPIOC(x)  (IRQ_GPIOB(32) + x)
#define IRQ_GPIOD(x)  (IRQ_GPIOC(32) + x)
#define IRQ_GPIOE(x)  (IRQ_GPIOD(32) + x)


extern void mxc_gpio_mode(int gpio_mode);
extern int mxc_gpio_setup_multiple_pins(const int *pin_list, unsigned count,
	const char *label);
extern void mxc_gpio_release_multiple_pins(const int *pin_list, int count);

#endif
