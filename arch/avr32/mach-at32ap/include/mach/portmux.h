/*
 * AT32 portmux interface.
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARCH_PORTMUX_H__
#define __ASM_ARCH_PORTMUX_H__

/*
 * Set up pin multiplexing, called from board init only.
 *
 * The following flags determine the initial state of the pin.
 */
#define AT32_GPIOF_PULLUP	0x00000001	/* (not-OUT) Enable pull-up */
#define AT32_GPIOF_OUTPUT	0x00000002	/* (OUT) Enable output driver */
#define AT32_GPIOF_HIGH		0x00000004	/* (OUT) Set output high */
#define AT32_GPIOF_DEGLITCH	0x00000008	/* (IN) Filter glitches */
#define AT32_GPIOF_MULTIDRV	0x00000010	/* Enable multidriver option */

void at32_select_periph(unsigned int port, unsigned int pin,
			unsigned int periph, unsigned long flags);
void at32_select_gpio(unsigned int pin, unsigned long flags);
void at32_deselect_pin(unsigned int pin);
void at32_reserve_pin(unsigned int port, u32 pin_mask);

#endif /* __ASM_ARCH_PORTMUX_H__ */
