/*
 * arch/arm/mach-at91/include/mach/gpio.h
 *
 *  Copyright (C) 2005 HP Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_ARCH_AT91RM9200_GPIO_H
#define __ASM_ARCH_AT91RM9200_GPIO_H

#include <linux/kernel.h>
#include <asm/irq.h>

#define MAX_GPIO_BANKS		5
#define NR_BUILTIN_GPIO		(MAX_GPIO_BANKS * 32)

/* these pin numbers double as IRQ numbers, like AT91xxx_ID_* values */

#define	AT91_PIN_PA0	(0x00 + 0)
#define	AT91_PIN_PA1	(0x00 + 1)
#define	AT91_PIN_PA2	(0x00 + 2)
#define	AT91_PIN_PA3	(0x00 + 3)
#define	AT91_PIN_PA4	(0x00 + 4)
#define	AT91_PIN_PA5	(0x00 + 5)
#define	AT91_PIN_PA6	(0x00 + 6)
#define	AT91_PIN_PA7	(0x00 + 7)
#define	AT91_PIN_PA8	(0x00 + 8)
#define	AT91_PIN_PA9	(0x00 + 9)
#define	AT91_PIN_PA10	(0x00 + 10)
#define	AT91_PIN_PA11	(0x00 + 11)
#define	AT91_PIN_PA12	(0x00 + 12)
#define	AT91_PIN_PA13	(0x00 + 13)
#define	AT91_PIN_PA14	(0x00 + 14)
#define	AT91_PIN_PA15	(0x00 + 15)
#define	AT91_PIN_PA16	(0x00 + 16)
#define	AT91_PIN_PA17	(0x00 + 17)
#define	AT91_PIN_PA18	(0x00 + 18)
#define	AT91_PIN_PA19	(0x00 + 19)
#define	AT91_PIN_PA20	(0x00 + 20)
#define	AT91_PIN_PA21	(0x00 + 21)
#define	AT91_PIN_PA22	(0x00 + 22)
#define	AT91_PIN_PA23	(0x00 + 23)
#define	AT91_PIN_PA24	(0x00 + 24)
#define	AT91_PIN_PA25	(0x00 + 25)
#define	AT91_PIN_PA26	(0x00 + 26)
#define	AT91_PIN_PA27	(0x00 + 27)
#define	AT91_PIN_PA28	(0x00 + 28)
#define	AT91_PIN_PA29	(0x00 + 29)
#define	AT91_PIN_PA30	(0x00 + 30)
#define	AT91_PIN_PA31	(0x00 + 31)

#define	AT91_PIN_PB0	(0x20 + 0)
#define	AT91_PIN_PB1	(0x20 + 1)
#define	AT91_PIN_PB2	(0x20 + 2)
#define	AT91_PIN_PB3	(0x20 + 3)
#define	AT91_PIN_PB4	(0x20 + 4)
#define	AT91_PIN_PB5	(0x20 + 5)
#define	AT91_PIN_PB6	(0x20 + 6)
#define	AT91_PIN_PB7	(0x20 + 7)
#define	AT91_PIN_PB8	(0x20 + 8)
#define	AT91_PIN_PB9	(0x20 + 9)
#define	AT91_PIN_PB10	(0x20 + 10)
#define	AT91_PIN_PB11	(0x20 + 11)
#define	AT91_PIN_PB12	(0x20 + 12)
#define	AT91_PIN_PB13	(0x20 + 13)
#define	AT91_PIN_PB14	(0x20 + 14)
#define	AT91_PIN_PB15	(0x20 + 15)
#define	AT91_PIN_PB16	(0x20 + 16)
#define	AT91_PIN_PB17	(0x20 + 17)
#define	AT91_PIN_PB18	(0x20 + 18)
#define	AT91_PIN_PB19	(0x20 + 19)
#define	AT91_PIN_PB20	(0x20 + 20)
#define	AT91_PIN_PB21	(0x20 + 21)
#define	AT91_PIN_PB22	(0x20 + 22)
#define	AT91_PIN_PB23	(0x20 + 23)
#define	AT91_PIN_PB24	(0x20 + 24)
#define	AT91_PIN_PB25	(0x20 + 25)
#define	AT91_PIN_PB26	(0x20 + 26)
#define	AT91_PIN_PB27	(0x20 + 27)
#define	AT91_PIN_PB28	(0x20 + 28)
#define	AT91_PIN_PB29	(0x20 + 29)
#define	AT91_PIN_PB30	(0x20 + 30)
#define	AT91_PIN_PB31	(0x20 + 31)

