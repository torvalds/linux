#ifndef __ASM_MACH_GPIO_PXA_H
#define __ASM_MACH_GPIO_PXA_H

#include <mach/addr-map.h>
#include <mach/cputype.h>
#include <mach/irqs.h>

#define GPIO_REGS_VIRT	(APB_VIRT_BASE + 0x19000)

#define BANK_OFF(n)	(((n) < 3) ? (n) << 2 : 0x100 + (((n) - 3) << 2))
#define GPIO_REG(x)	(*(volatile u32 *)(GPIO_REGS_VIRT + (x)))

#define gpio_to_bank(gpio)	((gpio) >> 5)

/* NOTE: these macros are defined here to make optimization of
 * gpio_{get,set}_value() to work when 'gpio' is a constant.
 * Usage of these macros otherwise is no longer recommended,
 * use generic GPIO API whenever possible.
 */
#define GPIO_bit(gpio)	(1 << ((gpio) & 0x1f))

#define GPLR(x)		GPIO_REG(BANK_OFF(gpio_to_bank(x)) + 0x00)
#define GPDR(x)		GPIO_REG(BANK_OFF(gpio_to_bank(x)) + 0x0c)
#define GPSR(x)		GPIO_REG(BANK_OFF(gpio_to_bank(x)) + 0x18)
#define GPCR(x)		GPIO_REG(BANK_OFF(gpio_to_bank(x)) + 0x24)

#include <plat/gpio-pxa.h>

#endif /* __ASM_MACH_GPIO_PXA_H */
