/* Unit-specific leds
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_UNIT_LEDS_H
#define _ASM_UNIT_LEDS_H

#include <asm/pio-regs.h>
#include <asm/cpu-regs.h>
#include <asm/exceptions.h>

#define MN10300_USE_7SEGLEDS	0

#define ASB2364_7SEGLEDS	__SYSREG(0xA9001630, u32)

/*
 * use the 7-segment LEDs to indicate states
 */

#if MN10300_USE_7SEGLEDS
/* flip the 7-segment LEDs between "Gdb-" and "----" */
#define mn10300_set_gdbleds(ONOFF)					\
	do {								\
		ASB2364_7SEGLEDS = (ONOFF) ? 0x8543077f : 0x7f7f7f7f;	\
	} while (0)
#else
#define mn10300_set_gdbleds(ONOFF) do {} while (0)
#endif

#if MN10300_USE_7SEGLEDS
/* indicate double-fault by displaying "db-f" on the LEDs */
#define mn10300_set_dbfleds			\
	mov	0x43077f1d,d0		;	\
	mov	d0,(ASB2364_7SEGLEDS)
#else
#define mn10300_set_dbfleds
#endif

#ifndef __ASSEMBLY__
extern void peripheral_leds_display_exception(enum exception_code);
extern void peripheral_leds_led_chase(void);
extern void peripheral_leds7x4_display_dec(unsigned int, unsigned int);
extern void peripheral_leds7x4_display_hex(unsigned int, unsigned int);
extern void debug_to_serial(const char *, int);
#endif /* __ASSEMBLY__ */

#endif /* _ASM_UNIT_LEDS_H */
