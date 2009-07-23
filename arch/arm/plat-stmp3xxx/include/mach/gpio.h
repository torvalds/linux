/*
 * Freescale STMP37XX/STMP378X GPIO interface
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __ASM_PLAT_GPIO_H
#define __ASM_PLAT_GPIO_H

#define ARCH_NR_GPIOS	(32 * 3)
#define gpio_to_irq(gpio) __gpio_to_irq(gpio)
#define gpio_get_value(gpio) __gpio_get_value(gpio)
#define gpio_set_value(gpio, value) __gpio_set_value(gpio, value)

#include <asm-generic/gpio.h>

#endif /* __ASM_PLAT_GPIO_H */
