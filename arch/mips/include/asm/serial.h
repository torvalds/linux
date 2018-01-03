/*
 * Copyright (C) 2017 MIPS Tech, LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef __ASM__SERIAL_H
#define __ASM__SERIAL_H

#ifdef CONFIG_MIPS_GENERIC
/*
 * Generic kernels cannot know a correct value for all platforms at
 * compile time. Set it to 0 to prevent 8250_early using it
 */
#define BASE_BAUD 0
#else
#include <asm-generic/serial.h>
#endif

#endif /* __ASM__SERIAL_H */
