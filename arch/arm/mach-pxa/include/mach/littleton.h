#ifndef __ASM_ARCH_LITTLETON_H
#define __ASM_ARCH_LITTLETON_H

#include <mach/gpio.h>

#define LITTLETON_ETH_PHYS	0x30000000

#define LITTLETON_GPIO_LCD_CS	(17)

#define EXT0_GPIO_BASE	(NR_BUILTIN_GPIO)
#define EXT0_GPIO(x)	(EXT0_GPIO_BASE + (x))

#endif /* __ASM_ARCH_LITTLETON_H */
