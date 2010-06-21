/*
 * linux/arch/arm/plat-samsung/include/plat/keypad-core.h
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * Samsung keypad controller core function
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __ASM_ARCH_KEYPAD_CORE_H
#define __ASM_ARCH_KEYPAD_CORE_H

/* These function are only for use with the core support code, such as
 * the cpu specific initialisation code
 */

/* re-define device name depending on support. */
static inline void samsung_keypad_setname(char *name)
{
#ifdef CONFIG_SAMSUNG_DEV_KEYPAD
	samsung_device_keypad.name = name;
#endif
}

#endif /* __ASM_ARCH_KEYPAD_CORE_H */