#define	AT91_PIN_PC0	(0x40 + 0)
#define	AT91_PIN_PC1	(0x40 + 1)
#define	AT91_PIN_PC2	(0x40 + 2)
#define	AT91_PIN_PC3	(0x40 + 3)
#define	AT91_PIN_PC4	(0x40 + 4)
#define	AT91_PIN_PC5	(0x40 + 5)
#define	AT91_PIN_PC6	(0x40 + 6)
#define	AT91_PIN_PC7	(0x40 + 7)
#define	AT91_PIN_PC8	(0x40 + 8)
#define	AT91_PIN_PC9	(0x40 + 9)
#define	AT91_PIN_PC10	(0x40 + 10)
#define	AT91_PIN_PC11	(0x40 + 11)
#define	AT91_PIN_PC12	(0x40 + 12)
#define	AT91_PIN_PC13	(0x40 + 13)
#define	AT91_PIN_PC14	(0x40 + 14)
#define	AT91_PIN_PC15	(0x40 + 15)
#define	AT91_PIN_PC16	(0x40 + 16)
#define	AT91_PIN_PC17	(0x40 + 17)
#define	AT91_PIN_PC18	(0x40 + 18)
#define	AT91_PIN_PC19	(0x40 + 19)
#define	AT91_PIN_PC20	(0x40 + 20)
#define	AT91_PIN_PC21	(0x40 + 21)
#define	AT91_PIN_PC22	(0x40 + 22)
#define	AT91_PIN_PC23	(0x40 + 23)
#define	AT91_PIN_PC24	(0x40 + 24)
#define	AT91_PIN_PC25	(0x40 + 25)
#define	AT91_PIN_PC26	(0x40 + 26)
#define	AT91_PIN_PC27	(0x40 + 27)
#define	AT91_PIN_PC28	(0x40 + 28)
#define	AT91_PIN_PC29	(0x40 + 29)
#define	AT91_PIN_PC30	(0x40 + 30)
#define	AT91_PIN_PC31	(0x40 + 31)

#define	AT91_PIN_PD0	(0x60 + 0)
#define	AT91_PIN_PD1	(0x60 + 1)
#define	AT91_PIN_PD2	(0x60 + 2)
#define	AT91_PIN_PD3	(0x60 + 3)
#define	AT91_PIN_PD4	(0x60 + 4)
#define	AT91_PIN_PD5	(0x60 + 5)
#define	AT91_PIN_PD6	(0x60 + 6)
#define	AT91_PIN_PD7	(0x60 + 7)
#define	AT91_PIN_PD8	(0x60 + 8)
#define	AT91_PIN_PD9	(0x60 + 9)
#define	AT91_PIN_PD10	(0x60 + 10)
#define	AT91_PIN_PD11	(0x60 + 11)
#define	AT91_PIN_PD12	(0x60 + 12)
#define	AT91_PIN_PD13	(0x60 + 13)
#define	AT91_PIN_PD14	(0x60 + 14)
#define	AT91_PIN_PD15	(0x60 + 15)
#define	AT91_PIN_PD16	(0x60 + 16)
#define	AT91_PIN_PD17	(0x60 + 17)
#define	AT91_PIN_PD18	(0x60 + 18)
#define	AT91_PIN_PD19	(0x60 + 19)
#define	AT91_PIN_PD20	(0x60 + 20)
#define	AT91_PIN_PD21	(0x60 + 21)
#define	AT91_PIN_PD22	(0x60 + 22)
#define	AT91_PIN_PD23	(0x60 + 23)
#define	AT91_PIN_PD24	(0x60 + 24)
#define	AT91_PIN_PD25	(0x60 + 25)
#define	AT91_PIN_PD26	(0x60 + 26)
#define	AT91_PIN_PD27	(0x60 + 27)
#define	AT91_PIN_PD28	(0x60 + 28)
#define	AT91_PIN_PD29	(0x60 + 29)
#define	AT91_PIN_PD30	(0x60 + 30)
#define	AT91_PIN_PD31	(0x60 + 31)

