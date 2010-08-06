#ifndef __ASM_MACH_GPIO_H
#define __ASM_MACH_GPIO_H

#include <mach/addr-map.h>
#include <mach/irqs.h>
#include <asm-generic/gpio.h>

#define GPIO_REGS_VIRT	(APB_VIRT_BASE + 0x19000)

#define BANK_OFF(n)	(((n) < 3) ? (n) << 2 : 0x100 + (((n) - 3) << 2))
#define GPIO_REG(x)	(*((volatile u32 *)(GPIO_REGS_VIRT + (x))))

#define NR_BUILTIN_GPIO	(192)

#define gpio_to_bank(gpio)	((gpio) >> 5)
#define gpio_to_irq(gpio)	(IRQ_GPIO_START + (gpio))
#define irq_to_gpio(irq)	((irq) - IRQ_GPIO_START)


#define __gpio_is_inverted(gpio)	(0)
#define __gpio_is_occupied(gpio)	(0)

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

#include <plat/gpio.h>
#endif /* __ASM_MACH_GPIO_H */
