/* linux/arch/arm/mach-s5p6440/include/mach/gpio.h
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P6440 - GPIO lib support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_GPIO_H
#define __ASM_ARCH_GPIO_H __FILE__

#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep
#define gpio_to_irq	__gpio_to_irq

/* GPIO bank sizes */
#define S5P6440_GPIO_A_NR	(6)
#define S5P6440_GPIO_B_NR	(7)
#define S5P6440_GPIO_C_NR	(8)
#define S5P6440_GPIO_F_NR	(2)
#define S5P6440_GPIO_G_NR	(7)
#define S5P6440_GPIO_H_NR	(10)
#define S5P6440_GPIO_I_NR	(16)
#define S5P6440_GPIO_J_NR	(12)
#define S5P6440_GPIO_N_NR	(16)
#define S5P6440_GPIO_P_NR	(8)
#define S5P6440_GPIO_R_NR	(15)

/* GPIO bank numbers */

/* CONFIG_S3C_GPIO_SPACE allows the user to select extra
 * space for debugging purposes so that any accidental
 * change from one gpio bank to another can be caught.
*/
#define S5P6440_GPIO_NEXT(__gpio) \
	((__gpio##_START) + (__gpio##_NR) + CONFIG_S3C_GPIO_SPACE + 1)

enum s5p_gpio_number {
	S5P6440_GPIO_A_START = 0,
	S5P6440_GPIO_B_START = S5P6440_GPIO_NEXT(S5P6440_GPIO_A),
	S5P6440_GPIO_C_START = S5P6440_GPIO_NEXT(S5P6440_GPIO_B),
	S5P6440_GPIO_F_START = S5P6440_GPIO_NEXT(S5P6440_GPIO_C),
	S5P6440_GPIO_G_START = S5P6440_GPIO_NEXT(S5P6440_GPIO_F),
	S5P6440_GPIO_H_START = S5P6440_GPIO_NEXT(S5P6440_GPIO_G),
	S5P6440_GPIO_I_START = S5P6440_GPIO_NEXT(S5P6440_GPIO_H),
	S5P6440_GPIO_J_START = S5P6440_GPIO_NEXT(S5P6440_GPIO_I),
	S5P6440_GPIO_N_START = S5P6440_GPIO_NEXT(S5P6440_GPIO_J),
	S5P6440_GPIO_P_START = S5P6440_GPIO_NEXT(S5P6440_GPIO_N),
	S5P6440_GPIO_R_START = S5P6440_GPIO_NEXT(S5P6440_GPIO_P),
};

/* S5P6440 GPIO number definitions. */
#define S5P6440_GPA(_nr)	(S5P6440_GPIO_A_START + (_nr))
#define S5P6440_GPB(_nr)	(S5P6440_GPIO_B_START + (_nr))
#define S5P6440_GPC(_nr)	(S5P6440_GPIO_C_START + (_nr))
#define S5P6440_GPF(_nr)	(S5P6440_GPIO_F_START + (_nr))
#define S5P6440_GPG(_nr)	(S5P6440_GPIO_G_START + (_nr))
#define S5P6440_GPH(_nr)	(S5P6440_GPIO_H_START + (_nr))
#define S5P6440_GPI(_nr)	(S5P6440_GPIO_I_START + (_nr))
#define S5P6440_GPJ(_nr)	(S5P6440_GPIO_J_START + (_nr))
#define S5P6440_GPN(_nr)	(S5P6440_GPIO_N_START + (_nr))
#define S5P6440_GPP(_nr)	(S5P6440_GPIO_P_START + (_nr))
#define S5P6440_GPR(_nr)	(S5P6440_GPIO_R_START + (_nr))

/* the end of the S5P6440 specific gpios */
#define S5P6440_GPIO_END	(S5P6440_GPR(S5P6440_GPIO_R_NR) + 1)
#define S3C_GPIO_END		S5P6440_GPIO_END

/* define the number of gpios we need to the one after the GPR() range */
#define ARCH_NR_GPIOS		(S5P6440_GPR(S5P6440_GPIO_R_NR) +	\
				 CONFIG_SAMSUNG_GPIO_EXTRA + 1)

#include <asm-generic/gpio.h>

#endif /* __ASM_ARCH_GPIO_H */