#define	AT91_PIN_PE0	(0x80 + 0)
#define	AT91_PIN_PE1	(0x80 + 1)
#define	AT91_PIN_PE2	(0x80 + 2)
#define	AT91_PIN_PE3	(0x80 + 3)
#define	AT91_PIN_PE4	(0x80 + 4)
#define	AT91_PIN_PE5	(0x80 + 5)
#define	AT91_PIN_PE6	(0x80 + 6)
#define	AT91_PIN_PE7	(0x80 + 7)
#define	AT91_PIN_PE8	(0x80 + 8)
#define	AT91_PIN_PE9	(0x80 + 9)
#define	AT91_PIN_PE10	(0x80 + 10)
#define	AT91_PIN_PE11	(0x80 + 11)
#define	AT91_PIN_PE12	(0x80 + 12)
#define	AT91_PIN_PE13	(0x80 + 13)
#define	AT91_PIN_PE14	(0x80 + 14)
#define	AT91_PIN_PE15	(0x80 + 15)
#define	AT91_PIN_PE16	(0x80 + 16)
#define	AT91_PIN_PE17	(0x80 + 17)
#define	AT91_PIN_PE18	(0x80 + 18)
#define	AT91_PIN_PE19	(0x80 + 19)
#define	AT91_PIN_PE20	(0x80 + 20)
#define	AT91_PIN_PE21	(0x80 + 21)
#define	AT91_PIN_PE22	(0x80 + 22)
#define	AT91_PIN_PE23	(0x80 + 23)
#define	AT91_PIN_PE24	(0x80 + 24)
#define	AT91_PIN_PE25	(0x80 + 25)
#define	AT91_PIN_PE26	(0x80 + 26)
#define	AT91_PIN_PE27	(0x80 + 27)
#define	AT91_PIN_PE28	(0x80 + 28)
#define	AT91_PIN_PE29	(0x80 + 29)
#define	AT91_PIN_PE30	(0x80 + 30)
#define	AT91_PIN_PE31	(0x80 + 31)

#ifndef __ASSEMBLY__
/* setup setup routines, called from board init or driver probe() */
extern int __init_or_module at91_set_GPIO_periph(unsigned pin, int use_pullup);
extern int __init_or_module at91_set_A_periph(unsigned pin, int use_pullup);
extern int __init_or_module at91_set_B_periph(unsigned pin, int use_pullup);
extern int __init_or_module at91_set_C_periph(unsigned pin, int use_pullup);
extern int __init_or_module at91_set_D_periph(unsigned pin, int use_pullup);
extern int __init_or_module at91_set_gpio_input(unsigned pin, int use_pullup);
extern int __init_or_module at91_set_gpio_output(unsigned pin, int value);
extern int __init_or_module at91_set_deglitch(unsigned pin, int is_on);
extern int __init_or_module at91_set_debounce(unsigned pin, int is_on, int div);
extern int __init_or_module at91_set_multi_drive(unsigned pin, int is_on);
extern int __init_or_module at91_set_pulldown(unsigned pin, int is_on);
extern int __init_or_module at91_disable_schmitt_trig(unsigned pin);

/* callable at any time */
extern int at91_set_gpio_value(unsigned pin, int value);
extern int at91_get_gpio_value(unsigned pin);

/* callable only from core power-management code */
extern void at91_gpio_suspend(void);
extern void at91_gpio_resume(void);

#endif	/* __ASSEMBLY__ */

#endif
