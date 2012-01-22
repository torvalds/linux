/*
 * Alchemy GPIO support.
 *
 * With CONFIG_GPIOLIB=y different types of on-chip GPIO can be supported within
 *  the same kernel image.
 * With CONFIG_GPIOLIB=n, your board must select ALCHEMY_GPIOINT_AU1XXX for the
 *  appropriate CPU type (AU1000 currently).
 */

#ifndef _ALCHEMY_GPIO_H_
#define _ALCHEMY_GPIO_H_

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/gpio-au1000.h>
#include <asm/mach-au1x00/gpio-au1300.h>

/* On Au1000, Au1500 and Au1100 GPIOs won't work as inputs before
 * SYS_PININPUTEN is written to at least once.  On Au1550/Au1200/Au1300 this
 * register enables use of GPIOs as wake source.
 */
static inline void alchemy_gpio1_input_enable(void)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(AU1000_SYS_PHYS_ADDR);
	__raw_writel(0, base + 0x110);		/* the write op is key */
	wmb();
}


/* Linux gpio framework integration.
*
* 4 use cases of Alchemy GPIOS:
*(1) GPIOLIB=y, ALCHEMY_GPIO_INDIRECT=y:
*	Board must register gpiochips.
*(2) GPIOLIB=y, ALCHEMY_GPIO_INDIRECT=n:
*	A gpiochip for the 75 GPIOs is registered.
*
*(3) GPIOLIB=n, ALCHEMY_GPIO_INDIRECT=y:
*	the boards' gpio.h must provide	the linux gpio wrapper functions,
*
*(4) GPIOLIB=n, ALCHEMY_GPIO_INDIRECT=n:
*	inlinable gpio functions are provided which enable access to the
*	Au1300 gpios only by using the numbers straight out of the data-
*	sheets.

* Cases 1 and 3 are intended for boards which want to provide their own
* GPIO namespace and -operations (i.e. for example you have 8 GPIOs
* which are in part provided by spare Au1300 GPIO pins and in part by
* an external FPGA but you still want them to be accssible in linux
* as gpio0-7. The board can of course use the alchemy_gpioX_* functions
* as required).
*/

#ifdef CONFIG_GPIOLIB

/* wraps the cpu-dependent irq_to_gpio functions */
/* FIXME: gpiolib needs an irq_to_gpio hook */
static inline int __au_irq_to_gpio(unsigned int irq)
{
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1000...ALCHEMY_CPU_AU1200:
		return alchemy_irq_to_gpio(irq);
	case ALCHEMY_CPU_AU1300:
		return au1300_irq_to_gpio(irq);
	}
	return -EINVAL;
}


/* using gpiolib to provide up to 2 gpio_chips for on-chip gpios */
#ifndef CONFIG_ALCHEMY_GPIO_INDIRECT	/* case (2) */

/* get everything through gpiolib */
#define gpio_to_irq	__gpio_to_irq
#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep
#define irq_to_gpio	__au_irq_to_gpio

#include <asm-generic/gpio.h>

#endif	/* !CONFIG_ALCHEMY_GPIO_INDIRECT */


#endif	/* CONFIG_GPIOLIB */

#endif	/* _ALCHEMY_GPIO_H_ */
