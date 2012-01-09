/*
 * Written by Philipp Zabel <philipp.zabel@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#ifndef __MACH_PXA_GPIO_PXA_H
#define __MACH_PXA_GPIO_PXA_H

#include <mach/irqs.h>
#include <mach/hardware.h>

#define GPIO_REGS_VIRT	io_p2v(0x40E00000)

#define BANK_OFF(n)	(((n) < 3) ? (n) << 2 : 0x100 + (((n) - 3) << 2))
#define GPIO_REG(x)	(*(volatile u32 *)(GPIO_REGS_VIRT + (x)))

/* GPIO Pin Level Registers */
#define GPLR0		GPIO_REG(BANK_OFF(0) + 0x00)
#define GPLR1		GPIO_REG(BANK_OFF(1) + 0x00)
#define GPLR2		GPIO_REG(BANK_OFF(2) + 0x00)
#define GPLR3		GPIO_REG(BANK_OFF(3) + 0x00)

/* GPIO Pin Direction Registers */
#define GPDR0		GPIO_REG(BANK_OFF(0) + 0x0c)
#define GPDR1		GPIO_REG(BANK_OFF(1) + 0x0c)
#define GPDR2		GPIO_REG(BANK_OFF(2) + 0x0c)
#define GPDR3		GPIO_REG(BANK_OFF(3) + 0x0c)

/* GPIO Pin Output Set Registers */
#define GPSR0		GPIO_REG(BANK_OFF(0) + 0x18)
#define GPSR1		GPIO_REG(BANK_OFF(1) + 0x18)
#define GPSR2		GPIO_REG(BANK_OFF(2) + 0x18)
#define GPSR3		GPIO_REG(BANK_OFF(3) + 0x18)

/* GPIO Pin Output Clear Registers */
#define GPCR0		GPIO_REG(BANK_OFF(0) + 0x24)
#define GPCR1		GPIO_REG(BANK_OFF(1) + 0x24)
#define GPCR2		GPIO_REG(BANK_OFF(2) + 0x24)
#define GPCR3		GPIO_REG(BANK_OFF(3) + 0x24)

/* GPIO Rising Edge Detect Registers */
#define GRER0		GPIO_REG(BANK_OFF(0) + 0x30)
#define GRER1		GPIO_REG(BANK_OFF(1) + 0x30)
#define GRER2		GPIO_REG(BANK_OFF(2) + 0x30)
#define GRER3		GPIO_REG(BANK_OFF(3) + 0x30)

/* GPIO Falling Edge Detect Registers */
#define GFER0		GPIO_REG(BANK_OFF(0) + 0x3c)
#define GFER1		GPIO_REG(BANK_OFF(1) + 0x3c)
#define GFER2		GPIO_REG(BANK_OFF(2) + 0x3c)
#define GFER3		GPIO_REG(BANK_OFF(3) + 0x3c)

/* GPIO Edge Detect Status Registers */
#define GEDR0		GPIO_REG(BANK_OFF(0) + 0x48)
#define GEDR1		GPIO_REG(BANK_OFF(1) + 0x48)
#define GEDR2		GPIO_REG(BANK_OFF(2) + 0x48)
#define GEDR3		GPIO_REG(BANK_OFF(3) + 0x48)

/* GPIO Alternate Function Select Registers */
#define GAFR0_L		GPIO_REG(0x0054)
#define GAFR0_U		GPIO_REG(0x0058)
#define GAFR1_L		GPIO_REG(0x005C)
#define GAFR1_U		GPIO_REG(0x0060)
#define GAFR2_L		GPIO_REG(0x0064)
#define GAFR2_U		GPIO_REG(0x0068)
#define GAFR3_L		GPIO_REG(0x006C)
#define GAFR3_U		GPIO_REG(0x0070)

/* More handy macros.  The argument is a literal GPIO number. */

#define GPIO_bit(x)	(1 << ((x) & 0x1f))

#define GPLR(x)		GPIO_REG(BANK_OFF((x) >> 5) + 0x00)
#define GPDR(x)		GPIO_REG(BANK_OFF((x) >> 5) + 0x0c)
#define GPSR(x)		GPIO_REG(BANK_OFF((x) >> 5) + 0x18)
#define GPCR(x)		GPIO_REG(BANK_OFF((x) >> 5) + 0x24)
#define GRER(x)		GPIO_REG(BANK_OFF((x) >> 5) + 0x30)
#define GFER(x)		GPIO_REG(BANK_OFF((x) >> 5) + 0x3c)
#define GEDR(x)		GPIO_REG(BANK_OFF((x) >> 5) + 0x48)
#define GAFR(x)		GPIO_REG(0x54 + (((x) & 0x70) >> 2))


#define NR_BUILTIN_GPIO		PXA_GPIO_IRQ_NUM

#define gpio_to_bank(gpio)	((gpio) >> 5)

#ifdef CONFIG_CPU_PXA26x
/* GPIO86/87/88/89 on PXA26x have their direction bits in GPDR2 inverted,
 * as well as their Alternate Function value being '1' for GPIO in GAFRx.
 */
static inline int __gpio_is_inverted(unsigned gpio)
{
	return cpu_is_pxa25x() && gpio > 85;
}
#else
static inline int __gpio_is_inverted(unsigned gpio) { return 0; }
#endif

/*
 * On PXA25x and PXA27x, GAFRx and GPDRx together decide the alternate
 * function of a GPIO, and GPDRx cannot be altered once configured. It
 * is attributed as "occupied" here (I know this terminology isn't
 * accurate, you are welcome to propose a better one :-)
 */
static inline int __gpio_is_occupied(unsigned gpio)
{
	if (cpu_is_pxa27x() || cpu_is_pxa25x()) {
		int af = (GAFR(gpio) >> ((gpio & 0xf) * 2)) & 0x3;
		int dir = GPDR(gpio) & GPIO_bit(gpio);

		if (__gpio_is_inverted(gpio))
			return af != 1 || dir == 0;
		else
			return af != 0 || dir != 0;
	} else
		return GPDR(gpio) & GPIO_bit(gpio);
}

#include <plat/gpio-pxa.h>
#endif /* __MACH_PXA_GPIO_PXA_H */
